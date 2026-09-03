# 构建与验收

## 构建

### Keil MDK

1. 安装 STM32F1 Device Family Pack 和 ARM Compiler 5。
2. 打开根目录 `STM32F103RCT6_BeatFrequency.uvprojx`。
3. 确认设备为 STM32F103RC，启动文件为 `startup_stm32f10x_hd.s`。
4. 确认 IROM 为 `0x08000000 / 0x40000`，IRAM 为 `0x20000000 / 0xC000`。
5. Build Target，随后通过SWD下载。

### EIDE

1. 用VS Code/EIDE打开本目录。
2. 工具链选择ARMCC 5。
3. 若本机尚无STM32F1 Pack，可在EIDE中安装；源码构建本身使用项目内标准库和启动文件。
4. 选择`Target 1`并构建。

## 最小验收路径

1. 连接PA4到PA0并上电。
2. 串口应输出`Beat-frequency project ready.`，状态中的frame和fifo持续增加。
3. PA4应输出约`440+df` Hz的x1，默认参数以源码当前配置为准。
4. 显示器应出现x1、x2和x三路波形，不应长期停留在`WAIT ADC`。
5. PA5应输出0~3.3V范围内的缩比求和x'。
6. KEY0进入菜单，编码器移动选项，KEY1进入编辑并再次确认保存。
7. 串口发送`D`，应收到`ADC_X1,FS=...,F=...,N=...`及一个周期的ADC码值。

## 固件目录

- `firmware/STM32F103RCT6_BeatFrequency_LCD.hex`：LCD + DDS配置。
- `firmware/STM32F103RCT6_BeatFrequency_OLED.hex`：OLED + DDS配置。

两套固件均在整理后的独立目录中完成编译验证。修改显示后端、x1模式或业务代码后必须重新编译，不应继续使用旧HEX。
