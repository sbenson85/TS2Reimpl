"""Slot-call arity: does each `RawSlot<N, R>(obj, args...)`, `VirtualCall<Fn, N>(obj, args...)` and
`VirtualSlot<Fn>(obj, N)(obj, args...)` in our source pass the stack bytes the slot's body pops (its `ret N`)?

Used by tools/arity.py (kind "slot call"; it runs this over the files it reports, so tools/guard.py checks a lane's
changed files). Lane KS's in-game crash was here: `RawSlot<33, bool>(image, RawSlot<21>(image, x, y, &r, &g, &b, &a))`
gave the four out pointers to slot 21 (GetPixel, `ret 8`) instead of slot 33 (ConvertNativeValueToRGBA, `ret 0x14`).
MSVC pushes an outer call's trailing arguments before it makes a call nested in its arguments, so reading the
original's pushes splits them wrongly; only the bodies' `ret N` tell. Each call's own arguments are counted (a nested
slot call is one argument of its outer call, of its result's size), so the nested split is checked call by call.

What a call passes (bytes): VirtualCall / VirtualSlot from Fn (thiscall: `this` in ecx; stdcall: on the stack; cdecl
pops nothing; a class-type result adds a hidden pointer); RawSlot (always thiscall) from its arguments as the template
deduces them: each argument's type from the expression (literals, casts, nested slot calls, `&x`, variables and members
by their declarations in the file, the headers it includes, then any header; structs by value by their size from a
`static_assert(sizeof(X) == N)` or a plain field list; 64-bit and double as 8, every smaller scalar as 4). An argument
whose size isn't known counts 4 to 8 (a scalar expression) or 4 and up (a variable of unknown type), and a verdict
holds only if every size in the range gives it.

Which body (the receiver's class), in order:
  type      the receiver's declared type: `T *x`, `T &x`, `cRZAutoRefCount<T> x` (and aliases of it), `auto x =
            static_cast<T *>(...)`, a cast in the call itself, `this` (the class whose member the call is in; unknown
            when one of the class's secondary vtables pops something else there, as a member hooked at that vtable
            gets the subobject as `this`), a member `&mObject` (its own class), `ifaces::X` (X's own vtables as an RTTI
            base, else the class its db/methods facts name). The candidates are every vtable in db/classes.jsonl of T,
            or of a class with T as an RTTI base (the subobject's vtable), at slot N. A local `using Fn = ...` is taken
            from the nearest definition before the call (a file may define the same alias name differently).
  inferred  an untyped receiver (`void *`, `cIGZUnknown`, a getter's result), or a type whose classes disagree at slot
            N: every slot call on the same receiver is a constraint (a local or parameter: the same declaration; a
            member `a->m`: the same member on the same declared type of `a`; another expression: the same text in the
            file), and the vtables that satisfy most of them (weight at least 3; a nested pair on the receiver counts
            as one constraint on the sum, weight 2) are taken as its class. When a declaration's own calls don't decide,
            the calls on every declaration of the name in the file are pooled, and each vtable kept must fit every call
            on the other declarations. A call is a mismatch only if every best-fitting vtable pops the same other amount
            there and fits more of the calls than any vtable that would agree with this one. Otherwise unknown. The
            image's RTTI lacks some classes and a member or a reused name can hold different objects, so an inferred
            mismatch is reported unknown ("probable mismatch") when the receiver is a member or an expression, when
            more than a fifth of the calls misfit the class, or (pooled) when the declaration's own other calls don't
            confirm the class.
  fact      no body's ret known but a db/methods fact names the slot of T: the fact's parameters (a mismatch against a
            tentative fact is reported unknown).
A receiver can be named for this check by a comment on the line of the call or of the receiver's declaration:
`// receiver: cIGZBuffer` (a class or an interface in db/classes.jsonl; also overrides an inferred class), or
`// receiver: none` (no inference: the call stays unknown, for a class the RTTI lacks or a wrong inference).

Verdicts: ok / MISMATCH / unknown, with how the class was found and the reason.
Not covered: file-local slot-call helpers other than RawSlot (`V<N>(obj)`), Call<T>(obj, N) / NarrowCall forms, and
named calls through generated interfaces (`As<ifaces::X>(p)->Name()`: tools/stackcheck.py checks the facts).
"""
import json
import re
from collections import Counter, defaultdict
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I drops the script directory
import stackcheck as S
import slotcalls as SC

ABSENT, UNKNOWN = -2, -1
MIN_FIT = 3       # distinct slots a best-fitting vtable must satisfy before its verdicts count
MIN_LEAD = 1       # how many more of the receiver's calls it must fit than any vtable agreeing with a flagged call
KEYWORDS = set("""return else case new delete throw goto sizeof typename using namespace operator co_return
co_await if while for switch do default public private protected template typedef friend""".split())
SCALAR_WORDS = {"void", "auto", "bool", "char", "short", "int", "long", "float", "double", "unsigned", "signed",
                "wchar_t", "__int64", "char8_t", "char16_t", "char32_t"}
UNTYPED = {"void", "cIGZUnknown", "IUnknown", "uint8_t", "uintptr_t", "uint32_t", "int32_t", "char", "auto"}
FLOAT_LIT = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+(?=[eE]))(?:[eE][-+]?\d+)?([fFlL]?)")
INT_LIT = re.compile(r"[-+]?(?:0[xX][0-9a-fA-F]+|\d+)([uUlLiI0-9]*)")
RECEIVER_HINT = re.compile(r"//.*?\breceiver:\s*([\w:]+)")


# ---------------------------------------------------------------- the image's vtables

