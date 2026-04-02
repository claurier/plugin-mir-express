@echo off
setlocal enabledelayedexpansion
title Drum Detector Trainer - Setup

echo.
echo  ================================================================
echo   Drum Detector Trainer  -  One-time setup
echo   Requires: Miniconda or Anaconda, NVIDIA RTX 4090 driver ^>=528
echo  ================================================================
echo.

REM ── Check conda ──────────────────────────────────────────────────────────────
where conda >nul 2>&1
if errorlevel 1 (
    echo [ERROR] conda not found.
    echo.
    echo  Install Miniconda first:
    echo    https://docs.conda.io/en/latest/miniconda.html
    echo  Then re-open a fresh Anaconda Prompt and run setup.bat again.
    pause
    exit /b 1
)

echo [1/3] Creating conda environment  ^(drum_trainer, Python 3.11^)...
conda create -n drum_trainer python=3.11 -y
if errorlevel 1 ( echo [ERROR] conda create failed. & pause & exit /b 1 )

echo.
echo [2/3] Installing PyTorch 2.x with CUDA 12.1  ^(RTX 4090^)...
call conda activate drum_trainer
pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu121 --quiet
if errorlevel 1 ( echo [ERROR] PyTorch install failed. & pause & exit /b 1 )

echo.
echo [3/3] Installing remaining dependencies...
pip install -r requirements.txt --quiet
if errorlevel 1 ( echo [ERROR] pip install failed. & pause & exit /b 1 )

echo.
echo  ================================================================
echo   Setup complete!
echo.
echo   Next step:  double-click  run_all.bat
echo  ================================================================
echo.
pause
