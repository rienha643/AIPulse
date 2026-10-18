@echo off
setlocal
title AI Pulse Update
set "AIPULSE_PWSH="
if exist "%~dp0powershell-path.local.txt" set /p AIPULSE_PWSH=<"%~dp0powershell-path.local.txt"
if defined AIPULSE_PWSH if exist "%AIPULSE_PWSH%" goto launch
set "AIPULSE_PWSH=%ProgramFiles%\PowerShell\7\pwsh.exe"
if exist "%AIPULSE_PWSH%" goto launch
where pwsh.exe >nul 2>&1
if errorlevel 1 (
    echo [FAILED] PowerShell 7 was not found. No update was performed.
    pause
    exit /b 1
)
set "AIPULSE_PWSH=pwsh.exe"
:launch
"%AIPULSE_PWSH%" -NoLogo -NoProfile -File "%~dp0update-click.ps1"
set "AIPULSE_UPDATE_EXIT=%ERRORLEVEL%"
echo.
if "%AIPULSE_UPDATE_EXIT%"=="0" (
    echo [SUCCESS] AI Pulse update completed.
) else if "%AIPULSE_UPDATE_EXIT%"=="2" (
    echo [WARNING] Update completed with changes. Check the result above.
) else (
    echo [FAILED] AI Pulse update failed. Check the error above and reports folder.
)
echo.
if /i "%~1"=="--no-pause" goto finished
pause
:finished
exit /b %AIPULSE_UPDATE_EXIT%
