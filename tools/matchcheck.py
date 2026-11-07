"""Byte-match checker: does our hooked replacement compile to the original's bytes?

    .venv/Scripts/python -I tools/matchcheck.py --class cTSNeighbor [--show]
    .venv/Scripts/python -I tools/matchcheck.py --rva 0x1466e0 0x1cbd45 --show
    .venv/Scripts/python -I tools/matchcheck.py --all-verified [--summary]

Takes each translation unit holding a hooked replacement (the status entries' `src` files plus every hook table)
from the build's own object when it is current, else compiles it with the build's command line for that file (v6
provenance: BuildLog; only current build objects verify, never `--extra`), resolves each hook's exact replacement
from the compiled kHooks relocation, and compares it with the original over the function's size
(db/functions.jsonl, the unpacked exe). Vtables, vtable ties and adapters are read from every src/ unit of the build
whatever is selected, so --rva, --class and --drift give one verdict per function (lane HR: 0x49e110's tie was seen by
--drift only, its vtable being in a unit --rva didn't read).

A function MATCHes (docs/archive/proposals/byte-match-verification.md, rule) when:
 1. the bytes are identical except relocation fields (ours from the object, the original's from .reloc);
 2. every relocation resolves to the same thing as the original at that position: a call/jump to the
    same original RVA (a hooked replacement of it, a library function matched to it, or an original
    referenced by RVA through a link-time symbol: `ts2_rva_<hex>`, or the `OriginalEntry<RVA>` symbol that
    OriginalFunction<Fn, 0xRVA>() calls directly, tools/origsyms.py), constant data (floats, strings) with
    the same contents, jump-table entries at the same offsets inside the function;
 3. its source has no inline asm, __emit or naked functions (checked on the lines the listing attributes
    to it).
Otherwise it tries the equivalences below; failing those it prints a similarity score (instruction streams,
operands with addresses masked) and the first differing instructions; --show prints the differing region side
by side with our source lines.

Equivalences (v2; --no-equiv turns them off; the proposal's "Equivalences" section has the argument). The
function is compared again instruction by instruction. Both sides are decoded from the entry following every
branch and jump table, and every byte of the original's extent must be reached code, a table or int3/nop
padding. Instructions compare by their decoded form (mnemonic, registers, operand sizes, immediates), so an
encoding difference (jcc rel8 vs rel32 once sizes shift) doesn't count; relocated operands compare by rule 2,
branch targets by instruction index, tables by contents. Two patterns may differ beyond that:
 - gameaddress: our `call GameBase` is dropped, and image base + K is tracked through `add`/`sub reg, imm`,
   `lea reg, [reg + d]` and `mov reg, reg` (dropped too). A register holding base + K, read as a whole
   register operand or as a memory base, stands for the original's absolute reference to exactly RVA K
   (`push <K>`, `mov [x], <K>`, `mov r, [<K + d>]`; `call reg` for its rel32 call to K, thunks as in rule 2).
   A value used any other way (returned, compared, written to) makes its definition stand for
   `mov reg, <K>`, which the original must then have in that place. Assumed: GameBase() returns the base the
   original image was relocated to (host and harness pass it to SetGameBase) and does nothing else.
 - vtable-call: our `mov R, dword ptr [M]` ... `call R` / `jmp R` stands for the original's
   `call dword ptr [M]` when the code between is straight-line (no branch target, branch, call or string
   instruction), M isn't esp-relative, and nothing between reads or writes R, writes a register M uses, or
   writes memory except by push (below esp, where nothing live is): both load the same pointer.
Register safety, for both: the dropped call clobbers eax ecx edx and flags (cdecl), the add/sub flags, the
moved load R. From there each such register differs from the original's until written, and no path may read
it first, counting implicit reads; ecx/edx at a call or tail jump as register arguments, unless the callee is
known not to take them (a direct original callee whose code reads neither before writing it, or our callee's
declared convention) or the register is undefined in the original too (not written since our entry and not
our own argument: no callee can depend on it); at ret, the return register for the declared return type (al
only for 8-bit, ax for 16-bit, edx:eax for 64-bit and aggregates) and the callee-saved ebx esi edi ebp. A
narrower write (al) makes the low bits equal again. MATCH lines name the patterns used; --summary counts them.

Callee proof (v3; --no-callee-proof turns it off): our call to F counts as the original's call to RVA X (rule 2)
when F is a function defined in the same object (an inline, template or library COMDAT: std::set/map/list/vector
members, allocator helpers) and this checker proves F equal to the function at X (db/functions.jsonl extent) by
the same rules: bytes and relocations, else the equivalences, with F's own calls resolved the same way,
recursively (depth limit CALLEE_DEPTH, memoised per object/symbol/RVA). Argument: two functions whose code is
equal, operation by operation, with every call going to functions proven equal, behave the same, so calling
either is the same call. A pair already being proven further up is assumed equal while proving its callees
(recursive _Erase, mutual recursion); that is the coinductive form of the same argument (the pair is accepted
only if every step matches under the assumption), and a failed proof discards every pair proven since it began.
Names don't enter into it, so set<int> vs set<unsigned int> or insert vs erase can only match if their code
does. Library entries (db/library-matches.jsonl, db/crt-equivalents.jsonl) are still tied by name first, by
full signature only: the decorated name, or the undecorated name with its parameter types; a plain C name
(`free`) only to one of our C symbols. Proven pairs are listed (names and RVAs) in
build/matchcheck/callee-proofs.jsonl, a record re-proven on every run. External functions (CRT from the
static libraries, not in our object) can't be proven this way and still need a library entry.

EH frames (v5): a function with a C++ EH frame pushes its handler stub's address (`push __ehhandler$f`). Once the
function's code matches (bytes, or the equivalences), the frame is compared too, and the MATCH names "eh-frame":
 - the stub (ours __ehhandler$f in .text$x, the original's in its .text$x region, often shared by identical folding):
   its code like a funclet (below), so the same cookie check, if any, and the jmp to the same __CxxFrameHandler3;
 - the FuncInfo the stub loads (x86, magic 0x19930520..22), field by field: magic, maxState, nTryBlocks,
   nIPMapEntries (must be 0 with no map, as on x86), EHFlags (noexcept / /EHs bits); the unwind map, per state its
   toState and action (both null, the same funclet, or an external function by rule 2, e.g. __std_terminate); the
   try-block map, per block tryLow, tryHigh, catchHigh, nCatches and per catch its adjectives (const/volatile/
   reference flags), the caught type (both catch (...), or TypeDescriptors with the same decorated name, which is
   what the runtime's type match compares), dispCatchObj and the handler funclet; the ESTypeList the same way;
 - funclets (unwind actions, catch blocks; x86 catch blocks follow the function's code in its section and are cut
   off the function's body): equal bytes from the symbol to the next one (padding dropped), relocated fields at the
   same places, calls/jumps and data by rule 2, and every reference into the function (a catch block's continuation
   address, an unrelocated branch leaving the funclet) to the original's instruction at the same instruction index
   (the same offset when the bytes match). The equivalences don't apply inside funclets.
The tables' own layout and addresses don't matter (only what they contain), which is all __CxxFrameHandler3 reads.

Vtables (v7, classes in the standalone shape: docs/standalone-shape.md; the proposal's "Vtables in the standalone
shape" has the argument):
 - a reference to our vtable `??_7X@@6B...` is rule 2's same target as the original's reference to the vftable of the
   same RTTI class (our locator's type descriptor) and subobject offset (our locator's), at the same slot offset, when
   both have the same slot count and no slot is known to be another function (vtable_ref, slot_conflict);
 - vtable tie: a hook whose adapter only forwards (`[[msvc::forceinline_calls]] return self->X::f(args);`, read from
   our sources with their `using` aliases, adapter_target) ties its RVA to the function our vtable holds in that
   slot (same class, member and arity; receiver = that face). When the adapter doesn't match, every tied function is
   compared instead ("vtable-tie"); tied functions count as the RVA's replacement in rule 2.
 - the same RTTI class (v8, game_class): our class in a namespace (`.?AVcGZFramework@gz@@`) is the original's class of
   the same unqualified name (`.?AVcGZFramework@@`) when exactly one original class has that name and no other class
   our build defines does; otherwise refused and listed at the end. Template names (`?$`) still need the exact name.
   --rules7 applies v7's lookup (exact names only) for comparison.

The game's bytes and disassembly are read at run time only and never written to disk (--json writes
names, scores and reasons only).

Known limits: the shipped flags include /Oy- (frame pointers), which the original doesn't use: try
`--extra /Oy` to see what /Oy would give. GameAddress(opaque)/GameBase match through the gameaddress
pattern only where the GameBase call doesn't change register allocation (it clobbers ecx/edx, so the
compiler often keeps `this` in esi where the original used ecx); link-time `ts2_rva_<hex>` and
`OriginalEntry<RVA>` symbols, which this checker resolves directly, avoid that. The shipped /GS puts a security cookie into every EH
frame, which the original's EH functions lack (11 of ~8,000 have one): try `--extra /GS-`. Unhooked verified
entries are skipped. Objects and listings go to build/matchcheck/obj.
"""
import argparse
import bisect
import copy
import ctypes
import difflib
import json
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import capstone
from capstone import x86_const as X86

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402
import stackframes  # noqa: E402
import flagprofile  # noqa: E402
import origsyms  # noqa: E402

VERSION = 8  # 8: classes in namespaces: our `.?AVcGZFramework@gz@@` is the original's `.?AVcGZFramework@@` when that is
#   the one original class of that unqualified name and no other class of ours has it (otherwise refused, and listed);
#   faces named with qualifiers; for vtable references and ties alike
# 7: vtables in the standalone shape: our vtable reference stands for the original vftable of the same
#   class and face (same slot count, no conflicting slot); vtable ties (the slot function behind a forwarding adapter
#   hook is compared, and counts as the RVA's replacement in rule 2)
# 6: exact hook targets (kHooks relocations), whole-object constants (literals only), build
#   provenance (per-file build command lines, current build objects; --extra never verifies)
# 5: EH frames (handler stub, FuncInfo, unwind and catch funclets), "eh-frame"
# 4: per-file build profile (`.size.cpp` files at /O1 /Oi, tools/flagprofile.py)
# 3: callee-proof (our inline/library callee proven equal to the original's); library names
#   tied by full signature only
# 2: equivalences (gameaddress, vtable-call, encoding); crt-equivalents resolved by signature
ROOT = tsdb.ROOT
OUT = ROOT / "build/matchcheck"
REL32, DIR32, DIR32NB = 0x14, 0x06, 0x07
IMAGE_SCN_CNT_CODE = 0x20
IMAGE_SCN_LNK_COMDAT = 0x1000
FORBIDDEN = re.compile(r"\b(__asm|_asm|__emit|_emit)\b|naked")
CALLEE_DEPTH = 6  # nesting limit for callee proofs
LITERALS = ("__real@", "__xmm@", "??_C@")  # compiler literals: no address identity, pooled
RVA_SYMBOL = re.compile(r"ts2_rva_([0-9a-fA-F]+)")


def rva_symbol(name):
    """The original RVA a link-time symbol stands for: ts2_rva_<hex> (plus the addend at the use), or the
    OriginalEntry<RVA> symbol of an OriginalFunction<Fn, 0xRVA>() call (tools/origsyms.py), else None. Both are
    absolute symbols equal to the game base + RVA (tools/checkgamebase.py checks that on the linked map)."""
    m = RVA_SYMBOL.search(name)
    return int(m.group(1), 16) if m else origsyms.decode(name)
EH_MAGIC = (0x19930520, 0x19930521, 0x19930522)  # x86 FuncInfo versions (__CxxFrameHandler3 accepts all three)


# ---------------------------------------------------------------- toolchain

def msvc_env():
    """Environment from vcvarsall x86 with the 14.40 toolset, cached in build/matchcheck/env.json."""
    cache = OUT / "env.json"
    if cache.exists():
        return json.loads(cache.read_text())
    vs = subprocess.run([os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"),
                         "-products", "*", "-latest", "-property", "installationPath"],
                        capture_output=True, text=True, check=True).stdout.strip()
    bat = Path(vs) / "VC/Auxiliary/Build/vcvarsall.bat"
    out = subprocess.run(f'"{bat}" x86 -vcvars_ver=14.40 >nul && set', shell=True, capture_output=True,
                         text=True, check=True).stdout
    env = dict(l.split("=", 1) for l in out.splitlines() if "=" in l)
    OUT.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps(env))
    return env


def _tlog(path):
    """An MSBuild tracker log: {"^" line (upper-case sources, `|`-joined when batched): [the lines after it]}."""
    out, cur = {}, None
    if not path.exists():
        return out
    for line in path.read_bytes().decode("utf-16", errors="replace").splitlines():
        if line.startswith("^"):
            cur = out.setdefault(line[1:].upper(), [])
        elif cur is not None and line.strip():
            cur.append(line)
    return out


def _tlogs(folder, kind):
    """_tlog over every tracker log of one kind: MSBuild with /MP writes CL.<pid>.<kind>.1.tlog beside (or instead of)
    CL.<kind>.1.tlog; the newest file's entry wins for a source in several."""
    out = {}
    for f in sorted(folder.glob(f"CL*.{kind}.*.tlog"), key=lambda f: f.stat().st_mtime):
        out.update(_tlog(f))
    return out


def command_flags(line):
    """The code-relevant flags of a cl command line from a tracker log: without include paths, output names, the
    source and the CMAKE_INTDIR define (its quoting doesn't survive; it is unused)."""
    line = re.sub(r'/D\s+"CMAKE_INTDIR=.*?"\s', " ", line)
    flags = []
    for tok in re.findall(r'"[^"]*"|\S+', line):
        t = tok.strip('"')
        up = t.upper()
        if up.startswith(("/I", "/FO", "/FD", "/FA")) or up.endswith((".CPP", ".C")) or up == "/C":
            continue
        flags.append(tok)
    return flags


def ninja_flags(defines, flags):
    """command_flags for a Ninja compile statement: its DEFINES and FLAGS as cl's own arguments (quotes gone, cl removes
    them), with the /nologo /TP of the rule, without CMAKE_INTDIR (as above) and /MP (only ever a driver option)."""
    out = ["/nologo", "/TP"]
    for tok in f"{defines} {flags}".split():
        t = tok.replace('"', "")
        if t.upper() != "/MP" and not t.startswith(("-DCMAKE_INTDIR=", "/DCMAKE_INTDIR=")):
            out.append(t)
    return out


def ninja_deps(build, program):
    """{upper-case object path relative to the build tree, with forward slashes: its headers as absolute paths}, from
    the deps log of the Release build (`ninja -t deps`; ninja leaves out the toolset's and the SDK's headers, which the
    toolset check covers)."""
    r = subprocess.run([program, "-C", str(build), "-f", "build-Release.ninja", "-t", "deps"], capture_output=True,
                       text=True, errors="replace")
    out, cur = {}, None
    for line in r.stdout.splitlines():
        if line.startswith(" "):
            if cur is not None and line.strip():
                cur.append(os.path.normpath(os.path.join(build, line.strip())))
        elif ": #deps" in line:
            cur = out.setdefault(line.split(": #deps")[0].upper(), [])
    return out


def shipped_flags():
    """The latest speed-profile ts2core compile flags (a fallback, and the summary line's label only: each file is
    compiled with its own command line from BuildLog)."""
    if BUILD.root and BUILD.ninja:
        flags = next((u["flags"] for s, u in sorted(BUILD.units.items()) if not s.endswith(".SIZE.CPP")), None)
        if flags:
            return flags, "build/CMakeFiles/impl-Release.ninja"
    log = BUILD.root / BuildLog.TARGETS["ts2core"] / "ts2core.tlog/CL.command.1.tlog" if BUILD.root else None
    if log and log.exists():
        lines = log.read_bytes().decode("utf-16", errors="replace").splitlines()
        line = next((l for prev, l in reversed(list(zip(lines, lines[1:])))
                     if l.startswith("/c ") and prev.startswith("^") and not prev.upper().endswith(".SIZE.CPP")), None)
        if line:
            return command_flags(line), str(log)
    return ["/nologo", "/O2", "/Ob2", "/Oy", "/D", "_MBCS", "/D", "WIN32", "/D", "_WINDOWS", "/D", "NDEBUG",
            "/EHsc", "/MT", "/GS-", "/fp:precise", "/Zc:wchar_t", "/Zc:forScope", "/Zc:inline", "/std:c++20",
            "/permissive-", "/Gd", "/TP", "/utf-8"], "built-in default (no CL command log found)"


