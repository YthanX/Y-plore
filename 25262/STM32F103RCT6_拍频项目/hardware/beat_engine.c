/**
  ******************************************************************************
  * @file    beat_engine.c
  * @brief   拍频信号产生、ADC 回采、求和及 DAC2 输出
  ******************************************************************************
  */

#include "beat_engine.h"

#include <string.h>

#include "beat_fifo.h"
#include "misc.h"
#include "stm32f10x_adc.h"
#include "stm32f10x_dac.h"
#include "stm32f10x_dma.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

/*
 * 本文件中的声明约定：
 *
 * 1. 文件作用域的 static
 *    表示变量或函数只在 beat_engine.c 内部可见，其他模块不能直接访问，
 *    必须通过 BeatEngine_* 公共 API 读取或修改。
 *
 * 2. volatile
 *    表示变量可能在中断中变化，编译器每次使用时都必须重新读取内存，
 *    不能把旧值长期保存在寄存器中。
 *
 * 3. const
 *    表示只读常量，通常存放在 Flash，不占用可写 RAM。
 *
 * 4. s_ 前缀
 *    表示 module static，即模块内部状态，不是全工程共享的公开全局变量。
 */

/* x2 的固定基准频率，同时也是 x1 计算频差时的中心频率。 */
#define BEAT_BASE_FREQUENCY_HZ    440
/* 第一版 TABLE 模式一个完整周期包含 256 个 DAC 点。 */
#define BEAT_DAC1_TABLE_SAMPLES   256U
/* 第二版 DDS 模式希望 TIM6 提供的 DAC1 更新率。 */
#define BEAT_DAC1_SAMPLE_RATE_HZ  112500UL
/* DMA 双半缓冲中每个半区包含 128 个样本。 */
#define BEAT_DMA_HALF_SAMPLES     128U
/* 完整 DMA 循环缓冲为两个半区，共 256 点。 */
#define BEAT_DMA_SAMPLES          (BEAT_DMA_HALF_SAMPLES * 2U)
/* 用户可设置的最小/最大频差。 */
#define BEAT_MIN_DELTA_HZ        (-30)
#define BEAT_MAX_DELTA_HZ        30
/* x1 幅度档位范围：1~11。 */
#define BEAT_MIN_AMPLITUDE_STEP  1U
#define BEAT_MAX_AMPLITUDE_STEP  11U
/* x1 相位档位最大值：12*15=180 度。 */
#define BEAT_MAX_PHASE_STEP      12U
/* ADC 软件触发自检的最大轮询次数，防止硬件异常时死循环。 */
#define BEAT_ADC_SELFTEST_TIMEOUT 100000UL

/*
 * 正弦第一象限查表，共 65 点，索引 0 和 64 分别对应 0 度和 90 度。
 * static const 使该表只在本文件可见，并作为只读数据存放。
 */
static const uint8_t s_sine_quarter[65] = {
    0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46,
    49, 51, 54, 57, 60, 63, 65, 68, 71, 73, 76, 78, 81, 83, 85, 88,
    90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113,
    115, 116, 117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126,
    126, 126, 127, 127, 127, 127
};

/* DAC1 DMA 源缓冲：TABLE 模式保存整周期，DDS 模式作为双半缓冲。 */
static uint16_t s_dac1_buffer[BEAT_DMA_SAMPLES];
/* DAC2 DMA 源缓冲，保存缩放到 0~4095 的求和信号 x'。 */
static uint16_t s_dac2_buffer[BEAT_DMA_SAMPLES];
/*
 * ADC1 DMA 目标缓冲。
 * volatile 表示内容由 DMA 硬件异步改写，CPU 读取时不能假设数据不变。
 */
static volatile uint16_t s_adc_buffer[BEAT_DMA_SAMPLES];
/* 暂存一个 ADC 半区的未缩放 sum，确定峰值后再写入 DAC2 缓冲。 */
static uint16_t s_half_sum[BEAT_DMA_HALF_SAMPLES];
/* 从 FIFO 复制出的临时三元组，随后拆分到 BeatFrame 三路数组。 */
static BeatSample s_copy_scratch[BEAT_FRAME_SAMPLES];

/* 当前生效配置；static 防止其他模块绕过 API 直接改写。 */
static BeatConfig s_config = {
    BEAT_WAVE_SINE,
    BEAT_WAVE_SINE,
    11U,
    0U,
    10
};

/* 每处理完一个 128 点 ADC 半区加一，由中断写、主循环读。 */
static volatile uint32_t s_frame_sequence;
/* 最近半区的 ADC 最小值和最大值，用于运行诊断。 */
static volatile uint16_t s_adc_min;
static volatile uint16_t s_adc_max;
/* DAC2 归一化使用的保持峰值，普通帧中只增不减。 */
static volatile uint16_t s_sum_peak;
/* 至少完成过一个 ADC 半区后置 1。 */
static volatile uint8_t s_frame_valid;
/* ADC 软件触发自检结果和对应的单次转换值。 */
static volatile uint8_t s_adc_selftest_ok;
static volatile uint16_t s_adc_selftest_value;
#if BEAT_X1_MODE == BEAT_X1_MODE_DDS
/* x1 的 32 位当前相位，只在 DDS 模式编译。 */
static uint32_t s_x1_phase;
/* x1 每个 DAC 更新点增加的相位量，主循环可能修改、中断读取。 */
static volatile uint32_t s_x1_phase_increment;
#endif
/* x2 的 32 位当前相位及每个 ADC 采样点的相位增量。 */
static uint32_t s_x2_phase;
static uint32_t s_x2_phase_increment;
/* APB1 定时器实际时钟，初始化时由 RCC 配置计算得到。 */
static uint32_t s_timer_clock_hz = 72000000UL;
/* TIM2 整数分频后的实际 ADC 采样率。 */
static uint32_t s_sample_rate_hz = BEAT_SAMPLE_RATE_HZ;
#if BEAT_X1_MODE == BEAT_X1_MODE_DDS
/* DDS 模式 TIM6 的实际整数分频值，参与相位增量计算。 */
static uint32_t s_dac1_timer_divider = 640U;
#endif

