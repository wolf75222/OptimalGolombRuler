@echo off
REM Build script for Sequential benchmark (MSVC)
REM Pure sequential version - no OpenMP dependency
REM
REM Usage:
REM   build_sequential.bat        - Build PROD version (full benchmark)
REM   build_sequential.bat dev    - Build DEV version (quick testing)

setlocal enabledelayedexpansion

REM Check for DEV mode
set "DEV_FLAG="
set "EXE_NAME=golomb_sequential.exe"
set "MODE_MSG=PROD"
if /i "%1"=="dev" (
    set "DEV_FLAG=/DDEV_MODE"
    set "EXE_NAME=golomb_sequential_dev.exe"
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

echo.
echo ================================================================
echo   GOLOMB RULER - SEQUENTIAL BENCHMARK BUILD
echo ================================================================
echo Mode: %MODE_MSG%
echo.
echo CSAPP Optimizations applied:
echo   - Iterative backtracking (no recursion overhead)
echo   - Loop unrolling 4x for ILP
echo   - Shift bits: ^>^> 6, ^& 63 for fast bit access
echo   - Direct bit manipulation (no std::bitset)
echo   - Stack-allocated arrays (no heap)
echo   - Cache-line alignment (64 bytes)
echo   - Fail-fast with [[likely]]/[[unlikely]]
echo.
echo Compiler flags:
echo   /O2        - Maximum speed optimization
echo   /Oi        - Intrinsic functions
echo   /Ot        - Favor fast code
echo   /GL        - Whole program optimization
echo   /arch:AVX2 - Advanced Vector Extensions
echo   /fp:fast   - Fast floating point
echo   /LTCG      - Link-time code generation
echo ================================================================
echo.

REM Compile pure sequential version (no OpenMP)
echo Compiling Sequential benchmark [%MODE_MSG%]...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /arch:AVX2 /fp:fast /EHsc /DNDEBUG %DEV_FLAG% ^
       /I"include" ^
       src\search_sequential.cpp src\main_sequential.cpp ^
       /Fe:build\%EXE_NAME% ^
       /Fo:build\ ^
       /link /LTCG

if %errorlevel% equ 0 (
    echo.
    echo ================================================================
    echo Build successful: build\%EXE_NAME% [%MODE_MSG% mode]
    echo.
    echo Run with: build\%EXE_NAME%
    echo ================================================================
) else (
    echo.
    echo Build failed.
    exit /b 1
)
