"""Stack-cleanup checker: do our calls into original code, and our replacements, pop what the original pops?

    .venv/Scripts/python -I tools/stackcheck.py [--build build] [--all] [--json out.json]
    .venv/Scripts/python -I tools/stackcheck.py --symbolize <ts2host.log or difftest output> --map <x.map>

A callee pops its stack arguments with `ret N` (stdcall, thiscall) or leaves them to the caller (cdecl,
`ret`). When a caller and a callee disagree, esp is off by the difference after the call. Code built with
frame pointers (/Oy-) addresses its locals through ebp and restores esp from ebp on return, so such a
mismatch usually goes unnoticed; built without them (/Oy, like the original and our Release build) it
corrupts every later esp-relative access. This tool compares, without running the game:

 1. hook: each hooked replacement's `ret N` (decoded from the built difftest.exe, whose `--list-hooks`
    gives the replacement addresses) against the original's `ret N` at the hooked RVA;
 2. vcall: the calls through vtable slots (`call [r+off]`) in each hooked replacement against those in the
    original function. Both are binary-level: for code without a frame pointer, esp is exact, so every path
    reaches `ret` at the entry depth and paths that meet agree; each indirect call pops an unknown amount
    and the paths give linear equations in them, solved with 0 <= pop <= depth above the prologue (ties in
    our code broken by "pops no more than was pushed since the previous call"). A slot is a MISMATCH when
    the original's equations have no solution with its calls through that offset popping what ours pop.
    Offsets are matched, not objects, so a slot called on two different objects (one inlined on one side
    only) can be a false positive: read both sides before changing anything;
 3. OriginalFunction<Fn, 0xRVA>(): the bytes Fn says the callee pops against the callee's `ret N`;
 4. VirtualCall<Fn, kSlot> / VirtualSlot<Fn>(obj, kSlot): the bytes Fn says the callee pops against the
    `ret N` of slot kSlot's implementations in every vtable of db/classes.jsonl that has the slot. The
    object's class isn't known here, so a site is flagged only when no vtable's slot pops what Fn says,
    and listed as "some" with --all when only some do (weak: most slots exist with every pop count);
 5. method: db/methods facts (the generated ifaces): each method's parameters against its class's slot.

A `ret N` is found by decoding from the entry, following conditional and direct jumps (a tail jump reaches
a function that pops the same) and jump tables, and stopping at calls to functions that never return.
A function that never reaches one is "unknown". The game's bytes are read at run time only and never
written anywhere. The run-time counterpart is the TS2_ESPCHECK build (CMakeLists.txt): --symbolize turns
its "espcheck:" lines into function names with the linker map.
"""
import argparse
import json
import re
import struct
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

import capstone
import pefile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GAME_BASE = int(re.search(r"kGameBase\s*=\s*(0x[0-9a-fA-F]+)", (ROOT / "src/game/game.h").read_text()).group(1), 16)
GAME_SIZE = 0x2000000  # the game image's span from kGameBase


# ---------------------------------------------------------------- ret N by decoding

# Library functions that never return but whose decoding can't show it (longjmp's indirect jump, the CRT's exits):
# a call to one ends the block, so a body ending `call longjmp` doesn't run on into the next function's `ret`.
NORETURN_NAMES = {"longjmp", "__longjmp_internal", "_CxxThrowException", "abort", "exit", "terminate",
                  "_invalid_parameter_noinfo_noreturn"}


