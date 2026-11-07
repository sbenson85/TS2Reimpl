# Runs a scripted in-game scenario against the game copy. Steps are separated by ';':
#   start            launch the game (windowed)       wait:N        sleep N seconds
#   click:X,Y        left-click client coordinates     key:0xNN      press a virtual key
#   type:TEXT        post TEXT as WM_CHAR messages (the focused text field; click.ps1 -Text)
#   shot[:name]      screenshot to local/screenshots   stop          force-close the game (no exit code)
#   quit[:N]         the game's own quit (host control "quit": no dialog, no save), then wait up to N s
#                    (default 120) for the process to end by itself; prints the exit line
#                    "exit code N (0x...)" from ts2exit.txt (game.ps1). Fails (and kills) at the deadline.
#                    Runs without a quit step print the line at the end ("killed: ..." after stop).
#   log              print the last host log line (hook call counts)
#   cheat:TEXT       run a cheat-console command on the game's main thread (host/control.cpp)
#   ctl:TEXT         send a host control command (host/control.cpp), e.g. ctl:loadlot 6
#   until:REGEX[,N]  wait (up to N s, default 120) until the host log matches REGEX
#   snap:NAME        copy the user-data folder to local/saves/<stamp>-NAME (for tools/savediff.py),
#                    and the host log beside it; keeps the newest -KeepSnapshots (12) snapshot folders.
#   keep:NAME        only the run's small files: local/saves/<stamp>-NAME.ts2host.log, .ts2devlog.txt,
#                    .ts2wintree.txt, .ts2services.txt, .ts2coverage.txt (those that exist) and .run.json
#                    Unchanged files are hardlinks into local/snapstore (tools/snapstore.py): never edit a
#                    snapshot's files in place (copy it first, or snapstore.break_link as setfunds.py does)
# Example (load Belladonna Cove from the main menu):
#   scenario.ps1 'start;wait:20;click:1004,660;wait:60;shot:hood;log;stop'
#   scenario.ps1 @live-belladonna -ResetUserData
# -Quiet starts the game in quiet mode; -Original runs the untouched exe instead of our host (game.ps1);
# -Reference runs the host without replacements; -Seed N fixes the game's clock seeds, -Draws counts
# generator draws (game.ps1). -Coverage LIST arms first-call coverage on the original functions in LIST (game.ps1;
# ts2coverage.txt in the game folder, copied beside each snap's host log; tools/coverage.py). -FrameClock runs the
# game's clocks at a fixed time per frame; -DeviceLog SPEC logs its Direct3D calls (game.ps1; ts2devlog.txt, copied
# beside each snap's host log as <stamp>-NAME.ts2devlog.txt; tools/devlog.py, docs/ui-testing.md); the "wintree" and
# "services" control commands' ts2wintree.txt and ts2services.txt are copied the same way (tools/services.py). -Baseline DIR makes -ResetUserData restore DIR (a prepared or edited
# user-data folder, kept under local/baselines/) instead of local/userdata-baseline.
# Parallel runs (tools/smoke.py): -Instance N runs in local/game-N (tools/instances.ps1) with its own
# user-data folder local/userdata-N (what -ResetUserData resets and snap: copies), never deploys (as
# -NoDeploy does for instance 0), and warns if the real Documents user-data folder changed meanwhile.
# Snapshots of instance runs are named <stamp>-iN-NAME; -SnapDir DIR puts them in DIR (not pruned).
# -DryRun prints the resolved steps and paths and runs nothing.
param([Parameter(Mandatory)][string]$Steps, [switch]$ResetUserData, [switch]$Quiet, [switch]$Original, [switch]$Reference, [string]$Seed, [switch]$Draws, [string]$Coverage, [switch]$FrameClock, [string]$DeviceLog, [string]$Baseline, [int]$KeepSnapshots = 12,
      [int]$Instance = 0, [switch]$NoDeploy, [string]$SnapDir, [switch]$DryRun)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$inst = Get-Instance $Instance
