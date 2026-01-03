@echo off
REM Build script for MPI+OpenMP version (MSVC + MS-MPI)
REM Usage:
REM   build_mpi.bat        - Build PROD version (full benchmark)
REM   build_mpi.bat dev    - Build DEV version (reduced sizes for quick testing)

setlocal enabledelayedexpansion

REM Check for DEV mode
set "DEV_FLAG="
set "EXE_NAME=golomb_mpi.exe"
set "MODE_MSG=PROD"
if /i "%1"=="dev" (
    set "DEV_FLAG=/DDEV_MODE"
    set "EXE_NAME=golomb_mpi_dev.exe"
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

REM MS-MPI paths
set MPI_INCLUDE=C:\Program Files (x86)\Microsoft SDKs\MPI\Include
set MPI_LIB=C:\Program Files (x86)\Microsoft SDKs\MPI\Lib\x64

if not exist "%MPI_INCLUDE%" (
    echo ERROR: MS-MPI SDK not found.
    echo Download from: https://www.microsoft.com/en-us/download/details.aspx?id=105289
    exit /b 1
)

REM Create build directory
if not exist build mkdir build

REM Compile with maximum optimization flags
echo Compiling MPI+OpenMP version [%MODE_MSG%]...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /openmp /EHsc /DNDEBUG %DEV_FLAG% ^
       /I"include" /I"%MPI_INCLUDE%" ^
       src\search_mpi.cpp src\main_mpi.cpp ^
       /Fe:build\%EXE_NAME% ^
       /Fo:build\ ^
       /link /LTCG "%MPI_LIB%\msmpi.lib"

if %errorlevel% equ 0 (
    echo.
    echo Build successful: build\%EXE_NAME% [%MODE_MSG% mode]
    echo.
    echo Run with:
    echo   mpiexec -n 2 build\%EXE_NAME%
    echo   mpiexec -n 4 build\%EXE_NAME%
) else (
    echo Build failed.
    exit /b 1
)