def profile_flags(source):
    """Flags a source's build profile adds to the shipped ones: `<name>.size.cpp` files are built like the original's
    size-optimised modules (tools/flagprofile.py; CMakeLists.txt applies the same flags). Used only for a file the
    build has no command line for (never verification)."""
    return flagprofile.PROFILE_FLAGS["size"] if source.endswith(".size.cpp") else []


_sha = {}


def file_sha(path):
    """Content hash of a file (memoised by path, size and time), None when it doesn't exist."""
    try:
        st = os.stat(path)
    except OSError:
        return None
    key = (str(path).upper(), st.st_size, st.st_mtime_ns)
    if key not in _sha:
        _sha[key] = hashlib.sha1(Path(path).read_bytes()).hexdigest()
    return _sha[key]


class BuildLog:
    """Build provenance (v6): what the CMake build compiled each source with, read from MSBuild's tracker logs (per
    target: CL.command.1.tlog the command line, CL.read.1.tlog every file the compile read, including headers under
    build/generated and the toolset's, CL.write.1.tlog the objects) or, in a Ninja tree, its build file and deps log,
    and whether its object is current."""
    TARGETS = {"ts2core": "build/ts2core.dir/Release", "ts2hooktables": "build/ts2hooktables.dir/Release"}
    NINJA_RULE = re.compile(r"^build (.+?\.obj): CXX_COMPILER__(ts2core|ts2hooktables)_unscanned_Release (.+?)(?: \|\||$)")

    def __init__(self, roots=None, here=None):
        roots = roots or [ROOT, tsdb.main_root()]
        self.checkout = here or ROOT  # the checkout whose sources are compared (a worktree has its own src/)
        self.root = next((r for r in roots if (r / self.TARGETS["ts2core"]).exists()
                          or (r / "build/build.ninja").exists()), None)
        self.units = {}  # upper-case absolute source path (in the build's checkout) -> record
        self.ninja = self.root is not None and (self.root / "build/build.ninja").exists()
        if self.root is None:
            return
        if self.ninja:
            self.read_ninja()
            return
        for target, rel in self.TARGETS.items():
            d = self.root / rel
            tl = d / f"{target}.tlog"
            reads, writes = _tlogs(tl, "read"), _tlogs(tl, "write")
            written = {o.upper() for v in writes.values() for o in v}
            state = tl / f"{target}.lastbuildstate"
            m = re.search(r"VCToolsVersion=([\d.]+)", state.read_text(errors="replace")) if state.exists() else None
            for src, lines in _tlogs(tl, "command").items():
                if not lines:
                    continue
                obj = d / (Path(src).name.rsplit(".", 1)[0] + ".obj")
                self.units[src] = dict(target=target, flags=command_flags(lines[-1]), deps=reads.get(src, []),
                                       obj=obj if str(obj).upper() in written else None, state=state,
                                       toolset=m.group(1) if m else None,
                                       project=self.root / "build" / f"{target}.vcxproj")

    def read_ninja(self):
        """The units of a Ninja tree: the compile statements of CMakeFiles/impl-Release.ninja, the deps log, the toolset
        from the compiler Invoke-Build configured by full path. The last build is .ninja_log's time: every build
        writes it (ts2origsyms always runs), and a configure after it rewrites the build file."""
        build = self.root / "build"
        cache = (build / "CMakeCache.txt").read_text(errors="replace")
        setting = lambda name: next(iter(re.findall(rf"^{name}:[^=]*=(.*)$", cache, re.M)), "")
        toolset = next(iter(re.findall(r"[/\\]MSVC[/\\]([\d.]+)[/\\]", setting("CMAKE_CXX_COMPILER"))), None)
        deps = ninja_deps(build, setting("CMAKE_MAKE_PROGRAM"))
        project = build / "CMakeFiles/impl-Release.ninja"
        current = None
        for line in project.read_text(errors="replace").splitlines():
            m = self.NINJA_RULE.match(line)
            if m:
                obj, source = (re.sub(r"\$(.)", r"\1", g) for g in (m.group(1), m.group(3)))
                current = dict(obj=obj, target=m.group(2), src=source)
            elif current and line.startswith("  DEFINES = "):
                current["defines"] = line[len("  DEFINES = "):]
            elif current and line.startswith("  FLAGS = "):
                # The source itself makes a logged compile with only system headers a non-empty list, as in a read log.
                read = deps.get(current["obj"].replace("\\", "/").upper())
                self.units[current["src"].upper()] = dict(
                    target=current["target"], flags=ninja_flags(current.get("defines", ""), line[len("  FLAGS = "):]),
                    deps=[] if read is None else [current["src"], *read], obj=build / current["obj"],
                    state=build / ".ninja_log", toolset=toolset, project=project)
                current = None

    def unit(self, source):
        """The build's record for a repo-relative source, or None."""
        if self.root is None:
            return None
        return self.units.get(str((self.root / source).resolve()).upper())

    def here(self, dep):
        """A dependency path from the log (the build's checkout) as the file this checkout compiles: under src/ it is
        ours (a worktree has its own), anything else (build/generated, the toolset) is shared."""
        up, src = dep.upper(), str(self.root / "src").upper() + "\\"
        return self.checkout / "src" / dep[len(src):] if up.startswith(src) else Path(dep)

    def stale(self, source, toolset):
        """None when the build's object for source is current (compiled from this checkout's files, by this
        toolset, with the configured flags), otherwise why not."""
        u = self.unit(source)
        if u is None:
            return "not in the build's compile log (build it)"
        obj = u["obj"]
        if obj is None or not obj.exists():
            return "the build has no object for it"
        try:
            built = u["state"].stat().st_mtime
            if built < max(u["project"].stat().st_mtime, (self.root / "CMakeLists.txt").stat().st_mtime):
                return "CMakeLists.txt or the project changed after the last build (rebuild)"
        except OSError:
            return "no build state"
        if u["toolset"] != toolset:
            return f"built by toolset {u['toolset']}, matchcheck's is {toolset}"
        if not u["deps"]:
            return "the build's read log has no entry for it"
        t = obj.stat().st_mtime
        for dep in [str(self.root / source), *u["deps"]]:
            try:
                if Path(dep).stat().st_mtime > t:
                    return f"object older than {Path(dep).name} (rebuild)"
            except OSError:
                return f"dependency {dep} is gone (rebuild)"
            mine = self.here(dep)
            if mine != Path(dep) and file_sha(mine) != file_sha(dep):
                return f"{mine.relative_to(self.checkout)} differs from the copy the build compiled"
        return None


BUILD = BuildLog()


def compiler_identity(env):
    """The compiler: toolset version, cl path, and size and time of cl.exe and its front and back end."""
    cl = shutil.which("cl", path=env.get("Path") or env.get("PATH"))
    parts = [env.get("VCToolsVersion", "?").strip("\\"), cl]
    for name in ("cl.exe", "c1xx.dll", "c2.dll"):
        p = Path(cl).parent / name
        st = p.stat() if p.exists() else None
        parts.append(f"{name}:{st.st_size}:{int(st.st_mtime)}" if st else f"{name}:-")
    return cl, "|".join(parts)


class Unit:
    """One translation unit as matchcheck compares it: obj (the build's own object when current, else one compiled
    here), cod (a /FAsc listing, or None), verifiable (False with the reason in why: only a current build object
    compiled by the build's own command, without --extra, verifies)."""

    def __init__(self, obj, cod, why):
        self.obj, self.cod, self.why = obj, cod, why
        self.verifiable = why is None


def forbidden_in(source):
    """Rule 3 without a listing: whether the source or anything it includes from src/ has inline asm, __emit or
    naked (then the listing is compiled to find out whether the compared function itself does)."""
    u = BUILD.unit(source)
    files = [ROOT / source] + ([BUILD.here(d) for d in u["deps"]] if u else [])
    if not u:
        return True
    for p in files:
        if str(p).upper().startswith(str(ROOT / "src").upper()) and p.exists() \
                and FORBIDDEN.search(p.read_text(encoding="utf-8", errors="replace")):
            return True
    return False


def compile_units(sources, extra=(), listing=False):
    """{source (repo-relative): Unit}. A source whose build object is current (BuildLog.stale) is compared from that
    object; otherwise, or for a listing (--show, or rule 3 when the source or its src/ headers mention inline asm),
    it is compiled into build/matchcheck/obj with the build's command line for it (and extra). Those objects are
    reused only while the flags, the compiler and every file the compile read (/showIncludes) are unchanged."""
    env = msvc_env()
    cl, ident = compiler_identity(env)
    toolset = env.get("VCToolsVersion", "").strip("\\")
    objdir = OUT / "obj"
    objdir.mkdir(parents=True, exist_ok=True)
    gen = ROOT / "build/generated"
    if not gen.exists():
        gen = tsdb.main_root() / "build/generated"
    out, todo = {}, []
    for s in sorted(set(sources)):
        why = BUILD.stale(s, toolset)
        if extra:
            why = f"experiment (--extra {' '.join(extra)})"
        u = BUILD.unit(s)
        if why is None and not listing and not forbidden_in(s):
            out[s] = Unit(u["obj"], None, None)
            continue
        flags = u["flags"] if u else ["/nologo", *shipped_flags()[0], *profile_flags(s)]
        flags = [*flags, *extra]
        key = hashlib.sha1("\n".join([ident, *flags]).encode()).hexdigest()[:12]
        stem = s.replace("/", "_").replace("\\", "_").rsplit(".", 1)[0] + "_" + key
        obj, cod, deps = objdir / (stem + ".obj"), objdir / (stem + ".cod"), objdir / (stem + ".deps.json")
        # Bytes come from the build's object when it is current (the listing is only for rule 3 and --show).
        out[s] = Unit(u["obj"] if why is None else obj, cod, why)
        if obj.exists() and cod.exists() and deps.exists():
            rec = json.loads(deps.read_text())
            if rec.get("key") == key and all(file_sha(p) == h for p, h in rec["files"].items()):
                continue
        todo.append((s, flags, obj, cod, deps, key))

    priority = tsdb.lane_priority_flag()

    def one(job):  # one cl per TU: each needs its own /Fo and /Fa names
        s, flags, obj, cod, deps, key = job
        cmd = [cl, "/c", *flags, "/showIncludes", "/FAsc", f"/Fa{cod}", f"/Fo{obj}", f"/I{ROOT / 'src'}", f"/I{gen}",
               str(ROOT / s)]
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, creationflags=priority)
        if not r.returncode:
            files = [str(ROOT / s)] + [l[len("Note: including file:"):].strip()
                                       for l in r.stdout.splitlines() if l.startswith("Note: including file:")]
            deps.write_text(json.dumps(dict(key=key, files={f: file_sha(f) for f in files})))
        return s, r

    with ThreadPoolExecutor(max_workers=4) as pool:
        for s, r in pool.map(one, todo):
            if r.returncode:
                sys.stderr.write(r.stdout[-3000:])
                raise SystemExit(f"compile failed: {s}")
    return out



# ---------------------------------------------------------------- symbols

_undname = ctypes.windll.dbghelp.UnDecorateSymbolName
_undname.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32]


def undecorate(name, flags=0x1000):  # UNDNAME_NAME_ONLY
    buf = ctypes.create_string_buffer(2048)
    n = _undname(name.encode("latin-1"), buf, len(buf), flags)
    return buf.value[:n].decode("latin-1") if n else name


def lib_key(name, plain=False):
    """Comparable form of a library function's name: ours decorated (C `_f`, `_f@N`, or C++), or as
    db/crt-equivalents.jsonl writes it (plain: `memcpy`, `operator delete(void*, size_t)`)."""
    if not plain:
        if name.startswith("?"):  # name and parameters, no return type, conventions or access
            name = undecorate(name, 0x2 | 0x4 | 0x80 | 0x100 | 0x200)
        elif name.startswith("_"):
            name = name[1:].split("@")[0]
    return re.sub(r"\s+", "", name.replace("size_t", "unsigned int"))


def norm_name(name):
    """Comparable form of a qualified name: decimal template arguments, no spaces, no leading &."""
    name = re.sub(r"0x([0-9a-fA-F]+)", lambda m: str(int(m.group(1), 16)), name)
    return name.replace(" ", "").lstrip("&")


def member_of(sym):
    """(class, member) of a decorated member function `?member@Class@...@@...` (the innermost class name; thunks
    included), or None."""
    m = re.match(r"\?(\w+)@(\w+)@", sym)
    return (m.group(2), m.group(1)) if m else None


def arity(sym):
    """Number of parameters in a decorated function's undecorated signature, or None."""
    full = undecorate(sym, 0)
    end = full.rfind(")")
    depth, i = 0, end
    while i >= 0:
        depth += {")": 1, "(": -1}.get(full[i], 0)
        if depth == 0:
            break
        i -= 1
    if i < 0:
        return None
    params = split_args(full[i + 1:end])
    return 0 if params in ([], ["void"]) else len(params)


def split_args(text):
    """Top-level comma-separated parts of an argument or parameter list, stripped."""
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "(<[":
            depth += 1
        elif ch in ")>]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


ADAPTER_DEF = re.compile(r"\b(\w+)::(\w+)\s*\(([^()]*)\)\s*\{\s*\[\[msvc::forceinline_calls\]\]\s*(?:return\s+)?"
                         r"([^;{}]*);\s*\}", re.S)
QNAME = r"(?:::)?(?:\w+\s*::\s*)*\w+"  # a possibly qualified name: `Terrain`, `Adapters::Terrain`, `::ts::cX`
ADAPTER_CALL = re.compile(rf"^(?:static_cast<\s*({QNAME})\s*\*\s*>\(\s*(\w+)\s*\)|(\w+))\s*->\s*({QNAME})::(\w+)\s*\((.*)\)$",
                          re.S)


ALIAS = re.compile(r"\busing\s+(\w+)\s*=\s*(?:struct\s+|class\s+)?([\w:]+)\s*;")
BRACE = re.compile(r"[{}]")


class SourceIndex:
    """Our sources read once for adapter_target: forwarding definitions by member name, `using` aliases per file and
    per struct/class body (by the struct's name)."""

    def __init__(self, texts):
        self.defs, self.aliases = defaultdict(list), {}
        self.scoped = defaultdict(lambda: defaultdict(set))
        for path, text in texts.items():
            self.aliases[path] = al = defaultdict(set)
            for a, b in ALIAS.findall(text):
                al[a].add(b.split("::")[-1])
            for m in ADAPTER_DEF.finditer(text):
                self.defs[m.group(2)].append((path, m))
            for m in re.finditer(r"\b(?:struct|class)\s+(\w+)\b[^;{()]*\{", text):
                depth, end = 1, len(text)
                for b in BRACE.finditer(text, m.end()):
                    depth += 1 if b.group() == "{" else -1
                    if not depth:
                        end = b.end()
                        break
                for a, b in ALIAS.findall(text, m.end(), end):
                    self.scoped[m.group(1)][a].add(b.split("::")[-1])


def adapter_target(sym, texts):
    """What the adapter `sym` (decorated) forwards to, read from our sources (texts: {path: text}): dict(cls, member,
    arity, receiver (the class or interface its first parameter points to), cast (static_cast to the class)) when its
    one definition is exactly the standalone-shape adapter (docs/standalone-shape.md, step 6):
        R __fastcall S::f(Recv *p, void *, A1 a1, ...) { [[msvc::forceinline_calls]] return <p or
            static_cast<X *>(p)>->X::g(a1, ...); }
    (types through `using` aliases), otherwise why not. The qualified call is X::g's behaviour by the language (no
    virtual dispatch), with the adapter's own arguments in order."""
    mo = member_of(sym)
    if not mo:
        return "not a member function"
    struct_name, member = mo
    ix = texts if isinstance(texts, SourceIndex) else SourceIndex(texts)
    # Names in a member definition `S::f(...) {...}` resolve in S's scope first: the aliases in S's own body.
    scoped = ix.scoped.get(struct_name, {})
    found = []
    for path, m in ix.defs.get(member, []):
        aliases = ix.aliases[path]

        def res(n, aliases=aliases):
            # A qualified name `A::B` (FO: `Adapters::Terrain *face`): B in the scope of the struct A names (through
            # aliases), else B as the last component of a namespace path.
            parts = [p for p in re.split(r"\s*::\s*", n.strip()) if p]
            if len(parts) > 1:
                head = res("::".join(parts[:-1]))
                inner = ix.scoped.get(head, {}).get(parts[-1]) if head else None
                if inner:
                    return next(iter(inner)) if len(inner) == 1 else None
            n = parts[-1] if parts else n
            got = scoped.get(n) or aliases.get(n, {n})
            return next(iter(got)) if len(got) == 1 else None

        if res(m.group(1)) == struct_name:
            found.append((path, m, res))
    if len(found) != 1:
        return f"{'no' if not found else len(found)} forwarding definitions of {struct_name}::{member} in our sources"
    path, m, res = found[0]
    params = split_args(m.group(3))
    if len(params) < 2 or not re.fullmatch(r"void\s*\*", params[1]):
        return "adapter parameters are not (receiver, void *, ...)"
    recv = re.fullmatch(rf"(?:const\s+)?({QNAME})\s*\*\s*(\w+)", params[0])
    names = [re.search(r"(\w+)\s*$", p) for p in params[2:]]
    call = ADAPTER_CALL.match(m.group(4).strip())
    if not recv or not call or not all(names):
        return "adapter body is not a single qualified call on its receiver"
    cast_to, cast_of, plain, qual, called, args = call.groups()
    if (cast_of or plain) != recv.group(2):
        return "the call's object is not the adapter's receiver"
    cls = res(qual)
    if cls is None or cast_to is not None and res(cast_to) != cls:
        return "the qualified call's class and the cast differ"
    if split_args(args) != [n.group(1) for n in names]:
        return "the call's arguments are not the adapter's parameters in order"
    return dict(cls=cls, member=called, arity=len(names), receiver=res(recv.group(1)), cast=cast_to is not None,
                where=path)


