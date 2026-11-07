#!/usr/bin/env python3
"""Reads the host's ts2services.txt (the "services" control command, host/control_services.cpp) for milestone I
(docs/roadmap.md: "the service spine is ours").

    tools/py -I tools/services.py FILE [--label main-menu]        per-service rows and the totals
    tools/py -I tools/services.py FILE --todo [--limit 15]        what is left: image classes and image slots by size

FILE is a run's ts2services.txt (in the game folder, or kept beside the run's logs as <stamp>-NAME.ts2services.txt).
It holds one section per "services" command ("services <frame> <label> framework=<class> <vtable>"); the last one is
read, or the last with --label.

Class names: an image vtable is named from db/classes.jsonl (its vftables' RVAs); one of ours by the RTTI name the host
wrote (class=..., our compiler's: "gz::cCTGFramework"), which needs no map file and does not change with the build. A
row is marked "I" when its class is on the milestone I list (I_LIST below, from docs/archive/proposals/next-milestones.md
section 3 "I" and docs/startup-map.md 4.5; the run is what confirms the list: rows not on it are named at the end).
Image vtables whose slot count differs from db's entry count are flagged ("db N"): the host counts slots up to the first
entry that is not a code pointer (host/shared/services.h).

--todo lists, for every service that is not ours (verdict image or mixed), its class and its image slots: for an image
vtable every entry db lists, for one of ours the slots the host listed (image-at=); open = not verified, not replaced and
not library (tsdb.resolve_done: jump stubs to done code and tiny leaves count as done). Classes by open bytes, largest
first; within a class, slots by size.
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

# Milestone I's classes by name (docs/archive/proposals/next-milestones.md section 3 "I": the named ones, and for "the message
# servers, timer, registry, ..." the classes docs/startup-map.md 4.5 gives). The run's list is the scope; this is what
# the proposal expected.
I_LIST = {
    "cTSPersistSystem", "nTSSG::cTSSGSystem", "cTSCustomContentSupport", "nTSCASCore::cTSCASSystem", "nTSWorld::cWorldDB",
    "cTSSimSystem", "cTSContentRegistry", "cGZWWWSystem", "cGZPersistResourceManager",
    "nGZSceneDBAnim::cSceneDBAnimSystem", "cTSMaterialCatalog", "cTSCommonSystem", "cRZLanguageManager", "cHitMan",
    "nGZLog::cService", "cTSWinCheatConsole", "nGZGraphic4::cGZCanvas", "nGZGraphic4::cGZGraphicSystem", "cGZFileSystem",
    "cTSPatternCatalog", "cGZSndMgr", "cRZNetManager",
    "cGZMessageServer", "cGZMessageServer2", "cGZMessageQueueManager2", "cGZTimerService", "cGZRegistry",
    "cGZClipboardManager", "cRZCallbackSystemService", "nGZEvent::cEventSystemService", "cGZDrawSystem", "cGZWinProcMgr",
    "nGZSceneGraph::cSceneGraphSystem", "cTSSkinCatalog", "cTSBinCatalog", "cTSCASBodyPartSystem", "cGZUIScriptService",
}

HEAD = re.compile(r"^services (\d+) (\S+)(?: framework=(\S+) (\S+))?$")
OBJECT = re.compile(r"^(service|listed) (.*) (ours|image|mixed|unknown)$")
FACE = re.compile(r"^  \+0x([0-9a-f]+) (?:(image|ours):0x([0-9a-f]+)|unknown) slots=(\d+) image=(\d+)(?: image-at=(\S+))?$")
SUMMARY = re.compile(r"^(\d+) services: (\d+) ours, (\d+) image, (\d+) mixed \(ours with image slots\)(?:, (\d+) unknown)?$")
IDMAP = re.compile(r"^idmap (\d+) entries, (\d+) differ$")


def parse(text):
    """The sections of a ts2services.txt: [{frame, label, framework, framework_vtable, objects, idmap, summary,
    incomplete}]; objects: {kind (service/listed), id, prio, tick, idle, cls, at, verdict, faces}; faces: {offset, where
    (image/ours/None), vtable, slots, image, image_at [(slot, rva)]}."""
    sections = []
    for line in text.splitlines():
        m = HEAD.match(line)
        if m:
            sections.append({"frame": int(m[1]), "label": m[2], "framework": m[3], "framework_vtable": m[4],
                             "objects": [], "idmap": None, "summary": None, "incomplete": False})
            continue
        if not sections:
            continue
        sec = sections[-1]
        m = OBJECT.match(line)
        if m:
            fields = dict(f.split("=", 1) for f in m[2].split())
            sec["objects"].append({"kind": m[1], "id": fields.get("id"), "prio": int(fields["prio"]) if "prio" in fields else None,
                                   "tick": fields.get("tick") == "1", "idle": fields.get("idle") == "1",
                                   "cls": fields.get("class", "?"), "at": fields.get("at"), "verdict": m[3], "faces": []})
            continue
        m = FACE.match(line)
        if m and sec["objects"]:
            at = [(int(s), int(r, 16)) for s, r in (p.split(":") for p in m[6].split(","))] if m[6] else []
            sec["objects"][-1]["faces"].append({"offset": int(m[1], 16), "where": m[2], "vtable": int(m[3], 16) if m[3] else None,
                                                "slots": int(m[4]), "image": int(m[5]), "image_at": at})
            continue
        m = IDMAP.match(line)
        if m:
            sec["idmap"] = (int(m[1]), int(m[2]))
            continue
        m = SUMMARY.match(line)
        if m:
            sec["summary"] = {"services": int(m[1]), "ours": int(m[2]), "image": int(m[3]), "mixed": int(m[4]),
                              "unknown": int(m[5] or 0)}
            continue
        if line.startswith("(containers not read"):
            sec["incomplete"] = True
    return sections


def pick(sections, label=None):
    chosen = [s for s in sections if label is None or s["label"] == label]
    if not chosen:
        raise SystemExit(f"no services section{' labelled ' + label if label else ''} in the file")
    return chosen[-1]


def vtable_index(classes):
    """image vtable RVA -> (class name, face offset, entry RVAs) from db/classes.jsonl rows."""
    out = {}
    for c in classes:
        for v in c.get("vftables", []):
            out[int(v["rva"], 16)] = (c["name"], v["offset"], v["entries"])
    return out


def class_name(obj, vtables):
    """The image class of the object's primary vtable (db), else the RTTI name the host wrote."""
    for f in obj["faces"]:
        if f["where"] == "image" and f["vtable"] in vtables and vtables[f["vtable"]][1] == f["offset"]:
            return vtables[f["vtable"]][0]
    return obj["cls"]


