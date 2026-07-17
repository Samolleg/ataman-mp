@echo off
set "ROOT=%~dp0.."
:: copy NVIDIA texture tools to app dir
xcopy /y "%ROOT%\nvtt" "%ROOT%\bin\debug\nvtt\" /E /Q
xcopy /y "%ROOT%\nvtt" "%ROOT%\bin\release\nvtt\" /E /Q
:: copy runtime icons to app dir
xcopy /y "%ROOT%\assets\icons\fluent" "%ROOT%\bin\debug\icons_fluent\" /E /Q
xcopy /y "%ROOT%\assets\icons\fluent" "%ROOT%\bin\release\icons_fluent\" /E /Q