class Vtables:
    """Every distinct vtable of db/classes.jsonl (identical entry lists merged), with the classes that have each, and
    the pops of their slots (decoded on demand)."""

    def __init__(self, classes, rets):
        self.rets = rets
        self.entries = []           # [tuple of int rvas]
        self.labels = []            # [[(class, offset)]]
        index = {}
        self.at = {}                # (class name, offset) -> index
        for c in classes or []:
            for v in c.get("vftables", []):
                es = tuple(int(e, 16) for e in v.get("entries", []))
                if es not in index:
                    index[es] = len(self.entries)
                    self.entries.append(es)
                    self.labels.append([])
                k = index[es]
                self.labels[k].append((c["name"], v.get("offset", 0)))
                self.at[(c["name"], v.get("offset", 0))] = k
        self.classes = classes or []
        self.by_bare = defaultdict(set)   # bare name -> indices of its vtables (own at 0, or as an RTTI base)
        for c in self.classes:
            bare = c["name"].split("::")[-1]
            if (c["name"], 0) in self.at:
                self.by_bare[bare].add(self.at[(c["name"], 0)])
            for b, o in zip(c.get("bases", []), c.get("base_offsets", [])):
                if o is not None and (c["name"], o) in self.at:
                    self.by_bare[b.split("::")[-1]].add(self.at[(c["name"], o)])
        self.cache = {}
        self.columns = {}

    def known(self, name):
        return name.split("::")[-1] in self.by_bare

    def of(self, name):
        return self.by_bare.get(name.split("::")[-1], set())

    def subobjects(self, name):
        """The class's vtables at nonzero offsets (a hooked member's `this` may be one of them)."""
        bare = name.split("::")[-1]
        return {k for (cls, off), k in self.at.items() if off and cls.split("::")[-1] == bare}

    def ret(self, rva):
        if rva not in self.cache:
            r = set(self.rets(rva))
            self.cache[rva] = next(iter(r)) if len(r) == 1 else UNKNOWN
        return self.cache[rva]

    def pops(self, k, slot):
        es = self.entries[k]
        return self.ret(es[slot]) if slot < len(es) else ABSENT

    def column(self, slot):
        """{pops: set of vtable indices} at a slot over every vtable that has it (UNKNOWN for no single ret)."""
        if slot not in self.columns:
            col = defaultdict(set)
            for k, es in enumerate(self.entries):
                if slot < len(es):
                    col[self.ret(es[slot])].add(k)
            self.columns[slot] = col
        return self.columns[slot]

    def name(self, k):
        cls, off = self.labels[k][0]
        return cls + (f" +0x{off:x}" if off else "") + (f" (+{len(self.labels[k]) - 1} more)" if len(self.labels[k]) > 1
                                                        else "")


# ---------------------------------------------------------------- source facts