def on_list(name, listed=I_LIST):
    """On the list by its full name, or with leading namespaces dropped (ours may sit in a namespace the original's
    class doesn't: gz::cCTGFramework)."""
    parts = name.split("::")
    return any("::".join(parts[i:]) in listed for i in range(len(parts)))


def db_mismatches(obj, vtables):
    """[(offset, db entry count)] for image faces whose slot count differs from db's."""
    return [(f["offset"], len(vtables[f["vtable"]][2])) for f in obj["faces"]
            if f["where"] == "image" and f["vtable"] in vtables and len(vtables[f["vtable"]][2]) != f["slots"]]


def face_text(f, mismatch=None):
    where = f"{f['where']}:0x{f['vtable']:x}" if f["where"] else "unknown"
    text = f"+0x{f['offset']:x} {where} {f['image']}/{f['slots']}"
    return text + (f" (db {mismatch})" if mismatch is not None else "")


def report(sec, vtables, out=sys.stdout):
    fw = f" (framework {sec['framework']} {sec['framework_vtable']})" if sec["framework"] else ""
    print(f"services at frame {sec['frame']}, {sec['label']}{fw}", file=out)
    print(f"  {'prio':>9} {'id':8} T I {'verdict':7} I  {'class':40} faces (image slots/slots)", file=out)
    off_list, on_count, on_image = [], 0, 0
    for o in sec["objects"]:
        name = class_name(o, vtables)
        mark = "I" if on_list(name) else "-"
        if o["kind"] == "service":
            if mark == "I":
                on_count += 1
                on_image += o["verdict"] != "ours"
            else:
                off_list.append(name)
        mism = dict(db_mismatches(o, vtables))
        faces = ", ".join(face_text(f, mism.get(f["offset"])) for f in o["faces"])
        prio = f"{o['prio']:>9}" if o["prio"] is not None else f"{'listed':>9}"
        print(f"  {prio} {o['id'] or '-':8} {'T' if o['tick'] else '-'} {'I' if o['idle'] else '-'} {o['verdict']:7} {mark}  "
              f"{name:40} {faces}", file=out)
    s = sec["summary"]
    if s:
        unknown = f", {s['unknown']} unknown" if s["unknown"] else ""
        print(f"totals: {s['services']} services: {s['ours']} ours, {s['image']} image, {s['mixed']} mixed{unknown}; "
              f"on the I list {on_count} ({on_image} not ours yet)", file=out)
    else:
        print("totals: no summary line (the section is cut short)", file=out)
    if sec["idmap"] and sec["idmap"][1]:
        print(f"id map: {sec['idmap'][1]} of {sec['idmap'][0]} entries differ from the priority container", file=out)
    if sec["incomplete"]:
        print("the host could not read the containers to their end", file=out)
    if off_list:
        print(f"not on the I list ({len(off_list)}): {', '.join(sorted(set(off_list)))}", file=out)