def known_noreturn():
    return {int(n["rva"], 16) for n in tsdb.jsonl("names.jsonl") if n["name"] in NORETURN_NAMES}


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(str(path), fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.cache = {}
        self.complete = {}
        self.tables = {}
        self.esp_cache = {}
        self.systems = {}
        self.call_pops_cache = {}
        self.text = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in self.pe.sections
                     if s.Characteristics & 0x20000000]
        self.game = None  # the original image, for our code: a call or jump to kGameBase + rva goes there
        # names.jsonl RVAs are the original image's; our build has none of them
        self.known_noreturn = known_noreturn() if Path(path).resolve() == Path(tsdb.unpacked_exe()).resolve() else set()

    def callee(self, address):
        """(image, rva) a direct call or jump to `address` reaches. In our build a target in the game image's span
        (OriginalFunction<Fn, 0xRVA>(), src/game/original.h) is the original function at address - kGameBase, whose
        `ret N` the original image knows; otherwise a function of this image."""
        if self.game is not None and GAME_BASE <= address < GAME_BASE + GAME_SIZE:
            return self.game, address - GAME_BASE
        return self, address - self.base

    def is_code(self, rva):
        return any(a <= rva < b for a, b in self.text)

    def noreturn(self, rva, depth):
        """True when the function at rva provably never returns: its whole decoding (no indirect jumps,
        within budget) reaches no `ret`. A call to it ends a block (what follows may be a jump table)."""
        if rva in self.known_noreturn:
            return True
        if rva not in self.cache:
            if depth > 6 or not self.is_code(rva):
                return False
            self.rets(rva, depth + 1)
        return not self.cache[rva] and self.complete.get(rva, False)

    def rets(self, rva, depth=0):
        """Set of `ret` immediates reachable from rva (empty: none found)."""
        if rva in self.cache:
            return self.cache[rva]
        self.cache[rva] = set()  # in progress: a recursive call sees "none found", never "noreturn"
        self.complete[rva] = False
        tables = set()
        rets, complete = self._decode(rva, tables, depth)
        if tables:  # a block decoded before its jump table was seen may have run into it: decode again
            rets, complete = self._decode(rva, tables, depth)
            self.tables[rva] = tables
        self.cache[rva] = rets
        self.complete[rva] = complete
        return rets

    def instructions(self, rva):
        """{rva: instruction} of the function at rva: decoded as rets() does, also following the jump
        tables (entries close to the entry)."""
        self.rets(rva)
        tables = set(self.tables.get(rva, ()))
        out = {}
        self._decode(rva, tables, 0, out=out)
        return out

    def moves_esp(self, rva):
        """True for helpers that leave esp somewhere else (__EH_prolog3, __EH_epilog3, _chkstk): code that
        writes esp other than by push/pop/add/sub imm without being a standard `push ebp; mov ebp, esp` frame."""
        if rva in self.esp_cache:
            return self.esp_cache[rva]
        result = False
        pc, first = rva, []
        for _ in range(80):
            if not self.is_code(pc):
                break
            i = next(self.md.disasm(self.pe.get_data(pc, 16), self.base + pc), None)
            if i is None:
                break
            first.append(f"{i.mnemonic} {i.op_str}")
            if i.mnemonic in ("ret", "retn", "jmp", "int3"):
                break
            writes = i.op_str.startswith("esp,") and not re.fullmatch(r"esp, (0x[0-9a-f]+|\d+)", i.op_str) \
                or i.mnemonic == "leave" or i.mnemonic == "xchg" and "esp" in i.op_str \
                or i.mnemonic in ("lea", "mov") and i.op_str.startswith("ebp, ")  # __EH_prolog sets the caller's frame
            if writes and first[:2] != ["push ebp", "mov ebp, esp"]:
                result = True
                break
            pc += i.size
        self.esp_cache[rva] = result
        return result

    def table_targets(self, ins, rva, tables=()):
        """Targets of a `jmp [r*4 + table]` in the function at rva: entries up to the first that isn't code
        near the entry or where another table starts."""
        t = re.fullmatch(r"jmp dword ptr \[\w+\*4\s*([+-])\s*(0x[0-9a-f]+|\d+)\]", f"{ins.mnemonic} {ins.op_str}")
        if not t:
            return []
        tables = tables or self.tables.get(rva, ())
        disp = int(t.group(2), 0) * (1 if t.group(1) == "+" else -1)
        table = ((disp & 0xFFFFFFFF) - self.base) & 0xFFFFFFFF
        out = []
        for k in range(512):
            if k and table + 4 * k in tables:
                break
            try:
                e = (struct.unpack("<I", self.pe.get_data(table + 4 * k, 4))[0] - self.base) & 0xFFFFFFFF
            except (pefile.PEFormatError, struct.error):
                break
            if not self.is_code(e) or abs(e - rva) > 0x20000:
                break
            out.append(e)
        return out

    def _decode(self, rva, tables, depth, budget=20000, out=None):
        seen, todo, rets, complete = set(), [rva], set(), True
        while todo:
            pc = todo.pop()
            while pc not in seen and pc not in tables and self.is_code(pc):
                seen.add(pc)
                budget -= 1
                if budget <= 0:
                    return rets, False
                ins = next(self.md.disasm(self.pe.get_data(pc, 16), self.base + pc), None)
                if ins is None:
                    break
                if out is not None:
                    out[pc] = ins
                m = ins.mnemonic
                if m in ("ret", "retn"):
                    rets.add(int(ins.op_str, 16) if ins.op_str else 0)
                    break
                if m in ("int3", "hlt", "ud2"):
                    break
                if m == "call":
                    try:
                        image, target = self.callee(int(ins.op_str, 16))
                    except ValueError:
                        image, target = self, None
                    if target is not None and image.noreturn(target, depth):
                        break
                elif m.startswith("j"):
                    try:
                        image, target = self.callee(int(ins.op_str, 16))
                        if image is not self:  # a tail jump into the original: it pops what that function pops
                            rets |= image.rets(target, depth + 1)
                        else:
                            todo.append(target)
                    except ValueError:  # a jump table, or a tail jump through a pointer
                        t = re.search(r"\*\s*4\s*([+-])\s*(0x[0-9a-f]+|\d+)\]", ins.op_str)
                        if t:
                            disp = int(t.group(2), 0) * (1 if t.group(1) == "+" else -1)
                            tables.add(((disp & 0xFFFFFFFF) - self.base) & 0xFFFFFFFF)
                            targets = self.table_targets(ins, rva, tables)
                            todo.extend(targets)
                            complete = complete and bool(targets)
                        else:
                            complete = False
                    if m == "jmp":
                        break
                pc += ins.size
        return rets, complete


