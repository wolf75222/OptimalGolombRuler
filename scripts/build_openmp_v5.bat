@echo off
REM Build script for OpenMP V5 version (uint64_t ops + prefix-based) - MSVC
REM Usage: build_openmp_v5.bat

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

REM Compile with maximum optimization flags + debug symbols for profiling
echo Compiling OpenMP V5 version (uint64_t ops + prefix-based + iterative)...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /openmp /EHsc /DNDEBUG ^
       /Zi /Fd:build\golomb_openmp_v5.pdb ^
       /I"include" ^
       src\search_v5.cpp src\main_openmp_v5.cpp ^
       /Fe:build\golomb_openmp_v5.exe ^
       /Fo:build\ ^
       /link /LTCG /DEBUG /OPT:REF /OPT:ICF

if %errorlevel% equ 0 (
    echo.
    echo Build successful: build\golomb_openmp_v5.exe
    echo Run with: build\golomb_openmp_v5.exe ^<n^> [prefix_depth]
) else (
    echo Build failed.
    exit /b 1
)