/**
 * @brief  根据 8 位相位计算归一化有符号波形
 * @param  wave 波形类型
 * @param  phase 0~255 对应一个完整周期
 * @retval -127~+127 的有符号波形值
 * @note   四种波形统一成同一数值范围，后续幅度和 DAC 映射可以共用。
 */
static int16_t BeatEngine_WaveSigned(BeatWaveformType wave, uint8_t phase)
{
    /* quadrant 保存 0~3 象限编号，position 保存象限内部 0~63 的索引。 */
    uint8_t quadrant;
    uint8_t position;

    switch (wave)
    {
        case BEAT_WAVE_TRIANGLE:
            if (phase < 128U)
            {
                /* 前半周期从 -127 线性上升到 +127。 */
                return (int16_t)(-127 + (int16_t)(((uint16_t)phase * 254U) / 127U));
            }
            /* 后半周期从 +127 线性下降到 -127。 */
            return (int16_t)(127 - ((uint16_t)(phase - 128U) * 254U) / 127U);

        case BEAT_WAVE_SAW:
            /* 一个周期内单调上升，uint8_t 回绕时形成锯齿下降沿。 */
            return (int16_t)(((uint16_t)phase * 254U) / 255U) - 127;

        case BEAT_WAVE_SQUARE:
            /* 前半周期高电平，后半周期低电平。 */
            return phase < 128U ? 127 : -127;

        case BEAT_WAVE_SINE:
        default:
            /* 高 2 位表示象限，低 6 位表示象限内的位置。 */
            quadrant = phase >> 6;
            position = phase & 0x3FU;
            if (quadrant == 0U)
            {
                /* 第一象限直接查 0~63。 */
                return s_sine_quarter[position];
            }
            if (quadrant == 1U)
            {
                /* 第二象限反向查表，包含索引 64 的峰值点。 */
                return s_sine_quarter[64U - position];
            }
            if (quadrant == 2U)
            {
                /* 第三象限取第一象限的相反数。 */
                return -((int16_t)s_sine_quarter[position]);
            }
            /* 第四象限反向查表并取负。 */
            return -((int16_t)s_sine_quarter[64U - position]);
    }
}

/**
 * @brief  把有符号波形映射为 12 位 DAC 数值
 * @param  wave 波形类型
 * @param  phase 当前 8 位相位
 * @param  peak 目标峰峰值对应的 DAC 码跨度
 * @retval 以 2048 为中心的 0~4095 DAC 码值
 */
static uint16_t BeatEngine_ToDac(BeatWaveformType wave, uint8_t phase, uint16_t peak)
{
    /* 先取得统一的 -127~+127 波形，再按幅度缩放。 */
    int16_t signed_value = BeatEngine_WaveSigned(wave, phase);
    /* 除以 254 等价于把 peak 分配到正、负两个方向，中心始终保持 2048。 */
    return (uint16_t)(2048 + (((int32_t)signed_value * peak) / 254));
}

/**
 * @brief  把外部传入的配置限制在课程任务允许范围
 * @param  config 待检查并原地修正的配置
 */
static void BeatEngine_ClampConfig(BeatConfig *config)
{
    /* 非法波形枚举统一回退到正弦。 */
    if (config->wave1 > BEAT_WAVE_SQUARE)
    {
        config->wave1 = BEAT_WAVE_SINE;
    }
    if (config->wave2 > BEAT_WAVE_SQUARE)
    {
        config->wave2 = BEAT_WAVE_SINE;
    }
    if (config->amplitude_step < BEAT_MIN_AMPLITUDE_STEP)
    {
        /* 幅度最低 1 档，即 0.3V。 */
        config->amplitude_step = BEAT_MIN_AMPLITUDE_STEP;
    }
    if (config->amplitude_step > BEAT_MAX_AMPLITUDE_STEP)
    {
        config->amplitude_step = BEAT_MAX_AMPLITUDE_STEP;
    }
    if (config->phase_step > BEAT_MAX_PHASE_STEP)
    {
        /* 相位最高 12 档，即 180 度。 */
        config->phase_step = BEAT_MAX_PHASE_STEP;
    }
    if (config->delta_hz < BEAT_MIN_DELTA_HZ)
    {
        /* 频差不回绕，直接钳位在 -30Hz。 */
        config->delta_hz = BEAT_MIN_DELTA_HZ;
    }
    if (config->delta_hz > BEAT_MAX_DELTA_HZ)
    {
        config->delta_hz = BEAT_MAX_DELTA_HZ;
    }
}

#if BEAT_X1_MODE == BEAT_X1_MODE_TABLE
/**
 * @brief  第一版 TABLE 模式：重建一个完整的 256 点 x1 周期
 */
static void BeatEngine_RebuildDac1Table(void)
{
    /* i 为波表索引，phase_offset 为相位对应的点数偏移。 */
    uint16_t i;
    uint16_t phase_offset;
    /* 1~11 档线性映射到 12 位 DAC 的峰峰码跨度。 */
    uint16_t peak = (uint16_t)(((uint32_t)s_config.amplitude_step * 4095U) /
                               BEAT_MAX_AMPLITUDE_STEP);

    /* phase_step 每档 15 度，并换算为 256 点周期中的索引偏移。 */
    phase_offset = (uint16_t)(((uint32_t)s_config.phase_step * 15U * 256U +
                               180U) / 360U);
    for (i = 0U; i < BEAT_DAC1_TABLE_SAMPLES; i++)
    {
        /* uint8_t 自然回绕，索引超过 255 时自动进入下一周期起点。 */
        s_dac1_buffer[i] =
            BeatEngine_ToDac(s_config.wave1, (uint8_t)(i + phase_offset), peak);
    }
}

