"""Finds an existing helper, type, constant or test fake by what it does: one line per declaration, best first.

Searches the declarations in src/**/*.h, tests/*.h and tests/*.cpp (functions, methods, classes / structs / enums,
aliases, templates, constants, globals, macros, DIFF_TESTs, and each file's header comment) by their name (split on
case, digits and underscores) and the comment above or beside them. Name matches rank first. Before grepping src/ or
tests/ for something that may already exist, ask this.

Usage: tools/py -I tools/symfind.py walk the message ids and call slot 5     # words, any order
       tools/py -I tools/symfind.py director framework --tests               # only tests/ (fakes, stand-ins, helpers)
       tools/py -I tools/symfind.py 0x1c0458                                 # the declarations naming an RVA, its hook
  --all      every match, not the best 15                                     #   entry and its status line
  --src / --tests   restrict to src/ or tests/
  --fields   also data members (off by default: tools/digest.py lists a class's members)
  --kind K   only one kind: function, method, type, alias, const, var, macro, test, file, field

Each line: `path:line  signature  // the comment's first sentence` (a leading RVA / slot / offset tag dropped).
The index is a regex parser (a search aid, not a compiler: a declaration it misses is a miss), cached in
build/symfind-cache.pickle by file size and mtime; tools/digest.py uses the same parser.
"""
import argparse
import bisect
import json
import pickle
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "build/symfind-cache.pickle"
PARSER_VERSION = 10  # bump when parse() changes: the cache is rebuilt
GLOBS = ("src/**/*.h", "tests/*.h", "tests/*.cpp", "harness/*.h")  # harness/: the difftest helpers tests use
HOOK_ENTRY = re.compile(r'^\s*\{\s*"[^"]*",\s*0[xX]([0-9a-fA-F]+)\s*,(.*)$')
HEX = re.compile(r"\b0[xX]([0-9a-fA-F]{4,8})\b")

# ---------------------------------------------------------------- comments and strings

_LEX = re.compile(r'//[^\n]*|/\*.*?\*/|R"([^(\s"]{0,16})\(.*?\)\1"|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'',
                  re.S)
_PREPRO = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", re.M)
_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)(\([^)]*\))?")


def lex(text):
    """(code, comments): `code` is `text` with comments blanked and string / char contents blanked (quotes kept),
    preprocessor lines blanked, same length and newlines; `comments` {line: [comment text, ...]} (1-based lines)."""
    out = []
    comments = {}
    last = 0
    line_starts = _line_starts(text)
    for m in _LEX.finditer(text):
        s = m.group(0)
        out.append(text[last:m.start()])
        if s.startswith("//") or s.startswith("/*"):
            line = bisect.bisect_right(line_starts, m.start())
            body = s[2:] if s.startswith("//") else s[2:-2]
            for k, part in enumerate(body.split("\n")):
                part = part.strip()
                if s.startswith("/*"):
                    part = part.lstrip("*").strip()
                comments.setdefault(line + k, []).append(part.lstrip("/").strip())
            out.append(re.sub(r"[^\n]", " ", s))
        else:
            q = s[-1]
            head = s[:s.index(q) + 1] if q == '"' else q
            out.append(head + re.sub(r"[^\n]", " ", s[len(head):-1]) + q)
        last = m.end()
    out.append(text[last:])
    code = "".join(out)
    return code, comments


def _line_starts(text):
    starts = [0]
    i = text.find("\n")
    while i >= 0:
        starts.append(i + 1)
        i = text.find("\n", i + 1)
    return starts


# ---------------------------------------------------------------- declarations

_ATTRS = re.compile(r"\[\[.*?\]\]|__declspec\s*\([^()]*(?:\([^()]*\)[^()]*)*\)|alignas\s*\([^()]*\)")
_ACCESS = re.compile(r"\b(public|private|protected)\s*:(?!:)")
_OPERATOR = re.compile(r"\boperator\s*(\(\)|\[\]|new\b|delete\b|[^\w\s(]+|\w[\w\s:*&<>]*?(?=\())")
_SKIP_HEADS = re.compile(r"^\s*(using\s+namespace|friend|static_assert|namespace\s+\w+\s*=|template\s+(class|struct)\b|"
                         r"extern\s+template|return|goto|case|default)\b")