HOOK_ENTRY = 20  # HookSpec (src/hook/hooks.h): {const char *name, uint32_t rva, checkLen, checkCrc, void *replacement}


def hook_entries(obj):
    """[(hook name, rva, decorated replacement or None)] from the `kHooks` arrays of a compiled hook table object.
    The replacement is the symbol of the static relocation at +16 of the entry (the exact function the hook installs,
    overloads included); None when the entry has none (a dynamic initializer fills it) or it has an addend."""
    out = []
    for name, value, sec, cls, typ in obj.symbols.values():
        if sec is None or not re.match(r"\?kHooks@", name):
            continue
        s = obj.sections[sec]
        rel = {va: (si, t) for va, si, t in s["relocs"]}
        end = min((v for n, v, sc, c, t in obj.symbols.values() if sc == sec and v > value and c in (2, 3)),
                  default=len(s["raw"]))
        for k in range(value, end - HOOK_ENTRY + 1, HOOK_ENTRY):
            n = rel.get(k)
            hook = "?"
            if n and n[1] == DIR32:
                nsym = obj.symbols[n[0]]
                if nsym[2] is not None:
                    raw = obj.sections[nsym[2]]["raw"]
                    p = nsym[1] + struct.unpack_from("<I", s["raw"], k)[0]
                    hook = raw[p:raw.index(b"\0", p)].decode("latin-1") if b"\0" in raw[p:] else "?"
            rva = struct.unpack_from("<I", s["raw"], k + 4)[0]
            t = rel.get(k + 16)
            target = obj.symbols[t[0]][0] if t and t[1] == DIR32 and \
                struct.unpack_from("<I", s["raw"], k + 16)[0] == 0 else None
            out.append((hook, hex(rva), target))
    return out


def resolve_hooks(tables):
    """tables: {table source: Obj}. Returns (targets {rva: decorated replacement}, ambiguous {rva: why},
    where {rva: [table sources]}). An RVA whose entries lack a static target, or name different functions, is
    ambiguous: never resolved by name or guessed."""
    seen, where = defaultdict(set), defaultdict(list)
    for tu, obj in tables.items():
        for hook, rva, target in hook_entries(obj):
            seen[rva].add(target)
            where[rva].append(tu)
    targets, ambiguous = {}, {}
    for rva, names in seen.items():
        if None in names:
            ambiguous[rva] = "hook table entry without a static relocation to its replacement (dynamic init)"
        elif len(names) > 1:
            ambiguous[rva] = "hook table entries name different replacements: " + ", ".join(sorted(names))
        else:
            targets[rva] = next(iter(names))
    return targets, ambiguous, where


# ---------------------------------------------------------------- COFF

class Obj:
    def __init__(self, path):
        self.path = path
        d = self.data = path.read_bytes()
        machine, nsect, _, symptr, nsyms, optsize, _ = struct.unpack_from("<HHIIIHH", d, 0)
        self.sections = []
        for i in range(nsect):
            o = 20 + optsize + 40 * i
            name, _, _, rawsize, rawptr, relptr, _, nrel, _, chars = struct.unpack_from("<8sIIIIIIHHI", d, o)
            relocs = [struct.unpack_from("<IIH", d, relptr + 10 * r) for r in range(nrel)]
            self.sections.append(dict(name=name.rstrip(b"\0").decode("latin-1"), raw=d[rawptr:rawptr + rawsize]
                                      if rawptr else b"\0" * rawsize, relocs=relocs, chars=chars))
        strtab = symptr + 18 * nsyms
        self.symbols = {}  # index -> (name, value, section index or None, class)
        i = 0
        while i < nsyms:
            o = symptr + 18 * i
            if d[o:o + 4] == b"\0\0\0\0":
                off = struct.unpack_from("<I", d, o + 4)[0]
                name = d[strtab + off:d.index(b"\0", strtab + off)].decode("latin-1")
            else:
                name = d[o:o + 8].rstrip(b"\0").decode("latin-1")
            value, secnum, typ, cls, naux = struct.unpack_from("<IhHBB", d, o + 8)
            self.symbols[i] = (name, value, secnum - 1 if secnum > 0 else None, cls, typ)
            i += 1 + naux

    def functions(self):
        """{decorated name: (section index, offset)} for external/static function symbols in code."""
        out = {}
        for name, value, sec, cls, typ in self.symbols.values():
            if sec is not None and (typ & 0xF0) == 0x20 and cls in (2, 3) \
                    and self.sections[sec]["chars"] & IMAGE_SCN_CNT_CODE:
                out.setdefault(name, (sec, value))
        return out


def obj_rtti_classes(path):
    """RTTI class names (`.?AVname@ns@@`) of the type descriptors (`??_R0?AV...@8`) an object file defines, read from
    its symbol table only (cheap enough to scan every object of the build)."""
    d = Path(path).read_bytes()
    _, _, _, symptr, nsyms, _, _ = struct.unpack_from("<HHIIIHH", d, 0)
    strtab, out, i = symptr + 18 * nsyms, set(), 0
    while i < nsyms:
        o = symptr + 18 * i
        if d[o:o + 4] == b"\0\0\0\0":
            off = struct.unpack_from("<I", d, o + 4)[0]
            name = d[strtab + off:d.index(b"\0", strtab + off)].decode("latin-1")
        else:
            name = d[o:o + 8].rstrip(b"\0").decode("latin-1")
        secnum, naux = struct.unpack_from("<h", d, o + 12)[0], d[o + 17]
        if secnum > 0 and name.startswith("??_R0?A") and name.endswith("@8"):
            out.add("." + name[5:-2])
        i += 1 + naux
    return out


RTTI_PLAIN = re.compile(r"\.\?A([UV])(\w+)(?:@\w+)*@@")


def rtti_bare(mangled):
    """An RTTI class name without its qualifiers (namespaces, enclosing classes): `.?AVcGZFramework@gz@@` ->
    `.?AVcGZFramework@@`; None for a name that isn't a plain qualified identifier (templates `?$`, anonymous
    namespaces), which only an exact name ties."""
    m = RTTI_PLAIN.fullmatch(mangled)
    return f".?A{m.group(1)}{m.group(2)}@@" if m else None


# ---------------------------------------------------------------- the original image

class Original:
    def __init__(self):
        import pefile
        self.pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        d = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[5]
        raw = self.pe.get_data(d.VirtualAddress, d.Size)
        self.relocs = set()
        pos = 0
        while pos + 8 <= len(raw):
            page, size = struct.unpack_from("<II", raw, pos)
            if size < 8:
                break
            for k in range(8, size, 2):
                e = struct.unpack_from("<H", raw, pos + k)[0]
                if e >> 12 == 3:
                    self.relocs.add(page + (e & 0xFFF))
            pos += size

        self.pe.parse_data_directories(directories=[10])  # load config: the /GS cookie
        lc = getattr(self.pe, "DIRECTORY_ENTRY_LOAD_CONFIG", None)
        self.cookie = lc.struct.SecurityCookie - self.base if lc else None

    def read(self, rva, n):
        return self.pe.get_data(rva, n)

    def dword(self, rva):
        return struct.unpack("<I", self.read(rva, 4))[0]


# ---------------------------------------------------------------- comparison

def listing_lines(cod_path, symbol):
    """{offset: (line number, source text)} for one function in a /FAsc listing, plus all its source lines."""
    if cod_path is None or not cod_path.exists():  # no listing: rule 3 was checked on the sources (forbidden_in)
        return {}, []
    lines = cod_path.read_text(encoding="latin-1").splitlines()
    out, src_lines, inside, cur = {}, [], False, None
    for l in lines:
        if not inside:
            if l.startswith(symbol + " PROC") or l.startswith(symbol + "\tPROC"):
                inside = True
            continue
        if l.startswith(symbol + " ENDP") or l.startswith(symbol + "\tENDP"):
            break
        m = re.match(r";\s*(\d+)\s*:\s?(.*)$", l)
        if m:
            cur = (int(m.group(1)), m.group(2).rstrip())
            src_lines.append(cur[1])
            continue
        m = re.match(r"\s+([0-9a-f]{5,8})\s+[0-9a-f]{2}", l)
        if m and cur:
            out.setdefault(int(m.group(1), 16), cur)
    return out, src_lines


_cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def instructions(code, masked, labels=None, call_name=None):
    """[(offset, size, normalised text, display text)]: operands touching masked bytes become `#`.
    labels: {field offset: target name} shown next to the instruction; call_name(offset of a rel32
    call/jmp's target relative to the code) names unrelocated near calls (the original's)."""
    out = []
    labels = labels or {}
    for ins in _cs.disasm(code, 0):
        text = f"{ins.mnemonic} {ins.op_str}".strip()
        span = range(ins.address, ins.address + ins.size)
        far = ins.size == 5 and code[ins.address] in (0xE8, 0xE9) or             ins.size == 6 and code[ins.address] == 0x0F and 0x80 <= code[ins.address + 1] <= 0x8F
        if any(o in masked for o in span) or far and (ins.address + ins.size > len(code) or
                                                    not 0 <= int(ins.op_str, 16) < len(code)):
            norm = re.sub(r"0x[0-9a-f]+", "#", text)
        else:
            norm = text
        label = next((labels[o] for o in span if o in labels), None)
        if label is None and far and call_name and norm.endswith("#"):
            label = call_name(int(ins.op_str, 16))
        if label:
            text = re.sub(r"0x[0-9a-f]+$", "", text) + f"<{label}>" if far else f"{text}  <{label}>"
        out.append((ins.address, ins.size, norm, text))
    end = out[-1][0] + out[-1][1] if out else 0
    if end < len(code):  # data (jump tables) after the code
        out.append((end, len(code) - end, f"db {code[end:].hex()}", "(data)"))
    return out


