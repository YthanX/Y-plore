# 硬件平台与接线说明

## 1. 主控平台

| 项目 | 配置 |
|---|---|
| MCU | STM32F103RCT6，Cortex-M3 |
| Flash | 256KB，`0x08000000~0x0803FFFF` |
| SRAM | 48KB，`0x20000000~0x2000BFFF` |
| 外部晶振 | 8MHz |
| 系统时钟 | 72MHz（HSE经PLL倍频） |
| 调试接口 | SWD；关闭JTAG但保留SWD |
| 标准库 | STM32F10x Standard Peripheral Library |

`STM32F10X_HD` 与 `startup_stm32f10x_hd.s` 是根据 256KB Flash 容量选择的。PA15、PB3、PB4 默认属于 JTAG，因此 `Key_Init()` 会关闭 JTAG，只保留 PA13/PA14 的 SWD。

## 2. 必需接线

| 信号 | MCU引脚 | 方向 | 说明 |
|---|---|---|---|
| x1模拟输出 | PA4 / DAC1_OUT | 输出 | DAC1真实输出x1 |
| x1模拟回采 | PA0 / ADC1_CH0 | 输入 | 必须用短跳线连接PA4 |
| 缩比求和x' | PA5 / DAC2_OUT | 输出 | 接高阻示波器，不直接驱动低阻负载 |
| 编码器A | PA6 / TIM3_CH1 | 输入 | 内部上拉 |
| 编码器B | PA7 / TIM3_CH2 | 输入 | 内部上拉 |
| KEY0 | PC5 / EXTI5 | 输入 | 低电平按下，切换页面 |
| KEY1 | PA15 | 输入 | 低电平按下，编辑/确认 |
| 扩展键 | PC4 | 输入 | LCD示波器控制项切换，可不接 |
| USART1_TX | PA9 | 输出 | 115200bps调试和CSV导出 |
| USART1_RX | PA10 | 输入 | 接收`D/d`命令 |

最关键的外部连接是：

```text
PA4 (DAC1_OUT) ---------------- PA0 (ADC1_CH0)
PA5 (DAC2_OUT) ---------------- 示波器高阻输入
GND             ---------------- 示波器/串口模块公共地
```

PA4 到 PA0 不是芯片内部连接，而是本项目为了满足“DAC真实输出后再由ADC采回”自行采用的短跳线。测试时 PA0 不应再连接其他主动模拟源。

## 3. 旋转编码器和按键

编码器使用 TIM3 Encoder mode：A、B 分别接 PA6、PA7，公共端接 GND；若模块需要供电，使用 3.3V。不要把 5V 输出直接送入本项目的 GPIO。

普通独立按键只需一端接 GPIO、另一端接 GND，程序使用内部上拉：

```text
PC4/PC5/PA15 ---- 按键 ---- GND
```

## 4. OLED基础显示后端

| OLED信号 | MCU引脚 | 说明 |
|---|---|---|
| SCL | PC10 | 软件I2C时钟 |
| SDA | PB15 | 软件I2C数据 |
| VCC | 3.3V | 按模块规格供电 |
| GND | GND | 公共地 |

OLED地址在 `hardware/OLED_I2C.h` 中定义为 `0x78`（8位写地址表示法）。OLED是任务要求的基础显示后端。

## 5. ILI9341 LCD扩展后端

| LCD信号 | MCU引脚 |
|---|---|
| D0~D15 | PB0~PB15 |
| CS | PC9 |
| RS/DC | PC8 |
| WR | PC7 |
| RD | PC6 |
| RST | PD3 |
| BL | PC10 |

LCD采用16位GPIO并口，显示320x240三路波形，并扩展示波器的通道、时基、增益、Y-T/X-Y、触发和RUN/STOP功能。

## 6. 显示后端冲突

OLED与LCD必须二选一：

- PC10同时是OLED SCL和LCD背光。
- PB15同时是OLED SDA和LCD D15。
- LCD还占用整个PB0~PB15数据口。

通过 `user/beat_project_config.h` 的 `BEAT_DISPLAY_BACKEND` 在编译期选择后端，未选中的驱动不会初始化。

## 7. 外设与DMA分配

| 功能 | 外设/触发 | DMA通道 | 设计作用 |
|---|---|---|---|
| x1输出 | TIM6 TRGO -> DAC1 | DMA2_CH3 | 连续更新PA4 |
| x1采样 | TIM2_CC2 -> ADC1 | DMA1_CH1 | 循环双半缓冲采PA0 |
| x2生成 | 32位相位累加器 | 无 | 每处理一个ADC点计算一个x2 |
| x'输出 | TIM2 TRGO -> DAC2 | DMA2_CH4 | 与ADC共用离散时间基准 |
| 编码器 | TIM3 Encoder mode | 无 | PA6/PA7硬件正交解码 |
| 串口发送 | USART1_TX | DMA1_CH4 | 非阻塞状态/CSV发送 |

TIM2同时承担 ADC 采样节拍和 DAC2 输出节拍，使 x1、x2、x' 使用同一离散时间轴。TIM6只负责DAC1输出更新。

## 8. 电气与测试注意事项

- 所有模块必须共地。
- PA4、PA5电压范围应保持在0~3.3V。
- PA5输出的是缩放后的求和信号x'，形状和包络对应x，但数值幅度不等于未缩放的x。
- DAC输出能力有限，只接示波器高阻输入或合适的缓冲级。
- 使用LCD时不要再连接OLED；使用OLED时不要让LCD并口继续驱动PB15/PC10。
- 下载调试使用SWD，避免恢复完整JTAG后与PA15按键及LCD数据线冲突。
