param([string]$OutputPath = (Join-Path $PSScriptRoot 'out/AIPulse.exe'))
$ErrorActionPreference = 'Stop'
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path (Split-Path -Parent $OutputPath) -Force | Out-Null
$compiler = Join-Path $PSScriptRoot 'tools/zig-x86_64-windows-0.15.2/zig.exe'
if (!(Test-Path $compiler)) {
    $zigCommand=Get-Command zig.exe -ErrorAction SilentlyContinue
    if(!$zigCommand) { throw 'Zig 0.15.2 was not found. Run setup-zig.ps1 first or put zig.exe on PATH.' }
    $compiler=$zigCommand.Source
}
if((& $compiler version).Trim() -ne '0.15.2') { throw 'This build requires Zig 0.15.2.' }
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot 'tools/cache'
& $compiler cc -target x86_64-windows-gnu -Os -s -municode '-Wl,--subsystem,windows' (Join-Path $PSScriptRoot 'aipulse.c') -o $OutputPath -luser32 -lgdi32 -ladvapi32 -lwtsapi32 -lole32 -loleaut32 -luuid -lshell32
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
