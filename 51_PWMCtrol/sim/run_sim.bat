@echo off
chcp 65001 >nul
cd /d "%~dp0"

set "PY=python"
where python >nul 2>nul
if errorlevel 1 set "PY=D:\Python\Python311\python.exe"

echo ============================================
echo  PWMCTROL 下位机模拟器
echo  串口: COM1   关闭串口请改用 --port none
echo ============================================
"%PY%" pwm_sim.py --port COM1
if errorlevel 1 pause
