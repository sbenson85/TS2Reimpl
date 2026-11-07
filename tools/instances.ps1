# Parallel game instances for smoke runs (tools/smoke.py). Instance N is local/game-N: the same
# folder tree as local/game (the frozen copy), every file a hardlink to local/game's (the 13 GB of
# assets cost nothing) except EP9\TSBin, which is a real copy (the host and the game write there:
# ts2host.log, ts2control.txt, simtrace.txt, ts2reimpl.ini, ts2deploy.txt; deploy.ps1 replaces the
# exe and DLLs). Its ts2reimpl.ini [Instance] makes the host give the game its own mutex names and
# user-data folders (local/userdata-N, local/game-N/allusers), and [Steam] Stub=1 the Steam stub
# (docs/contracts/steam.md). local/game itself is never written; instance 0 means local/game.
#
#   instances.ps1 create -Count 3 [-DryRun] [-RealSteam] [-Language english]
#                                    make local/game-1..3 that don't exist yet (a few seconds each);
#                                    -DryRun prints what would be linked, copied and written
#   instances.ps1 status             each instance: deployed commit, running processes, user data
#   instances.ps1 check              local/game files written since an instance was made (a write
#                                    through a hardlink would change local/game: must list nothing)
#   instances.ps1 remove -Instance 2 delete local/game-2 (links only; local/game keeps its files)
#                                    and local/userdata-2
#   instances.ps1 readonly [-Undo] [-DryRun]
#                                    PROPOSED (lane FV), not yet decided: set (or clear) the read-only attribute
#                                    on every file of local/game outside EP9\TSBin. Attributes belong to the file,
#                                    so every hardlink (all instances) gets it at once. Why: the content-folder
#                                    opener (0x1e3c70) probes each .package with a write open; when that succeeds
#                                    the segment is opened read+write with FILE_SHARE_READ only, so the first
#                                    instance holds write handles on the shared files and the second instance's
#                                    read opens fail with a sharing violation. Read-only files fail the probe, as a
#                                    Steam install under Program Files does, and every instance opens them for
#                                    reading. Changes local/game (instance 0) the same way.
param(
    [Parameter(Mandatory)][ValidateSet('create', 'status', 'check', 'remove', 'readonly')][string]$Action,
    [int]$Count = 0,
    [int[]]$Instance,
    [switch]$DryRun,
    [switch]$RealSteam,
    [switch]$Undo,
    [string]$Language = 'english'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
$source = (Get-Instance 0).Root
# TSBin files that belong to one run or one deploy, not copied from local/game.
$perRun = @('ts2host.log', 'ts2reimpl.log', 'simtrace.txt', 'ts2control.txt', 'ts2reimpl.ini', 'ts2deploy.txt')

if (-not ('TS2Link' -as [type])) {
    Add-Type -Namespace '' -Name TS2Link -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
public static extern bool CreateHardLink(string newFile, string existingFile, System.IntPtr security);
'@
}

function Get-Existing {
    Get-ChildItem $local -Directory -Filter 'game-*' | Where-Object Name -match '^game-(\d+)$' |
        ForEach-Object { [int]($_.Name.Substring(5)) } | Sort-Object
}

function Write-InstanceIni([object]$inst, [string]$path) {
    $lines = @(
        '[Hooks]',
        '',
        '; Written by tools/instances.ps1. The host reads [Instance] and [Steam] (host/instance.h).',
        '[Instance]',
        "Id=$($inst.Id)",
        "UserDataDir=$($inst.UserData)\",
        "AllUsersDataDir=$($inst.AllUsers)\",
        '',
        '[Steam]',
        "Stub=$(if ($RealSteam) { 0 } else { 1 })",
        "Language=$Language"
    )
    if ($DryRun) { "  write $path :"; $lines | ForEach-Object { "    $_" }; return }
    Set-Content -Path $path -Value $lines -Encoding ascii
}

switch ($Action) {
    'create' {
        if ($Count -lt 1) { throw 'create needs -Count N (instances 1..N)' }
        if ("$local" -match ' ') { throw "the instance folders would contain a space ($local): the game's -activeUserDataDir can't take it" }
        $files = Get-ChildItem $source -Recurse -File
        $dirs = Get-ChildItem $source -Recurse -Directory
        $binPrefix = "$source\EP9\TSBin\"
        foreach ($n in 1..$Count) {
            $inst = Get-Instance $n
            if (Test-Path $inst.Root) { "instance ${n}: $($inst.Root) exists, kept"; continue }
            $started = Get-Date
            $linked = 0; $copied = @()
            if ($DryRun) { "instance ${n}: would create $($inst.Root)" } else { New-Item -ItemType Directory $inst.Root | Out-Null }
            foreach ($d in $dirs) {
                $target = $inst.Root + $d.FullName.Substring($source.Length)
                if (-not $DryRun) { New-Item -ItemType Directory $target -Force | Out-Null }
            }
            foreach ($f in $files) {
                $rel = $f.FullName.Substring($source.Length)
                $target = $inst.Root + $rel
                if ($f.FullName.StartsWith($binPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                    if ($perRun -contains $f.Name -and $f.DirectoryName -eq $binPrefix.TrimEnd('\')) { continue }
                    $copied += $rel
                    if ($DryRun) { "  copy $rel ($([int]($f.Length / 1KB)) KB)" } else { Copy-Item $f.FullName $target }
                } else {
                    $linked++
                    if (-not $DryRun -and -not [TS2Link]::CreateHardLink($target, $f.FullName, [IntPtr]::Zero)) {
                        throw "hardlink $target -> $($f.FullName) failed (error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
                    }
                }
            }
            if ($DryRun) { "  link $linked files (everything outside EP9\TSBin), e.g. $($files[0].FullName.Substring($source.Length))" }
            Write-InstanceIni $inst "$($inst.Bin)\ts2reimpl.ini"
            foreach ($d in @($inst.UserData, $inst.AllUsers)) {
                if ($DryRun) { "  folder $d" } elseif (-not (Test-Path $d)) { New-Item -ItemType Directory $d | Out-Null }
            }
            $manifest = [ordered]@{ id = $n; created = $started.ToString('o'); source = $source; linked = $linked; copied = $copied;
                user_data = $inst.UserData; all_users = $inst.AllUsers; steam_stub = -not $RealSteam; language = $Language }
            if ($DryRun) { "  write $($inst.Root)\instance.json" }
            else {
                $manifest | ConvertTo-Json | Set-Content "$($inst.Root)\instance.json" -Encoding utf8
                "instance ${n}: $($inst.Root) ($linked linked, $($copied.Count) copied, $([int]((Get-Date) - $started).TotalSeconds) s); deploy.ps1 next"
            }
        }
    }
    'status' {
        foreach ($n in @(0) + @(Get-Existing)) {
            $inst = Get-Instance $n
            $deploy = if (Test-Path "$($inst.Bin)\ts2deploy.txt") { (Get-Content "$($inst.Bin)\ts2deploy.txt" -TotalCount 1) } else { 'not deployed (no ts2deploy.txt)' }
            $p = @(Get-InstanceProcess $n | Where-Object ProcessName -ne 'crashpad_handler')
            $run = if ($p) { "running pid $($p.Id -join ',')" } else { 'idle' }
            "instance ${n}: $run; $deploy; user data $($inst.UserData)"
        }
    }
    'check' {
        $bad = 0
        foreach ($n in Get-Existing) {
            $m = Get-Content "$((Get-Instance $n).Root)\instance.json" -Raw | ConvertFrom-Json
            $since = [datetime]$m.created
            $written = Get-ChildItem $source -Recurse -File | Where-Object {
                $_.LastWriteTime -gt $since -and -not $_.FullName.StartsWith("$source\EP9\TSBin\", [StringComparison]::OrdinalIgnoreCase) }
            foreach ($w in $written) { "instance ${n}: shared file written since $since : $($w.FullName)"; $bad++ }
        }
        if ($bad) { "$bad shared file(s) changed: the game writes there; copy them per instance (add to the TSBin rule in instances.ps1)"; exit 1 }
        'no shared file changed since the instances were made'
    }
    'readonly' {
        $running = @(@(0) + @(Get-Existing) | Where-Object { Get-InstanceProcess $_ })
        if ($running -and -not $DryRun) { throw "instance(s) $($running -join ', ') running: stop them first" }
        $want = -not $Undo
        $files = Get-ChildItem $source -Recurse -File | Where-Object {
            -not $_.FullName.StartsWith("$source\EP9\TSBin\", [StringComparison]::OrdinalIgnoreCase) -and $_.IsReadOnly -ne $want }
        $verb = if ($want) { 'set' } else { 'clear' }
        if ($DryRun) { "would $verb read-only on $(@($files).Count) file(s) of $source (outside EP9\TSBin) and so on their hardlinks"; break }
        foreach ($f in $files) { $f.IsReadOnly = $want }
        "read-only ${verb}: $(@($files).Count) file(s) of $source outside EP9\TSBin (every instance's hardlinks share it)"
    }
    'remove' {
        if (-not $Instance) { throw 'remove needs -Instance N[,M]' }
        foreach ($n in $Instance) {
            if ($n -le 0) { throw 'instance 0 is local/game: never removed' }
            if (Get-InstanceProcess $n) { throw "instance $n is running" }
            $inst = Get-Instance $n
            foreach ($d in @($inst.Root, $inst.UserData)) {
                if ($DryRun) { "would remove $d" } elseif (Test-Path $d) { Remove-Item $d -Recurse -Force; "removed $d" }
            }
        }
    }
}
