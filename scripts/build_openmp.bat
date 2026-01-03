@echo off
REM Build script for OpenMP version (MSVC)
REM Usage:
REM   build_openmp.bat        - Build PROD version (full benchmark)
REM   build_openmp.bat dev    - Build DEV version (reduced sizes for quick testing)

setlocal enabledelayedexpansion

REM Check for DEV mode
set "DEV_FLAG="
set "EXE_NAME=golomb_openmp.exe"
set "MODE_MSG=PROD"
if /i "%1"=="dev" (
    set "DEV_FLAG=/DDEV_MODE"
    set "EXE_NAME=golomb_openmp_dev.exe"
    set "MODE_MSG=DEV"
)

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

REM Compile with maximum optimization flags
echo Compiling OpenMP version [%MODE_MSG%]...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /openmp /EHsc /DNDEBUG %DEV_FLAG% ^
       /I"include" ^
       src\search.cpp src\main_openmp.cpp ^
       /Fe:build\%EXE_NAME% ^
       /Fo:build\ ^
       /link /LTCG

if %errorlevel% equ 0 (
    echo.
    echo Build successful: build\%EXE_NAME% [%MODE_MSG% mode]
    echo Run with: build\%EXE_NAME%
) else (
    echo Build failed.
    exit /b 1
)
