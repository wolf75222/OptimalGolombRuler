@echo off
REM Build script for OpenMP version (MSVC)

setlocal enabledelayedexpansion

REM Try to find Visual Studio
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
)

if defined VSDIR (
    call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
) else (
    echo Warning: Visual Studio not found, assuming cl.exe is in PATH
)

REM Create build directory
if not exist build mkdir build

REM Compile
echo Compiling OpenMP version...
cl.exe /std:c++20 /O2 /openmp /EHsc /DNDEBUG ^
       /I"include" ^
       src\golomb.cpp src\search.cpp src\main_openmp.cpp ^
       /Fe:build\golomb_openmp.exe ^
       /Fo:build\

if %errorlevel% equ 0 (
    echo.
    echo Build successful: build\golomb_openmp.exe
    echo Run with: build\golomb_openmp.exe
) else (
    echo Build failed.
    exit /b 1
)
