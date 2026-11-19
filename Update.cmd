@echo off
chcp 65001 >nul
title Обновление SamplerMode
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update.ps1"
echo.
pause
