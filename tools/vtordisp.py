"""Lists vtordisp thunks (sub ecx,[ecx-4]; sub ecx,N; jmp T) whose body T is verified but which aren't.

    .venv/Scripts/python -I tools/vtordisp.py [--all]   # --all includes folded shared_vf_* (unhookable)
"""
import json, sys
from collections import Counter, defaultdict
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tsdb, pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

funcs = tsdb.functions()
names = {n["rva"]: n.get("name") for n in map(json.loads, open(ROOT / "db/names.jsonl"))}
status = tsdb.load_status()
verified = {r for r, e in status.items() if e["status"] == "verified"}
pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
md = Cs(CS_ARCH_X86, CS_MODE_32)
B = 0xA0000000
by_target = defaultdict(list)
for f in funcs:
    if f["size"] != 14: continue
    rva = int(f["rva"], 16)
    ins = list(md.disasm(pe.get_data(rva, 14), B + rva))
    if len(ins) != 3 or ins[0].op_str != "ecx, dword ptr [ecx - 4]" or ins[1].mnemonic != "sub" or ins[2].mnemonic != "jmp":
        continue
    t = hex(int(ins[2].op_str, 16) - B)
    by_target[t].append((f["rva"], ins[1].op_str.split(", ")[1]))
# Thunks with any status line already belong to some group.
todo = [(t, th) for t, ths in by_target.items() if t in verified for th in ths if th[0] not in status]
print(f"{sum(len(v) for v in by_target.values())} vtordisp thunks; {len(todo)} onto verified bodies without a status line")
if "--all" not in sys.argv:
    todo = [(t, th) for t, th in todo if not (names.get(th[0]) or "").startswith("shared_vf_")]
    print(f"{len(todo)} hookable (not folded)")
for t, (th, adj) in sorted(todo):
    st = status.get(th, {}).get("status", "-")
    print(f"  {th:>9} -> {t:>9} adj {adj:>6} {st:<13} {names.get(th) or ''}  [{status[t]['name'][:50]}]")
