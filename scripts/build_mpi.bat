@echo off
REM Build script for MPI+OpenMP version (MSVC + MS-MPI)

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

REM Compile
echo Compiling MPI+OpenMP version...
cl.exe /std:c++20 /O2 /openmp /EHsc /DNDEBUG ^
       /I"include" /I"%MPI_INCLUDE%" ^
       src\golomb.cpp src\search_mpi.cpp src\main_mpi.cpp ^
       /Fe:build\golomb_mpi.exe ^
       /Fo:build\ ^
       /link "%MPI_LIB%\msmpi.lib"

if %errorlevel% equ 0 (
    echo.
    echo Build successful: build\golomb_mpi.exe
    echo.
    echo Run with:
    echo   mpiexec -n 4 build\golomb_mpi.exe
    echo   mpiexec -n 8 build\golomb_mpi.exe
) else (
    echo Build failed.
    exit /b 1
)
