"""Recovers MSVC x86 RTTI from the unpacked exe: classes, base classes and vftables.

Writes db/classes.jsonl, one class per line:
  {"name", "mangled", "td": rva, "bases": [names], "base_offsets": [offset of each base subobject,
   or null for virtual bases], "vftables": [{"rva", "offset", "entries": [rva...]}]}
All addresses are RVAs. Only facts (addresses, names) are written, never code bytes.

MSVC x86 layout:
  TypeDescriptor           { void* pVFTable; void* spare; char name[]; }       name like ".?AVcFoo@@"
  CompleteObjectLocator    { u32 signature=0; u32 offset; u32 cdOffset; TypeDescriptor*; ClassHierarchyDescriptor*; }
  ClassHierarchyDescriptor { u32 signature; u32 attributes; u32 numBaseClasses; BaseClassDescriptor** }
  BaseClassDescriptor      { TypeDescriptor*; u32 numContainedBases; PMD{mdisp,pdisp,vdisp}; u32 attributes; ... }
  A vftable is immediately preceded by a pointer to its CompleteObjectLocator.

Usage: python -I tools/rtti.py [exe] [out]
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class PE:
    def __init__(self, data):
        self.b = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        n = struct.unpack_from("<H", data, pe + 6)[0]
        opt = pe + 0x18
        self.base = struct.unpack_from("<I", data, opt + 28)[0]
        self.sections = {}
        for i in range(n):
            o = opt + struct.unpack_from("<H", data, pe + 0x14)[0] + 40 * i
            name = data[o:o + 8].rstrip(b"\0").decode()
            vsize, va, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
            self.sections[name] = (va, max(vsize, rsize), roff, rsize)

    def contains(self, sec, va):
        s = self.sections[sec]
        return s[0] <= va - self.base < s[0] + s[1]

    def off(self, va):
        rva = va - self.base
        for sva, size, roff, rsize in self.sections.values():
            if sva <= rva < sva + rsize:
                return roff + rva - sva
        return None

    def u32(self, va):
        o = self.off(va)
        return None if o is None else struct.unpack_from("<I", self.b, o)[0]

    def dwords(self, sec):
        """Yields (va, value) for every 4-aligned dword in a section's raw data."""
        sva, _, roff, rsize = self.sections[sec]
        vals = struct.unpack_from("<%dI" % (rsize // 4), self.b, roff)
        for i, v in enumerate(vals):
            yield self.base + sva + 4 * i, v


def demangle(m):
    """Plain demangling for non-template names: .?AVInner@Outer@@ -> Outer::Inner. Templates stay mangled."""
    body = m[4:-2] if m.endswith("@@") else m[4:]
    if "?" in body or "$" in body:
        return m
    return "::".join(reversed(body.split("@")))


def main():
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "local/original/Sims2EP9.exe.unpacked.exe"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "db/classes.jsonl"
    pe = PE(exe.read_bytes())
    rva = lambda va: "0x%x" % (va - pe.base)

    # TypeDescriptors: name string at +8.
    tds = {}
    for m in re.finditer(rb"\.\?A[VU][\x21-\x7e]+?@@\x00", pe.b):
        name_off = m.start()
        for sec, (sva, _, roff, rsize) in pe.sections.items():
            if roff <= name_off < roff + rsize:
                tds[pe.base + sva + name_off - roff - 8] = m.group()[:-1].decode()
                break

    # CompleteObjectLocators: signature 0 and a TypeDescriptor pointer at +12.
    cols = {}
    for va, v in pe.dwords(".rdata"):
        if v != 0:
            continue
        td = pe.u32(va + 12)
        if td in tds:
            chd = pe.u32(va + 16)
            if chd is not None and pe.contains(".rdata", chd):
                cols[va] = (pe.u32(va + 4), td, chd)

    # vftables: the slot after a pointer to a COL; entries run while they point into .text.
    vftables = {}
    rdata = list(pe.dwords(".rdata"))
    for i, (va, v) in enumerate(rdata):
        if v in cols:
            entries = []
            for _, e in rdata[i + 1:]:
                if e in cols or not pe.contains(".text", e):
                    break
                entries.append(rva(e))
            if entries:
                offset, td, _ = cols[v]
                vftables.setdefault(td, []).append({"rva": rva(va + 4), "offset": offset, "entries": entries})

    # Base classes from each class's ClassHierarchyDescriptor (first entry is the class itself).
    # BaseClassDescriptor: +0 TypeDescriptor*, +4 numContainedBases, +8 mdisp (offset of the base
    # subobject in the derived class), +12 pdisp (-1 unless virtual base), +16 vdisp, +20 attributes.
    bases, base_offsets = {}, {}
    for col_va, (_, td, chd) in cols.items():
        if td in bases:
            continue
        n, arr = pe.u32(chd + 8), pe.u32(chd + 12)
        names, offsets = [], []
        for k in range(1, min(n or 0, 256)):
            bcd = pe.u32(arr + 4 * k) if arr else None
            btd = pe.u32(bcd) if bcd else None
            if btd in tds:
                names.append(demangle(tds[btd]))
                pdisp = pe.u32(bcd + 12)
                offsets.append(None if pdisp != 0xFFFFFFFF else pe.u32(bcd + 8))
        bases[td], base_offsets[td] = names, offsets

    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="\n") as f:
        for td, mangled in sorted(tds.items(), key=lambda t: t[1]):
            f.write(json.dumps({
                "name": demangle(mangled),
                "mangled": mangled,
                "td": rva(td),
                "bases": bases.get(td, []),
                "base_offsets": base_offsets.get(td, []),
                "vftables": sorted(vftables.get(td, []), key=lambda x: x["offset"]),
            }) + "\n")
    print(f"type descriptors {len(tds)}, object locators {len(cols)}, "
          f"classes with vftables {len(vftables)}, vftables {sum(map(len, vftables.values()))}")


if __name__ == "__main__":
    main()
