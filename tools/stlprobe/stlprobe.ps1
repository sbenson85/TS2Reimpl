# Builds the STL probe objects libmatch.py matches the game's template instantiations against (library code, never
# hooked; docs/verification.md). Each source is compiled with several flag sets, because EA's translation units differ:
#   /EHsc /O1        most of the game's STL code;
#   /EHsc /O2, /EHsc /O2 /Ob1   other translation units;
#   /EHsc /O1 /GS-   units built without the security cookie (lane MJ);
#   /O1, /O2         units built without exception handling (no /EHsc: no unwind frames; lane MJ: most of the
#                    game's iostream code, and many container members);
# FlagSets lists them; the first three keep their old object indices (probe0..2), so rows' "objects" stay stable.
#   probe.cpp        named instantiations (strings, the game's own key/value types);
#   probe_ios.cpp    iostreams (filebuf, fstreams, stringstreams, num_get/num_put: libcpmt has only their non-template
#                    parts under /MT);
#   probe_sizes.cpp  containers over N-byte stand-in elements, with $SizeFlagSets (/O2 without /EHsc added nothing),
#                    built in parts (-DPART=n; one object holds only ~65k
#                    sections), trivial and non-trivial (-DNONTRIVIAL), and for $AllocFlagSets also over the stand-in
#                    for the game's allocator-service allocator (-DALLOC=1, objects probe_sizesA*); std::map's own
#                    members in objects of their own (-DMAPCLASS=1, probe_maps*: beside _Tree they change its /O2 code);
#   probe_tu.cpp     small translation units (code that matches only when little else is instantiated beside it),
#                    built with $TuFlagSets, which adds /O1 /Oi (one of them matches only so).
#   probe_members.cpp  container members that are templates (push_back's _Emplace_reallocate, insert, emplace,
#                    try_emplace, assign, range constructors), which an explicit class instantiation never instantiates,
#                    many per unit, with $MemberFlagSets (lane WB);
#   probe_one.cpp    the same members one per unit (-DFAM -DTI -DOP; OP 99: all of one type's), with $OneFlagSets: a
#                    helper called from one place in its unit is inlined, so one member has several shapes (lane WB).
#                    Hundreds of tiny units: compiled in batches under one vcvars call.
#   probe_render.cpp the rendering campaign's instantiations over the game's element types (-DFAM -DTI -DOP, as
#                    probe_one.cpp), with $RenderFlagSets and /std:c++14: several of the game's units match only in
#                    C++14 mode (pair's operator<, the uninitialized-copy loops; lane WJ). The other sources stay C++17.
#   probe_e11.cpp, probe_rl3.cpp  the effects collection's containers over its real records, and the shader lists' sorts
#                    (lane XI). $Rl2FlagSets and $RenderFlagSets gained /EHsc /O2 /GS- (the effects units) and /Ob3 sets: some
#                    of the game's units only inline as they do under /Ob3 (0xbd76f0, the collider's special members).
#   probe_lmm.cpp    the light map manager's two unordered_maps over the game's keys and functors (lane 261108q), three
#                    single units with their own flags.
# -Only SOURCE,... rebuilds only those sources' objects and keeps the rest of -Out (to try a probe change quickly);
# -Only14 keeps only the C++14 copies (probe14*.obj) of the sources selected.
# -Missing builds only the objects -Out lacks (a flag set added at the end of a list, or an interrupted build).
# Objects go to -Out (default: the main checkout's local/stlprobe, which libmatch.py reads; a lane builds into its
# own directory, -Only X -Out DIR, and passes the objects to libmatch.py --add-objects DIR; base.txt, written by a partial
# build into its own -Out, also lets libmatch.py --stlprobe DIR read the rest from local/stlprobe). sources.sha256 records the sources they were built from:
# libmatch.py warns when it is stale. -Score runs libmatch.py --dry-run on each object (what it alone adds).
param([string[]]$FlagSets = @('/EHsc /O1', '/EHsc /O2', '/EHsc /O2 /Ob1', '/EHsc /O1 /GS-', '/O1', '/O2'),
      [string[]]$SizeFlagSets = @('/EHsc /O1', '/EHsc /O2', '/EHsc /O2 /Ob1', '/EHsc /O1 /GS-', '/O1'),
      [string[]]$AllocFlagSets = @('/EHsc /O1', '/EHsc /O2', '/O1'), [int]$SizeParts = 4,
      [string[]]$TuFlagSets = @('/EHsc /O1', '/EHsc /O2', '/EHsc /O2 /Ob1', '/EHsc /O1 /Oi'), [int]$TuParts = 2,
      [string[]]$MemberFlagSets = @('/EHsc /O1', '/EHsc /O2', '/O1'), [int]$MemberParts = 4,
      [string[]]$OneFlagSets = @('/EHsc /O2', '/EHsc /O1', '/EHsc /O2 /Ob1', '/O1'),
      [string[]]$RenderFlagSets = @('/EHsc /O2', '/EHsc /O1', '/EHsc /O2 /GS-', '/EHsc /O1 /GS-', '/EHsc /O2 /Ob1 /GS-', '/EHa /O2 /GS-',
                                 '/EHsc /O2 /GS- /Ob3', '/EHsc /O2 /Ob3', '/EHa /O2 /GS- /Ob3'),
      [string[]]$FontFlagSets = @('/EHsc /O1', '/EHsc /O1 /GS-', '/EHsc /O1 /Oi /GS-', '/O1', '/EHsc /O1 /Ob1', '/O1 /Ob1', '/O1 /GS-'),
      [string[]]$Rl2FlagSets = @('/EHsc /O2', '/EHsc /O1', '/EHsc /O1 /Ob1', '/EHs /O2', '/EHa /O2', '/EHa /O1', '/EHa /O2 /GS-', '/EHa /O1 /GS-',
                              '/EHsc /O2 /GS-', '/EHs /O2 /GS-', '/EHsc /O1 /GS-', '/EHsc /O2 /GS- /Ob3', '/EHsc /O2 /Ob3', '/EHa /O2 /GS- /Ob3'),
      [string]$Out = '', [int]$Jobs = 4, [string[]]$Only = @(), [switch]$Only14, [switch]$Missing, [switch]$Score)
