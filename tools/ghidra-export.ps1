# Runs tools/ghidra/ExportDb.java against the analysed project (read-only) and writes db/.
param([string]$MaxMem = '32G')
$ErrorActionPreference = 'Stop'
$root   = Resolve-Path "$PSScriptRoot\.."
$ghidra = Get-ChildItem "$root\local\tools" -Directory -Filter 'ghidra_*' | Select-Object -First 1
$proj   = "$root\local\ghidra"

[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
$env:GHIDRA_HEADLESS_MAXMEM = $MaxMem
& "$($ghidra.FullName)\support\analyzeHeadless.bat" $proj Sims2EP9 -process -noanalysis -readOnly `
    -scriptPath "$root\tools\ghidra" -postScript ExportDb.java "$root\db" *> "$proj\export.out"
Get-Content "$proj\export.out" | Select-String 'ExportDb|functions:|vftables:|ERROR|Exception' | % Line
exit $LASTEXITCODE
