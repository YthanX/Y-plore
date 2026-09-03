/**
  ******************************************************************************
  * @file    main.c
  * @brief   拍频课程设计主程序入口
  * @note    main 只完成基础初始化，具体业务由 BeatApp 统一调度。
  ******************************************************************************
  */

#include "main.h"
#include "Delay.h"
#include "usart.h"
#include "beat_app.h"

/**
 * @brief  系统程序入口
 * @retval main 在嵌入式环境中不会返回
 */
int main(void)
{
    /* 使用 2 位抢占优先级 + 2 位响应优先级，供 ADC DMA、USART、按键等中断分级。 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* USART1 作为启动日志、运行诊断和 ADC CSV 导出的调试通道。 */
    USART1_Init(115200);
    /* 上电后稍作等待，避免串口工具尚未连接时立即丢失启动信息。 */
    Delay_ms(100);
    USART1_SendString("Beat-frequency project ready.\r\n");

    /*
     * 应用层统一初始化 Key、Display、Encoder 和 BeatEngine。
     * main 不直接操作 ADC/DAC，避免入口函数承担具体业务。
     */
    BeatApp_Init();

    while (1)
    {
        /* 每次执行一轮按键、编码器、串口、状态检查和显示调度。 */
        BeatApp_Task();
        /* 形成约 1ms 的应用层时间基准，刷新和超时计数均以此为单位。 */
        Delay_ms(1);
    }
}
