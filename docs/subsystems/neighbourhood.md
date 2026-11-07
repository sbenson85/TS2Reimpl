# Neighbourhood: the neighbourhood view and its decorations (subsystem notes)

Working notes for lanes in the `neighbourhood` subsystem (db/subsystems.jsonl, `tools/subsystems.py --summary`). The data
classes of a neighbourhood (cTSNeighborhood, cTSNeighborManager, cTSNeighborhoodManager, cTSNHoodOccupantManager, the lot
infos ...) are covered in docs/subsystems/persistence.md and are nearly all
verified and in the standalone shape. This file is for what is left: the scene side of the neighbourhood view, the
decoration occupants and the neighbourhood UI.

## What is left (lane GO, 2026-10-18)

Unverified game code in the subsystem, by class (subsystem basis `vtable:` / `ctor:`), largest first:
nTSSG::cNHoodOccupantManager (all done, lanes GR, GW, HB), nTSSG::cNHoodFloraGeometryNode (2.9 KB),
cTSNHoodRoadOccupant (2.4 KB), nTSSG::cNHoodAutomata (1.9 KB), cTSNHoodFloraOccupant (1.7 KB), cTSNHoodRoadOccupantWithModel,
cTSNHoodPropOccupant and their base cTSNHoodOccupant (together with Road and Flora about 5.8 KB: a family of small data
classes, the next sensible lane), then the cTSWinProc* neighbourhood panels (UI, 0.2-0.8 KB each). About 25 KB more is in
helpers assigned `called-only-from` (0x4aaf80 and 0x4a8640 are cTSNHoodOccupantManager's road tracer, still original calls of
that class; 0x471570 / 0x471980 / 0x471790 are cTSNeighborhood::Import's).

## nTSSG::cNHoodOccupantManager (lanes GO, GR)

src/ts/nTSSGNHoodOccupantManager.{h,cpp}, src/hook/tables/nTSSGNHoodOccupantManager.cpp, tests/ts_nhood_sg_manager.cpp,
db/status/nTSSGNHoodOccupantManager.jsonl.

- **What it is**: the scene side of the neighbourhood decorations. It listens for ten message ids, watches the property
  "neighborhoodPropFadeDistance", builds scene-graph nodes for the occupants cTSNHoodOccupantManager holds under one root group
  node (four layer nodes at +0x64 shown / hidden by slot 6, a merged flora node at +0x94 unless "dontMergeNHFlora"), and
  detaches them from the scene (DetachScene 0x2a56d0).
- **Faces**: rz::cRZUnknown (+0, 6 slots: QI 0x2a8da0, the folded AddRef / Release, the deleting destructor 0x2a5310, the
  base's RemoveRef / RefCount), nTSSG::cINHoodOccupantManager (+8, 12 slots, declared in the class header),
  cIGZMessageTarget2 (+0xc). 0xd4 bytes; layout in the header (every field from the constructor and destructor).
- **Creation**: the creator 0x2a7b70 (cdecl, `new 0xd4`, the +8 face; its address is stored by the nTSSG cDllCOMDirector
  constructor 0x244e30) calls the constructor 0x2a4a00, its only caller. The vtables are referenced only by the constructor
  and the destructor 0x2a50f0 (called only by 0x2a5310). The switch is the creator hook plus the constructor hook.
- **Done** (GO): constructor, creator, destructor and deleting destructor, QueryInterface and its two thunks, Initialize,
  Shutdown, SetLayerVisible, MarkDirty / ClearDirty, Update (slot 10), DetachScene. (GR): BuildScene (slot 5), SetMergeFlora
  (slot 9), UpdateFade (slot 11), DoMessage, and DoMessage's member helpers RefreshOccupant 0x2a9ba0, ApplyRef78Setting
  0x2a8020 (both byte match), HighlightOccupant 0x2a8040, ClearOccupantHighlight 0x2a8e30, ApplyTexts 0x2a9260. Every slot is
  ours; difftests run three routes (original; our object through our vtable; the adapter on a game-built object with a trap
  vtable). The adapters stay: `callers.py --vtable-only` rests each slot verdict on two hooks (Construct and Destruct).
- **What the slots do**: BuildScene(all) builds the root group node and four layers (framework slot 3), either for the whole
  neighbourhood or only the occupants whose footprint (0x2a8570) lies in the viewed lot's rectangle (grown by 3), merges flora
  per layer unless "dontMergeNHFlora", builds the app slot 12 "others", registers the root with the scene and sets mDirty = 1
  ("scene built"; DoMessage's occupant messages act only then). SetMergeFlora(merge) moves flora between the merged node
  and per-occupant nodes (ORIGINAL-BUG: a null occupant's layer -1 reads the byte at +0xb7). UpdateFade(view) fades props
  by distance: the camera service (0x4a037ee3), the view position (slot 140) on the ground (0x7ddd30, z = 0), each prop's
  box (slot 72), alpha = 1 - pow(d / fade, 4) as colour (0x2a55e0) and the ShapeColor flag (0x2a5560). DoMessage handles
  ten ids: occupant added / removed / changed / highlighted, rebuilds (BuildScene(0) unless build-all), the fade distance
  property (squared into mFadeDistanceSq; 0 unfades every faded prop), text pairs, the +0x78 setting.
- **Helpers done** (lane GW, hooked at their RVAs, difftested original vs our member): ChangeNodeFlags 0x2a5560 and
  SetNodeColor 0x2a55e0 (cdecl, recursive over a node's children: slot 42 gives {first, ?, end}, a child's slot 38 the next;
  slot 48(4) gates the work; the colour is a nGZEvent::cVectorEvent on the stack, vtable 0xdb07d4, sent through slot 7 with id
  0x8c798726; 0x2a55e0 is also called by 0x2b0d30), LayerOf 0x2a7f20, FootprintIn 0x2a8570, MergeOccupant 0x2a5340,
  ClearLayers1And2 0x2a5860, AddOccupant 0x2a7500 (dispatch by face: 0x4bd0e45b flora, 0xabd4c3f4, 0x6bd4d6ce, 0x2c0c10c8
  others), RemoveOccupant 0x2a8e70 (the node's object 0xbb263d5 is counted through a virtual base), HandOnPending 0x2a9100
  (reached through the thunk 0x2a8560; the props list is a vector of 0x34-byte entries, two allocator-service strings and a
  word, destroyed by the shared tidy 0x2a4ef0, which our PropsList does itself), RebuildMergedLayers 0x2a5d70, BuildFloraNode
  0x2a5a40.
- **Node builders done** (lane HB, hooked, difftested on the same fakes with the callees stood in, every caller switched):
  PrepareBuildAll 0x2a81f0, MergeLayerChildren 0x2a7be0, AddFaceCOccupant 0x2a7130, AddFloraOccupant 0x2a5ef0,
  AddOtherOccupant 0x2a63a0, AddRoadOccupant 0x2a69b0 (face 0xabd4c3f4: the road's quad and its triangle pieces as meshes
  named "neighborhood-roads-<hood>-%08x" / "neighborhood-roads-%08x"; the quad's two triangles come from the index table
  at 0xdb3544, read at run time). The class has no helpers of its own left.
- **Still game code** (original_calls), all outside the class: 0x2a4410 / 0x2a4cb0 (the +0x7c vector's insert / copy),
  0x2a5900 (a triangle's normal), 0x2a8640, 0x2a9000, 0x2a9c60 (the +0x88 vector's push), 0x2a53f0 (a node attached to its
  placement), 0x211ce1, 0x211da3, 0x267560, 0x1a72c0, 0x7ddd30, 0x19d340, and the scene-graph helpers 0xaeb9a0, 0xaeb8a0,
  0xaeba70, 0xaeac50, 0xaeb070, 0xaed340, 0xaead80, 0xaeb370, 0xae7e60, 0xb3c070.
- **Pitfalls (HB)**: the fixture's dispatcher logs pointer arguments raw, so a name in a heap block on one side differs:
  give the callee its own slot that logs the text. The svcfx fakes have 80 slots: past that, copy the table into a longer
  one (`Extend` in the test). The meshes of AddRoadOccupant and the extras of AddOtherOccupant are counted through a virtual
  base (vbtable at +4); a group's "answers a group" slot is on the second virtual base (`VirtualBaseAt(obj, 2)`). Size every
  out-buffer from the original's frame, not from the words it reads: +0x78's entry iterator (slot 5) fills 16 bytes though
  only two words are read; an 8-byte buffer passed the difftest and crashed in game (fakes now fill the whole 16).
- **Pitfalls**: the property set and notifier are called without a null test (the original's too); the option mask
  (0x10faf94) must be non-zero in tests or HasOptionBits queries the app; the property names are longer than 15 characters,
  so 0x1773e0 allocates through the game's allocator service (tests need `AllocatorScope`). The vectors at +0x7c (pairs of
  cRZString), +0x88 (counted node, faded byte) and +0xbc (counted pointers) are filled by game code: tests build them by hand.
  UpdateFade's bounds come back through x87 and the half extents are computed in double; keep that order or results differ in
  the last bit. 0x2a8020 pushes `[0xffac34]`, a pointer cell in the image's data, not the global 0x10fac34. The constructor
  writes bytes at +0x24 and +0x60 only, so a snapshot must mask the padding after them.
- **Next**: the class is done; its remaining original calls belong to other classes (the scene-graph helpers at 0xae....,
  the vectors' template instances), best taken with those classes.

## Left (from the backlog, 2026-11-03)

- cTSNeighborManager CollectReferenced: AddSourceKey returns bool in the shared interface but the original ORs the raw byte (FR). cTSNHoodOccupantManager: 8 slots still game code (FO list).
