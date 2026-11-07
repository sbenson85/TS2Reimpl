# World-lot campaign: subsystem map and lane plan

Lane VN, 2026-11-03: the campaign's dependency-map lane (`docs/roadmap.md`, Campaign order, *Inside a campaign*). It
was a planning lane, with no `src/` work.

**Resized by lane XN, 2026-11-06**, after XJ filed the unassigned code. Sections 1 and 2 are VN's figures from run-1023
and still name the groups well; sizes and the lane list here are current. By `tools/subsystems.py --summary --no-write`:
3,589 functions and 991,813 B filed, 66,719 verified, 499,635 on evidence, 492,178 inferred, and 133,086 B more guessed in
`other`. **The campaign is 1,124,899 B in 4,045 functions (filed plus guess), 1,056,966 B to do, 578,212 B of it ran in the
reference set** (`docs/coverage.md`), 342,987 B of the filed code in run-1023.

XN moved 181 KB of tool and dialog code out to ui-game: 110,342 B filed to filed, 11,192 filed to guessed, 59,139 guessed to ui-game. How right the
inferred band was, by hand: of XJ's 562,408 B, 87.5% stayed; 10.5% was wrong, nearly all of it these tools; the rest went
to guessed bands. After the move a random sample of 40 was 40/40 right, by digest (callers, callees, strings), so about
right for planning and not independent of the rule. In the guess band 25 sampled were mostly right; doubtful: 0xd15f60 (road
textures, likely neighbourhood), 0x7e4ce0, 0x257cf0 (60 callers), 0x4c7fc0. A lane takes a guessed row after looking at it.

**Measure.** The figures come from run-1023 (`local/coverage/run-1023.json`, 2026-10-23,
`tools/scenarios/coverage-phases.txt`), joined with the current `db/status`. Bytes are executed and not done, counted
by whole groups (class, else ~owner), as in `tools/py -I tools/coverage.py queue|deps local/coverage/run-1023.json
--by group`. Edges are function-level and weighted by callee bytes.

The run's rounds:

| round | label | what runs |
|---|---|---|
| 0 | start | start-up |
| 1 | menu-idle | the main menu, steady |
| 2 | hood-load | the neighbourhood load |
| 3 | hood-idle | the neighbourhood view, steady |
| 4 | lot-load | `loadlot 6`, 300 sim frames |
| 5 | lot-live | 300 more sim frames |

**There is no lot-resave coverage run**, so the lot save and write paths, build mode and the other roof shapes were not
recorded. Lot 6 exercises:
- only two SG roof shapes (diagonal half-hipped and diagonal half-gable);
- no pool geometry (cPoolGeometryNode never ran);
- almost no routing (cTSRoute: 14 B).

Static class sizes (from `db/classes.jsonl` slot bodies) are given where the run says little. Before the roof, pool and
route lanes, run a coverage pass over a lot with pools, other roofs and a save (see section 6).

## 1. Scope

The world-lot queue is **401,018 B in 1,252 functions, 113 groups**: 12.5% of the 2,560,164 B executed and not done.
The campaign has no hard test boundaries: no not-done world-lot function calls Win32, file or Direct3D imports
directly.

**Groups** (KB executed not done; "inf" is the part filed to the group by caller ownership, so check the owner first;
"rounds" are the coverage rounds that ran it):

| group | KB | fns | inf KB | done | rounds |
|---|---:|---:|---:|---:|---|
| nTSSG::cTerrain | 28.7 | 92 | 19.4 | 0% | 0-5 |
| nTSSG::cNHoodTerrain | 27.2 | 67 | 20.8 | 0% | 2,3,4 |
| nTSSG::cFenceGeometryNode | 23.2 | 47 | 21.3 | 6% | 4 |
| nTSSG::cFloor | 22.7 | 42 | 16.6 | 0% | 2,4,5 |
| cTSRouteManager | 20.6 | 50 | 12.2 | 0% | 0,2,4 |
| nTSSG::cFenceManager | 19.6 | 39 | 12.7 | 0% | 0,2-5 |
| nTSSG::cWallManager | 14.2 | 62 | 8.1 | 0% | 0,2-5 |
| cWallGeometryNode::cWallSide | 13.9 | 15 | 12.3 | 0% | 4 |
| nTSSG::cWallGeometryNode | 13.5 | 56 | 11.0 | 5% | 4,5 |
| nPathPlanner::cPathPlanner | 12.7 | 51 | 9.1 | 0% | 0,2,3,4 |
| nTSSG::cWallCutawayManager | 11.4 | 19 | 9.7 | 0% | 0,4,5 |
| nTSSG::cFloorManager | 10.9 | 33 | 5.1 | 0% | 0,2-5 |
| nTSWorld::cWorldRoof | 10.5 | 34 | 5.1 | 0% | 0,2,4,5 |
| nTSSG::cFloorGeometryNode | 9.8 | 27 | 6.1 | 4% | 2,4,5 |
| nTSWorld::cPartitionManager | 9.5 | 60 | 3.9 | 0% | 0-5 |
| cTSNHoodTerrain | 9.0 | 11 | 1.6 | 57% | 2,4 |
| nTSWorld::cWorldDB | 8.3 | 13 | 3.7 | 73% | 0,4 |
| nTSWorld::cWorldTerrain | 8.1 | 25 | 3.4 | 0% | 0,4 |
| nTSSG::cBaseSGRoof | 7.6 | 10 | 1.5 | 0% | 4 |
| cTSWinProcLotIntro | 6.8 | 13 | 5.9 | 0% | 2,4 |
| cRZIniFile | 6.7 | 22 | 6.5 | 96% | 0,2,4 |
| nTSSG::cRoofManager | 5.8 | 20 | 2.2 | 0% | 0,2,4 |
| nTSWorld::cRoomManager | 5.3 | 20 | 3.2 | 0% | 0,2,4 |
| nTSWorld::cWallGraph | 4.9 | 23 | 2.2 | 1% | 4 |
| nTSSG::cFloorThicknessGeometryNode | 4.7 | 20 | 3.8 | 0% | 4,5 |
| nTSWorld::cWorldOutsideSupport | 4.7 | 19 | 3.3 | 2% | 0,2,4,5 |
| ~nTSSG::cDiagonalHalfHippedRoof | 4.5 | 2 | 4.5 | 0% | 4 |
| nTSSG::cAwningManager | 4.5 | 19 | 2.7 | 0% | 0,2-5 |
| nTSSG::cTerrainGeometryNode | 4.4 | 8 | 3.4 | 15% | 4 |
| nTSSG::cAwningGeometryNode | 4.2 | 11 | 1.2 | 5% | 2,4 |
| nTSSG::cBaseGeometryNode | 3.9 | 10 | 0.7 | 0% | 2,4 |
| nTSSG::cCurvedPoolManager | 3.3 | 9 | 0.4 | 0% | 0,2-5 |
| nTSWorld::cProxyManager | 2.9 | 14 | 1.9 | 4% | 4 |
| nTSWorld::cBaseRoofType | 2.8 | 10 | 0.3 | 0% | 4 |
| cTSWinProcLotInfoDialog | 2.6 | 10 | 0.3 | 0% | 2,4 |
| nTSSG::cTerrainOverlayGeometryBase | 2.5 | 12 | 0 | 0% | 2-5 |
| nTSSG::cTerrainSnowManager | 2.1 | 11 | 0.7 | 0% | 0-5 |
| ~nTSSG::cCurvedPoolGeometryNode | 2.0 | 6 | 2.0 | 0% | 2,4 |
| nTSSG::cTerrainOverlayGeometry | 1.8 | 2 | 0 | 0% | 4,5 |
| nTSSG::cTerrainGeometryData | 1.7 | 7 | 0.5 | 0% | 0,4,5 |
| nTSWorld::cWorldCurvedPool | 1.5 | 7 | 0.4 | 3% | 0,4 |
| cWallGeometryNode::cWallTop | 1.5 | 3 | 0.6 | 0% | 4 |
| nTSWorld::cStringMap\<G\> | 1.4 | 9 | 0.4 | 0% | 0,4 |
| cTSWinProcLotCatalog | 1.3 | 10 | 0 | 0% | 2,3,4 |
| nTSSG::cPoolManager | 1.2 | 6 | 0 | 0% | 0,2-5 |

**The tail is under 1.2 KB a group, about 22 KB in all:**
- Templates:
  - two cSerializableMap copies (proxy manager: 1.2 + 0.4);
  - c3DArray\<T\> for cRoomIDTile, cFloorPatternStorageTile, cFloorPatternRotationTile, cDeckFoundationMarkerTile,
    cFloorVisibilityTile, float, cAwningTile and byte (0.3-0.7 each);
  - c2DArray\<tVertPaintInfo\> 0.8 and c2DArray\<E\> 0.2.