class Checker:
    use_v7 = True  # False: v6 rules (as_v6)
    use_v8 = True  # False: v7's class lookup (exact RTTI name, unqualified faces only)

    def __init__(self):
        self.orig = Original()
        self.funcs = {f["rva"]: f for f in tsdb.functions()}
        self.status = tsdb.load_status()
        self.impl_rvas = defaultdict(set)  # decorated replacement -> rvas it replaces (set_hooks)
        self.thunks = tsdb.thunk_targets(self.funcs)
        self.use_equiv = True
        self._entry = {}
        self.use_callee_proof = True
        self._proofs, self._proving, self._used, self._cod = {}, [], [], {}
        self._eh, self._relmaps = [], {}  # EH handler references per compare() frame; relocations by section
        self._why = {}  # (object, symbol, rva) -> first reason a callee proof failed
        self.proven = {}  # (object, symbol, rva) -> (signature, equivalences used): proven callee pairs
        self.library = defaultdict(set)  # rva -> decorated names / undecorated names
        for m in tsdb.jsonl("library-matches.jsonl"):
            self.library[m["rva"]].update(m["names"])
        for e in tsdb.jsonl("crt-equivalents.jsonl"):
            self.library[e["rva"]].add(e["name"])
        # v7: the original's vftables by (RTTI decorated class name, subobject offset), for vtable references and ties
        self.vftables = {(c["mangled"], v["offset"]): v for c in tsdb.jsonl("classes.jsonl") for v in c["vftables"]}
        self.note_our_classes(())

    def _game_index(self):
        """The original's class names (from self.vftables) and those names by bare name (rtti_bare), rebuilt when
        self.vftables is replaced."""
        idx = self.__dict__.get("_gidx")
        if idx is None or idx[0] is not self.vftables:
            names, bare = {m for m, _ in self.vftables}, defaultdict(set)
            for m in names:
                b = rtti_bare(m)
                if b:
                    bare[b].add(m)
            idx = self._gidx = (self.vftables, names, bare)
        return idx[1], idx[2]

    def note_our_classes(self, names):
        """Our RTTI class names (every polymorphic class our build defines: obj_rtti_classes over the build's objects
        and the ones compared), for game_class's check that no two of our classes take one original class."""
        self._our_bare = defaultdict(set)
        for m in names:
            b = rtti_bare(m)
            if b:
                self._our_bare[b].add(m)
        self.tie_refused = {}  # our RTTI class name -> why game_class refused it (printed by main)

    def game_class(self, mangled):
        """(the original's RTTI class name our class `mangled` is, None) or (None, why not). v7: the same name. v8: when
        the original has no class of that name, the one original class with the same name without qualifiers (our
        shape classes live in namespaces, `.?AVcGZFramework@gz@@`, the game's RTTI has `.?AVcGZFramework@@`), refused
        when that is ambiguous: the bare name names several original classes (`cDllCOMDirector@nTSSG`,
        `cDllCOMDirector@nTSWorld`, ...), or several of our classes have it."""
        game_names, game_bare = self._game_index()
        if mangled in game_names:
            return mangled, None
        b = rtti_bare(mangled) if self.use_v8 else None
        games = sorted(game_bare.get(b, ())) if b else []
        if not games:
            return None, f"the original has no vftable of {mangled}"
        if len(games) > 1:
            why = f"ambiguous: {mangled} without its namespace is {len(games)} original classes ({', '.join(games)})"
        else:
            ours = sorted(getattr(self, "_our_bare", {}).get(b, set()) | {mangled})
            if len(ours) == 1:
                return games[0], None
            why = f"ambiguous: our classes {', '.join(ours)} all name the original's {games[0]}"
        self.__dict__.setdefault("tie_refused", {})[mangled] = why
        return None, why

    def as_v6(self, targets):
        """A checker sharing this one's facts (original image, functions, library, status) that applies v6's rules: no
        vtable references to the original's vftables, no vtable ties; its own memo tables, since callee proofs may
        differ. For --brief, which prints v6's verdict where it differs from v7's."""
        v6 = copy.copy(self)
        v6.use_v7 = False
        v6._proofs, v6._proving, v6._used, v6._why, v6.proven = {}, [], [], {}, {}
        v6._eh = []
        v6.forwards = {}
        v6.__dict__.pop("_vtabs", None)
        v6.set_hooks(targets)
        return v6

    def set_hooks(self, targets):
        """targets: {rva: decorated replacement} from the compiled hook tables (resolve_hooks; ambiguous RVAs left
        out)."""
        self.impl_rvas = defaultdict(set)
        for rva, sym in targets.items():
            self.impl_rvas[sym].add(rva)

    def final(self, rva):
        seen = set()
        while rva in self.thunks and rva not in seen:
            seen.add(rva)
            rva = self.thunks[rva]
        return rva

    @staticmethod
    def library_name_match(sym, names):
        """Our decorated `sym` is one of a library entry's names. Compared by full signature (decorated name, or
        undecorated with parameters), never by bare name: template arguments and parameter types must agree
        (set<int> vs set<unsigned int>, operator delete(void*) vs (void*, size_t)). A plain C name (`free`) ties
        only to a C symbol of ours (`_free`)."""
        if sym in names:
            return True
        ours = lib_key(sym)
        for n in names:
            if n.startswith("?"):
                if lib_key(n) == ours:
                    return True
            elif "(" in n:  # crt-equivalents C++ signature
                if sym.startswith("?") and lib_key(n, plain=True) == ours:
                    return True
            elif not sym.startswith("?") and lib_key(n, plain=True) == ours:
                return True
        return False

    def prove_callee(self, obj, sym, rva):
        """True when our function `sym`, defined in obj (an inline/template/library COMDAT of the same TU), is
        proven equivalent to the original function at rva by this checker's own rules (bytes, rule 2 for its
        relocations, the v2 equivalences), recursively for its callees. Memoised per (object, symbol, rva);
        a pair already being proven further up is assumed equal (coinduction: recursive calls are consistent
        with it), and when a proof fails, every pair proven since it started is forgotten (it may rest on
        an assumption that just failed)."""
        key = (str(obj.path), sym, rva)
        if key in self._proofs:
            return self._proofs[key]
        if key in self._proving:
            return True
        if rva not in self.funcs or sym not in obj.functions() or len(self._proving) >= CALLEE_DEPTH \
                or sym == GAMEBASE or rva_symbol(sym) is not None:
            return False
        self._proving.append(key)
        before = set(self._proofs)
        try:
            r = self.compare(rva, obj, self._cod.get(str(obj.path), obj.path.with_suffix(".cod")), sym)
        finally:
            self._proving.pop()
        ok = bool(r["match"])
        if not ok:
            self._why[key] = (r["reasons"] or ["?"])[0]
            for k in set(self._proofs) - before:
                if self._proofs[k]:
                    del self._proofs[k]
                    self.proven.pop(k, None)
        self._proofs[key] = ok
        if ok:
            self.proven[key] = (undecorate(sym, 0), r["equiv"])
        return ok

    def resolve_call(self, sym, orig_target, obj=None):
        """Why our call to `sym` is (or isn't) the original's call to orig_target."""
        t = hex(orig_target)
        targets = {t, self.final(t)}
        sr = rva_symbol(sym)
        if sr is not None:
            return hex(sr) in targets, f"original {hex(sr)}"
        full = norm_name(undecorate(sym))
        rvas = self.impl_rvas.get(sym, set())  # by decorated name: the exact function a hook installs
        if rvas & targets:
            return True, ""
        for tt in targets:
            if self.library_name_match(sym, self.library.get(tt, ())):
                return True, ""
        if obj is not None and self.use_callee_proof:
            for tt in targets:
                if self.prove_callee(obj, sym, tt):
                    if self._used:
                        self._used[-1].add("callee-proof")
                    return True, ""
        what = f"our {full}" + (f" (replaces {', '.join(sorted(rvas))})" if rvas else "")
        why = next((self._why[k] for k in ((str(obj.path), sym, tt) for tt in targets) if k in self._why), None) \
            if obj is not None else None
        return False, f"{what} vs original {t}" + (f" (callee proof: {why})" if why else "")

    def data_ref(self, obj, symidx, field, orva, depth=0):
        """None when our absolute reference (symbol + field) is the original's absolute reference to orva
        (rule 2: a ts2_rva symbol of the same RVA, a compiler literal with the same contents, the /GS cookie),
        otherwise why not."""
        o = self.orig
        tname, tval, tsec, _, _ = obj.symbols[symidx]
        sr = rva_symbol(tname)
        if sr is not None:
            return None if sr + field == orva else f"global {tname} vs original {orva:#x}"
        if tsec is not None and not obj.sections[tsec]["chars"] & IMAGE_SCN_CNT_CODE:
            if tname.startswith("??_7") and self.use_v7:  # v7: our vtable stands for the original's vftable of the same face
                return self.vtable_ref(obj, symidx, field, orva)
            if not tname.startswith(LITERALS):
                # Named data has an address identity in C++ (a static, a vtable, a const table): two objects with
                # equal contents are still different objects, so only a ts2_rva symbol ties it to the original's.
                return f"reference to our data {undecorate(tname)} (named data: only a ts2_rva symbol ties it)"
            return self.constant(obj, symidx, field, orva, depth)
        if tname == "___security_cookie" and orva == o.cookie:
            return None
        if tname.startswith("__ehhandler$"):
            if not self._eh:
                return "EH handler outside a function comparison"
            # Deferred: compare() checks the handler stub, its FuncInfo and funclets once the function's code
            # matched (the funclets' references into the function need the code's offset mapping).
            self._eh[-1].add((symidx, field, orva))
            return None
        return f"absolute ref to our {undecorate(tname)} vs original {orva:#x}"

    # -- v7: vtables of classes in the standalone shape (docs/standalone-shape.md), ours vs the original's

    def our_vtable(self, obj, symidx):
        """Our vtable `??_7...` at symbol symidx: dict(mangled (the RTTI class name its locator names), offset (the
        locator's subobject offset), face (the base named in the decorated name, or None), slots [(symbol index,
        decorated name)]), or why it can't be read. The layout is MSVC's: the locator pointer in the word before the
        vtable, slots up to the end of its COMDAT section (or the next symbol)."""
        key = (str(obj.path), symidx)
        cache = self.__dict__.setdefault("_vtabs", {})
        if key in cache:
            return cache[key]
        name, value, sec = obj.symbols[symidx][:3]
        out = "not a vtable in our object"
        if sec is not None and name.startswith("??_7"):
            rel = self._relmap(obj, sec)
            col = rel.get(value - 4)
            out = "no complete-object locator before it"
            if col and col[1] == DIR32:
                cname, cval, csec = obj.symbols[col[0]][:3]
                if csec is not None:
                    craw, crel = obj.sections[csec]["raw"], self._relmap(obj, csec)
                    p = cval + struct.unpack_from("<I", obj.sections[sec]["raw"], value - 4)[0]
                    td = crel.get(p + 12)
                    out = "locator without a type descriptor"
                    if td and td[1] == DIR32 and obj.symbols[td[0]][2] is not None:
                        tsym = obj.symbols[td[0]]
                        traw = obj.sections[tsym[2]]["raw"]
                        q = tsym[1] + struct.unpack_from("<I", craw, p + 12)[0] + 8
                        mangled = traw[q:traw.index(b"\0", q)].decode("latin-1")
                        s = obj.sections[sec]
                        end = len(s["raw"]) if s["chars"] & IMAGE_SCN_LNK_COMDAT else \
                            min((v for n, v, sc, c, t in obj.symbols.values() if sc == sec and v > value), default=len(s["raw"]))
                        slots = []
                        for k in range(value, end - 3, 4):
                            r = rel.get(k)
                            if r is None or r[1] != DIR32 or struct.unpack_from("<I", s["raw"], k)[0]:
                                break
                            slots.append((r[0], obj.symbols[r[0]][0]))
                        # the face: the one base named after 6B; v8 also reads a qualified one (`6BcIFoo@gz@@@`,
                        # `6BcIFoo@1@@` with a back-reference) by its own name, as adapter_target names receivers
                        m = re.search(r"@@6B(\w+)@(?:\w+@|\d)*@@$" if self.use_v8 else r"@@6B(\w+)@@@$", name)
                        out = dict(mangled=mangled, offset=struct.unpack_from("<I", craw, p + 4)[0],
                                   face=m.group(1) if m else None, slots=slots)
        cache[key] = out
        return out

    def vtable_ref(self, obj, symidx, field, orva):
        """None when our reference to our vtable (+field) is the original's reference to orva (rule 2, v7): orva - field
        is the original's vftable for the same RTTI class and subobject offset, both have the same number of slots,
        and no slot is known to be another function (slot_conflict). What each slot does is that slot function's own
        verification, as a call to a hooked replacement is accepted whatever the replacement's status."""
        tname = obj.symbols[symidx][0]
        vt = self.our_vtable(obj, symidx)
        if isinstance(vt, str):
            return f"our vtable {undecorate(tname)}: {vt}"
        game, why = self.game_class(vt["mangled"])
        if game is None:
            return f"our vtable {undecorate(tname)}: {why}"
        ov = self.vftables.get((game, vt["offset"]))
        if ov is None:
            return f"our vtable {undecorate(tname)}: the original has no vftable of {game} at +{vt['offset']:#x}"
        if orva - field != int(ov["rva"], 16) or field % 4 or not 0 <= field < 4 * len(vt["slots"]):
            return f"our vtable {undecorate(tname)} +{field:#x} vs original {orva:#x} (its vftable is {ov['rva']})"
        if len(vt["slots"]) != len(ov["entries"]):
            return f"our vtable {undecorate(tname)}: {len(vt['slots'])} slots, the original's {ov['rva']} has " \
                   f"{len(ov['entries'])}"
        for k, ((_, sym), entry) in enumerate(zip(vt["slots"], ov["entries"])):
            why = self.slot_conflict(sym, entry, vt)
            if why:
                return f"our vtable {undecorate(tname)} slot {k}: {why}"
        return None

    def slot_conflict(self, sym, entry, vt):
        """Why our slot function `sym` can't be the original's slot function `entry`, when something says so: our
        function is the hook replacement of another RVA, or the original slot's hook adapter forwards to another member
        of ours (adapter_target). None otherwise (including unknown: a slot nobody hooked)."""
        targets = {entry, self.final(entry)}
        rvas = self.impl_rvas.get(sym, set())
        if rvas and not rvas & targets:
            return f"ours {undecorate(sym)} replaces {', '.join(sorted(rvas))}, the original's slot is {entry}"
        fwd = getattr(self, "forwards", {}).get(entry)
        if fwd and not isinstance(fwd, str) and member_of(sym) != (fwd["cls"], fwd["member"]):
            return f"the hook on {entry} forwards to {fwd['cls']}::{fwd['member']}, ours is {undecorate(sym)}"
        return None

    def find_ties(self, objs, targets, texts):
        """v7 vtable ties. For each hooked RVA R whose replacement is an adapter that only forwards to a member
        (adapter_target, read from our sources: `texts`), and each of our vtables (in `objs`) whose slot k the
        original's vftable of the same class and face fills with R: when slot k holds that member, of that class, for
        that face, the slot function is tied to R. A tied function is R's replacement as much as the adapter is
        (rule 2: calls to it count as calls to R), and matchcheck compares it with R when the adapter doesn't match.
        Returns {rva: [decorated tied function]} and sets self.forwards {rva: target or why not}."""
        self.forwards = {}
        self.__dict__.setdefault("tie_refused", {})
        ix = SourceIndex(texts)
        for rva, sym in targets.items():
            self.forwards[rva] = adapter_target(sym, ix)
        ties = defaultdict(set)
        for obj in objs:
            for i, (name, value, sec, cls, typ) in obj.symbols.items():
                if sec is None or not name.startswith("??_7"):
                    continue
                vt = self.our_vtable(obj, i)
                if isinstance(vt, str):
                    continue
                game, _ = self.game_class(vt["mangled"])  # a refusal is kept in self.tie_refused
                ov = self.vftables.get((game, vt["offset"])) if game else None
                if ov is None or len(ov["entries"]) != len(vt["slots"]):
                    continue
                for (_, sym), entry in zip(vt["slots"], ov["entries"]):
                    fwd = self.forwards.get(entry)
                    if not fwd or isinstance(fwd, str) or member_of(sym) != (fwd["cls"], fwd["member"]):
                        continue
                    if fwd["arity"] != arity(sym):
                        continue
                    # The adapter's receiver is the face pointer the slot gets: the class itself only at offset 0,
                    # otherwise that face's interface (cast back with static_cast).
                    if vt["offset"] == 0 and fwd["receiver"] in (fwd["cls"], vt["face"]) or \
                            vt["offset"] != 0 and fwd["receiver"] == vt["face"] and fwd["cast"]:
                        ties[entry].add(sym)
        for rva, syms in ties.items():
            for s in syms:
                self.impl_rvas[s].add(rva)
        return {r: sorted(s) for r, s in ties.items()}

    @staticmethod
    def extent(obj, sec, value):
        """[start, end) of the data object at `value` in section sec, or why it can't be established: a COMDAT
        section is one object (its symbol must be at its start); otherwise from the symbol to the next symbol in
        the section (none: unknown)."""
        s = obj.sections[sec]
        if s["chars"] & IMAGE_SCN_LNK_COMDAT:
            return (0, len(s["raw"])) if value == 0 else "symbol not at the start of its COMDAT section"
        nxt = [v for n, v, sc, c, t in obj.symbols.values() if sc == sec and v > value and c in (2, 3)]
        return (value, min(nxt)) if nxt else "extent unknown (no following symbol in its section)"

    def constant(self, obj, symidx, field, orva, depth=0):
        """A compiler literal (__real@, __xmm@, ??_C@: no address identity, pooling allowed) compared by contents:
        the whole object from its start (orva - field on the original's side), relocated fields only where both
        sides have one at the same place and each resolves by rule 2."""
        o = self.orig
        tname, tval, tsec, _, _ = obj.symbols[symidx]
        ext = self.extent(obj, tsec, tval)
        if isinstance(ext, str):
            return f"constant {tname}: {ext}"
        lo, hi = ext
        if not 0 <= field <= hi - lo:
            return f"constant {tname}: reference +{field:#x} outside the object"
        ostart, n = orva - field, hi - lo
        ours, theirs = obj.sections[tsec]["raw"][lo:hi], o.read(ostart, n)
        if len(theirs) != n:
            return f"constant {tname}: contents differ"
        mine = {va - lo: (si, t) for va, si, t in obj.sections[tsec]["relocs"] if lo <= va < hi}
        orel = {k for k in range(-3, n) if ostart + k in o.relocs}  # a field straddling the start counts too
        if set(mine) != orel:
            return f"constant {tname}: relocated fields differ"
        if mine and depth >= CALLEE_DEPTH:
            return f"constant {tname}: relocations nested too deep"
        masked = set()
        for k, (si, t) in sorted(mine.items()):
            if t != DIR32 or k + 4 > n:
                return f"constant {tname}: relocation type {t:#x} at +{k:#x}"
            masked.update(range(k, k + 4))
            why = self.data_ref(obj, si, struct.unpack_from("<I", ours, k)[0], o.dword(ostart + k) - o.base, depth + 1)
            if why:
                return f"constant {tname} +{k:#x}: {why}"
        if any(ours[k] != theirs[k] for k in range(n) if k not in masked):
            return f"constant {tname}: contents differ"
        return None

    # -- C++ EH frames (v5): the handler stub, its FuncInfo and the funclets it names

    def _relmap(self, obj, sec):
        key = (str(obj.path), sec)
        m = self._relmaps.get(key)
        if m is None:
            m = self._relmaps[key] = {va: (si, t) for va, si, t in obj.sections[sec]["relocs"]}
        return m

    def _optr(self, rva):
        """The original's pointer at rva: an RVA, None for null, or "bad" (non-null without a relocation)."""
        v = self.orig.dword(rva)
        if rva in self.orig.relocs:
            return v - self.orig.base
        return None if v == 0 else "bad"

    def _mptr(self, obj, sec, k):
        """Our pointer at offset k of section sec: (symbol index, addend), None for null, or "bad"."""
        v = struct.unpack_from("<I", obj.sections[sec]["raw"], k)[0]
        r = self._relmap(obj, sec).get(k)
        if r is None:
            return None if v == 0 else "bad"
        return (r[0], v) if r[1] == DIR32 else "bad"

    def _loc(self, obj, p):
        """(section, offset) our pointer p = (symbol, addend) points at, or None for an external symbol."""
        name, value, sec = obj.symbols[p[0]][:3]
        return None if sec is None else (sec, value + p[1])

    def eh_frame(self, obj, symidx, field, orva, ctx):
        """None when our EH handler stub (symbol + field) is equivalent to the original's stub at orva: the stub
        code compared like a funclet (bytes, relocations by rule 2, the jmp to __CxxFrameHandler3), and the FuncInfo
        it loads compared field by field (eh_funcinfo). ctx: the function the frame belongs to (eh_code)."""
        return self.eh_code(obj, (symidx, field), orva, ctx, "EH handler stub")

    def eh_funcinfo(self, obj, p, orva, ctx):
        """FuncInfo (x86, __CxxFrameHandler3 magic 0x19930520..22): magic, maxState, the unwind map (toState and
        action funclet per state), the try-block map (tryLow, tryHigh, catchHigh and per catch: adjectives, the
        caught type by its TypeDescriptor name, dispCatchObj, the handler funclet), nIPMapEntries (0 on x86),
        the ESTypeList and EHFlags."""
        loc = self._loc(obj, p)
        if loc is None:
            return "our FuncInfo is external"
        sec, b = loc
        raw = obj.sections[sec]["raw"]
        o = self.orig
        mi = lambda k: struct.unpack_from("<i", raw, b + k)[0]
        oi = lambda k: struct.unpack("<i", o.read(orva + k, 4))[0]
        magic = mi(0) & 0xFFFFFFFF
        if magic != oi(0) & 0xFFFFFFFF or magic not in EH_MAGIC:
            return f"FuncInfo magic {magic:#x} vs original {oi(0) & 0xFFFFFFFF:#x}"
        fields = [("maxState", 4), ("nTryBlocks", 0xc), ("nIPMapEntries", 0x14)] + \
            ([("EHFlags", 0x20)] if magic >= 0x19930522 else [])
        for n, k in fields:
            if mi(k) != oi(k):
                return f"FuncInfo {n} {mi(k)} vs original {oi(k)}"
        if mi(0x14) or self._mptr(obj, sec, b + 0x18) is not None or self._optr(orva + 0x18) is not None:
            return "FuncInfo has an IP-to-state map (not compared)"

        def table(name, k, n, esize):
            """(our section, offset, original rva) of a table of n entries pointed to at offset k, or a reason."""
            mp, op = self._mptr(obj, sec, b + k), self._optr(orva + k)
            if n == 0 and mp is None and op is None:
                return None
            if mp in (None, "bad") or op in (None, "bad") or self._loc(obj, mp) is None:
                return f"FuncInfo {name}: ours {'set' if mp else 'null'}, original {'set' if op else 'null'}"
            s2, o2 = self._loc(obj, mp)
            if o2 + n * esize > len(obj.sections[s2]["raw"]):
                return f"FuncInfo {name} runs past our section"
            return s2, o2, op

        max_state, ntry = mi(4), mi(0xc)
        if max_state < 0 or ntry < 0 or max_state > 4096 or ntry > 4096:
            return "FuncInfo counts out of range"
        t = table("unwind map", 8, max_state, 8)
        if isinstance(t, str):
            return t
        for i in range(max_state):
            s2, o2, op = t
            mt, ot = struct.unpack_from("<i", obj.sections[s2]["raw"], o2 + 8 * i)[0], \
                struct.unpack("<i", o.read(op + 8 * i, 4))[0]
            if mt != ot:
                return f"unwind map state {i}: toState {mt} vs original {ot}"
            why = self.eh_target(obj, self._mptr(obj, s2, o2 + 8 * i + 4), self._optr(op + 8 * i + 4), ctx,
                                 f"unwind state {i} action")
            if why:
                return why
        t = table("try-block map", 0x10, ntry, 20)
        if isinstance(t, str):
            return t
        for i in range(ntry):
            s2, o2, op = t
            r = obj.sections[s2]["raw"]
            mine = struct.unpack_from("<4i", r, o2 + 20 * i)
            theirs = struct.unpack("<4i", o.read(op + 20 * i, 16))
            if mine != theirs:
                return f"try block {i} (tryLow, tryHigh, catchHigh, nCatches) {mine} vs original {theirs}"
            why = self.eh_handlers(obj, self._mptr(obj, s2, o2 + 20 * i + 16), self._optr(op + 20 * i + 16),
                                   mine[3], ctx, f"try block {i}")
            if why:
                return why
        if magic >= 0x19930521:  # ESTypeList {nCount, HandlerType *pTypeArray}
            mp, op = self._mptr(obj, sec, b + 0x1c), self._optr(orva + 0x1c)
            if (mp is None) != (op is None) or "bad" in (mp, op):
                return "FuncInfo ESTypeList: one side null"
            if mp is not None:
                loc2 = self._loc(obj, mp)
                if loc2 is None:
                    return "our ESTypeList is external"
                s2, o2 = loc2
                n = struct.unpack_from("<i", obj.sections[s2]["raw"], o2)[0]
                if n != struct.unpack("<i", o.read(op, 4))[0]:
                    return "ESTypeList count differs"
                why = self.eh_handlers(obj, self._mptr(obj, s2, o2 + 4), self._optr(op + 4), n, ctx, "ESTypeList")
                if why:
                    return why
        return None

    def eh_handlers(self, obj, mp, op, n, ctx, what):
        """n HandlerType entries {adjectives, TypeDescriptor *pType, dispCatchObj, addressOfHandler}."""
        if n == 0 and mp is None and op is None:
            return None
        if mp in (None, "bad") or op in (None, "bad") or self._loc(obj, mp) is None:
            return f"{what}: handler array ours {mp!r:.20}, original {op!r:.20}"
        s2, o2 = self._loc(obj, mp)
        r = obj.sections[s2]["raw"]
        for j in range(n):
            k, ok = o2 + 16 * j, op + 16 * j
            for name, d in (("adjectives", 0), ("dispCatchObj", 8)):
                a, c = struct.unpack_from("<i", r, k + d)[0], struct.unpack("<i", self.orig.read(ok + d, 4))[0]
                if a != c:
                    return f"{what} catch {j}: {name} {a:#x} vs original {c:#x}"
            why = self.eh_type(obj, self._mptr(obj, s2, k + 4), self._optr(ok + 4)) or \
                self.eh_target(obj, self._mptr(obj, s2, k + 12), self._optr(ok + 12), ctx, f"{what} catch {j} handler")
            if why:
                return f"{what} catch {j}: {why}" if not why.startswith(what) else why
        return None

    def eh_type(self, obj, mp, op):
        """The caught type: both null (catch (...)), or TypeDescriptors with the same decorated name (the string
        at +8, which the EH runtime's type match compares when the pointers differ)."""
        if mp is None and op is None:
            return None
        if mp in (None, "bad") or op in (None, "bad"):
            return "caught type: one side catch (...)"
        loc = self._loc(obj, mp)
        if loc is None:
            return f"our TypeDescriptor {obj.symbols[mp[0]][0]} is external"
        s2, o2 = loc
        r = obj.sections[s2]["raw"]
        end = r.find(b"\0", o2 + 8)
        name = r[o2 + 8:end] if end >= 0 else None
        theirs = self.orig.read(op + 8, len(name) + 1) if name is not None else b""
        if name is None or theirs != name + b"\0":
            return f"caught type {name!r} vs original {theirs!r}"
        return None

    def eh_target(self, obj, mp, op, ctx, what):
        """A funclet pointer in the tables: both null, or code compared by eh_code."""
        if mp is None and op is None:
            return None
        if mp in (None, "bad") or op in (None, "bad"):
            return f"{what}: one side null"
        if self._loc(obj, mp) is None:  # an external function (__std_terminate for noexcept): rule 2, as a call
            ok, why = (False, "with an addend") if mp[1] else self.resolve_call(obj.symbols[mp[0]][0], op, obj)
            return None if ok else f"{what}: {why}"
        return self.eh_code(obj, mp, op, ctx, what)

    def eh_code(self, obj, p, orva, ctx, what):
        """Our code at pointer p (a handler stub, an unwind funclet or a catch block: from the symbol to the next
        function symbol in its section, trailing int3/nop padding dropped) against the original's at orva: equal
        bytes except relocation fields, relocated fields at the same places, each resolving by rule 2 (calls and
        jumps to the same RVA, constant data, the /GS cookie), a FuncInfo compared by eh_funcinfo, and references
        into the parent function (a catch block's continuation address, unrelocated branches leaving the
        funclet) to the same instruction of the original's parent, through ctx["map"]. The equivalences don't
        apply here."""
        loc = self._loc(obj, p)
        if loc is None:
            return f"{what}: our code is external ({obj.symbols[p[0]][0]})"
        sec, off = loc
        s = obj.sections[sec]
        if not s["chars"] & IMAGE_SCN_CNT_CODE:
            return f"{what}: ours not in code"
        end = min((v for n, v, sc, c, t in obj.symbols.values() if sc == sec and v > off and (t & 0xF0) == 0x20),
                  default=len(s["raw"]))
        code = s["raw"][off:end]
        while code and code[-1] in (0xCC, 0x90):
            code = code[:-1]
        if not code:
            return f"{what}: empty"
        n = len(code)
        o = self.orig
        orig = o.read(orva, n)
        rel = {va - off: r for va, r in self._relmap(obj, sec).items() if off <= va < off + n}
        ofields = {k for k in range(n - 3) if orva + k in o.relocs}
        absolute = {k for k, (_, typ) in rel.items() if typ != REL32}  # the original's rel32 fields carry no relocation
        if absolute != ofields or ofields & set(rel) - absolute:
            return f"{what} at {orva:#x}: absolute fields at ours {sorted(absolute)} vs original {sorted(ofields)}"
        masked = {k + j for k in rel for j in range(4)}
        if any(code[k] != orig[k] for k in range(n) if k not in masked):
            return f"{what} at {orva:#x}: bytes differ"

        def parent(t, orv):  # offset t in the parent's section, the original's RVA orv: the same instruction?
            m = ctx["map"](t - ctx["off"])
            return m is not None and orv == ctx["start"] + m

        for k, (si, typ) in sorted(rel.items()):
            tname, tval, tsec = obj.symbols[si][:3]
            field = struct.unpack_from("<I", code, k)[0]
            if typ == REL32:
                op = orig[k - 1:k] if k else b""
                if not (op in (b"\xe8", b"\xe9") or k >= 2 and orig[k - 2] == 0x0F and 0x80 <= orig[k - 1] <= 0x8F):
                    return f"{what} +{k:#x}: rel32 where the original has no branch"
                tgt = orva + k + 4 + struct.unpack_from("<i", orig, k)[0]
                if tsec == ctx["sec"] and ctx["sec"] is not None:
                    if not parent(tval + field, tgt):
                        return f"{what} +{k:#x}: branch into the function differs"
                    continue
                ok, why = self.resolve_call(tname, tgt, obj)
                if not ok:
                    return f"{what} +{k:#x}: {why}"
            elif typ in (DIR32, DIR32NB):
                orv = o.dword(orva + k) - o.base
                if tname.startswith("__ehfuncinfo$"):
                    why = self.eh_funcinfo(obj, (si, field), orv, ctx)
                    if why:
                        return why
                elif tsec is not None and tsec == ctx["sec"]:
                    if not parent(tval + field, orv):
                        return f"{what} +{k:#x}: address in the function differs (ours +{tval + field - ctx['off']:#x})"
                elif tsec == sec:
                    if orv - orva != tval + field - off:
                        return f"{what} +{k:#x}: internal reference differs"
                else:
                    why = self.data_ref(obj, si, field, orv)
                    if why:
                        return f"{what} +{k:#x}: {why}"
            else:
                return f"{what} +{k:#x}: relocation type {typ}"
        # unrelocated branches leaving the funclet (a catch block's jump back into the function)
        for i in _csd.disasm(code, 0):
            if not is_branch(i) or not i.operands or i.operands[0].type != OP_IMM or i.address + i.imm_offset in rel:
                continue
            t = i.operands[0].imm & 0xFFFFFFFF
            if 0 <= t < n:
                continue
            if sec != ctx["sec"] or not parent(off + t, (orva + t) & 0xFFFFFFFF):
                return f"{what} +{i.address:#x}: branch leaves the funclet to a different place"
        return None

    def entry_reads(self, rva, depth=0):
        """Which of ecx/edx the original function at rva may read before writing them (its register arguments:
        MSVC passes them only to __thiscall/__fastcall functions). Decided from its code over every path;
        a call that may read them, an indirect call or anything undecodable counts as a read."""
        if rva in self._entry:
            return self._entry[rva]
        self._entry[rva] = {"ecx", "edx"}  # while it is being decided (recursion)
        f = self.funcs.get(hex(rva))
        if not f or depth > 4:
            return {"ecx", "edx"}
        c = Code(self.orig.read(rva, f["size"]), orig_refs(self.orig, rva, f["size"]), f["size"], rva)
        live = set()
        for r in ("ecx", "edx"):
            seen, work = set(), [0]
            while work and r not in live:
                a = work.pop()
                if a in seen:
                    continue
                seen.add(a)
                i = c.ins.get(a)
                if i is None:
                    live.add(r)
                    break
                ops = i.operands
                zeroing = i.mnemonic in ("xor", "sub") and len(ops) == 2 and ops[0].type == ops[1].type == OP_REG                     and ops[0].reg == ops[1].reg
                reads = {fam(i.reg_name(x)) for x in i.regs_read}
                for op in ops:
                    if op.type == OP_REG and op.access & AC_READ and not zeroing:
                        reads.add(fam(i.reg_name(op.reg)))
                    elif op.type == OP_MEM:
                        reads |= {fam(i.reg_name(x)) for x in (op.mem.base, op.mem.index) if x}
                if r in reads:
                    live.add(r)
                    break
                t = c.branch_target(i)
                tail = i.mnemonic == "jmp" and (t is None or t[0] != "label") and not table_jump(c, i)
                if i.mnemonic == "call" or tail:
                    if t is None or t[0] != "rva" or r in self.entry_reads(int(self.final(hex(t[1])), 16), depth + 1):
                        live.add(r)
                        break
                    continue  # the callee overwrites it without reading it (it is caller-saved)
                writes = {i.reg_name(x) for x in i.regs_write} |                          {i.reg_name(op.reg) for op in ops if op.type == OP_REG and op.access & AC_WRITE}
                if r in writes:
                    continue
                work.extend(c.succ[a])
        self._entry[rva] = live
        return live

    def check(self, rva, cands):
        """cands: [(Obj, cod path, decorated name)]: every definition of the hook's exact replacement (an inline
        function is emitted by each object that uses it, possibly with different flags, and the linker keeps any
        one). Matches only if every one does: returns the first that doesn't, else the first."""
        results = [self.compare(rva, obj, cod, sym) for obj, cod, sym in cands]
        return next((r for r in results if not r["match"]), results[0])

    def compare(self, rva, obj, cod, sym):
        o = self.orig
        f = self.funcs[rva]
        self._cod[str(obj.path)] = cod
        self._used.append(set())
        self._eh.append(set())
        try:
            return self._compare(rva, obj, cod, sym)
        finally:
            self._eh.pop()

    def _compare(self, rva, obj, cod, sym):
        o = self.orig
        f = self.funcs[rva]
        start, size = int(rva, 16), f["size"]
        orig = o.read(start, size)
        sec, off = obj.functions()[sym]
        s = obj.sections[sec]
        code = s["raw"][off:]
        catches = [v for n, v, sc, c, t in obj.symbols.values() if sc == sec and v > off and n.startswith("__catch$")]
        if s["chars"] & IMAGE_SCN_LNK_COMDAT and off == 0:
            body = code[:min(catches) - off] if catches else code  # x86 catch blocks follow the function's code
        else:  # without /Gy: up to the next function symbol in the section
            nxt = min((v for n, v, sc, c, t in obj.symbols.values() if sc == sec and v > off and (t & 0xF0) == 0x20),
                      default=len(s["raw"]))
            body = s["raw"][off:nxt]
        ours_len = len(body)
        trimmed = body
        while len(trimmed) > size and trimmed[-1] in (0xCC, 0x90):
            trimmed = trimmed[:-1]
        reasons, labels = [], {}
        mine_masked, orig_masked = set(), set(k for k in range(size) if any(start + k - j in o.relocs
                                                                           for j in range(4)))
        lines, src_lines = listing_lines(cod, sym)
        if any(FORBIDDEN.search(l) for l in src_lines):
            reasons.append("inline asm / __emit / naked in source")
        for va, symidx, typ in s["relocs"]:
            if not off <= va < off + len(trimmed):
                continue
            k = va - off
            mine_masked.update(range(k, k + 4))
            labels[k] = undecorate(obj.symbols[symidx][0])
            if k + 4 > size:
                continue
            tname, tval, tsec, _, _ = obj.symbols[symidx]
            field = struct.unpack_from("<I", body, k)[0]
            if typ == REL32:
                if start + k not in o.relocs and (orig[k - 1:k] in (b"\xe8", b"\xe9") or orig[k - 2:k - 1] == b"\x0f"):
                    tgt = start + k + 4 + struct.unpack_from("<i", orig, k)[0]
                    ok, why = self.resolve_call(tname, tgt, obj)
                    if not ok:
                        reasons.append(f"+{k:#x} call/jmp: {why}")
                else:
                    reasons.append(f"+{k:#x} rel32 to {undecorate(tname)} where the original has no relative branch")
            elif typ in (DIR32, DIR32NB):
                if start + k not in o.relocs:
                    reasons.append(f"+{k:#x} absolute ref to {undecorate(tname)} where the original has none")
                    continue
                orva = o.dword(start + k) - o.base
                if tsec == sec:  # jump table / label inside our own function
                    if orva - start != tval + field - off:
                        reasons.append(f"+{k:#x} internal ref: ours +{tval + field - off:#x}, original +{orva - start:#x}")
                    continue
                why = self.data_ref(obj, symidx, field, orva)
                if why:
                    reasons.append(f"+{k:#x} {why}")
        for k in sorted(orig_masked - mine_masked):
            if k < len(trimmed):
                reasons.append(f"+{k:#x} original relocated field where ours has none")
                break
        same_len = len(trimmed) == size
        masked = mine_masked | orig_masked
        bytes_equal = same_len and all(trimmed[k] == orig[k] for k in range(size) if k not in masked)
        if not same_len:
            reasons.insert(0, f"size {len(trimmed)} vs original {size}")
        elif not bytes_equal:
            reasons.insert(0, "bytes differ")
        a = instructions(bytes(trimmed), mine_masked, labels)

        def call_name(rel):
            t = hex((start + rel) & 0xFFFFFFFF)
            f = self.funcs.get(self.final(t)) or self.funcs.get(t)
            return f"{t} {f['name'][:40]}" if f else t
        b = instructions(orig, orig_masked, {k - start + 0: f"{o.dword(k) - o.base:#x}" for k in
                                             range(start, start + size) if k in o.relocs}, call_name)
        match = bytes_equal and not reasons
        equiv = []
        pmap = (lambda t: t if 0 <= t < size else None) if match else None
        if not match and self.use_equiv and not any(x.startswith("inline asm") for x in reasons):
            e = Equivalence(self, start, size, orig, obj, sec, off, body, sym)
            if e.ok:
                match, equiv, reasons = True, e.used, []
                pmap = e.offset_map
            elif e.applied:  # show and score the streams after the substitutions, with the reasons they leave
                reasons = e.reasons
                a, b = e.listing()
        if match and self._eh[-1]:  # the EH frame: handler stub, FuncInfo, funclets (deferred by data_ref)
            ctx = dict(sec=sec, off=off, start=start, map=pmap, key=(rva, sym))
            for symidx, field, orva in sorted(self._eh[-1]):
                why = self.eh_frame(obj, symidx, field, orva, ctx)
                if why:
                    match, reasons = False, [f"EH frame: {why}"]
                    break
            else:
                equiv = sorted(set(equiv) | {"eh-frame"})
        proved = self._used.pop()
        if match and proved:
            equiv = sorted(set(equiv) | proved)
        sm = difflib.SequenceMatcher(None, [x[2] for x in a], [x[2] for x in b], autojunk=False)
        return dict(rva=rva, sym=sym, match=match, score=1.0 if match else round(sm.ratio(), 3), equiv=equiv,
                    reasons=reasons, size=size, ours_len=ours_len, a=a, b=b, ops=sm.get_opcodes(), lines=lines,
                    obj=obj.path, frame=[] if match else stackframes.smaller_objects(bytes(trimmed), orig))


