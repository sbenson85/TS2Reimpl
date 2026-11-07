"""Stack objects whose addresses a function passes on, and their sizes, for comparing ours with the original's.

A callee that writes through a pointer to the caller's stack writes as much as the original caller reserved. If our
replacement reserves less (a struct modelled too small), the callee overwrites our frame: the runs46 crash, a 12-byte
resource key where the game writes 16, which the difftest's fakes (built from our own type) couldn't see. The
original's frame is evidence about those sizes even when the function doesn't byte-match, so tools/matchcheck.py
reports an escaped stack object that is smaller in ours than in the original as a warning to read both sides.

Heuristic, linear sweep: esp depth is followed through push/pop/sub/add, `mov esp, ebp` and `lea esp, [ebp - n]`
(frame-pointer and SEH frames: ebp-relative offsets count too); the pushes since the last call are taken as that
call's arguments and gone after it (callee pops, or the `add esp, N` that follows a cdecl call). The prologue's
pushes are not arguments: callee-saved registers, an SEH frame's state and handler (`push -1; push handler`), the
previous registration (`mov eax, fs:[0]; push eax`) and a security cookie (`xor eax, ebp; push eax`). Code after a
ret or jmp continues with the depth a forward branch to it had.

An object's size follows the frame analysis of tests/stack_out.cpp (FillOut, lane HI): an object runs from its
offset to the next offset the function writes, pushes or takes the address of (or indexes: an array's start), or to
the return address; offsets only read are fields of an object a callee filled, not boundaries. An upper bound, so it
can only under-report a too-small object when a neighbour is never written.
"""
import capstone
from capstone import x86_const as X86

_cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
_cs.detail = True


SAVED = {X86.X86_REG_EBX, X86.X86_REG_ESI, X86.X86_REG_EDI, X86.X86_REG_EBP}
BRANCHES = {"jmp", "je", "jne", "jb", "jbe", "ja", "jae", "jl", "jle", "jg", "jge", "js", "jns", "jo", "jno", "jp",
            "jnp", "jecxz", "loop", "loope", "loopne"}


def _reg(op, r=None):
    return op.type == X86.X86_OP_REG and (r is None or op.reg == r)


