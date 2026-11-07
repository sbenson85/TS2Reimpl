# Builds the open-source libraries embedded in the game from their official source releases, so
# tools/libmatch.py can identify their functions. Sources are downloaded into local/thirdparty/dl-*/
# (see db/README.md for URLs); objects go to local/thirdparty/obj/<lib>-<flagset>/.
#   thirdparty.ps1 [-Flags '/O1'] [-Only lua] [-Compiler 14.40|ddk] [-Tag name]   (ddk: cl 13.10, see tools/thirdparty_build.py;
#   the winning builds are recipes there, which libmatch takes by default)
param([string]$Flags = '/O1', [string]$Only = '', [string]$Compiler = '14.40', [string]$Tag = '')  # -Compiler ddk: cl 13.10 from tools/thirdparty_build.py --fetch-ddk
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
$tp   = "$local\thirdparty"
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'

function Extract($archive, $dir) {
    if (-not (Test-Path "$tp\src\$dir")) {
        # A .zip has no versioned top directory (expat1_2.zip holds expat\): it goes into the first part of $dir.
        $into = if ($archive -like '*.zip') { "$tp\src\$(($dir -split '\\')[0])" } else { "$tp\src" }
        New-Item -ItemType Directory -Force $into | Out-Null
        tar -xf "$tp\$archive" -C $into
    }
    "$tp\src\$dir"
}

# jconfig.vc minus its "typedef unsigned char boolean": the game's libjpeg uses the default 4-byte int boolean.
function Set-JConfig($d, $vc) {
    $txt = (Get-Content $vc -Raw) -replace '(?s)/\* Define "boolean".*?#define HAVE_BOOLEAN[^\r\n]*', ''
    Set-Content "$d\jconfig.h" $txt -Encoding ascii
}

