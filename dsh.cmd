@echo off
setlocal
"%~dp0bin\deepseek-harness.exe" --cli %*
exit /b %errorlevel%
