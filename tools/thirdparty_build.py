"""Builds the embedded open-source libraries the way EA built them (lane IE), so tools/libmatch.py identifies their code.

tools/thirdparty.ps1 explores flag sets with MSVC 14.40; this tool holds the recipes that won, including the ones that
need patched sources or an older compiler. Each recipe writes its objects to local/thirdparty/obj/<recipe>-<tag>/
(untracked); libmatch takes RECIPES' output directories as default inputs when they exist (DEFAULT_OBJS below).

Sources and toolchains come from their official sites into local/thirdparty/ (never committed):
  lua.org          lua-5.0.tar.gz (dl-lua-5.0/), bugs.html (dl-lua-5.0.x/, fetched here): the official 5.0 bug patches
  openssl.org      openssl-0.9.7d.tar.gz (dl-openssl-0.9.7d/)
  zlib.net, libpng zlib-1.1.4.tar.gz (dl-zlib-1.1.4/), libpng-1.0.5.tar.gz (dl-libpng/)
  jclark.com       expat1_2.zip (dl-expat-1.x/; ftp://ftp.jclark.com/pub/xml/expat1_2.zip)
  archive.org      dxsdk_sum2004.exe (dl-dxsdk-sum2004/): Microsoft's DirectX 9.0 SDK Update (Summer 2004) installer,
                   signature- and hash-checked, unpacked by 7-Zip (dxsdk-sum2004/: d3dx9.lib, DxErr9.lib)
  microsoft.com    the Windows Server 2003 SP1 DDK ISO (dl-ddk3790sp1/): its x86 cl.exe 13.10.4035 (VC 7.1 era), CRT
                   and Windows headers; only the needed cabs are extracted (ddk3790sp1/). Nothing is installed.
Needs perl (Git for Windows') for OpenSSL's Configure and 7-Zip for the ISO.

Which compiler: the game's OpenSSL, expat and "set A" zlib/libpng were prebuilt by a VC 7.1-era compiler (no /GS
cookies, `mov reg,[mem]; push reg` sequences, 16-byte aligned int3 padding); MSVC 14.40 with any flags matches 36
OpenSSL functions, cl 13.10 with OpenSSL's own flags 808. Lua and the "set B" libraries are EA's 14.40 builds.
"Set A" turned out to be D3DX9's private copies (lane IN): the DirectX SDK's d3dx9.lib (--fetch-dxsdk) is the real
input; the jpeg6ba/libpng105a/zlib114a recipes stay as the record of how they were reconstructed.

Usage (tools/py -I tools/thirdparty_build.py ...):
  --list                 the recipes and whether their objects exist
  --fetch-ddk            download the DDK ISO (242 MB) and extract the compiler and headers
  --fetch-dxsdk          download the DirectX 9.0 SDK (Summer 2004, 239 MB) and extract d3dx9.lib and DxErr9.lib, which
                         libmatch then takes as inputs (they supersede the jpeg6ba, libpng105a and zlib114a recipes)
  RECIPE ...             build these recipes (all with --all)
"""
import os
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

TP = tsdb.main_root() / "local" / "thirdparty"
OBJ = TP / "obj"
SRC = TP / "src"
VCVARS = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
SEVENZIP = r"C:\Program Files\7-Zip\7z.exe"

DDK_URL = "https://download.microsoft.com/download/9/0/f/90f019ac-8243-48d3-91cf-81fc4093ecfd/1830_usa_ddk.iso"
DDK_ISO = TP / "dl-ddk3790sp1" / "1830_usa_ddk.iso"
DDK = TP / "ddk3790sp1"
DDK_CABS = ["X86dBINS", "CMNINCS", "wnet_SDKINCS1", "wnet_SDKINCS2", "wnet_SDKINCS3"]
LUA_BUGS_URL = "https://www.lua.org/bugs.html"
LUA_BUGS = TP / "dl-lua-5.0.x" / "bugs.html"