# Expat has no Windows config.h in the tarball (lib\xmlparse.c includes <config.h>).
$expatSetup = { param($d) Set-Content "$d\lib\config.h" "#include <string.h>`r`n#define XML_BYTE_ORDER 12`r`n#define XML_CONTEXT_BYTES 1024`r`n#define HAVE_MEMMOVE 1`r`n#define VERSION `"1.95`"" -Encoding ascii }

# name -> archive, extracted dir, source globs (relative), exclusions, include dirs, defines
$libs = @(
    @{ Name = 'lua50'; Archive = 'dl-lua-5.0\lua-5.0.tar.gz'; Dir = 'lua-5.0'; Src = @('src\*.c', 'src\lib\*.c'); Exclude = @(); Inc = @('include', 'src'); Defs = '' },
    @{ Name = 'lua501'; Archive = 'dl-lua-5.0.x\lua-5.0.1.tar.gz'; Dir = 'lua-5.0.1'; Src = @('src\*.c', 'src\lib\*.c'); Exclude = @(); Inc = @('include', 'src'); Defs = '' },
    @{ Name = 'lua502'; Archive = 'dl-lua-5.0.x\lua-5.0.2.tar.gz'; Dir = 'lua-5.0.2'; Src = @('src\*.c', 'src\lib\*.c'); Exclude = @(); Inc = @('include', 'src'); Defs = '' },
    @{ Name = 'lua503'; Archive = 'dl-lua-5.0.x\lua-5.0.3.tar.gz'; Dir = 'lua-5.0.3'; Src = @('src\*.c', 'src\lib\*.c'); Exclude = @(); Inc = @('include', 'src'); Defs = '' },
    @{ Name = 'zlib114'; Archive = 'dl-zlib-1.1.4\zlib-1.1.4.tar.gz'; Dir = 'zlib-1.1.4'; Src = @('*.c'); Exclude = @('example.c', 'minigzip.c'); Inc = @('.'); Defs = '' },
    @{ Name = 'zlib113'; Archive = 'dl-zlib-1.1.3\zlib-1.1.3.tar.gz'; Dir = 'zlib-1.1.3'; Src = @('*.c'); Exclude = @('example.c', 'minigzip.c'); Inc = @('.'); Defs = '' },
    @{ Name = 'jpeg6b'; Archive = 'dl-jpegsrc.v6b\jpegsrc.v6b.tar.gz'; Dir = 'jpeg-6b'; Src = @('j*.c');
       Exclude = @('jmemansi.c', 'jmemname.c', 'jmemdos.c', 'jmemmac.c', 'jpegtran.c'); Inc = @('.'); Defs = ''; Setup = { param($d) Set-JConfig $d "$d\jconfig.vc" } },
    # 6a has no jconfig.vc; 6b's is the same Win32 configuration.
    @{ Name = 'jpeg6a'; Archive = 'dl-jpegsrc.v6a\jpegsrc.v6a.tar.gz'; Dir = 'jpeg-6a'; Src = @('j*.c');
       Exclude = @('jmemansi.c', 'jmemname.c', 'jmemdos.c', 'jmemmac.c', 'jpegtran.c'); Inc = @('.'); Defs = ''; Setup = { param($d) Set-JConfig $d "$d\..\jpeg-6b\jconfig.vc" } },
    # Expat 1.95.x (the game's error strings are the pre-1.95.2 set: "not well-formed", no "unexpected parser state").
    # Needs a config.h; define sets vary in what the game was built with (XML_NS / XML_DTD).
    # James Clark's expat 1.x (ftp.jclark.com/pub/xml/expat1_2.zip): the game's parser struct has no memory-suite
    # fields (added in 1.95.0) and the DTD scanner state (XML_DTD). Static build of its xmlparse/xmltok projects.
    @{ Name = 'expat12'; Archive = 'dl-expat-1.x\expat1_2.zip'; Dir = 'expat-1.2\expat'; Src = @('xmlparse\xmlparse.c', 'xmltok\xmlrole.c', 'xmltok\xmltok.c');
       Exclude = @(); Inc = @('xmltok', 'xmlparse'); Defs = '/DNDEBUG /DXML_NS /DXML_DTD /DWIN32 /D_WINDOWS /DXMLTOKAPI= /DXMLPARSEAPI=' },
    @{ Name = 'expat12dtd'; Archive = 'dl-expat-1.x\expat1_2.zip'; Dir = 'expat-1.2\expat'; Src = @('xmlparse\xmlparse.c', 'xmltok\xmlrole.c', 'xmltok\xmltok.c');
       Exclude = @(); Inc = @('xmltok', 'xmlparse'); Defs = '/DNDEBUG /DXML_DTD /DWIN32 /D_WINDOWS /DXMLTOKAPI= /DXMLPARSEAPI=' },
    @{ Name = 'expat11';Archive = 'dl-expat-1.x\expat1_1.zip'; Dir = 'expat-1.1\expat'; Src = @('xmlparse\xmlparse.c', 'xmltok\xmlrole.c', 'xmltok\xmltok.c');
       Exclude = @(); Inc = @('xmltok', 'xmlparse'); Defs = '/DNDEBUG /DXML_NS /DXML_DTD /DWIN32 /D_WINDOWS /DXMLTOKAPI= /DXMLPARSEAPI=' },
    @{ Name = 'expat1950ns';Archive = 'dl-expat\expat-1.95.0.tar.gz'; Dir = 'expat-1.95.0'; Src = @('lib\xmlparse.c', 'lib\xmlrole.c', 'lib\xmltok.c'); Exclude = @(); Inc = @('lib'); Defs = '/DXML_NS'; Setup = $expatSetup },
    @{ Name = 'expat1950plain'; Archive = 'dl-expat\expat-1.95.0.tar.gz'; Dir = 'expat-1.95.0'; Src = @('lib\xmlparse.c', 'lib\xmlrole.c', 'lib\xmltok.c'); Exclude = @(); Inc = @('lib'); Defs = ''; Setup = $expatSetup },
    @{ Name = 'expat1950nsdtd'; Archive = 'dl-expat\expat-1.95.0.tar.gz'; Dir = 'expat-1.95.0'; Src = @('lib\xmlparse.c', 'lib\xmlrole.c', 'lib\xmltok.c'); Exclude = @(); Inc = @('lib'); Defs = '/DXML_NS /DXML_DTD'; Setup = $expatSetup },
    @{ Name = 'expat1951ns'; Archive = 'dl-expat\expat-1.95.1.tar.gz'; Dir = 'expat-1.95.1'; Src = @('lib\xmlparse.c', 'lib\xmlrole.c', 'lib\xmltok.c'); Exclude = @(); Inc = @('lib'); Defs = '/DXML_NS'; Setup = $expatSetup },
    # Speex (floating-point build; the game's Speex is the libspeex narrowband+wideband codec, wrapped by cGZSpeexCodec).
    @{ Name = 'speex105'; Archive = 'dl-speex\speex-1.0.5.tar.gz'; Dir = 'speex-1.0.5'; Src = @('libspeex\*.c'); Exclude = @(); Inc = @('include', 'libspeex'); Defs = '/DWIN32 /D_WINDOWS' },
    @{ Name = 'speex104'; Archive = 'dl-speex\speex-1.0.4.tar.gz'; Dir = 'speex-1.0.4'; Src = @('libspeex\*.c'); Exclude = @(); Inc = @('include', 'libspeex'); Defs = '/DWIN32 /D_WINDOWS' },
    @{ Name = 'speex103'; Archive = 'dl-speex\speex-1.0.3.tar.gz'; Dir = 'speex-1.0.3'; Src = @('libspeex\*.c'); Exclude = @(); Inc = @('include', 'libspeex'); Defs = '/DWIN32 /D_WINDOWS' },
    @{ Name = 'speex102'; Archive = 'dl-speex\speex-1.0.2.tar.gz'; Dir = 'speex-1.0.2'; Src = @('libspeex\*.c'); Exclude = @(); Inc = @('include', 'libspeex'); Defs = '/DWIN32 /D_WINDOWS' },
    @{ Name = 'speex101'; Archive = 'dl-speex\speex-1.0.1.tar.gz'; Dir = 'speex-1.0.1'; Src = @('libspeex\*.c'); Exclude = @(); Inc = @('include', 'libspeex'); Defs = '/DWIN32 /D_WINDOWS' },
    @{ Name = 'speex100'; Archive = 'dl-speex\speex-1.0.tar.gz'; Dir = 'speex-1.0'; Src = @('libspeex\*.c'); Exclude = @(); Inc = @('include', 'libspeex'); Defs = '/DWIN32 /D_WINDOWS' },
    @{ Name = 'speex110'; Archive = 'dl-speex\speex-1.1.tar.gz'; Dir = 'speex-1.1'; Src = @('libspeex\*.c'); Exclude = @(); Inc = @('include', 'libspeex'); Defs = '/DWIN32 /D_WINDOWS' },
    @{ Name = 'speex111'; Archive = 'dl-speex\speex-1.1.1.tar.gz'; Dir = 'speex-1.1.1'; Src = @('libspeex\*.c'); Exclude = @(); Inc = @('include', 'libspeex'); Defs = '/DWIN32 /D_WINDOWS' },
    @{ Name = 'speex1112'; Archive = 'dl-speex\speex-1.1.12.tar.gz'; Dir = 'speex-1.1.12'; Src = @('libspeex\*.c'); Exclude = @(); Inc = @('include', 'libspeex'); Defs = '/DWIN32 /D_WINDOWS' },
    # libpng: compiled against zlib 1.1.4 headers (the game's png code sits next to zlib 1.1.4, deflate/inflate 1.1.4).
    @{ Name = 'libpng105'; Archive = 'dl-libpng\libpng-1.0.5.tar.gz'; Dir = 'libpng-1.0.5'; Src = @('png*.c'); Exclude = @('pngtest.c'); Inc = @('.', '..\zlib-1.1.4'); Defs = '' },
    @{ Name = 'libpng103'; Archive = 'dl-libpng\libpng-1.0.3.tar.gz'; Dir = 'libpng-1.0.3'; Src = @('png*.c'); Exclude = @('pngtest.c'); Inc = @('.', '..\zlib-1.1.3'); Defs = '' },
    @{ Name = 'libpng096'; Archive = 'dl-libpng\libpng-0.96.tgz'; Dir = 'libpng-0.96'; Src = @('png*.c'); Exclude = @('pngtest.c'); Inc = @('.', '..\zlib-1.1.3'); Defs = '' },
    # OpenSSL: run `perl Configure VC-WIN32 no-asm` in the source dir first (Git for Windows' perl).
    # Test programs and other platforms' files fail to compile; that's fine, they aren't in the game.
    @{ Name = 'openssl097d'; Archive = 'dl-openssl-0.9.7d\openssl-0.9.7d.tar.gz'; Dir = 'openssl-0.9.7d'; Src = @('crypto\*.c', 'crypto\**\*.c', 'ssl\*.c');
       Exclude = @(); Inc = @('include', 'crypto', '.');
       Defs = '/DOPENSSL_SYSNAME_WIN32 /DWIN32_LEAN_AND_MEAN /DL_ENDIAN /DDSO_WIN32 /DOPENSSL_NO_ASM';
       Setup = { param($d)
           Copy-Item "$d\crypto\opensslconf.h" "$d\include\openssl\opensslconf.h" -Force
           Set-Content "$d\crypto\buildinf.h" "#define CFLAGS `"`"`r`n#define PLATFORM `"VC-WIN32`"`r`n#define DATE `"`"" -Encoding ascii } }
)

