# Controls the game copy in local/game (never the Steam install). The game runs through our host exe,
# ts2host.exe (host/), with our replacements installed; deploy.ps1 builds and copies it (start does).
#   game.ps1 start [-GameArgs '-w']   launch (refuses if an instance is already running)
#   game.ps1 stop                     force-close the game copy and its helper processes (writes
#                                     "killed: ..." to ts2exit.txt when the game was still running)
#   ts2exit.txt (beside ts2host.log)  after start: "exit code N (0x...)" once the game ends by
#                                     itself (a watcher started with it), with "trap fired" /
#                                     "trap not armed" for the host traps' codes; or the stop line
#   game.ps1 status                   print whether it's running
#   game.ps1 boot [-Wait 30]          start, wait, screenshot, stop: a boot smoke test
#   -Quiet (start/boot)               window off-screen, focus kept by the user's window, audio
#                                     muted (the host does it; for runs while the user is around)
#   -Original (start/boot)            run the untouched Sims2EP9.exe instead, without our code
#                                     (for comparing behaviour with the original)
#   -Reference (start/boot)           run the host without replacements (TS2HOST_NOHOOKS): the
#                                     original code with the host's control layer (milestone B)
#   -Seed N (start/boot)              replace the game's clock seeds with a fixed sequence from N
#                                     (TS2HOST_SEED, host/control_seed.cpp)
#   -Draws (start/boot, with -Seed)   count generator draws for the "draws" control command
#                                     (TS2HOST_DRAWS; replacement runs only)
#   -Coverage LIST (start/boot)       first-call breakpoints on the original functions in LIST (a site
#                                     list from tools/coverage.py sites; TS2HOST_COVERAGE, a diagnostic):
#                                     the host writes ts2coverage.txt beside its log
#   -FrameClock (start/boot)          the clocks the game reads advance by a fixed frame time per
#                                     framework tick (TS2HOST_FRAMECLOCK=1, docs/ui-testing.md)
#   -DeviceLog SPEC (start/boot)      log the game's Direct3D 9 calls (TS2HOST_DEVLOG): SPEC 'on' for
#                                     per-frame digests only, or frames to list in full ('900,4000-4002');
#                                     the host writes ts2devlog.txt beside its log (tools/devlog.py)
#   -Instance N                      act on local/game-N (tools/instances.ps1) instead of local/game:
#                                     never deploys (deploy.ps1 or smoke.py first), only refuses when
#                                     that instance runs; other instances may run beside it
#   -NoDeploy (start/boot)            instance 0: start what is deployed, without building
#   -DryRun (start)                   print what would be launched, launch nothing
param(
    [Parameter(Mandatory)][ValidateSet('start', 'stop', 'status', 'boot')][string]$Action,
    [string]$GameArgs = '-w',
    [int]$Wait = 30,
    [switch]$Quiet,
    [switch]$Original,
    [switch]$Reference,
    [string]$Seed,
    [switch]$Draws,
    [string]$Coverage,
    [switch]$FrameClock,
    [string]$DeviceLog,
    [int]$Instance = 0,
    [switch]$NoDeploy,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
$inst = Get-Instance $Instance
$bin  = $inst.Bin

function Get-Game { Get-InstanceProcess $Instance }

# How the last run ended: start removes ts2exit.txt (next to ts2host.log) and starts a hidden watcher that holds a handle
# on the game process and, when it ends, writes one line there: "exit code N (0xNNNNNNNN)", plus what a host code means
# (trap fired / trap not armed, lane GE's traps). stop writes "killed: ..." there first when it force-closes a running
# game, and the watcher then leaves that line alone, so a killed run never reports the kill's code as the game's.
$exitFile = "$bin\ts2exit.txt"
$watcherScript = @'
param([int]$Id, [string]$Out, [string]$HostLog)
try { $p = [System.Diagnostics.Process]::GetProcessById($Id); $null = $p.Handle } catch { exit }
$p.WaitForExit()
$code = $p.ExitCode
$line = 'exit code {0} (0x{1:X8})' -f $code, $code
if ($code -eq 0x54524150) { $line += ': trap fired (the host log has a "trap: original" line)' }
elseif ($code -eq 1 -and (Test-Path $HostLog) -and (Select-String -Path $HostLog -SimpleMatch "can't be guarded" -Quiet)) { $line += ': trap not armed (see the host log)' }
Start-Sleep -Milliseconds 300   # stop writes its "killed" line before Stop-Process; let it land
if ((Test-Path $Out) -and (Select-String -Path $Out -Pattern '^killed' -Quiet)) { exit }
Set-Content -Path $Out -Value $line -Encoding ascii
'@
function Start-ExitWatcher([int]$Id) {
    $text = "& { $watcherScript } -Id $Id -Out '$exitFile' -HostLog '$bin\ts2host.log'"
    $encoded = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($text))
    Start-Process powershell.exe -WindowStyle Hidden -ArgumentList '-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded | Out-Null
}

