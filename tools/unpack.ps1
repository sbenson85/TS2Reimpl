# Removes SteamStub DRM from the snapshotted exe (local/original/) with Steamless, for static analysis only.
# The game itself still runs the original packed exe.
param([string]$Target = "$PSScriptRoot\..\target\legacy-1.18.0.187.json")
$ErrorActionPreference = 'Stop'
$t   = Get-Content $Target -Raw | ConvertFrom-Json
$exe = Join-Path $PSScriptRoot "..\local\original\$(Split-Path $t.file -Leaf)"
& "$PSScriptRoot\..\local\tools\steamless\Steamless.CLI.exe" --quiet --keepbind $exe | Select-String 'Unpacker|Successfully|error' | % Line
$out  = "$exe.unpacked.exe"
$hash = (Get-FileHash $out -Algorithm SHA1).Hash
if ($hash -ne $t.unpacked_sha1) { throw "Unpacked SHA-1 $hash, expected $($t.unpacked_sha1)" }
Write-Output "Unpacked OK: $out ($hash)"
