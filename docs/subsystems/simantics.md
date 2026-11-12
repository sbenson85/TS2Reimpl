# SimAntics interpreter and primitives (subsystem notes)

The working notes of the SimAntics subsystem lane: what is done, the conventions every port here
follows, the pitfalls lanes hit, and what is left. Keep it short and current: the lane that changes
the subsystem updates this file on its branch (facts, not decompiler output). Background and the
opcode map: `docs/subsystems.md` "SimAntics VM". Verification route: `trace-verified` by default
(`docs/lane/brief.md` "Simulation lanes").

## Done (2026-10-13)

- Dispatch: `cTSObject::vf332` 0x534650 and `cTSPerson::vf332` 0x51e4c0 are ours
  (`src/simantics/PrimDispatch.cpp`): a handler table (`DefaultPrimHandlers`, `PersonPrimHandlers`)
  that calls our handlers directly. Consequence: a handler's own hook counter stays 0 in game; count
  its opcode in the simtrace (`grep -c ' op 33 '`) instead.
- Operand reading: `cTSObject::vf4_7` 0x531700 (`src/simantics/OperandReader.*`) and the person
  override 0x51b870 (`PersonReadOperand`).
- Object handlers ours: 0x00, 0x02, 0x07, 0x08, 0x0b-0x10, 0x12, 0x14, 0x17, 0x19, 0x1a, 0x1c, 0x1f,
  0x20-0x24, 0x2a, 0x2e, 0x30-0x33, 0x71, 0x73, 0x76, 0x77, 0x7a-0x7c, 0x7e.
- Person handlers ours: 3, 0x11 IdleForInput, 0x1d, 0x1e selector 0, 0x25 (`src/simantics/PrimPerson*`).
- String-token expansion `cTSObject::vf262` 0x535130 (`$Local1`, `$Neighbor`, ... in dialog and action texts) and its marker finder FindToken 0x534d90:
  ours, see "Lane AE" below.

## Left

- Object opcode 0x01 GenericSimCall at 0x58bc50: every case value is ported and the function is hooked (lane Z2, `reimplemented`),
  see "GenericSimCall (0x01)" below. The dispatch table still needs `h[0x01]` pointed at `PrimGscObject::GenericSimCall` and the
  trace suite has to promote it.
- The shared animate/look-at/light case (0x69-0x6c, 0x6e, 0x6f) forwards to slot 246 of another
  object: those implementations are elsewhere (not yet located).
- Original callees of the person handlers (lane Y's `original_calls`: 0x515940, 0x5155b0, 0x516c40,
  0x516160, 0x5150d0, 0x515270, 0x4529c0, 0x5248f0, 0x525380, 0x512c30, 0x44f510/0x44f540, 0x451060,
  0x451f00, 0x5120c0, 0x5205c0, 0x523da0, 0x1d49e0, 0x520360, 0x50da10) and of Snap (0x5a6780 snap
  worker 1.7 KB, 0x7dccd0, 0x1a6c40, 0x7dd0d0, 0x7f02f0, 0x7f04e0, 0x7f0ca0, 0x7f0c00).