_TYPE_HEAD = re.compile(r"^\s*(?:typedef\s+)?(class|struct|union|enum(?:\s+class|\s+struct)?)\b(.*)$", re.S)
_KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof", "decltype", "static_assert", "alignof", "catch",
             "noexcept", "throw", "new", "delete", "alignas", "__declspec", "defined"}
_SPECIFIERS = re.compile(r"\b(static|virtual|inline|explicit|constexpr|consteval|constinit|extern|mutable|friend|"
                         r"__forceinline|typename)\b\s*")


def _blank_same(pattern, s):
    """`s` with every match of `pattern` turned into spaces (offsets kept)."""
    return pattern.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), s)


def strip_template(s):
    """(prefix, rest): a leading `template <...>` (balanced, repeated) split off."""
    prefix = ""
    while True:
        m = re.match(r"\s*template\s*<", s)
        if not m:
            return prefix, s
        depth, i = 1, m.end()
        while i < len(s) and depth:
            depth += {"<": 1, ">": -1}.get(s[i], 0)
            i += 1
        prefix += s[:i]
        s = " " * i + s[i:]


def top_level(s, chars):
    """Indexes of `chars` in `s` outside (), [], {} and <> (`->` and `<<`/`>>` are not angles)."""
    out, depth, angle = [], 0, 0
    i = 0
    while i < len(s):
        c = s[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif depth == 0 and c == "<" and not s.startswith("<<", i) and (i == 0 or s[i - 1] != "<"):
            angle += 1
        elif depth == 0 and c == ">" and angle and (i == 0 or s[i - 1] != "-"):
            angle -= 1
        elif depth == 0 and angle == 0 and c in chars:
            out.append(i)
        i += 1
    return out


def first_top_paren(s):
    """Index of the first `(` at angle depth 0 outside brackets, or -1."""
    depth, angle = 0, 0
    for i, c in enumerate(s):
        if c == "(" and depth == 0 and angle == 0:
            return i
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif depth == 0 and c == "<" and not s.startswith("<<", i) and (i == 0 or s[i - 1] != "<"):
            angle += 1
        elif depth == 0 and c == ">" and angle and (i == 0 or s[i - 1] != "-"):
            angle -= 1
    return -1


def squash(s):
    return " ".join(s.split())


def function_parts(s, class_name):
    """(name, offset of the name, return type) when statement / head `s` (attributes blanked, template prefix
    blanked) declares a function, else None."""
    probe = _OPERATOR.sub(lambda m: "operator" + re.sub(r"\S", "_", m.group(0)[8:]).replace(" ", "_"), s)
    p = first_top_paren(probe)
    if p < 0:
        return None
    before = probe[:p]
    if top_level(before, "="):
        return None
    m = re.search(r"(~?[A-Za-z_]\w*(?:\s*::\s*~?[A-Za-z_]\w*)*)\s*$", before)
    if not m:
        return None
    name = re.sub(r"\s+", "", s[m.start(1):m.end(1)])
    last = name.split("::")[-1]
    if last in _KEYWORDS:
        return None
    ret = squash(_SPECIFIERS.sub("", before[:m.start(1)]))
    if not ret:
        parts = name.split("::")
        ctor = last.startswith("~") or last == class_name or (len(parts) > 1 and parts[-1].lstrip("~") == parts[-2])
        if not ctor and not last.startswith("operator"):  # `explicit operator bool()` has no return type
            return None  # a macro use (TS2_OWNED_STATIC(...)) or something else without a type
    if re.fullmatch(r"[*&\s]*", s[p + 1:p + 3]) is None and re.match(r"\(\s*(__\w+\s+)?[*&]", s[p:]):
        return None  # `void (*mFn)(int)`: a pointer, not a function
    return name, m.start(1), ret


def pointer_name(s):
    """The declared name of a function-pointer declarator `ret (cc *name)(args)` / `(*name[3])(...)`, or None."""
    m = re.search(r"\(\s*(?:__\w+\s+)?[*&]\s*(?:const\s+)?([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*\)\s*\(", s)
    return m


def declarators(s):
    """[(name, offset, type)] of a variable / field statement `s` (no trailing `;`)."""
    s = re.sub(r"\{[^{}]*\}", lambda m: " " * len(m.group(0)), s)
    pm = pointer_name(s)
    if pm:
        return [(pm.group(1), pm.start(1), squash(s[:pm.start(1)] + s[pm.end(1):]))]
    parts, prev = [], 0
    for i in top_level(s, ",") + [len(s)]:
        parts.append((prev, s[prev:i]))
        prev = i + 1
    out, base_type = [], None
    for k, (off, part) in enumerate(parts):
        eq = top_level(part, "=")
        decl = part[:eq[0]] if eq else part
        colon = [i for i in top_level(decl, ":") if decl[i - 1:i] != ":" and decl[i + 1:i + 2] != ":"]
        if colon:
            decl = decl[:colon[0]]  # a bit-field
        m = re.search(r"([A-Za-z_]\w*)\s*((?:\[[^\]]*\]\s*)*)$", decl)
        if not m or m.group(1) in _KEYWORDS:
            return out
        if k == 0:
            base_type = decl[:m.start(1)]
            if not squash(_SPECIFIERS.sub("", base_type)).strip("*& "):
                return []
            typ = squash(base_type)
        else:
            typ = squash(re.sub(r"[*&]", "", base_type or "") + " " + decl[:m.start(1)])
        typ = squash(typ + " " + m.group(2)) if m.group(2) else typ
        out.append((m.group(1), off + m.start(1), typ))
    return out


RVA_PARAGRAPH = re.compile(r"[+]?0x[0-9a-fA-F]+(?:\s+(?:slot|thunk|body|folded|ret)\b|\s*[:(,])")
HEADING = re.compile(r"-{3,}[^-]*?-{3,}|={3,}[^=]*?={3,}")  # `---- the maps ----`: a section's title, not a comment
IDENT = re.compile(r"[A-Za-z_]\w*")


class Parser:
    """One file's declarations: dicts with kind, name, scope ('ns::cls'), sig, type, line, above, trail."""

    def __init__(self, text):
        self.text = text
        self.code, self.comments = lex(text)
        self.starts = _line_starts(text)
        self.decls = []
        self.scopes = [("ns", "")]

    def line(self, off):
        return bisect.bisect_right(self.starts, off)

    def code_line(self, n):
        a = self.starts[n - 1]
        b = self.starts[n] if n < len(self.starts) else len(self.code)
        return self.code[a:b]

    def above(self, n):
        """The contiguous comment-only lines right above line n, joined."""
        lines = []
        k = n - 1
        while k >= 1 and k in self.comments and not self.code_line(k).strip():
            lines.append(" ".join(self.comments[k]))
            k -= 1
        if k == 0:
            return ""  # the file's header comment (a `file` entry of its own), not this declaration's
        lines.reverse()
        # A block that opens with a section's text and then an RVA-led paragraph (`0x2dfda0 slot 0 (ret 8): ...`)
        # after a finished sentence: the paragraph is this declaration's.
        for i in range(len(lines) - 1, 0, -1):
            if RVA_PARAGRAPH.match(lines[i]) and re.search(r"[.:;)]$|-{3,}$", lines[i - 1]):
                lines = lines[i:]
                break
        return HEADING.sub(" ", " ".join(lines)).strip()

    def trailing(self, a, b):
        return " ".join(" ".join(self.comments[k]) for k in range(a, b + 1)
                        if k in self.comments and self.code_line(k).strip()).strip()

    def scope(self):
        return "::".join(n for _, n in self.scopes if n)

    def class_name(self):
        kind, name = self.scopes[-1]
        return name if kind == "class" else None

    def add(self, kind, name, off, start, end, sig, typ=""):
        # The declaration's first line is past a `public:` before it: the comment above it is its own.
        seg = _blank_same(_ACCESS, self.code[start:end])
        first = self.line(start + len(seg) - len(seg.lstrip()))
        last = self.line(end)
        d = {"kind": kind, "name": name, "scope": self.scope(), "line": self.line(off), "first": first, "last": last,
             "sig": squash(sig)[:160], "type": typ, "above": self.above(first), "trail": self.trailing(first, last)}
        prev = self.decls[-1] if self.decls else None
        if (not d["above"] and not d["trail"] and prev and prev["scope"] == d["scope"] and prev["kind"] == kind
                and prev["last"] == first - 1 and prev["above"]):
            d["above"], d["shared"] = prev["above"], True  # one comment over a run of declarations
        self.decls.append(d)

    # -- statements

    def statement(self, start, end):
        raw = self.code[start:end]
        s = _blank_same(_ACCESS, _blank_same(_ATTRS, raw))
        if not s.strip() or _SKIP_HEADS.match(s):
            return
        while "{" in s and "=" not in s[:s.find("{")] and "(" not in s[:s.find("{")]:  # a brace initialiser (`cFloat3 mAxis{-1, {1, 0, 0}};`), blanked to keep the offsets
            t = re.sub(r"\{[^{}]*\}", lambda m: " " * len(m.group(0)), s)
            if t == s:
                break
            s = t
        tpl, s = strip_template(s)
        if re.match(r"\s*typedef\b", s):
            pm = pointer_name(s)
            m = pm or re.search(r"([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*$", s)
            if m:
                self.add("alias", m.group(1), start + m.start(1), start, end, raw)
            return
        m = re.match(r"\s*using\s+([A-Za-z_]\w*)\s*=", s)
        if m:
            self.add("alias", m.group(1), start + m.start(1), start, end, tpl + s)
            return
        if re.match(r"\s*using\b", s):
            return
        m = _TYPE_HEAD.match(s)
        if m and "(" not in m.group(2) and "=" not in m.group(2) and not m.group(2).strip().count(" "):
            return  # a forward declaration `class cFoo;`
        cls = self.class_name()
        f = function_parts(s, cls)
        if f:
            name, off, ret = f
            kind = "method" if cls is not None or "::" in name else "function"
            self.add(kind, name, start + off, start, end, tpl + s)
            return
        if re.match(r"\s*[A-Z][A-Z0-9_]*\s*\(", s):
            return  # a macro use
        for name, off, typ in declarators(s):
            konst = re.search(r"\bconstexpr\b|^\s*(static\s+)?const\b", s) is not None
            if cls is not None and not re.search(r"\bstatic\b", s):
                kind = "field"
            else:
                kind = "const" if konst else "var"
            dims = re.search(r"\s*((?:\[[^\]]*\]\s*)+)$", typ)  # `uint8_t mOwner[8]`, not `uint8_t [8] mOwner`
            fsig = typ[:dims.start()] + " " + name + dims.group(1).replace(" ", "") if dims else typ + " " + name
            self.add(kind, name, start + off, start, end, tpl + s if kind != "field" else fsig, typ)

    def head(self, start, brace):
        """What a `{` at `brace` opens: ('ns'|'class'|'enum'|'body'|'init'|'init-continue', name)."""
        raw = self.code[start:brace]
        s = _blank_same(_ACCESS, _blank_same(_ATTRS, raw))
        m = re.fullmatch(r"\s*(?:inline\s+)?namespace\s*([\w:]*)\s*", s)
        if m:
            return "ns", m.group(1)
        if re.fullmatch(r'\s*extern\s*"[^"]*"\s*', s):
            return "ns", ""
        tpl, s2 = strip_template(s)
        m = _TYPE_HEAD.match(s2)
        if m and "(" not in m.group(2).split(":")[0] and "=" not in m.group(2):
            rest = m.group(2)
            cut = [i for i in top_level(rest, ":") if rest[i - 1:i] != ":" and rest[i + 1:i + 2] != ":"]
            names = re.findall(r"[A-Za-z_]\w*(?:\s*<[^{]*>)?", rest[:cut[0]] if cut else rest)
            names = [n for n in names if n.split("<")[0].strip() not in ("final", "sealed")]
            name = names[-1].split("<")[0].strip() if names else ""
            kind = "enum" if m.group(1).startswith("enum") else "class"
            if name:
                sig = squash(tpl + " " + m.group(1) + " " + rest.split("{")[0])
                self.add("type", name, start + m.start(2) + rest.find(name), start, brace, sig)
            return kind, name
        f = function_parts(s2, self.class_name())
        if f:
            tail = s2.rstrip()
            if tail and (tail[-1].isalnum() or tail[-1] in "_>") and re.search(
                    r"\)\s*(?:const\s*|noexcept\s*|override\s*|final\s*|volatile\s*)*:(?!:)", s2):
                return "init-continue", ""
            name, off, ret = f
            m = re.match(r"\s*([A-Z][A-Z0-9_]+)\s*\(\s*([A-Za-z_]\w*)", s2)
            if m and not ret:
                self.add("test" if "TEST" in m.group(1) else "macro-use", m.group(2), start + m.start(2), start, brace,
                         s2)
                return "body", ""
            cls = self.class_name()
            kind = "method" if cls is not None or "::" in name else "function"
            self.add(kind, name, start + off, start, brace, tpl + s2)
            return "body", ""
        return "init", ""

    def run(self):
        code = self.code
        # macros first (their lines are blanked below)
        for m in _PREPRO.finditer(code):
            d = _DEFINE.match(m.group(0))
            if d:
                n = self.line(m.start())
                self.decls.append({"kind": "macro", "name": d.group(1), "scope": "", "line": n, "first": n,
                                   "last": self.line(m.end()), "sig": squash(m.group(0))[:160], "type": "",
                                   "above": self.above(n), "trail": " ".join(self.comments.get(n, []))})
        code = _blank_same(_PREPRO, code)
        self.code = code
        start, depth, skip, cont = 0, 0, 0, False
        owner, body = None, 0
        for m in re.finditer(r"[{};()]", code):
            c, i = m.group(0), m.start()
            if skip:
                if c == "{":
                    skip += 1
                elif c == "}":
                    skip -= 1
                    if skip == 0 and not cont:
                        start = i + 1
                    if skip == 0 and owner is not None:
                        # The identifiers a body uses: a weak signal for helpers nobody commented (`Snapshot` calls
                        # LogWords).
                        owner["body"] = sorted(set(words(" ".join(set(IDENT.findall(code[body:i]))))))[:300]
                        owner = None
                continue
            if c == "(":
                depth += 1
                continue
            if c == ")":
                depth = max(0, depth - 1)
                continue
            if depth:
                continue
            if c == ";":
                self.statement(start, i)
                start = i + 1
            elif c == "{":
                before = len(self.decls)
                kind, name = self.head(start, i)
                if kind == "body" and len(self.decls) > before:
                    owner, body = self.decls[-1], i + 1
                if kind in ("ns", "class"):
                    self.scopes.append((kind, name))
                    start = i + 1
                elif kind == "enum":
                    skip, cont = 1, False
                elif kind == "body":
                    skip, cont = 1, False
                else:  # an initializer or a constructor's member initialiser: the statement goes on
                    skip, cont = 1, True
            elif c == "}":
                if len(self.scopes) > 1:
                    self.scopes.pop()
                start = i + 1
        return self.decls


def file_comment(text):
    """The file's leading comment block (its header doc), joined, or ''."""
    lines = []
    for l in text.splitlines():
        s = l.strip()
        if s.startswith("//"):
            lines.append(s[2:].strip())
        elif s.startswith("#pragma") and not lines:
            continue
        else:
            break
    return " ".join(lines)


def parse(text, path=""):
    decls = Parser(text).run()
    head = file_comment(text)
    if head:
        decls.insert(0, {"kind": "file", "name": "", "scope": "", "line": 1, "first": 1, "last": 1, "sig": "(file)",
                         "type": "", "above": head, "trail": ""})
    for d in decls:
        d["rvas"] = sorted({int(h, 16) for h in HEX.findall(d["above"] + " " + d["trail"] + " " + d["sig"])})
        d["tokens"] = tokens(d, path)
    return decls


FIELDS = ("kind", "name", "scope", "line", "sig", "doc", "rvas", "tokens")


def compact(d):
    """What the index keeps of a declaration, as a tuple in FIELDS' order (30 MB of dicts and sets took two
    seconds to load): the first sentence of its comment rather than the comment, and its word sets as one
    string per part (` load light set `), searched with `in`."""
    return (d["kind"], d["name"], d["scope"], d["line"], d["sig"], first_sentence(comment_of(d)), tuple(d["rvas"]),
            tuple(" " + " ".join(sorted(d["tokens"][part])) + " " for part, _ in WEIGHTS))


def expand(t):
    return dict(zip(FIELDS, t))


# ---------------------------------------------------------------- the index

def source_files(root=ROOT):
    files = []
    for g in GLOBS:
        files += sorted(root.glob(g))
    return files


def load_index(root=ROOT, cache=None):
    """{relative path: [decls]} for the source files, re-parsing only files whose size or mtime changed."""
    cache = cache or (root / "build/symfind-cache.pickle")
    old = {}
    try:
        with open(cache, "rb") as fh:
            saved = pickle.load(fh)
        if saved.get("version") == PARSER_VERSION:
            old = saved["files"]
    except (OSError, EOFError, KeyError, pickle.UnpicklingError, AttributeError):
        pass
    new, todo = {}, []
    for f in source_files(root):
        rel = f.relative_to(root).as_posix()
        st = f.stat()
        key = (st.st_size, st.st_mtime_ns)
        if rel in old and old[rel][0] == key:
            new[rel] = old[rel]
        else:
            todo.append((str(f), rel, key))
    import multiprocessing
    # A cold start: four processes. Never from a worker (a script that calls this outside `if __name__ ==
    # "__main__"` would otherwise spawn workers from its workers).
    if len(todo) > 100 and multiprocessing.parent_process() is None:
        from concurrent.futures import ProcessPoolExecutor
        with ProcessPoolExecutor(max_workers=4) as pool:
            for (path, rel, key), decls in zip(todo, pool.map(parse_file, todo, chunksize=32)):
                new[rel] = (key, decls)
    else:
        for item in todo:
            new[item[1]] = (item[2], parse_file(item))
    if todo or set(old) != set(new):
        try:
            cache.parent.mkdir(exist_ok=True)
            with open(cache, "wb") as fh:
                pickle.dump({"version": PARSER_VERSION, "files": new}, fh, protocol=pickle.HIGHEST_PROTOCOL)
        except OSError:
            pass
    return {rel: v[1] for rel, v in new.items()}


def parse_file(item):
    path, rel, _ = item
    return [compact(d) for d in parse(Path(path).read_text(encoding="utf-8", errors="replace"), rel)]


# ---------------------------------------------------------------- comments to one line

_LEAD_WORD = re.compile(r"[+]?0x[0-9a-fA-F]+(?:\.\.[+]?0x[0-9a-fA-F]+)?|slots?|thunks?|folded|vtable|body|\d+|and|"
                        r"ret|[,/;]")
_OFFSET_LEAD = re.compile(r"^[+]?0x[0-9a-fA-F]+(?:\s*\.\.\s*[+]?0x[0-9a-fA-F]+)?[,:]?\s+")
_SENTENCE_END = re.compile(r"(?<!\be\.g)(?<!\bi\.e)(?<!\betc)\.(?=\s+[A-Z(`\"]|\s*$)")


def strip_lead(t):
    """`t` without a leading tag: `0x2dfda0 slot 0 (ret 8): ` (words only of RVAs, slots, numbers and
    parentheses before a colon in the first 100 characters) or a bare `+0x50c `."""
    colon = t.find(": ", 0, 100)
    if colon > 0:
        head = re.sub(r"\([^()]*\)", " ", t[:colon]).replace(",", " , ").replace(";", " ; ")
        if all(_LEAD_WORD.fullmatch(w) for w in head.split()):
            return t[colon + 2:]
    return _OFFSET_LEAD.sub("", t, count=1)


def first_sentence(text, limit=110):
    """The first sentence of a comment, without a leading tag (`0x2dfda0 slot 0 (ret 8): `, `+0x50c `)."""
    t = re.sub(r"(\w)::\s+(?=\w)", r"\1::", strip_lead(squash(text)))  # `ServiceHolder::` at a line's end
    m = _SENTENCE_END.search(t)
    if m:
        t = t[:m.end()]
    if len(t) > limit:
        t = t[:limit - 3].rstrip() + "..."
    return t


def comment_of(d):
    """A declaration's comment: the one above it, else the one beside it (a field: beside first)."""
    if d["kind"] == "field":
        return d["trail"] or d["above"]
    return d["above"] or d["trail"]


# ---------------------------------------------------------------- the query

STOP = {"the", "a", "an", "and", "or", "of", "to", "in", "on", "at", "by", "for", "with", "from", "that", "this",
        "its", "it", "is", "are", "be", "as", "into", "than", "then", "which", "what", "who", "how", "does"}


_CASE = [(re.compile(a), b) for a, b in ((r"([a-z])([A-Z])", r"\1 \2"), (r"([A-Z]+)([A-Z][a-z])", r"\1 \2"),
                                         (r"([A-Za-z])(\d)", r"\1 \2"), (r"(\d)([A-Za-z])", r"\1 \2"))]
_NONWORD = re.compile(r"[^A-Za-z0-9]+")
_STEMS = {}


def words(s):
    """Lower-case words of a name or text: split on case changes, digits and anything not alphanumeric."""
    for pattern, repl in _CASE:
        s = pattern.sub(repl, s)
    out = []
    for w in _NONWORD.split(s.lower()):
        if w:
            st = _STEMS.get(w)
            if st is None:
                st = _STEMS[w] = sys.intern(stem(w))
            out.append(st)
    return out


def stem(w):
    if len(w) > 3 and w.endswith("ies"):
        return w[:-3] + "y"
    if len(w) >= 3 and w.endswith("s") and not w.endswith("ss"):
        return w[:-1]
    return w


def tokens(d, path):
    """The word sets a declaration is matched on (computed once, when it is indexed)."""
    return {"name": frozenset(words(Path(path).stem if d["kind"] == "file" else d["name"])),
            "scope": frozenset(words(d["scope"].split("::")[-1])) if d["scope"] else frozenset(),
            "comment": frozenset(words(comment_of(d))), "sig": frozenset(words(d["sig"])),
            "body": frozenset(d.pop("body", ()))}


WEIGHTS = (("name", 3.0), ("scope", 1.5), ("comment", 1.0), ("sig", 0.5), ("body", 0.3))


def patterns(w):
    """(exact, prefixes) to look for in a part string ` a b c `: ` w ` for the word itself; ` w` (a longer word
    starting with it) and ` wor ` .. (a shorter word it starts with, 4 letters or more) for a prefix match."""
    if len(w) < 4:
        return f" {w} ", ()
    return f" {w} ", (f" {w}",) + tuple(f" {w[:n]} " for n in range(4, len(w)))


def score(parts, kind, qpats):
    """(rank key, coverage) of one declaration's part strings, or None: a word found in the name weighs 3, in the
    class 1.5, in the comment 1, in the signature 0.5, in the body's identifiers 0.3; a prefix match (`ident` for
    `identity`) 0.7 of that. Ranked by coverage times the sum plus one per word in the name; a file's header
    comment counts for 0.4 of it (it names much)."""
    total, matched, in_name = 0.0, 0, 0
    for exact, prefixes in qpats:
        best = 0.0
        for k, weight in enumerate(_WEIGHT_VALUES):
            s = parts[k]
            if len(s) < 3 or weight <= best:
                continue
            if exact in s:
                m = weight
            elif any(p in s for p in prefixes):
                m = 0.7 * weight
            else:
                continue
            if m > best:
                best = m
            if k == 0:
                in_name += 1
                break
        if best:
            matched += 1
            total += best
    if not matched:
        return None
    coverage = matched / len(qpats)
    key = coverage * total + in_name
    if kind == "file":
        key *= 0.4
    return key, coverage


_WEIGHT_VALUES = tuple(w for _, w in WEIGHTS)


def query_words(query):
    ws = words(" ".join(query))
    return [w for w in ws if w not in STOP and (len(w) > 1 or w.isdigit())] or ws


def search(index, query, kinds=None, where=None, fields=False):
    """[(rank, path, decl dict)] best first: the declarations matching at least half the query's words."""
    qwords = query_words(query)
    qpats = [patterns(w) for w in qwords]
    need = max(1, (len(qwords) + 1) // 2) / len(qwords) - 1e-9
    hits = []
    for path, decls in index.items():
        if where and not path.startswith(where):
            continue
        for t in decls:
            kind = t[0]
            if kind == "macro-use" or (kind == "field" and not fields and kinds != {"field"}):
                continue
            if kinds and kind not in kinds:
                continue
            s = score(t[7], kind, qpats)
            if s and t[4].endswith("= delete"):  # a deleted copy constructor or assignment says nothing
                s = (s[0] * 0.5, s[1])
            if s and s[1] >= need:
                hits.append((s, path, t))
    hits.sort(key=lambda h: (-h[0][0], -h[0][1], h[1], h[2][3]))
    return [(s, path, expand(t)) for s, path, t in hits]


def fmt(path, d, width=100):
    sig = d["sig"] if d["kind"] != "file" else "(file header)"
    if d["kind"] in ("method", "field") and d["scope"] and "::" not in d["name"]:
        sig = sig.replace(d["name"] + "(", d["scope"].split("::")[-1] + "::" + d["name"] + "(", 1) \
            if d["kind"] == "method" else sig
    if len(sig) > width:
        sig = sig[:width - 3] + "..."
    c = d["doc"] if "doc" in d else first_sentence(comment_of(d))
    return f"{path}:{d['line']}  {sig}" + (f"  // {c}" if c else "")


# ---------------------------------------------------------------- RVAs

def hook_entries(root=ROOT):
    """[(rva, 'path:line', target text)] of the hook table entries."""
    out = []
    for f in sorted((root / "src/hook/tables").glob("*.cpp")):
        for n, l in enumerate(f.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            m = HOOK_ENTRY.match(l)
            if m and not l.lstrip().startswith("//"):
                out.append((int(m.group(1), 16), f"{f.relative_to(root).as_posix()}:{n}", m.group(2).strip()))
    return out


def status_entries(root=ROOT):
    out = []
    for f in sorted((root / "db/status").glob("*.jsonl")):
        for l in f.read_text(encoding="utf-8").splitlines():
            if l.strip():
                e = json.loads(l)
                out.append((int(e["rva"], 16), f"db/status/{f.name}", e))
    return out


def qualified_targets(text):
    """`ns::cFoo::Bar` names in a hook entry's target (`MemberAddress(&ns::cFoo::Bar)`, `&A::Construct`)."""
    return re.findall(r"&\s*([A-Za-z_][\w:]*)", text)


_NAME_MAPS = {}


def _by_last_name(index):
    """{last name part: [(path, tuple)]} of an index's functions and methods, built once per index."""
    m = _NAME_MAPS.get(id(index))
    if m is None:
        m = {}
        for path, decls in index.items():
            for t in decls:
                if t[0] in ("method", "function"):
                    m.setdefault(t[1].rsplit("::", 1)[-1], []).append((path, t))
        _NAME_MAPS.clear()
        _NAME_MAPS[id(index)] = m
    return m


def find_qualified(index, qname):
    """[(path, decl)] whose scope::name ends with `qname`'s last two parts (`cFoo::Bar`, or `Bar` alone)."""
    parts = qname.split("::")
    want_name, want_scope = parts[-1], parts[-2] if len(parts) > 1 else None
    out = []
    for path, t in _by_last_name(index).get(want_name, ()):
        d = expand(t)
        n = d["name"].split("::")
        scope = (d["scope"].split("::") if d["scope"] else []) + n[:-1]
        if want_scope is None or (scope and scope[-1] == want_scope):
            out.append((path, d))
    return out


def by_rva(index, rva, root=ROOT):
    """Lines for one RVA: its hook entries and status line, the declarations they name, then the declarations whose
    comment or signature names it (src/ before tests/, file headers last)."""
    out, seen, aliases = [], set(), []
    for r, where, target in hook_entries(root):
        if r == rva:
            shown = re.sub(r"^\s*\d+\s*,\s*0x[0-9a-fA-F]+\s*,\s*", "", target)  # past the size and checksum
            out.append(f"hook   {where}  {squash(shown).rstrip('},')[:110]}")
            aliases.append(target)
    for r, where, e in status_entries(root):
        if r == rva:
            out.append(f"status {where}  {e['status']}  {e['name'][:100]}" + (f"  test {e['test']}" if e.get("test") else ""))
            head = e["name"].split(" (")[0].split(" ")[0]
            if "::" in head:
                aliases.append("&" + head)
    for target in aliases:
        for q in qualified_targets(target):
            for path, d in find_qualified(index, q)[:3]:
                if (path, d["line"]) not in seen:
                    seen.add((path, d["line"]))
                    out.append(fmt(path, d))
    named = [(path, expand(t)) for path, decls in index.items() for t in decls
             if rva in t[6] and (path, t[3]) not in seen]
    named.sort(key=lambda pd: (pd[1]["kind"] == "file", not pd[0].startswith("src/"), pd[0], pd[1]["line"]))
    out += [fmt(path, d) for path, d in named]
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("query", nargs="+")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--src", action="store_true")
    ap.add_argument("--tests", action="store_true")
    ap.add_argument("--fields", action="store_true")
    ap.add_argument("--kind", action="append")
    args = ap.parse_args(argv)
    index = load_index()
    where = "src/" if args.src else ("tests/", "harness/") if args.tests else None
    if where:
        index = {p: d for p, d in index.items() if p.startswith(where)}
    if len(args.query) == 1 and re.fullmatch(r"0[xX][0-9a-fA-F]+", args.query[0]):
        lines = by_rva(index, int(args.query[0], 16))
        for l in (lines if args.all else lines[:25]):
            print(l)
        if not lines:
            print(f"nothing names {args.query[0]} (comments, hook tables, db/status); try tools/whereis.py")
        return
    hits = search(index, args.query, set(args.kind) if args.kind else None, None, args.fields)
    shown = hits if args.all else hits[:15]
    for _, path, d in shown:
        print(fmt(path, d))
    if len(hits) > len(shown):
        print(f"... {len(hits) - len(shown)} more (--all)")
    if not hits:
        print("no match; fewer or other words, or tools/whereis.py for a class or RVA")


if __name__ == "__main__":
    main()