/**
 * @brief  第一版 TABLE 模式：根据 256 点/周期设置 TIM6 更新率
 */
static void BeatEngine_SetDac1Frequency(void)
{
    /* x1 频率等于 440Hz 基准加上用户设置的 df。 */
    uint32_t frequency = (uint32_t)(BEAT_BASE_FREQUENCY_HZ + s_config.delta_hz);
    /* 固定表一个周期 256 点，因此 DAC 更新率必须为 256*f1。 */
    uint32_t update_rate = frequency * BEAT_DAC1_TABLE_SAMPLES;
    /* 整数四舍五入选择分频值，仍会留下约 0.141Hz 等量化误差。 */
    uint32_t divider = (s_timer_clock_hz + update_rate / 2U) / update_rate;

    /* 更新 ARR 后清零计数器，并用更新事件立即装载预装值。 */
    TIM_SetAutoreload(TIM6, (uint16_t)(divider - 1U));
    TIM_SetCounter(TIM6, 0U);
    TIM_GenerateEvent(TIM6, TIM_EventSource_Update);
}
#else
/**
 * @brief  第二版 DDS 模式：根据目标 x1 频率计算 32 位相位增量
 */
static void BeatEngine_UpdateX1PhaseIncrement(void)
{
    /* 第1步：由 440Hz 基准频率加上 df，得到当前 x1 目标频率。 */
    uint32_t frequency = (uint32_t)(BEAT_BASE_FREQUENCY_HZ + s_config.delta_hz);

    /*
     * 第2步：把目标频率换算为每个 DAC 更新点应前进的 32 位相位。
     * 实际更新率为 timer_clock/divider，64 位中间值用于避免乘法溢出。
     */
    s_x1_phase_increment =
        (uint32_t)((((uint64_t)frequency << 32) * s_dac1_timer_divider +
                    s_timer_clock_hz / 2U) /
                   s_timer_clock_hz);
}

/**
 * @brief  第二版 DDS 模式：填充 DAC1 DMA 的一个 128 点空闲半区
 * @param  offset 0 表示前半区，128 表示后半区
 */
static void BeatEngine_FillDac1Half(uint16_t offset)
{
    /* 第1步：i 用于遍历 DMA 已经释放的 128 点半区。 */
    uint16_t i;
    /* 第2步：把 1~11 档幅度换算为 12 位 DAC 的峰峰值码跨度。 */
    uint16_t peak = (uint16_t)(((uint32_t)s_config.amplitude_step * 4095U) /
                               BEAT_MAX_AMPLITUDE_STEP);

    /* 第3步：逐点生成当前空闲半区的数据。 */
    for (i = 0U; i < BEAT_DMA_HALF_SAMPLES; i++)
    {
        /* 第3.1步：取相位高 8 位作为 0~255 的波形索引，再转换为 DAC 码。 */
        s_dac1_buffer[offset + i] =
            BeatEngine_ToDac(s_config.wave1, (uint8_t)(s_x1_phase >> 24), peak);
        /* 第3.2步：相位推进一个采样点；uint32_t 溢出时自然进入下一周期。 */
        s_x1_phase += s_x1_phase_increment;
    }
}
#endif

/**
 * @brief  读取 RCC 实际时钟，并得到 APB1 定时器输入时钟
 */
static void BeatEngine_UpdateTimerClock(void)
{
    /* 标准库结构体用于一次性接收 SYSCLK、HCLK、PCLK1、PCLK2。 */
    RCC_ClocksTypeDef clocks;

    SystemCoreClockUpdate();
    RCC_GetClocksFreq(&clocks);
    s_timer_clock_hz = clocks.PCLK1_Frequency;
    if (clocks.PCLK1_Frequency != clocks.HCLK_Frequency)
    {
        /* STM32F1 在 APB1 分频不为 1 时，定时器时钟自动乘 2。 */
        s_timer_clock_hz *= 2U;
    }
}

/**
 * @brief  参数变化或初始化时复位 FIFO、相位和显示发布状态
 */
static void BeatEngine_ResetStreamingState(void)
{
    /* 清空旧参数对应的历史样本。 */
    BeatFifo_Reset();
    /* DAC2 尚无有效求和数据时先输出 0。 */
    memset(s_dac2_buffer, 0, sizeof(s_dac2_buffer));
#if BEAT_X1_MODE == BEAT_X1_MODE_DDS
    /* x1 初相位由 phase_step*15 度换算为 32 位相位。 */
    s_x1_phase =
        (uint32_t)((((uint64_t)s_config.phase_step * 15U) << 32) / 360U);
#endif
    /* x2 固定从 0 度开始。 */
    s_x2_phase = 0U;
    /* 清除帧序号和诊断统计，等待新的 ADC 半区完成。 */
    s_frame_sequence = 0U;
    s_adc_min = 0U;
    s_adc_max = 0U;
    s_sum_peak = 1U;
    s_frame_valid = 0U;
}

/**
 * @brief  处理一个已经由 ADC DMA 填满的 128 点半区
 * @param  offset 0 表示前半区，128 表示后半区
 * @note   本函数处于 DMA1_CH1 中断上下文，不允许执行 OLED 刷新。
 */
