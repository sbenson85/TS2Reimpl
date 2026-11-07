"""Owned-statics check: the image-free definitions in standalone/owned_*.cpp against the image's bytes at the same RVA.

    .venv/Scripts/python -I tools/ownedcheck.py [--verbose] [files...]

An owned static (`TS2_OWNED_STATIC(type, rva) = ...;`, game/owned.h) is the image's data in a hooked build and our
definition in an image-free one, so the two must hold the same value. Checked, per definition:
  - a string (`const char name[] = "..."`): the bytes and the terminator equal the image's at the RVA. A definition
    marked `// own text` on its line is a message in our own wording (the original's text is not copied): there the
    printf conversions (in order), a leading '*' or newline and a trailing newline must match the image's instead;
  - a text cell (`const char *` = "..."): the image's cell points at the same text; `= ts2_rva_X`: at RVA X;
  - an import slot (`void *` = reinterpret_cast<void *>(::Name)): the image's import table has Name at that slot;
    a pointer cell `= &ts2_rva_X` holds X's address;
  - integers, integer arrays and GUIDs given as literals: the same little-endian bytes;
  - `{}` (zeroed) integer, byte and char scalars and arrays: the image's bytes are zero there (initialised data or
    .bss, which loads as zero).
Anything else (class objects, initialisers naming constants, function results) is counted as not checked and listed
with --verbose. Exits 1 on any mismatch. Facts only: reads the image, prints RVAs and lengths, never its text.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

import pefile  # noqa: E402

DEF = re.compile(r"TS2_OWNED_STATIC\(\s*(?P<type>[^,]+?)\s*,\s*(?P<rva>[0-9a-fA-F]+)\s*\)\s*(?P<dims>(\[[^\]]*\])*)\s*")
INT_TYPES = {"uint8_t": (1, False), "int8_t": (1, True), "char": (1, True), "uint16_t": (2, False),
             "int16_t": (2, True), "uint32_t": (4, False), "int32_t": (4, True), "int": (4, True),
             "unsigned": (4, False), "DWORD": (4, False), "uint64_t": (8, False), "int64_t": (8, True)}
INT_LITERAL = re.compile(r"^-?\s*(0[xX][0-9a-fA-F]+|\d+)[uUlL]*$")
CONVERSION = re.compile(r"%[-+ #0]*(\*|\d+)?(\.(\*|\d+))?(hh|h|ll|l|L|I64|I32|I|z|j|t)?[diouxXeEfgGaAcspn%]")


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(str(path), fast_load=True)
        self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.imports = {}
        for entry in getattr(self.pe, "DIRECTORY_ENTRY_IMPORT", []):
            for imp in entry.imports:
                if imp.name: self.imports[imp.address - self.base] = imp.name.decode("ascii", "replace")

    def read(self, rva, size):
        """The bytes at rva as loaded: raw data, zero beyond a section's raw size; None outside every section."""
        for s in self.pe.sections:
            start, vsize = s.VirtualAddress, max(s.Misc_VirtualSize, s.SizeOfRawData)
            if start <= rva and rva + size <= start + vsize:
                off = rva - start
                raw = s.get_data()[off:off + size] if off < s.SizeOfRawData else b""
                return raw + b"\0" * (size - len(raw))
        return None

    def cstring(self, rva, limit=0x10000):
        data = self.read(rva, 1)
        if data is None: return None
        out = bytearray()
        while len(out) < limit:
            b = self.read(rva + len(out), 1)
            if b is None or b == b"\0": break
            out += b
        return bytes(out)


def statements(text):
    """Each `TS2_OWNED_STATIC(...)...;` definition: (line number, the statement's text, the line's comment)."""
    for m in DEF.finditer(text):
        start, i, depth, in_str = m.start(), m.end(), 0, None
        line_start = text.rfind("\n", 0, start) + 1
        if text[line_start:start].strip().startswith("//"): continue
        while i < len(text):
            c = text[i]
            if in_str:
                if c == "\\": i += 1
                elif c == in_str: in_str = None
            elif c in "\"'": in_str = c
            elif c == "/" and text.startswith("//", i): i = text.find("\n", i) - 1
            elif c in "({[": depth += 1
            elif c in ")}]": depth -= 1
            elif c == ";" and depth == 0: break
            i += 1
        body = text[m.end():i]
        end_line = text.find("\n", i)
        comment = text[i + 1:end_line if end_line >= 0 else len(text)]
        yield text.count("\n", 0, start) + 1, m, body, comment


