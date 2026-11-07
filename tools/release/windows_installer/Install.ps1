param([Parameter(Mandatory=$true)][string]$Payload,
      [Parameter(Mandatory=$true)][string]$Destination,
      [Parameter(Mandatory=$true)][string]$ExpectedHash)
$ErrorActionPreference = 'Stop'
try {
    if ((Get-FileHash -LiteralPath $Payload -Algorithm SHA256).Hash -ne $ExpectedHash) {
        throw 'Content archive checksum mismatch. Copy the complete installer and its matching content archive again.'
    }
    if (Test-Path -LiteralPath $Destination) {
        if (@(Get-ChildItem -LiteralPath $Destination -Force).Count -ne 0) { throw 'Choose an empty installation folder.' }
    } else { New-Item -ItemType Directory -Path $Destination -Force | Out-Null }
    & "$env:SystemRoot\System32\tar.exe" -xf $Payload -C $Destination
    if ($LASTEXITCODE -ne 0) { throw 'Content extraction failed. Check free disk space and permissions.' }
    foreach ($name in @('OpenBFME.exe','OpenBFME.pck','content-profile.json')) {
        if (!(Test-Path -LiteralPath (Join-Path $Destination $name))) { throw "Installation is missing $name" }
    }
    exit 0
} catch { Write-Error $_; exit 1 }
