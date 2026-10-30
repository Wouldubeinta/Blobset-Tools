@echo off
rem Builds the Blobset Tools installer. Double-click it, or run it from any prompt (extra options are passed on,
rem e.g. build_installer.bat -QtBin "D:\Qt\6.11.2\mingw_64\bin").
rem Windows blocks .ps1 scripts by default; -ExecutionPolicy Bypass only applies to this one run.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_installer.ps1" %*
set RESULT=%ERRORLEVEL%
rem Keep the window open when started by double-click.
echo %CMDCMDLINE% | find /i "%~nx0" >nul && pause
exit /b %RESULT%
