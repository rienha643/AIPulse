param(
    [string]$RuntimeDirectory,
    [ValidateRange(1,30)][int]$StopTimeoutSeconds=10
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'update-helpers.ps1')
$updateMutex=[Threading.Mutex]::new($false,'Local\AIPulse.Updater.v1')
$ownsMutex=$false
$replaced=$false
$stoppedForUpdate=$false
$wasRunning=$false
$startedProcess=$null
$backupExe=$null
$targetExe=$null
try {
    try { $ownsMutex=$updateMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsMutex=$true }
    if(!$ownsMutex) { throw 'Another AI Pulse update is already in progress.' }
    if([string]::IsNullOrWhiteSpace($RuntimeDirectory)) {
        $localConfig=Join-Path $PSScriptRoot 'update.local.json'
        if(Test-Path -LiteralPath $localConfig) { $RuntimeDirectory=[string](Get-Content -LiteralPath $localConfig -Raw|ConvertFrom-Json).RuntimeDirectory }
        if([string]::IsNullOrWhiteSpace($RuntimeDirectory)) { $RuntimeDirectory=Join-Path $env:LOCALAPPDATA 'AIPulse' }
    }
    $runtime=(Resolve-Path -LiteralPath $RuntimeDirectory -ErrorAction Stop).Path
    if((Get-Item -LiteralPath $runtime).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Runtime directory must not be a reparse point.' }
    $targetExe=Join-Path $runtime 'AIPulse.exe'
    $settings=Join-Path $runtime 'AIPulse.ini'
    foreach($file in @($targetExe,$settings)) {
        $item=Get-Item -LiteralPath $file -ErrorAction Stop
        if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Expected a regular file: $file" }
    }
    $startupBefore=Get-AIPulseStartupState
    $candidate=Join-Path $PSScriptRoot 'out/AIPulse-update.exe'
    Write-Host 'Building AI Pulse. The running app remains active until the build succeeds.'
    & (Join-Path $PSScriptRoot 'build.ps1') -OutputPath $candidate
    if(!(Test-Path -LiteralPath $candidate) -or (Get-Item -LiteralPath $candidate).Length -lt 1024) { throw 'Build output is missing or invalid.' }
    $currentSession=[Diagnostics.Process]::GetCurrentProcess().SessionId
    $namedProcesses=@(Get-Process -Name AIPulse -ErrorAction SilentlyContinue)
    $running=@()
    foreach($process in $namedProcesses) {
        if($process.Path -ieq $targetExe) {
            if($process.SessionId -ne $currentSession) { throw 'AI Pulse is running in another Windows session; update was not started.' }
            $running+=$process
        } elseif($process.SessionId -eq $currentSession) {
            throw 'Another AI Pulse installation is running in this session; no app was stopped.'
        }
    }
    $wasRunning=$running.Count -gt 0
    if($wasRunning) {
        Write-Host 'Closing only the installed AI Pulse instance.'
        $quit=Start-Process -FilePath $targetExe -ArgumentList '--quit' -WorkingDirectory $runtime -WindowStyle Hidden -PassThru
        if(!$quit.WaitForExit($StopTimeoutSeconds*1000)) { throw 'AI Pulse quit request timed out; executable was not replaced.' }
        foreach($process in $running) {
            if(!$process.WaitForExit($StopTimeoutSeconds*1000)) { throw 'AI Pulse did not close; executable was not replaced. No process was force-killed.' }
        }
    }
    $stoppedForUpdate=$true
    $backupDirectory=Join-Path (Join-Path $PSScriptRoot 'backups') ((Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
    $backupExe=Join-Path $backupDirectory 'AIPulse.exe'
    Copy-Item -LiteralPath $targetExe -Destination $backupExe
    Copy-Item -LiteralPath $settings -Destination (Join-Path $backupDirectory 'AIPulse.ini')
    $settingsHash=(Get-FileHash -LiteralPath $settings).Hash
    Install-AIPulseExecutable -Source $candidate -Destination $targetExe
    $replaced=$true
    Write-Host 'Starting the updated AI Pulse.'
    $startedProcess=Start-Process -FilePath $targetExe -WorkingDirectory $runtime -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 3
    $startedProcess.Refresh()
    if($startedProcess.HasExited) { throw "Updated AI Pulse exited during startup (code $($startedProcess.ExitCode))." }
    $startupAfter=Get-AIPulseStartupState
    $result=[pscustomobject]@{
        UpdatedAt=(Get-Date -Format o);Executable=$targetExe;ProcessId=$startedProcess.Id
        SettingsUnchanged=($settingsHash -eq (Get-FileHash -LiteralPath $settings).Hash)
        StartupUnchanged=($startupBefore.Exists -eq $startupAfter.Exists -and $startupBefore.Value -ceq $startupAfter.Value -and $startupBefore.TaskXml -ceq $startupAfter.TaskXml)
        BackupDirectory=$backupDirectory;ExecutableHash=(Get-FileHash -LiteralPath $targetExe).Hash
    }
    New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'reports') -Force | Out-Null
    $result|ConvertTo-Json|Set-Content (Join-Path $PSScriptRoot 'reports/last-update.json')
    if(!$result.SettingsUnchanged) { Write-Warning 'The app changed its settings during startup; the updater did not overwrite the INI. A pre-update copy is in the backup directory.' }
    if(!$result.StartupUnchanged) { Write-Warning 'The startup registration changed externally during the update; the updater did not write to the registry.' }
    $result
} catch {
    $updateFailure=$_
    if($stoppedForUpdate) {
        try {
            if($startedProcess -and !$startedProcess.HasExited) {
                $quit=Start-Process -FilePath $targetExe -ArgumentList '--quit' -WindowStyle Hidden -PassThru
                if(!$quit.WaitForExit($StopTimeoutSeconds*1000) -or !$startedProcess.WaitForExit($StopTimeoutSeconds*1000)) { throw 'The failed app did not close for rollback.' }
            }
            if($replaced -and $backupExe) { Install-AIPulseExecutable -Source $backupExe -Destination $targetExe }
            if($wasRunning) {
                Start-Process -FilePath $targetExe -WorkingDirectory (Split-Path -Parent $targetExe) -WindowStyle Hidden | Out-Null
            }
            Write-Warning 'The update failed. The previous executable was retained/restored; settings and startup registration were not overwritten.'
        } catch {
            Write-Warning "Automatic recovery failed: $($_.Exception.Message). Previous executable backup: $backupExe"
        }
    }
    throw $updateFailure
} finally {
    if($ownsMutex) { $updateMutex.ReleaseMutex() }
    $updateMutex.Dispose()
}