# ---------------------------------------------------------------- equivalences (instruction level, v2)

_csd = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
_csd.detail = True
OP_REG, OP_IMM, OP_MEM = X86.X86_OP_REG, X86.X86_OP_IMM, X86.X86_OP_MEM
AC_READ, AC_WRITE = capstone.CS_AC_READ, capstone.CS_AC_WRITE
GAMEBASE = "?GameBase@@YAIXZ"  # uintptr_t __cdecl GameBase(void), src/game/game.h
FAMILY = {r: f for f, rs in {"eax": "al ah ax", "ecx": "cl ch cx", "edx": "dl dh dx", "ebx": "bl bh bx",
                             "esi": "si", "edi": "di", "ebp": "bp", "esp": "sp"}.items() for r in rs.split()}
FULL = {"eax", "ecx", "edx", "ebx", "esi", "edi", "ebp", "esp"}
FULL_FLAG_WRITERS = {"add", "sub", "cmp", "test", "and", "or", "xor", "neg", "adc", "sbb"}  # set CF ZF SF OF PF
TERMINAL = {"ret", "retf", "jmp", "int3", "ud2", "hlt"}
SIZEWORD = {1: "byte", 2: "word", 4: "dword", 6: "fword", 8: "qword", 10: "xword", 16: "xmmword"}


