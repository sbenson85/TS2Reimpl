# Rebuilds the annotated Ghidra project (pilot): copies the analysed project local/ghidra to
# local/ghidra-annotated/proj and applies the facts tools/annotate.py derives from db/ (names, class and
# vtable structures, db/annotations.jsonl). The analysed project is never written. Decompile from the
# annotated copy with `tools/decompile.ps1 -Annotated <rva>...`. Rebuild after db/ changes; one writer.
# Each rebuild is a new copy (proj-<stamp>) named in current.txt, so a lane decompiling meanwhile keeps
# its copy; older copies are removed when no Ghidra has them open.
param([string]$MaxMem = '12G')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
$ghidra = Get-ChildItem "$local\tools" -Directory -Filter 'ghidra_*' | Select-Object -First 1
$dir    = "$local\ghidra-annotated"
$proj   = "$dir\proj-" + (Get-Date -Format 'yyyyMMdd-HHmmss')
[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
$python = Join-Path (Split-Path $local -Parent) '.venv\Scripts\python.exe'
& "$PSScriptRoot\pdbfacts.ps1"   # our build's layouts and signatures, current with src/
& $python -I "$PSScriptRoot\annotate.py" --out "$dir\facts.json"
New-Item -ItemType Directory -Force $proj | Out-Null
Copy-Item "$local\ghidra\Sims2EP9.gpr" $proj
Copy-Item -Recurse "$local\ghidra\Sims2EP9.rep" $proj
$env:GHIDRA_HEADLESS_MAXMEM = $MaxMem
& "$($ghidra.FullName)\support\analyzeHeadless.bat" $proj Sims2EP9 -process -noanalysis `
    -scriptPath "$PSScriptRoot\ghidra" -postScript ApplyDb.java "$dir\facts.json" *> "$dir\apply.log"
Get-Content "$dir\apply.log" | Select-String 'ApplyDb:|ERROR|Exception' | ForEach-Object { $_.Line }
if (-not (Select-String -Path "$dir\apply.log" -Pattern 'ApplyDb:' -Quiet)) { throw "ApplyDb did not run; see $dir\apply.log" }
Set-Content -Path "$dir\current.txt" -Value $proj -Encoding ascii
foreach ($old in Get-ChildItem $dir -Directory -Filter 'proj*' | Where-Object { $_.FullName -ne $proj }) {
    # A project Ghidra has open holds a lock file; leave it for the next rebuild.
    if (-not (Get-ChildItem $old.FullName -Filter '*.lock*' -Recurse -ErrorAction SilentlyContinue)) {
        try { Remove-Item -Recurse -Force $old.FullName -ErrorAction Stop } catch {}
    }
}
