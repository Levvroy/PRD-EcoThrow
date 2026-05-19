@echo off
echo ============================================
echo   EcoThrow - Menjalankan Backend Server
echo ============================================
echo.

cd /d "%~dp0backend"

REM Aktifkan virtual environment
if exist "venv\Scripts\activate.bat" (
    call venv\Scripts\activate.bat
) else (
    echo [!] Virtual environment belum dibuat.
    echo [!] Jalankan dulu: python -m venv venv
    echo [!] Lalu: pip install -r requirements.txt
    pause
    exit /b
)

REM Install dependencies jika belum
echo [*] Mengecek dependencies...
pip install -r requirements.txt -q

echo.
echo [*] Backend berjalan di: http://localhost:8000
echo [*] API Docs:            http://localhost:8000/docs
echo [*] Frontend:            Buka file frontend/index.html di browser
echo.
echo [*] Tekan Ctrl+C untuk menghentikan server
echo.

uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
pause
