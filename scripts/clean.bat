@echo off
REM Clean build artifacts

if exist build (
    rmdir /s /q build
    echo Build directory cleaned.
) else (
    echo Nothing to clean.
)
