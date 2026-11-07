"""Function bodies in a C++ file, from the text alone (no build): enough for tools/testselect.py (which function a
mutant edits; a DIFF_TEST's body) and tools/testweaken.py (what each test block checks).

    blocks(text) -> [Block(name, qual, kind, start, open, close)]

`kind` is "test" for `DIFF_TEST(Name, rva) { ... }` (name = Name), else "function"; `qual` is the qualified name
(`cShaderCommand::EndBlock`, a method defined in its class body gets the class: `cFoo::Bar`); `start` the offset where
the definition's header starts, `open`/`close` the offsets of its braces. Namespaces, classes, structs and
`extern "C"` are containers; other brace blocks at container level (initialisers, enums) are not functions. Comments,
strings and preprocessor lines are ignored (tools/arity.py's blank()).
"""
import re
import sys
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from arity import blank  # noqa: E402

Block = namedtuple("Block", "name qual kind start open close")

NOT_NAMES = {"__declspec", "alignas", "decltype", "__attribute__", "if", "for", "while", "switch", "return", "sizeof",
             "noexcept", "throw", "static_assert", "catch"}
CLASS_RE = re.compile(r"(?:.*[^\w])?(?:class|struct|union)\s+(?:(?:__declspec|alignas)\s*\([^)]*\)\s*)*(\w+)[^(){};=]*$",
                      re.S)  # the last class-key of the header (template <class T> struct Foo: Foo)
IDENT_BEFORE_PAREN = re.compile(r"((?:[A-Za-z_~]\w*\s*::\s*)*(?:operator\s*(?:\(\)|[^\s(]+)|~?[A-Za-z_]\w*))\s*(?:<[^<>()]*>)?\s*\(")


def _container(header):
    """The class name ("" for a namespace or extern block) if `header` opens a container, else None."""
    h = header.strip()
    if re.search(r"\bnamespace\b", h) and "(" not in h:
        return ""
    if re.fullmatch(r"extern\s*", h):  # extern "C" { (the string is blanked)
        return ""
    m = CLASS_RE.match(h)
    if m and "(" not in h[m.start(1):]:
        return m.group(1)
    return None


def _header_name(header):
    """(name, kind) of a function definition's header, or None if it doesn't look like one."""
    m = re.search(r"\bDIFF_TEST\s*\(\s*(\w+)", header)
    if m:
        return m.group(1), "test"
    if "(" not in header:
        return None
    for m in IDENT_BEFORE_PAREN.finditer(header):
        name = re.sub(r"\s+", "", m.group(1))
        if name.split("::")[-1] in NOT_NAMES:
            continue
        return name, "function"
    m = re.search(r"\b(\w+)\s*=\s*\[", header)  # a lambda at namespace scope: `static auto f = [](...) {`
    return (m.group(1), "function") if m else None


def blocks(text):
    b = blank(text)
    out = []
    stack = []  # (kind, name, open offset); kind: container, function, other
    seg = 0  # start of the current statement / header at this level
    for i, c in enumerate(b):
        if c == "{":
            header = b[seg:i]
            inside_function = any(k in ("function", "other") for k, _, _ in stack)
            if inside_function:
                stack.append(("other", None, i))
            else:
                container = _container(header)
                if container is not None:
                    stack.append(("container", container or None, i))
                else:
                    hn = _header_name(header)
                    if hn and not re.search(r"=\s*$", header.rstrip()):
                        name, kind = hn
                        classes = [n for k, n, _ in stack if k == "container" and n]
                        qual = name if "::" in name or not classes or kind == "test" else f"{classes[-1]}::{name}"
                        start = seg + len(header) - len(header.lstrip())
                        stack.append(("function", (name, qual, kind, start), i))
                    else:
                        stack.append(("other", None, i))
            seg = i + 1
        elif c == "}":
            if stack:
                kind, info, at = stack.pop()
                if kind == "function":
                    name, qual, k, start = info
                    out.append(Block(name, qual, k, start, at, i))
            seg = i + 1
        elif c == ";" and not any(k in ("function", "other") for k, _, _ in stack):
            seg = i + 1
    out.sort(key=lambda x: x.open)
    return out


