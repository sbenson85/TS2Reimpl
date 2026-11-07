#!/usr/bin/env python3
"""DBPF 1.1 package reader (Sims 2 packages) with QFS/RefPack decompression.

Format facts used (public community documentation, cross-checked on the real saves):

Header (96 bytes, little endian):
  0x00 'DBPF'  0x04 major (1)  0x08 minor (1)  0x0c..0x17 reserved
  0x18 date created  0x1c date modified  0x20 index major (7)  0x24 index entry count
  0x28 index offset  0x2c index size  0x30 hole count  0x34 hole offset  0x38 hole size
  0x3c index minor (0, 1 or 2)

Index entries: type, group, instance, [instance-hi if index minor >= 2], offset, size.

DIR resource (type 0xE86B1EEF): one record per compressed entry: type, group, instance,
[instance-hi if index minor >= 2], uncompressed size.

Compressed data: dword compressed size (incl. itself), 0x10FB (stored as bytes 10 FB), 3-byte
big-endian uncompressed size, then the QFS control stream.
"""
import argparse
import collections
import struct
import sys

DIR_TYPE = 0xE86B1EEF


class DbpfError(Exception):
    pass


def qfs_decompress(data, expected_size=None):
    """Decompress a QFS/RefPack block that starts with the 9-byte header."""
    if len(data) < 9 or data[4] != 0x10 or data[5] != 0xFB:
        raise DbpfError("not a QFS block")
    size = (data[6] << 16) | (data[7] << 8) | data[8]
    if expected_size is not None and expected_size != size:
        raise DbpfError("QFS header size %d != expected %d" % (size, expected_size))
    out = bytearray()
    i = 9
    n = len(data)
    while i < n and len(out) < size:
        c0 = data[i]
        if c0 < 0x80:
            c1 = data[i + 1]
            plain = c0 & 3
            length = ((c0 & 0x1C) >> 2) + 3
            off = ((c0 & 0x60) << 3) + c1 + 1
            i += 2
        elif c0 < 0xC0:
            c1, c2 = data[i + 1], data[i + 2]
            plain = (c1 >> 6) & 3
            length = (c0 & 0x3F) + 4
            off = ((c1 & 0x3F) << 8) + c2 + 1
            i += 3
        elif c0 < 0xE0:
            c1, c2, c3 = data[i + 1], data[i + 2], data[i + 3]
            plain = c0 & 3
            length = ((c0 & 0x0C) << 6) + c3 + 5
            off = ((c0 & 0x10) << 12) + (c1 << 8) + c2 + 1
            i += 4
        elif c0 < 0xFC:
            plain = ((c0 & 0x1F) << 2) + 4
            length = 0
            off = 0
            i += 1
        else:
            plain = c0 & 3
            length = 0
            off = 0
            i += 1
        if plain:
            out += data[i:i + plain]
            i += plain
        if length:
            if off > len(out):
                raise DbpfError("QFS back-reference before start")
            start = len(out) - off
            if off >= length:
                out += out[start:start + length]
            else:  # overlapping copy repeats the pattern
                for k in range(length):
                    out.append(out[start + k])
    if len(out) != size:
        raise DbpfError("QFS produced %d bytes, header says %d" % (len(out), size))
    return bytes(out)


class Entry:
    __slots__ = ("type", "group", "instance", "instance_hi", "offset", "size",
                 "compressed", "uncompressed_size")

    def __init__(self, t, g, i, ihi, off, size):
        self.type, self.group, self.instance, self.instance_hi = t, g, i, ihi
        self.offset, self.size = off, size
        self.compressed = False
        self.uncompressed_size = size

    @property
    def key(self):
        return (self.type, self.group, self.instance, self.instance_hi)

    def __repr__(self):
        return "Entry(%08X %08X %08X %08X size=%d%s)" % (
            self.type, self.group, self.instance, self.instance_hi, self.size,
            " Z->%d" % self.uncompressed_size if self.compressed else "")


