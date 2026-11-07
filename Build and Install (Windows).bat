@echo off
rem  Builds UPFRONT on your PC and installs it. Needs Visual Studio 2022 (C++), CMake and Git.
cd /d "%~dp0"
where cmake >nul 2>&1 || (echo CMake not found. Install it from https://cmake.org/download & pause & exit /b 1)
echo Building UPFRONT (first build downloads JUCE, ~5-10 minutes)...
cmake -B build -G "Visual Studio 17 2022" -A x64 || (pause & exit /b 1)
cmake --build build --config Release --parallel || (pause & exit /b 1)
copy /Y installer\Install.bat build\UPFRONT_artefacts\Release\VST3\ >nul
call build\UPFRONT_artefacts\Release\VST3\Install.bat