static void BeatEngine_ProcessAdcHalf(uint16_t offset)
{
    /* 第1步：准备循环索引和当前 128 点数据块的统计初值。 */
    uint16_t i;
    /* 最小值从 ADC 满量程开始，后面遇到更小的 x1 就更新。 */
    uint16_t block_min = 4095U;
    /* 最大值从 0 开始，后面遇到更大的 x1 就更新。 */
    uint16_t block_max = 0U;
    /* 峰值从 1 开始，避免后面缩放时出现除以 0。 */
    uint16_t block_peak = 1U;

    /* 第2步：逐点处理 DMA 已经采满的半区。 */
    for (i = 0; i < BEAT_DMA_HALF_SAMPLES; i++)
    {
        /* 第2.1步：建立当前采样时刻的临时数据组。 */
        BeatSample sample;

        /* 第2.2步：从 ADC DMA 缓冲读取 PA0 实际采回的 x1。 */
        sample.x1 = s_adc_buffer[offset + i];
        /* 第2.3步：按当前连续相位计算与这个 x1 同时刻的 x2。 */
        sample.x2 = BeatEngine_ToDac(s_config.wave2,
                                     (uint8_t)(s_x2_phase >> 24), 4095U);
        /* 第2.4步：计算未缩放的 x=x1+x2，理论范围为 0~8190。 */
        sample.sum = (uint16_t)(sample.x1 + sample.x2);
        /* 第2.5步：推进 x2 相位；半区切换时不清零，保证前后批次连续。 */
        s_x2_phase += s_x2_phase_increment;

        /* 第2.6步：更新本批 x1 最小值，供串口诊断采样幅度。 */
        if (sample.x1 < block_min)
        {
            block_min = sample.x1;
        }
        /* 第2.7步：更新本批 x1 最大值。 */
        if (sample.x1 > block_max)
        {
            block_max = sample.x1;
        }
        /* 第2.8步：更新本批 sum 峰值，后面用于 DAC2 缩放。 */
        if (sample.sum > block_peak)
        {
            block_peak = sample.sum;
        }

        /* 第2.9步：暂存未缩放 sum，等本批峰值确定后再统一缩放。 */
        s_half_sum[i] = sample.sum;
        /* 第2.10步：把同一时刻的 x1、x2、sum 一起写入 FIFO。 */
        BeatFifo_Push(&sample);
    }

    /* 第3步：更新保持峰值；只增不减可避免每个短窗口重新拉满。 */
    if (block_peak > s_sum_peak)
    {
        s_sum_peak = block_peak;
    }

    /* 第4步：把未缩放 sum 从最高 8190 压缩到 DAC2 的 0~4095。 */
    for (i = 0; i < BEAT_DMA_HALF_SAMPLES; i++)
    {
        /* 四舍五入后写入与 ADC 当前半区对应的 DAC2 DMA 半区。 */
        s_dac2_buffer[offset + i] = (uint16_t)(((uint32_t)s_half_sum[i] * 4095U +
                                                s_sum_peak / 2U) / s_sum_peak);
    }

    /* 第5步：发布最小值，主循环通过串口显示该诊断数据。 */
    s_adc_min = block_min;
    /* 第6步：发布最大值。 */
    s_adc_max = block_max;
    /* 第7步：帧序号加一，表示又完成了一个 128 点半区。 */
    s_frame_sequence++;
    /* 第8步：置位有效标志，允许显示层读取 FIFO 快照。 */
    s_frame_valid = 1U;
}

/**
 * @brief  配置 DAC1、DAC2 和 ADC1 使用的三个 DMA 通道
 */
static void BeatEngine_InitDma(void)
{
    /* 第1步：定义 DMA 配置结构和中断优先级配置结构。 */
    DMA_InitTypeDef dma;
    NVIC_InitTypeDef nvic;

    /* 第2步：打开 DMA1、DMA2 的 AHB 外设时钟。 */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1 | RCC_AHBPeriph_DMA2, ENABLE);
    /* 第3步：先用标准库默认值初始化 DMA 配置结构。 */
    DMA_StructInit(&dma);
    /* 外设作为目标地址，默认搬运方向为内存到外设。 */
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    /* 外设寄存器地址固定，因此外设地址不递增。 */
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    /* 缓冲区包含连续样本，因此内存地址逐点递增。 */
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    /* DAC、ADC 数据都是 16 位存放，外设侧使用半字。 */
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    /* 内存缓冲同样使用 uint16_t，因此内存侧使用半字。 */
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    /* 循环模式让 DMA 到达缓冲区末尾后自动回到开头。 */
    dma.DMA_Mode = DMA_Mode_Circular;
    /* DAC 通道先使用 High，后面 ADC 通道再提高为 VeryHigh。 */
    dma.DMA_Priority = DMA_Priority_High;
    /* 数据经过外设搬运，不使用纯内存到内存模式。 */
    dma.DMA_M2M = DMA_M2M_Disable;

    /* 第4步：配置 DMA2_CH3，把 s_dac1_buffer 循环送往 DAC1。 */
    DMA_DeInit(DMA2_Channel3);
    /* 目标地址固定为 DAC1 的 12 位右对齐数据寄存器。 */
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->DHR12R1;
    /* 源地址为 x1 的 DMA 双半缓冲。 */
    dma.DMA_MemoryBaseAddr = (uint32_t)s_dac1_buffer;
    /* 一轮 DMA 共搬运 256 个 uint16_t 样本。 */
    dma.DMA_BufferSize = BEAT_DMA_SAMPLES;
    DMA_Init(DMA2_Channel3, &dma);
#if BEAT_X1_MODE == BEAT_X1_MODE_DDS
    /* DDS 需要知道哪个半区已经释放，因此打开半传输和完成中断。 */
    DMA_ITConfig(DMA2_Channel3, DMA_IT_HT | DMA_IT_TC, ENABLE);
#endif

    /* 第5步：配置 DMA2_CH4，把缩放后的 x' 循环送往 DAC2。 */
    DMA_DeInit(DMA2_Channel4);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->DHR12R2;
    dma.DMA_MemoryBaseAddr = (uint32_t)s_dac2_buffer;
    dma.DMA_BufferSize = BEAT_DMA_SAMPLES;
    DMA_Init(DMA2_Channel4, &dma);

    /* 第6步：配置 DMA1_CH1，把 ADC1 结果循环搬入 s_adc_buffer。 */
    DMA_DeInit(DMA1_Channel1);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
    dma.DMA_MemoryBaseAddr = (uint32_t)s_adc_buffer;
    /* ADC 是数据源，因此把方向改为外设到内存。 */
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize = BEAT_DMA_SAMPLES;
    /* ADC 采样不能丢失，所以优先级高于 DAC 输出通道。 */
    dma.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_Init(DMA1_Channel1, &dma);
    /* 每采满 128 点和 256 点分别进入一次处理函数。 */
    DMA_ITConfig(DMA1_Channel1, DMA_IT_HT | DMA_IT_TC, ENABLE);

    /* 第7步：为 ADC DMA 设置最高抢占优先级。 */
    nvic.NVIC_IRQChannel = DMA1_Channel1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

