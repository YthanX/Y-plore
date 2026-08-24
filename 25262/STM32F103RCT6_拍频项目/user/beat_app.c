/**
  ******************************************************************************
  * @file    beat_app.c
  * @brief   拍频页面、菜单、串口命令和主循环任务调度
  ******************************************************************************
  */

#include "beat_app.h"

#include <stdio.h>
#include <string.h>

#include "beat_engine.h"
#include "beat_display.h"
#include "encoder.h"
#include "key.h"
#include "usart.h"

/*
 * 本文件中的 static 状态只属于应用层：
 * 其他模块不能直接改变页面、光标和编辑状态，只能通过 BeatApp API 调度。
 * s_ 前缀用于区分模块状态与函数内部的临时变量。
 */

/* LCD 扩展模式提高刷新目标；OLED 受软件 I2C 限制，保持较低刷新频率。 */
#if BEAT_DISPLAY_BACKEND == BEAT_DISPLAY_LCD
#define BEAT_UI_REFRESH_TICKS  20U
#else
#define BEAT_UI_REFRESH_TICKS  30U
#endif
/* 超过约 500ms 没有新 ADC frame 时判为数据中断。 */
#define BEAT_ADC_STALE_TICKS   500U
/* 串口运行诊断的发送周期，约 1s。 */
#define BEAT_DIAG_TICKS        1000U
/* 单周期 CSV 文本的本地格式化缓冲大小。 */
#define BEAT_DUMP_BUFFER_SIZE  256U

typedef enum
{
    /* 三路波形和频差显示页面。 */
    BEAT_VIEW_WAVE = 0,
    /* 两列参数菜单页面。 */
    BEAT_VIEW_MENU
} BeatView;

/* 当前页面状态。 */
static BeatView s_view;
/* 菜单编辑使用的草稿；KEY1 确认前不会写入 BeatEngine。 */
static BeatConfig s_draft_config;
/* 最近一次从 FIFO 取得的一致性快照，供显示和串口导出共用。 */
static BeatFrame s_frame;
/* 菜单可选择 4 项：x1 波形、x2 波形、x1 幅度、x1 相位。 */
static uint8_t s_selected_item;
/* 非零表示菜单进入编辑态。 */
static uint8_t s_editing;
/* 页面或参数发生变化后置位，要求下一轮立即重画。 */
static uint8_t s_dirty;
/* 表示 s_frame 中是否至少保存过一帧有效数据。 */
static uint8_t s_have_frame;
/* 由 BeatApp_Task() 每轮加一，作为毫秒级软件节拍。 */
static uint32_t s_task_ticks;
/* 上一次完成界面刷新时的 task tick。 */
static uint32_t s_last_refresh_tick;
/* 上一次成功启动诊断发送时的 task tick。 */
static uint32_t s_last_diag_tick;
/* 上一轮观察到的 BeatEngine frame_count。 */
static uint32_t s_last_frame_count;
/* 连续多少个任务周期没有观察到新的 ADC frame。 */
static uint16_t s_stale_ticks;
/* 收到 D/d 命令后置位，等待 USART TX DMA 空闲再发送。 */
static uint8_t s_dump_pending;

/**
 * @brief  根据编码器步数修改当前草稿参数
 * @param  steps 正数表示向后切换或增加，负数表示向前切换或减小
 * @note   这里只修改 draft 参数，按 KEY1 确认后才写入 BeatEngine。
 */
