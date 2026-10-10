@echo off
cd /d "%~dp0"
if not exist "build\PrismSync.exe" (
  echo Application not built. Run !build.bat first.
  pause
  exit /b 1
)
start "" "build\PrismSync.exe"