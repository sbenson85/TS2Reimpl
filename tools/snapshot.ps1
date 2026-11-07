# Copies the pinned game executable out of the Steam install into local/original/
# and verifies it matches target/*.json. Read-only with respect to the Steam install.
param(
    [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\The Sims 2 Legacy Collection',
    [string]$Target  = "$PSScriptRoot\..\target\legacy-1.18.0.187.json"
)
$ErrorActionPreference = 'Stop'
$t    = Get-Content $Target -Raw | ConvertFrom-Json
$src  = Join-Path $GameDir $t.file
$dest = Join-Path $PSScriptRoot "..\local\original"
New-Item -ItemType Directory -Force $dest | Out-Null

$hash = (Get-FileHash $src -Algorithm SHA1).Hash
if ($hash -ne $t.sha1) {
    throw "Installed $($t.file) has SHA-1 $hash, expected $($t.sha1). The game was updated; add a new target file rather than overwriting."
}
Copy-Item $src $dest -Force
Write-Output "Snapshot OK: $(Join-Path $dest (Split-Path $src -Leaf)) ($hash)"
