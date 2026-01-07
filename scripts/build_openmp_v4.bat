@echo off
REM Build script for OpenMP V4 version (prefix-based + iterative + bitset shift) - MSVC
REM Usage: build_openmp_v4.bat

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

REM Compile with maximum optimization flags
echo Compiling OpenMP V4 version (prefix-based + iterative + bitset shift)...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /openmp /EHsc /DNDEBUG ^
       /I"include" ^
       src\search_v4.cpp src\main_openmp_v4.cpp ^
       /Fe:build\golomb_openmp_v4.exe ^
       /Fo:build\ ^
       /link /LTCG

if %errorlevel% equ 0 (
    echo.
    echo Build successful: build\golomb_openmp_v4.exe
    echo Run with: build\golomb_openmp_v4.exe ^<n^> [prefix_depth]
) else (
    echo Build failed.
    exit /b 1
)