switch ($Action) {
    'status' { $g = Get-Game; if ($g) { $g | Select-Object Id, ProcessName, Responding, @{n = 'MB'; e = { [int]($_.WS / 1MB) } } } else { 'not running' } }
    'stop' {
        $g = @(Get-Game)
        if ($g | Where-Object ProcessName -in 'Sims2EP9', 'ts2host') {
            Set-Content -Path $exitFile -Value 'killed: game.ps1 stop force-closed the game (Stop-Process -Force); no exit code of its own' -Encoding ascii
        }
        $g | Stop-Process -Force
        'stopped'
    }
    'start' {
        if ($Instance -gt 0) {
            if (-not (Test-Path $bin)) { throw "no instance $Instance ($($inst.Root)): tools/instances.ps1 create" }
            if ($Original) { throw '-Original runs the untouched exe, which uses the shared mutex, Steam and Documents: not in an instance' }
            if (Get-Game | Where-Object ProcessName -ne 'crashpad_handler') { throw "instance $Instance is already running" }
            if (-not (Test-Path "$bin\ts2deploy.txt")) { throw "instance $Instance has no deployed host: run deploy.ps1 (or smoke.py) first" }
        } elseif (Get-Process Sims2EP9, ts2host -ErrorAction SilentlyContinue) { throw 'A game instance is already running' }
        # Quiet mode is our code (the host); the untouched exe would show its window.
        if ($Original -and $Quiet) { throw '-Quiet needs the host: the original exe runs visibly (only when the user is away)' }
        if (-not $Original -and $Instance -eq 0 -and -not $NoDeploy -and -not $DryRun) { & "$PSScriptRoot\deploy.ps1" | Out-Null }
        $exe = if ($Original) { "$bin\Sims2EP9.exe" } else { "$bin\ts2host.exe" }
        $argList = @($GameArgs -split ' ' | Where-Object { $_ })
        if ($DryRun) {
            "would start $exe in $bin with '$GameArgs'" + $(if ($Quiet) { ', quiet' } else { '' }) + $(if ($Seed) { ", seed $Seed" } else { '' }) + $(if ($FrameClock) { ', frame clock' } else { '' }) + $(if ($DeviceLog) { ", device log $DeviceLog" } else { '' })
            if (Test-Path "$bin\ts2deploy.txt") { "deployed: $(Get-Content "$bin\ts2deploy.txt" -TotalCount 1)" }
            return
        }
        if (-not (Test-Path "$bin\steam_appid.txt")) { Set-Content "$bin\steam_appid.txt" '3314070' -Encoding ascii -NoNewline }
        if ($Quiet) { $env:TS2REIMPL_QUIET = '1' } else { Remove-Item Env:TS2REIMPL_QUIET -ErrorAction SilentlyContinue }
        if ($Reference) { $env:TS2HOST_NOHOOKS = '1' } else { Remove-Item Env:TS2HOST_NOHOOKS -ErrorAction SilentlyContinue }
        if ($Seed) { $env:TS2HOST_SEED = $Seed } else { Remove-Item Env:TS2HOST_SEED -ErrorAction SilentlyContinue }
        if ($Draws) { $env:TS2HOST_DRAWS = '1' } else { Remove-Item Env:TS2HOST_DRAWS -ErrorAction SilentlyContinue }
        if ($Coverage) { $env:TS2HOST_COVERAGE = (Resolve-Path $Coverage).Path } else { Remove-Item Env:TS2HOST_COVERAGE -ErrorAction SilentlyContinue }
        Remove-Item "$bin\ts2coverage.txt" -ErrorAction SilentlyContinue # a previous run's: never read as this run's
        if ($FrameClock) { $env:TS2HOST_FRAMECLOCK = '1' } else { Remove-Item Env:TS2HOST_FRAMECLOCK -ErrorAction SilentlyContinue }
        if ($DeviceLog) { $env:TS2HOST_DEVLOG = $DeviceLog } else { Remove-Item Env:TS2HOST_DEVLOG -ErrorAction SilentlyContinue }
        # wintree, services: control commands' files (host/control_wintree.cpp, host/control_services.cpp)
        Remove-Item "$bin\ts2devlog.txt", "$bin\ts2wintree.txt", "$bin\ts2services.txt" -ErrorAction SilentlyContinue
        # control commands: cleared here, not by the host, whose start can outlast a scenario's first "wait" (host/control.cpp)
        Remove-Item "$bin\ts2control.txt" -ErrorAction SilentlyContinue
        Remove-Item $exitFile -ErrorAction SilentlyContinue
        $p = if ($argList) { Start-Process $exe -WorkingDirectory $bin -ArgumentList $argList -PassThru }
             else { Start-Process $exe -WorkingDirectory $bin -PassThru }
        Start-ExitWatcher $p.Id
        "started pid $($p.Id) with '$GameArgs'" + $(if ($Instance -gt 0) { " (instance $Instance)" } else { '' })
    }
    'boot' {
        & $PSCommandPath start -GameArgs $GameArgs -Quiet:$Quiet -Original:$Original -Reference:$Reference -Seed $Seed -Draws:$Draws -Coverage $Coverage -FrameClock:$FrameClock -DeviceLog $DeviceLog -Instance $Instance -NoDeploy:$NoDeploy
        Start-Sleep $Wait
        $g = Get-Game | Where-Object ProcessName -in 'Sims2EP9', 'ts2host'
        if (-not $g) { 'game exited during boot'; exit 1 }
        & "$PSScriptRoot\screenshot.ps1" -Instance $Instance
        & $PSCommandPath stop -Instance $Instance | Out-Null
        'boot ok'
    }
}