#if BEAT_X1_MODE == BEAT_X1_MODE_DDS
    /* 第8步：DDS 补数中断低于 ADC DMA，先保证真实采样不丢失。 */
    nvic.NVIC_IRQChannel = DMA2_Channel3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
#endif
}

/**
 * @brief  配置两个 DAC 通道及各自的硬件触发源
 */
static void BeatEngine_InitDac(void)
{
    /* 第1步：建立一份 DAC 通用配置结构。 */
    DAC_InitTypeDef dac;

    /* 第2步：加载标准库默认值。 */
    DAC_StructInit(&dac);
    /* 第3步：DAC1 选择 TIM6 TRGO，每次触发输出下一个 x1 点。 */
    dac.DAC_Trigger = DAC_Trigger_T6_TRGO;
    /* 不使用 DAC 内部噪声波或三角波，波形数据全部来自 DMA。 */
    dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
    /* 打开输出缓冲，提高 DAC 引脚的驱动稳定性。 */
    dac.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &dac);

    /* 第4步：DAC2 改用 TIM2 TRGO，与 ADC 采样共用时间基准。 */
    dac.DAC_Trigger = DAC_Trigger_T2_TRGO;
    DAC_Init(DAC_Channel_2, &dac);

    /* 第5步：允许两个 DAC 通道接收 DMA 请求。 */
    DAC_DMACmd(DAC_Channel_1, ENABLE);
    DAC_DMACmd(DAC_Channel_2, ENABLE);
    /* 第6步：最终使能两个 DAC 输出通道。 */
    DAC_Cmd(DAC_Channel_1, ENABLE);
    DAC_Cmd(DAC_Channel_2, ENABLE);
}

/**
 * @brief  初始化 ADC1，先软件自检，再切换到 TIM2_CC2 + DMA
 */
static void BeatEngine_InitAdc(void)
{
    /* 第1步：准备 ADC 配置结构和软件自检的超时计数。 */
    ADC_InitTypeDef adc;
    uint32_t timeout;

    /* 第2步：PCLK2 除以 6 得到 12MHz ADC 时钟，满足芯片限制。 */
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);
    /* 第3步：复位 ADC1，并加载标准库默认配置。 */
    ADC_DeInit(ADC1);
    ADC_StructInit(&adc);
    /* ADC1 独立工作，不使用双 ADC 模式。 */
    adc.ADC_Mode = ADC_Mode_Independent;
    /* 只采一个通道，因此关闭扫描。 */
    adc.ADC_ScanConvMode = DISABLE;
    /* 每个 TIM2 触发只转换一次，因此关闭连续转换。 */
    adc.ADC_ContinuousConvMode = DISABLE;
    /* 自检阶段先不使用外部触发。 */
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    /* 12 位结果右对齐，直接保存为 0~4095。 */
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    /* 规则组中只有 PA0 对应的一个通道。 */
    adc.ADC_NbrOfChannel = 1U;
    ADC_Init(ADC1, &adc);
    /* 第4步：选择 ADC1_CH0，也就是 PA0，并设置 55.5 周期采样时间。 */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1U, ADC_SampleTime_55Cycles5);
    /* 第5步：使能 ADC1，准备执行校准。 */
    ADC_Cmd(ADC1, ENABLE);

    /* 第6步：按 STM32F1 流程先复位校准寄存器。 */
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1) != RESET)
    {
    }
    /* 第7步：启动校准，并等待校准完成。 */
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1) != RESET)
    {
    }
    /* 第8步：清 EOC 标志，再用软件触发做一次 ADC 内核自检。 */
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    /* 第9步：装入超时值，避免硬件异常时永久卡在等待循环。 */
    timeout = BEAT_ADC_SELFTEST_TIMEOUT;
    while ((ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET) && (timeout > 0U))
    {
        timeout--;
    }
    if (timeout > 0U)
    {
        /* 第10步：转换按时完成，保存自检结果并标记成功。 */
        s_adc_selftest_value = ADC_GetConversionValue(ADC1);
        s_adc_selftest_ok = 1U;
    }
    else
    {
        /* 第10步另一分支：超时后记录失败，但允许系统继续运行和诊断。 */
        s_adc_selftest_value = 0U;
        s_adc_selftest_ok = 0U;
    }

    /* 第11步：关闭旧触发配置，准备切换到正式采样模式。 */
    ADC_ExternalTrigConvCmd(ADC1, DISABLE);
    /* 正式模式使用 TIM2_CC2 的上升沿启动每次转换。 */
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T2_CC2;
    ADC_Init(ADC1, &adc);
    /* ADC_Init 后重新确认规则组仍然采 PA0。 */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1U, ADC_SampleTime_55Cycles5);
    /* 第12步：允许 ADC 转换完成后向 DMA1_CH1 发出搬运请求。 */
    ADC_DMACmd(ADC1, ENABLE);
    /* 第13步：最终打开 TIM2_CC2 外部触发，等待定时器启动。 */
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
}

/**
 * @brief  根据实际 APB1 定时器时钟配置 TIM2 和 TIM6
 */
