"""Shared loaders for db/ and paths, for the other tools (import with sys.path tweak, see below).

Status is kept one file per class or group (db/status/<group>.jsonl) so parallel pieces of work
never edit the same file. load_status() merges them and rejects an RVA listed twice.

    sys.path.insert(0, str(Path(__file__).resolve().parent)); import tsdb
"""
import ctypes
import ctypes.wintypes
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATUSES = ("mapped", "reimplemented", "replaced", "trace-verified", "verified")
# Statuses whose code is ours (hooked replacements).
OURS = ("reimplemented", "replaced", "trace-verified", "verified")
# The bands of "no longer needs decompiling" (docs/roadmap.md, Metrics). Status data records no dead proofs yet, so that
# band is zero: nothing returns it until a recorded proof exists.
HEADLINE_BANDS = ("verified", "absorbed", "replaced", "dead", "trace-verified")
BELOW_NORMAL = 0x4000  # Windows' BELOW_NORMAL_PRIORITY_CLASS


def object_dir(build, target, config="Release"):
    """A CMake target's object folder (searched with rglob: Ninja keeps the source's folders under it). A build tree
    stays with the generator it was configured with, so both layouts exist (tools/common.ps1, Invoke-Build)."""
    build = Path(build)
    return (build / "CMakeFiles" if (build / "build.ninja").exists() else build) / f"{target}.dir" / config


def main_root():
    """The main checkout (which holds local/ and .venv/), also when running in a git worktree."""
    try:
        common = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                capture_output=True, text=True, check=True).stdout.strip()
        return Path(common).parent
    except (OSError, subprocess.CalledProcessError):
        return ROOT


def in_worktree():
    """True in a lane's git worktree (common.ps1's $isWorktree)."""
    return ROOT != main_root()


def lane_priority_flag():
    """Creation flag for a compiler or runner started here: below normal in a worktree, the parent's class on main."""
    return BELOW_NORMAL if in_worktree() else 0


def lower_lane_priority():
    """This process, and so its children, at below normal in a worktree (common.ps1's Set-LanePriority)."""
    if in_worktree():
        kernel32 = ctypes.windll.kernel32
        kernel32.SetPriorityClass.argtypes = (ctypes.wintypes.HANDLE, ctypes.wintypes.DWORD)
        kernel32.SetPriorityClass(ctypes.wintypes.HANDLE(-1), BELOW_NORMAL)  # -1: the current process


def unpacked_exe():
    return main_root() / "local/original/Sims2EP9.exe.unpacked.exe"


def jsonl(name):
    return [json.loads(l) for l in open(ROOT / "db" / name, encoding="utf-8") if l.strip()]


def functions():
    """db/functions.jsonl plus the static initialisers Ghidra didn't find (db/static-initializers.jsonl,
    tools/initializers.py) and the functions it missed or cut short (db/missed-functions.jsonl,
    tools/missedfuncs.py), in the same shape, so tools see all the code."""
    funcs = jsonl("functions.jsonl")
    known = {f["rva"] for f in funcs}
    path = ROOT / "db/static-initializers.jsonl"
    if path.exists():
        for e in jsonl("static-initializers.jsonl"):
            if e["rva"] not in known:
                funcs.append({"rva": e["rva"], "size": e["size"], "name": f"static_init_{e['rva'][2:]}",
                              "namespace": "Global", "source": "initializers", "cc": "unknown", "thunk": False,
                              "callees": e["callees"]})
    # Functions Ghidra missed and records it cut short (db/missed-functions.jsonl, tools/missedfuncs.py).
    path = ROOT / "db/missed-functions.jsonl"
    if path.exists():
        by_rva = {f["rva"]: f for f in funcs}
        for e in jsonl("missed-functions.jsonl"):
            if e["kind"] == "resized" and e["rva"] in by_rva:
                f = by_rva[e["rva"]]
                f["size"] = e["size"]
                f["callees"] = sorted(set(f.get("callees", [])) | set(e["callees"]))
            elif e["kind"] == "missed" and e["rva"] not in by_rva:
                f = {"rva": e["rva"], "size": e["size"], "name": f"FUN_a0{int(e['rva'], 16):06x}", "namespace": "Global",
                     "source": "missedfuncs", "cc": "unknown", "thunk": False, "callees": e["callees"]}
                funcs.append(f)
                by_rva[e["rva"]] = f
    return funcs


