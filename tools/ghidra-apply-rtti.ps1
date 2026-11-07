# Applies db/classes.jsonl to the Ghidra project, then re-runs auto-analysis so the new functions get analysed.
param([string]$MaxMem = '32G', [int]$MaxCpu = 6)
$ErrorActionPreference = 'Stop'
$root   = Resolve-Path "$PSScriptRoot\.."
$ghidra = Get-ChildItem "$root\local\tools" -Directory -Filter 'ghidra_*' | Select-Object -First 1
$proj   = "$root\local\ghidra"

[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
$env:GHIDRA_HEADLESS_MAXMEM = $MaxMem
& "$($ghidra.FullName)\support\analyzeHeadless.bat" $proj Sims2EP9 -process -max-cpu $MaxCpu `
    -scriptPath "$root\tools\ghidra" -preScript ApplyRtti.java "$root\db\classes.jsonl" *> "$proj\apply-rtti.out"
Get-Content "$proj\apply-rtti.out" | Select-String 'ApplyRtti|Total Time|ERROR|Exception' | % Line
exit $LASTEXITCODE
