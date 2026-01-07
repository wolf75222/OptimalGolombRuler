@echo off
REM Build script for Sequential V2 DEBUG version (for profiling with Sleepy)
REM Includes debug symbols + optimizations for accurate profiling
REM
REM Usage: build_sequential_v2_debug.bat

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

echo.
echo ================================================================
echo   GOLOMB RULER - SEQUENTIAL V2 DEBUG BUILD (for Sleepy)
echo ================================================================
echo.
echo Compiler flags:
echo   /O2        - Optimizations ON (for realistic profiling)
echo   /Zi        - Debug info (PDB file)
echo   /DEBUG     - Full debug info in linker
echo   /Oy-       - Keep frame pointers (better stack traces)
echo ================================================================
echo.

REM Compile with optimizations + debug symbols
echo Compiling Sequential V2 DEBUG version...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /arch:AVX2 /EHsc /DNDEBUG ^
       /Zi /Oy- ^
       /Fd:build\golomb_sequential_v2_debug.pdb ^
       /I"include" ^
       src\search_sequential_v2.cpp src\main_sequential_v2.cpp ^
       /Fe:build\golomb_sequential_v2_debug.exe ^
       /Fo:build\ ^
       /link /LTCG /DEBUG /OPT:REF /OPT:ICF

if %errorlevel% equ 0 (
    echo.
    echo ================================================================
    echo Build successful: build\golomb_sequential_v2_debug.exe
    echo PDB file: build\golomb_sequential_v2_debug.pdb
    echo.
    echo Run with Sleepy:
    echo   1. Open Very Sleepy
    echo   2. File ^> New ^> build\golomb_sequential_v2_debug.exe 12
    echo   3. Profile!
    echo ================================================================
) else (
    echo.
    echo Build failed.
    exit /b 1
)