def fam(name):
    return FAMILY.get(name, name)


def is_branch(i):
    return i.group(X86.X86_GRP_JUMP) or i.group(X86.X86_GRP_CALL)


def ret_live(signature):
    """{register: low bits} the caller may read when our function returns, from its undecorated signature.
    MSVC returns values narrower than 32 bits in al/ax and leaves the upper bits undefined."""
    m = re.match(r"(?:(?:public|private|protected): )?(?:(?:virtual|static) )*(.*?)\s*"
                 r"\b__(?:thiscall|cdecl|stdcall|fastcall|vectorcall)\b", signature)
    if not m:
        return {"eax": 32, "edx": 32}
    rt = m.group(1).strip()
    if rt == "void":
        return {}
    if "__int64" in rt or rt.startswith(("class ", "struct ", "union ")) and not rt.endswith(("*", "&")):
        return {"eax": 32, "edx": 32}  # 64-bit and small aggregates come back in edx:eax
    if rt in ("bool", "char", "signed char", "unsigned char"):
        return {"eax": 8}
    if rt in ("short", "unsigned short", "wchar_t"):
        return {"eax": 16}
    return {"eax": 32}


def own_args(signature):
    """Registers our function itself receives arguments in."""
    if "__thiscall" in signature:
        return {"ecx"}
    if "__cdecl" in signature or "__stdcall" in signature:
        return set()
    return {"ecx", "edx"}


def orig_refs(orig, start, size):
    """{field offset: ref} for the original's relocated fields in [start, start + size)."""
    out = {}
    for k in range(size - 3):
        if start + k in orig.relocs:
            v = orig.dword(start + k) - orig.base
            out[k] = ("label", v - start) if 0 <= v - start < size else ("rva", v)
    return out


class Code:
    """One function's reachable instructions (recursive descent from offset 0, following branches and jump
    tables) and the tables inside it. refs: {field offset: ref} for relocated fields, a ref being ("label",
    offset inside the function), ("rva", K) or ("sym", our symbol index, addend). ext: the function's RVA for
    the original, whose out-of-function rel32 branches carry no relocation (None for ours)."""

    def __init__(self, code, refs, limit, ext=None):
        self.code, self.refs, self.limit, self.ext = code, refs, limit, ext
        self.ins, self.succ, self.problems = {}, {}, []
        data, work = set(), [0]
        while work:
            a = work.pop()
            if a in self.ins or not 0 <= a < limit:
                continue
            i = next(_csd.disasm(code[a:a + 15], a, 1), None)
            if i is None or a + i.size > limit:
                self.problems.append(f"+{a:#x} undecodable, or runs past the end")
                self.ins[a] = None
                continue
            self.ins[a] = i
            succ = [] if i.mnemonic in TERMINAL else [a + i.size]
            t = self.branch_target(i)
            if t and t[0] == "label":
                succ.append(t[1])
            if i.disp_offset and self.refs.get(a + i.disp_offset, ("",))[0] == "label":
                tbl = self.refs[a + i.disp_offset][1]
                data.add(tbl)
                if i.mnemonic == "jmp":  # jump table: the consecutive internal references from its start
                    k = tbl
                    while self.refs.get(k, ("",))[0] == "label":
                        succ.append(self.refs[k][1])
                        k += 4
            self.succ[a] = succ
            work.extend(succ)
        self.order = sorted(a for a, i in self.ins.items() if i is not None)
        self.tables = sorted(t for t in data if t not in self.ins)

    def branch_target(self, i):
        """A direct call/jmp/jcc's target as a ref (None for other instructions and indirect branches)."""
        if not is_branch(i) or not i.operands or i.operands[0].type != OP_IMM:
            return None
        field = i.address + i.imm_offset
        if field in self.refs:
            return self.refs[field]
        t = i.operands[0].imm & 0xFFFFFFFF
        if t < self.limit:
            return ("label", t)
        return ("rva", (self.ext + t) & 0xFFFFFFFF) if self.ext is not None else ("bad", t)

    def table(self, t):
        """A table's contents: bytes, with ("ref", r) for each relocated field; trailing int3 padding dropped."""
        end = min([x for x in self.tables if x > t] + [x for x in self.order if x > t] + [self.limit])
        out, k = [], t
        while k < end:
            if k in self.refs:
                out.append(("ref", self.refs[k]))
                k += 4
            else:
                out.append(self.code[k])
                k += 1
        while out and out[-1] == 0xCC:
            out.pop()
        return out


def table_jump(c, i):
    return bool(i.disp_offset) and c.refs.get(i.address + i.disp_offset, ("",))[0] == "label"


def merge(old, new):
    if old is None:
        return dict(new)
    out = {}
    for k in old.keys() | new.keys():
        x, y = old.get(k), new.get(k)
        if k.startswith("w:"):  # written on some path since the entry
            out[k] = True
        elif x == y:
            out[k] = x
        elif isinstance(x, tuple) and isinstance(y, tuple) and x[0] == y[0] == "low":
            out[k] = ("low", min(x[1], y[1]))
        else:
            out[k] = "dirty"
    return out


def width(name):
    """Low bits of its family a register name covers (32 for anything else, e.g. ah, which needs bits 8-15)."""
    return 8 if name in ("al", "cl", "dl", "bl") else 16 if name in ("ax", "cx", "dx", "bx", "si", "di", "bp", "sp") \
        else 32


