function Install-AIPulseExecutable {
    param([Parameter(Mandatory)][string]$Source,[Parameter(Mandatory)][string]$Destination)
    $destinationPath=[IO.Path]::GetFullPath($Destination)
    $directory=Split-Path -Parent $destinationPath
    $temporary=Join-Path $directory ('.aipulse-update-'+[guid]::NewGuid().ToString('N')+'.tmp')
    # Same-volume replacement: a failed copy leaves the installed executable intact.
    try {
        [IO.File]::Copy([IO.Path]::GetFullPath($Source),$temporary,$false)
        if((Get-FileHash -LiteralPath $Source).Hash -ne (Get-FileHash -LiteralPath $temporary).Hash) {
            throw 'Executable staging verification failed.'
        }
        [IO.File]::Replace($temporary,$destinationPath,[System.Management.Automation.Language.NullString]::Value)
    } finally {
        if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}

function Get-AIPulseStartupState {
    $key='HKCU:/Software/Microsoft/Windows/CurrentVersion/Run'
    if(!(Test-Path -LiteralPath $key)) { return [pscustomobject]@{Exists=$false;Value=$null} }
    $properties=Get-ItemProperty -LiteralPath $key -ErrorAction Stop
    $entry=$properties.PSObject.Properties['AIPulse']
    return [pscustomobject]@{Exists=($null -ne $entry);Value=$(if($entry){$entry.Value}else{$null})}
}
