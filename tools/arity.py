"""Forwarder arity check: does what our source declares pop the same stack bytes as the original's `ret N`?

    tools/py -I tools/arity.py --all            # every mismatch in src/ (exit 1 if any)
    tools/py -I tools/arity.py --all -v         # also the unknowns (with why)
    tools/py -I tools/arity.py src/cas/cTSCASSystem.h src/hook/tables/cTSCASSystem.cpp   # only these files
    tools/py -I tools/arity.py --all --json build/arity.json

A callee pops its stack arguments with `ret N` (stdcall, thiscall; cdecl pops none: plain `ret`). When our
declaration and the original disagree, the stack is off by the difference after the call, and the crash comes far
from the cause (lane JI: cITSCASSystem slot 23 declared ten dwords, the original 0x5039b0 ends in `ret 0x2c`, eleven;
every difftest passed because the stand-ins copied the header; it crashed Create-a-Sim). Checked, from the source
alone (no build), against the original's `ret N` found by decoding from its entry (tools/stackcheck.py's decoder:
conditional and direct jumps followed, tail jumps pop what their target pops, jump tables, calls that never return):

  OriginalFunction  every OriginalFunction<Fn, 0xRVA>() / OriginalAddress: what Fn pops against the callee's ret.
                    Fn may be a typedef or `using X = R(__conv *)(...)` alias of the same file (or one unique in src/).
  declaration       every function declaration whose trailing comment starts with the original it stands for:
                    `// 0xRVA`, `// 23 (0x5039b0)`, `// 3 (folded 0x145770)`, `// 24 (thunk 0x50a970 -> 0x502dc0)`
                    (the first RVA: a thunk pops what its target pops). Members are __thiscall unless static or
                    declared otherwise; free and static functions __cdecl unless declared otherwise. This is the check
                    for the standalone shape's interface slots and forwarded members (JI's mistake was here too).
  hook              every hook-table entry {"name", 0xRVA, ..., &Class::Method} whose replacement resolves to one
                    declaration in src/ (or overloads that agree): its pops against the hooked RVA's ret.
  interface slot    every virtual a src/ class declares first (not an override; slot numbered after its first base's
                    slots, a virtual destructor taking one) against what that slot holds in the image: the vtable of
                    the subobject where each db/classes.jsonl class has it as an RTTI base (lane KF: the cheat face's
                    slot 6 declared without the argument its `ret 4` pops). All implementations' rets must agree;
                    classes with overloaded virtuals, or a base chain not all in src/, are skipped; a declaration whose
                    comment names one of the implementations is checked above instead.

Pops: stdcall/thiscall the parameters' stack sizes (each rounded up to 4; `this` in ecx for thiscall), plus 4 for a
hidden return pointer; fastcall the same without the first two dword-sized parameters (ecx, edx); cdecl and variadic 0.
A function with no `ret` found (never returns, tail-calls through a pointer) or with rets that disagree, a parameter
or return type of unknown size, or a declaration not resolved: "unknown" (listed with -v), never a mismatch.

  slot call         every RawSlot<N, R>(obj, args...) / VirtualCall<Fn, N>(obj, args...) / VirtualSlot<Fn>(obj, N)(...)
                    site: the bytes the call passes against the `ret N` of the receiver's slot N body, the receiver's
                    class from its declared type, or inferred from every slot call on it in the file
                    (tools/slotarity.py: how, and what it can't resolve). Lane KS's nested
                    `RawSlot<33>(img, RawSlot<21>(img, x, y, &r, &g, &b, &a))` crashed lot load.

  return            "void that isn't": every declaration we declare `void` that the checks above map to an original
                    (its comment's RVA, a hook entry's RVA, or each implementation of an interface slot) against what
                    the original leaves in eax at its rets. Each path to each ret is followed (the same decoding) and
                    eax (or ax/al) classified by the last instruction that wrote it: strong when on every path it is a
                    stack argument or local ([esp+N]/[ebp+N]), `this` (ecx at entry, or a register holding it), an
                    address (lea), a constant (mov imm, xor eax, eax, mov al, imm), a condition (setcc al), a load, or
                    arithmetic on eax, and never afterwards used as a temporary: stored to memory, pushed, only tested,
                    consumed into another register, or only loaded through (an input pointer; a store through it is an
                    out pointer, which stays strong). Weak when on some path the last write is a call (a Release's
                    leftover: lane OY's cDataListExtension setters), a pop, or nothing (the caller's eax), or it is such
                    a temporary. A strong function is then checked against its direct call sites (db/functions.jsonl's
                    callees; a caller that returns with eax untouched is followed up two levels): weak when none reads
                    eax after the call; a function in a vtable is called indirectly, so its callers aren't checked.
                    A pure virtual no src/ class implements is ours only to call: weak. Strong ones are listed (lanes
                    OR and OX crashed: GetColor and cDataListItem::GetVector3 return their out pointer and game callers
                    use it; the difftests passed because test and implementation shared the void); weak ones with -v.
                    An interface slot is strong when every implementation is. No exit status: tools/guard.py lists
                    strong ones in the changed files as a warning. MSVC compiles `void Get(T *out)` and
                    `T *Get(T *out) { ...; return out; }` alike when it picks eax for the pointer, so a strong finding
                    in a vtable needs a look at a caller (`tools/ghidra.py decompile`) before changing the declaration.
                    Silencing: `eax-ignored: <reason>` in the declaration's trailing comment (the hook entry's line for
                    a hook), e.g. `void SetX(int v) override; // 12 (0xa99430) eax-ignored: no caller reads it`;
                    listed as "silenced" with --ok.

  width             "narrow that isn't": every declaration we declare bool, (u)int8_t, char, (u)int16_t, short or
                    wchar_t (through our type aliases), mapped to an original the same ways as "return" (and an
                    override's `// 7: 0xRVA` comment; a virtual whose class name src/ defines twice, in two
                    namespaces, by its comment's slot number in the namespace-qualified class's vtables), against what
                    the original leaves in eax: ours sets only al (`setne al`), so a caller reading the whole register
                    reads stale upper bits (lane TN: cRenderTargetTexture slot 7 declared `bool`, the original 0x147770
                    `mov eax, [ecx+0x2c]`, an image caller looped over the full eax: 15 of 18 smoke runs crashed).
                    Strong when some path leaves a full-width value: a 32-bit load, `this`, an address (lea from this,
                    a load or the frame), a constant outside the type's range (for bool other than 0/1: `mov eax, 1`
                    is fine), `or eax, -1`, `sbb eax, eax`, or a word extended into a byte type. Fine: only the low
                    part written (setcc al, mov al, xor al, al), extended or masked into range (movzx, and imm, shr 31,
                    neg/sbb), a constant in range. Weak (listed with -v): arithmetic, a stack slot (a spilled bool
                    looks the same), a call's result (a tail call's or a void original's leftover), a full-width value
                    used as a temporary (stored, pushed, a pointer) before the ret. A strong function outside vtables
                    is checked against its direct call sites: weak when every one reads only al (test al, al; movzx
                    ecx, al). An interface slot is strong when any implementation is. Silencing: `eax-width: <reason>`
                    in the declaration's comment (e.g. every caller reads only al). guard.py warns on strong ones.

  ecx              a thiscall called as a function: every OriginalFunction site whose declared convention leaves ecx
                    unset (stdcall, cdecl, fastcall without a dword first parameter) against the original's first use
                    of ecx (ecx_first(): a path that reads ecx, cx or cl before writing it; `push ecx` reserves a slot
                    and doesn't count, a call clobbers it unless it is a frame helper or a direct callee that reads it
                    first). Such a site is a MISMATCH (lane RD's `__stdcall` 0xacadd0 read its manager from ecx and
                    crashed the game; the pop counts agreed). `--ecx RVA...` prints the first read. 2026-10-31: 589
                    sites, none flagged; 788 of the 808 thiscall sites' originals read ecx first (the measure of how
                    much it sees).

Out of scope: file-local slot-call helpers (V<slot, R>), destructors (the vtable's deleting destructor takes a flags
argument the C++ declaration doesn't show), templates, and the built code itself (tools/stackcheck.py decodes our built
replacements' rets and their virtual calls; it needs a build).

tools/guard.py calls check() on a branch's changed src/ files and fails on a mismatch there (an ecx one included).

    tools/py -I tools/arity.py --all --kind return -v   # every void declaration's eax verdict (strong, then weak)
    tools/py -I tools/arity.py --all --kind width -v    # every narrow declaration's eax verdict
    tools/py -I tools/arity.py --all --no-return --no-width   # the stack checks only
"""
import argparse
import bisect
import json
import re
import sys
from collections import Counter, defaultdict, namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stackcheck as S  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

CONV_WORDS = r"__thiscall|__stdcall|__cdecl|__fastcall|WINAPI|APIENTRY|CALLBACK|NTAPI"
SPECIFIERS = re.compile(r"\b(static|virtual|inline|__forceinline|explicit|constexpr|consteval|extern|friend|"
                        r"mutable|register|thread_local|__declspec\s*\([^)]*\)|typename)\b")
RVA_COMMENT = re.compile(r"//\s*(?:slot\s+)?(?:\d+\s*)?(?:\(\s*)?(?:folded\s+|thunk\s+)?(0x[0-9a-fA-F]+)\b(?!\s*[+-]\s*\w)")
ENUMS = set()  # enum names in our source: four bytes on the stack, returned in eax


# ---------------------------------------------------------------- source text

_BLANKED = {}


def blank(text):
    """text with comments, string and character literals and preprocessor lines turned to spaces (newlines kept),
    so offsets still match the original text (memoised: the checks blank every file more than once)."""
    if text not in _BLANKED:
        if len(_BLANKED) > 20000:
            _BLANKED.clear()
        _BLANKED[text] = _blank(text)
    return _BLANKED[text]


# In code, past a line's indentation, the only characters that can start anything _blank acts on.
_BLANK_NEXT = re.compile(r"""[\n/"']""")
_RAW_STRING = re.compile(r'"([^(\s]*)\(')


def _blank(text):
    out = []  # the text up to `done`, a kept span and a blanked span at a time (spans come in order and don't overlap)
    done = 0
    i, n = 0, len(text)

    def fill(a, b):
        nonlocal done
        span = text[a:b]
        out.append(text[done:a])
        out.append(re.sub(r"[^\n]", " ", span) if "\n" in span else " " * len(span))
        done = max(a, b)

    line_start = True
    while i < n:
        c = text[i]
        if line_start and c == "#":
            j = i
            while j < n and text[j] != "\n":
                j += 2 if text[j] == "\\" and j + 1 < n else 1
            fill(i, j)
            i = j
            continue
        if c == "\n":
            line_start = True
            i += 1
            continue
        if c not in " \t\r":
            line_start = False
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            fill(i, j)
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            fill(i, j)
            i = j
        elif c == '"' and text[max(0, i - 1)] == "R" and _RAW_STRING.match(text, i):
            delim = _RAW_STRING.match(text, i).group(1)
            j = text.find(")" + delim + '"', i)
            j = n if j < 0 else j + len(delim) + 2
            fill(i + 1, j - 1)
            i = j
        elif c in "\"'":
            if c == "'" and i > 0 and text[i - 1].isalnum():  # a digit separator (1'000)
                i += 1
                continue
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            fill(i + 1, min(j, n))
            i = j + 1
        elif line_start:
            i += 1
        else:
            m = _BLANK_NEXT.search(text, i + 1)
            i = m.start() if m else n
    out.append(text[done:])
    return "".join(out)


def strip_template_args(name):
    out, depth = "", 0
    for ch in name:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0:
            out += ch
    return out


LABEL = re.compile(r"^\s*(?:(?:public|private|protected)\s*:(?!:)\s*)+")
ATTR = re.compile(r"\[\[.*?\]\]|\balignas\s*\([^)]*\)|__declspec\s*\([^)]*\)")


