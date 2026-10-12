@echo off
setlocal
cd /d "%~dp0"
if exist "build" rmdir /S /Q "build"
if exist "build" exit /b 1
mkdir "build"
if errorlevel 1 exit /b 1
set "OUTPUT=build"
set "PRISMSYNC_BUILD_WORK=%TEMP%\PrismSync-build-%RANDOM%-%RANDOM%"
mkdir "%PRISMSYNC_BUILD_WORK%"
if errorlevel 1 exit /b 1
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
powershell -NoProfile -ExecutionPolicy Bypass -File build-version.ps1 -OutputDirectory "%PRISMSYNC_BUILD_WORK%"
if errorlevel 1 exit /b 1
rc /nologo /I "%PRISMSYNC_BUILD_WORK%" /fo "%PRISMSYNC_BUILD_WORK%\app.res" src\app.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /O2 /W3 /DUNICODE /D_UNICODE /I "%PRISMSYNC_BUILD_WORK%" /Fo:"%PRISMSYNC_BUILD_WORK%\main.obj" src\main.cpp "%PRISMSYNC_BUILD_WORK%\app.res" /Fe:"%OUTPUT%\PrismSync.exe" /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib comdlg32.lib shell32.lib ole32.lib shlwapi.lib wininet.lib
if errorlevel 1 exit /b 1
xcopy /E /I /Y "assets" "%OUTPUT%\assets" >nul
if errorlevel 1 exit /b 1
powershell -NoProfile -Command "$prismWork=[IO.Path]::GetFullPath($env:PRISMSYNC_BUILD_WORK); $prismTemp=[IO.Path]::GetFullPath($env:TEMP).TrimEnd([IO.Path]::DirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar; if(-not $prismWork.StartsWith($prismTemp,[StringComparison]::OrdinalIgnoreCase) -or -not [IO.Path]::GetFileName($prismWork).StartsWith('PrismSync-build-')) { throw 'Unsafe build cleanup path' }; Remove-Item -LiteralPath $prismWork -Recurse -Force"
if errorlevel 1 exit /b 1
echo Built: %OUTPUT%\PrismSync.exe