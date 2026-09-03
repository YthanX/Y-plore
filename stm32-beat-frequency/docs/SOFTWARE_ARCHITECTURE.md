# 软件结构与数据流

## 1. 分层结构

```text
main.c
  |
  `-- BeatApp：应用状态机与任务调度
        |-- BeatEngine：实时信号链
        |     |-- DAC1 / ADC1 / DAC2
        |     |-- TIM2 / TIM6
        |     |-- DMA1_CH1 / DMA2_CH3 / DMA2_CH4
        |     `-- x2相位累加、求和与缩放
        |-- BeatFifo：连续三路样本与一致性快照
        |-- BeatDisplay：OLED/LCD后端抽象
        |-- Key：按键事件与消抖
        |-- Encoder：TIM3硬件编码器
        `-- USART1：运行诊断与CSV导出
```

## 2. 实时处理路径

1. TIM6触发DAC1更新，PA4输出x1。
2. PA4经外部跳线进入PA0。
3. TIM2_CC2按固定采样率触发ADC1。
4. DMA1_CH1把ADC结果循环写入缓冲区。
5. DMA半传输或传输完成中断处理对应半区：读取x1、计算同一序号的x2、求和并推入FIFO。
6. 求和结果按保持峰值缩放，写入DAC2空闲半区，随后由TIM2 TRGO驱动PA5输出。
7. 主循环只复制一致帧并刷新显示，不在DMA中断中操作屏幕。

## 3. FIFO生产者与消费者

```text
DMA中断（生产者） ----> BeatFifo ----> OLED/LCD（消费者）
                                  `--> USART CSV（消费者）
```

FIFO保存同一采样时刻的`x1/x2/sum`三元组。写指针指向“下一次写入位置”；读取最新窗口时从写指针向前回退所需数量，再按时间从旧到新复制。

`generation`用于一致性检查：复制前后版本必须相同且处于稳定状态，否则说明DMA中断在复制过程中更新了FIFO，本次结果放弃并重试。这样不需要长时间关闭中断。

## 4. 应用层调度

`BeatApp_Task()`约每1ms执行一次，依次完成：

1. 接收串口命令。
2. 获取按键事件并切换页面/编辑状态。
3. 读取编码器增量并调整频差、菜单参数或LCD示波器参数。
4. 检查ADC帧是否持续更新。
5. 发送周期诊断或待发送CSV。
6. 菜单按事件重画；波形页按固定周期读取一致帧并刷新。

## 5. 主要接口

| API | 模块 | 作用 |
|---|---|---|
| `BeatApp_Init()` | BeatApp | 初始化按键、显示、编码器和信号引擎 |
| `BeatApp_Task()` | BeatApp | 主循环任务调度 |
| `BeatEngine_Init()` | BeatEngine | 初始化时钟、GPIO、DMA、DAC、ADC和定时器 |
| `BeatEngine_ApplyConfig()` | BeatEngine | 应用波形、幅度和相位配置 |
| `BeatEngine_SetDeltaHz()` | BeatEngine | 调整x1与440Hz基准的频差 |
| `BeatEngine_CopyLatestFrame()` | BeatEngine | 获取可供显示/导出的稳定帧 |
| `BeatFifo_Push()` | BeatFifo | DMA中断追加三路样本 |
| `BeatFifo_CopyLatest()` | BeatFifo | 复制最新时间窗口 |
| `BeatDisplay_ShowWave()` | BeatDisplay | 绘制三路波形或LCD示波器页 |
| `BeatDisplay_ShowMenu()` | BeatDisplay | 绘制参数菜单 |
| `Encoder_GetDelta()` | Encoder | 读取旋转档位变化 |
| `Key_TakeEvent()` | Key | 获取已消抖按键事件 |
| `USART1_SendDma()` | USART | 通过DMA1_CH4非阻塞发送 |

## 6. 配置关系

显示后端会同时决定采样帧和FIFO容量：

| 后端 | 目标采样率 | 显示帧 | FIFO |
|---|---:|---:|---:|
| OLED | 8192Hz | 128点 | 256组 |
| LCD | 16384Hz | 256点 | 512组 |

这些数量不是任务书强制值，而是根据48KB RAM、屏幕横向分辨率、载波每周期采样点数和刷新开销综合选择的。