class Equivalence:
    """Compares our function with the original instruction by instruction, under the substitution rules in the
    module docstring ("Equivalences"). ok: equivalent; used: the patterns that made it so; reasons: why not."""

    def __init__(self, ck, start, size, orig, obj, sec, off, body, sym):
        self.ck, self.obj, self.start = ck, obj, start
        self.reasons, self.used, self.applied, self.ok = [], [], False, False
        mrefs = {}
        for va, symidx, typ in obj.sections[sec]["relocs"]:
            if not off <= va < off + len(body):
                continue
            k = va - off
            tname, tval, tsec = obj.symbols[symidx][:3]
            field = struct.unpack_from("<I", body, k)[0]
            sr = rva_symbol(tname)
            if typ not in (REL32, DIR32, DIR32NB):
                mrefs[k] = ("bad", typ)
            elif tsec == sec:
                mrefs[k] = ("label", tval + field - off)
            elif sr is not None and (typ != REL32 or field == 0):
                mrefs[k] = ("rva", sr + (field if typ != REL32 else 0))
            else:
                mrefs[k] = ("sym", symidx, field)
        self.mine = Code(body, mrefs, len(body))
        self.theirs = Code(orig, orig_refs(ck.orig, start, size), size, start)
        self.ret_live = {**ret_live(undecorate(sym, 0)), "ebx": 32, "esi": 32, "edi": 32, "ebp": 32}
        self.own_args = own_args(undecorate(sym, 0))
        self.reasons += [f"ours {p}" for p in self.mine.problems] + [f"original {p}" for p in self.theirs.problems]
        if self.reasons:
            return
        self.analyse()
        self.compare()
        self.ok = not self.reasons
        if self.ok:
            self.used = [n for n, x in (("gameaddress", self.deleted - set(self.vpairs) or self.mat),
                                        ("vtable-call", self.vpairs)) if x] or ["encoding"]

    # -- our side: which instructions are the GameAddress sequence, and what each register holds

    def analyse(self):
        """Data flow over our code to a fixed point; then any address value that escapes (is used other than as
        an address operand) is materialised at its definition, and the flow is run again."""
        c = self.mine
        self.materialise = {}  # offset of a defining instruction -> register it materialises
        self.vpairs = self.find_vcalls()
        self.vcall_of = {b: a for a, b in self.vpairs.items()}
        for _ in range(16):
            self.state = {0: {}}
            work = [0]
            while work:
                a = work.pop()
                out = self.step(a, self.state[a])[0]
                for b in c.succ[a]:
                    if c.ins.get(b) is None:
                        continue
                    new = merge(self.state.get(b), out)
                    if new != self.state.get(b):
                        self.state[b] = new
                        work.append(b)
            self.deleted, self.subs, self.problems, self.mat, escapes = set(), {}, [], {}, {}
            for a in c.order:
                _, kind, problems, subs, esc = self.step(a, self.state.get(a, {}))
                if kind in ("del", "vdel"):
                    self.deleted.add(a)
                elif kind:
                    self.mat[a] = kind
                self.subs[a] = subs
                self.problems += problems
                escapes.update(esc)
            new = {d: f for d, f in escapes.items() if d not in self.materialise}
            if not new:
                break
            self.materialise.update(new)
        self.applied = bool(self.deleted or self.mat)

    def step(self, a, s):
        """(state after, kind, problems, register families substituted by addresses, escapes {definition: family}).
        kind: None for an ordinary instruction, "del" for part of a GameAddress sequence, ("mat", register, K)
        for a definition materialised as `mov register, <K>`. A state maps a register family (or eflags) to
        ("addr", K, defining offset): holds image base + K, where the original holds nothing we compare; or
        to "dirty": differs from the original and must not be read."""
        i = self.mine.ins[a]
        written = {k: s.get(k) for k in ("w:ecx", "w:edx")}
        s, m, ops = dict(s), i.mnemonic, i.operands
        if m == "call" and (self.mine.branch_target(i) or ("",))[0] == "sym" \
                and self.obj.symbols[self.mine.branch_target(i)[1]][0] == GAMEBASE:
            s.update(eax=("addr", 0, a), ecx="dirty", edx="dirty", eflags="dirty")  # cdecl: clobbers eax ecx edx flags
            return self.define(a, s, "eax")
        addr = lambda f: isinstance(s.get(f), tuple) and s[f][0] == "addr"
        if len(ops) == 2 and ops[0].type == OP_REG and i.reg_name(ops[0].reg) in FULL:
            d = i.reg_name(ops[0].reg)
            if m in ("add", "sub") and ops[1].type == OP_IMM and addr(d) and (a + i.imm_offset) not in self.mine.refs:
                s[d] = ("addr", (s[d][1] + (ops[1].imm if m == "add" else -ops[1].imm)) & 0xFFFFFFFF, a)
                s["eflags"] = "dirty"
                return self.define(a, s, d)
            if m == "lea" and ops[1].mem.base and addr(i.reg_name(ops[1].mem.base)) and not ops[1].mem.index \
                    and not ops[1].mem.segment and (a + i.disp_offset) not in self.mine.refs:
                s[d] = ("addr", (s[i.reg_name(ops[1].mem.base)][1] + ops[1].mem.disp) & 0xFFFFFFFF, a)
                return self.define(a, s, d)
            if m == "mov" and ops[1].type == OP_REG and addr(i.reg_name(ops[1].reg)):
                s[d] = ("addr", s[i.reg_name(ops[1].reg)][1], a)
                return self.define(a, s, d)
        uses = defaultdict(list)  # family -> [(read could take an address operand instead?, low bits read)]
        zeroing = m in ("xor", "sub") and len(ops) == 2 and ops[0].type == ops[1].type == OP_REG \
            and ops[0].reg == ops[1].reg
        for op in ops:
            if op.type == OP_REG and not zeroing and op.access & AC_READ and a not in self.vcall_of:
                n = i.reg_name(op.reg)
                uses[fam(n)].append((n in FULL and op.access == AC_READ, width(n)))
            elif op.type == OP_MEM:
                if op.mem.base:
                    uses[fam(i.reg_name(op.mem.base))].append(
                        (not op.mem.segment and not (i.disp_offset and a + i.disp_offset in self.mine.refs), 32))
                if op.mem.index:
                    uses[fam(i.reg_name(op.mem.index))].append((False, 32))
        for r in i.regs_read:
            uses[fam(i.reg_name(r))].append((False, width(i.reg_name(r))))
        t = self.mine.branch_target(i)
        if m == "call" or m == "jmp" and (t is None or t[0] != "label") and not table_jump(self.mine, i):
            for f in self.call_args(i, s):  # a call, or a tail call
                # ABI: a register neither written since the entry nor one of our own arguments holds an undefined
                # value in the original too, so no callee can depend on it.
                if not (s.get(f) == "dirty" and not s.get("w:" + f) and f not in self.own_args):
                    uses[f].append((False, 32))
        if m in ("ret", "retf"):
            for f, bits in self.ret_live.items():
                uses[f].append((False, bits))
        problems, subs, escapes = [], set(), {}
        for f, kinds in uses.items():
            v = s.get(f)
            if v is None:
                continue
            if v == "dirty" or v[0] == "low" and max(b for _, b in kinds) > v[1]:
                problems.append(f"+{a:#x} reads {f}, which our extra instructions changed")
            elif v[0] == "addr":
                if all(k for k, _ in kinds):
                    subs.add(f)
                elif v[2] not in self.materialise:
                    escapes[v[2]] = f
                    problems.append(f"+{a:#x} uses {f} (image base + {v[1]:#x}) other than as an address")
                else:  # can't happen: a materialised definition leaves the register clean
                    problems.append(f"+{a:#x} uses {f} after materialising it")
        writes = [i.reg_name(r) for r in i.regs_write] + \
                 [i.reg_name(op.reg) for op in ops if op.type == OP_REG and op.access & AC_WRITE]
        for n in writes:
            f = fam(n)
            if f in ("ecx", "edx"):
                s["w:" + f] = True
            if n in FULL or n == "eflags" and m in FULL_FLAG_WRITERS:
                s.pop(f, None)
            elif f in s and width(n) < 32:  # the low bits now equal the original's
                v = s[f]
                s[f] = ("low", max(width(n), v[1] if v[0] == "low" else 0)) if isinstance(v, tuple) \
                    else ("low", width(n))
            elif f in s and isinstance(s[f], tuple) and s[f][0] == "addr":
                s[f] = "dirty"
        if m == "call":  # the same callee with the same inputs leaves the same eax ecx edx and flags
            for f in ("eax", "ecx", "edx", "eflags"):
                s.pop(f, None)
            s["w:ecx"] = s["w:edx"] = True
        if a in self.vpairs:  # the load of a pair `mov R, [M]` ... `call R`, compared as the original's `call [M]`
            r = i.reg_name(ops[0].reg)
            s[r] = "dirty"  # differs from the original until the call (which, if R is eax/ecx/edx, overwrites it)
            for k, v in written.items():  # the original doesn't write R here
                if v is None:
                    s.pop(k, None)
                else:
                    s[k] = v
            return s, "vdel", problems, subs, escapes
        return s, None, problems, subs, escapes

    def find_vcalls(self):
        """{offset of `mov R, dword ptr [M]`: offset of the `call R` / `jmp R` it feeds}, for pairs where the
        original's `call dword ptr [M]` must load the same pointer: straight-line code between them (no branch
        target, branch or call), M not esp-relative, and nothing in between reads or writes R, writes a register
        M uses, or writes memory other than by push (which writes below esp, where no live data is).
        That R holds the pointer and not the original's value is checked by the data flow."""
        c = self.mine
        targets = {b for a in c.order for b in c.succ[a] if b != a + c.ins[a].size}
        pairs = {}
        for n, a in enumerate(c.order):
            i = c.ins[a]
            ops = i.operands
            if i.mnemonic != "mov" or len(ops) != 2 or ops[0].type != OP_REG or ops[1].type != OP_MEM \
                    or ops[1].size != 4 or ops[1].mem.segment:
                continue
            r = i.reg_name(ops[0].reg)
            mregs = {fam(i.reg_name(x)) for x in (ops[1].mem.base, ops[1].mem.index) if x}
            if r not in FULL or r == "esp" or "esp" in mregs:
                continue
            end = a + i.size
            for b in c.order[n + 1:]:
                j = c.ins[b]
                if b != end or b in targets:
                    break
                end = b + j.size
                jo = j.operands
                if j.mnemonic in ("call", "jmp") and len(jo) == 1 and jo[0].type == OP_REG and j.reg_name(jo[0].reg) == r:
                    pairs[a] = b
                    break
                if is_branch(j) or j.mnemonic in TERMINAL or j.mnemonic.split()[0] in ("rep", "repne", "lock") \
                        or j.mnemonic.startswith(("ret", "stos", "movs", "cmps", "scas", "lods", "int", "enter", "leave")):
                    break
                reads, writes = j.regs_access()
                reads = {fam(j.reg_name(x)) for x in reads}
                writes = {fam(j.reg_name(x)) for x in writes}
                if r in reads | writes or mregs & writes \
                        or j.mnemonic != "push" and any(op.type == OP_MEM and op.access & AC_WRITE for op in jo):
                    break
        return pairs

    def define(self, a, s, f):
        """After an instruction of a GameAddress sequence that leaves image base + K in f: deleted, or, when
        the value escapes, kept as `mov f, <K>` (then f holds what the original's mov leaves in it)."""
        if self.materialise.get(a) == f:
            k = s.pop(f)[1]
            if f in ("ecx", "edx"):  # the original's mov writes it
                s["w:" + f] = True
            return s, ("mat", f, k), [], set(), {}
        return s, "del", [], set(), {}

    def call_args(self, i, s):
        """Which of ecx/edx the callee of a call (or tail jmp) may read as arguments."""
        t = self.mine.branch_target(i)
        if t is None and i.operands and i.operands[0].type == OP_REG:
            v = s.get(fam(i.reg_name(i.operands[0].reg)))
            if isinstance(v, tuple) and v[0] == "addr":  # OriginalFunction: the original function at image base + K
                t = ("rva", v[1])
        if t is None or t[0] in ("label", "bad"):
            return {"ecx", "edx"}  # indirect: unknown callee
        if t[0] == "rva":
            return self.ck.entry_reads(t[1])
        name = self.obj.symbols[t[1]][0]
        if name.startswith("?"):
            sig = undecorate(name, 0)
            if "__thiscall" in sig:
                return {"ecx"}
            if "__cdecl" in sig or "__stdcall" in sig:
                return set()
            return {"ecx", "edx"}
        return {"ecx", "edx"} if name.startswith("@") else set()  # C names: _f / _f@N; @f@N is __fastcall

    # -- the comparison

    def label(self, c, x, index):
        if x in index:
            return f"L{index[x]}"
        if x in c.tables:
            return f"T{c.tables.index(x)}"
        return f"L?{x:#x}"

    def render(self, c, a, index, subs):
        """(text, [(ref, branch?)]) of one instruction, with our address substitutions applied."""
        i = c.ins[a]
        refs, parts = [], []
        s = self.state.get(a, {}) if c is self.mine else {}
        if c is self.mine and a in self.mat:
            _, f, k = self.mat[a]
            return f"mov {f}, @", [(("rva", k), False)]
        if c is self.mine and a in self.vcall_of:  # `call R` loaded by `mov R, [M]`: as `call [M]`
            load, refs = self.render(c, self.vcall_of[a], index, self.subs.get(self.vcall_of[a], set()))
            return f"{i.mnemonic} {load.split(', ', 1)[1]}", refs

        def ref(r, branch):
            if r[0] == "label":
                return self.label(c, r[1], index)
            refs.append((r, branch))
            return "@"
        br = is_branch(i)
        for n, op in enumerate(i.operands):
            if op.type == OP_REG:
                name = i.reg_name(op.reg)
                if fam(name) in subs and op.access == AC_READ:
                    parts.append(ref(("rva", s[fam(name)][1]), br and n == 0))
                else:
                    parts.append(name)
            elif op.type == OP_IMM:
                t = c.branch_target(i) if n == 0 else None
                if t is not None:
                    parts.append(ref(t, True))
                elif i.imm_offset and a + i.imm_offset in c.refs:
                    parts.append(ref(c.refs[a + i.imm_offset], False))
                else:
                    parts.append(hex(op.imm & ((1 << 8 * op.size) - 1) if op.size else op.imm))
            else:
                mem, terms = op.mem, []
                base = i.reg_name(mem.base) if mem.base else None
                disp = c.refs.get(a + i.disp_offset) if i.disp_offset else None
                if base and fam(base) in subs:
                    disp = ("rva", (s[fam(base)][1] + mem.disp) & 0xFFFFFFFF)
                    base = None
                if base:
                    terms.append(base)
                if mem.index:
                    terms.append(i.reg_name(mem.index) + (f"*{mem.scale}" if mem.scale != 1 else ""))
                if disp:
                    terms.append(ref(disp, False))
                elif mem.disp or not terms:
                    terms.append(hex(mem.disp))
                seg = i.reg_name(mem.segment) + ":" if mem.segment else ""
                parts.append(f"{SIZEWORD.get(op.size, op.size)} ptr {seg}[{' + '.join(terms)}]")
        return f"{i.mnemonic} {', '.join(parts)}".strip(), refs

    def same_ref(self, ours, theirs, branch):
        if theirs[0] != "rva" or ours[0] in ("bad", "label"):
            return False
        t = theirs[1]
        if ours[0] == "rva":
            return ours[1] == t or branch and hex(ours[1]) == self.ck.final(hex(t))
        name = self.obj.symbols[ours[1]][0]
        if branch:
            return ours[2] == 0 and self.ck.resolve_call(name, t, self.obj)[0]
        return self.ck.data_ref(self.obj, ours[1], ours[2], t) is None

    def streams(self):
        """([(offset, text, refs)] ours without the GameAddress sequences, the same for the original."""
        out = []
        for c, skip, subs in ((self.mine, self.deleted, self.subs), (self.theirs, set(), {})):
            kept = [a for a in c.order if a not in skip]
            # a branch to a deleted instruction lands on the next kept one
            index = c.index = {a: bisect.bisect_left(kept, a) for a in c.ins}
            out.append([(a, *self.render(c, a, index, subs.get(a, set()))) for a in kept])
        return out

    def compare(self):
        ours, theirs = self.streams()
        self.reasons += self.problems
        for (a, ta, ra), (b, tb, rb) in zip(ours, theirs):
            if ta != tb:
                self.reasons.append(f"ours +{a:#x} `{ta}` vs original +{b:#x} `{tb}`")
                break
            for (x, br), (y, _) in zip(ra, rb):
                if not self.same_ref(x, y, br):
                    self.reasons.append(f"ours +{a:#x} `{ta}`: {self.describe(x)} vs original {self.describe(y)}")
        if len(ours) != len(theirs):
            self.reasons.append(f"{len(ours)} instructions (after substitutions) vs original {len(theirs)}")
        c = self.theirs  # every byte of the original's extent must be reached code, a table or padding
        covered = set()
        for a in c.order:
            covered.update(range(a, a + c.ins[a].size))
        for x in c.tables:
            covered.update(range(x, x + sum(4 if isinstance(v, tuple) else 1 for v in c.table(x))))
        stray = [k for k in range(c.limit) if k not in covered and c.code[k] not in (0xCC, 0x90)]
        if stray:
            self.reasons.append(f"original has {len(stray)} bytes not reached from its entry, from +{stray[0]:#x}"
                                " (its size in db/functions.jsonl may span more than one function)")
        if len(self.mine.tables) != len(self.theirs.tables):
            self.reasons.append(f"{len(self.mine.tables)} in-function tables vs original {len(self.theirs.tables)}")
        else:
            for t, u in zip(self.mine.tables, self.theirs.tables):
                ta = [x if isinstance(x, int) else self.label(self.mine, x[1][1], self.mine.index)
                      if x[1][0] == "label" else x for x in self.mine.table(t)]
                tb = [x if isinstance(x, int) else self.label(self.theirs, x[1][1], self.theirs.index)
                      if x[1][0] == "label" else x for x in self.theirs.table(u)]
                if ta != tb:
                    self.reasons.append(f"table at ours +{t:#x} differs from the original's at +{u:#x}")

    def offset_map(self, t):
        """The original's offset of the instruction compared with ours at offset t (one we kept), else None: how a
        catch block's continuation address or branch into the function is checked (Checker.eh_code)."""
        if self.mine.ins.get(t) is None or t in self.deleted:
            return None
        k = self.mine.index[t]
        return self.theirs.order[k] if k < len(self.theirs.order) else None

    def describe(self, r):
        if r[0] == "rva":
            return f"{r[1]:#x}"
        if r[0] == "sym":
            return undecorate(self.obj.symbols[r[1]][0]) + (f"+{r[2]:#x}" if r[2] else "")
        return str(r)

    def listing(self):
        """Both streams in instructions()'s format, for side_by_side and the similarity score."""
        out = []
        for c, stream in zip((self.mine, self.theirs), self.streams()):
            rows = []
            for a, text, refs in stream:
                shown = text
                for r, _ in refs:
                    shown = shown.replace("@", "<" + self.describe(r) + ">", 1)
                rows.append((a, c.ins[a].size, text, shown))
            out.append(rows)
        return out


def side_by_side(r, context=2, limit=40, first_only=False):
    a, b, lines = r["a"], r["b"], r["lines"]
    out = []
    ops = [op for op in r["ops"] if op[0] != "equal"]
    if first_only:
        ops = ops[:1]
    shown = 0
    for tag, i1, i2, j1, j2 in ops:
        i0, j0 = max(0, i1 - context), max(0, j1 - context)
        out.append(f"    @@ ours +{a[i1][0] if i1 < len(a) else 0:#x} / original +{b[j1][0] if j1 < len(b) else 0:#x} ({tag})")
        n = max(i2 - i0, j2 - j0)
        for d in range(n):
            ia, jb = i0 + d, j0 + d
            left = f"{a[ia][0]:4x}  {a[ia][3]}" if ia < i2 else ""
            right = f"{b[jb][0]:4x}  {b[jb][3]}" if jb < j2 else ""
            mark = " " if (ia < i1 and jb < j1) else "|"
            src = ""
            if ia < i2 and not first_only:
                ln = lines.get(a[ia][0])
                src = f"  ; {ln[0]}: {ln[1].strip()[:60]}" if ln else ""
            out.append(f"    {left[:44]:44} {mark} {right[:44]:44}{src}")
            shown += 1
            if shown >= limit:
                out.append("    ...")
                return out
    return out


# ---------------------------------------------------------------- driver

def src_files(entry):
    s = entry.get("src") or ""
    return [p.strip() for p in re.split(r"[,;]", s) if p.strip()]


def build_sources():
    """Every src/ translation unit (.cpp) the build compiled (its compile log), repo-relative: the units whose vtables,
    adapters and slot functions every run reads, so a verdict doesn't depend on which RVAs were selected."""
    return {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*.cpp")
            if BUILD.unit(p.relative_to(ROOT).as_posix()) is not None}


def table_sources(rva, tables):
    """For an RVA without a status line: the src/ files named like the hook table(s) whose entries mention it
    (src/hook/tables/cTSFoo.cpp -> src/**/cTSFoo.cpp, cTSFoo.size.cpp, cTSFoo*.cpp)."""
    pat = re.compile(r"\b0x0*" + rva[2:].lstrip("0") + r"\b", re.I)
    out = []
    for t in tables:
        if pat.search((ROOT / t).read_text(encoding="utf-8", errors="replace")):
            stem = Path(t).stem
            out += sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob(stem + "*.cpp")
                          if "hook/tables" not in p.as_posix())
    return list(dict.fromkeys(out))


def brief_verdict(r, prov):
    """--brief's verdict column: MATCH (verifiable), MATCH* (matched, not verifiable: prov) or the score."""
    return ("MATCH" if prov is None else "MATCH*") if r["match"] else format(r["score"], ".3f")