# ---------------------------------------------------------------------------------------------------------------- DDK

def fetch_ddk():
    """Downloads the DDK ISO and extracts the x86 compiler and the headers into local/thirdparty/ddk3790sp1/."""
    if not DDK_ISO.exists():
        DDK_ISO.parent.mkdir(parents=True, exist_ok=True)
        print(f"downloading {DDK_URL}")
        urllib.request.urlretrieve(DDK_URL, DDK_ISO)
    cabs = DDK_ISO.parent / "cabs"
    members = [f"common\\{c}.{e}" for c in DDK_CABS for e in ("cab", "inf")]
    subprocess.run([SEVENZIP, "e", str(DDK_ISO), f"-o{cabs}", "-y"] + members, check=True, stdout=subprocess.DEVNULL)
    for cab in DDK_CABS:
        extract_cab(cabs / f"{cab}.cab", cabs / f"{cab}.inf", DDK)
    print(f"cl: {ddk_cl()}")


def extract_cab(cab, inf, dest):
    """A DDK cab holds files named <CAB>_FILE_<n>; its .inf maps each to a name in a [Files_k] section, and
    [DestinationDirs] maps the section to a directory. Only sections under bin\\x86 (the compiler, not prefast or
    drvfast) and inc\\ are kept."""
    text = inf.read_text(errors="replace")
    dirs = dict(re.findall(r'^(Files_\d+)=49000,"\\?([^"]*)"', text, re.M))
    tmp = cab.parent / (cab.stem + "-x")
    subprocess.run([SEVENZIP, "e", str(cab), f"-o{tmp}", "-y"], check=True, stdout=subprocess.DEVNULL)
    n = 0
    for sec, body in re.findall(r"^\[(Files_\d+)\]\s*\n(.*?)(?=^\[|\Z)", text, re.M | re.S):
        d = dirs.get(sec, "").strip("\\")
        if not (d.lower() == "bin\\x86" or d.lower().startswith("inc\\")):
            continue
        for name, member in re.findall(r"^([^,\s]+),([^,\s]+),", body, re.M):
            src = tmp / member
            if src.exists():
                out = dest / d / name
                out.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(src, out)
                n += 1
    shutil.rmtree(tmp)
    print(f"{cab.name}: {n} files")


# ---------------------------------------------------------------------------------------------------------------- DirectX SDK

# The game links D3DX9 statically (lane IN): "Microsoft (R) D3DX9 Shader Compiler 5.04.00.2904" at rdata 0xe28c74, and
# d3dx9.lib + DxErr9.lib of the DirectX 9.0 SDK Update (Summer 2004) match 2,157 functions (844 KB) at 0x12310-0xe71af
# byte for byte, including D3DX's own copies of libjpeg 6b, libpng 1.0.5 and zlib 1.1.4 (namespace D3DX: the "set A"
# copies lane IE rebuilt) and DXGetErrorString9 (0xd5663). Microsoft no longer hosts the SDK; archive.org's copy of the
# original installer is checked against its Authenticode signature (Microsoft Corporation) and SHA-1, never run: 7-Zip
# unpacks the cabinet in its resources and then the two libraries.
DXSDK_URL = "https://archive.org/download/dxsdk_sum2004/dxsdk_sum2004.exe"
DXSDK_SHA1 = "73d875b97591f48707c38ec0dbc63982ff45c661"
DXSDK_EXE = TP / "dl-dxsdk-sum2004" / "dxsdk_sum2004.exe"
DXSDK = TP / "dxsdk-sum2004"
DXSDK_LIBS = ["d3dx9.lib", "DxErr9.lib"]


def dxsdk_libs():
    return [DXSDK / n for n in DXSDK_LIBS if (DXSDK / n).exists()]