def analyse(code, scoped=False):
    """-> {"locals": size of the sub esp area, "calls": [[(offset, estimated size), ...] per call], "ids": [...],
    "objects": {offset: size}, "edges": sorted boundaries, "starts" / "fields": the boundaries scoped keeps / drops as
    fields}. Offsets are from esp at entry (the return address at 0): locals are negative.

    scoped (our side, where an object too small is what we look for, so boundaries that aren't one only add noise):
     - the compiler gives objects of disjoint scopes the same stack slots (0x2a81f0: a 12-byte scratch passed to one
       call in an `if` lies inside the 16-byte entry a loop passes to another), so a boundary that is only an address
       taken counts for an object only when that `lea` lies within the object's own span of code (its first to last
       access, the calls it is passed to included);
     - our constructors fill an object's fields one by one (0x2a81f0's 12-byte iterator: three `mov [ebp-x], 0`;
       0x474df0's cRZString: vtable, a 16-byte `movups`, then the size, capacity and a byte), so writes each within 3
       instructions of the previous one form a run whose lowest offset starts an object and whose other offsets are
       fields, not boundaries (unless one starts a run of its own elsewhere). Locals zeroed together merge with it:
       this side errs towards larger, i.e. towards fewer warnings.
    Never smaller than unscoped, so identical code can't warn."""
    insns = list(_cs.disasm(bytes(code), 0))
    depth = 0          # bytes below esp at entry (locals, saved registers, arguments being pushed)
    local_size = 0
    saved_phase = True  # prologue pushes (callee-saved registers, SEH frame) before the first call or argument push
    args = 0           # bytes pushed as arguments since the last call
    edges = set()      # entry-relative offsets (< 0) written, pushed, address taken or indexed: object boundaries
    hard = set()       # of those, the ones written, pushed or indexed (not only an address taken or a field)
    runs = []          # runs of writes to stack offsets, each within 3 instructions of the previous one
    last_write = None  # instruction index of the last write to a stack offset
    leas = {}          # offset -> addresses of the instructions taking its address
    seen = {}          # offset -> addresses of every instruction using it (and the calls it is passed to)
    escaped = []       # (call index, offset)
    pending = []
    calls = 0
    ids = []           # per call: ("v", slot offset) for a vtable call, ("d", bytes pushed for it) otherwise
    loads = {}         # register -> slot offset of the `mov reg, [reg2 + off]` that loaded it
    frame_regs = set()  # registers holding the previous SEH registration (fs:[0]) or a security cookie (prologue)
    skip_add = 0       # after a call: the bytes it was taken to pop (a cdecl `add esp, <that>` is then skipped)
    ebp_frame = False  # push ebp; mov ebp, esp (EH frames): ebp-relative locals
    ebp_at = 0         # depth when ebp was set
    at_branch = {}     # forward branch target -> (depth, args) when branching
    ended = False      # the previous instruction was a ret or jmp: no fall-through
    for n, i in enumerate(insns):
        m, ops = i.mnemonic, i.operands
        if ended:
            ended = False
            if i.address in at_branch:
                depth, args = at_branch[i.address]
        if m == "mov" and len(ops) == 2 and _reg(ops[0], X86.X86_REG_EBP) and _reg(ops[1], X86.X86_REG_ESP) \
                and calls == 0:
            ebp_frame, ebp_at = True, depth
        for k, op in enumerate(ops):
            if op.type != X86.X86_OP_MEM:
                continue
            if op.mem.base == X86.X86_REG_ESP:
                e = op.mem.disp - depth
            elif ebp_frame and op.mem.base == X86.X86_REG_EBP:
                e = op.mem.disp - ebp_at
            else:
                continue
            if e >= 0:
                continue
            # this instruction's esp before its own push or pop; a boundary: written, address taken or indexed
            seen.setdefault(e, []).append(i.address)
            # capstone 5 marks the stored operand of SSE moves (movups, movq...) as read: a mov's destination is written
            written = op.access & capstone.CS_AC_WRITE or k == 0 and len(ops) == 2 and m.startswith("mov")
            if written and op.mem.index == 0 and m != "push":
                edges.add(e)
                # writes each within 3 instructions of the previous one form a run (a constructor filling fields)
                if last_write is None or n - last_write > 3:
                    runs.append([])
                runs[-1].append(e)
                last_write = n
            elif written or op.mem.index != 0:
                edges.add(e)
                hard.add(e)
            elif m == "lea":
                edges.add(e)
                leas.setdefault(e, []).append(i.address)
            if m == "lea" and op.mem.index == 0:
                pending.append(e)
        if m == "mov" and len(ops) == 2 and _reg(ops[0]) and ops[1].type == X86.X86_OP_MEM \
                and ops[1].mem.base not in (0, X86.X86_REG_ESP, X86.X86_REG_EBP) and ops[1].mem.index == 0:
            loads[ops[0].reg] = ops[1].mem.disp
        elif ops and _reg(ops[0]) and ops[0].access & capstone.CS_AC_WRITE:
            loads.pop(ops[0].reg, None)
        # the SEH registration (mov r, fs:[0]) and the security cookie (xor r, ebp / esp) the prologue pushes
        if calls == 0 and len(ops) == 2 and _reg(ops[0]) and (
                m == "mov" and ops[1].type == X86.X86_OP_MEM and ops[1].mem.segment == X86.X86_REG_FS
                or m == "xor" and _reg(ops[1]) and ops[1].reg in (X86.X86_REG_EBP, X86.X86_REG_ESP)
                or m == "mov" and _reg(ops[1]) and ops[1].reg in frame_regs):
            frame_regs.add(ops[0].reg)
        elif ops and _reg(ops[0]) and ops[0].access & capstone.CS_AC_WRITE and m != "push":
            frame_regs.discard(ops[0].reg)
        if m in ("push", "pop"):
            skip_add = 0
        if m == "push":
            depth += 4
            edges.add(-depth)
            hard.add(-depth)
            if saved_phase and _reg(ops[0]) and ops[0].reg in SAVED:
                continue
            if calls == 0 and _reg(ops[0]) and ops[0].reg in frame_regs:
                continue  # previous SEH registration or security cookie
            if calls == 0 and ops[0].type == X86.X86_OP_MEM and ops[0].mem.segment == X86.X86_REG_FS:
                continue  # push dword ptr fs:[0]
            if ebp_frame and calls == 0 and ops[0].type == X86.X86_OP_IMM and args == 0 and not pending:
                continue  # SEH frame pushes (state, handler)
            saved_phase = False
            args += 4
        elif m == "pop":
            depth -= 4
            args = max(0, args - 4)
        elif m == "mov" and len(ops) == 2 and _reg(ops[0], X86.X86_REG_ESP) and _reg(ops[1], X86.X86_REG_EBP) \
                and ebp_frame:
            depth, args = ebp_at, 0
        elif m == "lea" and _reg(ops[0], X86.X86_REG_ESP) and ops[1].mem.index == 0:
            if ebp_frame and ops[1].mem.base == X86.X86_REG_EBP:
                depth, args = ebp_at - ops[1].mem.disp, 0
            elif ops[1].mem.base == X86.X86_REG_ESP:
                depth -= ops[1].mem.disp
        elif m in ("sub", "add") and _reg(ops[0], X86.X86_REG_ESP) and len(ops) == 2 and ops[1].type == X86.X86_OP_IMM:
            if m == "add" and skip_add and ops[1].imm == skip_add:
                skip_add = 0
                continue  # a cdecl call's cleanup: its arguments are already gone
            skip_add = 0
            if m == "sub" and saved_phase and calls == 0 and args == 0:
                local_size += ops[1].imm  # the locals (before or after the saved registers)
                depth += ops[1].imm
            else:
                depth += ops[1].imm if m == "sub" else -ops[1].imm
                args = max(0, args + (ops[1].imm if m == "sub" else -ops[1].imm))
        elif m == "call":
            op = ops[0]
            if op.type == X86.X86_OP_MEM and op.mem.base not in (0, X86.X86_REG_ESP, X86.X86_REG_EBP):
                ids.append(("v", op.mem.disp))
            elif op.type == X86.X86_OP_REG and op.reg in loads:
                ids.append(("v", loads[op.reg]))
            else:
                ids.append(("d", args))  # direct calls pair by the bytes pushed for them (their targets aren't known)
            loads = {}
            escaped.extend((calls, e) for e in pending)
            for e in pending:
                seen.setdefault(e, []).append(i.address)
            pending = []
            calls += 1
            saved_phase = False
            # the callee pops its arguments, or the `add esp, N` (N = what was pushed) after it does: skipped when it
            # comes before the next stack operation or branch, however many other instructions lie between
            depth -= args
            skip_add, args = args, 0
            continue
        elif m in BRANCHES or m.startswith("ret"):
            if m in BRANCHES and ops and ops[0].type == X86.X86_OP_IMM and ops[0].imm > i.address:
                at_branch.setdefault(ops[0].imm, (depth, args))
            ended = m == "jmp" or m.startswith("ret")
            skip_add = 0
    bounds = sorted(edges | {0})
    # A run's lowest offset starts an object and the rest are its fields (an address taken there is a member's: the
    # `lea` of a string's buffer), unless the run only rewrites fields of an object an earlier run started (a string's
    # reset: size, capacity, buffer[0]).
    fields = set()
    for r in runs:
        if min(r) in fields and min(r) not in hard:
            fields |= set(r) - hard
        else:
            hard.add(min(r))
            fields |= set(r) - hard
    fields -= hard

    def size(e):
        lo, hi = min(seen.get(e, [0])), max(seen.get(e, [0]))
        for x in bounds:
            if x <= e:
                continue
            if scoped and x not in hard and x != 0 and (
                    x in fields or not any(lo <= a <= hi for a in leas.get(x, []))):
                continue  # a field, or only an address taken outside this object's code (another scope's object)
            return x - e
        return -e
    per_call = [[] for _ in range(calls)]
    objects = {}
    for c, e in escaped:
        if (e, size(e)) not in per_call[c]:
            per_call[c].append((e, size(e)))
        objects[e] = size(e)
    return {"locals": local_size, "calls": per_call, "ids": ids, "objects": objects, "edges": bounds[:-1],
            "starts": sorted(hard), "fields": sorted(fields)}


def smaller_objects(ours, original):
    """Warnings for stack objects passed to calls that are smaller in ours than in the original. Calls are paired by
    identity in order (vtable calls by slot offset, direct calls as such), so a different call count or order
    pairs only what lines up."""
    import difflib
    a, b = analyse(ours, scoped=True), analyse(original)  # ours: no false warnings from shared slots
    out = []
    sm = difflib.SequenceMatcher(None, a["ids"], b["ids"], autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag != "equal":
            continue
        for i, j in zip(range(i1, i2), range(j1, j2)):
            xs, ys = sorted(s for _, s in a["calls"][i]), sorted(s for _, s in b["calls"][j])
            if len(xs) != len(ys):
                continue
            for s, u in zip(xs, ys):
                if s < u:
                    what = f"slot {a['ids'][i][1] // 4}" if a["ids"][i][0] == "v" else "direct call"
                    out.append(f"call #{i + 1} ({what}): stack object passed is {s} bytes in ours, {u} in the original")
    return out
