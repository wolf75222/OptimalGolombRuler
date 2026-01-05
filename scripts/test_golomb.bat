@echo off
REM =============================================================================
REM Test script for Golomb Ruler - Windows
REM =============================================================================
REM Usage:
REM   test_golomb.bat seq 10        - Run sequential for n=10
REM   test_golomb.bat omp 11        - Run OpenMP for n=11
REM   test_golomb.bat mpi 12        - Run MPI+OpenMP for n=12 (2 processes)
REM   test_golomb.bat mpi 12 4      - Run MPI+OpenMP for n=12 (4 processes)
REM =============================================================================

setlocal enabledelayedexpansion

REM Check arguments
if "%1"=="" (
    echo Usage: test_golomb.bat ^<version^> ^<n^> [mpi_procs]
    echo.
    echo Versions:
    echo   seq  - Sequential
    echo   omp  - OpenMP
    echo   mpi  - MPI+OpenMP
    echo.
    echo Examples:
    echo   test_golomb.bat seq 10
    echo   test_golomb.bat omp 11
    echo   test_golomb.bat mpi 12 4
    exit /b 1
)

if "%2"=="" (
    echo ERROR: Missing n parameter
    echo Usage: test_golomb.bat ^<version^> ^<n^> [mpi_procs]
    exit /b 1
)

set "VERSION=%1"
set "N=%2"
set "MPI_PROCS=%3"
if "%MPI_PROCS%"=="" set "MPI_PROCS=2"

REM Get script directory and project root
set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."

REM Try to find Visual Studio
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
)

if defined VSDIR (
    call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
)

REM Build and run based on version
if /i "%VERSION%"=="seq" goto :run_seq
if /i "%VERSION%"=="omp" goto :run_omp
if /i "%VERSION%"=="mpi" goto :run_mpi

echo ERROR: Unknown version "%VERSION%"
echo Valid versions: seq, omp, mpi
exit /b 1

:run_seq
echo.
echo ============================================
echo  SEQUENTIAL - Golomb n=%N%
echo ============================================
echo.

REM Build if needed
if not exist "%PROJECT_DIR%\build\golomb_sequential.exe" (
    echo Building sequential version...
    call "%PROJECT_DIR%\scripts\build_sequential.bat"
    if errorlevel 1 exit /b 1
)

echo Running...
echo.
powershell -Command "& { $sw = [Diagnostics.Stopwatch]::StartNew(); & '%PROJECT_DIR%\build\golomb_sequential.exe' %N%; $sw.Stop(); Write-Host ''; Write-Host ('Total time: ' + $sw.Elapsed.TotalSeconds.ToString('F3') + ' seconds') -ForegroundColor Green }"
goto :end

:run_omp
echo.
echo ============================================
echo  OPENMP - Golomb n=%N%
echo ============================================
echo.

REM Build if needed
if not exist "%PROJECT_DIR%\build\golomb_openmp.exe" (
    echo Building OpenMP version...
    call "%PROJECT_DIR%\scripts\build_openmp.bat"
    if errorlevel 1 exit /b 1
)

echo Running...
echo.
powershell -Command "& { $sw = [Diagnostics.Stopwatch]::StartNew(); & '%PROJECT_DIR%\build\golomb_openmp.exe' %N%; $sw.Stop(); Write-Host ''; Write-Host ('Total time: ' + $sw.Elapsed.TotalSeconds.ToString('F3') + ' seconds') -ForegroundColor Green }"
goto :end

:run_mpi
echo.
echo ============================================
echo  MPI+OPENMP - Golomb n=%N% (MPI=%MPI_PROCS%)
echo ============================================
echo.

REM Build if needed
if not exist "%PROJECT_DIR%\build\golomb_mpi.exe" (
    echo Building MPI version...
    call "%PROJECT_DIR%\scripts\build_mpi.bat"
    if errorlevel 1 exit /b 1
)

REM Check for mpiexec
where mpiexec >nul 2>&1
if errorlevel 1 (
    echo ERROR: mpiexec not found. Install MS-MPI Runtime.
    exit /b 1
)

echo Running with %MPI_PROCS% MPI processes...
echo.
powershell -Command "& { $sw = [Diagnostics.Stopwatch]::StartNew(); & mpiexec -n %MPI_PROCS% '%PROJECT_DIR%\build\golomb_mpi.exe' %N%; $sw.Stop(); Write-Host ''; Write-Host ('Total time: ' + $sw.Elapsed.TotalSeconds.ToString('F3') + ' seconds') -ForegroundColor Green }"
goto :end

:end
echo.
