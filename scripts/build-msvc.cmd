@echo off
setlocal
if "%~1"=="" (
  echo Usage: build-msvc.cmd BUILD_DIRECTORY
  exit /b 64
)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
set "ROOT=%~dp0.."
set "BUILD=%~f1"
if "%~2"=="" (set "QT_ROOT=%ROOT%\.tooling\Qt\5.12.12\msvc2017_64") else (set "QT_ROOT=%~f2")
if not exist "%BUILD%" mkdir "%BUILD%"
pushd "%BUILD%"
"%QT_ROOT%\bin\qmake.exe" "%ROOT%\ei_maper.pro" "CONFIG+=release"
if errorlevel 1 (popd & exit /b 1)
nmake /NOLOGO
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
