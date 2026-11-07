# Persistence: saving and loading the neighbourhood (subsystem notes)

The current focus subsystem (agreed with the user 2026-10-15). Working notes for lanes in it: scope,
what's done, conventions and pitfalls, what's left. Keep it short and current; the lane that changes
the subsystem updates this file on its branch (facts, not decompiler output). Map: `docs/subsystems.md`
rows "persist-dbpf" and "neighbourhood". Verification: `verified` by difftest (this is data code, not
simulation); in game, the townie save scenario (`lot-townie-trace`) reaches most of it.

## Why this subsystem

It is the save/load path the save-comparison work (milestone B, `docs/save-diff.md`) checks end to end:
load a prepared save, re-save, reload, and compare, including an offline funds edit seen in the UI.
Owning it means our code reads and writes the neighbourhood files.

## Scope

- Storage layer: cGZPersist* (resource manager, factories, binary reader/writer, records), cGZDB*
  (DBPF segments and indexes), cTSPersist* (persist system, context, records, segment info, cache
  strategy, speculative loader).
- Neighbourhood data: cTSFamily, cTSFamilyManager, cTSFamilyHistory, cTSFamilyRelationshipArray,
  cTSNeighbor, cTSNeighborManager, cTSNeighborhood, cTSNeighborhoodManager, cTSNeighborhoodInfo,
  cTSNeighborhoodGroupData, cTSLotInfo, cTSRelationshipTable, cTSNeighborGeneticDataTable,
  cTSSimDNA, the data import/export contexts.

## Subsystem map (lane CU, 2026-10-17)

How a neighbourhood is loaded and saved, top down, and which class owns each resource. Facts are in the shared
Ghidra program (lane CU, tentative unless noted) and `db/`; handoffs and the evidence are in notes/feedback/CU.md.

**What the files hold.** `N001_Neighborhood.package` (tools/dbpf.py summary) by type, with the class that reads
and writes it (`TSResourceType` enum in Ghidra; factory from cTSPersistSystem::Init 0x1e0d60, primary slot 20
RegisterFactory(factoryClass, type, 0)):

| type | records (N001) | class (Read / Write) | factory | status |
|---|---|---|---|---|
| SREL 0xcc364c2a | 2597 | cTSRelationshipTable (Read 0x55b420 1.6 KB / Write 0x55c170) | built by cTSNeighbor | table done (lane CX); its cTSRelationshipEntry records not |
| 3IDR 0xac506764 | 1876 | cTSPersistResKeyList | generic | structure pass CE |
| BINX 0x0c560f39 | 1851 | nTSCASCore::cTSSimFragment, loaded by cTSBinInfoFactory (Read 0xb8ddb0 / Write 0xb90e10) as a binary property set | own (CAS) | structure (DM) |
| memories 0xcd95548e | 280 (1.87 MB, 2/3 of the file) | cTSWantSimulator slot 6 StoreSimWants 0x4985e0 / slot 7 RestoreSimWants 0x493510 / ReadSimRecord 0x492ea0 (per sim id): wants block (WriteSimWants 0x4983a0, v6) + memories block (WantMemoryStore::Write 0x551700, v2), entries are cTSWantInstance (Write 0x550830, v10) | none (own record I/O) | structure pass CY (format in notes/feedback/CY.md) |
| property set 0xebfee33f | 269 | cGZPropertySetFactory (0xc2c173 / 0xc2c2be), XML cGZPropertySetString; the genetic data table stores them | own | structure (DM) |
| SDSC 0xaace2efb | 175 | cTSNeighbor | generic | done |
| STR# 0x53545223, CTSS 0x43545353 | 62 + 1 | cTSStringSet (WriteStrings 0x1ef4e0; Commit = slot 21; format 0xFFFD in saves, DA.md) | 0xdb4cb1e8 cTSResourceFactoryStringSet | structure done (lane DA) |
| LOT_INFO 0x0bf999e7 | 26 | cTSLotInfo | generic | done |
| collection 0x6c4f359d | 25 | (UI collections, cTSUICollectionsSupport) | ? | not persistence |
| FAMI 0x46414d49 | 11 | cTSFamily | generic | done |
| NGBH 0x4e474248 | 1 | cTSNeighborhood | generic | done (vf8_5 left) |
| NHOOD_INFO 0xac8a7a2e | 1 | cTSNeighborhoodInfo | generic | done |
| TATT 0x54415454 | 1 | cTSObjectTypeAttributeManager (0x4d8ca0 / 0x4d9060) | generic | done (lane DN, 12 functions) |
| 0xcc2a6a34 | 1 | cTSNeighborManager | generic | done |
| 0x8c870743 | 1 | cTSFamilyRelationshipArray | 0xac870757 | mostly done |
| NHOOD_DECORATIONS 0xabd0dc63 | 1 (82 KB) | cTSNHoodOccupantManager (0x4aa110 / 0x4ab6f0) | generic | structure done (DD; v 0x6f shipped, 0xcb written; 4 occupant lists) |
| NHOOD_TERRAIN 0xabcb5da4 | 1 (83 KB) | cTSNHoodTerrain (0x4b0980 / 0x4b2fb0) | generic | structure done (DD; v 4, 2 c2DArrays, the u8 one never read) |
| 0x2c310f46 | 1 | cTSNeighborGeneticDataTable | 0xcc3113e4 | done |
| 0xec44bddc | 1 | cTSCameraSmoothSims1Controller 0xb48f30 / 0xb4aa40, through nTSSG::cTSSGSystem vf76 / vf77 | neighbourhood camera state, 115 bytes (`docs/save-diff.md`) | scenegraph |
| 0xebfee342 | 1 | cTSSaveGameInfo (Save 0xcd8770 / Load 0xcd7ed0) via the property-set factory, binary | own | structure (DM) |

Also registered with the generic factory: FAMh cTSFamilyHistory, 0xcc8a6a69 cTSNeighborhoodManager (in
NeighborhoodManager.package), SIMI cTSSimulator, 0xb21be28b cTSWeatherInfo, 0x104f6a6e cTSBusinessInfo, 0x3053cf74
cTSDataTables, 0xcd8b6498 nTSSG::cLotImposterDesc, the three import/export contexts (0x2db5c0f4, 0x6db6f410,
0x6e9c59cf), and object types (OBJD, BCON, BHAV, ...). String sets also cover TTAs and FAMs. Lot packages hold
world-lot types (LOT_OBJECT, XOBJ, terrain, walls...), plus SREL, SIMI, BASE_LOT_INFO (cTSBaseLotInfo 0x4ca120 /
0x4cb7e0) and the contexts: they belong to the world-lot subsystem, not to this one.

**Record <-> object.** cTSResourceFactoryForIGZSerializable (class 0xaba91d51; cIGZPersistResourceFactory slots in
db/methods/cTSResourceFactory.jsonl): slot 3 CreateInstance(record, riid, out, a4, a5) takes the record's key type as the
framework class id, creates it (iid 0x456b8f1d), QIs riid, then slot 5 Read(resource, record) = resource QI 0xe4fda3d4
(cIGZSerializable) -> Read(record QI 0x51b11bd cIGZIStream). Slot 6 Write is the same with 0xc51b11cb (cIGZOStream) and
Write. So every generic type is "framework class id = resource type id" plus the class's cIGZSerializable Read/Write.

**Load** (cTSLoadNeighborhoodController::Step 0xc471da, state at +0x10; the controller holds the info +0x1c, group
+0x20, globals setter face +0x2c, persist system +0x3c, neighbour manager +0x44, neighbourhood manager +0x38):
0 make the info current (manager slot 32); 2 open every `*.package` of the hood's directory (persist ITS slot 22, the
`_Neighborhood` name, "noStupidIFFUnlocking"); 3 post message 0xc5; 4 0xc49442(info); 5 create the group data
(class 0xf05f6b60) and Refresh(info) -> cTSNeighborhoodGroupData::LoadNeighborhoods 0x476a40 (NGBH {group, 1} for the
main hood and each sub-hood: generic factory -> cTSNeighborhood::Read, then its slot 6 Initialize); 7 terrain, 8
occupants (decorations), 9 TATT (when flag bit 0 is clear); 10 state service "neighborhood"; 11 neighbour manager
(0xcc2a6a34, then its slots 6 and 8(group, 1) load the neighbours); 12 a pass over the object list (OBJD keys); 13
genetic table (created when missing); 14 0x28a630 and a segment lookup; 15 family manager (class 0x2c1e46ae: slot 3, slot
5 Load(group)); 16 family relationship array (created when missing); 17 neighbour manager slot 25; 18 group-data slot 35;
19 store "lastLoadedNeighborhood". Every load is persist primary slot 5 GetResource(key, iid, out, 0, 0).

**Save** (cTSSaveNeighborhoodController::Step 0xc489e8, states 1..6): 1 collect the groups' resources (persist ITS
slot 48 GetSegmentsForGroup twice into the list at +0x28, then slot 49); 2-5 calls into the systems through the
globals (cITSGlobals, Singleton_20ddfb): 2 neighbourhood (slot 6) slots 35 and 13 -> 6, family manager (slot 9) slot 7;
3 (when +0x19) terrain (slot 11) slot 20, slot 12 -> 5, slot 25 -> 10 and the state service's slot 77; 4 slots 28/29
objects (their slots 7, 16), game state controller (slot 23) slot 18, slot 4 -> 8; 5 object table (slot 26) slot 45 list
-> neighbourhood slot 29 per element, neighbour manager (slot 10) slots 11 and 10(0) (meanings not settled); 6 framework listener 0xcdcbe0e3 /
0xcdcbe0dd, then persist slot 50 (list, 1) commits. Each object saves itself with persist ITS slot 16 SaveResource(res,
segment) -> factory slot 6 Write. The lot save (cTSSaveLotController::Step 0xc45a3d, 23 states) writes the lot package
through a cTSObjectFileRef (class 0xb54e37d): persist ITS slot 72 WriteResourceToFileRef, and deletes the file's old
neighbour / neighbourhood context records (slot 36 list keys, slot 17 delete) before writing new ones.

Ghidra: the controllers are /O1 code with `_EH_prolog` frames. Their decompiles stop after the prolog (flow overrides on
the 0x8248b0 call sites) except where a function was created late (0xc471da, 0xc45a3d decompile fully); read the
disassembly for the rest, or see notes/feedback/CU.md for the server fix.

## Milestone B: understood differences in the lot re-save (lane HA, 2026-10-18)

Every difference between reference re-saves of lot 10 (`lot-resave`, 26 fixtures) and between them and the
replacement re-saves, with its cause; `tools/lotcheck.py` normalises the first three (docs/save-diff.md,
"Normalised classes"), the last two are open. Facts tentative in Ghidra (lane HA) unless noted.

- **OBJM 4F626A4D/1 = cEdithObjectModule's save** (vf5 0x42b370 writes instance 1 with vf111 0x42ee60 header and
  object list, vf114 0x42ec30 schedule table and the rest; instance 2 with vf116; vf7 0x42bd00 is the stepwise
  load, states 1 and 7 read instances 1 and 2; vf113 0x42e610 reads the table back verbatim, OR-ing 0x10000 into
  two special objects' flags). Module fields: object-by-id table +0x1a24 (vf29 0x4281f0 = GetObjectById(id),
  1-based), schedule words +0x1a30 parallel to it, object list +0x1a3c (count +0x1a40).
- **Object ids.** AddObject 0x422910(object, id, flag): id 0 means the lowest free slot from 1; the slot's
  schedule word starts at 0x0000ffff. The objects created on every lot load (type words 0x51, 0x112, 0x118 on lot
  10) are created in an order that varies, so they take permuted ids, and sometimes a different set of free ids
  (0xf4 instead of 0x186). Their records, and the records of the objects deleted on load, stay in the package
  (the save writes only the live objects' records: vf112 0x42ebf0 -> object slot 264 0x53d590 -> cTSObject::Write
  0x53ebf0).
- **Schedule words** = flags << 16 | countdown: vf71 0x42b6c0 sets the countdown, vf73 0x4280e0 reads it, vf74
  0x42b9b0 sets/clears a flag, vf75 0x4283c0 tests one. The simulator's object pass, cTSSimulator vf4_11 0x483190
  (`this` = simulator+4; resume id at +0x11c): skip objects with flag 0x10000; flag 4 handling; requires flag 1,
  and flag 2 while vf4_26 0x480fe0 (word simulator+0x42 nonzero) is true; skips objects whose slot 13(0x38) & 7;
  countdown < 1 runs the thread (object slot 236 0x3b0), a false return ends the pass and keeps the resume id;
  countdown >= 1 is decremented when vf4_26 is true or +0xde bit 1 is set. In the re-saves only flag-1+2 entries
  move, by a shared pass count (1169..1392 passes; more load, fewer passes), so vf4_26 was true throughout (most
  likely the pause while the Options panel saves; not confirmed).
