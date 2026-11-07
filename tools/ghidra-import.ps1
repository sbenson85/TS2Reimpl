# Imports the unpacked exe into a Ghidra project under local/ghidra/ and runs auto-analysis.
# Runs at below-normal priority with limited threads so the desktop stays usable.
param(
    [int]$MaxCpu = 6,
    [string]$MaxMem = '32G',
    [string]$Target = "$PSScriptRoot\..\target\legacy-1.18.0.187.json"
)
$ErrorActionPreference = 'Stop'
$root   = Resolve-Path "$PSScriptRoot\.."
$t      = Get-Content $Target -Raw | ConvertFrom-Json
$ghidra = Get-ChildItem "$root\local\tools" -Directory -Filter 'ghidra_*' | Select-Object -First 1
$exe    = "$root\local\original\$(Split-Path $t.file -Leaf).unpacked.exe"
$proj   = "$root\local\ghidra"
New-Item -ItemType Directory -Force $proj | Out-Null

$env:GHIDRA_HEADLESS_MAXMEM = $MaxMem
$log = "$proj\import.log"
# Child processes (cmd -> java) inherit a below-normal priority class from this one.
[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
& "$($ghidra.FullName)\support\analyzeHeadless.bat" $proj Sims2EP9 -import $exe -overwrite -max-cpu $MaxCpu -log $log *> "$proj\import.out"
Write-Output "Ghidra exited with $LASTEXITCODE; log: $log"
exit $LASTEXITCODE
