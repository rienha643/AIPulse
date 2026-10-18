$ErrorActionPreference='Stop'
$shell=New-Object -ComObject WScript.Shell
$shortcut=$shell.CreateShortcut((Join-Path $PSScriptRoot 'AI Pulse Update.lnk'))
$shortcut.TargetPath=Join-Path $env:WINDIR 'System32/cmd.exe'
$shortcut.Arguments='/d /c ""'+(Join-Path $PSScriptRoot 'update.cmd')+'""'
$shortcut.WorkingDirectory=$PSScriptRoot
$shortcut.WindowStyle=1
$shortcut.Description='Build, update, restart AI Pulse, and show the result.'
$shortcut.Save()
Write-Output $shortcut.FullName