static void BeatApp_ChangeConfig(int16_t steps)
{
    /* 一个 steps 可能包含多个机械档位，因此逐步执行，避免跳过循环选项。 */
    while (steps > 0)
    {
        if (s_selected_item == 0U)
        {
            /* x1 波形在正弦、三角、锯齿、方波之间循环。 */
            s_draft_config.wave1 = (BeatWaveformType)((s_draft_config.wave1 + 1U) % 4U);
        }
        else if (s_selected_item == 1U)
        {
            /* x2 波形同样使用 4 种枚举循环。 */
            s_draft_config.wave2 = (BeatWaveformType)((s_draft_config.wave2 + 1U) % 4U);
        }
        else if ((s_selected_item == 2U) && (s_draft_config.amplitude_step < 11U))
        {
            /* 幅度共有 11 档，每档 0.3V，最大 3.3V。 */
            s_draft_config.amplitude_step++;
        }
        else if ((s_selected_item == 3U) && (s_draft_config.phase_step < 12U))
        {
            /* 相位共有 13 档，每档 15 度，最大 180 度。 */
            s_draft_config.phase_step++;
        }
        steps--;
    }

    /* 负方向与正方向对称处理，并在波形枚举起点处回绕。 */
    while (steps < 0)
    {
        if (s_selected_item == 0U)
        {
            s_draft_config.wave1 = s_draft_config.wave1 == BEAT_WAVE_SINE ?
                                   BEAT_WAVE_SQUARE :
                                   (BeatWaveformType)(s_draft_config.wave1 - 1U);
        }
        else if (s_selected_item == 1U)
        {
            s_draft_config.wave2 = s_draft_config.wave2 == BEAT_WAVE_SINE ?
                                   BEAT_WAVE_SQUARE :
                                   (BeatWaveformType)(s_draft_config.wave2 - 1U);
        }
        else if ((s_selected_item == 2U) && (s_draft_config.amplitude_step > 1U))
        {
            s_draft_config.amplitude_step--;
        }
        else if ((s_selected_item == 3U) && (s_draft_config.phase_step > 0U))
        {
            s_draft_config.phase_step--;
        }
        steps++;
    }
}

/**
 * @brief  把按键驱动产生的事件转换为页面和编辑状态变化
 * @param  event 已经完成消抖的 KEY0/KEY1 事件
 */
static void BeatApp_HandleKey(KeyEvent event)
{
    if (event == KEY_EVENT_MENU)
    {
        if (s_view == BEAT_VIEW_WAVE)
        {
            /* 进入菜单前复制当前生效配置，作为本次编辑的草稿起点。 */
            BeatEngine_GetConfig(&s_draft_config);
            s_view = BEAT_VIEW_MENU;
            s_editing = 0U;
        }
        else if (s_editing == 0U)
        {
            /* 浏览态允许 KEY0 返回波形页；编辑态下忽略 KEY0，防止未保存退出。 */
            s_view = BEAT_VIEW_WAVE;
        }
        /* 页面改变后要求下一轮立即重画。 */
        s_dirty = 1U;
    }
    else if ((event == KEY_EVENT_EDIT) && (s_view == BEAT_VIEW_MENU))
    {
        if (s_editing != 0U)
        {
            /* 第二次按 KEY1：保存草稿并重建相关信号数据流。 */
            BeatEngine_ApplyConfig(&s_draft_config);
            s_editing = 0U;
            /* 参数变化会清空 FIFO，因此旧显示帧也应判为无效。 */
            s_have_frame = 0U;
            s_stale_ticks = 0U;
        }
        else
        {
            /* 第一次按 KEY1：进入编辑态，编码器开始修改当前选项。 */
            s_editing = 1U;
        }
        s_dirty = 1U;
    }
    else if ((event == KEY_EVENT_EDIT) && (s_view == BEAT_VIEW_WAVE))
    {
        /* LCD 波形页复用 KEY1 作为 RUN/STOP；OLED 后端该接口为空操作。 */
        BeatDisplay_ScopeToggleRun();
        s_dirty = 1U;
    }
    else if ((event == KEY_EVENT_SCOPE) && (s_view == BEAT_VIEW_WAVE))
    {
        /* 外接 PC4 按键依次选择 DF/CH/TIME/GAIN/MODE/TRIG。 */
        BeatDisplay_ScopeNextControl();
        s_dirty = 1U;
    }
}

