@echo off
setlocal enabledelayedexpansion

:: =============================================================================
:: Build script for Sequential Golomb Ruler (Windows/MSVC)
:: Usage: build_sequential.bat [v2|v3|v4] [n] [--fast]
::   v2/v3/v4 : version to build (default: v4)
::   n        : Golomb ruler size to compute (optional)
::   --fast   : Use known optimal as initial bound (v4 only)
:: Examples:
::   build_sequential.bat v4 12         - Build v4 and compute Golomb(12)
::   build_sequential.bat v4 12 --fast  - Build v4 with optimal bound (fast)
::   build_sequential.bat v3 12         - Build v3 and compute Golomb(12)
::   build_sequential.bat               - Build v4 and run full benchmark
:: =============================================================================

:: Default values
set VERSION=v4
set GOLOMB_N=
set FAST_FLAG=

:: Parse arguments
:parse_args
if "%1"=="" goto end_parse
if /i "%1"=="v2" (
    set VERSION=v2
    shift
    goto parse_args
)
if /i "%1"=="v3" (
    set VERSION=v3
    shift
    goto parse_args
)
if /i "%1"=="v4" (
    set VERSION=v4
    shift
    goto parse_args
)
if /i "%1"=="--fast" (
    set FAST_FLAG=--fast
    shift
    goto parse_args
)
if /i "%1"=="-f" (
    set FAST_FLAG=--fast
    shift
    goto parse_args
)
:: Assume it's a number for n
set GOLOMB_N=%1
shift
goto parse_args

:end_parse

echo.
echo ============================================================
echo   Building Sequential %VERSION% for Windows (MSVC)
echo ============================================================
echo.

:: Find Visual Studio installation
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Is Visual Studio installed?
    exit /b 1
)

:: Get VS installation path
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo ERROR: Visual Studio with C++ tools not found.
    exit /b 1
)

echo Found Visual Studio at: %VS_PATH%

:: Setup environment
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to setup Visual Studio environment.
    exit /b 1
)

:: Create build directory
if not exist build mkdir build

:: Set source files based on version
if "%VERSION%"=="v2" (
    set SRCS=src\search_sequential_v2.cpp src\main_sequential_v2.cpp
    set OUTPUT=build\golomb_sequential_v2.exe
) else if "%VERSION%"=="v3" (
    set SRCS=src\search_sequential_v3.cpp src\main_sequential_v3.cpp
    set OUTPUT=build\golomb_sequential_v3.exe
) else (
    set SRCS=src\search_sequential_v4.cpp src\main_sequential_v4.cpp
    set OUTPUT=build\golomb_sequential_v4.exe
)

:: Compiler flags
set CXXFLAGS=/O2 /GL /arch:AVX2 /EHsc /std:c++20 /DNDEBUG /Iinclude /W3

:: Linker flags
set LDFLAGS=/LTCG

echo Compiling %VERSION%...
echo Sources: %SRCS%
echo.

cl.exe %CXXFLAGS% %SRCS% /Fe:%OUTPUT% /link %LDFLAGS%

if errorlevel 1 (
    echo.
    echo ERROR: Compilation failed!
    exit /b 1
)

echo.
echo ============================================================
echo   Build successful: %OUTPUT%
echo ============================================================
echo.

:: Run the executable
if defined GOLOMB_N (
    echo Running: %OUTPUT% %GOLOMB_N% %FAST_FLAG%
    echo.
    %OUTPUT% %GOLOMB_N% %FAST_FLAG%
) else (
    echo Running: %OUTPUT% %FAST_FLAG%
    echo.
    %OUTPUT% %FAST_FLAG%
)

endlocal