static void BeatEngine_InitTimers(void)
{
    /* 第1步：准备定时器时基和 TIM2_CH2 输出比较配置。 */
    TIM_TimeBaseInitTypeDef tim;
    TIM_OCInitTypeDef oc;
    /* 第2步：四舍五入计算最接近目标 ADC 采样率的整数分频。 */
    uint32_t sample_divider = (s_timer_clock_hz + BEAT_SAMPLE_RATE_HZ / 2U) /
                              BEAT_SAMPLE_RATE_HZ;
    /* TIM6 分频稍后按 DDS 或 TABLE 模式分别计算。 */
    uint32_t dac1_divider;
    /* TIM2 从 0 计数到 ARR，所以周期寄存器等于分频值减 1。 */
    uint16_t sample_period = (uint16_t)(sample_divider - 1U);

    /* 第3步：保存整数分频后的实际采样率，而不是只保存目标值。 */
    s_sample_rate_hz = s_timer_clock_hz / sample_divider;
#if BEAT_X1_MODE == BEAT_X1_MODE_DDS
    /* 第4步：DDS 模式把 TIM6 更新率固定在约 112.5kHz。 */
    dac1_divider = (s_timer_clock_hz + BEAT_DAC1_SAMPLE_RATE_HZ / 2U) /
                   BEAT_DAC1_SAMPLE_RATE_HZ;
    /* 保存实际 TIM6 分频，后面用它计算 x1 相位增量。 */
    s_dac1_timer_divider = dac1_divider;
    /* 根据固定更新率和目标 x1 频率重新计算 DDS 相位步长。 */
    BeatEngine_UpdateX1PhaseIncrement();
#else
    {
        /* TABLE 模式必须让 256 次更新正好构成一个目标周期。 */
        uint32_t update_rate =
            BEAT_DAC1_TABLE_SAMPLES *
            (uint32_t)(BEAT_BASE_FREQUENCY_HZ + s_config.delta_hz);
        dac1_divider = (s_timer_clock_hz + update_rate / 2U) / update_rate;
    }
#endif
    /* 第5步：按 ADC 的实际采样时钟计算固定 440Hz 的 x2 相位步长。 */
    s_x2_phase_increment = (uint32_t)((((uint64_t)BEAT_BASE_FREQUENCY_HZ << 32) *
                                       sample_divider + s_timer_clock_hz / 2U) /
                                      s_timer_clock_hz);

    /* 第6步：打开 TIM2 和 TIM6 的 APB1 外设时钟。 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM6, ENABLE);
    /* 第7步：建立两个定时器共用的向上计数基础配置。 */
    TIM_TimeBaseStructInit(&tim);
    /* 预分频为 0，直接使用完整 APB1 定时器时钟。 */
    tim.TIM_Prescaler = 0U;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;

    /* 第8步：把 ADC 采样周期写入 TIM2。 */
    tim.TIM_Period = sample_period;
    TIM_TimeBaseInit(TIM2, &tim);
    /* TIM2 更新事件通过 TRGO 触发 DAC2 输出下一个 x' 点。 */
    TIM_SelectOutputTrigger(TIM2, TIM_TRGOSource_Update);
    /* 第9步：配置 TIM2_CH2 在周期中间产生 ADC 触发上升沿。 */
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = (uint16_t)((sample_period + 1U) / 2U);
    TIM_OC2Init(TIM2, &oc);
    TIM_OC2PreloadConfig(TIM2, TIM_OCPreload_Enable);

    /* 第10步：把 DAC1 更新周期写入 TIM6。 */
    tim.TIM_Period = (uint16_t)(dac1_divider - 1U);
    TIM_TimeBaseInit(TIM6, &tim);
    /* TIM6 更新事件通过 TRGO 触发 DAC1 输出下一个 x1 点。 */
    TIM_SelectOutputTrigger(TIM6, TIM_TRGOSource_Update);
}

/**
 * @brief  按既定顺序启动 ADC、DAC、DMA 和两个定时器
 */
static void BeatEngine_StartStreams(void)
{
    /* 第1步：清除三个 DMA 通道在上一次运行中留下的全部标志。 */
    DMA_ClearFlag(DMA1_FLAG_GL1);
    DMA_ClearFlag(DMA2_FLAG_GL3);
    DMA_ClearFlag(DMA2_FLAG_GL4);
    /* 第2步：把 ADC DMA 的传输计数恢复为完整 256 点。 */
    DMA_SetCurrDataCounter(DMA1_Channel1, BEAT_DMA_SAMPLES);
    /* 第3步：恢复 DAC1 DMA 传输计数。 */
    DMA_SetCurrDataCounter(DMA2_Channel3, BEAT_DMA_SAMPLES);
    /* 第4步：恢复 DAC2 DMA 传输计数。 */
    DMA_SetCurrDataCounter(DMA2_Channel4, BEAT_DMA_SAMPLES);
    /* 第5步：先允许 ADC 产生 DMA 请求。 */
    ADC_DMACmd(ADC1, ENABLE);
    /* 第6步：先允许 ADC 接收外部触发，但定时器此时还没有启动。 */
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
    /* 第7步：依次使能 ADC、DAC1、DAC2 的三个 DMA 通道。 */
    DMA_Cmd(DMA1_Channel1, ENABLE);
    DMA_Cmd(DMA2_Channel3, ENABLE);
    DMA_Cmd(DMA2_Channel4, ENABLE);
    /* 第8步：两个定时器都从计数值 0 开始。 */
    TIM_SetCounter(TIM2, 0U);
    TIM_SetCounter(TIM6, 0U);
    /* 第9步：清除 TIM2 旧的更新和 CC2 标志。 */
    TIM_ClearFlag(TIM2, TIM_FLAG_Update | TIM_FLAG_CC2);
    /* 第10步：手动产生更新事件，把预装载的 ARR 等参数立即装入。 */
    TIM_GenerateEvent(TIM2, TIM_EventSource_Update);
    /* 手动更新也会产生标志，因此正式启动前再清除一次。 */
    TIM_ClearFlag(TIM2, TIM_FLAG_Update | TIM_FLAG_CC2);
    /* 第11步：最后启动 TIM2，开始 ADC 采样和 DAC2 输出。 */
    TIM_Cmd(TIM2, ENABLE);
    /* 第12步：启动 TIM6，开始 DAC1 输出 x1。 */
    TIM_Cmd(TIM6, ENABLE);
}