def slot_offset(ins, order, pos, a):
    """vtable offset of the indirect call at a (`call [r+off]`, or `mov r2, [r+off]` ... `call r2` in the same
    straight-line run), or None."""
    c = ins[a]
    m = re.fullmatch(r"dword ptr \[(\w+)(?: \+ (0x[0-9a-f]+|\d+))?\]", c.op_str)
    if m:
        return int(m.group(2) or "0", 0) if m.group(1) not in ("esp", "ebp") else None
    if not re.fullmatch(r"e[a-d]x|e[sd]i|ebx|ebp", c.op_str):
        return None
    k = pos[a]
    while k > 0:
        prev = ins[order[k - 1]]
        if prev.address + prev.size != ins[order[k]].address:
            return None
        k -= 1
        if prev.mnemonic in ("call", "ret") or prev.mnemonic.startswith("j"):
            return None
        mm = re.fullmatch(rf"dword ptr \[(\w+)(?: \+ (0x[0-9a-f]+|\d+))?\]", prev.op_str.split(", ", 1)[-1])
        if prev.mnemonic == "mov" and prev.op_str.startswith(c.op_str + ","):
            if mm and mm.group(1) not in ("esp", "ebp"):
                return int(mm.group(2) or "0", 0)
            return None
    return None


def call_pops(img, rva):
    """{call address: bytes the callee pops} for the indirect calls in the function at rva, solved from the
    stack depth: code without a frame pointer keeps esp exact, so every path reaches `ret` with the depth it
    had on entry and paths that meet agree. Each indirect call (and a direct one to a callee whose `ret N`
    isn't known) pops an unknown amount; the paths give linear equations in them. Only meaningful for code
    built without frame pointers (the original; ours with /Oy). Calls whose amount the equations don't fix
    are left out."""
    ins = img.instructions(rva)
    order = sorted(ins)
    pos = {a: k for k, a in enumerate(order)}
    unknowns = {}  # call address -> index

    def succ(i):
        m = i.mnemonic
        if m in ("ret", "retn", "int3", "hlt", "ud2"):
            return []
        out = []
        if m.startswith("j"):
            try:
                out.append(int(i.op_str, 16) - img.base)
            except ValueError:
                return img.table_targets(i, rva)
            if m == "jmp":
                return out
        nxt = i.address - img.base + i.size
        return out + [nxt]

    # depth: (const, {unknown index: coef}) bytes pushed below the entry esp
    state = {rva: (0, {})}
    since = {rva: 0}  # bytes pushed since the last call on the path that first reached an address
    local = {}        # unknown -> bytes pushed between the previous call and its call
    equations = []
    bounds = []
    base = prologue_depth(ins, order, rva)
    work = [rva]
    while work:
        a = work.pop()
        i = ins.get(a)
        if i is None:
            continue
        const, terms = state[a]
        m, op = i.mnemonic, i.op_str
        new = (const, terms)
        pushed = since[a]
        if m == "push":
            new = (const + 4, terms)
        elif m == "pop":
            new = (const - 4, terms)
            if op == "esp":
                continue
        elif m in ("pushal", "pushad"):
            new = (const + 32, terms)
        elif m in ("popal", "popad"):
            new = (const - 32, terms)
        elif m in ("pushfd", "pushfl"):
            new = (const + 4, terms)
        elif m in ("popfd", "popfl"):
            new = (const - 4, terms)
        elif m in ("sub", "add") and op.startswith("esp, "):
            try:
                n = int(op[5:], 0)
            except ValueError:
                continue  # sub esp, eax (alloca): untracked from here
            new = (const + n if m == "sub" else const - n, terms)
        elif op.startswith("esp,") or m == "leave" or m == "xchg" and "esp" in op:
            continue  # mov esp, ebp / lea esp, [...] / and esp, -8: a frame pointer restores esp; untracked
        elif m == "call":
            try:
                timg, target = img.callee(int(op, 16))
            except ValueError:
                timg, target = img, None
            if target is not None and (timg.noreturn(target, 0) or timg.moves_esp(target)):
                continue
            r = timg.rets(target) if target is not None else set()
            if len(r) == 1:
                new = (const - next(iter(r)), terms)
            else:
                u = unknowns.setdefault(a, len(unknowns))
                local[u] = max(pushed, 0)
                bounds.append((u, const, dict(terms)))  # depth at the call - P(u) >= base
                t = dict(terms)
                t[u] = t.get(u, 0) - 1
                new = (const, t)
        elif m in ("ret", "retn"):
            equations.append((const, terms))  # depth == 0
            continue
        pushed = 0 if m == "call" else max(pushed + new[0] - const, 0)
        for s in succ(i):
            if s not in ins:
                continue
            if s in state:
                c2, t2 = state[s]
                diff = dict(new[1])
                for k, v in t2.items():
                    diff[k] = diff.get(k, 0) - v
                diff = {k: v for k, v in diff.items() if v}
                if new[0] - c2 or diff:
                    equations.append((new[0] - c2, diff))
            else:
                state[s] = new
                since[s] = pushed
                work.append(s)

    img.systems[rva] = (unknowns, equations, bounds, base)
    solved = solve(equations, bounds, base, len(unknowns))
    if solved is None:  # inconsistent: something on the way isn't modelled (an esp-moving helper)
        img.systems[rva] = None
        return {}, ins, order, pos
    if len(solved) < len(unknowns):
        # Still ambiguous: prefer solutions where each callee pops no more than was pushed since the
        # previous call (true unless an argument was pushed ahead of an earlier call).
        guess = solve(equations, [(u, c - base, t) for u, c, t in bounds] + [(u, n, {}) for u, n in local.items()], 0,
                      len(unknowns))
        for u, v in (guess or {}).items():
            solved.setdefault(u, v)
    return {a: solved[u] for a, u in unknowns.items() if u in solved}, ins, order, pos


