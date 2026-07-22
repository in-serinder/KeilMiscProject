# KeilMiscProject

[简体中文](./README_CN.md)/
[English](./README.md)

- [简要介绍](#简要介绍)
- [文件夹命名结构](#文件夹命名结构)
- [当前项目列表](#当前项目列表)
- [驱动列表](#驱动列表)
- [项目列表](#项目列表)

## 简要介绍

一些完成的 Keil 练习（通常是单个模块或函数或是一些硬件的*驱动函数*）被放在一个文件夹里，作为独立的项目。每个文件夹都包含项目文件，包括 `.uvproj`、`.uvopt`、`.c` 和 `.h` 文件，以及一个 `ReadMe.md` 用于简要介绍。要使用它们，请克隆这个仓库。

```
git clone  https://github.com/in-serinder/KeilMiscProject.git
```

## 文件夹命名结构

在项目中 文件夹命名结构为

```
单片机平台_[带Driver为指定硬件驱动函数]_项目名称
```

### 当前项目列表

### 驱动列表

- [51 TM1637 显示驱动](./51_Driver_TM1637/)
- [MAX7219 显示驱动](./51_Driver_max7219/)
- [51 24C64 EEPROM 驱动](./51_Driver_24c64_EEPROM/)
- [51 SSD13xx OLED 显示屏驱动 适用于Ssd1306 ssd1315 OLED显示屏 可自定义屏幕纵横](./51_Driver_SSD13xx/)
- [51 PCF8574 I2C 接口扩展器 驱动](./51_Driver_PCF8574/)
- [51 TLCx543(TLC1543/TLC2543) 10-12位ADC 驱动](./51_Driver_TLCx543_10-12bitADC/)

- [STM32 DHT11 温湿度传感器 驱动](./STM32_Driver_DHT11/)

### 项目列表

- [51 18650 多通道充放电控制器](./51_18650_MultiChannelChargeDischargeController/)
- [51 四通道高压开关控制器](./51_high_voltage_isolator/)
- [51 简易周易抽签卡](./51_zhouyi/)
- [51 4v/6v铅酸电池充电放电控制器](./51_STC15w408as_4V~6vBatterChangerSimple/)
- [51 继电器数码管Wifi时钟](./51_esp8266RelayOclock_LCD/)
- [51 LCD1602 时钟项目](./51_LCD1602OClock/)
- [51 4-8位数码管显示项目](./51_digital_4-8tube/)
- [51 电阻穷举组合式假负载](./51_Rexhaustive_fakeload)
- [STM32 峰值辅助仪](./STM32_PeakValueAssistant/)
- [STM32 项目模板](./STM32_Project/)