def classify(stmt):
    """('class', name) / ('namespace', None) / ('enum', name) / ('function', None) / ('other', None) for the text
    in front of a `{`."""
    s = LABEL.sub("", ATTR.sub(" ", stmt)).strip()
    template = s.startswith("template")
    s = re.sub(r"^template\s*<.*?>\s*", "", s, flags=re.S) if template else s
    m = re.match(r"(?:inline\s+)?namespace\b\s*([\w:]*)|extern\s*\"\s*\"", s)
    if m:
        return "namespace", (m.group(1) or "").split("::")[-1] or None
    m = re.match(r"(?:typedef\s+)?enum\b(?:\s+(?:class|struct))?\s*(\w+)?", s)
    if m:
        return "enum", m.group(1)
    m = re.match(r"(?:typedef\s+)?(?:class|struct|union)\b\s*(\w+)?", s)
    if m and "(" not in s.split(":")[0]:
        return ("tclass" if template else "class"), m.group(1)
    if "(" in s:
        return "function", None
    return "other", None


def parse_declaration(stmt, member, class_name):
    """(name, (conv, ret, params), static) for a function declaration statement, or None."""
    s = LABEL.sub("", ATTR.sub(" ", stmt)).strip()
    if not s or re.match(r"(template|typedef|using|friend|return|static_assert|namespace|enum|class|struct|union|"
                         r"goto|case|default)\b", s):
        return None
    depth, name_at = 0, None
    for i, ch in enumerate(s):
        if ch in "<[":
            depth += 1
        elif ch in ">]":
            depth -= 1
        elif ch == "(" and depth == 0:
            m = re.search(r"(~?\b[A-Za-z_]\w*)\s*$", s[:i])
            if not m or m.group(1) in ("decltype", "sizeof", "alignof", "noexcept", "__declspec", "alignas"):
                return None
            name_at = (m.start(1), i)
            break
    if name_at is None:
        return None
    pre, name = s[:name_at[0]], s[name_at[0]:name_at[1]].strip()
    k, depth = name_at[1], 0
    for j in range(k, len(s)):
        depth += s[j] == "("
        depth -= s[j] == ")"
        if depth == 0:
            break
    else:
        return None
    params_text, post = s[k + 1:j], s[j + 1:].strip()
    if re.match(r"\s*[*&^]", params_text):  # a function pointer or reference variable: R (*name)(...)
        return None
    if "=" in pre or "(" in pre or ")" in pre or "operator" in pre or name.startswith("~") or name == "operator":
        return None
    if re.search(r"::\s*$", pre):
        return None  # an out-of-class member definition: its class's declaration says static or not
    if post and not re.fullmatch(r"((const|override|final|noexcept|volatile|&|&&)\s*)*(=\s*(0|default))?\s*", post):
        return None
    if name in ("if", "while", "for", "switch", "return", "sizeof", "new", "delete", "throw", "catch"):
        return None
    static = bool(re.search(r"\bstatic\b", pre))
    conv = re.search(rf"\b({CONV_WORDS})\b", pre)
    ret = re.sub(rf"\b({CONV_WORDS})\b", " ", SPECIFIERS.sub(" ", pre))
    ret = re.sub(r"\s+", " ", ret).strip()
    ctor = member and name == class_name
    if ctor:
        ret = "void"
    elif not ret:
        return None  # a macro call, or a constructor of another class
    params = S.split_top(params_text)
    if params in ([], ["void"]):
        params = []
    variadic = any(p.strip() == "..." for p in params)
    this = member and not static
    if variadic:
        c = "cdecl"
    elif conv:
        c = S.CONVS[conv.group(1)]
    else:
        c = "thiscall" if this else "cdecl"
    if this:
        params = ["void *this"] + params  # in ecx (thiscall, fastcall) or the first stack argument
    overrides = bool(re.search(r"\b(override|final)\b", post))
    virtual = member and bool(re.search(r"\bvirtual\b", pre) or overrides)
    return name, (c, ret, params), static, virtual, ctor, overrides


def hidden_return(ret, member):
    """True if the return value comes back through a hidden pointer argument, None if unknown."""
    r = re.sub(r"\s+", " ", S.QUAL.sub("", ret)).strip()
    r = re.sub(r"^(std|::)+::", "", r)
    top = strip_template_args(r)
    if r == "void" or "*" in top or "&" in top:
        return False
    if top != r:  # a class template's instance by value (std::pair<Node *, bool>)
        return True if member else None
    last = r.split("::")[-1]
    if last in ENUMS:
        return False
    if last in S.TYPE_ALIASES and S.TYPE_ALIASES[last] != last:
        return hidden_return(S.TYPE_ALIASES[last], member)
    if S.type_size(r, {}) is not None and last not in S.TYPE_SIZES:
        return False  # a scalar
    if last in S.TYPE_SIZES:
        # MSVC x86: an instance method returns every class type through a hidden pointer; a free function returns
        # PODs of 1, 2, 4 or 8 bytes in eax/edx:eax (the source can't tell POD here: assume our structs are).
        return True if member else S.TYPE_SIZES[last] not in (1, 2, 4, 8)
    return None


def pops(fn, member=False):
    """Bytes the callee pops according to the declared (conv, ret, params), or None when a size is unknown."""
    conv, ret, params = fn
    if conv == "cdecl" or any(p.strip() == "..." for p in params):
        return 0  # the caller pops (a hidden result pointer included)
    sizes = []
    for p in params:
        p = re.sub(r"=.*$", "", p).strip()
        try:
            sz = S.type_size(p, {})
        except RecursionError:  # an alias cycle the clean-up in check() didn't see
            sz = None
        if sz is None:
            words = re.sub(r"\s+", " ", S.QUAL.sub("", p)).strip().split(" ")
            if any(w.split("::")[-1] in ENUMS for w in words[:2]):
                sz = 4
        if sz is None:
            return None
        sizes.append(sz)
    hidden = hidden_return(ret, member)
    if hidden is None:
        return None
    if conv == "thiscall":
        sizes = sizes[1:]
    if conv == "fastcall":
        regs, rest = 0, []
        for sz in sizes:
            if sz == 4 and regs < 2:
                regs += 1
            else:
                rest.append(sz)
        sizes = rest
    return sum(sizes) + (4 if hidden else 0)


def trailing_comment(raw, pos):
    """The `//` comment after raw[pos] on the same line (nothing but `;{}` and blanks before it), or None."""
    eol = raw.find("\n", pos)
    tail = raw[pos:eol if eol >= 0 else len(raw)]
    k = tail.find("//")
    if k < 0 or tail[:k].strip(" \t;{}"):
        return None
    return tail[k:]


def comment_rva(raw, pos):
    """The RVA a declaration ending at raw[pos] stands for: its trailing comment on the same line."""
    c = trailing_comment(raw, pos)
    m = RVA_COMMENT.match(c) if c else None
    return int(m.group(1), 16) if m else None


Decl = namedtuple("Decl", "path line cls name fn static virtual ctor template rva ns overrides comment",
                  defaults=(False, None))
SCOPES = ("class", "tclass", "namespace")
VIRTUAL_DTOR = re.compile(r"\bvirtual\b[^;{}]*~\s*\w+\s*\(")
_DECL_STRUCTURE = re.compile(r"[(){};]")


def declarations(path, raw, dtors=False):
    """[Decl] for the function declarations in raw: members (cls = the class) and free functions (cls None), with
    inline bodies; what is inside function bodies is skipped. rva: the original named by the trailing comment.
    dtors: also a Decl named "~" (fn None) for each virtual destructor, which takes a vtable slot."""
    b = blank(raw)
    out = []
    stack = []  # (kind, class name) or ("body", pending Decl fields)
    stmt_start, paren = 0, 0
    breaks = [m.start() for m in re.finditer("\n", raw)]

    def first_line(start, end):
        stmt = b[start:end]
        lead = re.match(r"\s*(?:(?:public|private|protected)\s*:(?!:)\s*)*", stmt).end()  # access labels skipped
        return bisect.bisect_left(breaks, start + lead) + 1

    def make(d, enclosing, line, rva, comment=None):
        member = enclosing[0] != "namespace"
        name, fn, static, virtual, ctor, overrides = d
        ns = next((s[1] for s in reversed(stack) if s[0] == "namespace" and s[1]), None)
        return Decl(path, line, enclosing[1] if member else None, name, fn, static, virtual, ctor,
                    enclosing[0] == "tclass", rva, ns, overrides, comment)

    def dtor(stmt, enclosing, line):
        if dtors and enclosing[0] in ("class", "tclass") and VIRTUAL_DTOR.search(stmt):
            out.append(Decl(path, line, enclosing[1], "~", None, False, True, False, enclosing[0] == "tclass", None,
                            None, bool(re.search(r"\)\s*(?:const\s*)?(?:noexcept\s*)?(override|final)\b", stmt))))

    for m in _DECL_STRUCTURE.finditer(b):
        i, c = m.start(), m.group()
        if c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        elif paren > 0:
            continue
        elif c == "{":
            enclosing = stack[-1] if stack else ("namespace", None)
            kind, name = classify(b[stmt_start:i])
            if enclosing[0] not in SCOPES:
                stack.append(("inner", None))
            elif kind == "function":
                d = parse_declaration(b[stmt_start:i], enclosing[0] != "namespace", enclosing[1])
                stack.append(("body", (d, enclosing, i, stmt_start)))  # an inline body
            else:
                stack.append((kind, name))
            stmt_start = i + 1
        elif c == "}":
            top = stack.pop() if stack else ("other", None)
            if top[0] == "body" and top[1][0]:
                d, enclosing, opened, start = top[1]
                # the comment sits after the `{` on the header's line, or after the `}` of a one-line body
                rva, comment = comment_rva(raw, opened + 1), trailing_comment(raw, opened + 1)
                if rva is None and raw.count("\n", opened, i) == 0:
                    rva, comment = comment_rva(raw, i + 1), trailing_comment(raw, i + 1)
                out.append(make(d, enclosing, first_line(start, opened), rva, comment))
            elif top[0] == "body":
                _, enclosing, opened, start = top[1]
                dtor(b[start:opened], enclosing, first_line(start, opened))
            stmt_start = i + 1
        elif c == ";":
            enclosing = stack[-1] if stack else ("namespace", None)
            if enclosing[0] in SCOPES:
                d = parse_declaration(b[stmt_start:i], enclosing[0] != "namespace", enclosing[1])
                if d:
                    out.append(make(d, enclosing, first_line(stmt_start, i), comment_rva(raw, i + 1),
                                    trailing_comment(raw, i + 1)))
                else:
                    dtor(b[stmt_start:i], enclosing, first_line(stmt_start, i))
            stmt_start = i + 1
    return out


CLASS_HEAD = re.compile(r"\b(?:class|struct)\s+(?:__declspec\s*\([^)]*\)\s*)?(\w+)\s*(?:final\s*)?(:(?!:)[^{;]*)?\{")


def class_bases(raw):
    """{class name: its first base's bare name or None} for the class definitions in raw (a name defined twice with
    different bases maps to "?")."""
    out = {}
    for m in CLASS_HEAD.finditer(blank(raw)):
        base = None
        if m.group(2):
            first = S.split_top(m.group(2)[1:])[0] if S.split_top(m.group(2)[1:]) else ""
            first = re.sub(r"\b(public|protected|private|virtual)\b", " ", first)
            first = strip_template_args(first).strip().split("::")[-1].strip()
            base = first or None
        out[m.group(1)] = base if out.get(m.group(1), base) == base else "?"
    return out


def class_all_bases(raw):
    """{class name: set of its direct bases' bare names} for the class definitions in raw. A template base's
    arguments count as bases too (`cLightT<cIStandardLight, Traits>` derives from the face it is given)."""
    out = defaultdict(set)
    for m in CLASS_HEAD.finditer(blank(raw)):
        out[m.group(1)]  # noqa: B018 (a class without bases is still a class)
        for b in S.split_top(m.group(2)[1:]) if m.group(2) else []:
            b = re.sub(r"\b(public|protected|private|virtual)\b", " ", b)
            if "<" in b:
                out[m.group(1)] |= {x.split("::")[-1] for x in re.findall(r"[\w:]+", b[b.index("<"):])}
            b = strip_template_args(b).strip().split("::")[-1].strip()
            if b:
                out[m.group(1)].add(b)
    return out