$bin = $inst.Bin
$live = $inst.UserData
$tag = if ($Instance -gt 0) { "$stamp-i$Instance" } else { $stamp }
$saves = if ($SnapDir) { $SnapDir } else { "$local\saves" }
# The run's conditions, written beside each snapshot as <stamp>-NAME.run.json (tools/lotcheck.py binds
# fixtures to them): the scenario text, the baseline restored by -ResetUserData, the seed and flags.
function Get-Sha256Text([string]$text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    -join ($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($text)) | ForEach-Object { $_.ToString('x2') })
}
# Same digest as tree_digest() in tools/lotcheck.py: sorted "relpath<TAB>sha256" lines (lower-case,
# forward slashes, ordinal order), each ending in a newline.
function Get-TreeDigest([string]$dir) {
    $root = (Resolve-Path $dir).Path.TrimEnd('\') + '\'
    [string[]]$lines = Get-ChildItem $dir -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($root.Length).Replace('\', '/').ToLowerInvariant()
        "$rel`t$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())"
    }
    [Array]::Sort($lines, [StringComparer]::Ordinal)
    Get-Sha256Text (($lines | ForEach-Object { "$_`n" }) -join '')
}
function Get-Deployed { if (Test-Path "$bin\ts2deploy.txt") { Get-Content "$bin\ts2deploy.txt" -TotalCount 1 } else { $null } }
# The newest write in the real Documents user-data folder: an instance run must not change it.
function Get-DocumentsStamp {
    $docs = (Get-Instance 0).UserData
    if (-not (Test-Path $docs)) { return 'absent' }
    $newest = Get-ChildItem $docs -Recurse -Force -ErrorAction SilentlyContinue | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    "$((Get-ChildItem $docs -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object).Count) $($newest.FullName) $($newest.LastWriteTimeUtc.Ticks)"
}
$manifest = [ordered]@{ scenario = $Steps; seed = $Seed; reference = [bool]$Reference; draws = [bool]$Draws; coverage = [bool]$Coverage;
    frameclock = [bool]$FrameClock; devlog = $(if ($DeviceLog) { $DeviceLog } else { $null }); original = [bool]$Original; reset_userdata = [bool]$ResetUserData; baseline = $null; baseline_sha256 = $null }
if ($Instance -gt 0) { $manifest.instance = $Instance }
# '@name' runs tools/scenarios/name.txt (steps separated by ';' or newlines, '#' comments).
if ($Steps.StartsWith('@')) {
    $file = "$PSScriptRoot\scenarios\$($Steps.Substring(1)).txt"
    $manifest.scenario_sha256 = (Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant()
    $Steps = (Get-Content $file | ForEach-Object { ($_ -split '#')[0] }) -join ';'
} else {
    $manifest.scenario_sha256 = Get-Sha256Text $Steps
}
$stepList = @($Steps -split '[;\r\n]' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($DryRun) {
    "instance $Instance : game $bin, user data $live, snapshots $saves\$tag-NAME"
    "deployed: $(Get-Deployed)"
    if ($ResetUserData) { "reset user data from $(if ($Baseline) { $Baseline } else { "$local\userdata-baseline" })" }
    $stepList | ForEach-Object { "step: $_" }
    return
}
if ($Instance -gt 0) {
    if (-not (Test-Path $bin)) { throw "no instance $Instance ($($inst.Root)): tools/instances.ps1 create" }
    $documentsBefore = Get-DocumentsStamp
}
# -ResetUserData restores the game's user-data folder from the baseline first, so runs start from the
# same state. It mirrors the baseline (removing anything else in that folder).
if ($ResetUserData) {
    $Baseline = (Resolve-Path $(if ($Baseline) { $Baseline } else { "$local\userdata-baseline" })).Path
    $manifest.baseline = $Baseline
    $manifest.baseline_sha256 = Get-TreeDigest $Baseline
    robocopy $Baseline $live /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "restoring user data failed ($LASTEXITCODE)" }
}
# Appends a host control command. The host polls the file, so a write can meet it briefly open: retry.
function Add-Control([string]$text) {
    for ($i = 0; ; $i++) {
        # .NET's append (write access only): Add-Content also opens the file for reading and failed with "Stream was
        # not readable" while the host had it open (smoke-1a96f5f-i3, three instances).
        try { [System.IO.File]::AppendAllText("$bin\ts2control.txt", "$text`r`n", [System.Text.Encoding]::ASCII); return }
        catch { if ($i -ge 200) { throw }; Start-Sleep -Milliseconds 50 }
    }
}

# How the game ended (game.ps1 writes ts2exit.txt next to the host log: "exit code N (0x...)" when the game ended by itself,
# "killed: ..." when stop force-closed it): printed once, after a quit step or at the end of the run, and kept in the
# manifest (snap: steps after it carry it).
$exitReported = $false
function Write-ExitLine {
    # The watcher writes the line moments after the process ends.
    for ($i = 0; $i -lt 40 -and -not (Test-Path "$bin\ts2exit.txt"); $i++) { Start-Sleep -Milliseconds 250 }
    $line = if (Test-Path "$bin\ts2exit.txt") { (Get-Content "$bin\ts2exit.txt" -TotalCount 1) } else { 'exit code unknown: no ts2exit.txt (game started without game.ps1 start?)' }
    $manifest.exit = $line
    Write-Output $line
    $script:exitReported = $true
}

# A failed run (a step throws, e.g. until: after the game faulted) keeps its host log and manifest as
# local/saves/<stamp>-failed.*, since the next run overwrites the live log.
try {
foreach ($step in $stepList) {
    $name, $arg = $step.Trim() -split ':', 2
    switch ($name) {
        'start' {
            & "$PSScriptRoot\game.ps1" start -GameArgs '-w' -Quiet:$Quiet -Original:$Original -Reference:$Reference -Seed $Seed -Draws:$Draws -Coverage $Coverage -FrameClock:$FrameClock -DeviceLog $DeviceLog -Instance $Instance -NoDeploy:$NoDeploy
            # [string]: Get-Content's string carries PSPath and friends, which ConvertTo-Json wrote into run.json.
            $deployedLine = Get-Deployed
            $manifest.deployed = if ($deployedLine) { [string]$deployedLine } else { $null }
            if ($Instance -gt 0) { "deployed: $($manifest.deployed)" }
        }
        'stop'  { & "$PSScriptRoot\game.ps1" stop -Instance $Instance }
        'quit'  {
            # The game's own quit (host control "quit"), then wait for the process to end by itself.
            Add-Control 'quit'
            $scale = if ($env:TS2_SCENARIO_TIMEOUT_SCALE) { [double]$env:TS2_SCENARIO_TIMEOUT_SCALE } else { 1 }
            $deadline = (Get-Date).AddSeconds($scale * $(if ($arg) { [int]$arg } else { 120 }))
            while (Get-InstanceProcess $Instance | Where-Object ProcessName -in 'Sims2EP9', 'ts2host') {
                if ((Get-Date) -gt $deadline) { throw "quit: the game did not exit within $(if ($arg) { $arg } else { 120 }) s" }
                Start-Sleep -Milliseconds 250
            }
            Write-ExitLine
        }
        'wait'  { Start-Sleep ([int]$arg) }
        'click' { $x, $y = $arg -split ','; & "$PSScriptRoot\click.ps1" -X ([int]$x) -Y ([int]$y) -Instance $Instance }
        'key'   { & "$PSScriptRoot\click.ps1" -Key ([Convert]::ToInt32($arg, 16)) -Instance $Instance }
        'type'  { & "$PSScriptRoot\click.ps1" -Text $arg -Instance $Instance }
        'shot'  { & "$PSScriptRoot\screenshot.ps1" -Out "$local\screenshots\$tag-$(if ($arg) { $arg } else { 'shot' }).png" -Instance $Instance }
        'log'   { Get-Content "$bin\ts2host.log" -Tail 1 }
        'ctl'   { Add-Control $arg }
        'cheat' { Add-Control "cheat $arg" }
        'snap'  {
            if (-not (Test-Path $saves)) { New-Item -ItemType Directory $saves | Out-Null }
            # The game's files can stay locked for a moment after a forced stop (smoke-f2daf79: the two Thumbnails
            # packages): wait for the instance's processes to be gone; snapstore retries locked files and prints errors.
            $deadline = (Get-Date).AddSeconds(30)
            while ((Get-InstanceProcess $Instance | Where-Object ProcessName -in 'Sims2EP9', 'ts2host') -and (Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 250
            }
            # tools/snapstore.py: the files the run left as they were are hardlinks to a content-addressed store
            # (local/snapstore), so a snapshot costs its changed files only (~60 MB of ~1 GB for lot-resave); it reads
            # as a robocopy copy did (contents and modification times). It retries files still locked for 30 s. Its
            # errors go to stderr as they are (2>&1 under ErrorActionPreference Stop would throw on the first line).
            $copyLog = & "$mainRoot\.venv\Scripts\python.exe" -I "$PSScriptRoot\snapstore.py" snap $live "$saves\$tag-$arg"
            if ($LASTEXITCODE -ne 0) {
                $copyLog | Select-Object -Last 10 | ForEach-Object { Write-Output "snapshot: $_" }
                throw "snapshot failed ($LASTEXITCODE)"
            }
            $copyLog | Select-Object -Last 1 | ForEach-Object { Write-Output "$_" }
            Copy-Item "$bin\ts2host.log" "$saves\$tag-$arg.ts2host.log"
            if (Test-Path "$bin\ts2coverage.txt") { Copy-Item "$bin\ts2coverage.txt" "$saves\$tag-$arg.ts2coverage.txt" }
            if (Test-Path "$bin\ts2devlog.txt") { Copy-Item "$bin\ts2devlog.txt" "$saves\$tag-$arg.ts2devlog.txt" }
            if (Test-Path "$bin\ts2wintree.txt") { Copy-Item "$bin\ts2wintree.txt" "$saves\$tag-$arg.ts2wintree.txt" }
            if (Test-Path "$bin\ts2services.txt") { Copy-Item "$bin\ts2services.txt" "$saves\$tag-$arg.ts2services.txt" }
            $manifest | ConvertTo-Json | Set-Content "$saves\$tag-$arg.run.json" -Encoding utf8
            Write-Output "snapshot: $saves\$tag-$arg"
            # Each snapshot is ~1 GB: keep the newest $KeepSnapshots folders (agreed with the user 2026-10-15, after the
            # disk filled). Their small .ts2host.log / .run.json files stay. Fixtures live in local\fixtures, untouched.
            # -SnapDir folders (smoke.py batches) are pruned by smoke.py, not here.
            if (-not $SnapDir) {
                $old = @(Get-ChildItem "$local\saves" -Directory | Where-Object { $_.Name -match '^\d{8}-\d{6}-' } |
                    Sort-Object Name -Descending | Select-Object -Skip $KeepSnapshots)
                $old | ForEach-Object {
                        # A file still open elsewhere must not fail the run: warn and leave it for the next snapshot.
                        try { Remove-Item $_.FullName -Recurse -Force -ErrorAction Stop }
                        catch { Write-Warning "could not prune $($_.TargetObject): $($_.Exception.Message)" }
                    }
                # Store objects no snapshot links any more (safe while other runs snapshot: snapstore retries).
                if ($old.Count) { & "$mainRoot\.venv\Scripts\python.exe" -I "$PSScriptRoot\snapstore.py" gc }
            }
        }
        'keep'  { # the run's small files only, no user-data snapshot (docs/ui-testing.md)
            if (-not (Test-Path $saves)) { New-Item -ItemType Directory $saves | Out-Null }
            foreach ($f in 'ts2host.log', 'ts2devlog.txt', 'ts2wintree.txt', 'ts2services.txt', 'ts2coverage.txt') {
                if (Test-Path "$bin\$f") { Copy-Item "$bin\$f" "$saves\$tag-$arg.$f" }
            }
            $manifest | ConvertTo-Json | Set-Content "$saves\$tag-$arg.run.json" -Encoding utf8
            Write-Output "kept: $saves\$tag-$arg.*"
        }
        'until' {
            $pattern, $limit = $arg -split ',', 2
            # TS2_SCENARIO_TIMEOUT_SCALE stretches every limit (slow diagnostic builds, e.g. build-espcheck).
            $scale = if ($env:TS2_SCENARIO_TIMEOUT_SCALE) { [double]$env:TS2_SCENARIO_TIMEOUT_SCALE } else { 1 }
            $deadline = (Get-Date).AddSeconds($scale * $(if ($limit) { [int]$limit } else { 120 }))
            while (-not (Select-String -Path "$bin\ts2host.log" -Pattern $pattern -Quiet -ErrorAction SilentlyContinue)) {
                if ((Get-Date) -gt $deadline) { throw "until: '$pattern' not seen in the host log" }
                # A game that has exited will never log it: fail now rather than at the deadline (unless -Original,
                # whose process also counts; Get-InstanceProcess finds both).
                if (-not (Get-InstanceProcess $Instance)) {
                    Start-Sleep -Milliseconds 500
                    if (-not (Select-String -Path "$bin\ts2host.log" -Pattern $pattern -Quiet -ErrorAction SilentlyContinue)) {
                        throw "until: '$pattern' not seen and the game has exited"
                    }
                }
                Start-Sleep -Milliseconds 250
            }
        }
        default { throw "unknown step '$step'" }
    }
}
if (-not $exitReported -and ($stepList | Where-Object { $_ -match '^(start|stop)\b' })) {
    if (Get-InstanceProcess $Instance | Where-Object ProcessName -in 'Sims2EP9', 'ts2host') { Write-Output 'exit code: none yet, the game is still running' }
    else { Write-ExitLine }
}
} catch {
    if (-not (Test-Path $saves)) { New-Item -ItemType Directory $saves | Out-Null }
    # A game that ended by itself (a fault, a trap) has its exit code; one still running is killed below.
    if (-not $exitReported -and -not (Get-InstanceProcess $Instance | Where-Object ProcessName -in 'Sims2EP9', 'ts2host')) { Write-ExitLine }
    Copy-Item "$bin\ts2host.log" "$saves\$tag-failed.ts2host.log" -ErrorAction SilentlyContinue
    if ($DeviceLog) { Copy-Item "$bin\ts2devlog.txt" "$saves\$tag-failed.ts2devlog.txt" -ErrorAction SilentlyContinue }
    $manifest | ConvertTo-Json | Set-Content "$saves\$tag-failed.run.json" -Encoding utf8
    Write-Output "failed: $_ (host log kept as $saves\$tag-failed.ts2host.log)"
    # Don't leave the instance running: the next run would refuse to start.
    try { & "$PSScriptRoot\game.ps1" stop -Instance $Instance | Out-Null } catch {}
    throw
} finally {
    if ($Instance -gt 0 -and $documentsBefore -ne (Get-DocumentsStamp)) {
        Write-Output "WARNING: the Documents user-data folder changed during this instance run (another game running, or a path the instance doesn't redirect)"
    }
}