- Small classes and pieces:
  - cRugManager 1.1, cTSLot 1.1, cTerrainCutoutObjectManager 1.0, cRugGeometryNode 0.8, cTerrainCutoutObjectGeometryNode
    0.4;
  - the path planner's pieces: cQuadTree 1.0, cAStarGraphNode 1.0, cPortal 0.4, cQuadTreeCell 0.4, cAStarSearch 0.3,
    cAStarPortalNode 0.3;
  - the wall graph's pieces: cWallLayer 0.6, cVertexLayer 0.5, cWallEdge 0.4, cFencePostLayer 0.4, cWallVertex 0.3,
    cWallSection 0.2;
  - the paint alpha texture strategies (1.6);
  - the cWallGeometryNode, cFenceGeometryNode and cFloorThicknessGeometryNode pieces;
  - cFloorColorGrid 0.3, cTerrainShadeLight 0.2, cWallDiffusionManager 0.3, cTSUserToolElevator 0.2, cTSLotThumbnail
    0.6, cTSTerrainPaintEntry 0.6, cBasePolygon 0.05;
  - the roof destructor groups (~cDiagonalHalfGableRoof 1.1, ~cBaseDiagonalRoof 1.0, ~cDiagonalPagodaGableRoof 0.3);
  - cGableRoofType 0.5.
- STL residue under done owners (~std::_Tree_val, ~cTSLotInfo, ...) is 100% done and is counted here only by
  inference.

**Filed here but belonging elsewhere:**
- The tools and UI procs: **moved to ui-game by XN.** The rule now files cTSUserTool* and the lot dialogs and panels there.
- cRZIniFile (6.5 KB) is in framework-gz now.
- cTSNHoodTerrain's rest (9.0 KB, 57% done) is neighbourhood's class, but it sits on cNHoodTerrain; take it with lane
  WL-NT below.

**Filed elsewhere but world-lot by structure** (take them with their family):
- cMergedGeometryNode (scenegraph, a cBaseGeometryNode subclass);
- cLightSnowGeometryNode (scenegraph, a cGeometryNode subclass beside cTerrainSnowGeometryNode);
- cPoolWaterGeometryNode (unassigned);
- the gable polygon classes cEven/OddTriangle{First,Second}{Gable,HalfGable,Hipped,HalfHipped,Mansard} (scenegraph or
  unassigned) and their unnamespaced copies (world-lot);