def todo(sec, vtables, funcs, status, done):
    """[(class, open bytes, total bytes, [(rva, size, name, status, where, open)], verdict)] for the services not ours,
    by open bytes; each class's slots by size, open ones first. where: "<vtable>:<slot>" of the first face listing the
    function."""
    by_class, verdicts = {}, {}
    for o in sec["objects"]:
        if o["kind"] != "service" or o["verdict"] not in ("image", "mixed"):
            continue
        name = class_name(o, vtables)
        verdicts[name] = o["verdict"]
        slots = by_class.setdefault(name, {})
        for f in o["faces"]:
            if f["where"] == "image" and f["vtable"] in vtables:
                pairs = [(i, int(r, 16)) for i, r in enumerate(vtables[f["vtable"]][2])]
            else:
                pairs = f["image_at"]
            for slot, rva in pairs:
                slots.setdefault(f"0x{rva:x}", f"0x{f['vtable']:x}:{slot}")
    out = []
    for name, slots in by_class.items():
        rows = []
        for rva, where in slots.items():
            f = funcs.get(rva, {})
            rows.append((rva, f.get("size", 0), f.get("name", "?"), status.get(rva, {}).get("status", "-"), where,
                         rva not in done))
        rows.sort(key=lambda r: (not r[5], -r[1], r[0]))
        out.append((name, sum(r[1] for r in rows if r[5]), sum(r[1] for r in rows), rows, verdicts[name]))
    out.sort(key=lambda c: (-c[1], c[0]))
    return out


def print_todo(classes, limit, out=sys.stdout):
    total = sum(c[1] for c in classes)
    print(f"open in services not ours: {total:,} B in {len(classes)} classes (open = not verified/replaced/library)",
          file=out)
    for name, open_bytes, all_bytes, rows, verdict in classes:
        n_open = sum(1 for r in rows if r[5])
        # An image object whose slots are all done still carries the image's vtables: its creation is what is left.
        note = "; the object still has the image's vtables (constructor or creator not ours)" if verdict == "image" else ""
        print(f"{name} ({verdict}): {open_bytes:,} B open in {n_open} of {len(rows)} slot functions ({all_bytes:,} B){note}",
              file=out)
        shown = [r for r in rows if r[5]][:limit] if limit else [r for r in rows if r[5]]
        for rva, size, fname, st, where, _ in shown:
            print(f"    {rva:>9} {size:>6,} B  {st:13} {where:18} {fname}", file=out)
        if len(shown) < n_open:
            print(f"    ... {n_open - len(shown)} more", file=out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--label", help="the section with this label (default: the last section)")
    ap.add_argument("--todo", action="store_true", help="image classes and their open slots, largest first")
    ap.add_argument("--limit", type=int, default=15, help="--todo: open slots shown per class (0: all)")
    args = ap.parse_args(argv)
    sec = pick(parse(Path(args.file).read_text(encoding="utf-8", errors="replace")), args.label)
    vtables = vtable_index(tsdb.jsonl("classes.jsonl"))
    if not args.todo:
        report(sec, vtables)
        return
    funcs = {f["rva"]: f for f in tsdb.functions()}
    status = tsdb.load_status()
    done_base = {r for r, e in status.items() if e["status"] in ("verified", "replaced")} | tsdb.library_rvas()
    print_todo(todo(sec, vtables, funcs, status, tsdb.resolve_done(done_base, funcs)), args.limit)


if __name__ == "__main__":
    main()
