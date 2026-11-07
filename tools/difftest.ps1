# Configures (when needed), builds and runs the differential test runner against the unpacked exe.
# Works from the main checkout or a git worktree (the exe is read from the main checkout's local/).
# Usage: tools/difftest.ps1 [-Filter <name substring | tests/ file stem>[,...] | @file>] [-Config Release|Debug]
#                           [-Trace <n>] [-Summary] [-NoBuild] [-Force]
# -Filter matches a test-name substring (case-sensitive) or the stem of a test file (`ts_family_relationship_array`,
#   `.cpp` optional: every test that file registers); a comma list (`-Filter rz_istream,Prim03`) runs every test any
#   part matches. Before building, a part that no file in tests/ could match (not a stem, not in any file's text, nor
#   all its `_`-separated parts in one file) stops the run with exit 3 (-Force builds anyway); a run where nothing
#   matched prints "NO TESTS MATCHED" and exits 3.
# -Summary prints build errors, every [fail]/[error] line with its failure lines (exception address included), the
#   "N run, M failed" totals and, when the runner itself dies, the monitor's "DIED in test ..." report (test, ctx.cases,
#   exit code, fault address/symbol; harness/monitor.h) or else its exit code and last lines. The runner's stdout and
#   stderr go to build\difftest.out.txt and build\difftest.err.txt (stderr, e.g. the Bcon tests' expected crash notes,
#   never hides the summary).
# -NoBuild runs the existing build\<Config>\difftest.exe without building; -BuildOnly builds and runs nothing.
# -Trace n also traces the first n calls of each tested original (record/tracer.h).
# -Log <file> also writes everything this prints to <file> (its folder is created: a fresh worktree has no build\ yet,
#   where `*> build\x.log` fails before the script starts), for background runs; the exit code is kept.
# -MinFreeGB n (default 5): refuse (exit 4) when the worktree's disk has less than n GB free.
# -Jobs n (default: half the logical processors): runs the tests in n difftest.exe processes at once, at below-normal
#   priority (harness/shards.h): contiguous ranges of the registry order, balanced on each test's time from earlier runs
#   (local\difftest-times.txt in the main checkout, shared by the worktrees); the output is one run's (the shards' in
#   order, one totals line), so -Summary, the files and their readers are unchanged. A shard whose runner dies keeps its
#   DIED report and goes on with the tests after it. -Jobs 1 is a single runner, as before. -Trace runs one.
# -Shuffle s (s > 0): one runner, the selected tests in an order shuffled with seed s (harness/main.cpp --shuffle), to
#   find tests that depend on state an earlier test left; the same seed repeats the order (overrides -Jobs).
# -StackFill: sets TS2_STACK_FILL=1, so the runner fills 64 KB of stack below each test body with 0xCDCDCDCD first
#   (harness/main.cpp FillStackBelow; a test may call difftest::FillStackBelow() itself before a side). A read of an
#   uninitialised local then shows as 0xcdcdcdcd / -431602080.0 in a diff; run a failing test with and without it.
# A fault address inside the original is named from build\difftest.funcs.txt (tools/difftest_funcs.py, regenerated from
#   db/ when stale; DIFFTEST_FUNCS names it for the runner): "rva 0x.. (the original) in function 0x.. Name+0x..".
# After a run, sandbox folders in %TEMP%\ts2difftest left by runners that are no longer running (no difftest.exe with
#   that process id) are deleted.
param([string[]]$Filter = @(), [string]$Config = 'Release', [int]$Trace = 0, [switch]$Summary, [switch]$NoBuild,
      [switch]$Force, [switch]$BuildOnly, [string]$Log = '', [double]$MinFreeGB = 5, [int]$Jobs = 0,
      [int]$Shuffle = 0, [switch]$StackFill)
$ErrorActionPreference = 'Stop'
# -Filter a,b,c arrives as an array from PowerShell and as one "a,b,c" string from powershell -File: the same list.
$filterText = (@($Filter) | ForEach-Object { "$_".Trim() } | Where-Object { $_ }) -join ','
if ($Log) {
    $Log = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Log)
    $dir = Split-Path -Parent $Log
    if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
    $pass = @{} + $PSBoundParameters
    $pass.Remove('Log')
    & $PSCommandPath @pass *>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $Log
    exit $LASTEXITCODE
}
. "$PSScriptRoot\common.ps1"
Set-LanePriority # also for -NoBuild: a worktree's runner yields to the main checkout's work

