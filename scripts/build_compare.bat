@echo off
REM Build script for V1 vs V2 vs V3 comparison benchmark (Windows/MSVC)
REM Usage: build_compare.bat

setlocal EnableDelayedExpansion

echo =============================================================
echo   Building Golomb Ruler V1 vs V2 vs V3 Comparison Benchmark
echo =============================================================

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
echo Compiling search.cpp (V1)...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /openmp /EHsc /DNDEBUG ^
    /I"include" /c src\search.cpp /Fo:build\cmp_search.obj
if errorlevel 1 (
    echo ERROR: Failed to compile search.cpp
    exit /b 1
)

echo Compiling search_v2.cpp (V2)...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /openmp /EHsc /DNDEBUG ^
    /I"include" /c src\search_v2.cpp /Fo:build\cmp_search_v2.obj
if errorlevel 1 (
    echo ERROR: Failed to compile search_v2.cpp
    exit /b 1
)

echo Compiling search_v3.cpp (V3 Hybrid)...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /openmp /EHsc /DNDEBUG ^
    /I"include" /c src\search_v3.cpp /Fo:build\cmp_search_v3.obj
if errorlevel 1 (
    echo ERROR: Failed to compile search_v3.cpp
    exit /b 1
)

echo Compiling main_benchmark_compare.cpp...
cl.exe /std:c++20 /O2 /Oi /Ot /GL /openmp /EHsc /DNDEBUG ^
    /I"include" /c src\main_benchmark_compare.cpp /Fo:build\cmp_main.obj
if errorlevel 1 (
    echo ERROR: Failed to compile main_benchmark_compare.cpp
    exit /b 1
)

echo Linking...
cl.exe /Fe:build\golomb_compare.exe ^
    build\cmp_search.obj build\cmp_search_v2.obj build\cmp_search_v3.obj build\cmp_main.obj ^
    /link /LTCG /openmp
if errorlevel 1 (
    echo ERROR: Failed to link
    exit /b 1
)

echo.
echo =============================================================
echo   Build successful: build\golomb_compare.exe
echo =============================================================
echo.
echo Usage: build\golomb_compare.exe [n1 n2 n3 ...]
echo Default: runs n=10,11,12,13,14
echo.

endlocal
