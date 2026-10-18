$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'update-helpers.ps1')
$fixture=Join-Path $PSScriptRoot ('updater-fixture-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$installed=Join-Path $fixture 'AIPulse.exe'
$candidate=Join-Path $fixture 'candidate.exe'
$backup=Join-Path $fixture 'backup.exe'
$ini=Join-Path $fixture 'AIPulse.ini'
[IO.File]::WriteAllText($installed,'previous executable')
[IO.File]::WriteAllText($candidate,'updated executable')
[IO.File]::WriteAllText($ini,'preserved settings')
Copy-Item -LiteralPath $installed -Destination $backup
$settingsHash=(Get-FileHash -LiteralPath $ini).Hash
Install-AIPulseExecutable -Source $candidate -Destination $installed
if((Get-FileHash -LiteralPath $candidate).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Replacement failed' }
Install-AIPulseExecutable -Source $backup -Destination $installed
if((Get-FileHash -LiteralPath $backup).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Rollback failed' }
$failed=$false
try { Install-AIPulseExecutable -Source (Join-Path $fixture 'missing.exe') -Destination $installed } catch { $failed=$true }
if(!$failed -or (Get-FileHash -LiteralPath $backup).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Failed staging damaged installed executable' }
if($settingsHash -ne (Get-FileHash -LiteralPath $ini).Hash) { throw 'Settings changed' }
if(@(Get-ChildItem -LiteralPath $fixture -Filter '.aipulse-update-*.tmp' -Force).Count) { throw 'Staging files leaked' }
Write-Output 'PASS: atomic replacement, rollback, failed staging, settings preservation, temporary-file cleanup'
# Keep the small fixture as a reproducible test artifact; never touch the running app.