class Source:
    """Declarations and sizes over our source: types of names, plain struct sizes, the classes of `this`."""

    def __init__(self, files, enums):
        self.files = dict(files)
        self.blanked = {}
        self.enums = enums
        self.type_names = set(S.FOUR) | {"void", "float", "double", "auto", "bool", "char", "int", "short", "long",
                                         "unsigned", "signed"}
        self.sizes = {}
        self.headers = [p for p in self.files if p.endswith(".h")]
        self.decl_cache, self.global_cache = {}, {}
        for p in self.files:
            b = self.blank(p)
            for m in re.finditer(r"\b(?:class|struct|union|enum(?:\s+class)?)\s+(\w+)", b):
                self.type_names.add(m.group(1))
            for m in re.finditer(r"\busing\s+(\w+)\s*=", b):
                self.type_names.add(m.group(1))
            for m in re.finditer(r"\btypedef\b[^;]*?\b(\w+)\s*;", b):
                self.type_names.add(m.group(1))
        for p in self.files:
            self.struct_sizes(self.blank(p))

    def blank(self, p):
        if p not in self.blanked:
            import arity
            self.blanked[p] = arity.blank(self.files[p])
        return self.blanked[p]

    def is_type(self, word, classes):
        w = word.split("::")[-1]
        return (w in self.type_names or w in S.EIGHT or w in S.TYPE_ALIASES or w in self.enums or classes.known(w)
                or w.endswith("_t") or word.startswith("std::"))

    def struct_sizes(self, text):
        """Sizes of plain structs (no base, no virtual, fields of known size only) by natural alignment."""
        for m in re.finditer(r"\b(?:struct|class)\s+(\w+)\s*\{", text):
            name, start = m.group(1), m.end()
            depth, i = 1, start
            while i < len(text) and depth:
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            body = text[start:i - 1]
            if "{" in body or "virtual" in body or "(" in body or name in S.TYPE_SIZES:
                continue
            off, align, ok = 0, 1, True
            for stmt in body.split(";"):
                stmt = re.sub(r"\b(?:public|private|protected)\s*:", " ", stmt).strip()
                if not stmt:
                    continue
                fm = re.fullmatch(r"((?:const\s+)?[\w:]+(?:\s*<[^;]*>)?)\s*(\**)\s*(\w+)\s*(?:\[\s*(0x[0-9a-fA-F]+|\d+)\s*\])?"
                                  r"(?:\s*=.*)?", stmt, re.S)
                if not fm or "," in stmt:
                    ok = False
                    break
                base = fm.group(1).replace("const ", "").strip()
                if fm.group(2):
                    sz = al = 4
                else:
                    sz = natural_size(base, self.sizes)
                    if sz is None:
                        ok = False
                        break
                    al = min(sz, 8) if base.split("::")[-1] not in self.sizes and base.split("::")[-1] not in \
                        S.TYPE_SIZES else 4
                n = int(fm.group(4), 0) if fm.group(4) else 1
                off = (off + al - 1) // al * al + sz * n
                align = max(align, al)
            if ok and off:
                self.sizes.setdefault(name, (off + align - 1) // align * align)

    def size(self, t):
        """Stack bytes of a parameter of type t, or None."""
        t = t.strip()
        if not t:
            return None
        sz = S.type_size(t, self.sizes)
        if sz is not None:
            return sz
        words = re.sub(r"\s+", " ", S.QUAL.sub("", t)).strip().split(" ")
        if any(w.split("::")[-1] in self.enums for w in words[:2]):
            return 4
        return None

    DECL = r"(?<![\w.>])((?:(?:const|static|constexpr|volatile|unsigned|signed|struct|class|inline|thread_local)\s+)*" \
           r"(?:[A-Za-z_][\w]*::)*[A-Za-z_]\w*(?:\s*<[^;{{}}]*?>)?(?:\s*\*|\s*&|\s+const\b)*)" \
           r"(?:\s+|(?<=[*&]))(?:(?:[*&]\s*)*\w+\s*(?:\[[^\]]*\]\s*)?(?:=[^;{{}}(),]*)?,\s*)*([*&\s]*(?:const\s+)?){name}\s*(\[)?" \
           r"\s*(?=[;=,)\[{{(:])"

    def declared(self, name, path, pos, classes):
        """(type text, is_array, line text, initializer text) of the nearest declaration of `name` before pos in path,
        else in a header of the same stem or one it includes, else in any header (when they agree); or None."""
        pat = re.compile(self.DECL.format(name=re.escape(name)))

        def search(text, end):
            if name not in text:
                return None
            best = None
            for m in pat.finditer(text, 0, end):
                ty = m.group(1).strip()
                head = re.sub(r"^(?:(?:const|static|constexpr|volatile|struct|class|inline|thread_local)\s+)*", "", ty)
                first = re.match(r"[\w:]+", head)
                if not first or first.group(0) in KEYWORDS or not self.is_type(first.group(0), classes):
                    continue
                ty = ty + " " + m.group(2).strip()
                after = text[m.end():m.end() + 200]
                init = None
                im = re.match(r"\s*=\s*([^;]*)", after)
                if im:
                    init = im.group(1)
                eol = text.find("\n", m.end())
                best = (ty.strip(), bool(m.group(3)), (m.start(), eol if eol >= 0 else len(text)), init)
            return best

        got = search(self.blank(path), pos)
        if got:
            return got + (path,)
        if (name, path) in self.decl_cache:
            return self.decl_cache[(name, path)]
        self.decl_cache[(name, path)] = got = self.declared_in_headers(name, path, search)
        return got

    def declared_in_headers(self, name, path, search):
        stem = Path(path).name.split(".")[0]
        raw = self.files[path]
        incs = [i for i in re.findall(r'#include\s+"([^"]+)"', raw)]
        near = [p for p in self.headers if Path(p).name.split(".")[0] == stem or any(p.endswith("/" + i) or p == i
                                                                                       for i in incs)]
        for p in near:
            got = search(self.blank(p), len(self.blank(p)))
            if got:
                return got + (p,)
        if name not in self.global_cache:  # the near headers had none, so searching them again changes nothing
            found = {}
            for p in self.headers:
                got = search(self.blank(p), len(self.blank(p)))
                if got:
                    found.setdefault(got[0], got + (p,))
            self.global_cache[name] = next(iter(found.values())) if len(found) == 1 else None
        return self.global_cache[name]

    def hint(self, path, span):
        """A `// receiver: X` comment on the lines of span (start, end) in path."""
        raw = self.files[path]
        a = raw.rfind("\n", 0, span[0]) + 1
        b = raw.find("\n", span[1])
        b = len(raw) if b < 0 else b
        m = RECEIVER_HINT.search(raw[a:b])
        return m.group(1) if m else None

    def enclosing_class(self, path, pos):
        """The class of `this` at pos: the qualifier of the member definition the call is in, or the class body
        around it, or the file's stem."""
        b = self.blank(path)
        best = None
        for m in re.finditer(r"\b(\w+)::~?(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?::[^;{}]*)?\{", b, 0):
            if m.start() > pos:
                break
            best = m.group(1)
        if best:
            return best
        for m in re.finditer(r"\b(?:class|struct)\s+(?:__declspec\s*\([^)]*\)\s*)?(\w+)[^;{]*\{", b):
            if m.start() > pos:
                break
            best = m.group(1)
        return best or Path(path).name.split(".")[0]


def natural_size(t, sizes):
    """In-memory size of a field type (not its stack size), or None."""
    t = re.sub(r"\s+", " ", S.QUAL.sub("", t)).strip()
    last = t.split("::")[-1]
    one = {"bool", "char", "int8_t", "uint8_t", "signed char", "unsigned char", "BYTE"}
    two = {"short", "int16_t", "uint16_t", "unsigned short", "wchar_t", "WORD", "char16_t"}
    if t in one or last in one:
        return 1
    if t in two or last in two:
        return 2
    if last in sizes:
        return sizes[last]
    if last in S.TYPE_SIZES:
        return S.TYPE_SIZES[last]
    if t in S.EIGHT or last in S.EIGHT:
        return 8
    if t in S.FOUR or last in S.FOUR or t in ("unsigned int", "unsigned long", "long int"):
        return 4
    return None


# ---------------------------------------------------------------- call sites

def split_parens(e):
    e = e.strip()
    while e.startswith("(") and e.endswith(")"):
        depth = 0
        for i, ch in enumerate(e):
            depth += ch == "("
            depth -= ch == ")"
            if depth == 0 and i < len(e) - 1:
                return e
        e = e[1:-1].strip()
    return e


def top_level(e, ops):
    """True if e has one of ops outside parentheses / brackets / template-looking angle brackets."""
    depth = 0
    i = 0
    while i < len(e):
        ch = e[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif depth == 0:
            for op in ops:
                if e.startswith(op, i):
                    return True
        i += 1
    return False


class Sites:
    def __init__(self, src, classes, facts, aliases, per_file_alias, returns=None):
        self.src, self.classes, self.facts = src, classes, facts
        self.aliases, self.per_file_alias = aliases, per_file_alias
        self.returns = returns or {}

    def fn_type(self, text, path, pos=None):
        """The function type an Fn template argument names. A file may define the same local alias (`using Slot4 =`)
        in several functions with different signatures, so the nearest definition before pos wins over the per-file
        map (which keeps one)."""
        t = text.strip()
        if pos is not None and re.fullmatch(r"\w+", t):
            body = self.src.blank(path)[:pos]
            last = None
            for m in re.finditer(r"\busing\s+%s\s*=\s*([^;]+);|\btypedef\s+([^;]*\(\s*__\w+\s*\*\s*%s\s*\)[^;]*);"
                                 % (re.escape(t), re.escape(t)), body):
                last = m
            if last is not None:
                if last.group(1) is not None:
                    t = last.group(1).strip()
                else:
                    t = re.sub(r"\(\s*(__\w+)\s*\*\s*%s\s*\)" % re.escape(text.strip()), r"(\1 *)", last.group(2))
        return S.resolve_type(t, Path(path), self.aliases, self.per_file_alias)

    def type_bytes(self, t):
        """(lo, hi) stack bytes of a type text; hi None: unbounded."""
        sz = self.src.size(t)
        if sz is not None:
            return sz, sz
        return 4, None

    def result_type(self, e, path, pos=None):
        """The result type text of a slot call expression e (RawSlot / VirtualCall), or None."""
        m = re.match(r"(RawSlot|VirtualCall)\s*<", e)
        if not m:
            return None
        targs, j = SC.template_args(e, m.end() - 1)
        if not targs or j >= len(e) or e[j] != "(":
            return None
        _, k = SC.call_args(e, j)
        if e[k:].strip():
            return None  # more after the call: an expression using it
        if m.group(1) == "RawSlot":
            return targs[1] if len(targs) > 1 else "uint32_t"
        fn = S.parse_fn(self.fn_type(targs[0], path, pos))
        return fn[1] if fn else None

    def arg_bytes(self, e, path, pos):
        """(lo, hi) stack bytes of an argument expression as a template parameter pack deduces it (by value)."""
        e = split_parens(e)
        if not e:
            return 4, None
        r = self.result_type(e, path, pos)
        if r is not None:
            return self.type_bytes(r)
        m = re.match(r"(?:std::)?(static_cast|reinterpret_cast|const_cast|bit_cast)\s*<", e)
        if m:
            targs, j = SC.template_args(e, m.end() - 1)
            if targs and j < len(e) and e[j] == "(":
                _, k = SC.call_args(e, j)
                if not e[k:].strip():
                    return self.type_bytes(targs[0])
        if e in ("nullptr", "NULL", "true", "false", "this") or e[0] in "\"'" or re.match(r"[LuU]8?\"", e):
            return 4, 4
        if re.fullmatch(r"&\s*[^&].*", e, re.S) and not top_level(e[1:], ["?", "+", "-", "|", "^", "<<", ">>"]):
            return 4, 4
        if re.match(r"(?:sizeof|alignof|offsetof)\b", e):
            return 4, 4
        if top_level(e, ["==", "!=", "<=", ">=", "&&", "||"]) or e.startswith("!"):
            return 4, 4
        if top_level(e, ["?"]):
            return self.ternary(e, path, pos)
        m = FLOAT_LIT.fullmatch(e)
        if m:
            return (4, 4) if m.group(1) in ("f", "F") else (8, 8)
        m = INT_LIT.fullmatch(e)
        if m:
            sfx = m.group(1).lower()
            return (8, 8) if "ll" in sfx or "i64" in sfx else (4, 4)
        m = re.fullmatch(r"\(\s*((?:const\s+)?[\w:]+(?:\s*<[^()]*>)?\s*[*&]*)\s*\)\s*([\w(&*\-+~!].*)", e, re.S)
        if m and self.src.is_type(m.group(1).split("<")[0].replace("const", "").strip(" *&"), self.classes):
            return self.type_bytes(m.group(1))
        m = re.fullmatch(r"(\*?)\s*((?:\w+\s*(?:\.|->)\s*)*)(\w+)\s*(\[[^\]]*\])?", e)
        if m:
            name = m.group(3)
            d = self.src.declared(name, path, pos, self.classes)
            if d is None:
                return 4, None
            ty, array, _, _, _ = d
            if (array and not m.group(4)) or (array and m.group(1)):
                return 4, 4  # an array decays to a pointer
            if m.group(4) or m.group(1):
                if array:
                    ty = ty  # the element
                elif ty.rstrip().endswith("*"):
                    ty = ty.rstrip()[:-1]
                else:
                    return 4, None  # an operator[] / operator* of a class
            return self.type_bytes(ty)
        # a call or arithmetic: a scalar result unless it is a call of something returning a struct
        if re.search(r"[\w>)\]]\s*\(", e) and not top_level(e, ["+", "-", "*", "/", "%", "|", "^", "<<", ">>", "&"]):
            m = re.fullmatch(r"(?:[\w:]+(?:<[^()]*>)?\s*(?:\.|->|::)\s*|[\w:]+\([^()]*\)\s*(?:\.|->)\s*)*(~?\w+)\s*"
                             r"(?:<[^()]*>)?\s*\((.*)\)", e, re.S)
            if m and m.group(1) in self.returns:
                sizes = {self.type_bytes(r) for r in self.returns[m.group(1)]}
                if len(sizes) == 1:
                    return next(iter(sizes))
            return 4, None
        return 4, 8

    def ternary(self, e, path, pos):
        depth, q = 0, None
        for i, ch in enumerate(e):
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
            elif ch == "?" and depth == 0 and q is None:
                q = i
            elif ch == ":" and depth == 0 and q is not None and e[i - 1] != ":" and e[i + 1:i + 2] != ":":
                a = self.arg_bytes(e[q + 1:i], path, pos)
                b = self.arg_bytes(e[i + 1:], path, pos)
                return max(a[0], b[0]), (None if a[1] is None or b[1] is None else max(a[1], b[1]))
        return 4, None

    def find(self, path):
        """[site dict] for the slot calls in a file."""
        text = self.src.blank(path)
        raw = self.src.files[path]
        consts = {}
        for name, val in re.findall(r"\b(k\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\b", raw):
            consts.setdefault(name, int(val, 0))
        out = []
        for m in re.finditer(r"\b(RawSlot|VirtualCall|VirtualSlot)\s*<", text):
            form = m.group(1)
            if text[max(0, m.start() - 1)] in ":." or re.match(r"\s*(?:int|typename|class|auto)\b", text[m.end():]):
                continue
            targs, i = SC.template_args(text, m.end() - 1)
            if not targs or i >= len(text) or text[i] != "(":
                continue
            args, j = SC.call_args(text, i)
            if args is None:
                continue
            site = {"path": path, "pos": m.start(), "line": text.count("\n", 0, m.start()) + 1, "form": form,
                    "end": j}
            if form == "RawSlot":
                slot = SC.slot_value(targs[0], consts)
                if slot is None or not args:
                    continue
                site.update(slot=slot, obj=args[0], args=args[1:], conv="thiscall",
                            ret=targs[1] if len(targs) > 1 else "uint32_t", fn=None)
            elif form == "VirtualCall":
                if len(targs) < 2 or not args:
                    continue
                slot = SC.slot_value(targs[-1], consts)
                fn = S.parse_fn(self.fn_type(targs[0], path, m.start()))
                if slot is None:
                    continue
                site.update(slot=slot, obj=args[0], args=args[1:], fn=fn, fn_text=targs[0], member=True)
            else:
                if len(args) != 2 or j >= len(text) or text[j] != "(":
                    continue
                slot = SC.slot_value(args[1], consts)
                more, j2 = SC.call_args(text, j)
                if slot is None or more is None:
                    continue
                fn = S.parse_fn(self.fn_type(targs[0], path, m.start()))
                site.update(slot=slot, obj=args[0], args=more[1:] if more else [], fn=fn, fn_text=targs[0],
                            member=False, end=j2)
            site["obj"] = " ".join(site["obj"].split())
            out.append(site)
        return out

    def want(self, site):
        """(lo, hi, how) stack bytes the call passes; lo None: not known at all."""
        import arity
        path, pos = site["path"], site["pos"]
        if site["form"] == "RawSlot":
            lo, hi = 0, 0
            for a in site["args"]:
                x, y = self.arg_bytes(a, path, pos)
                lo += x
                hi = None if hi is None or y is None else hi + y
            hidden = arity.hidden_return(site["ret"], True)
            if hidden is None:
                return None, None, f"result type {site['ret']} of unknown kind"
            extra = 4 if hidden else 0
            return lo + extra, (None if hi is None else hi + extra), f"{len(site['args'])} argument(s)"
        fn = site["fn"]
        if fn is None:
            return None, None, f"Fn not parsed: {site.get('fn_text', '')[:80]}"
        conv, ret, params = fn
        if conv == "cdecl":
            return 0, 0, "cdecl"
        lo, hi = 0, 0
        ps = params[1:] if conv == "thiscall" else params
        if conv == "fastcall":
            return None, None, "fastcall slot"
        for p in ps:
            x, y = self.type_bytes(re.sub(r"=.*$", "", p))
            lo += x
            hi = None if hi is None or y is None else hi + y
        hidden = arity.hidden_return(ret, site["member"])
        if hidden is None:
            return None, None, f"result type {ret} of unknown kind"
        extra = 4 if hidden else 0
        return lo + extra, (None if hi is None else hi + extra), f"{len(ps)} parameter(s)"

    # ------------------------------------------------------------ receivers

    def receiver(self, site):
        """(key, type name or None, how) of the call's object."""
        path, pos = site["path"], site["pos"]
        o = split_parens(site["obj"])
        hint = self.src.hint(path, (pos, site["end"]))
        cast = None
        m = re.fullmatch(r"(?:static_cast|reinterpret_cast|const_cast)\s*<\s*(.+?)\s*>\s*\((.*)\)", o, re.S)
        if m:
            cast = class_name(m.group(1), self.aliases_plain())
            o = split_parens(m.group(2))
        m = re.fullmatch(r"(.*?)\s*(?:\.|->)\s*(?:Get|get|AsPPVoid)\s*\(\s*\)", o, re.S)
        if m:
            o = m.group(1).strip()
        addr = o.startswith("&")
        o = o.lstrip("&*").strip()
        key = re.sub(r"\s+", "", o)
        if hint == "none":
            return key, None, "none"
        if hint:
            return key, hint, "hint"
        if cast and cast not in UNTYPED:
            return key, cast, "cast"
        if o == "this":
            return "this", self.src.enclosing_class(path, pos), "this"
        m = re.fullmatch(r"(?:\w+\s*(?:\.|->)\s*)*(\w+)", o)
        if not m:
            return key, None, "expression"
        head = re.match(r"(\w+)\s*(?:\.|->)", o)
        if head:
            # a member: the same member name on another struct type is another object (two job structs' ref24)
            hd = self.src.declared(head.group(1), path, pos, self.classes) if head.group(1) != "this" else None
            site["owner"] = self.src.enclosing_class(path, pos) if head.group(1) == "this" else \
                (re.sub(r"\b(?:const|volatile|struct|class)\b|[\s*&]", "", hd[0]) if hd else None)
        d = self.src.declared(m.group(1), path, pos, self.classes)
        if d is None:
            return key, None, "no declaration"
        ty, array, span, init, where = d
        if where == path and re.fullmatch(r"\w+", o):
            site["decl"] = span[0]  # a local or parameter: the same name elsewhere in the file is another object
        h = self.src.hint(where, span)
        if h:
            return (key, None, "none") if h == "none" else (key, h, "hint")
        t = class_name(ty, self.aliases_plain(), init)
        if t and t.startswith("ifaces::"):
            # the interface's own vtables (as a base, at its offset in each class) when the RTTI names it; the
            # implementing class's offset-0 vtable is another interface when it isn't the primary base
            bare = t.split("::")[-1]
            t = bare if self.classes.known(bare) else self.iface_class(bare)
        if t is None or t in UNTYPED:
            return key, None, f"declared {ty.strip()}"
        if addr and ty.rstrip().endswith("*"):
            return key, None, f"address of a pointer ({ty.strip()})"
        return key, t, "type"

    def aliases_plain(self):
        return S.TYPE_ALIASES

    def iface_class(self, iface):
        cls = {f.get("class") for f in self.facts if f.get("iface") == iface and f.get("class")}
        return next(iter(cls)) if len(cls) == 1 else None


def class_name(ty, aliases, init=None):
    """The class a declared type text points at (or is): `T *`, `T &`, `cRZAutoRefCount<T>`, an alias of one,
    `auto` with a cast / As<> / new initializer; None for scalars and unknowns."""
    t = re.sub(r"\b(const|volatile|static|constexpr|struct|class|inline)\b", " ", ty)
    t = re.sub(r"\s+", " ", t).strip()
    for _ in range(4):
        bare = t.rstrip(" *&").split("::")[-1]
        if bare in aliases and aliases[bare] != bare and "(" not in aliases[bare]:
            t = aliases[bare] + t[len(t.rstrip(" *&")):]
        else:
            break
    if re.fullmatch(r"auto\s*[*&]*", t):
        if not init:
            return None
        m = re.match(r"\s*(?:static_cast|reinterpret_cast|const_cast)\s*<\s*([^<>]+?)\s*\*?\s*>", init) or \
            re.match(r"\s*As\s*<\s*([\w:]+)\s*>", init) or re.match(r"\s*new\s*(?:\([^)]*\))?\s*([\w:]+)", init)
        return m.group(1).strip().rstrip("*").strip() if m else None
    m = re.fullmatch(r"([\w:]+)\s*<\s*([\w:]+)\s*\*?\s*>\s*[*&]*", t)
    if m:
        return m.group(2) if re.search(r"Ref|Ptr|ptr", m.group(1)) else None
    m = re.fullmatch(r"([\w:]+)\s*[*&]*", t)
    if not m:
        return None
    name = m.group(1)
    if name.split("::")[-1] in SCALAR_WORDS or name in S.FOUR or name in S.EIGHT:
        return None
    return name


# ---------------------------------------------------------------- verdicts

def fits(p, lo, hi):
    return p >= lo and (hi is None or p <= hi)


def check(files, report, rets, classes, facts, aliases, per_file_alias, enums, returns=None):
    """Findings (arity.check's dicts, kind "slot call") for the slot calls in the `report` files (None: all)."""
    vt = Vtables(classes, rets)
    src = Source(files, enums)
    sites = Sites(src, vt, facts or [], aliases, per_file_alias, returns)
    paths = [p for p, _ in files if (report is None or p in report) and (p.endswith(".cpp") or p.endswith(".h"))]
    found = []
    for path in paths:
        if "RawSlot" not in src.files[path] and "VirtualCall" not in src.files[path] and \
                "VirtualSlot" not in src.files[path]:
            continue
        if path.endswith("src/game/vtable.h"):
            continue
        calls = sites.find(path)
        groups = defaultdict(list)
        text = src.blank(path)
        for c in calls:
            c["lo"], c["hi"], c["how"] = sites.want(c)
            c["key"], c["type"], c["by"] = sites.receiver(c)
            c["text"] = re.sub(r"\s+", "", text[c["pos"]:c["end"]])
            typed = c["by"] in ("hint", "type", "cast", "this")
            # an untyped local or parameter is grouped with the calls on the same declaration only, and a member with
            # those on the same struct type: the same name elsewhere in the file is often another kind of object (a
            # file-wide pool by name made false findings)
            scope = (None, None) if typed else (c.get("decl"), c.get("owner"))
            groups[(c["key"], c["type"] if typed else None) + scope].append(c)
        # a slot call that is an argument of another on the same receiver: MSVC pushes the outer call's trailing
        # arguments before the inner call, so the split between the two is what goes wrong; for the inference their
        # sum is one constraint (the split is then judged per call)
        for cs in groups.values():
            texts = {c["text"]: c for c in cs}
            for c in cs:
                for a in c["args"]:
                    inner = texts.get(re.sub(r"\s+", "", split_parens(a)))
                    if inner is not None and inner is not c:
                        inner["outer"] = c
                        c["inner"] = inner
        wide = defaultdict(list)  # the untyped locals and parameters of each name, every declaration
        for c in calls:
            if "decl" in c and c["type"] is None:
                wide[c["key"]].append(c)
        for cs in groups.values():
            for c in cs:
                found.append(verdict(c, cs, vt, facts or [], wide.get(c["key"]) if "decl" in c and c["type"] is None
                                     else None))
    return found


def universe(vt, t):
    return vt.of(t) if t else None


def units(group):
    """{unit: weight} for the inference: (slot, lo, hi) for a call on its own, ((inner slot, outer slot), lo, hi) for a
    nested pair on the same receiver (their summed bytes, weight 2)."""
    out = {}
    for c in group:
        if c["lo"] is None or "outer" in c:
            continue
        inner = c.get("inner")
        if inner is not None and inner["lo"] is not None:
            hi = None if c["hi"] is None or inner["hi"] is None else c["hi"] + inner["hi"]
            out[((inner["slot"], c["slot"]), c["lo"] + inner["lo"], hi)] = 2
        else:
            out[(c["slot"], c["lo"], c["hi"])] = 1
    return out


def unit_ok(vt, k, unit):
    slot, lo, hi = unit
    if isinstance(slot, tuple):
        a, b = vt.pops(k, slot[0]), vt.pops(k, slot[1])
        return a >= 0 and b >= 0 and fits(a + b, lo, hi)
    p = vt.pops(k, slot)
    return p >= 0 and fits(p, lo, hi)


def best_fit(vt, group, pool):
    """(score, [vtable indices], score Counter, candidates, total weight) of the vtables in pool (None: all) that
    satisfy the group's calls best (units()); a vtable lacking a slot the receiver is called at is out."""
    us = units(group)
    slots = {c["slot"] for c in group}
    if not us:
        return 0, [], Counter(), set(), 0
    alive = None
    for slot in slots:
        col = vt.column(slot)
        have = set().union(*col.values()) if col else set()
        alive = have if alive is None else alive & have
    if pool is not None:
        alive = alive & pool
    score = Counter()
    for unit, w in us.items():
        slot, lo, hi = unit
        if isinstance(slot, tuple):
            col = vt.column(slot[1])
            for p, ks in col.items():
                if p >= 0:
                    for k in ks & alive:
                        if unit_ok(vt, k, unit):
                            score[k] += w
        else:
            for p, ks in vt.column(slot).items():
                if p >= 0 and fits(p, lo, hi):
                    for k in ks & alive:
                        score[k] += w
    if not alive:
        return 0, [], score, alive, sum(us.values())
    best = max(score.get(k, 0) for k in alive)
    return best, sorted(k for k in alive if score.get(k, 0) == best), score, alive, sum(us.values())


def verdict(c, group, vt, facts, wide=None):
    where = f"{c['path']}:{c['line']} {c['form']}<{c['slot']}> on {c['obj'][:40]}"
    out = dict(kind="slot call", where=where, rva=None)
    lo, hi = c["lo"], c["hi"]
    passes = (f"{lo} bytes" if hi == lo else f"{lo}..{hi if hi is not None else ''} bytes") if lo is not None else ""
    if lo is None:
        return dict(out, verdict="unknown", detail=f"what the call passes: {c['how']}")
    if c["by"] == "none":
        return dict(out, verdict="unknown", detail=f"`// receiver: none` (its class isn't in db/classes.jsonl, or the "
                    f"inference was wrong): answer it in the lane report; passes {passes}")
    t = c["type"] if c["by"] in ("hint", "type", "cast", "this") else None
    pool = universe(vt, t)
    if t and not pool:
        fact = fact_pops(facts, t, c["slot"])
        if fact is not None:
            return against_fact(out, fact, lo, hi, passes, t, c)
        t, pool = None, None  # not a class of the image's RTTI: infer it like an untyped receiver
        c = dict(c, by=f"{c['by']} {c['type']}, not in db/classes.jsonl")
    if pool and c["by"] == "this":
        # a member hooked at a secondary vtable gets that subobject as `this`: judge only if they all agree
        mine = {vt.pops(k, c["slot"]) for k in pool} - {UNKNOWN, ABSENT}
        subs = {k for k in vt.subobjects(t) if vt.pops(k, c["slot"]) >= 0 and vt.pops(k, c["slot"]) not in mine}
        if mine and subs:
            k = min(subs)
            return dict(out, verdict="unknown", detail=f"{t} (this): slot {c['slot']} pops {sorted(mine)} at offset 0 "
                        f"but {vt.pops(k, c['slot'])} in {vt.name(k)}, and `this` may be that subobject; passes "
                        f"{passes}")
    if pool:
        have = {k: vt.pops(k, c["slot"]) for k in pool}
        known = {p for p in have.values() if p >= 0}
        if have and all(p == ABSENT for p in have.values()):
            return dict(out, verdict="unknown", detail=f"{t} ({c['by']}) has no slot {c['slot']}; passes {passes}")
        if len(known) == 1:
            p = next(iter(known))
            k = next(k for k, q in have.items() if q == p)
            rva = vt.entries[k][c["slot"]]
            n = sum(1 for q in have.values() if q == p)
            label = f"{t} ({c['by']}): slot {c['slot']} of {n} vtable(s), e.g. {vt.name(k)} 0x{rva:x}"
            return judge(dict(out, rva=f"0x{rva:x}"), p, lo, hi, passes, label)
        if not known:
            fact = fact_pops(facts, t, c["slot"])
            if fact is not None:
                return against_fact(out, fact, lo, hi, passes, t, c)
            return dict(out, verdict="unknown", detail=f"{t} ({c['by']}): no ret found at slot {c['slot']}; passes "
                        f"{passes}")
        if t in UNTYPED:
            pool = None
    # infer the class from every call on the same receiver: the same declaration first, then (an untyped local or
    # parameter whose calls don't decide) every declaration of the name in the file
    final, got = infer(c, group, vt, pool, t, out, lo, hi, passes, None)
    if final or not wide or len(wide) == len(group):
        return got
    final, wider = infer(c, wide, vt, pool, t, out, lo, hi, passes, [x for x in wide if x.get("decl") != c.get("decl")])
    return wider if final else got


def infer(c, group, vt, pool, t, out, lo, hi, passes, others):
    """(decided, finding) of the class inference over group; decided False: the calls don't identify a class.
    others: the calls of the group on other declarations of the name; each of them must fit the vtable inferred."""
    fit = best_fit(vt, group, pool)
    reason = f"receiver's class not known ({c['by']})" if not t else f"{t}'s vtables disagree at slot {c['slot']}"
    where = f"`{c['key']}` here" if others is None else f"`{c['key']}` (every declaration in the file)"
    if not fit[1]:
        return False, dict(out, verdict="unknown", detail=f"{reason}; no vtable has every slot the calls on {where} "
                           f"use; passes {passes}")
    best, B, score, alive, total = fit
    if best < MIN_FIT:
        return False, dict(out, verdict="unknown", detail=f"{reason}; the calls on {where} don't identify a class "
                           f"(best fit {best} of {total}); passes {passes}")
    if others:
        rest = units(others)
        B = [k for k in B if all(unit_ok(vt, k, u) for u in rest)]
        if not B:
            return False, dict(out, verdict="unknown", detail=f"{reason}; the calls on {where} don't agree on a "
                               f"class; passes {passes}")
    ps = {vt.pops(k, c["slot"]) for k in B}
    if len(ps) != 1 or next(iter(ps)) < 0:
        return False, dict(out, verdict="unknown", detail=f"{reason}; the {len(B)} best-fitting vtables (fit {best} "
                           f"of {total} for the calls on {where}, e.g. {vt.name(B[0])}) disagree or have no ret at slot "
                           f"{c['slot']}; passes {passes}")
    got = decide(c, vt, B, best, score, alive, total, out, lo, hi, passes, where)
    if got["verdict"] == "MISMATCH":
        weak = weakness(c, group, vt, B[0], best, total, others)
        if weak:
            got = dict(got, verdict="unknown", detail=f"probable mismatch ({weak}): {got['detail']}")
    return True, got


def weakness(c, group, vt, k, best, total, others):
    """Why an inferred mismatch is only probable (reported unknown, for the lane to answer), or None. The image's RTTI
    doesn't have every class, and a member or a reused name can hold different objects, so an inference is an error
    only when the calls on the one declaration agree on it."""
    if "decl" not in c:
        return "a member or an expression: it may hold different objects at different calls"
    if total - best > max(1, total // 5):
        return f"only {best} of {total} of the calls fit the class"
    if others is not None:
        own = [x for x in group if x.get("decl") == c.get("decl") and x is not c and x is not c.get("inner")
               and x is not c.get("outer")]
        if sum(w for u, w in units(own).items() if unit_ok(vt, k, u)) < 2:
            return "the class is from the name's other declarations; this one's other calls don't confirm it"
    return None


def decide(c, vt, B, best, score, alive, total, out, lo, hi, passes, where):
    p = vt.pops(B[0], c["slot"])
    k = B[0]
    rva = vt.entries[k][c["slot"]]
    label = (f"inferred {vt.name(k)}{f' and {len(B) - 1} other vtable(s)' if len(B) > 1 else ''} (fits {best} of "
             f"{total} for the calls on {where}): slot {c['slot']} 0x{rva:x}")
    out["rva"] = f"0x{rva:x}"
    if fits(p, lo, hi):
        return judge(out, p, lo, hi, passes, label)
    agree = [score.get(x, 0) for x in alive if vt.pops(x, c["slot"]) >= 0 and fits(vt.pops(x, c["slot"]), lo, hi)]
    alt = max(agree) if agree else 0
    if best - alt < MIN_LEAD:
        return dict(out, verdict="unknown", detail=f"{label} pops {p}, but a vtable popping what the call passes fits "
                    f"{alt} (too close to call); passes {passes}")
    return judge(out, p, lo, hi, passes, label + f"; the best vtable agreeing with this call fits {alt}")


def judge(out, p, lo, hi, passes, label):
    """ok when the ret is what the call passes (an argument of unknown size taken as a dword: ok only if that is
    what makes it agree), MISMATCH when no size in the range agrees, else unknown."""
    if fits(p, lo, hi):
        if p == lo:
            return dict(out, verdict="ok", detail=f"{label}: ret {p}; passes {passes}")
        return dict(out, verdict="unknown", detail=f"{label}: ret {p}; passes {passes} (agrees only if an argument "
                    "of unknown size is wider than a dword)")
    return dict(out, verdict="MISMATCH", detail=f"{label}: original ret {p} ({p // 4} dwords), the call passes {passes}"
                f" ({lo // 4}{'' if hi == lo else '+'} dwords)")


def fact_pops(facts, t, slot):
    """(pops, tentative, fact) for a db/methods fact of class or interface t at slot, or None."""
    bare = t.split("::")[-1]
    hits = [f for f in facts if f.get("slot") == slot and (f.get("class") == bare or f.get("iface") == bare)]
    if not hits:
        return None
    got = set()
    for f in hits:
        n = 0
        for p in f.get("params", []):
            sz = S.METHOD_SIZES.get(p.get("type"))
            if sz is None:
                n = None
                break
            n += sz
        if n is not None:
            got.add((n, bool(f.get("tentative"))))
    if len({g[0] for g in got}) != 1:
        return None
    n = next(iter(got))[0]
    return n, all(g[1] for g in got if g[0] == n), hits[0]


def against_fact(out, fact, lo, hi, passes, t, c):
    n, tentative, f = fact
    label = f"{t}'s method fact {f.get('iface')}::{f.get('name')} (slot {c['slot']}{', tentative' if tentative else ''})"
    r = judge(out, n, lo, hi, passes, label)
    if r["verdict"] == "MISMATCH" and tentative:
        r = dict(r, verdict="unknown", detail=r["detail"] + " (against a tentative fact: check the body)")
    return r


def load_facts():
    import tsdb
    out = []
    for p in sorted((tsdb.ROOT / "db/methods").glob("*.jsonl")):
        for line in p.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line:
                try:
                    out.append(json.loads(line))
                except ValueError:
                    pass
    return out
