#!/usr/bin/env python3
"""Compare two Sims 2 user-data snapshots (or two neighbourhood folders).

  python -I tools/savediff.py A B [--json report.json] [-v] [--fail-on persistent,unknown]

Packages are paired by relative path, then resources by (type, group, instance, instance-hi).
Resource content is compared after decompression, so re-compression alone is not a change.
The DIR resource is storage metadata and is excluded from pairing. Each difference is
classified by resource type using tools/sims2_types.py:

  persistent  a save/reload must preserve it
  transient   legitimately differs (timestamps, thumbnails, caches); a reason is recorded
  unknown     not yet classified; treated as significant
  excluded    differs only in fields excluded with an established reason (sims2_types.FIELD_EXCLUSIONS)

Exit status: 0 if nothing differs in the classes named by --fail-on (default
persistent,unknown), 1 if something does, 2 on usage errors.
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dbpf                                      # noqa: E402
from sims2_types import (type_info, file_class,  # noqa: E402
                         excluded_ranges, P, T, U, X)

# ----------------------------------------------------------------------------------------
# Field parsers. Each takes (data, entry) and returns an ordered dict name -> value, or
# raises. Only fields whose layout was verified against the real saves are named; everything
# else stays in the raw byte ranges. Confidence is documented in docs/save-diff.md.
# ----------------------------------------------------------------------------------------

TYPE_FAMI = 0x46414D49
TYPE_SDSC = 0xAACE2EFB
TYPE_LOT_OBJECT = 0xFA1C39F7


def parse_fami(data, entry=None):
    """Family information ('IMAF'), versions 85 (0x55) and 84 (0x54). Words (u32, LE), v85:
    0 magic, 1 version, 2 unknown, 3/4 lot id (equal; 0 when not living on a lot),
    5 unknown, 6 unknown (family-number-like), 7 FUNDS, 8,9 unknown, 10 member count n,
    then n member ids, one more id-like word, a flag word, and a second funds-like word.
    v84 has one lot-id word fewer, so every field from 3 on is one word earlier.
    The two funds-like words are equal in the neighbourhood package; in some suburb
    families word 7 is 32000/35000 while the tail word is 20000, so which one is the
    current balance is not settled: both are reported."""
    if data[:4] != b"IMAF":
        raise ValueError("bad FAMI magic")
    w = struct.unpack_from("<%dI" % (len(data) // 4), data)
    ver = w[1]
    if ver == 0x55:
        s = 0
    elif ver == 0x54:
        s = -1
    else:
        raise ValueError("unsupported FAMI version %d" % ver)
    n = w[10 + s]
    if len(w) != 11 + s + n + 3:
        raise ValueError("unexpected FAMI length")
    f = {"version": ver, "lot_id": w[3], "funds": w[7 + s], "member_count": n}
    if s == 0:
        f["lot_id_2"] = w[4]
    for i in range(n):
        f["member[%d]" % i] = "0x%08X" % w[11 + s + i]
    f["tail_id"] = "0x%08X" % w[11 + s + n]
    f["tail_flag"] = w[12 + s + n]
    f["funds_tail"] = w[13 + s + n]
    return f


def parse_sdsc(data, entry=None):
    """Sim description. Only the version (u32 @0) and GUID (u32 @0x1DC, a community fact
    also used by OpenTS2) are named. The u16 arrays at 0x10 (6), 0x1E (12) and 0x6A (5) are
    value tables in 0..1000 that look like personality, skills and interests; their exact
    mapping is unconfirmed, so they are exposed by offset only."""
    if len(data) < 4:
        raise ValueError("SDSC too short")
    f = {"version": struct.unpack_from("<I", data, 0)[0]}
    if len(data) >= 0x1E0:   # shorter records are older versions with another layout
        f["guid"] = "0x%08X" % struct.unpack_from("<I", data, 0x1DC)[0]
        for base, n in ((0x10, 6), (0x1E, 12), (0x6A, 5)):
            for i in range(n):
                f["u16@0x%X" % (base + 2 * i)] = struct.unpack_from("<H", data, base + 2 * i)[0]
    return f


class _R:
    def __init__(self, d, pos=0):
        self.d, self.p = d, pos

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.p)[0]
        self.p += 4
        return v

    def pstr(self):
        n = 0
        shift = 0
        while True:  # 7-bit variable length prefix
            b = self.d[self.p]
            self.p += 1
            n |= (b & 0x7F) << shift
            shift += 7
            if not b & 0x80:
                break
        s = self.d[self.p:self.p + n].decode("latin-1")
        if len(s) != n:
            raise ValueError("truncated string")
        self.p += n
        return s


def parse_lot_object(data, entry=None):
    """Lot object ('cObject' possibly wrapped in cAnimatable/cLocomotable/cPerson).
    64 zero bytes, then per class: type id, version, class name; wrappers start with the
    next class's header. cObject: resource name, skin list, position (3 floats),
    rotation quaternion (4 floats), bones, blend shapes. Only the cObject part is parsed;
    wrapper-specific tails stay raw."""
    r = _R(data, 64)
    chain = []
    while True:
        r.u32()  # type id
        version = r.u32()
        name = r.pstr()
        chain.append(name)
        if name == "cObject":
            break
        if name not in ("cAnimatable", "cLocomotable", "cPerson"):
            raise ValueError("unknown lot object class %r" % name)
    f = {"class": "/".join(chain), "object_version": version}
    f["resource"] = r.pstr()
    nskins = r.i32()
    if not 0 <= nskins < 1000:
        raise ValueError("bad skin count")
    for _ in range(nskins):
        r.pstr()
        if version > 16:
            r.u32()
        for _ in range(r.i32()):
            r.pstr()
            r.pstr()
    f["position"] = tuple(round(r.f32(), 6) for _ in range(3))
    f["rotation"] = tuple(round(r.f32(), 6) for _ in range(4))
    f["skin_count"] = nskins
    return f


TYPE_OBJM = 0x4F626A4D
OBJM_VERSION = 0xD8


def objm_layout(data):
    """The lot's object-module record OBJM 4F626A4D instance 1, as cEdithObjectModule::vf5 (0x42b370) writes it:
    64 zero bytes before the stream, then vf111 (0x42ee60): u32 0, u32 version 0xD8, u32 'OBJM', u32 count and
    `count` pairs (object id = object slot 47, a per-object type word = slot 57 of the object's +0xf4 object) in
    the module's object-list order (insertion order: 0x422910 adds each object through 0x41db50); then vf114
    (0x42ec30): u32 N and N words, one per object id 1..N, of the schedule table at module +0x1a30 (0 for a
    free id). The schedule word is (flags << 16) | countdown (vf71 0x42b6c0 sets the low half, vf73 0x4280e0
    reads it, vf74/vf75 set and test flag bits); the rest of the record follows unparsed. Returns None for
    anything else (instance 2 has another layout)."""
    if len(data) < 0x54:
        return None
    zero, ver, tag, count = struct.unpack_from("<4I", data, 0x40)
    if zero != 0 or ver != OBJM_VERSION or tag != TYPE_OBJM or 0x54 + 8 * count + 4 > len(data):
        return None
    o = 0x50
    objects = [struct.unpack_from("<II", data, o + 8 * i) for i in range(count)]
    o += 8 * count
    n = struct.unpack_from("<I", data, o)[0]
    if o + 4 + 4 * n > len(data):
        return None
    schedule = list(struct.unpack_from("<%dI" % n, data, o + 4))
    return {"objects": objects, "schedule_at": o + 4, "schedule": schedule, "rest_at": o + 4 + 4 * n}


def parse_objm(data, entry=None):
    lay = objm_layout(data)
    if lay is None:
        return None
    return {"objects": len(lay["objects"]), "schedule_ids": len(lay["schedule"]),
            "object_ids": sorted(i for i, _ in lay["objects"])}


PARSERS = {TYPE_FAMI: parse_fami, TYPE_SDSC: parse_sdsc, TYPE_LOT_OBJECT: parse_lot_object, TYPE_OBJM: parse_objm}


def field_diff(ta, a, b, ea=None, eb=None):
    """Return a list of (field, old, new), or None if no parser / parse failed."""
    fn = PARSERS.get(ta)
    if fn is None:
        return None
    try:
        fa, fb = fn(a, ea), fn(b, eb)
    except Exception as ex:                      # noqa: BLE001
        return [("<parse-error>", str(ex), "")]
    if fa is None or fb is None:                 # a parser that does not apply to this record
        return None
    out = []
    for k in list(fa) + [k for k in fb if k not in fa]:
        va, vb = fa.get(k), fb.get(k)
        if va != vb:
            out.append((k, va, vb))
    return out


# ----------------------------------------------------------------------------------------
# Byte-range diff
# ----------------------------------------------------------------------------------------

def diff_ranges(a, b, limit=64):
    """Mismatching [start, end) byte runs over the common length, plus a tail run if the
    lengths differ. Returns (ranges, total_run_count)."""
    n = min(len(a), len(b))
    runs = []
    stack = [(0, n)]
    leaves = []
    while stack:
        lo, hi = stack.pop()
        if a[lo:hi] == b[lo:hi]:
            continue
        if hi - lo <= 32:
            leaves.append((lo, hi))
        else:
            mid = (lo + hi) // 2
            stack.append((mid, hi))
            stack.append((lo, mid))
    for lo, hi in sorted(leaves):
        i = lo
        while i < hi:
            if a[i] != b[i]:
                j = i
                while j < hi and a[j] != b[j]:
                    j += 1
                if runs and runs[-1][1] == i:
                    runs[-1] = (runs[-1][0], j)
                else:
                    runs.append((i, j))
                i = j
            else:
                i += 1
    if len(a) != len(b):
        runs.append((n, max(len(a), len(b))))
    return runs[:limit], len(runs)


# ----------------------------------------------------------------------------------------
# Snapshot comparison
# ----------------------------------------------------------------------------------------

def list_files(root):
    out = {}
    for dp, _dn, fn in os.walk(root):
        for f in fn:
            ap = os.path.join(dp, f)
            out[os.path.relpath(ap, root).replace("\\", "/")] = ap
    return out


def _key_str(k):
    return "%08X/%08X/%08X/%08X" % k


def compare_packages(pa, pb, relpath, report):
    A, B = dbpf.Package(pa), dbpf.Package(pb)
    ea = {e.key: e for e in A.entries if e.type != dbpf.DIR_TYPE}
    eb = {e.key: e for e in B.entries if e.type != dbpf.DIR_TYPE}
    storage_only = 0
    unchanged = 0
    for k in sorted(set(ea) | set(eb)):
        t = k[0]
        name, cls, reason = type_info(t)
        rec = {"file": relpath, "key": _key_str(k), "type": t, "type_name": name,
               "class": cls}
        if reason:
            rec["reason"] = reason
        if k not in ea:
            rec.update(status="added", size=eb[k].uncompressed_size)
            report["resources"].append(rec)
            continue
        if k not in eb:
            rec.update(status="removed", size=ea[k].uncompressed_size)
            report["resources"].append(rec)
            continue
        x, y = ea[k], eb[k]
        if (x.compressed == y.compressed and A.raw(x) == B.raw(y)):
            unchanged += 1
            continue
        da, db = A.read(x), B.read(y)
        if da == db:
            storage_only += 1
            continue
        exa, exb = excluded_ranges(t, da), excluded_ranges(t, db)
        if exa or exb:
            ma, mb = bytearray(da), bytearray(db)
            for m, ex in ((ma, exa), (mb, exb)):
                for lo, hi, _ in ex:
                    m[lo:hi] = b"\0" * (hi - lo)
            reasons = sorted({r for _, _, r in exa + exb})
            rec["excluded"] = [[lo, hi] for lo, hi, _ in exa]
            if bytes(ma) == bytes(mb):
                rec.update(status="changed", size_a=len(da), size_b=len(db), ranges=[],
                           range_count=0, reason="; ".join(reasons))
                rec["class"] = X
                report["resources"].append(rec)
                continue
            da_cmp, db_cmp = bytes(ma), bytes(mb)
        else:
            da_cmp, db_cmp = da, db
        ranges, nruns = diff_ranges(da_cmp, db_cmp)
        rec.update(status="changed", size_a=len(da), size_b=len(db),
                   ranges=[[lo, hi] for lo, hi in ranges], range_count=nruns)
        fd = field_diff(t, da, db, x, y)
        if fd is not None:
            rec["fields"] = [[f, _jsonable(o), _jsonable(n)] for f, o, n in fd]
        report["resources"].append(rec)
    hdr = []
    if A.date_created != B.date_created:
        hdr.append("date_created")
    if A.date_modified != B.date_modified:
        hdr.append("date_modified")
    report["packages"].append({"file": relpath, "entries_a": len(ea), "entries_b": len(eb),
                               "unchanged": unchanged, "storage_only": storage_only,
                               "header_timestamp_diff": hdr})


def _jsonable(v):
    if isinstance(v, tuple):
        return list(v)
    return v


def compare(root_a, root_b):
    fa, fb = list_files(root_a), list_files(root_b)
    report = {"a": root_a, "b": root_b, "files": [], "packages": [], "resources": []}
    same_files = 0
    for rel in sorted(set(fa) | set(fb)):
        cls, why = file_class(rel)
        is_pkg = rel.lower().endswith(".package")
        if is_pkg:
            cls, why = None, ""   # classified per resource
        rec = {"file": rel, "class": cls, "reason": why}
        if rel not in fa:
            rec["status"] = "added"
            report["files"].append(rec)
            continue
        if rel not in fb:
            rec["status"] = "removed"
            report["files"].append(rec)
            continue
        with open(fa[rel], "rb") as f:
            da = f.read()
        with open(fb[rel], "rb") as f:
            db = f.read()
        if da == db:
            same_files += 1
            continue
        if is_pkg:
            try:
                compare_packages(fa[rel], fb[rel], rel, report)
            except Exception as ex:                   # noqa: BLE001
                rec.update(status="changed", error=str(ex))
                rec["class"] = U
                report["files"].append(rec)
            continue
        ranges, nruns = diff_ranges(da, db)
        rec.update(status="changed", size_a=len(da), size_b=len(db),
                   ranges=[[lo, hi] for lo, hi in ranges], range_count=nruns)
        report["files"].append(rec)
    report["same_files"] = same_files
    report["summary"] = summarize(report)
    return report


def summarize(report):
    s = {c: {"added": 0, "removed": 0, "changed": 0} for c in (P, T, U, X)}
    for r in report["resources"]:
        s[r["class"]][r["status"]] += 1
    for r in report["files"]:
        s[r["class"] or U][r["status"]] += 1
    return s


def text_summary(report, verbose=False, limit=40):
    lines = []
    s = report["summary"]
    lines.append("A: %s" % report["a"])
    lines.append("B: %s" % report["b"])
    with_content = len({r["file"] for r in report["resources"]})
    lines.append("byte-identical files: %d; packages differing on disk: %d "
                 "(resource content differs in %d)"
                 % (report["same_files"], len(report["packages"]), with_content))
    for c in (P, U, T, X):
        v = s[c]
        lines.append("%-10s added %-5d removed %-5d changed %-5d" %
                     (c, v["added"], v["removed"], v["changed"]))
    shown = 0
    order = {P: 0, U: 1, T: 2, X: 3}
    rows = [r for r in report["files"]] + report["resources"]
    rows.sort(key=lambda r: (order[r["class"] or U], r["file"]))
    for r in rows:
        if shown >= limit and not verbose:
            lines.append("... %d more (use -v or the JSON report)" % (len(rows) - shown))
            break
        shown += 1
        if "key" in r:
            head = "%-8s %-10s %-18s %s %s" % (r["status"].upper(), r["class"],
                                               r["type_name"], r["file"], r["key"])
            if r["status"] == "changed":
                head += "  %d->%d bytes, %d range(s) %s" % (
                    r["size_a"], r["size_b"], r["range_count"],
                    " ".join("[0x%X,0x%X)" % tuple(x) for x in r["ranges"][:4]))
            lines.append(head)
            if r["class"] == X:
                lines.append("           excluded: %s" % r["reason"][:150])
            for f, o, n in r.get("fields", [])[:8]:
                lines.append("           %s: %s -> %s" % (f, o, n))
        else:
            lines.append("%-8s %-10s FILE %s%s" % (r["status"].upper(), r["class"] or U,
                                                  r["file"],
                                                  "  (%s)" % r["reason"] if r["reason"] else ""))
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--json", help="write the full report here")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--fail-on", default="persistent,unknown",
                    help="comma list of classes that make the exit status 1")
    a = ap.parse_args(argv)
    for p in (a.a, a.b):
        if not os.path.isdir(p):
            print("not a directory: %s" % p, file=sys.stderr)
            return 2
    rep = compare(a.a, a.b)
    print(text_summary(rep, a.verbose))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(rep, f, indent=1)
    bad = set(x.strip() for x in a.fail_on.split(",") if x.strip())
    return 1 if any(sum(rep["summary"][c].values()) for c in bad) else 0


if __name__ == "__main__":
    sys.exit(main())