def prologue_depth(ins, order, rva):
    """Stack depth after the entry's run of callee-saved pushes and frame allocation: outgoing arguments
    are pushed above it, so no callee pops below it."""
    depth, k = 0, order.index(rva) if rva in ins else 0
    for a in order[k:k + 16]:
        i = ins[a]
        if i.mnemonic == "push" and i.op_str in ("ebx", "esi", "edi", "ebp"):
            depth += 4
        elif i.mnemonic == "sub" and re.fullmatch(r"esp, (0x[0-9a-f]+|\d+)", i.op_str):
            depth += int(i.op_str[5:], 0)
        elif i.mnemonic == "mov" and not i.op_str.startswith("esp") and "esp," not in i.op_str \
                and not re.search(r"\[|fs:", i.op_str.split(", ")[0]):
            continue  # mov esi, ecx / mov eax, [esp + 8] between the pushes
        else:
            break
    return depth


def solve(equations, bounds, base, n, domains=None, limit=200000):
    """{unknown: value} for the unknowns every solution agrees on (None: no solution with each unknown in
    `domains` {unknown: allowed values}). Equations: const + sum(coef * u) == 0. Bounds (u, const, terms):
    0 <= u <= const + sum(coef * v) - base. Every u is a multiple of 4 below 68. Gives up (returning what
    the equations fix one at a time) on more than 12 free unknowns or `limit` search nodes."""
    domains = domains or {}
    solved = {u: next(iter(d)) for u, d in domains.items() if len(d) == 1}
    changed = True
    while changed:  # one unknown at a time first
        changed = False
        for const, terms in equations:
            c = const + sum(v * solved[k] for k, v in terms.items() if k in solved)
            rest = {k: v for k, v in terms.items() if k not in solved}
            if len(rest) == 1:
                (k, v), = rest.items()
                if (-c) % v == 0 and (-c) // v >= 0 and (k not in domains or (-c) // v in domains[k]):
                    solved[k] = (-c) // v
                    changed = True
                else:
                    return None
            elif not rest and c != 0:
                return None
    free = [u for u in range(n) if u not in solved]
    if len(free) > 12:
        return solved
    index = {u: k for k, u in enumerate(free)}
    # each constraint is checked once its last free unknown is assigned
    checks = defaultdict(list)
    for const, terms in equations:
        vs = [index[k] for k in terms if k in index]
        checks[max(vs) if vs else -1].append(("eq", const, terms, None))
    for u, const, terms in bounds:
        vs = [index[k] for k in list(terms) + [u] if k in index]
        checks[max(vs) if vs else -1].append(("le", const, terms, u))
    values = {u: set() for u in free}
    nodes = [0]
    found = [False]
    assign = dict(solved)

    def ok(k):
        for kind, const, terms, u in checks[k]:
            depth = const + sum(v * assign[x] for x, v in terms.items())
            if kind == "eq" and depth != 0 or kind == "le" and depth - assign[u] < base:
                return False
        return True

    def search(k):
        nodes[0] += 1
        if nodes[0] > limit:
            raise TimeoutError
        if k == len(free):
            found[0] = True
            for u in free:
                values[u].add(assign[u])
            return
        for val in sorted(domains.get(free[k], range(0, 68, 4))):
            assign[free[k]] = val
            if ok(k):
                search(k + 1)
        del assign[free[k]]

    if not ok(-1):
        return None
    try:
        search(0)
    except TimeoutError:
        return solved
    if not found[0]:
        return None
    for u in free:
        if len(values[u]) == 1:
            solved[u] = next(iter(values[u]))
    return solved


def agrees(img, rva, ours):
    """Offsets of `ours` ({vtable offset: set of bytes popped}) that the original function's stack equations
    can't accommodate: with each of the original's calls through such a slot popping one of our values,
    there is no solution. A slot may be called on different objects, hence a set."""
    unknowns, equations, bounds, base = img.systems[rva]
    _, ins, order, pos = img.call_pops_cache[rva]
    at = defaultdict(list)
    for a, u in unknowns.items():
        off = slot_offset(ins, order, pos, a)
        if off in ours:
            at[off].append(u)
    if solve(equations, bounds, base, len(unknowns), {u: ours[off] for off, us in at.items() for u in us}) is not None:
        return []
    return [off for off, us in at.items()
            if solve(equations, bounds, base, len(unknowns), {u: ours[off] for u in us}) is None]