def vtable_slots(decls, bases):
    """{(class, slot): Decl} for the virtual functions src/ classes declare first (not overriding a base's): slots
    numbered after the base chain's (the first base's, as MSVC lays out the primary vtable; a virtual destructor takes
    one). A class whose base chain isn't all in src/ or defined twice differently, or that declares an overloaded virtual
    (MSVC's order for overloads isn't the declaration order), gets no slots."""
    by_class = defaultdict(list)
    defined_in = defaultdict(set)
    for d in decls:
        if d.cls and d.virtual:
            by_class[d.cls].append(d)
            defined_in[d.cls].add(d.path)
    layout = {}  # class -> [names by slot] or None

    def names(cls, depth=0):
        if cls in layout:
            return layout[cls]
        layout[cls] = None  # a cycle stays None
        if depth > 12 or cls not in bases or bases[cls] == "?" or len(defined_in.get(cls, ())) > 1:
            return None
        base = bases[cls]
        own = by_class.get(cls, [])
        if base is None:
            inherited = []
        else:
            inherited = names(base, depth + 1)
            if inherited is None:
                return None
        new = [d for d in own if not d.overrides and d.name not in inherited]
        if len({d.name for d in new}) != len(new):
            return None  # overloaded virtuals: MSVC groups them in an order this doesn't model
        layout[cls] = list(inherited) + [d.name for d in new]
        return layout[cls]

    out = {}
    for cls, own in by_class.items():
        slots = names(cls)
        if slots is None:
            continue
        first = len(names(bases[cls]) or []) if bases.get(cls) else 0
        for n, name in enumerate(slots[first:], first):
            d = next(d for d in own if d.name == name and not d.overrides)
            if d.fn is not None:  # a virtual destructor ("~") takes the slot; the deleting form isn't declared
                out[(cls, n)] = d
    return out


def implementations(classes, iface, slot):
    """[(class, rva)]: what slot `slot` of interface `iface` (bare name) holds in every class of db/classes.jsonl that
    has it as an RTTI base (or is it): the vtable of the subobject at that base's offset. A qualified iface
    (`ns::cIFoo`) matches only that class."""
    out = []
    name = (lambda n: n) if "::" in iface else (lambda n: n.split("::")[-1])
    for c in classes:
        offsets = [0] if name(c["name"]) == iface else []
        offsets += [o for b, o in zip(c.get("bases", []), c.get("base_offsets", []))
                    if name(b) == iface and o is not None]  # None: a virtual base (offset unknown)
        for off in sorted(set(offsets)):
            vt = next((v for v in c.get("vftables", []) if v.get("offset", 0) == off), None)
            if vt and slot < len(vt.get("entries", [])):
                out.append((c["name"], int(vt["entries"][slot], 16)))
    return out


def load_classes():
    import tsdb
    return tsdb.jsonl("classes.jsonl")


def collect_enums(texts):
    for t in texts:
        for m in re.finditer(r"\benum\s+(?:class\s+|struct\s+)?(\w+)\s*(?::\s*[\w:]+\s*)?[{;]", blank(t)):
            ENUMS.add(m.group(1))


# ---------------------------------------------------------------- hook tables

HOOK_ENTRY = re.compile(r"\{\s*\"(\w+)\"\s*,\s*(0x[0-9a-fA-F]+)\s*,[^,{}]*,[^,{}]*,\s*([^{}]*?)\}")
PLAIN_ALIAS = re.compile(r"\busing\s+(\w+)\s*=\s*([\w:<>, ]+?)\s*;")
USING_DECL = re.compile(r"\busing\s+((?:\w+::)+)(\w+)\s*;")


def hook_entries(path, raw, global_aliases=None):
    """[(path, line, hook name, rva, (class or None, function name) or None, why)] for a hook table. Class aliases
    (`using A = cXAdapters;`) are resolved from the table, then from `global_aliases` (name -> type text)."""
    b = blank(raw)
    aliases = dict(global_aliases or {})
    aliases.update({m.group(1): m.group(2) for m in PLAIN_ALIAS.finditer(b)})
    macros = {m.group(1): (m.group(2), m.group(3)) for m in
              re.finditer(r"^\s*#\s*define\s+(\w+)\(\s*(\w+)\s*\)\s+(.+?)\s*$", raw, re.M)}
    out = []
    for m in HOOK_ENTRY.finditer(raw):
        if b[m.start()] != "{":
            continue  # in a comment
        expr = m.group(3).strip().rstrip(",").strip()
        mac = re.fullmatch(r"(\w+)\(\s*(\w+)\s*\)", expr)
        if mac and mac.group(1) in macros:  # a one-parameter macro of the table (#define X(name) ...&Adapters::name)
            param, body = macros[mac.group(1)]
            expr = re.sub(rf"\b{param}\b", mac.group(2), body)
        line = raw.count("\n", 0, m.start()) + 1
        if re.search(r"\b(static_cast|MemberCast)\s*<", expr) and "(" in expr.split("<", 1)[1].split(">", 1)[0]:
            out.append((path, line, m.group(1), int(m.group(2), 16), None, "overload chosen by a cast"))
            continue
        a = re.search(r"&\s*((?:\w+(?:<[^<>()]*>)?::)*\w+)\s*[,>)]", expr)
        if not a:
            out.append((path, line, m.group(1), int(m.group(2), 16), None, f"replacement not parsed: {expr[:60]}"))
            continue
        q = strip_template_args(a.group(1)).split("::")
        q = [x for x in q if x]
        fn = q[-1]
        cls = q[-2] if len(q) > 1 else None
        for _ in range(3):
            if cls in aliases:
                cls = [x for x in strip_template_args(aliases[cls]).split("::") if x][-1]
        out.append((path, line, m.group(1), int(m.group(2), 16), (cls, fn), None))
    return out


# ---------------------------------------------------------------- what the original leaves in eax

REGS = {}
for _full, _parts in (("eax", "ax al ah"), ("ecx", "cx cl ch"), ("edx", "dx dl dh"), ("ebx", "bx bl bh"),
                      ("esi", "si"), ("edi", "di"), ("ebp", "bp"), ("esp", "sp")):
    for _r in [_full] + _parts.split():
        REGS[_r] = _full
TRACKED = ("ecx", "edx", "ebx", "esi", "edi", "ebp")  # besides eax; their origins kept coarse (COARSE)
# What eax holds at a ret, by the last instruction that wrote it (eax, ax or al):
STRONG_ORIGINS = {
    "stack": "a stack argument or local ([esp+N] / [ebp+N])",
    "this": "ecx at entry (this), or a register holding it",
    "lea": "an address (lea)",
    "const": "a constant (mov imm / xor eax, eax / mov al, imm)",
    "setcc": "a condition (setcc al)",
    "load": "a load from memory",
    "computed": "a computed value (arithmetic on eax)",
    "copy": "a copy of another register's computed value",
    "tail": "the result of a tail-called function that leaves a value (strong) in eax",
}
WEAK_ORIGINS = {
    "call": "a call's result (the last write to eax is a call)",
    "pop": "a pop",
    "entry": "nothing: eax not written on this path (the caller's value)",
}
WEAK_USES = {  # a use of eax after its last write that marks it as a temporary, not a result
    "stored": "stored to memory (a temporary for a store)",
    "tested": "only tested (cmp/test: a condition, e.g. a null check before a call)",
    "pushed": "pushed (an argument)",
    "copied": "consumed into another register (mov/or/cmov ecx, eax)",
    "readonly": "only read through (an input pointer: loads through eax, no store)",
    "storetemp": "a pointer stored through that isn't an argument (a temporary, e.g. a map node)",
}
COARSE = {"this": "this", "call": "call", "entry": "entry"}  # other registers: anything else is "val"
WRITES_FIRST = {"mov", "movzx", "movsx", "lea", "add", "sub", "and", "or", "xor", "adc", "sbb", "inc", "dec", "neg",
                "not", "shl", "shr", "sar", "sal", "rol", "ror", "rcl", "rcr", "imul", "bsf", "bsr", "bswap", "pop",
                "xchg", "xadd", "shld", "shrd", "movd", "cvttss2si", "cvtss2si", "cvttsd2si", "cvtsd2si", "movmskps",
                "pextrw", "popcnt", "lzcnt", "tzcnt"}
READS_ONLY = {"cmp", "test", "push", "bt", "call", "jmp"}
FLAG_ARITH = {"add", "sub", "and", "or", "xor", "adc", "sbb", "inc", "dec", "neg", "shl", "shr", "sar", "sal", "rol",
              "ror", "rcl", "rcr", "cmp", "test", "bt", "imul"}


def _operands(op_str):
    return [o.strip() for o in op_str.split(",")] if op_str.strip() else []


def _reg(op):
    return REGS.get(op)


def _mem_origin(op):
    return "stack" if re.search(r"\[(esp|ebp)\b", op) else "load"


EAX_IN_ADDRESS = re.compile(r"\[[^\]]*\b(eax|ax)\b")
FPU_STORES = re.compile(r"f(i?stp?|isttp|nst\w*|st\w*)$")


def eax_uses(m, ops):
    """How the instruction m with operands ops reads eax (or ax/al): a set of WEAK_USES keys plus "storebase" (a
    store through eax: an out pointer) and "loadbase" (a load through eax)."""
    out = set()
    if m.startswith("stos"):
        return {"stored"}
    for k, o in enumerate(ops):
        if _reg(o) == "eax":
            if m in ("cmp", "test", "bt"):
                out.add("tested")
            elif m == "push":
                out.add("pushed")
            elif k >= 1 and "[" in ops[0]:
                out.add("stored")  # mov [mem], eax / add [mem], eax / xchg [mem], eax
            elif k >= 1 and _reg(ops[0]) not in (None, "eax"):
                out.add("copied")  # mov ecx, eax / or edx, eax / cmovne ecx, eax: consumed into another register
        elif EAX_IN_ADDRESS.search(o) and m != "lea":
            one_op_store = len(ops) == 1 and (FPU_STORES.match(m) or m.startswith("set") or m in ("inc", "dec", "not",
                                                                                               "neg", "pop"))
            store = k == 0 and m not in ("cmp", "test", "push", "call", "jmp", "bt") and \
                (len(ops) > 1 or one_op_store) and not (m.startswith("f") and not FPU_STORES.match(m))
            out.add("storebase" if store else "loadbase")
    return out


