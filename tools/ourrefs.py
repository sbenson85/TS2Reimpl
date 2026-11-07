"""Which game addresses our built code holds: a scan of the host executable we ship (build/Release/ts2host.exe, with
its linker map) for 32-bit values that point into the game image, for tools/callers.py.

Our code turns a game RVA into a pointer in several forms: GameAddress(kConst) links to the absolute address (game base
+ RVA, an immediate or a dword in data); a helper taking the RVA as an argument (VTableOf(rva)), a table of RVAs, a
constant computed from another (kBase + 8) hold the bare RVA. Source patterns miss some of these; the built code holds
them all as one of the two values. scan() looks for both, at every byte offset of every section, and keeps a hit unless
the bytes around it show it is not a value: in an executable section, a hit is dropped only when the instruction
covering it (decoded linearly from the nearest preceding map symbol) is a real instruction and the hit is not its 4-byte
immediate or displacement (a look-alike straddling instructions; about 1 in 5 code hits in our host); bytes the decoding
doesn't cover cleanly (data, jump tables) count. In data every hit counts except unaligned ones inside compiler or
linker metadata (byte-packed unwind maps, volatile metadata). Hits inside a third-party library we link (THIRD_PARTY,
by the map's `library:object` name) are dropped: that code knows nothing of the game, so its bytes only look like an
address (FreeType's af_blue_strings held 20 e0 a4 a0, read as 0xa0a4e020, 2026-10-28). Nothing else is left out by
object: the map can't say where a symbol-less object's bytes end. Hook rows and traps hold bare function RVAs and CRCs; callers.py drops hits on their
CRCs (scan's `ignore`).

Usage: tools/py -I tools/ourrefs.py [--exe build/Release/ts2host.exe] 0xRVA...   (who in our binary holds each value)
"""
import argparse
import bisect
import re
import struct
import sys
from array import array
from itertools import compress, count
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

DEFAULT_EXE = "build/Release/ts2host.exe"
METADATA = re.compile(r"(unwindtable|tryblocktable|stateUnwindMap|ipToStateMap|catchsym|volatile_metadata)\b|"
                      r"(__tryblocktable|__unwindtable|__catchsym)\$")
THIRD_PARTY = ("ts2freetype:",)  # our static libraries built from third-party sources (CMakeLists.txt)
SYMBOL = re.compile(r"^\s*([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{8})\s+(?:[fi]\s+)*(\S+)\s*$", re.M)


def game_base():
    return int(re.search(r"kGameBase\s*=\s*(0x[0-9a-fA-F]+)", (tsdb.ROOT / "src/game/game.h").read_text()).group(1), 16)


def undecorate(name):
    """Readable enough for a report: ?Fn@Class@@... -> Class::Fn."""
    m = re.match(r"\?([^@?]+)@([^@?]+)@@", name)
    if m:
        return f"{m.group(2)}::{m.group(1)}"
    return name.lstrip("_?")


