@echo off
REM Build script for Sequential V2 benchmark (MSVC)
REM BitSet128 shift-based optimization from V5
REM
REM Usage:
REM   build_sequential_v2.bat        - Build PROD version
REM   build_sequential_v2.bat dev    - Build DEV version

setlocal enabledelayedexpansion

REM Check for DEV mode
set "DEV_FLAG="
set "EXE_NAME=golomb_sequential_v2.exe"
set "MODE_MSG=PROD"
if /i "%1"=="dev" (
    set "DEV_FLAG=/DDEV_MODE"
    set "EXE_NAME=golomb_sequential_v2_dev.exe"
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
echo   GOLOMB RULER - SEQUENTIAL V2 BENCHMARK BUILD
echo ================================================================
echo Mode: %MODE_MSG%
echo.
echo V2 Optimizations (from V5):
echo   - BitSet128 (2x uint64_t) for marks and diffs
echo   - O(1) collision detection via shift operation
echo   - No marks array copy on push
echo   - reversed_marks ^<^< offset computes all diffs
echo   - Cache-line aligned structures (64 bytes)
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

REM Compile Sequential V2 version
echo Compiling Sequential V2 benchmark [%MODE_MSG%]...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /arch:AVX2 /fp:fast /EHsc /DNDEBUG %DEV_FLAG% ^
       /I"include" ^
       src\search_sequential_v2.cpp src\main_sequential_v2.cpp ^
       /Fe:build\%EXE_NAME% ^
       /Fo:build\ ^
       /link /LTCG

if %errorlevel% equ 0 (
    echo.
    echo ================================================================
    echo Build successful: build\%EXE_NAME% [%MODE_MSG% mode]
    echo.
    echo Run with: build\%EXE_NAME% ^<n^>
    echo   Example: build\%EXE_NAME% 12
    echo ================================================================
) else (
    echo.
    echo Build failed.
    exit /b 1
)