def fetch_dxsdk():
    import hashlib
    if not DXSDK_EXE.exists():
        DXSDK_EXE.parent.mkdir(parents=True, exist_ok=True)
        print(f"downloading {DXSDK_URL} (239 MB)")
        urllib.request.urlretrieve(DXSDK_URL, DXSDK_EXE)
    h = hashlib.sha1(DXSDK_EXE.read_bytes()).hexdigest()
    if h != DXSDK_SHA1:
        sys.exit(f"{DXSDK_EXE}: SHA-1 {h}, expected {DXSDK_SHA1}")
    sig = subprocess.run(["powershell", "-NoProfile", "-Command",
                          f"$s = Get-AuthenticodeSignature '{DXSDK_EXE}'; \"$($s.Status)|$($s.SignerCertificate.Subject)\""],
                         capture_output=True, text=True).stdout.strip()
    if not sig.startswith("Valid|CN=Microsoft Corporation"):
        sys.exit(f"{DXSDK_EXE}: signature {sig}")
    inner = DXSDK_EXE.parent / "x1"
    subprocess.run([SEVENZIP, "e", str(DXSDK_EXE), f"-o{inner}", "-y"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([SEVENZIP, "e", str(inner / "dxsdk.exe"), f"-o{DXSDK}", "-y"] + [f"Lib\\{n}" for n in DXSDK_LIBS],
                   check=True, stdout=subprocess.DEVNULL)
    shutil.rmtree(inner)
    print("\n".join(str(p) for p in dxsdk_libs()))


def ddk_cl():
    return DDK / "bin" / "x86" / "cl.exe"


WINSDK_UM = Path(r"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um")


def ddk_overlay():
    """Headers the DDK lacks, for OpenSSL's rand_win.c: lmstats.h (Platform SDK only). Ours wraps the installed
    Windows SDK's copy, defining the two SAL annotations and the API-family test it uses (cl 13.10 predates them);
    only its declarations and struct layouts are used."""
    d = DDK / "inc" / "overlay"
    d.mkdir(parents=True, exist_ok=True)
    (d / "winapifamily.h").write_text("#pragma once\n#define WINAPI_FAMILY_PARTITION(x) 1\n")
    (d / "lmstats.h").write_text("#pragma once\n#ifndef _In_\n#define _In_\n#endif\n#ifndef _Outptr_\n#define _Outptr_\n"
                                 f'#endif\n#include "{WINSDK_UM / "lmstats.h"}"\n')
    return d


def ddk_env():
    """cmd lines that put the DDK's compiler first and its CRT and Windows headers (plus the overlay) on INCLUDE."""
    if not ddk_cl().exists():
        sys.exit("no DDK compiler: run tools/py -I tools/thirdparty_build.py --fetch-ddk first")
    inc = ";".join(str(p) for p in (DDK / "inc" / "crt", DDK / "inc" / "wnet", ddk_overlay()))
    return f"set PATH={ddk_cl().parent};%PATH%\r\nset INCLUDE={inc}\r\nset LIB=\r\n"


def perl():
    p = shutil.which("perl") or r"C:\Program Files\Git\usr\bin\perl.exe"
    if not Path(p).exists():
        sys.exit("needs perl (Git for Windows ships one)")
    return p


# ---------------------------------------------------------------------------------------------------------------- Lua

def lua_bug_patches(bugs, text=None):
    """The official patches for the given Lua 5.0 bugs (numbers n of 5.0-n on lua.org/bugs.html), as
    [(bug, file, [[op, (a1, a2), new_lines, old_lines]])]. Normal-diff hunks; a patch's line numbers are not always
    against pristine 5.0 (5.0-10's are off by 37), so apply_hunks places them by content."""
    import html
    if text is None:
        if not LUA_BUGS.exists():
            LUA_BUGS.parent.mkdir(parents=True, exist_ok=True)
            urllib.request.urlretrieve(LUA_BUGS_URL, LUA_BUGS)
        text = LUA_BUGS.read_text(encoding="utf-8", errors="replace")
    sec = text[text.index('NAME="5.0"'):]
    sec = sec[:sec.find("<H2>", 10)]
    groups = []
    for m in re.finditer(r'<A NAME="5\.0-(\d+)"></A>(.*?)(?=<A NAME="5\.0-\d+"></A>|\Z)', sec, re.S):
        if int(m.group(1)) not in bugs:
            continue
        p = re.search(r'<PRE CLASS="patch">(.*?)</PRE>', m.group(2), re.S)
        if not p:
            continue
        # The patch text keeps literal '<' (diff's old-line marker); only real tags are stripped.
        patch = html.unescape(re.sub(r"</?[A-Za-z][A-Za-z0-9]*(\s[^>]*)?>", "", p.group(1)))
        cur = None
        for line in patch.split("\n"):
            if re.fullmatch(r"\S+\.[ch]:\s*", line):
                groups.append((f"5.0-{m.group(1)}", line.strip()[:-1], []))
                continue
            h = re.fullmatch(r"(\d+)(?:,(\d+))?([acd])(\d+)(?:,(\d+))?", line.strip())
            if h:
                a1 = int(h.group(1))
                cur = [h.group(3), (a1, int(h.group(2) or a1)), [], []]
                groups[-1][2].append(cur)
            elif line.startswith("> ") or line == ">":
                cur[2].append(line[2:])
            elif line.startswith("< ") or line == "<":
                cur[3].append(line[2:])
    return groups


def apply_hunks(path, hunks, bug):
    """Applies one patch's hunks for one file. The patch's own numbering is consistent, so one offset (found from the
    first hunk that removes lines: the nearest place its old lines occur) places every hunk; each removal is checked."""
    lines = path.read_text().split("\n")
    norm = [s.rstrip() for s in lines]
    offset = None
    for op, (a1, a2), new, old in hunks:
        if op == "a":
            continue
        want = [s.rstrip() for s in old]
        at = [i for i in range(len(norm) - len(want) + 1) if norm[i:i + len(want)] == want]
        if not at:
            sys.exit(f"{bug} {path.name}: lines of hunk {a1}{op} not found")
        offset = min(at, key=lambda i: abs(i - (a1 - 1))) - (a1 - 1)
        break
    offset = offset or 0
    for op, (a1, a2), new, old in sorted(hunks, key=lambda h: -h[1][0]):
        a1, a2 = a1 + offset, a2 + offset
        if op == "a":
            lines[a1:a1] = new
            continue
        if [s.rstrip() for s in lines[a1 - 1:a2]] != [s.rstrip() for s in old]:
            sys.exit(f"{bug} {path.name}: hunk at {a1} does not apply")
        lines[a1 - 1:a2] = new
    path.write_text("\n".join(lines))


LUA_GAME_BUGS = [int(b) for b in os.environ.get("LUA_BUGS", "3,4,5,10").split(",")]


def prep_lua50p(src):
    """Lua 5.0 with the official bug patches the game has: 5.0-3 (file:close without a file), 5.0-4 (C function
    stacks in the GC), 5.0-5 ('pc' invalidated by a yield: the bugs page's form, which 5.0.2 does differently, so 5.0.2's
    lvm.c does not match) and 5.0-10 (dofile's return values). Not 5.0-2, 6, 7, 8, 9: with any of them some function
    stops matching (lua_resume, luaC_collectgarbage/mark/separateudata, luaB_tostring, statement, luaV_execute)."""
    for bug, name, hunks in lua_bug_patches(LUA_GAME_BUGS):
        path = src / "src" / name  # the core (lvm.c, ldo.c, ...) or the standard libraries (lbaselib.c, ...)
        apply_hunks(path if path.exists() else src / "src" / "lib" / name, hunks, bug)
        print(f"applied {bug} to {name}")


# ---------------------------------------------------------------------------------------------------------------- OpenSSL

OPENSSL_OPTIONS = ["no-idea", "no-mdc2", "no-rc5"]  # the patent-free configuration; ssl3's cipher table has no IDEA


def prep_openssl(src):
    """OpenSSL's own Windows build (INSTALL.W32): Configure VC-WIN32 with the options, then mk1mf's nmake file for
    the C-only build (the game has no x86 asm objects: no bn/des/sha asm body occurs in the image)."""
    subprocess.run([perl(), "Configure", "VC-WIN32"] + OPENSSL_OPTIONS, cwd=src, check=True, stdout=subprocess.DEVNULL)
    with open(src / "MINFO", "w") as f:
        subprocess.run([perl(), "util/mkfiles.pl"], cwd=src, check=True, stdout=f)
    with open(src / "ms" / "nt.mak", "w") as f:
        subprocess.run([perl(), "util/mk1mf.pl", "no-asm"] + OPENSSL_OPTIONS + ["VC-WIN32"], cwd=src, check=True,
                       stdout=f)


def build_openssl(name, src, out):
    """nmake -f ms\\nt.mak with the makefile's CFLAG minus /Gs0 (it probes the stack in every function: the game's
    code has none), /WX and /Fd, and /MT for /MD (the game links the static CRT). Objects go straight to `out`; the
    lib step fails for want of advapi32.lib, which does not matter."""
    mak = (src / "ms" / "nt.mak").read_text()
    cflag = re.search(r"^CFLAG=(.*)$", mak, re.M).group(1)
    cflag = re.sub(r"\s/(Gs0|WX|Fd\S*)", "", cflag).replace("/MD", "/MT").replace("/nologo", "/nologo /Ims")
    r = RECIPES[name]
    env = ddk_env() if r["compiler"] == "ddk" else f'call "{VCVARS}" x86 -vcvars_ver={r["compiler"]} >nul 2>nul\r\n'
    cmd = out / "build.cmd"
    cmd.write_text(f"@echo off\r\n{env}cd /d \"{src}\"\r\n"
                   f'nmake /nologo /k -f ms\\nt.mak "CFLAG={cflag}" "TMP_D={out}" OUT_D=out32 "{out}" out32 inc32 '
                   f'inc32\\openssl headers lib > "{out / "build.log"}" 2>&1\r\n')
    subprocess.run(["cmd", "/c", str(cmd)])
    (out / "cflags.txt").write_text(cflag + "\n")


# ---------------------------------------------------------------------------------------------------------------- libjpeg

# The set-A libjpeg 6b (D3DX9's private copy, found later: see DXSDK above) has three ints more in its public structs
# than IJG's (lane IN, from the game's field offsets: jinit_c_coef_controller stores coef at +0x160, not +0x158;
# reset_marker_reader clears input_scan_number at +0x94, not +0x90). Their names are ours. Compress: two booleans after
# write_Adobe_marker, both set TRUE by its jpeg_set_colorspace (0x7fe80). Decompress: one int after output_scanline.
JPEG_SET_A_FIELDS = [
    ("  boolean write_Adobe_marker;", ["  boolean extra_e0;\t/* D3DX: set TRUE by jpeg_set_colorspace */",
                                      "  boolean extra_e4;\t/* D3DX: set TRUE by jpeg_set_colorspace */"]),
    ("  JDIMENSION output_scanline;", ["  int extra_90;\t/* D3DX */"]),
]
# And its configuration: no 2-pass quantiser (default_decompress_parms sets two_pass_quantize FALSE), and backing-store
# file names of 520 bytes (request_virt_sarray allocates a 0x248-byte jvirt_sarray_control, IJG's is 0x80).
JPEG_SET_A_CONFIG = [
    ("jmorecfg.h", "#define QUANT_2PASS_SUPPORTED", "#undef  QUANT_2PASS_SUPPORTED"),
    ("jmemsys.h", "#define TEMP_NAME_LENGTH   64", "#define TEMP_NAME_LENGTH   520"),
]


def prep_jpeg_set_a(src):
    """jconfig.h from jconfig.vc without its `typedef unsigned char boolean` (the game's boolean is jmorecfg's int, as in
    set B: tools/thirdparty.ps1 Set-JConfig), and D3DX's extra struct fields in jpeglib.h."""
    vc = (src / "jconfig.vc").read_text()
    vc = re.sub(r'/\* Define "boolean".*?#define HAVE_BOOLEAN[^\n]*', "", vc, flags=re.S)
    (src / "jconfig.h").write_text(vc)
    path = src / "jpeglib.h"
    text = path.read_text()
    for anchor, extra in JPEG_SET_A_FIELDS:
        i = text.index(anchor)
        j = text.index("\n", i) + 1
        text = text[:j] + "\n".join(extra) + "\n" + text[j:]
    path.write_text(text)
    for name, old, new in JPEG_SET_A_CONFIG:
        p = src / name
        t = p.read_text()
        if old not in t:
            sys.exit(f"{name}: '{old}' not found")
        p.write_text(t.replace(old, new, 1))


# ---------------------------------------------------------------------------------------------------------------- recipes

# name -> archive (under local/thirdparty), top directory in it, source globs, include dirs ("@recipe/dir": another
# recipe's sources), flags (they name the output directory) and defines, compiler, prep. Every compile also gets
# /nologo /c /TC /MT /Gy /W0. "compiler": "14.40" (vcvars) or "ddk" (cl 13.10.4035 with the DDK's headers).
# The numbers are lane IE's (2026-10-19): rows libmatch finds with the recipe's objects alone.
PNG_SET_A_DEFS = " ".join(f"/DPNG_NO_{c}" for c in (
    "READ_tEXt", "READ_zTXt", "WRITE_tEXt", "WRITE_zTXt", "READ_tIME", "WRITE_tIME", "READ_sBIT", "WRITE_sBIT",
    "READ_USER_TRANSFORM", "WRITE_USER_TRANSFORM"))
RECIPES = {
    # Lua 5.0 (+ 4 official bug patches), EA's build: 387 exact + 26 prefix (lua50-O2 had 342 + 16). /GS-: no cookie
    # in any game Lua function; LUA_USERSTATE: lua_State has 8 bytes in front (close_state frees L - 8, 0x68 bytes).
    "lua50p": dict(archive="dl-lua-5.0/lua-5.0.tar.gz", top="lua-5.0", src=["src/*.c", "src/lib/*.c"],
                   inc=["include", "src"], flags="/O2 /GS- /DLUA_USERSTATE=int", compiler="14.40", prep=prep_lua50p),
    # OpenSSL 0.9.7d, prebuilt by an older compiler: 808 exact + 62 prefix, 97% of the 223 KB OpenSSL block.
    "openssl097d": dict(archive="dl-openssl-0.9.7d/openssl-0.9.7d.tar.gz", top="openssl-0.9.7d",
                        flags="ms nt.mak " + " ".join(OPENSSL_OPTIONS), tag="msnoidea", compiler="ddk",
                        prep=prep_openssl, builder=build_openssl),
    # "Set A" (the image block next to the libpng/libjpeg copy that has hotpatch prologues): zlib 1.1.4 built the
    # Windows way (stdcall default, /Oxs, string pooling): 46 exact (inflate/deflate/trees). deflate_fast and
    # deflate_slow (0xb4261, 0xb4507) match too but are no db function starts.
    "zlib114a": dict(archive="dl-zlib-1.1.4/zlib-1.1.4.tar.gz", top="zlib-1.1.4", src=["*.c"],
                     exclude=("example.c", "minigzip.c"), inc=["."], flags="/Oxs /Gz /GF", compiler="ddk"),
    # Set A libpng 1.0.5: hotpatch prologue and frame pointers, without text/time/sBIT chunks or user transforms
    # (png_info and png_struct offsets): 65 exact. A second copy with another png_struct layout remains.
    "libpng105a": dict(archive="dl-libpng/libpng-1.0.5.tar.gz", top="libpng-1.0.5", src=["png*.c"],
                       exclude=("pngtest.c",), inc=[".", "@zlib114a/zlib-1.1.4"], flags="/Oxs /Oy- /Gz /GF /hotpatch",
                       defs=PNG_SET_A_DEFS, compiler="ddk"),
    # Set B libpng 1.0.5 (the Gimex block, EA's 14.40 build): PNG_USER_MEM_SUPPORTED adds the 12 bytes of allocator
    # fields to png_struct: 146 exact (libpng105-O1OiGS had 132, all of them kept).
    "libpng105b": dict(archive="dl-libpng/libpng-1.0.5.tar.gz", top="libpng-1.0.5", src=["png*.c"],
                       exclude=("pngtest.c",), inc=[".", "@zlib114a/zlib-1.1.4"], flags="/O1 /Oi /GS-",
                       defs="/DPNG_USER_MEM_SUPPORTED", compiler="14.40"),
    # Set A libjpeg 6b (lane IN): cl 13.10 /O2 with /Ow (no aliasing inside a function: cinfo->err is loaded once in
    # ERREXIT), frame pointers and hotpatch prologues like set-A libpng, D3DX's extra struct fields
    # (JPEG_SET_A_FIELDS): 196 exact + 10 prefix. Superseded by d3dx9.lib itself (DXSDK_SUPERSEDES).
    "jpeg6ba": dict(archive="dl-jpegsrc.v6b/jpegsrc.v6b.tar.gz", top="jpeg-6b", src=["j*.c"],
                    exclude=("jmemansi.c", "jmemname.c", "jmemdos.c", "jmemmac.c", "jpegtran.c"), inc=["."],
                    flags="/O2 /Ow /Oy- /Gz /GF /hotpatch", compiler="ddk", prep=prep_jpeg_set_a),
    # James Clark's expat 1.2 (ftp.jclark.com/pub/xml/expat1_2.zip), its xmlparse/xmltok projects' defines, static.
    # /Ob1 (lane IN): only `inline`-marked functions inlined (cdataSectionProcessor calls contentProcessor, /O2's /Ob2
    # inlines it): 109 exact + 20 prefix of the block's 161 db functions, and 82 bodies Ghidra never made (xmlrole's
    # state handlers, reached only through PROLOG_STATE.handler: libmatch's "gap" rows); /O2 alone 92 + 10.
    # The other 32 differ by register choice, push order or tail duplication with the same source (dtdDestroy,
    # XmlInitUnknownEncoding, normal_scanPi), as cl 13.10's X509_NAME_oneline does between two builds (lane IE).
    "expat12": dict(archive="dl-expat-1.x/expat1_2.zip", top="expat",
                    src=["xmlparse/xmlparse.c", "xmltok/xmlrole.c", "xmltok/xmltok.c"], inc=["xmltok", "xmlparse"],
                    flags="/O2 /Ob1", defs="/DNDEBUG /DXML_NS /DXML_DTD /DWIN32 /D_WINDOWS /DXMLTOKAPI= /DXMLPARSEAPI=",
                    compiler="ddk"),
}


def tag(flags):
    return re.sub(r"[^A-Za-z0-9]", "", flags)


def out_dir(name):
    r = RECIPES[name]
    return OBJ / f"{name}-{r.get('tag') or tag(r['flags'])}{'-ddk' if r['compiler'] == 'ddk' else ''}"


# The "set A" recipes rebuild D3DX's private copies (lane IN): with the DirectX SDK's d3dx9.lib present they add nothing
# (every row they find, d3dx9.lib finds too) and would only label D3DX code as plain libjpeg/libpng/zlib.
DXSDK_SUPERSEDES = ("jpeg6ba", "libpng105a", "zlib114a")


# Every recipe's object directory; libmatch adds the ones that exist to its defaults.
def default_objs():
    skip = DXSDK_SUPERSEDES if dxsdk_libs() else ()
    return [p for name in RECIPES if name not in skip for p in sorted(out_dir(name).glob("*.obj"))]


def extract(name):
    """A private copy of the recipe's sources (local/thirdparty/src/<recipe>/), prepared (patched) once."""
    r = RECIPES[name]
    dest = SRC / name
    if not dest.exists():
        tmp = SRC / (name + ".tmp")  # renamed when prepared, so a failed prep is redone
        if tmp.exists():
            shutil.rmtree(tmp)
        tmp.mkdir(parents=True)
        archive = TP / r["archive"]
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as z:
                for n in z.namelist():
                    if os.path.isabs(n) or ".." in Path(n).parts:
                        sys.exit(f"unsafe path in {r['archive']}: {n}")
                z.extractall(tmp)
        else:
            with tarfile.open(archive) as t:
                members = [m for m in t.getmembers() if m.isfile() or m.isdir()]  # no links, no devices
                for m in members:
                    if os.path.isabs(m.name) or ".." in Path(m.name).parts:
                        sys.exit(f"unsafe path in {r['archive']}: {m.name}")
                t.extractall(tmp, members=members)
        if r.get("prep"):
            r["prep"](tmp / r["top"])
        tmp.rename(dest)
    return dest / r["top"]


def build(name):
    r = RECIPES[name]
    src = extract(name)
    out = out_dir(name)
    out.mkdir(parents=True, exist_ok=True)
    if r.get("builder"):
        r["builder"](name, src, out)
        print(f"{name:10s} {len(list(out.glob('*.obj')))} objects -> {out}")
        return
    files =[f for g in r["src"] for f in sorted(src.glob(g)) if not re.search(r"(test|speed)\.c$", f.name)]
    files = [f for f in files if f.name not in r.get("exclude", ())]
    (out / "files.txt").write_text("\n".join(f'"{f}"' for f in files))
    def inc_dir(i):
        if i.startswith("@"):  # another recipe's sources, e.g. zlib's headers for libpng
            other, rest = i[1:].split("/", 1)
            return extract(other).parent / rest
        return src / i
    inc = " ".join(f'/I"{inc_dir(i)}"' for i in r["inc"])
    if r["compiler"] == "ddk":
        env, cc = ddk_env(), f'"{ddk_cl()}"'
    else:
        env = f'call "{VCVARS}" x86 -vcvars_ver={r["compiler"]} >nul 2>nul\r\n'
        cc = "cl"
    cmd = out / "build.cmd"
    cmd.write_text(f"@echo off\r\n{env}cd /d \"{out}\"\r\n"
                   f"{cc} /nologo /c /TC /MT /Gy /W0 {r['flags']} {inc} {r.get('defs', '')} @files.txt > build.log 2>&1\r\n")
    subprocess.run(["cmd", "/c", str(cmd)])
    objs = len(list(out.glob("*.obj")))
    print(f"{name:10s} {objs} of {len(files)} files compiled -> {out}")


def main():
    args = sys.argv[1:]
    if not args or "-h" in args or "--help" in args:
        print(__doc__)
        return
    if "--fetch-ddk" in args:
        return fetch_ddk()
    if "--fetch-dxsdk" in args:
        return fetch_dxsdk()
    if "--list" in args:
        for name in RECIPES:
            n = len(list(out_dir(name).glob("*.obj")))
            print(f"{name:10s} {RECIPES[name]['compiler']:6s} {RECIPES[name]['flags']:40s} {n} objects")
        return
    for name in (RECIPES if "--all" in args else args):
        if name not in RECIPES:
            sys.exit(f"unknown recipe {name}; --list")
        build(name)


if __name__ == "__main__":
    main()