def virtual_calls(img, rva):
    """{vtable offset: set of bytes the callee pops} for the calls through a vtable slot in the function at
    rva, from call_pops()."""
    pops, ins, order, pos = img.call_pops_cache[rva] = call_pops(img, rva)
    out = defaultdict(set)
    for a, n in pops.items():
        off = slot_offset(ins, order, pos, a)
        if off is not None:
            out[off].add(n)
    return out


def fmt(rets):
    return "/".join(str(r) for r in sorted(rets)) if rets else "?"


# ---------------------------------------------------------------- declared types

FOUR = set("""bool char short int long float unsigned signed wchar_t char8_t char16_t char32_t
int8_t uint8_t int16_t uint16_t int32_t uint32_t size_t ptrdiff_t intptr_t uintptr_t
BOOL BYTE WORD DWORD UINT ULONG LONG INT HANDLE HWND HMODULE HINSTANCE HDC HKEY LPVOID LPCVOID LPSTR LPCSTR
LPWSTR LPCWSTR UINT_PTR ULONG_PTR LONG_PTR DWORD_PTR WPARAM LPARAM LRESULT HRESULT SIZE_T ATOM COLORREF
time_t __time32_t""".split())
EIGHT = {"double", "int64_t", "uint64_t", "__int64", "LONGLONG", "ULONGLONG", "__time64_t", "long long",
         "unsigned long long", "unsigned __int64", "long double", "DWORD64", "ULONG64", "LONG64"}
CONVS = {"__thiscall": "thiscall", "__stdcall": "stdcall", "__cdecl": "cdecl", "__fastcall": "fastcall",
         "WINAPI": "stdcall", "APIENTRY": "stdcall", "CALLBACK": "stdcall", "NTAPI": "stdcall"}
QUAL = re.compile(r"\b(const|volatile|struct|class|enum|union|typename)\b")


def split_top(s, sep=","):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "(<[{":
            depth += 1
        elif ch in ")>]}":
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [x.strip() for x in out]


TYPE_ALIASES = {}  # `using X = T;` in our source
TYPE_SIZES = {}    # static_assert(sizeof(X) == N) in our source


def type_size(t, sizes):
    """Stack bytes a parameter of type t takes, or None when unknown."""
    t = re.sub(r"=.*$", "", t).strip()  # default argument
    if "*" in t or "&" in t or "[" in t:
        return 4
    t = re.sub(r"\s+", " ", QUAL.sub("", t)).strip()
    t = re.sub(r"^(std|::)+::", "", t)
    for cand in (t, " ".join(t.split()[:-1])):
        c = cand.strip().split("::")[-1]
        if c in TYPE_ALIASES and TYPE_ALIASES[c] != c:
            return type_size(TYPE_ALIASES[c], sizes)
        if c in TYPE_SIZES:
            return (TYPE_SIZES[c] + 3) & ~3
    for cand in (t, " ".join(t.split()[:-1])):  # with and without a trailing parameter name
        c = cand.strip()
        if c in EIGHT:
            return 8
        if c in FOUR or c in ("unsigned int", "unsigned char", "unsigned short", "unsigned long",
                              "signed char", "long int", "short int"):
            return 4
        if c in sizes:
            return (sizes[c] + 3) & ~3
    return None


def ret_hidden(r, sizes):
    """True if a return type r comes back through a hidden pointer argument, None if unknown."""
    r = re.sub(r"\s+", " ", QUAL.sub("", r)).strip()
    if r == "void" or "*" in r or "&" in r or type_size(r, {}) is not None:
        return False
    return None  # an aggregate: depends on its size and the convention


def parse_fn(t):
    """(convention, return type, [param types]) of a function-pointer type `R(__conv *)(params)`."""
    m = re.search(r"\(\s*(__thiscall|__stdcall|__cdecl|__fastcall|WINAPI|APIENTRY|CALLBACK|NTAPI)\s*\*\s*\)", t)
    if not m:
        return None
    rest = t[m.end():].strip()
    if not rest.startswith("("):
        return None
    depth = 0
    for i, ch in enumerate(rest):
        depth += ch == "("
        depth -= ch == ")"
        if depth == 0:
            break
    params = split_top(rest[1:i])
    if params in ([], ["void"]):
        params = []
    return CONVS[m.group(1)], t[:m.start()].strip(), params