$ErrorActionPreference = 'Stop'
$common = (git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir)
$main = Split-Path $common  # the main checkout (local/ and .venv live there, also when run from a worktree)
$python = "$main\.venv\Scripts\python.exe"
$libmatch = Resolve-Path "$PSScriptRoot\..\libmatch.py"
if (-not $Out) { $Out = "$main\local\stlprobe" }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
Remove-Item "$Out\build*.cmd", "$Out\sources.sha256" -ErrorAction SilentlyContinue
$partial = $Only.Count -or $Missing  # the rest of -Out is kept (and may be stale: no stamp written)
if (-not $partial) { Remove-Item "$Out\probe*.obj", "$Out\base.txt" -ErrorAction SilentlyContinue }
$mainProbes = (New-Item -ItemType Directory -Force "$main\local\stlprobe").FullName
if ($partial -and $Out -ne $mainProbes) { Set-Content "$Out\base.txt" $mainProbes -Encoding ascii }
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'

$jobs_ = @()
$i = 0
foreach ($flags in $FlagSets) {
    $jobs_ += @{ src = 'probe.cpp'; obj = "probe$i.obj"; flags = $flags; def = '' }
    $jobs_ += @{ src = 'probe_ios.cpp'; obj = "probe_ios$i.obj"; flags = $flags; def = '' }
    foreach ($nt in @(0, 1) | Where-Object { $SizeFlagSets -contains $flags }) {
        for ($p = 0; $p -lt $SizeParts; $p++) {
            $jobs_ += @{ src = 'probe_sizes.cpp'; obj = "probe_sizes${nt}${p}_$i.obj"; flags = $flags; def = "/DPART=$p /DNONTRIVIAL=$nt" }
            if ($AllocFlagSets -contains $flags) {
                $jobs_ += @{ src = 'probe_sizes.cpp'; obj = "probe_sizesA${nt}${p}_$i.obj"; flags = $flags
                             def = "/DPART=$p /DNONTRIVIAL=$nt /DALLOC=1" }
            }
        }
        foreach ($h in 0, 1) {  # std::map's own members, apart from _Tree (-DMAPCLASS=1), half the sizes each
            $jobs_ += @{ src = 'probe_sizes.cpp'; obj = "probe_maps${nt}${h}_$i.obj"; flags = $flags
                         def = "/DPART=$(4 + $h) /DNONTRIVIAL=$nt /DMAPCLASS=1" }
            if ($AllocFlagSets -contains $flags) {
                $jobs_ += @{ src = 'probe_sizes.cpp'; obj = "probe_mapsA${nt}${h}_$i.obj"; flags = $flags
                             def = "/DPART=$(4 + $h) /DNONTRIVIAL=$nt /DMAPCLASS=1 /DALLOC=1" }
            }
        }
    }
    $i++
}
$i = 0
foreach ($flags in $TuFlagSets) {
    for ($p = 0; $p -lt $TuParts; $p++) {
        $jobs_ += @{ src = 'probe_tu.cpp'; obj = "probe_tu${p}_$i.obj"; flags = $flags; def = "/DPART=$p" }
    }
    $i++
}
$i = 0
foreach ($flags in $MemberFlagSets) {
    for ($p = 0; $p -lt $MemberParts; $p++) {
        $jobs_ += @{ src = 'probe_members.cpp'; obj = "probe_members${p}_$i.obj"; flags = $flags; def = "/DPART=$p" }
    }
    $i++
}
# probe_one.cpp: family -> number of types; the members each family has (99: all of them in one unit).
$oneTypes = @{ 0 = 43; 1 = 16; 2 = 16; 3 = 16; 4 = 16; 5 = 16; 6 = 16; 7 = 8; 8 = 1; 9 = 8; 10 = 8; 11 = 8 }
$i = 0
foreach ($flags in $OneFlagSets) {
    foreach ($fam in 0..11) {
        $ops = if ($fam -in 0, 9) { 0..17 } elseif ($fam -eq 8) { 0..27 } elseif ($fam -in 1, 2, 5, 10, 11) { 0..14 }
               else { (0..10) + 13, 14 }
        for ($t = 0; $t -lt $oneTypes[$fam]; $t++) {
            foreach ($op in @($ops) + 99) {
                $jobs_ += @{ src = 'probe_one.cpp'; obj = "probe_one${fam}_${t}_${op}_$i.obj"; flags = $flags
                             def = "/DFAM=$fam /DTI=$t /DOP=$op"; batch = $true }
            }
        }
    }
    $i++
}
# probe_render.cpp: family -> (number of types, number of members); 99: all of a family's members in one unit.
$renderFams = @{ 0 = @(4, 7); 1 = @(4, 5); 2 = @(1, 4); 3 = @(1, 4); 4 = @(2, 7); 5 = @(1, 7); 6 = @(3, 7) }
$i = 0
foreach ($flags in $RenderFlagSets) {
    foreach ($fam in 0..6) {
        for ($t = 0; $t -lt $renderFams[$fam][0]; $t++) {
            foreach ($op in @(0..($renderFams[$fam][1] - 1)) + 99) {
                $jobs_ += @{ src = 'probe_render.cpp'; obj = "probe_render${fam}_${t}_${op}_$i.obj"; flags = $flags
                             def = "/DFAM=$fam /DTI=$t /DOP=$op"; batch = $true; std = 'c++14' }
            }
        }
    }
    $i++
}
# probe_font.cpp: the font and text classes' containers (u16string, unordered_set/map over uint and string, a
# vector<int16_t> emplace), one member per unit (-DOP; 99: all), C++14, with $FontFlagSets (lane WL). Several of the
# game's units here are /O1 with /Ob1 or /GS-, which the other sets lack.
$i = 0
foreach ($flags in $FontFlagSets) {
    foreach ($op in (0..11) + 20 + 99) {
        $jobs_ += @{ src = 'probe_font.cpp'; obj = "probe_font${op}_$i.obj"; flags = $flags; def = "/DOP=$op"
                     batch = $true; std = 'c++14' }
    }
    $i++
}
# probe_rl2.cpp: family -> (number of types, number of members), as probe_render.cpp (lane WM).
$rl2Fams = @{ 0 = @(3, 2); 1 = @(3, 7); 2 = @(4, 5); 3 = @(2, 4); 4 = @(8, 6); 5 = @(3, 5) }
$i = 0
foreach ($flags in $Rl2FlagSets) {
    foreach ($fam in 0..5) {
        for ($t = 0; $t -lt $rl2Fams[$fam][0]; $t++) {
            foreach ($op in @(0..($rl2Fams[$fam][1] - 1)) + 99) {
                $jobs_ += @{ src = 'probe_rl2.cpp'; obj = "probe_rl2${fam}_${t}_${op}_$i.obj"; flags = $flags
                             def = "/DFAM=$fam /DTI=$t /DOP=$op"; batch = $true; std = 'c++14' }
            }
        }
    }
    $i++
}
# probe_e1.cpp: the effect descriptions' part vectors (lane WO), family 0..5, TI 0..1, members 0..6, with $Rl2FlagSets.
$i = 0
foreach ($flags in $Rl2FlagSets) {
    foreach ($fam in 0..5) {
        foreach ($t in 0, 1) {
            foreach ($op in (0..6) + 99) {
                $jobs_ += @{ src = 'probe_e1.cpp'; obj = "probe_e1${fam}_${t}_${op}_$i.obj"; flags = $flags
                             def = "/DFAM=$fam /DTI=$t /DOP=$op"; batch = $true; std = 'c++14' }
            }
        }
    }
    $i++
}
# probe_e11.cpp: the effects collection's containers (lane XI), family 0..7, TI 0..1, members 0..9, with $Rl2FlagSets.
$i = 0
foreach ($flags in $Rl2FlagSets) {
    foreach ($fam in 0..7) {
        foreach ($t in 0, 1) {
            $ops = if ($fam -eq 6) { 0..5 } else { 0..9 }
            foreach ($op in @($ops) + 99) {
                $jobs_ += @{ src = 'probe_e11.cpp'; obj = "probe_e11${fam}_${t}_${op}_$i.obj"; flags = $flags
                             def = "/DFAM=$fam /DTI=$t /DOP=$op"; batch = $true; std = 'c++14' }
            }
        }
    }
    $i++
}
# probe_rl3.cpp: the shader lists' sorts (lane XI), one family, even TI vertex, odd TI pixel, members 0..4.
$i = 0
foreach ($flags in $Rl2FlagSets) {
    foreach ($t in 0, 1) {
        foreach ($op in (0..4) + 99) {
            $jobs_ += @{ src = 'probe_rl3.cpp'; obj = "probe_rl30_${t}_${op}_$i.obj"; flags = $flags
                         def = "/DFAM=0 /DTI=$t /DOP=$op"; batch = $true; std = 'c++14' }
        }
    }
    $i++
}
# probe_lmm.cpp: cLightMapManager's two hash maps over the game's keys and functors (lane 261108q). Three units, each what
# library rows need ('family TI op flags', found by diffing): the floor map's _Find_last and _Forced_rehash (no /EH), the
# wall map's _Find_last and clear, the wall map's _Forced_rehash. scenegraph.md, LG6 notes (the emplaces match no unit).
foreach ($row in @('0 5 99 /O2', '1 0 99 /EHsc /O2', '1 37 99 /EHsc /O2')) {
    $fam, $t, $op, $flags = $row -split ' ', 4
    $tag = $flags -replace '[ /]', ''
    $jobs_ += @{ src = 'probe_lmm.cpp'; obj = "probe_lmm${fam}_${t}_${op}_$tag.obj"; flags = $flags
                 def = "/DFAM=$fam /DTI=$t /DOP=$op"; batch = $true; std = 'c++14' }
}
# Single units that needed their own shape (lane XI): 'probe family TI KN op flag-index', index into $RenderFlagSets for
# probe_render, else $Rl2FlagSets. KN > 0 forces probe_knobs.h in. Each row below is what a library row needs, found by
# diffing against the original; the cross product would be millions of units. rendering.md, "XI result".
$Tuned = @(
    'render 0 0 6 0 2', 'render 0 1 6 0 2', 'render 2 2 1 4 2', 'render 2 3 2 0 2', 'render 2 2 0 0 2', 'render 2 2 0 4 2',
    'render 4 56 0 1 0', 'render 5 0 1 3 2', 'render 6 0 3 0 0', 'render 6 0 1 0 0',
    'e1 0 0 7 99 8', 'e1 0 1 3 99 8', 'e1 1 0 6 99 8', 'e1 1 1 7 99 8', 'e1 2 0 1 99 8',
    'e1 6 0 16 0 8', 'e1 7 6 1 0 8', 'e1 7 7 1 0 8', 'e1 8 0 16 99 8',
    'e1 9 2 1 0 8', 'e1 9 20 1 2 8', 'e1 9 49 11 8 8', 'e1 10 1 5 0 8', 'e1 10 112 11 2 8',
    'e11 0 0 0 11 8', 'e11 0 10 0 0 8', 'e11 0 10 0 2 8', 'e11 0 10 0 3 8', 'e11 0 12 0 2 8', 'e11 0 16 0 99 8',
    'e11 1 10 0 0 8', 'e11 1 12 0 2 8', 'e11 5 10 0 0 8', 'e11 5 14 0 99 8', 'e11 2 6 0 0 8', 'e11 2 14 0 99 8',
    'e11 3 14468 0 0 8', 'e11 3 32770 0 10 8', 'e11 3 458754 0 0 8', 'e11 3 2097408 0 0 8',
    'e11 3 4718720 0 2 8', 'e11 3 6291520 0 2 8',
    'e11 6 128 0 6 8', 'e11 6 24708 0 0 8', 'e11 6 3702786 0 0 8',
    'e11 8 7296 0 99 8', 'e11 8 15488 0 5 8', 'e11 8 15488 0 99 8',
    'rl3 1 0 0 0 13')