def status_entries():
    """(file, entry) for every line of db/status/*.jsonl."""
    out = []
    for p in sorted((ROOT / "db/status").glob("*.jsonl")):
        with open(p, encoding="utf-8") as lines:
            out += [(p.name, json.loads(l)) for l in lines if l.strip()]
    return out


def headline_band(entry):
    """The status entry's band of "no longer needs decompiling", None when it has none (bare reimplemented, mapped)."""
    if entry["status"] in ("verified", "replaced", "trace-verified"):
        return entry["status"]
    return "absorbed" if "absorbed_by" in entry else None


def load_status():
    """rva -> status entry, merged across db/status/*.jsonl."""
    merged = {}
    for f, e in status_entries():
        if e["rva"] in merged:
            raise SystemExit(f"db/status: {e['rva']} listed twice ({merged[e['rva']]['_file']} and {f})")
        merged[e["rva"]] = dict(e, _file=f)
    return merged


def class_outer(name):
    """A class name's own (unqualified, untemplated) part: `nGZSceneGraph::cViewer_LEGACY` -> `cViewer_LEGACY`,
    a mangled template `.?AV?$cBoundedNodeT@VcIFoo@ns@@...` -> `cBoundedNodeT`."""
    if name.startswith(".?A"):
        return name[6:].split("@")[0] if name.startswith(".?AV?$") or name.startswith(".?AU?$") else name[4:].split("@")[0]
    return name.split("<")[0].split("::")[-1]


def resolve_class(query, names):
    """(name, candidates): the class `query` names among `names` (classes.jsonl names): the exact name, else the one
    class whose qualified name ends in `::query` or whose template's outer name is `query`. With no single match, name
    is None and candidates lists the near matches (several of the above, else names containing `query`)."""
    if query in names:
        return query, []
    hits = [n for n in names if n.endswith("::" + query) or class_outer(n) == query]
    if len(hits) == 1:
        return hits[0], []
    return None, hits or [n for n in names if query.lower() in n.lower()]


def pretty_class(name):
    """A readable form of a mangled template class name: `.?AV?$cBoundedNodeT@VcIFoo@ns@@VcFooImpl@2@@ns@@` ->
    `cBoundedNodeT<cIFoo,cFooImpl>` (the outer name and its class arguments; other names unchanged)."""
    if not name.startswith(".?AV?$") and not name.startswith(".?AU?$"):
        return name
    import re
    body = name[6:]
    outer, rest = body.split("@", 1)
    args = re.findall(r"(?:^|@)[VU]([A-Za-z_]\w*)@", rest)
    return f"{outer}<{','.join(args)}>"


def lookalike(match):
    """A library-matches row that is game code of the same shape, not library code: family_likely (the relocation
    targets are game code) with every name a Concurrency Runtime member, e.g. COM director destructors matching
    ThreadInternalContext::~ThreadInternalContext. The game doesn't use ConcRT (coordinator, 2026-10-19, lane HW's
    review): these stay game functions, named and verified as such."""
    # Lane IF's review (2026-10-19): any row whose names are all ConcRT (members, ConcRT template arguments, the C-named
    # _concrt* helpers), and any family_likely row with a ConcRT name at all.
    # Only rows whose relocations reach code no library match confirms (family flags or "calls"): genuine ConcRT
    # library code (std::mutex internals) calls its own library bodies and stays library.
    concrt = ["Concurrency@" in n or n.lstrip("_").startswith("concrt") for n in match["names"]]
    foreign = bool(match.get("family") or match.get("family_likely") or match.get("calls"))
    return (all(concrt) and foreign) or (bool(match.get("family_likely")) and any(concrt))


_shared = {}  # what -> (the identity of the files it came from, the rows)