/**
 * @brief  把 BeatEngineStatus 格式化成一行运行诊断并交给 TX DMA
 * @param  status 当前 ADC、DMA、定时器及 FIFO 状态
 * @retval 1 已成功启动 DMA 发送
 * @retval 0 USART DMA 正忙或参数无效
 */
static uint8_t BeatApp_SendDiagnostic(const BeatEngineStatus *status)
{
    /* 176B 足够容纳当前全部字段，同时小于 USART 内部 256B DMA 缓冲。 */
    char text[176];

    /*
     * 诊断字段用于分层定位问题：
     * cc2/dma/frame 判断触发和 DMA 链路，min/max 判断 ADC 是否采到变化信号。
     */
    (void)snprintf(text, sizeof(text),
                   "x1=%s clk=%lu fs=%lu t6=%u t2=%u cc2=%u dma=%u self=%u adc=%u "
                   "frame=%lu fifo=%u min=%u max=%u peak=%u\r\n",
                   BEAT_X1_MODE_NAME,
                   (unsigned long)status->timer_clock_hz,
                   (unsigned long)status->sample_rate_hz,
                   (unsigned int)status->tim6_arr,
                   (unsigned int)status->tim2_arr,
                   (unsigned int)status->tim2_cc2_seen,
                   (unsigned int)status->adc_dma_remaining,
                   (unsigned int)status->adc_selftest_ok,
                   (unsigned int)status->adc_selftest_value,
                   (unsigned long)status->frame_count,
                   (unsigned int)status->fifo_count,
                   (unsigned int)status->adc_min,
                   (unsigned int)status->adc_max,
                   (unsigned int)status->sum_peak);
    return USART1_SendDma((const uint8_t *)text, (uint16_t)strlen(text));
}

/**
 * @brief  读取 USART RX 环形缓冲，并识别 D/d 导出命令
 * @note   串口中断只收字节，真正的数据导出留在主循环中执行。
 */
static void BeatApp_HandleSerialCommands(void)
{
    /* byte 只保存当前从 RX 环形缓冲取出的一个字节。 */
    uint8_t byte;

    /* 一次任务循环把当前已收到的字节全部取出。 */
    while (USART1_ReadByte(&byte) != 0U)
    {
        if ((byte == 'D') || (byte == 'd'))
        {
            /* 只保留一次待发送请求，连续输入多个 D 不会堆积大量任务。 */
            s_dump_pending = 1U;
        }
    }
}

/**
 * @brief  导出最新一个 x1 周期的 ADC 原始码值
 * @note   输出内容是 CSV 格式文本，文件由电脑端串口工具保存。
 */
