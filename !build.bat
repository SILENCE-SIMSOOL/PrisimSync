@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
where cl >nul 2>nul
if errorlevel 1 (
  for %%V in ("%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe" "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe") do (
    if exist %%~V (
      for /f "usebackq tokens=*" %%P in (`"%%~V" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%P\VC\Auxiliary\Build\vcvars64.bat" call "%%P\VC\Auxiliary\Build\vcvars64.bat" >nul
      )
    )
  )
)
where cl >nul 2>nul
if errorlevel 1 (
  echo [ERROR] MSVC build tools not found. Install Desktop development with C++.
  exit /b 1
)
rc /nologo /fo build\app.res src\app.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /O2 /W3 /DUNICODE /D_UNICODE src\main.cpp build\app.res /Fe:build\PrismSync.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib comdlg32.lib shell32.lib ole32.lib shlwapi.lib urlmon.lib wininet.lib
if errorlevel 1 exit /b 1
if exist build\assets rd /s /q build\assets
xcopy /E /I /Y assets build\assets >nul
if errorlevel 1 exit /b 1
echo Built: build\PrismSync.exe