def brief_v6(r6, prov):
    """v6's verdict for --brief when it differs from v7's: the verdict, and its first reason when it doesn't match."""
    v = brief_verdict(r6, prov)
    return v if r6["match"] else f"{v} {(r6['reasons'] or ['?'])[0][:70]}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--class", dest="groups", nargs="+", action="extend", default=[],
                    help="status groups (db/status/<GROUP>.jsonl): `--class A B`, `--class A,B` or --class repeated")
    ap.add_argument("--rva", nargs="+", action="extend", default=[], help="one or more RVAs (repeatable)")
    ap.add_argument("--all-verified", action="store_true", help="every verified, hooked function")
    ap.add_argument("--drift", action="store_true",
                    help="periodic check: every entry recorded \"verify\": \"match\" (or those of --class / --rva), "
                         "printing only the ones that no longer give a verifiable MATCH; exit 1 if any")
    ap.add_argument("--status", default="verified,reimplemented,trace-verified",
                    help="statuses included with --class (default: ours, not replaced)")
    ap.add_argument("--show", action="store_true", help="differing instructions side by side, with our source lines")
    ap.add_argument("--summary", action="store_true", help="per-group totals and mismatch reasons only")
    ap.add_argument("--brief", action="store_true", help="one line per function: rva, score, size, name, first reason, "
                                                        "object and source times; `[v6: ...]` at the end when v6's rules "
                                                        "(no vtable references or ties) give another verdict")
    ap.add_argument("--extra", nargs="+", default=[], help="flags appended to the shipped ones (experiments, e.g. /Oy /GS-)")
    ap.add_argument("--no-equiv", action="store_true", help="exact matches only (v1 behaviour): no equivalence patterns")
    ap.add_argument("--no-callee-proof", action="store_true",
                    help="tie library callees by name only (v2): no recursive proof of our callee against the original's")
    ap.add_argument("--json", help="write per-function results (no game bytes) to this file")
    ap.add_argument("--rules7", action="store_true",
                    help="v7's class lookup (exact RTTI names only, unqualified faces), to compare verdicts with v8")
    ap.add_argument("--main-build", action="store_true",
                    help="in a worktree without a build of its own, read the main checkout's build log anyway "
                         "(its objects are the main checkout's: worktree changes show up as provenance, never verified)")
    args = ap.parse_args()

    # Which build's compile log (flags, objects, provenance) this run reads (lane FS: a fresh worktree silently read
    # the main checkout's).
    if BUILD.root is None:
        print(f"matchcheck: no build found ({ROOT / BuildLog.TARGETS['ts2core']} missing): build first "
              "(tools/difftest.ps1 -BuildOnly); flags fall back to the built-in defaults, nothing verifies", file=sys.stderr)
    elif BUILD.root.resolve() != ROOT.resolve():
        if not args.main_build:
            sys.exit(f"matchcheck: this checkout ({ROOT}) has no build of its own; the only build is {BUILD.root / 'build'}"
                     f" (another checkout's). Build here first (tools/difftest.ps1 -BuildOnly), or pass --main-build to "
                     "read that build anyway.")
        print(f"matchcheck: WARNING: reading another checkout's build, {BUILD.root / 'build'} (--main-build)",
              file=sys.stderr)
    else:
        print(f"matchcheck: build {BUILD.root / 'build'}", file=sys.stderr)

    Checker.use_v8 = not args.rules7
    ck = Checker()
    ck.use_equiv = not args.no_equiv
    ck.use_callee_proof = not args.no_callee_proof
    wanted = []  # rvas
    statuses = set(args.status.split(","))
    for rva in args.rva:
        wanted.append(hex(int(rva, 16)))
    groups = [g for a in args.groups for g in a.split(",") if g]
    unknown = [g for g in groups if not (tsdb.ROOT / "db/status" / f"{g}.jsonl").exists()]
    if unknown:
        ap.error(f"no status file db/status/<GROUP>.jsonl for: {', '.join(unknown)}")
    for g in groups:
        wanted += [r for r, e in ck.status.items() if e["_file"] == g + ".jsonl" and e["status"] in statuses]
    if args.all_verified:
        wanted += [r for r, e in ck.status.items() if e["status"] == "verified"]
    if args.drift:
        # Byte-match drift (0x144913: a later lane rewrote a match-verified function): the recorded matches only.
        recorded = [r for r, e in ck.status.items() if e["status"] == "verified" and e.get("verify") == "match"]
        wanted = [r for r in dict.fromkeys(wanted) if r in recorded] if (groups or args.rva) else recorded
        args.brief = args.show = False
    drifted = []  # (rva, line) for --drift
    if not wanted:
        ap.error("nothing selected")
    # The hook tables, compiled: each RVA's replacement is the exact symbol its kHooks entry points at.
    tables = [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / "src/hook/tables").glob("*.cpp"))]
    srcs = {rva: [p for p in src_files(ck.status.get(rva, {})) if p.endswith(".cpp") and (ROOT / p).exists()]
            for rva in dict.fromkeys(wanted)}
    for rva, found in srcs.items():
        if not found:  # no status line yet: the sources named like the hook table that lists the RVA
            found += table_sources(rva, tables)
    compared = set(tables) | {p for v in srcs.values() for p in v}
    # Vtables, vtable ties, adapters and slot functions come from every translation unit of the build, whatever was
    # selected: --rva and --drift must see the same build (0x49e110: cTSFamily's vtable is emitted by cTSFamily.cpp, which
    # --rva didn't read, so its tie was missing there and found by --drift). Only the compared units take --extra and
    # listings; the rest are the build's objects (compiled here only when stale).
    units = compile_units(compared, args.extra, listing=args.show)
    units.update(compile_units(build_sources() - compared))
    sources = set(units)
    loaded = {}

    def load(tu):
        if tu not in loaded:
            loaded[tu] = Obj(units[tu].obj)
        return loaded[tu]

    targets, ambiguous, where = resolve_hooks({tu: load(tu) for tu in tables})
    ck.set_hooks(targets)
    # v7 vtable ties: the member our vtable runs in a slot whose hook is a forwarding adapter (find_ties)
    texts = {}
    for p in sorted(sources):
        if not p.startswith("src/hook/tables/"):  # each source and its header (X.cpp, X.size.cpp -> X.h)
            for f in (ROOT / p, (ROOT / p).with_name((ROOT / p).name.split(".")[0] + ".h")):
                if f.exists():
                    texts[f.relative_to(ROOT).as_posix()] = f.read_text(encoding="utf-8", errors="replace")
    # v8: every class our build defines, so a namespace-stripped name taken by two of our classes is refused
    ours = set()
    for o in {u["obj"] for u in BUILD.units.values() if u["obj"] is not None} | {units[tu].obj for tu in sources}:
        if o is not None and Path(o).exists():
            ours |= obj_rtti_classes(o)
    ck.note_our_classes(ours)
    ties = ck.find_ties([load(tu) for tu in sorted(sources)], targets, texts)
    ck6 = ck.as_v6(targets) if args.brief else None  # --brief prints v6's verdict where it differs
    plan = {}
    for rva in srcs:
        if rva not in where or rva not in ck.funcs:
            plan[rva] = None
            continue
        tus = list(dict.fromkeys(srcs[rva] + where[rva]))
        plan[rva] = (tus, targets.get(rva), ambiguous.get(rva), ck.status.get(rva, {}).get("_file", "?.jsonl")[:-6])

    profiles = flagprofile.profiles()
    results, totals = [], defaultdict(lambda: [0, 0, 0, 0, 0, 0])  # group -> n, bytes, matched n, bytes, verifiable
    reasons, sole = defaultdict(int), defaultdict(int)
    frames = []  # (rva, name, warnings): tools/stackframes.py
    equivs = defaultdict(lambda: [0, 0])  # pattern -> functions, bytes matched through it
    for rva, p in plan.items():
        if p is None:
            print(f"{rva}  SKIP (not hooked or not a known function)")
            drifted.append((rva, "SKIP (not hooked or not a known function)"))
            continue
        tus, target, amb, group = p
        cands = [] if amb else [(load(tu), units[tu].cod, target) for tu in tus if target in load(tu).functions()]
        size = ck.funcs[rva]["size"]
        t = totals[group]
        t[0] += 1
        t[1] += size
        if amb:
            print(f"{rva}  AMBIGUOUS {amb}")
            drifted.append((rva, f"AMBIGUOUS {amb}"))
            reasons["hook target ambiguous"] += 1
            sole["hook target ambiguous"] += 1
            results.append(dict(rva=rva, group=group, size=size, match=False, verified=False, reasons=[amb]))
            continue
        if not cands:
            print(f"{rva}  NOSYM {undecorate(target)} (not in {', '.join(tus)}: the status entry's src and the hook"
                  f" table that hooks it; if the body moved, update src in db/status/{group}.jsonl)")
            drifted.append((rva, f"NOSYM {undecorate(target)}"))
            reasons["symbol not found"] += 1
            sole["symbol not found"] += 1
            results.append(dict(rva=rva, group=group, size=size, match=False, verified=False,
                                reasons=[f"{target} not defined in {', '.join(tus)}"]))
            continue
        # Provenance: the objects the replacement and its hook entry came from are the build's current ones.
        used = {tu for tu in tus if tu in where[rva] or any(c[0] is load(tu) for c in cands)}
        r = ck.check(rva, cands)
        if not r["match"] and rva in ties:
            # The hook's adapter doesn't match: compare the function our vtable runs in that slot (every tied one).
            tcands = {f: [(load(tu), units[tu].cod, f) for tu in sorted(sources) if f in load(tu).functions()]
                      for f in ties[rva]}
            rs = [ck.check(rva, c) for c in tcands.values() if c]
            bad = next((x for x in rs if not x["match"]), None)
            if rs and len(rs) == len(tcands) and bad is None:
                r = dict(rs[0], equiv=list(rs[0]["equiv"]) + ["vtable-tie"])
                used |= {tu for c in tcands.values() for o, _, _ in c for tu in sorted(sources) if load(tu) is o}
            elif bad is not None and bad["score"] > r["score"]:
                r = dict(bad, reasons=[f"vtable tie {undecorate(bad['sym'])} (adapter {r['score']:.3f}): "
                                       + (bad["reasons"] or ["?"])[0]] + bad["reasons"][1:])
        prov = next((f"{units[tu].why} ({tu})" for tu in sorted(used) if not units[tu].verifiable), None)
        if not r["match"] and rva not in ties:
            # Say why an adapter-looking hook made no vtable tie (FO: a receiver spelled through an alias was silent).
            fwd = ck.forwards.get(rva)
            if isinstance(fwd, str) and not fwd.startswith(("not a member", "no forwarding")):
                r["reasons"].append(f"no vtable tie: {fwd}")
            elif isinstance(fwd, dict):
                r["reasons"].append(f"no vtable tie: the adapter forwards to {fwd['cls']}::{fwd['member']} (receiver "
                                    f"{fwd['receiver']}), and no slot of our vtables for that face holds it")
        if not r["match"]:
            # A hint, not a reason of its own: the original was built with another profile than our source file.
            want = profiles.get(int(rva, 16), "speed")
            # the files that define the compared function (not every file named like the class)
            defs = [tu for tu in sorted(sources) if not tu.startswith("src/hook/tables/")
                    and r.get("sym") in load(tu).functions()] or tus
            have = "size" if any(tu.endswith(".size.cpp") for tu in defs) else "speed"
            if want != have:
                r["reasons"].insert(0, f"profile: the original is {want}-built, our source is {have} "
                                       f"({'move it to a .size.cpp file' if want == 'size' else 'move it out of the .size.cpp file'})")
        verified = r["match"] and prov is None and not args.extra  # a tie may run through a unit without --extra
        results.append(dict(rva=rva, group=group, size=size, match=r["match"], verified=verified, score=r["score"],
                            equiv=r["equiv"], name=undecorate(r["sym"]), sym=r["sym"], obj=Path(r["obj"]).name,
                            reasons=r["reasons"][:5], **({"frame": r["frame"]} if r.get("frame") else {}),
                            **({"provenance": prov} if prov else {}), **({"experiment": args.extra} if args.extra else {})))
        if r["match"]:
            t[2] += 1
            t[3] += size
            if verified:
                t[4] += 1
                t[5] += size
            for e in r["equiv"]:
                equivs[e][0] += 1
                equivs[e][1] += size
        else:
            if r.get("frame"):
                frames.append((rva, undecorate(r["sym"]), r["frame"]))
            cl = classify(r)
            for c in cl:
                reasons[c] += 1
            if len(cl) == 1:
                sole[next(iter(cl))] += 1
        if args.drift:
            if not verified:
                why = f"MATCH* not verifiable: {prov}" if r["match"] else \
                    f"{r['score']:.3f} {(r['reasons'] or ['?'])[0][:110]}"
                drifted.append((rva, why))
                print(f"{rva}  DRIFT {size:5} B  {undecorate(r['sym'])[:60]}  {why}  ({ck.status[rva]['_file']})")
            continue
        if args.summary:
            continue
        if args.brief:
            src = next((tu for tu in tus if not tu.startswith("src/hook/tables/") and any(c[0] is load(tu) for c in cands)), tus[0])
            stamp = lambda p: time.strftime("%H:%M:%S", time.localtime(p.stat().st_mtime)) if p.exists() else "-"  # noqa: E731
            first = "" if r["match"] else (r["reasons"] or ["?"])[0][:90]
            obj = Path(units[src].obj) if units[src].obj else None
            verdict = brief_verdict(r, prov)
            r6 = ck6.check(rva, cands)
            v6 = brief_verdict(r6, prov)
            print(f"{rva}  {verdict:6} {size:5} B  "
                  f"{undecorate(r['sym'])[:60]:60}  {first}  (obj {stamp(obj) if obj else '-'}, src {stamp(ROOT / src)}"
                  f"{', ' + prov if prov else ''})" + (f"  [v6: {brief_v6(r6, prov)}]" if v6 != verdict else ""))
            continue
        if r["match"]:
            how = f"  (equivalent: {', '.join(r['equiv'])})" if r["equiv"] else ""
            tag = "MATCH " if verified else "MATCH*"
            print(f"{rva}  {tag} {size:5} B  {undecorate(r['sym'])}{how}" + (f"  [not verifiable: {prov}]" if prov else ""))
        else:
            print(f"{rva}  {r['score']:.3f}  {size:5} B  {undecorate(r['sym'])}  [{'; '.join(r['reasons'][:3])}]")
            for w in r.get("frame", []):
                print(f"    STACK OBJECT: {w}: read both sides (tools/stackframes.py)")
            for line in side_by_side(r, first_only=not args.show, limit=60 if args.show else 6):
                print(line)
    if args.extra:
        print(f"\nEXPERIMENT (--extra {' '.join(args.extra)}): flags other than the build's; nothing here verifies.")
    print(f"\nmatchcheck v{VERSION}{' --rules7 (v7 class lookup)' if args.rules7 else ''}, each file with the build's own command line (build log: "
          f"{BUILD.root / 'build' if BUILD.root else 'none'})" + (f" + {' '.join(args.extra)}" if args.extra else ""))
    allt = [0] * 6
    for g, t in sorted(totals.items()):
        allt = [x + y for x, y in zip(allt, t)]
        if len(totals) > 1 or args.summary:
            print(f"  {g:32} {t[2]:4}/{t[0]:<4} functions  {t[3]:7}/{t[1]:<7} bytes")
    print(f"  total: {allt[2]}/{allt[0]} functions match, {allt[3]}/{allt[1]} bytes")
    print(f"  of which verifiable (current build objects{', never with --extra' if args.extra else ''}): "
          f"{allt[4]} functions, {allt[5]} bytes" + ("  (MATCH* lines: matched, not verifiable)" if allt[4] < allt[2] else ""))
    for e, (n, b) in sorted(equivs.items()):
        print(f"    of which through equivalence {e}: {n} functions, {b} bytes")
    if reasons:
        print("  mismatch reasons (functions showing it / functions where it is the only one):")
        for k, v in sorted(reasons.items(), key=lambda kv: -kv[1]):
            print(f"    {v:5} {sole[k]:5}  {k}")
    if ck.tie_refused:
        print(f"  classes not tied to the original's by name without namespace (v8; no vtable reference or tie): "
              f"{len(ck.tie_refused)}")
        for m, why in sorted(ck.tie_refused.items()):
            print(f"    {m}: {why}")
    if frames:
        print(f"  stack objects passed to calls smaller in ours than in the original (tools/stackframes.py; read both "
              f"sides, a callee may write past ours): {len(frames)} functions")
        for rva, name, ws in frames[:40]:
            print(f"    {rva} {name[:60]}: {ws[0]}" + (f" (+{len(ws) - 1} more)" if len(ws) > 1 else ""))
    if ck.proven:  # proven callee pairs (names and RVAs only), re-proven on every run: a record, not an input
        print(f"  callee pairs proven: {len(ck.proven)} ({len({k[2] for k in ck.proven})} original RVAs), "
              f"listed in build/matchcheck/callee-proofs.jsonl")
        (OUT / "callee-proofs.jsonl").write_text("".join(
            json.dumps(dict(rva=k[2], ours=v[0], obj=Path(k[0]).name, equiv=v[1])) + "\n"
            for k, v in sorted(ck.proven.items(), key=lambda kv: (int(kv[0][2], 16), kv[1][0]))))
    if args.json:
        Path(args.json).write_text("\n".join(json.dumps(r) for r in results) + "\n")
    if args.drift:
        # A recorded match that no longer matches is no longer verified (CLAUDE.md): drop the entry to reimplemented,
        # write a difftest, or bring the source back to matching.
        print(f"drift: {len(drifted)} of {len(plan)} functions recorded \"verify\": \"match\" give no verifiable MATCH"
              + (f": {' '.join(r for r, _ in drifted)}" if drifted else ""))
        sys.exit(1 if drifted else 0)


def classify(r):
    """Coarse mismatch classes present in a result (a function can have several)."""
    out = set()
    rs = " ".join(r["reasons"])
    if rs.startswith("profile:"):
        out.add("build profile differs from the original's (speed / size)")
    if "rel32 to GameBase" in rs:
        out.add("GameAddress / OriginalFunction (runtime base call)")
    if "our extra instructions changed" in rs:
        out.add("equivalence: reads a register a GameBase call or a moved vtable load changed")
    if "other than as an address" in rs:
        out.add("GameAddress: base-relative value used other than as an address")
    if "EH frame:" in rs:
        out.add("EH frame differs (handler stub, FuncInfo or funclets)")
    if "call/jmp:" in rs or re.search(r"`(call|jmp) @`: ", rs):
        out.add("call target not the same RVA (callee not hooked / not resolvable)")
    if "absolute ref to our" in rs:
        out.add("absolute reference to our own data")
    if "constant" in rs:
        out.add("constant data differs (or its extent / relocations can't be established)")
    if "our vtable" in rs:
        out.add("vtable reference differs from the original's (class, face, slot count or a slot)")
    if "reference to our data" in rs:
        out.add("reference to named data (address identity: needs a ts2_rva symbol)")
    for tag, i1, i2, j1, j2 in r["ops"]:
        if tag == "equal":
            continue
        ours = " ".join(x[2] for x in r["a"][i1:i2])
        orig = " ".join(x[2] for x in r["b"][j1:j2])
        if re.search(r"(call|jmp) e[a-ds][xip]\b", ours) and re.search(r"(call|jmp) dword ptr \[e..", orig):
            out.add("vtable call through a register (ours) vs call [reg+off] (original)")
        elif re.search(r"\bebp\b", orig) != re.search(r"\bebp\b", ours) or "fs:[0]" in ours + orig:
            out.add("frame / EH prologue differs")
        else:
            out.add("instruction differences (codegen)")
    return out or {"instruction differences (codegen)"}


if __name__ == "__main__":
    main()
