@echo off
setlocal
"%~dp0bin\deepseek-harness.exe" --launcher-install-only
exit /b %errorlevel%