def eax_step(ins, state):
    """The path state after one instruction. state: (eax origin, frozenset of eax uses since that write,
    tuple of TRACKED registers' coarse origins)."""
    eax, uses, regs = state
    regs = dict(zip(TRACKED, regs))
    m = ins.mnemonic.split(" ")[-1] if ins.mnemonic.startswith(("rep", "lock")) else ins.mnemonic
    ops = _operands(ins.op_str)

    def origin_of(op):
        r = _reg(op)
        if r == "eax":
            return eax
        if r in regs:
            o = regs[r]
            return o if o in COARSE else "copy"
        if r == "esp":
            return "stack"
        if "[" in op:
            return _mem_origin(op)
        return "const"

    def put(r, o):
        nonlocal eax, uses
        if r == "eax":
            eax, uses = o, frozenset()
        elif r in regs:
            regs[r] = COARSE.get(o, "val")

    def done():
        return eax, uses, tuple(regs[r] for r in TRACKED)

    uses = uses | eax_uses(m, ops)
    if m.startswith("j") and m != "jmp" and "flags" in uses:
        uses = uses | {"tested"}  # sub eax, 1; je ...: a switch's index, not a result
    elif m in FLAG_ARITH and (not ops or _reg(ops[0]) != "eax"):
        uses = uses - {"flags"}
    if m == "call":
        for r in ("eax", "ecx", "edx"):
            put(r, "call")
        return done()
    if m in ("mul", "div", "idiv") or m == "imul" and len(ops) == 1:
        put("eax", "computed")
        put("edx", "computed")
        return done()
    if m in ("cwde", "cbw"):
        put("eax", eax if eax == "entry" else "computed")
        return done()
    if m in ("cdq", "cwd"):
        put("edx", "computed")
        return done()
    if m.startswith(("lods", "movs", "stos", "cmps", "scas")) and ("es:[edi]" in ins.op_str or
                                                                   "[esi]" in ins.op_str):  # string instructions
        if m.startswith("lods"):
            put("eax", "load")
        for r in ("ecx", "esi", "edi"):
            put(r, "val")
        return done()
    if m in ("fnstsw", "fstsw", "lahf") and (not ops or _reg(ops[0]) == "eax"):
        put("eax", "computed")
        return done()
    if m == "leave":
        put("ebp", "val")
        return done()
    if not ops:
        return done()
    dst = _reg(ops[0])
    if m in ("xchg", "xadd") and len(ops) == 2:
        a, b = _reg(ops[0]), _reg(ops[1])
        oa, ob = origin_of(ops[0]), origin_of(ops[1])
        if a:
            put(a, ob if m == "xchg" else "computed")
        if b:
            put(b, oa if m == "xchg" else "computed")
        if "[" in ops[0] and b == "eax":
            uses = frozenset({"stored"})  # eax went to memory and came back with the old value
        return done()
    if dst is None or dst == "esp":
        return done()
    if m.startswith("set"):
        put(dst, "setcc")
    elif m.startswith("cmov"):
        put(dst, "computed")
    elif m == "pop":
        put(dst, "pop")
    elif m == "lea":
        if dst == "eax" and len(ops) > 1 and EAX_IN_ADDRESS.search(ops[1]):
            return done()  # lea eax, [eax + 2]: the same pointer advanced (a loop's cursor); origin and uses stand
        put(dst, "lea")
    elif m in ("mov", "movzx", "movsx"):
        src = ops[1] if len(ops) > 1 else ""
        if _reg(src) == dst:
            return done()  # mov al, ah and the like: no new origin
        put(dst, origin_of(src))
    elif m in ("xor", "sub", "sbb") and len(ops) == 2 and _reg(ops[1]) == dst and ops[0] == ops[1]:
        put(dst, "computed" if m == "sbb" else "const")  # sbb eax, eax: 0 or -1 from the carry
    elif m == "and" and len(ops) == 2 and re.fullmatch(r"0x0+|0", ops[1]) or \
            m == "or" and len(ops) == 2 and re.fullmatch(r"0xf{8}|-1", ops[1]):
        put(dst, "const")
    elif m in WRITES_FIRST or m not in READS_ONLY:
        prev = origin_of(ops[0])
        other = [o for o in ops[1:] if not re.fullmatch(r"-?(0x[0-9a-f]+|\d+)", o)]
        keep = uses
        put(dst, "entry" if prev == "entry" and not other else "computed")
        if dst == "eax" and m in FLAG_ARITH:
            # read-modify-write: the same value evolving (a loop's mask, a switch index), so its earlier uses stand
            uses = keep | {"flags"}
    return done()


def eax_successors(img, ins, rva, entry):
    """Successor RVAs of the original's instruction at rva (decoded by stackcheck.Image.instructions(entry))."""
    m = ins.mnemonic
    nxt = rva + ins.size
    if m in ("ret", "retn", "int3", "hlt", "ud2"):
        return []
    if m == "call":
        try:
            image, target = img.callee(int(ins.op_str, 16))
            if image.noreturn(target, 0):
                return []
        except ValueError:
            pass
        return [nxt]
    if m.startswith("j"):
        try:
            target = int(ins.op_str, 16) - img.base
        except ValueError:
            return list(img.table_targets(ins, entry)) if "*" in ins.op_str else []
        return [target] if m == "jmp" else [target, nxt]
    return [nxt]


def eax_at_rets(ins, entry, succ, budget=200000, tail=None):
    """{ret rva: set of (eax origin, eax uses)} over every path from entry. ins: {rva: instruction with .mnemonic,
    .op_str, .size}; succ(rva, instruction) -> successor RVAs; tail(rva, instruction) -> the function a tail jump
    there goes to (or None): such a jump counts as a ret with origin "tail:<hex rva>" (eax_verdict resolves it). None
    when the state space runs past the budget."""
    start = ("entry", frozenset(), tuple("this" if r == "ecx" else "entry" for r in TRACKED))
    seen = defaultdict(set)
    todo = [(entry, start)]
    out = defaultdict(set)
    while todo:
        budget -= 1
        if budget <= 0:
            return None
        pc, st = todo.pop()
        if pc not in ins or st in seen[pc]:
            continue
        seen[pc].add(st)
        i = ins[pc]
        if i.mnemonic in ("ret", "retn"):
            out[pc].add((st[0], st[1]))
            continue
        t = tail(pc, i) if tail is not None else None
        if t is not None:
            out[pc].add((f"tail:{t:x}", frozenset()))
            continue
        st2 = eax_step(i, st)
        for n in succ(pc, i):
            todo.append((n, st2))
    return dict(out)


def eax_verdict(at_rets, tail_verdict=None):
    """('strong' | 'weak' | 'none', detail) for eax_at_rets()'s answer: strong when every path to every ret leaves a
    value in eax by a strong origin with no use that marks it a temporary; none when no path writes eax.
    tail_verdict(rva) -> the verdict of a tail-called function: its result is ours (strong: "tail"; else "call")."""
    if at_rets is None:
        return "weak", "too many paths to follow"
    if not at_rets:
        return "none", "no ret reached"

    def resolve(o):
        if not o.startswith("tail:"):
            return o
        v = tail_verdict(int(o[5:], 16)) if tail_verdict else "weak"
        return "tail" if v == "strong" else "call"
    at_rets = {k: {(resolve(o), u) for o, u in v} for k, v in at_rets.items()}
    def temp(o, u):
        if "loadbase" in u and "storebase" not in u:
            return u | {"readonly"}
        if "storebase" in u and o not in ("stack", "this"):
            return u | {"storetemp"}  # an out pointer is an argument; a loaded or computed one is a temporary
        return u
    paths = {(o, temp(o, u)) for o, u in set().union(*at_rets.values())}
    if all(o == "entry" for o, _ in paths):
        return "none", "eax not written before any ret"
    strong = all(o in STRONG_ORIGINS and not (u & set(WEAK_USES)) for o, u in paths)

    def say(o, u):
        text = STRONG_ORIGINS.get(o) or WEAK_ORIGINS.get(o, o)
        return text + "".join(f", then {WEAK_USES[x]}" for x in sorted(u) if x in WEAK_USES)
    detail = "; ".join(sorted({say(o, u) for o, u in paths}))
    n = len(at_rets)
    lead = f"eax at the {'ret' if n == 1 else f'{n} rets'}: "
    if strong:
        return "strong", lead + detail
    return "weak", lead + detail + (" (differs between paths)" if len(paths) > 1 else "")


def original_image():
    global _IMAGE
    if _IMAGE is None:
        import tsdb
        _IMAGE = S.Image(tsdb.unpacked_exe())
    return _IMAGE


_IMAGE = None


PURE_WRITES = {"mov", "movzx", "movsx", "lea", "pop", "movd", "cvttss2si", "cvtss2si", "cvttsd2si", "cvtsd2si",
               "movmskps", "movmskpd", "pmovmskb", "pextrw", "popcnt", "lzcnt", "tzcnt", "bsf", "bsr"}


def eax_read_after(ins, start, succ, limit=60):
    """What a caller does with eax after a call, from the instruction at `start` (decoded instructions `ins`,
    successors `succ(rva, instruction)`): "read" (some path uses eax, ax or al before writing it), "returned" (some
    path reaches a ret or leaves the function with eax untouched: its own caller may read it), or "dropped"."""
    seen, todo, found = set(), [start], set()
    while todo and len(seen) < limit:
        pc = todo.pop()
        if pc in seen:
            continue
        seen.add(pc)
        i = ins.get(pc)
        if i is None:
            found.add("returned")  # left the decoded body (a tail jump)
            continue
        m, ops = i.mnemonic, _operands(i.op_str)
        if m in ("ret", "retn"):
            found.add("returned")
            continue
        if m == "nop":
            todo.extend(succ(pc, i))  # nop word ptr [eax + eax]: padding, not a read
            continue
        if m in ("xor", "sub") and len(ops) == 2 and ops[0] == ops[1] and _reg(ops[0]) == "eax":
            continue  # xor eax, eax: written without being read
        if eax_uses(m, ops) or any(_reg(o) == "eax" or EAX_IN_ADDRESS.search(o) for o in ops[1:]):
            return "read"  # movzx eax, al / lea ecx, [eax + 4] / mov ecx, eax
        if m == "call":
            continue  # writes eax
        if ops and _reg(ops[0]) == "eax":
            if m in PURE_WRITES or m.startswith("set") or m == "imul" and len(ops) == 3 or \
                    m in ("xor", "sub") and len(ops) == 2 and ops[0] == ops[1]:
                continue  # written without being read
            return "read"  # add eax, 4 / and al, 1: reads it
        if m == "jmp" and not re.fullmatch(r"0x[0-9a-f]+", i.op_str):
            found.add("returned")
            continue
        todo.extend(succ(pc, i))
    return "returned" if "returned" in found else "dropped"


def original_eax(img=None, classes=None):
    """eax(rva) -> (verdict, detail) of what the original at rva leaves in eax at its rets (memoised). A strong verdict
    is checked against the function's direct call sites (db/functions.jsonl's callees): weak when there are some and
    none reads eax after the call (or returns with it), unless the function sits in a vtable (classes: db/classes.jsonl
    rows; its callers are then mostly indirect and not seen)."""
    import tsdb
    img = img or original_image()
    classes = load_classes() if classes is None else classes
    in_vtable = defaultdict(set)  # rva -> its offsets in the image's vtables
    for c in classes:
        for v in c.get("vftables", []):
            for n, e in enumerate(v.get("entries", [])):
                in_vtable[int(e, 16)].add(4 * n)
    memo, raw_memo = {}, {}
    callers = None
    starts = {int(f["rva"], 16) for f in tsdb.shared_functions()}

    def call_sites(rva, depth=0):
        """[(site, "read" | "returned" | "dropped")] for the direct calls (and tail jumps) to rva. A caller that
        returns with eax untouched passes it on: its own call sites are asked in turn (two levels), unless it sits
        in a vtable."""
        nonlocal callers
        if callers is None:
            callers = defaultdict(set)
            for f in tsdb.shared_functions():
                for c in f.get("callees", []):
                    if c.startswith("0x"):  # not an import (DLL!Name)
                        callers[int(c, 16)].add(int(f["rva"], 16))
        out = []
        target = f"0x{img.base + rva:x}"
        for caller in sorted(callers.get(rva, ())):
            ins = img.instructions(caller)
            for a, i in sorted(ins.items()):
                if i.op_str != target or i.mnemonic not in ("call", "jmp"):
                    continue
                how = "returned" if i.mnemonic == "jmp" else eax_read_after(
                    ins, a + i.size, lambda pc, x, c=caller: eax_successors(img, x, pc, c))
                if how == "returned" and depth < 2 and not in_vtable.get(caller) and caller != rva:
                    up = [h for _, h in call_sites(caller, depth + 1)]
                    if "read" in up:
                        how = "read"
                    elif up and "returned" not in up:
                        how = "dropped"
                out.append((a, how))
        return out

    def tail_target(entry):
        def tail(pc, i):
            if i.mnemonic != "jmp":
                return None
            try:
                t = int(i.op_str, 16) - img.base
            except ValueError:
                return None
            return t if t != entry and t in starts else None
        return tail

    def raw(rva):
        """The verdict from the body alone (tail calls resolved the same way), without the call-site check."""
        if rva not in raw_memo:
            raw_memo[rva] = ("weak", "recursive tail call")  # in progress
            ins = img.instructions(rva)
            raw_memo[rva] = eax_verdict(eax_at_rets(ins, rva, lambda pc, i: eax_successors(img, i, pc, rva),
                                                    tail=tail_target(rva)), lambda t: raw(t)[0])
        return raw_memo[rva]

    def eax(rva):
        if rva not in memo:
            v, detail = raw(rva)
            if v == "strong":
                if rva in in_vtable:
                    offs = "/".join(f"0x{o:x}" for o in sorted(in_vtable[rva]))
                    detail += f"; in a vtable at offset {offs} (callers indirect, not checked)"
                else:
                    sites = call_sites(rva)
                    used = [a for a, how in sites if how != "dropped"]
                    if not sites:
                        detail += "; no direct call site found"
                    elif not used:
                        v = "weak"
                        detail += f"; none of its {len(sites)} direct call sites reads eax"
                    else:
                        read = [a for a, how in sites if how == "read"]
                        detail += (f"; {len(read)} of {len(sites)} direct call sites read eax" +
                                   (f" (e.g. 0x{read[0]:x})" if read else "") +
                                   (f", {len(used) - len(read)} return with it" if len(used) > len(read) else ""))
            memo[rva] = (v, detail)
        return memo[rva]
    return eax