class Package:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.entries = []
        self._parse()

    def _parse(self):
        d = self.data
        if d[:4] != b"DBPF":
            raise DbpfError("not a DBPF file: %s" % self.path)
        major, minor = struct.unpack_from("<II", d, 4)
        if major != 1:
            raise DbpfError("unsupported DBPF major %d" % major)
        self.major, self.minor = major, minor
        # fields following the 12 reserved bytes at 0x0c: created, modified, index major,
        # count, offset, size, hole count, hole offset, hole size, index minor
        (self.date_created, self.date_modified, self.index_major, count, idx_off, idx_size,
         self.hole_count, self.hole_off, self.hole_size, self.index_minor) = \
            struct.unpack_from("<10I", d, 0x18)
        words = 6 if self.index_minor >= 2 else 5
        if idx_size != count * words * 4 and count:
            # tolerate: trust count, but flag
            self.index_size_mismatch = True
        pos = idx_off
        for _ in range(count):
            if self.index_minor >= 2:
                t, g, i, ihi, off, sz = struct.unpack_from("<6I", d, pos)
            else:
                t, g, i, off, sz = struct.unpack_from("<5I", d, pos)
                ihi = 0
            pos += words * 4
            self.entries.append(Entry(t, g, i, ihi, off, sz))
        self._by_key = {e.key: e for e in self.entries}
        self._apply_dir()

    def _apply_dir(self):
        de = [e for e in self.entries if e.type == DIR_TYPE]
        if not de:
            return
        raw = self.read(de[0])  # DIR itself is never compressed
        words = 5 if self.index_minor >= 2 else 4
        for p in range(0, len(raw) - words * 4 + 1, words * 4):
            if words == 5:
                t, g, i, ihi, usz = struct.unpack_from("<5I", raw, p)
            else:
                t, g, i, usz = struct.unpack_from("<4I", raw, p)
                ihi = 0
            e = self._by_key.get((t, g, i, ihi))
            if e is not None:
                e.compressed = True
                e.uncompressed_size = usz

    def raw(self, e):
        return self.data[e.offset:e.offset + e.size]

    def read(self, e):
        """Decompressed bytes of an entry."""
        raw = self.raw(e)
        if e.compressed:
            return qfs_decompress(raw, e.uncompressed_size)
        return raw

    def get(self, key):
        e = self._by_key.get(tuple(key))
        return None if e is None else self.read(e)

    def entry(self, key):
        return self._by_key.get(tuple(key))


def write_package(path, items, index_minor=2):
    """Write a package with every resource stored uncompressed (no DIR resource).

    items: iterable of ((type, group, instance, instance_hi), bytes). Used by the tests and
    the self-check to build a modified copy; the output is a valid DBPF 1.1 file.
    """
    items = list(items)
    words = 6 if index_minor >= 2 else 5
    body = bytearray()
    index = bytearray()
    for (t, g, i, ihi), data in items:
        off = 96 + len(body)
        body += data
        if index_minor >= 2:
            index += struct.pack("<6I", t, g, i, ihi, off, len(data))
        else:
            index += struct.pack("<5I", t, g, i, off, len(data))
    idx_off = 96 + len(body)
    hdr = bytearray(96)
    hdr[0:4] = b"DBPF"
    struct.pack_into("<II", hdr, 4, 1, 1)
    struct.pack_into("<10I", hdr, 0x18, 0, 0, 7, len(items), idx_off, len(items) * words * 4,
                     0, 0, 0, index_minor)
    with open(path, "wb") as f:
        f.write(bytes(hdr) + bytes(body) + bytes(index))


def open_package(path):
    return Package(path)


def _parse_hex(s):
    return int(s, 16)


def main(argv=None):
    ap = argparse.ArgumentParser(description="DBPF package tool")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list")
    p.add_argument("package")
    p = sub.add_parser("summary")
    p.add_argument("package")
    p = sub.add_parser("extract")
    p.add_argument("package")
    p.add_argument("type", type=_parse_hex)
    p.add_argument("group", type=_parse_hex)
    p.add_argument("instance", type=_parse_hex)
    p.add_argument("--instance-hi", type=_parse_hex, default=0)
    p.add_argument("-o", "--out", required=True)
    p.add_argument("--raw", action="store_true", help="write stored (compressed) bytes")
    a = ap.parse_args(argv)
    pkg = Package(a.package)
    if a.cmd == "list":
        print("type     group    instance instHi   size  usize comp")
        for e in pkg.entries:
            print("%08X %08X %08X %08X %6d %6d %s" % (
                e.type, e.group, e.instance, e.instance_hi, e.size, e.uncompressed_size,
                "Z" if e.compressed else "-"))
    elif a.cmd == "summary":
        from sims2_types import type_name
        c = collections.Counter()
        sz = collections.Counter()
        for e in pkg.entries:
            c[e.type] += 1
            sz[e.type] += e.uncompressed_size
        for t, n in sorted(c.items(), key=lambda kv: -kv[1]):
            print("%08X %-28s n=%-6d bytes=%d" % (t, type_name(t), n, sz[t]))
    else:
        e = pkg.entry((a.type, a.group, a.instance, a.instance_hi))
        if e is None:
            print("no such entry", file=sys.stderr)
            return 1
        with open(a.out, "wb") as f:
            f.write(pkg.raw(e) if a.raw else pkg.read(e))
    return 0


if __name__ == "__main__":
    sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
    sys.exit(main())
