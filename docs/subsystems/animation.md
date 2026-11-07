# Animation campaign: subsystem map and lane plan

Lane XN, 2026-11-06: the campaign's dependency-map lane (`docs/roadmap.md`, Campaign order, *Inside a campaign*). It was
a planning lane, with no `src/` work.

**Measure.** Sizes come from `tools/subsystems.py --summary --no-write` after XN's filing changes. "Filed" is what the
map files here; "guess" is what stays in `other` with animation as its guess. Class and cluster sizes take filed plus
guess, with unnamed functions given to the class of their callers, then to the class before them by address: a size
is a planning figure, and a lane checks the owner of unnamed rows before taking them. "Ran" is the reference set
(`docs/coverage.md`: run-1023 plus six lots, one session), not run-1023 alone. Run-1023 played lot 6, where Sims hardly
walk, so it understates this campaign: 185,885 B against 407,074 B of the clusters below (cActiveAnimLoco 0 against
18,519, cLocomotable 80 against 42,483). Edges are function-level, by distinct callee bytes.

| | funcs | bytes | verified | evidence | inferred | guess |
|---|---:|---:|---:|---:|---:|---:|
| animation (XJ's filing) | 3,406 | 499,497 | 7,273 | 330 KB | 170 KB | 69 KB |
| animation (now) | 4,258 | 583,398 | 7,273 | 355,050 | 228,348 | 69,968 |

**Campaign: 653,366 B in 4,536 functions and 122 classes (filed + guess), 646,045 B to do. 407,074 B of it ran (62%).**
`coverage.py queue --subsystem animation --whole` over the reference set gives 390,466 B in 2,169 functions and 98
groups; it leaves out 182 library instances, 22,681 B, which stay the toolchain's. It has no hard test boundary except
the recorder's video imports (section 5).

## 1. Scope

The campaign is two layers, both running on the scenegraph's node code:
- **nGZSceneDBAnim** (0x9d0000-0xa33000, 389,905 B, 380,549 of it animation): resources, channels, active animations, IK,
  the choreography queue. It does not know about Sims. It is the module the rest of the game calls; the small remainder
  of that range is cRZCursor and other framework code, rightly filed elsewhere.
- **The game's node chain** (0x255f70-0x288c00, then the custom layer at 0x2b6000-0x2bd480): nTSSG::cObject > cAnimatable
  > cLocomotable, the base of nTSSG::cPerson, with the custom queue, event handlers and timers.

### What XN moved

XJ's rules filed the two ends of the chain differently from the middle. Rule change in `tools/subsystems.py` (commit
ab312e88), which moves whole classes and then ripples through the inferred and guessed bands:

| from | to | B | what |
|---|---|---:|---|
| scenegraph | animation | 45,208 | cObject 12,538, cAnimRequest 2,854, cCustomChoreoQueue 1,725, cCustomAnimTimer2 1,346, cSharedChannelFunction 766, the rest of the chain |
| other, guess scenegraph | animation | 23,242 | among them 0x27dc00 (4,085 B, the walk-style parser) and 0x9d3260 |
| other, guess animation | animation | 12,381 | promoted by the class rule |
| other, guess scenegraph | other, guess animation | 7,644 | |
| scenegraph | other, guess animation | 5,166 | |

Moved rows sampled by hand: scenegraph to animation 15/15, other to animation 12/12.

**How right the inferred share is:**
- Inferred band: the 30 largest functions 30/30 right; 40 size-weighted at random (seed 1) 40/40. Both by digest
  (callers, callees, strings, vtables), not by decompile. The digest shows the neighbours, so this isn't independent of
  the rule (`docs/subsystems.md`, the block holdout); the band is good enough to plan on.
- Guess band: 25 sampled, 20-22 right. Unclear: 0x7e78a0, 0x7e6630, 0x3aa720, 0xd95a90, 0x7eb8a0. Those are helpers
  in mixed modules: a lane takes a guessed row only after looking at it.

### Filed elsewhere, but the family's

