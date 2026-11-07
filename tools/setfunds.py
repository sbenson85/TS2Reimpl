#!/usr/bin/env python3
"""Set a family's two funds-like FAMI words in a neighbourhood package, in place (local/ copies only).

  python -I tools/setfunds.py PACKAGE FAMILY WORD7 [TAIL]

FAMILY is the FAMI instance (e.g. 0x8). WORD7 is FAMI word 7 (savediff's `funds`), TAIL the second
funds-like word (`funds_tail`, default: the same value). Setting them to different values shows which
one the game uses as the household's balance. Only an uncompressed FAMI entry is patched (its bytes
are overwritten where they are; the file's size and every other byte stay the same); a compressed one
is refused rather than rewriting the package.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dbpf                     # noqa: E402
import savediff                 # noqa: E402
import snapstore                # noqa: E402


def main(argv):
    if len(argv) not in (3, 4):
        sys.exit(__doc__)
    path, family, word7 = argv[0], int(argv[1], 0), int(argv[2], 0)
    tail = int(argv[3], 0) if len(argv) == 4 else word7
    if "local" not in os.path.abspath(path).lower().split(os.sep):
        sys.exit("refusing to edit a package outside local/")
    pk = dbpf.Package(path)
    e = pk.entry((savediff.TYPE_FAMI, 0xFFFFFFFF, family, 0))
    if e is None:
        e = next((x for x in pk.entries if x.type == savediff.TYPE_FAMI and x.instance == family), None)
    if e is None:
        sys.exit("no FAMI instance 0x%x" % family)
    if e.compressed:
        sys.exit("FAMI 0x%x is compressed: not patched in place" % family)
    data = bytearray(pk.read(e))
    before = savediff.parse_fami(bytes(data))
    s = 0 if before["version"] == 0x55 else -1
    n = before["member_count"]
    struct.pack_into("<I", data, 4 * (7 + s), word7)
    struct.pack_into("<I", data, 4 * (13 + s + n), tail)
    after = savediff.parse_fami(bytes(data))
    # A snapshot's or fixture's file is a hardlink into local/snapstore, shared with other snapshots: give this
    # path its own copy first, so the edit stays here.
    if snapstore.break_link(path):
        print("%s was hardlinked (snapstore): edited a private copy" % path)
    with open(path, "r+b") as f:
        f.seek(e.offset)
        f.write(data)
    print("FAMI 0x%x: funds %d -> %d, funds_tail %d -> %d" % (family, before["funds"], after["funds"],
                                                             before["funds_tail"], after["funds_tail"]))


if __name__ == "__main__":
    main(sys.argv[1:])
