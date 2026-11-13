@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo PDL 下位机模拟器
python run_sim.py %*
if errorlevel 1 pause
