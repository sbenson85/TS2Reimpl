# Shared paths for the tools, dot-sourced: . "$PSScriptRoot\common.ps1"
#   $root      this checkout (the main one, or a git worktree of it)
#   $mainRoot  the main checkout, which holds local/ (game copy, unpacked exe) and .venv/
#   $local     $mainRoot\local
#   $build     this checkout's build directory
$root     = (Resolve-Path "$PSScriptRoot\..").Path
$gitCommon = git -C $root rev-parse --path-format=absolute --git-common-dir 2>$null
$mainRoot = if ($gitCommon) { (Resolve-Path (Split-Path $gitCommon -Parent)).Path } else { $root }
$local    = "$mainRoot\local"
$build    = "$root\build"
$isWorktree = $root -ne $mainRoot

# A game instance's paths. Instance 0 is the game copy in local/game with the real user-data folder
# (today's setup); N > 0 is local/game-N (tools/instances.ps1) with local/userdata-N, which the host
# passes to the game as -activeUserDataDir (ts2reimpl.ini [Instance], host/instance.cpp).
function Get-Instance([int]$N = 0) {
    $root = if ($N -gt 0) { "$local\game-$N" } else { "$local\game" }
    [pscustomobject]@{
        Id       = $N
        Root     = $root
        Bin      = "$root\EP9\TSBin"
        UserData = if ($N -gt 0) { "$local\userdata-$N" } else { "$env:USERPROFILE\Documents\EA Games\The Sims 2 Legacy" }
        AllUsers = if ($N -gt 0) { "$root\allusers" } else { $null }
    }
}

# The game processes (host, original exe, crash handler) running from one instance's folder.
function Get-InstanceProcess([int]$N = 0) {
    $i = Get-Instance $N
    Get-Process Sims2EP9, ts2host, crashpad_handler -ErrorAction SilentlyContinue | Where-Object Path -like "$($i.Root)\*"
}

function Get-VisualStudio {
    & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
}

