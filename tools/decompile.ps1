# Decompiles functions with Ghidra (tools/ghidra/Decompile.java, read-only on the analysed project) into
# local/decomp/<rva>.c, for reading while writing a replacement. Decompiler output is game-derived:
# it stays in local/ (never db/, src/, tests/ or a commit), and replacements are written from an
# understanding of it, not copied or closely paraphrased. Works from worktrees (uses the main checkout's
# local/). The headless start takes ~15 s: pass several RVAs at once.
#   tools/decompile.ps1 0x5890c0 0x5a1060
# -Annotated reads the annotated copy (tools/ghidra-annotate.ps1: names, types and layouts from db/)
# and writes to local/decomp-annotated/.
# When the decompile server runs (tools/ghidra.py start; docs/lane/understand.md), this asks it instead: under a
# second per call, no project lock. Its annotated state holds the facts of whoever last ran start/reload (current
# db/), not the -Annotated project copy's. Without a server, the headless path below runs as before.
param([Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Rva, [string]$MaxMem = '8G', [switch]$Annotated)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
if (Test-Path "$local\ghidra-server\server.json") {
    $python = Join-Path $mainRoot '.venv\Scripts\python.exe'
    if ($Annotated) { & $python -I "$PSScriptRoot\ghidra.py" decompile --files $Rva }
    else { & $python -I "$PSScriptRoot\ghidra.py" decompile --files --raw $Rva }
    if ($LASTEXITCODE -eq 0) { return }
    Write-Host 'decompile server not answering; falling back to headless'
}
$ghidra = Get-ChildItem "$local\tools" -Directory -Filter 'ghidra_*' | Select-Object -First 1
$proj   = if ($Annotated) { (Get-Content "$local\ghidra-annotated\current.txt" -TotalCount 1).Trim() } else { "$local\ghidra" }
$out    = if ($Annotated) { "$local\decomp-annotated" } else { "$local\decomp" }
[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
$env:GHIDRA_HEADLESS_MAXMEM = $MaxMem
# Ghidra locks the project: one decompile at a time (a lane gets "project locked" if another runs).
& "$($ghidra.FullName)\support\analyzeHeadless.bat" $proj Sims2EP9 -process -noanalysis -readOnly `
    -scriptPath "$PSScriptRoot\ghidra" -postScript Decompile.java $out @Rva *> "$out.log"
Get-Content "$out.log" | Select-String 'Decompile:|ERROR|Exception|locked' | ForEach-Object { $_.Line }