def popped(fn, sizes):
    """Bytes the callee pops according to the declared type, or None when a size is unknown."""
    conv, ret, params = fn
    sz = [type_size(p, sizes) for p in params]
    if any(s is None for s in sz):
        return None
    hidden = ret_hidden(ret, sizes)
    if hidden is None:
        return None
    if conv == "cdecl":
        return 0
    if conv == "thiscall":
        sz = sz[1:]
    if conv == "fastcall":
        regs = 0
        rest = []
        for s in sz:
            if s == 4 and regs < 2:
                regs += 1
            else:
                rest.append(s)
        sz = rest
    return sum(sz) + (4 if hidden else 0)


# ---------------------------------------------------------------- source scan

def find_template_arg(text, start):
    """Text of the balanced <...> template argument list starting at text[start] == '<'."""
    depth = 0
    i = start
    while i < len(text):
        ch = text[i]
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i + 1
        i += 1
    return None, start


def sources():
    files = sorted(p for d in ("src", "host", "record") for p in (ROOT / d).rglob("*") if p.suffix in (".cpp", ".h"))
    return [(p, p.read_text(encoding="utf-8", errors="replace")) for p in files]


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def scan(files):
    aliases = {}   # name -> function-pointer type text (last one wins per file; per-file first)
    consts = {}    # name -> int
    per_file_alias = defaultdict(dict)
    for p, text in files:
        t = strip_comments(text)
        for m in re.finditer(r"\busing\s+(\w+)\s*=\s*([\w:<>, ]+\s*[*&]?)\s*;", t):  # plain type aliases
            TYPE_ALIASES.setdefault(m.group(1), m.group(2).strip())
        for m in re.finditer(r"\bstatic_assert\s*\(\s*sizeof\s*\(\s*(\w+)\s*\)\s*==\s*(0x[0-9a-fA-F]+|\d+)", t):
            TYPE_SIZES.setdefault(m.group(1), int(m.group(2), 0))
        for m in re.finditer(r"\busing\s+(\w+)\s*=\s*([^;]*\(\s*(?:__\w+|WINAPI|CALLBACK|APIENTRY)\s*\*\s*\)[^;]*);", t):
            per_file_alias[p][m.group(1)] = m.group(2)
            aliases.setdefault(m.group(1), m.group(2))
        for m in re.finditer(r"\btypedef\s+([^;]*?)\(\s*(__\w+|WINAPI|CALLBACK)\s*\*\s*(\w+)\s*\)\s*(\([^;]*\))\s*;", t):
            ty = f"{m.group(1)}({m.group(2)} *){m.group(4)}"
            per_file_alias[p][m.group(3)] = ty
            aliases.setdefault(m.group(3), ty)
        for m in re.finditer(r"\b(k\w+|\w*Slot\w*)\s*=\s*(0x[0-9a-fA-F]+|\d+)\b", t):
            consts.setdefault(m.group(1), int(m.group(2), 0))
    return aliases, per_file_alias, consts


def resolve_type(t, p, aliases, per_file_alias):
    t = t.strip()
    for _ in range(4):
        key = t.split("::")[-1].strip()
        if key in per_file_alias[p]:
            t = per_file_alias[p][key]
        elif key in aliases:
            t = aliases[key]
        else:
            break
    return t


def eval_slot(expr, consts):
    e = expr.strip()
    e = re.sub(r"static_cast<\w+>\s*", "", e)
    e = re.sub(r"[A-Za-z_][\w:]*", lambda m: str(consts.get(m.group(0).split("::")[-1], "X")), e)
    if "X" in e.replace("0x", "").replace("0X", ""):
        return None
    try:
        return int(eval(e, {"__builtins__": {}}))  # noqa: S307 - digits and operators only
    except Exception:
        return None


def call_sites(files, aliases, per_file_alias, consts):
    """[(kind, file, line, Fn text, rva or slot)] for OriginalFunction/VirtualCall/VirtualSlot uses."""
    out = []
    for p, text in files:
        t = strip_comments(text)
        for m in re.finditer(r"\b(OriginalFunction|OriginalAddress|VirtualCall|VirtualSlot)\s*(?=<)", t):
            kind = "OriginalFunction" if m.group(1) == "OriginalAddress" else m.group(1)
            if t[m.start() - 9:m.start()].endswith("typename ") or re.match(r"\s*<\s*typename", t[m.end():]):
                continue
            arg, after = find_template_arg(t, m.end())
            if arg is None:
                continue
            line = t.count("\n", 0, m.start()) + 1
            parts = split_top(arg)
            fn_text = resolve_type(parts[0], p, aliases, per_file_alias)
            if kind == "OriginalFunction":
                a = re.fullmatch(r"\s*(0x[0-9a-fA-F]+)\s*", parts[1]) if len(parts) > 1 else None
                if not a or not re.match(r"\s*\(\s*\)", t[after:]):
                    continue
                out.append((kind, p, line, fn_text, int(a.group(1), 16)))
            elif kind == "VirtualCall":
                if len(parts) < 2 or parts[0] in ("Fn",):
                    continue
                out.append((kind, p, line, fn_text, eval_slot(parts[1], consts)))
            else:
                a = re.match(r"\s*\(", t[after:])
                if not a or parts[0] in ("Fn",):
                    continue
                inner, _ = find_template_arg(t, after + a.end() - 1)
                args = split_top(inner or "")
                out.append((kind, p, line, fn_text, eval_slot(args[1], consts) if len(args) > 1 else None))
    return out