- cGableRoof (labelled scenegraph by its callers; it is the roof family's base shape).

cNHoodFloraGeometryNode (neighbourhood) is also a cBaseGeometryNode subclass, but stays in neighbourhood.

**Already done:**

| class or group | functions verified | test |
|---|---:|---|
| nTSWorld::cWorldDB (standalone shape, lane JC) | 105 | tests/world_db.cpp |
| TSWorld group (QI, GetGZCLSID and small slots of cWorldTerrain, cWorldRoof, cWorldPool, cWorldCurvedPool, cWallGraph, cWorldOutsideSupport, cPartitionManager, cProxyManager; the wall-graph slots moved to the classes) | 22 | tests/world_ts_world.cpp |
| cTSNHoodTerrain | 56 | tests/ts_nhood_terrain.cpp |

Status files also exist for cTSLotInfo, cTSNHoodOccupantManager, nTSSGNHoodOccupantManager, WorldMessageData and
PathAttributes. In nGZSceneGraph, cGeometryNode (D3) is done: it is the base of 17 world-lot nodes, ctor 0xa3dd80.

## 2. Dependencies

In the campaign graph, world-lot owns 401,018 B, calls out 37,774 B, is called in for 53,420 B and has 1,488 internal
edges. Bottom-up it sits at position 6, after other-ts and before the scenegraph. Calls into later campaigns total
28,659 B: scenegraph 15,238, unassigned 3,522, ui-game 3,462, neighbourhood 3,079.

The campaign is inside the big cycle {animation, neighbourhood, objects, scenegraph, ui-game, ui-windowing, unassigned,
world-lot}. Its lightest edges are animation -> world-lot (4,615) and neighbourhood -> world-lot (5,200).

### After the resize (XN, 2026-11-06)

The tables below are VN's, from run-1023 at its filing, and they name the groups well; these are the totals now. Static
direct calls of filed code:

| out to | calls | callees | B | not done |
|---|---:|---:|---:|---:|
| framework-gz | 1,828 | 108 | 8,105 | 2,406 |
| other | 1,128 | 252 | 38,941 | 37,730 |
| scenegraph | 716 | 138 | 19,510 | 4,165 |
| rendering-gfx | 163 | 35 | 4,921 | 13 |
| ui-game | 42 | 3 | 284 | 10 |
| objects | 35 | 9 | 3,447 | 3,240 |
| simantics-vm | 31 | 11 | 1,392 | 367 |
| persist-dbpf | 30 | 8 | 3,970 | 3,506 |

Called in by: scenegraph 211 calls from 107 callers, other 138 from 120, ui-game 36 from 31, audio-video 19, neighbourhood 17,
cas 17, animation 14, objects 13.

**The 381 upward calls to ui-game weren't a dependency.** XJ filed part of the build and lot tools here and part in
ui-game: cTSUserToolFireplace, Object, Roof, Wall, FloorPattern, TerrainPaint and TerrainSculpt and the lot dialogs here,
their bases (cTSUserToolObject, the ground cursor, buy-build and ground-based tools, cTSWinProcGrid, cRZWinProc4,
cTSWinProcPopup) and Awning, Stairs, Water and CurvedPool there. Those calls were the tools calling their bases. With
the tools moved, filed code makes 42 calls to 3 callees (284 B), 10 B not done. Counting guesses too, 106 calls to 27
callees, 5,122 B, which are misfiled small helpers: FreeStringBuffer 0x152b80 (39 calls, 127 B, a foundation helper filed
through group-majority:AppDebugger), GetCachedSingleton_10cc45c 0x212e61 (22), and the module of unnamed helpers at
0x7dd000-0x7f3000. XO filed all three: FreeStringBuffer is framework-gz, the other two are the next section's. There is
no real call up.

What the lanes need from unfinished campaigns is small and mostly one lane at a time (distinct callee bytes, not done, by
lane cluster): scenegraph at most 2,641 (roof nodes), usually under 1 KB; that module and shared helpers 1-5 KB each;
persist-dbpf 4,063 (WL-L only).

### The helper module 0x7dcb60-0x7f45c0 (XO, 2026-11-06)

Once "the module of unnamed helpers". It isn't generic strings and containers: it is **nTSWorld's helper module**, 88 KB
of free functions and small value classes over `cIWorldDB`, plus object placement. The evidence: the cached global
0x10cc45c that 95 of its 363 functions use is service 0x4a037ee3 / iid 0x49ff7d6f, our cWorldDB; nTSWorld::cDllCOMDirector
sits inside it (0x7e5b50) and nTSWorld::cBaseDiagonal, the first roof class, starts at 0x7f45c0, the byte after it ends. It
starts after cTSKissingPointsPersonalStringDialog (0x7dcb5b), and it is aligned at 16 bytes where the ui-game classes before
it are packed. No vtables; two strings. So nothing in the map could place it: the call graph filed it by whoever called
most, ui-game for a third of it. The map now files it by address, `band:` (`db/subsystem-bands.jsonl`; docs/subsystems.md).

| cluster | range | fn | KB to do | what it is | not-done callees outside |
|---|---|---:|---:|---|---|
| tile maths | 0x7dcb60-0x7ddea0 | 54 | 3.7 | position to tile and back (0x7dd9d0, 70+ callers), quadrants, angles, 16.16 fixed; some verified, group SimAnticsGeometry (stays simantics-vm) | framework thunk |
| tile predicates | 0x7ddea0-0x7e4000 | 94 | 24.0 | walls, floors, fences, objects and terrain on a tile, through cIWorldDB | 1 KB: 0x256950, 0x257cf0, 0x2c9bd0, 0x350e40 |
| heights | 0x7e4000-0x7e5b50 | 16 | 6.0 | GroundHeightAt 0x7e4680 (1.4 KB, levels, slopes), roof height, the rest | framework thunk |
| placement validator | 0x7e5fd0-0x7eec00 | 37 | 32.0 | **objects**: Validate 0x7e8c60 (SimAntics, Lua, tools), position blocked 0x7e7e00, snap search; object slots, QI 0x5a6216f6 | 0.7 KB |
| tile rects | 0x7eec00-0x7ef640 | 17 | 2.2 | tiles of a box, tile-edge records | framework thunk |
| catalog accessors | 0x7ef640-0x7efa00 | 30 | 0.8 | catalog entry (cITSGlobals slot 16, slot 19) kind and flags, thin wrappers | none |
| SnapState | 0x7efa00-0x7f0e00 | 31 | 4.6 | **objects**: the placement result, four maps, position record 0x7eff40, add 0x7f0720; 0x7f0c00, 0x7f0ca0 done (SimAntics) | 0.3 KB |
| iterators | 0x7f0e00-0x7f1df0 | 49 | 3.5 | tile box iterator 0x7f12a0 / 0x7f1840 and small value classes | framework thunk |
| wall accessors | 0x7f1df0-0x7f29a0 | 16 | 2.8 | through slot 39 GetPartitions | 0.1 KB |
| tile edges | 0x7f29a0-0x7f45c0 | 14 | 6.7 | 1/16 inset geometry of tile edges, the tail (read lightly) | 0.3 KB |

**It's a shared layer, and it's a leaf now.** Every cluster's callees are cWorldDB (verified), the framework, the world's
sublayers through their interfaces, and each other. What each campaign calls of it, distinct not-done callees from filed
not-done callers: ui-game 20.8 KB (111 functions), world-lot 19.9 (131), scenegraph 6.7 (50), objects 5.5 (29), animation
4.5 (28), simantics-vm 3.7 (10), lua-scripting 2.0, rendering-gfx 1.6, audio-video 1.3, sim-person 0.6, sim-core 0.4. So
what lanes have been faking, one at a time, is one 50 KB world layer. The lanes, before WL's tool lanes and, for WU-1, worth
having before scenegraph's world-facing lanes:

| lane | clusters | KB | needs | risk | model |
|---|---|---:|---|---|---|
| WU-1 | tile maths, tile rects, catalog accessors, iterators, heights | 16.2 | fake cIWorldDB in the global 0x10cc45c, or our cWorldDB; a fake catalog in cITSGlobals | low: mostly pure; 0x7e4680 is the one with thought in it | Sonnet medium | **Part 1 done (lane RY): tile maths and catalog accessors verified, fixture tests/worldfx.h. Left: tile rects 0x7eec00-0x7ef3b0, catalog helpers 0x7ef3f0-0x7ef590, iterators 0x7f0e00-0x7f1df0, heights 0x7e4000-0x7e5b50 (0x7e4680 GroundHeightAt, still original; semantics in the lane note). 0x7dd770 and 0x7ddcf0 call the partitions service: WU-2.**
| WU-2 | tile predicates | 24.0 | WU-1 | medium: unnamed slots of the world's sublayers (partitions, roofs), the fence and wall bit meanings | Opus for the first third, then Sonnet |
| WU-3 | wall accessors, tile edges, the last of the director (0.7) | 10.2 | WU-1 | medium: edge geometry | Sonnet medium |
| SP-1 | SnapState, footprint rects 0x7e95b0, 0x7e9050 | 7.2 | WU-1; the node allocators are already understood (simantics.md) | low | Sonnet medium |
| SP-2 | the validator | 29.4 | WU-1 to 3, SP-1; a fake object with slots to 0x4d4 | high: 4 KB bodies, 0x7e9e40, 0x7ebff0, 0x7eced0, and the error codes | Opus |

Total 87 KB. SP lanes are filed under objects: they call objects through slots, and Snap and the tools call them.

Filing check: 25 functions read by hand, seeded random across the five bands (0x7f12f0, 0x7ef260, 0x7e6ba0...): 25 belong
to the module, no generic helper from elsewhere. The world-lot and objects split inside it is my judgement of what the
code does, not something the module's layout shows; 0x7e72b0 and small ones like it could go either way. Those samples
are reads of the code, but they were picked inside bands drawn from the same reading, and most of the other functions
I saw only as one-line digests, which show their neighbours, so they aren't an independent check of the address rule.

### Out of world-lot

| into | B | what (callee groups, B) | state there |
|---|---:|---|---|
| scenegraph | 15,238 | ~cGeometryNode+1 2,760; cLightMapManager 1,509; cOverlayNode 1,324 (11 callees); ~cIndexedMeshBuilder 756; gable polygons (cOddTriangleFirstGable 613, cEvenTriangleFirstGable 610, the Second ones ~190 each: from ~cDiagonalHalfGableRoof); ~cGeometryDataContainer 601 (from cPathPlanner); cGableRoof 583 (from cRoofManager); cSceneManager 569; cWaterGeometryBuilder 540 + 403 (from cTerrain); ~cShadowManager 458; ~cGeometryNode 408; small: cIndexedMeshBuilder+4, cShapeRefNodeImpl, cShape, cTSWinSceneGraph, cLightingManager, cGeometryBuilder, cCompositedTextureUpdateStrategy, cCompactorBuilder, cExplicitBoundNodeT, cLightT, cExplicitShearBoundNode, cViewerRefNodeImpl, cRayQueryNearerStrategy, cLotImposter, cDiffusionManager | nGZSceneGraph implementation-complete (cGeometryNode, builders, data container ours). Open: nTSSG later clusters (lighting 202 KB incl. cLightMapManager / cShadowManager, lot geometry cluster: cOverlayNode, cLotSkirt; TSSG system; camera) |
| shared-utility | 4,495 | cTSUserToolObject ~1.3K, ~cTSEffectCheat+5 441, cTSUserToolRoof helpers, cRZStringVector, cGZDrawContext, cTSDecalDescription, property-set serializer, cBaseParser, cProcessDeformationsBuilder | foundations campaign; mostly mis-filed tool/effect helpers |
| unassigned | 3,522 | cGZPersistResourceManager 415, cApplyShaderProgramConstants 322, std helpers, cTSUserToolFlattenLot, cTSPatternCatalog, cTSPerson, the hipped polygon helpers | triage |
| ui-game | 3,462 | cTSWinProcGrid 1,602, cRZWinProc4 727, cTSWinProcFamilyDetails 445, cTSMainToolbar getters | all from the UI procs, now filed in ui-game (XN); see *After the resize* |
| other-ts | 3,303 | cTSPatternBase 1,480 (from cTSTerrainPaintEntry), cTSDecalEffect 776, cTSDataTables, the cTSProduct* catalogue products (wall, fence, roof pattern, deck foundation, terrain paint) | other-ts is not a campaign; the products are catalogue data |
| neighbourhood | 3,079 | cNHoodFloraGeometryNode 2.0K, cTSWinProcNeighborhoodUCP 646, cNHoodOccupantManager 369 | neighbourhood follows world-lot |

### Into world-lot

| from | B | what |
|---|---:|---|
| scenegraph | 23,745 | cBaseSGRoof 6,160 (from cGableRoof, really intra-family); ~cDiagonalHalfHippedRoof+9 2,567 (from ~cGableRoof); ~cWorldRoof+13 1,403 (lighting, lightmap, shadow); ~cBaseSGRoof+8 1,342; cTerrain 922; ~cFenceManager+9 661; cWorldRoof 589; ~cCurvedPoolGeometryNode+2 584 (from cMergedGeometryNode); cNHoodTerrain ~1.1K; cFloorManager 538 (cTSWinSceneGraph, cTSSGSystem); ~cPathPlanner+15 521; cGableRoofType 453; then decals, awning, rug, floor colour grid, wall diffusion, partition, route, room, outside support |
| unassigned | 19,097 | mostly "(none found)" callers: virtual or COM entry points (cNHoodTerrain 1,650, cFenceGeometryNode 1,334, cWorldTerrain 784, cFloorManager 784, cTSRouteManager 723, ...); PrimRObject, PrimTObject, cPinToFloorConstraint, cChoreoQueue, cTSPerson |
| neighbourhood | 5,200 | cNHoodFloraGeometryNode -> cBaseGeometryNode 2,462, awning and curved-pool helpers; cTSWinProcNeighborhoodUCP -> LotInfoDialog / LotCatalog |
| animation | 4,615 | nTSSG::cObject / cAnimatable -> ~cWorldRoof+13, cFenceManager, cWallManager, cPartitionManager, cWorldOutsideSupport, cRoomManager, cCurvedPoolManager; nGZSceneDBAnim -> ~cPathPlanner+15 |
| ui-game | 3,797 | cRZWinProc4 / cTSUIDialogManager -> the lot UI procs; cTSMainToolbar, cTSUIModeBuild -> cFloorManager, cWorldRoof, cFloor, cWallCutawayManager |

### Cut edges

These follow the roadmap (scenegraph first; its calls into world-lot's geometry are the cut).
1. **world-lot -> nTSSG later clusters** (cLightMapManager, cShadowManager, cLightingManager, cOverlayNode,
   cSceneManager, cLotImposter, cDiffusionManager, cTSWinSceneGraph): about 6 KB. They stay original calls in
   world-lot's tests, behind recording fakes of the managers' interfaces. These clusters are open in the scenegraph,
   and world-lot doesn't wait for them.
2. **world-lot -> other-ts products and patterns** (3.3 KB): fakes. The catalogue lookups return product records the
   test plants (section 6).
3. **world-lot -> neighbourhood** (cNHoodFloraGeometryNode, UCP): original calls. Neighbourhood follows.
4. **Callers into world-lot** (animation, unassigned prims, ui-game): not world-lot's concern. They reach our code
   through hooks.
5. **Inside the campaign**, the one real cycle is roofs: cGableRoof (scenegraph-labelled) <-> cBaseSGRoof <->
   cWorldRoof. Take the roof family as one track (lane WL-R0 first).

**Internal edges** (`deps --pair world-lot,world-lot`): the heaviest callee groups are:
- cNHoodTerrain helpers 20.2 KB (from cNHoodTerrain, cFloor, cTerrain);
- cTerrain 19.4 (from cTerrain, cNHoodTerrain);
- cFenceGeometryNode 15.1 + 6.1 (from fence node and manager);
- cFloor+1 12.9 (from cFloor, the floor and thickness nodes);
- cWallSide+1 11.7, cFenceManager 11.1, cTSRouteManager 10.9, cWallCutawayManager 9.5, cWallGeometryNode 9.5,
  cPathPlanner 8.5, cWallManager 7.6.

cPartitionManager (4.3 KB) is called from route, fence, wall and partition code, so it is a leaf for them.

Inferred owners to recheck:
- ~cWorldRoof+13 (1.4 KB) is called from floor, wall, outside support and lighting: probably a shared tile helper, not
  roof code.
- ~cCurvedPoolGeometryNode (1.1 KB) and +2 (1.0 KB) are called from wall and fence code.

## 3. Template families and shared layouts

Sizes are the to-do slot bodies of the class's vtables (static, `db/classes.jsonl`). "Unique" means not shared with
another family member.

**F-a. nTSWorld storage templates.**
- c3DArray\<T\>: 8 copies, 7 slots, 0.7 KB each.
- c2DArray\<T\>: 2 copies.
- cSerializableMap: 2 copies.
- cStringMap\<G\>: 1.4 KB.

That is about 5.4 KB executed, 7.5 KB static. Ordinary practice does one template, one lane, one test per body with a
case per copy (as scenegraph F1). These are not a trial family: there is nothing to reuse beyond the template itself.

**F-b. cBaseGeometryNode family.** All members derive from cBaseGeometryNode (0xdb7d80+0:56) on nGZSceneGraph's
cGeometryNode (D3, done).
- cBaseGeometryNode itself: 61 to-do slots, 3.9 KB.
- 3.45 KB of its slots are inherited unchanged by every member.
- Two sub-shapes:
  - **At +0:** cFenceGeometryNode (74 to-do / 5.0 KB static, 41 unique, 1.4 KB) and cPoolGeometryNode (57 / 4.8 KB, 24
    unique, 1.2 KB); also cMergedGeometryNode (scenegraph-filed) and cNHoodFloraGeometryNode (neighbourhood).
  - **At +4**, behind an interface vtable at +0 and with a nested `c*GeomPart : cRZUnknown`, 6 vtables, 110 slots,
    38 done each:

    | member | to-do slots / static | unique slots / B | executed KB |
    |---|---|---|---:|
    | cRugGeometryNode | 72 / 9.5 KB | 14 / 4.8 KB | 0.8 |
    | cTerrainCutoutObjectGeometryNode | 72 / 8.6 KB | 14 / 4.0 KB | 0.4 |
    | cCurvedPoolGeometryNode | 74 / 7.8 KB | 38 / 3.8 KB | 2.0 |
    | cAwningGeometryNode | 56 / 6.7 KB | 21 / 3.1 KB | 4.2; plus cAwningPiece and c3DArray\<cAwningTile\> |

    Rug and terrain-cutout share 58 slot bodies, so they are near copies. Curved pool and awning share 35-36 with the
    rest.
- Differences between members: the GeomPart's geometry builder (rug quad, cutout mask, curved pool shell, awning tiles)
  and the manager that owns the node.
- Each node has a manager of its own: cRugManager, cTerrainCutoutObjectManager, cCurvedPoolManager, cAwningManager.
  The managers share no base (below).

**F-c. Roof family.** The largest repetition in the campaign, and mostly not executed.
- **SG roofs.** nTSSG::cBaseSGRoof : cRZUnknown has 11 to-do slots, 8.6 KB static (7.6 KB executed); cBaseSGRoofSnow
  derives from it.
- **The shapes.** 21 shapes plus a *Snow variant of each: Gable, GlassGable, TallGable, PagodaGable, HalfGable,
  PagodaHalfGable, Hipped, PagodaHipped, HalfHipped, Mansard, Conical, Dome, Octogonal, DiagonalGable,
  DiagonalTallGable, DiagonalHipped, DiagonalPagodaHipped, DiagonalHalfHipped, DiagonalHalfGable,
  DiagonalPagodaGable, DiagonalPagodaHalfGable.
- **Shape layout.** Each shape has vtables at +0 (24-25 slots), +20 (the nTSWorld roof type, 21-23 slots) and +28
  (tRoofObject, 2).
- **Unique to-do per shape** is 1.4-10 KB: Conical 1.4, Gable 4.0, Mansard 5.0, Hipped 5.8, DiagonalHalfHipped 6.1,
  HalfHipped 10.0.
- **Roof types.** nTSWorld::cBaseRoofType (2.5 KB static) has 15 types, 1.5-10 KB each (HalfHippedRoofType 10.2,
  HippedRoofType 6.1, DiagonalHalfGableRoofType 5.6, ...). The diagonal ones sit under cBaseDiagonalRoof.
- **Polygons.** nTSSG::cBasePolygon -> cBase{Even,Odd}Triangle{First,Second} -> {Gable, HalfGable, Hipped, HalfHipped,
  Mansard}. That is 20 classes of 0.9-2.1 KB, plus about 10 unnamespaced copies of about 0.9 KB.
- **Size** (XN, summed): lanes WL-R0, R1, RF1, RF2 and RS are 236 KB, filed plus guess, replacing VN's 200-250 KB estimate.
  46.5 KB of that ran in the reference set, about 17 KB in run-1023.
- This is the campaign's candidate for intervention B (a generator), and it should wait for:
  - cBaseSGRoof and one shape done by hand (lane WL-R1);
  - a coverage run over a lot with several roof shapes, because executed bytes don't justify the rest yet.

**F-d. Terrain overlay family.**
- Members: cTerrainOverlayGeometryBase (2.1 KB, nothing unique), cTerrainOverlayGeometry (1.8 unique),
  cFloorOverlayGeometry (1.4), cLotSkirtOverlayGeometry (1.5), cNHoodTerrainOverlayGeometry (0.7).
- 9 slots (1.75 KB) are shared by all five. That is about 5 KB executed, 15 KB static.
- Small enough for one ordinary lane, so the trial would add nothing.

**F-e. Managers (a pattern, not a family).** cWallManager, cFloorManager, cFenceManager, cRoofManager, cPoolManager,
cRugManager, cAwningManager, cCurvedPoolManager, cTerrainSnowManager and cTerrainCutoutObjectManager share a shape, not
a base:
- each is `nTSSG::cI<X>Manager` at +0 plus cIGZMessageTarget2 at +4 (4 slots);
- a few slot bodies are folded across them (0x145260, 0x308f40);
- what they share is the fixture: message dispatch, a cWorldDB-backed lot and the node factory. That makes them the
  main consumers of section 6's fixture.

**F-f. Direct cGeometryNode subclasses** (not a family: they share only D3's base, which is done):
- cFloorGeometryNode (3.9 KB own, 7 vtables), cWallGeometryNode (2.5 plus cWallSide / cWallTop / cWallPiece),
  cTerrainGeometryNode (0.7), cTerrainSnowGeometryNode (5.2), cFloorThicknessGeometryNode (0.9);
- also cLightSnowGeometryNode (scenegraph) and cPoolWaterGeometryNode (unassigned).

*Parked (`notes/decisions.md`, 2026-11-03: the trial runs on the first family of 3+ members of the foundations and
rendering closure audit, not here). The members are now in lanes WL-1, WL-B1 and WL-B2. This nomination and its cost
table are history.*

**Nomination for the family-reuse trial: F-b's four +4 nodes** (rug, terrain cutout, curved pool, awning):
- They meet the rules: they are not yet implemented, they share the cBaseGeometryNode layout and the GeomPart
  arrangement, and one fixture serves all four (geometry node at +4, devfx device, a planted lot).
- The first lane, WL-1, is the "first member": cBaseGeometryNode plus the frozen fixture. The four nodes are the
  consumers.
- Managers stay out of the member lanes, so the measured unit is the node class.

Per-member costs as an ordinary lane would see them (from the cConvexT curve: first about 2x the median of the later
members; lanecost medians Sonnet medium $6.3, Opus $13.1):

| member | model | ordinary estimate | why |
|---|---|---:|---|
| WL-1 cBaseGeometryNode + fixture (first member) | Opus | $13 | new fixture, the +0/+4 layouts, 3.45 KB of inherited slots |
| cAwningGeometryNode (+ cAwningPiece) | Sonnet medium | $7 | most executed, tile array |
| cCurvedPoolGeometryNode | Sonnet medium | $6.5 | 38 unique slots |
| cRugGeometryNode | Sonnet medium | $5.5 | 14 unique, 4.8 KB |
| cTerrainCutoutObjectGeometryNode | Sonnet medium | $4.5 | 58 slots shared with rug |

- Ordinary family estimate: $36.5 (consumers $23.5).
- Pilot cap: $36.5 + $13 = **about $50**.
- The trial is "promising" if the four consumers total at most about $18.8 (20% under $23.5).
- Alternates if the coordinator prefers executed bytes: cFenceGeometryNode and cPoolGeometryNode (the +0 sub-shape). Fence
  is 23 KB executed, but 21 KB of that is inferred, so it is an owner-check risk rather than a clean member.

## 4. Class order and lane list

Order rule: `notes/decisions.md` 2026-11-01. Families and shared layouts come first, then classes from the leaves up,
each class whole. Lanes are clusters of 20-60 KB (XN, 2026-11-06), because a lane's merge, mutants and validation are a
fixed cost; a split needs a real dependency or risk. KB is to do, static, whole classes, filed plus guess. "Ran" is the
reference set, counting a class's done rows too (`docs/coverage.md`). Figures come from the filing after XN's moves,
with unnamed functions given to the class of their callers; they don't count code still filed in other campaigns, which
the row names with a +.

XN checked the order against run-time calls between these clusters over the reference set, and VN's layers hold:
storage and partition first (cPartitionManager <-> storage 1,221 + 605 B; walls -> partition 5,324), terrain leaves
below the structure nodes (fence -> leaves 2,462, floors -> leaves 2,310), roof shapes -> types 2,239, routes -> path
planner 5,850. The 1,425 B edges from fence and from the awning, pool and rug nodes to neighbourhood terrain are one
shared helper, not a dependency on WL-NT.

Tracks after WL-0 and WL-1:
- **world data:** WL-2, WL-3, WL-4, WL-4S;
- **nodes:** WL-B1, WL-B2;
- **roofs:** WL-R0, WL-R1, then WL-RF1, WL-RF2, WL-RS;
- **structure:** WL-F, WL-W, WL-FL;
- **terrain:** WL-T, WL-NT;
- **path:** WL-P1, WL-P2, WL-P3;
- **catalogue:** WL-L.

Lanes on different classes can run two at a time when they share no files.

| lane | classes | KB | ran | needs | test world | risk | model |
|---|---|---:|---:|---|---|---|---|
| WL-0 | **Storage templates and wall-graph pieces.** c3DArray\<T\> (8 copies), c2DArray\<T\> (2), cSerializableMap (2), cStringMap\<G\>; cWallLayer, cVertexLayer, cFencePostLayer, cWallEdge, cWallVertex, cWallSection; cWorldDB's remaining 7 slots and its inferred residue (~cWorldDB+2, owner check). **Lane VO done:** c3DArray (ten copies, 37 functions) and cVertexLayer + cFencePostLayer (22 functions), sections below. **Lane VP done (WL-0b part):** cWorldDB calls our cVertexLayer, StoreVTable returns its target, c2DArray\<tVertPaintInfo\> + 0x4af570 (6), cSerializableMap GUID copy + shared bodies (6). **Lane VQ done (WL-0c):** cSerializableMap string copy, cStringMap\<G\> (8; cWorldDB's pattern table is ours), cWallLayer (13), sections below. **Lane VR done (WL-0d):** cWallEdge (27), cWallSection (4), cWallVertex (8), section "Wall graph". **Left, 36.7 KB, 18.0 of it guessed, mostly cWorldDB's (the class is 49.3 KB, 18.5 done):** cWallLayer's setter 0x820080, the cStringMap helpers 0x8135e0 / 0x8138a0 / 0x814130 / 0x814080, cWorldDB Load 0xd0fb30 / Create 0xd0ef80 / thunks / residue | 36.7 | 27.9 | - | plain + our cWorldDB (tests/world_db.cpp) | low: hook per copy, check `shared_vf_*` | Sonnet medium |
| WL-1 | **lotfx fixture, cBaseGeometryNode and terrain cutout** (section 6). cBaseGeometryNode 5.5, both sub-shapes' layouts (+0, +4 with GeomPart) written down here; cTerrainCutoutObjectGeometryNode 6.9, its manager 2.4 as the first consumer | 14.7 + fixture | 6.7 | WL-0 (planted grids) | lotfx: sgfx node world + devfx + planted cWorldDB lot | medium: every later lane uses the fixture | Opus |
| WL-2 | **Partition, wall graph, rooms.** cPartitionManager 26.3, cWallGraph 18.1, cRoomManager 10.1 | 53.3 | 25.5 | WL-0 | lotfx world half (no device) | medium: big static class, graph invariants | Opus |
| WL-3 | **World components.** cWorldTerrain 17.8, cProxyManager 8.4, cWorldOutsideSupport 6.6, cWorldPool 6.3, cWorldCurvedPool 3.3, cSupportChanged 0.5 | 42.6 | 18.6 | WL-0 | lotfx world half | low-medium | Sonnet medium |
| WL-4 | **Terrain geometry leaves.** The overlay family F-d, cTerrainGeometryNode 6.1, cTerrainGeometryData 3.5, cFloorColorGrid 1.5, the paint alpha strategies, cTerrainShadeLight, the elevation change specs, cTSTerrainPaintEntry 1.3 | 24.3 | 15.8 | WL-1 | lotfx | medium: float, texture strategies | Sonnet medium |
| WL-4S | **Terrain snow.** cTerrainSnowGeometryNode 31.0, cTerrainSnowManager 5.2 (+ cLightSnowGeometryNode from scenegraph) | 36.0 | 2.7 | WL-4 | lotfx | medium: barely run, needs a snow scenario for the in-game check | Sonnet medium |
| WL-B1 | **Awning and rug.** cAwningGeometryNode 14.7, cRugGeometryNode 10.1, cAwningManager 7.9, cRugManager 7.6, parts | 41.9 | 22.4 | WL-1 | lotfx + message fake | low-medium | Sonnet medium |
| WL-B2 | **Pools.** cCurvedPoolGeometryNode 18.1, cPoolGeometryNode 13.2, cCurvedPoolManager 9.1, cPoolManager 5.4, parts (+ cPoolWaterGeometryNode from scenegraph) | 47.3 | 21.3 | WL-1, WL-3 | lotfx + message fake | medium | Sonnet medium |
| WL-R0 | **Roof data.** cWorldRoof 15.1 (with the ~cWorldRoof+13 owner check), 15 roof types (cHalfHippedRoofType 8.1, cDiagonalHalfGableRoofType 6.5, cMansard 5.4, cHipped 4.0, ...), cBaseRoofType 3.4, cBaseDiagonalRoof 5.2 | 58.9 | 15.1 | WL-0, WL-2 | lotfx world half | medium | Opus |
| WL-R1 | **SG roof base.** cBaseSGRoof 23.0, cRoofManager 11.1, cGableRoof 1.6, the polygons (+ the 7.9 KB filed in scenegraph), cAppendWallLocator 11.9 and cAppendValidWallLocator 1.6 (0x7a3e9a-0x7a752c, reached from the gable polygons, never run) | 58.4 | 20.0 | WL-R0, WL-1 | lotfx | high: float geometry, the family's first member | Opus |
| WL-RF1, WL-RF2 | **Roof shapes**, two lanes of about 36 KB: cGlassGableRoof 9.3, cOctogonalRoof 7.9, cPagodaGable 5.0, HalfGable 4.7 and the rest; generator candidate | 72.3 | 9.1 | WL-R1 + coverage | lotfx | medium (repetition) | Sonnet medium, after a generator decision |
| WL-RS | **Roof snow.** cBaseSGRoofSnow 16.4, cDiagonalPagodaHippedRoofSnow 6.2 and the other shapes' snow; generator candidate | 45.5 | 2.3 | WL-R1, WL-4S + coverage | lotfx | medium (repetition) | Sonnet medium, after a generator decision |
| WL-F1 | **cFenceGeometryNode** 42.1 + its pieces | 42.5 | 26.7 | WL-1, WL-2 | lotfx | medium-high: largely inferred, owner check first | Opus |
| WL-F2 | **cFenceManager** | 29.9 | 20.5 | WL-F1, WL-2 | lotfx + message fake | medium | Sonnet medium |
| WL-W1 | **cWallGeometryNode** 18.2 + cWallSide 13.3 + cWallTop 1.5 + cWallPiece | 33.2 | 30.1 | WL-1, WL-2 | lotfx | high: most of cWallSide is inferred | Opus |
| WL-W2 | **cWallManager** 27.5, cWallCutawayManager 20.1, cWallDiffusionManager 0.3 | 48.1 | 28.9 | WL-W1 | lotfx + message fake | medium: 13.4 KB guessed | Sonnet medium |
| WL-FL1 | **Floor nodes.** cFloorGeometryNode 24.2, cFloorThicknessGeometryNode 11.0 + piece | 35.9 | 31.3 | WL-1, WL-4 | lotfx | medium | Opus |
| WL-FL2 | **cFloorManager** 16.3, **cFloor** 12.1, the query filters | 29.2 | 21.4 | WL-FL1 | lotfx | medium-high | Opus |
| WL-T | **cTerrain** (half of it guessed; cWaterGeometryBuilder calls stay original or scenegraph) | 38.7 | 28.9 | WL-3, WL-4, WL-FL2 | lotfx | high: rounds 0-5 | Opus |
| WL-NT | **cNHoodTerrain** 36.6, cTSNHoodTerrain's rest 15.6, cNHoodTerrainGeometryBuilder 4.1, cSubset 0.5 | 56.9 | 48.8 | WL-T | lotfx + neighbourhood terrain planting | high | Opus |
| WL-P1 | **Path planner.** cPathPlanner 51.6, cAStarPortalNode 3.3, cQuadTreeCell 3.3, cQuadTree 2.3, cAStarGraphNode 1.5, cPathObstacleInfo, cPath, cPortal, cAStarSearch, cTSPathFilter 0.5. Split into leaves and planner if the first look says so | 65.1 | 57.2 | WL-2 | plain (planted obstacle grid) | medium: search order, 103 of cPathPlanner's 143 functions inferred | Opus |
| WL-P2 | **Routes.** cTSRoute 32.1, cTSRouteManager 24.2, cTSLot 3.2 | 59.5 | 47.1 | WL-P1, WL-2 | lotfx + person/object fakes | high | Opus |
| WL-P3 | **cTSRoutingObjectAggregate** (0x4627f0 10,329 B, vf34 0x467430 4,179 B, vf18 0x45c630 3,882 B; 12.7 KB guessed). Reached virtually, almost no direct external callers. Not in VN's plan: run-1023 showed 12 B of it | 51.3 | 34.7 | WL-P2 | lotfx + person/object fakes | high: new structure | Opus |
| WL-L | **Lot catalogue.** cTSLotCatalog 27.1 (0x4c4ed0 5,926 B; 305 B ran), cTSBaseLotInfo 4.5, cTSLoadLotController 1.8 (cTSLotInfo is done). Calls persist-dbpf 4.1 KB | 34.0 | 6.8 | persist-dbpf's lot lanes, or original calls | plain + catalogue fakes | medium | Sonnet medium |

About 1,057 KB in 25 lanes (the table's KB columns; the figures for each class include its unnamed rows, by owner: a
lane checks the owner first). The cTSUserTool* and the lot dialogs and panels are ui-game's lanes now: `tools/subsystems.py` files them there, with
50 KB unnamed. cRZIniFile is framework-gz's.

**First 8 lanes, in order:**
1. WL-0's rest (Sonnet)
2. WL-1 (Opus, fixture)
3. WL-2 (Opus)
4. WL-3 (Sonnet)
5. WL-4 (Sonnet)
6. WL-B1 (Sonnet)
7. WL-B2 (Sonnet)
8. WL-R0 (Opus)

WL-0 and the WL-2 / WL-3 pair can run in parallel with WL-B1 and WL-B2 once WL-1 is merged.

(Sections 5, Milestone O's blockers, and 7, Milestone L, were absorbed on 2026-11-03: L's definition and acceptance are in `docs/roadmap.md` (L), O's open rows in the owning campaigns' docs, the old text in `docs/archive/world-lot-milestones-O-L.md`. The numbering is kept because other sections cite 6.)

## 6. Shared test boundaries

What exists:
- `tests/sgfx.h`: the node world and device world.
- `tests/sg_geometry_node_device_fx.h`: devfx, the recording Direct3D-side fakes for a renderable.
- `tests/sg_geometry_node.cpp`: cGeometryNode's harness.
- `tests/world_db.cpp`: our standalone cWorldDB, driven slot by slot.
- `tests/ts_services_fixture.h`: cached service cells holding recording fakes (used by `tests/sg_tssg_system_lot.cpp`,
  whose 12 lot managers are fakes).
- `tests/world_ts_world.cpp`: the TSWorld small-slot tests.

None of these builds a lot.

**Missing: the lotfx fixture** (WL-1's deliverable; `tests/lotfx.h`). It is the analogue of sgfx, and it would serve
almost every lane: WL-1 to WL-4S, WL-B*, WL-R*, WL-F*, WL-W*, WL-FL*, WL-T and WL-NT.

- **World half.** Our cWorldDB, built and sized from the seed: SetBounds, SetLevels, floor patterns and rotations,
  elevations, visibility, awning and deck grids.
  - It is planted in the cell the managers read (the world database service).
  - Optionally: our cWallGraph and cPartitionManager once WL-2 is done; recording fakes until then.
  - The same planted lot serves both sides, so a test compares the grid state and the resulting geometry.
- **Node half.** sgfx's node world, plus devfx for the render slots.
  - A geometry node is built through its factory or ctor on both sides (hook shape) and driven through its vtables.
  - The two sub-shapes (+0, +4 with GeomPart) get helper accessors.
- **Fakes**, each a recording drawfx::Fake (strict argument kinds), with ids from the seed:
  - message server (cIGZMessageServer2) for the managers' cIGZMessageTarget2 face;
  - scene manager / cTSSGSystem cells (from ts_services_fixture);
  - cLightingManager, cLightMapManager, cShadowManager, cOverlayNode / cLotSkirt (nTSSG clusters not done);
  - the cTSProduct catalogue: wall, fence, roof pattern, deck foundation and terrain paint lookups return planted
    product records;
  - cTSPatternBase;
  - the resource manager (texture and material keys).
- **Coverage gap** (rewritten by XN). The reference set has closed part of it: a Sim walks in it, so routes ran 47 of 59 KB,
  cTSRoutingObjectAggregate 35 of 51, the path planner 57 of 65, pools 21 of 47, awning and rug 22 of 42. Still thin or
  missing, for the in-game check only:
  - a lot save, the missing lot-resave run, and build mode (nothing in the set);
  - other roof shapes (roof shapes 9.1 of 72 KB, roof snow 2.3 of 45.5);
  - snow (terrain snow 2.7 of 36: lot 50s didn't reach it);
  - the lot catalogue (cTSLotCatalog ran 305 B of 27 KB);
  - cAppendWallLocator, 13.5 KB, 0 ran.
  This is coordinator work: it runs the game.

Also needed, smaller:
- a planted obstacle and portal grid for the path planner (WL-P1), which needs no device;
- a neighbourhood terrain planting for cNHoodTerrain (WL-NT): terrain heightfield, paint alpha and the cTSNHoodTerrain
  ini.

## c3DArray

`nTSWorld::c3DArray<T> : cIGZSerializable` (lane VO): src/world/c3DArray.{h,cpp}, tests/world_c3darray.cpp, status group
c3DArray (37 entries), hooks src/hook/tables/c3DArray.cpp.

- **Layout, all ten copies:** vtable +0, refcount +4, sizes x/y/z at +8/+0xc/+0x10, `std::vector<T>` +0x14 (0x20
  bytes). Class id 0x2a51171b and version 1 for every copy; record name "c3DArray". Elements are stored level by
  level (index `(sizeX * z + x) * sizeY + y`, `At()`).
- **Ten copies, not eight:** besides cWorldDB's eight grids, cWallGraph owns c3DArray\<cWallTile\> and cRoomManager
  c3DArray\<cRoomIDTile\>; cWorldOutsideSupport embeds c3DArray\<E\> (bytes) at +0xc and destroys it through
  0x81d2f0.

  | copy (element) | vtable | Write / Read / slot 6 / Resize | element on the stream |
  |---|---|---|---|
  | cFloorPatternStorageTile | 0xebb740 | 0xd14160 / 0xd11a70 / 0xd0bf50 / 0xd12260 | 4 x uint16 |
  | cFloorPatternRotationTile | 0xebb760 | 0xd14040 / 0xd11930 / 0xd0bee0 / - | uint16 |
  | float | 0xebb780 | 0xd13b30 / 0xd113e0 / 0xd0bd60 / - | float32 |
  | cFloorVisibilityTile | 0xebb7a0 | 0xd142a0 / 0xd11ba0 / 0xd0bfc0 / 0xd122e0 | 4 x uint8, read as `!= 0` |
  | E (byte) | 0xe05250 | 0x81e6e0 / 0x81df60 / 0x81d3d0 / 0x81e070 | uint8 |
  | std::vector\<long\> | 0xebb7c0 | 0xd13c60 / 0xd11520 / 0xd0bdd0 / - | sint32 count, then the ids |
  | cDeckFoundationMarkerTile | 0xebb7e0 | 0xd13f00 / 0xd117d0 / 0xd0be70 / - | 4 x uint32 |
  | cAwningTile | 0xebb800 | 0xd13dc0 / 0xd116a0 / 0xd0be00 / 0xd121e0 | 2 x uint32 |
  | cWallGraph::cWallTile | 0xe048b0 | 0x8099b0 / 0x806cd0 / 0x805910 / 0x8072b0 | uint8 (ORed in) |
  | cRoomManager::cRoomIDTile | 0xe0495c | 0x80c490 / 0x80bc90 / 0x80a700 / - | 4 x sint16 |

  "-": that copy's Resize is inlined into its callers (no body of its own).
- **Shared slots:** QueryInterface 0x178b20, AddRef 0x148fb0 and Release 0x4b1d00 are folded across unrelated classes
  (overrides only, no entries). GetGZCLSID shared_vf_806aa0 is referenced only by the ten copies' slot 5, so it is
  family-only and hooked (CRC by hookcrc).
- **Per-copy quirks** (reproduced, see the comments in c3DArray.cpp):
  - Read's locals the original never initialises: the class id (the byte copy reads it into the stream argument's
    slot), the visibility flag byte (persists across tiles; starts as the stream address >> 24) and the objects'
    id count (persists; starts as the stream address). A failed read leaves them, so the tests run every script with
    reads failing at each position.
  - The wall copy ORs each byte read into its tile (`// ORIGINAL-BUG:`): reading into a grid that wasn't freshly
    resized keeps old bits.
- **Fixture for WL-1 and later lanes:** tests/world_grid_fx.h (`gridfx`): slot-tagged cIGZIStream / cIGZOStream fakes
  (fail injection, keep-on-fail mode), per-copy traits `Copy<T>` (RVAs, values), `NewGrid<T>(vtable)` (game or our
  vtable), `Plant(grid, seed, x, y, z, ...)` to fill a grid deterministically, `Record<T>` for a stream script and
  `Capture` to compare grids word by word. tests/world_db.cpp compares our cWorldDB's grids (ours) with the original's
  (the game's) element by element, the vtable word normalised. The objects copy's first id count (the stream address)
  is tested with the input stub placed at 0x40000000 or above (nothing lower is free in the harness): the resize then
  throws length_error on every side, where a count starting at 0 would not.

## cVertexLayer and cFencePostLayer

`nTSWorld::cVertexLayer` and `nTSWorld::cFencePostLayer`, both `cVertexMapLayer<Value> : cIGZSerializable` in our source
(lane VO): src/world/cVertexLayer.{h,cpp}, tests/world_vertex_layer.cpp, status group cVertexLayer (22 entries), hooks
src/hook/tables/cVertexLayer.cpp.

- **Key:** a lot vertex `{float x, float y, int32 level}`, ordered by level (signed), then x, then y (`x == x'` picks
  the y compare, else `x < x'`; a NaN coordinate makes keys equivalent, tested alike on both sides).
- **Shared layout:** vtable +0, refcount +4, `std::map<LayerVertex, 32-bit value>` +8. MSVC's tree as in our
  toolchain (head pointer and size; 0x20-byte nodes, the pair at +0x10, colour +0xc, isnil +0xd).
- **cVertexLayer** (vtable 0xebb834, 0x18 bytes, class id 0xcb4387a1): cWorldDB's object index (+0xc4). +8 vertex -> id,
  +0x10 id -> vertex. Add 0xd147a0 removes the id's old vertex first and keeps a vertex's first id (the new id is then
  left without a vertex). Remove 0xd14b70 erases only a matching pair. Read empties only the vertex map
  (`// ORIGINAL-BUG:` stale id entries survive a reload).
- **cFencePostLayer** (vtable 0xe053ac, 0x10 bytes, class id 0xab4ba572): cPartitionManager's fence posts. Set
  0x8205b0 inserts only (true when new), Remove 0x8209f0, slot 7 Clear 0x820710.
- **Shared bodies** on the +8 map of either layer: Contains 0x8207a0, ValueOf 0x820810 (a missing vertex answers the
  head node's never-written value field: `// ORIGINAL-BUG:`), Vertices 0x820730. Vertices' list is
  `std::vector<LayerVertex, GZServiceAllocator>` (the allocator service, not the CRT heap), so the test runs under
  `svcfx::AllocatorScope`.
- **Record:** u32 class id (own slot 5), u32 version 1, GZStr name ("cVertexLayer" / "cFencePostLayer"), s32 count,
  then per entry s32 level, f32 x, f32 y and the value (s32 id / u32). Read's entry locals persist across entries
  (keep-on-fail mode tests them once the header and first entry are whole).
- **Test pattern** (reusable for the other map-backed pieces): plant the maps with `std::map`'s own inserts in the same
  order on each side (identical trees), plant the head node's value word, and dump each map as a tree (size, head flags,
  preorder colour/links/payload, extreme links) after every call of a random member sequence.
- **cWorldDB's use (lane VP):** Init builds `new cVertexLayer`; slots 26 / 27 (`SetObjectVertex` /
  `RemoveObjectVertex`, void) and 68 (`ObjectAtVertex`) call Add / Remove / Contains / ValueOf directly.
  tests/world_db.cpp has no recorders at those RVAs any more: case 16 passes real vertices and dumps both index maps as
  trees, the index's vtable word normalised.

## cWorldDB (WL-0b notes)

- **StoreVTable 0xd0db20** (slot 42): stores 0xebb730 through its argument and returns the argument (the original's
  `mov eax,[esp+4]` leaves it in eax; the guard's arity warning). Declared `void *StoreVTable(void *)`, tested.
- **Left:** Load 0xd0fb30 (4.6 KB) and Create 0xd0ef80 (1.7 KB) still run the original by RVA from ours. Thunks and the
  residue not looked at (lane VP ran out of room). The pattern table is our cStringMap since lane VQ (WL-0c).

## c2DArray\<tVertPaintInfo\>

cWorldTerrain's vertex paint (lane VP): the generic c2DArray (src/ts/c2DArray.{h,cpp}, status group c2DArray) with
`tVertPaintInfo {u8 paint[3] = ff ff ff; u8 flag = 1}`; vtable 0xe0464c, class id as the other copies.

- **Write 0x804d40:** header as the other copies (clsid via own slot 5, u32 version, GZStr "c2DArray"), but width and
  height go out **signed**; per element the count of paint bytes before the first 0xff (of 3), u8, then those bytes.
- **Read 0x803340:** clsid / version checked, name ignored, Resize to the record's sizes, then per element a u8 count and
  that many bytes; the rest 0xff, the flag 1. If the terrain record version being read (the global 0x10df504, stored by
  cWorldTerrain::Read 0x8034d0) is < 4, each byte is read as a u16 (low byte kept). The count and the u16 locals live
  across elements. `// ORIGINAL-BUG:` a count over 3 is not clamped: the bytes run into the next elements (and past the
  array after the last).
- **Resize 0x8040b0** fills with the default {ff ff ff, 1}; **vf6 0x801730** / body **0x801440** as the other copies.
- **Slot 5 0x4af570** (`shared_vf`, all three c2DArray copies only) hooked under the family rule.
- Test: tests/world_c2darray_paint.cpp (terrain versions 3 / 4 / 0 / 7, overflowing counts, modes A and B, throws at
  every call). tests/ts_c2darray.cpp's fail sweeps used to stop at fail 0 (the total was overwritten); fixed.

## cSerializableMap

`nTSWorld::cSerializableMap<K, V, Less, cProxyManager>` (lane VP): src/world/cSerializableMap.{h,cpp}, tests
tests/world_serializable_map.cpp, status group cSerializableMap, hooks src/hook/tables/cSerializableMap.cpp. Both
copies are cProxyManager's, built inline in its vf3 0x81adc0 with the game's vtables (so the struct keeps the layout).

- **Layout (0x18):** cRZUnknown +0 (vtable, count), cIGZSerializable face +8, `std::map` +0xc, a u32 at +0x14 (primary
  slots 6 / 7, the shared 0x20daf0 / 0x149d00, set / get it; the record carries it). Bases are declared map,
  cRZUnknown, cIGZSerializable: the destructor stores both tables, runs ~cRZUnknown 0x1b9096, then frees the tree.
- **Record:** u32 class id 0xace46235 (through the face's slot 5), u32 version 1, u32 the +0x14 word, s32 count, then
  key / value per entry in key order. Read checks id and version (false otherwise), reads the word straight into the
  object, then inserts each pair whose key is new (lower bound, then the node): **the map is not cleared** and an
  existing key keeps its value. The key / value locals live across entries.
- **GUID copy** (vtables 0xe051a8 / 0xe051cc; u32 keys and values, unsigned order, 0x18-byte nodes): dtor 0x81ac60,
  Write 0x81d0f0, Read 0x81bc20, all **verified**.
- **Shared** (both copies only): QueryInterface 0x81bb90 (null out fails; 0xe4fda3d4 answers this + 8, or 0 for a null
  this, and AddRefs through slot 1; else 0x1b945c), the face thunk 0x81bbcf, the face's GetGZCLSID 0x81adb0: verified,
  hooked under the family rule.
- **String copy** (lane VQ; vtables 0xe05168 / 0xe0518c; cRZString keys and values at node +0x10 / +0x30, 0x50-byte
  nodes, order `_stricmp` of the two strings' ToChar (slot 5) < 0): dtor 0x81aca0 (tree erase 0x2567d0), Write 0x81d190
  (two GZStr per entry), Read 0x81bd90 (two fresh local cRZStrings per entry) and its out-of-line insert 0x81a8a0
  (pair\<iterator, bool\> through a hidden return pointer; lower bound 0x2568e0; called only by that Read), all
  **verified** (tests/world_serializable_map.cpp, templated over both copies; 10 mutants killed). Read's count is read
  into the stream argument's own stack slot: in mode B a failed count read leaves the stream's address as the count
  (tested with throws a few entries in).

## cStringMap\<G\>

`nTSWorld::cStringMap<unsigned short>`, the floor pattern table (lane VQ): src/world/cStringMap.{h,cpp}, tests
tests/world_string_map.cpp, status group cStringMap, hooks src/hook/tables/cStringMap.cpp. cWorldDB's `PatternTable` is
now an alias of it (Init builds ours with `new`; UsePatternName / PatternName / PatternId call slots 9 / 8 / 7 by name).

- **Users:** the ctor 0x813b30 is called by cPartitionManager vf3 0x815ae0, cWorldDB Init 0xd0e6b0 (ours) and cWorldDB
  Load 0xd0fb30; the vtable 0xe050a8 is stored only by the ctor and dtor, so the ctor hook makes every table ours.
- **Layout (0x20):** vtable, refcount +4, table word +8 (the key the pattern catalog files names under; the ctor leaves
  it, cWorldDB::Init sets 1), byte +0xc (ctor 1, cWorldDB::Init 0: whether id 0 can be handed out), map A
  name -> {u16 id, u32 use count} +0x10 (0x1c nodes), map B id -> name +0x18 (0x18 nodes). Names are the catalog's
  strings (pointers, never copied), ordered null first then `_stricmp`; map A never holds a null name (the original's
  find helper builds std::strings from the keys and crashes on one).
- **Slots:** 0-2 folded (QI shared_vf_178b20 riid 1 only, AddRef 0x148fb0, Release 0x4b1d00; ours only in our vtable),
  5 GetGZCLSID 0x815980 (0xcac4fc40), 6 deleting dtor 0x8141f0 (map B, then A), 7 IdOf 0x819d20 (null or absent:
  0xffff), 8 NameOf 0x816050 (id by address; null for none), 9 NextId 0x816680: starts at `byte == 0 ? 1 : 0` and walks
  map B in order while id <= candidate, one up per id (`// ORIGINAL-BUG:` ids below the start count too, so with the
  byte clear the set {0, 1} gives 3).
- **Record:** u32 class id (own slot 5), u32 version 1, GZStr "cStringMap", s32 map A size, then per entry GZStr name,
  u16 id, u32 use count. Read checks id and version, ignores the name, clears A then B, takes the catalog (cached app
  singleton slot 16), then per entry: catalog slot 42 (table word, text, 0); a hit's slot 7 is the key; a miss makes a
  catalog entry through the director framework's GetClassObject(0x2ac90e94, 0xe9da450e) (virtual base 2: slot 6(1),
  slot 1(0); slot 0(word ? 1 : 2); slot 1(text); catalog slot 10(word, entry); key = slot 7; release through virtual
  base 1, vbtable at entry +4). Then `A[key] = {id, count}`, `B[id] = key`. The u16 / u32 locals live across entries.
- **All 8 hooked functions verified** (difftests with svcfx fakes for the singleton, catalog and framework and a test
  entry object with a vbtable). **Left:** the helpers 0x8135e0 / 0x8138a0 / 0x814130 / 0x814080 (map find / insert /
  erase bodies shared with cPartitionManager and Load); they run inside the originals only, nothing of ours calls them.

## cWallLayer

`nTSWorld::cWallLayer` (lane VQ): src/world/cWallLayer.{h,cpp}, tests tests/world_wall_layer.cpp, status group
cWallLayer, hooks src/hook/tables/cWallLayer.cpp.

- **Users:** cPartitionManager holds three (its vf3 0x815ae0 constructs them; ctor 0x81ff50 is its only caller). The
  vtable 0xe0536c is stored only by the ctor and the destructor body 0x81ffa0, so the ctor hook makes every layer ours.
  The members are called from cPartitionManager only: Clear 0x8200b0 (vf10, vf13), Erase 0x820240 (vf15, vf17, vf21,
  vf87), SetHalf 0x820290 (vf20, vf36, vf37, 0x814d60), Has 0x820300 (vf41), Half 0x820340 (vf51, vf52), First 0x8203a0
  (vf17, vf50, vf74, 0x816460), the setter 0x820080 (vf16, vf20, 0x814d60).
- **Layout (0x10):** vtable, refcount +4, `std::map<int32_t, {u32 first; u32 second}>` +8 (node 0x1c: key +0x10, first
  +0x14, second +0x18). The second word is used as two u16 halves: index 0 the high one (node +0x1a), index 1 the low one.
- **Slots:** 0 QI 0x8200d0 (riid 1 or the class id, non-null out; AddRef through slot 1), 1-2 folded (0x148fb0,
  0x4b1d00), 3 Write 0x8203f0, 4 Read 0x820100, 5 GetGZCLSID 0x8200c0 (0x8a84d7b0), 6 deleting dtor 0x820060 (0x10).
- **Members:** Has / First / Half answer 0 for a missing key (Half also for an index other than 0 / 1); SetHalf
  changes an existing entry only; Erase removes the key's entry if any.
- **Record:** u32 class id (own slot 5), u32 version 1 (read as u32), GZStr "cWallLayer", s32 count, per entry in key
  order s32 key, u32 first, u16 high half, u16 low half. Read checks id then version, reads the name (ignored) and the
  count, clears the map, then `map[key] = {first, high << 16 | low}` per entry; the four entry locals live across
  entries, and the high half is read into the stream argument's own stack slot (before any read it holds the stream
  pointer's low half).
- **13 functions verified** (difftests on the members, both destructors, QI with a test table whose AddRef differs, and
  Write / Read with gridfx streams). **Left:** the setter 0x820080 (39 bytes) does `map[key] = {value[0], <an
  uninitialised stack word>}`: the second word is whatever its caller left on the stack (`// ORIGINAL-BUG` candidate;
  not deterministic, so not reproducible or testable as is: a decision for cPartitionManager's lane, WL-2). The map's
  operator[] 0x81ffb0 is library code (std::map), called by Read and the setter in the original only.

## Wall graph: cWallVertex, cWallSection, cWallEdge (WL-0d, lane VR: done)

Three real classes on interfaces (standalone shape), in src/world/cWallEdge.{h,cpp}, cWallSection.{h,cpp} and
cWallVertex.{h,cpp}, hook tables beside them, tests world_wall_edge.cpp, world_wall_section.cpp, world_wall_vertex.cpp.
cWallGraph (WL-2) builds them. The loose helpers that used to sit in the TSWorld group (the edge slots, WallSectionEntry,
WallVertexCopyFrom) are folded into the classes.

- **cWallEdge** (vtable 0xe053f4, 38 slots, 0x18 bytes; 27 functions verified) is a quad-edge record entry
  (Guibas-Stolfi): +4 a flag byte, +8 its index 0-3 in its section, +0xc Onext, +0x10 the vertex, +0x14 the owner. Slots
  6/7 Rot, 8/9 InvRot, 10/11 Sym, 12/13 Onext, 14-27 the compositions (15 = Rot.Onext.Rot = Oprev); 30 and 32 the edge's
  angle (through the vertex's slot 5); 37 0x820c20 deleting destructor (0x18), body 0x820c10. Tests plant 16 edges over
  fake vertices by hand.
- **cWallSection** (vtable 0xe05300, 5 slots, 0x68 bytes; 4 verified): id +4, four cWallEdge at +8; the constructor
  0x81fc70 gives each its owner and index and links Onext 0->0, 1->3, 2->2, 3->1; EdgeAt (slots 1 and 2) is this + 8 +
  24 * index.
- **cWallVertex** (vtable 0xe0532c, 10 slots, 0x1c bytes; 8 verified): id +4, position +8 (-1, -1, 0 by default), first
  edge +0x14, edge count +0x18. AddEdge 0x81fe40 keeps the first unless its angle is greater than the new edge's (the
  original's x87 compare makes a NaN keep the first; the NaN rigs caught the SSE-style `<=`); RemoveEdge
  0x81fef0; EdgeForAngle 0x81fe90 (the original dereferences the first edge, so an empty vertex is never asked);
  SetPosition 0x81ff30 reads src+8 after writing this+8, so an overlapping source sees the write.
- **Shared one-word slots stay unhooked** (bodies shared with unrelated classes, under 12 bytes), ours only in our
  vtables: cWallEdge 0x20daf0, 0x14a260, 0x1eeb80, 0x149c60, 0x145770, 0x149d00, 0x46390, 0x1458b0, 0x149cf0;
  cWallSection 0x1489e0, 0x147a30; cWallVertex 0x1489e0, 0x147a30, 0x142840, 0x148ba0, 0x149d00.
- **Open:** two cWallEdge bodies 0x820be0 and 0x820c10 are still named FUN_ (a rename needs an annotate --apply facts
  file). No ORIGINAL-BUG found in the three classes.

## Left (from the backlog, 2026-11-03)

- cWorldDB (JC): Load 0xd0fb30 (4.6 KB) and Create 0xd0ef80 (1.7 KB) still game code (forwarded from our world vtable slots 9/11); once ours, drop the five helper hooks (0xd0e1d0, 0xd127b0, 0xd13230, 0xd13290, 0xd138d0). Other original_calls: object index 0xd146c0/0xd147a0/0xd14b70/0x8207a0/0x820810, pattern table ctor 0x813b30, derived rebuild 0xd17090, level shifter 0xd0d040, 0x7dd9d0, slot 12 helpers 0x81e800/0x81ea20/0x801530, static tables 0xd15080.