# A full disk fails builds and runs half-way (2026-10-18: C: down to 48 MB). Exit 4 below the floor.
$drive = [System.IO.DriveInfo]::new([System.IO.Path]::GetPathRoot($root))
if ($drive.AvailableFreeSpace -lt $MinFreeGB * 1GB) {
    Write-Output ("difftest: only {0:N1} GB free on {1} (floor {2} GB, -MinFreeGB): free space first (tools/diskuse.py shows what uses it; tools/snapstore.py gc, old trace batches)" -f ($drive.AvailableFreeSpace / 1GB), $drive.Name, $MinFreeGB)
    exit 4
}

# A mutation run stopped outright (TaskStop) leaves its mutant in the source until the next mutate.py start (lane QG).
# -BuildOnly is mutate.py's own build, which runs while the backup exists.
if (-not $BuildOnly -and (Test-Path (Join-Path $root 'build\mutate-backup.json'))) {
    Write-Output "difftest: WARNING build\mutate-backup.json exists: a stopped mutation run may have left a mutant in src/. If no mutate.py is running: .venv/Scripts/python -I tools/mutate.py --restore"
}

if ($filterText -and -not $Force -and -not $filterText.StartsWith('@')) {
    # Cheap check before a (possibly cold) build: could any test match? Names built by macros (`LotInfo_##Name`)
    # don't appear verbatim, so all `_`-separated parts in one file count as a possible match.
    # Each part of a comma list must be able to match on its own.
    $files = @(Get-ChildItem "$root\tests" -Filter *.cpp -Recurse)
    $texts = $null
    $impossible = @()
    foreach ($one in ($filterText -split ',' | Where-Object { $_ })) {
        $stem = $one -replace '\.cpp$', ''
        $possible = [bool]($files | Where-Object { $_.BaseName -ieq $stem })
        if (-not $possible) {
            if (-not $texts) { $texts = @($files | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) }
            $parts = @($one -split '_' | Where-Object { $_.Length -ge 3 })
            foreach ($text in $texts) {
                if ($text.Contains($one) -or ($parts.Count -and -not ($parts | Where-Object { -not $text.Contains($_) }))) {
                    $possible = $true; break
                }
            }
        }
        if (-not $possible) { $impossible += $one }
    }
    if ($impossible) {
        "NO TESTS CAN MATCH -Filter '$($impossible -join ',')': not a tests/ file stem and no tests/*.cpp mentions it"
        "(names are case-sensitive). Nothing built. Use test-name substrings or file stems (a comma list runs every test"
        "any part matches); -Force builds and runs anyway."
        exit 3
    }
}

if (-not $NoBuild) {
    if ($Summary) {
        # Errors only (a cold build prints thousands of warnings); the count of compiler warnings at the end.
        $warnings = 0
        try {
            Invoke-Build difftest $Config *>&1 | ForEach-Object { "$_" } | ForEach-Object {
                if ($_ -match ' error |error (C|LNK)\d') { $_ } elseif ($_ -match 'warning C4') { $warnings++ }
            }
        }
        catch { "$_"; exit 1 }
        if ($warnings) { "($warnings compiler warning lines; build without -Summary to see them)" }
    } else { Invoke-Build difftest $Config }
}
if ($BuildOnly) { exit 0 }

$t   = Get-Content "$root\target\legacy-1.18.0.187.json" -Raw | ConvertFrom-Json
$exe = "$local\original\$(Split-Path $t.file -Leaf).unpacked.exe"
$runner = "$build\$Config\difftest.exe"
if (-not (Test-Path $runner)) { "no $runner (build first, without -NoBuild)"; exit 1 }
$runArgs = @('--image', $exe, '--json', "$build\difftest.json")
if ($filterText) { $runArgs += @('--filter', $filterText) }
if ($Trace) { $runArgs += @('--trace', $Trace) }
if ($Shuffle -gt 0) { $runArgs += @('--shuffle', $Shuffle); $Jobs = 1 }
if ($Jobs -le 0) { $Jobs = [Math]::Max(1, [Math]::Floor([Environment]::ProcessorCount / 2)) }
if ($Jobs -gt 1 -and -not $Trace) { $runArgs += @('--jobs', $Jobs, '--times', "$local\difftest-times.txt") }
# The function table that names a fault address inside the original (best effort: a failure here only loses the names).
$funcsFile = "$build\difftest.funcs.txt"
try {
    & "$mainRoot\.venv\Scripts\python.exe" -I "$PSScriptRoot\difftest_funcs.py" $funcsFile | Out-Null
    if (Test-Path $funcsFile) { $env:DIFFTEST_FUNCS = $funcsFile }
} catch { }
if ($StackFill) { $env:TS2_STACK_FILL = '1' } else { Remove-Item Env:\TS2_STACK_FILL -ErrorAction SilentlyContinue }
# Tests print expected crash notes to stderr (Bcon_GetBconTable): never let that end the script.
$ErrorActionPreference = 'Continue'