# ---------------------------------------------------------------- narrow returns ("width")

# Declared return types narrower than eax: (bits of the value's range, signed). bool's range is 0..1.
NARROW_TYPES = {"bool": (1, False), "uint8_t": (8, False), "unsigned char": (8, False), "BYTE": (8, False),
                "int8_t": (8, True), "signed char": (8, True), "char": (8, True), "uint16_t": (16, False),
                "unsigned short": (16, False), "unsigned short int": (16, False), "WORD": (16, False),
                "int16_t": (16, True), "short": (16, True), "short int": (16, True), "signed short": (16, True),
                "wchar_t": (16, False), "char16_t": (16, False)}
SUBREG_BITS = {**{r: 32 for r in ("eax", "ecx", "edx", "ebx", "esi", "edi", "ebp", "esp")},
               **{r: 16 for r in ("ax", "cx", "dx", "bx", "si", "di", "bp", "sp")},
               **{r: 8 for r in ("al", "cl", "dl", "bl", "ah", "ch", "dh", "bh")}}
PTR_BITS = {"byte": 8, "word": 16, "dword": 32}
# What eax holds at a ret of an original we declare narrow, by the last instruction that wrote it: fine (a caller
# reading the whole register gets what ours gives it, or reads only al), strong (a full-width value our al-only
# return can't stand for: lane TN's texture slot 7, `mov eax, [ecx+0x2c]`), weak (can't tell).
WIDTH_FINE = {"low": "only its low part written (setcc al / mov al, .. / xor al, al)",
              "bounded": "zero- or sign-extended or masked into the type's range (movzx / and imm / shr / neg sbb)",
              "const": "a constant in the type's range"}
WIDTH_STRONG = {"load": "a 32-bit load (mov eax, [mem])", "this": "this (ecx at entry)",
                "lea": "an address (lea from this, a load or the frame)",
                "wideconst": "a constant outside the type's range (mov eax, imm / or eax, -1)",
                "mask": "0 or -1 (sbb eax, eax)",
                "widezext": "a zero/sign-extended value wider than the type (movzx eax, word ..)"}
# Weak: a stack slot may be a spilled bool (MSVC reuses argument slots as locals: 0x16ac20's [ebp+8]); the last write
# a call: a tail call's result or a void original's leftover (a destructor's), the two can't be told apart.
WIDTH_WEAK = {"computed": "a computed full-width value (arithmetic on eax)", "entry": "nothing: eax not written",
              "pop": "a pop", "wide": "a 16-bit write wider than the type", "call": "a call's result (unresolved)",
              "stack": "a 32-bit stack slot (an argument, or a spilled value)",
              "tail": "a called function's result, full width in the callee (or a void original's leftover)",
              "temp": "a full-width value used as a temporary (stored, pushed or a pointer) before the ret"}
EAX_WIDTH = re.compile(r"\beax-width\s*:\s*(\S.*)?")
SLOT_NUMBER = re.compile(r"//\s*(?:slot\s+)?(\d+)\s*[:(]")
SLOT_COMMENT = re.compile(r"//\s*(?:slot\s+)?\d+\s*:\s*(?:\(\s*)?(?:folded\s+|thunk\s+)?(0x[0-9a-fA-F]+)\b(?!\s*[+-]\s*\w)")


def narrow_type(ret):
    """(bits, signed) when the declared return type `ret` is narrower than eax (NARROW_TYPES, through our type
    aliases), else None."""
    t = re.sub(r"\s+", " ", re.sub(r"\b(const|volatile)\b", " ", ret or "")).strip()
    for _ in range(6):
        t = re.sub(r"^(std)?::", "", t)
        if t in NARROW_TYPES:
            return NARROW_TYPES[t]
        nxt = S.TYPE_ALIASES.get(t)
        if not nxt or nxt == t:
            return None
        t = re.sub(r"\s+", " ", re.sub(r"\b(const|volatile)\b", " ", nxt)).strip()
    return None


def _imm(op):
    try:
        return int(op, 0)
    except ValueError:
        return None


WIDTH_REGS = ("eax",) + TRACKED


def width_at_rets(ins, entry, succ, bits, signed, base, budget=100000, tail=None):
    """{ret rva: set of eax classes} over every path from entry, for a declaration whose type holds `bits` (bool: 1)
    of value: eax's class by its last write (WIDTH_FINE / WIDTH_STRONG / WIDTH_WEAK keys, "const:<v>" for a constant
    in range, "call:<hex rva>" for a direct call's or tail jump's result). The other registers' classes are tracked
    the same way for copies into eax. base: the image base (call targets are absolute). None past the budget."""
    top = (1 << bits) - 1 if not signed else (1 << (bits - 1)) - 1
    narrow = max(bits, 8)  # al for bool and the 8-bit types, ax for the 16-bit ones

    def const(v):
        v &= 0xffffffff
        return f"const:{v}" if v <= top else "wideconst"

    def fine(c):
        return c in ("low", "bounded") or c.startswith("const:")

    def src_class(op, regs):
        r = _reg(op)
        if r is not None:
            if SUBREG_BITS.get(op, 32) < 32:
                return "low" if SUBREG_BITS[op] <= narrow else "wide"
            return "stack" if r == "esp" else regs.get(r, "computed")
        if "[" in op:
            return "stack" if re.search(r"\[(esp|ebp)\b", op) else "load"
        v = _imm(op)
        return const(v) if v is not None else "computed"

    class Regs(dict):
        """The registers' classes; notes a write to eax (which ends its use as a temporary)."""
        wrote = False

        def __setitem__(self, k, v):
            self.wrote |= k == "eax"
            super().__setitem__(k, v)

    def step(i, state):
        regs = Regs(zip(WIDTH_REGS, state))
        m = i.mnemonic.split(" ")[-1] if i.mnemonic.startswith(("rep", "lock")) else i.mnemonic
        ops = _operands(i.op_str)
        # eax used as a temporary after its last write (stored, pushed, a pointer loaded or stored through): the
        # path's value isn't a result (a void original's leftover), so a full-width one there is weak
        used = state[-1] or bool(eax_uses(m, ops) & {"stored", "pushed", "storebase", "loadbase"})

        def out():
            return tuple(regs[r] for r in WIDTH_REGS) + (used and not regs.wrote,)
        if m == "call":
            v = _imm(i.op_str)
            regs["eax"] = f"call:{v - base:x}" if v is not None else "call"
            regs["ecx"] = regs["edx"] = "computed"
            return out()
        if m in ("mul", "div", "idiv") or m == "imul" and len(ops) == 1:
            regs["eax"] = regs["edx"] = "computed"
            return out()
        if m in ("cwde", "cbw", "cdq", "cwd", "fnstsw", "fstsw", "lahf"):
            regs["edx" if m in ("cdq", "cwd") else "eax"] = "computed"
            return out()
        if m.startswith(("lods", "movs", "stos", "cmps", "scas")) and ("es:[edi]" in i.op_str or "[esi]" in i.op_str):
            if m.startswith("lods"):
                regs["eax"] = "low" if "byte" in i.op_str or m == "lodsb" else "load"
            for r in ("ecx", "esi", "edi"):
                regs[r] = "computed"
            return out()
        if m == "leave":
            regs["ebp"] = "computed"
            return out()
        if not ops:
            return out()
        dst = _reg(ops[0])
        if m in ("xchg", "xadd") and len(ops) == 2:
            a, b = _reg(ops[0]), _reg(ops[1])
            ca, cb = src_class(ops[0], regs), src_class(ops[1], regs)
            if a in regs:
                regs[a] = cb if m == "xchg" else "computed"
            if b in regs:
                regs[b] = ca if m == "xchg" else "computed"
            return out()
        if dst not in regs or m in READS_ONLY or m.startswith(("cmp", "test")):
            return out()
        prev = regs[dst]
        if SUBREG_BITS.get(ops[0], 32) < 32:  # a write to al / ax: the upper part keeps the previous value
            regs[dst] = "low" if SUBREG_BITS[ops[0]] <= narrow else "wide"
            return out()
        src = ops[1] if len(ops) > 1 else ""
        if m.startswith("set"):
            return out()
        if m.startswith("cmov"):
            c = src_class(src, regs)
            regs[dst] = prev if c == prev else ("bounded" if fine(c) and fine(prev) else "computed")
        elif m == "pop":
            regs[dst] = "pop"
        elif m == "lea":
            # an address from a pointer (this, a load, the stack): strong; lea eax, [edx + 1] after sbb dl, dl
            # (MSVC's bool from a remainder: only al counts) keeps a low part low; other arithmetic: computed
            mem = re.search(r"\[([^\]]*)\]", src)
            terms = re.findall(r"\b[a-z]{2,3}\b", mem.group(1)) if mem else []
            parts = [_reg(t) for t in terms if _reg(t)]
            base_cls = regs.get(parts[0], "computed") if len(parts) == 1 and "*" not in src else None
            if parts and parts[0] in ("esp", "ebp") or base_cls in ("this", "load", "lea"):
                regs[dst] = "lea"
            elif base_cls == "low" and SUBREG_BITS.get(terms[0], 32) == 32:
                regs[dst] = "low"
            else:
                regs[dst] = "computed"
        elif m == "mov":
            if _reg(src) != dst:
                regs[dst] = src_class(src, regs)
        elif m in ("movzx", "movsx"):
            sw = SUBREG_BITS.get(src) or next((b for k, b in PTR_BITS.items() if src.startswith(k)), 32)
            regs[dst] = "bounded" if sw <= narrow else "widezext"
        elif m in ("xor", "sub") and len(ops) == 2 and ops[0] == ops[1]:
            regs[dst] = "const:0"
        elif m == "sbb" and len(ops) == 2 and ops[0] == ops[1]:
            regs[dst] = "mask"
        else:
            v = _imm(src) if src else None
            pc = int(prev[6:]) if prev.startswith("const:") else None
            if m == "and" and v is not None:
                regs[dst] = const(pc & v) if pc is not None else ("bounded" if (v & 0xffffffff) <= top else "computed")
            elif m == "or" and v is not None and (pc is not None or (v & 0xffffffff) == 0xffffffff):
                regs[dst] = const((pc or 0) | v)
            elif m in ("add", "sub") and v is not None and pc is not None:
                regs[dst] = const(pc + v if m == "add" else pc - v)
            elif m in ("inc", "dec", "neg") and pc is not None:
                regs[dst] = const(pc + 1 if m == "inc" else pc - 1 if m == "dec" else -pc)
            elif m in ("neg", "inc") and prev == "mask":
                regs[dst] = "bounded"  # sbb eax, eax; neg eax: 0 or 1
            elif m == "shr" and v is not None and v >= 32 - bits:
                regs[dst] = "bounded"  # shr eax, 31: the sign bit
            else:
                regs[dst] = "computed"
        return out()

    start = tuple("this" if r == "ecx" else "entry" for r in WIDTH_REGS) + (False,)
    seen = defaultdict(set)
    todo = [(entry, start)]
    found = defaultdict(set)
    while todo:
        budget -= 1
        if budget <= 0:
            return None
        pc, st = todo.pop()
        if pc not in ins or st in seen[pc]:
            continue
        seen[pc].add(st)
        i = ins[pc]
        if i.mnemonic in ("ret", "retn"):
            found[pc].add("temp" if st[-1] and st[0] in WIDTH_STRONG else st[0])
            continue
        t = tail(pc, i) if tail is not None else None
        if t is not None:
            found[pc].add(f"call:{t:x}")
            continue
        st2 = step(i, st)
        for n in succ(pc, i):
            todo.append((n, st2))
    return dict(found)


