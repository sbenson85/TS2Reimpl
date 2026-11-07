# Writes local/pdbfacts.jsonl: what our compiler knows about our code (class layouts, function signatures),
# read from a debug-info build of ts2host (RelWithDebInfo, beside the Release build) by tools/pdbfacts/pdbfacts.cpp
# (DIA SDK). tools/annotate.py reads it for the annotated Ghidra copy. Facts about our own code, kept in local/.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
Invoke-Build 'ts2host' 'RelWithDebInfo'
$dia = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\DIA SDK"
$out = "$local\pdbfacts"
New-Item -ItemType Directory -Force $out | Out-Null
$exe = "$out\pdbfacts.exe"
if (-not (Test-Path $exe) -or (Get-Item "$PSScriptRoot\pdbfacts\pdbfacts.cpp").LastWriteTime -gt (Get-Item $exe).LastWriteTime) {
    $vcvars = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    $cmd = "$out\build.cmd"
    Set-Content $cmd ("@echo off`r`ncall `"$vcvars`" x86 -vcvars_ver=14.40 >nul 2>nul`r`ncd /d `"$out`"`r`n" +
        "cl /nologo /EHsc /O1 /std:c++17 /I`"$dia\include`" `"$PSScriptRoot\pdbfacts\pdbfacts.cpp`" `"$dia\lib\diaguids.lib`" ole32.lib oleaut32.lib advapi32.lib > build.log 2>&1") -Encoding ascii
    cmd /c $cmd
    if ($LASTEXITCODE) { Get-Content "$out\build.log"; throw 'pdbfacts build failed' }
    Copy-Item "$dia\bin\msdia140.dll" $out
}
& $exe "$build\RelWithDebInfo\ts2host.pdb" | Set-Content "$local\pdbfacts.jsonl" -Encoding utf8
if ($LASTEXITCODE) { throw 'pdbfacts failed' }
"$((Get-Content "$local\pdbfacts.jsonl").Count) facts -> local\pdbfacts.jsonl"