# After a run: the sandbox folders runners left in %TEMP%\ts2difftest (harness/sandbox.cpp names each by its runner's
# process id and deletes it at a normal exit; a runner that died or was killed leaves it). Only folders whose name is a
# process id that no running difftest process has are removed (a reused id of another program doesn't keep a folder),
# so a runner of another worktree keeps its own.
function Remove-StaleSandboxes {
    $sandboxRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'ts2difftest'
    if (-not (Test-Path -LiteralPath $sandboxRoot)) { return }
    $live = @{}
    foreach ($proc in [System.Diagnostics.Process]::GetProcessesByName('difftest')) { $live[[string]$proc.Id] = $true }
    foreach ($d in @(Get-ChildItem -LiteralPath $sandboxRoot -Directory -ErrorAction SilentlyContinue)) {
        if ($d.Name -notmatch '^\d+$' -or $live.ContainsKey($d.Name)) { continue }
        try { Remove-Item -LiteralPath $d.FullName -Recurse -Force -ErrorAction Stop } catch {}
    }
}

if (-not $Summary) {
    & $runner @runArgs
    $code = $LASTEXITCODE
    Remove-StaleSandboxes
    exit $code
}

$out = "$build\difftest.out.txt"
$err = "$build\difftest.err.txt"
$quoted = $runArgs | ForEach-Object { '"' + "$_" + '"' }
$p = Start-Process -FilePath $runner -ArgumentList $quoted -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $out -RedirectStandardError $err
$code = $p.ExitCode
$lines = @(Get-Content $out)
$totals = $null
$keep = $false
$shown = 0
$died = $false
$inDied = $false
foreach ($l in $lines) {
    # The monitor's report when the runner dies (harness/monitor.h): shown whole, to its "difftest.exe died ..." line
    # (with -Jobs, the other shards' results and the restarted shard's follow it).
    if ($l -match '^DIED ') { $died = $true; $inDied = $true; $keep = $false; $l; continue }
    if ($inDied) { $l; if ($l -match '^difftest\.exe died') { $inDied = $false }; continue }
    # -Jobs: a shard restarted after a death or that never ran a test, and the shards' times.
    if ($l -match '^difftest: shard') { $keep = $false; $l; continue }
    if ($l -match '^\[(fail|error)\]') { $keep = $true; $shown = 0; $l; continue }
    if ($l -match '^\[') { $keep = $false; continue }
    if ($l -match '^\d+ run, \d+ failed$') { $totals = $l; $keep = $false; continue }
    if ($l -match '^NO TESTS MATCHED') { $l; continue }
    if ($keep -and $l -match '^\s' -and $shown -lt 12) { $l; $shown++ }
    elseif ($keep -and $l -match '^\s' -and $shown -eq 12) { '    ... (more in build\difftest.out.txt)'; $shown++ }
}
$errLines = @(Get-Content $err -ErrorAction SilentlyContinue).Count
if ($totals) {
    $totals
    # -Jobs: a shard's runner died and the rest of its tests ran in a new one (died tests count as failed).
    if ($died) { 'a runner died (report above; its shard went on after the test): exit code {0} (0x{1:x8})' -f $code, $code }
} elseif ($died) {
    'difftest.exe ended without its totals line: exit code {0} (0x{1:x8}); report above' -f $code, $code
    if ($errLines) { 'last stderr lines:'; Get-Content $err | Select-Object -Last 5 | ForEach-Object { "    $_" } }
} else {
    # The runner died mid-test: the last test it started is the last [..] line's successor; show the tail.
    'difftest.exe ended without its totals line: exit code {0} (0x{1:x8}); last output lines:' -f $code, $code
    $lines | Select-Object -Last 8 | ForEach-Object { "    $_" }
    if ($errLines) { 'last stderr lines:'; Get-Content $err | Select-Object -Last 5 | ForEach-Object { "    $_" } }
}
if ($errLines) { "($errLines stderr lines in build\difftest.err.txt)" }
Remove-StaleSandboxes
exit $code