foreach ($row in $Tuned) {
    $n, $fam, $t, $kn, $op, $i = $row -split ' '
    $set = if ($n -eq 'render') { $RenderFlagSets } else { $Rl2FlagSets }
    $knobs = if ([int]$kn) { "/FI`"$PSScriptRoot\probe_knobs.h`" /DKN=$kn " } else { '' }
    $jobs_ += @{ src = "probe_$n.cpp"; obj = "probe_${n}_f${fam}_${t}k${kn}_${op}_$i.obj"; flags = $set[[int]$i]
                 def = "$knobs/DFAM=$fam /DTI=$t /DOP=$op"; batch = $true; std = 'c++14' }
}
# The game's STL units are C++14 (lane WJ): the C++17 sources again in that mode, as probe14*.obj (lane WM). pair's
# operator< and the uninitialized-copy loops compile differently there. Not probe_one.cpp: 2,806 such objects at /O2 added no row.
$cxx14 = @($jobs_ | Where-Object { -not $_.std -and $_.src -ne 'probe_one.cpp' } | ForEach-Object {
    $j = $_.Clone(); $j.obj = $j.obj -replace '^probe', 'probe14'; $j.std = 'c++14'; $j })
$jobs_ += $cxx14
if ($Only.Count) { $jobs_ = @($jobs_ | Where-Object { $Only -contains $_.src }) }
if ($Only14) { $jobs_ = @($jobs_ | Where-Object { $_.std -eq 'c++14' -and $_.obj -like 'probe14*' }) }
if ($Missing) { $jobs_ = @($jobs_ | Where-Object { -not (Test-Path "$Out\$($_.obj)") }) }
foreach ($job in $jobs_) { Remove-Item "$Out\$($job.obj)" -ErrorAction SilentlyContinue }

# Compile $Jobs at a time, below normal priority (CLAUDE.md: long-running work). Batched jobs (the tiny probe_one
# units) go 50 to a command file, under one vcvars call.
$batches = @()
$pending = @()
foreach ($job in $jobs_) {
    if (-not $job.batch) { $batches += , @($job); continue }
    $pending += $job
    if ($pending.Count -ge 50) { $batches += , $pending; $pending = @() }
}
if ($pending.Count) { $batches += , $pending }
$running = @()
$n = 0
foreach ($batch in $batches) {
    while (@($running | Where-Object { -not $_.HasExited }).Count -ge $Jobs) { Start-Sleep -Milliseconds 500 }
    $cmd = "$Out\build$n.cmd"
    $n++
    $lines = @('@echo off', "call `"$vcvars`" x86 -vcvars_ver=14.40 >nul 2>nul")
    foreach ($job in $batch) {
        $std = if ($job.std) { $job.std } else { 'c++17' }
        $lines += "cl /nologo /c /MT /Gy /std:$std $($job.flags) $($job.def) /Fo`"$Out\$($job.obj)`" `"$PSScriptRoot\$($job.src)`" >nul 2>nul"
    }
    Set-Content $cmd ($lines -join "`r`n") -Encoding ascii
    $proc = Start-Process cmd -ArgumentList '/c', "`"$cmd`"" -NoNewWindow -PassThru
    try { $proc.PriorityClass = 'BelowNormal' } catch {}
    $running += $proc
}
$running | ForEach-Object { $_.WaitForExit() }
Remove-Item "$Out\build*.cmd" -ErrorAction SilentlyContinue
$unbuilt = @($jobs_ | Where-Object { -not (Test-Path "$Out\$($_.obj)") })
if ($unbuilt.Count) { throw "not built: $(($unbuilt | ForEach-Object { $_.obj }) -join ', ')" }
if (-not $partial) { & $python -I $libmatch --stlprobe $Out --write-stamp }
"{0} objects in {1}" -f $jobs_.Count, $Out
if ($Score) {
    foreach ($job in $jobs_) {
        $result = & $python -I $libmatch "$Out\$($job.obj)" --dry-run | Select-Object -Last 1
        "{0,-16} {1,-26} {2}" -f $job.flags, $job.obj, $result
    }
}