def _once(what, load, *names):
    """load() once per process while the files db/<names> stay as they are, for tools that read them several times."""
    key = []
    for name in names:
        path = ROOT / "db" / name
        s = path.stat() if path.exists() else None
        key.append((str(path), s and s.st_size, s and s.st_mtime_ns))
    if what not in _shared or _shared[what][0] != key:
        _shared[what] = (key, load())
    return _shared[what][1]


def shared_functions():
    """functions(), for readers: the rows are shared, so don't change them."""
    return _once("functions", functions, "functions.jsonl", "static-initializers.jsonl", "missed-functions.jsonl")


def library_matches():
    """The rows of db/library-matches.jsonl, shared likewise."""
    return _once("library", lambda: jsonl("library-matches.jsonl"), "library-matches.jsonl")


def library_rvas():
    """Functions provided by our toolchain, never reimplemented: byte-matched library code
    (library-matches.jsonl, names from library/FID) plus CRT functions identified by structure or
    behaviour where EA's CRT version differs from ours (crt-equivalents.jsonl)."""
    rvas = {m["rva"] for m in library_matches() if not lookalike(m)}
    rvas |= {n["rva"] for n in jsonl("names.jsonl") if n["source"] in ("library", "ghidra-fid")}
    rvas |= {e["rva"] for e in jsonl("crt-equivalents.jsonl")}
    return rvas


def third_party_rvas():
    """Library matches from third-party libraries (libjpeg, libpng, zlib, Lua, OpenSSL, D3DX...: `lib` not a toolchain
    .lib or an stlprobe object). Not our toolchain's: the standalone build links the tools/thirdparty.ps1 rebuilds of
    the same versions, so until then a replacement may call them through OriginalFunction."""
    def third(m):
        lib = (m.get("lib") or ["?"])[0]
        return not (lib.endswith(".lib") or lib.startswith("probe"))
    return {m["rva"] for m in library_matches() if third(m) and not lookalike(m)}


def thunk_targets(funcs):
    """rva -> target rva for jump stubs (`jmp rel32` at the start of a function marked thunk)."""
    import pefile
    pe = pefile.PE(str(unpacked_exe()), fast_load=True)
    out = {}
    for rva, f in funcs.items():
        if not f["thunk"]:
            continue
        code = pe.get_data(int(rva, 16), 5)
        if code[0] == 0xE9:
            out[rva] = hex((int(rva, 16) + 5 + int.from_bytes(code[1:5], "little", signed=True)) & 0xFFFFFFFF)
    return out


def find_import_thunks(funcs, read, import_slots, image_base):
    """The functions that are `jmp dword ptr [slot]` (FF 25 imm32, 6 bytes) through an import address table slot.
    read(rva, n) -> bytes; import_slots: the RVAs of the import table's slots."""
    out = set()
    for rva, f in funcs.items():
        if f.get("size") != 6:
            continue
        code = read(int(rva, 16), 6)
        if code[:2] == b"\xff\x25" and int.from_bytes(code[2:6], "little") - image_base in import_slots:
            out.add(rva)
    return out


def import_thunks(funcs):
    """Import thunks in the image (find_import_thunks over its import directory): the DLL calls our exe makes the same
    way, so the metrics count them as done by rule, like library code."""
    import pefile
    pe = pefile.PE(str(unpacked_exe()), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
    base = pe.OPTIONAL_HEADER.ImageBase
    slots = {i.address - base for d in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []) for i in d.imports}
    return find_import_thunks(funcs, pe.get_data, slots, base)


def resolve_done(done, funcs, targets=None):
    """`done` plus jump stubs whose final target is in it (calling a stub is calling its target), plus
    leaf functions under 5 bytes (`ret 4`, `xor eax, eax; ret`, field getters): they can never be
    hooked, and a replacement reproduces their effect inline instead of calling them. targets (rva -> jump
    target) defaults to the image's (thunk_targets)."""
    targets = thunk_targets(funcs) if targets is None else targets
    out = set(done) | {rva for rva, f in funcs.items() if f["size"] < 5 and not f["callees"] and not f["thunk"]}
    for rva in targets:
        seen, t = set(), rva
        while t in targets and t not in seen:
            seen.add(t)
            t = targets[t]
        if t in done:
            out.add(rva)
    return out