- The interpreter loop and cTSTreeSim/cTSTreeStack/cTSTreeStackElem are done (lanes AB and AC, below): the interpreter
  reaches no original code any more (the record/replay table and the cTSObject glue were lane AC's).
- Trace coverage the current scenarios lack: 0x0d, 0x77, 0x7b, ResolveEntityId (never reached in
  lot-simtrace); 0x73 beyond its early return (needs the debug console); Snap's modes 0-4 and failure
  branches; IdleForInput's queued-interaction paths. Writing scenarios for these is part of the work.

## Conventions

- Handlers are `__thiscall (frame, param)`, ret 8, returning the next-node code in eax (0 false,
  1 true, 2 yield, 3). Errors are `throw static_cast<short>(code)`.
- Param bytes are read directly through `PrimParamView` (the game's per-field accessors are plain
  field reads; lanes W and X list them). All `*Param` classes share one 0x18-byte layout.
- Operands go through the object's secondary vtable slot 7 (7 stack args, value in ax), which our
  dispatch now routes to `SimOperandSource::Read` / `PersonOperandSource::Read`.
- Reproduced original bugs are marked `// ORIGINAL-BUG:` (several missing null checks in 0x33, 0x73,
  IdleForInput; 0x33 mode 1 calls two inventory slots with too few arguments).

## Pitfalls

- The decompiler misattributes pushes around nested calls (e.g. `push 0; push 0; push 1` before a
  slot-63 call belong to the next call). Check every call's arity against `ret N` / stack balance.
- Function boundaries: large switch functions may be cut at the jump (0x01). Compare the size in
  db/ against the jump table targets before planning.
- Harness: originals that read a cached singleton global fault (null in the harness); redirect the
  getter in the test, and exclude such opcodes from the dispatch mapping test with a reason.
- Python edits: keep LF or CRLF as the file has it (`newline=''`); text mode broke mutate.py once.

## GenericSimCall (0x01), handler 0x58bc50 (lanes Z and Z2, 2026-10-13)

Code: `src/simantics/PrimGenericSimCall.h` (class `GenericSimCall`: the object, frame, param and the four prologue results; one
member per case), `PrimGenericSimCall.cpp` (prologue and shared helpers), `PrimGenericSimCallSwitch.cpp` (the switch), `...A..F.cpp`
(the cases, grouped by what they touch), `...Message.h` / `...Math.h` (helpers). Lane Z left 16 cases throwing `GscUnportedCase`;
lane Z2 ported them and removed it (see "Lane Z2" below). Status entry: db/status/SimAnticsPrimsGenericSimCall.jsonl.

Shape of the original: unoptimised, statement-at-a-time code (every temporary goes through a stack slot, every `Arg(i)` is a fresh
vtable call); one `_chkstk` frame of 0x1904 bytes; an EH frame with no try block (EHs), so unwinding only runs the holders'
destructors. Prologue: `this->[0x40]` (the context) slots 9, 10, 26, 24 are called in this order and kept (`mL2c`, `mL24`, `mL1c`,
`mL18c`); ebp-0x18 holds the result (starts 1; a case may store 0/2 or `jmp 0x5986e2` with its own eax). The switch value is the word
at param +4 (through 0x141120 and 0x18ea60), sign-extended, unsigned-compared with 0x54; 7, 8, 9, 15, 16 and everything above share
the default body (0x5986b8: always throws 0x2a). Cases 1/3 and 26/80 have identical bodies (26/80 is one body; 1 and 3 are two copies).

Jump table 0x5986f8 (`case:body RVA/bytes`, `n(=m)` shares case m's body; default 0x5986b8; common exit 0x5986df):
  0:591768/678 1:58bd82/24 2:58bdb2/500 3:58bd9a/24 4:58bfa6/2803 5:58cf99/213 6:58ca99/1280 10:58d06e/54
  11:58d0a4/54 12:58d0da/198 13:58d1a0/132 14:58d224/95 17:58d36b/195 18:58d283/232 19:58d42e/707 20:58d6f1/590
  21:58d93f/281 22:58da58/283 23:58db73/281 24:58dc8c/406 25:590e9b/394 26:591025/246 27:59111b/228 28:5911ff/652
  29:58e6b8/866 30:58edf8/406 31:58ea1a/990 32:58de22/2198 33:591a0e/112 34:591a7e/426 35:591c28/227 36:591d0b/181
  37:591dc0/323 38:592525/157 39:591f03/1570 40:5925c2/525 41:58f555/583 42:58fe19/664 43:5900b1/3562 44:5927cf/363
  45:59293a/1140 46:58f79c/1205 47:58fc51/456 48:592dae/654 49:59303c/402 50:5931ce/1273 51:5936c7/306 52:5937f9/1635
  53:58ef8e/1479 54:593e5c/263 55:593f63/263 56:59406a/239 57:594159/221 58:594236/387 59:5943b9/2022 60:594b9f/2798
  61:59568d/1184 62:59600e/579 63:596251/67 64:596294/214 65:59636a/22 66:596380/565 67:5965b5/520 68:5967bd/256
  69:5968bd/18 70:596a6e/2854 71:597594/277 72:59148b/589 73:5916d8/144 74:5976a9/545 75:5978ca/651 76:597b55/1047
  77:5968cf/18 78:5968e1/397 79:597f6c/157 80(=26) 81:598009/1711 82:595e48/454 83:595c7b/461 84:595b2d/334

Ported: all 80 case values (lane Z 64, lane Z2 the 16 largest: 4, 6, 29, 31, 32, 39, 43, 46, 50, 52, 53, 59, 60, 61, 70, 81).

Original callees the ported cases use (`OriginalFunction`): 0x57ee10 (cdecl, two sign-extended words, al: attribute match; cases 17,
19, 20, 26, 80), 0x211da3 (cdecl getter; 63), 0x57d860 (thiscall, one arg, ret 4: sets/clears object flags; 69, 77), 0x9c5a89 (cdecl:
the message server; 0, 28, 37, 40, 44, 45, 51, 72), 0x7e8c60 (cdecl, 7 args: object placement; 49, 68), 0x57c6d0 and 0x1d20f0 (a list
local's constructor/destructor, unused; 49), 0x4f8eb0 (cdecl: parse a GUID text; 41), 0x54b2d0 (cdecl, al; 42). Everything else is ours
or inlined (0x57c700 is a 4-dword constructor, 0x1c033d the message constructor, 0x57d6d0 the rectangle centre, 0x57cc80/0x57c290/0x57c2f0
and friends the holders).

Facts for the next lane (confirmed = read from the disassembly, nothing run):
- `this` slot 19 (0x530c10) reads argument word i (i < 8) from the array at this+0x96; slot 20 (0x53dbe0) stores it. The cases read
  their operands only through these two.
- The context (`this+0x40`) slots: 3 (a small sink object: slots 16, 20, 24, 25, 44), 4 (a per-id store: 17/19 read, 50/51 write),
  6 (a lookup list: 13, 57), 7 (a list walked with slots 19/20), 8 (slot 9 = lookup by id), 9 (entries by id, slot 11), 10 (people by
  id, slot 16 = (id, 1)), 23 (a holder: slot 24 al, slot 6), 26 (the object table: slot 29 finds an object by id; slot 39), 29.
- The frame's slot 12 is the stack object's id word, so `StackObject()` = table(slot 29)(frame.slot12()), throwing 0xa when absent.
- Messages: 0x9c5a89 is the message server; cases build cRZMessage2Standard objects (0x2c bytes, ctor inlined) and post them with slot 3
  or slot 4 (message, 0). Slots 11 / 12 of the message both store their argument (0x1c0874), so 28 and 72 store *addresses of
  locals* in the message (ORIGINAL-BUG candidate; kept as is).
- Interface ids: 0x5a69d3d5 (QueryInterface on the object itself, throws 0x1c when it fails), (0xfa1c39f7, 0xfa1c39ed) through slot 9
  of the object (a virtual-base holder), 0x1a34f3f4 through that holder's virtual base (vbptr at +4, entry 1 = displacement from +4).
- Cases 62 and 66 turn floats into ints with the 1.5*2^23 trick (add 12582912.0f, subtract the bits 0x4b400000): `gscmath::RoundViaMagic`.
  Float constants: 0xda74e8 = 0.01f, 0xda20dc = 0.001f.

Traps and ORIGINAL-BUG candidates:
- **Cases 29 and 31 (resolved, lane Z2): no stack garbage.** The 4-dword key constructor 0x57c700 (`ret 0x10`) is called with
  `push 0; push 1; call vf11; push eax; push 0xac598eac`; slot 11 of the graph object (and of the attribute-key holders in
  59, 60, 61, 32, 70) takes NO arguments, so the first two pushes belong to the constructor: the keys are
  {0xac598eac, slot11(), 1, 0} (case 29) and {0x53545223 'STR#', slot11(), 0x85, 0} (case 31). Every other case with this
  pattern agrees (case 31's 0x85 is the STR# instance), and `ret 0x10` with four pushes balances. This is the decompiler pitfall
  of docs "a push before call [reg+N] belongs to the next call", not an original bug.
- The container helpers of the unoptimised code are small and now inlined as std types: 0x57c3f0 / 0x1d20f0 / 0x22ea36 are a
  `std::vector` of 16-byte keys (default constructor, destructor), 0x288e80 a `std::vector<int16_t>`, 0x257830 (-> 0x26b820) a
  `std::vector` of rows of ten reference-counted pointers (`gscmisc::RefRow`: released from the last pointer to the first),
  0x57c230 / 0x5a9970 / 0x5a9c60 / 0x57cce0 / 0x57cda0 / 0x57cd80 / 0x57ce30 / 0x57cbb0 / 0x57cc60 / 0x57cc20 plain iterator
  steps (begin, end, deref, advance, compare, element address), 0x57c2f0 / 0x57ca80 / 0x4365a0 / 0x142080 / 0x57cee0 / 0x1dae60 /
  0x57cf70 the reference holder (`gscref::PlainRef`: AddRef on construct and assign, Release on clear), 0x57c290 / 0x2296b4 /
  0x258c70 / 0x57cf20 / 0x210cfc / 0x1fcd10 its virtual-base twin (`gscref::VBaseRef`), 0x2cbb50 / 0x57cae0 a pair of references
  (`gscmisc::HandlePair`).
- Case 47 AddRefs the virtual-base-queried object (slot 1) and then releases the holder through the virtual base: a net +1 reference
  per call unless the slots are distinct (reproduced as is).
- Case 0's flag test `if (slot6 != 0) { if (slot6 == 0) flags |= 4; }` asks twice and can only add the bit if the answer changes.
- Argument words are re-read by calling slot 19 again wherever the original does; `cwde` (sign extension) follows most of them, a
  few use `movzx`: copy the width from the disassembly, not from the name.
- Reading a case: every `mov [ebp-X], reg` / `mov reg, [ebp-X]` pair is a temporary; a `push` run before a `call [reg+N]` belongs
  to that call unless an inner call sits between (then the inner call's arguments are the *last* pushes). Check `add esp, N` after
  direct calls (cdecl) and the callee's `ret N`.

### Lane Z2: the 16 large cases (2026-10-13)

Files: `PrimGenericSimCallG.cpp` (29, 31, 32, 6, 61, 50, 39, 46, 53), `...H.cpp` (59, 60, 4), `...I.cpp` (81), `...K.cpp` (70),
`...L.cpp` (52, 43), headers `...Refs.h` (holders, loader, key service, small pure helpers moved out of D), `...Misc.h` (row/pair
holders, GUID test, scaled distance, float sqrt), `...OriginalsB.h` (more original wrappers, defined in PrimGenericSimCall.cpp so the
status entry can declare them all). Hook table `src/hook/tables/SimAnticsPrimsGenericSimCall.cpp`, status entry in
`db/status/SimAnticsPrimsGenericSimCall.jsonl` (`reimplemented`, `original_calls` 20). Method facts: `db/methods/SimAnticsGenericSimCall.jsonl`
(interfaces GscFrame, GscContext, GscGate, GscPeopleTable, GscEntryTable, GscObjectTable, GscObjectDefTable, cITSPersistSystem; the
new cases call through them, the older ones still use raw slots).

What the cases do (names from behaviour; the only community name the binary gives is 61):
- 61: `kAddExternalOutfitToFamilyClothingBin` (the game's own log text at 0xdd7be0 names it).
- 59 and 60: the sim's outfit change: 59 is the failed-plastic-surgery pick ("plasticsurgery_fail" table at 0xdd7bc8, a random row,
  key text "%d:%d:%d:"), 60 applies an outfit GUID (from a string-table text like 46/53, or from the picker object's entry when the
  table argument is 0); both end in `NotifyAppearanceChange` (tag service 0xb38ea0 slot 3 flag 0x1f, then the component holder).
- 46 and 53: read a GUID text from the string tables (table = argument 0, entry = argument 1 + 1; the group comes from the object's
  type, its owner chain or 0x7fd46cd0) and hand the parsed 16 bytes to the stack object's component interface (46: this object's).
- 31: preloads the models listed in an object's STR# 0x85 (adds "_cres" when missing, type 0xe519c933, 60 s hold).
- 32: counts (argument 3: 0 category/age mask of an entry, -1 family keys of the persist system, else a range test on the object's
  property resource); the result goes to argument 1. 29: repeats (resume state = repeats + 100) applying a property set through the
  tag service. 4: interaction start/yield with the resume state keeping the record id. 6, 50, 39, 52, 43, 70, 81: see the comments above
  each member (43 = can the object stand where it is: footprint interface, distances to other objects, blocked-position tests,
  placement worker mode 5; 52 = nearer of the two tiles in front of an object; 70 = a list pass over a sim's belongings).
- Pure helpers ported with difftests (`tests/simantics_prims_gsc_misc.cpp`, 5 of 5, 8 of 9 mutants killed, the survivor is equivalent):
  0x23f55f (category bit table), 0x54d830 (family key test), 0x4f5de0 (GUID compare), 0x57edb0 (scaled distance), 0x5a9eb0 (float
  sqrt). Ported inline without a test (tiny): 0x54a380 (slot 11 of the cached slot-23 object), 0x549fb0 / 0x54ac40 (person / object by
  id through the singleton), 0x54b290 (marked object), 0x504cb0 (null GUID test), 0x57c740/0x350d80/0x40be10 (3-float and box init/copy).
- New original callees: 0x1d0e90 (string-table loader constructor), 0x4f8e50 (the null GUID static), 0x212e61 (tile service),
  0x7dccd0 / 0x1a6c40 (notch quaternion, rotate), 0x7eed90 / 0x7eec00 (tiles of a box), 0x7deff0 (object on a tile), 0x7e7e00
  (position blocked, 1493 B), 0x7e8c60 with mode 5, 0x5a9de0 / 0x5a9a90 (sine, cosine wrappers; Snap and 0x17 use them too).

Facts for the next lane (confirmed = read from the disassembly, nothing run):
- The persist system's resource interface (the cached 0xb390c0 object, `cITSPersistSystem` at +0xa8): slot 5 = GetResource(key, type
  word, &out, 0, 0) (a thunk to the base class slot 3); slot 6 queue a resource load (key, 0, ms, 0, 1); slot 34 (ret 0xc) =
  (key, interface id, &out); slot 36 lists the keys of one type in one group into a `std::vector<Key16>`; its slot 3 returns the primary
  object whose slot 34 takes (key, &out). Resource keys are {type, group, instance, instance2}; 0xac598eac / 0xb0858c5f is the family
  of property sets of cases 29, 32, 61, 76.
- The frame's slots 18 / 19 are a resume state (0 on the first visit): cases 4 and 29 keep a counter or an id (+100) there.
- The object's component interface asked through slot 9 with (0xfa1c39f7, 0xfa1c39ed) is asked again with (0xfa1c39f7, 0x1a34f3f4)
  in 59/60/70 (the second id names the virtual-base interface directly).
- 0x1b8d5c is `cRZRandom::RandomUint32Uniform(n)`: slot 55 of the owner at this+0x48 returns the random source (case 59).
- Traps seen: `this+0x240` (outfit owner: slot 65 gates cases 59 and 60) and `this+0x48` (a log object, slot 28 takes a text) are
  fields of the object; in case 4 and 81 the row/entry index is used without a bounds check (kept).

Case 43 fix (lane Z3, 2026-10-13; the hook and the dispatch were NOT changed, the main session re-wires and runs it in game): the
bug that broke lot-simtrace-108 was three wrong things in `Case43`, found by reading the disassembly again and then proven with
`tests/simantics_prims_gsc_case43.cpp` (a recording-fake world for the whole 0x58bc50, case selector 43; 6000 worlds, 16 of 16
mutants killed, reusable as a template for the other cases: fakes log (object, slot, args) and count AddRef/Release, the world-dependent
callees 0x7e7e00 and 0x7e8c60 are redirected to logging stand-ins that also log the memory they are handed):
  - slots 21 (kind), 48 and 49 (door-like) are slots of the attribute interface itself (`[ebp-0x28]`), not of its owner (the vf8
    result); ours asked the owner, whose slots 48/49/21 are other methods;
  - the placement worker 0x7e8c60 takes (object, position block filled by owner slot 42, address of the direction handle from owner
    slot 43, 0, 0, mode, 0): ours had the second and third swapped, so the worker saw an address of a stack integer as a position and
    the position block as a handle (the likely source of the heap damage seen in cTSPerson::vf359);
  - call order: both owners (the other object's, then ours) are fetched before either one is asked for its cell.
  Lesson for the other cases: when a case calls a slot, check which object it is called on (`[ebp-0x28]`-style holder = the
  interface, `call [x+0x20]` result = its owner) and the order of evaluation; none of the other cases has a whole-function difftest yet.

### Lane Z4: whole-case difftests for every case value (2026-10-13)

Every case value now has a difftest against 0x58bc50 (only 69 and 77, one call to 0x57d860 each, were read instead). Files:
`tests/simantics_gsc_world.h` (the reusable world), then `simantics_gsc_case04/06/29/32/39/46/49/50/52/59/60/61/70/81/82.cpp`,
`simantics_gsc_attr.cpp` (17, 19, 20, 26, 80), `simantics_gsc_small_a.cpp` (the short A/B/E cases), `simantics_gsc_small_b.cpp` (messages,
holders, lookups, the per-person store) and `simantics_gsc_default.cpp` (the default body); lane Z3's `..._case43.cpp` is unchanged.
Each test ran 4,000-12,000 seeded worlds, 25% of them with a `short` thrown out of the Nth fake call (so the unwinding is compared too),
and 3-16 hand-picked mutants per file were all killed (a few survivors turned out equivalent or were fixed in the world).

How the world works (`gscw::`): a test declares fake types (name, instance count, and per virtual slot the argument words, e.g. `"VVO"`
= value, value, out-pointer, and the return kind, e.g. `"o:Person?"`, `"h{0,1,5}"`, `"b"`) and the header builds a machine-code thunk per
slot, so the callee pops exactly the declared words like the original's callees; a slot nobody declared is a trap that logs and throws, so
a call on a wrong receiver shows up. Answers are stable per (object, slot, first argument) and come from the seed; hooks (`Slot::hook`)
produce the rest (a vector filled by the fake, a list walked by successive calls). The log has (object, slot, arguments) of every call
in order; the result or thrown code and every instance's reference count are compared too. World-dependent original callees are
redirected with `CodePatch` (0x7e8c60, 0x7deff0, 0x57ee10, 0x1d0e90, 0x9c5a89, 0x1be3b0) and the cached service globals in the image
point at fake instances; the COM director singleton (0x10c9390) gets a fake vtable so 0x1ae18d and ours reach the same framework fake;
the game allocator global (0x10c95d4, guard 0x10c95d8 = INT_MIN) is a quiet fake that frees the vectors the fakes fill. Messages use the
fake type `Msg` copied into the game's message vtable (`MessageSupport`). `GSCW_PROGRESS=1` prints every call, `GSCW_ONLY=<seed>` runs one
world. Pitfalls met: a vtable slot of a fake past the table size reads garbage (kSlots is 400 now); getters must answer the same twice
(an iterator needs a hook that counts); a Release must never throw during the test's injected throw; out-of-range indexes the
original does not check (case 81 rows) must stay inside the fake's data.

Divergences found (all fixed in this lane; the hook and the dispatch are unchanged):
- 60: the picker's slot 4 takes (selector, address of the holder) and the attribute interface's slot 57 takes nothing (ours passed the
  holder to slot 57 and only the selector to the picker); the temporary pair is released before the text is read; the pair assigned is the
  pair the slot returns, not the one passed in.
- 70: the head's "refused" test is `list->slot10()->slot10()` (ours had one level more and would have dereferenced a flag).
- 59 and 81: the vector destructor of the rows releases nine references, ours ten (the tenth word is not a reference; `RefRow`).
- 31, 61, 82, 83, 84: the argument words were read in C++'s unspecified argument order (MSVC: right to left), the original reads them in
  source order (0 then 1; 3,2 then 5,4 then 7,6). Any new case must read arguments in separate statements.
- 67, 78, 83, 84: the branch result is the callee's answer (1 for a nonzero low byte, else 0); ours dropped it and always returned 1.
  Check every case for `res=` stores in the original (`mov dword ptr [ebp-0x18], X`), not only the exit value.
- 76: ours stored the reference into the holder and then called AddRef; an AddRef that throws left a holder to release (the original's
  holder constructor takes the reference first).
Original bugs reproduced (the worlds count them as "both crashed"): case 4 and 70 dereference a null entry the table did not find; case 31
dereferences the table when the director cannot make one; cases 24 and 30 call a slot on the second interface without checking it; case 81
reads rows[index] with no bounds check.

Done (main session, 2026-10-13): merged, all three scenarios PASS 3/3, 0x58bc50 `verified` on these difftests; its full extent is in
db/missed-functions.jsonl and the annotated Ghidra copy decompiles it whole.


## Lane AA: 0x6d, 0x70

Object opcodes 0x70 Effect (0x5805c0, real size 8,635 B) and 0x6d MaterialChange (0x5827e0, 11,686 B) are ported
(`src/simantics/PrimAA70.cpp`, `PrimAA6D.cpp`, shared helpers `PrimAACommon.*`), hooked and `verified` by recording-fake
differential tests (`tests/simantics_prims_aa70.cpp`, `..._aa6d.cpp`, framework `tests/simantics_prims_aa.h`, 20,000
worlds each, 23 + 27 mutants killed). The dispatch still wires the original wrappers for both: `DefaultPrimHandlers`
needs `PrimAAObject::Prim70` / `Prim6D`.

- Both handlers are long chains of virtual calls on objects reached from the context (slots 10, 24, 26), the frame
  (20 -> 38, 12, 18, 19) and cached services. Shapes that recur: the "component" holder (object slot 9 with the
  identifiers 0xfa1c39f7 / 0xfa1c39ed and an output holder with a virtual base), the string-table loader (0x1d0df0, group
  from the language entry's slot 11 or 0x7fd46cd0), reference holders released through slot 2, and two handle tags
  (`Tag`, the 0xae8260 family with the service 0xae8750; `Tag2`, the 0xae8820 family with the verified
  `GetCachedQueriedObject`) whose constructor takes a reference and destructor gives it back.
- Cached services behind original getters (still original calls): 0x20e031 (dialog service holder; slot 4 the service,
  slot 7 its panel object), 0x20e006 (UI service: slots 6/7 return handles), 0x20df26 (file/material service), 0x20deb5,
  0x211da3 (wall-art/station service), 0xb3c1d0 (a scene component's effect manager, created on demand), 0xae7340 (string
  key parser), 0xaece50, 0x54a410, 0xb394b0 (option mask bit).
- The game's vector shapes appear (`ShortVec`: 16-bit values; `AttrVec`: 0x14-byte records; first/last/end, elements from
  the allocator service slots 3 and 4): `AllocateThroughAllocatorService` is the allocation twin of the verified
  `FreeThroughAllocatorService`.
- A recording-fake world for big handlers (reusable): `tests/simantics_prims_aa.h` describes each fake slot by its
  arguments (one character each: value, string, out string, out pointer, key, vector by value ...), logs by content so
  stack pointers never reach the trace, scripts returns per (object, slot) and per output argument, injects an exception
  at the Nth call (checks the unwinding releases), counts which (role.slot) events the worlds reach, and traps unscripted
  slots. Original callees are redirected in the image (`Redirect`) or served through their cache globals (`GlobalCell`),
  and real objects' vtable entries (cRZFile Open/SetPath, property set SetProperty) can be replaced (`VtPatch`).
  Pitfalls it exposed: a thunk with the wrong argument count corrupts the stack (fast-fail 0xc0000409, no output); the
  harness has no game TLS, so original code that reads the thread epoch (allocator helpers, the director getter) needs
  its redirect or the cell set (`0x10df814` = our `_tls_index`).
- Bugs the tests found in the first port: flag tests on bits 8..13 of the word at +0xe (a byte read lost them), a
  QueryInterface result not released when an error leaves its block (an owner that was a raw pointer), destructors that
  call virtual slots must be `noexcept(false)` (the original lets exceptions out of them).
- Decompiler pitfalls seen again: pushes before `call 0x1d0df0` and slot-11 calls are the loader's own arguments (slot 11
  takes none); `1acd4f` is `c_str()` with `this` in ecx, a preceding `push` belongs to the outer call; small displacements
  print as decimal in the disassembly (`[edx + 8]`).
- Original quirks kept: the QueryInterface result in 0x70's slot-104 path and the shader interface in 0x6d's material
  build are used unchecked (marked `ORIGINAL-BUG`), `0xb3c1d0` is called with a null component when slot 9 finds nothing.



## Lane AB: the interpreter (cTSTreeSim, cTSTreeStack, cTSTreeStackElem)

Code: `src/simantics/TreeSim.h` (the sim: `TreeSim.cpp` small slots, `TreeSimStep.cpp` the step, `TreeSimRun.cpp` push and
run, `TreeSimLife.cpp` constructor and destructors, `TreeSimRunNode.cpp` cTSObject::vf246; record/replay stays original, called from `TreeSimStep.cpp`), `TreeStack.*`,
`TreeFrame.*`, `SimTraceLog.*` (the trace log functions the interpreter calls), `TreeHeld.h` (reference holder),
`TreeSimInternal.h` (aliases for the generated interfaces). Method facts: `db/methods/SimAnticsTreeSim*.jsonl`
(interfaces cTSTreeSim, cTSTreeStack, cTSTreeStackElem, cTSBehaviorNode/Tree slots, TreeOwner, TreeIn/OutStream ...).
Hooks: `src/hook/tables/SimAnticsTreeSim.cpp`. Status: `db/status/SimAnticsTreeSim.jsonl`.

Objects (RVAs 1.18.0.187):
- `cTSTreeSim` (0x70 bytes) is the sim subobject at object + 4 of every cTSObject/cTSPerson (so its `this` is object + 4; the
  object's own fields +0x2c8 etc. are `this + 0x2c4`). Layout in the header: step counter +0xc (limit: global 0x1016914,
  100000, or 300000 when the context's gate option 0x33 bit 0 is set), the stack at +0x18, last jump target +0x28, last
  branch +0x2c, default argument block +0x30, error code +0x34, result +0x36, call depth +0x38, services +0x3c (context),
  +0x40 (debug), +0x44 (service C), +0x48 (the context's gate), replay hash map +0x4c, record/replay switch +0x6c (this is
  object + 0x70, the byte person Prim03 sets while it runs: replay is live during person actions).
- `cTSTreeStack` (0x10): vtable + `std::vector<TreeFrameRef>`. The elements are smart pointers (copy adds a reference,
  destroy releases): a reallocating push copies every element and releases the old ones, call for call, and the code
  reproduces that by using the same element type. Frames are pool blocks (allocator 0x1016994, 0x3c bytes).
- `cTSTreeStackElem` (0x3c): see `TreeFrame.h` for the fields. Slot 41 returns `this` (Self); the interpreter calls it on
  every frame it takes from the stack.

Interpreter step, `DoNodeAction` (the community name; slot 13, 0x5762d0): result 0 keep going, 1 the stack is down to its last
frame, 2 yield. Frame without owner/null: pops (needs >= 2 frames, else throws short 0x3e9); paused frame: ResumeFrame; node
index 0xfffd/0xfffe: the tree returned true/false, back to the caller frame, which jumps to the node's true/false target (the
caller's tree id lookup, the empty-tree case, the 0xfffc "no target" marker throwing 0x44c); node flag (bit 15 of the opcode
word, a breakpoint): ResumeFrame with the node; otherwise step counter and limit check (0x44d), then the node runs: opcodes
below 0x100 through slot 6 (`RunNode`, cTSObject::vf246 0x53e560), opcodes from 0x100 are a call of the tree
of that id: arguments built by one of three modes chosen by byte 0x10 of the node's parameter block (bit 0: operand
descriptors read through slot 7, bit 1: the caller frame's own arguments, else the parameter bytes +4 as the argument array),
only for tree formats >= 0x8003; then PushTree. Error codes thrown as 16-bit values: 0x11 unknown tree, 0x3e8 push failed,
0x3e9 stack too small, 0x44c bad target, 0x44d step limit, 0x44e no owner, 0x44f unknown current tree, 0x450 missing node
or called tree, 0x451 yielded in a synchronous run, 0x452 stack too deep; a missing top frame throws the 32-bit
`cTSTreeStack::kStackErrorNoFrame` (0x3ea).
`CallTree` (slot 15) runs a tree to completion above a dummy bottom frame (tree id -1), saving and restoring the step counter,
call depth, run flag byte 0x1016910 and the gate's run state; an error stores the code, tells the object (slot 8) and, for an
outermost run with service C enabled, cancels the object (slot 237) when its flags say so. `RunToCompletion` (slot 36) steps
until a step reports 1 or 2 (an error is caught and still returns true). `PushTree` (slot 12) takes a frame from the stack's
NewFrame, sets owner, tree id, inherited words and arguments, and pushes it.

Reproduced original quirks (marked ORIGINAL-BUG in the code): PushTree dereferences the context's object table without a null
test and the top frame without an empty-stack test (a null owner argument); StartTree (slot 11) passes an 8-byte argument block, so
PushTree reads garbage after the fourth argument word of a tree with more than four arguments (ours reads zeros, the one
unreproducible difference, in no test); GetArg/GetLocal/SetArg only test the upper bound.

Facts for the next lane: the record/replay table (hash map at sim + 0x4c; 0x575ed0 Record, 0x576e70 Replay, with 0x575280
insert, 0x575000 / 0x574df0 / 0x575950 / 0x575580 / 0x5758c0 / 0x575860 row and node helpers, 0x5757a0 map destructor, 0x5787b0
clear) is the old MSVC hash_map: keys are 8 bytes, values a vector of 0x1c-byte rows {args vector, result, temps vector}.
`cTSObject::vf246` (slot 6 of the sim subobject: asks slot 38 (Edith) then slot 332, falls back to opcode 0x1c, and logs when
the gate's word 0x35 changed) is ported (`TreeSimRunNode.cpp`); still original are the record/replay functions and the map helpers, and the cTSObject glue vf4_15 (0x53d2d0, forwards to CallTree) and vf4_36 (0x5315a0, 337 bytes, ends a run).

Verification: differential tests with a fake world (recording objects behind every vtable the interpreter calls, real field layout
for the frames and the stack vector): `tests/simantics_treesim*.h/.cpp`, `simantics_treeframe.cpp`, `simantics_treestack.cpp`,
`simantics_treeio.h`, `simantics_pool.h` (a stand-in for the frame pool), `simantics_treesim_life.cpp`. The fakes are described in
`simantics_treesim.h`; the loop test runs 60,000 worlds plus 40,000 multi-step worlds with the replay table live.

Status: the lane's functions are `verified` in `db/status/SimAnticsTreeSim.jsonl` (difftest on the fake world plus chosen
mutants killed); the exceptions are `SimTraceLog::LogNode` and `ClearReplayCache` (`reimplemented`). The tests also record
the run flag at each step, the sim's error word when the error handler runs, and throw at any of the first 30 fake calls.
The in-game trace has to show: person actions through Prim03 (replay switch live) with trees flagged 8, tree calls in all
three argument modes, the CallTree error path with service C, the step-limit option 0x33, the edithConfigFlags
notification, the opcode 0x1c fallback of RunNode (no test reaches it), and the trace log switched on.

## Lane AC: the record/replay table and the cTSObject glue

Code: `src/simantics/TreeReplay.h/.cpp` (the table: `TreeReplayMap`, its `TreeReplayList`, nodes, rows, keys),
`TreeSimReplay.cpp` (`cTSTreeSim::RecordResult` 0x575ed0 and `ReplayResult` 0x576e70), `TreeSimObject.h/.cpp`
(`TreeSimObject::CallTree` = cTSObject::vf4_15 0x53d2d0, `RunToCompletion` = vf4_36 0x5315a0). Method facts:
`db/methods/SimAnticsTreeSimReplay.jsonl`. Tests: `tests/simantics_treesim_replay.cpp`, `simantics_treesim_object.cpp`.
The interpreter (lane AB's files included) now calls no original function.

Facts for the next lane:
- The table is an old MSVC `hash_map`: float max load at +0, a `std::list` at +4 (sentinel `{head, size}`; its destructor
  0x575860 takes the list at map + 4, not the map), bucket vector +0xc (two entries per bucket: first, last), mask +0x18,
  bucket count +0x1c. Nodes are 0x20 bytes: next, prev, 8-byte key, `vector<Row>`. Row (0x1c): args `vector<u16>`, result
  dword (1 true / 0 false), temps `vector<u16>` (the object's 8 argument words, empty unless the tree's cache mask is set).
- Key: `{owner->slot12(opcode), (int16)opcode << 16 | (int16)stackObjectId}`; hash is the sum of the two words. Tree flag 0x10
  drops the object id from the key. TreeOwner slot 12 (`GetTreeIdentity`, tentative) is not identified in the binary.
  The object-id half is sign-extended before the OR, so an id with bit 15 set turns on the whole upper half (kept).
- Insert grows the table when `(size + 1) / buckets > maxLoad`: new bucket count = max(8, ceil(size / load)), raised to
  8 x count below 512 buckets; Rehash picks the next power of two and re-links nodes so equal keys sit together. The
  bucket vector only grows (a smaller rehash resets all its entries and keeps its size).
- Interface 0x9b25d761, asked of the sim, has the object's `GetArgument/SetArgument` at slots 19/20.
- Replay reads the recorded temps unchecked: a row recorded without them (cache mask 0, or the query failed) and replayed
  with a mask crashes (ORIGINAL-BUG, untested). A frame without an owner is dereferenced too.
- cTSObject::vf4_36 passes object slot 343 a flag whose upper 24 bits are leftover register contents; the callee ignores it.
- STL instantiations the original has for these vectors (0x575000, 0x574df0, 0x575950, 0x575460, 0x5754f0, 0x575b00,
  0x4ebd10, 0x4dffe0, 0x4124c0) are made by our toolchain from `std::vector<TreeReplayRow>`; not hooked, not in
  `library-matches.jsonl` yet (a stlprobe probe for `vector<Row>` would identify them).
- In-game trace needed: replayable trees (flag 8) while a person action runs (switch +0x6c live), including a replay hit.


## Lane AD: the person's interaction bookkeeping (queues, check-tree runner)

Why this group: the person handlers' leftover original calls (IdleForInput, opcode 3, opcode 0x1e selector 0, opcode 0x0d) fell into
two clusters. The interaction-queue cluster (0x512c30 / 0x5120c0 / 0x5205c0 / 0x520360 / 0x523da0 / 0x520e40 / 0x5208a0, the
`cEdithObjectTestSim` check-tree runner, the deque helpers) was called by IdleForInput (13 original calls), by opcode 0x1e and
by opcode 0x0d, and every function reaches only vtables, so it could be finished completely. The other cluster, opcode 3's
0x515940 / 0x5155b0 / 0x516c40 / 0x516160 / 0x5150d0 / 0x515270 (6.1 KB, with 0x515c80 / 0x516e00 / 0x50f780 below it), is one
big family that calls each other and is left for a later lane.

Code (RVAs 1.18.0.187):
- `src/simantics/RefCountedDeque.*`: the game's `std::deque` of reference-counted pointers (the toolchain's `std::deque<void *>`
  has the same 0x14-byte layout): `PushBack` 0x1d49e0, `PopFront` 0x525380 (noexcept), `Clear` 0x1d48f0 (noexcept: releases
  back to front, frees blocks and map), `ThrowDequeRangeError` 0x5248f0. `At` is `std::deque::at` (0x524900 is library code,
  not hooked). The person's queues are `PrimPersonObject::QueueA()` (+0x594, the pending queue the idle poll pops), `QueueB()`
  (+0x5a8, the action queue) and `History()` (+0x5c8); the interaction holders are +0x5bc current, +0x5c0 previous, +0x5c4 selected.
- `src/simantics/PersonInteractions.cpp`: `ClearActionQueue` 0x512c30, `RejectInteraction` 0x5120c0, `PublishActionQueue` 0x520e40,
  `PublishPendingQueue` 0x5208a0, `SelectInteraction` 0x5205c0, `StartInteraction` 0x520360, `CountInteractionUse` 0x523da0 (the
  per-Sim use counter, capped at 100; opcode 0x1e inlines the same code), and `SetUiManager` 0x4529c0 (the global 0x10da968).
  Person Data field numbers they write are named in `PersonData.h`.
- `src/simantics/EdithObjectTestSim.*`: `cEdithObjectTestSim` (0x2c bytes): constructor 0x44f510, destructor 0x44f540 (noexcept: it
  writes back the actor object's eight saved tree arguments), `Begin` 0x451060 (vf3), `RunCheckTree` 0x451f00 (vf9, `ret 0x18`:
  six stack arguments). `TestSimScope` is the frame holder the primitives use. `SimTraceLogInteractionCheck.cpp`: 0x526fc0 and
  0x526b60, the trace-log writers around a check tree.
- Method facts: `db/methods/SimAnticsPerson.jsonl` (cTSInteraction, InteractionTableEntry, cITSPerson, cTSObject additions, CheckRecorder,
  AttributeTable, SimRowTable). Hooks: `src/hook/tables/SimAnticsPersonInteractions.cpp`. Status: `db/status/SimAnticsPersonInteractions.jsonl`.

What the objects are (from the code, tentative names in the facts): a queue entry is a `cTSInteraction` (vtable 0xdc8324; word +0xc =
actor object id, +0xe = stack object id, +0x14 = index in the stack object's interaction list, flag dword +0x68). Its slot 16 is the
stack object (a cTSObject; the trace logs it as "Stack Object"), slot 17 the actor's `cITSPerson` interface (the one opcode 0x0d
calls its "caller interface"), slot 24 the tree table entry (check tree id in the entry's slot 78, age mask in slot 18). The
check-tree runner (`RunCheckTree`) is the TTAB "check" step: it clears four attributes of the actor's Sim object, ANDs the actor's age
mask (cITSPerson slot 52) with the entry's, applies the vetoes, and runs the check tree through the tree sim's `CallTree` (slot 15,
eight arguments: owner, stack object id, tree id, actor id, mode 2, the four tree arguments, 0, 0) unless the entry has no check tree.

Pitfalls found (carry them into the next lane):
- Call arity from the pushes is a trap: the decompiler and the earlier lanes attributed the pushes of a call to its neighbour. Count
  the stack with the callee's `ret N` (`tools/classinfo.py <class> --disasm <rva>`). Opcode 0x0d called `ownerObject->vf63()` with
  three arguments and the runner with three: the real call is `vf63()` with none and `RunCheckTree` with six
  (`notifier, 2, recorder, runTree, 0, 1`); it only worked because the extra pushes stay on the stack. Fixed in `PrimRPush.cpp`.
  IdleForInput still calls `cTSInteraction` slot 15 with three arguments although the slot takes none (the same trick, left alone:
  the three values are the trailing arguments of the slot-248 call that follows).
- The runner stores its verdict into the stack slot of its first argument (the interaction pointer), so the call that stores it
  passes the interaction's address with the low byte replaced by the verdict; the setter reads the byte only.
- 0x44f540, 0x525380 and 0x1d48f0 are `noexcept` frames (flags 5): a C++ exception leaving them terminates, so the tests keep fakes
  called inside them from throwing.
- Tests: `tests/simantics_person_world.h` is the shared fake world (6 interactions, 3 table entries, 3 stack objects, the actor with
  its Sim object and tree sim, settings, sink, recorder; heap deques reduced to their contents after each run by the `Queues`
  guard). The settings singleton (0x10cc288) and the trace log (0x10149e8) are set in the mapped image, which ours reads too.

Left: the opcode 0x03 family (0x515940, 0x5155b0, 0x516c40, 0x516160, 0x5150d0, 0x515270 and their callees 0x515c80, 0x516e00,
0x50f780, 0x50dca0, 0x50de10, 0x5109a0, 0x510aa0, 0x511880, 0x512860), the motive-vector insert 0x50da10 (Prim1D), and the cTSInteraction
slot meanings still named by number in `PersonInteractions.cpp` (slots 33, 47, 66, 68, 70).

## Lane AF: FindBestAction (opcode 0x03), the scoring and choosing part

The family behind `PrimPersonObject::Prim03` is the Sim's autonomy search. Names (community opcode map: 0x03 FindBestAction):
`BeginSearch` 0x515940 (clears the last search, snapshots the state), `GatherInteractions` 0x5155b0 (fills the pending list +0x840 through
`cEdithObjectTestSim` vf6), `ScorePending` 0x516c40, `CheckInteraction` 0x515c80, `ScoreInteraction` 0x516160, `ChooseAction` 0x5150d0,
`StartChosenAction` 0x515270. Prim03's loop runs `Gather` / `ScorePending` / `ScoreInteraction` over the reaction pairs / `ChooseAction`.

Done (ours, hooked, verified, in `db/status/SimAnticsAutonomy.jsonl`; tests `tests/simantics_autonomy_*.cpp`): `ChooseAction`, `StartChosenAction`,
`CheckInteraction`, `ScoreInteraction`, `ScorePending` (`src/simantics/FindBestAction*.cpp`), `SearchSnapshot::Assign` 0x4365d0, the qsort
comparators 0x512d70 / 0x512db0, `InteractionTreeHasFlags` 0x516e00, `GetStackObjectScoreFactor` 0x512860, `GetBconTable` / `GetBconValue`
0x54a770 / 0x54a6b0, the action-string flag 0x4529b0 / 0x452690, `cEdithObjectTestSim::SetPerson` 0x4510b0, `SimTraceLog::CloseSink` 0x526b40.

Data model (`FindBestAction.h`; the person's fields are named in `PrimPerson.h`): `ActionRecord` 0x58 (id, entry index, score, parameters,
eight tree arguments, name, "checked" byte, counted interaction), `ScoreRecord` 0x44 (how a score came about; two vectors), `RecentAction`
0x28 (the memory of started actions, sorted by expiry), `SearchSnapshot` 0x8c. Person vectors: candidates +0x2f8 (+ scores +0x314), rejected
+0x43c (+ scores +0x458, only while score recording is on), the actions to choose from +0x84c, recent actions +0x580. The sorts, vector growth
and copies the original has for these types (0x50f780 introsort family, 0x50dca0 / 0x50de10 / 0x50d6a0 inserts, 0x5109a0 / 0x510aa0 / 0x510920
copies, 0x511880 / 0x511820 destructors, 0x50fea0 move) are the toolchain's `std::sort` / `std::vector` of these types and are not
reimplemented or hooked. Allocators matter: the recent-action vector uses the game's allocator (RZMemory::Malloc / Free), the others operator new.

Pitfalls found:
- Stack arities again: Prim03 (lane Y) calls `GetPersonData(0x1f, 1, 1)` with three arguments and the kind list's slot 16 with one; the real
  arities are two and two (`GetKind(kind, 1)`): the extra `push 1` of the first belongs to the second call. `ScorePending` makes the calls
  with their own counts. `0x5150d0` calls the generator's `RandomUint32Uniform(limit)` with the limit pushed before `holder->GetRandom()`.
- Return registers: `InteractionTableEntry::GetEntryIndex` (slot 8) is compared as the whole eax with a sign-extended word; declared `u32`.
- Uninitialised bytes: the rejected `ActionRecord` `CheckInteraction` builds carries stack leftovers (+8..+0x27 except two floats, +0x50) in the
  original; ours are zero, the test compares the set fields only.
- Original bugs reproduced (unreached by the tests, the original crashes): `StartChosenAction` dereferences a missing stack object / owner /
  tracker; `ScoreInteraction` and `CountingObjectId` use the tracker interface of a primitive-forwarding object without a test.
- Harness: fake vtables have 256 slots; slots 280 / 366 (stack object owner, person factor) need a longer table of their own (see
  `tests/simantics_autonomy_start.cpp`, `..._scoreworld.h`). The image's game allocator must be constructed for tests that grow the recent-action
  vector (`GameAllocatorFixture`). `tests/simantics_autonomy_checkworld.h` / `..._scoreworld.h` are worlds over the lane AD person world in which
  the check step and the scoring run for real on both sides.

Full-suite note (2026-10-13, after merging main): `tools/difftest.ps1` with no filter shows 7 failures that are there without lane AF's tests too
(PrimR_Prim10, PrimS_Prim2A, PrimT_Prim71 fault in our code on a null object at offset 0xc, TreeSim_ReplayMapFree, WinMainInfra_SendAnalyticsEvent,
simantics_gsc_small_b45 / b51; each passes alone), and 4 more `simantics_gsc_small_b37 / b40 / b44 / b72` ("ours crashed, the original did not") only when
the `simantics_autonomy_*` tests run before them; those pass alone too. The autonomy tests restore every game global they touch (`AutonomyGlobals`)
and use the image's global allocator only when it is unconstructed, so the likely cause is heap contents left behind (uninitialised reads in the gsc
replacement), not state; the next session should look at it with the gsc lane.

`BeginSearch` 0x515940 (with `std::sort` of the recent actions, descending expiry) and `SimTraceLog::LogSearchStart` 0x5273b0 are ours as well
(`FindBestActionBegin.cpp`, `SimTraceLogChecktree.cpp`; the log's file class constructor 0x1b4dd2 is still an original call).

(Done since by lane AH, see below: the gather chain is ours and verified, Prim03 has no original calls; lane HH, 2026-10-18,
checked the status files and the sources and found nothing of this list left. The paragraph is kept as history.)
Left at the time (the rest of Prim03's original calls): the gather chain `GatherInteractions` 0x5155b0 with
`cEdithObjectTestSim` vf6 0x44fb90 -> 0x44fbc0 (959) / 0x44ff80 AppendInteractionsForMenu (1432) / 0x44f760 AddCheatInteraction / 0x4519c0 /
0x38f140 and the `unordered_map<uint32, uint32>` / list-range helpers it uses (0x3b1d10, 0x42d5f0, 0x15ac20, 0x1fcdb0, 0x195720, 0x1d57e0).
Then `Prim03` must call ours: its test stands the six helpers in with `CodePatch` in the mapped image, which cannot redirect a direct call, so it
needs the check / score world instead. Also still original: 0xb39490 (option mask, used by the score factor), 0x50da10 (motive vector insert,
Prim1D), and `PrimSText.cpp` still calls 0x4529b0 as an original.
In-game trace to check: FindBestAction with score recording off and on, a gathered interaction that needs its check run before scoring (tree
flags 2 / 4), a recent-action discount, a player-controlled and an NPC choice (the random rank), the minimum-score refusal in `StartChosenAction`.

## Lane AE: string-token expansion (cTSObject::vf262, 0x535130; 2026-10-13)

What it is: `cTSObject::vf262` (slot 262, 20,690 bytes, one body shared by cTSObject, cTSPerson and cEdithMTObject) replaces the
`$Token` markers of a text with names and values. `(this, cIGZString *text, int16 stackObjectId, cTSTreeStackElem *frame, int16
outPair[2], void **outPerson)`, `__thiscall`, `ret 0x14`. Callers: MakeActionString (Prim32, 0x59d740) and the dialog / notification
code (0x572720, 0x6159ce, 0x6db392, 0x6e52e6, 0x6e9607, 0x6f7422, 0x707ebb, 0x70f04d, 0x71a80b, 0x7c1824). It works on a copy of the
text: 50 stages in a fixed order, one per marker (`StringTokenExpander::Run` lists them), each finding every occurrence of its marker
with `FindToken` (0x534d90: case-insensitive, optional `:n` and `:n:m` suffixes, the numbers are indices of the frame's *locals*; only
`$NameParam` reads arguments) and replacing it. The search resumes one character after the occurrence's start, so a replacement that
contains a later marker is expanded too. The result is handed back with `text->Copy(work)` (cIGZString slot 17) at the end.

Code: `src/simantics/TokenExpander.h` (the class, the object layout, the constants), `TokenExpander.cpp` (entry, prologue, replace and
frame helpers), `TokenExpanderPeople.cpp` (token names, families, sims, neighborhoods, lots, full names), `TokenExpanderValues.cpp`
(numbers, times, object names), `TokenExpanderStrings.cpp` (string-table texts, days, wants and fears, group and property texts),
`TokenExpanderInline.h`, `TokenFinder.{h,cpp}`. Method facts: `db/methods/SimAnticsTokens.jsonl` (tentative names; the game's
objects behind them are described by what the stages do with them). Hooks: `src/hook/tables/SimAnticsTokens.cpp` (0x535130, 0x534d90,
0x20de26); `src/ts/cTSString.{h,cpp}` + `src/hook/tables/cTSString.cpp` (0x1d0fb0, 0x1d1000). Status: `db/status/SimAnticsTokens.jsonl`,
`db/status/cTSString.jsonl` (all `verified`).

Facts for the next lane:
- Prologue: the context (this+0x40) is asked for slots 9 (entry table), 10 (people table), 8 (neighborhood table), 24 (definition
  table) in this order; slot 6 (the lookup list) is asked afresh by the stages that need it, and a few stages ask slot 9 again instead
  of using the prologue's table ($LotNameLocal); reproduced call for call. `this+0x1c4` is the owner of the object table (a vector of
  object pointers at +0x1a24 / +0x1a28, id 1 = first); the table entry answers `GetResolvedObject` (slot 280, which returns itself).
- `$TokenName*` / `$TokenDesc*` (8 markers): the family markers use the token set's two accessors the other way round than the person
  markers (Counted with the slot the person's Singular uses); kept, marked in the code. The token set returns a pointer to a cell that
  holds a pointer to the reference holder of the token (`push [eax]` then 0x304910 reads the holder's first word).
- 0x20db44 / 0x20de26 are the language service's query with a second cache global (0x10cc2dc): the "name service" of the full-name
  tokens is the language service itself (`GetLanguage(0)` then slot 28 `FormatFullName`). 0x20de26 is now a `CachedServiceGetter`
  instantiation.
- A call like `push 0x2026960b; call [reg+0x2c]` followed by the string-table constructor means the slot takes no argument and the
  pushed constant is the constructor's last argument (the decompiler shows it as the slot's argument): the group getters
  `GetStringGroup`, `GetPrivateGroup`, `GetSemiGlobalGroup` take none.
- Original bugs reproduced (ORIGINAL-BUG comments, each a crash in the original that the difftest compares as "both crash" on
  `crashy` seeds): `$Attrib` / `$NameAttrib` without a frame or a stack object; `$CatalogLocal` / `$JobTitle` for an object without a
  record; `$TimeLocal` without a language; `$FearDescriptionLocal` / `$WantHelpTextLocal` / `$FearHelpTextLocal` for a null list
  element; a token set that yields a null token. `$TokenNameLocal` can never finish: it asks `this` for interface 0x5a69d3d5, throws
  0x1c when it *gets* one (the test is inverted), and otherwise asks the context for slot 6 and reads the dword at address 0.
- Error injection: a fake that throws mid-stage shows the unwinding rules: the token reference is released only once its AddRef
  returned (`HeldReference::AddRefAndHold` adds the reference before it takes it), the interface a QueryInterface produced is
  released while an error passes.

Still original code (listed in the status entry's `original_calls`): the property text formatter 0x5307b0 (the string-table loader
0x1d0e90 / TSStringLoad was lane AG's, see "Lane AG" below; 487 bytes: a switch on a property kind id that formats numbers through the language; called
only by the property markers).

Verification: `tests/simantics_token_expander.cpp` (6,000 seeded worlds on the recording fake world of `tests/simantics_gsc_world.h`:
a random text of the 50 markers, random options, error injection, crash comparison; the marker strings are built by the test because
the harness does not run the game's static initialisers: without them every marker is the empty string and the stages never end),
`tests/simantics_token_finder.cpp` (40,000 cases), `tests/ts_string.cpp`, `tests/cached_name_service.cpp`. Mutants: FindToken 10/10
killed; the expander 47 + 19 + 3 chosen mutants over every stage family (frame and object-table bounds, stage order, each stage's
slot, argument and formula, the unwinding order of the token reference, the error and crash paths), all killed except two equivalent
ones (the empty-name choice `last.empty() ? first : last` against its mirror differs only when both are empty; `elementMayBeMissing =
!fear` is the same value on every path that reads it); cTSString 6/6 (the seventh, a memset before `std::string()`, is equivalent: the
default constructor clears the whole buffer); the 0x20de26 getter 2/2 (ids swapped, release dropped; mutants of the shared template).
Two bugs the difftest and the mutation round found in the first version of the code: the crash of `$TokenNameLocal` happens after a call
to the context's slot 6 that was missing, and an error thrown by the token reference's AddRef must not release the reference.

## Lane AG: cTSString finished (the string-table loader; 2026-10-13)

Code: `src/ts/cTSString.{h,cpp}`, hooks `src/hook/tables/cTSString.cpp`, status `db/status/cTSString.jsonl`, test `tests/ts_string.cpp`
with the fake resource manager `tests/ts_string_fake.h`, method fact `db/methods/cTSString.jsonl`. All functions of the class and its
getter are `verified`: ConstructEmpty 0x1d0fb0, the loading constructors 0x1d0df0 (with a flags byte, ret 0x14) and 0x1d0e90 (without,
ret 0x10), the loader `Load` (TSStringLoad) 0x1d10d0, Assign 0x1d1000, slot 16 0x1d1050 (a jmp into cRZString::operator=, verified but not
hooked: hookspec refuses thunks), the deleting destructor 0x1d1060 (slot 33) and the resource manager getter 0x9c5b26 (a
`CachedServiceGetter<0x10f51a8, 0x056b906e, 0x656b8efc>`). The other 32 slots of vtable 0xda5128 are cRZString's, already verified.

What the loader does: the object is a cRZString plus the 16-byte resource key (type 0x2026960b, group, instance = table << 16 | entry, a
word that is always 0 at +0x2c), a "loaded" byte (+0x30) and the flags byte (+0x31, sign-extended when passed). `Load` asks the resource
manager (slot 3 of cIGZPersistResourceManager: `GetResource(key, 0x089b7dc8, &out, flags, 0)`) for the key as a cIGZString; when it is found
it copies the text (the resource's Strlen is asked before its Data) and replaces every bullet (U+2022, UTF-8 e2 80 a2) by a zero width space
(U+200B) through two function-local statics (0x10cc044 and 0x10cc068); the resource is released before a failed load clears the text. An error
while the resource is held releases it (RZAutoRef::ReleaseUnguarded: a throwing Release ends the process, in the original too); an error out
of `Load` inside a loading constructor destroys the string again.

Facts for the next lane:
- Callers: the `Loader` helpers in PrimSText, PrimT24, PrimX73, PrimAACommon (the template over the original callees is gone),
  PrimGenericSimCallRefs, PrimR and `StringTableText` of the token expander construct a `cTSString` directly. Nothing of ours calls 0x1d0df0 /
  0x1d0e90 / 0x1d10d0 / 0x1d1000 / 0x1d0fb0 / 0x1d1060 through OriginalFunction any more.
- Testing a caller that loads texts: install the fake of `tests/ts_string_fake.h` (`ManagerFixture::InstallManagerOnly()` for a world that
  keeps its own service object, plus `tsfake::gAnswerer`, a callback `(ask, text) -> found`) instead of redirecting 0x1d0df0 in the image. Both
  the original and ours then run the real loader. The fixture builds the two statics of the original's bullet pass (the harness does not run the
  game's static initialisers) and makes their guards read as initialised with the lowest epoch (0x80000000), not by changing the thread's
  epoch: when a test aliases the game's TLS index cell (0x10df814) to `_tls_index`, that epoch is the test exe's own and our function-local
  statics would read as initialised too.
- The S/T tests (`TextFixture` in tests/simantics_prims_s_text.h) keep their own service-level fake and a needle that never matches; they
  pass unchanged with our loader on one side.
- Tests changed because a callee became ours: simantics_prims_x.h / _aa.h / _r_b.cpp, simantics_gsc_case46 / 60 and simantics_token_expander
  replaced their stand-ins for the loader constructor (a redirect of 0x1d0df0 / 0x1d0e90 in the image, which our code no longer reaches) by the
  fake resource manager; their logs keep the same words (0x10ad, table, entry, group, flag, type).
- Mutation: cTSString.cpp / .h 21 mutants (bullet and replacement strings, loaded byte, flags, interface id, reserved word, Strlen/Data order,
  clear on failure, release, unwinding in the constructors, key word 3, table/entry swap, group, flags in the no-flags constructor, flags
  zero-extended, delete flag, destructor, slot 16 result, the getter's two ids): all killed. Callers: 1 to 3 mutants each in PrimX73, PrimR,
  the token expander, PrimGenericSimCallRefs (case 46), PrimAACommon, PrimSText, PrimT24 (swapped table/entry, dropped flags, dropped Assign):
  all killed.
- Not tested: an error out of the final Release (it ends the process, as in the original).

## Lane AH: the gather step of FindBestAction, and Prim03 without original calls (2026-10-14)

`PrimPersonObject::Prim03` (opcode 0x03) now calls only our code: `BeginSearch`, `GatherInteractions`, `ScorePending`,
`ScoreInteraction`, `ChooseAction`, `StartChosenAction`; its `original_calls` are empty. It also uses the named interfaces for the
kind lookup (the real arities are `GetPersonData(0x1f, 1)` and `GetKind(kind, 1)`) and the module's function table.

Code (RVAs 1.18.0.187):
- `GatherInteractions` 0x5155b0 (`src/simantics/FindBestActionGather.cpp`): asks the object module (person slot 63, `cEdithObjectModule`,
  slot 48) for interaction groups (nodes of its std::list: stack object + vector of table entry indexes), looks every stack object
  up again in the object table, keeps the available, unhidden, not busy ones (cTSObject slots 188, 25 mask 0x10000, 190, attribute 0x38
  & 5) and, unless Prim03's flag 0x81c limits the search to the object the frame's argument 0 names, those whose owner id is in the set
  the person's cITSPerson slot 150 fills. Each kept object goes to the test sim's `AppendInteractions` (flag A set: autonomy gather).
- `cEdithObjectTestSim` gather members (`EdithObjectTestSimGather.cpp`): `AppendInteractions` 0x44fb90 (slot 6), `AppendAutonomousInteractions`
  0x44fbc0, `AppendInteractionsForMenu` 0x44ff80 (also publishes itself and the list in globals 0x10da960 / 0x10da964 for 0x4526a0),
  `AppendIfPasses` 0x4519c0 (check tree run, then list), `AddCheatInteraction` 0x44f760 (cdecl, 7 arguments). The sim's field +0x14 is
  `mEntryIndexes` (the group's vector, set by the one-store setter 0x20daf0, not hooked); +0xc is the stack object itself (slot 95 returns `this`).
- `ServiceRefVector` (`ServiceRefVector.{h,cpp}`): vectors of counted pointers whose storage is the allocator service (= RZMemory::Malloc/Free):
  `InsertGrown` 0x1d57e0 (the reallocating insert; 0x14aac0 is the same algorithm for operator new), `ReleaseAndFree` 0x38f140,
  `ServiceBlockVector::FreeBlock` 0x195720. `mPending` is one.
- The gather's local `std::unordered_set<uint32_t>` has the game's allocator (`OwnerIdSet`, 0x20 bytes): its head node (0x3b1d10), buckets
  (0x42d5f0), free (0x1fcdb0) and lookup (0x15ac20) are the toolchain's instantiation; they are not hooked and no longer called.
- Method facts: `db/methods/SimAnticsGather.jsonl` (interaction table = cTSTreeTable slots 9/12/13/19, table entry slots 56/87, interaction
  slots 7/8/13/14/26/41/48/78, cITSPerson 126-129 and 150, module 48/49, framework `CreateInstance`). `InteractionTableEntry` slot 39 is
  renamed `IsDebugOnly` (flag bit 7; the runner lets it through only on a 0x2000 stack object with the debug setting on, the gathers skip it).

Pitfalls: the sim's `0x4519c0` takes four stack arguments (`ret 0x10`), `0x44fbc0`/`0x44ff80` two, vf6 three; the `push 0; push 0x32` before the
entry's slot 8 call belong to the interaction's `SetUp` (5 arguments). The nine-slot calls of 0x44ff80 keep an unused empty reference
vector (its 0x38f140 does nothing). Original bugs reproduced and unreached by tests: a null table entry (0x44fbc0 fetches the table name and
then dereferences it), the unchecked `QueryInterface` of a Sim stack object in 0x44fbc0, a Sim without a current interaction.
The score world's interaction 5 sits on the person itself: a check run on it crashes (the person has no slot 135), so the gather worlds move it.

Tests: `tests/simantics_autonomy_gatherworld.h` (gather world over the score/check/person worlds; the director chain, allocator service
and COM director are set in the image), `tests/simantics_autonomy_gather.cpp` (six tests), `tests/simantics_service_ref_vector.cpp`,
`tests/simantics_autonomy_searchworld.h` + `tests/simantics_prims_person_03.cpp` (the whole opcode on both sides, no stand-ins; the input
object also leaves interaction/entry pairs in the reference vectors). All 24 harness fakes are taken, so gather roles share fakes by slot.

## Lane AJ: removing original callees (2026-10-14)

Done (all verified, hooked, `original_calls` entries removed from their callers):
- `SetActionStringShown` 0x4529b0 now called by MakeActionString (0x59d740); `HasOptionBits` 0xb39490 (`src/app/AppInit.cpp`) by
  `GetStackObjectScoreFactor`; `RefPtrVector::EmplaceReallocate` 0x14aac0 (`src/simantics/RefPtrVectorGrow.cpp`, the `operator new` twin of
  `ServiceRefVector::InsertGrown`: the capacity limit throws bad_alloc where the service version clamps) by cTSBehaviorTree's pushes;
  `FormatPropertyText` 0x5307b0 (`src/simantics/PropertyText.cpp`) by the token expander's `$GlobalString`-style stage.
- `src/simantics/SimGeometry.*`: 0x1a6c40 quaternion rotation, 0x7dd0d0 object vector, 0x5a9de0/0x5a9a90 float sin/cos, 0x7dcb60/0x7dcc50/
  0x7dd620 tile and position conversions, 0x7dcde0/0x7dcdb0/0x7dccd0 notch functions. Callers: GenericSimCall, Snap (X2E), T17, S2A, RFind.
- `src/simantics/SnapReport.*`: 0x7f0c00 (`SnapErrorReport::LookupMessage`) and 0x7f0ca0 (`SnapStateReports::BuildReport`), used by Snap (X2E) and T71.

Learned: the x87 steps of 0x7dccd0 (fadd, the fmod intrinsic) give the same bits as double arithmetic when the process runs at 53-bit
precision; `std::sin(double)` of this CRT equals the game's `_libm_sse2_sin_precise` result; SSE NaN-with-NaN operations follow operand
order, so NaN inputs are left out of the rotation test. A test can watch `operator new/delete` exactly by patching the image's
0x824632/0x824624 and our own `operator new/delete` to logging stubs (`tests/simantics_ref_ptr_vector.cpp`).

Left (not done, why): 0x1b4dd2 / 0x1b52d8 (cRZOFStream constructor/destructor, used by the debug log in Prim12 and
`SimTraceLog::LogSearchStart`) sit on the game's own `basic_ofstream` instantiation (0x1b4a09 -> 0x1b4a93 filebuf, 0x1b7f42 open, 0x1b48d6,
0x1b6faf, none library-matched) and open a file, which the harness traps: a class lane (cRZOFStream plus the file-stream chain) with a file
I/O fixture. SnapState construct/destroy 0x7f02f0/0x7f04e0 need four map-node allocators (0x2e5870, 0x7f0da0, 0x7f0d00) and four erasers
(0x7efa10..0x7efbb0). The Snap worker 0x5a6780 (1.7 KB) and 0x7dcc00 -> 0x7dd160 -> 0x7dd1b0 -> 0x7e4680 (cell to vector) are the next
Snap-side bodies; 0x5a98e0/0x5a9bd0/0x5a9c60/0x5a9970 are unoptimised debug-iterator copies (list begin/end) of one container.

## Lane GN: cTSTreeTableEntry, the interaction table's entry (2026-10-18)

The whole class in the standalone shape: `src/simantics/cTSTreeTableEntry.{h,cpp}` (our class deriving
`cITSTreeTableEntry : cIGZSerializable`, compiler vtable, 99 slots), hooks `src/hook/tables/cTSTreeTableEntry.cpp`, test
`tests/simantics_tree_table_entry.cpp`, status `db/status/cTSTreeTableEntry.jsonl`. vftable 0xdc4440, 0x7c bytes; built by
cTSTreeTable (0x431410 slot 15, 0x433b10 Read) and one site at 0x432896 (outside a known function).

Layout (+0x04 refcount, +0x08 check tree id, +0x0a action tree id, +0x0c UI display type (i16), +0x10 motive ads by life stage
0..4 (vector of vector of cTSTreeTableAd), +0x1c trait ads by life stage 5..12 (three levels, cTSTraitAdValue), +0x28 attenuation code
(0 custom, 1 none, 2..4 tuning ids 1000/0x3e9..0x3ed), +0x2c custom attenuation, +0x30 flags, +0x34 index, +0x38 autonomy threshold
(>= 100 signed: autonomy off), +0x3c join index, +0x40 facial anim type, +0x44 memory iterative multiplier, +0x48 name (cRZString),
+0x68 icon table index, +0x6c icon guid, +0x70 use records {tick, actor (i16), 0} through the allocator service). The ads are
polymorphic 12-byte triples `cTSTriple<short>` (game vtables 0xdc4428 base, 0xdc4430 cTSTreeTableAd, 0xdc4438 cTSTraitAdValue);
a motive ad is {type (row of the 12-byte table 0xdc4314: person data index, inverted), min, delta}. Slot names and meanings are in
the header; flag bits per slot: 1 CanJoin, 2 RunImmediately, 5, 7 DebugOnly, 8 AutoFirstSelect, 14 AllowSubQueue, 15
TypeSubQueue, 17 ParentalControl, 18 AffectsOwnership, 19 ResetMotiveDecay, 20 AllowOnSelf, 21 OverrideDisable, 22, 23
PreferredByFurious; age bits 0x3f013e51.

Facts that were easy to get wrong:
- The constructor 0x430f60 takes a stack word it never reads (`ret 4`; cTSTreeTable passes its slot 8). A test calling it as
  `(this)` corrupts its caller's stack (the symptom was a seed variable changing between routes).
- Slots 65 (GetAdValue) and 67 (GetTraitAdFor) return **float** in st0 (fldz on every early exit), not an int: a caller reading
  eax sees leftovers, and a test calling them as int leaks x87 registers until a later float result reads as the indefinite NaN
  (0xffc00000). Values are thousandths: motive ad ((weight * 0.001) * delta + min) * 0.001, weight the person's data (slot 10) or
  1000 minus it per the type row; trait ads the sum of each element's value picked by two mask bits (bit 2j first, 2j+1 second,
  else third; the shift count wraps at 32), * 0.001.
- The final `* 0.001` of slots 66 and 67 is an x87 fmul left in st0 at extended precision: a product of two floats is exact
  there, so callers that store it (all seen) get our single-precision product.
- Slot 57 (GetMotiveAd) with `create` and a negative stage: the signed `> 4` check lets it through, the stages are resized to 0
  and element -1 is read (ORIGINAL-BUG, reproduced; the difftest leaves that input out).
- Slot 73's third argument is an index into the use records (resized up to it), not an id compared against.
- Read (slot 4) has three formats by the stream owner's version (stream slot 20, owner slot 78): below 0x44, 0x44..0x53 (stage
  counts first, ads stored min/delta/type and set through slot 59), from 0x54 whole vectors. Versions below 0x52 toggle flags
  0x10 and 0x40. A failed read leaves some counts in slots that held `this` or the version in the original; ours starts them at 0
  (only a stream that leaves its out-parameter unwritten can tell).

Verified: all 78 hooked functions (45 byte matches, 957 bytes; the other 33 by difftest; 10970 bytes). The test runs each case
on three entries seeded alike (original constructor + RVA; our constructor + our vtable slot found by `shape::SlotOf`; the
adapter on an original entry with a trap in that slot) and compares results, every fake call and the whole entry including
vector capacities. The folded slots (shared_vf_*: plain getters/setters, AddRef, GetGZCLSID, AsTreeTableEntry) are not hooked;
`TreeTableEntry_Slots` checks ours against the game's slot on the same entries.

Left: DumpText (slot 92, 0x432060, 1888 bytes of tab-separated debug output) is the original's code: our slot 92 forwards to it
through `OriginalFunction` (listed in the constructor's original_calls), and the RVA is not hooked (a hook would loop). The STL
instantiations the class used (0x435d90, 0x435ed0, 0x435cf0, 0x435e30, 0x435c70, 0x42f3e0, 0x42f860, 0x42f660, 0x42fcb0, 0x4300e0,
0x430a10, 0x42fe80, 0x42ffc0, 0x430bb0, 0x430a40, 0x430680, 0x435ae0) were not hooked: ours uses our std::vector; whether
other classes still call them was not checked (`tools/callers.py`). The ad element classes (cTSTriple and its two subclasses) are ours in our
entries; entries built while the Construct hook is off keep the game's element vtables (shared deleting destructor 0x4311c0).

## Lane GU: cTSBehavior, an object's behaviour context (2026-10-18)

The whole class in the standalone shape: `src/simantics/cTSBehavior.{h,cpp}` (our class deriving `cITSBehavior : cIGZUnknown`,
compiler vtable, 51 slots, the deleting destructor last), hooks `src/hook/tables/cTSBehavior.cpp`, test
`tests/simantics_behavior.cpp`, status `db/status/cTSBehavior.jsonl`. vftable 0xdc916c, 0x28 bytes, interfaces 1 and
0x7b269499, class id 0x1b269450. The factory 0x469e70 (`__cdecl`, no arguments) is registered by address with the sim COM
director (0x415c27) and is the only place the game builds one: its hook is the switch. The vtable is referenced only by the
factory and the deleting destructor 0x469560, so every slot is reached through objects only; the slots that are not byte
matches have no adapter and no hook, and their originals are trapped behind `Hooks.cTSBehavior_Create` (25 traps).

Layout: +4 refcount, +8 global source, +0xc global group, +0x10 semi-global source, +0x14 semi-global group, +0x18 private
source, +0x1c private group, +0x20 owner word (Init's first argument), +0x24 primitive set. Tree ids are 16 bits: below 0x100
primitives, 0x100..0xfff global, 0x1000..0x1fff private, 0x2000..0x2710 semi-global (kinds 0 prim, 1 global, 2 private, 3
semi-global; slot 47 / 0x46f230 compares unsigned, slots 12 and 17 signed). Trees are BHAV resources {0x42484156, group of the id,
id sign-extended, 0} from the resource cache (GetCachedSingletonC slot 72, interface 0x5b267c7c); constants are BCON the same
way. Breakpoints live in a static `std::vector` of 0x28-byte {cRZString source name, int tree id, int node} at 0x10da9f4,
shared with the original file reader 0x46f290: ours works on the game's vector directly.

Facts that were easy to get wrong:
- The factory leaves +0x0c (global group) and +0x20 (owner) unwritten; ours does too (the test leaves those two words out of the
  factory comparison).
- GetTreeSize (slot 30) is the root's size shifted 16 bits up, plus the sizes of every tree reachable through calls (node opcodes
  >= 0x100), the root included. The original's two work vectors are function statics with thread-safe guards (0x10daa0c,
  0x10daa1c): a test must mark them initialised, or the original runs the CRT's init-thread code.
- GetTree, UncacheTree and GetStringCount use the resource without a null test when the lookup answers true; CreateTree uses
  the database's tree even when it answers false (ORIGINAL-BUG, kept). The fakes answer consistently (found means written).
- GetTreeName and DescribeNode call the primitive set at +0x24 without a null test (Init always sets it in game).
- Slot 46 (0x46a280) is labelled as a library function (std::less) but is the forwarder to KindName 0x46f1c0 (`ret 8`); ours
  implements the slot, and there is no status entry or hook for it. 0x46c000 (slot 47) forwards to KindForId the same way.
- Slots 12 and 17 read only the id's low word (`mov edx, [esp+4]` then 16-bit compares): with an `int16_t` parameter (the
  adapter's too) they byte-match; with a 32-bit one MSVC adds a `movzx`.
- Void slots 28 and 42 (folded 0x140fe0) leave the call target in eax: compare nothing for them.

Verified: 40 functions, 6804 bytes; 12 byte matches (matchcheck v8, 574 bytes: QueryInterface, ApplyAllBreakpoints, the three
source setters, GetGroupForId, GetSourceForId, GetSourceName, DescribePrimitive, LoadBreakpoints, GetKindForId, KindForId); the
rest by difftest. Each case runs on objects seeded alike against one fake world (every fake call logged with its arguments and
keys): the original factory's object through the game's slot, ours through our slot (`shape::SlotOf`), and for the kept
adapters the adapter on the original's object with a trap in its slot; compared are the result, the call log, every object word,
every fake's reference count, the test strings and the game's breakpoint list. The folded slots (AddRef, Release, the plain
getters, the empty slots, AsBehavior) are checked against the game's slots in `Behavior_Slots`.

Left: the file work stays original code, called through `OriginalFunction`: the breakpoint file reader 0x46f290 (slot 41) and
the tree dump writer 0x46c610 (slot 43; it calls 0x46ca20, which calls KindName and KindForId, both hooked). GetBaseIdForKind and
GetMaxIdForKind compile to the original's instructions, but matchcheck counts our jump table (placed inside our function) in the
size, so they are difftest-verified and have no hook. In game: anything that runs trees exercises GetTree, GetGroupForId and
GetSourceForId; the SimAntics debugger exercises breakpoints; SaveTree runs when the tree editor saves.

## Lane GV: cTSInteraction, the queued interaction (2026-10-18)

The whole class in the standalone shape: `src/simantics/cTSInteraction.{h,cpp}` (our class deriving `cITSInteraction :
cIGZSerializable : cIGZUnknown`, one vtable at +0, 81 slots, the deleting destructor slot 79), hooks
`src/hook/tables/cTSInteraction.cpp`, test `tests/simantics_interaction.cpp`, status `db/status/cTSInteraction.jsonl`. vftable
0xdc8324, 0x6c bytes, class id 0xfc5ecc3d, interface 0x5aca6692. The only creator is 0x450a70 (cdecl, `new 0x6c` with the
constructor inline), registered by address in the cTSSimCOMDirector constructor (0x415bbb; src/rz/COMDirectorData.cpp): it is a
manual entry in `db/missed-functions.jsonl` (Ghidra has no function there) and its hook `cTSInteraction_Create` is the switch that
makes every interaction ours. Only the creator and the deleting destructor reference the vtable.

Layout: +0x04 refcount, +0x08 source kind (1 after SetUpBetween), +0x0c actor object id, +0x0e stack object id, +0x10 group object
id, +0x12 extra object id (all i16), +0x14 interaction index (-1 when created), +0x18 four i16 tree arguments, +0x20 priority,
+0x24 action tree id, +0x28 attenuation (float), +0x2c / +0x2e allow / type sub-queue (u16), +0x30 UI display type, +0x32 / +0x34
words (slots 74..77), +0x36 padding (never written), +0x38 / +0x3c parameters A / B, +0x40 link id, +0x42 next link id, +0x44 name
(cRZString), +0x64 serial, +0x68 flags. Flag pairs (slots 31..60) use bits 0, 1, 14, 2, 3, 4, 5, 6, 10, 11, 7, 8, 9, 12, 13 in slot
order: 0 AutoFirstSelect, 2 CheckPending, 3 CheckPassed, 5 Branch, 7 NameSet (SetName sets it, GetName clears it), 9 Vetoed.

Corrections to the earlier tentative facts (lane AD and the method facts): slot 12 is the attenuation (a float the entry gives per
the actor's choice byte, person slot 30), not a distance; slot 13 is the action tree id, not a menu string id; slot 67 is
CancelLinked(exceptPerson), not SetOwnerPerson; slot 7 is SetUpBetween(actor, source): the first argument is the actor, the
second the person whose current interaction (person slot 132) is copied and joined at its entry's join index.

Facts that were easy to get wrong:
- SetUp passes the attenuation choice to the entry as a zero-extended byte (`movzx`), not a bool.
- SetUpBetween with a null actor still looks up the actor id -1, and with an entry the original reads the actor's slot 30
  unchecked (0x4511f7): a test needs a Sim actor whenever the entry exists.
- Read (slot 4): versions from the stream owner (stream slot 20, owner slot 78): +0x12 from 0xa6; below 0x3e five bools through
  the setters (the fifth from 0x3c); from 0x3e the flag word (read into a temporary holding the version), +0x30 from 0x6f, sub-queues
  from 0x72, links from 0x7c, name and parameters from 0x97 with the language check (stream QI 0xe56b8f03, resource slot 3 key,
  framework object 0xcdcbe0e3/0xcdcbe0dd, its slots 11 and 6; the name is cleared when the language service's slot 7 differs),
  +0x32 from 0xbb. The serial counter 0x10da95c is raised to the serial (signed compare).
- FindByLinkId (slot 71) returns the last interaction it examined when none matches (ORIGINAL-BUG, reproduced).
- CancelLinked follows the chain from the next link over every person but the one excluded; a current or selected match sets the
  person's data 0x6e (person slot 11), a pending or queued one is removed by serial (person slot 123).

Verified: 65 functions (the creator, 54 byte matches with adapters, 10 by difftest through their callers). Byte matches came
from writing the object lookups to fetch the table before reading the id, and Release / HasGroup in the original's branch shape.
The 10 difftested slots (Read, SetUpBetween, SetUp, CopyFrom, GetActor, GetTableEntry, GetName, CancelLinked, FindByLinkId, the
deleting destructor) have no adapter and no hook: `"in_effect": "callers"`, trapped under `Hooks.cTSInteraction_Create=1`. The 17
folded slots (shared_vf_*) are ours in our vtable and tested against the game's slot, never hooked. No original calls.

## Simulation lanes

(Moved from `docs/lane/brief.md`, 2026-11-09.) The default route is `trace-verified` (docs/verification.md), not a per-function difftest: port the function, hook it, check
it builds and the full difftest still passes, and mark it `reimplemented` with a note naming what the
trace suite must exercise (opcodes, internal cases, owners/scopes). The main session deploys, runs
the seeded trace scenarios and promotes it. Write a difftest only where it is cheap (small pure
helpers, value conversions) or where a case is unlikely to be reached by the scenarios; say which.
Spend the time saved on understanding the code: the trace check only catches what the scenarios
reach, so a careful port matters more here.
