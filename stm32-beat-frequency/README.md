<div align="center">

# STM32F103RCT6 拍频测量项目

基于 **STM32F103RCT6** 的拍频信号采集、测量与显示课程设计。

<p>
  <img src="https://img.shields.io/badge/MCU-STM32F103RCT6-03234B?style=flat-square&logo=stmicroelectronics&logoColor=white" alt="STM32F103RCT6">
  <img src="https://img.shields.io/badge/Language-C-A8B9CC?style=flat-square&logo=c&logoColor=white" alt="C">
  <img src="https://img.shields.io/badge/Startup-ARM%20Assembly-0091BD?style=flat-square&logo=arm&logoColor=white" alt="ARM Assembly">
  <img src="https://img.shields.io/badge/IDE-Keil%20MDK-394049?style=flat-square" alt="Keil MDK">
</p>

</div>

---

## 项目简介

本项目以 **STM32F103RCT6** 为核心，实现拍频信号的采集、处理与显示。

程序采用较清晰的应用层结构：`main.c` 负责系统基础初始化和主循环调度，具体拍频业务由 `BeatApp` 统一管理。工程中包含按键、编码器、显示、串口以及拍频信号处理相关模块。

主要功能包括：

- 拍频信号采集与处理
- ADC / DMA 数据获取
- 串口调试与数据输出
- 按键与编码器交互
- 显示界面刷新
- 拍频应用层统一调度

---

## 技术栈

| 项目 | 使用内容 |
| --- | --- |
| MCU | STM32F103RCT6 |
| 主要语言 | C |
| 启动代码 | ARM Assembly |
| 开发环境 | Keil MDK / µVision |
| 固件基础 | STM32F10x Standard Peripheral Library |
| 工程文件 | `STM32F103RCT6_BeatFrequency.uvprojx` |

---

## 目录结构

```text
25262/
├── README.md
└── STM32F103RCT6_拍频项目/
    ├── firmware/     # 固件及外设相关代码
    ├── hardware/     # 硬件驱动
    ├── library/      # STM32 标准外设库
    ├── start/        # CMSIS 与启动文件
    ├── system/       # 系统级功能
    ├── user/         # 主程序与拍频应用逻辑
    ├── docs/         # 项目文档
    ├── build/        # 构建输出
    └── STM32F103RCT6_BeatFrequency.uvprojx
```

其中核心应用代码位于 `user/`：

```text
user/
├── main.c
├── beat_app.c
├── beat_app.h
├── beat_display.c
├── beat_display.h
└── beat_project_config.h
```

---

## 程序结构

```text
系统初始化
    │
    ├── USART
    ├── Key
    ├── Display
    ├── Encoder
    └── Beat Engine
    │
    ▼
BeatApp_Init()
    │
    ▼
while (1)
    │
    ├── 按键 / 编码器
    ├── 串口与状态处理
    ├── 拍频数据处理
    └── 显示刷新
    │
    ▼
BeatApp_Task()
```

---

## 说明

该目录用于保存本阶段的 STM32 拍频课程设计工程与相关实现，代码以嵌入式 C 为主，并保留 STM32F1 的启动汇编及标准外设库文件。
