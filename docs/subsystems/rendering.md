# Rendering and the scenegraph (subsystem notes)

The campaign after the foundations (docs/roadmap.md, "Campaign order"). Working notes for lanes in it: scope, the plan,
conventions and test patterns, what's done and left. Keep it short and current; the lane that changes the subsystem
updates this file on its branch (facts, not decompiler output). Map: `docs/subsystems.md` rows "rendering-gfx" and
"scenegraph". Verification: `verified` by difftest (this is not simulation code); in game, the device call log of the
ui-compare scenario (docs/ui-testing.md, "Coordinator: the in-game comparison") is the acceptance check.

## Why this subsystem

It is the largest block of EA code that runs and isn't ours: 1,174 KB of the 3,257 KB that ran and is not done in
the reference session (run-1023: scenegraph 848 KB, rendering-gfx 326 KB, by class or ~owner group). By dependency it
comes early (`coverage.py deps --by group`): the scenegraph calls 39 KB of not-done rendering code and rendering calls
12 KB of the scenegraph; both call little of the later campaigns (world-lot, animation) and are called by most of them.
Below it is one hard test boundary, the Direct3D 9 device, which this plan builds once, first.

## Scope

Executed not-done code, grouped (`tools/py -I tools/coverage.py queue local/coverage/run-1023.txt --subsystem
scenegraph,rendering-gfx --whole --top 0`; add `--functions` for the function lists): 530 groups, 5,194 functions.

| family | KB | main groups (KB) |
|---|---:|---|
| lighting and shadows (nTSSG) | 196 | cLightingManager 94 (77 of it owner-inferred), cDiffusionManager 49, cLightMapManager 16, cShadow 16, cShadowManager 13 |
| SG nodes and display list | 145 | cDisplayList 29, cShape 22, cShapeRefNodeT 15, the node templates (cViewerRefNodeT, cCompositionTreeNodeT, ...) ~30, cLooseOctreePartitionNode 8 |
| other nTSSG | 116 | cTSSGSystem 17 (the class 95% done), cDisplayListDynamicRenderStrategy 22, cVideoBroadcastManager 15, cTSWinSceneGraph 11, cSceneManager 7 |
| G4 device and DX resources | 115 | cDeviceDX 25, cDeviceStateDX 23, cImageManipDX 13, cShaderProgramsManagerDX 9, cShaderProgramInputsDX 8, cDeviceEnumDX 5, cGraphicSystemDX 4 |
| SG materials, shaders, textures | 114 | cMaterialManager 17, cApplyShaderProgramConstants 12, cMaterialDefinition 10, cAttributeFFTextureMatrix 9, cMaterialParser 9, composited-texture strategies 12 |
| terrain and lot geometry | 86 | cLotSkirt 26, cLotImposterDisplay 21, cMergedGeometryNode 9, cSeasonManager 10, roofs 7 |
| SG geometry builders | 84 | cGeometryDataContainer 25, cGeometryNode 25, cIndexedMeshBuilder 21, cBoundingVolumeBuilder 5 |
| GZDraw and fonts | 82 | cGZFont 50, cGZDrawContextRenderToHardware 9, cGZFontSys 7, cGZDrawContext 5 |
| effects and particles | 78 | cTSParticlesEffect 32, cTSEffectsManager 14 (47% done), cTSParticlesGeometry 10, cTSVisualEffect 9 |
| camera | 69 | cTSCameraSmoothSims1Controller 21, cTSCameraControllerManager 9, other controllers |
| G4 other | 26 | cDraw2D 19, cGZCanvas, cGZGraphicSystem |

