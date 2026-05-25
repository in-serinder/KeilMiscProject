# KeilMiscProject

[简体中文](./README_CN.md)/
[English](./README.md)

- [Brief Introduction](#brief-introduction)
- [Folder Naming Structure](#folder-naming-structure)
- [Current Project List](#current-project-list)
- [Driver List](#driver-list)
- [Project List](#project-list)

## Brief Introduction

Some completed Keil exercises (usually a single module or function, or _driver functions_ for some hardware) are placed in a folder as separate projects. Each folder contains the project files, including `.uvproj`, `.uvopt`, `.c` and `.h` files, and a `ReadMe.md` for a brief introduction. To use them, please clone this repository.

```
git clone https://github.com/in-serinder/KeilMiscProject.git
```

## Folder Naming Structure

In the project, the folder naming structure is:

```
MCU_Platform_[Driver_specifies_hardware_driver_function]_Project_Name
```

### Current Project List

### Driver List

- [51 TM1637 Display Driver](./51_Driver_TM1637/)
- [MAX7219 Display Driver](./51_Driver_max7219/)
- [51 24C64 EEPROM Driver](./51_Driver_24c64_EEPROM/)
- [51 SSD13xx OLED Display Driver for Ssd1306/ssd1315 OLED displays, customizable screen aspect ratio](./51_Driver_SSD13xx/)
- [51 PCF8574 I2C Expander Driver](./51_Driver_PCF8574/)

- [STM32 DHT11 Temperature and Humidity Sensor Driver](./STM32_Driver_DHT11/)

### Project List

- [51 18650 Multi-channel Charge/Discharge Controller](./51_18650bsmb/)
- [51 Four-channel High Voltage Switch Controller](./51_high_voltage_isolator/)
- [51 Simple Zhouyi Divination Card](./51_zhouyi/)
- [51 4V/6V Lead-acid Battery Charge/Discharge Controller](./51_STC15w408as_4V~6vBatterChangerSimple/)
- [51 Relay LED Wifi Clock](./51_esp8266RelayOclock_LCD/)
- [51 LCD1602 Clock Project](./51_LCD1602OClock/)
- [51 4-8 Digit LED Display Project](./51_digital_4-8tube/)
- [STM32 Project Template](./STM32_Project/)