foreach ($lib in $libs) {
    if ($Only -and $lib.Name -ne $Only) { continue }
    $src = Extract $lib.Archive $lib.Dir
    if ($lib.Setup) { & $lib.Setup $src }
    $tag = if ($Tag) { $Tag } else { ($Flags -replace '[^A-Za-z0-9]', '') }
    $out = "$tp\obj\$($lib.Name)-$tag" + $(if ($Compiler -eq 'ddk') { '-ddk' } else { '' })
    New-Item -ItemType Directory -Force $out | Out-Null
    $files = foreach ($g in $lib.Src) {
        if ($g -like '*\`*`*\*') {
            # 'dir\**\*.c': every subdirectory of dir (not dir itself)
            $dir, $filter = $g -split '\\\*\*\\'
            Get-ChildItem "$src\$dir" -Recurse -File -Filter $filter | Where-Object { $_.DirectoryName -ne "$src\$dir" }
        }
        else { Get-ChildItem "$src\$g" -File }
    }
    $files = $files | Where-Object { $lib.Exclude -notcontains $_.Name -and $_.Name -notmatch '(test|speed)\.c$' }
    $inc = ($lib.Inc | ForEach-Object { "/I`"$src\$_`"" }) -join ' '
    $list = "$out\files.txt"
    Set-Content $list (($files | ForEach-Object { "`"$($_.FullName)`"" }) -join "`r`n") -Encoding ascii
    $cmd = "$out\build.cmd"
    $env = if ($Compiler -eq 'ddk') {
        $ddk = "$tp\ddk3790sp1"
        if (-not (Test-Path "$ddk\bin\x86\cl.exe")) { throw "no DDK compiler: tools\py -I tools\thirdparty_build.py --fetch-ddk" }
        "set PATH=$ddk\bin\x86;%PATH%`r`nset INCLUDE=$ddk\inc\crt;$ddk\inc\wnet`r`nset LIB="
    } else { "call `"$vcvars`" x86 -vcvars_ver=$Compiler >nul 2>nul" }
    Set-Content $cmd "@echo off`r`n$env`r`ncd /d `"$out`"`r`ncl /nologo /c /TC /MT /Gy /W0 $Flags $inc $($lib.Defs) @files.txt > build.log 2>&1" -Encoding ascii
    cmd /c $cmd
    $objs = (Get-ChildItem "$out\*.obj").Count
    "{0,-8} {1,-6} {2} of {3} files compiled" -f $lib.Name, $Flags, $objs, $files.Count
}