A lane's status group claims these; no filing change is needed.
- Inside 0x255f70-0x288c00 (203,301 B, 149,212 filed here): `other` guessed scenegraph 11,757, simantics-vm 4,732,
  sim-core 2,332, objects 1,439; filed simantics-vm 3,610, scenegraph 2,715, cas 1,632. The largest: 0x267ee0 (3,204),
  0x26a700 (2,645), 0x2601d0 (2,536), 0x25a4b0 (1,778), 0x2629b0 (1,632), 0x265ff0 (1,521), 0x26cbe0
  (RecordAnimationTrack, 1,389), 0x274c20 (1,366), 0x284a10 (1,090), 0x2616c0, 0x272210, 0x259610, 0x259390. They are
  filed by a data reference or a caller guess, and sit between the class's own slots.
- 0x2b6000-0x2bd480: 11 small stubs guessed scenegraph, about 1.3 KB.
- Helper modules: 0xae2280-0xae8000 (23,675 B: 14,416 guessed animation, 4,225 guessed scenegraph, including 0xae5840
  2,611), 0xaf3860-0xaf46e0 (4,442), 0xc63380-0xc643b4 (IK math), 0x7eced0 (4,235), 0x7764a1 (recorder, 2,269),
  0x401f00-0x404300 (the recorder's GPU state, 7,920; largest 0x403340 2,854).

0x7dd000-0x7f3000 is a mixed module of unnamed helpers, not triaged. Five campaigns guess into it.

**Stays out:** nTSSG::cPerson (77,568 B of its slot bytes are bodies animation owns, see section 3; the class is
sim-person's). cTSObject and cEdithObject are not derived from nTSSG::cObject: bases cITSObject, cIGZSerializable and
cEdithObject > cTSTreeSim. They own a node and are called back by it, so a fake is enough. cTSAnimTable is here.

## 2. Dependencies

### Out of animation

Static direct calls, filed code, after the moves:

| into | calls | callees | B | not done | what |
|---|---:|---:|---:|---:|---|
| framework-gz | 745 | 98 | 11,200 | 2,099 | director, allocators, cRZString |
| other | 442 | 164 | 33,000 | 32,296 | the guessed rows above, mostly this campaign's own |
| **scenegraph** | 418 | 105 | 17,665 | **2,995** | cExtension, cSGResource, node templates, cBranchNodesMask: 83% done |
| rendering-gfx | 134 | 26 | 2,373 | 8 | done |
| simantics-vm | 55 | 11 | 4,318 | 3,514 | from the chain |
| persist-dbpf | 18 | 6 | 272 | 20 | |
| cas | 16 | 2 | 1,636 | 1,636 | one function, 0x2629b0, the family's by address |
| ui-windowing | 14 | 11 | 894 | 526 | recorder |
| ui-game | 12 | 3 | 157 | 30 | |

Observed at run time in run-1023 (`coverage.py deps --by group`, which also sees virtual calls):
- animation -> scenegraph 5,963 B in 41 callees: cLightingManager 3,321, cShadow 557 + 301, ~LightingManager 238,
  cFindAttributeNodesTraversal 224, cVideoBroadcastManager 192, cTSSGSystem 167, cSceneManager 162 + 122. Nearly all from
  the chain; nGZSceneDBAnim's own are a few hundred bytes: camera-manager helpers 95 + 78, cTSSGSystem 21, and a shared 238 B callee.
- animation -> objects 12,629 B: cObject and cAnimatable into ~cEdithObject 4,005 + 3,513 and cTSObject 3,252.
- animation -> world-lot 4,064, persist-dbpf 2,645, ui-game 2,345.

### Into animation

| from | calls | callers | what |
|---|---:|---:|---|
| sim-person | 360 | 191 | cPerson 344: the class above the chain |
| objects | 125 | 65 | cTSProduct* calling tiny folded stubs (static); run time, cEdithObject and cTSObject -> cObject and cAnimatable 21,727 B in 106 callees |
| scenegraph | 57 | 31 | run time 16,887 B in 119 callees, mostly slots (extension and virtual dispatch), from cTSSGSystem and the node walks |
| other | 53 | 48 | |
| cas | 38 | 35 | |
| simantics-vm | 24 | 16 | the animate primitives |
| ui-game | 23 | 15 | |
| world-lot | 14 | 9 | |

### Order against the scenegraph

**Interleave: nGZSceneDBAnim doesn't wait, and the chain comes after the scenegraph's lighting clusters or cuts them.**

| clusters (section 4) | to do | needs from unfinished scenegraph | order |
|---|---:|---|---|
| A0-A10, nGZSceneDBAnim and the custom layer | 424 KB | 2.7 KB summed over the clusters (A8 1,028, A10 1,003, filed or guessed), all small: original calls for a lane or two | any time, alongside the scenegraph's later clusters |
| A11-A13, the chain | 192 KB | the 2,995 B above plus the lighting and shadow calls: 5,963 B at run time; the guessed 9,544 + 2,831 + 4,150 B are mostly the chain's own rows (section 1) | after the LG lighting lanes, or before them with these as `original_calls` |
| A14, recorder | 29 KB | 150 B | last, not on the way to anything |

The scenegraph needs animation only by virtual dispatch: its calls in land on interface slots, so its lanes test against
fakes and don't wait. That makes 16,887 B in and 5,963 B out at run time, and neither blocks the other.

The chain sits below objects and sim-person: cEdithObject and cTSObject call it, and cPerson derives from it. So the chain
goes before the cTSObject and cPerson lanes that remain. Bottom-up, by `coverage.py deps`, animation comes at position 8,
with 25,546 B into later campaigns (objects 12,629, scenegraph 5,963, persist-dbpf 2,645, ui-game 2,345). It is inside the
big cycle {animation, audio-video, neighbourhood, objects, scenegraph, ui-game, ui-windowing, world-lot}.

Cut edges:
1. **Chain -> cTSObject, cEdithObject:** a fake owner (cITSObject) in tests; the game keeps its own until objects' lanes.
2. **Chain -> lighting and shadow:** original calls, recorded fakes in tests.
3. **Chain -> simantics-vm (3,514 B), cas:** mostly rows of the family (section 1); take them, and fake what's left.
4. **cPerson -> chain:** not animation's concern, but see section 3 for hooks.

## 3. Layouts and families

**The node chain.** `cAbstractBehaviorTypeT<cIObjectV> > cObjectGraphNodeT > cCompositionTreeNodeT > cTransformNodeT >
cBoundedNodeT<cIObjectV, cBoundedNodeImpl> > nTSSG::cObject` (184 slots) `> cAnimatable` (248) `> cLocomotable` (294)
`> nTSSG::cPerson` (317).
- It is the `cIObjectV` instance of the scenegraph's node templates. `src/sg/SGNodeTemplates.h`, `SGRenderableTemplates.h`,
  `cViewerRefNodeBaseT.h` and `cShapeRefNode.cpp` are the templates to instantiate rather than rewrite.
- The tables share bodies down the chain, so a slot can be reached from cPerson's table. Check the `shared_vf_*` rule
  (`docs/verification.md`): a body only this family references may be hooked; cPerson is in the family, and hook-through
  needs its table counted among the references.
- cAnimatable has secondary subobjects at +424, +432 and +444, reached through thunks with vtordisp (`src/sg/cAnimatable.h`,
  `tests/sg_animatable.cpp`). cObject's ctor is at about 0x256f10. Regions: cObject 0x255f70-0x26b000, cAnimatable
  0x26b190-0x27dbf1, cLocomotable 0x27dc00-0x288759. Then cObjectModule 0x288ca0 (scenegraph) and cPerson 0x28a660.

**The packed keys, and the resource file as a memory image.** 0xa16630 (2,636 B, it runs in game, named `ReadImage`,
tentative) reads a cAnimResourceConst. It reads the file's blob as one block and patches it in place: it places vtables
into the blob, sets back-pointers and turns running offsets into pointers. So the on-disk struct **is** the
in-memory proxy, at fixed sizes. The file's version picks the key proxy:

| version | proxy | element | ctor | size | ran |
|---|---|---:|---|---:|---:|
| 3 | cSharedChannelCubicKeysConstProxy | 0x3c | 0xa29950 | 1,214 | 0 |
| 4 | Proxy2 | 0x3c | 0xa2a1a0 | 5,305 | 0 |
| 5 | Proxy3 | 0x3c | 0xa2b910 | 8,297 | 0 |
| 6 and up | Proxy4 | 0x18 | 0xa2db10 | 21,767 | 13,518 |

Under version 7 it also rewrites the 12-bit field at bits 12-23 of the word at +8 of the image, adding an amount chosen by
thresholds. Each 0x3c element has an id at +0x24, translated through framework slot 3 with 0x5b016a10; a 0x18 element has none.

- So Proxy to Proxy3 are **old file versions**: none ran in seven lots, and they may ship in no package. They can't be
  called dead: custom content may carry them, and a resource file reaches them. Test them with synthetic images. First
  step of AN-1: count versions across the shipped ANIM resources, read-only, so the lane knows (a fact, safe to record).
- The image layout is the key shared layout of the campaign. A struct for it, with sizes asserted, comes before any proxy.
- The same image holds cEventChannelConstProxy (ctor 0xa29320) and cAnimTargetConstProxy (0xa326e0, element 0x1c).

**Interface chains.** Each is a vtable family with few bodies:
- cIChannelType > F1..F4, XYZ, Q (about 900 B each); cIChannelStrategy > Q, XYZ.
- cChannelUber > Attribute, Q, XYZ; cChannelUberOutlet (with cExtension) > Attribute, Q, XYZ.
- cChannelFunction > cChannelFunctionHair, nTSSG::cChannelFunction.
- cISharedChannel > cSharedChannel > FunctionHair, UniformFrames, nTSSG::cSharedChannelFunction.
- cActiveAnim > Attribute, Camera; cActiveAnimEvent, Loco, Skeletal; cActiveAnimIK > LookAt > LookAtDog.
- cIKStrategy > 7DOF, LookAt, LookAtDog. 0xa021a3 (LookAt, 10,313 B) and 0xa063b3 (LookAtDog, 10,142 B) are near-copies,
  as are their classes.
- cChoreoRequest > Anim (> LookAt, FromCamera), Loco; cChoreoHandler > Anim, Event, IK, Loco.
- cPathStrategy > Cubic (> InPlace, Linear); cPathData Cubic, Linear.
- cEventHandler > the custom handlers (cCustomEventHandler, 2, Cinematics, Reach).

## 4. Classes and layers

Clusters are classes that call each other most; the layers come from the run-time edges between them.

| cluster | classes | bytes | fns | ran | done | guess |
|---|---|---:|---:|---:|---:|---:|
| A0 rest | free nGZSceneDBAnim, director | 8,633 | 104 | 8,068 | 2,429 | 70 |
| A1 keys | cSharedChannelCubicKeys 10,466, ConstProxy 1,214, 2 5,305, 3 8,297, 4 21,767; UniformFrames 3,598; cSharedChannel 1,965 | 53,522 | 282 | 14,875 | 0 | 98 |
| A2 channels | types, strategies (Q 6,868, base 3,354, XYZ 2,083), cChannelData, Uber, outlets, cChannelFunctionHair 7,735, shared functions | 53,585 | 613 | 36,551 | 2,477 | 1,628 |
| A3 events and targets | cEventKey, List, Channel; cAnimTarget, cIKTarget, cIKChain 5,929; cBlendFunction2 5,745, cBlendInterval; cAnimTimer, cCustomAnimTimer2 | 35,758 | 453 | 19,434 | 710 | 1,068 |
| A4 paths, loco data | cPath, cPathData, cPathStrategy*, cWayPoint, cLocoCycle 7,981, cLocoSet 4,845 | 30,048 | 255 | 21,624 | 0 | 31 |
| A5 resources | cAnimResourceConst 36,665, cAnimResource 7,087, cAnimScene 6,690, cCinematicScene 4,934, cTSAnimTable | 56,574 | 283 | 7,544 | 540 | 1,657 |
| A6 active base | cActiveAnim, Attribute, Camera, Event, Skeletal | 24,842 | 125 | 20,461 | 0 | 505 |
| A7 active loco | cActiveAnimLoco | 23,013 | 44 | 18,519 | 0 | 1,877 |
| A8 IK, look-at | cActiveAnimLookAt 17,814, LookAtDog 12,284, cActiveAnimIK 8,262, cIKStrategy7DOF 8,513, LookAt, LookAtDog | 53,113 | 200 | 35,039 | 0 | 4,451 |
| A9 choreography | requests, handlers, cChoreoQueue, cAnimManager, cSceneDBAnimSystem 8,775 | 49,808 | 703 | 40,202 | 439 | 1,083 |
| A10 custom | cCustomChoreoQueue 21,662, cCustomEventHandler2 10,784, handlers, cAnimRequest, cAnimEventCB | 42,478 | 322 | 30,708 | 333 | 7,477 |
| A11 cObject | cObject 63,012 and the cIObjectV template stack | 68,689 | 507 | 51,940 | 0 | 10,223 |
| A12 cAnimatable | | 71,636 | 287 | 59,609 | 183 | 15,908 |
| A13 cLocomotable | | 52,302 | 283 | 42,483 | 210 | 13,049 |
| A14 recorder | cAnimationRecorder 22,903, cTSWinAnimationRecorder 5,940, VfW thunks | 29,365 | 75 | 17 | 0 | 10,843 |

**Layers** (run-time edges, distinct not-done callee bytes, of 150 B and up; a heavy back edge is noted):
- **L0, keys and channels.** A2 -> A1 13,326; A1 -> A2 4,266. A cycle by interface. A1 goes first: keys are data with
  few callees (only 18 B not done out of the campaign), and A2's calls into them are through cISharedChannel.
- **L1, data.** A3 -> A2 755, A4 -> A2 542, A4 -> A9 1,978 (a back edge: path code asks the queue), A5 -> A3 289.
- **L2, active animations.** A6 -> A2 5,065, A6 -> A3 1,805, A6 -> A1 822, A6 -> A10 9,783 (event dispatch through virtual
  handlers: a fake). A7 -> A4 6,976, A7 -> A6 1,099. A8 -> A6 2,389, A8 -> A2 1,519, A8 -> A3 1,192, A8 -> A10 1,328.
- **L3, choreography.** A9 -> A8 16,147, A9 -> A7 10,873, A9 -> A10 10,160, A9 -> A6 3,936, A10 -> A9 7,064, A10 -> A2 5,918. A9
  and A10 are a cycle by inheritance: cCustomChoreoQueue is a cChoreoQueue, cEventHandler is the base of the custom
  handlers. Base first.
- **L4, the chain.** A12 -> A11 21,722, A12 -> A10 9,164, A13 -> A12 6,360, A13 -> A9 5,282, A13 -> A4 1,956. Back edges
  A11 -> A13 3,045 and A11 -> A12 2,227 are virtual slots.
- **A14** stands off to one side.

Static cross-cluster call counts are misleading here: a few tiny folded stubs (0x2b6330 7 B, 0x326070 10 B, 0xae6870 43
B) carry most of them.

## 5. Shared test boundaries

**The fixture, `tests/animfx.h`, built once in three parts by the lanes that first need it:**
1. *Blobs* (AN-1). A builder of synthetic key images per version, with an independent image layout, plus a real-resource
   check: ANIM resources read at test time from `local/game`, never committed, SKIP without the copy. The pattern and the
   reader are in `tests/gmdc_decode_acceptance.cpp` and `tests/gmdc_package_fx.h` (`gmdcfx`: an independent DBPF, QFS and
   RCOL reader, also used by `tests/sg_render_object.cpp`).
2. *Targets* (AN-6). A target node fake with bone extensions (`tests/sgfx.h` node and extension fakes, real cExtension), a
   recording event handler, a timer fake. Extend sgfx rather than copy it (its header asks for that).
3. *The chain* (AN-10). Real chain instances, each side built by its own factory, the pilot pattern of
   `tests/sg_transform_pilot.h` and `tests/sg_transform_fx.h`; `tests/sg_tssg_wide.h` widened to 317 slots, with float returns
   and the vbtable at +4; the subobject thunks; a cITSObject owner fake.

Reuse as they are: `tests/ts_services_fixture.h` (director, allocator, framework cell), `free_recorder.h` and the three-sided
objects of `tests/anim_channels.cpp`, `tests/sg_anim_resource.cpp`, `tests/sg_anim_resource_const.cpp`.

**Where the method doesn't fit, flagged now:**
- **x87 against SSE2.** This is the campaign's main risk. The original computes in x87, ours in SSE2, and the game thread
  runs at 24-bit precision (`docs/lane/test.md`, lines on `TS2_X87_PC` and `x87::Rounded`). Rotation and key blending
  accumulate intermediates, so a difference shows in the last bits and then spreads. Run both precisions, as
  `tests/compgeom_rigid.cpp` does, from the first keys lane. A8, A7 and 0xa0aba0 (loco cycle, 4,720 B) have the most.
- **Library maths.** 32 calls of `_libm_sse2_sqrt_precise`, 16 cos, 16 sin, 8 acos, 13 `_ftol3`. Ours must use the same
  forms, in double, or the last bits differ.
- **Virtual re-entry.** The queue calls handlers that call the queue. Test a level at a time with a fake on the far side,
  comparing the order of events as well as results.
- **Sim behaviour is emergent.** What a lane can check is one class's calls and state. The whole (a Sim walks the right
  way) is for the host trace suite; so A9 to A13 should expect a `trace-verified` share, and the status table decides
  which (`docs/verification.md`).
- **Slots shared with cPerson:** section 3.
- **Width.** Generic fakes at 316 slots: sgfx's `kNode` has 62.
- **Resource bytes** can't be committed, so `animfx` builds images itself, with a real-file check beside them.
- **The recorder** needs fake AVIFIL32 and MSVFW32 imports (AVIFileInit, AVIFileOpenA, AVIFileCreateStreamA, AVIStreamSetFormat,
  AVIStreamWrite, ICOpen, ICSendMessage, ICSeqCompressFrame and the rest) and cImageReadback. They are the only imports in
  the campaign. No threads, clock or file calls elsewhere.
- **Unknown shape.** 0xa12640 (cAnimResourceConst vf44_62, 13,872 B, never ran) takes a source animation through virtual
  slots and fails with a too-many-frames message: it looks like the path compiling a source resource into the const form, so
  it might only run on a tool path. 0xa178a0 (13,436 B, never ran) has the field names of the resource as strings, a text
  dump by the look of it. Both unconfirmed. AN-5 starts with these two, since together they are half of the cluster's
  unrun code.

**Verified maths ready to call:** RigidTransform3 (0x19cd80, 0x19c900, 0x19c080, 0x19c800), RotateByQuaternion 0x1a6c40,
math::Quaternion 0x1a7020, NotchQuaternion 0x7dccd0, cRay3, cLinear3T, cRZRandom.

## 6. Coverage and scenarios

Coverage only decides what also gets the in-game check; a lane doesn't skip code that never ran. 62% ran. What didn't, and
the scenario to reach it:

| code | B | ran | scenario |
|---|---:|---:|---|
| Pets: cActiveAnimLookAtDog, cIKStrategyLookAtDog | 15,282 | 385 | a pet on the lot |
| Cinematics: cAnimScene, cCinematicScene, cEventHandlerCinematics | 13,150 | 0 | a story or cinematic scene |
| Camera: cActiveAnimCamera, cChoreoRequestAnimFromCamera | 6,877 | about 1,084 | the same, or a camera-driven animation |
| Video capture: A14 | 29,365 | 17 | the capture feature; a recorder start and stop |
| Targets: cAnimTarget, cIKChain | 10,272 | 3,899 | carrying, handing objects |
| Path strategies: Linear, InPlace | 3,363 | 0 | not known; look when the class is taken |
| cSharedChannelCubicKeys (non-const) 10,466, UniformFrames 3,598, cChannelStrategy 3,354 | 17,418 | 551 | a non-const resource path; not known what reaches it |
| Key proxies 1-3 | 14,816 | 0 | an old-version resource, none known |
| 0xa12640, 0xa178a0 | 27,308 | 0 | none known, see section 5 |

A lane with a real reach question asks the coordinator for a coverage run; these are its candidates, with pets and a
cinematic worth most.

## 7. Next lanes

From the leaves up, each 20-60 KB. KB is to do (filed plus guess, less done). Lanes in the same layer can run two at a
time; they share only `animfx.h`, so part 1 goes first. Model: Sonnet medium where the recipe is known, Opus where the
structure is new.

| lane | clusters | KB | needs | risk | model |
|---|---|---:|---|---|---|
| AN-1 | A1 keys. The image layout, the proxies, UniformFrames, cSharedChannel. Fixture part 1 | 53.5 | - | high: image layout, x87, versions | Opus |
| AN-2 | A2 + A0 channels, Uber, outlets, Hair, free helpers (existing recipe, `tests/anim_channels.cpp`) | 57.3 | AN-1 | medium: Hair's 0x9f1170 rigid maths, 4,306 B | Sonnet medium |
| AN-3 | A3 events, targets, IK chain, blends, timers | 35.0 | AN-2 | low-medium | Sonnet medium |
| AN-4 | A4 paths, loco cycles and sets | 30.0 | AN-2 | medium: float | Sonnet medium |
| AN-5 | A5 resources. First the two unrun big functions | 56.0 | AN-1, AN-3 | high: the two unknown bodies | Opus |
| AN-6 | A6 + A7 active animations (0x9fab64 10,055 B). Fixture part 2 | 47.9 | AN-2 to AN-4 | high: first driven animations | Opus |
| AN-7 | A8 IK and look-at; the Dog variant as a near-copy | 53.1 | AN-6 | high: x87 | Opus |
| AN-8 | A9 choreography base | 49.4 | AN-6, AN-7 | medium: re-entry | Sonnet medium |
| AN-9 | A10 custom queue, handlers, requests | 42.1 | AN-8 | medium: 7.5 KB guessed | Sonnet medium |
| AN-10 | A11 cObject, first half: layout, templates, ctor. Fixture part 3 | about 35 | scenegraph node templates | high: new structure | Opus |
| AN-11 | A11 cObject, second half (0x25dd70 vf3 4,716 B, 0x268b70 vf8 4,123 B) | about 34 | AN-10 | medium | Sonnet medium |
| AN-12 | A12 cAnimatable, big slots vf29-34 (vf34 0x27aae0 10,821 B, vf33 5,288 B, vf29-31 about 3.6 KB each) | about 35 | AN-9, AN-11 | high | Opus |
| AN-13 | A12 cAnimatable, the rest | about 36 | AN-12 | medium | Sonnet medium |
| AN-14 | A13 cLocomotable (0x27f720 5,387 B, the walk-style parser) | 52.1 | AN-13 | medium | Sonnet medium |
| AN-15 | A14 recorder. Deferred | 29.4 | fake VfW imports | low, and nothing needs it | Sonnet medium |

Total 646 KB in 15 lanes. AN-10's split point is by class of slot, and can move once the layout is written down.

## Left

Not checked:
- Guessed rows one by one; the 0x7dd000-0x7f3000 module.
- 0xa12640 and 0xa178a0 (above) were read only at their heads.
- Which ANIM versions ship, and the bit-field rewrite's meaning.
- Whether `shared_vf_*` bodies in the chain are referenced only by the family: a lane step, per body.
- Dead-code proofs: none claimed. Code that never ran is listed as not reached.
