# Subsystem ownership map

Answers review suggestion A (`docs/archive/review-2026-10-12.md`): which subsystem owns each of the ~61,000
non-library functions, so progress can be read as "subsystem X: N% of bytes" and the big unnamed
functions have an owner. Facts only. Data: `db/subsystems.jsonl` (one line per function:
`{"rva","subsystem","basis"}`, plus `"guess"` on a call-graph guess), produced by `tools/subsystems.py` (run it to
regenerate and print the summary; about a minute). Library functions (`tsdb.library_rvas()`: libmatch matches, library
names, CRT equivalents) are excluded.

## Method

A function gets a subsystem only from evidence about the function itself. A placement that rests on the call graph
alone is a guess: the function is filed under **`other`** and the guess is kept in `"guess"` (user decision,
2026-11-03, after lanes FA-FD found campaign lists built from guesses over-counted badly). Readers that want guesses
read them explicitly (`coverage.py queue|deps --guesses`; `status.py --rva` shows them as "guess").

Evidence, strongest first (the `basis` field says which applied; first match wins):

1. `third-party:<name>`: the address ranges in `db/third-party.jsonl` (OpenSSL, Expat, libjpeg/png/zlib
   sets, Gimex, Speex, Lua, crashpad, MPEG) plus the Steam DRM `.bind` section.
2. `vtable:<class>`: every slot of a named class's vftable (`db/classes.jsonl`). The class goes to a
   subsystem by ordered name rules in `RULES` (namespaces `nTSSG`, `nTSWorld`, `nGZSceneDBAnim`,
   `nTSCASCore`, ... and class-name prefixes `cGZWin*`, `cTSWinProc*`, `cEdith*`, ...); a class that
   matches no rule is judged by its base classes. Slots shared by several classes take the majority
   subsystem; `shared_vf_*` (folded trivial functions) are skipped.
3. `ctor:<class>`: functions that store a named class's vftable address into `this` (constructors, destructors,
   factories), found from the exe's relocation table. The most derived class wins.
4. `string-ref:<tag>`: functions referencing strings that name a subsystem (few keywords; see
   `STRING_RULES`), for example the DirectX error-name table. Evidence: the function's own content, not its callers.
