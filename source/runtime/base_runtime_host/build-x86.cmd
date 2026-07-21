@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b 1
if "%EI_RUNTIME_HOST_OUT_DIR%"=="" (set "OUT_DIR=%~dp0bin") else (set "OUT_DIR=%EI_RUNTIME_HOST_OUT_DIR%")
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
cl /nologo /std:c++14 /O2 /EHsc /W4 /DUNICODE /D_UNICODE /Brepro /Fo"%OUT_DIR%\\" ^
  "%~dp0base_runtime_host.cpp" /link /SUBSYSTEM:CONSOLE ^
  /OUT:"%OUT_DIR%\EIMapperEnduranceRuntimeHost.exe" ^
  /PDB:"%OUT_DIR%\EIMapperEnduranceRuntimeHost.pdb" /DEBUG:FULL /INCREMENTAL:NO
exit /b %errorlevel%