Already ours (verified): rendering-gfx 49 KB of its 641 KB in db/, scenegraph 64 KB of 1,557 KB. Mostly cTSSGSystem (46
KB, src/sg/cTSSGSystem*.cpp), the effects resources and collections (cTSEffectsManager, cTSEffectsResource,
cTSEffectsCollection, cTSParticlesDescription), the pixel formats (src/gz/PixelFormat*.cpp), the render states
(src/gz/RenderStates.cpp), and parts of cImageData, cGeometryDataContainer, cLightingManager, cShadowManager,
cDisplayListDynamicRenderStrategy, cSceneManager (src/sg/). The effect, lighting, material and particle command
parsers are in src/gz/*Commands.cpp.

## Subsystem map (lane LK, 2026-10-23)

Layers, from the call edges of the code that ran (callers above callees):

- **TSSG** (game scene): cTSWinSceneGraph, cTSSGSystem, cSceneManager; lighting (cLightingManager -> cDiffusionManager,
  cLightMapManager, cImageData), shadows (cShadowManager, cShadow), lot geometry (cLotImposterDisplay ->
  cMergedGeometryNode -> cGeometryNode; cLotSkirt -> cIndexedMeshBuilder), the camera controllers (a cluster around
  cTSCameraControllerManager), effects (cTSEffectsManager -> cTSVisualEffect -> cTSParticlesEffect ->
  cTSParticlesGeometry -> cDisplayList).
- **Scenegraph core** (nGZSceneGraph): shapes and node templates (cShape, cShapeRefNodeT, cViewerRefNodeT) ->
  cDisplayList, cLooseOctreePartitionNode, cMaterialManager -> cShader -> the device state; cMaterialManager ->
  cMaterialParser and the static vertex and index arrays; cGeometryNode -> cIndexedMeshBuilder, cCompactorBuilder,
  cGeometryDataContainer, cVertexFormatsManager; resources read through cGZPersistBinaryReader.
- **2D drawing**: cGZFont -> cGZDrawFontRenderer -> cGZDrawContext -> cGZDrawContextRenderToHardware / ToBuffer ->
  nGZGraphic4::cDraw2D -> cDeviceStateDX / cDeviceDX.
- **Graphic4 device layer** (nGZGraphic4 *DX classes): cGraphicSystemDX -> cDeviceDX <-> cDeviceStateDX; resource
  managers and resources (textures, cube textures, surfaces, vertex/index arrays, vertex declarations, shader
  programs and inputs), cDeviceEnumDX, cImageManipDX.
- **Direct3D 9.** The only import is `Direct3DCreate9`; every device call is a COM call (`this` on the stack, stdcall)
  through the IDirect3DDevice9 vtable, so the call graph doesn't see it. Each call is followed by an HRESULT check
  through LogD3DError (0x18def0, ours: src/rz/MiscHelpersA.cpp) with the reporting .cpp's path and line: its 139
  callers are the device boundary. 128 of them are nGZGraphic4 *DX classes (cDeviceDX 12 functions, 17.9 KB;
  cDeviceStateDX 23, 9.2 KB; the resource classes; cDeviceEnumDX; cImageManipDX; cGraphicSystemDX); the others are
  cDraw2D (4), cTextureFilterHintCommand (2), cGeometryNode helpers (2), cMaterialManager (1),
  cFloorThicknessGeometryNode (1). D3DX is linked in statically (third-party, 38 KB ran).

Layouts found (tentative, Ghidra lane LK):
- cDeviceDX (RTTI 0x10978c8, vftable 0xd9e478, 50 slots): +0x38 IDirect3DDevice9*, +0x44 cDeviceStateDX*, +0x6c scene
  depth. Slot 29 BeginScene (0x167850), 30 EndScene (0x169660), 48 returns the device; slots 19/20 return the state.
  Its LogD3DError reports pass the string 0xd9e540 (its source path).
- cDeviceStateDX (about 0x75b0 bytes): +0x10 the device; +0x16 render-target count; dirty flags +0x74 (targets and
  depth surface), +0x75 (viewport), +0x76/+0x78/+0x7a (vertex shader constant F/I/B counts, u16) with start indices at
  +0x80/+0x84/+0x88, +0x7c/+0x7d/+0x7e (pixel shader constant counts, u8) with starts +0x8c/+0x90/+0x94, +0x7f (scissor);
  render targets wanted at +0xadc (4) and sent at +0x7570 (4), depth surface wanted +0xaec / sent +0x7580; surface size
  +0xaf0/+0xaf4 and viewport/scissor fractions (floats) +0xaf8..+0xb20; constants: vertex F at +0x1230 (16 bytes
  each), I at +0x5230, B at +0x6230 (4 each), pixel F at +0x6630, I at +0x7430, B at +0x7530.
  0x17cdf0 FlushPending(groups): 8 = targets, depth surface, scissor and render state 0xae; 0x10 = viewport; 4 =
  shader constants. 0x17ec50 sets D3DRS_CLIPPING (0x88) through the cache at +0xb24.

## Before this campaign (prerequisites)

What the campaign's code calls in other subsystems' not-done code (`coverage.py deps RUN --pair scenegraph,X` and
`--pair rendering-gfx,X`, by subsystem):
- **framework-gz**: cRZString members (3.6 KB), cGZBuffer and cGZTransientBuffer (1.3 KB), nGZEvent::cConnectionManager
  (1 KB), cRZIniFile (0.8 KB), cGZRESIDtoExtensionMap; and the behaviour-type templates (cAbstractBehaviorTypeT) that
  every node and the canvas derive from.
- **persist-dbpf**: cGZPersistBinaryReader (2.1 KB not done; its group is 18% done) and Writer (1.2 KB).
- **shared-utility and unassigned**: mostly helpers of this campaign's own classes filed elsewhere (owners
  ~nTSSG::cAnimatable, ~cGeometryNode, ~cShape, ~cLightingManager); `--whole` puts them in the lanes below.
- **other-ts**: the effect classes cTSMetaParticlesEffect, cTSDecalEffect, cTSModelEffect, cTSSequenceEffect,
  cITSVisualEffect (11 KB), called only from cTSVisualEffect and cTSEffectsManager: lanes E1-E10 below.
- **animation** (6 KB: cChoreoQueue, cAnimatable) and **world-lot** (33 KB: terrain, roofs, floors, fences) are later
  campaigns: their callees stay original-calls in this campaign's tests (the cycle's cut).
If the foundations campaign (docs/roadmap.md) has not run first, lane P0 takes the framework-gz and persist items above.
State 2026-11-03 (lane VY): the framework-gz items are done (F1-F4) apart from the behaviour bases' instances and
cConnectionManager's rest (foundations lane FD) and cGZTransientBuffer's rest (FC); P0 is no longer a lane.

## Next lanes after the re-filing (lane XO, 2026-11-06)

The re-filing (lane XJ) took rendering-gfx from 543 to 913 KB filed. This section is the current list; the audit below
it is kept for its history, recipes and class notes. Its family table and its E8 row are out of date: cTSMetaParticlesEffect
has 3 functions to do, all folded and unmapped (`tools/classinfo.py cTSMetaParticlesEffect --todo`). Figures are from
db/ at 7ee40161, before lane XI's library rows: those can only shrink the unreached and tail figures.

| rendering-gfx | KB |
|---|---:|
| filed, done | 587.7 |
| filed, the pixel band (absorbed by NW's template, lane XP: below) | 245.2 |
| filed, to do | 58.8 |
| guessed here, to do (these are `other`, not rendering's yet) | 29.7 |
| guessed here, done | 0.5 |

To do is 88.5 KB (646 functions, library matches left out). Split by `tools/callers.py --binary <current host>` over
each row, then repeated: a row is **unreached** when every reference is ours or redirected, or comes from a row already
unreached, and no status entry lists it in `original_calls`.

| | KB | functions |
|---|---:|---:|
| unreached by anything original | 43.3 | 328 |
| named by our code as an original | 7.3 | 22 |
| reached by original code | 37.8 | 296 |

### The pixel band: absorbed, adopted (lane XP, 2026-11-06)

The 252 KB of leaf code at 0x8c8000-0x90c000 is GZ's own software texture span routines, not a library and not a D3D
fallback: no string references, no library matches, and the only callee is memset (from 3 of them). 909 functions,
245.2 KB; the rest of the band is the verified cGZDrawContextRenderToBuffer and span renderer classes, then cGZFont from
0x909f57. There are five two-level tables, one per destination format: 14 row pointers (index format * 2 + key; rows 12
and 13 share a row) over rows of 15 modes (0..4 plain, +5 scaled, +10 tiled; copy, blend, modulate, modulate+blend,
premultiplied).

| table | destination | functions | KB by table |
|---|---|---:|---:|
| 0xe4f580 | 4444 | 195 | 60.2 |
| 0xe4f948 | 1555 | 195 | 59.6 |
| 0xe4fd08 | 565 | 171 | 49.6 |
| 0xe500b8 | 8888 | 189 | 44.9 |
| 0xe50418 | 8 | 165 | 31.4 |

The tables hold 975 slot words over 909 distinct bodies: some bodies serve several slots, three of them across destinations.

**Lane NW already replaced it** (R5b step 1 below): one template per destination, tests/gz_span_texture.cpp comparing
every cell (5 renderers x 7 formats x 2 keys x 15 modes) against the original's slot, 25 mutants. The band's behaviour is
under test at slot level, so **no per-function test, no Sonnet lane and no generator is needed**: the routines are pure
functions and would be cheap to difftest, but there is nothing left to write.

Reference proof, from the image's relocations and a scan of rel32 targets, recounted by lane XP with the same result:
relocated references to the 909 outside the tables, 0; into their bodies, 0; rel32 instructions from outside, 0 (84 byte
patterns that decode as a rel32 at a body are operand bytes of other instructions, none an instruction start; rel8 from
outside, 0). References into the tables, 75: the 70 row pointers and one read of each table base, from the five vf5 slots (0x8c9c78, 0x8d9948, 0x8e9248, 0x8f6128, 0x901bb8). Each vf5 is `verified`,
`in_effect: callers`, trap `Hooks.cGZDrawSystem_Create=1`, and callers.py finds all its references covered; the five span
renderer vtables are named only by cGZDrawContextRenderToBuffer::SetTarget 0x8c7ee0, which is ours. So by the roadmap's
rule the band's only references are from code no longer reached, resting on that one hook. Not "unused": the tables are
selected by data, and 7 of the 909 ran in the reference runs of the original (1.6 KB, destination 8888, format index 4,
called from 0x8c8a4c); 0 ran in lot10-ours.

**Adopted:** the five tables are declared in db/data-tables.jsonl and callers.py covers their slots (the declared-table
rule, docs/verification.md, "In effect through callers, and traps"). The 909 are in db/status/nGZDrawSoftSpans.jsonl,
`reimplemented` with `absorbed_by` naming their readers and tests/gz_span_texture.cpp as the test, trap
`Hooks.cGZDrawSystem_Create=1`. They count as done for this subsystem, outside verified bytes (a line of their own in
tools/status.py). They are armed like any callers entry: 909 more traps (7115 to 8024), about 50 ms at start.

### The 43.3 KB unreached

Not called dead here: dead is a proof per function (roadmap), and this is the candidate list for it. Mostly STL shapes our
code no longer reaches (the 0xbd0000 page 12.7 KB, lane XC's; the 0x170000 page 5.6 KB, most of the Shader* sorts among
them, so RL2's open 9.3 KB needs no lane for what is unreached) and stubs that only replaced static initializers and
funclets named (the 0xd80000 page 5.1 KB). 18 functions, 2.9 KB, have no reference at all, which is weaker: a computed
address would look the same.
Close these with the closure lists, after lane XI's rows, by rerunning callers.py against a current host build.

Hand check: 14 rows drawn at random from the 190 of 40 B and over, their reference lists read, bodies not. 12 hold up (each
reference is from a hooked or replaced caller); 2 have no references, which the check can't confirm. callers.py's limits
apply: computed addresses, jumps into the middle of a function.

### Where the 45.1 KB still live goes

| KB | what | goes to |
|---:|---|---|
| 15.0 | the effects' last helpers and the overlay's rendering-filed rows | lane RX |
| 7.1 | the colliders and constraints | lane RY, with their registrar (0.5, not filed here) |
| 6.4 | cTSEffectCheat, cTSEffectsManagerCheat: their bodies, the two cheat constructors 0xbc46d0 / 0xbc4fb0, their maker 0xbc5aa0 and the SciTE launcher 0xbc9c00 with 0x1b14c5 / 0x1b184b / 0x1b18c4 | command-parser, with cOverlayCheat |
| 0.6 | the cConvexT shape instances | compgeom |
| 0.4 | cViewportBrush (vtable 0xdd9574, made at 0x5c23ac / 0x5c244e from cTSWinPersonHead) | ui-game, with the head window |
| 2.7 | 109 small rows: cRZMemPtrIterator, cVectorEvent, behaviour type stubs, `shared_vf_` bodies (1.0 KB ran) | deferred: the last pass over this campaign, most stay unhooked |
| 12.8 | guesses that are not rendering's: left as `other` | their callers' campaigns |

Worth naming among the guesses: a URL splitter 0x993755-0x994495 (3.4 KB; host, scheme, 'localhost'; called from 0x992d46, its
neighbours are network-web), 0x21253b (2.3 KB, from 0x211dce), 0x1cd4c1 ('TSWebService'), 0x272210 (animation), 0xb6fb30,
persist helpers 0x1c116e / 0x1c3505. They came here on `calls-into`, `data-ref` and `flanks`: no lane should take them as
rendering.

### The lanes

| lane | groups | KB | needs | risk | model |
|---|---|---:|---|---|---|
| RX | the manager's pending walk 0xbcb4c0 2.3 KB with 0xbc30e0, 0xbc41e0, 0xbc4540, 0xbc5380, 0xbc57a0, 0xbc6050, 0xbcb260; 0xbe9670, 0xbe8de0 (4.9 KB, 4.4 ran). cTSEffectsResource's record readers and writers 0xbf9620..0xbf9ee0 with 0x1a5910 / 0x1a5950 (2.4). The curve's 0xbdc6f0, 0xbdcdb0, 0xbdc760 (0.5). Extension list 0xcc4db0, 0xcc4e30, 0xcc5280 (0.6). Model effect's 0xc05be0 and 0xaef310 (0.9). Description and command helpers 0xbe0b90, 0xbe1120, 0xbe1290, 0xbf32b0, 0xbf3730 (1.5). The STL-shaped tail 0xbcc810..0xbd94c0, 0xbdd800, 0xbe89c0 (2.2: try stlprobe first). The overlay cluster whole: cTSOverlayManager, cTSDecal, cTSOverlay, 0xbd9da0-0xbdc6e0 (9.9, 8.6 ran), with cTSTestMap (0.6). **RX done:** everything above but the hand-backs, verified. The overlay is one header, src/ts/cTSOverlayManager.h: pools 0x107d9a8 (cTSOverlay, 0xd4 bytes, blocks of 0x6a00) and 0x107d9b8 (cTSDecal, 0x108, blocks of 0x8400); vtables manager 0xe8e6c0 / 700 / 714, overlay 0xe8e5e0 / 624, decal 0xe8e640 / 660 / 6a4, test map 0xe8e4e0 / 504; interfaces 0xac22fa89 (manager), 0x2c2a7106 (test map); classes 0xac22fa8d, 0x2c2a7110. The two factories 0xbdaf50, 0xbd9700 are the only hooks: with Init ours, every object carries our vtables. Update clears the manager's changed byte only when it filled a table. SetRecordActive 0x3afe70 stays an original (shared with nTSSG::cLotSkirtOverlayGeometry vf11). **Handed back:** cOverlayCheat (0xbd9da0, 0xbda350, 0xbdade0, 0xbdb320, 0xbdb700): it stands on cCheatCommand's constructor 0xaf88a0 and destructor 0x2b3ef0 and the option-spec parser, and goes with the two effect cheats (command-parser row); 0xbe0b90, 0xbe1290 are library rows (lane XI) and 0xbe1120, 0xbf32b0, 0xbf3730 STL expansions of the same shape, with no XI row yet (vector growth and copies of the attractor, collider and sequence records), so they go with their callers; 0xcc4e30, 0xcc5280, 0xcc4db0, 0xcc6c60, 0xcc6a80 are library, the registry is a std::unordered_map of ours (no game key copy left: 0x4f5a10, 0x257860 are not called from it); toolchain instances left alone: 0xbd9b80, 0xbda0d0, 0xbdc480, 0xbdc520 (the record vector and its unwinding), no library row yet; 0xbf9620, 0xbf9800 are library rows, their status entries went; the tail after XI: 74 rows library, 118 owned, 50 (15 KB) left, among them the anonymous effect commands (cEffectsResourceCommand, cBrushIDCommand, cEffectGroupIDCommand, cEffectGroupIDInstanceCommand, cGroupEffectCommand, 27 rows in 0xbcd780-0xbd0630), which are command-parser's and not STL | 23.5 | nothing not done of size: cOverlayCheat's option-spec parser 0xaf90e0 stays an original | medium: 0xbdb750, the decal's projection, is 1.9 KB of x87; the overlay is new structure | Opus for the overlay; Sonnet medium would do the rest (tests/ts_effects_manager.cpp, EffectsManager_Lifecycle's fixture) |
| RY | WU-1 (world-lot.md) with the world-space colliders: cFloorsCollider, cWallsCollider, cWallsAndFloorsCollider and the three pin constraints in ?A0x65722b7a, 0x3a2ac3-0x3a3c00, (4.0) with their registrar 0x3a3c30 (0.5, filed `other`; cTSSGSystem::CreateManagers calls it as an original); cTSPlaneCollider 0xbdea30, 0xbded70, 0xbdf420 (3.1) | 7.6 + 16.2 | WU-1's tile predicates: the colliders call 1.7 KB of them (0x7ddc40, 0x7ddc50, 0x7ddd60, 0x7ddd90) | low: the three slots repeat cTSCommonSystem's verified copies in ?A0x4c801c06 (0xcb9640, 0xcb97a0, 0xcb9ab0..0xcb9c70, tests/ts_common_system_objects.cpp); none ran in the reference runs, so difftest only | Sonnet medium | **Status (2026-11-06): WU-1 part 1 done (tile maths 0x7dcb90-0x7dde80, catalog accessors 0x7ef640-0x7ef9d0, 63 functions, verified, shared fixture tests/worldfx.h); the predicates, the colliders, the registrar and cTSPlaneCollider rows were not reached and are unchanged.**

Total 31.1 KB (23.5 + 7.6) besides WU-1, of which 9.0 KB is filed under scenegraph, sim-core and `other`. Rendering's
to-do figure drops by 15.0 + 7.1 = 22.1 KB; the rest is the overlay cluster's and the registrar's other filing. Why not smaller lanes:
RX's two halves are 13.0 and 10.5 KB and RY's colliders 7.6, each under the 20 KB floor. If the coordinator would rather
run WU-1 alone, the colliders go as a short lane after it.

The cluster stays filed as it is (docs/subsystems.md gives cTSOverlayManager to scenegraph on its callers, 5.0 KB), but
cTSEffectsManager makes it (0xbdb700 in the constructor, 0xbd9fd0 in Init), so those two are original calls until
it is ours, and a class split across three subsystems should go to one lane. If scenegraph takes it instead, RX is 13.0 KB
and joins another lane.

After RX and RY, of the 22 originals ours names only these stay: the cheats' maker 0xbc5aa0, the folded 0x148310 and seven
guesses (0x158da0, 0x19d340, 0x1c116e, 0x1c3505, 0x1c99d2, 0x9cea90, 0xae7d00; 2.0 KB). Rendering's closure then rests on the 909's adoption, the unreached list and the hand-offs above.

## Next lanes (lane VY's closure audit, 2026-11-03)

Lane LK's plan (2026-10-23, R rows refreshed by lane PO) is in `docs/archive/rendering-lane-plan-LK.md`: its R rows are
done apart from what is listed here, its S / T / C rows are the scenegraph campaign's (`docs/subsystems/scenegraph.md`)
and its E rows are replaced by the effects lanes below. Method as foundations.md's "Next lanes": every function filed
under rendering-gfx, plus the effects classes filed under other-ts (roadmap: "effects classes filed under other-ts
belong to rendering"), not `verified` or `replaced`, grouped by class and placed in the majority subsystem. 165 KB is
filed under rendering-gfx (111 KB executed), and about 58 KB of effects under other-ts.

### What is left, by family (todo bytes / executed todo bytes)

| family | groups (todo / exec) | KB |
|---|---|---:|
| device leftovers and gates | **Lane WI (2026-11-04), done but for the items marked left.** Verified and hooked: 0x15e3d0 (vf12, byte match), 26 class-id stubs (the "6-byte helper stubs", group GraphicsClassIds, byte matches), 0x1773b0 HoldGraphicService (byte match), the getters 0x159390 / 0x167e00 (difftest tests/gz_graphic_event_managers.cpp), cGZBufferStub 0x18a8c0 / 0x18a8f0 / 0x18a320 (through callers); cError's strings and vtable 0xd9cd70 are ours. Misfiled to library (lane RL): 0x15acf0 / 0x15af60 / 0x15ac90 (std::sort internals), 0x17abe0 / 0x17ac80 (heap helpers); 0x1771b0 / 0x177300 / 0x17bc20 were already library matches. Dead: the behaviour base destructor 0x167330 is reached only from the unwind funclet 0xd1a180 of the hooked constructor 0x167200, so its vtable 0xd9e3ac and slot 0x169bb0 never run (0x159d50 already "never runs"). Left then, closed by lane WN (row RD): GetDraw2DCached 0x18c000 hooked, the cPixelFormatRange copy constructor 0x167180 (not library; inline in ours, in effect through callers), cLockableSurfaceDX<cIImageSurface>'s QueryInterface 0x1628e0 and thunk 0x16295d (an abstract base's slot, tested on a complete class of the test's). 0x162c55 and 0x162c75 are `shared_vf_` adjustor thunks: no entry. 0x18a400 verified on a complete class of the test's. The row before: nGZGraphic4::cPixelFormatsManagerDX 939 / 713 (0x15acf0 621 B, 0x15af60, 0x15ac90, vf12 0x15e3d0); cDeviceResourceManagerDX 622 / 464 (0x17bc20 357 B, 0x17abe0, 0x17ac80; shared_vf_17b050 4 B stays); cDeviceEnumDX 387 / 0 (0x1771b0, 0x177300, 0x1773b0); cGraphicSystemDX's 150 B of helpers and its `GameAddress<const char>` (the GetMessage strings, the cError vtable 0xd9cd70); nRZGraphic4::cGZBufferStub 130 / 65 (QueryInterface 0x18a8c0, Release 0x18a8f0, 0x18a320, 0x18a400, the vtable 0xda0c00, the unwind funclets 0xd18c2f / 0xd1b103) with the back-buffer buffer 0x1468d0; cLockableSurfaceDX<cIImageSurface> 133; nGZGraphic4's 6-byte helper stubs 397 / 386; the behaviour bases of cGraphicSystemDX, cDeviceDX and cDraw2D (~270 B) | 3.0 |
| brushes and surfaces | **Done but cViewportBrush** (re-check by lane YB, 2026-11-08: cPatternBrush, cColorBrush, cOutlineColorBrush and the cMemBlock surface classes are verified, db/status/cBrushes.jsonl and cMemBlock.jsonl). Left: cViewportBrush 435 B (vf4 0x5c2524, vf5 0x5c24bf, QI 0x5c2aae), made only by cTSWinPersonHead (0x5c23ac / 0x5c244e): take it with that UI class | 0.4 |
| draw contexts, fonts, text | **Lane WL (2026-11-04): seven library matches recorded** (probe_font.cpp, C++14, /O1 variants with /Ob1 or /GS-: 0x1af945 / 0x1af9da / 0x90a4a5 unordered_set<uint>::emplace, 0x1afac9 the _Hash constructor, 0x1c0ab6 / 0x1c3ce0 u16string _Reallocate_for / assign, 0x90a53a vector<int16_t>::emplace; 0x64a7ee and 0x90a0bb were already rows). Left, with blockers: LegacyCalculateUIScale 0x8b8390 (game code, reads the framework service 0x14323f, a property get through its slot +0x14, a std::string key through slot +0x24, FreeStringBuffer and ScopedRef release: the seams outweigh 398 B), 0x90afb0 (61 B, fills the glyph-name map 0x10ecd04 from a 495-entry table at first lookup; tied to StaticInitContainers IntIntMap), 0x8baca0 / 0x8bab40 / 0x8bae00 / 0x8c0ad0 (vector internals with an EH frame and out-of-line allocate / construct / move helpers that no probe shape reproduced), 0x90efbb / 0x90f043 / 0x90f0cb / 0x90f153 (map<int, V>::try_emplace, V non-trivially destructible of 32 and 80 bytes; Blob4 probes 0.31 similar), 0x90f21e (equal_range on a tree keyed by {int, string}, not probed). The row before: cGZDrawContextRenderToHardware 1,282 / 1,039 (LegacyCalculateUIScale 0x8b8390, 0x8baca0, 0x8bab40, 0x8bae00); nGZDrawSoftRender::cSpanRenderBase 348; cGZDrawSystem 494 / 490 (0x8c0ad0 404 B); cGZFontSys 1,288 / 952 (0x90f21e and four 135-byte siblings 0x90efbb..0x90f0cb, 0x64a7ee); cGZFont 399 (0x1af9da, 0x90a4a5, 0x90a53a); cGZFontFile 206 (0x90a0bb, 0x90afb0); RZText 458 (0x1af945, 0x1c0ab6, 0x1c3ce0, 0x1afac9) | 4.5 |
| library instances | after lanes WJ (RL), WM (RL2) and XI, see "XI result": 5.2 KB library-shaped and left, each with the reason there: the pixel Shader sorts and equal_range 0x171ff0 / 0x170070 / 0x171880 / 0x1730c0 / 0x1732e0 (3.9 KB), Elem16 Med3 0x16c540 and sort 0x16caa0 (832 B), 0xbf99f0 269, 0x90f21e 145. cTSFloatConstants' insert(pos, n, x) 0x4dfc90 800 stays `reimplemented`, unmatched. Not library: the comparators 0x1747d0 / 0x1749b0 / 0x174920, cPixelFormatBaseT x8 (destructors reached from cPixelFormatsManagerDX's unwind funclets), the cRZMemPtrIterator slots 282 (game templates), 0x194d20 (cAABBTreeBuilder::Node's allocator-service allocate, game code) | 5.2 |
| effects: descriptions, maps, transforms | cTSMetaParticlesDescription 1,962 / 1,828; cTSParticleEffectDescriptor 820 / 0 (vf12 0x4dc6e0, Write 0x4dca30); cITSVisualEffect 800 / 800 (0xbe86a0); cEMObjectQualityEffect 612 / 265; cTSBaseParticlesDescription 576 / 0 (0xbfc270, 0xbfb8b0); cTSEffectsMaps 568 / 344 (vf6 0xbd9a00, vf4 0xbd9b00); cTSLightEffect 450; EffectsTransform 243 | 6.0 |
| effects: the small effects | cTSDecalEffect 4,396 / 4,371 (vf2 0xbfd010, vf5 0xbfc830, 0xbfc3f0, vf6 0xbfcd90); cTSSequenceEffect 2,005 / 1,913 (vf5 0xc06e80); cTSWaterEffect 1,491 / 0; cTSSoundEffect 1,481 / 0 | 9.4 |
| effects: particle geometry | cTSParticlesGeometry 10,587 / 10,448 (vf8 0xbbdd50 alone 9,848 B; 0xbb4e90, 0xbc0f80, vf3 0xbb9460); cTSParticlesRenderable 1,692 / 1,558 (vf83 0xbb58e0, 0xbb54f0, 0xbb5010, vf82 0xbb65a0) | 12.3 |
| effects: particles | cTSParticlesEffect 42,407 / 32,567 (72% owner-inferred; vtable 0xe8bdbc, its own slots small, vf5 0xbb6830 6,840 B) | 42.4 |
| effects: meta-particles | cTSMetaParticlesEffect 36,018 / 29,072 (84% owner-inferred, filed under other-ts); after lane WZ 19.9 KB + 1.1 KB of bases left, row E8, E9 | 36.0 |
| effects: visual and model effects | cTSVisualEffect 8,641 / 8,040 (vf4_6 0xbe99c0 3,390 B, vf14 0xbea700, vf13 0xbe9700); cTSModelEffect 8,602 / 4,632 (vf0 0xc05cf0 2,143 B, 0xaef950, 0xaf05b0, 0xaef5b0) | 17.2 |
| effects: collection and resource | cTSEffectsCollection 12,687 / 7,257 (0xbd5dc0, 0xbd5a30, 0xbd6710, 0xbd9110, ...); cTSEffectsResource 4,298 / 2,535 (0xbd22c0, 0xbf9800, 0xbd26b0, 0xbf9620) | 17.0 |
| effects: extension and parser | cTSEffectsExtension 7,453 / 5,764 (slots 0xcc61c0, 0xcc5480, 0xcc5ef0, 0xcc5810); cTSEffectsParser 5,609 / 3,783 (0xbcf820 2,088 B, 0xbcce40, 0xbcd2e0) | 13.1 |
| effects: the manager (done, lane XG) | cTSEffectsManager 23,713 / 23,713: every slot and helper reimplemented and verified (EffectsManager_Lifecycle, 45 mutants); only the folded `shared_vf_207d70` (slot 1 and the +8 AddRef 0xbc5d10, a reference-count increment at +0x80 shared with other families) stays unhooked, as the verification rules require | 0 |

Gate references: render_object's noimage count is 2 after lane WI and lane WN (2 before each), both scenegraph's: the address 0x0
data reference from cDisplayList::Init, the cMaterialParser adapter thunk and cShapeRefNodeImpl::RebuildMaterialBlock,
and the cTextureHandleIterator vtable 0xe7c7f4 (cTextureManager's NewHandleIterator). Rendering's own references are
gone. History: lane VX's count (7 left; scenegraph.md "Milestone O"): cGraphicSystemDX's
`GameAddress<const char>` and cGZBufferStub's pieces were lane RA's; the others were foundations FA (the stopwatch
cells), FB (the cRZOFStream vtable), FD (cVectorEvent<cVector4f>), FA (the cRZStd vtable) and the scenegraph's
cTextureHandleIterator vtable 0xe7c7f4 (scenegraph.md "Open").

Owned or hooked but not verified: nGZEvent's 32 B `reimplemented` (scenegraph items 0xa606d0 / 0xa633d0, filed here:
scenegraph's); the folded slot bodies of 0xda0ca8 (not claimed: shared); cTSFloatConstants' five STL vector helpers
0x4ded40 / 0x4dedc0 / 0x4def60 / ... (lane WJ: four are library matches, entries retired; 0x4dfc90 insert(pos, n, x) is still `reimplemented`, unmatched
by the probes); cTSConstantsClient::vf2 0x4df450 (4 B, `mapped`: under 5 bytes, never hooked); the
cAABBNoLeafTree<TriangleSet> deleting destructor 0x195a40 (compgeom's).

### Filed here, owned elsewhere

| to | groups |
|---|---|
| command-parser | cTSEffectCheat 4.3 KB, cTSEffectsManagerCheat 2.6 KB (cheat commands; they call cOverlayCheat's option-spec parser, also command-parser's); the effects script commands (cMetaParticle*Command, cParticleInjectCommand, cGroupEffectCommand, cSequenceEffectCommand, ...) are filed there already |
| compgeom (docs/subsystems/compgeom.md) | nGZCompGeom::cBoundT<cITriangleSetShape> 327, the cConvexT shape instances (~1 KB) |
| scenegraph | the nGZEvent group's 0xa606d0 / 0xa633d0; cTSCameraEffect and nTSSG::cCameraEffectsSupport (camera, scenegraph's C rows) |
| ui-game / sim-person | cTSScreenEffect 4.0 KB; cTSMotiveEffects 3.9 KB |

### Lane order

The device leftovers first (their gates and the surfaces the brushes draw on), then the brushes, the draw contexts
and fonts, a library pass, then the effects bottom-up: descriptions, the leaf effects, particle geometry, the two
particle systems, the effects that hold them (visual, model), the collection and resource that load them, the parser
and extension, the manager last. Effects call little that is not done (call edges weighted by callee bytes,
run-1023): cOverlayCheat 4.7 KB and cTSEffectCheat 1.3 KB (command-parser, from the cheats only),
cRZString-built helpers 2.9 KB, cPathPlanner 2.1 KB (world-lot: original calls), the multi-threaded behaviour base
1.4 KB (foundations FD), cIndexedMeshBuilder 0.8 KB (scenegraph). The scenegraph's TSSG classes call the effects
(cLightingManager 2.3 KB, the camera 2.3 KB), so the effects come before the scenegraph's T rows.

| lane | classes | KB | mark |
|---|---|---:|---|
| RA | device leftovers and gates (the first family above) | 3.0 | Opus: gate references, unwind funclets and a data object; folded bodies to leave alone |
| RB | brushes and surfaces | 7.3 | Sonnet: brush slots over drawfx surfaces, cColorBrush's pattern repeated |
| RD | done (lane WN, 2026-11-04): what RA, RB, RC left: cPatternBrush whole, LegacyCalculateUIScale, the glyph-name fill, WI's leftovers; see "RD result" | 2.5 | Opus |
| RC | draw contexts, fonts, text | 4.5 | Sonnet: finishing classes whose other members are verified (R5, R6 conventions) |
| RL | done (lane WJ, 2026-11-04): 46 rows, 16.8 KB matched. RL2 done (lane WM, 2026-11-04): the misfiled sorts and WL's vector and map internals matched. XI done (2026-11-06): all but 5.2 KB of WJ's list and E11's STL matched, see "XI result" | (5.2) | Opus: tooling (probe sources and template arguments), as F7 / lane MJ |
| E1 | done by lane WO, see "The effects world" below; effects descriptions, maps, transforms; extends tests/ts_effects_collection.h into the effects world | 6.0 | Opus: fixes the effects world (descriptions, the effect base's layout) every later E lane uses |
| E2 | done: sound, sequence, water by lane WP, the decal effect by lane WS (see "E2 notes" below) | 9.4 | Sonnet: four classes of the same base, after E1's layout |
| E3 | particle geometry and renderable (done, lane WQ) | 12.3 | Opus: one 9.8 KB float body (vertex generation), x87 exactness, the device fakes |
| E4 | done by lane WR: structure pass and every slot but 5 of cTSParticlesEffect, see "cTSParticlesEffect" below | - | - |
| E5 | done by lane WV (2026-11-05): the particle (integrators, forces, collisions, InitParticle, position), 31 functions + the free helpers; notes below | - | - |
| E6 | done: lanes WX and XB (2026-11-05): emitters, Emit, slot 5 (ours, in effect through callers), the renderable's 0xbc1d10 and its helper 0x1a6db0 (hooked) | 18 | none: the effect has no game code left |
| E7 | done by lane WZ (2026-11-05): structure pass and every slot but 5 of cTSMetaParticlesEffect, see "cTSMetaParticlesEffect" below | - | - |
| E8 | cTSMetaParticlesEffect: the particle (integrators, surface, InitParticle): recipe in "cTSMetaParticlesEffect" below | 12.4 | Sonnet: Frame and the fakes are there; the hard one |
| E9 | done by lane XE (2026-11-05): cTSMetaParticlesEffect's bases, RotateZ, place, draw tick, three emitters and slot 5 (ours, in effect through callers); notes after Recipe E9 below. The effect has no game code left | 8.5 | none |
| E10 | cTSVisualEffect, cTSModelEffect | 17.2 | Sonnet: containers of the effects above. DONE (lane XD): cTSModelEffect and cTSVisualEffect complete: constructor hooked, every slot ours and verified, including Update 0xbe99c0, SetResource 0xbea700, the camera follow 0xbead40 (cTSEffectsManager now calls ours) and the event index walk 0xbe9590; no original_calls left |
| E11 | done: nothing left in either class (lane XC, 2026-11-05). The 17 KB were STL instantiations (15.6 KB in 0xbd1000..0xbd9400) reached only from our hooked code: matched by the library-instances pass (lane XI, "XI result"), but for 0xbf99f0 and the game code named there | - | - |
| E12 | cTSEffectsParser (done, lane WU, 71 hook rows, 15 mutants killed); cTSEffectsExtension (done, lanes WW, WY; ten event/attribute thunk rows mapped) | 13.1 | WW: synthetic tests only; extension record layout 0x58, registry at +0x20, class size 0x70. Partial implementation is not ready to count as done. |
| E13, E14 | cTSEffectsManager's 11 slots and helpers | 23.7 | done (lane XG, 2026-11-06) |

RA, RB, RC follow foundations (FA..FD at least: RA's gates sit next to FA's, the brushes use FC's containers); RL any
time. E1..E14 are a chain apart from E2 / E3 (parallel after E1) and E10 / E11 (parallel).

### Per-lane recipes

- **RA, device leftovers and gates (Opus).** Calls: the Direct3D device and IDirect3D9 (fakes), cDeviceDX /
  cDeviceStateDX (ours), the pixel-format iterator (ours). Fixture: tests/d3dfx.h / fx_d3d9.h (the fake device and
  IDirect3D9, "Shared fixture: the fake Direct3D device" below), pixel_format_fixture.h. Test: each slot on the fake
  device; cGZBufferStub's QueryInterface / Release on its own object; the gates by render_object's noimage count.
  Pitfalls: cDeviceResourceManagerDX's folded bodies stay unhooked overrides by design (R1c); the 6-byte helper stubs are
  hookable (5 bytes and over) but check `shared_vf_` first; the unwind funclets are owned as data (their table), not
  hooked; `GameAddress<const char>` strings become our own string literals (the cError vtable 0xd9cd70 goes with them).
  Learned (lane WI): the "6-byte helper stubs" are COM class-id getters (`mov eax, imm32; ret`), all byte matches;
  much of the row's byte count was STL instances (std::sort, heap helpers) that belong to lane RL; the cached
  connection-manager getters don't byte-match (the original inlines the service acquire), so they're difftested
  against a fake framework (tests/gz_graphic_event_managers.cpp, reusable for other behaviour-template getters).
- **RB, brushes and surfaces (Sonnet).** Calls: cIGZDrawSurface / cIGZBuffer faces (ours: cGZBuffer, cImageManipDX),
  cMemBlockAsSurface (in the lane). Fixture: drawfx.h (the draw faces), gz_draw2d_device_fx.h. Test: each brush's fill
  over random rectangles and clip lists, compared as the buffer's bytes; the surface classes' Lock / Unlock and copies.
  Pitfalls: "Original bugs reproduced in cGZBuffer" (ClipRects' wrapping compare, F2 conventions); cPatternBrush ran 0
  bytes in run-1023 (difftest only); the surfaces' 26 functions are mostly small (byte matches likely).
  *Result (lane WK, 2026-11-04):* src/gz/cBrushes.h/.cpp. cColorBrush and cOutlineColorBrush are ours whole (creators
  0x1538b0 / 0x1538f0 hooked, slots in effect through them; difftests Brushes_Color / Brushes_Outline). cPatternBrush's
  QueryInterface, Draw, ResetCache, SetAngle, SetOffset are hooked at their RVAs (Brushes_Pattern); left: its creator
  0x153930, SetImage 0x153a40, GetStateBlock 0x153ae0 and destructor 0x152fa0 (the bitmap vector is filled and freed by
  game code: GetStateBlock needs the director framework and image manipulator, and the heap pairing makes the class
  whole or nothing). cViewportBrush exists after all, in ?A0x1f47b951 at 0x5c23ac-0x5c2686 (ui-game's, see the section above). The surfaces and the atlas were verified by earlier lanes.
- **RD result (lane WN, 2026-11-04).** cPatternBrush is ours whole: creator 0x153930 hooked (the only maker; the vtable 0xd9c76c is named by
  creator and destructor alone), slots in effect through it. Its bitmap is std::vector<uint8_t> on std::allocator: growth is the library's
  0x152c50 with operator new 0x824632 and sized delete 0x824624, the destructor inlines the same (a block of 0x1000 or more is the aligned
  one), so RZMemory was never involved. Brushes_* now compare the blocks the last Release frees (shape::DeleteRecorder), which verifies
  the destructors 0x152f00 / 0x152f70 / 0x152fa0 too. LegacyCalculateUIScale 0x8b8390 hooked, same logic as cGZWinMgrW32::GetUIScale.
  0x90afb0 fills the map 0x10ecd04 from 479 entries (not 495), read only by GlyphIndex 0x90cf86: ours keeps the constant table, the map stays
  empty, StaticInitContainers' entry for it is left as it is. An abstract base's slot is reached by a complete class of the test's
  (LockableProbe keeps 0x1628e0; StubProbe reads the stub's vtable in its second base's constructor, for 0x18a400: the game build
  optimises that vtable away).
- **RC, draw contexts, fonts, text (Sonnet).** Calls: cGZDrawSystem / the font renderer (ours), Font Fusion (original
  library entry points, hooked by R6), MultiByteToWideChar (impfx). Fixture: drawfx.h (cIGZFontRenderer), impfx.
  Test: as R5 / R6 (drawfx call logs). Pitfalls: "Text conversion in tests" and "Dead branches on DefaultEncoding" (F
  conventions); LegacyCalculateUIScale is float (compare bits).
- **RL, library pass (Opus).** tools/stlprobe/probe.cpp gets the template instances (sort, vector growth, tree
  insert, the std::list emplace) with the element types the owners use; `libmatch` records the matches. No test.
  **RL result (lane WJ, 2026-11-04).** tools/stlprobe/probe_render.cpp (built by stlprobe.ps1 with $RenderFlagSets,
  in C++14 mode) matches 46 targets, 16,827 B, all std::allocator (operator new 0x824632 / sized delete 0x824624):
  ~cShaderProgramInputsDX 24 (11,453 B: ConstRec / SampRec sorts, heaps, insertion sorts, equal_range, swaps, the
  records' moves, the vectors' destructors), ~cDraw2D 9 (2,544 B: vector<Record> growth and destructor, the
  pair<uint32, uint32> greater<> sort), ~cVertexFormatsManager 6 (1,740 B: vector<vector<Elem16>>, the Elem16
  partition and heap), ~cShaderProgramsManagerDX 3 (301 B: the name set's lower bound, two Shader* Med3s), and
  cTSFloatConstants' 0x4ded40 / 0x4dedc0 / 0x4def60 / 0x4df000 (789 B; their `reimplemented` entries retired; the
  vector is the toolchain's already). Findings: the game's units are C++14 (under /std:c++17 pair's operator< and the
  uninitialized-copy loops compile differently; every other probe source is still built C++17, so their leftovers
  may match the same way); several units are /GS- (the security cookie). The Elem16 partition matches only with
  SortKey defined in the unit; Med3 0x16c540 (0.89) and sort 0x16caa0 still differ, because the original keeps a value
  in xmm1 across the SortKey calls (it knows the callee's register use: try SortKey's real body, or /GL). Left (9.3
  KB, the table row above), with near misses: pixel partition 0x1706a0 0.71 and pop-heap 0x1719d0 0.62 (Compare over
  PShader*); 0x172c00 0.59 (an element move loop: vector of vectors?); 0x4dfc90 and 0x16ed40 / 0x16efb0 / 0x16c930 /
  0x16f200 / 0x1730c0 / 0x1732e0 / 0x174480 have none. Not library: the eight cPixelFormatBaseT functions
  0x15d7d0..0x15da70 (the template's destructor: vtable store, then the std::string at +8; reached from the unwind
  funclets of the pixel formats' constructors, about 0xd198f3..0xd19d45: RA's area), the cRZMemPtrIterator slots
  0xa6f710..0xa706f0 (vtable members of game templates) and 0x194d20 (GZServiceAllocator allocate for the 36-byte
  cAABBTreeBuilder::Node: compgeom's). 0x14c930 is shared_vf (skip); 0x1758a0 is 4 bytes.
  **RL2 result (lane WM, 2026-11-04).** `libmatch --add-objects DIR` adds a lane's objects without the 25-minute
  re-index; tools/stlprobe gained probe_rl2.cpp and C++14 copies of every probe source but probe_one.cpp (`-Only14`;
  2,806 probe_one objects at /O2 added no row). 511 rows, 48.4 KB, 3 dropped, table in the WM report and
  notes/feedback/WM.md. Matched from the task's list: 0x15acf0, 0x15af60, 0x15ac90, 0x17abe0, 0x17ac80, 0x8bab40,
  0x8bae00, 0x8c0ad0, 0x90efbb, 0x90f043, 0x90f0cb, 0x90f153, 0x1af77b, 0x90a08d, 0x298af0. Findings: the try frame
  with a saved esp and a state variable but no cookie (0x8c0ad0, 0x8bab40, 0x8bae00) is /EHa /GS-, so $Rl2FlagSets and
  $RenderFlagSets carry /EHa ... /GS-; the Fmt* sorts need the comparison virtual or reversed (probe_rl2.cpp FAM 0).
  The Shader* family is still open and the reason is now known: the original's partition 0x1706a0 pushes a dword
  comparator by value and calls 0x1749b0 directly, so WM took the comparator for a non-empty class. (XI: wrong. It is an empty class passed by value, the dword is its
  padding, and the comparison function is out of line. See "XI result".) A `void *` member gives the
  right instruction sequence with different registers (0.76, the comparator stays in a register across the
  size test where the original reloads it inside the branch); a `bool` member is worse (0.71). The real comparator type
  is what WM thought to look for next. Also open then: 0x8baca0
  (321 B, 16-byte vector growth with a try frame), 0x90f21e (145 B, map<Str> find calling
  std::operator<(string, string) directly on key+4 and node+0x14; neither a Str operator< nor a functor gave it),
  0x1af72b (41 B). Status entries left on rows, on purpose: 0x14aac0, 0x14c060, 0x176f00 (RefPtrVector, real
  vectors of a counted pointer with many callers), 0x1d57e0 (ServiceRefVector, an allocator-service instance),
  0x824727 (host-started), 0x1bdbc9 (process-heap allocator 0x1be421: the name only shares bytes with a std::allocator
  stand-in).

  **XI result (lane XI, 2026-11-06).** 165 rows, 32.4 KB (135 functions 31.9 KB, 30 funclets and tails), 7 gone (3 tiny rows, 29 B, that a second name
  made non-unique; 4 gap pieces now inside whole rows); 8 exclusions. By region: 0xbd1000..0xbd9400 12.4 KB, 0xbf9620 /
  0xbf9800 893 B, 0xbcc000..0xbd1000 1.7 KB, 0xbdd000..0xbe9000 4.6 KB, the shader and vertex lists 4.0 KB, the
  effects registry 944 B, the rest in other subsystems' containers (WO's 27 included). Done with $Tuned in stlprobe.ps1 (single units, each what one or a
  few rows need; a cross product would be millions) and probe_e11.cpp, probe_rl3.cpp, probe_knobs.h. How it went: score
  a row against a directory of objects, read the first differing instruction, say what the source would have to be.
  - The effects unit is `/EHsc /O2 /GS-`; some bodies appear only with /Ob3; 0x8baca0 is `/EHa /O2 /GS- /Ob3`.
  - **Knobs.** The game's units keep some STL helpers out of line where a small probe unit inlines them. Declaring an
    explicit specialization and leaving it undefined forces the call (objects are read, never linked, relocations
    masked). A probe's `/DKN=` bits do this through the forced include probe_knobs.h.
  - **Defined, not declared.** Where the game's compiler had seen the helper, a declaration isn't enough: it assumes a
    throw and keeps a try frame, or spills what the call might clobber. Define the helper in the unit
    `__declspec(noinline)`. `noexcept` and `__declspec(noalias)` don't reproduce it. 0x16c930 needs both
    `_Uninitialized_value_construct_n` and `_Allocate_at_least_helper` so defined.
  - **Register allocation is reproducible** once the helpers and the source shape are right: fields as scalars, not
    arrays; the hash written as the game's functor, `first=H(); second=H(); second|=H(number); return second|first`.
  - Which members inline depends on the element's real shape, so use the real records (src/ts/cTSEffectsCollection.h,
    cTSParticlesDescription.h, EffectsTransform.h). A 4-byte element of two uint16 fields gives the non-memset value-init
    loop.
  - An explicit instantiation emits what a small unit would inline (`_List_node_emplace_op2`, 0x1af72b). An implicit
    destructor comes out of line through a derived `__declspec(noinline) ~T() {}`. A `call` on `node+8` in a hash node
    was the string's out-of-line `_Tidy_deallocate`: specialize it before anything instantiates the string.
  - 0x8baca0 is `_Resize_reallocate<_Value_init_tag>` over a 16-byte element with an empty user constructor; the `ja`
    to the throw is the compiler threading the capacity clamp.
  - 0xcc4e30 is library. So is 0xcc4d10, verified as cTSEffectsExtension::FindRecord: it is truly `_Find_last<Key>`,
    and its status entry and hook are for the owner to retire.
  Six new rows were callees of our code (0x4209f0, 0xbe09e0, 0xbe0b90, 0xcc4e30, 0xcc5280, 0x1d6520): those call sites
  now use the toolchain's container over the same bytes, on the allocator each row's own calls show (the process heap
  for five, the allocator service for 0x1d6520: `src/rz/CountedRefVector.h`). The registry's key copy and destructor stay
  the game's (0x4f5a10, 0x257860).

  Left, 5.2 KB, each with why:
  - **Pixel Shader sorts:** 0x171ff0 861, 0x170070 1584, 0x171880 324, equal_range 0x1730c0 532 (0.90), 0x1732e0 607
    (0.84). The game's pixel comparison 0x174920 is inlined at some call sites of one body and not at others (0x171880
    calls both it and Less 0x1747d0 with a filled key). `inline`, plain and noinline each miss it: noinline gives no
    body of the size, `inline` 0.73 on the partition, 0.56 on the insertion sort. The inlined key fill's store order is
    not settled either (best of 120 orders 0.57). The vertex side, 0x1706a0 / 0x1719d0 / 0x172350, matched.
  - **Med3 0x16c540 119, sort 0x16caa0 713:** with SortKey extern, Med3 is 122 B at 0.99 (the game loads xmm1 earlier);
    with SortKey defined, which the partition needs, Med3 takes a stack temp (135 B) and the sort's frame grows 0x10.
    Element shapes and a fuller key body didn't help.
  - **0xbf99f0 269**, `_Uninitialized_value_construct_n` over EffectComponent: every instruction matches but the
    loop's induction base (esi+0x38 in the game; probes give 0x2c, 0x30, 0x3c, 0x44). The transform constructor's
    source form isn't settled.
  - **0x90f21e 145**, `_Tree::_Eqrange` of a map keyed by a class with a string after its vtable: `__forceinline` on
    the functor gets 0.86 (loop rotation and the `this` slot differ). A comparator form we couldn't reproduce.

  Game code, not library (no row, wherever they were filed): comparators 0x1747d0 336, 0x1749b0 154, 0x174920 140;
  EffectsTransform rotations 0xbd1760, 0xbd1870 (272 each); curve-from-parser helpers 0xbd1a50 286, 0xbd1b70 110,
  0xbd1be0 245, 0xbd1ce0 92; by call shape 0xbdc6f0 110, 0xbdcdb0 132, 0xbe8de0 305, 0xbe8f20, 0xbe8f90, 0xbe92d0,
  0xbe93f0, 0xbe9670, 0xbe9980, 0xbf9b00 338, 0xbf9c60, 0xbf9da0, 0xbf9ee0, 0xbfbb60 135; Key's copy constructor
  0x4f5a10 227 and destructor 0x257860 155.
- **E1, effects descriptions (Opus).** Calls: the persist reader / writer (ours), cRZString, the effects maps.
  Fixture: tests/ts_effects_collection.h (fxtest: FakeDescription, the collection dump) extended with the effect base's
  layout and a fake effect owner; sgfx.h's persist fakes (cIGZPersistReader / Writer). Test: Read / Write round trips
  of each description on generated fields, compared byte for byte; the maps' lookups. Pitfalls: the 72-84% owner-inferred
  code in the big effects means some "description" helpers are theirs: settle owners first; float fields compared as
  bits.
- **E2, the small effects (Sonnet).** Calls: the effect base (E1), the decal's terrain query (world-lot: original call),
  the sound effect's audio service (svcfx fake). Fixture: E1's effects world, svcfx. Test: each slot (start, update,
  stop) on generated descriptions, compared as the effect's state and the service calls. Pitfalls: water and sound ran 0
  bytes (difftest only).
- **E3, particle geometry (Opus).** Calls: the vertex buffer (the fake device), cIndexedMeshBuilder (scenegraph:
  original call until the scenegraph's S2), cTSParticlesRenderable (in the lane). Fixture: fx_d3d9.h / sg_geometry_node
  _device_fx.h, the effects world. Test: vf8 over generated particle sets, the vertex bytes compared exactly. Pitfalls:
  x87 vs SSE evaluation order in the 9.8 KB body (check the disassembly before writing; float compares bit for bit).
- **E4..E9, particles and meta-particles.** Calls: the descriptions (E1), particle geometry (E3), cPathPlanner (world-lot
  original calls), the multi-threaded behaviour base (foundations FD). Fixture: the effects world; drawfx for the
  device side. Test: each slot over seeded simulations, the particles' state compared after N steps. Pitfalls: random
  number sources (the game's generator: seed both sides the same); the structure pass decides what of the owner-inferred
  code is the class's own. E5 and E6 have their own recipes in "cTSParticlesEffect" below, E8 and E9 in "cTSMetaParticlesEffect".
- **E10..E14.** Fixture: the effects world plus the existing tests' fakes (ts_effects_manager.cpp, ts_effects_resource
  .cpp); E12's parser on synthetic text. Pitfalls in the effects tests' headers.

### The effects world (lane WO, row E1; what E2 onward start from)

**Done in E1:** EffectsTransform::ToLocal, cTSLightEffect whole, cTSEffectsMaps whole, cTSParticleEffectDescriptor whole.
The audit row's other names: cTSMetaParticlesDescription's and cTSBaseParticlesDescription's bytes are nearly all
standard-library shapes (27 library rows added, the unmatched left are vector and tree helpers with no game logic; lane
report); cTSVisualEffect's constructor 0xbe86a0 (the row's "cITSVisualEffect 800 B") stays with E10; cEMObjectQualityEffect
is an audio event modifier (subclass of cEventModifierBase, vtables 0xdad63c / 0xdad65c), no effect at all: its two slot
bodies are done (src/audio/cEMObjectQualityEffect.h), its constructor and destructors (132 B) go with cEventModifierBase and
its siblings (cEMAliasModifier, cEMDestFootstep, cEMDestSurfaceType, cEMObjectQuality) in the audio campaign, together with
the curve 0xc0a6a1, 0xc0a86a.

**Slot 1 of cITSComponentDescription is CreateEffect**, not "Prepare" as lane IW called it: it makes and returns the
cITSComponentEffect*, null for brush, scrubber and game. Each kind's creator is a cdecl static taking the description:

| Effect | Create | new | Constructor |
|---|---|---|---|
| particles | 0xbc0c50 | 0x210 | 0xbb4a80 |
| meta particles | 0xc03910 | 0x1f0 | 0xbfd4f0 |
| sound 0xc05670, water 0xc09210, sequence 0xc07490, model 0xc06c00, camera 0xc08b90, screen 0xc09ee0, decal 0xbfd440 | | | |
| light | 0xc0a140 (ours) | 0x2c | 0xc09f40 (ours) |

When a lane replaces one, its description's CreateEffect calls ours and leaves original_calls (the light's in
src/ts/cTSCompDescription.cpp). A created effect then carries our vtables alone: the slots need no adapters.

**Shared headers.** src/ts/cTSEffectBase.h: cITSComponentEffect (14 slots), cTSEffectBase (0x14: vtable, cRZRefCount face at
+4 with its count at +8, mDescription +0xc, mRunning +0x10), cTSNodeEffectBase (the light's and the model's: mOwner +0x14,
mNode +0x18, Start, Stop, IsRunning, AddToScene, Update, AttachNode(owner, class, iid); the model's vf0 0xc05cf0 uses it
with class 0x65246462, iid 0xe51c10f5). src/ts/EffectsTransform.h: fx::Vector3, fx::EffectsTransform (flags +0, bit 1
rotated; m[3][3] +4; scale +0x28; translation +0x2c), first-operand float helpers Add, Sub, Mul, Div, RowFromX, RowFromY.
src/ts/CountedRef.h: fx::CountedRef<T>, the inlined cRZAutoRefCount.

**Slots** (rva without 0x; `.` is the base's default body, which is under 5 bytes or folded across classes):

| Effect (vtable +0) | 0 Attach | 1 Detach | 2 Start | 3 Stop | 4 IsRunning | 5 AddToScene | 6 Update | 7 HandleEvent | 8 Query | 9 SetFlagged | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| decal e91214 | bfcbe0 | . | bfd010 | bfd3f0 | 150f10 | bfc830 | bfcd90 | bfcce0 | 1477b0 | bfcd70 | . | . |
| sequence e91494 | c071d0 | . | c07310 | c07360 | c07210 | c06e80 | c07290 | . | . | c07230 | . | . |
| water e91594 | 405100 | . | c091c0 | c091f0 | 150f10 | c08f40 | c09080 | . | . | . | . | . |
| sound e9132c | . | . | c05410 | c05580 | 2279f0 | c051a0 | c052d0 | . | . | . | . | . |
| screen e91618 | c09820 | . | c09e40 | c09e90 | 16db30 | c09420 | . | . | . | . | . | . |
| camera e914fc | c08160 | . | c08530 | c08a00 | 1fc030 | c07d70 | c083b0 | c083a0 | . | . | . | . |
| light e918dc (ours) | c0a080 | c0a120 | c06bb0 | c06bc0 | 150f10 | c05ad0 | c069e0 | c0a0e0 | . | c0a100 | . | . |
| model e9139c | c05cf0 | c06b80 | c06bb0 | c06bc0 | 150f10 | c05ad0 | c069e0 | c06710 | . | c069c0 | . | . |
| particles e8bdbc | bba860 | bc07e0 | bc0a10 | bc0b90 | 1b79e0 | bb6830 | bbd8f0 | bbd0e0 | bbdc90 | bbd380 | bbd0a0 | bc0c20 |
| meta e91284 | bffe20 | c02ea0 | c035f0 | c03770 | 1b79e0 | bfe210 | c02bb0 | c020c0 | c02dd0 | c02600 | c02080 | c038f0 |

Slots 12 and 13 (AddRef 0x1dab20, Release 0x5b7980) are the count's, folded. The model has a 15th slot, c06550. Bodies shared
between kinds (150f10, 1b79e0 and the light's and model's) are folded: see each one's references before hooking, and a body
in cTSNodeEffectBase stays unhooked until the model is ours. The destructors: each effect's stores 0xe8bd68 (and
0xda2fa8 last at +4) on the way out; the base's deleting destructor 0xbb5830 and its thunk 0xbb5759 belong to no object
(entered `mapped`, cTSLightEffect's status file). Test "Owned tables" through a table code covering both the live and the
destroyed vtables (TableCode in tests/ts_light_effect.cpp).

**E2 notes (lane WP).** cTSSoundEffect, cTSWaterEffect, cTSSequenceEffect whole (src/ts/, tests ts_sound_effect.cpp,
ts_water_effect.cpp, ts_sequence_effect.cpp), only Create hooked. cTSDecalEffect whole by lane WS (tests/ts_decal_effect.cpp,
only Create 0xbfd440 hooked, and all four CreateEffects are ours).
- The decal effect's slot 8 is the shared 0x1477b0, which answers true: not the base's no, which the other effects inherit.
- Decal x87 sites: the curve 0xbb5620 returns on the x87 and the product with an effect scale is x87 (alpha's first reading
  is stored, size and rotation chains are held), so they round to the thread's precision control: the tests run at 24 and
  53 bits. The rest is SSE. Start's repeat product is aspect * repeat, AddToScene's repeat * aspect: the operand order shows
  on NaN payloads.
- Decal bugs reproduced (`// ORIGINAL-BUG:` in src/ts/cTSDecalEffect.cpp): Attach with a null owner, Attach clearing running
  with a decal out (it stays with the manager), Start with an empty colour list.
- `cITSComponentEffect::~cITSComponentEffect` is out of line (cTSEffectBase.cpp): an inline empty one is dead-store
  eliminated, so a derived destructor would not store the base vtable the game's does and destroyed-object vtable words differ.
- tests/ts_comp_description.cpp stands the effect Creates in with shape::StandIn (ours vs. a logging stand-in).
- Originals bugs found, not reproduced (tests avoid them): water Start with the body flag and no owner dereferences null;
  a negative splash interval loops forever. Sequence Start without restart leaves the index past the end, then reads garbage.
- FewValues / FewOrUniform include inf, NaN and huge values: never feed them to a time step that drives a loop.

**The owner** Attach gets is the cTSEffectsManager (checked 2026-11-05, lane XD; the earlier note said cTSVisualEffect's +4 face):
slot 23 (+0x5c, 0x238f10, a folded getter of +0x84) gives the kind (2 and 3 set flags in 0xbe9150), slot 26 (+0x68, 0xbc9e10)
is GetSlotObject(int) (+0xbd0+4*i), slot 42 (+0xa8, 0xbca330, no arguments) GetScene (+0xc1c), the tree node a node effect goes
under, slot 43 (0xbcbde0) GetCamera (named by lane XD). The fakes' "owner" (FakeOwner) is therefore a cTSEffectsManager, not an effect.
cTSVisualEffect (E10): 0x190, three faces (0xe8f7e0 24 slots, +4 0xe8f844 14 slots, +8 0xe8f880 5 slots), description null at
+0x10, a string at +0x14, four EffectsTransform blocks (+0x50, +0x88, +0xc0, +0xf8), the owner (a cTSEffectsManager) at +0x18c;
constructor 0xbe86a0, destructor 0xbe8a40, deleting 0xbe8be0; made by the manager's pool factory 0xbc9ab0 (operator new 400).

**The fixture**, tests/ts_effects_collection.h, namespace fxtest (its `// fakes:` line lists what it fakes):
- `EffectsWorld w;` per run: `w.nodes[kNodes]`, `w.owners[kOwners]`, `w.trees[kTrees]`, `w.failClasses` and the patched
  director framework; cIGZCOM::GetClassObject hands out the next FakeNode and logs class and iid.
- `FakeNode`, `FakeOwner` log every call by their id, so a blob is the same on both sides. gFakeNodeVT and gFakeOwnerVT are
  writable: a test sets the slots its effect asks for. An unset slot logs its number and pops nothing, so one called with
  arguments crashes the run.
- `EffectStep(b, effect, w, r, hasNode, hasOwner)` runs one random slot of the 14 through the object's own vtable;
  `PushEffectBase`, `PushNodeEffect` the object's fields; `EffectVt::Of(effect)` both vtable words; `RandomTransform(t, r)`
  the transforms with NaNs and scale 1; `Bits(float)`, `CallSlot<R>(face, slot, args...)`.
- Shape of a test: tests/ts_light_effect.cpp. Original Create in mode 0, ours in mode 1; the two-word table code; the
  block and the fakes' counts after the destructor; `difftest::FreeRecorder` for the sizes.
- The padding after mRunning is the heap's on each side: compare the byte (LightEffect_Slots), or one garbage block
  (LightEffect_Construct).

### Particle geometry and renderable (lane WQ, row E3; what E4 onward start from)

**Done in E3:** cTSParticlesGeometry whole (src/ts/cTSParticlesGeometry.h), cTSParticlesRenderable whole with its template
copies (src/ts/cTSParticlesRenderable.h), the curve reading 0xbb5620 (src/ts/ParticleCurve.h) and EffectsTransform::Rotate
0xbc0f80. The game makes the renderable in 0xbba860 (cTSParticlesEffect slot 0, not ours yet) and the geometry in its Setup.

**Left:** the template levels' copies (constructors 0xbb48e0..0xbb4a20, destructors 0xbb5120..0xbb51f0, level deleting slots 0xbb5770..0xbb5810, level vtables) stay live: nTSSG::cOverlayNode 0x3ae470 still builds on them, so they go when that class does. The real effect and particle classes replaced this row's views in E4
(src/ts/cTSParticlesEffect.h). The slots of the transform and renderable family folded across classes (`shared_vf_*`, about 70) run through the pilot here but belong to their defining classes.

**What was learned:**
- vf8 0xbbdd50 is scalar SSE single throughout. x87 appears only at the 0xbb5620 calls and at the warp / stretch dots:
  `nGZCompGeom::convex::{Mul87,Add87,Stored}` and `nGZCompGeom::Dot87` model them. So the body's result depends on the
  control word only there. Tests run each seed at 53 and 24 bits (`difftest::X87Precision`); the harness default is 53, the
  game's device thread 24. `fx::Add` / `fx::Mul` keep the first operand's NaN; `x + 0.0f` normalises -0 and quiets NaN.
- Key lists: the nearest integer by the 2^23 + 2^22 trick, moved up when i + 1 converts to exactly x
  (`fx::KeyPosition`). The curve's low and high clamps are equivalent one at a time; only a combined mutant separates them.
- `// ORIGINAL-BUG:` a particle's t outside [0, 1] reads past a key list longer than one entry. Worlds with NaNs and
  infinities keep their lists to one entry, as what lies past the list is not comparable.
- `// ORIGINAL-BUG:` the clone of slot 15 makes a plain cTransformNode (67 slots) that inherits the bounded flag, so a
  disconnect reads slot 69 past its vtable. The game's nodes are leaves, so the pilot clones leaves only (`Cfg::CloneSource`).
- The renderable's slot 87 is `mov al,2; ret`: its return is uint8_t. Compare it as a byte.
- Slot 60 is the Impl's SetWorldTransform 0xae04b0, then RefreshDetailLimit 0xae06b0, then the detail object's slot 43: a
  detail fake needs 44 slots.
- The description header's `mAlpha` (+0x88) and `mAspect` (+0xb4) look swapped: +0x88 behaves as the aspect, +0xb4 as the
  alpha. Left as named until the rows that use them settle it (E4: Attach reads +0x88 for mMaxSize's neighbour mMaxAlpha,
  still not settled; InitParticle in E5 decides it).
- tests/sgfx.h's traversal slot 7 is `VisitParticles`. The Lock fake of tests/ts_particles_geometry.cpp is the pattern for
  vertices: fill 64 KB with 0xcd, log the first 16 KB at Unlock, and a hash of the rest.

### cTSParticlesEffect (lane WR, row E4; the recipes of E5 and E6)

**Structure.** new 0x210; vtable 0xe8bdbc (14 slots), the +4 refcount face 0xe8bdf8 (5 slots). Create 0xbc0c50 (only caller
0xbe58b0, the description's slot 1), constructor 0xbb4a80, destructor 0xbb5290, deleting 0xbb5860, its +4 thunk 0xbb5761.
Slots: 0 Attach, 1 Detach, 2 Start, 3 Stop, 4 IsRunning (shared 0x1b79e0), 5 the step 0xbb6830 (`void(float step, stats *)`, it
is the slot the class is about), 6 Update, 7 HandleEvent, 8 Query, 9 SetFlagged, 10, 11, 12/13 shared refcount. It is a
standalone-shape class: only Create is hooked, a live effect carries our vtables, slots need no adapters. Helpers the game's
remaining code calls are hooked one by one (the table src/hook/tables/cTSParticlesEffect.cpp). Layout, every field with its
offset: src/ts/cTSParticlesEffect.h; the particle (0x7c bytes), cTSParticlesSettings, the attractor and collider states, the
box are there too. Names of the rest are recorded tentative in Ghidra.

Filing: the region 0xbb4a80..0xbc2f80 is the effect's apart from the template levels' copies (E3) and `.?AV?$c...` names in
0xbb9190..0xbbd070 (node template slots, the renderable's). Nothing else in it is STL-shaped except what the closure's outer
range has: 0xbdd870 and 0xbddc40 (std::vector resize of the attractor states, only caller 0xbddf10, gone with it; library rows, lane XI) and
0xbddce0 (37 B, a `_Deque_val` constructor's row by fold). Library code already has its rows (0x177770, 0x2b36f0, 0x152c50, 0x152e90, 0x29b310).
Not the effect's though filed with it: 0xbc1d10 is a member of **cTSParticlesRenderable** (`this` the renderable, the effect read at
its +0x7c), 0xbc0cb0 of the box (this = box, argument a transform), 0xbba1b0 of the path object in the description
(a vector of 0x1c-byte waypoints plus a loop flag), 0xbdf6a0 / 0xbe01c0 a free collision sweep shared with the meta-effect
(0xbfe190 calls it too). Outside the region, reached from here: 0x1443b0, 0x1a6250 (matrix of absolute values), 0x1a6db0 (618 B
compgeom, quaternion rotate), 0x1ef450, 0x2b7d50 (COM cast, 8 callers), 0x360530 (identity, 11 callers), 0x4dca10 (shared_vf,
unhookable), 0x824624, 0xa39700 NodeTransformChanged. Uncalled: 0xbb5270 (base destructor).

**The test.** tests/ts_particles_effect.cpp has the world every row after this one uses: read its header. Particles_Effect_Frame
already runs the game's 0xbb6830 on our object for 32 frames, so everything E5 and E6 write is running there today, game
code against our layout. Its comparison is the frame-by-frame whole-object fingerprint; new tests add to it rather than
replace it. When a replacement lands, stand the callee in on both sides (docs/lane/test.md) only where it receives no
object of our side; these all receive our effect, so **their test calls the original and ours for real**, one side each,
with the same seed. Move the world (lines 84..690: ResetWorld, the fakes, the fingerprint) to a tests/ts_particles_world.h
before adding a second test file. Slots takes about 50 s at 600 seeds, Frame a few seconds.

**What bites here** (each cost a debug round in E4):
- *The NaN branch rule.* `comiss a,b; ja/jbe`: a NaN takes the JBE side, so the "then" is the ordered `a > b`. Ghidra prints jbe
  as `a <= b` or `a < b || a == b`: write `if (a > b) X; else Y` or `!(a > b)`, never `a <= b`. `comiss a,b; jae` hands on only for
  an ordered a >= b. `jb` is taken on NaN. Box comparisons, the life check and the wind turn all needed it.
- `fx::Add`, `Mul`, `Sub`, `Div` keep the first operand's NaN. Test at 53 and 24 bits (the test's `Precision`); the curves return on
  x87 (EvaluateCurve, src/ts/ParticleCurve.h) and the product is taken there before it is stored: nGZCompGeom::convex's Mul87,
  Stored.
- *A negative pause never ends.* An emitter's loop with d+0x34 negative (`m30.y`) never finishes, in the game too. Test
  descriptions keep it positive (`tame` in RandomDescription). A rate curve of zero and `gSettings` of zero give no particles at all
  (the frame test passes with nothing in it): check how many frames had particles, 4322 of 7680 now.
- Random order is part of the behaviour: every RandomDoubleRangeUniform in its order, each with the bounds as doubles from
  the floats. The generator is 0x10fd138, compared after every step.
- Particle pool: cells 0x107cfc8 element size (0x7c), 0x107cfcc block bytes (0x3e00, 200 particles), 0x107cfd0 block list,
  0x107cfd4 free list (`kPoolFreeCell`). Keep the game's cells (GameAddress), Retire and the emitters share them. A block is
  RZMemory::Malloc(block + 8), first word the list link, then the chain. An emitter allocates the block inline: one shared helper
  in our code is fine, the three do the same thing.
- Particle counts and the 2^23 + 2^22 trick: `0.5 + x + 12582912.0f`, low mantissa bits as the integer, minus one where
  converting back gives exactly x. HandleEvent 0xd has a plain form of it; fx::KeyPosition the same.
- The mask at 0x107dda8 (8 bytes, the integrator choice) is in .data, never written: read through GameAddress, the harness has it.
- `in_ECX` is not always the effect: see the owners under Filing. Check before writing a member.

**cTSEffectsParser's set +0x278.** `EffectsBlob56Less` is a placeholder: only clear and destroy are ours, the game's
cEffectRemapCommand 0xbce130 inserts with its own comparator. Whoever replaces that command takes the real one from it.

**E5 landed (lane WV).** All of it hooked and `verified` (RandomInBox and RandomInEllipsoid by byte match, the rest by difftest and
mutants; the mutant counts are in db/status/cTSParticlesEffect.jsonl). What the recipe below missed, for E6 and later work:
- 0xbb6730 CollideColliders returns a bool (false: a collider killed the particle). 0xbbb390 InitParticle is `ret 4`, this the effect.
- Slot 5 of the surface returns its height as a float on the x87 stack; the callers do `call eax; fstp dword`. Call it as
  `float(__thiscall*)(void*, float, float)` and a test fake as a float-returning function. The surface's class is not identified.
- InitParticle's scene call is slot 140 (vtable +0x230), thiscall (out[0x1c bytes], 0), answering with a pointer to {position, quaternion}
  (the original reads the answer). It reads the size curve at t = 0 (ORIGINAL-BUG, probably, in src/ts/cTSParticlesInit.cpp).
- IntegrateParticle's mask global 0x107dda8 is taken as the constant 0x04028018; its runtime value was not checked in a running
  game. The same for the global at 0x10fd170 (open, see the report).
- Open edges, not tested: an empty path in InitParticle, and a rotate count above 3 (the loop reads on into the description's
  curves). OnPath (0xbbcb70) and OnPathPlaced (0xbc08b0) have the same 32-byte hook crc 0x65d698e4.
- A test counts what it exercised (Particles_Init fails if a branch was never reached): follow that for the emitters' tests.

**Recipe E5, the particle** (callees of E6; about 15 KB). In this order, each hooked at its RVA as it lands (game's 0xbb6830 still
calls them), `this` the effect unless said:
1. 0xbbcd80 RandomInBox(out, box): a random in each of [0],[3] / [1],[4] / [2],[5]. 0xbc2f80 fx::Normalize(out, in) (184 B): len²
   as double; if 1e-6 < len², divide by the float sqrt, else copy. 0xbdd2f0 RandomInEllipsoid, 0xbdd430 RandomOnRing (mSourceRing).
   Test directly, random boxes and rings, generator compared.
2. 0xbb9840 AttractorForce (803 B, three randoms), 0xbb9b90 SumAttractors (769 B, over mAttractors, curve reads), 0xbb6730
   CollideColliders (244 B) which calls 0xbdf6a0 / 0xbe01c0 (not cPathPlanner: the sweep over mColliders; slots of `gColliders`: 5
   (+0x14), owner slot 8 with argument 2). The sweep is shared with the meta-effect: hook it at its RVA, keep it a free function.
   0xbb85b0 CollideSurface (1607 B): the surface's slots 5 and 6 (height, normal); `gSurface` needs them, one random. x87: float10
   returns from slot 5. 0xbb82f0 ApplyForces (696 B): attractors, drag, colliders under flag 8/0x18, then the surface test by the
   world placement (+0x19c, +0x1a0 the range; outside and flag 0x40000 kills, inside and flag 0x8000/0x28000 collides). Return al;
   a kill also clears mAlive. It multiplies by the world's rotation only when mWorld.flags & 2.
3. Integrators (`bool (particle, step)`, al): 0xbc1190 dispatch on d+0x15c: flag 4 → (flag 8 ? 0xbc08b0 : 0xbbcb70); flag 2 → (flag 8 ?
   0xbc19f0 : 0xbc1530); else masks clear and step × drag <= 0.05 → 0xbc12a0, else 0xbc13c0. 0xbc12a0 is the plain one (278 B): age,
   the end, then gravity, drag, midpoint. 0xbc13c0 and 0xbc19f0 are substeps of at most 0.06666667 s around 0xbb82f0. 0xbb8c00 (1411 B) finds
   the waypoint in d+0x148 by age, 4 at a time then 1 (write the plain search) and steers. 0xbba1b0 (1410 B) evaluates the path at
   a time (this = the path); 0xbbcb70, 0xbc08b0, 0xbc1530 use it. These are unrolled, not structured differently: the compare is
   what matters.
4. 0xbb9ea0 ParticlePosition (782 B): out, particle, description (not a member): position plus the screw around the velocity.
   Wave and warp curves by age/life, libm sqrt, sin, cos (`fx::SqrtF`, `SinF`, `CosF`: the precise ones), quaternion rotate 0x1a6db0
   (`t_RotateVec3ByQuaternion`). The radius clamps both ways on d+0x214, NaN by the rule above. Below 1e-6 speed or a zero
   radius: the plain position.
5. 0xbbb390 InitParticle (3085 B), last, since it is the one that writes a whole particle, in random-call order: alive, age 0, life,
   frame offset; the path's first point to +0x30 and +0x3c, velocity; position by ring, box (mSource) or ellipsoid (flag 0x100); velocity
   box d+0x38 (flag 0x40: from the position); speed random(d+0x50, d+0x54) × mSpeedScale, normalise threshold 1.4210856e-20; transform by
   mLocal; high flag 0x20 the facing axis through 0xbc2f80; low flag 0x20 asks mScene's slot 0x230/4 and stretches (a curve,
   Dot87); +0x48 = random(1 ∓ d+0x84) × mSpawnSize; +0x4c random(1 ∓ d+0x98); pivots random(±d+0x1c4, ±d+0x1c8) + 0.5; +0x60 random(±d+0x1cc); +0x64
   random(1 ∓ d+0xc4); colour random(1 ∓ d+0xa8..0xb0); +0x74 random(1 ∓ d+0x210); +0x78 random(1 ∓ d+0x22c); rotations from d+0x180's pairs,
   max(1, count); the end, mConstrain's slot 3 (mLocal, mWorld, the position, d+0x248) when set: `gConstraint` pops its 4 arguments.
   When it lands, HandParticleOn's `OriginalFunction` goes (status: drop 0xbbb390 from `original_calls`). Settles mAlpha / mAspect.

Test E5 with one new DIFF_TEST per step above, each built on `Helper` (mode 0 by RVA, mode 1 by member) over `tame` and wild
descriptions, particles from the fingerprint's reader, the Frame test run after each landing.

**Recipe E6, emission and the step** (about 18 KB, needs E5).
1. 0xbc0cb0 cTSParticleBox::Transform(transform, 706 B): no rotation, scale and translate both corners; rotated (flag 2), by
   centre and half size with the matrix of absolute values 0x1a6250. Rigid data, test directly.
2. Emitters, `(float step)`: 0xbb5c70 continuous, 0xbbbfa0 bursts (1470 B), 0xbbc560 once-per-step (1100 B). All three: pool pop, InitParticle,
   dead or 0xbc1190 false → RetireParticle, else AddParticle and grow the box (inline extend when the size or d+0x1f8 is under
   1e-6, else 0xbb5bd0 twice along the velocity, half the max size × mSizeScale / d+0x1f8); ParticlePosition unless d+0x1fc == 0 and a single
   0 at d+0x200. Write the common part once. Continuous: step <= 0 returns; scale mScale17c by mWorld scale to flag 0x800 / 0x1000 / 0x2000
   powers; distance moved from +0xf0 over +0x134 with d+0x160 boost; ticks of at most 0.06666667 s: clock + tick × period; passing a count:
   flag 0x40000000 clears, d+0x24 loop limit (Stop(1) or hold at limit - 1e-6 with flag 4), d+0x30 >= 0 pause (reset clock, shorten the
   tick, random(d+0x30, d+0x34) off what remains: the trap above); rate from d+0x10 curve inline. Bursts: curve × +0x17c count per
   whole period, limit, pause, and the leftover into mDelay. 0xbb97a0 Emit (154 B): unless stopping: mDelay down (leave while positive,
   the leftover goes on); clamp to d+0xc - 0.01; flag 2 / 1 / else picks the emitter. Reached only when +0x208 == 0.
3. 0xbc1d10 on the **renderable** (4687 B), after cTSParticlesRenderable's shape: for each child and particle, an orientation
   (cells 0x107cd10.. the axes, static) and six events by id (+0x88..+0xa0 of the renderable) to the child's slots 7/14. Our own
   objects, ours on both sides; only reached when visible and shape set. Needs 0x1a6db0, RigidInvert, libm. Fake the child; `gShape`
   of Frame has none of it, so a new world part.
4. 0xbb6830 the step (6840 B), then drop the forward in AddToScene and its `original_calls` on Create; hook nothing: `in_effect`
   callers. Order: preroll; parent once (owner slots 42, 43, then its slot 45 with the renderable, when visible with no parent);
   when step >= 0: +0x134 if moved more than 1, the three lerps by settings +0xc times mRateScale, mLifeScale, mEvent3 (flag 0x10000000 swaps
   two; 0x4000 scale; flags 1, 2 by the settings' factors; flag 1 only if curve(0) > 20, else +0x17c = 1); attractors Follow; seven particle
   loops by integrator (write one loop, with the choice inside it only if the Frame test agrees; the game hoists the choice):
   dead → delete node (sized 0xc), count, Retire, RemoveParticleChild; alive → ParticlePosition, Extend. Then Emit, box grow and Transform,
   the end test (no count and stopping: clear +0xd), StepWind (flag 0x100000), 0xbc1d10, NodeTransformChanged, +0x134 = the position
   kept, stats (+8 count; +0xc / +0x14 by flag 0x400000 when +0x142 < +0x140; +0x10 conditionally).
   Frame compares this end to end: run with more seeds, and it is `verified` with mutants on each loop.

**E6 landed (lanes WX and XB).** Emitters, Emit and slot 5 (0xbb6830, src/ts/cTSParticlesStep.cpp, lane WX) are `verified`; slot 5 is not
hooked (in effect through the vtable Create installs). UpdateParticleChildren 0xbc1d10 (src/ts/cTSParticlesChildren.cpp) and
Quaternion::SetRotationBetween 0x1a6db0 (lane XB) are hooked. Nothing in the effect calls the game any more. Particles_Effect_Frame: 1200
seeds, a shape on a third of them, AddParticleChild and UpdateParticleChildren real on both sides.

What UpdateParticleChildren does (facts for E9's sibling loop, 0xc03ea0): the renderable's world transform inverted (slot 54), then
children (slot 42 for the range, slot 38 for the next, stopping at the range's end) and particles walked together. Mode byte +0xec:
0 the description's rotations (axis-angle, libm, a second one by PreMultiplyNormalised when the count is 2); 2, 3, 4 a model axis on to
the direction flattened to z = 0, then a tilt on to the whole direction; 7, 8, 9 the first half alone; any other kind leaves at the first
particle without the +0x140 bump (`// ORIGINAL-BUG:`). Mode 3's byte test (0x107d178) is 1 in the image with no writer, so it always tilts.
Then position and rotation composed with the effect's world and the inverse, slot 63, and up to seven parameters: each goes only when
+0x88.. is not negative and below the child's list length from slot 14, in order colour (0x09f5fdbc), alpha and size (0x09f5fdb0), then
four 0x09f5fdc0 events through slot 7. `FindKey` is the x87 trick in the form this function needs, so `CountUp` stayed Emit's. Colour keys
read past the list when age > life or t is NaN with more than one key: the game's own read, kept; tests stay on one key there.

**The fake scene (tests/ts_child_nodes.h, namespace childnodes).** Built for E9 too. `ChildNode` extends `fxtest::FakeNode`; `ChildNodes`
holds a pool of 8192, a table copied from `gFakeNodeVT`, so make it after the test's overrides of that table, and `Link(order, end)` sets
the sibling list. Slots 7, 14, 38, 42, 49, 54, 63 log to `gEv` with float bits; `gParameterSets`, `gTransformSets` count for floors. Traps
for the next user: `EffectsWorld` holds its own stand-in over the director 0x1ae18d, so a test's stand-in made before it is silently
replaced: point `w.com.vt` at a table instead (`fr::FrameworkMake` in tests/ts_particles_effect.cpp: child nodes for class 0x65245517,
the world's pool for the rest). The real tree's AddChild links at the head, so `TreeAddChild` inserts at the front under
`gChildNodesLinked`.

### cTSMetaParticlesEffect (lane WZ, row E7; the recipes of E8 and E9)

**Structure.** new 0x1f0; vtable 0xe91284 (14 slots), the +4 refcount face 0xe912c0 (5 slots). Create 0xc03910 (only caller 0xbefb70,
the description's slot 1), constructor 0xbfd4f0, destructor 0xbfd840, deleting 0xbfdad0, its +4 thunk 0xbfdac6. The slots are the
sibling's: 0 Attach, 1 Detach, 2 Start, 3 Stop, 4 IsRunning (shared), 5 the step 0xbfe210, 6 Update, 7 HandleEvent, 8 Query, 9
SetFlagged, 10 (the follower), 11 (a new description), 12/13 shared refcount. Standalone shape: only Create is hooked, a live effect
carries our vtables; the helpers the game's remaining code calls are hooked one by one (src/hook/tables/cTSMetaParticlesEffect.cpp).
Layout, with every field's offset: src/ts/cTSMetaParticlesEffect.h, with cTSMetaParticle (0x74, its own pool) and cTSMetaWalk (0x14:
one pair in the effect, one in each particle). The struct and the names are in Ghidra (names of what is left tentative).

Filing: the whole region 0xbfd4a0..0xc05090 (51 functions, 31,265 B) is the class's. Outside it the class owns only 0xbdce90 (314),
0xbdcfd0 (309) and 0xbdd110 (468): free `cdecl(dir *, float out[9])` bases from a direction, used only by 0xc018f0 for mode d+0xec 2 /
3 / 4. The other 3.7 KB of the 36 KB once filed here are library rows or shared code, which keep their groups: 0x2b7d50 the COM cast,
0xbb5620 Evaluate, 0xbbcd80 RandomInBox, 0xbb9840 Force, 0xbde650, 0xbde5e0, 0xbddf10, 0x4dca10 (folded `shared_vf`, the attractor's
strength, not hookable), 0x18c0c0, 0x1be3b0 RZMemory::Malloc, the string compares and appends, the vector and list instances.
Shared with the sibling by reuse, not by copy: the attractor and collider states, the box, the path point, `fx::SweepColliders`.

**State after E7 (11.4 KB of 31.3 KB).** Every slot but 5, Prepare, the walks, the pool and the bookkeeping helpers are ours, with Create
hooked: slot 5 forwards with OriginalFunction, and Emit / NewParticle call the three emitters and 0xc006f0 the same way, all on our
object. Meta_Effect_Frame runs the game's step and emitters on our object for 32 frames (the per-particle draw tick 0xc03ea0 stood in
on both sides; tests/ts_child_nodes.h is the node world it needs), so what E8 and E9 write is running there today.

**Fixtures.** tests/ts_meta_particles_world.h, on top of the effects world: `MetaWorld` (Overrides plus the sprintf and malloc stand-ins,
over an arena), `ResetWorld(seed, w)` (seeds, cells, fakes), the fakes gSource, gSurface,
gDescriptionFace, gColliders[4], gEffectFakes[6] with their tables, `RandomDescription(d, r, tame, low, high)`, `MakeEffect(mode, d)`
(mode 0 the game's Create, 1 ours; the words the constructor leaves unset get the same filler), `FillPool`, `TakeParticle`, `Fingerprint(b)` (every field that has
been set, with its particles and spawned effects; `PushEffectState`, `PushParticle`, `PushPool`; `kOffsetMarkers = true` puts the
offset in a report's marker), `Precision` (x87 single), `Drain`. In tests/ts_meta_particles_effect.cpp `Step` is 26 ops over the slots
and helpers (add a case per new helper), and Meta_Effect_Frame is the end-to-end test. A fake's unfilled slot takes no arguments:
a game call with arguments through one unbalances the stack and shows up as a jump to a float-looking address (slot 13 and surface
slot 5 did this). What it took to get Frame running: a test calls slot 5 on a running effect only (`mRunning`): the game's emitter
otherwise ran the 2 MB arena dry with new particles on the first frame of a seed, on the game's own object: a fixture's fault, not ours.

**Order: callees first.** Call edges inside what is left: 0xc04840 -> 0xc02f30, 0xc04a00 -> 0xbfe480 (-> 0xbff400), 0xbfed50; 0xc006f0 ->
0xbff8d0; 0xbfdcb0, 0xc01310, 0xbfe210 -> 0xc04840; 0xbfe210 -> 0xc03ea0 -> 0xc018f0 -> 0xc017e0, 0xbdce90, 0xbdcfd0, 0xbdd110.

**Recipe E8, the particle** (12.4 KB, Sonnet; the hard one of the two). Each hooked at its RVA as it lands (game code still calls it),
tested alone on our object on a filled particle, like the sibling's E5 recipe, whose integrators are the same family (read
src/ts/ for the forms; don't assume the constants). Then a Frame run, which now goes through ours.

| step | RVA | B | what | hard |
|---|---|---:|---|---|
| 1 | 0xbff400 | 156 | rotate a vector by an EffectsTransform | no; DONE lane XA (verified) |
| 2 | 0xbfed50 | 1707 | (particle, step): steers along the path: finds the segment by age in mPath, or the description's at d+0x148 when empty, a four-at-a-time search the compiler unrolled (one loop) | yes: float order, x87; DONE lane XA (verified; SSE only, no x87, the NaN rule of the segment search matters) |
| 3 | 0xc03970 | 829 | (suffix, place): spawns the effect `name + suffix` when mVisible; the name in a function-local static string (0x107ef18), first use guarded (keep the static, the string compare of the empty case is observable only as allocation) | no; DONE lane XA (verified) |
| 4 | 0xbfe480 | 2251 | surface collide, returns a byte: the surface's slots 5 (`__fastcall(n, 0, x, y)`, x87 return, height) and 6 (out vector, x, y), 0xbc1010 (ours or the sibling's), 0xc03970 with the surface suffixes, atan2; flag 0x1000000 | yes: the largest branchy one; surface slot 6 needs a real fake (slot 5 has one); DONE lane XA (verified) |
| 5 | 0xc02f30 | 1717 | the path: position by age, the same search; returns a float | yes; DONE lane XA (verified; the dt <= 1e-6 test is not taken on NaN) |
| 6 | 0xc04a00 | 1676 | substeps: the particle's walks (0xc03cb0, 0xbfdb00; descriptions at d+0x190, d+0x1ac), attractor forces (the sibling's), the curve, steps 2 and 4, CollideColliders (tests the low byte) | medium; DONE lane XA (verified) |
| 7 | 0xc04840 | 435 | the integrate dispatch | no; DONE lane XA (verified; calls our 0xc04a00 for the sub-stepped path) |
| 8 | 0xbff8d0 | 601 | snap to the surface (surface slot 5 at (x, y), flag 0x10000) | no; DONE lane XA (verified) |
| 9 | 0xc006f0 | 3088 | InitParticle: slot 13 event 0xd with mFirstFrame when >= a constant; name override, a new string through slot 14 key 0; slots 9, 10, 8, 3; walks (0xbfffb0) | yes: 12 callees, the longest; remove the 2 original_calls after it; DONE lane XA (verified, with the ellipsoid helper 0xbdd5e0) |

**Recipe E9, the draw tick, emitters, the step** (8.5 KB, Sonnet, after E8).

| step | RVA | B | what | hard |
|---|---|---:|---|---|
| 1 | 0xbdce90, 0xbdcfd0, 0xbdd110 | 1091 | the three bases from a direction: alone, both x87 precisions | no |
| 2 | 0xc017e0 | 272 | RotateZ | no |
| 3 | 0xc018f0 | 1924 | BuildParticlePlace by mode d+0xec | yes: mode d+0xec below 2 or over 4 skips the bases; atan2 and a -999 sentinel in mHeading with a 0.5 band; d+0xf8 lists |
| 4 | 0xc03ea0 | 2360 | per-particle tick: 3, then slots +0xac, +0x23c of a world object, then the particle effect's slots 13, 12, 9, 10 | yes: drop Frame's stand-in here, a new fake for those two slots |
| 5 | 0xc01590 | 343 | emit once | no |
| 6 | 0xc01310 | 638 | burst | no |
| 7 | 0xbfdcb0 | 1236 | continuous: mRateScale × mEvent101[0], flags 0x80000 / 0x100000 / 0x200000 scale by +0x50 power; ticks <= 1/15 s; mEmitClock, mEmitted, mEmitRemainder, mDelay on pause; list node 0xc via operator new; slot 3 Stop when d+0x24 count is reached unless flag 4 | medium: the do / while over what remains of the step |
| 8 | 0xbfe210 | 623 | the step: `stats` at param2 +0x18; collider slot 4 Place with (desc, local, world, state) once a frame; TickWalks, Emit, PruneSpawned, particles; makes mNode | no once 1..7 are in; then drop the forward and Create's `original_calls`; `in_effect` callers, no hook |

**E9 done (lane XE, 2026-11-05).** src/ts/cTSMetaParticlesPlace.cpp (bases, RotateZ, BuildParticlePlace, TickParticles),
src/ts/cTSMetaParticlesEmit.cpp (the three emitters and AddToScene, slot 5). Tests: tests/ts_meta_particles_place.cpp (Meta_Place_Basis,
RotateZ, Place, Tick, Emit; both x87 precisions) and Meta_Effect_Frame / Meta_Effect_Slots, where the emitters, the tick and slot 5 now
run for real on both sides (Slots tames the emission fields: `TameEmission`). The fakes: the owner's world object (owner slot 43, then
its slot 143 which writes three floats) and the particle effect's slot 12 (colour). Facts and original bugs found:
- The place leaves byte +1 of the transform (`pad[0]`) as the stack had it: tests mask it (`0xffff00ff` on word 0). When neither bloom byte
  is set the tick's bloom A..D are never initialised (and never read then).
- With no colours the draw tick looks up the key of -t and reads the vector far out of range (a crash in the test): the fakes give at
  least one colour; ours draws nothing. A key past the last colour reads past the vector too, so the tick test keeps age/life in [0,1).
- The screw turn is added to the particle's old position rather than the centre.
- A once-only emitter whose rate count is below the live count, and the burst and continuous emitters with a negative count, loop about
  2^32 times; a NaN rate makes huge counts. Tests keep the rate at 101..120 with at most 100 live, and tame the fields.
- Emit picks Once (flag 0x2) before Burst (0x1), else continuous; flag 0x4 restartable, 0x80000 / 0x100000 / 0x200000 scale the rate.

Coverage (docs/coverage.md's scenarios): 27,199 B in 40 functions were reached; vf8 only once.

## Done (2026-10-23)

- Prototype (lane LK): cDeviceDX BeginScene (0x167850) and EndScene (0x169660), src/gz/cDeviceDX.{h,cpp}, hooked;
  tests/gz_device_dx.cpp runs them against the raw fake device, with the original flush 0x17cdf0 running on both
  sides against a random state block, failing HRESULTs reported through LogD3DError (compared as log lines).
- R0 (lane LM): the shared fake Direct3D 9 (below), and cDeviceStateDX as a whole class (src/gz/cDeviceStateDX.{h,cpp},
  0x9a1c bytes, 41 functions verified): the constructor hook 0x17c490 is the switch, the 17 virtual slots are in
  effect through their callers (trapped), the 23 non-virtual members the device and the state commands call are
  hooked. Slots 9-12 and 27-29 (Reset, the state blocks and commands, Validate: 0x17e070, 0x17cc00, 0x17fc00,
  0x17fd40, 0x181f00, 0x182220, 0x1829d0) stay game code, called from our vtable (`original_calls`). cDeviceDX's
  BeginScene now calls our Flush. Tests: tests/gz_device_state_dx.cpp (Ops, Construct, Destroy, GameSlots).
- R1 (lane NC, 2026-10-25): cDeviceDX as a whole class but CreateDevice (src/gz/cDeviceDX.{h,cpp}, 0x78 bytes, 43
  status entries verified): the constructor hook 0x167200 is the switch and Init 0x169cc0 is hooked (the graphic
  system's 0x159410 calls both directly); every slot and the two non-virtual members (destructor body 0x167340,
  HandleCanvasEvent 0x1699b0) are in effect through callers (trap `Hooks.nGZGraphic4_cDeviceDX_Construct=1`; LK's
  BeginScene / EndScene hooks are gone). Six folded bodies are entered here (AddRef 0x146a20, OnQuery 0x147ce0, the
  getters 0x1674c0, 0x1674d0, 0x169f60, 0x1694c0). The event bodies (cDeviceEvent 0x16a7e0 / 0x167e80,
  cDeviceRenderSceneEvent 0x16a7f0 / 0x167ea0) are ours inside our code but stay unentered: CreateDevice and 0x361060
  still build events with the game's vtables. Still game code, called from ours (`original_calls`): CreateDevice
  0x167ec0 (ours since R1b, below), the enumeration's constructor 0x1774a0, flags 0x178b80 and dump 0x1788f0, the resource manager's
  constructor 0x17ad50 and ReleaseAll 0x17bb80, the state dump 0x17da90, the pixel formats getter 0x18acd0, the
  template's event registry 0x16a170 and the id iterator's constructor 0x1a9942. Tests: tests/gz_device_dx_class.cpp
  (Construct, then sweeps per area: Behaviour, Getters, Settings, Lost, Release, WaitForGpu, Canvas, Clear,
  ClearTargets, Draw, ProcessVertices, DumpInfo, InitDestroy; ProcessVerticesFault ours only), tests/gz_device_dx.cpp
  (LK's), tests/gz_device_d3dfx_selftest.cpp (the fixture additions). Untested in game: the device-lost and reset
  paths (OnDeviceLost, ReleaseDevice from SetFlags / ApplySettings, the canvas recovery and its cError throw),
  ProcessVertices' structured-exception guard, WaitForGpu's sync-surface path, ApplySettings' windowed retry, Init's
  constructor-throw cleanup.
- R1b (lane NF, 2026-10-26): cDeviceDX::CreateDevice 0x167ec0 (5557 bytes) is ours, a member of our class split in
  ChooseDisplayMode, ChooseFormats, ReleaseForRecreate, BuildPresentParameters, ResetD3DDevice, CreateD3DDevice and
  TakeNewDevice; in effect through callers (its only references are ApplySettings, OnEvent and HandleCanvasEvent, ours
  and calling ours; same trap). The enumeration's interfaces it uses are src/gz/cDeviceEnum.h (below). What it does,
  in order: nothing while +0x75 is set; the adapter identifier (and the desktop mode, for the default mode) asked and
  dropped; the window owner service held; a non-default mode kept when the enumeration lists it, else the closest
  listed one at the format's bits and the settings' refresh, at 16-32 bits, or to 1280x720 (0x18b490, three tries;
  none: ReleaseDevice, false); the display format the mode's, or the desktop's (formats manager slot 5) for the
  default mode; 0x178b80 gives the device type; the colour format the settings' when supported (slot 12, usage 0),
  else the listed ARGB format with the most bits (slot 10; signed compare, first of equals); the depth format the
  settings' when supported (slot 16, usage 0xb), else 0x18ab60 over the list (slot 14) with the settings' stencil
  bits, then with none (none: ReleaseDevice, false). Then listeners hear code 0, a usable device ends its scene, its
  state is reset and flushed and its managed resources evicted, WaitForGpu, flags (failed, lost cleared; releasing
  set), window slots 38(1) and 22(1), the state's and the device's frame objects released, the manager's 0x17b710,
  window slot 37(1) for a full-screen mode. Present parameters: copy swap with flag 0x40 (one back buffer), flip with
  feature 4, else discard with the settings' multisampling clamped by 0x18b7d0; back buffers clamped to 3; device
  clip unless flag 0x80; immediate interval unless flag 0x01 without feature 3; windowed (size 0) for the default
  mode; behaviour software without capabilities 0 and 2, mixed with flag 0x04 or one of them, else hardware (pure with
  capability 7), driver management off without feature 2. An existing device is Reset (multisampling dropped, then
  extra back buffers, on failure; a full-screen device reset windowed is reset twice; lost or internal error: lost,
  given up: failed, its objects and the manager's released, the device released); without a device IDirect3D9
  (graphic service slot 25(device type)) makes one, the window's slot 39 offering the desktop fallback (slot 37(0),
  the default mode into the settings, slot 18(w, h), windowed), with the same retries; lost: OnDeviceLost. On success
  the manager reads its memory (0x17bb00), and TakeNewDevice: flags, 0x178b80 with the device, the manager's 0x17bb30,
  one swap chain object per back buffer (COM class 0x5b75b757, slot 5(device, surface)), the target provider (slot
  5(device, depth surface)), for a non-software device the event query (feature 0; D3DERR_NOTAVAILABLE not reported),
  else the sync surface, and the DXT1 probe (feature 6); the state's 0x1807b0, SetFVF(XYZ), ValidateDevice (lost:
  OnDeviceLost; internal error: cError), SetVertexDeclaration(null); a new or full-screen device is cleared
  (0xff404080) and presented once per swap chain plus once. Last: window slots 22(1), 38(0); a usable device that is
  not lost tells listeners codes 1 and 2 and reopens an open scene (true), else window slot 26 (false).
  Tests: tests/gz_device_create.cpp (Choices, Device: CreateDevice directly, 600 and 900 cases; Callers:
  ApplySettings, a resize and a canvas frame through each side's vtable). Untested in game: everything but the first
  creation at startup and a mode change (the reset and lost paths, the window fallback, the retries).
  The event bodies (0x16a7e0 / 0x167e80 / 0x16a7f0 / 0x167ea0) stay unentered: besides CreateDevice (now ours), the
  game's cDeviceEvent vtable 0xd9e36c is built by 0xcd6980 (game code; cTSSGSystem::OnEvent, ours, too), and the
  render-scene vtable 0xd9e38c by 0x361060 (game code). They can be entered once those are ours.
- R1c (lane NT, 2026-10-26): cDeviceResourceManagerDX as a whole class (src/gz/cDeviceResourceManagerDX.{h,cpp}, 22
  status entries in db/status/cDeviceResourceManagerDX.jsonl, all `verified`, in effect through callers): the 19 slots,
  the constructor 0x17ad50, the four members (ReleaseAll 0x17bb80, ReleaseNonSurvivors 0x17b710, ReleaseOnModeChange
  0x17bb30, TakeDevice 0x17bb00) and the iterator class (cDeviceResourceIterator, 0xc bytes from the pool 0xfedb58:
  Clone 0x17b620, the deleting destructor 0x17aed0). cDeviceDX::Init builds the manager with a plain constructor on
  game-new memory (the switch) and CreateDevice / ReleaseDevice call our members, so `original_calls` of Init,
  CreateDevice and ReleaseDevice lost 0x17ad50 and the four members. The set (+0x34) and the working vector (+0x28) are
  this toolchain's std::set / std::vector with the original's offsets (only our code and the test read them: nothing
  in the image does once the manager's slots are ours). ManageTextureMemory (1439 bytes) is a whole function with the
  std heap algorithms; the difftest (tests/gz_device_resource_manager.cpp, 1200 seeds x 36 operations through each
  object's own vtable; 73 mutants, all killed) compares the call log, the object after every operation, the pool pages
  and the allocation counts. What the test found: the lowering loop's last path asks GetD3DTexture twice (GetLOD, then
  SetLOD) while the finish pass asks once; the size test is a not-below branch (`comiss` then `jb`), so a NaN size
  (quality zero or less) stops the lowering instead of passing.
  Left (R1c): the shared folded bodies the vtables hold are not entries (0x178b20 QueryInterface in 61 classes,
  0x178b60 Release in 49, 0x148fb0 AddRef, 0x46390 slots 3 / 4, the iterator's IsEqual 0x17b680 / Get 0x17b670 /
  Next 0x17b700 / Prev 0x17b760, 4 bytes 0x17b050 GetQuality): our class implements them as overrides and the test calls
  them through our vtable; they stay unhooked. The other resource classes (cSurfaceDX, cTextureDX, cVertexArrayDX,
  cVertexFormat) still reach the manager through the generated `ifaces::cIDeviceResourceManager`; the class header
  declares its own interface `nGZGraphic4::cIDeviceResourceManager` with typed slots (Add and Remove take
  cIAllocDeviceResource, Erase returns the count, GetResources and GetPolicy return their structures): fold the two
  once R3's classes are ours and call the manager's slots by name.
- R4 (lane NG, 2026-10-26): cImageManipDX as a whole class (src/gz/cImageManipDX.{h,cpp}, 14 status entries, 13
  verified slots + the getter): the getter 0x16c0c0 (cGraphicSystemDX slot 20's only target; the game's object is a
  constant-initialised function-local static at 0xfecf98) is hooked and is the switch; the 13 slots and the deleting
  destructor are in effect through callers (trap `Hooks.nGZGraphic4_GetImageManip=1`; the vtable's one reference, the
  static's vtable word, is cut by the getter's hook: callers.py's static-object rule, lane NI). Still game code, called from ours (`original_calls`): D3DX
  (0x1e310 LoadSurfaceFromSurface, 0x1d57c LoadSurfaceFromMemory, 0x1e256 LoadSurfaceFromFileInMemory, 0x1d73e
  SaveSurfaceToFileA, 0x1e5ac FilterTexture, 0x1d884 ComputeNormalMap: third-party, statically linked), the clipping
  helper 0x18ce70, the memory-texture paths 0x184710 (copy / mips) and 0x183500 (normal map), and cMemBlockAsTexture's
  constructor 0x18c300, destructor 0x18c350, SetTopLevel 0x18c5b0, AddLevel 0x18c3e0. New method facts
  (db/methods/cImageManipDX.jsonl): cITexture / cICubeTexture 16-19, 25-28, cILockableSurface 19-21, 26-27,
  cIPixelFormat 3 / 6, cIGraphicSystemDX 20 / 28. Tests: tests/gz_image_manip_dx.cpp (one sweep per slot over a
  seeded world, plus DeletingDestructor and Unknown). Untested: an engine QueryInterface saying no (the original then
  passes uninitialised locals to D3DX in four slots); real D3DX and the memory-texture helpers (stood in). Smoke: the
  slots run in game when textures load (CopyTexture / GenerateMipmaps through the texture manager, SaveSurface for
  screenshots and thumbnails, ComputeNormalMap for bump maps).
- R3 part 1 (lane NJ, 2026-10-26): the surfaces and the textures as whole classes. src/gz/cSurfaceDX.{h,cpp}:
  cSurfaceDX<P> (the common part: +4 the cISurfaceDX face (Init / SetSurface / GetD3DSurface), count +8, device +0xc,
  width / height u16 at +0x10 / +0x12, IDirect3DSurface9 +0x14), cLockableSurfaceDX<P> (the lock count +0x18 and
  LockRect / Lock / Unlock over the 0x24-byte cSurfaceLock record), and the five leaves cImageSurfaceDX,
  cDepthStencilSurfaceDX, cRenderTargetDX, cLockableRenderTargetDX (0x1c / 0x18 / 0x18 / 0x1c bytes, each from its
  game pool, 0x10ca058 / 0x10ca064 / 0x10ca070 / 0x10ca07c) and cMipSurfaceDX (a texture level, 0x20 bytes from pool
  0xfec63c; the texture owns its surface: ReleaseResource does nothing, SurvivesModeChange false). src/gz/cTextureDX.{h,cpp}:
  one template cTextureBaseDX<P, Faces> for cTextureDX (0x5c bytes, 16 level slots) and cCubeTextureDX (0x19c, 6 x 16),
  with the +4 face cITextureBaseDX (GetD3DFormat, GetLevelBytes). Switch: the six creators are hooked (0x161d40,
  0x161ce0, 0x161da0, 0x161e00, 0x163600, 0x1648a0; cdecl, no arguments, count 0); every slot, destructor, base
  destructor (0x1609a0 / 0x160a00 / 0x160a60 / 0x160ac0) and the mip helpers (constructor 0x163190 / 0x160900, operator
  new 0x1631c0, Attach 0x163ff0, SetSurface 0x162c80) are in effect through callers (traps on the creators that make
  the vtable: the image surface's also rest on the texture creators, which make mips with the image base vtable); 123
  status entries (0x1a20 + 0x20f7 bytes, about 15 KB), all verified (55 mutants, 54 killed, 1 equivalent). Folded bodies shared with unrelated classes are entered as overrides, not hooked: 0x162c40 (Release,
  also cTerrain), 0x1624f0 / 0x162d10 / 0x1630d0 / 0x1628d0 (IsCreated, SetPriority, PreLoad, GetPriority: also the
  vertex arrays, R3b), 0x1630e0 (GetWidth, also the CAS entries), 0x162f00 (LRT access flags, also cLinearFogLight).
  Still game code, called from ours (`original_calls`): the pixel formats getter 0x18acd0, the surface copy
  0x1614b0 (CopyRect / Copy), and StretchRect's clip 0x18ce70 and CanStretchRect 0x162d60. Tests:
  tests/gz_dx_resources.cpp (gz_dx_Surfaces, gz_dx_Textures: 1500 seeded cases each, creators then a seeded slot
  sequence through both faces, the pools compared). Untested: the copies' insides (0x1614b0's paths through
  0x160ef0 / 0x161b20 run on both sides as game code); a device enum's P8 substitute of 0 for every texture kind.
  Smoke: every texture, render target and depth surface the game makes goes through these creators from the first
  frame (the loading screen), and locks run when the game fills textures (thumbnails, terrain, CAS).
- R3 part 2 (lane NJ, 2026-10-26): the vertex and index arrays as whole classes. src/gz/cVertexArrayDX.{h,cpp}:
  cVertexArrayDX<P, Traits> (the common part: +4 the cIVertexArrayDX face (GetArray / GetDevice / GetD3DBuffer), +8
  the kind, +0xc the lock count, +0x10 the device (not counted), +0x14 the IDirect3DVertexBuffer9 / IndexBuffer9,
  +0x18 the count), cStaticVertexArrayDX<T> (Create through the usage / pool choice 0x167020, Lock read-only when
  `write` is 0) and cDynamicVertexArrayDX<T> (+0x1c the span's offset, +0x20 its bytes, +0x24 the buffer's bytes;
  Reserve moves the span on, discarding the buffer when it would pass the end; Lock never overwrites); the leaves
  cStaticVertexComponentArrayDX / cDynamicVertexComponentArrayDX / cStaticVertexIndexArrayDX /
  cDynamicVertexIndexArrayDX (0x1c / 0x28 bytes from the pools 0x10ca160 / 0x10ca16c / 0x10ca178 / 0x10ca184).
  Switch: the creators 0x165fa0, 0x165ea0, 0x166000, 0x165f20 are hooked; every slot, the destructors and 0x167020 are
  in effect through callers (traps on the creators). 48 status entries (0x163e bytes, about 5.6 KB), all verified (43
  mutants, all killed); the surfaces' 0x162d10 / 0x1630d0 / 0x1628d0 (SetPriority, PreLoad, GetPriority, shared with
  the arrays) are now in effect through callers too. AddRef 0x165e30 is folded with 17 other references (an unhooked
  override); 0x1624f0 (IsCreated) stays one as well. Not entered: the template bases' destructors 0x165910 /
  0x1658d0 / 0x165990 / 0x165950 (only in the bases' vtables 0xd9dee8 / 0xd9dfe0 / 0xd9e0f0 / 0xd9e1e8, which only the
  leaves' destructors store: unreachable) and the faces' adjustor thunks (compiler-made, ours). No `original_calls`.
  Test: tests/gz_dx_arrays.cpp (gz_dx_Arrays, 2000 seeded cases: two arrays per case, Create with kinds 0-3, every
  slot through both faces, reserves past and exactly at the end); d3dfx gained `d3dfx::Buffer` / `MakeBuffer`
  (vertex and index buffers). Untested: kinds above 3 (0x167020's table read past its end; callers pass 0-3). Smoke:
  every mesh, the terrain, the UI's 2D quads and the sims' skinned meshes go through these arrays from the first
  frame; the dynamic arrays' reserves and discards run every frame the UI draws.
- R3 part 3 (lane NJ, 2026-10-26): the vertex formats and declarations as whole classes. src/gz/cVertexFormat.{h,cpp}:
  cVertexFormat, cVertexFormatsManager, cVertexDeclarationDX and cVertexDeclarationsManagerDX (facts in the R3b plan
  item 2 below) and nGZGraphic4::VertexUsageName 0x18dbe0 (also called by cValidateRenderShaderContext::vf8 0xac4740).
  Switch: the managers' getters 0x16db40 / 0x16e760 (function-local statics 0xfecfb4 / 0xfed22c, guards 0x10ca29c /
  0x10ca2e8) and 0x18dbe0 are hooked; every slot, the sort key 0x16d5c0 and the declarations manager's destructor body
  0x16df40 are in effect through callers. 41 status entries (0x1897 bytes, about 6.3 KB), all verified (63 mutants: 60 killed, 3 equivalent).
  Folded (unhooked overrides): 0x16db30 IsBuilding, 0x16db20 the count, 0x16e890 the declaration's Release (shared
  with the shaders), 0x16e800 IsCreated (cFreshCellPlayer), 0x16e970 GetFormat (17 other classes). 0x16db10 (the
  entry getter, shared by the two managers only) is in effect through callers. Not entered: the statics' atexit
  destructors 0xd90370 / 0xd903d0 (our statics have their own) and the STL helpers the sort and vectors use. No
  `original_calls`; ours reaches the declarations manager directly where the original asks 0x18dd70 (which caches
  the graphic system's slot 18 answer, the hooked getter). Deviation: GetElementSize answers 0 for types past 5 (the
  original reads past its six-entry size table into the formats manager's static); callers build only types 0-5.
  ORIGINAL-BUG kept: a CreateVertexDeclaration failure other than the quiet codes still makes a declaration; with no
  Direct3D object it is never unregistered and its list entry dangles after release. Tests:
  tests/gz_dx_vertex_formats.cpp (gz_dx_VertexFormats: recipes built in shuffled orders, every format slot, Describe,
  BeginFormat's drops, the destructor with witnesses; gz_dx_VertexDeclarations: GetDeclaration with a scripted
  CreateVertexDeclaration, the declarations' slots, releases, the rename table, the pool); d3dfx gained
  `d3dfx::Declaration` / `MakeDeclaration` and the slot constants `d3dfx::decl`. Smoke: every mesh's format is built
  through the formats manager and its declaration made on first draw (loading screen onwards); a device reset releases
  and remakes the declarations.
- R3 part 4 (lane NR, 2026-10-26): the shader programs as whole classes. src/gz/cShaderProgramsDX.{h,cpp}:
  cShaderProgramsManagerDX (vtable 0xd9f108, 0x58 bytes: +4 six fragment linkers by adapter ordinal, +0x1c / +0x28 the
  vertex / pixel programs sorted by key, not counted, +0x34 the gathered fragment buffers, +0x40 a std::set of counted
  fragment names ordered by length then text, +0x48 the counted names to link, +0x54 "linking"), cVertexShaderDX /
  cPixelShaderDX (0xd9f054 / 0xd9f098, 0x28 bytes from the pools 0x10ca2fc / 0x10ca308: +4 count, +8 device, +0xc the
  Direct3D shader, +0x10 the D3DX function buffer, +0x14 counted fragment names, +0x20 the info {kind, major, minor,
  software, hardware}) and cShaderProgramInputsDX (0xd9f0dc, 0x20 bytes, game operator new: constants as 0x44-byte
  records and samplers as 100-byte records, both sorted by strcmp). Switch: the getter 0x1758c0 (static 0xfed650,
  guard 0x10ca368; the graphic system's slot 19 jumps to it and 0x18ddf0 caches it in 0x10caa3c) is hooked; every slot,
  CreateProgram 0x176100, the key builders 0x1738a0 / 0x174920 / 0x1749b0, the order 0x1747d0, the inputs' constructor
  0x173950 and the destructor body 0x1745d0 are in effect through callers. 39 status entries (0x2fee bytes, about 12 KB), all
  verified (mutants in notes/feedback/NR.md). Folded (unhooked overrides): 0x153f50 (the inputs' Release, 7 classes), 0x175a90 (the
  programs' IsCreated, 3 classes); 0x176d00 / 0x176d10 (GetFunction / GetFunctionSize) are shared by the two programs
  only and in effect through callers; 0x1758a0 / 0x1758b0 are under 5 bytes (no entry). Not entered: 0x16f520..0x173950's
  other 35 functions are STL instantiations (equal_range 0x1730c0 / 0x1732e0, vector growth 0x16f200 / 0x158c50, sort,
  the tree's insert and erase 0x16f520, which releases each name) and compiler-made record moves (0x1738e0, 0x173840,
  0x173720, 0x173540): lane WJ (RL) matched most of them (see "RL result"); the rest stay unentered. `original_calls`: the D3DX functions only
  (0x1e89e Assemble, 0x1e981 Compile, 0x22220 GatherFragments, 0x28c0c CreateFragmentLinker, 0x2168f Disassemble,
  0x1eca8 GetShaderConstantTable). Behaviour worth knowing: a program is shared by key (device, fragment count,
  software, hardware, then the function's bytes or, for linked ones, the names' addresses); LinkProgram looks for a
  pixel program, then a hardware vertex one, then a software one, else makes the adapter's linker (adding every
  gathered buffer) and links into a profile it prints with `_snprintf(buf, 7, ...)` into eight unzeroed bytes
  (ORIGINAL-BUG: "vs_2_sw" and "ps_3_sw" end on stack garbage; ours ends them); FindConstant's "none" carries three
  uninitialised words (registerSet, type, shape). Test: tests/gz_dx_shaders.cpp (gz_dx_Shaders, 1200 seeded cases
  of 40 steps plus the getter; D3DX stands in at its RVAs with synthetic token streams). Untested: Dump into a null
  stream (the original makes a cRZTextODSStream and writes to OutputDebugStringA, a harness trap). Smoke: every
  shader program the game makes comes from this manager (material and effect setup, from the loading screen on); the
  programs are registered with the device's resource manager (slot 5 Insert), which releases them on a device reset.
- **R3b plan** (left by lane NJ): the rest of R3, in this order.
  1. Done (part 2 above).
  2. Done (part 3 above). Facts: the formats manager is a static object 0xfecfb4 (+4 a counted vector of
     formats, entry 0 always empty; +0x10 the builder format; +0x14 "building") made by the getter 0x16db40, reached
     only from cGraphicSystemDX slot 17 (0x15a8f0, a jmp); slots 3 BeginFormat (drops formats only the list holds),
     4 AddElement(stream, type, usage, index), 5 EndFormat(out) (sorts each stream by the key 0x16d5c0, renumbers,
     answers an equal format or makes one), 6 IsBuilding, 7 / 8 count and entry (0x16db20 / 0x16db10, shared with the
     declarations manager only). cVertexFormat (vtable 0xd9e8e0, 0x18 bytes, game operator new): +8 its id, +0xc a
     vector of streams, each a vector of 16-byte elements {usage, index byte, type, stream byte, position byte};
     slots 3-16 are getters / finders / sizes (the type sizes table 0xfecf9c: 4, 8, 12, 16, 4, 4) and 16 Describe
     (text through cIGZString slots 26 / 19 / 20, names from 0x18dbe0). The declarations manager is the static
     0xfed22c (+4 a vector of declarations, not counted; +0x10 a scratch vector of D3DVERTEXELEMENT9) made by the
     getter 0x16e760 (from cGraphicSystemDX slot 18, 0x15a8e0); slot 3 GetDeclaration(device, format, out) finds by
     (device, format) or converts the format (usage ids to D3DDECLUSAGE with index offsets, types to D3DDECLTYPE) and
     calls CreateVertexDeclaration (quiet codes as the arrays'; LogD3DError file 0xd9ea18, line 0x1b5), registers
     the new cVertexDeclarationDX (vtable 0xd9e9b0, 0x58 bytes, pool 0xfed220) through the resource manager's slot 5
     (Insert; slot 7 Erase unregisters); slot 6 answers a null-terminated table of shader semantic renames
     (0xfed248). The declaration's ReleaseResource / destructor empty their manager entry through 0x18dd70 (the
     manager as the graphic system answers it, cached in 0x10caa28).
  3. Done (part 4 above).
  4. Done (lane OS, 2026-10-28; db/status/cSurfaceCopy.jsonl, 5 entries, 3.6 KB, all verified, in effect through
     callers). src/gz/cSurfaceCopy.{h,cpp}: ClipCopyRects 0x18ce70, CanStretchRectDX 0x162d60,
     CopySurfaceRectLocked 0x160ef0, MakeSystemMemorySurfaceLike 0x161b20, CopySurfaceRectDX 0x1614b0; tests/gz_surface_copy.cpp
     (gz_SurfaceCopy_Clip 40000 rectangle sets, _CanStretch 2000 x 3 calls, _Locked 3000, _Twin 3000, _Copy 6000
     worlds). cSurfaceDX's CopyRect / Copy / StretchRect, cDraw2D (DrawImage), cImageManipDX (the two copy paths) and
     cDeviceEnumDX::CanStretchRect call ours now; their `original_calls` lost 0x1614b0 / 0x162d60 / 0x18ce70. Facts:
     - 0x1614b0 (source face, sourceRect, target face, point), all cdecl: the faces' QueryInterface 0x4a5a7223 hands the
       uncounted Direct3D surface; the early "nothing to copy" tests are signed on the source size and the rectangle
       (past the near or far edge); the target is cut only at its far edges (the point is not checked against the near
       ones); the render-target branch (source usage bit 0) tries GetRenderTargetData (target pool 2, same size, whole
       rectangle, point 0,0, after the device's WaitForGpu), StretchRect (target pool 0 and CanStretchRectDX), fails a
       multisampled target, then a locked copy (source's cISurface access flag bit 0) or a system-memory twin of the
       source filled by GetRenderTargetData; the other sources go by (source pool << 16 | target pool): 0 StretchRect
       (the scene ended and begun again around it for a depth-stencil source while the device is in a scene), 1-3 /
       0x10001-3 / 0x20001-3 / 0x30001-3 a locked copy, 0x20000 UpdateSurface, 0x10000 and 0x30000 a locked copy when the
       target is lockable, else a twin of the TARGET and UpdateSurface.
     - 0x162d60: false for the same surface or a non-default pool; the kind of each (depth stencil 2, else render target
       4, plus 1 for a texture level) indexes the table at 0xd9db08 for the device enumeration's slot 19; the filter is 0,
       or 1 (point caps 0x1000100) without `wantFilter`, or 2 (linear caps 0x2000200) with it.
     - 0x160ef0: refuses a default-pool, non-dynamic surface with a container; locks the source read-only, the target
       no-syslock; rows = height / block height (at least 1), row bytes = bits per pixel x width / 8 (unsigned).
     - 0x161b20: GetContainer answering exactly 0 (S_FALSE counts as none) picks by the container's GetType (3 texture,
       5 cube, else fail); the container is released before the device.
     - 0x18ce70: single-precision proportional cut as in the header comment; the fail tests pass a NaN (comiss + jae).
     Original calls left: the formats getter 0x18acd0 in 0x162d60 and 0x160ef0 (the pixel formats manager's static).
  mutants 110 + 78, 183 killed, 5 argued equivalent in the entries). The service's
  creator 0x14f2b0 (the Draw2D COM director's class 0x4c9282f8) is hooked and is the switch: only the service's Init
  0x150210 makes a cDraw2D (the one caller of 0x14ba80), so the game's vtables 0xd9c3c0 / 0xd9c474 / 0xd9c504 / 0xd9c518
  and the base's 0xe4ec84 are never stored, and every slot, the constructors, destructors and helpers are in effect
  through that hook. The rectangle intersection 0x150e20 has callers all over the image and is hooked as itself. Layout:
  the primary face cIDraw2D on our cAbstractBehaviorTypeST (+0), the engine base at +8 (0xf8 bytes; its slots 25-34 are
  cDraw2D's overrides), the engine factory at +0x100; the device and state at +0x104/+0x108, the blocks, the vertex
  format, shader, declaration and dynamic arrays, the material +0x150, three atlas pools +0x154, records +0x19c, free
  records +0x1a8, the clip stack +0x1b4. cDraw2DBufferHardwareCache (0xd9c558) is neither made by the constructor nor a
  member: only the engine face's SetTextureSource 0x14f970 stores its vtable (ours since lane OL, below). Tests: tests/gz_draw_render_to_hardware.cpp (the base on its own: a recording class
  for slots 25-34 on both sides) and tests/gz_draw2d.cpp (GZDraw2D_Slots: 1500 seeded objects, 90 calls each through the
  three faces, every word but the vtables compared after each call; _Intersect; GZDraw2DService_Create / _Run). The
  device objects are drawfx fakes at the cIDeviceDX / cIDeviceState level, not d3dfx under our cDeviceDX: the device
  objects come from CreateDeviceObjects 0x150330 (game code) and the render-state commands go to the state cache's
  ApplyStateCommand (game code), so the command bytes are the observable.
  **Done since (lane OL, 2026-10-28, step 1 of the plan below)**: the atlas and record code is ours (src/gz/cDraw2D.cpp:
  slots 25 0x14d2e0 DrawImage, 26 0x14d8e0 DrawRecord, 29 0x14eb10 CreateRecord, 30 0x1522d0 SetRecordPixels, 31 0x14e1e0
  RebuildAtlases, the engine face's 30 0x14f970 SetTextureSource, ReleaseRecordCells 0x14eca0, AllocateRecord 0x14c960,
  PlaceRecord 0x14ca90; the packer was cTextureAtlas, lane OL earlier), and the cache class is a real class
  (src/gz/cDraw2DBufferHardwareCache.{h,cpp}: vtable 0xd9c558, 0x20 bytes from the pool 0xfeb8e0, the record handle
  cDraw2DRecordRef 0x18bf60 / 0x18bfc0 / 0x18c090 / 0x18bf90). 9 + 14 new status entries, all verified, in effect through
  callers on the same switch (the hooked creator 0x14f2b0; only our SetTextureSource makes a cache). Tests: the recorders
  over these RVAs are gone from tests/gz_draw2d.cpp; GZDraw2D_Slots runs them for real against fakes of the COM objects
  they make and use (the class factory's textures and buffers, the device enum, the formats, the image manipulator, the
  draw system's watch list; the cache pool's page is a seeded page of the test's) and calls the cache's slots on the
  buffers' caches; tests/gz_draw2d_cache.cpp checks the four record-handle members on their own (400 sequences).
  Mutants: 86, 84 killed, 2 argued equivalent (in the entries). Still game code, called from ours: UploadRecord 0x152410,
  AdjustTextureSize 0x18c990, the formats manager 0x18acd0, the image manipulator 0x18d120, the memory-block surface
  0x18d1a0 / 0x18d2c0, ClipCopyRects 0x18ce70, GetDraw2D 0x18c000, the pixel format by id 0x18a7f0, the current device
  0x18ae40 and the registry 0x1516f0. The cache's slot 2 (0x151900, 21 bytes) is shared with
  cTSPersistSpeculativeLoader and cTSObjectDialog, slots 7, 12 and AddRef are shared bodies hooked elsewhere: none is an
  entry here (our class has its own).
  **Done since (lane OU, 2026-10-28, step 2 of the plan below)**: CreateDeviceObjects 0x150330 (2776 bytes) is ours
  (src/gz/cDraw2D.cpp; the members it fills are now typed: Ref<cIDeviceResource> blocks, cIVertexFormat, cIShaderProgramDX,
  cIVertexDeclarationDX). Its callers, the constructor and OnEvent, are ours, so it is in effect through callers on the
  same switch (one new trap, `Hooks.cDraw2DSystemService_Create=1`). The vertex shader is our own vs_1_1 text (user decision
  2026-10-28), not the game's; the test judges both by what they do (tests/gz_draw2d_device_fx.h: a small vs_1_1 assembler
  and interpreter; the digest is the declared inputs and the outputs for six input sets, with write masks). `Install()`
  is gone from tests/gz_draw2d.cpp: the original runs on one side and ours on the other against fakes of the
  settings, the three cached service getters (patched: 0x18dcf0 formats, 0x18ddf0 shader compiler, 0x18dd70
  declarations; named in Ghidra), the state cache's CreateStateBlock (a fake per block), the arrays' create state, and
  the state commands for texture addressing (0x1c) and mip filter (0x1f) are logged byte for byte. Mutants: 36, all killed
  (two first-run survivors were test gaps, closed). Still game code, called from ours: the current device 0x18ae40 and
  the three getters above. Nothing about cStateBlockDX (see 2b).
  **Plan for the rest of cDraw2D** (game code our class still calls, each listed in `original_calls`):
  1. (done, above.)
  2. (done, below: lane OU.)
  2b. cStateBlockDX (nGZGraphic4, vtable 0xd9fb74, 12 slots) and the state-block slots of cDeviceStateDX (lane OV,
     2026-10-28; src/gz/cStateBlockDX.{h,cpp}, cDeviceStateDXBlocks.cpp, cDeviceStateDXReset.cpp, cDeviceStateDXCommands.cpp).
     Done:
     - cStateBlockDX as a whole class (0x18 bytes: vtable, count, owner, the record as a vector; a fixed pool at 0xfee35c):
       QueryInterface (ids 1, 0xc9714531, 0xda02e09f), ReleaseResource, GetDevice / GetDeviceConst, IsCreated, GetMemorySize,
       SurvivesReset / SurvivesModeChange, GetGroups, the deleting destructor, the list lookup 0x17bdc0 (a std::equal_range
       over the cache's list by record size, then bytes) and removal 0x17dda0, the record release 0x17dcd0. In effect through
       callers: the block's vtable is stored only by CreateStateBlock (callers.py 2026-10-28).
     - Slot 10 CreateStateBlock (0x17cc00) with StateDelta::Build / Serialize (0x17d510, 0x182d90): flush, delta against the
       defaults, the record, a block of the cache's list shared when an equal one exists, else registered with the device's
       resource manager. The record is an 8-byte header, section A (the groups' differences), section B (declaration, streams,
       indices), with the pointers Serialize also takes a reference on. Slot 11 ApplyStateBlock (0x17fc00) with
       ApplyRecordStates / ApplyRecordStreams / RestoreChangedStates / RestoreChangedStreams (0x17f180, 0x17ee30, 0x181820,
       0x1816d0). Slot 12 ApplyStateCommand with its 33 helpers (lane OV, first half). Slot 9 Reset (0x17e070).
       Slots 10 / 11 were retyped (the out-argument as a word, void) and slot 9 became void: callers ignore the result.
     - Tested by cDeviceStateDX_Blocks (4000 cases: sequences of block steps with the cache's list, the held blocks, the
       resource manager's Insert / Erase and the pool's rings compared) and, for Reset, as an operation of cDeviceStateDX_Ops
       (a limits fake for the device enumeration and a creation-parameters handler were added to its fakes).
     Plan for what is left (cDeviceStateDX slots 27, 28, 29, shader-validation debug drivers, game code in the original
     vtable; they run only when the engine's validation switch is on, so a smoke run never reaches them):
     - Slot 27 (0x181f00, (a, b)): validates a ProcessVertices-style call against the cache and the limits; logs through the
       director framework object (service 0x3bffec52, the QueryServiceValue / cRZStd logging pattern of LogD3DError). Needs
       the framework fake of tests/d3dfx.h (ErrorLog) and the enumerator's GetLimit fake added here.
     - Slot 28 Validate (0x182220, (report); 1826 bytes, Ghidra's 55 are only the prologue up to its jump table): Flush(0x7f), then the
       enumerator's slot 24, then a jump-table loop over the validators (one per state kind) that appends to the report.
     - Slot 29 (0x1829d0, (a, b, c)): 12 non-trivial validators of a shader pair against the declaration and the streams.
     Each needs its validators decoded first (docs/lane/understand.md); take the three as one group because they share the
     logging helpers, then the forwarders in cDeviceStateDX.cpp go.
  3. DONE (lane OZ, 2026-10-28): the cDraw2D registry 0x1516f0, pixel format lookup 0x18a7f0 and current device
     0x18ae40 are ours (see Done below). The rest of it is DONE too (lane PD, 2026-10-28; see Done below): the other four
     behaviour registries (cCanvasW32, cGZCanvas, cGZGraphicSystem, cGraphicSystemDX), the base registry 0x1485f0 and the
     base-event registration 0x1464f0 (with its halves 0x1465d0 / 0x146580), the cached service getters (already ours,
     verified, in src/gz/GraphicServices.h before this lane), cGZFontSys::DetectKind 0x910b35.
  The template's own vtable 0xd9c30c (stored only by its 7-byte destructor 0x14be20, reached only from an unwind
  funclet of the original constructor) is handled per slot in `callers_handled` (the cDeviceDX precedent); its
  template-only OnEvent 0x1500a0 and the destructor 0x14be20 have no entry (see the pitfalls below).
- R5 (lane NM, 2026-10-26): cGZDrawContext as a whole class (src/gz/cGZDrawContext.{h,cpp}, 0xdc bytes, 54 status
  entries verified, 48 of 49 slots; GetEngine 0x8bb7d0 is 4 bytes): the constructor 0x8baff0 (cGZDrawSystem slot 5's,
  0x8c0130) and the COM director's creator 0x8bf7d0 are hooked and are the switch; the slots, the destructor 0x8bb120,
  FlushState 0x8bbef0 and DrawKind6 0x8befb0 are in effect through callers (trap `Hooks.cGZDrawContext_Construct=1&
  Hooks.cGZDrawContext_Create=1`). Dispatch 0x8bc040 is hooked too: the game's primitive builders 0x8bc290, 0x8be3d0
  and 0x8bea10 call it back. Still game code, called from ours (`original_calls`): the builders (kind 0 0x8bc290, 1
  0x8bc5f0, 2 0x8bd430 / 0x8bd660, 3 route 2 0x8be790, 4 0x8bdc50 / 0x8be3d0, 5 0x8bea10 / 0x8bec30, 6 route 2
  0x8beff0, 7 and kind 3 route 1 0x8bf130, 8 0x8bf480) and the rectangle intersection 0x373520. Tests:
  tests/gz_draw_context.cpp (Slots: 1500 cases of 40 random slot calls each, every field and vector, every fake call;
  Construct). Original bugs met (left out of the test's random cases, cited there): 0x8bc290's route 1 writes past
  its vector from 2 points on; 0x8bea10 then 0x8bf480 reads freed memory; 0x8bf480's route 1 throws below 2 points;
  0x8bd430 throws on line widths near 1e9. Smoke: every UI draw goes through the context (windows, text, the
  loading screen). Since cGZDrawSystem landed (below), the constructor's only caller is ours, so these entries'
  trap is `Hooks.cGZDrawContext_Create=1&Hooks.cGZDrawSystem_Create=1` (callers.py's dependency set).
- R5 (lane NM, 2026-10-26): cGZDrawContextAsBuffer as a whole class (src/gz/cGZDrawContextAsBuffer.{h,cpp}, 0x58 bytes
  from the pool 0x105f820, 53 entries verified plus the pool allocator 0x8c1460): a cIGZBuffer (+0), cIGZShader (+4),
  count (+8) and cIGZDrawContextAsBuffer (+0x10) drawing view of a buffer through a cGZDrawContext. The creator 0x8c22a0
  and constructor 0x8c13a0 are hooked (the switch); DrawEllipse 0x8c1e50 is hooked too (a game caller). Trap
  `Hooks.cGZDrawContextAsBuffer_Create=1&Hooks.cGZDrawSystem_Create=1`. Lock and GetPixel go through the context's
  LockSurface / UnlockSurface (slots 47 / 48, engine slots 20 / 21). Test tests/gz_draw_context_as_buffer.cpp (4000
  seeds, every face and pixel format, the pool page shared by both sides).
- R5 (lane NM, 2026-10-26): cGZDrawSystem as a whole class (src/gz/cGZDrawSystem.{h,cpp}, 100 bytes, 45 entries, plus
  its engine factory cGZDrawContextRenderToBufferFactory, 12 bytes): the draw service 0xeb2ea3cb (cIGZDrawSystem, iid
  0x4b2ea3b3, 21 slots named in src/gz/cIGZDrawContextEngine.h). It keeps the engine factories (newest asked first;
  Init's own render-to-buffer factory at the back), makes drawable buffers (slot 5: an AsBuffer over a new graphics
  buffer, colour formats 2 = 8 bits, 3-7 = 16, 8-9 = 32), converts pixels (slot 6, the converter 0x8c6b10, ours since lane NW
  below) and trims memory: every fifth tick, hardware caches (cDraw2DBufferHardwareCache) unused for five ticks unload,
  and while the ready transient buffers hold more than the budget (1000000 bytes at first), the ones unchanged longest
  commit first, weighted sqrt(idle ticks) * bytes. The creator 0x8c0920 and constructor 0x8bfdb0 are hooked; the
  constructor has no reference, so the trap is `Hooks.cGZDrawSystem_Create=1`. Still game code, called from ours:
  cGZDrawContextRenderToBuffer's constructor 0x8c7dc0 (the factory's CreateEngine).
  Test tests/gz_draw_system.cpp (Slots: 1500 seeds of 90 random steps over fakes of the framework, graphics system,
  buffers, transient buffers, caches and factories, every field, vector and weight compared; Construct). Smoke: the
  draw system is created at startup and ticks every frame; Trim runs every fifth frame (UI textures).
- R5 (lane NM, 2026-10-26): cGZDrawFontRenderer as a whole class (src/gz/cGZDrawFontRenderer.{h,cpp}, 0x458 bytes, 20
  entries: 11 of 11 slots, the creator, constructor and destructor, BuildAtlas 0x8c2d10, Reset 0x8c2f10, AddGlyph
  0x8c2f80 and three map helpers), with cIGZFontRenderer (iid 0xa8bc2a75, 8 slots) and the glyph (cGZFontGlyph, 0x24
  bytes) in src/gz/cIGZFontRenderer.h. BeginDraw picks a route per target: a drawable buffer (cIGZDrawContextAsBuffer)
  whose context cannot lock its surface (a hardware context) is drawn through the context from a glyph atlas (a 256 x
  256 buffer, format 7 / 16 bits, from COM class 0xc470d325; glyphs packed in rows as first met, keyed by font and
  character; each glyph a SetTexture + FillRect, colour from the graphics system's ConvertNativeToRGB, slot 36);
  anything else goes to the buffer font renderer (COM class 0x8cda30b) with the glyphs moved by the context's origin.
  The creator 0x8c32a0, the constructor 0x8c2440 and the destructor 0x8c2530 (both referenced by nothing) are
  hooked; trap `Hooks.cGZDrawFontRenderer_Create=1`. No original calls left. Original bugs: EndDraw unlocks through an unchecked query answer (guarded), the colour table divides by
  zero for 16-bit types without alpha (4, 5; guarded, never met: the atlas is type 7), and DrawGlyphs skips the glyph
  after each placed one until its next pass (reproduced). Test tests/gz_draw_font_renderer.cpp (Slots: 1500 seeds of
  40 steps, draw sessions on both routes with runs up to 99 glyphs, every field, place, the colour table and the atlas
  pixels compared; Construct). Smoke: UI text through hardware draw contexts (every window caption and label).
- R5b step 1 (lane NS, 2026-10-26): cGZDrawContextRenderToBuffer as a whole class (src/gz/cGZDrawContextRenderToBuffer.{h,cpp},
  0xe4 bytes, 25 entries in db/status/cGZDrawContextRenderToBuffer.jsonl: 22 of 24 slots, the constructor, UpdateLock
  0x8c8b30 and FillPolygon 0x8c8c00) with the five software span renderers (src/gz/nGZDrawSoftRender.{h,cpp}, 24
  entries in db/status/nGZDrawSoftRender.jsonl: the base cSpanRenderBase (+4 / +0x10 two std::vector<TexelSpan>,
  TiledSpans 0x8c92d0, ScaledSpans 0x8c93d0, destructor 0x8c9200 and deleting destructor 0x8c92b0, every vtable's
  slot 0) and slots 1-4 (Fill, FillGradient, Blend, BlendGradient) of cSpanRender8 / 1555 / 565 / 4444 / 8888).
  The switch: the factory's CreateEngine (ours, src/gz/cGZDrawSystem.cpp) news our class, the constructor's only
  reference, so every engine and renderer carries our vtables and all of it is in effect through callers (trap
  `Hooks.cGZDrawSystem_Create=1`, as the factory). SetTarget picks the renderer by native format (2: 8-bit; 4, 6:
  1555; 5: 565; 7: 4444; 8, 9: 8888; others fail with the target released still locked, as the original). The slots
  under 5 bytes (SetLineWidth 0x8c8520, vf17 0x8c8620) and the destructor 0x8c7e10 (inline in Release, nothing
  references it) have no entry; HX's byte-matched DeletingDestructors stub for 0x8c92b0 was removed (the class is
  ours now; lane IX's precedent). The texture slot of each renderer (vf5: 0x901ba0 0x8d9930 0x8e9230 0x8c9c60
  0x8f6110, 3-4 KB each, every texture format x mode) and the scan converter 0x9cf0c0 FillPolygon calls were game code
  then: both are ours now (lane NW, below). 0x8c94d0 (the texel span vectors' growth) is STL vector code: no entry. Original bug reproduced:
  cSpanRender1555::FillGradient starts green and red unshifted while their steps are shifted (red also +0x80000000).
  Tests: tests/gz_span_render.cpp (Slots: 1500 seeds x 5 classes of random slots 1-4 on random rows with padding, CPU
  level 0 and 2; TexelSpans: the texel span lists word for word over reused vectors, the deleting destructor's heap,
  and 1000 texture draws ours-forwarding against the original's), tests/gz_draw_render_to_buffer.cpp (Slots: 3000
  seeds of 60 slot calls over buffer fakes with real pixel memory: every fake call, every object word but the vtable
  and the renderer pointer, the targets' memory after every draw, refs and heap; Construct). tests/gz_draw_system.cpp's
  engine check now asks "is it the side's cGZDrawContextRenderToBuffer" rather than comparing vtable words. Mutants:
  engine 38 (36 killed, 2 equivalent), renderers 28 (25 killed, 3 equivalent); the arguments are in the status notes.
  One of them is a fact worth knowing: every renderer's vf5 table has rows 12 and 13 (8-bit texture, without and with
  key) pointing at the same span functions, so the key never matters for 8-bit textures. Smoke:
  any software drawing into a cIGZBuffer (cGZDrawSystem::CreateBuffer's drawable buffers, whose contexts take this
  engine); which in-game screens reach it was not observed by the lane: the coordinator's run should check the host
  log's trap lines and, if possible, a screen with buffer-drawn UI.
- R5b steps 1, 2 and 3 (lane NW, 2026-10-26): the rest of the software drawing path is ours.
  - Step 1, the span renderers' texture slot vf5 (0x901ba0 cSpanRender8, 0x8d9930 1555, 0x8e9230 565, 0x8c9c60 4444,
    0x8f6110 8888; src/gz/nGZDrawSoftTexture.cpp, 5 entries in db/status/nGZDrawSoftRender.jsonl): each is the renderer's
    DrawTexture, which runs `DrawTextureSpans` instantiated once per destination format (`DestPixel::...`), replacing
    the ~180 span functions per renderer the original picks from five tables (source format x key x mode 0..14: plain,
    scaled, tiled; copy / blend / modulate / modulate+blend / premultiplied). In effect through callers (only
    cGZDrawContextRenderToBuffer::SetTarget makes renderers and ours makes ours; trap `Hooks.cGZDrawSystem_Create=1`),
    the 5 Fill entries lost their `original_calls`. Tests: tests/gz_span_texture.cpp (Cells: every renderer x texture
    format x key x mode, 256 trials a cell with every colour alpha, destination rows compared byte for byte with the
    padding, CPU level 0 and 2; Exhaustive: every 16-bit texel); mutants 25 (24 killed; `v < 0xff` to `<=` in the 8-bit
    premultiplied blend is equivalent). **The 909 table functions** (0x8c9ce0 .. 0x909e70; the tables at 0xe4f580 4444, 0xe4f948 1555, 0xe4fd08 565,
    0xe50418 8, 0xe500b8 8888, 14 row pointers over 13 rows of 15 words): adopted by lane XP ("The pixel band" above has
    the proof), through callers.py's declared-table rule, in db/status/nGZDrawSoftSpans.jsonl.
  - Step 2a, the polygon scan converter 0x9cf0c0 with 0x9cd620 and 0x9ce570 (src/gz/GZScanConvert.{h,cpp}, hooked
    through GZScanConvertAdapters::Convert; FillPolygon and the builders call `GZScanConvertPolygon`; a triangle
    routine 0x9ce1d0 of game code calls the hook). Float and double steps as the original's (slope in float, the rest
    in double, floor, truncation giving 0x80000000 on overflow). tests/gz_scan_convert.cpp: 40000 polygons of 8 kinds;
    mutants 8/8.
  - Step 2b, the twelve primitive builders and three helpers of cGZDrawContext (src/gz/cGZDrawContextBuilders.cpp, 15
    entries in db/status/cGZDrawContext.jsonl; 0x8bc290 BuildPoints, 0x8bc5f0 BuildLines, 0x8bc8f0 AppendLineQuad,
    0x8bccd0 BuildLineSpans, 0x8bd430 BuildCurveQuads, 0x8bd660 BuildCurveSpans, 0x8bdc50 / 0x8be3d0
    BuildEllipseOutlineQuads / Curves, 0x8be790 BuildRectSpans, 0x8bea10 BuildEllipsePolygons, 0x8bec30
    BuildEllipseSpans, 0x8beff0 BuildTriangleSpans, 0x8bf130 DrawQuadList, 0x8bf250 BuildQuadSpans, 0x8bf480
    BuildPolygon), members of cGZDrawContext that Dispatch and DrawKind6 call. Dispatch stays hooked at its RVA
    (cGZDrawContextAdapters::Dispatch): the builders call it back (kinds 0, 2, 4, 5), so it is the cut of that cycle
    for the in-effect check, and they carry `Hooks.cGZDrawContext_Dispatch=1`. Dead clone functions (no reference
    anywhere) call the builders: their sites are in `callers_handled`. tests/gz_draw_context.cpp: 12000 seeds of a
    recording engine fake, every kind, counts 0..8, extended point values (NaN / infinity only for the kinds that
    cannot reach the scan converter: see Pitfalls), the state's +0x50 seeded; mutants 42 (38 killed; the 4 survivors are
    equivalent or sit behind the unreachable `lineWidthRounded` branches, each explained in the status notes).
  - Step 3, the pixel converter 0x8c6b10 and its 62 loops 0x8c38e0 .. 0x8c5610 (src/gz/GZConvertPixels.{h,cpp}, a free
    function family; 63 entries in db/status/GZConvertPixels.jsonl; the loops were already in db/missed-functions.jsonl).
    `GZConvertPixels` checks the formats (4..9 only), applies the overlap rule, masks the key with
    cGZBuffer::FormatMask and picks the loop from the table `[dst - 4][src - 4][keyed]`; each loop is a template
    instance of one row loop over a per-pair pixel formula (`To<dst>From<src>`; formats 4 xRGB1555, 5 565, 6 ARGB1555, 7
    ARGB4444, 8 xRGB8888, 9 ARGB8888 by the formulas). The original gives every loop one by-value 0x58-byte argument
    block (`GZConvertArgs`), so the converter and all 62 loops are hooked at their addresses
    (src/hook/tables/GZConvertPixels.cpp, `GZConvertLoopHook<I>`); both callers (cGZDrawSystem::ConvertPixels, cGZBuffer
    BltUnclipped) are ours and call `GZConvertPixels` directly, their `original_calls` are gone. tests/gz_convert_pixels.cpp:
    Loops (every loop, 1200 rectangles per loop and CPU level 0 and 2, two buffers or one overlapping, random
    strides including negative, garbage in the block's unused words; the pair of each loop is read from the original's
    table), Exhaustive (every 16-bit source; every 24-bit colour with a hashed alpha, plus every alpha byte), Pixels
    (the converter over formats 0..11 x 0..11, key on / off, overlapping in one buffer or not: 184320 cases).
    tests/gz_buffer.cpp now stands in for the converter on both sides (shape::StandIn over the image and ours).
    Mutants 59 (55 killed; 4 equivalent: the range test `dstLow <= srcHigh` as `<`, and `srcHigh` without its `- 1` in
    both stride cases (the rule's third test, destination below source, makes them equal whenever it matters), and
    loop 0x8c5370 (6 -> 5) given 0x8c4fe0's formula (4 -> 5): the same function over every 16-bit value). Smoke for
    the coordinator: any BltUnclipped (a buffer-to-buffer blit with a format change or a transparent colour) and
    ConvertPixels; the 63 new hook names are `GZConvertPixels` and `GZConvertLoop_<rva>`. Not covered: a width or
    height of zero or less (the original's do-while loops would run 2^32 times; the callers rule it out).
- **R5b plan (lane NS, 2026-10-26, steps 1-3 done by lane NW above): what is left of R5.** Order by dependency:
  1. cGZDrawContextRenderToHardware: done by lane OD with R4b, 2026-10-27 (the entry "R4b with R5b step 1" above).
  2. cGZFontSys: done by lane OA, 2026-10-26 (the entry "R5b step 2" below). Then R6 (cGZFont) on top: its plan is
     in that entry.
- R5b step 2 (lane OA, 2026-10-26): cGZFontSys as a whole class (src/gz/cGZFontSys.{h,size.cpp}, cIGZFontSys.h, cIGZFont.h;
  33 status entries in db/status/cGZFontSys.jsonl, all verified). The COM director's creator 0x909fd2 (`new(0x5c)` and a
  jump to the constructor) is the only hook (`Hooks.cGZFontSys_Create`); the constructor 0x90f626, the destructor
  0x90f85c, the 22 own slots (both faces, +0 cIGZFontSys 0xe50968 and +4 cIGZSystemService 0xe509d0), SetDefaultFace
  0x910a9d and GetServicePriority 0x910daf are in effect through callers; the five +4 thunks are unhooked; SetEncoding
  (folded 0x1f5250) is an override. Layout (0x5c): flag +8, count +0xc, id +0x10, quality +0x14, default size range
  +0x18/+0x1c, encoding +0x20, renderer +0x24 (counted), then five MSVC trees/vectors: lowercased families +0x28,
  default face by kind +0x30, file records by path +0x38 (0x34 bytes each: list of {font, style}, family, style, group,
  kind), aliases +0x40, file patterns (vector, 4 from the globals 0x10618b0..bc) +0x48, size ranges per family +0x54.
  Tests (tests/gz_font_sys.cpp): GZFontSys_Slots (600 seeds x 60 steps over every slot of both faces, creator and
  constructor builds alternating), GZFontSys_Construct (seeded memory), GZFontSys_SetDefaultFace (the replacement branch
  only this reaches); 84 mutants, 79 killed, the rest argued equivalent or replaced by a stronger killed one (notes in the status entries). Original calls left
  (`original_calls`): the cGZFont constructor 0x90a7db (NewFont's fallback when the director has no font class), the
  font file reader (0x90a6c5 ctor, 0x90d6f7 Open, 0x90ccd5 Name, 0x90d27e Style, 0x90a934 dtor) and the kind detector
  0x910b35 (reads the file's header through cRZFile): R6 takes the reader, the detector needs cRZFile read fakes.
  Smoke for the coordinator: any text on screen (every font is made through LoadFont/FindFont), the startup scan of the
  Windows fonts folder (AddFontDirectory with an empty name) and the fonts folder of the game; an unknown font name must
  still fall back to a default face.
- **R6 step 1 (lane OC, 2026-10-27): cGZFont's object, accessors and loading, and the font file reader**
  (src/gz/cGZFont.{h,size.cpp}; 53 status entries, 4959 bytes, verified).
  - **Library decision.** The reader's callees at 0x9116c7..0x91dc8d are Bitstream Font Fusion (T2K), a proprietary
    library linked into the image: the evidence is in `db/third-party.jsonl`, and the library rows are in
    `db/subsystems.jsonl`. Its entry points are original calls for good (the 14 named `FontFusion::*` in Ghidra,
    tentative). A standalone build needs a replacement font engine; see the open question below.
  - **Classes.** cGZFont is 0x190 bytes and has the full layout. cGZFontGlyphCache is 0x38 bytes: two of them, at
    +0x120 (mono) and +0x158 (smooth), with cGZFontCachedGlyph entries of 0x18 bytes in an MSVC hash. cGZFontFile, the
    reader, is 0x30 bytes. cGZFontFileStream is 0xa8 bytes: a cRZFile in a union, then the 'MXFN' header length, the
    obfuscated length and its key. Ghidra has all five as confirmed structs.
  - **Hooks.** The creator 0x909fb9 (`Hooks.cGZFont_Create`) is the switch: it covers both the director's path and
    cGZFontSys::NewFont's hand-made fallback, which now calls our constructor. RendererMode 0x90ae06 is also hooked,
    because slots 52 and 53 are still the original's and call it.
  - **In effect through callers.** Everything else: the constructor and destructors, QueryInterface and Release, Load
    and Unload (slots 3 and 4), the accessors and setters, BeginDraw and EndDraw (56 and 57), the glyph caches, the
    gamma table, and the reader with its stream (Open, Name, Style, Close, the read callback). Trap:
    `Hooks.cGZFontSys_Create=1&Hooks.cGZFont_Create=1`. The constructor and the reader have callers in cGZFontSys
    (LoadFont's fallback, AddFontFile) that are ours only under that class's hook.
  - **cGZFontSys switch.** Its original calls 0x90a7db, 0x90a6c5, 0x90d6f7, 0x90ccd5, 0x90d27e and 0x90a934 are now
    ours (status and source).
  - **Tests (tests/gz_font.cpp).**
    - GZFont_Slots: 400 seeds x 40 steps over 31 operations. Each side's object is built by the creator or by new and
      the constructor, alternating. The whole object is dumped after each step, next to the fakes' log and the heap
      balance.
    - GZFontFile_Reader: 400 seeds x 12 steps. Cases are TrueType, PFB (ExtractPureT1) and MXFN files with seeded
      names and styles.
    - GZFont_RendererMode.
    - GZFont_GameSlots: every named slot is checked against the game's vtable 0xe507ec. The 23 forwarders are checked
      by recording stubs at their RVAs.
    - Mutants: 135, of which 134 were killed and 1 is equivalent (the stream's unmask test at equality, which unmasks 0 bytes). A first run left 4 more because GZFont_Slots rarely loaded a font; the rerun with weighted steps killed them (see the pitfalls below).
  - **Original calls left.** The 23 forwarded slots and helpers are listed in the constructor's `original_calls`;
    every forwarder calls its RVA through OriginalFunction with `this` as given. The Font Fusion entry points are
    listed per function.
  - **ORIGINAL-BUG in cGZFontFile::Name.** The family is cut at "bold" and then at "italic". When "italic" comes after
    "bold", the second cut is past the end and std::string::erase throws. We keep the throw; the test's seeds avoid
    those names.
  - **Smoke for the coordinator.**
    - Any text on screen: every font goes through Create, Load and SetSize.
    - Bold and italic faces picked by style. The OS/2 selection bits and the name cut are both on our side now.
    - Fonts from the game's own folder: MXFN-wrapped, through the obfuscated stream.
    - Any change of text quality: SetQuality, the gamma table and the cache clear.
- **R6, cGZFont complete (lanes OC plan, OF, OH, 2026-10-27).** Every slot of the class is ours and no original call
  is left in the class (src/gz/cGZFont*.cpp; the Font Fusion calls are library code, called where they are). Lane OH
  finished it: the Thai shaper 0x90dd90 with ThaiClass 0x1b1d25 (hooked, it has outside callers) and ThaiForm 0x90d324;
  slots 27 HasGlyphs, 28 CacheGlyphs, 51 ClipText, 60 WrapText; the draws 52 DrawText 0x90c129 and 53 DrawBox 0x90c526
  with the colour-ramp builder 0x90e536 (SetColourRamp: a 4-block LRU of 0x200-byte tables). The Layout, IsSpace and
  RendererMode hooks are dropped (callers.py: every caller is ours). The hooks left are cGZFont's creator 0x909fb9 and
  ThaiClass. Tests: tests/gz_font.cpp GZFont_Text (800 seeds; the draw ops run on fake graphics-system and framework
  cells 0x10f51a4 / 0x10c9378 and a fake renderer, comparing the call log, the glyph runs, the block list and table
  bytes and the heap), GZFont_Slots, GZFont_GameSlots. 40 draw mutants plus 1 BreakLine fix-up mutant: all killed
  except 5 equivalents (argued in the status entries).
  Reproduced original bugs (ORIGINAL-BUG in src): the ramp block dropped from a full list is leaked; DrawBox writes
  `pos` from an uninitialised value when no line drew (ours writes it only if one did, which no caller can tell).
  BreakLine's final space fix-up needs 3+ leading spaces then "." then text to differ; op 14 case 2 builds that and
  kills the mutant.
  - **What is left in the class: nothing.** Next for fonts: the standalone engine below.
  - **Pitfalls found.** The original wrapping loops (WrapLines, MeasureRect, FitRect) never return for any text where
    BreakLine returns 0 for the limit (a limit of the widest character, mode 2 with zero-width text, a space before a
    character that cannot start a line, Thai clusters): the tests pre-check with this side's BreakLine and skip. The draw
    tests need the world cells above set; without the graphics cell the original's colour conversion reads null. A
    negative spacing makes GetWidth wrap: bound any `% (width+n)` in test generators.
  - **Standalone font engine (decided 2026-10-27; done by lane OJ, 2026-10-28).** Font Fusion (library code we can't
    ship) is replaced by FreeType 2.14.3 (CMake FetchContent, pinned by SHA-256, built static from its own sources with
    our module list and options in src/font/freetype/; FTL credit in src/font/FontFusion.h) behind a T2K-shaped shim,
    src/font/FontFusion.{h,cpp} and FontFusionType1.{h,cpp}: our versions of the 18 entry points with the originals'
    cdecl shapes and errCode out-parameter (no longjmp), and the memhandler, sfntClass (0xdc + our state pointer) and
    T2K (0x18c + ours) with every field cGZFont or the comparison reads at Font Fusion's offsets (Ghidra structs
    FontFusion_head / _sfntClass / _T2K, lane OJ). Hooked at the 18 RVAs (src/hook/tables/FontFusion.cpp); cGZFont's `ff::`
    wrappers call the shim directly (no original_calls left on the engine), and tests/gz_font.cpp stands its fakes in over
    both the image's entry points and ours (shape::StandIn). The fonts: Helvetica Neue and ITC Benguiat Gothic are PC
    Type 1 (PFB); the rest TrueType (LetterGothic, Maxis Password, Phantok and the CJK fonts). What the shim does as Font
    Fusion did, checked exactly against it: the font type, the PFB extraction (its length), the cmap choice (preferred
    platform/encoding, then format 0/2/4/6), glyph indices (codes masked to 16 bits) and existence, Type 1 glyph names
    and codes (its own Type 1 parser for the encoding, names, FontMatrix, /ascent /descent and the 'f'/'g' fallbacks),
    names, and T2K_NewTransformationAndGrid's results (Type 1 FontMatrix premultiply, 0x91bb69; pixels per em, scales,
    matrix normalisation, the metric transforms 0x91db29 / 0x91dad8 with Font Fusion's FixMul / FixDiv / distance
    0x91f537 / 0x91f4da / 0x91f41f; also checked at skewed, rotated and x/y-apart transforms and under other cmap
    preferences). Rendering is FreeType's, sized and placed as Font Fusion does: hinting only with
    command bit 0x80 (TrueType instructions; not under head.lowestRecPPEM, nor glyph formats 2000-2002), unhinted
    outlines scaled with Font Fusion's unit scaling (0x91bae9), the advance its rounding of the scaled advance width, and
    the bitmap's box from the outline's crossings with the pixel-centre rows and, when the scan converter also scans
    columns (0x92860e: grey levels, or 1-bit with dropout control), the centre columns (0x926c5f). Within the stated
    tolerances of tests/font_fusion.cpp (13 fonts, 22 sizes and 26 at x0.8, both pixel modes, 107,272 renders):
    y and linear advances exact, x advances exact but 4 (LetterGothic's section sign at 60/70, hinted, a pixel); box
    edges within a pixel but 4 renders (DFPGothicP's '!' at 8, two); boxes exact in 82-98% per font; bitmaps differing in 0.5-1.3% of 1-bit pixels and 1-5%
    of the ink. Not done: embedded bitmaps (sbits; T2K's enableSbits is ignored, the game's fonts have none we saw used),
    CFF metrics beyond FreeType's, the caret sine for italic Type 1 (std::sin, no italic font in the game), Font
    Fusion's own grey rasteriser (FreeType's smooth one instead), and read-failure error codes beyond the main ones.
    Status: a library replacement, not EA code; no status entries (proposal in notes/feedback/OJ.md); the coordinator
    checks text in game by screenshots.
- R2 (lane NK, 2026-10-26): cPixelFormatIterator as a whole class (src/gz/cPixelFormatIterator.{h,cpp}, 15 status
  entries verified): the COM factory 0x15ea00 (class 0xbb782354) and the manager's CreateIterator 0x15e860 (slot 3; the
  constructor 0x15d040's only caller) are hooked and are the switch; the constructor, the 10 own slots and the counted
  set's Release / deleting destructor (cRZRefCountedT<std::set<...>>, vftable 0xd9d070) are in effect through callers
  (trap: both hooks, `Hooks.nGZGraphic4_cPixelFormatIterator_CreateEmpty=1&Hooks.nGZGraphic4_cPixelFormatsManagerDX_CreateIterator=1`;
  the constructor only the second). The folded slots (AddRef 0x148fb0, Release 0x151920,
  Clone 0x15e580, IsEqual 0x15e840, GetFilter 0x46390, the set's QueryInterface 0x148350 and AddRef 0x15e520) are
  shared with other classes and stay unentered. src/gz/cDeviceEnum.h gained the iterator's full 15 slots, cIPixelFormat
  slots 7-10 (block height / width, row bytes, Less) and cIDisplayModeIterator's Prev and destructor. Tests:
  tests/gz_gfx_pixel_format_iterator.cpp (CreateEmpty, Construct, CreateIterator: 300 seeded scripts each over 56 fake
  formats). Our manager's slot 3 (src/gz/PixelFormatsManager.cpp) still reaches it through the store's
  `createIterator` (the image's address, which the hook turns into ours); the standalone store has none and aborts.
  Next step there: call `cPixelFormatIterator::CreateRange` directly and retire the store field (touches
  tests/pixel_formats_manager.cpp and tests/pixel_format_vtable_slots.cpp, which check and fake the field).
- R2b step 1 (lane NK, 2026-10-26): cDeviceEnumDX with cDisplayModeIterator and its counted vector as whole classes
  (src/gz/cDeviceEnumDX.{h,cpp}, 37 status entries in db/status/cDeviceEnumDX.jsonl, 0x784 bytes; the facts, offsets,
  property keys and LogD3DError lines are in the source). cDeviceDX's Init constructs ours and its SetFlags,
  CreateDevice, Init, ReleaseDevice and DumpInfo call our Refresh / DumpInfo (0x1774a0 / 0x178b80 / 0x1788f0 left
  cDeviceDX's `original_calls`), so the game's vtables 0xd9f758 / 0xd9f7e8 / 0xd9f7d4 are never stored: every slot,
  the constructor, Refresh, the dump, the caps clamp 0x178560 and the mode order 0x1778c0 are in effect through
  callers (trap `Hooks.nGZGraphic4_cDeviceDX_Init=1`; Refresh, the clamp and the order also rest on
  `Hooks.nGZGraphic4_cDeviceDX_Construct=1`, the dump only on it). The folded slots (0x148fb0, 0x178b40, 0x145770,
  0x178b20, 0x178b60, 0x178550, 0x148350, 0x146a40, 0x148840) are shared with other classes and stay unentered.
  Original calls left: the pixel formats getter 0x18acd0 (most slots, Refresh) and the rectangle clip 0x18ce70
  (CanStretchRect). src/gz/cDeviceEnum.h now types slots 20 / 25 as the raw byte (`uint8_t`): the original returns
  the stored byte, not 0 / 1. Tests: tests/gz_gfx_device_enum.cpp (Construct, Refresh, Slots, DumpInfo, ClampCaps:
  the original's and our object built alike on a fake IDirect3D9, fake formats, graphic service and string fakes;
  the log, results, object bytes, the mode walks, the pool and heap compared).
- R2b step 2 (lane NQ, 2026-10-26): cGraphicSystemDX (0x458 bytes) as a whole class (src/gz/cGraphicSystemDX.{h,cpp},
  48 entries verified in db/status/cGraphicSystemDX.jsonl) with the behaviour template every graphics service shares,
  `nGZEvent::cAbstractBehaviorTypeMT<Iface, Traits>` (src/gz/cAbstractBehaviorTypeMT.h: the atomic count with the
  active bit, Release disconnecting the last reference, Notify / Ask over the connection manager, the event registry
  through `Traits::Registry()`, still each class's game function), and the do-nothing device slot 29 hands out
  (src/gz/cNullDevice.h). The creator 0x159940 (COM class 0x855227cd) is hooked and is the switch; every slot,
  BuildDevices 0x159410, TearDown and the canvas handler are in effect through callers. cDeviceDX's Construct / Init
  and cImageManipDX's GetImageManip entries now also rest on this hook. Tests: tests/gz_graphic_system_dx.cpp (7);
  48 mutants, 46 killed, 2 equivalent (argued in the entries).
- R2b step 3 (lane NQ, 2026-10-26): cGZGraphicSystem (0x40 bytes) with its back buffer cBackbufferAsGZBuffer (0x38,
  our class over cGZBuffer's layout) (src/gz/cGZGraphicSystem.{h,cpp}, 52 entries verified), and cGZCanvas (0x30) with
  the three messages it sends, cGZCanvasMessage / cGZMouseMessage / cGZKeyMessage (src/gz/cGZCanvas.{h,cpp}, 45
  entries verified, with the template's vtable-only base destructor 0x146660, whose other callers are the two
  creators' unwind funclets). The creators 0x1475e0 (COM class 0x20732180) and 0x1495f0 (0x445fabbb) are hooked and are the
  switches; every slot of the five classes, the mouse message's constructor 0x148da0 and the template slots the system
  and the canvas share (0x1487e0, 0x148890, 0x1474c0, 0x147150, 0x1473e0) are in effect through callers (both traps).
  The messages come from the game's fixed pools (0xfeb660 / 0xfeb670 / 0xfeb680); the pool allocator 0x149040 is game
  code shared with other pools and stays an original call, as do the registries 0x148440 / 0x149ef0 and the system's
  device and mode helpers (0x18ae40, 0x18b470, 0x18acd0, 0x18a9b0, 0x18b440, 0x18b490, 0x18b250, 0x154a80). Tests:
  tests/gz_graphic_system.cpp (6) and tests/gz_canvas.cpp (8: every slot called through the block's own vtables, so
  each original slot runs against ours); mutants: 44 / 44 killed on the system, 57 / 57 on the canvas and messages.
- R2b step 4, stage 1 (lane ON, 2026-10-28): cCanvasW32 (0xdc bytes, the window service 0xfb6372ee with its three
  vtables, src/gz/cCanvasW32.{h,cpp}), on cAbstractBehaviorTypeMT with traits for its cell 0x10c9708, missing attribute
  0xfebc60 and registry 0x156a70 (still game code, an original call of slots 12-14). 52 entries verified
  (db/status/cCanvasW32.jsonl): the creator 0x155c70 (hooked, the switch), the screen-saver helper 0x157780 (hooked:
  the window procedure and 0x1563f0 still call it), every slot of the three faces and the helper 0x155ab0 in effect
  through callers (trap Hooks.nGZGraphic4_cCanvasW32_Create=1; 0x156160 IsMinimized also rests on cTSBehaviorTree_Read,
  a byte-match look-alike call at 0x46d60a), the four adjustor thunks unhooked, and the folded bodies 0x155e20 /
  0x155e40 / 0x156180 / 0x157200 (other classes' code calls 0x157200 directly) entered without a hook. Tests:
  tests/gz_canvas_w32.cpp (5: Create 64 seeded blocks byte for byte; Behaviour 15 ops x 96; Show 1500; Slots 11 ops x
  400; Lifetime 5 ops x 600), over 37 Win32 import fakes (impfx) and drawfx fakes for the framework and its window
  service, the monitor, strings, hooks and listeners, with a real connection manager in the event cell. Still game
  code, called through OriginalFunction or handed out: the registry 0x156a70, the multi-monitor stub 0x154ae0 (slot
  42), and the window procedure 0x157800 / icon callback 0x155b30 that Init gives the window class. Mutants: notes/
  feedback/ON.md.
- R2b step 4b (lane OQ, 2026-10-28): cCanvasW32's window procedure and the canvas events. 12 entries added to
  db/status/cCanvasW32.jsonl (6340 bytes, src/gz/cCanvasW32Proc.cpp): the procedure 0x157800 (WindowProc, then
  OnMessage's switch), the icon callback 0x155b30 (LoadClassIcon), OnChar 0x156260, ExitSizeMove 0x1563f0, OnKey
  0x1564d0 / 0x156550, OnButtonDown 0x1565d0 / 0x1566c0 / 0x1568c0, TrackMouseLeave 0x1567b0, KeyModifiers 0x155860 (the IME
  server's PostCharacter now calls ours) and the monitor callback 0x155710 (FillOtherMonitors). Init hands our
  procedure and icon callback to the window class, so all 12 are in effect through callers (trap
  Hooks.nGZGraphic4_cCanvasW32_Create=1; 0x155860 also Hooks.cGZWinIMEServerW32_Create=1). The events are classes of
  their own (src/gz/cCanvasEvents.{h,cpp}, db/status/cCanvasEvents.jsonl, 19 entries, 376 bytes): cCanvasEvent
  0xd9caac, cCanvasSizeMoveEvent 0xd9cacc, cMouseEvent 0xd9caec, cKeyEvent 0xd9cb0c. Our window code builds ours; the
  game's vtables are still stored by other classes' code that copies an event it heard (0x21a52b, 0x61e43c, 0xcc3158
  and each class's own clone slot), so the 13 bodies each class has to itself are hooked with adapters on the game's
  vtables, and the 6 bodies folded with other classes are entered without hooks. Tests: tests/gz_canvas_w32.cpp
  cCanvasW32_WindowProc (6000 seeded streams of up to 8 messages over every handled message and others, the hook
  loop, foreign windows, listener throws through the procedure; results, the import and fake call stream, every event
  with its class, words and its own slots' answers, the object and the TrackMouseEvent static) and
  cCanvasW32_KeyModifiers (0x155860 alone: the procedure never passes the flags' bit 30), and
  tests/gz_canvas_events.cpp (2: the game's slots against the adapters, ours against the game's). Original calls left:
  the multi-monitor stub 0x154950 (WM_PAINT), the lead-byte count 0x1b06a6 (OnChar, RZText) and ON's 0x154ae0 (slot
  42). Mutants: notes/feedback/OQ.md.
- **R2b plan, what is left**:
  4c. Done (lane OS, 2026-10-28; db/status/MultiMon.jsonl, 3 entries, all verified). src/gz/MultiMon.{h,cpp} (namespace
     nMultiMon): InitMultipleMonitorStubs 0x1547f0 (hooked: the display monitor class's vf7 0x154620 and 0x154a80 are
     still game code and call it, and read the same statics 0x10c96b8..0x10c96d8), EnumDisplayMonitorsStub 0x154950 and
     MonitorFromWindowStub 0x154ae0 (in effect through callers: cCanvasW32's procedure and GetMonitor, both ours now,
     no `original_calls` left in db/status/cCanvasW32.jsonl). CheckScreenSaver 0x157780's hook is dropped (all seven
     references are ours: SetFlag19, ExitSizeMove, the procedure; trap Hooks.nGZGraphic4_cCanvasW32_Create=1).
     tests/gz_multimon.cpp (gz_MultiMon_Init 600, _Enum 3000, _FromWindow 3000 seeded cases; impfx fakes for the 11
     Win32 imports, recording stdcall stand-ins for USER32's functions). 0x1b06a6 and the display monitor's
     0x154620 are ours since lane VU (2026-11-03). Left: the folded destructors 0x154ce0 / 0x154eb0 (wait for the event header's class).
  Then the base template slots shared across instantiations (checked by lane ON with callers.py, 2026-10-28): 0x147aa0,
  0x146f50, 0x146810 and the constructor 0x146400 have no live reference left (the generic instantiation's vtable
  0xd9bd34 is carried by no live object; the constructor's one caller is the hooked cGZCanvas creator), but nothing in
  our code instantiates that generic class, so no test can name them: they stay without entries (dead code). 0x1481c0
  (slot 12) and 0x146a70 (slot 13) are still jumped to by 16 other classes' slot stubs each (scene-graph extensions,
  the animation channels and queues, cTSEffectsExtension), so they wait for those classes.
- **Rendering leftovers (lane OW, 2026-10-28)**: 32 functions, 12,553 bytes, all `verified`
  (db/status/ImageManipMemory.jsonl, hooks in src/hook/tables/ImageManipMemory.cpp).
  - The mip kernels (src/gz/MipKernels): twelve row halvers and combiners for 8888, 2101010 and 8-bit rows (0x185100..
    0x1863c0). The DXT decoders and block painter (src/gz/DxtBlocks): 0x185b90 / 0x185da0 / 0x185fb0 and PaintBlock.
    The five cached manager getters (src/gz/GraphicServices; 0x18acd0, 0x18d120, 0x18dcf0, 0x18dd70, 0x18ddf0), now
    called by every caller (cDraw2D, cDeviceDX, cDeviceEnumDX, cGZGraphicSystem, cSurfaceCopy, cSurfaceDX, cTextureDX,
    MaterialCommands, cTSSGSystemCapture, the CAS face export; their `original_calls` entries are gone and the tests'
    patches stand in through `shape::StandIn` on our getters). The memory-texture chain (src/gz/cImageManipDXMemory):
    0x184290 CopyLevel, 0x1844b0 ConvertRgbToArgb, 0x185a30 ChooseConverter, 0x1854e0 / 0x185770 two- and four-tap
    reducers, 0x184ec0 normal-map level, 0x184710 CopyMemoryTextures, 0x183500 ComputeMemoryNormalMap.
  - Tests: tests/gz_mip_kernels.cpp (kernels and DxtBlocks), gz_graphic_services.cpp, gz_image_manip_memory.cpp
    (13 tests: real formats for every converter pair, plus logging fakes for the manager, image manip and surface calls).
    Mutants: mip 21/21, dxt 30/30, services 9/9, memchain 78 of 81 killed; the 3 left are argued equivalent
    (`window[slot]` in the two-tap combine: slot flips twice per iteration so it is 0 there; `halvings < 1` for 1 on the
    simple path: the long path gives the same bytes; the `level != current` guard: the two never coincide).
  - **Pitfalls found**: compressed (DXT) sources always take the long path through two temporary memory surfaces, however
    few halvings there are, because they are decoded first; a source with no levels takes the texture's own address as
    the size (ORIGINAL-BUG, untested: the arena would be gigabytes). The cached getters are never reset, and
    the graphic system query has no null check. The cRZStopwatch the original keeps in CopyMemoryTextures is never read,
    so ours has none. The unlock clears the record's surface, so a fake CopySurfaceRect must not release a null one.
  - **Not ported**: ~cShaderProgramInputsDX, ~cShaderProgramsManagerDX, ~cVertexFormatsManager (stale STL or dead
    instantiations under replaced callers: nothing to port). The foreign owners the chain calls: cMemBlockAsSurface
    0x18d1a0 / 0x18d2c0, cMemBlockAsTexture 0x18c300 / 0x18c350 / 0x18c5b0 / 0x18c3e0 (real game code, left as
    `original_calls` of 0x184710); shared 0x16a870, 0x1461c0, 0x14a620, 0x14ad00, 0x2c1170, 0x14c930.
  - **Lane OZ (2026-10-28), cDraw2D's rest, all verified** (db/status/cDraw2DUpload.jsonl, GraphicServices2.jsonl):
    0x18ae40 (cached current device) and 0x18a7f0 (pixel format for a colour type) in src/gz/GraphicServices.{h,cpp},
    callers switched, test stand-ins moved to shape::StandIn; UploadRecord 0x152410 (1808 B) and UploadKeyed32 0x14dfa0
    (tests/gz_draw2d_upload.cpp: fakes for surface/texture/buffer/manipulator, 3000 and 6000 seeds); the draw system
    getter 0x14f330 (tests/gz_draw_system_getter.cpp); the registry 0x1516f0 (tests/gz_draw2d_registry.cpp: our map is a
    std::optional in cDraw2D.cpp, not the game global 0x10c95e0). Mutants: 10/10, 54/54, 3/3, 12/12 killed. Gaps: the
    registry's first-use build is not differential (magic statics cannot run in the harness; the test plants the post-init
    state); UploadRecord with rows > 0 and columns == 0 reads stale locals in the original and is not reproduced.
    Original call left then: 0x1464f0 (shared base registration, in RegistryFor); ours since lane PD.
  - **Lane PD (2026-10-28), OZ's plan for the rest of item 3: done or accounted for** (db/status/cGZFontSys.jsonl,
    cGZEventReflector.jsonl, cCanvasW32 / cGZCanvas / cGZGraphicSystem / cGraphicSystemDX.jsonl):
    - cGZFontSys::DetectKind 0x910b35 (tests/gz_font_sys_detect.cpp, 6000 cases, 20/20 mutants killed): the extension
      (.ttf 1, .ttc and .otf 4, .pfb 2) from the path's extension routine 0x1c1e0e (still the original, listed in
      `original_calls`), else the first 0x20 bytes of the file through cRZFile (%!PS anywhere in them 2, 00 01 00 00 1,
      ttcf 4). The test stands in cRZFile::Open / Read / Close (0x1c285d / 0x1c2c6e / 0x1c0faa) on both sides with a scripted
      file and compares the kind, the call log and the heap balance; the real constructor and destructor run. AddFontFile
      calls ours; the generated gz_font_sys tests keep the detector stand-in on both sides.
    - The behaviour registries (tests/gz_behavior_registry.cpp, 8 tests, 52/52 mutants killed): the base registry 0x1485f0
      (`cAbstractBehaviorType::RegistryFor`, a `std::optional` map in cGZEventReflector.size.cpp: nothing else in the image
      touches the game static 0x10c94f4) and the three registration helpers 0x1464f0 / 0x1465d0 / 0x146580 (the two base
      events ConnectionChanged 0x5b51e806 and AttributeInfosUpdated 0xe9dd42cf; they have other original callers, so all
      four are hooked); the four classes' registries 0x156a70 (cCanvasW32, 6 events), 0x149ef0 (cGZCanvas, 5), 0x148440
      (cGZGraphicSystem, 1), 0x15a250 (cGraphicSystemDX, 7) share one body, `ClassRegistryFor` in src/gz/cBehaviorRegistry.h
      (first use: the base events registered, the map a copy of the base registry, the class's own events stored; then the
      store). The original keeps a small leaked "holder" object beside each map whose constructor does the registering;
      ours has none. The 12 template slots (GetEventCount / GetEventIds / GetEventAttribute) of the four classes and
      cGZEventReflector::Registry and cDraw2D's RegistryFor now call ours, so their status entries lost `original_calls`.
      Tests: the originals' first-use build cannot run in the harness (magic statics), so the original side starts from the
      state it leaves, built from the image's own event tables and a real run of 0x1464f0; the class tests' planted maps
      stay and a `shape::StandIn` on the registry function (tests/gz_registry_fx.h, `pdfx::StandRegistry<map>`) keeps both
      sides on them. Names of the events are our literals, not pointers into the image's text; compared by content.
    - cGZDrawContextRenderToHardware 0x8baca0 and 0x8bab40: STL vector instantiations whose only callers are
      DrawRectRoute 0x8b8d30 (ours, replaced through its callers): nothing to port. ~cCanvasW32's remainder was the
      registry above, the event-manager instance 0x1557e0 (called only from replaced slots: dead, no entry) and shared
      one-liners. The cached getters 0x18dcf0 / 0x18ddf0 / 0x18dd70 were already ours and verified (ImageManipMemory.jsonl).
    - Gaps: the first-use builds are checked against the decoded expectation, not differentially (above); the other
      registries that call the base ones (0x16a170, 0x2455e0, 0x2570a0, 0x3602a0, 0xa44a20, 0xa9a020, 0x21886d, 0x60da52,
      0x618e47: the sg lanes' and others') are original and now fill ours through the hooks. Nothing of OZ's list is left.
- R0 SetUpDevice (lane PO, 2026-10-29): cDeviceStateDX::SetUpDevice 0x1807b0 (src/gz/cDeviceStateDXSetUp.cpp) and its 33
  default-command helpers 0x186550..0x18a1a0 (one switch, `ApplyDefaultCommand`, in src/gz/cDeviceStateDXCommands.cpp),
  34 entries verified in group cDeviceStateDX, in effect through callers (CreateDevice 0x167ec0 is the only caller of
  0x1807b0, SetUpDevice the only one of each helper); CreateDevice lost 0x1807b0 from its `original_calls`. What it does:
  builds mDefaults once (the wanted tables with stage 0's colour op MODULATE, alpha op SELECTARG2, every stage's
  coordinate index its own; the caps chosen by software vertex processing, flags & 0xa0), records the auto depth
  stencil and the back buffer's size, clears the sent state and the pending constant starts, applies the 33 default
  commands, then sends everything to the device in a fixed order (render states, stages, samplers, the five
  displacement-map textures null, N-patch, shaders, material, declaration, streams with frequency when GetLimit(4) >= 3,
  indices, transforms, lights disabled), Reset(all), Flush(all), and the palettes (`// ORIGINAL-BUG:` there). Facts:
  - The helpers are default commands whose values come to constants: the fog colour and the lights' colours read
    globals nothing writes (black). The material's ambient is 4.0 (the command scales by 4). The lights helper reads
    the caps' MaxActiveLights unchecked.
  - Command 0 asks the target provider's surface before the back buffer (the order shows in the reference counts).
  - The stream count is signed: a negative limit sends none.
  - Test: `cDeviceStateDX_SetUp` (tests/gz_device_state_dx.cpp), 1500 rounds on the shared fake device with the enum's
    HasProcessingCap / GetLimit / GetCaps, the device's GetBackBuffer / GetTargetProviderSurface and failing D3D calls
    logged through LogD3DError (the graphic-service fake needs slot 30 GetErrorString for that path). kEngineSlots and
    kEnumSlots grew to 41 and 24 slots.
- R0 validation (lane PQ, 2026-10-29): cDeviceStateDX slots 27-29 (CheckVertexProcessing 0x181f00, Validate 0x182220, ValidateCommand 0x1829d0)
  and their 25 validators (0x1865f0..0x189e40: 19 command validators, 6 state validators), src/gz/cDeviceStateDXValidate.cpp, 28 entries verified in
  group cDeviceStateDX; the constructor entry 0x17c490 lost its `original_calls`. The validators are in effect through callers
  (only slots 28/29 call them, both ours through our vtable). Facts:
  - Slot 29 dispatches by the command's type byte to 19 validators (null caps reports at line 0xc89); it reads past its own 24-entry engine-op table
    for larger indices, so the tests keep op indices < 24 and argument nibbles < 7.
  - Slot 27 (software vertex processing probe) ProcessVertices into a held vertex buffer made through the framework's GetClassObject
    (director framework 0x1ae18d, class 0x3bffec52; the index buffer 0x9bfff0ea). ORIGINAL-BUG: the two interfaces its queries return are never released.
  - Slot 28 runs the six state validators without short circuit, the texture type checks, the stream and index-buffer checks, ValidateDevice (unless
    feature 1), then a one-triangle trial draw. ORIGINAL-BUG: the volume texture test in the sampler addressing validator compares GetType() with 0x2000.
  - Two texture helpers stay original (`original_calls` of 0x189a70 / 0x189e40): 0x18c7f0 texture format support, 0x18df30 cube format support (CheckDeviceFormat behind SEH).
  - Ghidra cuts slot 28 at its jump table (0x182220 listed as 55 bytes); the code after it (0x182257.., 1771 bytes, 33 cases) calls the six state
    validators. Lane PT: a manual `resized` entry in db/missed-functions.jsonl (`missedfuncs.py --switch`) gives it its 1826 bytes, so the
    validators' calls belong to slot 28 and need no `callers_handled`.
  - Tests `cDeviceStateDX_ValidateCommand` (40000 rounds), `_CheckVertexProcessing` (6000), `_Validate` (60000) on the shared fake device; 55 mutants,
    54 killed, 1 equivalent (sampler mip filter's unknown-type return).

## Left

`tools/py -I tools/coverage.py queue local/coverage/run-1023.txt --subsystem scenegraph,rendering-gfx --whole` (largest
first); after each merge, `coverage.py headline` on the newest reference session.

## Conventions and test patterns

- **Device layer: the fake Direct3D device** (below). Build the object on seeded random bytes with the device and
  state pointers aimed at the fakes, run each side on the same addresses from one snapshot, compare the device calls
  (slot and stack words), the LogD3DError lines and every byte of the objects. Pointer arguments are the same on both
  sides because the objects sit at one address; compare what they point at when the callee reads it (viewport, rects,
  constants).
- **Above the device** (cDraw2D, materials, shaders, display list, effects): call ours and the original over the same
  objects; once the device classes are ours, both sides end at the fake device, and the device call stream is the
  observable result, the same text the in-game device log writes.
- **2D drawing and fonts**: drawfx (docs/ui-testing.md, part 1): cIGZDrawContext, cIDraw2D, cIGZFontRenderer and
  cIGZGraphicSystem tables exist.
- **Data and geometry** (containers, builders, parsers, camera maths): plain difftests on synthetic inputs (no
  package content in tests); stream fakes as in persistence.
- **Floats**: ours must give the same bits as the original (check per function whether it is x87 or SSE code). Sweep
  values near rounding boundaries (the flush rounds with the 1.5*2^23 trick), negative zero, and large values.
- **Original callees in later campaigns** (world-lot geometry, animation) stay original-calls; record their calls in
  tests rather than writing stand-ins.
- **cDeviceStateDX layout** (lane LM; src/gz/cDeviceStateDX.h has every offset): +0xa0 the wanted state (0x1190
  bytes: textures, render / stage / sampler states by the engine's index, texture transforms, material, streams,
  targets and their size, scissor and viewport as fractions, depth range, clipping, projection, world, ambient, 16
  lights), +0x1230..+0x7570 the shader constants (vertex float 0x400, int 0x100, bool 0x100; pixel float 0xe0, int
  0x10, bool 0x10), +0x7570 what the device was last sent (targets, depth, viewport, scissor and its enable), +0x75b0
  a second DeviceState of defaults, +0x8740 a recorded change set (0x12c8 bytes, the state-block code's), +0x9a08 a
  std::vector filled by game code, +0x9a14/+0x9a18 two held objects. +0x74 is 12 bytes of pending flags and constant
  range ends (vertex ends u16, pixel ends u8; starts are separate dwords at +0x80 with 0x7fffffff for none), tested as
  three dwords for "anything pending". Engine indices map to Direct3D's through three global maps the constructor
  rebuilds (0x10ca448 u16[0x59], 0x10ca4fc u8[0x12], 0x10ca510 u8[0xd]).
- **D3DX** (lane NR): the D3DX entry points the image calls (0x1e89e Assemble, 0x1e981 Compile, 0x22220
  GatherFragments, 0x28c0c CreateFragmentLinker, 0x2168f Disassemble, 0x1eca8 GetShaderConstantTable) stay
  `original_calls` in ours and are replaced in tests by `shape::CodePatch` stand-ins at their RVAs (both sides reach
  the same stand-in); their buffers, constant tables and linkers are drawfx COM fakes, and shader bytecode is a
  synthetic token stream (version token, a word picking the case's constant table, 0x0000ffff). tests/gz_dx_shaders.cpp.
- **Pitfalls found in R0**:
  - Engine objects asked for their Direct3D object by QueryInterface write nothing for an id they don't answer: the
    caller's local must start null. drawfx fakes behave the same for an unknown iid, so tests reach that path.
  - Float compares and word compares are both used: depth range, viewport and scissor fractions compare as floats
    (NaN never equal, -0 equals 0), matrices, materials, lights and the world transform word by word. Feed NaN and
    -0 in tests.
  - Pixel conversion: target size (unsigned, through double) times the fraction minus 0.5, rounded by the 1.5*2^23
    bias, then one less where that equals the unrounded value exactly; an empty viewport becomes 0,0,1,1.
  - The list setters (render, stage and sampler states) are do-while loops: a count of 0 runs 2^32 times. Tests never
    pass 0 (the original would run off its arrays).
  - The constant locks only widen the pending range; Flush sends [start, end) and resets it. SetVertexShader flushes
    pending groups 0x47 first only when leaving software vertex processing.
  - Slots the class still leaves to game code are forwarders through `OriginalFunction` from our vtable (handoff D2);
    the vector's teardown is the library's (the destructor test keeps it empty).
- **cDeviceDX layout** (lane NC; src/gz/cDeviceDX.h has every field): +0x04 the count (low 31 bits) with the
  behaviour's active bit 31, +0x08 the 0x2c-byte cDeviceSettings, +0x34 adapter, +0x38 the IDirect3DDevice9, +0x3c
  enumeration, +0x40 resource manager, +0x44 state cache, +0x48 display monitor, +0x4c swap chain count and +0x50
  three swap chains, +0x5c target provider, +0x60 event query, +0x64 sync surface, +0x68 probe texture, +0x6c scene
  depth, +0x70..+0x75 six flag bytes (full screen, lost, failed, releasing, +0x74 only ever cleared, creation
  blocked), +0x76 two bytes never written. The behaviour slots 0-14 are nGZEvent's cAbstractBehaviorTypeT template
  (event registry 0x16a170, the connection manager in the cell 0x10ca22c); slots 15-48 the device's.
- **Pitfalls found in R1**:
  - The difftest harness maps the image without registering its SEH handlers: the original's `__try/__except`
    (ProcessVertices) cannot catch there and the runner dies. C++ `try/catch` in original code works (Init's catch of
    a cD3DException, the canvas frame's catch). Test structured-exception paths on ours only.
  - LogD3DError asks the graphic service (0x85522803 / 0xc55289ba) through QueryServiceValue for its slot 30 (one
    argument): a fake answering that service must give slot 30 one word (`d3dfx::ErrorLog::Answer`), or the stack
    corrupts.
  - `drawfx::gLog` keeps its capacity across `clear()`: under `difftest::AllocationCounter` reset and reserve it
    before each side, or its growth is counted on one side only.
  - The drawing slots (Clear, the draws, ProcessVertices) dereference the device unchecked: test them with a device.
  - The class is not `final` (standalone shape): self-calls go through the vtable as the original's do. Tests that
    call ours on a block carrying another vtable qualify the call (`device->cDeviceDX::BeginScene()`).
  - Original locals left uninitialised when an engine QueryInterface fails (ProcessVertices' two answers) pass stack
    garbage; ours passes null, and the test zeroes the stack below the original's frame so the value compares.
- **The device enumeration as CreateDevice sees it** (lane NF; src/gz/cDeviceEnum.h): nGZGraphic4::cIDeviceEnum
  (cDeviceEnumDX, vftable 0xd9f758, 30 slots) with the slots CreateDevice calls named (6 desktop mode, 7 display modes,
  10 / 12 colour formats and support, 14 / 16 depth formats and support, 18 multisample qualities, 20 processing
  capabilities (0 and 2 hardware, 7 pure), 25 features (0 event query, 2 driver management, 3 immediate presentation, 4
  flip, 6 DXT1), 27 adapter identifier) and the others holding their place with the stack bytes their bodies pop.
  cDisplayMode is 20 bytes (width, height, aspect float, refresh, pixel format) and the first five words of
  cDeviceSettings; the process default at 0x10ca208 (the constructor's) means windowed on the desktop. Ranges are
  three counted iterators (cursor, begin, end), released end, begin, cursor (0x146670) and copied by cloning the cursor
  and counting the ends (0x167180). The settings' flags: 0x01 immediate presentation only with feature 3, 0x04 mixed
  vertex processing, 0x20 wait for the GPU after Present, 0x40 copy swap (one back buffer, no multisampling), 0x80 no
  device clip.
- **Pitfalls found in R1b**:
  - MSVC instance methods return every struct through a hidden pointer (the first stack argument after `this`, eax
    the pointer back), even 4- or 8-byte ones: declare such slots with by-value results (cDisplayMode,
    cDisplayModeRange, D3DADAPTER_IDENTIFIER9) and their fakes write through `args[0]` and return it.
  - The cdecl pixel-format helpers 0x18b490 (mode search) and 0x18ab60 (depth search) take the range by value (three
    stack words) and release it themselves: the caller passes a counted copy. Their stand-ins must release it too, or
    the reference counts differ from the game's.
  - 0x17bb30 reads none of its arguments, so the "windowed" bool's high bytes are a register's leftovers: log the
    low byte.
  - IDirect3D9::CreateDevice answering a positive (success) code other than D3D_OK on its last try leaves no device,
    and both the original and ours then set it up (a null call): cDeviceDX.cpp's ORIGINAL-BUG; tests never answer one.
  - GetBackBuffer and GetDepthStencilSurface results are released unchecked (also after a failure): the fakes always
    hand out a surface.
- **Pitfalls found in R4** (cImageManipDX):
  - Function-local statics: the original's thread-safe statics work in the harness (the image's TLS epoch); a test sets
    the guard to INT_MIN for "initialised" (fast path) or 0 for "empty" and restores both words after. Ours keeps a
    plain flag per static (src/gz/cImageManipDX.h PaletteCache), filled at the same moment, so the service calls
    compare.
  - A static the game constant-initialises (vtable word in .data, no constructor) leaves its vtable "referenced from
    data": callers.py (lane NI's static-object rule) counts the object dead when every reference to its address is in code
    in effect or dead (here the hooked getter and its atexit stub), and the slots' traps rest on the getter's hook.
  - Level loops run to the texture's level count: never seed a count like 2^32 - 1 (both sides loop until the log
    exhausts memory, and the run dies with a C++ exception).
  - RZText conversions with `kSystemCodePage` read GetACP's cache 0xff23a4 (-3 until first use; GetACP is a trap): set
    it to 8 (UTF-8, the strings' own encoding, so nothing is converted) for the test and restore it.
  - Rectangles: seed most inside the surfaces, or the clipping helper 0x18ce70 rejects nearly every copy and the D3DX
    paths go untested.
- **Engine STL containers** (lane NK, R2): the original's std::set / std::vector code is this toolchain's MSVC STL
  (db/names.jsonl names the tree helpers as library matches, e.g. 0x15dc50 `_Tree_unchecked_const_iterator::operator++`).
  A class holding one can hold ours with the same layout (std::set: head, size; nodes 0x14 bytes with the value at
  +0x10 for a pointer, 0x24 for a cDisplayMode), and inserts, erases and comparator calls happen in the same order,
  which the tests compare through the fakes' Less calls. cRZRefCountedT<T> (vtable, T at +4, the count after it;
  QueryInterface answers 1 only; Release deletes at a count of 1) is in src/gz/cPixelFormatIterator.h.
- **Fixed pools in tests** (lane NK): an RZFastFixed pool at its RVA can be stood in by pages from VirtualAlloc with a
  fake page vtable (new page / free page slots 2 / 3 logging), each page's free list chained in block order; compare the
  pool's head words and every page's links, free list and used count after each side (normalised to page offsets), so
  the order of allocations and frees is part of the result. tests/gz_gfx_pixel_format_iterator.cpp has one.
- **Pitfalls found in R2**:
  - Iterators sharing one counted set: SetFilter (slot 11) erases nodes from the shared set, so a clone whose cursor
    sat on an erased node holds a freed node (the original's behaviour; reading it is use-after-free and differs by
    heap state). Tests set every live iterator back to the beginning after a SetFilter.
  - The manager's CreateIterator (0x15e860) reads the image's static format table 0xd9cf78 (56 pointers); ours reads
    the current pixel format store's table, so a test writes its fakes into both (VirtualProtect the .rdata table and
    restore it).
- **Pitfalls found in R2b step 1** (cDeviceEnumDX):
  - The object starts at count 0: a test holding it must AddRef after constructing, or the first Release deletes it.
  - The constructor's probe CreateDevice counts S_FALSE as success (it reads the device out): a fake must write the
    device for any non-negative result.
  - Slots 20 / 25 return the stored byte (not 0 / 1); slot 27 copies the identifier out and leaves eax unset (don't
    compare its eax).
  - GetAdapterDisplayMode asks the HAL IDirect3D9 without a null check; only the REF / SW types may be null in a test.
  - impfx counts a reference parameter (REFGUID) by the size of the type, not 4 bytes: StringFromGUID2 needs its arity
    given by hand (`imp.Add("StringFromGUID2", 3, ...)`), else the stack is off by 12.
  - A NaN aspect in the owner's default mode makes the original's slot 16 dereference a null format (the mode never
    matches): tests keep the default mode's aspect a number.
- **Pitfalls found in R3** (lane NJ, the surfaces and textures):
  - Creators that look alike (0x161da0 / 0x161e00 differ only in the pool, the size and the vtable): a swapped creator RVA in a hook
    row or a test still builds and half works; the runner died later with corrupted locals. Check each creator's pool
    and vtable stores against the class before wiring it.
  - The engine reads Direct3D out-parameters without checking the HRESULT (GetDesc, LockRect: it reports through
    LogD3DError and carries on), so fakes must write them on failure too (d3dfx::Level, unlike SurfaceData).
  - Uninitialised locals the original copies into a record (LockRect's empty case and Unlock write a 16-byte stack
    rectangle nobody set): zero the stack below the call on both sides, from a `__declspec(noinline, safebuffers)`
    helper; without `safebuffers` the /GS cookie lands in the zeroed area and one side sees it.
  - The copy helpers 0x1614b0 / 0x162d60 dereference their point and rectangle arguments unchecked: the slots that
    reach them (CopyRect, StretchRect) are only called with both.
  - Fake widths: a level wider than its pixel buffer (kLevelBytes) makes the original's row copy overrun it; seed large
    widths only on levels nothing copies.
  - Pools: the classes' fixed allocators find a block's page by masking its address, so test pools need page-aligned
    pages (VirtualAlloc), and each side needs a fresh pool on the same region for addresses to compare.
  - In a worktree, run the worktree's tools (stagea.py, callers.py): the main checkout's tools read the main checkout's
    hook tables and miss the lane's.
  - (Arrays) A pool global is the allocator object, not its page: the creators load `[pool + 4]` (the current page),
    so `DAT_pool+4` in the decompile is not the pool's address. Wiring the +4 address as the pool crashed the
    original creator inside the test.
  - (Arrays) Code that never runs can still be read as a branch: the static arrays' Create refreshes the manager's
    memory only for the default pool, which the usage / pool table never holds. Say so in the test rather than
    seeding for it.
  - (Formats) The original builds 16-byte vertex elements on the stack without clearing their three padding bytes, and
    GetElement / FindElement copy all 16: zero the stack before each call (the safebuffers helper above), or the pads
    differ between sides.
  - (Formats) Function-local statics behind thread-safe guards (0x10ca29c / 0x10ca2e8): the test sets the guard words
    to "initialised" (INT_MIN, lane NG's precedent) so the original getter answers its static without constructing it.
  - (Declarations) A null Direct3D object from a failing CreateVertexDeclaration with a non-quiet code leaves a
    dangling list entry (ORIGINAL-BUG above): seed null objects only with the quiet codes.
  - (Arrays) Boundary picks: a random sweep rarely hits "exactly at the end" (Reserve's `>` survived the first mutant
    run); pick sizes that divide the buffers and make the sweep require the case.
  - (Manager) A test that counts allocations per case must not print or start anything lazily inside the counter: the
    first `fprintf` to a stream allocates its buffer (4096 bytes) on whichever side runs first, which reads as a leak of
    the original; warm both sides up with a throwaway case first.
  - (Manager) The clone of an iterator starts uncounted: one AddRef and one Release free it (a second Release is a use
    after free that crashes through a pool block's free-list word).
  - (Manager) A float comparison that is a not-below branch (`comiss` then `jb`) is taken for NaN: write the
    continuing condition as `>=` rather than `!(a < b)` (the same bytes for ordered values, different for NaN).
  - (Shaders, lane NR) LogD3DError's QueryServiceValue asks the framework for the graphic service and calls its slot 30
    (src/rz/MiscHelpersA.cpp): a test that also answers that service with its own fake (ErrorLog::Answer) must give the
    fake 31 slots, or the first failing HRESULT on the original side jumps to 0 ("exception at 0x0, no module").
  - (Shaders) The original's helper vectors come from the game's operator new (0x824632) and ours from std::vector:
    a COM fake logs a pointer argument by where it points (drawfx's 'i' and '@' presence words), so a heap array
    handed to D3DX differs between sides by address only; normalise that word in the handler, citing the allocation.
  - (Shaders) Ordering by pointer (the programs' key compares fragment-name addresses) makes each side's heap order
    decide the result: intern the names the sequence uses in one test-owned array on both sides, and make the sweep
    require that interned names were linked (a name the test meant to intern but the original didn't find shows up only
    there).
  - (Shaders) "Never released" was a misreading: a set whose values are counted pointers releases them in its tree
    erase (0x16f520 calls each value's Release before freeing the node). Read the erase helper before calling a leak
    an ORIGINAL-BUG.
  - (cGZGraphicSystem) Compare every out-parameter a slot writes, not just the log and the block: five mutants
    (scale, rate, cursor position, screen planes, colour conversion) survived the first run because the sweep wrote
    them and never compared them.
  - (cGZCanvas) The connection manager calls back into the behaviour while connecting (QueryInterface for
    cIAbstractBehaviorType through the source): a block whose vtables are not yet live crashes it. Embed a source fake
    (drawfx `Embed`) that answers the block, and connect only once the block is constructed.
  - (cGZCanvas) Call every slot through the block's own vtables on both sides (the original's block with the game's
    vtables, ours with ours): the original slots then run against the original layout, and no slot table can drift
    between the sides. Check the slot RVAs once in the creator test.
  - (cGZCanvas) Fixed message pools (0xfeb660 / 0xfeb670 / 0xfeb680) are game globals the creators read: set each to
    the same free list over static elements per side, compare the elements with their vtable word normalised, and
    compare the pool words after.
- **Pitfalls found in R5b** (lane NS, the software engine and span renderers):
  - The span renderers loop `do { } while (--count)`: a count of 0 runs 2^32 spans. The engine's callers never pass
    0; tests must not either.
  - The engine dereferences its target unchecked in most slots (SetColors, GetBounds, UpdateLock with a level change,
    FillPolygon) and its renderer in DrawSpans: a test tracks "holds a target" from SetTarget's answer and Detach and
    calls those slots only then.
  - The texture paths read texels without bounds checks: keep the spans inside the area SetTexture returned (plain and
    scaled) and give the texture memory margins (a scaled area's edge may reach a texel past the width).
  - The 16-bit blends switch to an SSE path at CPU level (global 0x10df81c) above 1 and spans of 8 pixels or more:
    run every renderer test at level 0 and 2 (a drawfx::Cells scope sets it).
  - The engine's constructor leaves most fields as they were: build both sides in one seeded block (see
    tests/gz_draw_render_to_buffer.cpp's Block) or the object comparison sees heap garbage.
- **Conventions from R5b**: an engine test compares every object word but the vtable and the renderer pointer
  (+0xc0: the game's renderer on the original side, ours on ours) and compares the renderers' work through hashes of
  the target memory after every drawing call; the renderer classes themselves are compared on a stand-in object
  carrying the game's vtable (tests/gz_span_render.cpp's GameRenderer).
- **Pitfalls found in R5b steps 1-3** (lane NW):
  - Original bugs reproduced (each marked `// ORIGINAL-BUG:` in src/gz/cGZDrawContextBuilders.cpp): BuildPoints steps
    its source 12 bytes while the points are 8; BuildEllipsePolygons' output pointer never advances; BuildQuadSpans steps
    4 floats where a quad is 8; BuildPolygon aliases its working list and drops the answer of its route-2 call;
    BuildLineSpans divides by a zero length (a guard keeps ours alive where the original faults).
  - `cGZDrawContext::mState.lineWidthRounded` (+0x50) is 1 from the constructor and nothing ever changes it, and the sent
    copy holds the rounded width: the wide-line branches of the line, curve and ellipse builders are dead in the game.
    They are ported and tested by seeding +0x50 in the test, so a later lane that makes the field live finds them tested.
  - NaN and infinite points reaching the scan converter make the original throw C++ exceptions (a huge vector size):
    tests/gz_draw_context.cpp sanitizes the points of the kinds that reach the scan converter and gives the other kinds
    the extended values.
  - Builders and Dispatch are a cycle (Dispatch -> builder -> Dispatch for kinds 0, 2, 4, 5): the in-effect check cannot
    close it, so one side stays hooked at its address (Dispatch).
  - callers.py cannot follow a data table: functions only reached through .rdata function-pointer tables (the 909
    texture span functions) have no way to a status entry today; a family hooked at each address (the pixel loops) does.
  - A table of loops that take one big by-value block is hookable as it is: declare the loop `void __cdecl f(Block b)`
    and the caller's copy lands in the argument; the original's loops do not read the unused words (the test fills them
    with garbage to prove it).
  - The pixel loops read and write pixel by pixel: an overlapping row (same buffer, destination above the source) sees
    pixels it has written; ours must not use memmove / memcpy over the row. Keep the order.
  - Two loops can be the same function under two names (0x8c5370 and 0x8c4fe0 over all 16-bit values): a mutant that
    swaps them survives and is equivalent, not a hole in the test.
- **Conventions from R5b steps 1-3**: a family of table-reached loops is tested with the table read from the original's
  image (so a swapped or missing entry shows), each loop against its original at its own address through the same
  argument block, plus the dispatcher over every pair; exhaustive inputs where the domain allows (every 16-bit value,
  every 24-bit colour with a hashed alpha). Hook-table lines for a family are generated: `hookspec.py` for the CRCs, a
  script for the `Hook<I>` names.
- **Pitfalls found in R5b step 2** (lane OA, cGZFontSys):
  - A first use inside the game's text code allocates a 4 KB table that never goes back: the side that runs first keeps a
    block the other does not, so the heap balance differs by one block. Run a throwaway case on both sides before the
    seeds.
  - Globals of the test's own stand-ins (a std::string holding the last opened path) keep the buffer one side grew: reset
    them in the side's build, or the sides' heap counts differ by a block mid-run.
  - An empty MSVC tree's head node has uninitialised bytes after the colour and nil bytes (the allocator does not clear
    them): compare the low 16 bits of that word only.
  - A scoring rule with gaps between its constants (0 / 2 / 4 against 8 / 0x10 / 0x20) has equivalent mutants: any value
    inside a gap keeps the order. Mutate across the gap (9 for 2 or 4) to make the test prove the ordering.
  - GetAliases loops to the caller's count, not the number of aliases (`// ORIGINAL-BUG` in the source): a test gives a
    non-null string only for the aliases that exist (it first asks with a null array to learn the number).
  - A failing class lookup of the director answers a nonzero word with a zero low byte (the callers test only al); a
    hand-made font then costs a `new(0x190)` block that the constructor stand-in has to free.
  - Only Init reaches SetDefaultFace, once per kind, so its replace-an-existing-name branch is unreachable through the
    slots: it has its own test (the original called at its RVA on a constructed object).
- **Conventions from R5b step 2**: a service class with containers is compared by dumping the object after every step
  through our own types (the original's containers have our layout: sets, maps and vectors read by content, the heap
  pointers and the padding left out) next to the fakes' call log; the world is the director fake, per-path hash answers
  for the files and fonts, StandIn over the file-system callees on both sides and CodePatch over the original callees
  ours still calls. The service face is called through the +4 pointer with the generated interface, which runs the
  adjustor thunks on the original side.
- **Pitfalls found in R6 step 1** (lane OC, cGZFont):
  - **Count an original callee's arguments from its call site** (the pushes and the `add esp` after the call), not from
    the wrapper you are replacing. Font Fusion's GetGlyphIndex 0x91c04e takes three arguments (t2k, code, &err). We
    first declared four, and both sides' fakes agreed with that wrong declaration. In game, the engine would have
    written its error through a null pointer. A fake that takes the declared arity can't catch this; only reading the
    caller's pushes can.
  - **Keep the log vector out of the heap balance.** A std::vector log that grows during a side shows up as one block of
    heap difference on that side only. Give each side a fresh vector, reserved before the allocation counter starts.
    The same goes for any container a fake pushes to, such as an error list: `clear()` keeps the first side's buffer.
  - **Check how often the seeds reach the state a branch needs.** The first mutation run left four survivors in Load
    and Unload because only 19 of 400 seeds ever loaded a font. The paths were mostly empty or missing, and Load was 2 of
    31 steps. Counting with a temporary probe (seeds that load, that fault in the glyph lookup, that unload with a run
    count) showed it. Weighting the steps (a SetPath followed by a Load, the case's files three times as often) raised it
    to 115 of 400 seeds.
  - **The image's STL has our layout** (std::_Hash, std::list, std::basic_string at the same offsets), so a test can
    read and fill the original's containers through our types. This holds only once the image's operator new
    0x824632, delete 0x824624 and free 0x6c4b50 are stood in on both sides, so that both heaps are the same.
  - **cGZFontFileStream holds a cRZFile in a union** (constructed and destructed by hand). It goes through the cRZFile
    stand-ins of the test (Construct 0x1c0c07, Destruct 0x1c0e6d) on both sides; a member cRZFile would run our
    constructor on our side only.
- **Conventions from R6 step 1**: a class over a linked library (Font Fusion) fakes the library's entry points with
  CodePatch on both sides. The fakes hand out fixed objects and log every call with its arguments, each pointer as the
  name of the object it is (mem, sfnt, t2k, input0.., self, or ?address), so a wrong argument or a missing Delete shows in the log rather than as a crash. Forwarder
  slots (still the original's) are tested by patching each forwarded RVA with a stub that records `this` and up to 9
  stack words (one `Record<I...>` template and one `FWD` line per slot), then calling our slot by name. That proves the forwarder's
  target and its argument passing.
- **Pitfalls found in R4b / R5b step 1** (lane OD, cDraw2D and cGZDrawContextRenderToHardware):
  - **A `__try` / `__except` in the original is not a handler in the harness.** cDraw2D's shapes call the material
    under `__except(1)` (0x14f04f..0x14f076); with no material the original faults and the difftest process dies, since
    the mapped image's handler isn't registered. Seed only states where the guarded call succeeds and say so in the
    test; ours keeps the handler (`DrawGuarded`).
  - **NaN signs follow SSE operand order.** A zero-length DrawLine divides by zero; which NaN an addition of two NaNs
    keeps is its first source operand's, and MSVC may commute an addition. The original's signs (read from its
    instructions) are set explicitly in ours. Expect this wherever a float path can see 0/0.
  - **`GameAddress(State::kVTable)` in a template hides the class from callers.py's source scan.** The scan resolves a
    `kVTable` by its scope; a template parameter can't be resolved, so it falls back to every class's constant and
    flags all vtables as held by our code. Name the class at each call site (pass the vtable in).
  - **A function reached only from an unwind funclet gives callers.py no hook to rest on.** The funclet is code outside
    any function, so `callers_handled` covers it but the trap's hook set comes out empty, and a `Hooks.` trap is then
    refused. cDraw2D's template destructor 0x14be20 is such a case: we leave it without an entry and handle the template
    vtable's slots instead. Hooking it with an adapter (the ObjectSaveTypeTable precedent) doesn't work when our
    destructor is empty: the compiler drops its vptr store, so the adapter does nothing observable.
- **Conventions from R4b / R5b step 1**: a class whose device objects come from game code is tested with drawfx fakes
  at the level it talks to (cIDeviceDX, cIDeviceState, the dynamic arrays), each render-state command logged by its
  bytes, the bytes the original leaves unwritten masked with a reason. The abstract engine base is tested on its own
  with a recording class for its pure slots on both sides (the original built by its constructor over a copy of its
  vtable whose pure slots are the recorder's), then again as the derived class's +8 face.
- **Pitfalls found in the atlas and record code** (lane OL, cDraw2D step 1):
  - **The original indexes unchecked, so the test world must keep every index it can reach valid.** Besides record
    indices: the engine's current record must be usable (placed, with a buffer, tile indices inside the pool's textures)
    whenever the engine holds a texture (its source at engine +0x28, object +0x30), else GetTextureTiles / SelectTile
    fault in the original with a null or a heap address; the test lets the engine's texture go (`SetTexture(nullptr)`) at
    the end of a step that left it unusable, which the game does before drawing again. A cache holds a count of its
    record, so the test's random ReleaseRecord must not take it (the record would be freed under the cache).
  - **A counted-pointer vector copies with AddRef and releases the old elements when it grows** (the pools' textures,
    `RefPtrVector::EmplaceReallocate` 0x14aac0, growth to cap + cap / 2): a fake's reference count differs between sides
    if ours uses a plain push_back. `AppendCountedNull` in cDraw2D.cpp is the port.
  - **Fresh records carry stack garbage in places**: a new original record's byte +0x14 is the only byte written of that
    word, and its border bytes (+0x38/+0x39) are garbage until PlaceRecord sets them; the test logs the byte and the
    border word only for placed records. The original's Load seeds the colour-key alpha byte from the top byte of the
    tick argument and writes a fifth colour word in DrawQuad (both marked ORIGINAL-BUG in the source; the first is
    reproduced, the second is not).
  - **Test bookkeeping that allocates must not be counted**: a fake's lazily grown tables and the draw system fake's
    watch list are wrapped in `difftest::NotCounted`, else the two sides' heap balances differ by the order of growth.
  - **`callers.py --write-traps <file>` overwrites `<file>`** (it is the output, default src/hook/trap_table.cpp): never
    give it a status file.
- **Conventions from the atlas and record code** (lane OL): when the class under test makes COM objects through the
  class factory, the fake factory hands out numbered fakes (textures, buffers) whose counts are logged at the end; a
  third-party-owned pool the class allocates from (the cache's 0xfeb8e0) is given a seeded page by the test
  (`drawfx::Cells`), and the page is logged with the object. A handle class that only one class uses (cDraw2DRecordRef)
  still gets its own small difftest over the originals' addresses, with the service it counts on patched to a logging fake.
- **Conventions from the font engine** (lane OJ): a library replacement that can't match the original bit for bit is
  compared with it on the real data, read from local/game at test time (tests/font_fusion.cpp: every non-render result
  exact, renders within tolerances stated in the test's header, and per-font bounds so a mutant that shifts every
  render fails even when each render stays within a pixel). Its callers' tests stand their fakes in over both the
  image's entry points and ours (shape::StandIn), so the class tests keep their fake engine.
- **Pitfalls found in the font engine** (lane OJ):
  - **Font Fusion leaves fields unwritten that look like results**: the vertical metrics (T2K +0xa4..+0xcb) unless the
    font has 'vhea', and a Type 1 font's underline values (sfnt +0x7c). Compare them only when the original wrote them.
  - **Font Fusion's bitmap box is not the outline's control box**: it is where the outline crosses pixel-centre lines
    (rows always, columns too in grey mode or with dropout control). A dot that crosses no centre adds nothing; a
    floor/ceil of the control box is a pixel off on a quarter of the renders.
  - **FT_Get_Advance may reuse the glyph slot**: take the advance before FT_Load_Glyph, or the outline you loaded is gone.
- **Conventions from the window service** (lane ON, cCanvasW32): a class whose slots call Win32 through the import
  table is tested with one `impfx::Imports` per world and a handler on every import it makes (37 here), each answering
  from the case (rectangles, styles kept like a window's, return values), so both sides see the same system; the
  framework, its window service, the monitor, strings, hooks and listeners are drawfx fakes, the listeners on a real
  `cConnectionManager` in the event cell. A listener reads what the window sent from the event object (its vtable word
  and code), not from the event id: a connection delivers its target event (the "heard as" id). Pointer arguments are
  logged with kind `i` (stack addresses are normalised), never `u`.
- **Pitfalls found in the window service** (lane ON, cCanvasW32):
  - `OriginalFunction<Fn, 0xRVA>()` needs the RVA as a hex literal at the call site: a named `constexpr` fails at link
    time with an unresolved `OriginalEntry<N>` (tools/origsyms.py scans for literals).
  - A held COM object is released where the original releases it: Init keeps the framework's window service in a local
    through SetTimer, AddToTick, SetPowerFlags and the settings, and releases it last. An early Release shifts one call
    in the log and the test names the slot.
  - A destroyed `std::string` leaves its stale heap pointer's bytes 1..3 (only byte 0 is zeroed): the destructor test
    masks the pointer word of strings that were long before the run.
  - The creator and constructor leave bytes untouched (+0x1f, +0xa2, +0xc7, +0xcd..): the Create test compares the
    original creator against our constructor on the same seeded block, and the rest of the tests seed those bytes the
    same on both sides.
  - A hooked member of a multiple-inheritance class needs `MemberAddress<&C::f>()`, not `MemberAddress(&C::f)`.
  - `cmake` is not on the PATH of the lanes' shells: build ts2host for `callers.py` / `status.py --check` with
    `. .\tools\common.ps1; Invoke-Build ts2host` (twice after `--write-traps`).
  - A same-address 7-13 byte slot (a setter or getter) may be called directly by other classes' code (ICF folded):
    SetServiceID 0x157200 is, so it is a folded body without `in_effect`; run callers.py before writing the entry.
- **Conventions from the window procedure** (lane OQ): a procedure is driven with seeded message streams through the
  side's own address (the original's, then ours), each message's result or throw logged; the listeners note each
  event's class (by vtable: the game's or ours) and words, and call the event's own slots (type, size, clone into a
  guarded buffer) so a wrong class shows. Code pointers handed to fakes (the monitor and icon callbacks) are logged
  with kind `i` after `drawfx::AddRange(ptr, 1, tag)`, so both sides log the tag. A function-local static on the
  game's guard (TrackMouseEvent's cell) is saved, set per case (unbuilt, built empty, built) and restored by the test.
- **Pitfalls found in the window procedure** (lane OQ):
  - A C++ exception thrown by a fake through an original frame runs the image's CRT unwinder, which calls
    GetProcAddress ("FlsGetValue") and SetLastError through the game's import table: a world that fakes those must
    forward other names to the real ones (or leave SetLastError real), else the throw path crashes in the CRT.
  - The original's clone slots return garbage in eax (0x155900 returns the source's +4 word): compare the copy, never
    the return value.
  - cMouseEvent's bytes +0x15..+0x17 are never written (the constructor stores a byte, the copies copy the byte): mask
    them when comparing objects built on the stack.
  - The hook loop reads a handled flag the original leaves uninitialised: hook fakes must always write it.
- **Conventions from the surface copies and the multi-monitor shim** (lane OS): when a callee a test stood in with a
  `shape::CodePatch` over the image's code becomes ours, switch the patch to `shape::StandIn` (image and ours alike), or
  ours runs for real on its side (lane OQ's gz_canvas_w32.cpp did, for 0x154ae0 and 0x154950). Win32 shims that resolve
  USER32 through GetProcAddress are tested with impfx fakes for the imports (stdcall stand-ins handed out by
  GetProcAddress) and the statics preset and restored with drawfx::Cells. A "seeded random rectangle" never lands on an
  equality edge: add explicit shapes (right == 0, left == the screen width, a target starting at the bounds' far edge,
  a point of x 0 and y < 0), or the `<` against `<=` mutants survive.
- **Pitfalls found in the surface copies and the multi-monitor shim** (lane OS):
  - 0x18ce70's four fail tests (`targetLeft >= boundsRight` and mirrors) are equivalent to `>`: at equality the cut is
    0 wide and `1.0f > cutRight - cutLeft` returns false anyway; do not chase that mutant.
  - The copy 0x1614b0 cuts the target only at its far edges, so a point above or left of the target (negative) keeps
    the whole source rectangle and still passes the "whole surface" GetRenderTargetData test only when x and y are 0.
  - 0x154ae0 never sets the WINDOWPLACEMENT length (ORIGINAL-BUG marked in src): GetWindowPlacement's success for an
    iconic window rests on the stack.
  - A mutated build that loops forever shows as "killed (timeout)" in mutate.py (the filter hang in CanStretch): that
    is a kill, but note it in the entry.
- **Pitfalls found in CreateDeviceObjects** (lane OU):
  - **The original's shader size counts the terminator** (`strlen + 1`); the text pointer and size differ between the
    sides (our own source), so the fake logs presence only for both and the handler logs "size == length + 1" and the
    digest. A state command whose id the logging switch does not name logs no bytes: a mutant of a sampler state
    survived until texture addressing (0x1c) and mip filter (0x1f) were named in `StateCommand`.
  - **The device event "back" (code 1) calls CreateDeviceObjects with the device present**, and the original dereferences
    the current device unchecked: the test's current-device recorder (0x18ae40) answers null only for the constructor's
    first ask (`gCase.deviceAtStart`), and a device from then on.
  - **A zero-width target is the only way to reach the empty-clip flag**: the case 49 step sets it for one call.
- **Conventions from CreateDeviceObjects** (lane OU): a shader whose source we rewrite is compared by behaviour with a
  small interpreter in the test (tests/gz_draw2d_device_fx.h), written to refuse what it cannot judge (digest word 0,
  and a `gErrors` entry), so a text outside the instruction set fails rather than passes. Made objects (blocks, the
  shader, the declaration) answer "created" from the shared script at creation, so the "no longer created, make it
  again" branches are reached without the test reaching into the object.
- **Pitfalls found in the state blocks and Reset** (lane OV):
  - **The cache's defaults are the constructor's constants, not random**: a comparison of the wanted state against the
    defaults (the delta's memcmp of a matrix or the material) is only reached from values near the defaults. Mutants that
    shortened a compare by one word survived until the fixture pool held a matrix equal to the identity but for its last
    word and a material equal to zero but for Power (tests/gz_device_state_dx.cpp, FillPools).
  - **A record-order compare that stops one byte short** needs two records that differ in the last byte alone: a pair of
    materials whose Powers differ in the top byte (0x00123456, 0x3f123456), made in two blocks of group 1 with nothing else
    changed (the directed rounds of cDeviceStateDX_Blocks). Pointers in the pools never differ in their top byte.
  - **Reset ends the integer and boolean pending ranges at the boolean limit** (vertex and pixel; ORIGINAL-BUG markers),
    and asks the enumeration for the limits in the order floats, ints, bools. The limit index of the software mode is the
    hardware one plus one; a fake that ignores the index would not tell the modes apart (the limits fake returns a
    different programmed word for every index).
  - **GetCreationParameters is read after a failed call too** (the original's stack structure is uninitialised then): the
    fake writes the whole structure for any result.
  - The block removal (0x17dda0) searches by record size only and then for the pointer; dedup (0x17bdc0) is by size and bytes.
    A record whose count is zero loops 2^32 times in ApplyRecordStates: the tests never make one.
- **Conventions from the state blocks** (lane OV): a block-level test builds sequences of public steps (create, apply,
  setters, queries, releases) on both sides and compares the log, the cache's bytes (the list vector's three words left
  out: the library's), and every listed or held block's count, owner and record; then releases everything and compares
  the list and the pool again. Directed rounds (`round % 10 == 3`) are fine inside the random rounds when a path needs
  a particular pair of steps.
- **Conventions from the memory-texture chain** (lane OW): run both sides on identical arenas (a world of texture and
  surface objects built from seeded bytes) and compare the arena bytes plus a log of the fakes' calls. Pixel formats
  are real objects (ours) for the all-pairs tests; logging fakes are for the paths real formats cannot separate. A cached
  getter is tested by its global and the slot it called (CheckGetter in tests/gz_graphic_services.cpp); a caller's test
  stands in a fake with `shape::StandIn{rva, &ours, &Stand}` so it patches both original and ours.

## Shared fixture: the fake Direct3D device

Built by lane LM (R0) on drawfx (tests/drawfx_core.h), replacing LK's raw `FakeDevice`:
- `tests/fx_d3d9.h`, generated by `tools/d3dslots.py` from the SDK's d3d9.h: slot tables for 17 Direct3D 9
  interfaces (IDirect3D9, IDirect3DDevice9, state blocks, swap chains, resources, declarations, shaders, textures,
  buffers, surfaces, volumes, queries), slot names (`fx_d3d9::dev::SetViewport`) and argument kinds. `Iface::com`
  makes the thunks `__stdcall` with `this` on the stack. Matrices, viewports, rects, lights, materials and
  shader-constant arrays (`@<a>x<n>`: counted by another argument) are logged by content; COM pointers also by where
  they point; unreadable pointees log as such instead of crashing.
- `tests/d3dfx.h`: `Answers` (per-slot HRESULTs chosen once per case: 0, S_FALSE, D3DERR_INVALIDCALL, E_FAIL; applied
  to each side's fresh fake), `ErrorLog` (LogD3DError's report and error-string lookup become notes in the same log,
  so a report's place among the device calls is compared), and log helpers (`Calls`, `LastCall`, `Notes`).
- Fakes: `drawfx::Fake f; f.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);` results by `Returns(slot, hr)`,
  out-parameters by `On(slot, handler)`; engine-side COM objects (textures, shaders, surfaces asked for their Direct3D
  object) are fakes of small hand-written ifaces with `AddFace(iid, nativeFake)` answering QueryInterface.
- Users: tests/gz_device_dx.cpp (LK's two tests, ported), tests/gz_device_state_dx.cpp, tests/gz_device_dx_class.cpp. `tests/host_devlog_fakes.h`
  (a typed C++ fake device that names its calls) stays for the host's device-log tests.
- Lane NC (R1) added, in `tests/d3dfx.h`, checked by tests/gz_device_d3dfx_selftest.cpp:
  - `Creates`: the device's Create* slots hand out queued fakes (`Queue(slot, fake)`, in order per slot, AddRef'd,
    S_OK); with nothing queued, or a failing `Returns` on the slot, they write null and answer the failure
    (E_OUTOFMEMORY when none was programmed). It takes the device fake's `owner`.
  - `SurfaceData` / `MakeSurface` / `MakeTexture`: GetDesc / GetLevelDesc and LockRect answer from the data (bits,
    pitch, description); GetSurfaceLevel hands out `data.level`. A failing `Returns` writes nothing, as Direct3D.
  - `QueryData` / `MakeQuery`: GetData answers S_FALSE `busy` times, then the result (Returns ignored on purpose).
  - `ErrorLog::Answer(first, second, fake)`: what the error log's service lookups answer for an id pair (the graphic
    service and the window service in the device tests).
  - Slot constants `d3dfx::surf`, `d3dfx::tex`, `d3dfx::query`.
- Lane NJ (R3) appended general resources (used by tests/gz_dx_resources.cpp): `d3dfx::Level` (one surface: its
  description, GetDesc / LockRect / UnlockRect results, lock count, a kLevelBytes pixel buffer, priority, GetDevice;
  out-parameters written whatever the result), `d3dfx::Resource` (texture or cube texture: level count, LOD, up to 16
  levels' fakes for GetLevelDesc / GetSurfaceLevel / GetCubeMapSurface), `MakeLevel` / `MakeResource`, `d3dfx::Script`
  (the device's Create* slots by script: per call an HRESULT and an optional fake, so a failure can still hand out an
  object) and the slot constants `d3dfx::res`; for the arrays (tests/gz_dx_arrays.cpp) `d3dfx::Buffer` / `MakeBuffer`
  (a vertex or index buffer: description, GetDesc / Lock / Unlock results, lock count, a kBufferBytes byte buffer,
  priority; out-parameters written whatever the result) and the slot constants `d3dfx::buf`; for the declarations
  (tests/gz_dx_vertex_formats.cpp) `d3dfx::Declaration` / `MakeDeclaration` (GetDeclaration writes the element count
  whatever it answers) and `d3dfx::decl`.

## Risks and open questions

- **Owner inference lumps code.** cLightingManager's 94 KB is 77 KB inferred; cGZFont 43 of 50. A structure pass
  assigns owners before those lanes are cut (T0, R6's first step).
- **COM edges are invisible** to the call graph and to `deps`: device calls are found through LogD3DError only, and
  other COM objects (D3DX interfaces, DirectSound in audio-video) not at all.
- **Float exactness** in lighting, diffusion, the camera and the flush's viewport rounding; the cost of matching
  bit for bit is unknown until T2.
- **D3DX** is statically linked (third-party). The standalone build needs our own copy (the old DirectX SDK's static
  library or a replacement for the few functions used): open.
- **Device lost, reset and adapter changes** don't run in the reference session, so metric 1 doesn't see them; lanes
  R1 and R2 test them in the harness and the smoke batch doesn't.
- **Template node bodies** (cShapeRefNodeT, cViewerRefNodeT, ...) may be folded across classes (`shared_vf_*`): check
  each before hooking (CLAUDE.md).
- **Threads**: a texture or resource loader thread may call the device (the device log's async records with `w`); one
  rendering function waits on a synchronisation object. Check before R3.
- **World-lot coupling**: 33 KB of scenegraph calls into world-lot's nodes and 29 KB back; the world-lot campaign
  comes next and owns them.

## In-game call traces and frame images: apitrace (coordinator, 2026-10-23)

apitrace 14.0 (win32, open source) is in `local/tools/apitrace/apitrace-14.0-win32/`. The host loads system DLLs
from System32 only, so the wrapper is opted in with `TS2HOST_D3D9_DLL=<...>\lib\wrappers\d3d9.dll` (host/main.cpp),
and `TRACE_FILE=<path>.trace` names the output. Tried on own-start-menu (instance 12, quiet): a 760 MB trace, 1,199
frames, start to menu; `apitrace dump` lists every call with arguments; `d3dretrace --snapshot-prefix=dir/
--snapshot=<call no>` replays it and writes that call's frame as PNG (the menu came out exactly). Uses: when a
lane's tests pass but the picture differs, trace a reference and a replacement run and compare frames and calls
(`apitrace diff`, `diff-images`); find the first draw call that differs. Coordinator only (runs the game); traces are
large (lot runs several GB): delete after use. Our own device log (`devlog.py`) stays the routine acceptance check.

(Lane OZ, 2026-10-28; Pitfalls/Conventions)
- A function that a `shape::StandIn` targets on our side needs `__declspec(noinline)`: inlined into a caller, the stand-in is missed.
- Function-local statics (magic statics) cannot run in the harness (CRT init and atexit are traps): plant the post-init state on the original side and give ours a reset hook.
- tools/mutate.py maps tests to code through status entry names, so write the status entry first; header constants need `--force-filter`.

(Lane PD, 2026-10-28; Pitfalls/Conventions)
- A function-local static map with a leaked holder (the behaviour registries) is a `std::optional` of the same map type in ours, with a `ResetRegistryForTest()`; only code that the originals' holder constructors reach matters (check the static's address with `immsearch.py 0x...`: if only the builder touches it, the map is ours entirely).
- Once a shared builder (0x1485f0) is ours, every test that plants the original's static for its callers needs a `shape::StandIn` on the builder so both sides read the planted map (tests/gz_registry_fx.h `pdfx::StandRegistry<map>`): grep the tests for the static's address before hooking.
- Header-only helpers (src/gz/cBehaviorRegistry.h) are not reached by `mutate.py`'s status mapping: `--force-filter` and say in the report which tests reach them (here the four class registry tests).
- A test whose planted original starts from the empty registry must also empty ours (`map->clear()`): ours builds with the two base events in it.

## Left (from the backlog, 2026-11-03)

- P18 rest (IQ, IS): camera + shake blocks (shared slots 9/12: 0xbf5720, 0xbf4f20), screen, water; material-side commands cRenderTargetCommand 0xad5180, cColorScalarCommand 0xad1f10, cBindShaderProgramConstantsCommand 0xad1990, cTextureMatrixAnimationCommand 0xad7ac0 (need fakes for the framework lookup by class id, render-state objects, material parser fields +0x1d0/+0x1dc/+0x1fc/+0x27c).
- P17 rest (IT): the curve class first (0x10 bytes; 0xbdc8f0, 0xbdc880, 0xbdcac0 read, 0xbdce40 write, 0xbdcb80, 0xbdcd10, 0xbdcd40, 0xbdcd60, 0xbdcbd0; IQ/IS call it via OriginalFunction), then camera/screen/light descriptions (factories 0xbf6530, 0xbf8360, 0xbf9370; IS's 0xbf4610.. and 0xbf7280.. calls), then decal, meta and particle descriptions; Prepare passes 0xc05670/0xc09210/0xc07490/0xc06c00; effect serializers 0xbf9ee0/0xbf9da0. Meta-particle and particle descriptions (IW): 0x1e8 and 0x288 bytes, vtabled cTSBaseParticlesDescription base, colliders/attractors/warps/path points with own ctor/copy/parser; meta serializer 2.4 KB; particle factories 0xbe7ba0/0xbf1a70; curve 0xbdc950, scaling 0xbdc9f0: one lane. EffectCommands/ParticleCommands still write the game's vtables into new camera/screen/decal components (reach ours via slot hooks only): switch to our vtables. ParticleCommands still calls originals 0xbde020, 0xbdf9c0, 0xbdded0, 0xbe09e0/0xbe0b90 (grow), 0xbe58d0, 0xbefb90, 0xbfa*/0xbfb*/0xbd1*/0xaf6f* (IX).
- Device (NC, NJ, NT): R1 untested in game (device lost/reset, ApplySettings retry, canvas recovery, PresentFrame catch, ProcessVertices __except, WaitForGpu failure, Init throw): alt-tab/resize scenario; 4 RawSlot calls with unknown receivers (cDeviceDX.cpp Init slot 25 on the graphics service, slot 27 on mDeviceEnum, slot 4 on swap chain / target provider): type them when R2 decodes those classes; event bodies 0x16a7e0/0x167e80/0x16a7f0/0x167ea0 not entered until CreateDevice and 0x361060 are ours; fold generated ifaces::cIDeviceResourceManager into src/gz/cDeviceResourceManagerDX.h (R3 classes still use the generated one); in-game coverage of ManageTextureMemory, ReleaseNonSurvivors, ReleaseOnModeChange, TakeDevice.
- Shaders (NR): mutant #27 (AddFragment AddRefs the made name on a duplicate) invisible: the harness cannot count our side's cRZString temporaries; RawSlot<27> in tests/gz_dx_shaders.cpp waits on NQ's graphic-system slot fact; STL in 0x16f520..0x173950 is a stlprobe candidate. 0x176e40 (32 B, releases and nulls the shader programs manager's fragment linker i at +4+i*4; only caller cGraphicSystemDX::OnEvent): make it a cShaderProgramsManagerDX member (layout guarded by a static_assert in src/gz/cGraphicSystemDX.cpp), then drop it from original_calls.
- Fonts: standalone engine is FreeType behind a T2K-shaped shim (decision 2026-10-27; plan R6; lane after cGZFont is complete; OC). Shim gaps (OJ): no embedded bitmaps (sbits), grey bitmaps from FreeType's smooth rasteriser (~1/4 of grey pixels differ by a level), LetterGothic section sign advance off by 1 px at 60/70 px (v35 hinting); visual check needs a visible run (ask the user). ScaleUnits mutants 'num > 0x8000' and 'no even reduction' survive (equivalent below ppem 512): add ppem 512+ cases to tests/font_fusion.cpp. OF: BreakLine's space fix-up survivor ("a cluster swallows its trailing spaces") has no confirming input: build one when the Thai shaper lane runs; GZFont_Text skips wrapping ops (5-12) on double-byte code pages (original hangs/overreads): wrapping in 932/936/949/950 untested.
- 0xaa8990 (cShaderDynamicRectPreRenderStrategy, listed as cIShaderComponent slot 8 Init) returns lea eax,[ebx+0x10] and ends ret 8 while cIShaderComponent::Init is ret 0x10: the class's slot 8 is probably another interface's (TU, 2026-11-02); settle when a lane takes the strategy class.
