# Verification: what counts

The one reference for status levels, the evidence each needs, and how a replacement is in effect. Every rule is stated
here once; other pages point here. Reasons and history are in `notes/decisions.md` (cited by date and title) and in
`docs/archive/proposals/byte-match-verification.md` (matchcheck's rules, version by version). When a rule changes, record it in
decisions.md and edit this page.

Status entries live in `db/status/<group>.jsonl`, one line per function, exactly one status each; `tools/status.py
--check` validates them (fields below). Work a lane reports counts only after the coordinator has merged it and run the
smoke test (CLAUDE.md, "Parallel agents").

## The table

"Smoke" means the coordinator's in-game smoke run passes with the replacement in effect (procedure below). "Mutants"
means the mutation check below.

| Case | Status it may get | Required evidence | How it is in effect | Decided |
|---|---|---|---|---|
| Named, typed, purpose understood | `mapped` | RTTI, strings, references, community sources | not ours | CLAUDE.md (founding rules) |
| Ours, without the evidence for a higher status (e.g. not isolable yet: rendering, UI, an original that reaches a harness import trap) | `reimplemented` | hand-written C++ in `src/`; smoke | hook at its RVA (or callers, below) | CLAUDE.md (founding rules) |
| Ours, difftested | `verified` | difftest (procedure below) comparing results and side effects, naming every RVA it verifies; mutants; smoke | hook (or callers) | CLAUDE.md (founding rules); mutants capped 2026-10-13 "Process changes" |
| Ours, byte match | `verified`, `"verify": "match"`, `"matchcheck": N` | a verifiable `MATCH` from `tools/matchcheck.py` (procedure below); no test file or mutants needed; smoke | hook: the hook's replacement is what matched | 2026-10-15 "byte-match verification"; equivalences v2 2026-10-16 (proposal) |
| Byte match through our vtable (standalone shape: the RVA's hook is a one-statement forwarding adapter) | `verified`, `"verify": "match"`, `"matchcheck": 7` or later | `MATCH` with "vtable-tie": the member our vtable holds in the RVA's slot has the original's bytes (v8 also pairs `gz::`/`rz::` classes by unambiguous bare RTTI name); smoke | hook (the adapter); the adapter stays even when nothing else references the RVA | 2026-10-18 "byte match through our vtable (matchcheck v7 and v8)" |
| Slot functions of a class in the standalone shape | as for any function (difftest or match) | the difftest builds our side with our constructor and calls every slot through our vtable; an adapters test with trap vtables (`docs/standalone-shape.md`, Tests) | our vtable on every object the game creates, plus the adapter hook at the RVA | milestone D (accepted 2026-10-18), `docs/standalone-shape.md` |
| Slot adapters nothing references (only the game's vtable, which no live object carries) | unchanged (`verified` / `reimplemented`), `"in_effect": "callers"` | `tools/callers.py --vtable-only` says droppable; the member's test still calls it through our vtable and names the RVA; not for byte-match entries or `keep` lines | callers: adapter and hook removed, the original trapped under `"trap": "Hooks.<hook>=1"` (or a conjunction `Hooks.A=1&Hooks.B=1`) | 2026-10-18 "drop adapters nothing references"; conjunctions lane GT (`docs/standalone-shape.md`, Dropping adapters) |
| Original routines whose work moved into a template or other code of ours, with no one-to-one replacement, reached only through a declared data table every reader of which is replaced (the rule under "In effect through callers, and traps") | `reimplemented`, `"in_effect": "callers"`, `"absorbed_by"`: done for the subsystem figures, never verified bytes | each reader named in `absorbed_by` is `verified` by a difftest, the entry's `test`, which exercises the behaviour the routine had; `callers.py` finds only table slots of those readers | callers: nothing reaches the original, trapped under the readers' cut | 2026-11-06 "replaced behaviour counts as covered" (lane XP) |
| Compiler-made adjustor thunks that exist only in our vtables | `verified`, no hook | a test calls them through our vtable; the entry names the original thunk's RVA and says it is unhooked | our vtable (the class's constructor / factory hooks put it on every object) | 2026-10-18 "adjustor thunks only in our vtables" |
| Adjustor thunks the tools list as `shared_vf_*` (byte-identical across classes, e.g. `sub ecx, 4; jmp <folded AddRef>`) | no entry | implemented by the compiler with the class; covered by the class's slots test | our vtable | pilot DK (`docs/standalone-shape.md`, Status entries) |
| A `shared_vf_*` body shared only within one class family (e.g. one template's instantiations: the allocator pages' Alloc / FreePage) | as for any function (difftest or match), hooked | the image's references to it (`tools/xrefs` / callers.py), all from that family's vtables, jump stubs or members, every one of them ours; hook CRC by `tools/hookcrc.py` (hookspec refuses the name) | hook | 2026-10-19 "shared_vf within one family" (user: relax as the coordinator sees fit) |
| Folded `shared_vf_*` bodies (one body, several unrelated classes) | `verified` without a hook, entered once, in the group of the class that defines the body | implemented as overrides in our vtables and tested through them | our vtables only; never hooked; objects the game builds inline still run the original body until those constructors are ours | 2026-10-18 "folded bodies and embedded strings" (option "keep as is") |
| Started by the host behind a switch (CRT entry, WinMain, start functions, XC static initialisers) | `verified` / `reimplemented`, `"in_effect": "callers"` | the usual evidence, plus the callers check; counts once the switch's in-game run passes with the traps armed and none fired | callers: `"trap": "Startup.OwnStart=..."` (static initialisers: `Startup.OwnStart=1,2&Startup.OwnStatics=1`) | 2026-10-18 "in effect generalised"; milestones E and G (`docs/roadmap.md`) |
| Simulation code (SimAntics primitives, object and person behaviour) | `trace-verified` (lanes leave it `reimplemented` with a note on what the traces must exercise; the coordinator promotes) | trace suite (procedure below); `"trace"` field; a difftest is optional and promotes to `verified` | hook | 2026-10-13 "Process changes"; `docs/archive/review-2026-10-12-throughput.md` item 2 |
| Infrastructure (crash reporting, logging, analytics, Steam/launcher glue, process setup, error handlers; graphics device layer, window creation and setup, device-level input) | `replaced` | a written contract in `docs/contracts/` (interface, what the game reads back, external effects, any gameplay-relevant effect, all preserved), checked by contract tests; smoke | hook | `docs/roadmap.md`; contracts 2026-10-11 "WinMain infrastructure contracts"; list widened 2026-10-17 "graphics device, window and input"; widening rules 2026-10-13 "Subsystem lanes" |
| Simulation, gameplay, data loading or saving, the UI framework, anything visible as the game's look and feel | never `replaced` | | | `docs/roadmap.md`; 2026-10-17 "graphics device, window and input" |
| Third-party library replaced by another library behind its entry points (we can't ship it: Font Fusion -> FreeType, lane OJ) | no `db/status` entries; `db/third-party.jsonl` row `"kind": "replaced"` with a `"replacement"` record | a differential comparison against the original library on the real game data: exact for the shim's own logic (mutated), within tolerances stated in the test for the new library's rendering; the coordinator's in-game visual check | hooked at the library's entry points (`src/hook/tables/<library>.cpp`) | counts toward standalone bytes, never toward `verified` or the EA-code headline; 2026-10-27 "font engine (user)", 2026-10-28 adopted from lane OJ's proposal |
| Library code (CRT, vcruntime, STL): `db/library-matches.jsonl` (byte-identical to the MSVC 14.40 libraries or to STL instantiations compiled by `tools/stlprobe/stlprobe.ps1` under the flag sets EA used; rows with `"kind": "prefix"`/`"unlisted"`/`"inside"` are bodies whose db size differs, or funclets and tails inside one, `tools/libmatch.py`), `db/names.jsonl` entries with source `library` or `ghidra-fid`, `db/crt-equivalents.jsonl` (CRT functions identified by structure or behaviour where EA's CRT version differs); `tools/tsdb.py` `library_rvas`. Not library code: `family_likely` rows whose names are all Concurrency Runtime members (`tsdb.lookalike`: game functions of the same shape, e.g. COM director destructors; the game doesn't use ConcRT) | no entry: never reimplemented or hooked | our toolchain provides it; our code calls the same library function (check the element type first, `docs/lane/write.md`) | ours by construction | CLAUDE.md (founding rules) |
| Jump stubs to a done function; leaf functions under 5 bytes | no entry | counted as done in the coverage and class metrics (`tools/tsdb.py` `resolve_done`); a replacement reproduces a leaf's effect inline | never hooked or trapped (the jump doesn't fit) | CLAUDE.md "Never"; tools |
| Creator that is not a known function start (hooked with a hand-computed CRC) | as usual | its difftest (matchcheck skips it) | hook | `docs/standalone-shape.md`, Status entries |
| A subsystem whose replacements only get "no crash" in game (UI, neighbourhood view, rendering) | | before focused work on it, its testing approach is worked out first | | 2026-10-18 "rule review" |

## Status entry fields

| Field | Meaning | Required |
|---|---|---|
| `rva`, `name`, `status`, `src`, `notes` | `src` holds the replacement (for an adapter-hooked slot, the file with the adapter); `notes`: mutants and results, what is untested and why, skipped comparisons and why, hook or no hook | always |
| `test` | the tests/ file that covers the RVA | every `verified` entry except `"verify": "match"` |
| `"verify": "match"`, `"matchcheck": N` | verified by byte match; N is the version matchcheck printed | byte-match entries (only on `verified`) |
| `trace` | scenarios, traces and call counts of the trace check | `trace-verified` |
| `original_calls` | RVAs of original functions still called through `OriginalFunction` / `OriginalAddress` | when there are any |
| `in_effect` | `"hook"` (the default, may be omitted) or `"callers"` | `"callers"` entries: also the three below |
| `callers_check` | `"callers.py vN YYYY-MM-DD"` | with `"in_effect": "callers"` |
| `trap` | the host switch values under which the replacement runs and the original is trapped: `"Section.Key=v1,v2"`, several joined by `&` (all required); the `Hooks.` terms name exactly the hooks the check rests on, each `=1`; other terms are allowed (they only narrow when the trap arms); `status.py --check` enforces this | with `"in_effect": "callers"` |
| `callers_handled` | `{"<site>": "<why>"}` for references the host takes care of | when there are any |
| `absorbed_by` | RVAs of the functions that took this routine's work: exactly the readers of the declared tables holding it; `status.py --check` and `callers.py` enforce both | routines absorbed by a template (table above) |

`notes` say what is specific to this function: its mutants and results, its gaps, its quirks. What a whole test file
covers is described once, in the test file's header comment; a member's notes name the test rather than repeat that
description (lane PF, 2026-10-29: 45% of all notes bytes were the same text pasted onto every member of a group).

## What counts

- **Headline:** "no longer needs decompiling" (`docs/roadmap.md`, Metrics, has its bands and exclusions): decompilation
  work remaining, not image independence, and never an addition to `verified`.
- **Verified bytes:** `verified` bytes of game code (library excluded), reported split into byte match and difftest
  (`tools/status.py`). `trace-verified` is its own column, never added to `verified`. `replaced` counts toward image
  independence, not toward `verified`. Entries with `absorbed_by` are done in the subsystem figures and have their
  own line in `tools/status.py`'s output; they count toward neither `verified` nor the status table's `reimplemented`.
- **Verified, no known original calls** (was "standalone bytes"; `tools/status.py` prints it): `verified` code that reaches no original code: no `original_calls`, and no direct callee of ours
  that has them (a lower bound: virtual calls are not followed).
- **Original callees:** a replacement may call an original function nobody has replaced yet only through
  `OriginalFunction<Fn, 0xRVA>()` (src/game/original.h; a direct `call rel32`, as the original's own calls;
  `OriginalAddress<Fn, 0xRVA>()` where only the address is needed, e.g. for `atexit`), listed in `original_calls`;
  `tools/origcalls.py` checks them (function starts, not library code, declared). Once the callee has a replacement,
  call ours. Each such call is a dependency on the image that the standalone build must remove.
- **Widening the `replaced` list:** proposed with its size, what the contract preserves, what differs and the time
  saved, and decided with the user.
- **Byte-match claims** are rechecked after the function changes, or periodically; not critical, history keeps the old
  version (2026-10-18 "rule review").

## Procedures

### Difftest
How to write one: `docs/lane/test.md`; for classes in the standalone shape, also `docs/standalone-shape.md`, Tests.
`DIFF_TEST(name, rva)` runs the original (mapped image, no initialisation, every import a trap) and ours on the same
inputs, fuzzed or captured from real play, and compares results **and** side effects. Every RVA marked verified appears
in the test file. A comparison left out has its reason in a comment next to it and in the status notes; a test that
leaves state out because it is "uninitialised" or "unused" cites the original's code showing why (coordinator's lane
rule, 2026-10-18, after the terrain crash). If the original calls a trapped import, the function isn't isolated:
`reimplemented` at most, or replace the callee that reaches it first.

### Mutants
Before a test counts as covering a function: chosen mutants, aimed at what is most likely wrong, with at least one
killed on each covered function's own behaviour (members of a family test need no run of their own). Scale the number to
the risk: one or two for a getter or forwarder, more for branchy code, parsers and anything with ordering or float
subtleties. The coordinator's mutants on main after merging count too, and it aims them at what the lane covered thinly.
Each is killed, or argued equivalent in the notes, and the mutants and results go in the notes. How (`tools/mutate.py`, or by hand): `docs/lane/test.md`, Mutation check. The
coordinator reruns a mutation check on main after merging. Not for byte-match entries (any change changes the bytes).

### Byte match (matchcheck)
`tools/py -I tools/matchcheck.py --rva 0x... --show` on the hooked RVA. Verifying it is a `MATCH`: our replacement,
built with the shipped flags, has the original's bytes over the whole function except relocation fields, and every
relocation resolves to the same target (the same original RVA for calls, jumps and globals; identical contents for
constant data, which since v6 means compiler literals only, other named data matching only through a `ts2_rva` symbol;
jump tables to the same offsets), up to the equivalences and rules of the version that printed it
(`docs/archive/proposals/byte-match-verification.md`). Not verification: `MATCH*` (compiled outside the build, provenance not
verifiable), `EXPERIMENT` (`--extra`), or any similarity score. The function contains no inline asm, `__emit`, naked
functions or byte blobs. A byte-match entry keeps its adapter hook (the tie goes through it). The coordinator reads every
`STACK OBJECT` warning on merge (`docs/orchestrator.md`).

### In effect through callers, and traps
A replacement is in effect when (a) it is hooked at its RVA through the target table, or (b) nothing reaches the
original any more: every reference to it (calls, jumps, vtable slots, data, the host) is our code or redirected to ours,
checked against the image's references rather than assumed, and the original is trapped so a missed caller fails
loudly (a log line `trap: original 0x... reached from ...` and a stop with exit code 0x54524150, never a silent run).
Route (b) needs at least 5 bytes (the trap's jump) and no hook on the RVA. Commands, what `callers.py` can and can't
see, and dropping adapters: `docs/lane/hook-and-record.md` ("In effect through its callers") and
`docs/standalone-shape.md` ("Dropping adapters"). After changing entries: `tools/callers.py --write-traps`,
`tools/status.py --check` (reruns the check) and `tools/callers.py --check-traps`. The entry counts once the run with
its switch on passes with the traps armed and none fired (for a `Hooks.` trap, the ordinary smoke run).
Which hooks a `Hooks.` trap rests on is one rule, `callers.py` `Checker.trap_hooks`, used by both `status.py --check`
and `traps.py --sync`: the nearest hooks that cut every path from live code to the original. A hooked function reached
only through vtables is walked past only when its own references are cut without it; another class's live vtable, our
code calling an original, data and code outside any function are live (rule and its cases:
`docs/lane/hook-and-record.md`, "Traps"; lane LP, 2026-10-23).
A vtable word in a writable section (outside any function or vtable) is a constant-initialised static object's vptr:
the object (based at the word less its vtable's RTTI subobject offset, spanning all its class's vptr words, each of
which must be in place) is dead when every reference to an address in it is code in effect or dead, and there is at
least one; its cut is those references' cuts (a getter's hook: cImageManipDX's static 0xfecf98, only the hooked
getter and its atexit stub reference it). Data, our sources, the host or our build naming the object, or no visible
reference at all, keep it live (`callers.py` `static_objects`; lane NI, 2026-10-26). Don't list such vtable slots in
`callers_handled`: the rule carries both the verdict and the trap's hooks.
The same rule covers a function referenced only from a declared data table: `db/data-tables.jsonl`, each row a reviewed
read-only table with its address range, its row structure (`row_pointers`, `rows`, `row_words`; 0 pointers for a plain
table) and the reason. Tables are never guessed. The check reads the structure back from the image (every pointer word
relocated to a row's start, every row pointed at, every row word relocated to a function's start, a read-only section,
outside functions and vtables) and ignores a declaration that fails, which `status.py --check` reports. A slot is
covered when every reference to an address in the table, row pointers and rows alike, is code in effect or code that
never runs, and there is at least one; the trap's cut is those readers' cuts. The table's own pointer words don't
count. Any reference to the function outside a declared table, any other reference into the table (data or code that
runs), and our sources, the host or our build naming the function or an address in the table keep it live
(`callers.py` `data_tables`; lane XP, 2026-11-06).

### Trace suite (simulation code)
Run by the coordinator: the host trace scenarios (`tools/simtrace.py verify --ref R1 R2 ... --repl P1 ...`; at least two
seeded scenarios scheduled by simulator frames). It passes when the host log shows the hook running (non-zero call
counts) and the replacement runs match the reference runs no worse than two reference runs match each other (objects
that differ between reference runs are the noise). The `trace` field records scenarios, traces and call counts.

### Smoke
Run by the coordinator after merging (`docs/orchestrator.md`, Merging step 3; batches with `tools/smoke.py`): the
standard scenario with the replacements on; the host log shows all hooks installed (`smoke.py` fails a run whose host
skipped one, and names it) and call counts for the new hooks,
no trap fired, the screenshot looks right. Bisect by lane, then by hook (`tools/hookbisect.py`), when it fails. A smoke
run is a precondition, never evidence of `verified` by itself (CLAUDE.md, "Never").
