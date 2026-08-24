# 整理版验证记录

验证日期：2026-08-24

## 工程完整性

- Keil工程引用文件：74个，缺失0个。
- EIDE工程引用文件：74个，缺失0个。
- MCU：STM32F103RC。
- 宏：`USE_STDPERIPH_DRIVER`、`STM32F10X_HD`。
- 启动文件：`start/startup_stm32f10x_hd.s`。
- Flash：256KB；RAM：48KB。

## ARM Compiler 5构建结果

| 配置 | Code | RO-data | RW-data | ZI-data | RAM总量 | ROM总量 | 结果 |
|---|---:|---:|---:|---:|---:|---:|---|
| LCD + DDS | 22492B | 5632B | 380B | 44308B | 44688B（43.64KB） | 28316B（27.65KB） | 通过 |
| OLED + DDS | 16412B | 1520B | 152B | 7912B | 8064B（7.88KB） | 18084B（17.66KB） | 通过 |

LCD模式RAM占用约90.9%，主要来自LCD局部RGB565缓冲、512组三路FIFO、DMA缓冲和显示帧。后续增加大数组前必须重新检查链接结果。

## 最终状态

- `user/beat_project_config.h` 已恢复为默认 LCD 后端。
- `BEAT_X1_MODE` 保持 DDS。
- 两种显示后端的HEX/BIN均保存在 `firmware/`。
- 固件SHA-256见 `firmware/SHA256SUMS.txt`。

本记录只证明源码和工程配置可以完成编译、链接，不替代PA4/PA5、按键、编码器、显示器和串口的实物验收。
