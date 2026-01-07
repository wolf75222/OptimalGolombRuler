@echo off
REM Run benchmark comparing V1 vs V2 algorithms
REM Usage: run_benchmark.bat [n1 n2 n3 ...]

setlocal EnableDelayedExpansion

echo =============================================================
echo   Golomb Ruler Benchmark: V1 vs V2
echo =============================================================

REM Check if executable exists
if not exist build\golomb_compare.exe (
    echo Executable not found. Building...
    call scripts\build_compare.bat
    if errorlevel 1 (
        echo Build failed!
        exit /b 1
    )
)

REM Create benchmarks directory if needed
if not exist benchmarks mkdir benchmarks

echo.
echo Running benchmark...
echo.

REM Pass any arguments to the executable
if "%~1"=="" (
    build\golomb_compare.exe
) else (
    build\golomb_compare.exe %*
)

echo.
echo =============================================================
echo   Benchmark complete!
echo   Results saved to: benchmarks\compare_v1_v2.csv
echo =============================================================

endlocal