def width_verdict(at_rets, call_verdict=None):
    """('strong' | 'weak' | 'none', detail) for width_at_rets()'s answer: strong when some path leaves a full-width
    value (WIDTH_STRONG), weak when none does but some path can't be told (WIDTH_WEAK), none when every path leaves the
    low part, an extended value or a constant in range. call_verdict(rva) -> a called function's verdict (its result
    is ours when the call is the last write: strong "tail", none fine, else weak "call")."""
    if at_rets is None:
        return "weak", "too many paths to follow"
    if not at_rets:
        return "none", "no ret reached"

    def resolve(c):
        if c.startswith("call:"):
            v = call_verdict(int(c[5:], 16)) if call_verdict else "weak"
            return {"strong": "tail", "none": "bounded"}.get(v, "call")
        return "const" if c.startswith("const:") else c
    paths = {resolve(c) for cs in at_rets.values() for c in cs}
    n = len(at_rets)
    lead = f"eax at the {'ret' if n == 1 else f'{n} rets'}: "
    strong = sorted(c for c in paths if c in WIDTH_STRONG)
    weak = sorted(c for c in paths if c in WIDTH_WEAK)
    if strong:
        return "strong", lead + "; ".join(WIDTH_STRONG[c] for c in strong) + (
            f" (on some paths; others: {', '.join(sorted(paths - set(strong)))})" if paths - set(strong) else "")
    if weak:
        return "weak", lead + "; ".join(WIDTH_WEAK[c] for c in weak)
    return "none", lead + "; ".join(WIDTH_FINE[c] for c in sorted(paths))


_CALLERS = None


def direct_callers():
    """{callee rva: set of caller function rvas} from db/functions.jsonl's callees (memoised)."""
    global _CALLERS
    if _CALLERS is None:
        import tsdb
        _CALLERS = defaultdict(set)
        for f in tsdb.shared_functions():
            for c in f.get("callees", []):
                if c.startswith("0x"):  # not an import (DLL!Name)
                    _CALLERS[int(c, 16)].add(int(f["rva"], 16))
    return _CALLERS


def eax_width_read_after(ins, start, succ, narrow, limit=60):
    """How a caller reads eax after a call returning a narrow type: "low" (its first read on every path is al, or ax
    for a 16-bit type), "full" (some path reads eax or pushes it), "returned" (passes eax on untouched), "dropped"."""
    low_regs = {"al"} | ({"ax"} if narrow > 8 else set())
    seen, todo, found = set(), [start], set()
    while todo and len(seen) < limit:
        pc = todo.pop()
        if pc in seen:
            continue
        seen.add(pc)
        i = ins.get(pc)
        if i is None:
            found.add("returned")
            continue
        m, ops = i.mnemonic, _operands(i.op_str)
        if m in ("ret", "retn"):
            found.add("returned")
            continue
        if m == "nop":
            todo.extend(succ(pc, i))
            continue
        if m in ("xor", "sub") and len(ops) == 2 and ops[0] == ops[1] and _reg(ops[0]) == "eax":
            continue
        # operands reading eax: a source, a destination the instruction also reads (and al, 1), an address through it
        reads = [o for k, o in enumerate(ops) if (_reg(o) == "eax" and (k > 0 or m not in PURE_WRITES and
                 not m.startswith("set"))) or EAX_IN_ADDRESS.search(o)]
        if reads:
            if all(o in low_regs for o in reads) and m != "push":
                found.add("low")  # test al, al / movzx ecx, al / mov [x], al
                continue
            return "full"  # test eax, eax / mov ecx, eax / push eax / [eax + 4]
        if m == "call":
            continue
        if ops and ops[0] == "eax":
            continue  # written whole without being read
        if m == "jmp" and not re.fullmatch(r"0x[0-9a-f]+", i.op_str):
            found.add("returned")
            continue
        todo.extend(succ(pc, i))
    for k in ("returned", "low"):
        if k in found:
            return k
    return "dropped"


def original_width(img=None, classes=None):
    """width(rva, bits, signed) -> (verdict, detail) for an original we declare as returning a narrow type (memoised).
    A strong verdict is checked against the function's direct call sites: weak when every one that reads eax reads
    only al (ax), unless the function sits in a vtable (callers indirect)."""
    import tsdb
    img = img or original_image()
    classes = load_classes() if classes is None else classes
    in_vtable = defaultdict(set)
    for c in classes:
        for v in c.get("vftables", []):
            for n, e in enumerate(v.get("entries", [])):
                in_vtable[int(e, 16)].add(4 * n)
    starts = function_starts()
    memo, raw_memo = {}, {}

    def tail_target(entry):
        def tail(pc, i):
            if i.mnemonic != "jmp":
                return None
            t = _imm(i.op_str)
            if t is None:
                return None
            t -= img.base
            return t if t != entry and t in starts else None
        return tail

    def raw(rva, bits, signed, depth=0):
        key = (rva, bits, signed)
        if key not in raw_memo:
            raw_memo[key] = ("weak", "recursive call")
            ins = img.instructions(rva)
            at = width_at_rets(ins, rva, lambda pc, i: eax_successors(img, i, pc, rva), bits, signed, img.base,
                               tail=tail_target(rva))
            raw_memo[key] = width_verdict(at, lambda t: raw(t, bits, signed, depth + 1)[0] if depth < 4 else "weak")
        return raw_memo[key]

    def call_sites(rva, narrow, depth=0):
        out = []
        target = f"0x{img.base + rva:x}"
        for caller in sorted(direct_callers().get(rva, ())):
            ins = img.instructions(caller)
            for a, i in sorted(ins.items()):
                if i.op_str != target or i.mnemonic not in ("call", "jmp"):
                    continue
                how = "returned" if i.mnemonic == "jmp" else eax_width_read_after(
                    ins, a + i.size, lambda pc, x, c=caller: eax_successors(img, x, pc, c), narrow)
                if how == "returned":
                    up = [h for _, h in call_sites(caller, narrow, depth + 1)] \
                        if depth < 2 and not in_vtable.get(caller) and caller != rva else []
                    how = "full" if not up or "full" in up or "returned" in up else (
                        "low" if "low" in up else "dropped")
                out.append((a, how))
        return out

    def width(rva, bits, signed):
        key = (rva, bits, signed)
        if key not in memo:
            v, detail = raw(rva, bits, signed)
            if v == "strong":
                if rva in in_vtable:
                    offs = "/".join(f"0x{o:x}" for o in sorted(in_vtable[rva]))
                    detail += f"; in a vtable at offset {offs} (callers indirect, not checked)"
                else:
                    sites = call_sites(rva, max(bits, 8))
                    full = [a for a, how in sites if how == "full"]
                    if not sites:
                        detail += "; no direct call site found"
                    elif not full:
                        v = "weak"
                        detail += f"; every one of its {len(sites)} direct call sites reads only " + (
                            "al" if bits <= 8 else "ax") + " (or none)"
                    else:
                        detail += f"; {len(full)} of {len(sites)} direct call sites read the whole eax " \
                                  f"(e.g. 0x{full[0]:x})"
            memo[key] = (v, detail)
        return memo[key]
    return width


ECX_PART = re.compile(r"\b(ecx|cx|cl|ch)\b")


def ecx_first(img, rva, depth=0, budget=4000, memo=None):
    """How the original at rva treats ecx at entry: (rva of the first instruction some path reads it at, text), or
    None when every path writes it (or calls out) first. A `push ecx` reserves a stack slot (no read); a call ends the
    path (it clobbers ecx) unless ecx is still the entry's, when a direct callee that reads it first counts as the read
    (two levels: `push 5; call <thiscall>` passes this on); a call to a frame helper (__EH_prolog, _chkstk:
    Image.moves_esp) keeps it and the path goes on; a jump to another function's start is followed the same way.
    `rep` string instructions, `loop`/`jecxz` and shifts by cl read it."""
    memo = {} if memo is None else memo
    if (rva, depth) in memo:
        return memo[(rva, depth)]
    memo[(rva, depth)] = None  # in progress
    try:
        ins = img.instructions(rva)
    except Exception:
        return None
    seen, todo, found = set(), [rva], None
    while todo and budget > 0 and found is None:
        budget -= 1
        pc = todo.pop()
        if pc in seen or pc not in ins:
            continue
        seen.add(pc)
        i = ins[pc]
        m, ops = i.mnemonic, _operands(i.op_str)
        text = f"0x{pc:x}: {m} {i.op_str}".strip()
        if m in ("ret", "retn", "int3", "hlt", "ud2"):
            continue
        if m.startswith("rep") or m in ("loop", "loope", "loopne", "jecxz", "jcxz"):
            found = (pc, text)
            break
        if m in ("call", "jmp") and re.fullmatch(r"0x[0-9a-f]+", i.op_str):
            target = int(i.op_str, 16) - img.base
            if m == "call" or target not in ins:
                if depth < 2 and img.is_code(target):
                    inner = ecx_first(img, target, depth + 1, budget, memo)
                    if inner is not None:
                        found = (pc, f"{text} (whose body reads it: {inner[1]})")
                        break
                if m == "call" and img.moves_esp(target):
                    todo.append(pc + i.size)  # __EH_prolog / _chkstk keep ecx (and use the frame they set up)
                    continue
                continue  # a call clobbers ecx (this path no longer has the entry's); a tail jump ends the path
        elif m in ("call", "jmp") and ECX_PART.search(i.op_str):
            found = (pc, text)  # call [ecx + 8]: through this
            break
        if m == "push" and ops == ["ecx"]:
            todo.extend(eax_successors(img, i, pc, rva))
            continue
        if m in ("xor", "sub", "sbb") and len(ops) == 2 and ops[0] == ops[1] and _reg(ops[0]) == "ecx":
            continue  # written without being read
        dest = ops[0] if ops else ""
        dest_ecx = _reg(dest) == "ecx"
        sources = ops[1:] if dest_ecx else ops
        if any(ECX_PART.search(o) for o in sources) or ECX_PART.search(dest) and not dest_ecx:
            found = (pc, text)  # mov eax, [ecx + 4] / cmp ecx, 0 / mov [ecx], eax / shl eax, cl
            break
        if dest_ecx:
            if m in PURE_WRITES or m.startswith("set") or m.startswith("cmov") and False or \
                    m == "imul" and len(ops) == 3:
                continue  # written without being read
            found = (pc, text)  # add ecx, 4 / test ecx, ecx / xchg ecx, eax
            break
        if m == "call":
            continue
        todo.extend(eax_successors(img, i, pc, rva))
    memo[(rva, depth)] = found
    return found


def passes_ecx(fn):
    """True when a declared (conv, ret, params) puts a value in ecx: thiscall, or fastcall with a first argument that
    fits a register."""
    conv, _, params = fn
    if conv == "thiscall":
        return True
    if conv == "fastcall" and params:
        p = re.sub(r"=.*$", "", params[0]).strip()
        try:
            sz = S.type_size(p, {})
        except RecursionError:
            sz = None
        return sz is None or sz <= 4
    return False


def original_ecx(img=None):
    """ecx(rva) -> ecx_first() on the original image (memoised)."""
    img = img or original_image()
    memo = {}
    return lambda rva: ecx_first(img, rva, memo=memo)


EAX_IGNORED = re.compile(r"\beax-ignored\s*:\s*(\S.*)?")


# ---------------------------------------------------------------- the check

def verdict(want, rets):
    """('ok' | 'MISMATCH' | 'unknown', detail) for a declared pop count against an original's ret set."""
    if not rets:
        return "unknown", f"no ret found in the original (never returns or tail-jumps through a pointer); declared pops {want}"
    if len(rets) > 1:
        return "unknown", f"the original's rets disagree ({S.fmt(rets)}); declared pops {want}"
    (r,) = rets
    if r == want:
        return "ok", f"pops {want}"
    return "MISMATCH", f"original ret {r} ({r // 4} dwords), declared pops {want} ({want // 4} dwords)"