class Binary:
    """Our built executable and its map: symbols by address, section bytes."""

    _detail = None  # the capstone decoder for operand fields, made on first use

    def __init__(self, exe, mapfile=None):
        import pefile
        self.exe = Path(exe)
        self.map = Path(mapfile) if mapfile else self.exe.with_suffix(".map")
        pe = pefile.PE(str(self.exe), fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.sections = []  # (name, va, executable, bytes)
        for s in pe.sections:
            data = pe.get_data(s.VirtualAddress, s.Misc_VirtualSize)
            self.sections.append((s.Name.rstrip(b"\0").decode(errors="replace"), self.base + s.VirtualAddress,
                                  bool(s.Characteristics & 0x20000000), data))
        self.symbols = []  # sorted (va, name, object)
        for _, _, name, va, obj in SYMBOL.findall(self.map.read_text(errors="replace")):
            v = int(va, 16)
            if obj != "<absolute>" and self.base <= v < self.base + pe.OPTIONAL_HEADER.SizeOfImage:
                self.symbols.append((v, name, obj))
        self.symbols.sort()
        self._starts = [s[0] for s in self.symbols]
        self._decoded = {}

    def symbol_at(self, va):
        i = bisect.bisect_right(self._starts, va) - 1
        return self.symbols[i] if i >= 0 else (self.base, "?", "?")

    def operand_check(self, sym_va, data, sec_va, site):
        """True when `site` is an immediate or displacement of an instruction decoded linearly from sym_va, False when
        it lies inside an instruction elsewhere (opcode or ModRM bytes, a field straddling two instructions), True
        (unknown: keep it) when the decoding doesn't cover it cleanly."""
        if sym_va not in self._decoded:
            import capstone
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
            md.skipdata = True
            nxt = bisect.bisect_right(self._starts, sym_va)
            end = self._starts[nxt] if nxt < len(self._starts) else sec_va + len(data)
            end = min(end, sec_va + len(data))
            # Boundaries first, cheaply, for the whole symbol; the operand fields only of the instruction a site is in.
            self._decoded[sym_va] = [(address, address + size, name != ".byte")  # (start, end, real instruction)
                                     for address, size, name, _ in md.disasm_lite(data[sym_va - sec_va:end - sec_va], sym_va)]
        spans = self._decoded[sym_va]
        i = bisect.bisect_right(spans, (site, float("inf"))) - 1
        if i < 0 or not spans[i][2] or not (spans[i][0] <= site < spans[i][1]):
            return True
        return site in self.operand_fields(spans[i][0], data[spans[i][0] - sec_va:spans[i][1] - sec_va])

    def operand_fields(self, address, code):
        """Where one instruction (its bytes `code`) holds an address: its 32-bit immediate and displacement, by site.
        Everything when capstone can't say, as the caller keeps a site then."""
        import capstone
        if self._detail is None:
            self._detail = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
            self._detail.detail = True
        fields = set()
        try:
            for ins in self._detail.disasm(code, address, 1):
                # A relative call/jmp/jcc's rel32 is a displacement, not an address: its bytes can equal a game address
                # by chance (a call into the image at 0x5bc524 read as 0xa05b72e7).
                relative = ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP)
                if ins.imm_size == 4 and not relative:
                    fields.add(ins.address + ins.imm_offset)
                if ins.disp_size == 4:
                    fields.add(ins.address + ins.disp_offset)
        except capstone.CsError:
            return range(address, address + len(code))
        return fields

    def scan(self, values, ignore=None):
        """{value: [(site va, symbol, object, in code)]} for every 32-bit occurrence of a value in `values`; ignore(value,
        symbol, object, in code) -> True drops a hit the caller knows is something else (a hook row's CRC)."""
        values = set(values)
        out = {}
        for _, sec_va, executable, data in self.sections:
            for shift in range(4):
                n = (len(data) - shift) // 4
                words = array("I", data[shift:shift + 4 * n])
                if sys.byteorder != "little":
                    words.byteswap()
                for k in compress(count(), map(values.__contains__, words)):
                    w = words[k]
                    site = sec_va + shift + 4 * k
                    sym_va, name, obj = self.symbol_at(site)
                    if obj.startswith(THIRD_PARTY):
                        continue  # third-party library bytes (see the module docstring)
                    if not executable and site % 4 and (obj == "<linker-defined>" or METADATA.match(name)):
                        continue  # byte-packed compiler / linker metadata (unwind maps, volatile metadata)
                    if executable and not self.operand_check(sym_va, data, sec_va, site):
                        continue
                    if ignore and ignore(w, name, obj, executable):
                        continue
                    out.setdefault(w, []).append((site, name, obj, executable))
        return out


def default_exe():
    return tsdb.ROOT / DEFAULT_EXE


def references(rvas, exe=None, encodings=("abs", "rva")):
    """{rva: [description]} for each RVA our binary holds as game base + RVA ("abs") or as the bare RVA ("rva")."""
    b = Binary(exe or default_exe())
    gb = game_base()
    want = {}
    for r in rvas:
        if "abs" in encodings:
            want.setdefault((gb + r) & 0xFFFFFFFF, []).append((r, "game address"))
        if "rva" in encodings:
            want.setdefault(r, []).append((r, "RVA"))
    out = {}
    for value, hits in b.scan(want).items():
        for r, how in want[value]:
            for site, name, obj, code in hits:
                out.setdefault(r, []).append(f"{b.exe.name} {site:#x} in {undecorate(name)} ({obj}): "
                                             f"{'operand' if code else 'data'} holds the {how} {value:#x}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rvas", nargs="+")
    ap.add_argument("--exe", default=None)
    args = ap.parse_args()
    found = references([int(r, 16) for r in args.rvas], args.exe)
    for r in args.rvas:
        hits = found.get(int(r, 16), [])
        print(f"{r}: {len(hits)} reference(s) in our binary")
        for h in hits:
            print(f"  {h}")


if __name__ == "__main__":
    main()
