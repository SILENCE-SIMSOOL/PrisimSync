@echo off
setlocal
cd /d "%~dp0"

if not exist "build\PrismSync.exe" (
	echo [INFO] Building PrismSync.exe first...
	call "!build.bat"
	if errorlevel 1 exit /b 1
)

set "ISCC="
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if exist "C:\Program Files\Inno Setup 6\ISCC.exe" set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"

if defined ISCC goto :found
where iscc >nul 2>nul
if %errorlevel% equ 0 (
	set "ISCC=iscc"
	goto :found
)

echo [ERROR] Inno Setup 6 (ISCC.exe) not found.
exit /b 1

:found
echo [INFO] Compiling installer...
"%ISCC%" "installer.iss"
if errorlevel 1 exit /b 1

echo [SUCCESS] Installer generated:
dir /b "build\*Setup*.exe"