TestBlock = namedtuple("TestBlock", "name rva start end")  # rva: the expression given (literal or constant name)
DEFINE_RE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)\(([^)]*)\)((?:[^\n]*\\\r?\n)*[^\n]*)", re.M)
DIFF_TEST_IN_MACRO = re.compile(r"\bDIFF_TEST\s*\(\s*([^,()]+?)\s*,\s*([^()]+?)\s*\)")


def wrappers(text):
    """Macros in `text` that expand to DIFF_TEST: {macro: (params, [(name expression, rva expression)])}
    (`#define OCC_TEST(name, rva) ... DIFF_TEST(name, rva) ...`, `DIFF_TEST(LotInfo_##Name, Rva)`)."""
    out = {}
    for m in DEFINE_RE.finditer(text):
        if m.group(1) == "DIFF_TEST":
            continue
        body = re.sub(r"\\\r?\n", " ", m.group(3))
        tests = DIFF_TEST_IN_MACRO.findall(body)
        if tests:
            out[m.group(1)] = ([p.strip() for p in m.group(2).split(",")], tests)
    return out


def split_args(s):
    """A macro call's arguments (the text inside its parentheses): like the preprocessor, only parentheses keep a comma
    inside one argument."""
    out, depth, cur = [], 0, []
    for c in s:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
    out.append("".join(cur).strip())
    return out


def _expand(expr, params, args):
    pieces = []
    for piece in expr.split("##"):
        p = piece.strip()
        if p in params and params.index(p) < len(args):
            p = args[params.index(p)]
        elif p == "__VA_ARGS__":
            p = ", ".join(args[len(params) - 1:])
        pieces.append(p)
    return "".join(pieces)


def _match(b, at, open_c, close_c):
    depth = 0
    for i in range(at, len(b)):
        if b[i] == open_c:
            depth += 1
        elif b[i] == close_c:
            depth -= 1
            if depth == 0:
                return i
    return len(b) - 1


def test_blocks(text, defs_text=""):
    """Every difftest a file registers with DIFF_TEST or a wrapper macro defined in it or in `defs_text` (the tests/
    headers it includes): [TestBlock(name, rva expression, start, end)], end exclusive. A wrapper call's span is the
    call and the brace block right after it, if any."""
    b = blank(text)
    out = []
    for blk in blocks(text):
        if blk.kind == "test":
            m = re.match(r"\s*DIFF_TEST\s*\(\s*\w+\s*,\s*([^)]*?)\s*\)", b[blk.start:blk.open])
            out.append(TestBlock(blk.name, m.group(1) if m else "", blk.start, blk.close + 1))
    ws = wrappers(defs_text + "\n" + text)
    if ws:
        for m in re.finditer(r"^[ \t]*(" + "|".join(map(re.escape, ws)) + r")\s*\(", b, re.M):
            params, tests = ws[m.group(1)]
            close = _match(b, m.end() - 1, "(", ")")
            args = split_args(b[m.end():close])
            end = close + 1
            nxt = re.match(r"\s*\{", b[end:])
            if nxt:
                end = _match(b, end + nxt.end() - 1, "{", "}") + 1
            for nexpr, rexpr in tests:
                name = _expand(nexpr, params, args)
                if re.fullmatch(r"\w+", name):
                    out.append(TestBlock(name, _expand(rexpr, params, args), m.start(), end))
    out.sort(key=lambda t: t.start)
    return out


def leading_comment_start(text, start):
    """`start` moved up over the comment lines right above it (a test's doc comment belongs to the test)."""
    while start > 0:
        prev_end = text.rfind("\n", 0, start - 1)
        line = text[prev_end + 1:start].strip()
        if not line.startswith(("//", "/*", "*")):
            break
        start = prev_end + 1
    return start


def enclosing(blocks_, offset):
    """The function block containing `offset` (outermost), or None."""
    for blk in blocks_:
        if blk.start <= offset <= blk.close:
            return blk
    return None