def check(files, rets, funcs=None, report=None, classes=None, facts=None, eax=None, ecx=None, width=None):
    """files: [(path, text)] of all our source (the aliases, sizes and declarations come from all of them);
    rets(rva) -> set of the original's ret immediates; funcs: set of function-start RVAs (a declaration comment's
    RVA that isn't one is ignored), None for no filter; report: paths whose sites are reported (None: all);
    classes: db/classes.jsonl rows for the interface-slot check (None skips it); eax(rva) -> (verdict, detail) of
    what the original leaves in eax (original_eax()) for the "return" check (None skips it); ecx(rva) -> the
    original's first read of ecx at entry or None (original_ecx()) for the "ecx" check (None skips it): an
    OriginalFunction declared with a convention that leaves ecx unset (stdcall, cdecl, fastcall without a first
    register argument) whose original reads ecx first is a thiscall called as a plain function (lane RD, 0xacadd0);
    width(rva, bits, signed) -> (verdict, detail) of whether the original leaves a full-width value in eax
    (original_width()) for the "width" check of narrow (bool, 8- and 16-bit) declarations (None skips it).
    Returns [dict(kind, verdict, where, rva, detail)]."""
    files = [(str(p).replace("\\", "/"), t) for p, t in files]
    paths = [Path(p) for p, _ in files]
    S.TYPE_ALIASES.clear()
    S.TYPE_SIZES.clear()
    ENUMS.clear()
    aliases, per_file_alias, _ = S.scan(list(zip(paths, (t for _, t in files))))
    collect_enums(t for _, t in files)
    # `using X = ns::X;` (and longer cycles) would send stackcheck.type_size round in circles: drop them
    for key in list(S.TYPE_ALIASES):
        seen, cur = {key}, key
        while cur in S.TYPE_ALIASES:
            nxt = re.sub(r"\s+", " ", S.QUAL.sub("", S.TYPE_ALIASES[cur])).strip().split(" ")[-1].split("::")[-1]
            if nxt in seen:
                S.TYPE_ALIASES.pop(key, None)
                break
            seen.add(nxt)
            cur = nxt
    # an alias defined differently in two files is ambiguous outside them
    for p, t in files:
        for name, ty in per_file_alias[Path(p)].items():
            if aliases.get(name) != ty:
                aliases[name] = None
    aliases = {k: v for k, v in aliases.items() if v is not None}
    want_file = (lambda p: True) if report is None else (lambda p, r=set(x.replace("\\", "/") for x in report): p in r)
    findings = []
    cache = {}

    def ret_of(rva):
        if rva not in cache:
            cache[rva] = set(rets(rva))
        return cache[rva]

    def add(kind, v, where, rva, detail):
        findings.append(dict(kind=kind, verdict=v, where=where, rva=f"0x{rva:x}" if rva is not None else None,
                             detail=detail))

    # 1. OriginalFunction<Fn, 0xRVA>()
    called = set()  # every original some OriginalFunction site calls
    for kind, p, line, fn_text, key in S.call_sites(list(zip(paths, (t for _, t in files))), aliases, per_file_alias, {}):
        p = str(p).replace("\\", "/")
        if kind != "OriginalFunction":
            continue
        called.add(key)
        if not want_file(p):
            continue
        where = f"{p}:{line}"
        fn = S.parse_fn(fn_text)
        if fn is None:
            add("OriginalFunction", "unknown", where, key, f"type not parsed: {fn_text[:100]}")
            continue
        want = pops(fn)
        if want is None:
            add("OriginalFunction", "unknown", where, key, f"a size is unknown: {fn_text[:100]}")
            continue
        v, d = verdict(want, ret_of(key))
        add("OriginalFunction", v, where, key, d + ("" if v == "ok" else f": {fn_text[:120]}"))
        if ecx is not None and not passes_ecx(fn):
            first = ecx(key)
            add("ecx", "MISMATCH" if first else "ok", where, key,
                f"declared __{fn[0]}, which leaves ecx unset, but the original reads ecx first at {first[1]}: a "
                f"thiscall (declare __thiscall with the receiver first): {fn_text[:120]}" if first else
                f"__{fn[0]}; the original writes ecx before reading it")

    # 2. declarations naming their original
    decls = []
    by_name = defaultdict(list)
    for p, t in files:
        for d in declarations(p, t, dtors=True):
            decls.append(d)
            by_name[(d.cls, d.name)].append(d)
    def want_of(d):
        return pops(d.fn, member=d.cls is not None and not d.static)

    # Several declarations may name one RVA (a slot and the body behind it, `Destruct` and `DeletingDestructor`):
    # when one of them agrees with the original, the others are helpers around it, not the boundary.
    naming = defaultdict(list)
    for d in decls:
        if d.rva is not None and not d.ctor and not d.template:
            naming[d.rva].append(d)
    checked = set()
    for d in decls:
        rva = d.rva
        if rva is None or not want_file(d.path) or (funcs is not None and rva not in funcs):
            continue
        where = f"{d.path}:{d.line} {d.cls + '::' if d.cls else ''}{d.name}"
        if d.ctor:
            continue  # a C++ constructor is never the boundary (no address to hook or call): its adapter is
        if d.template:
            add("declaration", "unknown", where, rva, "a class template's member (not a boundary by itself)")
            continue
        if rva in called and not d.virtual:
            continue  # a wrapper that calls the original it names: the OriginalFunction site is what's checked
        checked.add((d.path, d.line, rva))
        want = want_of(d)
        if want is None:
            add("declaration", "unknown", where, rva, f"a size is unknown: {d.fn[1]} ({', '.join(d.fn[2])[:100]})")
            continue
        v, detail = verdict(want, ret_of(rva))
        if v == "MISMATCH":
            agree = [o for o in naming[rva] if o is not d and want_of(o) is not None and
                     verdict(want_of(o), ret_of(rva))[0] == "ok"]
            if agree:
                o = agree[0]
                add("declaration", "unknown", where, rva, f"{detail}; {o.path}:{o.line} {o.name} names the same "
                    "original and agrees (a helper of that one?)")
                continue
        add("declaration", v, where, rva, detail + ("" if v == "ok" else f" [{d.fn[0]}]"))

    def resolve(p, cls, name):
        """The declarations a hook replacement `&cls::name` in table p may name."""
        cands = by_name.get((cls, name), [])
        if not cands:
            # a free function (`ns::Function` or `Function`): declared in a header or the table itself
            cands = [d for d in by_name.get((None, name), []) if (d.ns == cls if cls else True)
                     and (d.path.endswith(".h") or d.path == p)]
        return cands

    # 3. hook tables
    for p, t in files:
        if not p.startswith("src/hook/tables/") or not want_file(p):
            continue
        for _, line, hook, rva, target, why in hook_entries(p, t, S.TYPE_ALIASES):
            where = f"{p}:{line} {hook}"
            if target is None:
                add("hook", "unknown", where, rva, why)
                continue
            cls, name = target
            cands = resolve(p, cls, name)
            if not cands:  # declared in a base class, or not in src/
                add("hook", "unknown", where, rva, f"no declaration of {cls + '::' if cls else ''}{name} in src/")
                continue
            if len(cands) == 1 and (cands[0].path, cands[0].line, rva) in checked:
                continue  # its declaration names this RVA and was checked above
            got = {want_of(c) for c in cands}
            label = f"{cls + '::' if cls else ''}{name}"
            if None in got:
                add("hook", "unknown", where, rva, f"{label}: a size is unknown")
                continue
            if len(got) != 1:
                # overloads (or same-named functions in several namespaces): whichever the hook takes, it must pop
                # what the original pops, so none agreeing is a mismatch; one agreeing can't say which was taken
                orig = ret_of(rva)
                if len(orig) == 1 and not orig & got:
                    add("hook", "MISMATCH", where, rva, f"original ret {S.fmt(orig)}, no {label} overload pops that "
                        f"(they pop {sorted(got)})")
                else:
                    add("hook", "unknown", where, rva, f"{label}: overloads pop {sorted(got)}, original ret {S.fmt(orig)}")
                continue
            want = got.pop()
            v, detail = verdict(want, ret_of(rva))
            c = cands[0]
            add("hook", v, where, rva, detail + ("" if v == "ok" else f" ({c.path}:{c.line} [{c.fn[0]}])"))

    # 4. interface slots: a virtual a src/ class declares first, against what that slot holds in the image's vtables
    if classes is not None:
        bases = {}
        for p, t in files:
            for k, v in class_bases(t).items():
                bases[k] = v if bases.get(k, v) == v else "?"
        for (cls, slot), d in sorted(vtable_slots(decls, bases).items(), key=lambda x: (x[1].path, x[1].line)):
            if not want_file(d.path) or d.template:
                continue
            impls = implementations(classes, cls, slot)
            if not impls or (d.rva is not None and any(r == d.rva for _, r in impls)):
                continue  # nothing in the image has the interface, or the comment's RVA was checked above
            where = f"{d.path}:{d.line} {cls}::{d.name}"
            want = want_of(d)
            if want is None:
                add("interface slot", "unknown", where, None, f"slot {slot}: a size is unknown: {d.fn[1]} "
                    f"({', '.join(d.fn[2])[:100]})")
                continue
            got = defaultdict(list)
            for c, r in impls:
                rs = ret_of(r)
                if len(rs) == 1:
                    got[next(iter(rs))].append((c, r))
            if len(got) != 1:
                add("interface slot", "unknown", where, None, f"slot {slot}: " + (
                    "no implementation's ret found" if not got else "implementations disagree: " + "; ".join(
                        f"ret {n} ({who[0][0]} 0x{who[0][1]:x}{f' +{len(who) - 1}' if len(who) > 1 else ''})"
                        for n, who in sorted(got.items()))) + f"; declared pops {want}")
                continue
            (r, who), = got.items()
            v, detail = verdict(want, {r})
            add("interface slot", v, where, who[0][1], f"slot {slot}: {detail}; {len(who)} implementation(s), "
                f"e.g. {who[0][0]}" + ("" if v == "ok" else f" [{d.fn[0]}]"))

    # 5. slot calls: RawSlot / VirtualCall / VirtualSlot sites against the receiver's slot body (tools/slotarity.py)
    if classes is not None:
        import slotarity
        rep = None if report is None else {x.replace("\\", "/") for x in report}
        returns = defaultdict(set)  # function name -> its declared return types (a call argument's size)
        for d in decls:
            if d.fn is not None and not d.ctor:
                returns[d.name].add(d.fn[1])
        findings += slotarity.check(files, rep, ret_of, classes, slotarity.load_facts() if facts is None else facts,
                                    aliases, per_file_alias, ENUMS, returns)

    # 6. void that isn't: a declaration we declare void whose original leaves a value in eax
    if eax is not None:
        findings += check_returns(files, decls, resolve, called, want_file, funcs, classes, eax)

    # 7. narrow that isn't: a declaration we declare bool (8, 16 bits) whose original leaves a full-width value
    if width is not None:
        findings += check_widths(files, decls, resolve, called, want_file, funcs, classes, width)
    return findings


def _is_void(d):
    return d.fn is not None and re.sub(r"\s+", " ", d.fn[1]).strip() == "void"


def _marker(pattern, *comments):
    """The reason of the first `<marker>: <reason>` in these comments ("" without a reason), or None."""
    for c in comments:
        m = pattern.search(c or "")
        if m:
            return (m.group(1) or "").strip()
    return None


def _ignored(*comments):
    """The reason of the first `eax-ignored: <reason>` marker in these comments, or None."""
    return _marker(EAX_IGNORED, *comments)


def check_returns(files, decls, resolve, called, want_file, funcs, classes, eax):
    """The "return" findings (see the docstring): verdict strong / weak / silenced for each void declaration mapped to
    an original whose eax at its rets is a value (eax(rva) -> (verdict, detail), see original_eax()).
    A virtual that no src/ class implements (pure, and no class of src/ deriving from its class declares it) is only
    called by our code: its findings are weak (the game never calls ours, so it can't read the missing value)."""
    return _eax_findings("return", files, decls, resolve, called, want_file, funcs, classes,
                         select=lambda d: True if _is_void(d) else None, analyse=lambda rva, _: eax(rva),
                         marker=(EAX_IGNORED, "eax-ignored"), every_impl=True, only_called_weak=True)


def check_widths(files, decls, resolve, called, want_file, funcs, classes, width):
    """The "width" findings (see the docstring): verdict strong / weak / silenced for each declaration returning a
    narrow type (narrow_type()) mapped to an original that leaves a full-width value in eax on some path
    (width(rva, bits, signed) -> (verdict, detail), see original_width()). An interface slot is strong when any
    implementation is (ours replaces or stands beside each). Overrides also name their original as `// 7: 0xRVA`."""
    return _eax_findings("width", files, decls, resolve, called, want_file, funcs, classes,
                         select=lambda d: narrow_type(d.fn[1]) if d.fn is not None else None,
                         analyse=lambda rva, info: width(rva, *info), marker=(EAX_WIDTH, "eax-width"),
                         every_impl=False, only_called_weak=False, slot_comments=True)


