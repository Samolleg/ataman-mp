@echo off
setlocal
if not defined EI_VS_DEV_CMD set "EI_VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist "%EI_VS_DEV_CMD%" set "EI_VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%EI_VS_DEV_CMD%" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b 1
if "%~1"=="" exit /b 64
cl /nologo /std:c++14 /EHsc /W4 /DUNICODE /D_UNICODE /Fo"%~1\portable_registry_tests.obj" /Fe"%~1\portable_registry_tests.exe" "%~dp0portable_registry_tests.cpp" /link advapi32.lib
exit /b %errorlevel%
