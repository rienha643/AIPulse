$ErrorActionPreference='Stop'
$exitCode=1
$transcribing=$false
$logPath=$null
$status=[ordered]@{Timestamp=(Get-Date -Format o);Status='failed';Message=$null;Log=$null}
try {
    $reports=Join-Path $PSScriptRoot 'reports'
    New-Item -ItemType Directory -Path $reports -Force | Out-Null
    $logPath=Join-Path $reports ('update-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)+'.log')
    $status.Log=$logPath
    Start-Transcript -LiteralPath $logPath -Force | Out-Null
    $transcribing=$true
    Write-Host 'AI Pulse 업데이트를 시작합니다.' -ForegroundColor Cyan
    Write-Host '창을 닫지 말고 결과가 나올 때까지 기다려 주세요.'
    $result=& (Join-Path $PSScriptRoot 'update.ps1')
    if($result.SettingsUnchanged -and $result.StartupUnchanged) {
        $status.Status='success';$status.Message='업데이트 성공: AI Pulse 재실행, 기존 설정 및 자동 실행 유지 확인.'
        Write-Host "`n$($status.Message)" -ForegroundColor Green
        $exitCode=0
    } else {
        $status.Status='warning';$status.Message='업데이트와 재실행은 완료됐지만 설정 또는 자동 실행 상태에 변화가 감지됐습니다. 로그를 확인해 주세요.'
        Write-Host "`n$($status.Message)" -ForegroundColor Yellow
        $exitCode=2
    }
    $status.BackupDirectory=$result.BackupDirectory
    Write-Host ('이전 버전 백업: '+$result.BackupDirectory)
} catch {
    $status.Message=$_.Exception.Message
    Write-Host "`n업데이트 실패" -ForegroundColor Red
    Write-Host $status.Message -ForegroundColor Red
    Write-Host '오류 및 자동 복구 결과를 아래 로그에서 확인해 주세요.'
    if($_.ScriptStackTrace) { Write-Host $_.ScriptStackTrace }
} finally {
    if($logPath) { Write-Host ('로그: '+$logPath) }
    try {
        $status|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'reports/last-update-launch.json') -Encoding UTF8
    } catch { Write-Host ('결과 파일 저장 실패: '+$_.Exception.Message) -ForegroundColor Red }
    if($transcribing) { Stop-Transcript | Out-Null }
}
exit $exitCode
