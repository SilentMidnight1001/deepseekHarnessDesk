@echo off
setlocal
"%~dp0bin\deepseek-harness.exe" %*
exit /b %errorlevel%
