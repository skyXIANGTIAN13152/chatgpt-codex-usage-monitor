@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\preview.ps1" %*