static void BeatApp_SendPendingDump(void)
{
    /* config/status 是本次导出使用的参数和运行状态局部快照。 */
    BeatConfig config;
    BeatEngineStatus status;
    /* text 保存即将交给 USART TX DMA 的完整 CSV 文本。 */
    char text[BEAT_DUMP_BUFFER_SIZE];
    /* frequency 为 x1 频率，period_samples 为一周期采样点数。 */
    uint16_t frequency;
    uint16_t period_samples;
    /* start 是帧内截取起点，i 是 CSV 数据循环索引。 */
    uint16_t start;
    uint16_t i;
    /* snprintf 返回 int，负值表示格式化失败。 */
    int length;

    /* 没有请求或 TX DMA 正忙时暂不处理，下一轮任务会继续尝试。 */
    if ((s_dump_pending == 0U) || (USART1_TxDmaBusy() != 0U))
    {
        return;
    }

    /* CSV 导出和屏幕共用一致性快照，避免读到 DMA 正在改写的数据。 */
    if (BeatEngine_CopyLatestFrame(&s_frame) == 0U)
    {
        static const char error_text[] = "ERR,NO_ADC\r\n";

        /* ADC 尚无完整帧时返回错误文本，并在成功启动发送后清除请求。 */
        if (USART1_SendDma((const uint8_t *)error_text,
                           (uint16_t)(sizeof(error_text) - 1U)) != 0U)
        {
            s_dump_pending = 0U;
        }
        return;
    }

    BeatEngine_GetConfig(&config);
    BeatEngine_GetStatus(&status);
    /* x1 当前频率为 440+df。 */
    frequency = (uint16_t)(440 + config.delta_hz);
    /* 四舍五入计算一个周期包含的 ADC 样本数。 */
    period_samples = (uint16_t)((status.sample_rate_hz + frequency / 2U) / frequency);
    if (period_samples > s_frame.sample_count)
    {
        /* 防止请求点数超过当前快照长度。 */
        period_samples = s_frame.sample_count;
    }
    /* 从帧尾向前截取最新一个周期，而不是导出较旧的数据。 */
    start = (uint16_t)(s_frame.sample_count - period_samples);

    /* CSV 首行写出采样率 FS、信号频率 F 和点数 N。 */
    length = snprintf(text, sizeof(text), "ADC_X1,FS=%u,F=%u,N=%u\r\n",
                      (unsigned int)status.sample_rate_hz,
                      (unsigned int)frequency,
                      (unsigned int)period_samples);
    for (i = 0U; (i < period_samples) &&
                 (length > 0) && ((uint16_t)length < sizeof(text)); i++)
    {
        /*
         * written 是本次 snprintf 实际需要写入的字符数；
         * 最后一个数值使用换行，其余数值使用逗号分隔。
         */
        int written = snprintf(&text[length], sizeof(text) - (uint16_t)length,
                               (i + 1U < period_samples) ? "%u," : "%u\r\n",
                               (unsigned int)s_frame.x1[start + i]);
        if ((written < 0) || ((uint16_t)written >= sizeof(text) - (uint16_t)length))
        {
            /* snprintf 失败或缓冲空间不足时放弃本次发送。 */
            length = 0;
            break;
        }
        length += written;
    }

    /* DMA 成功接收数据后才清除 pending，失败则保留到下一轮重试。 */
    if ((length > 0) &&
        (USART1_SendDma((const uint8_t *)text, (uint16_t)length) != 0U))
    {
        s_dump_pending = 0U;
    }
}

/**
 * @brief  初始化应用所需的输入、显示和实时信号模块
 */
void BeatApp_Init(void)
{
    /*
     * Key_Init() 会关闭 JTAG、保留 SWD，必须在使用 PA15/PB3/PB4 等资源前执行。
     */
    Key_Init();
    /* 当前宏只会初始化 OLED 或 LCD 中的一种显示后端。 */
    if (BeatDisplay_Init() == 0U)
    {
        USART1_SendString("Display init failed.\r\n");
    }
    /* TIM3 Encoder mode 初始化。 */
    Encoder_Init();
    /* 最后启动 DAC、ADC、DMA 和定时器实时数据流。 */
    BeatEngine_Init();
    /* 保存当前配置，供之后进入菜单时显示。 */
    BeatEngine_GetConfig(&s_draft_config);

    /* 设置应用层初始状态：波形页、未编辑、等待第一帧 ADC 数据。 */
    s_view = BEAT_VIEW_WAVE;
    s_selected_item = 0U;
    s_editing = 0U;
    s_dirty = 1U;
    s_have_frame = 0U;
    s_task_ticks = 0U;
    s_last_refresh_tick = 0U;
    s_last_diag_tick = 0U;
    s_last_frame_count = 0U;
    s_stale_ticks = 0U;
    s_dump_pending = 0U;
}

/**
 * @brief  执行一次应用层调度
 * @note   由 main 每约 1ms 调用一次，函数内部不使用长时间阻塞。
 */
