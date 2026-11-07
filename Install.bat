@echo off
rem  Installs UPFRONT.vst3 (in this folder) into the standard VST3 folder.
net session >nul 2>&1 || (powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs" & exit /b)
set "DEST=%CommonProgramFiles%\VST3"
if not exist "%DEST%" mkdir "%DEST%"
xcopy /E /I /Y /Q "%~dp0UPFRONT.vst3" "%DEST%\UPFRONT.vst3" >nul
if errorlevel 1 (echo Could not copy UPFRONT.vst3 - make sure it is next to this file.& pause & exit /b 1)
echo.
echo  UPFRONT installed to %DEST%\UPFRONT.vst3
echo.
echo  In FL Studio: Options ^> Manage plugins ^> Find installed plugins
echo.
pause
