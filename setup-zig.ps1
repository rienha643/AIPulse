$ErrorActionPreference='Stop'
$version='0.15.2'
$tools=Join-Path $PSScriptRoot 'tools'
$compiler=Join-Path $tools "zig-x86_64-windows-$version/zig.exe"
if(Test-Path -LiteralPath $compiler) {
    if((& $compiler version).Trim() -eq $version) { Write-Output "Zig $version is ready."; return }
    throw 'An unexpected compiler already exists in tools. Inspect it before continuing.'
}
New-Item -ItemType Directory -Path $tools -Force | Out-Null
$archive=Join-Path $tools "zig-$version.zip"
$expected='3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c'
Invoke-WebRequest -Uri "https://ziglang.org/download/$version/zig-x86_64-windows-$version.zip" -OutFile $archive
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Zig archive checksum mismatch. The archive was not extracted.'
}
Expand-Archive -LiteralPath $archive -DestinationPath $tools
if((& $compiler version).Trim() -ne $version) { throw 'Compiler version verification failed.' }
Write-Output "Zig $version is ready. Run build.ps1."
