# Builds the host (host/: our exe, which maps the game image and installs our replacements before it
# starts) and deploys it into the game copy's TSBin: ts2host.exe, ts2tls.dll and the unpacked image
# (Sims2EP9.unpacked.exe). game.ps1 start runs it (and deploys first). Log: TSBin\ts2host.log.
# One build goes to local/game and to every instance local/game-N (tools/instances.ps1); each TSBin
# gets ts2deploy.txt (commit, dirty flag, build and deploy times), which the host logs first, so a
# run's log says what it tested. -DryRun prints the targets and the stamp without building or copying.
# Only from the main checkout: worktrees build and test, but the game runs what's merged.
param([switch]$FromWorktree, [switch]$DryRun)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
if ($isWorktree -and -not $FromWorktree -and -not $DryRun) { throw 'Deploy from the main checkout (or pass -FromWorktree deliberately)' }
$targets = @((Get-Instance 0).Bin) + @(Get-ChildItem $local -Directory -Filter 'game-*' | Where-Object Name -match '^game-\d+$' |
    Sort-Object { [int]$_.Name.Substring(5) } | ForEach-Object { "$($_.FullName)\EP9\TSBin" })
# TS2_DEPLOY_BUILD=<folder>: deploy that build folder's host as it is, unbuilt (e.g. build-espcheck, the
# diagnostic build in CMakeLists.txt), for every start until the variable is cleared.
$from = $build
if ($env:TS2_DEPLOY_BUILD) { $from = Join-Path $root $env:TS2_DEPLOY_BUILD } elseif (-not $DryRun) { Invoke-Build ts2host }
if (-not $DryRun -and (Get-Process ts2host, Sims2EP9 -ErrorAction SilentlyContinue)) { throw 'Stop the game before deploying' }
$commit = (git -C $root rev-parse --short=10 HEAD)
$dirty = if (git -C $root status --porcelain --untracked-files=no) { ' +uncommitted changes' } else { '' }
$exe = "$from\Release\ts2host.exe"
$built = if (Test-Path $exe) { (Get-Item $exe).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') } else { '(not built)' }
$stamp = "commit $commit$dirty, built $built, deployed $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') from $from"
$image = "$local\original\Sims2EP9.exe.unpacked.exe"
foreach ($bin in $targets) {
    if ($DryRun) { "would deploy to $bin : $stamp"; continue }
    Copy-Item "$from\Release\ts2host.exe", "$from\Release\ts2tls.dll" $bin -Force
    $deployed = "$bin\Sims2EP9.unpacked.exe"
    if (-not (Test-Path $deployed) -or (Get-Item $image).LastWriteTime -gt (Get-Item $deployed).LastWriteTime) {
        Copy-Item $image $deployed -Force
    }
    Set-Content "$bin\ts2deploy.txt" $stamp -Encoding ascii
}
if (-not $DryRun) { "deployed ts2host.exe, ts2tls.dll to $($targets.Count) folder(s): $stamp" }
