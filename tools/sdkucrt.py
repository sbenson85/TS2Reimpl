"""The Windows SDK whose static UCRT (libucrt.lib, x86) EA linked into the game, and a fetcher for it.

EA's UCRT is not the 10.0.26100 one installed with our toolset; UCRT_VERSION below is the SDK whose
libucrt.lib matches the game best: 10.0.22621 (lane IA, 2026-10-19; the table is in compare()'s
docstring). The lib comes from Microsoft's NuGet package Microsoft.Windows.SDK.CPP.x86 (official
source); only libucrt.lib is extracted, into local/thirdparty/sdk-<version>/ (untracked), and the
package is deleted afterwards. Nothing is installed or run.

Usage (tools/py -I tools/sdkucrt.py ...):
  --fetch [VERSION ...]   download and extract libucrt.lib (default: UCRT_VERSION)
  --path                  print the chosen lib's path (fetch it first)
  --compare VERSION|LIB...  match each SDK's libucrt.lib alone against the game: matched, new to
                          db/library-matches.jsonl, and db/crt-equivalents.jsonl entries made exact

tools/libmatch.py takes the path as an extra input: libmatch.py --dry-run <MSVC libs> <path>.
"""
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

UCRT_VERSION = "10.0.22621.3233"  # NuGet version of Microsoft.Windows.SDK.CPP.x86 (table in compare())
PACKAGE = "microsoft.windows.sdk.cpp.x86"
FEED = "https://api.nuget.org/v3-flatcontainer"
THIRDPARTY = tsdb.main_root() / "local" / "thirdparty"


def lib_path(version=UCRT_VERSION):
    """Where the fetched libucrt.lib of an SDK lives."""
    return THIRDPARTY / f"sdk-{version}" / "libucrt.lib"


def pick_member(names):
    """The x86 libucrt.lib in a package's member list (the package also has debug and other libs)."""
    hits = [n for n in names if n.lower().replace("\\", "/").endswith("/ucrt/x86/libucrt.lib")]
    if len(hits) != 1:
        raise ValueError(f"expected one x86 libucrt.lib in the package, found {hits}")
    return hits[0]


def fetch(version):
    out = lib_path(version)
    if out.exists():
        print(f"{out} already there")
        return out
    url = f"{FEED}/{PACKAGE}/{version}/{PACKAGE}.{version}.nupkg"
    print(f"downloading {url}")
    with urllib.request.urlopen(url) as r:
        data = r.read()  # ~50 MB; kept in memory so no archive is left on disk
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        member = pick_member(z.namelist())
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(z.read(member))  # our own file name: nothing from the archive picks the path
    print(f"{member} -> {out} ({out.stat().st_size} bytes)")
    return out


def match_lib(lib, image, funcs):
    """{rva: (size, [names])} of game functions whose bytes equal a function of one .lib or directory of .obj
    files (libmatch's rules:
    relocations masked, jump-table cuts, functions under 16 bytes only when unique both ways)."""
    import libmatch
    from collections import defaultdict
    index = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    members = ([(p.name, p.read_bytes()) for p in sorted(lib.glob("*.obj"))] if lib.is_dir()
               else libmatch.archive_members(lib.read_bytes()))
    for objname, obj in members:
        for name, body, mask in libmatch.coff_functions(obj):
            if len(body) < libmatch.MIN_SIZE_UNIQUE:
                continue
            for n in [len(body)] + libmatch.jump_table_cuts(body, mask):
                m = frozenset(i for i in mask if i < n) if n != len(body) else mask
                index[n][m][bytes(0 if i in m else b for i, b in enumerate(body[:n]))].append(name)
    hits, per_key = {}, defaultdict(int)
    for f in funcs:
        rva, size = int(f["rva"], 16), f["size"]
        for mask, table in index.get(size, {}).items():
            body = image[rva:rva + size]
            key = bytes(0 if i in mask else b for i, b in enumerate(body))
            if key in table:
                hits.setdefault(rva, []).append((table[key], (size, mask, key)))
                per_key[(size, mask, key)] += 1
    out = {}
    sizes = {int(f["rva"], 16): f["size"] for f in funcs}
    for rva, found in hits.items():
        if sizes[rva] < libmatch.MIN_SIZE:
            found = [(n, k) for n, k in found if per_key[k] == 1 and len(set(n)) == 1]
        names = sorted({x for n, _ in found for x in n})
        if names:
            out[rva] = (sizes[rva], names)
    return out


def compare(versions):
    """Matches each SDK's libucrt.lib alone against the game and counts what it adds to
    db/library-matches.jsonl and how many db/crt-equivalents.jsonl entries it would make exact.

    Result (2026-10-19, lane IA; each lib alone, new = not yet in db/library-matches.jsonl):
      SDK (NuGet)          matched  bytes    new  new bytes  crt-equivalents made exact
      installed 26100.0      659     70200     5        54     0
      10.0.17763.4           369     24654    70      5320     6
      10.0.18362.5           518     48162   125     16204    12
      10.0.19041.5           537     51414   126     16685    13
      10.0.20348.19          748     84562   197     34290    52
      10.0.22000.194         767     87564   205     35885    55
      10.0.22621.1 .. .3233  976    164172   319     93947   117   (all four servicing builds identical)
      10.0.26100.1           696     74788    43      4667    12
    22621 is EA's. The installed 26100 lib still matches 3 small functions 22621 doesn't (51 bytes:
    ~__crt_scoped_stack_ptr, __get_FMA3_enable, ___acrt_getptd), so libmatch keeps both."""
    import json
    import pefile
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    root = Path(__file__).resolve().parent.parent
    image = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True).get_memory_mapped_image()
    funcs = [json.loads(l) for l in open(root / "db/functions.jsonl")]
    known = {int(json.loads(l)["rva"], 16) for l in open(root / "db/library-matches.jsonl")}
    equiv = {int(json.loads(l)["rva"], 16): json.loads(l)["name"] for l in open(root / "db/crt-equivalents.jsonl")}
    results = {}
    for v in versions:
        lib = Path(v) if v.endswith(".lib") else lib_path(v)
        m = match_lib(lib, image, funcs)
        new = {r: s for r, s in m.items() if r not in known}
        eq = {r: s for r, s in m.items() if r in equiv}
        results[v] = m
        print(f"{v}: {len(m)} matched ({sum(s for s, _ in m.values())} bytes); "
              f"new {len(new)} ({sum(s for s, _ in new.values())} bytes); crt-equivalents exact {len(eq)}", flush=True)
    return results


def main():
    args = sys.argv[1:]
    if args and args[0] == "--fetch":
        for v in args[1:] or [UCRT_VERSION]:
            fetch(v)
    elif args and args[0] == "--compare":
        compare(args[1:])
    elif args == ["--path"]:
        print(lib_path())
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