function Get-CMake {
    "$(Get-VisualStudio)\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}

# The Ninja that ships beside Visual Studio's CMake (same component, no download).
function Get-Ninja {
    $ninja = "$(Get-VisualStudio)\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if (-not (Test-Path -LiteralPath $ninja)) { throw "No ninja.exe at $ninja (Visual Studio's C++ CMake tools component)" }
    $ninja
}

# Ninja takes the compiler from the environment, where the Visual Studio generator took it from -T v143,version=14.40:
# this is that toolset, on the same 64-bit host tools for x86. Kept in the build tree as what vcvarsall changes, not a
# whole environment, so later runs keep their own variables (TS2_CHECKOUT_LOCK) and PATH.
function Use-MsvcEnvironment([string]$BuildDir) {
    if ($env:TS2_MSVC_ENV -eq $BuildDir) { return }
    $saved = "$BuildDir\msvc-env.txt"
    $added = @{}
    if (Test-Path -LiteralPath $saved) {
        foreach ($line in Get-Content -LiteralPath $saved -Encoding utf8) {
            $i = $line.IndexOf('=')
            if ($i -gt 0) { $added[$line.Substring(0, $i)] = $line.Substring($i + 1) }
        }
    }
    if (-not $added.VCToolsInstallDir -or -not (Test-Path -LiteralPath "$($added.VCToolsInstallDir)bin\Hostx64\x86\cl.exe")) {
        $vcvars = "$(Get-VisualStudio)\VC\Auxiliary\Build\vcvarsall.bat"
        $before = @{}
        foreach ($line in (cmd /c set)) {
            $i = $line.IndexOf('=')
            if ($i -gt 0) { $before[$line.Substring(0, $i)] = $line.Substring($i + 1) }
        }
        $added = @{}
        foreach ($line in (cmd /c "`"$vcvars`" x64_x86 -vcvars_ver=14.40 >nul 2>&1 && set")) {
            $i = $line.IndexOf('=')
            if ($i -le 0) { continue }
            $name = $line.Substring(0, $i)
            $value = $line.Substring($i + 1)
            if ($name -eq 'Path') {
                $known = $before.Path -split ';'
                $value = ($value -split ';' | Where-Object { $_ -and $known -notcontains $_ }) -join ';'
            }
            # The toolset's own variables always: a process that already has them (a developer prompt) changes nothing.
            if ($value -cne $before[$name] -or $name -match '^(INCLUDE|LIB|LIBPATH|VCTools\w+)$') { $added[$name] = $value }
        }
        if ($added.VCToolsVersion -notlike '14.40.*') { throw "vcvarsall gave toolset '$($added.VCToolsVersion)', not 14.40" }
        New-Item -ItemType Directory -Force $BuildDir | Out-Null
        $added.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" } | Set-Content -LiteralPath $saved -Encoding utf8
    }
    foreach ($name in $added.Keys) {
        if ($name -eq 'Path') { $env:PATH = "$($added.Path);$env:PATH" }
        else { [Environment]::SetEnvironmentVariable($name, $added[$name], 'Process') }
    }
    $env:TS2_MSVC_ENV = $BuildDir
}

# A lane's (a worktree's) builds and test runs yield to the main checkout's, whose validations and deploys must not
# starve. Children inherit a below-normal class; the main checkout keeps the class it was started with.
function Set-LanePriority {
    if ($isWorktree) { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' }
}

# A new Ninja tree's shared compile cache (the end of CMakeLists.txt, docs/lane/brief.md): nothing without ccache.exe.
function Get-CompileCacheArguments {
    $ccache = "$local\tools\ccache-4.14.2\ccache-4.14.2-windows-x86_64\ccache.exe"
    if (Test-Path -LiteralPath $ccache) { @("-DTS2_COMPILE_CACHE=$ccache", "-DTS2_COMPILE_CACHE_DIR=$local\compile-cache") }
}

# Configures (when needed) and builds a target, at below-normal priority in a worktree (Set-LanePriority).
# A new tree uses Ninja. A tree keeps the generator in its CMakeCache: delete build/ to move one from Visual Studio.
function Invoke-Build([string]$Target, [string]$Config = 'Release') {
    # A mutation run or validation holding this checkout's lock (tools/checkout_lock.py) owns build/: refuse unless
    # we are its child (TS2_CHECKOUT_LOCK inherited).
    $busy = & "$mainRoot\.venv\Scripts\python.exe" -I "$root\tools\checkout_lock.py" check 2>&1
    if ($LASTEXITCODE -eq 1) { throw "$busy" }
    $cmake = Get-CMake
    Set-LanePriority
    # Configure unless tools/configure_state.py can show it would change nothing: a new or deleted file in
    # src/ or tests/ must be in the very next build, and CONFIGURE_DEPENDS alone only catches it on the build after.
    $state = @('--build', $build, '--main', $mainRoot, '--cmake', $cmake)
    $python = "$mainRoot\.venv\Scripts\python.exe"
    $generator = ''
    if (Test-Path "$build\CMakeCache.txt") {
        $line = Select-String -LiteralPath "$build\CMakeCache.txt" -Pattern '^CMAKE_GENERATOR:INTERNAL=(.*)$' | Select-Object -First 1
        if ($line) { $generator = $line.Matches[0].Groups[1].Value }
    }
    $fresh = -not $generator
    $useNinja = $fresh -or $generator -like 'Ninja*'
    if ($useNinja) { Use-MsvcEnvironment $build }
    $tool = "$root\tools\configure_state.py"
    if (-not $fresh) { & $python -I $tool check @state }
    if ($fresh -or $LASTEXITCODE) {
        & $python -I $tool begin @state
        if ($fresh) {
            $tools = "$($env:VCToolsInstallDir)bin\Hostx64\x86"
            & $cmake -S $root -B $build -G 'Ninja Multi-Config' "-DCMAKE_MAKE_PROGRAM=$(Get-Ninja)" "-DCMAKE_C_COMPILER=$tools\cl.exe" `
                "-DCMAKE_CXX_COMPILER=$tools\cl.exe" "-DCMAKE_ASM_MASM_COMPILER=$tools\ml.exe" @(Get-CompileCacheArguments) | Out-Null
        } else {
            & $cmake -S $root -B $build | Out-Null
        }
        if ($LASTEXITCODE) { throw 'CMake configure failed' }
        & $python -I $tool commit @state
    }
    if ($useNinja) {
        # --quiet: errors and warnings without a status line per file. -k 0: every error, not the first (as MSBuild).
        & $cmake --build $build --config $Config --target $Target -- --quiet -k 0
    } else {
        & $cmake --build $build --config $Config --target $Target -- /nologo /verbosity:minimal
    }
    if ($LASTEXITCODE) { throw 'Build failed' }
}
