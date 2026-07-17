@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b 1
if not exist "%~dp0bin" mkdir "%~dp0bin"
cl /nologo /std:c++14 /O2 /EHsc- /GR- /W4 /DUNICODE /D_UNICODE /LD /Brepro /Fo"%~dp0bin\\" ^
  "%~dp0jmv_launch_bridge.cpp" /link /OUT:"%~dp0bin\EIMapperEnduranceJmvBridge.asi" ^
  /PDB:"%~dp0bin\EIMapperEnduranceJmvBridge.pdb" /DEBUG:FULL /INCREMENTAL:NO advapi32.lib
exit /b %errorlevel%