# ---------------------------------------------------------------- vtables

def vtables():
    out = []
    for line in (ROOT / "db/classes.jsonl").open(encoding="utf-8"):
        c = json.loads(line)
        for i, v in enumerate(c.get("vftables", [])):
            out.append((c["name"], i, [int(e, 16) for e in v["entries"]]))
    return out


METHOD_SIZES = {"void": 0, "ptr": 4, "u32": 4, "i32": 4, "u16": 4, "i16": 4, "u8": 4, "i8": 4, "bool": 4,
                "f32": 4, "f64": 8, "u64": 8, "i64": 8}


def method_facts():
    out = []
    for p in sorted((ROOT / "db/methods").glob("*.jsonl")):
        for line in p.open(encoding="utf-8"):
            if line.strip():
                f = json.loads(line)
                if "params" in f:
                    out.append(f)
    return out


def symbolize(log, map_path):
    """Prints each espcheck report of `log` with its frames named from the linker map."""
    import bisect
    import ctypes
    undname = ctypes.windll.dbghelp.UnDecorateSymbolName
    text = map_path.read_text(encoding="latin-1")
    base = int(re.search(r"Preferred load address is ([0-9a-fA-F]+)", text).group(1), 16)
    syms = sorted((int(m.group(2), 16) - base, m.group(1)) for m in
                  re.finditer(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8}) f ", text, re.M))
    addrs = [r for r, _ in syms]

    def name(rva):
        k = bisect.bisect_right(addrs, rva) - 1
        if k < 0:
            return "?"
        raw = syms[k][1]
        buf = ctypes.create_string_buffer(1024)
        n = undname(raw.encode("latin-1"), buf, len(buf), 0x1000)
        return f"{buf.value[:n].decode('latin-1') if n else raw}+0x{rva - syms[k][0]:x}"

    content = log.read_bytes().decode("utf-8", errors="replace").replace("\r", "")
    content = re.sub(r"\n(?!espcheck:|\d\d:)", " ", content)  # rejoin reports wrapped by the console
    for line in content.split("\n"):
        if "espcheck:" not in line:
            continue
        head, _, stack = line.partition("stack:")
        print(head.strip()[:120])
        for f in stack.split():
            m = re.fullmatch(r"ours\+0x([0-9a-f]+)", f)
            print(f"    {f:18} {name(int(m.group(1), 16)) if m else ''}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", default=str(ROOT / "build"), help="build folder holding Release/difftest.exe")
    ap.add_argument("--all", action="store_true", help="also list unknowns and partly matching virtual sites")
    ap.add_argument("--json", help="write findings (names, files, numbers) to this file")
    ap.add_argument("--symbolize", help="a log with TS2_ESPCHECK's 'espcheck:' lines (ts2host.log, difftest output)")
    ap.add_argument("--map", help="the linker map of the exe that wrote the log (build-espcheck/Release/*.map)")
    a = ap.parse_args()
    if a.symbolize:
        return symbolize(Path(a.symbolize), Path(a.map))

    orig = Image(tsdb.unpacked_exe())
    findings = []
    counts = Counter()

    def report(kind, verdict, where, detail):
        counts[(kind, verdict)] += 1
        findings.append(dict(kind=kind, verdict=verdict, where=where, detail=detail))
        if verdict == "MISMATCH" or a.all:
            print(f"{verdict:8} {kind:16} {where}  {detail}")

    # 1. hooks
    exe = Path(a.build) / "Release" / "difftest.exe"
    if exe.exists():
        ours = Image(exe)
        ours.game = orig
        lines = subprocess.run([str(exe), "--list-hooks"], capture_output=True, text=True, check=True).stdout.split("\n")
        for line in lines:
            parts = line.split()
            if len(parts) != 3:
                continue
            name, rva, rep = parts[0], int(parts[1], 16), int(parts[2], 16)
            o, r = orig.rets(rva), ours.rets(rep)
            verdict = "ok" if o and r and o == r else ("unknown" if not o or not r else "MISMATCH")
            report("hook", verdict, f"{name} 0x{rva:x}", f"original ret {fmt(o)}, ours ret {fmt(r)}")
            # 5. the virtual calls each replacement makes against the original's stack equations, by offset
            vr, vo = virtual_calls(ours, rep), virtual_calls(orig, rva)
            _, oins, oorder, opos = orig.call_pops_cache[rva]
            if orig.systems.get(rva) is None:
                if vr:
                    report("vcall", "unknown", f"{name} 0x{rva:x}", "the original's stack isn't modelled")
                continue
            offsets = {slot_offset(oins, oorder, opos, a) for a in orig.systems[rva][0]} | set(vo)
            # our offsets whose every call is solved
            rpops, rins, rorder, rpos = ours.call_pops_cache[rep]
            partial = {slot_offset(rins, rorder, rpos, a) for a, i in rins.items()
                       if i.mnemonic == "call" and not i.op_str.startswith("0x") and a not in rpops}
            known = {off: v for off, v in vr.items() if off in offsets and off not in partial}
            bad = set(agrees(orig, rva, known))
            for off in sorted(known):
                where = f"{name} 0x{rva:x} +0x{off:x}"
                if off in bad:
                    report("vcall", "MISMATCH", where, f"slot {off // 4}: ours pops {sorted(known[off])}, the "
                           f"original's stack can't (solved there: {sorted(vo.get(off, ())) or '?'})")
                else:
                    report("vcall", "ok", where, f"slot {off // 4} pops {sorted(known[off])}")
            for off in sorted((set(vr) | partial) - set(known) - {None}):
                if off in offsets:
                    report("vcall", "unknown", f"{name} 0x{rva:x} +0x{off:x}", f"ours pops {sorted(vr.get(off, ()))}+?")
    else:
        print(f"(no {exe}: hook check skipped)")

    # 2, 3. call sites in our source
    files = sources()
    aliases, per_file_alias, consts = scan(files)
    tabs = vtables()
    slot_rets = {}

    def rets_of_slot(slot):
        if slot not in slot_rets:
            c = Counter()
            for _, _, entries in tabs:
                if slot < len(entries):
                    for r in orig.rets(entries[slot]):
                        c[r] += 1
            slot_rets[slot] = c
        return slot_rets[slot]

    sizes = {}
    for kind, p, line, fn_text, key in call_sites(files, aliases, per_file_alias, consts):
        where = f"{p.relative_to(ROOT).as_posix()}:{line}"
        fn = parse_fn(fn_text)
        if fn is None:
            report(kind, "unknown", where, f"type not parsed: {fn_text[:80]}")
            continue
        want = popped(fn, sizes)
        if want is None:
            report(kind, "unknown", where, f"size not known: {fn_text[:100]}")
            continue
        if kind == "OriginalFunction":
            o = orig.rets(key)
            if not o:
                report(kind, "unknown", where, f"0x{key:x}: no ret found; declared pops {want}")
            elif want in o and len(o) == 1:
                report(kind, "ok", where, f"0x{key:x} pops {want}")
            else:
                report(kind, "MISMATCH", where, f"0x{key:x}: original ret {fmt(o)}, declared pops {want}: {fn_text}")
        else:
            if key is None:
                report(kind, "unknown", where, f"slot not resolved; declared pops {want}")
                continue
            c = rets_of_slot(key)
            if not c:
                report(kind, "unknown", where, f"slot {key}: no vtable has it")
            elif want not in c:
                report(kind, "MISMATCH", where, f"slot {key}: no vtable pops {want} (rets {dict(c)}): {fn_text}")
            elif len(c) > 1 and c[want] * 4 < sum(c.values()):
                report(kind, "some", where, f"slot {key}: pops {want} in {c[want]} of {sum(c.values())} vtables {dict(c)}")
            else:
                report(kind, "ok", where, f"slot {key} pops {want}")

    # 4. method facts (the generated ifaces)
    by_class = defaultdict(list)
    for name, i, entries in tabs:
        by_class[name].append(entries)
    for f in method_facts():
        cls = f.get("class")
        vt = f.get("vtable", 0)
        where = f"db/methods {f.get('iface')}::{f['name']} slot {f['slot']}"
        if cls not in by_class or vt >= len(by_class[cls]) or f["slot"] >= len(by_class[cls][vt]):
            report("method", "unknown", where, f"no vtable for {cls}")
            continue
        sz = [4 if p["type"].endswith("*") else METHOD_SIZES.get(p["type"]) for p in f["params"]]
        if any(s is None for s in sz) or not (f["ret"] in METHOD_SIZES or f["ret"].endswith("*")):
            report("method", "unknown", where, f"type sizes unknown: {[p['type'] for p in f['params']]} -> {f['ret']}")
            continue
        want = sum(sz)
        impl = by_class[cls][vt][f["slot"]]
        o = orig.rets(impl)
        if not o:
            report("method", "unknown", where, f"0x{impl:x}: no ret found; declared pops {want}")
        elif o == {want}:
            report("method", "ok", where, f"0x{impl:x} pops {want}")
        else:
            report("method", "MISMATCH", where, f"0x{impl:x}: original ret {fmt(o)}, declared pops {want}")

    print("\nsummary:")
    for kind in ("hook", "vcall", "OriginalFunction", "VirtualCall", "VirtualSlot", "method"):
        row = {v: counts[(kind, v)] for v in ("ok", "MISMATCH", "some", "unknown") if counts[(kind, v)]}
        print(f"  {kind:16} {row}")
    if a.json:
        Path(a.json).write_text(json.dumps(findings, indent=1), encoding="utf-8")
    return 1 if any(f["verdict"] == "MISMATCH" for f in findings) else 0


if __name__ == "__main__":
    sys.exit(main())
