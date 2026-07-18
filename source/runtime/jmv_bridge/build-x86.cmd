@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b 1
if "%EI_BRIDGE_OUT_DIR%"=="" (set "OUT_DIR=%~dp0bin") else (set "OUT_DIR=%EI_BRIDGE_OUT_DIR%")
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
cl /nologo /std:c++14 /O2 /Oy- /EHsc- /GR- /W4 /DUNICODE /D_UNICODE /LD /Brepro /Fo"%OUT_DIR%\\" ^
  "%~dp0jmv_launch_bridge.cpp" /link /OUT:"%OUT_DIR%\EIMapperEnduranceJmvBridge.plg" ^
  /PDB:"%OUT_DIR%\EIMapperEnduranceJmvBridge.pdb" /IMPLIB:"%OUT_DIR%\jmv_launch_bridge.lib" ^
  /DEBUG:FULL /INCREMENTAL:NO advapi32.lib user32.lib
exit /b %errorlevel%