/**
 * @brief  停止定时器和 DMA，供完整参数重配置使用
 */
static void BeatEngine_StopStreams(void)
{
    /* 先停止触发源，防止关闭 DMA 时外设仍继续请求数据。 */
    TIM_Cmd(TIM2, DISABLE);
    TIM_Cmd(TIM6, DISABLE);
    DMA_Cmd(DMA1_Channel1, DISABLE);
    DMA_Cmd(DMA2_Channel3, DISABLE);
    DMA_Cmd(DMA2_Channel4, DISABLE);
    /* 清除旧 HT/TC/TE 标志，避免重启后误进入中断。 */
    DMA_ClearFlag(DMA1_FLAG_GL1);
    DMA_ClearFlag(DMA2_FLAG_GL3);
    DMA_ClearFlag(DMA2_FLAG_GL4);
}

/**
 * @brief  完成拍频实时引擎的全部初始化并启动数据流
 */
void BeatEngine_Init(void)
{
    /* 第1步：准备 GPIO 配置结构。 */
    GPIO_InitTypeDef gpio;

    /* 第2步：打开 GPIOA、ADC1 和 DAC 的外设时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
    /* 第3步：选择 PA0、PA4、PA5 三个模拟引脚。 */
    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_4 | GPIO_Pin_5;
    /* 模拟模式关闭数字输入缓冲，分别供 ADC1、DAC1、DAC2 使用。 */
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    /* 第4步：PA1 不输出 TIM2_CH2 波形，只保留内部 ADC 触发关系。 */
    gpio.GPIO_Pin = GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    /* 第5步：读取实际 APB1 定时器时钟，供后续分频计算。 */
    BeatEngine_UpdateTimerClock();
    /* 第6步：把默认配置限制在任务书允许的参数范围内。 */
    BeatEngine_ClampConfig(&s_config);
#if BEAT_X1_MODE == BEAT_X1_MODE_TABLE
    /* TABLE 模式额外步骤：启动前一次性生成完整 256 点周期。 */
    BeatEngine_RebuildDac1Table();
#endif
    /* 第7步：配置 TIM2 采样时间轴和 TIM6 DAC1 更新时间轴。 */
    BeatEngine_InitTimers();
    /* 第8步：清空 FIFO、相位、峰值和帧状态。 */
    BeatEngine_ResetStreamingState();
#if BEAT_X1_MODE == BEAT_X1_MODE_DDS
    /* 第9步：DDS 启动前先准备 DAC1 DMA 前 128 点。 */
    BeatEngine_FillDac1Half(0U);
    /* 第10步：继续准备后 128 点，防止 DMA 读到未初始化数据。 */
    BeatEngine_FillDac1Half(BEAT_DMA_HALF_SAMPLES);
#endif
    /* 第11步：建立三条 DMA 搬运链路。 */
    BeatEngine_InitDma();
    /* 第12步：设置 DAC1 和 DAC2 各自的硬件触发源。 */
    BeatEngine_InitDac();
    /* 第13步：校准 ADC，并切换到 TIM2_CC2 外部触发模式。 */
    BeatEngine_InitAdc();
    /* 第14步：所有缓冲和外设就绪后，统一启动实时数据流。 */
    BeatEngine_StartStreams();
}

/**
 * @brief  复制当前生效配置
 * @param  config 目标结构体；空指针时不执行操作
 */
void BeatEngine_GetConfig(BeatConfig *config)
{
    if (config != 0)
    {
        *config = s_config;
    }
}

/**
 * @brief  应用波形、幅度、相位等完整配置
 * @param  config 新配置
 * @note   完整配置会停止并重启数据流，以保证相位起点和 FIFO 状态确定。
 */
void BeatEngine_ApplyConfig(const BeatConfig *config)
{
    /* 第1步：空指针没有可应用的参数，直接返回。 */
    if (config == 0)
    {
        return;
    }

    /* 第2步：停止触发和 DMA，防止配置过程中继续使用旧参数。 */
    BeatEngine_StopStreams();
    /* 第3步：复制用户确认后的新配置。 */
    s_config = *config;
    /* 第4步：再次执行范围钳位，防止越界参数进入硬件链路。 */
    BeatEngine_ClampConfig(&s_config);
#if BEAT_X1_MODE == BEAT_X1_MODE_TABLE
    /* TABLE 参数变化需要重建波表并更新 TIM6 频率。 */
    BeatEngine_RebuildDac1Table();
    BeatEngine_SetDac1Frequency();
#else
    /* DDS 只需重算 x1 相位增量，波形点随后动态生成。 */
    BeatEngine_UpdateX1PhaseIncrement();
#endif
    /* 第5步：清空旧参数留下的 FIFO、相位、峰值和帧状态。 */
    BeatEngine_ResetStreamingState();
#if BEAT_X1_MODE == BEAT_X1_MODE_DDS
    /* 第6步：按新配置重新准备 DDS 前半区。 */
    BeatEngine_FillDac1Half(0U);
    /* 第7步：按新配置重新准备 DDS 后半区。 */
    BeatEngine_FillDac1Half(BEAT_DMA_HALF_SAMPLES);
#endif
    /* 第8步：两个半区准备完成后重新启动实时链路。 */
    BeatEngine_StartStreams();
}

/**
 * @brief  只调整频差 df，不重置 FIFO 和相位连续性
 * @param  delta_hz 目标频差，最终限制为 -30~+30Hz
 */