ESCAPES = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, "\"": 34, "'": 39, "a": 7, "b": 8, "f": 12, "v": 11, "?": 63}


def strip_comments(s):
    out, i, in_str = [], 0, False
    while i < len(s):
        c = s[i]
        if in_str:
            out.append(c)
            if c == "\\":
                out.append(s[i + 1]); i += 1
            elif c == '"': in_str = False
        elif c == '"':
            in_str = True; out.append(c)
        elif s.startswith("//", i):
            j = s.find("\n", i); i = len(s) if j < 0 else j; continue
        elif s.startswith("/*", i):
            i = s.find("*/", i) + 2; continue
        else: out.append(c)
        i += 1
    return "".join(out)


def string_literals(expr):
    """The bytes of a run of adjacent string literals (with the terminator), or None when expr is anything else."""
    expr = strip_comments(expr).strip()
    out, i = bytearray(), 0
    if not expr.startswith('"'): return None
    while i < len(expr):
        if expr[i].isspace():
            i += 1; continue
        if expr[i] != '"': return None
        i += 1
        while i < len(expr) and expr[i] != '"':
            c = expr[i]
            if c == "\\":
                n = expr[i + 1]
                if n == "x":
                    j = i + 2
                    while j < len(expr) and expr[j] in "0123456789abcdefABCDEF": j += 1
                    out.append(int(expr[i + 2:j], 16) & 0xff); i = j; continue
                if n in "01234567":
                    j = i + 1
                    while j < len(expr) and j < i + 4 and expr[j] in "01234567": j += 1
                    out.append(int(expr[i + 1:j], 8) & 0xff); i = j; continue
                if n not in ESCAPES: return None
                out.append(ESCAPES[n]); i += 2; continue
            out += c.encode("latin-1"); i += 1
        i += 1
    return bytes(out) + b"\0"


def int_literal(s):
    s = s.strip()
    if not INT_LITERAL.match(s): return None
    neg = s.startswith("-")
    s = s.lstrip("-").strip().rstrip("uUlL")
    v = int(s, 0)
    return -v if neg else v