5. `static-init`: initialisers by what they construct or call. Runs before propagation, so it follows evidence only.
6. `status:<class>`: a `db/status` entry (any level, `mapped` or above) whose name's class has a class-backed
   subsystem: the class's subsystem by the name rules (`db/classes`), else the one that 2/3 or more of the class's
   evidence-placed named functions share (a class without RTTI, such as `cRZFile`). A lane that mapped or
   reimplemented the function read it and named its class. `status-group:<class>`: the same for a name that gives no
   known class (a bare name, or an EA qualifier the map doesn't know, such as `nGZSceneGraph::cShapeRefNodeImpl`), by
   its group file when that file is one class's (`db/status/cShapeRefNode.jsonl`). Not for STL / std instantiations
   (a `std::`, `vector<`, `_Tree`, `hash_map`... qualifier, or a bare name whose text says `std::`): those are library
   code a lane met beside its class, not the group's (`STL_QUALIFIER`; rule 8 places the single-user ones). Folded `shared_vf_*` bodies are skipped. Runs after the other evidence: where it disagrees with an existing basis the
   existing one stays, and `subsystems.py` prints the disagreements (`--conflicts N` lists N; 191 at FT, mostly
   inherited base-class slots: ui-game vtables vs ui-windowing names 58, world-lot vs scenegraph 36, objects vs
   simantics-vm 22). Added by lane FT (2026-11-04, coordinator).
   `status-rule:<name>`: a status name whose class (or, for a name without one, whose group file's name) has no
   class-backed subsystem but matches the name rules `RULES` (classes without RTTI, such as
   `nGZCommandParser::EvalFunctionTable`, or a group named `nGZSceneGraph...`). The other-ts rule says nothing here
   (a bare cTS / nTS prefix is no campaign), so such a name falls through to its group. A name with a non-STL
   qualifier is that class's even when its comment mentions `std::` (lane WH, 2026-11-04).
7. Status groups (a `db/status` file named for a group, not a class, such as `SimAnticsPrimsR`): its members that no
   evidence above placed (not `shared_vf_*` bodies, not STL / std instantiations) take the group's subsystem:
   - `group-list:<group>`: from `db/subsystem-groups.jsonl`, the coordinator's list (`{"group", "subsystem",
     "reason"}`, a campaign subsystem: never third-party, other-ts or shared-utility). Seeded by lane WH from what
     the functions do and the `src/` files they live in. `"subsystem": "mixed"` marks a group reviewed as several
     subsystems' code (batches such as `ClassesRound4`, `StaticInit`, the `Std*` instantiations, the Gimex groups):
     its members keep per-function evidence.
   - `group-majority:<group>`: otherwise, when at least 2/3 of the bytes of the group's evidence-placed members are
     in one campaign subsystem (not other-ts, shared-utility or third-party).
   The list wins over the majority, so a listed group whose majority later disagrees is reported, not overridden.
8. `stl-single-user`: a non-library STL / std instantiation (by name: an STL qualifier, or a bare name that says
   `std::`) whose callers are all evidence-placed in one subsystem (not shared-utility or third-party) takes it;
   iterated, so an instantiation called only from another follows it. Library-matched code
   (`db/library-matches.jsonl`) has no row, as before.

Merge check (coordinator, at merge): `tools/subsystems.py --check-groups` lists the status groups with members still in
`other` that neither rule 7 places (with the members' call-graph guesses, to help pick an entry), and notes list entries
that are stale, redundant (the majority now gives the same, or a "mixed" group now has one) or disagree with the
majority. Exit 1 if it lists anything: add or fix an entry in `db/subsystem-groups.jsonl`. Lanes maintain nothing.

Propagation through the call graph (direct calls plus tail-call jumps, callers inside library code ignored),
iterated to a fixpoint between phases. Two rules follow evidence and keep the subsystem when what they follow is
evidence; the rest are guesses:

- `thunk`: a jump stub follows its target (a stub is part of its target: evidence if the target's is).
- `data-ref`: it shares a global referenced by 12 or fewer functions with exactly one subsystem (a class's private
  statics; evidence if every referrer that voted is, else a guess).
- Guesses (filed `other`, `"guess"` kept; a guess of third-party stays `third-party`, below): `called-only-from` (all callers in one subsystem), `calls-into` (calls at
  least two assigned functions, 70% or more in one subsystem), `called-mostly-from` (75% or more of the callers), and
  `shared-callers` (called from three or more subsystems: guess `shared-utility`). A guess never seeds another
  function's evidence: a thunk or data-ref that follows a guess is a guess too.
- `unassigned`: nothing connects it (filed `other`, no guess).
- `inferred:<basis>`: a guess that both address flanks agree with is filed; `flanks` is the flanks as a guess only. Newer bases
  (`vtable-shared`, `ptr-table`, `inside`, `group-force`, `group-held`) and the measurements: Filing the unassigned code, below.
- Third-party is an exclusion category, not a campaign: the EA-code denominator of `status.py`, `coverage.py` and
  `tools/subsystem_progress.py` leaves out `subsystem == "third-party"`. So a call-graph guess of third-party stays
  subsystem `third-party`, keeps its call-graph basis, and carries `"guess": "third-party"` to show it is a guess
  (coordinator, 2026-11-04): otherwise reclassification alone would move the contiguous OpenSSL, HLSL-compiler and
  Gimex clusters into EA code and shift the headline.

Nothing is stored beyond RVA, subsystem, basis and guess. Strings and data references are recomputed from the
unpacked exe on every run.

## Filing the unassigned code (lane XJ, 2026-11-06)

`other` went from 4,856 KB (42.1% of the map, 20,146 functions) to 1,709 KB (14.8%, 8,880 functions). 3,147 KB moved out:
about 2,780 KB on inference from the address order and the rest on new evidence. Campaign sizes below are this map, and they
replace the table under Result for sizing. `tools/subsystems.py --summary --no-write` prints it (about a minute now).

**New evidence** (filed like the older evidence, `tools/subsystems.py`, tests in `tools/tests/test_subsystems.py`):
- `vtable-shared`: a folded `shared_vf_*` body whose vtables are all of one subsystem's classes.
- `ptr-table:<table>`: a run of pointers in data, with its users found through tables of tables. It moved 252 KB of leaf code at 0x8c8000-0x90c000 into rendering-gfx (GZ's software texture span routines for all five
  destination formats, 909 functions in the tables at 0xe4f580 on, already replaced by one template: `docs/subsystems/rendering.md`, "The pixel band").
- `inside:<parent>`: a piece listed as a function inside a body resized by `tools/missed-functions` follows that body.
- `band:<name>` (lane XO): `db/subsystem-bands.jsonl`, `{"lo", "hi", "subsystem", "name", "reason"}`: an address range of
  unnamed code read and judged as one module, for code with no RTTI and no strings. A hand judgement, like the group list;
  a vtable, constructor, status name or listed group inside it wins, nothing from the call graph does. Band functions don't
  vote in `calls-into`: they are helper layers with many callers, and when they did, 57 KB of callers followed them
  (with the helpers filed in world-lot, tool code at 0x690000 became a guess of world-lot). First use: nTSWorld's helper
  module, world-lot.md "The helper module"; 0x212e61, the cached cIWorldDB getter. The bands hold 87.1 KB,
  and filing them took away what held up another 32 KB outside (86 functions: 16.5 KB `inferred` ui-game went back to a
  guess, their votes having been the helpers' own wrong guesses; 6.9 KB of guesses became `unassigned`).
- Code that takes a function's address now counts as a caller of it.
- `group-force:` and `force` in `db/subsystem-groups.jsonl`: the list overrides a wrong class basis (`SimAnticsPrimsGenericSimCall`,
  whose status names put 52 KB of primitive dispatch under `objects`; `SimAnticsPrimExpression`).
- `held` in the same list: the Gimex groups stay guesses of third-party (Open questions, end of this page), so the new evidence does not move
  them into EA code by itself.
- More class rules, which empty `other-ts` (271 KB to 599 B). `nTSCASCore` is now `cas` (60 KB `cTSSimOutfitData` was in
  sim-person), `cRZHTTP`/`cRZFTP` are network-web, `cGZWave`/`cWave*` are audio-video.
- Calling framework-gz or crt-glue no longer votes in `calls-into` (`EVERYONE_CALLS`). Of 10 `calls-into` framework-gz guesses
  checked by hand, 2 were framework code. Ties in propagation no longer depend on hash order, so runs are repeatable.

**Inference.** The exe is laid out by object file, so a function between two evidence functions of one subsystem is usually
that subsystem's. `flanks()` finds the nearest evidence on each side (`other-ts`, `crt-glue` and `shared-utility` are not
evidence of a module). `inferred()` sorts every guess into a stratum, and only `both=guess`, both flanks agreeing with the call
graph, is filed: basis `inferred:<prior basis>`, shown in its own column, third-party never a target. A function the graph
left with nothing, between agreeing flanks, gets the flanks as a guess only (basis `flanks`).

**Measurements.** Two holdouts: hide evidence, rerun `build(hide=...)`, compare with the full map. `labelled` hides the 3,496
functions (1.2 MB) that are named lane work: friendly, because their neighbours stay and the set leans to foundations. `blocks`
hides every function in a random quarter of the 32 KB or 128 KB blocks (four runs, 41,304 functions hidden): harsh, because it
also hides their flanks, and it leans to module boundaries. The real figure should lie between them, which nothing here proves.
Percentages are of bytes where the stratum's flanks or guess named the right subsystem. Sizes are from the run before the Gimex hold:

| Stratum (bytes open when measured) | labelled | blocks | Decision |
|---|---|---|---|
| `both=guess`, gap under 4 KB (1,553 KB) | 98.5% | 86.1% | filed |
| `both=guess`, gap 4-16 KB (997 KB) | 99.4% | 83.6% | filed |
| `both=guess`, gap 16-256 KB (213 KB) | 99.9% (117 functions, all under 64 KB) | 92-93% | filed |
| `both` (flanks, no guess; 372 KB) | 56-83% | 53-87% | guess only |
| `both>guess` (flanks against the graph; 284 KB) | flanks 15-49%, graph 40-73% | flanks 31-91%, graph 6-39% | stays: neither side wins |
| `side=guess` (graph picks a flank; 620 KB) | 86-100% | 62-78% | stays |
| neither (463 KB) | 5-14% | 11-27% | stays |

Splitting `side=guess` by nearer flank and by contiguous runs of one guess found 101 KB that passes (runs of 8 KB or more touching
their flank: 100% and 90%). Not worth another rule.

Hand sample of the filed rows, size-weighted, after the rules were final: 96 functions, read from a digest of callers, callees, strings
and names, not decompiled, 95 right. By subsystem: scenegraph 6/6, world-lot 6/6, ui-game 6/6, objects 6/6, cas 6/6, animation 6/6,
and 5/5 each for audio-video, command-parser, lua-scripting, neighbourhood, network-web, persist-dbpf, rendering-gfx, sim-core, sim-person,
simantics-vm and ui-windowing; framework-gz 4/5 (`cWaveXA`, a class filed wrongly, since fixed). The digest shows the neighbours,
so the sample is not independent of the rule; the block holdout is the floor to plan with. Hand sample of the guesses before
promotion, about 145 functions: 12/12 scenegraph, 11/12 world-lot, 10/10 ui-game, 8/8 objects, cas, audio-video, 7/8 sim-person, 6/8
animation, 7/7 lua-scripting, neighbourhood, ui-windowing, network-web, simantics-vm, 6/7 rendering-gfx, about 5/7 persist-dbpf and
sim-core, about 3/7 command-parser and 2/10 framework-gz (the `calls-into` fault fixed above). Hand sample of what
stayed in `other`: about 20 of 25 guesses right, which agrees with the holdouts. Do not promote those wholesale.

**Before and after, KB.** Evidence + inferred = filed. Guess is what stays in `other` with this subsystem as its guess, so the
true size is about filed plus some of the guess. For scale, a block-holdout error rate of 10% on inferred bytes would move
about 280 KB between neighbouring campaigns; most of the confusions were scenegraph, world-lot, ui-game, rendering and cas/objects
with each other.

| Subsystem | Filed before | Verified before | Filed now | Verified now | Evidence | Inferred | Guess |
|---|---:|---:|---:|---:|---:|---:|---:|
| framework-gz | 469 | 418 | 489 | 419 | 465 | 24 | 49 |
| persist-dbpf | 286 | 197 | 347 | 199 | 311 | 36 | 85 |
| simantics-vm | 184 | 144 | 284 | 196 | 256 | 28 | 37 |
| sim-person | 319 | 161 | 366 | 160 | 272 | 93 | 45 |
| objects | 408 | 215 | 672 | 174 | 441 | 231 | 60 |
| sim-core | 79 | 33 | 117 | 35 | 94 | 23 | 30 |
| world-lot | 522 | 67 | 992 | 67 | 500 | 492 | 133 |
| neighbourhood | 180 | 119 | 220 | 120 | 187 | 32 | 23 |
| scenegraph | 805 | 516 | 1,326 | 519 | 800 | 526 | 253 |
| rendering-gfx | 543 | 521 | 913 | 591 | 870 | 43 | 33 |
| animation | 313 | 7 | 583 | 7 | 355 | 228 | 70 |
| ui-windowing | 225 | 41 | 329 | 41 | 225 | 103 | 35 |
| ui-game | 920 | 31 | 1,596 | 32 | 1,019 | 577 | 324 |
| cas | 303 | 137 | 604 | 138 | 366 | 238 | 84 |
| audio-video | 185 | 83 | 305 | 84 | 204 | 101 | 85 |
| lua-scripting | 32 | 0 | 80 | 0 | 29 | 51 | 54 |
| command-parser | 220 | 131 | 229 | 132 | 223 | 6 | 45 |
| network-web | 50 | 20 | 69 | 21 | 53 | 16 | 18 |
| third-party | 340 | 99 | 347 | 104 | 347 | 0 | (98 of the 347) |
| other-ts | 271 | 75 | 0.6 | 0 | 0.6 | 0 | 0 |
| crt-glue | 17 | 13 | 17 | 13 | 17 | 0 | 4 |
| shared-utility | 0 | 0 | 0 | 0 | 0 | 0 | 47 |
| other | 4,856 | 45 | 1,641 | 24 | | | |

Filed bytes moved between campaigns as well as out of `other`, so a verified figure can fall: objects lost
the 52 KB body above to simantics-vm.

Lane XN (2026-11-06) moved the four rows world-lot, scenegraph, animation and ui-game, and `other`, with two class rules: the whole node
chain cBoundedNodeT\<cIObjectV\> > nTSSG::cObject > cAnimatable > cLocomotable and the custom layer to animation (from scenegraph), and the build
and lot tools and dialogs to ui-game, with their bases (from world-lot, 181 KB; `docs/subsystems/world-lot.md`). The inferred and guessed
bands rippled with them, so other rows' guesses changed by a few KB, and they aren't rewritten here. The rest of this section is XJ's.

**What stayed in `other`** (1,709 KB): a guess with one flank (598 KB), flanks-only guesses (371 KB, of which ui-game 139), a guess with
neither flank (281 KB, including `shared-utility` 47), flanks against the graph (278 KB), and nothing at all (181 KB). It is a long
tail: no run of unfiled code exceeds 41 KB. The largest runs are 0x78f5e6-0x799296 (41 KB), 0x7dd660-0x7e5b40 (32 KB), 0xbad8f0-0xbb46b0 (28 KB)
and 0x56c250-0x572720 (26 KB, lua). `tools/status.py` and the map carry the rest as guesses.

**Dead candidates, not counted as dead** (dead is a proof, `docs/roadmap.md`): 1,984 functions, 349 KB, had no reference found, the
largest 0xc42d84 (7,280 B), 0x694b32 (5,254 B), 0xc130db, 0x739b9d and 0x590080. This is a list to check and not an estimate: the set
mixes real functions with jump-table cases and pieces Ghidra split off the function before (0x694b32 looks like one, unconfirmed).
The pieces would also make their neighbours' sizes too small.

### Campaigns with no map yet, in campaign order

For scheduling map lanes. To do is filed minus verified, KB; calls are direct calls between subsystems (framework-gz not counted in the
"calls to" column).

| Campaign | To do | Guess | Calls from others | Calls to, mostly |
|---|---:|---:|---:|---|
| objects | 499 | 60 | 543 | command-parser 244, persist-dbpf 175, simantics-vm 128 |
| sim-core | 82 | 30 | 72 | ui-game 58, scenegraph 43 |
| lua-scripting | 80 | 54 | 198 | little: objects, persist-dbpf, simantics-vm |
| ui-game with ui-windowing | 1,852 | 359 | 1,171 + 778 | ui-game to ui-windowing 561, audio-video 278, rendering 194. Largest job left |
| audio-video | 221 | 85 | 757 | little outwards; not a leaf from below: check who calls it before leaving it to the end |
| cas | 466 | 84 | 229 | ui-game 266, objects 103, scenegraph 100 |
| sim-person | 206 | 45 | 171 | scenegraph 137, objects 115, animation 72 |
| network-web | 48 | 18 | 121 | persist-dbpf 78. Not a campaign in the roadmap's order; a small leaf |

Remaining work in campaigns that have a map, filed minus verified: world-lot 925 KB (1,057 with its guess; `docs/subsystems/world-lot.md`,
resized by XN), scenegraph 807 KB, animation 576 KB (646 with its guess; `docs/subsystems/animation.md`, XN), rendering-gfx 322 KB (252 KB of it
the pixel code; XJ's figure). The call figures in this table are XJ's, from before XN's moves.

## Result as of lane WH (11.6 MB in 60,781 non-library functions; for sizes, use the table above)

Regenerated by lane WH (2026-11-04), with the status-group rules (Method 6-8). "Guessed" is what the map files under
`other` with this subsystem as its guess; before FT those bytes counted as the subsystem's (and
`unassigned`/`shared-utility` were their own rows). WH moved 349 KB (331 KB verified) out of `other`: simantics-vm
+127 KB (the primitive groups), framework-gz +113 KB, rendering-gfx +48 KB, scenegraph +33 KB, command-parser +13 KB.

| subsystem | funcs | bytes | % | verified bytes | guessed bytes | of which verified |
|---|---:|---:|---:|---:|---:|---:|
| ui-game (cTSWin*, cTSUI*, tools, toolbars, dialogs) | 5499 | 919,638 | 7.9 | 31,325 | 524,970 | 861 |
| scenegraph (nGZSceneGraph, nTSSG nodes) | 6176 | 805,201 | 6.9 | 516,318 | 741,612 | 3,542 |
| world-lot (nTSWorld, roofs/walls/floors/pools, lots, path planner, routes) | 2581 | 521,776 | 4.5 | 66,525 | 619,416 | 1,903 |
| rendering-gfx (nGZGraphic4, soft render, particles, effects, fonts) | 2785 | 502,538 | 4.3 | 435,880 | 107,349 | 1,208 |
| framework-gz (GZ/RZ framework, app, logging, events, ini, strings, containers) | 3233 | 468,851 | 4.0 | 417,841 | 589,681 | 3,241 |
| objects (cTSObject, cEdith* object module/definitions, catalogs, inventory) | 1490 | 407,821 | 3.5 | 215,046 | 250,742 | 490 |
| third-party (Expat, image libs, Gimex, Steam DRM stub, crashpad, Speex, Lua; OpenSSL and the HLSL compiler are library matches now; includes its guesses) | 1635 | 365,806 | 3.1 | 99,440 | 90,432 | 58,027 |
| sim-person (cTSPerson, wants, family, genetics) | 1292 | 318,813 | 2.7 | 161,386 | 179,340 | 955 |
| animation (nGZSceneDBAnim, animatable/locomotable) | 2569 | 312,591 | 2.7 | 6,873 | 197,015 | 15 |
| cas (nTSCASCore, nTSUICAS, sim editor) | 1167 | 303,113 | 2.6 | 136,719 | 197,911 | 13 |
| persist-dbpf (cGZDB*, cGZPersist*, cTSPersist*) | 1385 | 285,741 | 2.5 | 197,343 | 72,665 | 152 |
| other-ts (named cTS*/nTS* classes no rule placed) | 2631 | 241,755 | 2.1 | 25,944 | 143,679 | 3,545 |
| ui-windowing (cGZWin*, HTML control, IME) | 1959 | 225,266 | 1.9 | 40,856 | 118,146 | 507 |
| command-parser (nGZCommandParser and its templates) | 1328 | 219,747 | 1.9 | 130,664 | 46,421 | 4,721 |
| simantics-vm (BHAV container classes and the primitive groups; see below) | 474 | 184,697 | 1.6 | 144,403 | 41,314 | 147 |
| audio-video | 1777 | 184,688 | 1.6 | 82,858 | 147,043 | 420 |
| neighbourhood | 875 | 179,779 | 1.5 | 119,436 | 39,754 | 580 |
| sim-core (cTSSimulator, sim system, configuration) | 507 | 79,391 | 0.7 | 32,621 | 28,734 | 176 |
| network-web | 365 | 49,710 | 0.4 | 20,388 | 13,214 | 121 |
| lua-scripting (cGZLua*, nTSSimulatorLua; the Lua 5.0 library is under third-party) | 228 | 31,883 | 0.3 | 381 | 30,564 | 141 |
| crt-glue (std/iostream classes the library matcher didn't cover) | 116 | 17,579 | 0.2 | 13,318 | 4,402 | 0 |
| shared-utility (a guess only: called from 3 or more subsystems) | 0 | 0 | 0.0 | 0 | 72,675 | 2,601 |
| **other** (guesses above except third-party's, plus 821,913 B `unassigned`) | 20709 | 4,988,560 | 42.9 | 45,985 | | |

Animation campaign: `docs/subsystems/animation.md`. Scenegraph campaign (per-class facts, conventions, fixtures, milestones N and O, what is open): `docs/subsystems/scenegraph.md`; the geometry library it calls (nGZCompGeom, nGZNumerics): `docs/subsystems/compgeom.md`; lane history: `docs/archive/scenegraph-lane-history.md`.

56.3% of non-library bytes (6.54 MB) have a subsystem from evidence: vtable slots 4.33 MB, status names 0.93 MB
(0.60 MB by the name's class, 0.20 MB by name rules, 0.14 MB by the group), status groups 0.21 MB (list 0.17 MB,
majority 0.04 MB), constructors 0.62 MB, third-party ranges 0.27 MB, data-ref, strings 0.07 MB, single-user STL
24 KB, static-init and thunks 10 KB. 36.7% (4.26 MB) is a call-graph guess: called-only-from 3.52 MB, calls-into
0.51 MB, called-mostly-from 0.08 MB, shared-callers 0.07 MB, and guessed data-ref. 7.1% (0.82 MB, 7,781 functions)
is unassigned: 6,110 of them (552 KB) have no direct caller,
so they are reached only through function pointers or vtables of classes without RTTI names; the rest have callers
in several subsystems below the thresholds. The 46 KB of verified code left in `other` is mostly per-function
batches listed "mixed" (StaticInit 13 KB, cTSCompDescription 4 KB, COMDirectors 4 KB), std::sort instantiations
whose callers are only guesses (NameValueTableSort 4 KB), and `shared_vf_*` bodies folded across one class family
(cSurfaceDX 3 KB).

Guessed third-party code (378 functions, about 94 KB, 58 KB of it verified, mostly Gimex: `src/gz/Gimex*`) stays
`third-party` (Method), so it stays out of the EA-code denominator. EA verified, current statuses: 24.71% on the
map before FT, 24.72% with guessed third-party kept, 24.75% with the status basis (16 functions, 4 KB, that were
guessed third-party are EA code by their names: nGZCommandParser helpers, `cRZFile::LengthOfPath`, the
cRZNetSSLManager lock setup). Filing guessed third-party under `other` would have read 25.04%.

### The six large functions

| function | size | subsystem | basis and notes |
|---|---:|---|---|
| 0x522ac | 23,580 | library | HLSL shader compiler (was string-ref: "intrinsic function '%s' is not yet implemented"); now a library match, excluded from the map |
| 0xd5663 | 20,267 | library | a table of HRESULT/COM error names (E_ABORT, CO_E_*), the DirectX error-to-string helper; now a library match |
| 0x598ac0 | 16,341 | simantics-vm | group-list:SimAnticsPrimsW (primitive 0x33; WH). Its only direct caller is 0x534650 (`cTSObject::vf332`, called from `cTSPerson::vf332`) |
| 0x5a12c0 | 15,794 | simantics-vm | group-list:SimAnticsPrimSetToNext (primitive 0x1f; WH); same single caller, 0x534650 |
| 0x122d40 | 15,097 | library | OpenSSL 0.9.7d (0xf1b10..0x136e30), library-matched |
| 0x11f780 | 13,756 | library | OpenSSL, library-matched |

So four of the six are not game code at all: the review's "largest unowned functions" were mostly third-party
code (shader compiler, OpenSSL, DX tables), all library matches now. The two that are game code (0x598ac0, 0x5a12c0)
sit in the 0x57b000..0x5aa000 block next to the `cTSTreeSim` code and are reached only from object slot 332: the
primitive dispatch (lanes have since reimplemented both as SimAntics primitives).

## SimAntics VM

What is established (high confidence):

- RTTI has 68 `*Param` classes derived from `XPrimParam`: one operand-layout class per primitive
  (`IdleParam`, `ExpressionParam`, `GenericSimCallParam`, `FindBestActionParam`, `RandomParam`, ...), vftables
  at 0xdc9430..0xdc96xx, plus seven `...Version0Param` variants (animate-overlay, goto-routing-slot, look-at,
  new-relationship, snap-into, snap, turn-towards) built outside the main switch at 0xc0accb..0xc0b072.
- The BHAV node is `cTSBehaviorNode` (vftable 0xdc8fb4, pooled by `cRZFastAllocator<cTSBehaviorNode>`
  0xdc8fa0); trees are `cTSBehaviorTree` (0xdc9020, serialisable: `Read` at 0x46d530), the resource is
  `cTSBehavior` (0xdc916c), `cTSBehaviorConstants` (0xdc90f4). Tree tables and interactions:
  `cTSTreeTable` (0xdc45d0), `cTSTreeTableEntry` (0xdc4440), `cTSTreeConstants`, `cTSTreeProperties`,
  `cTSInteraction` (0xdc8324).
- **Primitive dispatch by opcode (the table):** function 0x46a860 (3,952 bytes, the `cTSBehaviorNode` operand
  builder; called from `cTSBehaviorNode` slot 0x46d2b0 and 0x46e300) does a switch on the 16-bit opcode:
  opcode 0..0x7d (126 values, bound check `> 0x7d`), a byte index table at 0x46b8c4 (0x7e bytes) selecting one of
  61 jump-table entries at 0x46b7d0 (61 dwords). Each case constructs the matching `*Param` object. Opcodes
  without a primitive (unlisted ones up to 0xff, and negative ones as int16) build the plain base
  `XPrimParam` (vtable 0xe9199c); opcodes from 0x100 (subroutine calls) build `RunPrivateTreeParam`
  (corrected by lane L's differential test of 0x46a860, 2026-10-12). 60 explicit
  primitives, opcode numbering matches the Sims 1 / Sims 2 community primitive numbers:

  `0x00 Idle, 0x01 GenericSimCall, 0x02 Expression, 0x03 FindBestAction, 0x07 Update, 0x08 Random, 0x0a Tutorial,
  0x0b DistanceTo, 0x0c DirectionTo, 0x0d PushAction, 0x0e FindFunctionalObject, 0x0f TreeBreak, 0x10 FindGoodLocation,
  0x11 IdleForInput, 0x12 KillObject, 0x13 MakeNewCharacter, 0x14 CallFunctionalTree, 0x16 TurnTowards, 0x17 PlaySound,
  0x19 Budget, 0x1a NewRelationship, 0x1b GotoRelative, 0x1c CallNamedTree, 0x1d SetMotiveDelta, 0x1e GosubAction,
  0x1f SetToNext, 0x20 TestObjectType, 0x21 Find5WorstMotives, 0x22 UIEffect, 0x23 UserEvent, 0x24 Dialog,
  0x25 TestInteractingWith, 0x2a CreateObject, 0x2d GotoRoutingSlot, 0x2e Snap, 0x30 KillSounds, 0x31 NotifyStackObject,
  0x32 MakeActionString, 0x33 ManageInventory, 0x69 AnimateObj, 0x6a AnimateSim, 0x6b AnimateOverlay, 0x6c AnimateStop,
  0x6d MaterialChange, 0x6e LookAt, 0x6f ChangeLight, 0x70 Effect, 0x71 SnapInto, 0x72 Loco, 0x73 Debug, 0x74 NewReach,
  0x75 Age, 0x76 ArrayOperation, 0x77 Message, 0x78 RayTrace, 0x79 ChangeOutfit, 0x7a OnTimer, 0x7b Cinematic,
  0x7c WantSatisfy, 0x7d FollowSim`

  (The opcode-to-case mapping is read from the byte table, and the `*Param` class of each case from the vftable
  store inside the case; both are exact, but the names are the game's own `*Param` class names, not the
  community's primitive names. Opcodes 0x34..0x68 and the ones missing above build `XPrimParam`.)
- The tree evaluator state: `cTSTreeSim` (vftable 0xdd7814, factory 0x578b50, constructor 0x5755e0),
  `cTSTreeStack` (0xdd77cc), `cTSTreeStackElem` (0xdd771c, pooled via 0xdd7674).

Established by the host trace (2026-10-12, `simtrace`, host/control_simtrace.cpp; notes/log.md):

- The interpreter loop is `cTSTreeSim` slot 13 = 0x5762d0. It reads each node's opcode at 0x576544
  (node slot 3); opcodes from 0x100 are subroutine calls (the BHAV id is the opcode), lower ones are
  primitives, all run through one call site at 0x5768cd: the object's secondary vtable slot 6
  (`cTSObject::vf4_6` 0x52184d, `sub ecx,4; jmp` `cTSObject::vf246` 0x53e560, or a subclass's). The
  result (caller switches on -1..3; seen: 0 false, 1 true, 2 yield, 3) picks the next node.
- vf246 tries the secondary vtable's slot 38 first, then the primary slot 332 (`cTSObject::vf332`
  0x534650), a switch over opcodes 0..0x7e with one handler per primitive, e.g. Expression 0x5890c0,
  Random 0x5a1060 (`this` = object, arguments: the stack frame and the node's param object).
- The stack frame (`cTSTreeStackElem`, vtable 0xdd771c): BHAV id at +8 (16-bit), node index at +0xc
  (16-bit), two 16-bit object ids at +0x18 (low word: the object itself, stable between runs; high
  word: the stack object). A node (16 bytes): vtable 0xdc8fb4, u16 at +4 (1 so far), opcode at +6,
  true/false targets at +8/+0xa, the `*Param` object at +0xc. ExpressionParam: lhs data +4, rhs data
  +6, operator byte +9 (0..0x15, the handler's switch), lhs/rhs data owners +0xa/+0xb (7: literal).
- Two seeded runs with frame-timed clicks run the same primitives for most objects; the first
  differences are timer- or input-like (an Expression on data owner 0x19 flipping a tick early,
  IdleForInput finishing a tick early) and objects created on load running in different ticks.
- The `vf332` switch (0x534650; byte table 0x534aac indexes the pointer table 0x534a04). Each case calls
  `handler(this = object, frame, param)` (`param` = the node's slot 16 result), stores the handler's eax in
  the result and returns al = 1; opcodes not listed take the default case (returns al = 0, result -1).
  A handler signals a bad operand by throwing a 16-bit code (`throw (short)n`, ThrowInfo 0xf52a40, size 2).
  Operands are read through the secondary slot 7 (`cTSObject::vf4_7` 0x531700, 7 stack args, ret 0x1c:
  scope, id, 16-bit destination, float-destination pointer, three zeros), which returns the value in ax.
  Opcode: handler (bytes). 0x00: 0x5988a0 (529). 0x01: 0x58bc50 (367 in db/; really ~51 KB, see
  docs/subsystems/simantics.md). 0x02 Expression: 0x5890c0 (3930).
  0x07: 0x5a86b0 (317). 0x08 Random: 0x5a1060 (509). 0x0a: no handler (result 1 set inline). 0x0b: 0x588eb0
  (526). 0x0c: 0x588bb0 (753). 0x0d: 0x5a0560 (2795). 0x0e: 0x58a2c0 (2845). 0x0f: 0x5a7bb0 (164). 0x10:
  0x58ae50 (3581). 0x12: 0x59cba0 (2471). 0x14: 0x580360 (494). 0x17: 0x59f370 (4587). 0x19: 0x5801b0 (402).
  0x1a: 0x59e6e0 (1186). 0x1c: 0x53de40 (1801). 0x1f: 0x5a12c0 (15794). 0x20: 0x5a79f0 (437). 0x21:
  0x58a110 (430). 0x22: 0x5a7c60 (2589). 0x23: 0x5a87f0 (825). 0x24: 0x5879a0 (4578). 0x2a: 0x585a60
  (2769). 0x2e: 0x5a5210 (5466). 0x30: 0x59d590 (418). 0x31: 0x59eb90 (1032). 0x32: 0x59d740 (1857).
  0x33: 0x598ac0 (16341). 0x6d: 0x5827e0 (11686). 0x70: 0x5805c0 (8626). 0x71: 0x5a6e40 (2979). 0x73:
  0x586560 (4987). 0x76: 0x57f2b0 (3756). 0x77: 0x59de90 (2122). 0x7a: 0x59efa0 (962). 0x7b: 0x585590
  (1228). 0x7c: 0x5a8b30 (1284). 0x7e: 0x5a1260 (84). 0x69, 0x6a, 0x6b, 0x6c, 0x6e, 0x6f share one inline
  case at 0x53491d: if the object's slot 104 (0x1a0) says yes, it looks up an interface (id 0x5a6216f6)
  and forwards to slot 246 (0x3d8) of the object found; otherwise it throws 0x92 (opcode 0x6f) or 0x47.
  All other opcodes (0x03-0x06, 0x09, 0x11, 0x13, 0x15, 0x16, 0x18, 0x1b, 0x1d, 0x1e, 0x25-0x29, 0x2b-0x2d,
  0x2f, 0x34-0x68, 0x72, 0x74, 0x75, 0x78, 0x79, 0x7d) go to the default case.

Earlier candidates (before the trace; kept for reference):

- The interpreter loop that fetches a node, calls the primitive's handler and follows the true/false branch is
  not pinned. Primitive handlers are not in a function-pointer table (no run of function pointers exists in
  `.rdata` outside the CRT, the 800-entry initialiser table and library tables). Candidates:
  `cTSTreeSim` slot 13 = 0x5762d0 (2,847 bytes; calls 0x575ed0, 0x576e70, 0x5270a0, 0x526fb0), slots 12/15
  (0x577460, 0x577e80); `cTSObject` slot 4_7 = 0x531700 (8.9 KB, a 61-case switch whose cases call into
  0x512030/0x7decd0/0x7e43c0), and the object slot-332 chain 0x534650 -> 0x598ac0 / 0x5a12c0 (the two
  15-16 KB functions). The primitive implementations most likely live in the cTSObject/cTSPerson code and
  the 0x57b000..0x5aa000 block rather than in the 432 functions classed as `simantics-vm` (72 KB, which
  are the data classes). For the host trace, hook `cTSTreeSim` slot 13 and the `cTSBehaviorNode`
  operand builder 0x46a860 first and watch which one runs per BHAV step.
- The count `0x7e` bound (126) is the node opcode space; opcodes at or above 0x100 are tree calls
  (confirmed: the trace's subroutine calls, and the builder's `RunPrivateTreeParam`). All `*Param`
  classes share one 0x18-byte layout (vtable, 16 operand bytes, version byte at +0x14) and differ
  only by vtable (lane L, src/simantics/SimAnticsParams.h).

## Triage: "unassigned", "other-ts" and misfiled groups, by owning campaign

Lane LL, 2026-10-23, from run-1023 (`coverage.py queue` / `deps --by group`), before lane FT filed guesses under
`other` (2026-11-04: "unassigned" and "shared-utility" are no longer subsystems; the groups below are now under
`other`, with the guess in `"guess"`). "unassigned" (159 KB) and "other-ts"
(141 KB) are not campaigns (docs/roadmap.md, "Campaign order"): this assigns their groups to the campaign that owns them,
so each campaign plan picks its rows up (`coverage.py queue local/coverage/run-1023.txt --group <group> --functions`).
Also listed: groups that `queue --whole` files under framework-gz or shared-utility but other campaigns own
(docs/subsystems/foundations.md, "Scope"). Assignments are leads (tentative): the owning lane's structure pass confirms. Moved here from docs/subsystems/triage.md by lane VY (2026-11-03); the foundations and rendering rows are in those docs' Next lanes (hand-off tables).

### Method

A group is `queue`'s: the function's class, else "~owner" (the class of the nearest callers). For each group the
evidence was, in order: (1) the class's own functions in db/subsystems.jsonl (where most of the class lives); (2) the
campaigns of the callers outside the group, by callee bytes; (3) the nearest functions in address order that have a
class (MSVC keeps a .cpp's functions together); (4) a manual decision where those disagree or point at a parser or
loader (marked "manual"). The ownerless group ("(none found)", 90 KB, 716 functions) is split per function: its own
db/subsystems.jsonl entry when that is specific (56 KB), else the address neighbours (32 KB), else callers (2 KB).

### Totals by owning campaign

| from | KB | to (KB) |
|---|---:|---|
| unassigned | 159.1 | rendering-gfx 23.3, scenegraph 22.3, objects 19.9, audio-video 19.1, animation 15.9, library (STL) 12.4, framework-gz 9.4, ui-game 9.2, command-parser 5.9, world-lot 4.2, ui-windowing 3.9, lua-scripting 3.6, sim-person 3.0, persist-dbpf 1.9, sim-core 1.8, unknown 1.7, neighbourhood 0.8, other 0.7 |
| other-ts | 140.6 | objects 54.4, rendering-gfx 39.9, sim-core 10.3, scenegraph 9.6, ui-game 7.9, audio-video 6.1, sim-person 4.1, neighbourhood 3.0, world-lot 2.2, persist-dbpf 1.5, other 1.6 |
| framework-gz (groups, --whole) | 140.5 | framework-gz 121.1 (but cRZString's 14.3 is its users' code: below), objects 11.4, audio-video 4.7, scenegraph 1.1, neighbourhood 0.9, command-parser 0.7, ui-game 0.4, library 0.3 |
| shared-utility (groups, --whole) | 43.5 | command-parser 12.7, ui-game 11.1, neighbourhood 3.0, world-lot 2.9, scenegraph 2.1, persist-dbpf 1.8, shared/framework 3.4, objects 1.5, sim-person 1.3, library 1.1, rendering-gfx 1.0, other 1.5 |

"objects" from other-ts is mostly the build-catalog product family (cTSProduct*, the pattern, fence, roof-pattern,
material and CANH entries, about 25 KB): its parsers load catalogue content, world-lot and ui-game consume it; placed with
objects ("catalogs") pending the objects plan (open question 2).

### Groups of 2 KB and more

| from | group | KB | fns | campaign | evidence |
|---|---|---:|---:|---|---|
| unassigned | (no owner): per function | 89.9 | 716 | rendering 22.2, scenegraph 17.1, audio-video 16.0, framework-gz 9.2, ui-game 6.3, world-lot 3.8, lua 3.5, animation 3.4 | clusters below |
| other-ts | cTSMetaParticlesEffect (with ~helpers) | 28.9 | 46 | rendering-gfx (lane E3) | callers cTSVisualEffect, cTSEffectsManager |
| unassigned | nGZSceneDBAnim::cChoreoQueue | 12.5 | 85 | animation | class filed animation; callers scenegraph 11.6, animation 9.8 |
| unassigned | cEdithObject | 11.4 | 50 | objects | class filed objects; callers animation 14.9 |
| other-ts | cTSOverlayManager | 7.3 | 29 | scenegraph | callers scenegraph 5.0 |
| unassigned | PrimRObject | 5.7 | 9 | objects | class filed objects |
| other-ts | cTSDataTables | 5.5 | 24 | sim-core (manual) | registered with the generic persist factory (type 0x3053cf74); callers unassigned and other-ts |
| unassigned | std (unmatched STL instances) | 5.2 | 46 | library | stlprobe (foundations F7) |
| other-ts | cTSToolTipManager | 4.8 | 5 | ui-game | callers ui-game 9.4 |
| other-ts | cTSDecalEffect | 4.4 | 11 | rendering-gfx (E3) | callers rendering 6.5 |
| other-ts | cTSAttractionManager | 4.1 | 3 | sim-person (manual) | called from the load path (persist-dbpf 3.9); attraction between sims |
| other-ts | cTSModelEffect | 3.7 | 11 | rendering-gfx (E3) | callers rendering 3.6 |
| other-ts | cTSSlotLoader | 3.1 | 15 | objects | callers objects 1.3 |
| other-ts | cTSProduct | 2.7 | 42 | objects (manual) | product family |
| other-ts | cTSPatternBase | 2.7 | 18 | objects (manual) | product family; callers cas 5.3, world-lot 1.7 |
| other-ts | cFreshCellPlayer | 2.7 | 16 | audio-video | callers audio-video 1.6 |
| other-ts | cTSConfigDevice | 2.6 | 4 | sim-core (manual) | configuration (ReadDisplayMode 0xb6f140, GetSystemMetrics) |
| unassigned | cTextureFilterHintCommand | 2.4 | 7 | command-parser | class filed command-parser; callers rendering 1.5 |
| other-ts | cTSProductDeckFoundation | 2.2 | 58 | objects (manual) | product family |
| unassigned | std::_Hash<...> (unmatched) | 2.2 | 13 | library | stlprobe |
| other-ts | cTSSaveNeighborhoodController | 2.0 | 7 | neighbourhood (manual) | the save controller beside cTSLoadNeighborhoodController |

Between 1 and 2 KB (campaign, by the same evidence): cTSFloatConstants (objects), cTSConfigMaterialSettings
(sim-core), cTSLightingParserState (scenegraph), cTSProductFence / AwningParser / Parser / Wall / Partition / FenceArch,
cTSModelMaterialEntry, cTSFenceEntry, cTSRoofPatternEntry, cTSCANHEntry, cTSProductPatternEntry (objects), cTSSequenceEffect
(rendering E3), cTSStringPool (objects), cTSAudioCOMDirector (audio-video), cTSEnvironmentScoreManager (objects),
cTSCtrlCreator (ui-game), cLightingStateCommand (command-parser), cTSOverlay (scenegraph), cGZSndEa (audio-video),
cGZPersistResourceManager (persist-dbpf), cTSBaseLotInfo (world-lot), cTSSaveLotController (ui-game), cGZWinHTML
(ui-windowing), cTSCommonSystem (sim-core), cTSWeatherInfo (neighbourhood), cTSLoadLotController (world-lot).

### The ownerless code: clusters of 2 KB and more

By address neighbours (the functions between two classed functions):

| KB | fns | between | campaign | largest |
|---:|---:|---|---|---|
| 13.3 | 55 | cGZFontSys .. cGZWinCOMDirector (filed rendering-gfx) | rendering-gfx (R5/R6 fonts) | 0x914a78 3.6 KB, 0x927faf, 0x928ee6 |
| 12.8 | 69 | no classed neighbours (0xe8f..0xf1x, the EA sound library) | audio-video | 0xf1180 2.4 KB, 0xee15d, 0xe9e0d (timeGetTime) |
| 4.4 | 1 | cCanvasW32 | rendering-gfx (R2) | 0x157800 cCanvasW32::WindowProc (named by LL) |
| 3.5 | 3 | MmxResampleTables | audio-video | 0xc97793 2.9 KB |
| 3.3 | 8 | nTSSimulatorLua::cTSLuaWeatherInfo .. cTSLuaBusinessInfo | lua-scripting | 0x560810, 0x562c00, 0x565650 |
| 2.3 | 3 | cRenderTargetDX .. cDepthStencilSurfaceDX | rendering-gfx (R3) | 0x1614b0 1.6 KB |

Smaller clusters: scenegraph (around cTSRenderTextureSurface, cDisplayListDynamicRenderStrategy, cPrintCompositionTree,
cLotImposterDesc, cLightingManager), framework-gz (near AnyStringSet, cGZPropertySetSerializerBinaryUint32, the
clipboard: foundations F2/F1), ui-game (cTSMainToolbar, cTSWinProcNeighborhoodUCP, nTSUI::cAlbum).

### Filed under framework-gz or shared-utility, owned elsewhere

| group | KB | campaign | why |
|---|---:|---|---|
| cRZString (51 functions) | 14.3 | callers' campaigns: cas 6.2 (ParseFontStyleDefinition 0xb824b0 5.5 KB sits between CAS code but parses UI font styles: rendering R5), scenegraph 2.6 (camera and lighting parser states), others | the class rule: constructors of structs with a cRZString at +0 (foundations.md, Scope) |
| ~cTSObjectFolder | 11.4 | objects | class filed objects; callers objects 21 |
| cOverlayCheat (two groups) | 9.5 | command-parser (cheats) | 0xaf90e0 4.4 KB option-spec parser used by cheat commands |
| ~cTSWinProcClothingDialog | 4.8 | framework-gz (F5) | really LoadUIScript 0xb41b50 and 0xb40d20 |
| cTSAudioResourceManager | 4.7 | audio-video | class mostly audio-video |
| cTSUserToolObject, ~CameraRightClick, ~ModularStairs | 7.3 | ui-game | class filed ui-game |
| ~cTSWinSceneDBShape | 2.2 | ui-game | class filed ui-game |
| ~cTSUserToolFireplace | 1.9 | world-lot | class filed world-lot |
| ~cTSNHoodOccupantManager, ~cTSNeighborhoodManager | 2.9 | neighbourhood | class filed neighbourhood |
| cTSRenderTargetTextureManager | 1.4 | scenegraph | callers scenegraph |
| cTSPersistDBSegmentWithRollbackT | 1.4 | persist-dbpf | class filed persist-dbpf |
| cNodeCloneContext (anonymous) | 1.1 | scenegraph | called only from scenegraph |
| cTSEffectCheat, cCatalogTextIndicesCommand, cOutputVertexFormatCommand, cLODCommand | 2.6 | command-parser | command classes |

### Class leftovers for campaigns without a doc yet

From the backlog triage (2026-11-03), by class: CAS, audio, UI, custom content, services and misc class leftovers whose campaigns
have no `docs/subsystems/` page yet. Facts only; when a campaign opens, move its part into that campaign's doc.

- **CAS:** cTSCASBodyPartSystem (JA): open slots 3, 5, 6, 7, 12, 13, 14 (face 0), 25-28, 31, 40-42, 48, 49, 63, 66, 71,
  76-78, 82, 84-88, vf4_3/QI thunks; shared slots 57/68/69/73/75/79 only through the standalone shape (ctor 0xcddf40 from
  creator 0xcebc20, dtor 0xcdfb50); notes/feedback/JA.md (slot 76 stride ORIGINAL-BUG, slots 6/7 per-age maps at +0x38,
  slot 71 SetAge); IU rest: ctors/dtors (0xcddf40, 0xb72310, 0x4ff850), map insert 0xcf7700 (2 KB), record add 0xce0b30,
  cTSCASSystem OnTick 0x506720. cTSCASSystem: 42 open slots (24.9 KB); forwarded slots, suggested order (JI): filters 22
  0x504910, 26 0x50a0c0, 27 0x509f60, 28 0x50a4f0, 29 0x50a790, 30 0x50a000, 31 0x50a840 (GatherByType 0x509b90 /
  GatherFromBin 0x50a1d0); then 21 0x500d30, 19 0x501dd0, 33 0x505de0, 18 0x506a10; then 5 0x5011d0, 6 0x500e50, 9
  0x502490, 10 0x5009b0, 14 0x50a9f0 (2.5 KB), 20 0x506ee0, 23 0x5039b0, 25 0x50a590, 32 0x502e40, 34 0x503580, 35
  0x505fb0, 36 0x501730, 38 0x504230, 39 0x5028d0; job steppers 0x507600/0x5083b0/0x508c60, Enter/LeaveCASMode
  0x50b960/0x50b830. Seed seam: Init (ours since IU) seeds cRZRandom from our clock(); host/control_seed.cpp still lists
  image call site 0x5049ef: route Init through the seed seam. Fragment property interface (JT): BQ's tentative
  FamilyPart/FamilyPartProperties facts name slots 19-22 CategoryMask/SecondMask/Flags/KindFlags; CAS reads them as
  ages/genders/styles/categories (iid 0x8c1571d6): settle with the cTSFamily 0x49e6f0 owner. cTSBinCatalog (JG)
  original_calls: EnsureLoaded 0xb88e10, LoadSource 0xb8d6b0, 0xb8f3d0 (calls our ClearIndex 0xb89320, no entry yet),
  0xb90b80, 0xb8cbe0, 0xb8a8d0, 0xb8adc0, index keepers 0xb87a40/0xb90150/0xb87eb0/0xb90210/0xb8f420/0xb903a0, gatherers
  0xb8ba90/0xb8b4a0/0xb8b660/0xb8b6c0/0xb8bf50/0xb8aba0, neighbour finders 0xb8b340/0xb8b770, 0xb87f70, 0xb8feb0,
  cTSSimFragment ctor 0xb86ea0, cTSBinTypeFilter ctor 0xb644a0; bin info factory still on the game's vtables
  0xe8af20/0xe8af40; shared Shutdown 0xb82190 (skin + bin catalogs, both ours: one-family hook candidate); skin catalog
  (JF): EnsureLoaded 0xb74910 (5.3 KB), LoadSource 0xb7b2d0, source helpers 0xb79f50/0xb81820/0xb7ae10, ten insert
  helpers 0xb742b0.., 0xb8feb0; skin-filter members 0x267350, 0xb65750, 0xb65550 (JA's system calls them).
- **cTSCustomContentSupport (JB; ~27 KB):** slots still forwarding on our object: 4 0x39eb80, 8 0x39f680, 11 0x39cf70, 12
  0x39d400 ImportDownloads (4.8 KB, ~13 file-system callees), 13 0x391c20, 14 0x3923f0, 16 0x39a2f0, 22 0x390400, 24
  0x397730, 28 0x396070, 29 0x398240; helpers 0x391350, 0x39bf30, 0x397490; src/rz/COMDirectorData.cpp:165 registers the
  factory by hex 0x3989c0 (and 0x197740 at :49, frustum family now ours; 0x229c42 / 0x92f5ed rows could point at our
  Create functions): switch to ours. cGZDBSegmentPackedFile::Compact 0x9a83d0 (1.6 KB, IP): needs a file-system test
  world or a contract.
- **Audio:** (IY) cTSAudioAmbienceSystem (creates cTSAudioAmbienceLayer at 0x224fa2 in 0x224ce3), the layer (seven
  0x243a7f expression objects, primitive registry 0x236634, scene cache, worker 0x220c69, loader 0x23c5ae), then
  cTSAudioMusicManager, the Listener, cGZSndEaRenderer last; the sound manager has 86 original_calls into these.
  cTSAudioTSObjectUpdateDemon (LD): placement 0x22a5ad, step 0x2299b7 + line of sight 0x2296d2, sound update 0x22a119,
  helpers 0x229e85/0x2297af/0x229d15/0x229f1d/0x22ab53 (~3.1 KB); then drop the SetMonitor/Seconds hooks; also its
  .size.cpp still calls 0x18e720/0x18e880/0x18e860 only in comments (check: the OriginalFunction calls are gone). Parser
  originals still called in cTSAudioListener.size.cpp:91, cTSAudioSoundManager.cpp:1396, cGZPropertySetSerializerDelegates.h:717
  (switch to RZTextParse; LS). cTSAudioSoundManager.cpp:348 still calls 0x1a98e0 (check).
- **UI:** cGZWinListBox (~10.4 KB, 102 fn, slot table cIGZWinListBox in fx_slots.h) and cTSWinProcNeighborhoodMainMenu
  (~6 KB): one lane (UD). db/status/UIWinProcs.jsonl: 0x936a5c named cGZWinGrid::vf13 is the button's IsStyleSet body
  shared with the grid (UD; rename; one-family rule before any hook change). winfx caption slot (cGZWinText text at
  +0x64, setter unknown), cIGZWinMgr slot names (UA). cTSMainToolbar (JE): slots forwarding on our object 25 0x621006
  (1,394 B), 27 0x620c7e, 38 0x61b36c, 82 0x61fed5, 76 0x61fc26, 37 0x61b1d5, 29 0x621ba2 (+ member 0x6206fc), DoMessage
  0x61b99c (700 B), DoMessage2 0x61bc58 (8,774 B); panel members 0x61b843, 0x620643, 0x61b61e, 0x61b75f.
  cGZWinIMEServerW32 (KN): calls imm32 and DefWindowProcA directly, the harness traps every import: decide the seam (a
  function-pointer table tests point at fakes, the host at real imports) first; test fixture's 80 fake slots too few for
  cIGZWin. cGZWinMgrW32 original_calls (KQ): root cGZWin ctor 0x92fded, cursor manager ctor 0x9d0158, stopwatch restart
  0x1c95ec, notifier/message-server getters 0x9c5ab4/0x9c5a89, cRZMessage2Standard 0x1c033d, cGZWinMsgBox ctor 0x952874,
  cRZVariant ctor/dtor 0x9c7c75/0x9c7d0c.
- **Services and commands:** cTSGameTipService (KT; ~3.5 KB): service factory 0xcb5ce0 not in missed-functions, ctor
  0xcb59c0, id 0x5181421b; cTSGameTipResourceFactory vtable 0xeb76ac; cTSGameTip ~27 fns. cTSTradeableRewardsService
  (KZ, LG; ~3.3 KB + more): key map XML 0xcb8730, rewards XML 0xcb81c0 (ParseRewards), bin unlocking 0xcb84c0, reward
  coder 0xd09870/0xd09af0/0xd09cd0 (encoder scrambler 0xd09e80 is SSE), cTSTradeableRewardsLoader (ctor 0xd08c30, vtable
  0xebad1c, handlers 0xd09020/0xd09140/0xd09300/0xd09370/0xd09420/0xd096d0, helpers 0xd08340/0xd08490/0xd088d0/0xd08a10/
  0xd08cb0/0xd08d20/0xd08ea0); open question: in the harness no encoder-made code passes the decoder's checksum: check in
  game whether a shown reward code redeems. cTSCommonSystem (LC): cIsDayFunction/cIsLightMaterialFunction and the five
  effects constraint classes (vtables 0xeb7bb0..0xeb7d54; bodies 0xcb9640, 0xcb97a0, 0xcb9b60, 0xcb9c70, 0xcb9ab0); check
  the expression registry (scene graph system 0xe5074b6b slot 11) slot 5 Unregister compares names by content?
  src/gz/ExpressionFunctionBase.h (LF) overlaps the raw nGZCommandParser::cExpressionFunction in gz/CommandParser.h:
  merge. cITSPersistSystem (JR): slot 7 GetResourceSpeculative callers test only the low byte (fact ret probably u8);
  QueueResourceLoad/GetResource take const Key16 * but callers pass cGZPersistResourceKey: retype (full suite);
  src/cas/cTSCASSystem.cpp and CASFragmentFilters.cpp: delete the local V helper, use RawSlot, name calls with one
  method fact (notes/feedback/JR.md: 25 + 17). cGZClipboardManager: slots other than AddRef/Release are raw calls of
  unknown meaning (KD). Small math callees (TP): 0x19c1c0 (99 B), 0x19c800 RigidRotateVector (19 B, tentative) with
  0x19c0e0/0x1a6410/0x1a7200 (tentative matrix helpers): a math cluster lane.
- **Misc:** 0x5ba211 colour-format bits helper (56 callers) for a shared-utility lane; ClassesRound4.h glyph fields
  misnamed (+0 dest rect, +0x10 bitmap ptr, +0x14 pitch); GetCountryCode 0xc71342 callers_handled 0xc7132e is a jump-table
  entry of 0xc711c2 (LS); cGZMessageQueue/2 ctor/dtor 0xc74c32/0xc74cea, 0xc66b0d/0xc66c91 (vtables 0xea03d4 30 slots,
  0xe9e588 25 slots) still original (KU).

### Open questions

1. cTSDataTables and cTSConfigMaterialSettings to sim-core, cTSAttractionManager to sim-person: plausible from names and
   the persist registration; confirm in those campaigns' structure passes.
2. The build-catalog product family: objects (catalogues) or world-lot (the walls, fences and roofs that use them).
3. The tools: `queue --whole` and `deps --by group` inherit both misfilings (the class rule's offset-0 member, owner
   inference across a value type's users); the campaign figures in docs/roadmap.md move by about 70 KB once fixed.
4. Gimex (band 0x87b412-0x89e47a, 112.5 KB in 391 status rows): third-party library or EA code. Until decided its groups are `held`
   as third-party guesses (200 rows), so the headline does not move on a reclassification. Decide, then drop `held`.

## Confidence and limits

- Class-to-subsystem rules are name heuristics. `other-ts` (under 1 KB since lane XJ) holds classes no rule placed; check
  `--classes-unmatched` for those without a vftable-bearing rule. Judgement calls: Edith (object-definition
  system) is grouped with `objects`, not the VM; `cTSLightingManager` is in scenegraph/rendering by name only.
- Vtable slot sharing: a function used by classes of two subsystems takes the majority; many are base-class
  virtuals, so framework-gz absorbs some functions that really serve a derived subsystem.
- Propagation errs on the side of the caller: a utility called from one subsystem is placed there even if it
  is generic (`called-only-from` is 3.8 MB). So the map files propagated placements under `other` with the
  `"guess"` kept (Method); evidence entries (vtable, ctor, third-party, string, static-init, status, and thunks and data-refs
  that follow evidence) keep their subsystem. `other-ts` is a weak label. Generic class code still follows its class
  (a container template's slots filed with its first user's namespace).
- Indirect calls (function pointers, vtable calls from non-RTTI objects) are invisible to the call graph, so
  6,469 leftover functions have no connection at all; a runtime call trace (`coverage.py`'s owner inference) would
  settle many.
- The data-reference rule uses only globals referenced by 12 or fewer functions; it adds 0.20 MB (0.08 MB following
  evidence).
- `status-group` also places names whose qualifier is an EA class unknown to the map (350 functions, 89 KB at FT,
  such as `nGZSceneGraph::cShapeRefNodeImpl::*` in `cShapeRefNode.jsonl`); a lane's group is its judgement, not a fact
  about the class. STL / std instantiations are left out (61 functions, 21 KB at FT: 42 by qualifier, 19 bare names
  described as `std::` instantiations); since WH the single-user ones follow their callers (`stl-single-user`).
- The group rules (`group-list`, `group-majority`) are a group's judgement too: a "mixed" list entry keeps a batch
  group's members per function, and the list's reasons say why each entry was chosen. `stl-single-user` and the
  majority follow evidence only, never a guess.
- The third-party ranges in `db/third-party.jsonl` are partly approximate (libjpeg/png set A is marked
  "extent unfinished"), so some bytes labelled game code near 0x7d000..0x90000 may be library code.