- **XOBJ 0x173 word 0x1ba**: (baseline + passes) mod 30 in every run; field not identified (open).
- **EC44BDDC/0x262A floats**: the neighbourhood camera, moved by the idle drift camera's random walk on real time; held to a relation from each side's loaded record (`docs/save-diff.md`, "The neighbourhood camera record").
- **Reload leg**: a save made from an earlier save is a chain (`reload-N` fixtures, bound to the root baseline's
  digest). On load the schedule is read verbatim (vf113), so countdowns and the 0x173 counter run on from each
  side's own previous save; the load again deletes and recreates the per-load objects, so fresh ids are measured
  from the root; stale records must equal each side's own previous save. Per-load objects are the types 0x51,
  0x112, 0x118 (one object each), matched by type wherever they land (a reload sometimes recreates them at the
  root's 0x187..0x189). Each keeps its predecessor's id (the deleted object of its type) at XOBJ 0x27e, so the
  two first saves' per-load objects are matched by type too. Schedule entry 0x15d: countdown + session passes =
  20005 or 20006 in every session (reset one pass apart; why not established; compared exactly). Commands,
  status and verdicts: docs/save-diff.md, "Reload leg".
- **Verdicts 2026-10-18**: all 26 references MATCH-OPEN against each other once normalised; the replacement
  re-saves of commit 46e5f1b: notes/feedback/HA.md.

## Next lanes (lane CU's ranking, 2026-10-17)

For milestone B (save, reload, re-save, compare), ranked by bytes of the save they own and by what they unblock.
Sizes are the class totals (cu_sum); "done" counts verified.

1. ~~cTSRelationshipTable finish~~ done by lane CX (2026-10-17). ~~cTSRelationshipEntry~~ done by lane CZ
   (src/ts/cTSRelationshipEntry.{h,cpp}: 0x2c bytes, `std::vector<uint32_t> mValues` at +0x20; no ctor/dtor function in
   the game, the table builds it inline and Release destroys it, ours is `new cTSRelationshipEntry(key)`; every virtual
   and the four face thunks hooked, 5 byte matches, the rest difftests with EH injection). The old-STL vector
   instantiations 0x55c450 (insert_n, SetValueCount's only caller), 0x1d7050 and 0x1461c0 are no longer called from
   it. Record stream: u32 version 2, u32 count, u32 values (IIDs 0x051b11bd read, 0xc51b11cb write).

**SREL format (lane CX).** Read: s32 version first. Versions 2/3: u32 count, then u32 person keys; every key maps to a
null entry, loaded lazily by LoadEntry 0x55b090 (SetEntry 0x55ac60 builds a cTSRelationshipEntry with key {table type,
table group, owner << 16 | person, 0}, then the segment's OpenRecord(key, &rec, 1) and entry slot 4 ReadFromRecord, record
slot 6 Close). Negative versions (old inline format): s32 count, then per slot an s32 present flag; 0 nulls the entry at
key = slot index, otherwise u32 person, then the entry's own cIGZSerializable Read (s32 count, u32 values) inline. Any
other version returns 0 (after the pre-clear and key setup). With a record stream (QI 0xe56b8f03), Read takes the
record key, swaps mStorage for resource manager slot 34 FindSegmentWithKey, and sets type/group to (CLSID, key group)
for version 3, else (key type, 0xac3a7122). Without one, type/group/storage are left alone. Read never checks stream
errors: a short stream reads whatever the stream leaves in the locals.
2. **cTSWantSimulator persistence** (class 34.5 KB, 28 done). Two thirds of the neighbourhood file's bytes. Structure
   pass done (lane CY, 2026-10-17): handoff, record format and lane split in notes/feedback/CY.md; header
   src/ts/cTSWantSimulator.h declares the members (slot, RVA, ret N) and the WantMemoryStore / WantSimContext layouts.
   What a later lane needs:
   - The record {0xcd95548e, hood group, sim id, 0} is two blocks: the sim's wants (lifetime want, 4 want slots, 3 fear
     slots, one short from +0x48) and its memories (WantMemoryStore: map guid -> list of cTSWantInstance, one per sim in
     +0x28). Every entry is a **cTSWantInstance** written through its cIGZSerializable face, so the bytes also need
     that class's Read/Write (0x550420 / 0x550830, its own group, 38 functions to do).
   - Save: SaveAllSims (slot 8) per loaded sim: CopyPersonToNeighbor for live persons, else StoreSimWants. Load:
     RestoreSimWants pads wants to 4 / fears to 3 and queues the sim (slot 35). Delete: DoMessage 0x9d/0x9e ->
     DeleteSim 0x487be0 (segment slot 26 DeleteRecord). Import: cITSDataContainer slot 5 0x492fd0 remaps sim ids.
   - `TokenWantFearService` is an alias of cITSWantSimulator (globals slot 4 is the want simulator).
   - Lane DB (2026-10-17) did the record path: fields renamed, WantMemoryStore (7), GetMemoryStore, AddDefaultMemory,
     IsWantAllowed, WriteSimWants, ReadSimWants, ReadSimRecord, StoreSimWants: verified by difftest (tests/ts_want_simulator.cpp,
     namespace pw: instance pool page, globals/hood-key chain and persist fakes, scripted streams). Left of lane 1:
     WantSimContext::Construct, RestoreSimWants, SaveAllSims, DeleteSim, ResetMemories, FlushUpdates, QueueSimUpdate
     (QueueSimUpdate has dead code for priority 0/1, see notes/feedback/DB.md); the pw world covers their needs.
   - Implementation: lane 1 (persistence, ~150 calls) can start now; then glue/leaves; cTSWantInstance as its own
     lane; RunUpdate 0x4880b0 and slots 52/53/58/59/61 (~18 KB) need a structure pass first.
3. **cTSStringSet + cTSResourceFactoryStringSet** (34 + 8 functions, 6.5 KB). Every family, neighbourhood and catalog
   text (STR#, CTSS, FAMs, TTAs). **Structure pass done (lane DA, 2026-10-17)**: all 38 cITSStringSet slots named (21 is
   now Commit, so FamilyStringTable can become an alias of cITSStringSet: coordinator), headers src/ts/cTSStringSet.h
   (layout 0x7c with static_asserts, every function as a member with RVA/ret), record formats and per-function handoff in
   notes/feedback/DA.md. 0x1ecf60 is GetStrings(language) and 0x1ec7e0 GetComments(language): the lazy per-language
   loaders (parse every format version; nothing is loaded at CreateInstance). Implementation split in three lanes
   (DA.md): factories + shell and readers; loaders and save; editing.
4. **Generic factory cTSResourceFactoryForIGZSerializable** (9 functions, ~0.7 KB). Every generic record passes through it.
   Structure done (lane DA): src/ts/cTSResourceFactoryForIGZSerializable.h, handoff in DA.md (CU's checked). Goes with
   implementation lane 1 of the string set.
5. **cTSNHoodTerrain** (49 functions, 8.2 KB) and **cTSNHoodOccupantManager** (32, 7.7 KB): one record each, but 165 KB
   of the file and two load steps. Structure done (lane DD): src/ts/cTSNHoodTerrain.h (with nTSWorld::c2DArray<T>,
   0x1c) and src/ts/cTSNHoodOccupantManager.h; record formats, per-function entries and a four-lane split in DD.md
   (1 terrain persistence + c2DArray, 2 terrain edits/derived maps, 3 occupant manager core + persistence, 4 roads
   and queries + a structure pass on the four cTSNHood*Occupant classes, whose Reads carry the decorations' bytes).
   LoadTerrainTypes (ini file I/O, 4.3 KB) is left for a later lane.
   Lane DF (terrain split item 1) done: the 33 lane-1 functions plus the six c2DArray ones (db/status/cTSNHoodTerrain.jsonl,
   c2DArray.jsonl; tests/ts_nhood_terrain.cpp, ts_c2darray.cpp, fakes in ts_nhood_fakes.h). Corrections to DD: Read runs
   ResetVertexFlags and RegisterCheatAndMessages only for version >= 3, and reads the height array only when mHeights is
   null (an existing array is kept and the stream is not consumed); the primary QueryInterface has no message-target id;
   the vertex-water tests and SetHeightAt bound x, y by the vertex counts (+0x1c/+0x20), the cell tests by the cell counts;
   SetSize does not zero the normals or face-row elements it resizes (0x3b58f0 default-initialises them) but does zero
   each new paint layer's type and weight (0x4aca60; the pad byte is left; corrected 2026-10-17 after the in-game crash), and
   resizes the weight map embedded at +0x6c of every node of the static type list (head cell 0x10dab04). Left for the
   later terrain lanes: lane 2 (edits, derived maps), LoadTerrainTypes, the faces' remaining slots.
6. ~~cTSObjectTypeAttributeManager~~ done by lane DN (src/ts/cTSObjectTypeAttributeManager.*, tests/ts_object_type_attribute_manager.cpp):
   factory 0x4d8b60 (found from the vtable; the cTSSimCOMDirector constructor registers it), all 10 own slots and the +4
   QI thunk. The attribute objects are cEdithObjectTypeAttribute (cIEdithObjectTypeAttribute 4 GetCount, 5 GetValue, 6 GetId,
   7 GetData: an id at +8 and a u16 vector at +0xc). Quirk: AddAttributes asks GetId twice on insert (find, then the pair).
7. **Property sets** (cGZPropertySetFactory 4 functions 1.1 KB + the property-set class) and **cTSBinInfoFactory** (BINX):
   CAS data the families save (1851 + 269 records). Shared with the CAS subsystem. Structure pass done (lane DM,
   notes/feedback/DM.md "## Handoff"; headers src/gz/cGZPropertySet*.h, src/ts/cTSSaveGameInfo.h,
   src/cas/cTSBinInfoFactory.h): the string-keyed property set (cGZPropertySetString on cGZPropertySetT<uint32>), its
   cIGZAny values, resource, binary/XML serializers, the factory, cTSSaveGameInfo and cTSBinInfoFactory, ~120 functions
   split into four lanes PS1 (set core, first), PS2 (Any values, cheap), PS3 (binary path, resources, factories),
   PS4 (XML serializer, cTSSaveGameInfo).
   **PS1 done (lane DP):** src/gz/cGZPropertySet.size.cpp, 30 functions verified by difftest (tests/gz_property_set.cpp).
   The sets hold `std::vector<cGZPropertyEntry>` and `std::vector<char>` (key text); string keys are `GZServiceString`
   (std::basic_string over GZServiceAllocator: the game's ibasic_string allocates through the allocator service).
   Interfaces `cIGZPropertySetString` / `cIGZPropertySetUint32` (db/methods/cGZPropertySet.jsonl) replace the ad-hoc
   DNAPropertyStore / PropertyTextService / SimPropertySink. Left: the shared_vf slots Release 0x1bf485, Clear 0x1bee23,
   GetCount 0x1bf41f, KeyToString 0x1bf336 (written and tested, folded with other classes so not hooked); deleting
   destructors 0x1bedac / 0x1bed8d (written and tested; hook once db/names.jsonl is regenerated: its stale library name
   makes hookspec refuse them); STL instantiations 0x1bebca (lower_bound), 0x1bf8d4 (vector erase), 0x1be8b6
   (vector<char> insert), 0x1bed67 (string assign) are no longer reached from our code. Pitfall for tests: one
   difftest::FreeRecorder at a time (a second one resets the first's results).
   **PS4 mostly done (lane DV):** cTSSaveGameInfo (src/ts/cTSSaveGameInfo.cpp, 15 of 16 functions, tests/ts_save_game_info.cpp)
   and the XML reader (src/gz/cGZPropertySetSerializerXml.cpp: Read, the three expat handler slots, QI, destructors,
   KeyFromString; tests/gz_property_set_xml.cpp). Left: XmlT Write 0x556a30 (2 KB; needs a fake XML writer, framework class
   0xed50b6ac / QI 0x8c0f3d0d) and cTSSaveGameInfo::Describe 0xcd8140 (text dump). Facts: cITSSaveGameInfo (iid 0xcdcbe0dd)
   is db/methods/cTSSaveGameInfo.jsonl (slot 3 Fill, 10 bool Save(group, instance)); the service 0x056b906e / 0x9af5e924
   Save/Load use is cITSPersistSystem (slot 16 SaveResource, slot 3 GetResourceManager, whose slot 3 GetResource loads);
   GetIndices fills a vector over GZServiceAllocator (growth 0xb5b2e0) and parses with atoi (0x830972: db/names says
   _wtoi, a byte-identical alias; DM's "_wtoi on narrow text" ORIGINAL-BUG does not exist); the XML handlers' this is the
   +4 face, StartElement's last two arguments are unused, EndElement trims mText (0x1ad83c is cRZString::TrimBoth, not
   the library name) and parses Sint32 with sscanf (0x242ec5), Float32 with 0x1b1008 (original call).
8. **Neighbourhood loose ends**: (LoadNeighborhoods 0x476a40 done by lane DN: the "group" CU named is the source info's file
   prefix text, GetFilePrefix()->ToChar(), handed to the neighbourhood manager's GetByName / GetNamedList (cITSGlobals slot
   8); key group = info->AsPersistResource()->GetGroup(). It empties only the first table, leaving the second dangling until
   a Rebuild: ORIGINAL-BUG), cTSNeighborhood vf8_5 0x477d70 (2.4 KB import), cTSNeighborhoodManager vf3 0x4a4160 (4 KB directory walk, needs
   file-system fakes), 0x4a2950 (4.7 KB delete).
9. **cTSObjectFileRef** (31 functions, 7.5 KB): the lot package file used by the lot save and persist ITS slots 69-74.
   Needed when the lot save (milestone B's lot-resave scenario) is owned; world-lot boundary.
10. **The controllers** (cTSLoad/SaveNeighborhoodController, cTSSaveLotController, ~6 KB of state machines): last; they are
    glue over everything above and need the Ghidra flow-override fix to read comfortably.

In progress elsewhere: cTSPersistSystem new functions (CT), cGZDBSegmentPackedFile retrofit (CR).

## Done (2026-10-15)

Verified, by group (`db/status/<group>.jsonl`): cTSPersistSystem 76, cTSPersistDBRecord 72,
cTSFamily 57 (incl. Read/Write, funds), cGZPersistResourceManager 42, cTSNeighborhood 41 (incl.
Read/Write), cGZPersistDBSegmentMultiPackedFiles 27, cTSNeighborManager 25 (incl. Read/Write/Save),
cTSFamilyManager 26 (incl. Load/Save/Unload, create/remove, DoMessage), cTSPersistDBSegmentInfo 22, cTSNeighborGeneticDataTable 21, cTSSimDNA 17, cTSRelationshipTable 10,
cTSPersistCacheStrategyGenerational 16, cTSPersistContext 16, cTSNeighborhoodInfo 33, cTSNeighborhoodGroupData 22, plus the binary reader/writer groups. In game (townie save):
cTSFamily Read 49 / Write 92, cTSNeighborhood Read 6 / Write 2, cTSNeighborManager Read/Write/Save 1,
cTSPersistSystem OnTick ~7k, its factory lookups ~13M per run.

## Left (largest first; `tools/candidates.py --class <group> --originals --by-size`)

| group | functions | bytes | note |
|---|---|---|---|
| cTSPersistSystem | 13 (+2 8-byte thunks) | 11.3 KB | lane ES did OpenContentPaths (ITS 43, 0x1dae90), service Init (0x1e0d60), AddCategoryPath (ITS 44, 0x1da360) and CollectPathGroups (ITS 47, 0x1da600); see "cTSPersistSystem (lane ES)" below. Lanes EW and EZ did eight more, lane FD five (50, 46, 54, 55, 58; see their sections). lane FK vf39 0x1e2850 with cRAMShadowRecord, lane FL the segment set-up 0x1e7230. Left: 0x1e3c70 |
| cTSNeighborManager | 0 | | finished (lanes BM, BO, BR). Lane BR did slot 20 CreateUserNeighbor (0x4bb840), the container face (slot 3 CollectReferencedNeighbors 0x4bc700, slot 4 Export 0x4bf710, slot 5 Import 0x4bd570) and Import's helper RemapLotEntry (0x4be170). Left: only unhookable pieces (the six 8-byte QI/Release thunks, shared_vf_ slots, the 4-byte vf11) |
| cTSNeighborhood | 0 | 0 | finished (86 entries; lane BN did 16, lanes EG/EM and others the rest: Import 0x477d70, ValidateInventories 0x478ed0, LoadLots 0x476390, AddLot 0x4730b0, DoMessage 0x4738d0, GetLotInventory 0x474530, DebugDrawFamilyArches 0x479de0, the tuning-table lookups 0x4b8f80 / 0x4b8fd0; standalone shape by lane EM). Left (checked by lane NN, 2026-10-26): only vf31 0x475330, a 5-byte `jmp 0x4b8fd0` (jump stub to a verified function: no entry; referenced only by the game's vtable 0xdc9be0) and folded `shared_vf_*` getters/setters (+0x10 AddRef, +0xc0, +0xb1, +0x8c, +0x90) |
| cTSFamilyManager | 0 | 0 | lane BP finished it: Import (0x4b5fb0), Export (0x4b6f90), ReportStatistics (0x4b45b0), all verified by difftest (the originals have EH frames and /GS-less prologues of their own, so no byte match). Left: the 8-byte QI/Release thunks per face (not hookable). Facts: the container face (+4) is a cITSDataContainer: slot 0 QI (0x6db6f3ff family context, 0x4e9c59c7 neighbourhood context), slot 12 gives the persist segment (0x656b8f0c), slot 14 the group. Fake-world helpers for these live in tests/ts_family_manager.cpp (BuildIo). ORIGINAL-BUGs: Import uses the destination object map unchecked; a lot entry id count above 0x7fff (as 16 bits) loops about 4e9 times |
| cGZDBSegmentDirectoryFiles | 0 | 0 | finished: lane BA did 25, lane NP the file-system bodies (vf6 Open, vf13, vf19, vf20, vf27 Refresh, vf32 OpenRecordFile, vf33 BuildKeyList, vf34 FileChanged, vf4_36/41/42/47), the Open thunk, the constructor 0x99e4c0, the deleting destructor 0x99ebf0 and its thunk 0x99eaeb (all verified, see "cGZDBSegmentDirectoryFiles (lane NP)"). Left: only shared_vf_ slots (not hookable). Original callees still used: the serial record base constructor 0x99b840 (OpenRecordFile), file-system helpers 0x1c2732 (vf19), 0x1c1ea9 / 0x1c1e0e (vf20, vf34), 0x1c2e25 (DeleteRecord), 0x1c2068 (GetRecordStamp), the instance stamp 0x9a11c0 (vf33) |
| cGZPersistResourceManager | 0 | 0 | finished: lane NP did Shutdown 0x9b57e0 (the last body; cTSPersistSystem's 0x1e53c0 and 0x9b3f30 now call ours). Left: only 8-byte thunks (not hookable) |
| cGZPersistBufferResourceFactory | 0 | 0 | finished by lane NP (10 verified, see "cGZPersistBufferResourceFactory (lane NP)"). Left: the 8-byte QI thunk 0x9b7d2f (not hookable) and shared_vf_ slots. Original callees still used: the codec 0x9b6670 / 0x9b6860, 0x9cdec0 (the resource class is ours since lane NU) |
| cGZPersistBufferResource | 17 | | lane NU did the whole class (see "cGZPersistBufferResource and cTSFloatConstants (lane NU)"). Left: the 8-byte adjustor thunks (not hookable) and the original callees 0x9cd6a0 / 0x9cea90 / 0x9cd890 of ReadTgaRecord |
| cTSNeighborhoodManager | 0 | 0 | finished: lane FN did Load vf3 (0x4a4160), lane FU DeleteFiles 0x4a2950 and AddTutorial 0x4a27a0, lane GJ dropped the slot adapters (in effect through callers); left only the folded persist-key `shared_vf_*` accessors of the +4 face (checked by lane NN, 2026-10-26). Earlier: lanes AY + BF did 26 (BF: Save vf5, NextFreeName vf17, Add vf18, Remove vf19, the 4 face thunks). Left: vf3 (0x4a4160, Load: enumerates neighbourhood records through the persist system, probes `_Neighborhood.package` with cRZFile::PathExists and scans the neighbourhood directories with the directory finder 0x1c48da/0x1c63d4/0x1c5047/0x1c509f/0x1c49eb; a real file-system walk, skipped). Original callees still: 0x4a2950 (4.7 KB, deletes a neighbourhood's files), 0x4a27a0 (vf3 only) |
| cTSLotInfo | 0 | 0 | finished: lane ED did vf8_56 SetMappedValue (0x4cb630) with the standalone shape (63 verified); left only `shared_vf_*` bodies and adjustor thunks (checked by lane NN, 2026-10-26). Earlier: lanes AZ + BE did 53 (BE: vf8_41 CreateNewLotInfo, vf8_35 GetOrCreateExtraInfo, vf8_29 StoreNamesInResource, via tests/ts_services_fixture.h). Left: vf8_56 (0x4cb630, builds and posts a message), the four 8-byte QI adjustor thunks |
| cTSFamily | 0 | 0 | lane BQ finished it (71 verified): group saves vf35/vf79 (one body, versions 0x80/0x10), DeleteResources vf36, SaveTexts vf34, CheckMoveIn vf15 (codes 0-9), CopyIdSet vf64, ScaleByLotModifiers vf74 (two-pass discount/markup percent, result at least 1), CountOutfitCombinations vf25 (bottoms * tops + full bodies), InsertKeyIntoGroup1 vf10, ImportLotIntoGroup1 vf9, and the lot-objects bodies 0x49c1a0 / 0x49cad0 (InsertObjectIntoGroup1/2 now call ours). All by difftest (tests/ts_family_save.cpp, ts_family_queries.cpp, ts_family_import.cpp): EH frames rule out byte matching, and vf74 (no EH frame) scored 0.28. Left: only shared_vf_ slots (not hookable). Original callees still used: 0x1773e0 (string build), 0x41d6e0 (ref-vector assign), 0x1b0e2c (hex parse). Facts (tentative, db/methods/cTSFamily.jsonl): resource service 0x10faf74 (getter 0xb39110) slot 22 FindResources takes 9 arguments (own fake in ts_family_import.cpp); property service (0xac1887e/0x4bb0d269) slot 9 GetValue(std::string*, any*); the id set is a std::set with GZServiceAllocator nodes. ORIGINAL-BUGs: DeleteResources asks the record's segment to delete the text key; the GUID parse writes past four words into dead locals (ours keeps four) |
| cTSFamilyRelationshipArray | 0 | 0 | finished (28 entries, all verified): lane CK did Relate vf9 (0x4f2280), Unrelate vf8 (0x4f3950) and the container face (CollectReferenced 0x4f27a0, Export 0x4f3ff0, Import 0x4f3300), lane DK the standalone shape, lane GJ dropped the adapters (in effect through callers); left only folded `shared_vf_*` accessors (checked by lane NN, 2026-10-26). Earlier: lane BC did 14 (ctor, QI, Release, Read/Write, set/remove/get, ReadFrom/WriteTo, Save, Clear). Left: vf9 (0x4f2280, relation state machine, recursive through slots 7/3/8/9, framework create 0x8c870733), vf8 (0x4f3950, links two families), container face vf4_3/4_4/4_5 (import/export through a data store), adjustor thunks |
| cTSFamilyHistory | 0 | 0 | lane BC did 10; lane FI took it to the standalone shape and did vf8 (0x49d9c0, text dump: fopen (0x83aa0f, folded with _wfopen; mode "w") on the argument's ToChar, fprintf per field, fclose (0x838ff4, folded with _close); difftested with recording CRT fakes through cTSFamilyHistory::sOpen / sPrint / sClose), the creator 0x49e1e0 (hooked, hand CRC) and the 2 thunks. No slot is game code |
| cTSNeighborhoodInfo | 0 | | lane BD did all 33 (ctor, factory, dtor, QI and the four QI thunks, Read/Write, accessors, the four persist-system members, describe face); layout below |
| cTSNeighborhoodGroupData | 0 | | lane BD did 22 (factory, deleting destructor, QI, the table members, vf10/vf11 rebuilds); lane DN did LoadNeighborhoods 0x476a40, so Add and Refresh have no original callee left |
| import/export contexts | ~20 | ~1 KB | lane BG did cTSFamilyDataImportExportContext (22) and cTSNeighborDataImportExportContext (31); lane BJ did cTSNeighborhoodDataImportExportContext (45, see below). Left: any other import/export context classes, and the shared_vf_ slots of these three (key get/set 3/4 of the family and neighbour contexts, setters 8/9/10, 14, 15: folded with other classes, not hooked) and the pure `sub ecx, N; jmp` adjustor thunks of the faces (not hookable) |
| cTSNeighbor | 0 | 0 | lanes BB + BE did 49 (BE: vf40 BuildDisplayName). Left only the two 8-byte QI/Release thunks of the persist face (0x4b87c2, 0x4b8d0a, not hookable) and the shared bodies (0x4b8d20, 0x4b8e40; slots 8/9/12/15/36/37/42/49 are tiny getters shared with other classes) |
| cTSRelationshipTable | 0 | | lane CX finished it (24/24): Read 0x55b420, slots 5-10 (value accessors, RemovePerson, Clear), LoadEntry 0x55b090, SetEntry 0x55ac60, the +4 face's QI/Release thunks; all difftested (tests/ts_relationship_table.cpp), no byte matches (EH frames, inlined unordered_map). Original callee left: 0x20deb5 (cached resource manager getter). Next on this record: cTSRelationshipEntry (22 of 28 left: Read 0x55b370, ReadFromRecord vf4 0x55bd20, WriteToRecord vf5 0x55be00, resize 0x55c450, QI/Release) |
| cGZPersistBinaryReader / cGZPersistBinaryWriter | 0 | 0 | lane NN finished both (2026-10-26): reader ReadRoot vf3 0x9bfd10, GetUint16 0x9c0840, the three array reads 0x9c0860 / 0x9c08c0 / 0x9c05f0, GetGZStr 0x9c0830, Skip 0x9c0920, ReadObject 0x9c0630, ReadStringRecord 0x9c0490, deleting destructor 0x9bfc20; writer WriteRoot vf3 0x9c0d50, deleting destructor 0x9bfc50 (tests/gz_persist_binary_reader*.cpp, gz_persist_binary_writer_root.cpp; 18 mutants, 17 killed, the survivor an equivalent redundant context test, since removed). Facts: the entry stack at +4 is a MSVC 14.40 std::deque (0x14 bytes) and used as one; the constructors 0x9bf9a0 / 0x9bfa10 have no references (dead); the deque destructors 0x9bfa70 / 0x9bfb50 are library code reached only from our deleting destructors. Left: slot 2 Release and the writer's WriteSerializableObject (shared_vf_9bf780), folded, not hookable |
| cTSPersistCacheStrategyGenerational | 0 | 0 | lane NN finished it (2026-10-26): Get vf7 0x1fbf00, Add vf8 0x1fb580, Remove vf9 0x1fc160, Contains vf10 0x1fbff0, GetKeys vf12 0x1fbde0, GetKeysOfType vf13 0x1fbe70, Clear vf18 0x1fb7b0, Evict vf23 0x1fb870 (1.3 KB) and the deleting destructor 0x1fb4f0 (tests/ts_persist_cache_strategy_generational_cache.cpp; 26 mutants, 25 killed, the survivor an equivalent re-index after a splice, removed). Facts: four generations are MSVC 14.40 std::list<Entry> at +0x0c + 8g (12-byte entries: resource, generation, age, recent, pad, stamp) and the index a std::unordered_map<PersistKey, list iterator> at +0x2c (src/ts/PersistKey.h: hash = sum of the four words, not noexcept); tick +0x50, count +0x54, limit +0x58, table +0x5c ([0] minimum age, [6+g] promotion thresholds, low byte). Key lists (slot 12/13) are vectors on the allocator service (GZServiceAllocator; the game's growth 0x1d1370 goes through it, so a plain std::vector crashes the original). Get/Add/Remove return al only. Left: the folded `shared_vf_*` accessors (QI 0x1fc060, +0x4c setter 0x1fc100, +0x70/+0x74/+0x58 getters and setters) |
| cTSPersistContext | 0 | 0 | lane NN finished it (2026-10-26): QueryInterface 0x1fabb0, GetResource vf3 0x1faaa0, AddResource vf4 0x1fa720, RemoveResource vf5 0x1fabf0, DeleteResource vf6 0x1fab80, FindIndexedResource vf7 0x1faa00 (windows.h defines FindResource), HasResource vf8, SetDirty vf10, GetDirty vf11, SetMask vf12, GetMask vf13, Save vf14 0x1fad60, Clear vf15, ReleaseResources vf16 0x1fafb0 and the deleting destructor 0x1fa6e0; vf9 rewritten over the same map (tests/ts_persist_context_index.cpp; 26 mutants, 24 killed, 2 equivalent survivors removed: AddResource only ever fills a new entry). Facts: the index at +0x0c is a std::unordered_map<PersistKey, Entry> (entry: dirty byte, priority, mask, state face 0x456b8543, resource); Save sorts by signed priority with std::sort (the game's 0x1fa0b0 is that library code) and passes the state face's flag from a setz (upper bits undefined). Left: the folded `shared_vf_*` AddRef/Release and the 8-byte +4 face thunks |
| cTSNeighborhoodConstants | 1 | 0.6 KB | lane NN did GetFloat vf5 0x474100, GetValue vf6 0x4740a0, the deleting destructor vf4 0x417060 and the base cTSConstantsClient destructor body 0x4df1b0 (tests/ts_neighborhood_constants.cpp; 11 mutants, all killed). Lane NU did vf3 Load 0x47a630 and the float-constants table below it; left only vf2 0x4df450 (4 bytes, not hookable) |

## Conventions and test patterns

- Streams: recording fake cIGZIStream/cIGZOStream (tests/ts_family_io.cpp, ts_neighborhood.cpp);
  sweep every version threshold the Read checks, plus 0, 0xffffffff and failing reads.
- Globals: app singleton 0x10cc288, persist system cache 0x10faf6c, object store 0x10faf78, framework
  through the director: the generic recording fake in tests/ts_family_services*.cpp (any slot, 0-6
  args, pointers normalised) and `Globals` in tests/ts_neighbor_genetic_data_table_world.cpp.
- Original callees too big to fake: stand-in by patching the image (`CodePatch`, or `bpt::Redirect` in
  tests/cas_body_part_system.h) on both sides, and record the calls.
- Frees: `difftest::FreeRecorder` (docs/lane/test.md "Observing frees"); game-allocator (RZMemory)
  blocks are not recorded yet.
- Containers in these objects are MSVC 14.40 std::map/set/vector/unordered_map; ours can be the same
  types (the game's operator new and our CRT share the process heap).

## Shared service fixture (lane BE)

`tests/ts_services_fixture.h` (header only, namespace `svcfx`): recording fakes `Obj` (24, ids 200 + index, any slot below 80
with 0..7 dword arguments via `Set(slot, args, result, custom)`, slots 1/2 count references), an event log with normalised
pointers (fakes -> ids, registered ranges -> tag | offset, stack -> marker), `Globals` (app singleton 0x10cc288 via `Set`, persist
cache 0x10faf6c, the director static 0x10c9390 whose slot 12 answers the fake `gFrameworkIndex`; every cell set is restored),
`AllocatorScope` (the game's allocator service over the general allocator: build it ONCE per test, shutting it down and building
it again per case exhausts the address space after ~130 cases and crashes in `GeneralAllocator::SetTop`), `gAllocated` /
`CaptureAllocated` (whether blocks a fake handed out through RZMemory were freed afterwards, read from the dlmalloc chunk bits;
without it a missing `FreeBlock` / `ReleaseAndFree` survives every mutant) and `gTrace` (prints each fake call to stderr; use
it to find where a crash happens). Used by `tests/ts_lot_info_services.cpp` and `tests/ts_neighbor_services.cpp`; strings come
from the resource manager fake of `tests/ts_string_fake.h`. Fakes that fill an allocator-service vector must allocate the whole
capacity (end - begin is what the free passes as the size).

Facts: cTSLotInfo's +8 face slot 8 returns the address of the name string (+0x20), slot 4 the base (+0x80), slot 10 the origin
into two outs, slot 22 the rotation. 0x1f1f70 is a copy of ServiceRefVector::ReleaseAndFree (0x38f140). 0x55d500 (id -> value through
app slot 6 -> 15 -> 29) and 0x55d4d0 (a table lookup 0x54a290, 5 on failure) are the helpers of the extra-info setup;
0x54ac40(neighbour) finds an object through app slot 26 -> 43(data id) and cTSNeighbor::BuildDisplayName asks it for attribute 0xe6.
Paths whose original dereferences a null fake (a creation failing in CreateNewLotInfo's holder/settings) crash and can't be tested.

## cTSNeighborhoodManager (lane AY)

Layout (0x44 bytes, `src/ts/cTSNeighborhoodManager.h`): +0x10 `std::map<uint32, counted ref>` (id -> neighbourhood; erasing
releases), +0x18 `std::map<cRZString, {counted object, ServiceRefVector}>` ordered by `_stricmp` (node 0x40), +0x20 next id (1),
+0x24 cursor (a node of the id map) for vf7/vf8, +0x28 `ServiceRefVector` of counted neighbourhoods, +0x34..0x43 zero. Test
fixtures for allocator-service vectors: `GameAllocatorFixture` + the 0xda3cd4 service object at 0x10c95d4 (copied in
tests/ts_neighborhood_manager.cpp); a fake allocator does not work because ServiceRefVector uses RZMemory.
vf16 calls persist system slot 21 with 7 stack args (the string from 0x4c4324c5 via CommonImplSlot4 is the 4th).
Lane BF: +0x34..+0x43 is the persist key (face slot 6 0x147800 reads the group). Neighbourhood slots: 8 name (cIGZString), 11 SetId,
12 id, 14(1) hidden, 18 kind (1 = main neighbourhood), 24 persist face (slot 6 group, slot 4 SetKey), 27 save. Persist system slots
in Save: 48 (group, list) collect, 49 (list), 16 (face, 0) save, 50 (list, 1) commit; framework CreateInstance(0xcdcbe0e3, 0xcdcbe0dd)
gives a listener (slot 3, slot 10 (group, 1)). Messages: 0xc3 added (string at slot 30), 0xc4 main removed (string), 0xc6 removed (id
in value 0); server is 0x9c5a89 (cache 0x10f519c). Path helpers (cdecl (name, out, 1, -1), root from CommonImplSlot4 0x4c4324c5 or
0x8ce453ed): 0xc0b362 takes text, 0xc0b232 / 0xc0b150 a cIGZString and append two different subfolders; 0x1c5031 = DirectoryExists
or CreateDirectories. Strings: 0xdcbc6c "N%03d"-style format, 0xdcbc44 the tutorial name. Test world: tests/ts_neighborhood_manager_load.cpp
(svcfx fakes, CodePatch stand-ins for 0x1c5031, 0x4a2950, DirectoryExists image + ours, real messages captured at the server).
Remove of a main neighbourhood crashes on a null entry in its record's list (ORIGINAL-BUG).

## cTSNeighborManager (lane BM)

Slots seen from the manager's code (the neighbour fakes in tests/ts_neighbor_manager.cpp answer them): neighbour 8 id word, 10 person
data object, 12 relationship table, 15 id, 16 data value by index (AX), 17 set data value (index, value), 35/36 flags, 41, 42; relationship
table (cTSRelationshipTable) 5 (id) known, 9 (id, kind) state with kinds 1, 3, 9 (bits 4/5 and 0 are the ones the checks read).
Predicates 0x4be670 / 0x4be7f0 (cdecl, two neighbours) say whether two neighbours are related; slot 27 (0x4bcc10) collects neighbour ids
for a target through them; 0x4bfd00 counts related neighbours and tells the target (its slot 42). Slot 14 (QueryNeighbor) is a switch over
questions about one neighbour (query 3 and 4 answer 0). Queries 7/8/10 return the family object's yes/no with the upper half of eax kept from
the callee. Slot 25 sets data value 0x3d of every member of every family and clears it for neighbours that are in none.
Byte matching: calls through abstract view classes (`vtable_detail::Pad<N>` plus named virtual functions, see the views in
src/ts/cTSNeighborManager.cpp) give the original's `call [reg+off]` forms; explicit iterator loops (`it != end()`, `++it`) give
the original's tree walk with the library `operator++`; `__declspec(noinline)` keeps a function the original tail-jumps to
(slot 28 to 0x4bfd00). What keeps the predicates (0.56) from matching is that the original loads a table's vtable pointer into a
register before evaluating the neighbour id and keeps it across the call.
Test additions: kSlots is 100; the manager fake vtable has slots 16 (GetNeighbor) and 17 (FindByKey) answered from the neighbour fakes.

Lane BO (tests/ts_neighbor_manager_services.cpp, svcfx fixture; `UseAsMessageServer` + `kMessageServerCell` 0x10f519c added to
tests/ts_services_fixture.h: the server fake logs posted messages by content): done and verified (difftest, mutants): 0x4bf4f0 UpdateKeyMap
(remove flag: 1 removes the neighbour's key, 0 enters it; no neighbour = clear and rebuild), 0x4be3f0 RemoveNeighborEffects (post 0x9c, key
map, `mNeighbors.erase(id)` = 0x42e280, relationship tables slot 7 / 8, singleton slots 29 / 27, neighbour slot 7, delete record via persist
slot 17), 0x4bdf00 LoadNeighbors (persist slot 36 lists keys of type 0xaace2efb, slot 5 creates through interface 0x3b43bf8a, slot 39 (0)),
vf8 Reload, vf21 RemoveNeighbor (message 0x9d), vf22 RetireNeighbor (0x9e, data value 0xa5 = 1 / 0x3d = 0, persist slot 48 collects the
person group's resources, manager slot 23, framework helper 0xcdcbe0e3), vf23 DeleteNeighborResources (std::set of five resource types,
resource QueryInterface 0x656b8f0c, holder slot 19 keys / 26 / 10; the original has dead code for a never-set second holder), vf29
CreateNeighbor (free id counter at +0x18, framework class 0xaace2efb). Message ids 0x9c / 0x9d / 0x9e carry the neighbour word as value 0.

Lane BR (tests/ts_neighbor_manager_container.cpp, own fakes over svcfx; the manager is a zeroed block with a shim vtable): container face at +8 (`NeighborManagerContainerFace`). The neighbour context is interface 0xdb5c0c2 (slots 23 lot ids, 24/25 add, 27/28 has, 29/30 id lists, 32 map id (id, *out), 33 source key, 34 set key, 36 map key/type); the neighbourhood context 0x4e9c59c7 maps lots with slot 25. Export writes each neighbour's record with the container's group in its key (slot 48 face GetKey/SetKey, persist slot 16), the lot side (lots slot 39) as record 0xcdb8bdc4 version 0xcb, and copies record 0x3053cf74 (persist slot 37: from, source, to, segment; Import passes from, segment, to, source). Import copies a 0x1ce-byte person block (neighbour slot 18: 1 source, 0 destination), keeps data value 0x3d, remaps 0xc3 (else clears 0xc3..0xc6), rebuilds the relationship table (slots 5/6/7/9/10/11/12/13) and the lot side from the neighbourhood record 0x4e474248 (slot 39) or segment record. Quirks: the destination neighbour, lots (CollectReferenced) and created character (CreateUserNeighbor) are used unchecked; slot 30 refills a vector slot 29 already filled. CreateUserNeighbor: file names <user dir (app slot 23, slot 6, slot 8)>_User%05d from the counter at +0x14 (wraps at 32000), persist slot 18 says whether one exists, app slot 24 slot 52 creates (10 arguments), the character's slot 4 fills a GZServiceAllocator std::set<void*> whose first element goes to slot 29. All four have EH frames: verified by difftest.
Fakes: element pointers handed out by a fake that frees heap blocks need `AddFreshRange` (drop ranges of freed blocks that the next block reuses).
Still left: vf20 (0x4bb840, ret 0x14: loops a counter at +0x14 below 0x7d00, builds a name with cRZString::Sprintf format 0xdcc6d8, asks persist
slot 18 whether it exists, creates through framework slot 52 (0xd0/4, class 0x3b2271bb), appends to a tree, returns a wrapper), and the container
face vf8_3 (0x4bc700, syncs neighbour ids with a container: app slot 6 object slot 38, kind 0x4dd398a5 / 0xedd39930), vf8_4 (0x4bf710), vf8_5 (0x4bd570).
Not tried for byte match.

## cTSNeighbor (lane BB)

Layout (0x214 bytes, `src/ts/cTSNeighbor.h`; factory 0x4b73c0, deleting destructor = slot 53 0x4b72e0): +8 refcount, +0xc int16 id,
+0x10 data id of the person data object, +0x14 counted person data (loaded lazily by slot 10 through app singleton slot 24 -> slot 14),
+0x18 counted cTSRelationshipTable (built by the factory), +0x1c counted record object (resource type 0x3053cf74, slot 14 loads or
creates it), +0x21 modified byte, +0x28 layout number (0x36), +0x2c `uint16_t[0xe7]` attributes, +0x1fc the 16-byte resource key
{type, group, instance, high} (the persist face at +4 is `this` of GetKey/SetKey/Get*/Set*, so those use +0x1f8 and +0x1d), +0x20c/+0x210
two words (Read/Write version 0x71). Attribute indexes seen: 0x3a age (<=1 baby, 2 toddler, 3..15 child, 16..18 teen, 19..50 adult, above
elder), 0x41 gender, 0xaf a flag the adult/age-mask code reads, 0xba species (0 = Sim, otherwise a pet). Read picks the number of attribute
shorts from the layout number (table in the .cpp; layouts 1-2 read 0x40, 3-5 0x50, 0x36 and up 0xe7), zeroes the rest.
Slots 8/12/15 of the primary vtable are shared getters (id, relationship table, data id); the person data object's slot 18 gives the
sim definition whose slot 29 gives the tuning table vf45 (0x4b74e0) thresholds come from. vf45 reads its tuning values with indexes 0..5,
3 and 0x2d. SetKey (0x4b8d90) compares after copying, so a changed key never sets the modified byte (ORIGINAL-BUG marker in src).
Tests: `tests/ts_neighbor.cpp` has a world with the neighbour's own vtable as recording fakes (so `this->slot(...)` calls are logged), the
app singleton 0x10cc288, persist 0x10faf6c, the director 0x10c9390 (+ guard 0x10c93f4) and scripted streams; vf50 stands in for 0x55cce0 with
a patch on both sides. 16-bit fake answers carry junk in the upper half of eax.

## cTSLotInfo (lane AZ)

Layout (0xac bytes, `src/ts/cTSLotInfo.h`): cRZUnknown +0/+4, faces +8 (cITSLotInfo, 58 slots), +0xc (serializable), +0x10
(persist key at +0x70), +0x14 (self-describing); +0x18/+0x1c origin, +0x20 name and +0x50 description (cRZStrings: their counts
are +0x3c / +0x6c), +0x40 elevation (-FLT_MAX = not computed), +0x44, +0x48 rotation byte (neither constructor sets it), +0x4c,
+0x80 counted cTSBaseLotInfo (0xa4 bytes, its +8 face; ctor 0x4c86c0, copy 0x4c83d0), +0x84 counted, +0x88
`std::vector<16-byte connection>`, +0x94 lot id, +0x98 `std::map<int16,int16>` (slot 55, 100 when missing), +0xa0 byte, +0xa4,
+0xa8. Setters tell the base "changed" through its slot 22 with 0x20. Base slot 8 is the lot type: 9 defers the connection
slots to the lot info that app singleton slot 6 -> slot 57(lot id) gives, 8 keeps them here. Write is version 0x12 (returns 1);
Read returns the base record's read answer. Test fixtures in tests/ts_lot_info.cpp: a generic recording fake (0-5 args, any
slot), the option mask cache 0x10faf94 preset so the base copy constructor's type query stays local, the persist cache
0x10faf6c and the allocator static 0x10c95d4 (guard 0x10c95d8 = INT_MIN) pointed at fakes.

## cGZDBSegmentDirectoryFiles (lane BA)

Layout from the constructor 0x99e4c0 (`src/gz/cGZDBSegmentDirectoryFiles.h`): +0x1c `unordered_map<instance, {bool found,
uint32 resource}>` (probe cache, identity hash), +0x8c `unordered_map<16-byte key, {cRZString name, list<counted record>}>`
(hash = key[2], node 0x40), +0x4c the path cRZString, +0x6c a 0x5c-byte object (dtor 0x1c49eb), +0x7c cGZRESIDtoExtensionMap
(embedded, 0x10 bytes), +0xb8 a timer (slots 6 Start(ms, ctx), 7 Stop, 8 IsRunning, 9 SetCallback(fn, ctx)), +0xe0 cRZRandom.
Both tables are plain MSVC 14.40 `std::unordered_map`s with non-noexcept hashers (the game's clear() has no small-table path):
our code uses the std types directly on the game's memory, and tests build the object with them and compare tables
structurally (list order, contents, per-bucket first/last as list positions). The key list vector (sub slot 45) uses the
allocator service (`GZServiceAllocator`). The +0xac object's slot 5 takes this as a 4th argument.
Test world: tests/gz_dbsegment_directory_files_tables.cpp (fakes for every embedded object, CodePatch on 0x1c49eb).

## cGZDBSegmentDirectoryFiles (lane NP): the file-system bodies, construction and destruction

- Records: a cGZDBSDFRecord is 0x188 bytes (vtables +0 0xe5ead4, +4 0xe5eb1c, +8 0xe5ec18, +0x24 0xe5ec70, +0x34 0xe5ecd4;
  the serial record base 0x99b840 at +4 with flag 1, two cRZFiles at +0x4c and +0xe8, +0x48 a byte, +0x184 the access; 2
  and 3 mean writing). OpenRecordFile (slot 32) builds one in place, opens it through its slot 14 (key, path, access),
  pushes it at the FRONT of the file's open list (the game's push 0x9a3080 links at the front and AddRefs) and asks it for
  0xe56b8f03. ORIGINAL-BUG: when that query fails, the record is removed from the entry at the exact key, inserted when
  missing (the files map's operator[] 0x99e9c0 default-inserts a name with the game's cRZString vtable and refcount 0).
- GetRecordSize / DeleteRecord / ReadRecord / GetRecordStamp (sub slots 36/41/42/47) work on the file `path + name`
  through a local cRZFile; a file whose only open record is writing answers from that record instead.
- Constructor 0x99e4c0: the persist segment base (ours), our two hash tables, path, extension map (game vtable 0xe5ed38,
  refcount 0, empty multimap), refresh 120 s, the timer (0x9ccf32) and cRZRandom (seed 0xffffffff, so from the clock).
  Deleting destructor 0x99ebf0: puts both game vtables back, Shutdown (non-virtual), timer destructor, releases +0xac
  when still set, then the tables, extension map, path and probe table, the base destructor, and frees 0xf8 bytes on
  flags & 1. The thunk 0x99eaeb (sub slot 33) steps back 4.
- Tests: the constructor runs on a heap block filled with random bytes and is compared word by word (heap blocks by
  shape: lock block present, hash heads self-linked, bucket vector size and every bucket at the head, tree head linked to itself).
  The destructor runs on a heap copy of the usual world (plus a real lock and a real extension map tree) and compares
  the calls, every freed block's size (FreeRecorder) and, when not freed, the object's non-pointer words. Shutdown's
  Close is a stand-in there because the destructor restores the game's vtables first.

## cGZPersistResourceManager::Shutdown and cGZPersistBufferResourceFactory (lane NP)

- Shutdown 0x9b57e0 (src/gz/cGZPersistResourceManager.cpp, tests/gz_persist_resource_manager_shutdown.cpp): under lock C
  every cached value node's value gets slot 4 and a Release, the nodes are freed and the list reset; under lock A every
  factory is released and the map cleared; under lock B each segment gets slot 9 then slot 4 and a Release, the vector is
  cleared, then +0x84 (slot 10, 9, 4, cleared, released). Returns 1.
- cGZPersistBufferResourceFactory (0x1c bytes, src/gz/cGZPersistBufferResourceFactory.*): the factory of record type
  0x856ddbac (images), cGZPersistResourceFactory base plus the cIGZPersistBufferResourceFactory face at +0x10 (iid
  0x656ddc26) and a default colour type (+0x14 format, +0x18 bits; slot 9 Create passes the format). ReadRecord fills a
  resource's buffer from a BMP record (0x9b6930), else rewinds and tries TGA (0x9b77b0), through a stack resource whose
  buffer is blitted into the target's.
- Buffer records (face slots 6 ReadBuffer / 7 WriteBuffer): uint32 kind (1 raw, 2 compressed by 0x9b6670), uint32 data
  size, width, height, sint32 colour format, the packed rows. ReadBuffer also takes the older kind 0x856ddbac (width,
  height, width * height uint32 pixels via SetPixel into a {3, 16} buffer). Bytes per pixel: format 2 1, 3..7 2, 8..9 4.
- ORIGINAL-BUGs kept: NewBufferFromFile leaks its resource; NewBufferFromBitmap returns true when GetObjectA fails;
  ReadBuffer's raw repack swaps the strides, and a failed read before the buffer exists calls through null (not
  testable: the original crashes).
- Tests (tests/gz_persist_buffer_resource_factory.cpp): logging fakes for cIGZBuffer (with real surfaces), the serial
  record (typed reads scripted, failure injection), graphics system, GZCOM director (StandIn 0x1ae18d), the resource
  constructor/destructor/readers and the codec as CodePatches, and the four Windows imports patched in the IAT.

## cTSFamilyHistory / cTSFamilyRelationshipArray (lane BC)

History (0x28): +8 `std::vector<FamilyHistoryEntry>` (std::allocator), +0x14 refcount, +0x18..0x27 zero. Entry 0x54 bytes, user copy
ctor = zero then assign (no move), texts at +0x44 are a service-allocator vector of by-value cRZStrings (copy resets refcount,
assignment copies only the text, destroyed through cRZString slot 33). Version thresholds 0x29, 0x4e, 0. Slot 7 appends to a
service-allocator vector. Stream metadata version: slot 20 object, its slot 78 returns a pointer to the version.
Relationship array (0x30): `std::map<uint16, NeighborDataRef>` at +0x28, dirty byte +0x14, faces +4/+8/+0xc. Write puts the
version word (1) before the metadata and never queries the error state; Read inserts only absent ids. Tests patch the
relationship constructor 0x4f1fd0 with a fake.
Relationship (lane CB, structure only; `src/ts/cTSFamilyRelationship.h`, handoff in notes/feedback/CB.md): one sim's family ties
(the array's uint16 key is a sim id, not a family id). 0x28 bytes: faces +4 persist (16-byte key at +0x10, type 0x8c870733) and +8
serializable, count +0xc, `std::multimap<int type, uint16 sim>` at +0x20. Types 0/1 parents, 2 spouse (single), 3 siblings, 4 children
(lists). Serialized: version 1 + metadata, count, (type u32, sim u16) per tie; Read neither clears nor checks errors. No original callees
beyond library/STL code and the verified UtilObjectB::Construct.

## cTSPersistResKeyList (lane CE)

Saved list of persist resource keys (record class id 0xac506764, interface 0x6cd04176; `src/ts/cTSPersistResKeyList.h`). 0x2c bytes: faces +0
(cITSPersistResKeyList, 9 slots), +4 persist resource (key at face+0x18 = object+0x1c), +8 serializable, refcount +0xc, then a
`std::vector<16-byte key, GZServiceAllocator>` at +0x10 (the serializable face's Read/Write run with `this` at +8, so the vector is at
`this+8` there; the header's `cTSPersistResKeyListSerialFace` covers +8..+0x2c). Factory 0x1f4dc0 (new 0x2c, constructor inline; called by
the cTSPersistCOMDirector getter; not a known function start, so not hookable until db/missed-functions.jsonl has it), deleting
destructor = slot 8 0x1f4d20. 0x1f4b70 / 0x1f4ce0 are STL (the vector's `_Resize_reallocate` / zero-fill; 0x9b2fb0 is a second user):
`vector::resize` reproduces them. Stream format: marker 0xdeadbeef, version (2), count, then per key 4 words (type, group, instance,
instance high; version < 2 has only 3 and the high word stays as it was); a stream without the marker starts with the count itself.
A stream that is a persist record (QueryInterface 0xe56b8f03 gives the cIGZPersistDBRecord face) hands out, through the face's slot 15
(query 0x3b2271bb), a key mapper: its slot 10 maps each key before writing (key, out), slot 11 after reading (key in/out, answers whether
to take the result). Commit (slot 4, 0x1f5130): persist system (service 0x056b906e / 0x9af5e924) slot 16 SaveResource(face, segment), then
flush (segment slot 10) the given segment or the one persist slot 34 FindSegment(key, 0x656b8f0c, out) finds for the list's key.

## cTSNeighborhoodInfo / cTSNeighborhoodGroupData (lane BD)

Info (0xd4, `src/ts/cTSNeighborhoodInfo.h`; ctor 0x4a6350, factory 0x4a6720, dtor 0x4a6500, deleting 0x4a66c0): faces +4 serializable,
+8 persist resource (key at +0xb0: type, group, instance, high; face slot 6 / 7 = group / instance), +0xc self-describing (QI thunk
0x4a6d94), +0x10 cRZUnknown (count at +0x14, QI thunk 0x4a6d8c, deleting thunk 0x4a66ac). Four cRZStrings at +0x18 (name, from string
table entry 1), +0x38 (description, entry 2), +0x58 and +0x78 (the two saved texts); then number (+0x98, -1), flags (+0x9c), version of
the last Read (+0xa0, 10), category (+0xa4), kind (+0xa8, 1), word (+0xac), counted record object (+0xc0, created on demand through the
slot-23 service's manager 0xd0590041 or the framework), four small values (+0xc4, saved as bytes from version 10). Read/Write are the
+4 face: Write puts version 10, Read takes versions up to 12; older versions derive the category from the kind (2 -> 1, 3 -> 2, 5..7
-> 10, else 0); the word read at version 7 must be <= 0x11 (0xb39350 answers that constant). The string table lookups use a key
{0x43545353, group, 1, 0} (version >= 3) or {0x53545223, group, instance, 0} (version 2). Read reuses one stack local across its word and
byte reads (a failed read leaves the previous value; mirrored with `scratch`). StoreTexts uses the resource the framework creates without
a null check (ORIGINAL-BUG).
Group data (0x60, `src/ts/cTSNeighborhoodGroupData.h`; factory 0x473c40, deleting destructor 0x4729f0 also reached through the +4 face thunk
0x4728af): +0xc `unordered_map<id, {counted neighbourhood, dirty byte}>` (node 0x14), +0x2c `unordered_map<key, entry pointer>` (node 0x10, the
pointer is the first table's mapped part), +0x4c counted current neighbourhood, +0x50 counted first one stored, +0x54 `map<int, counted object>` with
a cursor at +0x5c. Both tables are plain MSVC 14.40 `std::unordered_map`s (FNV-1a hash of the four key bytes), the ordered map a `std::map`; our code
uses the std types on the game's memory (an Entry type releases its pointer in its destructor so erase / clear / destruction release in the
original's order). Rebuild (vf10) loops for ever on a member whose id equals its argument (ORIGINAL-BUG). Tests: a cursor left dangling by
Rebuild must be parked first (its position depends on node reuse); a freed block's words must not be compared.
Test fixtures: tests/ts_neighborhood_info.cpp and tests/ts_neighborhood_group_data.cpp both carry the generic recording fake (any slot, 0-5 args).

## Data import/export contexts (lane BG)

Family context (0x50, `src/ts/cTSFamilyDataImportExportContext.h`; factory 0x470bf0 (called by the cTSSimCOMDirector constructor),
deleting destructor = slot 28 0x470a60): faces +0 (29 slots) and +4 serializable (Write 0x471260, Read 0x470fb0, QI / Release /
GetGZCLSID thunks), count +8, +0xc `unordered_map<family id, mapped id>` (-1 = not mapped yet), +0x2c 16-byte resource key (shared slots
3/4/8/9/10), +0x3c `std::list` of counted objects, +0x44 and +0x48 counted objects (slots 12 / 27 forward a QueryInterface to them, slot 13
sets +0x44, slot 26 re-queries +0x48 for interface 0xdb5c0c2), +0x4c = -1. Neighbour context (0xa8, `src/ts/cTSNeighborDataImportExportContext.h`;
factory 0x46fb80, deleting destructor = slot 37 0x46f950): +0xc id vector in the allocator service's memory (`ServiceWordVector`,
`std::vector<uint32_t, GZServiceAllocator>`), +0x28 counted-object list, +0x30 / +0x38 `std::set<int>` ("resolved" / "pending": slot 24 moves an
id to resolved, 25 adds pending unless resolved, 26 removes, 27 / 28 query), +0x40 / +0x60 / +0x80 `unordered_map<uint32,uint32>` (+0x80 is filled
by slot 34 for known ids, slot 35 maps by value into +0x60), +0xa0 counted object (slots 12 / 13), +0xa4 = -1. Serialized: version 2; family writes
the count through slot 8, the neighbour context counts through slot 9 and ids / values through slot 8; the error query is stream slot 18 and the
result is its negation. The sets / tables / list are plain MSVC 14.40 std types on the game's memory; `std::set::insert` looks up before it
allocates (so the original's find-then-insert in Read is the same as a plain insert). Test pitfall: never read a freed block (vtable words
compare) after a deleting destructor; and offsets computed from an untouched `out` sentinel depend on heap addresses.

Neighbourhood context (lane BJ; 0x110 bytes, `src/ts/cTSNeighborhoodDataImportExportContext.h`; constructor 0x472040, factory 0x473be0 (answers the +8 face;
the cTSSimCOMDirector constructor calls it), deleting destructor = slot 3 of the +0 vtable 0x4728f0): it is the family and neighbour
contexts in one object. cRZUnknown at +0 (count at +4), then FOUR faces with their own vtables: +8 cITSNeighborhoodDataImportExportContext (32 slots,
0xdc9e0c), +0xc the family face (28 slots, 0xdc9e90), +0x10 the neighbour face (37 slots, 0xdc9f04) and +0x14 serializable (0xdc9f9c); the +0xc and
+0x10 slots are mostly `sub ecx, N; jmp` thunks to the +8 bodies (not hookable). Members: `unordered_map<id, mapped id>` at +0x18 (+8 slots 22..25) and
+0x60 (+0xc slots 22..25, the family context's), a third `unordered_map<id, value>` at +0x38 (+8 slots 28 / 29), pair (key +0x58 = -1, value +0x5c = 1;
+8 slots 26 / 27), `std::set<int>` resolved +0x80 and pending +0x88, `unordered_map` +0x90 / +0xb0 / +0xd0 (the neighbour context's tables A / B / C: +0x10
slots 31..36), 16-byte key +0xf0 (+8 slots 3 / 4 / 8 / 10), `std::list` of counted objects +0x100, counted held object +0x108, -1 at +0x10c. Slot 22 of the
+0x10 face forwards to the +0xc face's slot 22 through its vtable. Serialized: version 2; the first two tables write their counts through stream
slot 8 (read back as signed counts) and the +0x38 table is neither written nor cleared by Read.
Byte matching (tools/matchcheck.py): with the shipped /Oy- flags the matcher fails every function that has a frame (`push ebp; mov ebp, esp`), so only the
frameless 6-byte GetClassId matched. `#pragma optimize("y", on)` at the top of the .cpp omits frame pointers for that file (as /Oy would for the
build; `"y", off` does nothing useful) and 12 more functions then match (key accessors, held object, pair, tail calls through a vtable slot). To match a
tail call through a slot use `VirtualSlot<Fn>(obj, slot)(obj, ...)` (the original loads the slot into a register; `VirtualCall` emits `jmp [eax]`) and
put the early-exit as `if (x) return call; return 0;`. What still blocks the rest: calls to std instantiations whose RVA matchcheck does not know
(`std::set<int>` insert/erase 0x2c4870 / 0x470940, `_Find_lower_bound` for a set 0x1d5d80, the list `_Free_non_head` 0x46f6d0, the service vector
`_Emplace_reallocate` 0x38c800 / 0x41dbd0; the unordered_map `_Find_last` 0x15ac20 is recognised only for the
`unordered_map<uint32, uint32>` shape); functions with EH frames (constructor, factory, destructor, AddObject, RemoveObject, Read); and helper
loops that the original inlines (Write: `__forceinline` the helpers and use `it != map.end()` loops, the sets use a cached end).

## cTSNeighborhood services (lane BN)

New members (src/ts/cTSNeighborhood.cpp, tests/ts_neighborhood_services.cpp): Initialize / Shutdown (message-service registration through the
verified key walkers 0x1c0458 / 0x1c081e; the service global 0x10c9378 is faked), Save, GetNameText, FormatNumber(Of), NotifyMembers,
GetStringTable (+0xa4: the 'STR#' resource 0x1f4 of the group, loaded on first use), AdjustCounter (slot 74: counter record
{0xfdfb4f5, group, 0, 0} in the persist system's segment), LookupEntryText (slot 75), SaveLot / SaveLots / ResetLots / RefreshLotFlags
(lot records {0xbf999e7, group, lot slot 18, 0}), GetOrCreateMember2/3. Persist system slots used: 16 SaveResource(resource, 0), 17 delete
record(key), 34 FindSegment(key, 0x656b8f0c, out), 36 list keys(&vector, group, type). DB segment slots: 20 exists(key), 22 open(key, out, mode),
23 create(key, out), 24 close(record), 10 flush. Record QueryInterface ids: 0xc51b11cb writer, 0x51b11bd reader (slot 8 = one int).
Byte matching: only Initialize and GetNameText matched. What blocks the others: a callee that is only difftest-verified (matchcheck proves callees
by bytes: GetDirectorFrameworkObject 0x1ae18d blocks GetStringTable, which is otherwise 100% identical; the persist getter 0xb390c0 blocks Save and
SaveLot), `std::map` iteration (the original calls the shared 0x15dc50, ours inlines it), and register scheduling of vtable loads. Tricks that
worked: `this ? (uint32)this + 0xc : 0` reproduces the `neg/sbb/and` base-conversion select; a struct key (not an array) plus `__declspec(safebuffers)`
removes the /GS cookie the original lacks; narrow (bool/byte) virtual results load the slot into a register, so declare them as real narrow
virtual functions (`NarrowSlot` in the .cpp) instead of VirtualCall. Test pitfall: a fake registered with N arguments must match the real
arity (a 0-argument fake for a 1-argument slot corrupts the stack).

## cTSPersistSpeculativeLoader (lane CD, structure pass)

The persist system's background preloader (primary +0x1c0, "object B"): a worker thread loads queued resource records,
the main thread's Update turns them into pooled/cached resources. Layout and declarations: `src/ts/cTSPersistSpeculativeLoader.h`;
per-function handoff (merge rules, EH maps, stack objects, fakes): `notes/feedback/CD.md`. Two ORIGINAL-BUGs to reproduce:
Queue::Insert erases a request it has just merged into, and QueueReferences leaves the child key's fourth field
uninitialised for version-0 reference lists.

## cPreloadedRecord and the serial record's variant bodies (lane CG, structure pass)

`src/ts/cPreloadedRecord.h` (the loader's raw-bytes record, 0x58) and `src/gz/cGZPersistDBSerialRecord.h` (now the full
0x44 layout, field codes, the base constructor 0x99b7c0 and destructor 0x1da060, and GetFieldString / GetFieldVariant /
SetFieldVariant). Handoff: `notes/feedback/CG.md`. Things the next lanes need:
- The serial record's translation unit has no C++ EH (no frames despite string locals); cPreloadedRecord's has.
- Stored variant codes are not cRZVariant tags (table 0xe5e788); Set writes Char/RZUnicodeChar scalars that Get cannot read.
- ORIGINAL-BUG: cPreloadedRecord::SeekRelative moves a negative in-range seek to the end of the record.
- Ghidra: method facts (db/methods/*) type the vtable calls, but their `this` is not in the slot's parameter list, so
  Ghidra's __thiscall puts the first real parameter in ECX and every typed virtual call leaves the stack model 4 bytes
  off (locals after such a call read as their neighbour). Read stack offsets from the disassembly until that is fixed.

## cTSPersistSystem (lane ES)

Facts learned (all in src/ts/cTSPersistSystem.h comments too):
- +0x174 is two bytes: OpeningContent (set during OpenContentPaths) and ContentOpened; OpenContentPaths ends by writing
  the word 0x100.
- +0x23c is the category map: std::map<int category mask, std::list<cRZString by value>> (map node 0x1c, key +0x10,
  list at +0x14; list node 0x28 = links + a cRZString with the game vtable 0xd9b470 and refcount 0). AddCategoryPath
  (ITS 44) appends lowercased names; OpenContentPaths passes the address of a mask's entry to 0x1e3c70 as the category.
  The names in it are the folder names a category accepts ("neighborhoods" for 0xf00).
- 0x1e3c70 (thiscall on the primary, ret 0x18: mode, cRZString path, flag, category entry or null, std::vector<segment>*
  out or null, 0) opens one content folder; it is where every file effect of OpenContentPaths and CollectPathGroups
  happens. Not replaced yet (biggest original callee left in this area).
- Init order: command line (unused), 0x1f3b70, the four caches (general +0xe8, long-term +0xe4, small-object +0xe0,
  sim +0xec) with their key filters (vtables 0xda5d20 / 0xda5d38 / 0xda5d50), Version.sig over the 0xec43275c path list
  (first hit: ITS slot 19(1), +0xb4 = 1), 36 RegisterFactory calls, the 0x2026960b / 0x4d45f8e6 type handlers and the
  speculative loader through the framework COM, the service hooks (cached service 0x10f5194 slots 9 and 7), the flush
  stopwatch (mode 4), then 14 fixed path groups (uitext 0x7fa7dc7c ... eptext 0x7fe10cb9) into mPathGroups.
- CollectPathGroups (ITS 47): the groups of every resource key in the segments under one folder, into a
  std::set<uint32_t> with allocator-service nodes. The key vector is freed through the service object's slot 4 (the
  original's 0x195720), not RZMemory::Free directly: the difftest's allocator fake sees the difference.

Test patterns (tests/ts_persist_system_es.cpp): original callees that touch files (0x1e3c70) are CodePatch stand-ins on
both sides; the remaining file probes (cRZFile::PathExists, DirectoryExists) run against folders the test makes in the
harness sandbox, with the game's wide-function globals (0x10cb5e0..0x10cb5f4) pointed at `difftest::SandboxFunction`.
Objects the code creates on the heap are compared by creation index, not address. In the difftest the game's operator
new/delete and ours can be mixed (no crash), but a ServiceBlockVector freed with RZMemory::Free while the allocator
service is a fake crashes in the GeneralAllocator: free through the service when the original does.

Standalone shape (not done): the class is still three faces over one primary block at game offsets, with `Self()`
casts. Converting it needs (1) the four faces as real C++ bases with the game's vtable order, or one class with three
vtable pointers built by us; (2) the 0x1e3c70 folder opener, the cache constructors 0x1fb300 / 0x9b35f0, 0x1f3b70,
0x1ea760 / 0x1e9eb0 and the path-group try_emplace 0x1d7390 replaced; (3) the category map and mPathGroups declared as
the std types they are (now raw head/size words); (4) the factory/handler classes Init creates through the framework
COM constructed directly.

## cTSPersistSystem (lane EW)

Done (difftest + mutants, tests/ts_persist_system_ew.cpp): ITS 20 OpenSegmentByInfo 0x1ddb10, its helper 0x1e5260
(PersistSetPackedFileGroup), ITS 37 CopyResource 0x1dd4f0, ITS 38 CopySegmentResources 0x1dd7a0, ITS 28
IsSegmentWritable 0x1e1d70.

Facts:
- mNamedMap (+0xf4) is the table of open files: key a cRZString (lowercased, backslashes) at node+8, the segment at
  node+0x28 (node 0x2c), hashed with nGZLog::HashChars; OpenSegmentByInfo walks a bucket from first to last inline.
- The segment info (cTSPersistDBSegmentInfo) as OpenSegmentByInfo reads it: slot 3 a value passed to 0x1e7230, 4 the
  folder, 5 the name, 6 the type (1 package, 2 ".files" folder, 3 plain folder, 0 = decide from the content-root option
  0x4c4324c5 through 0x1c2269, else +0xf0), 7 the group (16 bits), 9 flag test (1: add the extension, 2: set the group).
- Segment classes: 0x9af5e892 packed file, 0x9af5e881 directory; both created as cITSPersistDBSegment (0x3b2271bb).
- ifaces::cIGZCOM::GetClassObject is typed uint32_t in the method table; callers test al (cast to uint8_t).
- CopySegmentResources: the key fix-up for path-tagged groups (0x6f000000 in both segments) writes the source's group
  back over itself (no-op; marked ORIGINAL-BUG). CopyResource's 0x800-byte stack buffer is 0x1de280's scratch buffer.
- IsSegmentWritable's directory listing is a 0x3c-byte object with original methods 0x1f58f0 (ctor: path, primary),
  0x1f6dd0 first, 0x1f64f0 entry path (constructs a cRZString), 0x1f65c0 kind (low nibble 4 = skip), 0x1f61e0 next,
  0x1f6090 dtor: a replacement target of its own (0x1f58f0 is 1.6 KB and reads a listing cache through primary slot 47).

Test patterns: no sandbox needed when every file probe is a fake: GetFileAttributesW (wide global 0x10cb5f4), FindFirstFileA
/ FindClose (import slots 0xd9a1f8 / 0xd9a1f4, what DirectoryExists uses), CreateDirectoryW / RemoveDirectoryW (globals
0x10cb620 / 0x10cb624); callees that open files (0x1c2182, 0x1c116e) are CodePatch stand-ins. Buffers that live on the
original's stack are logged by content hash, never address. Overriding one slot of an adt::Fake (`fake.vtable[n] = ...`)
lets a fake log only the low byte of a bool argument the original pushes with stack garbage above it.

Left: see lane EZ below.

## cTSPersistSystem (lane EZ)

Done (difftest + mutants, tests/ts_persist_system_ez.cpp): primary slot 28 AddSegment 0x1e36f0, primary slot 30
RemoveSegment 0x1e57a0, ITS 24 DeleteGroupFiles 0x1de470.

Facts:
- NormaliseSeparators appends a trailing separator, so every mNamedMap key ends in a backslash (test keys must too, or
  the erase paths never match).
- +0x70 is the base deque's container proxy (its first word points back at +0x70); the base slot 30 reads through it.
- mMap154 (+0x154) node (0x3c): link, cRZString key at +8 (0x20 bytes with its refcount), two dwords at +0x28/+0x2c, a
  vector of dwords at +0x30..+0x3c. 0x1d9a10 is that node's value destructor (ecx = node+8: vector at ecx+0x28, then
  the key). Hash nGZLog::HashChars of the key.
- 0x1e6e80 (thiscall on the primary, ret 8) turns a path into mMap154's key (reads options 0x2bcfce83 / 0x4bcfce5e).
- 0x1c1931(path, 8) is the attribute test (directory); 0x1c5fa0(path, 1) deletes a folder recursively, 0x1c2e25(path)
  a file (both __cdecl).
- 0x1d6df0 is a counted-pointer move (AddRef the incoming, Release the overwritten); 0x1e6a40 the counted vector's
  destructor (allocator service); 0x1d86f0 erases an mSegmentIdRanges node without freeing its range; 0x1d8450 is
  appctor::DestroyElement; 0x1d89b0 copies a counted vector (AddRef each, allocator service).
- vf39 0x1e2850 (not done): listeners 4/5 notified, base vf39, then an inline cGZPersistDBSerialRecord subclass with
  its own vtables (0xda6470 and others) is built on the stack; needs that record class first.

Test patterns: segments are kOther fakes; adt::Tag names them all "<other>", so the EZ tests append "@segN" to each log
line (NameSelf) and log segments by index (SegName). The primary slot 54 is past BuildVTable's 48 slots: patch that
vtable entry with a free function. Hash tables are built for real (Table::Build(offset, nodes) with each node's hash) and
compared list order plus buckets.

Left (by size): vf168_50 0x1dcef0, vf168_46 0x1e3180, vf39 0x1e2850, vf168_37, vf168_55, vf168_58, vf168_38, vf168_54
(std::map at +0x1b8 via 0x1463b0 / 0x1e8fd0 / 0x1d9e80 / 0x1d9f70); then 0x1e3c70 and 0x1e7230 (6.4 KB segment set-up).
Lanes EW and FD did all of these but vf39 (see lane FD below).

## cTSPersistSystem (lane FD)

Done (difftest + mutants, tests/ts_persist_system_fd.cpp): ITS 50 CommitSegmentsWithBackup 0x1dcef0, ITS 46
AddPackageFile 0x1e3180, ITS 54 SetResourceName 0x1e4de0, ITS 55 GetResourceName 0x1e21b0, ITS 58 GetCachedResource
0x1dfaa0.

Facts:
- The rollback face (QI 0x8ceec300) slots: 3 GetSegment, 5 IsRollbackEnabled, 6 IsUnchanged, 7 EndRollback, 8 Commit.
  ITS 51 is CommitSegments; primary 54 ClearSpeculativeLoader. 0x1c1a65(path) is a drive's free space (64-bit).
- The map at primary +0x1b8 is std::map<segment id, std::map<type, std::map<pair(group, instance), cRZString*>>>
  (all nodes 0x1c; the strings counted). 0x1e8fd0 (thiscall primary, ret 4) fills one segment's names from its 'NmaP'
  resources. A segment with the directory-files face (0x856c8cf7) names a resource from its file name instead.
- 0x1c8365 (thiscall cRZFileName, ret 8: other, flag) is a compare, 0 = same. 0x1c2182(path) is the writable test.
- mChangeRecords (+0x20c) node 0x30: group at +8, change records at +0xc (built by 0x1f5850): load, list head (0x20
  entries: key 4 dwords at +8, counted object +0x18, change id +0x1c), size, bucket vector, mask, count, flag +0x20.
  0x1f6bd0(stream) reads them, 0x1f6780(key, changeId, out) is the cached lookup (stale entry erased), 0x1f6410(key)
  erases. The store record for a group is (0xad1a6720, 0, group, 0) in the object at +0x84 (slot 22 OpenRecord).
- ORIGINAL-BUG (reproduced): AddPackageFile's segment walk stops at the first segment whose name differs, so with two
  or more segments one is always "found".

Test patterns: cRZFile is faked by patching both the image's methods and ours (CodePatch on MemberCode(&cRZFile::X)),
since our replacement calls our cRZFile. cRZFileName runs for real with canonical absolute paths. Counted strings a test
hands over should start at refcount 2 so that a missing Release stays visible.

Left: vf39 0x1e2850 (needs the inline serial-record subclass), 0x1e3c70 (3.6 KB) and 0x1e7230 (6.4 KB segment set-up),
the 8-byte thunks.

## cTSPersistSystem (lane FK)

Done: primary slot 39 OpenRecord 0x1e2850 and its debug compare 0x1dacb0 (CompareRecordContents) by difftest + mutants
(tests/ts_persist_system_fk.cpp); the record it builds, cRAMShadowRecord (`src/ts/cRAMShadowRecord.h`, anonymous namespace
of this TU, 0x58 bytes, vtables 0xda6470 / 0xda656c / 0xda65c4 / 0xda6628): GetFieldVoid, GetSize, SeekAbsolute,
SeekRelative byte matches, deleting destructor 0x1da120 by difftest. Hook-shaped (the base serial record is).

Facts:
- cRAMShadowRecord = cGZPersistDBSerialRecord + position (+0x44), a std::vector<uint8_t> (+0x48, the 14.40 STL layout:
  the destructor inlines its _Tidy, big-block alignment included), error byte (+0x54). It shares cPreloadedRecord's folded
  slots (51, 53, 54, stream slot 18) and has the same ORIGINAL-BUG in SeekRelative.
- OpenRecord's shadow path: with +0x22d set, a store at +0x84 and a read (mode's low byte 0), ITS 58
  GetCachedResource(0x6d1b65ee, key) decides; a hit opens the store's record and hands out a RAM copy (its child is the
  segment primary slot 34 finds). A miss opens through the base and, for records of at most 0x4000 bytes, deletes the
  store's copy (store slot 26), copies the record in (ITS 37, source = the record's child queried for 0x656b8f0c) and
  notes it (ITS 59 with an empty cRZVariant). The result is 1 whenever a record came back, whatever the base returned.
- +0x22e turns on a debug compare (the base's record against the copy; the answer is unused) whose record the original
  does not null-check: a failed base open there crashes the game too.
- cIGZPersistDBRecord slots 15 QueryChild(iid, out) and 16 SetChild(child) (the child at record +0x18, i.e. object +0x1c).
- The serial record's streams write only a byte at +8 (cRZOStream/cRZIStream::Construct): the three bytes after it, and
  after +0x40, stay heap garbage; tests compare them by low byte.

Left: 0x1e3c70 (3.6 KB; lane FL did 0x1e7230), the 8-byte thunks.

## cTSPersistSystem (lane FL)

- 0x1e7230 is `cTSPersistSystemPrimary::SetUpSegment` (verified, 19 mutants); OpenSegmentByInfo and AddPackageFile call
  ours. What it does is in the header comment and the status entry. Facts worth knowing elsewhere: the path-group table
  mPathGroups (+0x1e8) gets entries from a segment's own 'NREF' (text = path, key group = id; overwrites) and 'GLOB'
  records (name at +0x40, or +0x41 when the record's last byte is non-zero: an original quirk, kept); "SCObjects" and
  "objects" folders and ".bundle.package" files get group -1; the type-3 (template) directory gets 19 fixed extensions
  from the table at 0xda6420 and a scene-graph file-name converter (ctor 0x1f5f80, vtable 0xda6e70).
- Test pattern (tests/ts_persist_system_fl.cpp): one adt kind per fake object (segment, group face, record, key status,
  directory; the packed face a FakeObject with BuildVTable<64>) so no two fakes share slot definitions; the real
  mPathGroups table built with 0x1d8a50 and compared as a dump; the table's node bytes zeroed in the primary block.
  The keys block GetResourceKeyList fills is freed through the allocator service's slot 4 (FreeThroughAllocatorService),
  not ServiceBlockVector::FreeBlock: the latter goes to RZMemory's general allocator and crashes under the allocator fake.
- Callers' tests (EW, FD) patch their 0x1e7230 stand-in over our SetUpSegment as well as the image.
- 0x1e3c70 (named `cTSPersistSystemPrimary::OpenContentFolder`, tentative) is left. Its shape, from a read: a listing
  over the folder (0x1f58f0 ctor, 0x1f6dd0 First, 0x1f64f0 entry path with '\' made '/', 0x1f65c0 entry flags, 0x1f61e0
  Next, 0x1f6090 dtor; returns 1). Per entry: SplitInto name and extension; flag 4 (a folder, as read) with extension
  ".files" (after 0x1603f0(0xda52b4, 1)) gives kind 2 (class 0x9af5e881); flag 8 (a file) with ".package" gives kind 1
  (class 0x9af5e892, cRZFile slot 3 (2,2,1) for the read-only flag); another folder gives kind 3 when 0x1ad030 on a
  ".sgfiles" string answers (meaning not worked out) or, at the top level (flag 0, no extension), its name is CASFace /
  LightRigs / Objects / (0xda63d4) / Textures / Thumbnails; any other folder recurses (flag 1) unless the category entry's name list excludes it. Kind k counts in primary dword
  [0x2e + k]. The path key (0x1e6e80) is looked up in the hash at +0x178 (an existing segment: appended to +0xd8, path
  normalised and lowercased, 0x1d84b0, slot 6(1, read-only), 0x1e9b50, 0x1e97b0) and then in the one at +0x198 (found: nothing
  more). Otherwise, with a last-argument list, each list object's slot 5(name + extension) goes through 0x845799 and
  the segment is made only at the first that answers 0 (null list: always). The new one is created through the framework (QI 0x3b2271bb), primary slot 47 into its slot 17, our SetUpSegment(path, name,
  mode, kind, segment), 0x1dab60 for kind 1, and when the segment opens: primary slot 27 AddSegment, slot 6(1,
  read-only), the map at 0x1d7df0 (+0x28 holds it, counted), and the out vector gets it. Expect a test world close to lane EW's
  (director framework, segment fakes) plus a fake listing; the recursion makes it the largest piece left here.

## cTSPersistSystem (lane FQ)

- 0x1e3c70 is `cTSPersistSystemPrimary::OpenContentFolder` (verified by difftest, tests/ts_persist_system_fq.cpp);
  OpenContentPaths and CollectPathGroups call ours, and their ES tests patch the stand-in over our member too. What it
  does is in the header comment. The two open questions of lane FL's map, settled from the code:
  - the listing's kind byte (0x1f65c0, entry byte +0x24 & 0x7f) is 4 for a folder and 8 for a file: the scan 0x1c5896
    stores 4 when FindFirst's directory flag (entry +0x20) is set, else 8;
  - 0x1ad030 is `mov eax, [ecx]; jmp [eax + 0x30]`: cRZString slot 12, IsEqual(const cIGZString &, bool), called on the
    extension with ".sgfiles" and false. So ".sgfiles" folders (any level, case-insensitive) are template directories.
- Other facts: "Thumbnails" folders are templates at any level; CASFace / LightRigs / Objects / Sims / Textures only at
  the top level (`recursive` byte 0) and without an extension. Only `recursive` folders recurse (top-level calls with 0
  never do). Template and ".files" folders get a trailing '/' (0xda52b4) and open writable; the entry's '\' are all '/'.
  mFieldB8[1..3] (+0xbc..+0xc4) count package / files / template entries. mMap178 and mMap198 are cRZString-keyed
  tables of counted segments (node 0x2c, value +0x28, try_emplace 0x1d7df0): 178 holds segments kept from an earlier
  open (re-listed, not re-created), 198 the segments this opener made (a key in it is skipped). The sixth argument (no
  caller passes one) is a std::list<cRZString> of wanted file names, matched with _stricmp against name + extension.
- Test pattern: the listing stand-in keeps its state in the listing's own 0x3c bytes (nested listings during the
  recursion), and its entries come from the seed and the listing path's hash so the test can preload mMap178 / mMap198
  with keys the listing will produce. cRZFile's constructor stand-in must store the image's cRZFile vtable: the original
  calls Open through it.
- Left in the class: the 8-byte thunks; the standalone shape (lane ES's list).

## cTSPersistSystem (lane FX): standalone shape, stage A

- The live object is now our class (`cTSPersistSystem` at the end of src/ts/cTSPersistSystem.h; constructor, destructor and
  forwarding overrides in src/ts/cTSPersistSystemShape.cpp): our six vtables, our constructor (0x1d8b90) and destructors
  (0x1d9cb0, 0x1da310), and the base as a twin class `gz::cGZPersistResourceManager` (src/gz/cGZPersistResourceManagerShape.*,
  constructor 0x9b3f30, destructors 0x9b4120 / 0x9b41d0) over the hook-shaped struct's bodies. The bodies are unchanged:
  every slot forwards to the hook-shaped member. Bisect switch: the hook 0x1d8b90.
- The four unmapped thunks are now verified without a hook (our vtable's compiler code): 0x1dab30 / 0x1e3980 (primary
  AddRef / Release), 0x1da053 (+8 deleting destructor), 0x1e012a (provider slot 0 -> ITS 10).
- Facts: the base is built only inside 0x1d8b90; 0x1d9cb0 is called only by 0x1da310, which is reached only through the +8
  slot 3 thunk 0x1da053. The base destructor releases the default segment +0x84; the derived destructor does not release
  the background thread +0x1e4 (as the original).
- Left: stage B (bodies into the class, one-statement adapters, two-route tests); cTSPersistCOMDirector::OnStart
  (src/rz/COMDirectors.cpp) still calls the constructor as an original (through the hook it reaches ours; switch it to
  `new cTSPersistSystem`).

## cGZDBPFIndexCompressed (lane GM): the large-package index, whole class, standalone shape

- **Which index a package gets.** cGZDBSegmentPackedFile 0x9af590 (index load from the file): no entries -> a Modifiable
  (`RecordStore`, 0x9a6cf0); fewer than 0x40 -> Static (0x9a6da0, 0x20 bytes); otherwise Compressed (`new 0x38`, constructor
  0x9a6ca0, slot 20 from the index words) and Static when slot 20 answers false (the compressed build never does). 0x9ae5c0
  (load from a cached index record {0x086d4a89, 0, path crc}) builds Static / Compressed **inline** with the game's vtables
  and calls slot 18 and the destructor directly (0x9adb20, 0x9a7720): game code, so those objects reach our code through the
  slot adapters. The neighbourhood package (thousands of records) is a Compressed index after every load.
- **Layout** (src/gz/cGZDBPFIndexCompressed.h, 0x38): `std::vector<Entry>` +4 (16 bytes: instance, resource, offset, u16
  size, u16 memory size), `std::vector<Record>` +0x10 ({size, memory size} for entries whose sizes need 32 bits: Entry size
  0xffff and the u16 is the record index), `std::vector<Block>` +0x1c ({type, group, first entry, next}), block count +0x28,
  the record FindRecord returns +0x2c ({offset, size, memory size | bit 31}). The constructor leaves +0x28..+0x37 alone.
- **The blocks are a hash table** of (type, group) groups: bucket (type + group) % count; SetEntries (slot 18) groups the
  entries in an unordered_map hashed by type + group (MSVC 14.40's: its list order decides the placement), gives each
  group its bucket's block, and puts a group whose bucket is taken into the next empty block, linked after its own bucket
  (coalesced chains, `next` -1 ends). Entries are sorted by (resource, instance) per block; FindEntry 0x9ab2e0 binary-searches
  them (signed indices; with the two flag bytes it falls back to a scan for "any resource" / "resource 0"). The vector has
  one block more than the count: its first-entry word is the entry count, and CollectTypes/Groups (slots 8, 9) insert its
  zero type and group too (they walk the whole vector).
- **Slots** (cIGZDBPFIndex, src/gz/cIGZDBPFIndex.h, the implementable interface the three indexes share): 15 SetRecordMemSize,
  20 SetEntriesFromIndex (index words: 6 per entry for minor version 2, else 5; memory size = size | bit 31), 21
  WriteIndexEntries (slot 19 then the words; the key map runs twice per key, ORIGINAL-BUG), 22 writes the **DIR record**
  (records whose size < memory size; 4 words for v1, 5 for v2): the caller view's name `WriteIndex` for slot 22 is misleading.
  Slot 18 returns a bool in al (the caller view says void). The key map (segment +0x18) is called through slot 4 in SetEntries
  (building from the file) and slot 3 everywhere else (handing keys out), each with (in, out) = the same key; GetResourceKeyList skips DIR blocks (0xe86b1eef), sorts by offset
  and appends to a vector over the allocator service. Folded: 1 false, 2 entries empty, 12 null, 13 nothing, 14 false, 23 nothing.
- **Done**: all 19 functions (constructor, deleting destructor, 16 slots, FindEntry), verified (3 byte matches, the rest
  difftests on three routes in tests/gz_dbpf_index_compressed.cpp). No original callee left. 0x9acb40 (slot 3) is also
  called directly by three cTSWinProcInventoryNew functions on their own objects: it is `vector<16 bytes>::size` folded,
  and our adapter compiles to the same 10 bytes.
- **Left in the family**: cGZDBPFIndexStatic (9 functions, 2.2 KB) and cGZDBPFIndexModifiable (11, 2.9 KB) are hook-shaped;
  both can derive from src/gz/cIGZDBPFIndex.h now (its destructor is defined in cGZDBPFIndexCompressed.cpp: move it to a
  file of its own then). 0x9ae5c0 and 0x9af590 (cGZDBSegmentPackedFile) are the creation sites to convert to `new`.
  (Done by lane GS, next section; the two creation sites are still game code.)

## cGZDBPFIndexStatic and cGZDBPFIndexModifiable (lane GS): the other two indexes, standalone shape

- **cGZDBPFIndexStatic** (vtable 0xe5fedc, 0x20 bytes): `std::vector<cGZDBPFIndexEntry>` (+4, full 0x1c-byte entries),
  `std::vector<Entry *>` buckets (+0x10), bucket count (+0x1c, (n / 6) * 6 + 1). A key's bucket is (type + group +
  instance) % count; BuildBuckets 0x9ad220 std::sorts the entries by bucket and points bucket b at its first entry
  (count + 1 pointers). FindRecordForWrite (slot 12) scans the bucket: first exact match, else the *last* any-resource /
  resource-0 candidate (the compressed and modifiable indexes take the first). Slot 11 (folded 0x1ad030) is a tail jump to
  slot 12; slot 15 (folded 0x9afdb0, shared with Modifiable) calls slot 12 and replaces the memory size's low 31 bits.
  CopyEntries (slot 19) maps keys with the key map's slot 3 into a *zeroed* key, not in place. Every slot keeps its adapter:
  0x9ae5c0 builds Static indexes inline with the game's vtable and calls slot 18.
- **cGZDBPFIndexModifiable** (vtable 0xe5ff40, 0x24 bytes): an `unordered_map<key, cGZDBPFIndexRecord>` hashed by the
  instance alone, nodes and buckets from RZMalloc (MSVC 14.40's map reproduces the game's find / emplace / rehash / erase:
  tested). Its constructor 0x9a6cf0 was RecordStore::Construct in the packed file's group: moved here, and the packed file's
  EnsureStore / Open now `new cGZDBPFIndexModifiable` (RecordStore is gone; tests use cGZDBPFIndexModifiable::kVTable and
  patch our vtable too, OurStoreSlotPatch in tests/gz_dbsegment_io_fixtures.h). The vtable is then stored nowhere, so
  17 slot functions are "in effect through callers" (trap Hooks.cGZDBPFIndexModifiable_Construct=1); vf4 and vf7 keep
  adapters (byte matches). vf5, vf6, vf16 lost their byte matches in the shape (std iteration allocates registers
  differently) and are difftested.
- **Helpers left as the game's STL instantiations**, reached only from our replaced slots now: 0x9a7460 (map destructor),
  0x9b2c90 (emplace), 0x9a6910 (erase), 0x9b2f20 (vector<entry> resize), 0x9a6090 / 0x9a5bf0 (std::sort bodies).
- **Pitfall**: callers.py resolves `GameAddress(kVTable)` through every `kVTable` in the header of the same stem: a nested
  struct's constant (RecordStore::kVTable in cGZDBSegmentPackedFile.h) made the packed file's own vtable stores look like
  stores of 0xe5ff40.

## cGZDBSegmentPackedFile: the file's own I/O (lane IP)

- **Done** (src/gz/cGZDBSegmentPackedFile*.cpp, tests/gz_dbsegment_packed_file_io_*.cpp and gz_dbsegment_io_fixtures.h (split by lane FX), all verified and hooked): the
  segment-cache index load/save (LoadCachedIndex 0x9ae5c0, SaveCachedIndex 0x9b0670), Open's readers (ReadHeader
  0x9af2a0, FindHeader 0x9aadf0, ReadIndex 0x9af590, ReadHoles 0x9af390, ReadCompressionDirectory 0x9aea00), Flush's
  writers (WriteIndex 0x9b1d50, WriteHoles 0x9b1a70), StoreWrittenRecord 0x9b1fe0 (the subobject's close commits a record
  written through a handle) and CopyOneRecord 0x9a8be0 (CopyRecordsTo's per-record copy). Open, Flush, CopyRecordsTo and
  the record close now reach no original code of this class but Compact's 0x9a83d0 and the record constructors (0x9a70f0,
  0x9a6e60, 0x9a6de0, another class).
- **Cache record**: key {0x086d4a89, 0, Crc32(path, 1), 0} in the segment cache (+0x14): version 3, the subobject's
  GetSegmentChangeID, the 0x60-byte header, the entries (CopyEntries, namespace +0x18), the free-range count and pairs.
- **Slot 20 of the index (SetEntriesFromIndex) takes the minor version as its fourth argument**, not the word count;
  the readers and writers pick 5 words per entry for minor version 1, 6 for 2 and 0 otherwise (nothing is read then).
- **mSwapRangesOnFlush (+0x19c)** is WriteHoles' flag: cleared first, set once +0x194 has been rebuilt as +0x184 plus the
  pending ranges +0x18c; Flush swaps the trees on it.
- **ORIGINAL-BUGs reproduced**: ReadIndex deletes the old index twice below 0x40 entries (a double delete of the same
  pointer when there was one); StoreWrittenRecord counts references *down* for a record stored uncompressed (up on the
  other two paths); AddFreeRangeTo's selector is not range checked.
- **Unreachable paths kept**: ReadIndex's fallback when the compressed index refuses (its SetEntriesFromIndex always
  answers true); WriteHoles' branch for a refused AddFreeRangeTo (always true).
- **ABI**: CopyOneRecord's two bytes arrive as stack words that CopyRecordsTo passes on unchanged (take uint32_t, use the
  low byte); the packed interface's GetRecordStoredSize / ReadRecordData / WriteRecordData answers are tested as whole
  words, so they are called with word returns.
- **Left**: Compact (0x9a83d0, 1.6 KB): copies every record into a temporary packed file next to the package
  ("TempDBSegmentPackedFile_" names through 0x1c23bf), moves the files with cRZFile::MovePath and reopens; it needs a
  file-system world in the harness (the moves, PathExists, the delete 0x1c2e25) or a contract.
- **Test pitfall**: anything that runs the original RebuildRefs (0x9ad3b0, through ReadIndex) reaches an atexit
  registration (0x197100 -> 0x1be693) and corrupts the heap unless the test has a ServiceScope. EnsureStore may replace
  the fake store with a fresh cGZDBPFIndexModifiable: patch its slots on both vtables (SlotPatch + OurStoreSlotPatch).

## cGZPersistBufferResource and cTSFloatConstants (lane NU)

Two whole classes in the standalone shape (real C++ classes, construct adapters hooked at the original constructors, slot
adapters dropped as "in effect through callers").

- **cGZPersistBufferResource** (0x3c bytes; `src/gz/cGZPersistBufferResource.*`, tests `tests/gz_persist_buffer_resource.cpp`,
  10 tests). Layout: +0 cGZPersistResource, +0x1c the buffer face, +0x20 buffer, +0x24 aggregate, +0x28 format, +0x2c bits,
  +0x30 a `vector<uint32_t>` of dither error rows. Hooked: constructor 0x9b64a0, ReadBmpRecord 0x9b6930, ReadTgaRecord
  0x9b77b0, ReadBmpFile 0x9b6c30, ConvertBitmap 0x9b8140, IsSupportedBitmap 0x9b8db0, the two dithers 0x9b88e0 / 0x9b8b40
  (Floyd-Steinberg, serpentine, over 2W+4 words). The factory and the Gimex factory now call ours; their
  `original_calls` for the class are gone. Readers are tested with synthetic BMP/TGA bytes through the record fake.
  Mutants: 55 + 40 + 7 + 1 + 2, each killed or argued. Still original: ReadTgaRecord calls 0x9cd6a0 / 0x9cea90 / 0x9cd890.
- **cTSFloatConstants** (0x18 bytes: +4 refcount, +8 dirty byte, +0xc a `vector` of 0x48-byte entries; class id 0x46434e53,
  iid 0x7b6faf7e; `src/ts/cTSFloatConstants.*`, tests `tests/ts_float_constants.cpp`, 25 tests). The entry has its own
  2-slot vtable (Read 0x4df500, Write 0x4dfa90), a value at +4, the name at +8 and the description at +0x28 (cRZString).
  Hooked: constructor 0x4df0c0, destructor 0x4df260, Create 0x4df2d0, GetFloat 0x4df320, Remove 0x4df760, Load 0x4df4a0 and
  Save 0x4df880 (the persist system's slots 73 and 72, cell 0x10faf6c). Mutants: 38 (36 killed, 2 equivalent).
- **cTSNeighborhoodConstants::Load** 0x47a630 (stack table, 13 named constants with defaults, the three int ones truncated,
  globals 0x10daa30..48, singleton slots 48/50/52, a save when dirty and the segment's slot 9 says writable, loaded byte),
  **cTSConstantsClient** vf0 0x4df3d0 and **cTSAutonomyConstantsClient** vf4 0x417030 (the deleting destructor): tests
  `tests/ts_neighborhood_constants_load.cpp`, 24 mutants, all killed. cTSConstantsClient vf2 0x4df450 is 4 bytes: left.
- Still original callers of ours: cTSAutonomyConstantsClient vf3 0x5230f0 and 0x6161a5 (they call our hooked constructor,
  destructor, GetFloat, Remove, Load and Save). Three SEH funclet thunks (0xd3d5a3, 0xd45d53, 0xd509e9) were added to
  `db/missed-functions.jsonl`. The five original STL instantiations (0x4ded40, 0x4dedc0, 0x4def60, 0x4df000, 0x4dfc90)
  stay `reimplemented`, in effect through callers only.

## Pitfalls seen

- (lane NU) The original dither fills its error rows with `rep stosd`; fill the same words, not just the used ones.
- (lane NU) The TGA RLE reader has a stray balanced allocator push; it is balanced, so only the freed-block log shows it.
- (lane NU) A hex literal of an original vtable RVA anywhere in `src/` counts as holding the original vtable: name it in
  a comment in words, not as a literal.
- (lane NU) A trap stub for a function with N arguments leaves the stack unbalanced if a fake slot has a different count:
  fake vtable slots must be `__fastcall` with the same argument count.
- (lane NU) Freed-block ranges are reused inside a test: compare freed sizes, never addresses; reserve log vectors before
  the recorded span (`ResetLog()`), or their growth shows up as extra frees on one side.
- (lane NU) A cRZString inside a hand-built entry needs its Construct/Destruct by hand (no compiler union ctor).
- (lane NU) Python on Windows rewrites files with CRLF: `sed -i 's/\r$//'` after any script that writes db/ or src/.
- (lane NU) Some original paths decode uninitialised bytes (BMP sizeImage smaller than the file, a ReadBmpFile with no
  rows read): such inputs cannot be tested, the generators leave them out.
- (lane NU) The Save condition's `IsDirty()` needs a seed whose table ends clean (every constant present) to be killable.

- Several originals leave stack bytes unwritten when a stream read fails (cTSFamily Read, cTSNeighborhood
  Read's flag byte): start locals at zero and keep test streams delivering those bytes.
- Fakes that hand memory to the original must initialise all of it; anything left unset makes results
  depend on memory layout (TSObject footprint tests, 2026-10-15).
- Argument evaluation order: MSVC evaluates call arguments right to left, so `Call(Call(app, 26), 43, Call(n, 15))` calls
  n's slot 15 before app's slot 26. Hoist nested calls into locals in the original's order (cTSFamilyManager, 2026-10-15).
- Sorted vectors of counted pointers (cTSFamilyManager's families, comparator 0x4b6e60 sorts descending): std::sort over
  `NeighborDataRef` (copy-only counted pointer) reproduces the game's sort 0x4b4370 call for call, AddRef/Release included.
- RZMemory::Malloc goes to the GeneralAllocator at 0x10cb098: tests that allocate messages or block vectors construct it
  (tests/ts_family_services_world.cpp, tests/ts_family_manager.cpp) and make the allocator-service fake forward to it.
- tools/mutate.py refuses to run until a status entry ties the test to the mutated src: write db/status first.
- A log vector that grows during a side allocates through the patched game allocator and shows up in FreeRecorder's
  sizes: reserve log vectors before the call (cGZPersistBufferResourceFactory tests).
- ReadBuffer's raw path with a padded surface reads `stride * height` bytes of the packed source (swapped strides):
  test records must hold that many bytes even when dataSize is smaller, or the original reads past the fake's buffer.
- ORIGINAL-BUGs reproduced: cTSNeighborhood Read with a null state helper; cTSPersistSystem vf168_14
  uses an object after releasing it; cTSFamily Read writes flag bit 1 twice.

## Left (from the backlog, 2026-11-03)

- cTSWantSimulator: 28 vtable slots still game code (slot 52 11.7 KB, 53/58/59 ~2.3 KB each, Import 1.3 KB, DoMessage 648 B; GA's list in the class status notes); P16 rest (IV): 22 remaining functions. The loaders' remaining entries are expat handler default bodies folded across classes (not class code).
- cGZPersistResourceManager vf17 0x9b4df0, vf19 0x9b49f0, vf20 0x9b5380: verified by byte match until QN made the key filter class ours; need a differential test (a manager object with fake locks and slots) to return to verified. It must also exercise our key-filter class (IsKeyIncluded, QueryInterface iid 1, Release): coordinator mutants on all three survived (2026-10-30), nothing reaches it yet.
- cTSPersistSystem row of the Left table says 13 left, candidates.py shows 0: correct it. 0x1e5200 SetKeyedObject and 0x1e3b10 vf168_56 dropped to reimplemented (byte match drifted, HI `--drift`; 5 more slots lost the match but keep their difftest).
- cTSPersistResKeyList_Create 0x1f4dc0 hook is skipped in every game run ("site doesn't match"; hand CRC by lane DW): fix the CRC (relocations?) or the function-start fact; its 4 dropped slots' traps rest on it.
- cTSFloatConstants (20 functions, ~2.2 KB) as a class, then cTSNeighborhoodConstants vf3 Load 0x47a630, cTSConstantsClient vf0/vf2, cTSAutonomyConstantsClient vf4 0x417030 (NN).
