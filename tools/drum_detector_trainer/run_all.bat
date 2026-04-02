@echo off
setlocal enabledelayedexpansion
title Drum Detector Trainer

echo.
echo  ================================================================
echo   Drum Detector Trainer
echo   RTX 4090  ^|  E-GMD 10%%  ^|  Causal CNN  ^|  Kick/Snare/Hi-hat
echo  ================================================================
echo.
echo  Estimated total time  (full E-GMD, 444 h, 43 drum kits):
echo    Download zip ~2-4 h   (one 90 GB file, resumes if interrupted)
echo    Extract      ~10 min  (zip auto-deleted after, net disk = 132 GB)
echo    Preprocess   ~90 min  (mel spectrograms + MIDI labels, 8 threads)
echo    Train        ~90 min  (30 epochs on RTX 4090)
echo    Export       ^< 1 min  (ONNX + normalization.json)
echo    ──────────────────────────────────────────────────────────────────
echo    Total        ~4-7 h   ^|  Peak disk: ~222 GB  ^|  Final: ~145 GB
echo.
echo  Output files:
echo    models\drum_detector.onnx     ^<-- copy this to the plugin
echo    models\normalization.json     ^<-- copy this to the plugin
echo.
pause

REM ── Activate environment ─────────────────────────────────────────────────────
call conda activate drum_trainer
if errorlevel 1 (
    echo [ERROR] Could not activate 'drum_trainer' conda environment.
    echo  Run setup.bat first.
    pause
    exit /b 1
)

REM ── Step 1: Download ─────────────────────────────────────────────────────────
echo.
echo  ╔══════════════════════════════════════════╗
echo  ║  Step 1 / 3  -  Download E-GMD 10%%      ║
echo  ╚══════════════════════════════════════════╝
echo.
python download.py
if errorlevel 1 (
    echo.
    echo [ERROR] Download failed. Check your internet connection and retry.
    pause
    exit /b 1
)

REM ── Step 2: Preprocess ───────────────────────────────────────────────────────
echo.
echo  ╔══════════════════════════════════════════╗
echo  ║  Step 2 / 3  -  Preprocess audio         ║
echo  ╚══════════════════════════════════════════╝
echo.
python preprocess.py
if errorlevel 1 (
    echo.
    echo [ERROR] Preprocessing failed.
    pause
    exit /b 1
)

REM ── Step 3: Train + Export ───────────────────────────────────────────────────
echo.
echo  ╔══════════════════════════════════════════╗
echo  ║  Step 3 / 3  -  Train + Export ONNX      ║
echo  ╚══════════════════════════════════════════╝
echo.
python train_export.py
if errorlevel 1 (
    echo.
    echo [ERROR] Training or ONNX export failed.
    pause
    exit /b 1
)

echo.
echo  ================================================================
echo   All done!
echo.
echo   Copy these two files to the plugin's resources folder:
echo     models\drum_detector.onnx
echo     models\normalization.json
echo  ================================================================
echo.
pause
