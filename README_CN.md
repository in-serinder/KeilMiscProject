# KeilMiscProject

[简体中文](./README_CN.md)/
[English](./README.md)

- [简要介绍](#简要介绍)
- [文件夹命名结构](#文件夹命名结构)
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

### 驱动列表

- [51 TM1637 显示驱动](./51_Driver_TM1637/)
- [51 TM1638 八位数码管 + 八按键 + 八LED 模块驱动](./51_Driver_TM1638/)
- [51 MAX7219 显示驱动](./51_Driver_max7219/)
- [51 24C64 EEPROM 驱动](./51_Driver_24c64_EEPROM/)
- [51 SSD13xx OLED 显示屏驱动 适用于Ssd1306 ssd1315 OLED显示屏 可自定义屏幕纵横](./51_Driver_SSD13xx/)
- [51 PCF8574 I2C 接口扩展器 驱动](./51_Driver_PCF8574/)
- [51 TLCx543(TLC1543/TLC2543) 10-12位ADC 驱动](./51_Driver_TLCx543_10-12bitADC/)
- [51 EC11 旋转编码器 驱动](./51__Driver_ec11_ENCODE/)
- [51 JLX12864G-086-PC 128x64 液晶屏驱动（UC1701X，串行接口；内置 5x7 ASCII 字模，可选模块字库 IC 显示汉字）](./51_Driver_12864G-086-PC/)

- [STM32 DHT11 温湿度传感器 驱动](./STM32_Driver_DHT11/)
- [STM32 TM1637 显示驱动](./STM32_Driver_TM1637/)
- [STM32 MAX7219 显示驱动](./STM32_Driver_max7219/)
- [STM32 JLX12864G-086-PC 128x64 液晶屏驱动](./STM32_Driver_12864G-086-PC/)

### 项目列表

- [51 18650 多通道充放电控制器](./51_18650_MultiChannelChargeDischargeController/)
- [51 四通道高压开关控制器](./51_high_voltage_isolator/)
- [51 简易周易抽签卡](./51_zhouyi/)
- [51 4v/6v铅酸电池充电放电控制器](./51_STC15w408as_4V~6vBatterChangerSimple/)
- [51 继电器数码管Wifi时钟](./51_esp8266RelayOclock_LCD/)
- [51 LCD1602 时钟项目](./51_LCD1602OClock/)
- [51 4-8位数码管显示项目](./51_digital_4-8tube/)
- [51 电阻穷举组合式假负载](./51_Rexhaustive_fakeload/)
- [51 自行车里程计](./51_Odometry/)
- [51 AS62256 32K 静态存储器读写](./51_AS62256/)
- [51 24Cxx EEPROM 拷贝机](./51_24Cxx_Copier/)
- [51 Capsa 电容 / ESR 测量仪](./51_Capsa/)
- [51 NFG 即时销毁存储卡（AS6C62256 掉电保持）](./51_NFG/)
- [51 PWMCtrol 多通道 PWM 控制器](./51_PWMCtrol/)
- [51 BLAKE2s 哈希算法](./51_Algo_BLAKE2s/)

- [STM32 峰值辅助仪](./STM32_Peak_Assistance/)
- [STM32 光伏简易气象站](./STM32_Weather_Monitoring_Node/)
- [STM32 项目模板](./STM32_Project/)
- [STM32 PDL 电源数据记录仪](./STM32_PDL/)
- [STM32 ADC 采样与串口上报练习](./STM32_lol/)

### 浏览量

![View](https://count.getloli.com/get/@:KeilMiscProject)