void BeatApp_Task(void)
{
    /* 第1步：status 保存本轮读取到的 ADC、DMA、FIFO 等运行状态。 */
    BeatEngineStatus status;
    /* config 保存本轮显示波形页时使用的当前生效参数。 */
    BeatConfig config;
    /* event 保存 KEY0 或 KEY1 经过消抖后产生的一个按键事件。 */
    KeyEvent event;
    /* steps 保存编码器从上次任务到现在累计转过的完整机械档位数。 */
    int16_t steps;

    /* 第2步：任务每约 1ms 执行一次，因此加一可作为软件毫秒时基。 */
    s_task_ticks++;

    /* 第3步：读取 USART RX 缓冲；发现 D/d 时只设置待导出标志。 */
    BeatApp_HandleSerialCommands();

    /* 第4步：取得一个已经处理好消抖和长按锁存的按键事件。 */
    event = Key_TakeEvent();

    /* 第5步：取得编码器增量；0 表示没转，正负号表示两个方向。 */
    steps = Encoder_GetDelta();

    /*
     * 第6步：先处理按键。
     * 这样同一轮既按键又转编码器时，会按切换后的页面/编辑状态解释旋转。
     */
    BeatApp_HandleKey(event);

    /* 第7步：只有编码器确实转动时，才进入参数或光标处理。 */
    if (steps != 0)
    {
        /* 第7.1步：当前在波形页，编码器用于直接调整频差 df。 */
        if (s_view == BEAT_VIEW_WAVE)
        {
            /* 这里的 config 只在调频分支中临时保存当前配置。 */
            BeatConfig config;
            /* delta 先用 int16_t 计算，避免加减后立刻发生 int8_t 溢出。 */
            int16_t delta;

            /* 示波器控制项不是 DF 时，编码器由 LCD 后端直接使用。 */
            if (BeatDisplay_ScopeAdjust(steps) == 0U)
            {
                /* 第7.1.1步：先读取当前生效的 df。 */
                BeatEngine_GetConfig(&config);
                /* 第7.1.2步：每个机械档位对应 1Hz，把步数加到当前 df。 */
                delta = (int16_t)config.delta_hz + steps;
                /* 第7.1.3步：低于 -30Hz 时固定在下限，不允许继续减小。 */
                if (delta < -30)
                {
                    delta = -30;
                }
                /* 第7.1.4步：高于 +30Hz 时固定在上限，不发生首尾回绕。 */
                if (delta > 30)
                {
                    delta = 30;
                }
                /* 第7.1.5步：把限制后的 df 交给信号引擎，更新 x1 频率。 */
                BeatEngine_SetDeltaHz((int8_t)delta);
            }
        }
        /* 第7.2步：当前在菜单编辑态，编码器用于修改当前草稿参数。 */
        else if (s_editing != 0U)
        {
            /* 这里只改 s_draft_config，KEY1 再次确认后参数才真正生效。 */
            BeatApp_ChangeConfig(steps);
        }
        /* 第7.3步：当前在菜单浏览态，编码器只移动选中框。 */
        else
        {
            /* 正方向可能一次累计多档，因此循环移动到对应的后续项目。 */
            while (steps > 0)
            {
                /* 四个项目按 0→1→2→3→0 循环。 */
                s_selected_item = (uint8_t)((s_selected_item + 1U) % 4U);
                steps--;
            }
            /* 反方向同样可能一次累计多档，因此逐档向前移动。 */
            while (steps < 0)
            {
                /* 当前为 0 时向前移动到 3，否则直接减一。 */
                s_selected_item = s_selected_item == 0U ? 3U :
                                  (uint8_t)(s_selected_item - 1U);
                steps++;
            }
        }
        /* 第7.4步：编码器改变了频差、参数或光标，要求本轮立即重画。 */
        s_dirty = 1U;
    }

    /* 第8步：一次性读取信号引擎状态，后续超时、串口和显示共同使用。 */
    BeatEngine_GetStatus(&status);

    /* 第9步：比较 ADC 半区完成次数，判断采样链路是否仍在更新。 */
    if (status.frame_count != s_last_frame_count)
    {
        /* 第9.1步：帧数变化说明刚有新的 128 点完成，保存最新帧数。 */
        s_last_frame_count = status.frame_count;
        /* 采样正常，把“多久没来新帧”的计数重新清零。 */
        s_stale_ticks = 0U;
    }
    /* 第9.2步：帧数没变就累计停顿时间，并防止 uint16_t 溢出回零。 */
    else if (s_stale_ticks < 0xFFFFU)
    {
        s_stale_ticks++;
    }

    /*
     * 第10步：尝试发送 D/d 请求的单周期 CSV。
     * TX DMA 忙时函数会保留 pending，下一轮任务继续尝试。
     */
    BeatApp_SendPendingDump();

    /* 第11步：判断距离上次诊断发送是否已经达到约 1 秒。 */
    if ((s_task_ticks - s_last_diag_tick) >= BEAT_DIAG_TICKS)
    {
        /* 第11.1步：只有成功启动 TX DMA，才记录本次发送时间。 */
        if (BeatApp_SendDiagnostic(&status) != 0U)
        {
            s_last_diag_tick = s_task_ticks;
        }
        /*
         * 如果 TX DMA 正忙，SendDiagnostic 返回 0，
         * last_diag_tick 不更新，下一轮任务会再次尝试，不会漏掉整次诊断。
         */
    }

    /*
     * 第12步：决定这一轮是否刷新屏幕。
     * dirty 表示发生了页面、参数或光标变化，需要立即刷新；
     * 波形页即使没有操作，也要每约 30ms 刷新一次新波形。
     */
    if ((s_dirty != 0U) ||
        ((s_view == BEAT_VIEW_WAVE) &&
         ((s_task_ticks - s_last_refresh_tick) >= BEAT_UI_REFRESH_TICKS)))
    {
        /* 第12.1步：如果当前是菜单页，显示正在浏览或编辑的草稿参数。 */
        if (s_view == BEAT_VIEW_MENU)
        {
            /*
             * 传入草稿、当前选中项和编辑标志。
             * 因此旋转时菜单数字可以先变化，但 KEY1 确认前不会影响信号。
             */
            BeatDisplay_ShowMenu(&s_draft_config, s_selected_item, s_editing);
        }
        /* 第12.2步：否则当前是波形页，需要判断 ADC 数据能否显示。 */
        else
        {
            /* frame_valid 要求 ADC 至少完成过一帧。 */
            /* stale_ticks 小于 500 还要求最近约 500ms 内持续有新帧。 */
            uint8_t frame_ready = (uint8_t)((status.frame_valid != 0U) &&
                                             (s_stale_ticks < BEAT_ADC_STALE_TICKS));
            /* display_frame 最终决定画真实波形，还是只画基线并显示 WAIT ADC。 */
            uint8_t display_frame;

            /* 第12.2.1步：ADC 正常时，尝试从 FIFO 复制最新一致性快照。 */
            if ((frame_ready != 0U) &&
                ((BeatDisplay_ScopeIsRunning() != 0U) || (s_have_frame == 0U)) &&
                (BeatEngine_CopyLatestFrame(&s_frame) != 0U))
            {
                /* 复制成功后，s_frame 中已经有完整的 x1、x2、sum 三路数组。 */
                s_have_frame = 1U;
            }
            /*
             * 第12.2.2步：同时满足“ADC 还活着”和“手里有完整帧”，
             * 才允许显示真实波形；否则显示层进入 WAIT ADC 状态。
             */
            display_frame = (uint8_t)((frame_ready != 0U) &&
                                      (s_have_frame != 0U));
            /* 第12.2.3步：取得当前 df 等参数，用于显示顶部状态。 */
            BeatEngine_GetConfig(&config);
            /* 第12.2.4步：把参数、状态、三路数据和有效标志一起交给显示层。 */
            BeatDisplay_ShowWave(&config, &status, &s_frame, display_frame);
        }
        /* 第12.3步：本轮画面已经更新，清除立即重画标志。 */
        s_dirty = 0U;
        /* 第12.4步：记录刷新时刻，下一次波形刷新从这里重新计时。 */
        s_last_refresh_tick = s_task_ticks;
    }
}
