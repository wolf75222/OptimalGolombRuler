@echo off
REM Build script for OpenMP V6 version (Branchless + prefix-based) - MSVC
REM Optimized for both Intel and AMD with branchless 128-bit operations
REM Usage: build_openmp_v6.bat

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

REM Compile with maximum optimization flags + branchless ops + debug symbols
echo Compiling OpenMP V6 version (Branchless + prefix-based + iterative)...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /openmp /EHsc /DNDEBUG ^
       /arch:AVX2 ^
       /Zi /Fd:build\golomb_openmp_v6.pdb ^
       /I"include" ^
       src\search_v6.cpp src\main_openmp_v6.cpp ^
       /Fe:build\golomb_openmp_v6.exe ^
       /Fo:build\ ^
       /link /LTCG /DEBUG /OPT:REF /OPT:ICF

if %errorlevel% equ 0 (
    echo.
    echo Build successful: build\golomb_openmp_v6.exe
    echo Run with: build\golomb_openmp_v6.exe ^<n^> [prefix_depth]
) else (
    echo Build failed.
    exit /b 1
)