def pack(value, size):
    return (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")


def conversions(b):
    text = b.decode("latin-1")
    return [m.group(0) for m in CONVERSION.finditer(text)]


def shape(b):
    """What an own-wording text must keep: the conversions, the first character when it is '*' or a newline, a
    trailing newline."""
    body = b.rstrip(b"\0")
    return conversions(body), body[:1] if body[:1] in (b"*", b"\n") else b"", body.endswith(b"\n")


def check(image, path, results, verbose):
    text = path.read_text(encoding="utf-8")
    rel = path.relative_to(tsdb.ROOT).as_posix()
    for line, m, body, comment in statements(text):
        rva = int(m.group("rva"), 16)
        typ = " ".join(m.group("type").split())
        base_type = typ.replace("const ", "").strip()
        dims = m.group("dims")
        own_text = "own text" in comment
        where = f"{rel}:{line} {rva:#x}"

        def fail(msg):
            results["fail"].append(f"{where}: {msg}")

        def unchecked(why):
            results["unchecked"].append(f"{where}: {why}")

        body = body.strip()
        if body in ("{}", "{ }"):
            n = 1
            if dims:
                inner = dims.strip("[]")
                n = int_literal(inner) if dims.count("[") == 1 else None
                if n is None:
                    unchecked("zeroed array of no literal length"); continue
            if base_type.endswith("*") or base_type in ("FARPROC", "float"): unit = 4
            elif base_type == "bool": unit = 1
            elif base_type in INT_TYPES: unit = INT_TYPES[base_type][0]
            else:
                unchecked(f"zeroed {typ}"); continue
            size = unit * n
            data = image.read(rva, size)
            if data is None: fail(f"{size} bytes outside the image")
            elif any(data): fail(f"zeroed here, the image's {size} bytes are not all zero")
            else: results["ok"] += 1
            continue
        if not body.startswith("="):
            unchecked(f"initialiser of {typ}"); continue
        expr = strip_comments(body[1:]).strip()

        if base_type == "char" and dims == "[]":
            ours = string_literals(expr)
            if ours is None:
                unchecked("char array not given as string literals"); continue
            theirs = image.read(rva, len(ours))
            if theirs is None:
                fail("outside the image"); continue
            if own_text:
                img = image.cstring(rva) + b"\0"
                if shape(ours) != shape(img):
                    fail(f"own text: its conversions, leading or trailing character {shape(ours)} are not the "
                         f"image's {shape(img)}")
                elif ours == img:
                    fail("marked own text, but the text is the image's (drop the marker)")
                else: results["ok"] += 1
            elif theirs != ours:
                at = next(i for i in range(len(ours)) if ours[i] != theirs[i])
                fail(f"text differs from the image's at byte {at} of {len(ours)} (own wording: mark the line `// own text`)")
            else: results["ok"] += 1
            continue

        if base_type == "char *" and not dims:
            cell = image.read(rva, 4)
            if cell is None:
                fail("outside the image"); continue
            target = struct.unpack("<I", cell)[0] - image.base
            ours = string_literals(expr)
            ref = re.fullmatch(r"&?\s*ts2_rva_([0-9a-fA-F]+)", expr)
            if ours is not None:
                if image.cstring(target) + b"\0" != ours: fail("the image's cell points at other text")
                else: results["ok"] += 1
            elif ref:
                if target != int(ref.group(1), 16): fail(f"the image's cell points at {target:#x}, ours at {ref.group(1)}")
                else: results["ok"] += 1
            else: unchecked("text cell of no literal")
            continue

        if base_type in ("void *", "FARPROC") and not dims:
            imp = re.fullmatch(r"reinterpret_cast<\s*(void \*|FARPROC)\s*>\(\s*&?\s*::(\w+)\s*\)", expr)
            ref = re.fullmatch(r"&\s*ts2_rva_([0-9a-fA-F]+)", expr)
            if imp:
                name = image.imports.get(rva)
                if name is None: fail(f"no import slot at {rva:#x} (ours: {imp.group(2)})")
                elif name != imp.group(2): fail(f"the image's import slot is {name}, ours {imp.group(2)}")
                else: results["ok"] += 1
            elif ref:
                cell = image.read(rva, 4)
                value = struct.unpack("<I", cell)[0] if cell else None
                if value == 0: unchecked("pointer cell the image sets at run time (zero at load)")
                elif value is None or value - image.base != int(ref.group(1), 16):
                    fail(f"the image's cell holds {value and hex(value - image.base)}, ours {ref.group(1)}")
                else: results["ok"] += 1
            else: unchecked(f"pointer initialiser {expr[:40]}")
            continue

        if base_type in INT_TYPES:
            size, _ = INT_TYPES[base_type]
            if not dims:
                values = [int_literal(expr)]
            elif dims.count("[") == 1 and expr.startswith("{") and expr.endswith("}"):
                values = [int_literal(v) for v in expr[1:-1].split(",") if v.strip()]
                n = int_literal(dims.strip("[]")) if dims != "[]" else len(values)
                if n is not None and len(values) < n: values += [0] * (n - len(values))
            else: values = [None]
            if any(v is None for v in values):
                unchecked(f"{typ}{dims} not given as integer literals"); continue
            ours = b"".join(pack(v, size) for v in values)
            theirs = image.read(rva, len(ours))
            if theirs is None: fail("outside the image")
            elif theirs != ours and not any(theirs):
                unchecked(f"{typ}{dims} the image sets at run time (zero at load)")
            elif theirs != ours: fail(f"{len(ours)} bytes differ from the image's")
            else: results["ok"] += 1
            continue

        if base_type == "IID" and not dims:
            nums = [int_literal(v) for v in re.split(r"[{},]", expr) if v.strip()]
            if len(nums) != 11 or any(v is None for v in nums):
                unchecked("GUID not given as literals"); continue
            ours = struct.pack("<IHH8B", *nums)
            if image.read(rva, 16) != ours: fail("GUID differs from the image's")
            else: results["ok"] += 1
            continue

        unchecked(f"{typ}{dims}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("files", nargs="*", help="default: standalone/owned_*.cpp")
    ap.add_argument("--verbose", action="store_true", help="list what is not checked")
    args = ap.parse_args()
    files = [Path(f).resolve() for f in args.files] or sorted((tsdb.ROOT / "standalone").glob("owned_*.cpp"))
    image = Image(tsdb.unpacked_exe())
    results = {"ok": 0, "fail": [], "unchecked": []}
    for f in files: check(image, f, results, args.verbose)
    for line in results["fail"]: print("FAIL " + line)
    if args.verbose:
        for line in results["unchecked"]: print("not checked " + line)
    print(f"ownedcheck: {results['ok']} match the image, {len(results['fail'])} differ, "
          f"{len(results['unchecked'])} not checked")
    return 1 if results["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