void BeatEngine_SetDeltaHz(int8_t delta_hz)
{
#if BEAT_X1_MODE == BEAT_X1_MODE_TABLE
    /* TABLE 模式改 ARR 前暂时停止 TIM6。 */
    TIM_Cmd(TIM6, DISABLE);
#endif
    s_config.delta_hz = delta_hz;
    BeatEngine_ClampConfig(&s_config);
#if BEAT_X1_MODE == BEAT_X1_MODE_TABLE
    /* 波表内容不变，只改变每个点的输出速度。 */
    BeatEngine_SetDac1Frequency();
    TIM_Cmd(TIM6, ENABLE);
#else
    /* DDS 模式只改变相位步长，当前相位保持连续。 */
    BeatEngine_UpdateX1PhaseIncrement();
#endif
}

/**
 * @brief  从 FIFO 取得最新完整显示帧
 * @param  frame 接收 x1、x2、sum 和帧序号
 * @retval 1 成功取得 BEAT_FRAME_SAMPLES 点
 * @retval 0 尚无有效帧或一致性复制失败
 */
uint8_t BeatEngine_CopyLatestFrame(BeatFrame *frame)
{
    /* 第1步：准备实际复制数量和数组拆分循环索引。 */
    uint16_t count;
    uint16_t i;

    /* 第2步：目标为空或 ADC 尚无有效帧时，直接报告失败。 */
    if ((frame == 0) || (s_frame_valid == 0U))
    {
        return 0U;
    }

    /* 第3步：从 FIFO 一致性复制最新 128 组三元组。 */
    count = BeatFifo_CopyLatest(s_copy_scratch, BEAT_FRAME_SAMPLES);
    /* 第4步：不足完整显示帧时不发布半帧数据。 */
    if (count != BEAT_FRAME_SAMPLES)
    {
        return 0U;
    }

    /* 第5步：把每组三元组拆成显示层需要的三路独立数组。 */
    for (i = 0; i < count; i++)
    {
        frame->x1[i] = s_copy_scratch[i].x1;
        frame->x2[i] = s_copy_scratch[i].x2;
        frame->sum[i] = s_copy_scratch[i].sum;
    }
    /* 第6步：记录本帧点数。 */
    frame->sample_count = count;
    /* 第7步：附带当前 ADC 半区序号，便于判断帧是否更新。 */
    frame->sequence = s_frame_sequence;
    /* 第8步：返回 1，表示已经得到完整稳定的一帧。 */
    return 1U;
}

/**
 * @brief  发布当前实时链路的诊断状态
 * @param  status 目标状态结构体
 */
void BeatEngine_GetStatus(BeatEngineStatus *status)
{
    if (status == 0)
    {
        return;
    }

    status->frame_count = s_frame_sequence;
    status->timer_clock_hz = s_timer_clock_hz;
    status->sample_rate_hz = s_sample_rate_hz;
    status->adc_min = s_adc_min;
    status->adc_max = s_adc_max;
    status->sum_peak = s_sum_peak;
    status->fifo_count = BeatFifo_GetCount();
    status->adc_selftest_value = s_adc_selftest_value;
    /* DMA 剩余计数和定时器寄存器直接读取硬件当前状态。 */
    status->adc_dma_remaining = DMA_GetCurrDataCounter(DMA1_Channel1);
    status->tim2_arr = (uint16_t)TIM2->ARR;
    status->tim6_arr = (uint16_t)TIM6->ARR;
    status->frame_valid = s_frame_valid;
    status->adc_selftest_ok = s_adc_selftest_ok;
    /* CC2IF 出现说明 TIM2_CH2 至少产生过一次 ADC 触发边沿。 */
    status->tim2_cc2_seen = (TIM2->SR & TIM_SR_CC2IF) != 0U ? 1U : 0U;
}

/**
 * @brief  ADC1 DMA 半传输/传输完成中断
 */
void DMA1_Channel1_IRQHandler(void)
{
    /* 第1步：检查 ADC DMA 是否刚刚采满前 128 点。 */
    if (DMA_GetITStatus(DMA1_IT_HT1) != RESET)
    {
        /* 第1.1步：先清半传输标志，避免重复进入同一分支。 */
        DMA_ClearITPendingBit(DMA1_IT_HT1);
        /* 第1.2步：处理偏移 0 开始的前半区，DMA 同时继续采后半区。 */
        BeatEngine_ProcessAdcHalf(0U);
    }

    /* 第2步：检查 ADC DMA 是否刚刚采满后 128 点。 */
    if (DMA_GetITStatus(DMA1_IT_TC1) != RESET)
    {
        /* 第2.1步：先清传输完成标志。 */
        DMA_ClearITPendingBit(DMA1_IT_TC1);
        /* 第2.2步：处理偏移 128 的后半区，DMA 已回到前半区继续采样。 */
        BeatEngine_ProcessAdcHalf(BEAT_DMA_HALF_SAMPLES);
    }
}

#if BEAT_X1_MODE == BEAT_X1_MODE_DDS
/**
 * @brief  DAC1 DMA 半区释放中断，用于 DDS 动态补点
 */
void DMA2_Channel3_IRQHandler(void)
{
    /* 第1步：检查 DAC1 DMA 是否已经读完前 128 点。 */
    if (DMA_GetITStatus(DMA2_IT_HT3) != RESET)
    {
        /* 第1.1步：清除半传输标志。 */
        DMA_ClearITPendingBit(DMA2_IT_HT3);
        /* 第1.2步：DMA 正在读后半区，CPU 可以安全重填前半区。 */
        BeatEngine_FillDac1Half(0U);
    }

    /* 第2步：检查 DAC1 DMA 是否已经读完后 128 点。 */
    if (DMA_GetITStatus(DMA2_IT_TC3) != RESET)
    {
        /* 第2.1步：清除传输完成标志。 */
        DMA_ClearITPendingBit(DMA2_IT_TC3);
        /* 第2.2步：DMA 已回到前半区，CPU 可以安全重填后半区。 */
        BeatEngine_FillDac1Half(BEAT_DMA_HALF_SAMPLES);
    }
}
#endif