def _eax_findings(kind, files, decls, resolve, called, want_file, funcs, classes, select, analyse, marker,
                  every_impl, only_called_weak, slot_comments=False):
    """Findings of `kind` for every declaration select(d) accepts (-> info for analyse(rva, info) -> (verdict,
    detail)) that names its original: by its comment's RVA (slot_comments: also `// N: 0xRVA`), a hook entry's RVA,
    or the implementations of an interface slot (strong when every one is, or any one with every_impl False).
    only_called_weak: a pure virtual no src/ class implements is weak. marker: (pattern, name) whose reason in the
    declaration's comment (or the hook entry's line) silences a finding."""
    out = []
    done = set()
    texts = dict(files)
    parents = defaultdict(set)
    for p, t in files:
        for k, bs in class_all_bases(t).items():
            parents[k] |= bs
    defines = defaultdict(set)  # virtual's name -> classes of src/ declaring a virtual of that name
    for d in decls:
        if d.cls and d.virtual and d.fn is not None:
            defines[d.name].add(d.cls)

    def derives(c, base, depth=0):
        return depth < 16 and any(b == base or derives(b, base, depth + 1) for b in parents.get(c, ()))

    def implemented(d):
        """False for a pure virtual no src/ class deriving from d's class declares (we only call it)."""
        if not d.virtual or not d.cls:
            return True
        stmt = "\n".join(texts.get(d.path, "").split("\n")[d.line - 1:d.line + 3]).split(";")[0].split("{")[0]
        if not re.search(r"=\s*0\s*$", blank(stmt).strip()):
            return True
        return any(c != d.cls and derives(c, d.cls) for c in defines[d.name])

    def add(where, rva, v, detail, reason, ours=True):
        if v == "none":
            return
        if not ours and v == "strong":
            if only_called_weak:
                v, detail = "weak", detail + "; only called: no src/ class implements it"
            else:
                detail += "; only called by our code (no src/ class implements it), which reads only the low part"
        if reason is not None:
            v, detail = "silenced", f"[{v}] {detail}; {marker[1]}: {reason}"
        out.append(dict(kind=kind, verdict=v, where=where, rva=f"0x{rva:x}" if rva is not None else None,
                        detail=detail))

    def rva_of(d):
        if d.rva is not None or not slot_comments or not d.comment:
            return d.rva
        m = SLOT_COMMENT.match(d.comment)
        return int(m.group(1), 16) if m else None

    # declarations naming their original (wrappers that call it are ours only: a hook on it is checked below)
    for d in decls:
        rva = rva_of(d)
        if rva is None or d.ctor or d.template or not want_file(d.path):
            continue
        info = select(d)
        if info is None:
            continue
        if (funcs is not None and rva not in funcs) or (rva in called and not d.virtual):
            continue
        done.add((d.path, d.line, rva))
        v, detail = analyse(rva, info)
        add(f"{d.path}:{d.line} {d.cls + '::' if d.cls else ''}{d.name}", rva, v, detail,
            _marker(marker[0], d.comment), implemented(d))

    # hook entries whose replacement is a selected declaration
    for p, t in files:
        if not p.startswith("src/hook/tables/") or not want_file(p):
            continue
        lines = t.split("\n")
        for _, line, hook, rva, target, why in hook_entries(p, t, S.TYPE_ALIASES):
            if target is None:
                continue
            cands = resolve(p, *target)
            infos = {select(c) for c in cands}
            if not cands or len(infos) != 1 or None in infos:
                continue
            if any((c.path, c.line, rva) in done for c in cands):
                continue
            v, detail = analyse(rva, next(iter(infos)))
            reason = _marker(marker[0], lines[line - 1] if line <= len(lines) else "", *(c.comment for c in cands))
            c = cands[0]
            ret = re.sub(r"\s+", " ", c.fn[1]).strip()
            add(f"{p}:{line} {hook}", rva, v, f"{detail} ({c.path}:{c.line} declares {ret})", reason)

    # interface slots: every implementation in the image's vtables
    if classes is not None:
        bases = {}
        for p, t in files:
            for k, b in class_bases(t).items():
                bases[k] = b if bases.get(k, b) == b else "?"
        slots = [(cls, slot, d, cls) for (cls, slot), d in vtable_slots(decls, bases).items()]
        if slot_comments:
            known = {c["name"] for c in classes}  # a bare name may stand for classes of several namespaces
            slots = [(c, s, d, f"{d.ns}::{c}" if d.ns and f"{d.ns}::{c}" in known else i) for c, s, d, i in slots]
            # a virtual the layout above can't place (its class's name is defined twice in src/, in two namespaces,
            # or its base chain leaves src/): the slot its comment names (`// 7: ...`, `// slot 5: ...`), in the
            # implementations of the namespace-qualified class (lane TN's nGZSceneGraph::cITexture slot 7)
            placed = {(d.path, d.line) for _, _, d, _ in slots}
            for d in decls:
                if not (d.cls and d.virtual and not d.overrides and d.fn is not None and d.comment) or \
                        (d.path, d.line) in placed:
                    continue
                m = SLOT_NUMBER.match(d.comment)
                if m:
                    slots.append((d.cls, int(m.group(1)), d, f"{d.ns}::{d.cls}" if d.ns else d.cls))
        for cls, slot, d, iface in sorted(slots, key=lambda x: (x[2].path, x[2].line)):
            if not want_file(d.path) or d.template:
                continue
            info = select(d)
            if info is None:
                continue
            impls = implementations(classes, iface, slot)
            named = rva_of(d)
            if not impls or (named is not None and any(r == named for _, r in impls)):
                continue
            seen = {}
            for c, r in impls:
                seen.setdefault(r, c)
            got = {r: analyse(r, info) for r in seen}
            got = {r: x for r, x in got.items() if x[1] != "no ret reached"}  # _purecall and the like
            kinds = Counter(v for v, _ in got.values())
            if not kinds or set(kinds) == {"none"}:
                continue
            if every_impl:
                v = "strong" if set(kinds) == {"strong"} else "weak"
            else:
                v = "strong" if kinds["strong"] else "weak"
            shown = {}  # one implementation per verdict, strong first
            for r, (k, text) in sorted(got.items(), key=lambda x: ("strong", "weak", "none").index(x[1][0])):
                shown.setdefault(k, (r, text))
            r0 = next(iter(shown.values()))[0]
            detail = f"slot {slot}: {kinds['strong']} of {len(got)} implementation(s) strong; " + "; ".join(
                f"{k}: {seen[r]} 0x{r:x}: {text}" for k, (r, text) in shown.items())
            add(f"{d.path}:{d.line} {cls}::{d.name}", r0, v, detail, _marker(marker[0], d.comment), implemented(d))
    return out


def source_files(root=ROOT):
    root = Path(root)
    return [(p.relative_to(root).as_posix(), p.read_text(encoding="utf-8", errors="replace"))
            for p in sorted((root / "src").rglob("*")) if p.suffix in (".cpp", ".h")]


def original_rets(cache=True):
    """rets(rva) over the unpacked original exe (tools/stackcheck.py's decoder). With `cache`, the answers persist in
    local/cache/ (keyed by the exe's size and the decoder's source), since the slot-call check decodes every vtable's
    entries at the slots it meets (tens of thousands of functions)."""
    import atexit
    import hashlib
    import tsdb
    exe = tsdb.unpacked_exe()
    rets = original_image().rets
    if not cache:
        return rets
    key = hashlib.sha1(Path(S.__file__).read_bytes() + str(exe.stat().st_size).encode()).hexdigest()[:16]
    path = tsdb.main_root() / "local" / "cache" / f"rets-{key}.json"
    try:
        known = {int(k): set(v) for k, v in json.loads(path.read_text(encoding="utf-8")).items()}
    except (OSError, ValueError):
        known = {}
    size = len(known)

    def cached(rva):
        if rva not in known:
            known[rva] = set(rets(rva))
        return known[rva]

    def save():
        if len(known) == size:
            return
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            tmp = path.with_suffix(f".{id(known)}.tmp")
            tmp.write_text(json.dumps({str(k): sorted(v) for k, v in known.items()}), encoding="utf-8")
            tmp.replace(path)
        except OSError:
            pass
    atexit.register(save)
    return cached


def function_starts():
    import tsdb
    return {int(f["rva"], 16) for f in tsdb.shared_functions()}


def summary(findings):
    c = Counter((f["kind"], f["verdict"]) for f in findings)
    rows = []
    for kind in ("OriginalFunction", "declaration", "hook", "interface slot", "slot call", "ecx"):
        row = {v: c[(kind, v)] for v in ("ok", "MISMATCH", "unknown") if c[(kind, v)]}
        rows.append(f"  {kind:16} {row}")
    row = {v: c[("return", v)] for v in ("strong", "weak", "silenced") if c[("return", v)]}
    if row:
        rows.append(f"  {'return (void)':16} {row}")
    row = {v: c[("width", v)] for v in ("strong", "weak", "silenced") if c[("width", v)]}
    if row:
        rows.append(f"  {'width (narrow)':16} {row}")
    return "\n".join(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="*", help="report only these src/ files (default with --all: every file)")
    ap.add_argument("--all", action="store_true", help="every file under src/")
    ap.add_argument("-v", action="store_true", help="also list the unknowns")
    ap.add_argument("--ok", action="store_true", help="also list the sites that agree")
    ap.add_argument("--json", help="write every finding to this file")
    ap.add_argument("--kind", action="append", help="list only findings of this kind (repeatable): OriginalFunction, "
                    "declaration, hook, 'interface slot', 'slot call', return, width, ecx")
    ap.add_argument("--no-return", action="store_true", help="skip the void-that-isn't check (eax at the rets)")
    ap.add_argument("--no-width", action="store_true", help="skip the narrow-return check (bool and 8/16-bit "
                    "declarations whose original leaves a full-width eax)")
    ap.add_argument("--root", help="read src/ from this checkout instead (e.g. a temporary worktree of an older "
                    "commit); the facts (db/, the exe) stay this checkout's")
    ap.add_argument("--no-ecx", action="store_true", help="skip the ecx check (a thiscall original called through a "
                    "non-thiscall OriginalFunction)")
    ap.add_argument("--ecx", nargs="+", metavar="RVA", help="print where each of these originals first reads ecx at "
                    "entry (or that it doesn't)")
    ap.add_argument("--rets", nargs="+", metavar="RVA", help="print each ret the decoder reaches from these RVAs, "
                    "with its address (to read a disagreement or a surprise)")
    a = ap.parse_args()
    if a.rets:
        import tsdb
        img = S.Image(tsdb.unpacked_exe())
        for r in a.rets:
            rva = int(r, 16)
            ins = img.instructions(rva)
            found = [(k, i) for k, i in sorted(ins.items()) if i.mnemonic in ("ret", "retn")]
            print(f"0x{rva:x}: rets {S.fmt(img.rets(rva))}" + ("" if img.complete.get(rva) else " (decoding incomplete)"))
            for k, i in found:
                print(f"  0x{k:x}: {i.mnemonic} {i.op_str}")
        return 0
    if a.ecx:
        img = original_image()
        for r in a.ecx:
            first = ecx_first(img, int(r, 16))
            print(f"0x{int(r, 16):x}: " + (f"reads ecx first at {first[1]}" if first else "writes ecx before reading it"))
        return 0
    if not a.all and not a.files:
        ap.error("name files or pass --all")
    report = None if a.all else [Path(f).as_posix() for f in a.files]
    findings = check(source_files(a.root or ROOT), original_rets(), function_starts(), report, load_classes(),
                     eax=None if a.no_return else original_eax(), ecx=None if a.no_ecx else original_ecx(),
                     width=None if a.no_width else original_width())
    for f in (x for x in findings if not a.kind or x["kind"] in a.kind):
        v = f["verdict"]
        if v in ("MISMATCH", "strong") or (a.v and v in ("unknown", "weak")) or (a.ok and v in ("ok", "silenced")):
            print(f"{f['verdict']:8} {f['kind']:16} {f['where']}  {f['rva']}: {f['detail']}")
    print("summary:\n" + summary(findings))
    if a.json:
        Path(a.json).write_text(json.dumps(findings, indent=1), encoding="utf-8")
    return 1 if any(f["verdict"] == "MISMATCH" for f in findings) else 0


if __name__ == "__main__":
    sys.exit(main())
