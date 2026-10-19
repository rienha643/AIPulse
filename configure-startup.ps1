param([string]$RuntimeDirectory)
$ErrorActionPreference='Stop'
if([string]::IsNullOrWhiteSpace($RuntimeDirectory)) {
    $config=Join-Path $PSScriptRoot 'update.local.json'
    if(Test-Path -LiteralPath $config) { $RuntimeDirectory=[string](Get-Content -LiteralPath $config -Raw|ConvertFrom-Json).RuntimeDirectory }
    if([string]::IsNullOrWhiteSpace($RuntimeDirectory)) { $RuntimeDirectory=Join-Path $env:LOCALAPPDATA 'AIPulse' }
}
$exe=(Resolve-Path -LiteralPath (Join-Path $RuntimeDirectory 'AIPulse.exe')).Path
$sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$name='AIPulse-'+$sid
$run='HKCU:/Software/Microsoft/Windows/CurrentVersion/Run'
$oldRun=(Get-ItemProperty -LiteralPath $run -ErrorAction SilentlyContinue).AIPulse
$previous=Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue
$backup=Join-Path $PSScriptRoot ('backups/startup-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
@{RunValue=$oldRun;TaskName=$name}|ConvertTo-Json|Set-Content (Join-Path $backup 'previous-startup.json')
if($previous) { Export-ScheduledTask -TaskName $name|Set-Content (Join-Path $backup 'previous-task.xml') }
$action=New-ScheduledTaskAction -Execute $exe -Argument '--startup' -WorkingDirectory (Split-Path -Parent $exe)
$trigger=New-ScheduledTaskTrigger -AtLogOn -User $sid
$trigger.Delay='PT15S'
$principal=New-ScheduledTaskPrincipal -UserId $sid -LogonType Interactive -RunLevel Limited
$settings=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
try {
    Register-ScheduledTask -TaskName $name -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'AI Pulse: current-user logon, delayed 15 seconds.' -Force|Out-Null
    $task=Get-ScheduledTask -TaskName $name
    if(!$task.Settings.Enabled -or $task.Actions.Execute -cne $exe -or $task.Actions.Arguments -ne '--startup' -or $task.Triggers.Delay -ne 'PT15S') { throw 'Startup task verification failed.' }
    # Remove the legacy entry only after the replacement has been verified.
    if($null -ne $oldRun) { Remove-ItemProperty -LiteralPath $run -Name AIPulse -ErrorAction Stop }
} catch {
    if($previous) { Register-ScheduledTask -TaskName $name -Xml (Get-Content (Join-Path $backup 'previous-task.xml') -Raw) -Force|Out-Null }
    else { Unregister-ScheduledTask -TaskName $name -Confirm:$false -ErrorAction SilentlyContinue }
    throw
}
[pscustomobject]@{TaskName=$name;Enabled=$task.Settings.Enabled;Delay=$task.Triggers.Delay;Executable=$exe;BackupDirectory=$backup}
