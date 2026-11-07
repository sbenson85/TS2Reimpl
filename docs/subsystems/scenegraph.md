# The scenegraph: nGZSceneGraph and nTSSG (subsystem notes)

Campaign doc for the scenegraph: the nGZSceneGraph library (nodes, shapes, geometry, materials, the display list) and
the game's nTSSG layer over it (lighting, shadows, lot geometry, render strategies, the system and the camera). Planned by
lane NX (2026-10-26) from run-1023. It takes over lanes S1-S10, T0-T13 and C1-C2 of `rendering.md`'s plan; the device
layer, 2D, fonts and effects stay in `rendering.md`. Facts are RVAs and names; decompiler output stays in `local/`.

How this doc is laid out (lane VL, 2026-11-03): front matter, then the rules every scene-graph lane uses (conventions,
the sgfx fixture, pitfalls), then one `## ` section per class or class family (layout, vtables, quirks and
`ORIGINAL-BUG` sites, what stays the image's, files, test pitfalls; each lane's result kept under its own `###` heading
with the lane's tag), then milestones N and O, and **Open**. `tools/whereis.py <class>` lands on the class's section.
Elsewhere:
- nGZCompGeom and nGZNumerics (the G0 / G0b work, lanes NY, PC-PV, TR, TS): `docs/subsystems/compgeom.md`.
- The lane-by-lane history this doc used to carry (plans, superseded lane maps, gate counts per lane):
  `docs/archive/scenegraph-lane-history.md`, verbatim.
- Milestone O's assessment (lanes VJ-VX): `docs/archive/scenegraph-O-assessment.md`; its open rows are under **Open**.

## Why this subsystem

- Size: 842,042 B executed and not done by function subsystem (844,892 B by whole groups: 3,823 functions in 420
  groups) of the 3,031,707 B not done in run-1023. It is the largest open subsystem: about 28% of what is left.
- Every lot frame runs it: the scene tree, the display list, materials, lighting and the camera. The standalone exe
  cannot draw a lot without it.
- It is the base for four other campaigns: world-lot (the cIGeometryNode subclasses), animation (nTSSG::cObject,
  cAnimatable, cPerson are scene nodes), effects (cTSParticlesRenderable, cScreenShapeRef) and UI (cTSWinSceneGraph).
  Its node templates and resource base are fixed once here and reused there.

## Scope

Families of the 845 KB (whole groups, run-1023, `coverage.py queue --subsystem scenegraph`; family = class prefix):

| family | KB | main groups (KB not done) |
|---|---:|---|
| nTSSG lighting | 202 | cLightingManager 94.4 (76.8 owner-inferred), cDiffusionManager 49.3 (45.5 inferred), cLightMapManager 16.3, cShadow 15.6, cShadowManager 13.2, cLightSettings 3.9, cLightRigManager 3.6, cDiffusionLightExtension 1.4, cAmbientFromLightMapLight 1.3, cLightingParserState 1.0, cHeightMapShadowLight 0.9 |
| lot geometry | 103 | cLotSkirt 25.6, cLotImposterDisplay 21.0, cSeasonManager 9.6, cDiffuseCompositedTextureUpdateStrategy 9.6, cMergedGeometryNode 8.6, cLotImposter 7.1, cGableRoof 3.5, the other gables ~5, cOverlayNode 2.9, cCompositedTextureUpdateStrategy 2.1, cWaterGeometryBuilder 1.9, cLotImposterManager 1.9, cBlurBrush 1.5 |
| materials | 102 | cMaterialManager 16.6, cApplyShaderProgramConstants 12.5, cMaterialDefinition 10.1, cAttributeFFTextureMatrix 9.5, cMaterialParser 9.1, cShaderInit 5.9, cShader 5.8, cImageData 5.0, cMaterialAttributes 4.3, cTextureManager 3.5, cProceduralTexture 3.2, cRenderTargetTexture 3.0, cTextureImage 2.3, cAnimatedTextureMatrix 1.5, cShaderComponentT (21 copies) and cShaderControllerT (19 copies), ~0.2 KB each |
| TSSG system | 93 | cDisplayListDynamicRenderStrategy 21.8, cTSSGSystem 17.1, cVideoBroadcastManager 15.3, cTSWinSceneGraph 10.8, cSceneManager 7.4, cDisplayListCensorRenderStrategy 5.0, cObjectModule 2.6, render surfaces ~2.4, cQueryStrategyManager 1.7, cAnimRequest 1.5, c3DUIManager 1.5, cConfigParserState 1.3, cCustomChoreoQueue 1.0, cTSUIThumbnailFactoryObject 0.9 |
| camera | 71 | cTSCameraSmoothSims1Controller 21.5, cTSCameraControllerManager 9.2, cTSCameraSmoothFirstPersonController 6.5, cCameraLive 5.4, cTSCameraEffect 5.3, cCameraEffectsSupport 4.2, cCameraNeighborhood 4.0, cCameraCreateASimController 3.1, cCameraCameraManController 2.9, cTSConfigTargets 1.9, cTSCameraParserState 1.7, cCameraFirstPersonController 1.6, cTSCameraGenericController 1.2 |
| geometry | 67 | cGeometryNode 24.5 (18.3 inferred), cIndexedMeshBuilder 21.2 (18.5 inferred), cBoundingVolumeBuilder 5.3, cSkinExtension 3.5, cCompactorBuilder 2.9, cLODLevelsStore 2.3, cDynamicGeometryRect 1.9, cBoneDataExtension 1.4, cTangentSpaceBuilder 0.5, cLODLevelsTable 0.5, cFindBonesTraversal 0.4 |
| geometry data | 43 | cGeometryDataContainer 24.9 (5% done), reader-owned helpers 7.8 (~cGZPersistBinaryReader: 0xab23d0, 0xab2da0, 0xa16630, ...), cDataListExtension 4.0, cStringPool 3.2, cDataListItem 2.6, cExtension 0.8 |
| viewers and SG lights | 43 | cViewerRefNodeT 6.3, cViewerRefNodeRecursiveT 4.4, cViewerRefNodeBaseT 3.6, cLightRefNodeT 3.5, cCompositionTreeNodeT<cIViewerRefNodeRecursive> 2.8, cSpotLight 2.5, cPointLight 2.2, cStandardLightBase 1.2, cEnvironmentCubeLight 0.7, cViewer_LEGACY 0.6 |
| shape | 42 | cShape 22.0, cShapeRefNodeT 15.3 (14.5 inferred) |
| nodes | 42 | cLooseOctreePartitionNode 7.7, cSceneNode 4.4, pre- and postorder iterators 4.5, cSceneGraphSystem 2.5, cResourceNode 2.2, cSceneGraphFactory 1.3, cIObjectV template copies ~4, cAbstractBehaviorTypeT instantiations 0.2-0.9 each, cSGResource 0.5 |
| display list | 34 | cDisplayList 28.8 (19.9 inferred), cApplyFFLightStates 1.9, cRenderGroupManager 1.6, cValidateRenderShaderContext 1.6, cFrameCache 0.3 |

Out of scope: effects (cTSParticles*, cTSVisualEffect, cTSEffectsManager: `rendering.md` E1-E3), the world-lot
geometry nodes (floor, wall, terrain, pool, fence, roof, rug, snow, awning, flora: the world-lot campaign), the
animation nodes (nTSSG::cObject 41 KB group, cAnimatable, cPerson: animation). Since lane XN (2026-11-06) the filing agrees:
nTSSG::cObject, cAnimRequest, cCustomChoreoQueue, cCustomAnimTimer2 and cSharedChannelFunction are animation's, so drop them from the TSSG
system row and the later clusters; their plan and the order against this campaign are in `docs/subsystems/animation.md`.

## Campaign status (2026-11-03)

- **nGZSceneGraph: implementation-complete.** Every class of the scene-graph cluster is ours in the standalone shape
  (our vtables, factories hooked as the switch, bodies hooked or in effect through callers). What is left there is
  listed under **Open**: bodies shared with classes outside the cluster, template copies of classes that are not ours
  yet, and a few named bodies.
- **Milestone N** (the image-free GMDC decoder) is done (2026-10-30).
- **Milestone O** (one object rendered image-free) is not: (a) partial, (b) fails, (c) blocked, (d) not run. After
  lanes VJ-VX the image-free gate has 7 references, each with an owner (**Open**; assessment archived in
  `docs/archive/scenegraph-O-assessment.md`).
- **nTSSG** (lighting, lot geometry, the TSSG system, render strategies, the camera) is the next set of clusters
  ("Later clusters" below); only the parts listed under Scope's done items are ours.

## Subsystem map (lane NX, 2026-10-26)

**Layers**, bottom up:
1. Node templates (nGZSceneGraph). cAbstractBehaviorTypeT<I, nGZEvent::cRefSingleThreaded> (13 instantiations) ->
   cObjectGraphNodeT<I, Impl> (20) -> cCompositionTreeNodeT (12) -> cTransformNodeT (10) -> cBoundedNodeT (9) ->
   {cPartitionNodeT (2: cSceneNode, cLooseOctreePartitionNode); cRenderableNodeT (5) -> cShapeRefNodeT,
   cLightRefNodeT, cViewerRefNodeBaseT -> cViewerRefNodeT / cViewerRefNodeRecursiveT; cExplicitBoundNodeT}, and
   cReferentNodeT (6: cShape and the lights). The second parameter is a data class: cObjectGraphNodeImpl,
   cCompositionTreeNodeImpl, cTransformNodeImpl, cBoundedNodeImpl, cPartitionNodeImpl, cRenderableNodeImpl,
   cShapeRefNodeImpl, cLightRefNodeImpl, cViewerRefNodeImpl, cViewerRefNodeRecursiveImpl, cExplicitBoundNodeImpl,
   cReferentNodeImpl. The Impl member functions are unnamed in db. Template slots are folded and forward to the Impl
   at +0xc with the outer object (shared_vf_3a6340: `this+0xc`, call 0xa34900).
2. Resources. cSGResource (virtual bases: cISGResource, cIGZPersistResource2; layout in `src/gz/cSGResource.h`:
   vtable 0xe695a4 at +0, vbptr +0x1c, helper +0x20, resource +0x24, flags +0x28/+0x29, vtordisp +0x2c, secondary
   vtable 0xe695dc (25 slots) at +0x30). Every resource class derives from it (below), with its cSGResource part at
   the last vtable offset.
3. Data and geometry: cGeometryDataContainer, the builders, cGeometryNode, extensions.
4. Materials and textures: cMaterialManager, cMaterialParser, cMaterialDefinition, cShader, cTextureManager.
5. Shape and display list: cShape, cShapeRefNodeT, cDisplayList and its helpers. The display list calls the device
   layer (ours: cDeviceDX, cDeviceStateDX, the resource managers) through nGZGraphic4 interfaces.
6. nTSSG: render strategies, lighting, shadows, lot geometry, cTSSGSystem, cSceneManager, the camera.

**Classes deriving from cSGResource** (`db/classes.jsonl`, vtable offsets): cShape [0,44,52,152], cGeometryDataContainer
[0,44,128], cMaterialDefinition [0,44,84], cImageData [0,4,108], cLevelInfo [0,80], cResourceNode [0,8,40,96],
cSceneNode [0,8,108,232], cGeometryNode [0,8,20,64,304], cViewer_LEGACY [0,44,52,272], the lights (cStandardLightBase,
cAmbientLight, cDirectionalLight, cPointLight, cSpotLight, cEnvironmentCubeLight, cLinearFogLight, cDrawStateLight, six
cLightT instantiations, the nTSSG lights), and outside the campaign the 17 world-lot geometry nodes,
cTSTextureRecipeImageData and three nGZSceneDBAnim resources.

**Creators and switch points.** Almost every class is created by a COM director factory, `X *Create()` doing
`operator new(size)` (0x824632, or a class pool) and the constructor. The factories are stored by address in the
directors' AddCls steps (`src/rz/COMDirectorData.cpp`): nGZSceneGraph::cDllCOMDirector 0xa33bf0 (steps 635-708, 73
classes), nTSSG::cDllCOMDirector 0x244e30 (steps 64-145, ~76 classes). Hooking the factory with `return new X` is the
switch (`docs/standalone-shape.md`, "Factories registered by address").

nGZSceneGraph (vtables as rva+offset:slots; size; factory -> constructor):

| class | vtables | size | factory -> ctor |
|---|---|---|---|
| cCompositionTreeNode | 0xe68b2c:62 | 0x28 | 0xa37910 -> 0xa36900 |
| cTransformNode | 0xe692ac:67 | 0x48 | 0xa3a6c0 -> 0xa3a000 |
| cExplicitBoundNode | 0xe6c2a4:80 | 0x70 | 0xa4bc30 -> 0xa4b500 |
| cSceneGraphSystem | 0xe6c474+0:15, 0xe6c4b4+4:13, 0xe6c4ec+8:6 | 0x40 | 0xa4cb40 -> 0xa4c6f0 |
| cSceneNode | 0xe6d0b4+0:98, 0xe6d240+8:7, 0xe6d260+108:13, 0xe6d298+232:25 | 0xec | 0xa4e9b0 -> 0xa4ddf0 |
| cResourceNode | 0xe6d698+0:66, 0xe6d7a4+8:7, 0xe6d7c4+40:13, 0xe6d7fc+96:25 | 0x64 | 0xa503a0 -> 0xa4fd10 |
| cLooseOctreePartitionNode | 0xe6e1b4+0:90, 0xe6e320+8:7 | 0xec | 0xa51ab0 -> 0xa50ef0 |
| cShapeRefNode | 0xe6ef70+0:101, 0xe6f108+8:7 | 0xd0 | 0xa55270 -> 0xa541e0 (base cShapeRefNodeT ctor 0xa54070, vtables 0xe6edb8:101, 0xe6ef50) |
| cShape | 0xe6f514+0:13, 0xe6f54c+44:62, 0xe6f648+52:7, 0xe6f668+152:25 | 0x9c | 0xa5d3b0 -> 0xa5bd40 |
| cLightRefNode | 0xe70334:99 | 0x90 | 0xa61a80 -> 0xa60c00 |
| cViewerRefNode | 0xe71b10:147 | 0x110 | 0xa65fb0 -> 0xa644a0 |
| cViewer_LEGACY | | 0x114 | 0xa66010 -> 0xa64520 |
| cViewerRefNodeRecursive | 0xe72eac:126 | 0x10c | 0xa6ac70 -> 0xa69f50 |
| cGeometryDataContainer | 0xe731ec+0:13, 0xe73224+44:59, 0xe73314+128:25 | 0x84 | 0xa70aa0 -> 0xa6ed50 |
| cGeometryNode | 0xe6987c+0:51, 0xe6994c+8:7, 0xe6996c+20:13, 0xe699a4+64:9, 0xe699cc+304:25 | 0x134 | 0xa42590 -> 0xa3dd80 (also the base ctor of ~17 world-lot nodes) |
| cIndexedMeshBuilder | 0xe73f4c+0:1, 0xe73f54+12:6, 0xe73f70+464:18, 0xe73fbc+472:48 | 0x1e0 | 0xa7ffa0 -> 0xa7b1d0 |
| cCompactorBuilder, cBoundingVolumeBuilder, cTangentSpaceBuilder | | 0x2c, 0x34, 0x30 | 0xa83930, 0xa85090, 0xa86bf0 |
| cSkinExtension, cBoneDataExtension | | 0x74, 0x4c | 0xa97720 -> 0xa97240; 0xa98470 |
| cDataListExtension, cDataListItem | | 0x30, 0x24 | 0xa990b0 -> 0xa98950; 0xac6d50 |
| cDisplayList | 0xe75404+0:49, 0xe754cc+8:4 | 0x160 | 0xaa0e00 -> 0xa9d460 |
| cFrameCache, cRenderGroupManager | | 0x50, 0x1c | 0xaa0e60, 0xaa37f0 |
| cShader | 0xe758dc:14 | 0x98 | 0xaa4fa0 (class operator new 0xaa4d90, ctor 0xaa4a40) |
| cShaderInit | 0xe760a4:27 | 0xa4 | 0xaa82a0 -> 0xaa7b90 |
| cMaterialAttributes | 0xe76188:16 | 0x28 | 0xaaa3c0 -> 0xaa9e40 |
| cMaterialDefinition | 0xe76240+0:13, 0xe76278+44:27, 0xe762e8+84:25 | | 0xaadf20 -> 0xaad480 (vtable storer 0xaad1c0) |
| cImageData | 0xe764fc+0:41, +4:13, +108:25 | 0x70 | 0xab1590 -> 0xaafd60 |
| cLevelInfo | | 0x54 | 0xab15f0 |
| cProceduralTexture, cRenderTargetTexture, cTextureImage | 0xe76750:15, 0xe766fc:20, - | 0x28, 0x58, 0x28 | 0xab4d90 -> 0xab4230; 0xab4df0 -> 0xab42b0; 0xab4e50 / 0xab4d30 -> 0xab4400 |
| cLODLevelsStore, cLODLevelsTable | | 0x1c, 0x1cc | 0xab6f40, 0xab6fa0 |
| cApplyShaderProgramConstants | 0xe76afc:18 | 0x4c | 0xac3200 -> 0xabd8c0 |
| cAttributeFFTextureMatrix | 0xe77578:27 | 0x88 | 0xac32a0 -> 0xabdc00 |
| cValidateRenderShaderContext | | 0x6c | 0xac3370 |
| lights | | 0x70, 0x70, 0x98, 0x68, 0x58 | cAmbientLight 0xa47ab0, cDirectionalLight 0xa47b10, cEnvironmentCubeLight 0xa47bd0, cLinearFogLight 0xa47c30, cDrawStateLight 0xa47b70 |

Not from a factory: cMaterialManager (vtable 0xe78514:39; getter 0xacc040 -> ctor 0xac9810), cTextureManager
(0xe7c778:30; 0xade2e0 -> 0xadd170) and cStringPool (0xe7c844:14; 0xadf8b0 -> 0xadf120) are cSceneGraphSystem's
singletons: its slots 5, 6 and 8 return them, Init 0xa4cbb0 (face +4 slot 8, 1151 B) builds them and Shutdown 0xa4d190
(slot 9) frees them; OnTick 0xa4d040 is slot 10. cMaterialParser (0x304 bytes; vtables 0xe79e30+0:5, +744:3, +752:51,
+764:9) is built by Init through ctor 0xacec90. 0xa4d600 returns a static built by 0xa4d660 (also called by 0xa4c6f0
and Init).

nTSSG (same columns):

| class | vtables | size | factory -> ctor |
|---|---|---|---|
| cTSSGSystem | 0xdae264:100 | 0x3b8 | ctor 0x246110 (verified), built in cDllCOMDirector::OnStart 0x2454c0 |
| cVideoBroadcastManager | | 0x54 | 0x2ab880 -> 0x2a9ca0 |
| cLotImposterDisplay | 0xdb38e0 | 0x60 | 0x2b0f50 -> 0x2adfa0 |
| cSceneManager | 0xdb5344:34 | 0x528 | 0x2be660 -> 0x2bd480 |
| cQueryStrategyManager, cLightRigManager | | 0x50, 0x34 | 0x2c1010, 0x2c2af0 |
| cLightMapManager | 0xdb5ba0+0:24, +4, +8 | 0x1a0 | 0x2c73f0 -> 0x2c4950 |
| cLightingManager | 0xdb5e90+0:46, 0xdb5f4c+4:4 | 0x548 | 0x2d83a0 -> 0x2ca6f0 |
| cShadowManager | 0xdb68ac:18 | 0xd4 | 0x2e7ba0 -> 0x2e63a0 |
| cDiffusionLightExtension | | 0xbc | 0x2ea3b0 |
| cLotSkirt | 0xdb7384+0:6, +8:17, +12:4 | 0x13c | 0x2fadd0 -> 0x2f6ab0 |
| cSeasonManager | | 0x2c | 0x3362c0 |
| cMergedGeometryNode | | 0x154 | 0x34fc20 |
| cShadow | 0xdba2f4:24 | 0x150 | 0x355700 -> 0x354320 |
| cCameraEffectsSupport | | 0x38 | 0x360dc0 |
| cLotImposterManager, cLotImposter | | 0x1c4, 0x14 | 0x367790, 0x36d0a0 |
| cDisplayListDynamicRenderStrategy | 0xdbbadc+0:7, +8:11 | 0x194 | 0x372190 -> 0x36e400 |
| cDisplayListCensorRenderStrategy | | 0x38 | 0x377ee0 -> 0x3775c0 (also built inline by 0x36e400) |
| c3DUIManager, cObjectModule | | 0x3c, 0x44 | 0x37ab30, 0x289680 |

Not from a factory: cDiffusionManager (0x94; vtables 0xdbeb7c+0:25, +4:4, +8:6; ctor 0x3c4140 called by cLightingManager
vf3 0x2d98c0); cLightSettings (0x48; ctor 0x3c6a30 from 0x2da270); cOverlayNode (0x19c; ctor 0x3ae470, called by
cFloor::vf3, cLotSkirt vf8_5 and others); cGableRoof (0x64; 0x3d1ad0, from cRoofManager vf13); 0x2bcb10 builds a 0x50-byte
object through a vbtable adjustment.

Camera: cTSCommonCOMDirector 0xcb4ec0 registers cTSCameraControllerManager (0xeb88a0:42, 0x34; 0xcc3040 -> 0xcc2710)
and cTSCameraSmoothSims1Controller (0xb48000; ctor 0xb45b70, 0x47c bytes, seven vtables). The nTSSG camera factories
build classes that embed it or cTSCameraSmoothFirstPersonController (ctor 0xb4df80) as a base: cCameraLive 0x358b00
(0x4d0), cCameraBuild 0x35a510 (0x47c), cCameraNeighborhood 0x35b350 (0x498), cCameraFirstPersonController 0x35c0c0
(0x2f8), cCameraCameraManController 0x35cf10 (0x35c), cCameraCreateASimController 0x35ebc0 (0x394). cTSCameraEffect
(0xa0) is built by ctor 0xc074f0 from cTSCameraComponentDescription::Prepare 0xc08b90. cTSWinSceneGraph (0xdd89fc:174,
0x1b8) is registered by cTSUICOMDirector 0x5b4a34: 0x5b9972 -> 0x5b7fff.

**Calls out** (callee bytes, run-1023, `coverage.py deps --by group`): 123 KB out, 126 KB in, 5,002 internal edges.
- world-lot 32.7 KB (cBaseSGRoof 6.2 from cGableRoof, cBaseGeometryNode 2.3, terrain and overlay strategies,
  cFloorGeometryNode, cWallGeometryNode); world-lot calls back 29.2 KB.
- unassigned 27.2 KB (cChoreoQueue ~9.8 from cCustomChoreoQueue, cShadow and cTSSGSystem; 6.7 KB with no owner).
- rendering-gfx 22.7 KB: cTSParticlesGeometry 9.9 (virtual calls from cDisplayList), cDeviceStateDX leftovers 3.7,
  cTSParticlesRenderable ~2.2, cDraw2D 1.0 (from cBlurBrush), and the nGZCompGeom helpers (below).
- shared-utility 9.4; animation 6.4 (nTSSG::cObject 0.5, cAnimatable 0.5, cSceneDBAnimSystem, cAnimManager);
  framework-gz 6.2 (cRZString 3.5, cNodeCloneContext 0.6, cIndexedIterator, the connection manager). persist-dbpf
  calls in 7.2 KB.

## Node templates: structure and the composition level (cObjectGraphNodeT, cCompositionTreeNodeT, the Impls)

From RTTI (`db/classes.jsonl`), the constructors and the vtables; scratch scripts, not committed. 125 node classes carry a
cObjectGraphNodeT vtable of 30 or more slots (the template instantiations, the concrete nodes, the lights, the nTSSG
geometry nodes, cTSParticlesRenderable, cScreenShapeRef, cPoolWaterGeometryNode).

**Object layout** (every instantiation): +0 the node vtable; +4 the cRefSingleThreaded count (cAbstractBehaviorTypeT's
constructor 0xa353a0 stores 0; slot 11 0x147cf0 returns its sign bit); +8 the cIGZPersistSerializableReferent face (7
slots); +0xc the Impl (a data class, non-virtual, no vtable except cViewerRefNodeRecursiveImpl, which has RTTI). The nTSSG
cIObjectV family (cObject, cAnimatable, cLocomotable, cPerson) puts the Impl at +0x10 (ctor 0x256f10) and is the animation
campaign's.

**Constructor chain** (cCompositionTreeNode 0xa36900 -> cCompositionTreeNodeT 0xa353b0 -> cObjectGraphNodeT 0xa36870 ->
cAbstractBehaviorTypeT 0xa353a0): each level calls the one below, then stores its own two vtables (+0, +8); cObjectGraphNodeT
first stores 0xdaf4a4 at +8 (the persist interface's), then its vtables, then constructs the Impl at +0xc. cTransformNodeT
(0xa39fa0) also calls slot 29's body on the Impl (0xa38f50 -> 0xa38320 with (0, 1)) after storing its vtables.

**Impl classes** (RTTI of cViewerRefNodeRecursiveImpl gives the chain cObjectGraphNodeImpl <- cCompositionTreeNodeImpl <-
cTransformNodeImpl <- cBoundedNodeImpl <- cRenderableNodeImpl <- cViewerRefNodeBaseImpl <- cViewerRefNodeRecursiveImpl).
Constructor / destructor as the cObjectGraphNodeT instantiation calls them (`lea ecx, [esi+0xc]; call`), size = the
concrete node's size less 0xc where one exists:

| Impl | ctor / dtor | size | used by (cObjectGraphNodeT<I, Impl>) |
|---|---|---:|---|
| cObjectGraphNodeImpl | inline / 0xa342a0 | 8 | cIGeometryNode (cGeometryNode, the nTSSG geometry nodes), cIObjectGraphNode (cViewer_LEGACY) |
| cCompositionTreeNodeImpl | 0xa36920 / 0xa36bd0 | 0x1c | cICompositionTreeNode, cIResourceNode |
| cTransformNodeImpl | 0xa3a020 / 0xa3a140 | 0x3c | cITransformNode |
| cExplicitBoundNodeImpl | inline / 0xa4b630 | 0x64 | cIExplicitBoundNode |
| cPartitionNodeImpl | 0xadfe10 / 0xadfe30 (a jump to cBoundedNodeImpl's) | 0x60 (lane OI: the bounded Impl's; both classes' own words start at +0x6c) | cISceneNode, cILooseOctreePartitionNode |
| cRenderableNodeImpl | 0xadfff0 / 0xadfe30 (folded with the partition's) | | cIRenderableNode (cTSParticlesRenderable) |
| cShapeRefNodeImpl | 0xa54200 / 0xa544f0 | 0xc4 | cIShapeRefNode |
| cLightRefNodeImpl | 0xa60c20 / 0xa60ec0 | 0x84 | cILightRefNode |
| cViewerRefNodeImpl | 0xa644c0 / 0xa64840 | 0x104 | cIViewerRefNode |
| cViewerRefNodeRecursiveImpl | 0xa69f70 / 0xa6a150 | 0x100 | cIViewerRefNodeRecursive |
| cReferentNodeImpl | 0xa446b0 / 0xa44710 | | cILight, cIStandardLight, cITextureLight, cILinearFogLight, cIDrawStateLight, cIShape |
| cBoundedNodeImpl | 0xa3b290 (at +0x10) | | cIObjectV (nTSSG, animation) |

cCompositionTreeNodeImpl (0xa36920): +0 0, +4 -1 (0x18bf80), +8..+0x18 0. Its member functions 0xa37d00-0xa38430 and
cObjectGraphNodeImpl's 0xa34530-0xa34bb0 are what the folded template slots call (below); named and ours since lane NZ.

**Folded slot map** of the node vtable (slots 0-29, cIAbstractBehaviorType and cIObjectGraphNode, common to all 125; from
slot 30 the interfaces differ by family, so a slot index means different things):
- One body for 114 classes, a second for the 8 viewer instantiations, a third for cAnimatable's three (nTSSG), and no
  reference from any non-node class: slots 18, 19, 22, 23, 24, 26, 27, 28 (`push arg; push ecx; add ecx, 0xc; call Impl` or
  `add ecx, 0xc; jmp Impl`: 0x3a6340 -> 0xa34900, 0x3a6350 -> 0xa349d0, 0x3a4680 -> 0xa34530, 0x3a5f20 -> 0xa34870,
  0x3a5a30 -> 0xa345a0, 0x3a64b0 -> 0xa34bb0, 0x3a64a0 -> 0xa34af0, 0x3a5740 -> 0xa34570). Family-only `shared_vf_*`: hookable
  once every vtable that holds them is ours (`docs/verification.md`), i.e. after N0-N3, D3, H1 and the nTSSG geometry nodes;
  until then ours live in our vtables only.
- Folded with unrelated classes, never hooked: 1 (AddRef 0x360650 and five variants, 0x360650 also in 5 non-node classes),
  7 and 10 (0x377e0, an empty `ret 0xc`, 86 other classes), 11 (0x147cf0, 62), 16 / 17 (0x3a4f40, 10), 20 / 21 (0x145770 /
  0x149cf0, `mov eax, [ecx+0xc]` and its twin, 300+), 12 / 13 (0x25f160 / 0x259390 and variants, shared with a few non-node
  behaviour types).
- Per instantiation (20-41 bodies): 0, 2, 3, 5, 6, 14, 15, 25, 29; 8 and 9 (cAbstractBehaviorTypeT, each body also in the
  instantiation's own cAbstractBehaviorTypeT vtable); 4 is one body (0x3a5700) for 111 classes.
- Slots 30-61 for the composition-tree family (cICompositionTreeNode): forwarders to cCompositionTreeNodeImpl (43 0xa37fd0,
  44 0xa37e40, 45 0xa37d00, 46 0xa38240, 51 0xa37d80, 60 0xa38320, 25 0xa38340, 29 0xa38320) and field getters on the Impl
  (33 / 34 +0x14, 35 / 36 +0x18, 37 / 38 +0x1c, 39 / 40 +0x20, 49 +0x24; most folded with 50-700 other classes).

### Before this campaign (prerequisites, lane NX)

- **The single-threaded behaviour template**: `src/gz/cAbstractBehaviorTypeMT.h` is the cRefMultiThreaded one (slots
  0-14, Traits: event manager cell, missing attribute, Registry). Lane N0 writes the cRefSingleThreaded sibling.
- **Ours already**: cConnectionManager, cGZPersistBinaryReader / Writer (lane NN), cGZPersistResource (EL), the
  resource manager, cGZCOM, the device layer (cDeviceDX, cDeviceStateDX, cGraphicSystemDX, cGZGraphicSystem, the
  resource managers, textures, arrays, formats, shader programs). cDeviceStateDX's leftovers (25 KB, 46% done) and
  cDeviceResourceManagerDX (rendering R1c) are better finished before DL.
- **Original calls that stay**: world-lot, animation, unassigned (cChoreoQueue) and effects callees stay
  `OriginalFunction` calls; lanes list them in `original_calls`. Note the order: `coverage.py deps --by group` lists
  world-lot (5) before scenegraph (7), while `docs/roadmap.md` puts the scenegraph first and cuts world-lot calls. The
  scenegraph defines the bases world-lot derives from (cGeometryNode, cSGResource, the templates), so it goes first; the
  33 KB of world-lot callees are leaf calls.
- **The virtual-base shape**: decided (lane NY, N0 part 1): plain C++ `virtual` inheritance reproduces cSGResource's
  layout, vtordisp and thunks (`docs/standalone-shape.md`, "Virtual bases and vtordisp"; tests/sg_virtual_base_layout.cpp).
  The base-constructor question (game subclasses call it with the most-derived flag 0) is decided too (lane OE, N1): the
  hook is our compiler's own constructor (Pitfalls).

### Composition level and the pilot cCompositionTreeNode (lane NZ, N0 part 2)

- N0 part 1 (lane NY): the virtual-base decision (tests/sg_virtual_base_layout.cpp) and the template structure above.
- N0 part 2, steps 1-4 for the composition level (lane NZ): 33 verified.
  - **cObjectGraphNodeImpl** (`src/sg/cObjectGraphNodeImpl.{h,cpp}`, group cObjectGraphNodeImpl): the 14 members
    0xa342a0-0xa34f20, the four subordinate notifications (stdcall 0xa34310 / 0xa34370 / 0xa34430 / 0xa344d0) and
    cSubordinateListChanged's own slots (0xa342f0, 0xa34fc0, 0xa34410), all hooked at their RVAs. cGraphConnChanged
    (vtable 0xdb1f98) is ours as a class but its slots are not claimed (0x28caf0 builds that vtable too).
  - **cCompositionTreeNodeImpl** (`src/sg/cCompositionTreeNodeImpl.{h,cpp}`): its 11 members 0xa36920-0xa38430, hooked.
  - **Templates** (`src/sg/SGNodeTemplates.h`): cObjectGraphNodeT and cCompositionTreeNodeT over
    `src/gz/cAbstractBehaviorTypeST.h` (cRefSingleThreaded: the MT template with plain count arithmetic). Interfaces and
    the event bodies: `src/sg/SGNodeInterfaces.h` (method facts `db/methods/SGNodes.jsonl`).
  - **Pilot cCompositionTreeNode** (`src/sg/cCompositionTreeNode.{h,cpp}`): factory 0xa37910 hooked to Create (the
    switch). The game's slot bodies stay unhooked (the status entry lists them): 0xa36900 still builds game nodes for
    0x25a050, 0xa6a750 and the game's Clone 0xa373c0, and most bodies are folded across the node family.
  - Tests: tests/sg_object_graph_node_impl.cpp (20), tests/sg_composition_tree_node_impl.cpp (11),
    tests/sg_composition_tree_node.cpp (5: factory, slots, persist, clone, events; each side builds its trees with its
    own factory). Fixture `tests/sgfx.h` (below).
  - Original calls left: 0xaeb710 (the string pool's cell constructor), 0x263f30 (the node event registry), 0xa384f0
    (the Impl's CloneInto; ours since lane OR), 0xa37fd0 / 0xa37e40 (the pre/postorder iterator builders; slots 43 / 44 untested). The tree
    propagation and the slot-51 builder 0xa37d80 are ours since lane OM (Done below).

## Conventions and test patterns

- **Standalone shape** (`docs/standalone-shape.md`): whole classes with our vtables; the factory hook is the switch;
  bisect switch per class = its factory, constructor and any inline builder (0x36e400 for the censor strategy, Init
  0xa4cbb0 for the singletons and cMaterialParser, cLightingManager vf3 0x2d98c0 for cDiffusionManager).
- **Templates**: write the C++ templates once (N0) with the Impl classes as members at the original offsets; each
  instantiation lane adds its explicit instantiation and the status entries of its copies. Folded `shared_vf_*` bodies
  shared across unrelated families are `verified` without a hook, entered once in the defining class's group
  (`docs/verification.md`); a body only one family references may be hooked once every reference is ours.
- **Shader controller copies** (cShaderComponentT 21, cShaderControllerT 19, ~202 B each, not folded): one test body
  parameterised by the copy's RVA and interface, one status entry per copy.
- **Float maths** (bounds, frustum, lighting, diffusion, camera): compare results bit-exact (`memcmp`), not with a
  tolerance; a mismatch means a different operation order, not noise.
- **Owner-inferred groups**: confirm the owner of each function before writing it (vtable slot, this-type of callers);
  record corrections with `tools/ghidra.py annotate --lane <x>`.
- **Original calls**: world-lot, animation, effects and cChoreoQueue callees through `OriginalFunction<Fn, 0xRVA>()`.
- **Float inputs and NaNs** (lane NY, tests/compgeom_linear.cpp): generate small integers, decimals, wide exponents,
  denormals, signed zeros, infinities and NaNs, and nudge values by a few ulps around the branch thresholds (the ulp step
  never makes a NaN). Allow at most one NaN per case, and then no infinities or values of 1e6 and above (so the maths
  makes no second NaN): with two NaNs the operand order decides which payload survives, and the original's order is not
  worth reproducing. `NearlyEqual` (32 ulps on ordered bits) is the game's containment tolerance: compare its result
  exactly. Require minimum counts of each outcome (true / false, origin hits) so a generator change can't hollow a test.
- **Vtable RVAs stay out of `src/`** except in comments: `tools/callers.py` reads a hex literal of a vtable RVA in src as
  "our code holds the game's vtable" and then refuses the slot hooks. Tests may name them (`GameVTable`).
- **Node classes** (lane NZ): Impl members are plain non-virtual members taking the outer node as a parameter, hooked
  at their RVAs and difftested on a raw Impl block (`ctx.Original<thiscall>(rva)(block, outer, ...)` against our member
  on the same block) with fake outer / child nodes. A concrete node is `class X : public <Level>T<Iface, Impl, Traits>`
  plus its own slots (Clone, the iterator builders: each instantiation names its Impl's), `kSize` static_asserted; the
  template's virtual destructor lands in the slot after the interface's last (61 for the composition family).
- **Instantiation tests**: each side builds its nodes with its own factory (`ctx.Original<sgfx::Factory>(rva)` and our
  Create) and drives them through the slots; compare every node's words except the two vtable words (+0 and +8: skip
  mask 0b101), never against a node of the other side.
- **Status of an instantiation**: the factory carries the class (`verified`, hooked); the class's game slot bodies stay
  unhooked while the game's constructor still has other callers, and are listed in the factory entry's notes.
- **Transform-family pilots** (lane OB, tests/sg_transform_pilot.h): one driver, `Pilot<Cfg>`, for both pilots (a Cfg
  names the factory, size, interface, level and behaviour cell); the explicit-bound level adds the bound, holder and shape
  ops. Real nodes on both sides, so the tree code (propagation 0xa39820 / 0xa39920, the world-transform refresh 0xa39b70)
  runs as original code; three scopes put its statics in place per test (`sgfx::kNodeRegistry`, `kTransformRegistry`
  for 0x2640f0, and `RefreshScope` for the refresh's table, worklist and guard at 0x10f5fa0-0x10f5ff0). Fakes: the shape
  (cIBound slots 3, 20, 37, 38), a linear (cILinear3 slots 18, 29) and the holder (80 slots; the propagation calls 78 and
  79). Mutants on the template bodies need `--force-filter` (they have no status entry of their own).
- **Constructors set flags** (the first run of the pilots found it): every template level's constructor sets flags on its
  own node: transform flag 0 (0xa39fa0), bounded 1 and 11 (0xa4b350), explicit-bound 9 (0xa4b3d0). A template that
  leaves them out passes every Impl-level test and fails the first node comparison.
- **Event bodies** (cSubordinateListChanged, cGraphConnChanged): compare them through their own slots (type, sizes,
  Assign, CloneInto), the game's vtable over a raw body against ours; a fake that receives a body notes it with
  `sgfx::NoteBody`.
- **A switched node class's status** (lane OG, db/status/cResourceNode.jsonl, generated by a small script from the
  survey): once the factory is the only builder (the game's constructor reached only by the factory and the game's own
  Clone), every body only the game's vtables of that class hold is `in_effect: callers` with the factory's trap; the
  template-level bodies of the instantiation (behaviour, object-graph and composition levels: constructor, destructors,
  Release, the event copies) too. Adjustor thunks need no in_effect field; vtordisp thunks do. Two kinds of references
  `status.py --check` reports need `callers_handled`: the C++ EH unwind funclets (`code:0xd723e3` jumping to a base
  destructor; they belong to the game constructor's and Clone's frames, which never run) and the destructor <-> deleting
  destructor cycle (handle the code reference in the base destructor, not the deleting destructor's vtable slot, or its
  trap rests on no hook).
- **Uncounted QI answers** (lane OG): the original's QueryInterface answers some iids without AddRef (0xdb01d4b2, the
  raw composition face, and 0xfb859324, the behaviour type). A test that releases after every successful QI crashes the
  original; release only when the count grew (tests/sg_resource_node.cpp op 0, as sg_composition_tree_node.cpp does).
- **More nodes than `sgfx::Nodes::kMax`** (lane OG): chained clones make more nodes than the 16 a test holds. Name the
  extra ones past kMax (`drawfx::AddRange(n, size, kNodeTag | ((kMax + k) << 8))`) so their pointers normalise, and
  never walk a node that was not held (it may be freed): tests/sg_resource_node.cpp `Hold` returns false and the clone
  helper then answers null.
- **Line and ray share bodies** in the game (Reframe 0x18fa30, Transform 0x18fab0, the direction / origin getters): one
  adapter per body, registered once; the hook table's aliases (`L`, `R`, `S`) name which instantiation's adapter it is.
- **Partition nodes** (lane OI, tests/sg_loose_octree_partition_node.cpp): the transform pilot drives level 3 (the
  partition level: its Cfg names the octree's factory, size and behaviour cell; `kSkipWords` per class) for the tree,
  persist, clone and event slots; the class's own work gets its own driver over fakes: content nodes (cIRenderableNode,
  84 slots: holder 67 / 68, bound 70 / 71, flags, Draw 82), sub-partitions (cIPartitionNode, 89 slots) and the query
  visitors, each fake's answers from seeded state so both sides see the same world. Compare after every step, not
  only at the end: the octree's words, its cells walked from the root, its item list, and the pools.
- **A class with its own pools** (the octree's cRZFastFixedAllocator2 cells 0x10f620c / 0x10f6218): point the pool at a
  test region and fake its NewPage / FreePage slots (`Pool<Rva, Log2, BlockBytes>` in the octree test), reset to the
  same bytes per side and seed; then the blocks, and every pointer to them the class stores, compare as they are.

- **Pilot switches for nodes the tree code treats specially** (lane OK, tests/sg_transform_pilot.h): a Cfg may set
  `kRootsOnly` (no parents: MakeTree links nothing, op 1 never links, op 13's subtree is the node alone) and
  `kFixedFlags` (a mask of flag bits ops 2 and 20 never flip). cSceneNode needs both (tests/sg_scene_node.cpp says
  why); other classes leave them out and run as before.
- **A system slot called from a constructor** (lane OK): the node is not named (`drawfx::AddRange`) yet when its
  constructor calls out, so its pointer does not normalise and differs per side. Record the argument as presence only
  (`W0`) and keep the real list in the fixture (sgfx `Resources::sceneNodes`, `SceneNodeIndex`): the test then notes
  each node's index in that list, after it is named.
- **Pointer answers through Norm** (lane OK): a slot that answers a pointer (GetFromParent31 / 32: a scene node answers
  itself; GetOwnerInfo: the virtual base) is noted through `drawfx::Norm`, never raw: raw it is a per-side heap address.
- **A content fake whose call carries the tree code's record** (lane OK): the refresh 0xa39b70 hands OnContentChanged
  (slot 60) record+4 in a table it allocates per side (the static 0x10f5fc4): log the pointee (`F7`), not the pointer.
- **Tree-code tests over fakes** (lane OM, tests/sg_tree_code.cpp): one 97-slot table (kNode's 0-60, then GetHolder 69,
  Clear 77, RemovePartition / RemoveNode 78 / 79, QueueNode / UnqueueNode 95 / 96) serves the nodes, the scenes and the
  holders; each node fake keeps its flags, links, limit, scene and holder in `value[]` and answers SetFlag / TestFlag /
  GetFlags from them, so both sides walk the same tree and the final flags compare. NotifyListeners logs its body's
  vtable word (`F1`), which is the game's cNullEvent vtable on both sides. Random 15-bit flags reach every branch
  (bounded / partition / stale / queued), and a mixed test interleaves the four entries so one call's flags feed the
  next.
- **A system with singletons and COM objects** (lane OM, tests/sg_scene_graph_system.cpp): stand in the singleton
  getters with `shape::CodePatch` over their RVAs (both sides reach them: the original directly, ours through
  OriginalFunction), the COM getter with `shape::StandIn`, and give the fakes the vbtables the real objects have when
  the code reaches a face through a virtual base (the factory's face, the material parser's).
- **The refresh over fakes** (lane OP, tests/sg_world_transform_refresh.cpp): the original side still needs the game's
  function-local work table, so every test that reaches 0xa39b70 on the original side opens a `sgtp::RefreshScope`
  (tests/sg_transform_pilot.h; ours ignores it). Node fakes come from `sgfx::MakeFlagNode` with slot 57 (ToParent) a
  deterministic function of the node id and the input transform (logged `F7`), slot 60 logged `F7@2x8u` (the
  transform, then the limits array of args[2] records of 8 words) and slot 41 a child range whose end the fake picks,
  so early range ends are reached.
- **Iterators over node fakes** (lane OP, tests/sg_tree_iterators.cpp): the iterators' pools are the game's
  cRZFastFixedAllocator2 objects on both sides; the test resets them to a test region (the octree test's pattern) and
  notes their pages at the end, mapping the default filter word (each side's own object) to one label.
- **Light layout** (lane OR, `src/sg/SGLightTemplates.h`): `class cLightT<I, Traits> : public cSGResource, public
  cReferentNodeT<I, Traits>` puts the cSGResource part first (+0, vbptr +0x1c, name +0x20, segment +0x24, flags +0x28),
  the node face I at +0x2c (count +0x30), its persist face at +0x34, the cReferentNodeImpl at +0x38 (referrer vector
  +0x40..+0x48), the derived fields from +0x4c, then the vtordisp and the virtual base last. The light's count is the
  node's (+0x30; the cSGResource part's own count at +4 stays 0): every face's AddRef / Release / GetRefCount is the
  node's. MSVC gives the game's layout from the declaration order (static_asserts in src/sg/cStandardLight.cpp).
  Sizes: ambient / directional 0x70, draw state 0x58, linear fog 0x68, point 0x90, env cube 0x98, spot 0xa4.
- **A light test** (tests/sg_lights_standard.cpp): the resource node's pattern (per-side factories, four faces, word
  dump with vtables / vbptr / the +0x28 word's padding skipped) plus the referrer vector noted by content (ids, size,
  capacity) instead of its three heap pointers, referrer fakes (kBehavior, slot 4 answering at random) and the lights'
  own event registry placed with `RegistryScope{0x10f6138, 0x10f6140, 0x10f6150, 0x10f6158}` (0xa49210, the same
  shape as the node registry). Mixed classes in one world (ambient and directional at random) test the shared bodies
  once for both.
- **Classes the nTSSG lights derive from keep their game bodies live** (lane OR): hooking a light's factory makes the
  director's lights ours, but nTSSG classes built on the same game constructor (cAmbientFromLightMapLight on the
  ambient light, cHeightMap(Binary)ShadowLight on the directional, cHalfspacePointLight on the point light) still run
  the game's bodies through their own vtables, so those bodies get no status until the nTSSG class is ours (or until
  each is hooked to an adapter tested on the game-built nTSSG objects, which nobody has tried). The factory entries list the bodies ours
  replace. Spot, env cube, fog and draw-state lights have no nTSSG derivation: their own bodies can be claimed in
  effect through their factories.
- **A value union whose stale words getters read keeps the game's layout** (lane OX, cDataListItem): getters of
  another type read the old words (GetCount of a vector, GetSint32 of a float), so ours stores the same words at the
  same places, and its tests compare cross-type reads only after a value that wrote all of them.
- **COM-made children on both sides** (lane OX): stand in for the framework getter 0x1ae18d on both sides
  (`shape::StandIn{0x1ae18d, &GetDirectorFrameworkObject, stand}`) with a fake cIGZCOM whose GetClassObject makes the
  running side's object; then Read / Parse of a list build each side's own children (tests/sg_data_list_item.cpp).
- **A virtual-base class hooked on the game's layout** (lane OY, cExtension): bodies take `this` as the subobject the
  game passes, each hooked at its RVA, the game's vtordisp thunks left original; a game-derived test runs a real
  derived class's constructor and destructor (cTagExtension 0xa962d0 / 0xa96390) on both sides, ours with every hooked
  RVA patched to the hook table's target (tests/sg_extension.cpp, Extension_GameDerived).
- **An interface with its own vbptr has no IUnknown slots** (lane OY): cIDataListExtension's slot 0 is SetListName;
  release or QI such a face through its virtual base (`static_cast<cIDataListExtension *>(p)->Release()` reaches
  cIExtension's), never by casting the face pointer to cIGZUnknown.

## Shared fixture: sgfx

**`tests/sgfx.h`, the node world (lane NZ)**: documented at its top; extend it there rather than copying it. It holds:
- slot tables for the node interfaces (kNode: cICompositionTreeNode's 62 slots, the first 29 being cIObjectGraphNode's)
  and their helpers (extension, tag face, referent, string pool, persist reader / writer, clone context, traversal,
  behaviour);
- state-backed fake nodes (links, children range, extension list, events noted) and fake extensions (with the
  cITagExtension face behind a vbptr);
- the string pool in its cell 0x10f9070, a queue-driven reader and a noting writer;
- our cConnectionManager in a behaviour's cell (`Manager`) and the node event registry placed at its static
  (`RegistryScope`, `kNodeRegistry`);
- `Nodes` (real nodes from either side's factory, ranges named, linked through AddChild, NoteShape / NoteWords) and
  `CheckSides` (build per side, a seeded run, `MarkStep` so a runner that dies reports the step).

Still to build (D1 / M2 on):
1. **Interface fakes**: fx_slots tables (`tools/slotsig.py --header`) for the other SG interfaces (cISceneNode, cIShape,
   cIGeometryNode, cIMaterialDefinition, cIDisplayList, cIViewer, cILight, cISGResource) and the nGZGraphic4 device
   interfaces, recording through `tests/drawfx_core.h` (argument kinds i/u/b/f/r/R/p/P/F/W/@/s/w/S). Wide fakes with a
   vbtable at +4 for virtual-base faces: `tests/sg_tssg_wide.h` (lane JD).
2. **Node world, further levels**: done to slot 78 (transform 61-65, bounded 66-76, explicit 77-78: tests/sgfx.h kNode...kExplicitBoundNode, tests/sg_transform_pilot.h); the partition level (pilot level 3, traversal slot 13 `VisitPartition`; `Nodes::NoteWords` takes a 64-bit skip mask) and its fakes for slots 77-88 live in tests/sg_loose_octree_partition_node.cpp (lane OI; move them into sgfx when cSceneNode needs them). Resources (lane OE): `sgfx::gResources`, the
   framework in its cell 0x10c9378 (slot 5 GetSystemService answers the resource manager for service 0x056b906e / iid
   0x656b8efc), the resource manager (slot 34 FindSegmentWithKey hands out three segment fakes in turn, every fourth
   null; 9 / 14 return what the test sets) and the segments (slot 21). Still missing: a COM fake that answers class ids
   through the side's factory.
3. **Persist reader**: our cGZPersistBinaryReader over synthetic byte buffers (resource Read / Write round trips:
   cGeometryDataContainer, cShape, cMaterialDefinition, cImageData, cSceneNode); `tests/ts_persist_world.h` for the
   world side.
4. **Device world**: our cGraphicSystemDX, cDeviceDX and cDeviceStateDX over the fake Direct3D device
   (`tests/fx_d3d9.h`, `tests/d3dfx.h`: Answers, Creates, Script, Buffer, Declaration); the recorded device call stream
   is the result for M2-M4, DL, TS1, LG6 and LT2.
5. Reused: `tests/shape.h`, `tests/sg_deleting_destructors.h`, `tests/ts_services_fixture.h`, `tests/code_patch.h`,
   `tests/game_allocator.h`.

## Pitfalls and risks

General pitfalls. Class-specific ones ("What bit lane X") are in each class's section.

- **Virtual bases and vtordisp.** cSGResource puts a vbptr at +0x1c and a vtordisp at +0x2c; its derived classes keep
  the cSGResource part at their last vtable offset (152 in cShape, 128 in cGeometryDataContainer, 304 in cGeometryNode).
  The vtordisp thunks are `sub ecx,[ecx-4]` (VtordispThunks). Decided (N0 part 1): MSVC's own `virtual` inheritance
  matches (`docs/standalone-shape.md`, "Virtual bases and vtordisp"; tests/sg_virtual_base_layout.cpp). What to carry
  into N1 and later:
  - Bodies of the virtual base's slots take ecx = object + 0x30 (cSGResource's static offset), whatever the real
    placement; their hook adapters subtract 0x30 by hand (`reinterpret_cast`; C++ has no static_cast from a virtual base).
  - Derived classes get vtordisp thunks in their virtual-base vtable like the game's (tested with a user-declared
    constructor, which every game derived class has; a derived class without one is untested).
  - **Base constructors called with flag 0** (0xa3c270 from 20 derived constructors; cGeometryNode's 0xa3dd80 from the
    world-lot nodes): a hook cannot be `new (p) X`, which sets the vbptr and builds the virtual base at X's own offset.
    Decided (lane OE, N1): the hook is our compiler's constructor itself (`??0X@...@QAE@XZ`, same thiscall + flag + `ret 4`
    ABI; with flag 0 it is the base part), its address through a linker `/alternatename` (src/sg/cSGResource.h and
    cSGResourceClass.cpp; docs/standalone-shape.md). D3 does the same for 0xa3dd80. Test it as
    tests/sg_resource_class.cpp does: the base part alone at several virtual-base placements, and inside a game-derived
    constructor with the hooks patched over (`shape::CodePatch`). Keep the hook row a constant initializer
    (`&OE_X_Constructor`, not a function call): one dynamic row hides the whole table from matchcheck.
  - **No two sources with one file name in a target.** CMake then names both objects `src/<dir>/X.cpp.obj`, and
    matchcheck (which looks for `X.obj`) loses every recorded match of both files. That is why the class lives in
    src/sg/cSGResourceClass.cpp next to the hook-shaped bodies in src/gz/cSGResource.cpp; name later classes' files so
    they don't collide either (src/sg vs the src/gz bodies of cSceneNode / cResourceNode, if any).
  - **A hooked base part changes what game-derived objects carry**: between a game derived constructor's base-part call
    and its own vtable store (and in its destructor after the base-part destructor) the object carries our vtables;
    every game derived class's own words are untouched. The game's cSGResource vtables are then stored by nothing that
    runs: their slots and thunks are in effect through callers (db/status/cSGResource.jsonl).
  - **Write's eax.** cSGResource's 0xa3ced0 returns the writer's slot 23 answer, and the writer's WriteSerialized
    (0x9c0a60) returns whatever eax the object's Write slot (slot 5) leaves; a derived Write leaves its last writer
    call's answer (cResourceNode 0xa50c00: slot 10's), not 0xa3ced0's. The shared referent interface
    (sg/SGNodeInterfaces.h) declares Write `void`, so our Write slots leave no defined eax. Whether any caller of
    WriteSerialized reads it is not settled (lane OE): check before relying on it, and retype the shared slot (a shared
    fact: full suite) if one does. cSGResource keeps a named `WriteResource` that returns the answer for the 0xa3ced0
    adapter, which game code calls directly.
- **Shared template layouts.** The node templates are used by world-lot, animation (cIObjectV with cBoundedNodeImpl),
  effects and the UI. A layout mistake in N0 shows up in four campaigns; N0's tests cover every instantiation's size
  and vtable.
- **Folded bodies.** Many template slots are `shared_vf_*` across unrelated node families; never hook those
  (CLAUDE.md), and check the family before claiming one.
- **Owner inference** in the large groups (above): sizes move between lanes after the owner check.
- **Inline construction**: some classes are built inside other constructors or getters (the list under Creators); the
  switch must cover those sites too.
- **Per-frame hot path**: DL and TS1 run every frame; an error shows as a wrong frame, not a crash. Device-stream
  comparison is the acceptance, not the smoke test alone.
- **Particles inside the display list**: cDisplayList reaches cTSParticlesGeometry (9.9 KB) by virtual calls; those
  stay original until effects (rendering E2).
- **Node tests, what bit lane NZ** (each one cost a crash or a hang before it was understood):
  - drawfx's Dispatch treats slots 1 and 2 as AddRef / Release (counting `refs`) unless the slot has a handler: a face
    whose slot 1 is something else (cITagExtension's GetTagText) needs `On(1, ...)`.
  - A slot table that is one entry short shifts every later slot's arity: the callee pops the wrong byte count and the
    caller's stack breaks far from the cause. sgfx static_asserts its table sizes and some names; do the same.
  - Slots with a hidden out (GetChildren 41 / 42, the transforms 53 / 54) must write it in a fake: the tree propagation
    0xa39820 iterates the cChildRange it gets. sgfx's node fake writes all four (its transforms are distinct per id,
    so a world transform that skips its parent shows under a fake parent: a pilot mutant survived until it did).
  - Flag bit 1 marks a bounded node: the propagation 0xa39820 calls its slot 69 (the holder getter, beyond the
    composition family's 62) and, when the holder is non-null, the holder's slot 79 with the node (slot 78 without
    arguments under flag 6). Don't set it on a plain composition node; a bounded node's holder fake needs 80 slots.
    The other propagation, 0xa39920, calls slot 95 (the brief said 96; 0x17c / 4) on its second argument when that is
    non-null: the tests' root lookup (slot 32 of the node) is null, so it does not run.
  - Slot tables are easy to miscount: a cILinear3 table with two entries too many put "IntersectsBoundIn" at slot 20 and
    the original called a null slot (`exception at 0x0 outside the original`). Build long tables with a constexpr
    function that fills unsettled entries and sets the few used slots by index.
  - The refresh 0xa39b70 allocates its table in a function-local static behind a thread-safe-static guard: without
    `RefreshScope` the first bounded-node query that finds flag 12 set dies of heap corruption later, in unrelated code.
  - The behaviour's event manager cell (0x10f5f98) must hold a manager: when it is null the original asks the framework
    service, which the harness has not set up. The node event registry 0x263f30 is a thread-safe static: place it
    (`sgfx::RegistryScope`) or the original's first use corrupts the heap.
  - A clone context fake that answers one node for every source makes that node its own child under mode 1 (the
    Impl's CloneInto 0xa384f0 asks it for each child): the run hangs.
  - Freshly made nodes (clones and their children) must be named (`drawfx::AddRange`) before their pointers are noted.
- **NaN payloads follow operand order** (lane PC): when both operands of an SSE add, multiply or subtract are NaNs the
  result is the first one, quieted; MSVC may swap the operands of + and *, and a float returned from a non-inlined
  function is combined on the x87 stack, where the NaN with the larger significand wins. Frame and box maths that must
  match bit for bit with two NaNs computes plainly and, only when the result is a NaN, again with each operation's
  first operand spelled out (src/compgeom/Frames.h `AddInOrder`, Frames.cpp `Ordered`). Tests with up to two NaNs per
  case find this; one NaN per case never does.
- **x87 precision control** (lane PJ): the game's D3D9 device is created without D3DCREATE_FPU_PRESERVE, so the
  device thread's x87 can run at 24-bit while the harness runs at 53. Our SSE2 code is unaffected; the original's
  x87 chains (values kept on the x87 stack, or double arithmetic) round differently, and so must any emulation of them
  in doubles: use `x87::Rounded` (src/misc/X87.h) after each operation. Test such functions at both precisions
  (`difftest::X87Precision`, or the whole suite with `TS2_X87_PC=24`; docs/lane/test.md). And check our own .obj:
  MSVC does x87 arithmetic on values returned in st(0) even under /arch:SSE2 (docs/lane/test.md, same bullet).

## cSGResource (resource base, virtual bases)

The base of every resource class (Subsystem map, "Classes deriving from cSGResource"); the virtual-base and
base-constructor rules are in Pitfalls ("Virtual bases and vtordisp").

### Result (lane OE, N1 part 1)

- N1 part 1 (lane OE, 2026-10-27): **cSGResource as a whole class** and the base-constructor decision (Pitfalls below,
  docs/standalone-shape.md). `src/sg/cSGResource.h` + `src/sg/cSGResourceClass.cpp`, group cSGResource: 27 new verified
  entries (~1.2 KB; 35 in the group): the constructor 0xa3c270 (hooked to our compiler's constructor), destructor
  0xa3c2f0, QueryInterface 0xa3c4d0, Read 0xa3c540 and Write 0xa3ced0 hooked to adapters; GetTypeInfo 0xa3ce10 and the
  deleting destructor 0xa3c380 in effect through callers (trapped); the game vtable's 20 vtordisp thunks verified
  unhooked (ours are our compiler's). Tests: tests/sg_resource_class.cpp (SGResource_BasePart 16 seeds, _Slots 96,
  _GameDerived 48: cResourceNode's own constructor and destructor run with our base part patched in); sgfx gains the
  framework, resource manager and segment fakes. 37 mutants killed. Original calls left: none. Gaps: Write's eax (the
  shared interface's Write slot is void; Pitfalls), cIGZPersistResource2's slot split tentative.

### For every cSGResource-derived class (lane OG)

5. **For every cSGResource-derived class** (N2 lights, D1 cGeometryDataContainer, D3 cGeometryNode, M2, H1, the world-lot
   nodes): the class derives from our `cSGResource` (src/sg/cSGResource.h); the virtual base's slot bodies are
   cSGResource's (hooked at their RVAs, shared by every derived class's vtordisp thunks); a derived Write calls
   `cSGResource::WriteResource` where the original calls 0xa3ced0 (Pitfalls, "Write's eax"). A derived class whose constructor stays game
   code still works: 0xa3c270 / 0xa3c2f0 are ours and build our base part inside it (SGResource_GameDerived).
   cResourceNode (lane OG) is the worked example: the class file, a test file over the sgfx world with the cSGResource
   part's padding skipped, and a status file in the "switched node class" shape (Conventions). The vtordisp thunks
   in the class's own address range (the virtual base's slots the class overrides) are entries of their own, in effect
   through callers.

## cResourceNode

### Result (lane OG, N1 part 2)

- N1 part 2 (lane OG, 2026-10-27): **cResourceNode as a whole class**, the first class derived from our cSGResource.
  `src/sg/cResourceNode.{h,cpp}`, group cResourceNode: 70 verified entries (~3.9 KB). The factory 0xa503a0 is hooked to
  `Create` (the switch), and the class-id static 0xa50400 (`mov eax, 0xe519c933`, called by cSceneGraphSystem's Init)
  to `GetClassId`. The class's own bodies, the template levels' bodies of this instantiation, the 8 adjustor thunks and the
  21 vtordisp thunks are verified unhooked: the bodies in effect through callers (trapped), the thunks per
  docs/verification.md. `class cResourceNode : public cCompositionTreeNodeT<cIResourceNode, cCompositionTreeNodeImpl,
  cResourceNodeBehaviorTraits>, public cSGResource`, members mSource +0x54 (counted) and mWord +0x58; MSVC gives the
  game's layout from the declaration (static_asserts). Interfaces: cIResourceNode (kIid 0xe519c92c, slots 61-64) and
  traversal slot 12 `VisitResource`; writer slot 27 `WriteFlaggedObject` (method fact). Tests:
  tests/sg_resource_node.cpp (6: Factory 4, ClassId 1, Slots 64 x 20 ops, Persist 64, Clone 64, Events 48). Mutants:
  41, all killed (the main status entry lists them). Original calls left: the Impl's CloneInto 0xa384f0 (ours since lane OR), and
  the iterator builders the template calls (slots 43 / 44 / 51, untested).

### What bit lane OG

  - cSGResource's flags are two bytes at its part's +0x28 / +0x29 (cResourceNode +0x50 / +0x51); the next two bytes are
    padding that neither side writes, so they hold heap leftovers and differ between runs. Skip that word and note its
    low half (tests/sg_resource_node.cpp `kSkipWords`, word 20). Every cSGResource-derived node has the same padding.
  - `callers.py --write-traps` changes src/hook/trap_table.cpp, which leaves the ts2host build stale; `status.py --check`
    and `callers.py` then report problems in unrelated groups (cWorldDB) until ts2host is rebuilt
    (`. .\tools\common.ps1; Invoke-Build ts2host`).
  - The behaviour's missing-event record (each instantiation's `kMissingAttribute`, 0x106b328 here) is reached only for
    an event the registry lacks, and `sgfx::kRegistryIds` registers 0x1234 as well, so asking for 0x1234 never reads
    it: ask for an unregistered id (the Events test uses 0x5678). The record is image data the harness maps without running any
    initialiser (likely zeros there), so a wrong record address may read the same bytes: the test also fills
    it with a pattern and restores it after (tests/sg_resource_node.cpp `MissingRecordScope`).
  - The original Write's eax gap (above) applies to cResourceNode's Write 0xa50c00 too: ours is void, the original
    leaves slot 10's (SetUint32's) answer.

## cPartitionNodeImpl / cPartitionNodeT / cLooseOctreePartitionNode

### Result (lane OI, N1 part 3)

- N1 part 3 (lane OI, 2026-10-27): **the partition level and cLooseOctreePartitionNode as a whole class.**
  - **cPartitionNodeImpl** (`src/sg/cPartitionNodeImpl.{h,cpp}`, group cPartitionNodeImpl): 0x60 bytes, the bounded
    Impl's state plus a type record ("cPartitionNode", version 3, iid 0x1c8e36aa). Constructor 0xadfe10, IsKindOf
    0xadfe40, GetTypeInfo 0xadfe60, Write 0xadff10 hooked and verified (tests/sg_partition_node_impl.cpp, 5 tests);
    its destructor 0xadfe30 is a 5-byte jump to cBoundedNodeImpl's and its Read is 0xa4bd60, folded with
    cExplicitBoundNodeImpl's (verified there).
  - **cPartitionNodeT** (`src/sg/SGNodeTemplates.h`): the bounded level plus flag 6 in the constructor, QI for
    cIPartitionNode (shared 0xa4ef20), Accept / AcceptReadOnly through the traversal's slot 13 `VisitPartition`
    (shared 0xa4f700) and slot 80 (shared 0xa4ee60: RemovePartition, then false). Interfaces: cIPartitionNode (slots
    77-88) and cILooseOctreePartitionNode in `src/sg/SGNodeInterfaces.h`.
  - **cLooseOctreePartitionNode** (`src/sg/cLooseOctreePartitionNode.{h,cpp}`, group cLooseOctreePartitionNode): 73
    entries. The factory 0xa51ab0 is hooked to `Create` (the switch); the class's own bodies (slots 25, 60, 71-75,
    77-88, the +8 face's Read / GetTypeInfo / Write, the cell walks, the pool helpers) and the instantiation's template
    levels (constructors, destructors, deleting destructors, Release and the event copies) are verified in effect
    through callers; the class-id static 0xa51b10 too (its one caller is the COM director's constructor, ours); 3
    adjustor thunks. Tests: tests/sg_loose_octree_partition_node.cpp (8: Factory, ClassId, Slots / Persist / Clone /
    Events through the transform pilot at level 3, Content 96 x 40 and ContentLong 24 x 120 over content-node and
    sub-partition fakes, both pools in test regions). Original calls: nGZCompGeom only (16 functions, tentative names
    in Ghidra: the rigid-transform helpers, box containment, the cuboid boxes, the slab test, the 4x4 product, the
    frustum and the translated cuboid shape), plus the tree code and iterator builders through the shared wrappers.

### What bit lane OI

  - nGZCompGeom's rigid-to-matrix 0x19c1c0 writes a full 4x4 matrix (0x40 bytes, the translation in the last column),
    not an AffineTransform3 (0x34): a buffer sized for the latter is overrun on our stack and the run dies later in
    unrelated code. Size out buffers to what the callee writes (read the callee, not its caller's local).
  - The translated cuboid shape's constructor 0x18fc10 stores half the extents it is given: the octree passes twice
    the loose half edge. Don't "simplify" the factor away.
  - Slot 75's first-hit query hands cILinear3's FirstHitCuboidIn (slot 28) an uninitialised local for t (an out only):
    a fake that logs the pointee logs stack garbage that differs per side. Log it as a pointer (`'i'`).
  - The octree's constructor 0xa50ef0 never writes the half size (slot 88 does): a fresh node holds what the allocator
    left, so the pilot skips those words; the content tests set the size first.
  - Each std::vector member's three words are heap addresses: skip them and note sizes, capacities and elements.
  - The level constructors' EH unwind funclets (0xd724e0-0xd72560) and the deleting-destructor cycles need
    `callers_handled` in the status entries, as lane OG's (db/status/cLooseOctreePartitionNode.jsonl shows each).
  - Random boxes leave a loose octree sparse: one item per cell, and rays that cross at most one populated child. Ten
    mutants survived that (the item run order, the fit test's `>=`, the radius's sum order, the ray's sort, direction and
    stop). What killed them (tests/sg_loose_octree_partition_node.cpp): boxes gathered at anchors a cell boundary apart,
    at the anchor's depth or anywhere on its path; boxes nudged by ulps onto the fit test's boundary; lines through two
    anchors from outside the octree (every child starts past t 0), now and then axis-aligned (ties on entry); a nearer
    visitor that can set t to 0. The ray walk's stop is only visible through items held directly in a later child's cell:
    its descendants start further along and are filtered anyway.
  - Cull's plane mask: classifying a box against a full mask (0x3f) tests no plane and returns -1, and a child's loose
    box lies inside its parent's (3/4 of the parent's half extent from its centre), so skipping the test under a full
    mask, or handing children the parent's mask, only saves work: those two mutants are equivalent.

## cSceneNode

### Result (lane OK, N1 part 4)

- N1 part 4 (lane OK, 2026-10-28): **cSceneNode as a whole class.** `src/sg/cSceneNode.{h,cpp}`, group cSceneNode:
  88 verified entries (~7.7 KB). `class cSceneNode : public cPartitionNodeT<cISceneNode, cPartitionNodeImpl,
  cSceneNodeBehaviorTraits>, public cSGResource` (0xec bytes: the resource part at +0x6c, mInQuery +0x98, six
  std::vectors +0x9c-+0xe0 (sub-partitions, nodes, their two free lists, the two pending queues), the virtual base at
  +0xe8; static_asserts). The factory 0xa4e9b0 is hooked to `Create` (the switch) and the class-id static 0xa4ea10 to
  `GetClassId` (0x25232b11, the id Init registers). Everything else is in effect through callers with the factory's trap:
  the class's own bodies (QI, IsKindOf, Accept, the bound slots 71 / 72 / 75, the lists 77-87, the scene slots 88-96,
  the +8 face's Read / GetTypeInfo / Write, the resource face's GetRefCount / GetOwnerInfo), the instantiation's
  template levels, 7 adjustor and 9 vtordisp thunks, and the partition level's bodies the octree shares (slot 80
  0xa4ee60, QI 0xa4ef20, Accept 0xa4f700, IsKindOf 0xa4f6d0, the partition part's Write 0xa4fbd0: traps on both
  factories, `Hooks.nGZSceneGraph_cLooseOctreePartitionNode_Create=1&Hooks.nGZSceneGraph_cSceneNode_Create=1`).
  Interfaces: cISceneNode (slots 88-96: GetResource twice, the four scene queries, FlushPending, QueueNode 95, UnqueueNode 96)
  and cISceneGraphSystemPrivate slots 13 / 14 (AddSceneNode / RemoveSceneNode; method facts db/methods/SGSystem.jsonl).
  Tests: tests/sg_scene_node.cpp (10: Factory, ClassId, EventRecord, Slots / Persist / Clone / Events through the
  transform pilot at level 3 with roots only, Resource 64 seeds over the +0x6c face and the virtual base, Content 96 x
  40 and ContentLong 24 x 160 over the octree test's kind of content and sub-partition fakes plus a strategy, nearer
  visitor, filter and viewer). Mutants: 46, all killed (the factory entry lists them). Original calls left: the tree code (0xa39710 directly; 0xa39ae0,
  0xa39b70 and the iterator builders through the shared wrappers). Not counted: the virtual base's slots 7-17 and 20-24
  (vtordisp thunks folded with nTSSG::cHeightMapShadowLight's), shared_vf_a4c3b0 (Read's callee, folded). Status gap:
  15 entries (the destructor ~cObjectGraphNodeT 0xa4e020 and what hangs off it: 0xa4dfb0, 0xa4e3c0, 0xa4e420, the event
  copies 0xa4e560-0xa4f440, the adjustor 0xa4f165) show as open in `status.py --check` only because FreeType's
  af_blue_strings data in ts2host holds the bytes 20 e0 a4 a0 (Pitfalls, "What bit lane OK").
- cSceneNode's compaction tie (lane OP, 2026-10-28): SG_SceneNode_CompactTie (tests/sg_scene_node.cpp) runs the flush
  at the threshold's float ties (3 free of 20 and 6 of 40, where `free * 6.6666665f` equals the size and nothing is
  compacted; 4 of 20 and 7 of 40, compacted). It kills the coordinator's surviving mutant `>` -> `>=`.

### What bit lane OK

  - A scene node answers slots 31 / 32 with itself, so the tree code treats every scene node as the root of its own
    scene. Nest one under another and the original asks the wrong scene for slot 96 (UnqueueNode): its erase then runs
    on a queue that lacks the node and copies past the end (heap corruption in the memcpy 0x8264a0, seen at 0x8264ee).
    Keep scene nodes as roots in a test (`kRootsOnly`).
  - Flipping flag 1, 6 or 13 on a queued scene node from outside breaks the queue's invariant: the destructor's loop
    (`while queue not empty: 0xa39710(front)`) never ends, or the erase runs past the end as above. Leave those flags
    to the class (`kFixedFlags`). A hung difftest burns a core until killed: kill it by command line.
  - The +8 face's Read reads the cSGResource record between the class's record and the partition level's (Cfg's
    ReadResource in the pilot); the class's type record is version 4 and named "cSceneNode".
  - The deleting destructor lives in slot 97 (the interface ends at 96); slots 88 and 89 both hold 0xa4e540 (GetResource:
    the virtual base).
  - The functions 0xa4ead0 / 0xa4eac0 named as GetRecord18 / 19 in early surveys are not function starts: they are the
    bodies inside the vtordisp thunks 0xa4eac6 / 0xa4eaad (the thunk falls through). Status entries are on the thunks.
  - `status.py --check` / `callers.py` read our built ts2host's data too: FreeType's af_blue_strings (ts2freetype
    autofit.obj) holds the UTF-8 bytes 20 e0 a4 a0 (a space and Devanagari U+0920), which read as the game address
    0xa0a4e020 (cSceneNode's ~cObjectGraphNodeT). callers.py records it as a `bin:` reference of kind src, which
    `callers_handled` cannot answer, and its address moves with every build (0x8309e4, then 0x831f34). It holds 15
    entries open (Done above). The fix is in the tools (skip data hits inside third-party objects, db/third-party.jsonl),
    not in the status file. Any class whose functions' addresses spell UTF-8 text may meet it.
  - UpdateNode (0xa4eda0) pushes a removed node's index onto the sub-partitions' free list, not the nodes' (0xa4ee17):
    reproduced, `// ORIGINAL-BUG:` in src/sg/cSceneNode.cpp. A content test that removes nodes and then adds
    sub-partitions sees it.

## cSceneGraphSystem

### Result (lane OM, N1 step 4)

- N1 step 4 (lane OM, 2026-10-28): **cSceneGraphSystem as a whole class** (`src/sg/cSceneGraphSystem.{h,cpp}`, group
  cSceneGraphSystem, 25 verified). Our class (0x40 bytes): cISceneGraphSystemPrivate at +0 (slots 3-14, method facts
  db/methods/SGSystem.jsonl, tentative names), rz::cRZSystemService at +4, the std::set<cISceneNode *> at +0x20 (our
  toolchain's set: the game's node layout, so the set's tree compares node by node), the factory face +0x28, render
  groups +0x2c, display list +0x30, file parser +0x34, material parser face +0x38, LOD store +0x3c; and its iterator
  cSceneNodeIterator (12 bytes from the pool 0x10f617c, the cConnectionIterator pattern). The factory 0xa4cb40 is the
  switch; the constructor, destructor, QI (and its two thunks), GetSceneNodes, the getters, Add / RemoveSceneNode, Init,
  Shutdown, OnTick and the iterator's own bodies are in effect through it. What reads the set: only the system itself
  (slot 3, GetSceneNodes, hands out iterators over it; nothing else in the image touches +0x20). Init registers 19 node
  classes with the scene graph factory (cSceneNode's and cResourceNode's class ids are ours, the 17 others original
  statics), sets up the string pool, the four COM objects, the material parser (its constructor 0xacec90 original) and
  the file parser, and adds the system to the tick; Shutdown undoes it and advances the resource manager's cache
  generations (slot 45). Tests: tests/sg_scene_graph_system.cpp (SG_SceneGraphSystem_Set, _Service, 48 seeds each:
  each side builds its own system and drives it through its faces; the singletons, the factory static 0xa4d660, the
  parser constructor and the COM getter stand in on both sides). Mutants: 30, all killed. Original calls left: the
  factory static 0xa4d660, the singletons 0xacc040 / 0xade2e0 / 0xadf8b0, cMaterialParser's constructor 0xacec90 and
  17 class-id statics (Init's entry lists them). sgfx: the framework's AddToTick / RemoveFromTick (9 / 10) and the
  resource manager's slot 45 are settled.

### Survey and facts (lanes OG, OK)

4. **cSceneGraphSystem**: done (lane OM, Done above). Left for it: the 22 original calls (Init's entry), each moving
   when its class is ours (the factory static with cSceneGraphFactory, the singletons with M1 / TS1, cMaterialParser).
   The survey it was written from (kept for the RVAs):
   (0x40 bytes; vtables 0xe6c474 +0 (15), 0xe6c4b4 +4 (13), 0xe6c4ec +8 (6); factory 0xa4cb40 ->
   ctor 0xa4c6f0; 34 functions, 21 verified, 13 to do): QI 0xa4d060 (+ thunks 0xa4d09d / 0xa4d0a5), vf3 0xa4d0c0 (->
   0xa4c820, 0xa4c930), the singleton getters 5 / 6 / 8 (0xa4d030 -> 0xacc040, 0xa4d300 -> 0xade2e0, 0xa4d2f0 ->
   0xadf8b0), vf13 0xa4ca40 (a std::map insert), vf14 0xa4d0b0 (-> 0x17bc20), +8 slot 3 0xa4c9d8 (-> 0xa4c9e0), Init
   0xa4cbb0 (1151 B), Shutdown 0xa4d190 (337 B), OnTick 0xa4d040. Init's 27 callees: the singletons' getters / builders
   (0xacc040, 0xade2e0, 0xadf8b0, cMaterialParser's ctor 0xacec90, 0xa4d660), what look like the class-id statics of
   the node classes it registers (0xa50400 is cResourceNode's, hooked and verified by lane OG: `mov eax, 0xe519c933`; 0xa4ea10, 0xa47d50 / 0xa47d60 /
   0xa47da0 / 0xa47db0 (in the lights' range), 0xa5d410, 0xa66080, 0xa70b10, 0xa80020, 0xa839b0, 0xa85110, 0xa86c70,
   0xa8f2f0, 0xa925d0, 0xaadf80, 0xab1660 / 0xab1670, 0xa425f0; 0xa4ea10 (0x25232b11, in cSceneNode's range: check
   against its GetTypeInfo), 0xa5d410 and 0xa425f0 are checked 6-byte `mov eax, id; ret` statics too, the rest
   unchecked), the director framework getter 0x1ae18d, operator new 0x824632 and 0x14323f. Keep them all as
   original calls (the brief: the singletons stay original); a test needs the framework / director fakes that record
   the registrations. Init is the bisect switch for the singletons and cMaterialParser (Conventions), so hooking it
   moves them all at once.
   What lane OK learned about it (cSceneNode's constructor and destructor call it): a scene node finds it through the
   framework's GetSystemService (service 0xe5074b6b, iid 0x894978cd, the interface cISceneGraphSystemPrivate generated from
   db/methods/SGSystem.jsonl, a tentative name). QI 0xa4d060 answers 0x25074b4d and 0x894978cd with the main (+0) face and
   hands every other iid to the cRZSystemService part at +4. Its slot 13 (0xa4ca40,
   `AddSceneNode`) inserts the node into the std::set at +0x20 (the plan above said map: it is a set of node pointers),
   slot 14 (0xa4d0b0, `RemoveSceneNode`) erases it (-> 0x17bc20, the tree erase). The node's constructor calls 13 with no
   null check on the system (ORIGINAL-BUG in src/sg/cSceneNode.cpp), the destructor calls 14 only when there is one.
   sgfx's system fake records 13 / 14 (tests/sgfx.h `Resources::sceneNodes`, slot 13's argument as `W0`: the node is
   not named yet when its constructor calls). What the set is read by (OnTick 0xa4d040? the renderer?) is not checked:
   read it before writing the class. The class-id static 0xa4ea10 that Init calls is cSceneNode's (0x25232b11, hooked).

### What bit lane OM (the system)

  - The system's base constructor 0x1b9584 stores the running flag as a byte (`mov byte ptr [edi + 0x18], 0`): the
    word's upper bytes are whatever the heap held, so a word comparison of the object masks them.
  - The factory static 0xa4d660 can answer null (the constructor checks it); the factory face is reached through a
    vbptr (+0x10, entry 2) and its cIGZUnknown through the face's own vbptr: a fake factory needs both vbtables, or
    the AddRef lands in the fake's vtable words.

## cSceneGraphFactory and cIStreamStringBehaviourAdaptor

The game's resource read path is `cSceneGraphFactory::Read` (0xa4d750), which wraps the record's stream in a
`cIStreamStringBehaviourAdaptor` (Milestone N, "The adaptor").

#### N1 rest result (lane RC, 2026-10-30)

cSceneGraphFactory (`src/sg/cSceneGraphFactory.h`, 0x40 bytes) and the 16 class-id statics Init called are ours; the
group is `db/status/cSceneGraphFactory.jsonl`, the tests `tests/sg_scene_graph_factory.cpp`.
- **Layout.** Primary part at +0 (vtable 0xe6c568: refcount +4, lock +8), vbptr at +0x10 (vbtable 0xe6c5cc), option14
  +0x14, option18 +0x18, an unused word +0x1c, writer source +0x20, reader +0x24, writer +0x28, the virtual cIGZUnknown
  at +0x30, the cISceneGraphFactory face at +0x38 (8 slots, vbtable 0xe6c5d8 with two entries). The compiler lays it
  out (virtual inheritance); `SG_SceneGraphFactory_Layout` compares the words of both constructors' objects.
- **Hooked and verified:** the getter 0xa4d660 (global 0x10f61b0), the creator 0xa4d600 (the COM director's class
  creator for 0xe5259c1b; hooked by address, the coordinator wires it from COMDirectorData), the 17 class-id statics
  (6 bytes each), and two O-adapter bodies (cOStreamStringBehaviourAdaptor, vtable 0xe6c5e4: Flush 0xa4d5f0 and the
  deleting destructor 0xa4d510).
- **In effect through callers** (trap `Hooks.nGZSceneGraph_GetSceneGraphFactory=1`): the constructor 0xa4d310, the
  destructor body 0xa4d3e0, the slots, Read 0xa4d750, Write 0xa4da90, the face slots and their adjustor thunks.
  0xa4d5b0 (3 bytes, `GetResourceFactory`) has no entry: under 5 bytes, nothing to trap.
- **Findings.** CreateResource (slot 9) returns the low byte of the COM's raw word, not a bool (a mutant on it is
  killed). The original never null-checks the resource manager in RegisterClass, the class object in GetBinaryWriter,
  the serial in Write, or the created resource in CreateInstance: the test scripts keep clear of those (they crash the
  original).
- **Tests.** The script test runs seeded sequences of slot calls through the three faces on the original's own object
  and on ours, every callee a recording fake, and compares the log and the object's words. 31 mutants run, none of the
  valid ones survived (one did not build and was replaced by two creator mutants).
- **render_object gate.** Image references left in the linked render_object.exe: 149 (lane RB's figure) -> 134; the new
  O-adapter vtable 0xe6c5e4 is among them until it is wired in standalone/owned_statics.cpp (with the `/alternatename`
  for e6c5e4 and the 10f61b0 definition).

## Tree code: SGTreeCode, the slot-51 builder, the pre- / postorder iterators, the world-transform refresh, CloneInto / cNodeCloneContext

### Results (lanes OM, OP, OR)

- N0 part 3 step 4 (lane OM, 2026-10-28): **the tree code** (`src/sg/SGTreeCode.{h,cpp}`, group SGTreeCode, 8 verified):
  NodeConnected 0xa396d0, NodeDisconnecting 0xa39710, NodeTransformChanged 0xa39690 (the jump stub 0xa39700 needs no
  entry) and NodeBoundFlagChanged 0xa39ae0 hooked; FindPropagationRoot 0xa39620 and the recursions PropagateConnected
  0xa39740, PropagateDisconnecting 0xa39820 and PropagateTransform 0xa39920 in effect through them. Every src caller
  calls ours (the `game_code::BoundFlagChanged` / `TransformChanged` wrappers are gone; the octree, cSceneNode, the
  Impls and the templates call `NodeBoundFlagChanged` / `NodeTransformChanged`). The flags it reads are in the header
  (1 bounded, 6 partition, 7 stops the root climb, 12 stale, 13 queued). Tests: tests/sg_tree_code.cpp (one per entry
  and SG_TreeCode_Mixed, 64 seeds each, on a tree of node fakes). The transform pilots and the cSceneNode / octree
  tests, which ran the game's tree code on both sides, now run ours on our side.
- N0 part 3 step 4, the slot-51 builder (lane OM, 2026-10-28): **the empty attribute-entry range and its null iterator**
  (`src/sg/cNullArrayAttributeEntryIterator.{h,cpp}`, group cNullArrayAttributeEntryIterator, 4 verified):
  MakeEmptyAttributeEntryRange 0xa37d80 hooked (every composition-family node's slot 51; our
  cCompositionTreeNode::MakeIterator51 and node_family::BuildIterator51 call it); the iterator
  cRZNullRandomAccessIterator<cArrayAttributeEntryName> (8 bytes, game vtable 0xe68db0) has its Clone 0xa37630, Get
  0xa37720 and deleting destructor 0xa37080 in effect through the hook (only those three and the builder store the
  vtable); its other slots are folded bodies (QI, AddRef, Release, IsEqual, Next / Previous, Advance, At) we implement
  for our objects only. Test: tests/sg_attribute_entry_range.cpp (64 seeds of random slot calls and releases).
- N0 part 3 step 4, the iterators (lane OP, 2026-10-28): **the pre- / postorder tree iterators as whole classes**
  (`src/sg/cCompositionTreeIterators.{h,cpp}`, group cCompositionTreeIterators, 44 verified, 8823 B). One template
  `cCompositionTreeIteratorT<Order, kConst>` gives the four classes (0x9c bytes: vtable, count, filter, depth, 16
  inline {node, cursor} entries of counted pointers, a std::vector of deeper entries), each derived class with its own
  0x9c pool (0x10f5f54 / 60 / 6c / 78, the game's cRZFastFixedAllocator2 objects, reached through `GameAddress`) for
  the builders' blocks. The builders MakeConstTreeIterators 0xa37fd0 and MakeTreeIterators 0xa37e40 (slots 43 / 44)
  are hooked: they are the switch. Every other body (the template's ConstructEnd / ConstructBegin / CopyConstruct /
  Clone / Destruct / deleting destructor, the derived deleting destructors and pool operator new / delete, the shared
  slots IsEqual 0xa37760, Get 0xa37700, Next 0xa38a80 / 0xa38840, the entry helpers 0xa36ef0 / 0xa36990) is in effect
  through them; the pool frees reached from unwind funclets are `callers_handled`. OM's pool-vs-heap question: Clone
  takes plain `operator new 0x9c` and the template's deleting destructor frees with the sized delete, while the
  derived classes' deleting destructors (the slot the builders' objects carry) return the block to the pool: each
  object is freed the way it was allocated, so no ORIGINAL-BUG. Our callers (cCompositionTreeNode,
  cTransformNode, node_family) call ours. Test: tests/sg_tree_iterators.cpp (SG_TreeIterators: trees of 1-40 node
  fakes, deep chains past the 16 inline entries, both orders, const and not, filters answering 0 / 1 / 2 per node,
  clones mid-walk, IsEqual across iterators, releases in random order; the four pools' pages are noted). Mutants:
  28, 27 killed; the survivor (dropping cTreeNodeRef::Assign's same-pointer check) is equivalent: no reachable
  assignment is of the same non-null pointer (the builder entries say why).
- N0 part 3 step 4, the refresh (lane OP, 2026-10-28): **the world-transform refresh** (`src/sg/SGWorldTransformRefresh.
  {h,cpp}`, group SGWorldTransformRefresh, 5 verified, 1520 B). RefreshWorldTransforms 0xa39b70 is hooked; the walk
  0xa39a10 and the three list appends 0xa39430 / 0xa39500 / 0xa39590 are in effect through it. The work table's atexit
  destructor 0xd95bd0 (271 B) is held at mapped: only 0xa39b70's first run registers it, so nothing reaches it now,
  and no differential test reaches an exit handler. Our work lists are a function-local static of ours (three std::vectors of 32-byte {word, transform}
  records; the limits reserve 50 like the game's), shared across re-entrant calls as the game's are (a content node's
  slot 60 may call the refresh again: the inner call appends to and clears the same lists, and the outer loops re-read
  the sizes). The semantics are in the header. Every src caller calls ours (cBoundedNodeImpl's RefreshIfStale,
  cSceneNode's flush; the `game_code::RefreshWorldTransform` wrapper is gone). Test:
  tests/sg_world_transform_refresh.cpp (SG_WorldTransformRefresh: 160 seeds of 1-40 node fakes with random flags 4 /
  12, limits 0 / 0x7fffffff / small, chains and early range ends; a quarter of the seeds re-enter from slot 60).
  Mutants: 20, all killed.
- N0 part 3 step 4, CloneInto (lane OR, 2026-10-28): **the composition Impl's CloneInto 0xa384f0 and the anonymous
  cNodeCloneContext** (`src/sg/cNodeCloneContext.{h,cpp}`, group cNodeCloneContext, 5 verified). CloneInto is hooked
  (a `__fastcall` free function `CloneCompositionInto`); the context's FindClone 0xa37980, QueryInterface 0xa38ce0 and
  its +8 thunk 0xa38d08 and the deleting destructor 0xa371d0 are in effect through it (only CloneInto builds the
  context). Our callers (cCompositionTreeNode::Clone, cResourceNode::CopyContentTo, cTransformNodeImpl::CloneInto) call
  ours. The context is 0x30 bytes, a std::unordered_map from source to counted clone with the game's identity hash
  (Pitfalls). Test: tests/sg_clone_context.cpp (SG_CloneContext, 160 seeds: child fakes whose slot 15 calls the context
  they are given). Mutants: 19, all killed.

### What bit lanes OM and OP

  - The tree code's "scene" is the root's slot 32, handed down to every node of the subtree (the children's own slot
    32 is never asked); NodeBoundFlagChanged asks the node itself. Slot 77 on a partition (flag 6) is Clear: a
    connected or moved partition is emptied (what refills it was not traced).
  - The tree code is cdecl and recursive; FindPropagationRoot climbs only past nodes whose limit (slot 55) is not 0
    and returns the node itself when the climb runs out of parents or the found ancestor has flag 7: a test tree needs
    limits of 0, 0x7fffffff and small values, and parents several levels up, to reach each way out.
  - Tests that ran the game's tree code on both sides (the transform pilots, cSceneNode's, the octree's) now compare
    the game's against ours: a failure there after a tree-code change is real. The first such failure was the fixture's:
    the pilot's holder fake (tests/sg_transform_pilot.h) declared slot 78 (RemovePartition) without its argument, so
    it popped nothing; the original's frame-pointer code survived the extra 4 bytes, ours (frame pointer omitted)
    crashed later in SG_LooseOctree_Slots. A fake slot's argument count must be the real callee's `ret N`
    (`tools/arity.py --rets`), not the decompiler's call display.
  - The slot-51 null iterator's Get reads a static (0x10f5f90) that only looks like data: it is bss and nothing writes
    it, so it is the zero entry. Most of the iterator's slots are folded bodies (`mov al, 1; ret 4` IsEqual, the
    shared QI / AddRef / Release): once the builder is hooked only its three own bodies are claimable.
- **What bit lane OP** (the iterators, the refresh):
  - A preorder iterator whose root the filter answers 2 (skip the branch) still walks the root's children: the begin
    constructor steps with Next, and Next pushes children before it asks the filter. Postorder descends through
    HasChildren and the first child; a fake answering HasChildren true with no first child crashes the original. Next
    at the end iterator is undefined (it pops the sentinel): tests stop at IsEqual(end).
  - Freed pool blocks keep the filter word, and each side's default filter is its own object (the game's 0x1069ffc,
    ours `DefaultBranchFilter()`): a test that notes the pools' blocks maps that word to one label per side.
  - The refresh's limit list starts at 50 records; past 50 the original reallocates and frees the old buffer, which
    under `sgtp::RefreshScope` is the scope's own memory. Tests that re-enter the refresh keep their trees at 24 nodes
    or fewer so the list never grows (tests/sg_world_transform_refresh.cpp says why).
  - Pool frees and destructors reached only from unwind funclets show OPEN in callers.py: record them under
    `callers_handled` with the funclet's address (`"code:0x..."`) and why it is ours.

## Template copies (F1): cAbstractBehaviorTypeT copies, node-level copies, SGBehaviorCopies, SGNodeCopies

Progress of lanes SV, SY and TA (2026-11-01); the F1 row it reports on is in the archive.

**F1 progress (lane SV, 2026-11-01).** Done: the cAbstractBehaviorTypeT copies' bodies, 153 of them (19.9 KB, all
verified): Release, OnEvent, NotifyListeners, AskListeners, Connect, Disconnect, GetEventAttribute and the cdecl
GetEventManager for the 20 interfaces (the row's "dtor 118" is GetEventManager, the cell getter; the row's 9.1 KB
undercounted). src/sg/SGBehaviorCopies.h: cBehaviorCopy<deleting slot, cell, registry> adapts our
cAbstractBehaviorTypeST over a face padded to the copy's deleting-destructor slot; the 20 cells are owned statics, the
five registries nobody owns yet (shape ref 0xa585a0, light ref 0xa633d0, viewer ref 0xa68db0, display list 0xaa2210,
camera effects 0x361720) are OriginalFunction calls; the missing record is gMissingRecord. Not hooked: the geometry
copy's Release / Connect / Disconnect / GetEventAttribute (cGeometryNode's), 0xa46ec0 (GetEventAttribute shared by the
draw-state, fog and texture lights' copies, all game vtables), slots 12 / 13 (folded across families). The object-graph
copy's registry is 0x264010 (not 0x25f030). Tests: tests/sg_behavior_copies.cpp.
**F1b part 1 (lane SY, 2026-11-01): 60 verified, 2.6 KB.** The game's complete constructors of cTransformNode
(0xa3a000) and cCompositionTreeNode (0xa36900) are hooked (src/sg/SGNodeCopies.h: an adapter builds our class in the
memory given; tests/sg_node_copies.cpp, in place against the game's constructor, plus the transform pilot over the
adapter). With those and lane SP's shape-ref constructor / destructor hooks, nothing stores the game's vtables of the
three instantiations, so 58 copies are `in_effect: callers` in db/status/SGNodeCopies.jsonl (tests: the three in-place
tests, whose original side runs the game's chains; level-only slots through the class tests):
the BT / OG / CT / TN (/ BN / RN / SR) constructors, level destructors, deleting stubs, slots and Release thunks. The
destructor <-> deleting-stub references and the 0xd7xxxx unwind funclets are `callers_handled`. Each note says whether
the copy runs on the original side of the class tests (code-reached) or is held only by a level vtable.
Findings: the "cWorldTransformWork" 0xa392e0 and 0xa35110 / 0xa35300 / 0xa39030 / 0xa39160 / 0xa391f0 / 0xa39250 in the
iterator range are STL (vector reallocate, hash rehash): library code, left. The explicit-bound base 0xa4b500 is not
switched: nTSSG::cExplicitShearBoundNode's constructor 0x3cffc0 calls it and then lays its own vtables, so a switch would
put a game-derived class over our base and its copies would stay reachable. Shape ref: cScreenShapeRef (0xc09820, vtables
0xe91708) still holds 0xa55230 (AskListeners) and 0xa58850 (NotifyListeners), and the original 0xa586e0 reaches Release
0xa586d0 / 0xa586d5; those, the "3 classes share it" bodies and 0xa54130 / 0xa54160 / 0xa541a0 (called from original
0xa55f00 / 0xa54fb0 / 0xa55830) stay open. The class-id getters 0xa3a720 / 0xa37970 (6 B) are covered only through the
hooked cDllCOMDirector and have no test: left.
**F1b (left):** the BT constructor / destructor stubs of the other interfaces; the light-ref, viewer, viewer-recursive,
renderable, geometry and shape instantiations (light ref and viewer classes are not ours: per-copy adapters as in SV;
cGeometryNode and cShape are ours: check their constructors' callers for the same switch); explicit bound (needs
cExplicitShearBoundNode first); the postorder iterator's non-STL bodies and traversal stubs; the light-ref, viewer and
viewer-recursive Impls' constructors and destructors.
**F1b part 2 (lane TA, 2026-11-01): 4 verified (hooked), 0.5 KB.** The light-ref Impl's constructor / destructor 0xa60c20 /
0xa60ec0 (src/sg/cLightRefNodeImpl.{h,cpp}: the renderable Impl plus the light +0x70, a reference +0x74, -1 +0x78, a flag
+0x7c and the configuration service's "lightBoundsInflate" as a float +0x80) and the viewer-ref Impl's 0xa644c0 / 0xa64840
(src/sg/cViewerRefNodeImpl.{h,cpp}: the viewer base plus a vector of 0xcc-byte entries +0xe8, each holding a vector of
ShapeMaterialHandle at +0xb4 (the game's element destructor is 0xae8870, the handle's), three words, a scale 1.0 +0x100).
Tests: SG_NodeCopies_LightRefImpl / SG_NodeCopies_ViewerRefImpl. Findings, for the next lane:
- Postorder iterator: nothing non-STL is left (0xa35110 / 0xa35300 / 0xa39030 / 0xa39160 / 0xa391f0 / 0xa39250 are STL;
  0xa37970 is 6 B). 0xa64780 (the viewer entries' vector tidy) is STL too.
- Geometry (trap Hooks.cGeometryNode_Construct=1) can go `callers`: 0xa3dd00 / 0xa3dd10 clean; 0xa3e110 (handle 0xa3e153,
  0xa3e6a3, funclet 0xd71813), 0xa3e120 (0xa3e6c3, 0xd71843), 0xa3e6a0 / 0xa3e6c0, 0xa433e0. Not: 0xa43090 / 0xa43095 /
  0xa430b0 (nTSSG geometry classes), 0xa3e6e0 (cShapeGeometry's vtable 0xe6987c holds it).
- Shape (trap Hooks.nGZSceneGraph_cShape_Create=1): 0xa5baa0 / 0xa5bab0 / 0xa5bb10 / 0xa5bb30 clean; the destructors
  0xa5bf20 (0xa5bf63, 0xa5c453, funclet 0xd72b83), 0xa5bf30 (0xa5bf7d, 0xa5c473), 0xa5bf70 (0xa5c493, funclet 0xd72bee)
  are cycle sites / funclets only; then the level vtables' slots (0xa5c450 / 470 / 490, 0xa5cba0, 0xa5cef0, 0xa5d370,
  0xa5e230, 0xa5f140 / 145, 0xa5f4c0, 0xa5f4f0) should clear. Both need their status test to name the RVAs: the natural
  tests are tests/sg_geometry_node.cpp and tests/sg_shape.cpp (their worlds), so a lane owning those files records them.
- Viewer-recursive Impl (0xa69f70 / 0xa6a150, 0xa6a000 7 B; 0xa6a030 / 0xa6a070 its CT / OG level destructors) is
  polymorphic (own vtable 0xe71de4 at +0, the viewer base at +4, slots such as 0xa6a650 not ours): its slots first.
- Light-ref (0xa60920-0xa610e0) and renderable (cTSParticlesRenderable 0xbb48d0...) level copies and the BT stubs of
  those interfaces: their node classes are not ours, so no constructor switch; they need the classes (or per-copy
  adapters over a game-derived class). Explicit bound stays deferred until cExplicitShearBoundNode (0x3cffc0) is ours.

## Lights: cReferentNodeImpl, cLightT, cStandardLightBase, cAmbientLight, cDirectionalLight, cPointLight, cSpotLight, cLinearFogLight, cDrawStateLight, cEnvironmentCubeLight

Still unexplained: the game's cLightT is a template over (interface, int): 0 for cIStandardLight, 1000 cITextureLight,
2000 cILinearFogLight, 3000 cIDrawStateLight and one cILight copy, 4000 another cILight copy; what the int does is not
known (not the type-info record: cLightT's Write 0xa4ad50 is shared by all of them), and our template does not carry it
(lanes OR, OT). The nTSSG lights (cAmbientFromLightMapLight, cHeightMapShadowLight, cHeightMapBinaryShadowLight,
cHalfspacePointLight, cShadeLight) derive from the game's standard lights and keep their game bodies live until they
are ours (Conventions, "Classes the nTSSG lights derive from keep their game bodies live").

### Results (lane OR, N2 part 1; lane OT, N2 part 2)

- N2 part 1 (lane OR, 2026-10-28): **cReferentNodeImpl, the light templates, cStandardLightBase, cAmbientLight and
  cDirectionalLight.**
  - **cReferentNodeImpl** (`src/sg/cReferentNodeImpl.{h,cpp}`, group cReferentNodeImpl, 10 verified, hooked at their
    RVAs): the object-graph Impl plus a std::vector of referrers (raw cIAbstractBehavior pointers; 0x14 bytes, vector at
    +0x8): Construct 0xa446b0, Destruct 0xa44710, AddReferrer 0xa44770 (push_back, duplicates allowed), RemoveReferrer
    0xa44850 (the first match erased; the caller checks it is there), Ask 0xa447a0 (asks every referrer's slot 4, answers
    false whatever they say), Notify 0xa44880 (every referrer's slot 3 with the node as sender), Read 0xa447e0 / Write
    0xa449a0 (the record "cReferentNodeImpl" version 1, then the object-graph Impl's part), IsKindOf 0xa448d0,
    GetTypeInfo 0xa448f0. Our vector grows as the game's (capacities compared). Test: tests/sg_referent_node_impl.cpp (10,
    each over a world of referrer fakes). Mutants: 17, all killed.
  - **Templates** (`src/sg/SGLightTemplates.h`): `cReferentNodeT<I, Traits>` (the object-graph node on a
    cReferentNodeImpl: QI 0x3a5b50, NotifyListeners 0x3c9930 and AskListeners 0x3c87b0 through the referrers first, slots
    29-31) and `cLightT<I, Traits> : cSGResource, cReferentNodeT<I, Traits>` (QI 0x3a5a60, Read 0x3a5c30 / Write
    0xa4ad50 / type info 0xa4a610 "cLightT" version 1, IsKindOf 0x3a6460, record 18 "_lght"). Interfaces cIReferentNode
    (0x9c6ab984), cILight (0x9c5ed164) and cIStandardLight (0xea932e72): `src/sg/SGLightInterfaces.h`.
  - **cStandardLightBase, cAmbientLight, cDirectionalLight** (`src/sg/cStandardLight.{h,cpp}`, group cStandardLight, 2
    verified: the factories). The director's factories 0xa47ab0 / 0xa47b10 are hooked to `Create` (the switch): every
    ambient and directional light the director makes is ours (0x70 bytes; layout in Conventions). The game's class
    bodies are NOT claimed: the nTSSG lights derived from them (cAmbientFromLightMapLight 0x3c7d30,
    cHeightMapBinaryShadowLight 0x3c7e80, cHeightMapShadowLight 0x3c7fd0; cHalfspacePointLight 0x3c7de0 derives from
    the point light) call the game's constructors 0xa45d40 / 0xa45db0 and run the vtable bodies, so they stay live and
    keep no status until those nTSSG classes are ours (the factory entries list the bodies ours replace). Test:
    tests/sg_lights_standard.cpp (Factory 4, Slots 64 x 24 ops through all four faces, Persist 64, Events 48; each side
    builds ambient and directional lights at random with its own factories). Original call: the lights' event registry
    0xa49210. Mutants: 46 over cStandardLight.cpp and SGLightTemplates.h (`--force-filter`), 45 killed (2 after
    strengthening), 1 equivalent (CopyTo's mode, unused by the object-graph Impl).
- N2 part 2 (lane OT, 2026-10-28): **the point, spot, linear fog and draw-state lights.** Each is a whole class on
  `cLightT` with the director's factory hooked to its `Create` (the switch); notes and tests as lane OR's standard
  lights.
  - **cPointLight** (`src/sg/cPointLight.{h,cpp}`, group cPointLight, factory 0xa47c90, class 0xc9c81ba9, 0x90 bytes)
    and **cSpotLight** (`src/sg/cSpotLight.{h,cpp}`, group cSpotLight, factory 0xa47cf0, class 0xc9c81bad, 0xa4 bytes):
    cStandardLightBase plus falloff, range, strength and an embedded shape (`src/sg/SGLightShapes.{h,cpp}`: the sphere
    for the point light, the cone Fit for the spot, StrengthOverBox / StrengthAt, the radiance cutoff). OnLightChanged
    reads the "lightRadianceRangeCutoff" setting through the framework (service 0x0ac1887e / 0x4bb0d269, settings slot
    9), so these tests build the game's allocator service (`svcfx::AllocatorScope`, Pitfalls). Bit-exact against the
    original (libm tan / cos / sqrt match). cHalfspacePointLight (nTSSG, 0x3c7de0) derives from the point light, so
    the point bodies stay live; the spot light has no nTSSG derivation, its bodies are in effect through its factory.
  - **cLinearFogLight** (`src/sg/cLinearFogLight.{h,cpp}`, factory 0xa47c30, class 0xac06a66f, 0x68 bytes: near
    +0x4c = 10, far +0x50 = 100, colour +0x54 = white) and **cDrawStateLight** (`src/sg/cDrawStateLight.{h,cpp}`,
    factory 0xa47b70, class 0xac06a676, 0x58 bytes: two flag bytes at +0x4c / +0x4d, both 1). Constant strengths 2000 /
    3000, records "5lf" / "5ds", their own event-manager cells (0x10f6148 / 0x10f614c) and missing-attribute record
    0x106ab68; the shared event registry 0x3a5d00 is an original call of the fog entry (the draw state's sits in the
    same file). Interfaces cILinearFogLight / cIDrawStateLight and kDrawState = 6 in `SGLightInterfaces.h`.
  - Tests: tests/sg_lights_point_spot.cpp (Factory x2, Slots, Persist, Events; 64 seeds) and
    tests/sg_lights_fog_drawstate.cpp (the same shape). Original calls left: 0xa49210 (point / spot registry),
    0x3a5d00 (fog / draw-state registry). Mutants: see notes/feedback/OT.md.
  - **Not done:** cEnvironmentCubeLight and cLightRefNode (Left: plans below). Slot 51 `GetColor` returns its out
    pointer (a coordinator catch); setter and GetParams eax returns are unverified (callers seen so far ignore them).

### cEnvironmentCubeLight and the rests (lane TC, N2r)

**N2r, lane TC (2026-11-01).** Done: cEnvironmentCubeLight whole (src/sg/cEnvironmentCubeLight.h: texture handle
at +0x4c through the texture manager 0xae8750 (slots 17 FindOrLoad, 18 / 19 refs, 24 GetInfo), four RGBA colours at
+0x50, all 1; Read takes the colours only at version 2; the game's Clone copies the texture but not the colours, kept;
factory hooked, 58 entries with its three template levels); the draw-state (36), fog (39) and spot (30) rests in effect
through their factory hooks, the destructor <-> deleting-destructor cycles and the cube constructor's unwind funclet
0xd71de3 in callers_handled. 0xa46ec0 is `shared_vf` over 17 classes: owning these lights doesn't change it (no entry, no
hook). Left: cLightRefNodeT / cLightRefNode and its level copies (below); the point light, cStandardLightBase, ambient
and directional rests and the standard levels (0x3c9030 etc.), live while the nTSSG lights derive from them; the nine
SGLights spot hooks (fold into cSpotLight, then callers); the cLightT copies 0x3c7cb0 / 0xa45a40 / 0xa45ac0 (not looked at).

### The SGLights spot hooks and the cLightT copies (lane TL, N2r)

**N2r, lane TL (2026-11-02).** Done: (1) the nine SGLights spot hooks (resource-face slots 7 / 8 / 9 / 11 / 15 / 16 and
IStandardLight 47 / 48 / 50 / 39) folded into cSpotLight: they were a hook-shaped class duplicating bodies our cSpotLight
already has; the class, hook table, status file and test (SGLights) are gone, nine entries joined cSpotLight.jsonl as
in effect through callers (the factory 0xa47cf0 makes ours). SG_PointSpotLights gained identity ops (slots 7 / 8, 9, 11)
on both sides, which close the coverage the removed test gave. (2) The cLightT copies: 0xa45a40 is
`cLightT<cIStandardLight, standard behaviour>`'s constructor, called by cStandardLightBase's 0xa46250 only (the ambient,
directional, point, spot and, through them, the nTSSG standard-derived lights); 0x3c7cb0 is `cLightT<cILight>` (cell
0x10da038), called by the nTSSG shade lights' 0x3c80b0 / 0x3c8120 only; 0xa45ac0 is the cube light's, already ours.
Both are base-part constructors that game-derived classes still call, so they are hooked, not callers: like cSGResource's
0xa3c270 (lane OE) the hook is our compiler's own constructor of the template, its address through a linker
`/alternatename` C name (src/sg/cLightLevels.{h,cpp}, table src/hook/tables/cLightLevels.cpp). Two linker facts: an
/alternatename does not make the linker pull an object out of a static library (a `/include` of a real symbol in the
object does: `TL_cLightLevels_Anchor`), and an explicit instantiation emits only user-provided members (cLightT's
constructor was `= default`, now `{}`). Tests: SG_StandardLights_Level (the game's ambient and directional lights, 0xa45a40
patched on one side, the whole Slots operation set) and SG_ShadeLightLevel (tests/sg_light_levels.cpp: the game's
cShadeLight by its own constructor, 0x3c7cb0 patched). Left: the inner constructors 0xa45d00 / 0xa45c00 / 0x3a4000 (now
reached only from dead originals) could become callers; the standard lights' bases (cStandardLightBase 0xa46250) are game code.

### cStandardLightBase, cAmbientLight and cDirectionalLight whole (lane VW, N2r)

**N2r, lane VW (2026-11-03).** Done: the three classes are ours in the game (src/sg/cStandardLight.{h,cpp}, table
src/hook/tables/cStandardLight.cpp, db/status/cStandardLight.jsonl): 77 functions, 5,012 B, verified.
- **Constructors and destructors.** cStandardLightBase's 0xa46250 / 0xa467e0 and the leaves' 0xa45d40 / 0xa45db0 /
  0xa465f0 / 0xa46630 are hooked to our compiler's own (the `/alternatename:_X=??0cX@nGZSceneGraph@@QAE@XZ` pattern of
  cLightLevels). The game's lights built on the base (the point light 0xa46060, the nTSSG AmbientFromLightMap 0x3c7d30,
  the binary-shadow 0x3c7e80 / 0x3c7fd0 and HalfspacePoint 0x3c7de0 lights) now run our base constructor and destructor,
  so the three classes' image vtables are stored by nothing that runs.
- **Slot bodies those game lights still reach** (their own vtables keep the image's entries) are hooked to static
  `__fastcall` adapters (`cStandardLightAdapters`: ecx the face, +0 the part, +0x2c the node, +0x34 persist) calling the
  class's bodies: 40 hooks. 28 more bodies are in effect through callers (the copy 0xa47eb0, GetTypeInfo 0xa4ac70, the
  deleting destructors and their thunks, the persist thunks, the level constructors 0xa45d00 / 0xa45c00 / 0xa45920 and
  the 6-byte shared StrengthOverBox / StrengthAt 0xa49520 / 0xa49530, referenced only by this family).
- **The level destructor cycle.** 0xa463d0 / 0xa465b0 / 0xa464f0 / 0xa46310 and their deleting destructors store the
  level vtables only among themselves: callers.py sees a cycle. It is cut with `callers_handled` on the deleting
  destructors' code calls and the two unwind funclets (0xd71e63 of the hooked 0xa46250, 0xd71da3 of 0xa45c00), so the 9
  entries rest on the constructor and destructor hooks (`traps.py --sync` names them).
- **Kept the game's** (other classes still reach them; not hooked under the family rule): the shared Amb/Dir Read
  0xa48ce0 (nTSSG vtables 0xdbf128 / 0xdbf2e4 / 0xdbfaa4 and the jmps 0x3c8f1f / 0x3c8f2a / 0x3c8f35), 0xa4a610
  (shared_vf_a4a5f6, vtable 0xe6a0bc), 0xa4ad50 (shared_vf_a4ad3a, 0xe6a0bc), 0x3a59c0 (nTSSG / ProjectiveTexture /
  point thunks), and the point light's 0xa47440 / 0xa4a170 (the HalfspacePoint vtables).
- **Test.** SG_StandardLights_GameBuilt (tests/sg_lights_standard.cpp): 64 seeds; each side builds 1-5 lights of any kind
  (its standard lights, the game's point light by 0xa47c90, the three nTSSG lights by their game constructors), then 24
  steps (event ids, Read into a fresh light through either persist face, more lights, the Slots operations); every hooked
  RVA is patched to ours on the second side. The point light's OnLightChanged 0xa48330 asks the framework for the
  settings service (0x0ac1887e / 0x4bb0d269) and its slot 9 GetProperty "lightRadianceRangeCutoff": the test fakes it
  with a seeded cutoff, present or not. Strengthened in passing: the Slots strength op had read past two stack arrays
  (a real box and AffineTransform3 now), and the reader queue is reset per record.
- **Gate.** The render_object noimage gate stays at 23: the rig isn't linked into render_object. Linking the
  thumbnail's ambient and directional lights would need cStandardLight.cpp and its levels (SGLightTemplates, the
  referent node Impl, the event registry 0xa49210, cSGResource) in render_object's object list, and the thumbnail's light
  setup to call `cAmbientLight::Create` / `cDirectionalLight::Create` instead of the factories through the director.
- **Left.** The rest of cPointLight (constructor 0xa46060, destructor 0xa46740, the slot bodies 0x3c90a0, 0xa47300,
  0xa48330, 0xa48bc0, 0xa48f30, 0xa4a2d0, 0xa4aaf0, 0xa4b0e0, 0xa48190 and the copy 0xa47f50 as adapters; then 0xa46b40,
  0xa4686a, 0xa49540, its persist thunks and 0xa48186 through callers; 0xa47440 / 0xa4a170 stay the game's for the
  HalfspacePoint vtables): the same pattern, and GameBuilt already builds the game's point light. shared_vf_2c4080
  (21 B, in cRenderTargetTexture's vtable and nTSSG::cLightRigManager's 0xdb582c) may not be hooked: the two are
  unrelated classes, so the family rule doesn't apply.

### What bit lanes OR and OT

  - **One name, two bases.** A light is a cSGResource and a node: a method on the node's interface with the same name
    and signature as one of cISGResource's / cIGZPersistResource's is ONE final overrider for both in C++. cIStandardLight
    slot 49 was first declared `GetName()`, which silently replaced cISGResource::GetName (slot 17, the resource's own
    name): the Persist test showed it as the original calling the pool's GetText where ours wrote the light's name.
    Slots 40 / 49 are `SetLightName` / `GetLightName` now; check every new light interface's names against
    cSGResource's (QueryInterface / AddRef / Release, Read / GetTypeInfo / Write / GetOwnerInfo are meant to be shared).
  - **The cSGResource part is read and written twice** by every standard light: cStandardLightBase's Read 0xa49070
    reads the cSGResource record and name, then cLightT's Read 0x3a5c30 reads them again (Write the same, 0xa4b250 then
    0xa4ad50). A reader script needs both records (leaf, base, cSGResource + text, cLightT, cSGResource + text,
    referent Impl, object-graph Impl, then the light's own fields).
  - **Release order inside Write** is visible only through the frees: the type-info name each Write releases after
    WriteTypeInfo. tests/sg_referent_node_impl.cpp notes the frees so far at the writer's slots 10 / 23 to see it.
  - **The clone context's hash is the identity** (CloneInto's std::unordered_map hashes a node pointer to itself, not
    std::hash's FNV): bucket placement, rehash and so the destructor's release order follow it.
  - **A light's Clone ignores its context** and answers through the clone's QueryInterface: an iid the clone does not
    answer leaves `*out` as the QI left it and the clone at count 0 (a leak on both sides; tests note it, never free it).
  - **mutate.py and a template header**: a header no status entry names as `src` (src/sg/SGLightTemplates.h) is refused
    unless `--force-filter`; the light tests reach it because every light derives from it (say so in the report).

- **What bit lane OT** (point, spot, fog, draw-state lights):
  - **A test that builds a light whose OnLightChanged reads a setting needs the game's allocator service.** The
    point / spot light looks up "lightRadianceRangeCutoff" through the framework with a GZServiceString key; with no
    allocator service the ORIGINAL side corrupted the heap (the first Slots run died inside the original). Put
    `svcfx::AllocatorScope allocator;` (tests/ts_services_fixture.h; namespace svcfx, not difftest) first in every test
    that makes such lights; it tears the allocator down itself.
  - **Check eax returns of slots declared void.** `GetColor` (slot 51) returns its out pointer and callers use it;
    setters and GetParams may return something too. Only GetColor is checked against callers; the rest are marked
    unverified in the status notes.
  - **A free is the only trace of a released type-info name.** Dropping the `Release` after WriteTypeInfo in
    Write survived until the Persist test wrapped the first Write in a `difftest::FreeRecorder` and noted the count.
  - **A status-less helper file** (SGLightShapes.cpp) needs `mutate.py --force-filter`, like the template headers.

## cLightRefNode / cLightRefNodeT / cLightRefAttributeNameIterator

### Result (lane TE, N2r)

**N2r, lane TE (2026-11-01).** Done: cLightRefNodeT / cLightRefNode whole (src/sg/cLightRefNode.{h,cpp}, Impl in
src/sg/cLightRefNodeImpl.h): `cRenderableNodeT<cILightRefNode, cLightRefNodeImpl, cLightRefNodeBehaviorTraits>`,
0x90 bytes, Impl at +0xc (light +0x70, held copy +0x74, -1 +0x78, flag +0x7c). Hooked: factory 0xa61a80, complete
constructor 0xa60c00, destructor 0xa60ea0, deleting destructor 0xa61150; every other class slot (52 entries, 4 hooked)
and the 23 level copies (constructors 0xa60a20 / 0xa60ae0 / 0xa60930 / 0xa60b40 / 0xa60990 / 0xa60a80, BT 0xa60920,
the seven level destructors and deleting destructors, the level-only slots 0xa63690 / 0xa61540 / 0xa61640 / 0xa61b30
and the +8 Release thunk 0xa63505) in effect through callers, all verified (db/status/SGNodeCopies.jsonl,
tests/sg_node_copies.cpp `LightRef*`); 74 verified, ~7.5 KB. The display list (vf30 / vf26) stays virtual (a fake
display list in the tests); N3 0xa66150 an original call. Slot 51 0xa611d0 is verified since lane TN (below). No entries: the 5-byte jumps 0xa63500 / 0xa63510 and the 6-byte leaf
0xa61ae0. Finding: setting partition flag 6 on a node that is not a partition makes the tree walk call slot 77
(0xa54910, AddLayer) where it means Clear, on both sides (undefined; the test leaves bits 1, 6 and 12 alone). Left:
Task 3 of the brief (the cLightT copies 0x3c7cb0 / 0xa45a40, folding the nine SGLights spot hooks into cSpotLight),
0xa61d90 (done by TN).

**N2r, lane TN (2026-11-02).** The slot-51 builder 0xa61d90 (the Impl's MakeIterator51: the group "lightsGroup" or the standard light's
name, the attribute id to name switch, unknown ids to the empty range) is ours and hooked, with the iterator it hands out
(cLightRefAttributeNameIterator, src/sg/cLightRefAttributeNameIterator.*, vtable 0xe704e4, pool 0x10f6340): constructors 0xa60bd0 /
0xa60ba0, Clone 0xa61810, deleting destructor 0xa61110 and the pool allocation 0xa60f20 in effect through callers. Next 0xa632b0,
Previous 0xa63350, Get 0xa618e0 and Advance 0xa611c0 are shared with cViewerRefAttributeNameIterator (vtable 0xe71d80, used by
original code at 0xa64470): ours reproduce them, never hooked. Test SG_LightRefNode_Names (64 seeds), 10 mutants killed.

## cRenderableNodeImpl / cRenderableNodeT (the renderable level)

#### N3 part 1: the renderable level (lane PX, 2026-10-29)

- **cRenderableNodeImpl** (`src/sg/cRenderableNodeImpl.{h,cpp}`, group cRenderableNodeImpl, tests/sg_renderable_node_impl.cpp,
  fixture tests/sg_renderable_fx.h): 0x70 bytes (the bounded Impl, then +0x60 the render-group mask (1 at construction),
  +0x64 a word slot 81 sets and the node persists, +0x68 the detail limit the constructor leaves, +0x6c zero). Thirteen
  members hooked: the constructor 0xadfff0, AddLayer / RemoveLayer by name 0xae00a0 / 0xae02a0, by bits 0xae00d0 / 0xae02d0
  (the detail limit refreshed only when the mask changes), CountLayers 0xae00f0, the names' range builder 0xae02f0,
  SetWorldTransform 0xae04b0 (the viewer's and light reference's slot 60, ret 0xc: three words, the pointer first), RefreshDetailLimit 0xae06b0, Read 0xae0140,
  IsKindOf 0xae03d0 (iid 0x1c585e30), GetTypeInfo 0xae03f0 ("cRenderableNode", version 5), Write 0xae04f0. Not hooked:
  0xadfe30 (a 5-byte thunk shared with the partition) and CloneInto 0xae0630 (73 B: it calls slots 80 / 78 / 81 of the
  clone, so it needs real nodes: do it with the viewer pilot).
- **cRenderGroupNameIterator** (`src/sg/cRenderGroupNameIterator.{h,cpp}`): the 16-byte iterator over the mask's named
  bits (vtable 0xe7c8dc). Its vtable is stored only by the constructors 0xadffc0 / 0xadff90, which only the builder
  0xae02f0 (hooked) and Clone reach, so its slots (Clone 0xae0050, Get 0xae0080, Next 0xae0680, deleting destructor
  0xae0030) are in effect through callers (trap on the builder's hook); the shared slots stay unclaimed.
- Original calls left: the render-group registry getter 0xaeb370 (slot 3 BitOfName, 4 Count, 5 NameOfBit) and the detail
  object getter 0xaead80 (slot 43 = the limit). Both are services (stand-ins in the test over their getters).
- tests/sgfx.h: the writer's SetUint8 argument kind is now "b" (a byte): the original passes the result of `sete` with
  garbage in the upper bytes.
- cIRenderableNode slots (SGNodeInterfaces.h): 77 AddLayer, 78 AddLayerBits, 79 RemoveLayer, 80 RemoveLayerBits, 81 the
  word, 82 Draw, 83 per class (viewer 0xa661a0 via 0xa64c30, two arguments; light reference 0xa61bb0, three; shape
  reference 0xa55430), 84 CountLayers (shared 0xa57ff0), 85 MakeLayerNames (shared 0xa58740), 86 the layer-state pair
  (shared 0xa58720).

**N3, plan for the rest (lane PX).** Not started: the viewer base. Order:
1. cRenderableNodeT (the template, slots 77-86 over the Impl above; the shared_vf bodies 0xa57ff0 / 0xa58740 / 0xa58720
   are referenced by the shape, light and viewer references, so hook them only once all three are ours) together with
   the first user, cViewerRefNodeT, so CloneInto 0xae0630 gets a node-level test.
2. cViewerRefNodeImpl (0x104 bytes; the Impl bodies are 0xae06d0-0xae1ed0, ~8 KB): constructor 0xae06d0 (453 B: it takes
   the director framework object and registers event 0xbcf4e33, then sets the layer mask to all ones), destructor 0xae08a0
   (releases the refs at +0x70, +0xa4, +0xa8 and the vector at +0x84..), the cull-and-draw walk 0xae09e0 (426 B: the
   viewer's frustum against the child's bound via Frustum::ClassifyBoxAt, then slot 27 of the node), the event handler
   0xae0c20, then ~40 small accessors 0xae0b90-0xae1ed0 (Get / Set pairs on the fields at +0x78..+0xe4, mostly under 30
   bytes: inline only; the bigger are 0xae1000 (233), 0xae10f0 (271), 0xae1200 (288), 0xae1340 (295), 0xae1550 (170),
   0xae1600 (220), Read 0xae0e20 (470), Write 0xae1ad0, CloneInto 0xae1cb0). Several call the service helpers 0xae82b0 /
   0xae8730 / 0xae8710 (not read yet). The fixture is the node world with fakes for the held refs (drawfx).
3. cViewerRefNodeT's own functions (0xa64400-0xa68a00: 0xa661a0 at 666 B, 0xa66eb0 at 1555 B, 0xa68060 at 1512 B,
   0xa656f0 at 1211 B, 0xa68a00 at 916 B), with the frustum (src/compgeom/Frustum.h).
4. cViewerRefNodeRecursiveT and its composition-tree copy (0xa69f70-0xa6c9xx, whose Impl has a vtable), then cViewer_LEGACY.

## cViewerRefNodeT / cViewerRefNodeRecursiveT / cViewerRefNodeBaseImpl / cViewer_LEGACY

### N3 part 2: the viewer Impl, part 1 (lane PY, 2026-10-29)

- **cViewerRefNodeBaseImpl** (`src/sg/cViewerRefNodeBaseImpl.{h,cpp}`, group cViewerRefNodeBaseImpl,
  tests/sg_viewer_ref_node_base_impl.cpp, fixture tests/sg_viewer_ref_fx.h): 0xe8 bytes (the 0x70 renderable Impl, then
  +0x70 ref, +0x74 the framework's event answer, +0x7c mode, +0x80 scale, +0x84 four textures, +0x94 ref, +0x98 a texture
  manager handle, +0x9c / +0xa0 colour and depth surfaces, +0xa4 / +0xa8 refs, +0xac the held bound node (weak), +0xb0
  plane, +0xc0 flags (bit 2 = plane clips), +0xc4..+0xe4 persisted words and floats). 25 members hooked: constructor
  0xae06d0, destructor 0xae08a0, surfaces 0xae0b90 / 0xae0bd0 / 0xae0c10, bound forwarders 0xae0ce0..0xae0d40, boxes
  0xae0d60 / 0xae0db0, Read 0xae0e20, CallTextureSlot7 0xae0e00, 0xae1320, setters 0xae1470..0xae17a0, SetBound 0xae1870 and
  SetBoundAndPlane 0xae19b0 (event-service listeners), IsKindOf 0xae19e0 (iid 0xc0c0c0b), GetTypeInfo 0xae1a00
  ("cViewerRefNodeBase", 5), Write 0xae1ad0. Texture manager and handle operations (0xae8750, 0xae8260, 0xae8280, 0xae82e0,
  0xae82b0, 0xae8710, 0xae8730) stay original calls.
- Deviation: the original SetBound calls through the event service's answer without a null test (it crashes when the
  framework global is null); ours tests it. Reachable only without a framework.
- Part 2 (lane QC, 2026-10-29; tests/sg_viewer_ref_node_base_impl_part2.cpp, same fixture): six more members hooked and
  verified (31 in the group): the texture getters 0xae1000 (GetTargetSize), 0xae10f0 (GetTextureRef94), 0xae1200
  (GetTextureN) and 0xae1340 (GetHandleTexture), the cull walk 0xae09e0 (CullAndDraw, through our
  Frustum::ClassifyBoxAt) and the event handler 0xae0c20 (OnEvent). The texture protocol: the manager's handle names an
  object; a query for iid 0xb96cc22 gives the target (slot 6 a test, 16 the scale as a float in st0, 17(index) a level
  surface, 18 the ref; the surface's slots 20 / 21 width and height). 0xae1340 tests slot 6 on the queried target, but
  0xae10f0 / 0xae1200 test it on the handle object. The device side: 0x18aea0 (the render-target size, shared, stays
  original) and the device's slots 35 (ready), 39 (back buffer) and 40 (target provider surface). The cull walk's plane
  goes through the transform's plane carrier 0x19ca50 (stays original, shared with 0xa6aec0); the mask is the stack
  argument's low byte for the 0x3f test and the classify, the high bytes are kept when slot 27 is called. The original
  dereferences null for a mode 0 view with an invalid handle (0xae10f0 / 0xae1200) and for mode >= 2 with no first
  texture (0xae1000); the tests keep out of both and ours does the same.
- 0xae1ed0 (113 B, about 30 animation callers) and 0xae1f50 (812 B, director framework, event 0xc9de69b6) sit in the
  same address run but are not this class's (already named AnimatedFaceOf / AttachAnimation by lane JD).
- Left: CloneInto 0xae1cb0 with cRenderableNodeImpl's 0xae0630. They build and copy real nodes through slots of the node
  hierarchy (clone slots 80 / 78 / 81), which the fakes' world (an Impl block with a few counted stand-ins) does not
  provide; they wait for a world with real nodes (the first user, cViewerRefNodeT).

### N3r result, part 1: cViewerRefNodeT whole (lane TZ, 2026-11-02)

cViewerRefNode (the standalone shape cRenderableNodeT<cIViewerRefNode, cViewerRefNodeImpl, ...>) is ours from the
factory 0xa65fb0 down: 50 verified, 2 reimplemented (db/status/cViewerRefNode.jsonl). Files: src/sg/cViewerRefNode.{h,cpp}
(the node, slots 14, 15, 51, 91, 103, 104, QueryInterface, Accept), src/sg/cViewerRefNodeImpl.{h,cpp} (the entries,
the draw, the attributes, the clone), src/sg/cViewerRefNodeView.cpp (projection, viewport, the four transforms),
src/sg/cViewerRefAttributeNameIterator.{h,cpp} (slot 51's iterators and their pool), hooks in
src/hook/tables/cViewerRefNode.cpp (also the base Impl's CloneInto 0xae1cb0).
- cIViewerRefNode.h: slots 88-120's placeholders typed, slots 123-146 added (nothing renamed or reordered).
- Facts:
  - The Impl's entries (cViewerRefEntry, 0xcc bytes) are a std::vector at Impl+0xe8: type (+0, 0 perspective,
    1 ortho, 2 explicit), viewport, subrect, near/far, pixel aspect, depth range, fov (default 60 degrees), offsets,
    ortho box, the eye's placement, the 4x4 projection (+0x74), a vector of material handles, the display list's slot
    block (-1 none), the aspect it was made for and a dirty byte. Setters mark it dirty (SetDepthRange and
    SetProjection don't); GetEntry (slot 139) rebuilds a dirty entry, after RefreshAspect when the target's size or
    scale changed. Indexes are never range-checked.
  - Vector growth (0xa63c00) moves entries, materials vector taken over, nothing counted; shrinking and destroying
    (0x2a3d40) release each material handle (0xae8870). ResizeEntries frees each dropped entry's display-list slots
    first. The move constructor of cViewerRefEntry reproduces that (a copy would count the handles).
  - UpdateProjection 0xa656f0 (TS2-Extender's RTAspectRatioCheck patches 0xa6589d inside it): the configuration
    service 0x0ac1887e's "usePixelAspectRatio" (on when missing) selects vw / (vh * pixelAspect); a fixed-aspect entry
    always uses it; the rows' middle columns are swapped, then the subrect matrix is applied (Matrix4Multiply).
  - NaN order: each projection row is y*m1 + x*m0, then + z*m2, then + m3 (SSE, first operand's NaN kept); the
    viewport rows likewise c1*v1 + c0*v0 + c2*v2 + c3*v3, each output times 1/w. WorldToScreen differed by payload only
    until ours followed that order (ConvexMath.h MulS / AddS).
  - Render (slot 123) and Cull (83): an entry is drawn when its clipped viewport covers at least a pixel each way;
    the display list's DrawViewer, the parent's word's slot 93 on the pass (unchecked: ORIGINAL-BUG, a parentless
    node crashes) and DrawPass with the entry's materials. Cull skips when the list's frame equals the Impl's +0x78.
  - The base Impl's +0xc0 is the plane flags; it was called mFlags and shadowed the node Impl's mFlags (+0x18), so
    the renderable templates' GetFlags / TestFlag / SetFlag read the wrong word: renamed mPlaneFlags.
- Tests: tests/sg_viewer_ref_node.cpp (Factory, Slots 64 seeds x 40 operations, View 96, Clone, Names): every fake
  call with its arguments, the nodes' words and entries, every float by its bits. The original crashes in mode >= 2
  with no first texture (GetTargetSize 0xae1000) and in mode 0 with an invalid handle (0xae10f0, 0xae1200, 0xae0e00):
  the slot driver skips those (TargetSafe / HandleSafe). tests/sg_viewer_ref_fx.h: framework slot 3's out pointer is
  kind 'a' (a heap block made during the side).
- Left of N3r: the Impl's Read 0xa66eb0 and Write 0xa68060 (1.5 KB each; the persist slots 0xa689d0 / 0xa69a70 call
  them through OriginalFunction), the registry 0xa68db0 (game code on both sides), cViewerRefNodeRecursiveT (4.4 KB),
  cViewer_LEGACY (0.6), the cViewerRefNodeBaseT and composition copies (0.8).

### N3r result, part 2: persistence and the registry (lane VA, 2026-11-02)

cViewerRefNodeT has nothing of the game's left: Read 0xa66eb0 and Write 0xa68060 (src/sg/cViewerRefNodeImpl.cpp) and
the registry 0xa68db0 with its holder 0xa63750 (src/sg/cViewerRefNode.cpp) are ours, and the persistence slots
0xa689d0 / 0xa69a70 now verified: 6 more verified (db/status/cViewerRefNode.jsonl), 0xa65270's last original call gone.
- Facts:
  - The record (type "cViewerRefNode", version 15) is the base Impl's record, then a count of entries. From version 12
    an entry is its type, a fixed-aspect byte (from 14), viewport, subrect, near, far, pixel aspect, depth range, fov,
    offsets, ortho box, the rotation (an array of 4) and the translation, the 4x4 projection (an explicit entry's;
    every entry's at version 12), then materials: per material a present byte and a name. Read marks the entry dirty
    unless it is explicit.
  - Before 12 an entry is an object reference to a legacy viewer (cViewer_LEGACY, iid read through GetObjectRef) whose
    view is copied (ORIGINAL-BUG kept: no null check), then from 10 its placement: a 3x3 matrix turned into a quaternion
    (version 10) or the quaternion (11), and the translation; the entry is marked dirty and the viewer released.
  - Material names: read in the stream's group (reader slot 28; 0x1c0532fa counts as none) through
    ResolveResourceName, or as given when there is none; written with what follows the "!" of a "*group!name"
    reference.
  - The registry: built on first use as a copy of the transform family's map, plus ViewerFOV (0x4d40c00f) and
    ViewerNearFarClip (0x4d40c014) from 0x106c0e8 / 0x106c100; the same shape as the shape reference's (H2f).
    Hooked: the game's behaviour copies 0xa64f20 and 0xa687d0 still call it.
- Tests: tests/sg_viewer_ref_node.cpp Read (128 seeds, versions 9-15 over nodes holding entries, half through the +8
  interface), Write (128) and Persist (96, a round trip), on a typed tape (tests/sg_viewer_ref_tape.h: one queue per
  item kind, the position of each noted after the read); tests/sg_viewer_ref_registry.cpp (600 seeds, the shape
  reference's registry test's method). sgfx::RegistryScope stands the viewer registry in on both sides. Mutants: 16
  on Read / Write (one survived until the stream's group took a real group: 0x1c0532fa is ResolveResourceName's
  "none"), 7 on the registry, all killed.
- Left of N3r: cViewerRefNodeRecursiveT (4.4 KB), cViewer_LEGACY (0.6), the cViewerRefNodeBaseT and composition
  copies (0.8).

### N3r result, part 3a: cViewer_LEGACY and the template copies (lane VD, 2026-11-02)

cViewer_LEGACY is ours and verified, and so are the cViewerRefNodeBaseT / composition / behaviour copies in
cViewerRefNode's vtables: 26 entries (2.4 KB) in db/status/cViewer_LEGACY.jsonl and 63 (2.0 KB) added to
db/status/cViewerRefNode.jsonl.
- cViewer_LEGACY (src/sg/cViewer_LEGACY.{h,cpp}, hooks src/hook/tables/cViewer_LEGACY.cpp): a cSGResource plus a
  cObjectGraphNodeT<cIObjectGraphNode> node at +0x2c (persist face +0x34, Impl +0x38), the view (a cViewerRefEntry,
  0xcc bytes) at +0x40, the vtordisp at +0x10c and the virtual base cISGResource at +0x110; 0x114 bytes. MSVC
  reproduces the layout from the declaration. Hooked: the factory 0xa66010, the constructor 0xa64520, the destructor
  0xa64860 and the deleting destructor 0xa64c00. Everything else is in effect through callers (trap
  `Hooks.FUN_a0a64520=1&Hooks.FUN_a0a64860=1`). VA's ReadLegacyEntry now copies the typed view.
  - Read 0xa68a00 (record type "cViewer", version 0x11): the resource's record, the node's, then (before 0x11 only)
    a word, five floats and a word nobody uses any more, the material names (as the viewer-reference node reads them),
    viewport, subrect, near, far, fov, pixel aspect, depth range, offsets, an orthographic flag (the view's type) and
    the ortho box.
  - GetRecord18 / GetRecord19 (0xa68710 / 0xa68720) are 3-byte bodies (in db/missed-functions.jsonl), never hooked:
    in effect through callers.
- The template copies: QueryInterface's adjustor through the persist face (0xa68912), Release's (0xa68f55), OnEvent 3
  (0xa66150), NotifyListeners / AskListeners 5 / 6 (0xa69240 / 0xa65f70), the attribute slots 7 / 10, the behaviour's
  12 / 13 (0xa687d0 / 0xa64f20: cAbstractBehaviorTypeMT, lane SV's cBehaviorCopy hooks cover the viewer behaviour's
  slots 2-11 only), slot 25, the bound slots 70-75, the persist slot 0xa69720, and the template-level constructor
  chain 0xa64250 .. 0xa63fd0 and destructor chain 0xa64730 .. 0xa64600 with their template deleting destructors.
  All in effect through callers (trap `Hooks.FUN_a0a644a0=1&Hooks.FUN_a0a64820=1`, TZ's). Each template-level
  destructor's other references are its level's deleting destructor and one unwind funclet (0xd732e3 .. 0xd73393),
  whose handler stub only the matching template constructor references: no other path.
  - Open references (callers_handled): 0xa687d0 / 0xa64f20 and 0xa69240 / 0xa65f70 also sit in the template levels'
    own vtables (cAbstractBehaviorTypeT<cIViewerRefNode> 0xe707b0, cObjectGraphNodeT 0xe70a00, cCompositionTreeNodeT
    0xe70c70, cTransformNodeT 0xe70ee0, cBoundedNodeT 0xe71150, cRenderableNodeT 0xe713c0), which only those
    constructors and destructors store: unreachable once the hooked constructor and destructor are ours.
- Tests: tests/sg_viewer_legacy.cpp (Factory, Slots, Read, Events: the four faces, words, frees, the fakes' calls) and
  tests/sg_viewer_ref_node.cpp Slots ops 24-26 (OnEvent from the held bound or not, the listeners told or asked, the
  bound slots with and without a held bound). TZ's Slots test had a latent null dereference in the original (the
  attribute read of the fov / near-far brings the entry up to date, so 0xae1000, on a node left in mode >= 2 with no
  first texture); the read now uses another id unless TargetSafe, on both sides. Mutants: 20 on cViewer_LEGACY (one,
  GetRefCount unmasked, survived until the Events test noted the count with the active bit set) and 12 on the copies
  (two survived first: NotifyListeners' sender, until op 25 planted the node as its parent's held bound with random
  flags 0xb / 0xd, and AskListeners' parent, until an answering listener sat on a random tree node); all killed.
- Left of N3r: cViewerRefNodeRecursiveT (4.4 KB; done in part 3b below), which also closes the open references above. callers.py reports the
  hooks of cViewerRefNode's deleting destructor 0xa64bd0 and cViewer_LEGACY's vtordisp thunks as no longer needed
  (every caller ours): left in place for the coordinator to decide.

### N3r result, part 3b: cViewerRefNodeRecursiveT whole (lane VF, 2026-11-03)

cViewerRefNodeRecursive is ours: 119 entries (5.1 KB) in db/status/cViewerRefNodeRecursive.jsonl: 118 verified (5,040 bytes; 30 mutants, 29 killed, the survivor equivalent: slot 51's argument, which the empty attribute range ignores), slot 83 0xa6a370 reimplemented.
- Classes: src/sg/cViewerRefNodeRecursive.{h,cpp} (the node, 0x10c bytes, vtables 0xe72eac / 0xe730a8) on
  cViewerRefNodeBaseT<cIViewerRefNodeRecursive, cViewerRefNodeRecursiveImpl, ...> (src/sg/cViewerRefNodeBaseT.h, lifted
  out of cViewerRefNode's header: the viewer base level shared by both nodes) and src/sg/cViewerRefNodeRecursiveImpl.{h,cpp}
  (the Impl at node+0xc, with a vtable of its own: slot 0 FillViewport 0xa6a650; the base Impl at node+0x10, so every
  template slot reaches the composition part there). The Impl adds the materials (a vector of material handles, a
  display-list slot range sized to them: SetMaterialCount 0xa6b980 / SetMaterial 0xa6b9c0), Read / Write 0xa6b750 /
  0xa6bb80 (type record "cViewerRefNodeRecursive" version 1, the base record, then the materials as cViewerRefNode
  writes them: VA's ReadViewMaterials / WriteViewMaterials), CloneInto 0xa6bde0.
- Hooked: the factory 0xa6ac70, the constructor 0xa69f50, the destructor 0xa6a130, and slot 97 0xa6ca60 (13 bytes,
  SetWordC4: cTSFenceEntry's slots 24 / 26, 0xca3690 / 0xca0e40, call it directly). Everything else is in effect through
  callers, trap `Hooks.FUN_a0a69f50=1` (traps.py --sync: the destructor hook isn't needed by any check). The template
  constructor chain 0xa69f30 .. 0xa69db0 and destructor chain 0xa6a110 .. 0xa6a070 are claimed (our base classes' own
  constructors and destructors do each level's work); the destructors' deleting-destructor calls (0xa6a1e0 / 0xa6a210 /
  0xa6a240 / 0xa6a270 / 0xa6a2a0) and unwind funclets (0xd735b3 / 0xd735f3 / 0xd73613 / 0xd73633) are in
  callers_handled.
- Kept original (done since by lane VG, part 3c): slot 83 0xa6a370 is ours but calls the Impl's Cull 0xa6aec0 through OriginalFunction (with Reflect
  0xa6c430, ObliqueClip 0xa6bf10 and PlaneToFrame 0x19c980 under it): reimplemented, untested (the original Cull makes a
  null call on the test's viewer world). That is the rest of N3r's recursive viewer (about 1.9 KB).
- Not claimed: the shared bodies 0x3aecf0, 0x25f160, 0x259390, 0xa696e0 (Accept 30 / 50), 0xa6aea0, 0x1fc0e0, 0x24bdc0,
  0x24a310, 0x239850, 0x4da8c0, 0x4e5d90, 0xa6cc50, 0x316685; 0xa69ca0 / 0xa69bf0 look like STL vector instantiations.
- Tests: tests/sg_viewer_ref_recursive.cpp (Pilot_Slots / Pilot_Events through the transform pilot at level 4,
  Factory, Slots: 40 ops of 23 kinds, Clone, Read, Write, Persist). Shared helpers moved: TapeBase, BoolWord and
  StreamGroup into sg_viewer_ref_tape.h, the TargetSafe / HandleSafe guards into sg_viewer_ref_fx.h (keyed on the node
  and its base Impl, so both viewer nodes use them); sgfx.h's traversal fake settles slot 10 (the recursive node's
  Accept).
- Hooks that can go (callers.py --vtable-only after this part, not removed: other groups' entries): cViewerRefNode's
  deleting destructor 0xa64bd0 (trap `Hooks.FUN_a0a644a0=1&Hooks.FUN_a0a64820=1`) and the 15 viewer vtordisp thunks in
  VtordispThunks (0xa6618d, 0xa69052, 0xa64c99, 0xa686b4, 0xa686e8, 0xa686f6, 0xa68d95, 0xa69028, 0xa69044, 0xa692ef,
  0xa693c1, 0xa693f8, 0xa69439, 0xa69447, 0xa695cd), each with the trap callers.py names; the cGeometryNode-family
  thunks in that group can go too. This lane's own four hooks stay (each is referenced from outside the vtables).

### N3r result, part 3c: the recursive viewer's Cull, and AdjustTextureSize (lane VG, 2026-11-03)

cViewerRefNodeRecursive is now ours whole: 126 entries, all verified. This lane added 8 (4,005 bytes; 24 mutants, 23
killed, the survivor equivalent).
- Cull 0xa6aec0 (src/sg/cViewerRefNodeRecursiveImpl.cpp), with slot 83 0xa6a370 (now verified with it) and the
  geometry under it in `recursive_view`:
  - Reflect 0xa6c430 and ObliqueClip 0xa6bf10;
  - PlaneToFrame 0x19c980 (a cAffineTransform3 member; its only callers are these two);
  - the 3x3 / 4x4 helpers Subtract3 0x1a6760, Transpose4 0x1a65e0 and MultiplyBy4 0x1a6690.
- In effect through callers. Every reference is inside the cluster, under slot 83 of the vtable the constructor hook
  0xa69f50 keeps; the trap is `Hooks.FUN_a0a69f50=1`.
- Kept original (shared): CarryPlane 0x19ca50 (thiscall: world transform, plane) and the box-to-screen 0xaf22d0 (cdecl:
  low3, high3, box, mvp).
- ORIGINAL-BUGs kept:
  - The settings service is called unchecked: GetPropertyValue on a null object.
  - The parent's stage word is called through slot 93 unchecked.
- Layout facts:
  - The projection is at view+0x144, past sizeof(cViewerView). The rectangle is at +0x84 and the depth range at +0x94.
  - Reflect's mirror carries flags 0x1f, but Compose only ORs them in and Reflect sets the result's flags to 3. Its x row
    is negated (the mirrored eye keeps its handedness).
  - ObliqueClip skips when |w| <= 1.192093e-10 (0x2f03126f), and flips the plane when w > 0.
- Tests (tests/sg_viewer_ref_recursive.cpp):
  - The world gained stage, parent and settings fakes, and the projection behind the view (gBig).
  - `SG_ViewerRefRecursive_Cull`: 128 seeds x 24 steps mixing OperateSlots and Cull, with random frames, projections,
    bounds and plane flags. Each of the five draw branches must be reached 20 times.
  - `SG_ViewerRefRecursive_Geometry`: 4000 cases of the six helpers called directly, including NaNs, in-place and
    overlapping arguments. It needs at least 200 ObliqueClip cases kept and 200 clipped.
  - Test fix: OperateSlots' box frame is a whole cTransform34. The slot reads 13 words, so the old 12-float array left
    the 13th to the stack on both sides.
- AdjustTextureSize 0x18c990 (679 bytes, verified, group TextureDescription):
  - src/sg/TextureDescription.cpp, hooked: the game's cRenderTargetTexture::Restore (last argument 1), cPatternBrush
    vf3 and 0x37b3d0 call it.
  - ChooseTextureDescription and cDraw2D::PlaceRecord call ours now. tests/sg_texture_description.cpp and
    tests/gz_draw2d.cpp stand it in on both sides (StandIn instead of a CodePatch of the image only).
  - `SG_AdjustTextureSize` tests it: the last argument is 0, 1 or high bits only; each of the four branches must be
    reached 100 times.
- VE's "possibly live, not chased" list: all six are shared outside their class (each has an original caller in
  another class), so they were left:
  - 0xaad320: 0xaceb60, plus a jmp at 0xd76ed1.
  - 0xa7b0f0: 0xaee2c0 / 0xaee570.
  - 0xa5af90: 0xbab1e0.
  - 0xa845c0: 0xa916b0.
  - 0xa84a60: 0xa8dd90.
  - 0x42ffc0: cTSTreeTableEntry::Read and a std::vector instantiation at 0x6675cf, so likely STL.

## cStringPool / cDataListItem

### Result (lane OX, D1a part 1)

- D1a part 1 (lane OX, 2026-10-28): **cStringPool and cDataListItem**, whole classes in the standalone shape.
  - **cStringPool** (`src/sg/cStringPool.{h,cpp}`, group cStringPool): the interned-string table (cIStringPool
    0x4caa9810, 14 slots) and its handle iterator; our std::unordered_map / vector / free list stand for the game's MSVC
    hash table. Hooked: the getter 0xadf8b0 (a function-local static) and JenkinsHash 0x98bff0 (lookup2, also called by
    other code); cSceneGraphSystem's StringPool() calls ours. Tests: tests/sg_string_pool.cpp.
  - **cDataListItem** (`src/sg/cDataListItem.{h,cpp}`, group cDataListItem): one named, typed value of a data list
    (cIDataListItem 0x6a7b33bb, 37 slots; class 0x6a7b33c3), persisted three ways: persist writer / reader (6 / 7),
    binary stream (8 / 9), one text line per item (Describe 10 / Parse 11, types "Undefined" .. "ByteArray" from
    0xe78318). Hooked: the COM director's factory 0xac6d50; everything else through callers. Ours keeps the game's
    layout (name +4, type +0xc, count +0x10, value words +0x14): getters of another type read the old words, and the
    name's bug (below) reads into the type word. Its list holds counted references (Clear releases them: the
    `list<unsigned int>::_Tidy` name on 0x46f7f0 is a folded smart-pointer list's; its walk 0x46f6d0 calls Release).
    Tests: tests/sg_data_list_item.cpp (Slots, Read, ReadStream, Parse), the COM getter 0x1ae18d stood in on both
    sides so children come from the side's own factory.
  - **Not done, plan for D1a part 2 (cExtension, cDataListExtension):**
    - **cExtension** (0x18 bytes before its virtual base; vtables 0xe694f4 +0, 0xe694fc +12 (the persist referent
      face, 7 slots), 0xe6951c at the virtual base (cIExtension, 27 slots, all vtordisp thunks `sub ecx, [ecx-4]`)).
      It is the base of ~17 game classes (cTagExtension, cSkinExtension, cBoneDataExtension, the anim channel outlets,
      nTSSG extensions), so it cannot go "through callers": do it as cSGResource was done, bodies with `this` = the
      subobject, each hooked at its RVA, thunks left original, on the game's layout. Constructor 0xa3b750 (ret 8; the
      behaviour-type base 0xa3b710 sets +8 = 0; +0x10 / +0x14 zeroed: owner and next?), destructor 0xa3b880 (releases
      +0x14), QI 0xa3be60 (iids 0x5b0aaa74, 0xbb11a53e, 0xfb06dba0, then the referent's 0xa3bde0: 1, 0x3b253dc7,
      0xfb859324), Release 0xa3bee0 (count at +4 of the face, high bit masked; at one: a registry 0xa3bac0 told, then
      the deleting destructor), type info 0xa3c160 ("cExtension", version 3), Write 0xa3c210 (type info through the
      writer's slot 28). The cIExtension bodies 0xa3bd10 .. 0xa3c130 are small; slots 17-20 are the shared
      0x9e8850 / 0x9e7e90 (owner / next getters of a family: check who else references them before hooking).
    - **cDataListExtension** (leaf; 0x30 bytes; factory 0xa990b0 (new 0x30, constructor 0xa98950 with the
      virtual-base flag 1)). The constructor makes a cDataListItem through the COM (0x6a7b33c3 / 0x6a7b33bb) into +0x18
      and calls its MakeList (slot 28). Its own face (vtable 0xe7520c, cIDataListExtension, 27 slots) is a named-value
      store over that list: a setter looks the name up (item slot 32 FindByName), sets the found item (slot 29 GetItem,
      then the setter slot), or makes a new item through the COM, names it (4), sets it and AddItem's it (33), then
      releases its own reference (0xa99430 Bool .. 0xa99910). Persist: type info "cDataListExtension" 0x6a836d56
      version 1 (0xa99dd0); Write 0xa99fa0 = cExtension's Write then the list item's Write (6); Read 0xa99330 = the
      type info, cExtension's Read 0x9dc310 (shared), then the item's Read (7). Clone 0xa98c10 (new 0x30, copy
      constructor 0xa98840). As a leaf it can be our C++ class whole (factory hooked, its own bodies through callers)
      once cExtension's bodies are ours; tests: the sgfx kExtension table (15 Clone, 17-22 owner / next, 24
      IsPersistent, 26 Accept) and Read / Write round trips (sgfx item 3) on both sides, with the item's factory
      0xac6d50 ours on both sides (cDataListItem::Create).
  - **Not done, D1b:** cGeometryDataContainer (derives from our cSGResource; its part at vtable offset 128; Write calls
    cSGResource::WriteResource, not 0xa3ced0) and the reader-owned helpers.

### What bit lane OX

- **cDataListItem's name bug** (lane OX): 0xac6ae0 copies a long name (8 bytes or more with its terminator) to the
  heap without zeroing the marker byte, so over a short name it reads back as that name's first four bytes, the heap
  pointer's bytes and on into the type word (and the copy leaks). Reproduced (`ORIGINAL-BUG`); tests note the state and
  empty the name before comparing more (the pointer differs per side).
- **cDataListItem's text byte arrays don't round-trip** (lane OX): Parse splits each byte off up to the space after it,
  but the line was trimmed, so the last byte of a line as Describe writes it has none and the parse fails. Only an
  empty array or a line with more text after the bytes parses (`ORIGINAL-BUG`; tests use such "x" lines for the
  byte checks).
- **A library name can hide behaviour** (lane OX): 0x46f7f0 is named `std::list<unsigned int>::_Tidy` (library), but
  its node walk 0x46f6d0 releases each element: the list holds smart pointers. Read the callee before believing a
  "frees the nodes only" reading.
- **sscanf and the image's CRT** (lane OX): the original's Parse runs the image's own sscanf in the harness without
  trouble; ours uses `sscanf_s` (the same conversions for %d / %f).

## cExtension / cDataListExtension

### Result (lane OY, D1a part 2)

- D1a part 2 (lane OY, 2026-10-28): **cExtension and cDataListExtension**, whole classes.
  - **cExtension** (`src/sg/cExtension.{h,cpp}`, group cExtension): the event template
    cAbstractBehaviorTypeST<cExtensionV> (traits: event-manager cell 0x10f6010, missing record 0x106a48c, the plain
    behaviour registry 0x1485f0) plus the persist referent at +0xc, owner +0x10, next +0x14 (counted), vtordisp +0x18,
    the virtual base cIExtension at +0x1c; 0x20 bytes. A base of ~17 game classes, so its bodies are hooked at their
    RVAs on the game's layout (the cSGResource way); the constructor hook is our compiler's own constructor through a
    linker /alternatename. Interface facts fixed in `SGNodeInterfaces.h`: slot 16 / 21 / 22 / 23 / 26 signatures,
    slot 25 `GetTypeName` (a kind name, "data_list"), slot 26 `GetDescription(cIGZString *)`. Tests:
    tests/sg_extension.cpp (BasePart, Slots, GameDerived: cTagExtension's constructor and destructor on both sides).
  - **cDataListExtension** (`src/sg/cDataListExtension.{h,cpp}`, group cDataListExtension): a leaf, our C++ class
    (`cExtension, public virtual cIDataListExtension`; cIDataListExtension is `public virtual cIExtension`, the
    game's layout falls out of the same base order). The factory 0xa990b0 is hooked; the rest is in effect through
    callers (the seven bodies the thunk records swallowed are in db/missed-functions.jsonl). Tests:
    tests/sg_data_list_extension.cpp (Slots: every face of a game-made and an our-made extension, real
    cDataListItems on both sides through a fake COM director; ReadWrite: tapes written by each side read into a fresh
    extension). Original calls left: none in the two classes' own code. Mutants: notes/feedback/OY.md.
  - **Not done:** 0xa99130 (6 bytes, returns the class id 0x6a836d56; called by 0xa33bf0, outside this class's
    vtables) stays original.

### What bit lane OY

- **cIExtension slots 25 / 26** (lane OY): 25 is `GetTypeName` (a kind name: "data_list"), 26 is
  `GetDescription(cIGZString *)` (cExtension's: the empty string; cDataListExtension's: "(%s n%d)"). Older notes and
  sgfx's kExtension comments that call 26 "Accept" predate this.
- **cExtension's eax-returning "setters"** (lane OY): SetNext (22) returns the last Release / AddRef answer, SetOwner
  (21) the owner; GetTypeInfo's adapter returns its out (a mutant returning null survived until the test compared eax
  through 0xa3c160 directly). Check eax at ret for each slot, as write.md says.
- **The behaviour template's destructor leaves dead stores** (lane OY): 0xa3b850 writes three vtable / vtordisp words
  into the dying object last; a destroyed object's words are compared without them (tests/sg_extension.cpp cites it).
- **Cloning an unnamed data list faults** (lane OY, `ORIGINAL-BUG`): cDataListExtension's list copy 0xa98c90 calls
  SetName(GetName()) on each copy, and cDataListItem's SetName (0xac6ae0) dereferences a null name. Tests clone only
  named lists.

## cGeometryDataContainer (GMDC)

### Result (lane PA, D1b)

- D1b (lane PA, 2026-10-28): **cGeometryDataContainer** (GMDC), a whole class, with its iterators and reader helpers.
  - `src/sg/cGeometryDataContainerClass.{h,cpp}` (group cGeometryDataContainer): `cSGResource, public
    cIGeometryDataContainer` (face +0x2c, vtable 0xe73224, 59 slots named from what the bodies do; virtual base
    +0x80); the members are MSVC std::vector / std::string with the game's layout (static_asserts). The factory
    0xa70aa0 and the class-id static 0xa70b10 are hooked (cSceneGraphSystem::Init now calls ours); everything else is
    in effect through callers. The byte-matched hook-shaped bodies of the earlier D1 work
    (`src/sg/cGeometryDataContainer.h`, the face as `this`) are what our slots call and keep their hooks; the
    non-matching ones lost their hooks. Write calls cSGResource::WriteResource.
  - Iterators (`src/sg/cGeometryIterators.{h,cpp}`): only the container stores the vtables of cRZMemPtrIterator<
    cVector<4>> (0xe734f0), cRZNullRandomAccessIterator<unsigned short> (0xe73580) and the anonymous cUnitIterator
    (0xe73384), so those bodies are ours; the other MemPtr instantiations are stored elsewhere too and stay game code
    (ours are separate classes of the same shape).
  - Reader helpers 0xa71fc0 / 0xa72320 / 0xa720e0 / 0xa72200 (cdecl, only Read's). Triangle sets
    (nGZCompGeom::cTriangleSetShape, the director's class 0xfcfe5320) stay the geometry library's: Read fills them
    through the originals 0x195af0 (reserve), 0x196d00 (vertex), 0x196cc0 (triangle) and 0x196300 (build), G0b's.
  - Tests (tests/sg_geometry_data_class.cpp): factory, class_id, slots (random calls through the face and the
    virtual base, every getter and range walked), persist (write, read into fresh or populated containers, write
    again), read (synthetic buffers of versions 0-5), through our cGZPersistBinaryReader / Writer on both sides.
  - **Not done:** the four Original calls above (G0b); GetOwnerInfo 0xa72c90, the GetSegment thunk 0xa72ca3 and the
    primary vtable's folded slots stay shared_vf (no entries).

### What bit lane PA

- **The game's allocator service static in a harness test** (lane PA): game code that allocates through 0x197100 on a
  first use registers an atexit handler for the static at 0x10c95d4 (guard 0x10c95d8) with the uninitialised game
  CRT and corrupts the heap (0xc0000374 in _recalloc). Tests that run such code fill the static and its guard with a
  fake service first and restore them after (tests/sg_geometry_data_class.cpp, ServiceFake).
- **Triangle sets need a triangle** (lane PA): the geometry library's build 0x196300 -> 0x195c80 -> 0x195d90 sizes its
  node vector to 2 * triangles - 1 (0x196650): a set with vertices and no triangles throws the vector's length error
  0x1443b0 on both sides. Generate 1 or more.
- **sgfx Pool::Reset keeps string capacity** (lane PA): the pool's texts are cleared, not freed, so the second side
  starts with buffers the first side grew and the heap-block counts differ by side. Tests comparing
  AllocationCounter swap each text with an empty string after Reset.
- **cGeometryDataContainer::Write leaves eax** (lane PA): the original's Write 0xa73920 returns with eax = the subset
  mesh count; the interface declares Write void and no caller is known to read it (all reach it through the vtable),
  so ours returns nothing (gap).

## Geometry builders: cGeometryBuilder, cCompactorBuilder, cBoundingVolumeBuilder, cTangentSpaceBuilder, cIndexedMeshBuilder

The four builders derive from `nGZSceneGraph::cGeometryBuilder` (0x18 bytes before its virtual base
cIGeometryBuilder), as do five more (cAlphaSortBuilder, cOptimizedGeometryBuilder, cProcessDeformationsBuilder, nTSSG's
cNHoodTerrainGeometryBuilder and cWaterGeometryBuilder) (lane QE).

### D2 part 1: the compactor and bounding volume builders (lane QE, 2026-10-29)

- **cGeometryBuilder** (`src/sg/cGeometryBuilder.{h,cpp}`, no group: its game bodies stay original for the other seven
  builders): our C++ base, 0x18 bytes then the cIGeometryBuilder virtual base (vtordisp before it); the behaviour is
  `cAbstractBehaviorTypeST` with the builder registry 0xa453a0 (original call, a function-local static map), plus an
  extra listener at +0x10 (SetListener 0xa456c0) told first by NotifyListeners 0xa45660. Persist record
  {"cGeometryBuilder", 0, 3}; Read 0x9dc310 only reads the record.
- **cCompactorBuilder** (group cCompactorBuilder, 11 verified; factory 0xa83930 the only hook, 0x2c bytes: +0x14
  per-group reset, +0x15 enabled, both set by the constructor and neither persisted). Build (0xa839cc's vtordisp thunk
  jumps to an un-db'd body at 0xa839e0, which tests +0x15 and tail-jumps to 0xa83a60) renumbers each linkage's
  vertices in first-use order, rebuilds the three alias lists through the new-to-old table and sets the referenced size.
  ORIGINAL-BUG: the per-group reset forgets [lowest, last) of the group's vertices, not [lowest, highest]. A group with no
  indices reads past its range (kept out of the tests).
- **cBoundingVolumeBuilder** (group cBoundingVolumeBuilder, 28 verified, 13 of them vtordisp or adjustor thunks in our
  vtables; factory 0xa85090 the only hook, 0x34 bytes: +0x14 a std::set<std::string> of group names, +0x1c named-only).
  Build 0xa85d50: SelectBones 0xa84dc0 (weight elements 0x5c4afc5c with block and set format 0: the strictly greatest
  weight's repetition per vertex, NaN never wins), then two passes of Fill 0xa85120 (sizes, then triangles) into a
  bounding mesh (no weights) or one subset mesh per transform (each corner in its bone's frame through compgeom
  RigidTransform3), the meshes our compgeom TriangleSet from the COM director's GetClassObject (0xfcfe5320). Position
  element: 0x5b830781, repetition 0, block format 2, the first in the linkage's list (0xaeda40, shared, ours is a local
  copy); its set format picks the alias list. Record version 2 adds the flag and names (Read from version 2).
- Tests: tests/sg_geometry_builders.cpp (4 tests: both slots tests through all three faces, both builds over synthetic
  containers made with our cGeometryDataContainer on both sides). Mutants: 29, 28 killed; the survivor is equivalent
  (Fill's used[2] test). One test fix from a mutant: the position decoys never had block format 2.
- cTangentSpaceBuilder was ~7 KB (Build 0xa86c80 ~5.5 KB of SSE), not the 0.5 KB the run's owner lines showed; done in
  part 2 below.

### D2 part 2: cTangentSpaceBuilder (lane QG, 2026-10-29)

- Done: the whole class on QE's cGeometryBuilder base (src/sg/cTangentSpaceBuilder.{h,cpp}), 0x30 bytes. Only the factory
  0xa86bf0 is hooked; constructor, destructors, QueryInterface, Read, GetTypeInfo, Write, Build, GetName and AddName are
  "in effect through callers". Layout: behaviour face +0, vbptr +4, persist face +0xc, listener +0x10, the name set
  (std::set of std::string, case-insensitive) +0x14, cIGeometryBuilder base +0x20, cITangentSpaceBuilder base +0x28.
- Build: for each triangle-list group of the container whose name is in the set (or all, when the set is empty), it reads
  the position, normal and texcoord elements, builds a per-triangle frame (0xa88560: tangent, bitangent from the uv
  deltas, skipped when |det| <= eps 0x2f03126f), accumulates it area-weighted into each vertex slot once per linked
  vertex (9 floats, 0x1a66d0), then writes the tangent element (identity 0x89d92ba0) by Gram-Schmidt against the normal
  with a fallback axis when degenerate. Floats are SSE single with `(float)sqrt((double))` and no FMA.
- ORIGINAL-BUG: the Gram-Schmidt step scales its projection by the normal's length instead of normalising it.
- Tests: tests/sg_tangent_builder.cpp (slots through all three faces; Build over 160 seeded synthetic containers with
  special floats, empty/linked/degenerate groups, and a texcoord "few values" mode that reaches the |det| == eps case).
  Mutants: 17.

### D2 part 3: cIndexedMeshBuilder (lane QH, 2026-10-29)

- Done: the whole class (src/sg/cIndexedMeshBuilder.{h,cpp}), 0x1e0 bytes, on QE's cGeometryBuilder base. Only the
  factory 0xa7ffa0 is hooked; every other body (constructor, destructors, the 48 slots of the cIIndexedMeshBuilder face
  at +0x1d8, Build 0xa80200, Read, Write, GetTypeInfo, QueryInterface) is "in effect through callers"; the compiler's
  thunks on both faces (+0x1d0 cIGeometryBuilder, +0x1d8) are verified through the face. 89 status entries.
- Layout (offsets in the object): positions 0x14, normals 0x20, texcoords 0x2c.., colours 0x5c.., word 0x74, extra word
  lists 0x78 / 0x84, groups 0x90 (0x80 bytes each, flags first), runs 0x9c, names 0xa8, bones 0xb4, influences 0xc0,
  morphs 0xcc, name index 0xd8, collecting flag 0x138. Group flag bits: 0 normals, 1-4 texcoord sets, 5-6 colour sets,
  7 influences, 8 / 9 morph positions / normals, 10 / 11 the two extra word lists (set only by the finalize helpers).
- Still the game's, called through OriginalFunction: the five Finish helpers (also 0xa820a0 from Read for versions
  below 10) and the matrix-to-quaternion 0x1a7570 (bones of version below 9). That is part 4.
- Tests: tests/sg_indexed_mesh_builder.cpp (collect, build, persist, faces), synthetic meshes through the face, whole
  object, Build container and the tape compared. Findings the harness had to respect: the original's Build crashes on
  unclosed runs or runs naming missing groups / names; its helper 0xa820a0 (Finish, and Read below version 10) crashes
  or corrupts the heap on unfinished or inconsistent data (so those paths run only on finished meshes); the game takes
  a lazy one-off block on the first builder, compensated by warming it on both sides.
- Mutants: 50, 49 killed; the survivor is equivalent (the clamp compare 0xff to 0xfe in ChannelByte: x = 255 gives 255
  either way).

### D2 part 4: the finalize helpers and the matrix-to-quaternion (lane QL, 2026-10-30)

- Done: cIndexedMeshBuilder's WeldVertices 0xa7e380, MergeDuplicateVertices 0xa7f4a0, ClassifyGroups 0xa820a0, MergeGroups
  0xa7ed00 and RebuildRuns 0xa7f0b0 (src/sg/cIndexedMeshBuilder.cpp), and QuaternionFromMatrix 0x1a7570 (hooked, ten
  callers, bit exact; tests/sg_matrix_to_quaternion.cpp). Finish and Read no longer call the game; the five helpers are
  "in effect through callers" (only our Finish and Read reach them), `original_calls` of Finish and Read are empty.
  Four helpers `verified`; MergeGroups is `reimplemented` (below).
- Findings: the raw object dumps compare allocation state, so copy-assign against move-assign matters. MergeGroups ends
  with a copy-assign of the group vector (lists of old groups that end up empty keep their buffers); RebuildRuns
  copy-assigns the triangle lists and the run list. The game's group vector is a vector of 128-byte blobs: growth moves
  positions, normals, source and triangles but copy-constructs texcoords and colours (a copy of an empty list has a null
  begin). Classify constants: weight limit 1e-4f (0x38d1b717), squared move 0x322bcc76.
- Gap: the equal-flag merge branch of MergeGroups cannot be reached with legal data (CommitGroup already joins equal masks;
  Finish over kept groups makes the original throw), so its mutants (triangle offset, run shift, count, normals append)
  survive and the entry stays `reimplemented`.
- Leftover: 0xa836c0 (the plain vector::resize QH used for new groups) now has no original caller left and is unattributed.

### D2 part 5: the cGeometryBuilder base bodies hooked (lane QM, 2026-10-30)

- Done: twelve bodies of nGZSceneGraph::cGeometryBuilder are ours at their RVAs for all nine derived classes
  (src/sg/cGeometryBuilder.cpp, `cGeometryBuilderAdapters`, src/hook/tables/cGeometryBuilder.cpp): Write 0xa45790,
  QueryInterface 0xa45310, Release 0xa45530 (the family check passed), OnEvent 0xa45240, AskListeners 0xa450f0, Connect
  0xa44fd0, Disconnect 0xa450a0, NotifyListeners 0xa45660, SetListener 0xa456c0, and the registry slots GetEventCount
  0xa452c0, GetEventIds 0xa44c60, GetEventAttribute 0xa44ee0 (these three call the game's registry 0xa453a0, an
  `original_calls` entry). All `verified`. Read 0x9dc310 and AddRef 0x2582b0 stay original (folded `shared_vf_*`).
- Faces: persist +0xc (QI, Release, Write), behaviour +0x10 (OnEvent, AskListeners, Connect, Disconnect, registry slots),
  listener +0x18 (NotifyListeners, SetListener). The adapters recover the object from the face by those offsets.
- Tests: tests/sg_geometry_builder_base.cpp drives Write through compactor, bounding, tangent and indexed builders (game
  factory objects, one side patched to the adapters); sg_indexed_mesh_builder_faces calls QI, Release, OnQuery, AskListeners,
  slot 7 and the registry slots through the faces. cIndexedMeshBuilder slots 4, 6, 7, 12, 13 and 14 are `verified` on that.
- Test gap closed: the persist test now clears the classifier bits (0xf80) on tapes of version below 10, so
  `mVersion < 10` -> `< 9` and `< 11` die. ClassifyGroups' morph loops now run over the vertex lists as the original does;
  mutating them back to the delta lists survives because Read and every Add give both lists one count (equivalent).
  The persist test skips builders whose indices ClassifyGroups would read out of range (the original crashes there).
- MergeGroups' equal-flag branch is still unreachable with legal data; the entry stays `reimplemented`.

### Test gap noted by the coordinator (lane QL)

   Same lane, test gap (coordinator mutant, QL): `info.mVersion < 10` -> `< 9` before ClassifyGroups survives, because
   the persist test's version-9 tapes come from finished builders whose group flags are already classified. A real
   version-9 file carries unclassified flags: clear the classification bits (0x80/0x100/0x400/0x800) in the tape's
   group flags when it makes a version below 10, so the rerun shows. Also MergeGroups 0xa7ed00's equal-flag branch.

## cBoneDataExtension / cSkinExtension

### D3 part 1: cBoneDataExtension and cSkinExtension (lane QP, 2026-10-30)

- Done: both extensions as whole classes in the cDataListExtension shape (`final : public cExtension, public virtual
  cIX`, C4250 off), each switched over by hooking its COM factory only (0xa98470, 0xa97720); every other body is in
  effect through callers (the clones are reached only through the class's vtables). src/sg/cBoneDataExtension.*,
  src/sg/cSkinExtension.*, tables src/hook/tables/cBoneDataExtension.cpp / cSkinExtension.cpp. 20 + 52 entries
  `verified` (db/status/cBoneDataExtension.jsonl, cSkinExtension.jsonl; mutants 21 and 42, all killed).
- cBoneDataExtension (0x4c, type name "bone_data"): three axis codes, a scale and a quaternion; Read takes the
  quaternion only from version 4 on; GetDescription formats "(H%c M%c S%c[%c%3.2f])" (the scale compared with ucomiss:
  NaN takes the long form).
- cSkinExtension (0x74, type name "skin"): a name cRZString, a word, a vector of counted pointer pairs, a vector of
  lowercased {name, key} cRZString pairs (slot 9 MapName: a null key resets the map) and a vector of counted pointers.
  Ours keeps the name as a cRZString (Read fills it through cIGZString::FromChar) and the map as std::string pairs
  (only the class's own slots read it). The pointer lists are std::vector over a one-word counted pointer (cSkinRef):
  MSVC 14.40's push_back / erase / clear reproduce the game's AddRef / Release order (temporary, copy, release;
  assign-down then release the last; forward destruction, a pair's second before its first). ORIGINAL-BUG kept: the
  copy constructor (Clone) copies the name and word but not the lists. Read / Write carry only the name and word.
- Tests: tests/sg_bone_data_extension.cpp and tests/sg_skin_extension.cpp (Slots: random calls through all three faces
  over counted fakes; ReadWrite: tapes). Pitfall: the cIX face's own slots 0-2 are not QI/AddRef/Release (cIExtension
  is a virtual base), so a QI result for the class's own iid is released through the cIExtension face.
- The cISkinExtension face's slots 1, 10, 14, 19 and cIBoneDataExtension's getters are folded accessors
  (`shared_vf_*`): no entries, not hooked; ours are our class's own.

## cFindBonesTraversal / cDynamicGeometryRect

### D3 part 2: cFindBonesTraversal and cDynamicGeometryRect (lane QR, 2026-10-30)

- Done: both classes as whole classes with our vtables. src/sg/cFindBonesTraversal.*, src/sg/cDynamicGeometryRect.*,
  src/sg/cIGeometryRender.h (the render interface, slots 3-9, and cGeometryRenderContext: width +0xc0, height +0xc4,
  device +0x2a8, state +0x2ac, dynamic arrays +0x2b0..+0x2bc; reusable by part 3's cTSParticlesGeometry), tables
  src/hook/tables/cFindBonesTraversal.cpp / cDynamicGeometryRect.cpp. db/status/cFindBonesTraversal.jsonl (8 entries;
  42 mutants, 33 killed, 9 equivalent) and cDynamicGeometryRect.jsonl (6 entries; 115 mutants, 114 killed, 1 equivalent:
  moving the vertex 1 y advance after its texture writes changes nothing).
- cFindBonesTraversal: a visitor over the node tree; hooked at its factory only (0xa95d30), the other bodies in effect
  through callers. The two template bodies shared with cFindAttributeNodesTraversal (QueryInterface 0xa95d60, Traverse
  0xa95eb0) are `verified` without a hook (row 29). SGNodeInterfaces.h: slot 3 is `Traverse`, slots 6-11
  `TraversalSlotN`. cNodeRef (counted node pointer) duplicates cSkinRef.
- cDynamicGeometryRect (0xb0): five hooks, the three slots (deleting destructor 0xa44150, Begin 0xa44180, Render
  0xa441b0) plus the constructor 0xa43ec0 and destructor 0xa440f0 (the constructor is the switch for the three inline
  construction sites 0x377920, 0xaa0eb0, 0xaa8810). Slot 5 (0xa441a0) is a 6-byte folded accessor shared with
  cTSParticlesGeometry: `verified`, not hooked. The constructor builds a vertex format (position + texcoord, stream 0)
  and asks stride and offsets; Render fetches a declaration if missing, reserves and locks the dynamic arrays (flag
  picks the A or B pair), writes nx*ny quads (UV flag shifts by half a cell), the 16-bit indices, and draws
  `DrawIndexedPrimitive(3, trunc(2*nx*ny), ...)`; it returns the first vertex.
- ORIGINAL-BUG reproduced: the quad's index base is i*nx+j while the vertices are written in i*ny+j order, so the
  indices point at the wrong vertices when nx and ny differ.
- Tests: tests/sg_find_bones_traversal.cpp, tests/sg_dynamic_geometry_rect.cpp (fakes for format, managers,
  declarations, state, device and four dynamic arrays; Render compares logs, object words and 256-byte-chunk hashes of the
  64 KB buffers over 600 seeds). Pitfall: draw each random number into a local; a short-circuit or rng() as a call
  argument desyncs the two sides.
- Not tested or not observable: the constructor's dead branch (old +0x6c non-null) is not implemented; the `flags & 1`
  sized delete is the CRT's on the original side (Rect_ReleaseLast covers ours); Begin's return value; +0x3c and +0x6a.
  The callers 0x18ae40 / 0x18dcf0 / 0x18dd70 are already ours.

## cLODLevelsTable / cLODLevelsStore

### D3 part 3: cLODLevelsTable and cLODLevelsStore (lane QS, 2026-10-30)

- Done: src/sg/cLODLevels.{h,cpp}, table src/hook/tables/cLODLevels.cpp, db/status/cLODLevels.jsonl (all `verified`),
  tests tests/sg_lod_levels_table.cpp (10) and tests/sg_lod_levels_store.cpp (5); 22 mutants, all killed.
- cLODLevelsTable (0x1cc) is hook-shaped like its base cBaseParser (src/gz/cBaseParser.h, used as is): the cBaseParser
  part at +0, `std::vector<LODLevel>` {size, level} at +0x1ac (nothing else touches it), then the cIGZUnknown face
  (+0x1bc) and the cIBaseParser face (+0x1c4, own vbptr +0x1c8), each with a vtordisp before it. The faces' 11-byte
  thunks (`sub ecx,[ecx-4]; sub ecx,0xc; jmp`) land on cBaseParser's bodies with `this` where cBaseParser's own faces
  sit, so each is hooked to a forwarder (TableShiftedThunk) into cBaseParserAdapters; the table's own bodies are
  Initialise (registers the LOD command), Shutdown and Reset (both empty the levels after the base call).
- The LOD command ("LOD <level> <size>", a cCommandT with vtable 0xe76914) evaluates the size, then the level, through
  face slot 32 (EvalInt) and caps the level at 30.
- cLODLevelsStore (0x1c) is standalone-shaped: only the COM factory 0xab6f40 is hooked; every other body is in effect
  through callers (trap `Hooks.nGZSceneGraph_cLODLevelsStore_Create=1`). It watches "useLODs" (0xcc6887ab, bool) and
  "lodOverride" (0x8c7df62a, uint32) as a cIGZMessageTarget2. DoMessage stores the useLODs byte as it came (not as a
  bool: IsEnabled tests the byte); lodOverride 0 means override -1 and LODs off. Refresh walks every scene node's shape
  children (iid 0xa516cd10, child lists via slot 93) and extension children (0x7d14eb66, extensions via 0xaea060, still
  the original's) and calls slot 44 on each. GetLevel: the larger of width and height truncated, a lower bound over the
  sorted sizes, past the end the last row (ORIGINAL-BUG: an empty table reads before the storage).
- Test pitfalls: the settings key is a GZServiceString with the MSVC std::string layout (not a cIGZString); the
  table's RunText fixture needs the RZMemory Malloc/Free stand-ins (0x1be3b0 / 0x1be3a1); vtordisps given to a face must
  keep its vtable word clear of the base part (a vtordisp of 0xc or more); the store's padding bytes are uninitialised
  on both sides (not compared); the store sorts with std::sort, compared on more than 32 rows with duplicate sizes
  (MSVC 14.40's sort matches the game's order there).

## cGeometryNode

### D3 part 4: cGeometryNode (lane QU, 2026-10-30)

- Done: src/sg/cGeometryNode.h, src/sg/cGeometryNodeClass.cpp, table src/hook/tables/cGeometryNode.cpp (31 hooks:
  the base constructor 0xa3dd80, the destructor 0xa3e320, the factory 0xa42590 and the slot bodies),
  db/status/cGeometryNode.jsonl, tests tests/sg_geometry_node.cpp (6 tests: Factory, Construct, Helpers, Slots,
  Persist (Read versions 1-12, then Write), Subsets).
- Shape: `cObjectGraphNodeT<cIGeometryNode, cObjectGraphNodeImpl, Traits>, cSGResource, cIGeometryRender`, 0x134
  bytes: the node +0 (vtable 0xe6987c), the persist face +8, the Impl +0xc, the cSGResource part +0x14 (vbptr +0x30),
  the render face cIGeometryRender +0x40 (QR's src/sg/cIGeometryRender.h), the members +0x44 .. +0x129, the vtordisp
  +0x12c, the virtual base cISGResource +0x130. Not final: the derived world-lot nodes (nTSSG's cBaseGeometryNode,
  cTerrainGeometryNode, cPoolWaterGeometryNode, ...) inherit Read / GetTypeInfo / Write and most slots, so our bodies
  call the node's own virtuals through the vtable as the original does (Read's Rebuild / ClearParts / AddPart).
- **Read / Write (shared by 4 classes):** the 4 are cGeometryNode and three classes derived from it (inheritance, not
  folding: callers.py shows only those vtables and their thunks), so hooking the cGeometryNode body is the class's own
  hook; a derived object runs our body on its cGeometryNode part.
- **For part 5 (the build 0xa3f180, called `(this, container)` thiscall from Rebuild 0xa3ebe0, still
  OriginalFunction):** it fills mSubsets (+0x44, a `std::multimap<cGeometryNodeCell, uint32_t>`: the key is a counted
  string-pool cell, closed when the node goes (0x2eb770), ordered by handle), mElements (+0x78, 0x7c-byte
  cGeometryNodeElement: name cell +0x38, index vector +0x40, six counted refs +0x54 .. +0x68, [2] .. [5] the device
  objects), mRecords (+0x84, 16 bytes), mRecords24 (+0xa0), mRecordsF4 / mRecords100 (+0xf4 / +0x100, 0x28 bytes),
  mNamesA / mNamesB (+0x10c / +0x118, cells) and the render words mRenderInfo (+0x4c). The tests stand it in
  (StandBuild: elements, subsets and device objects from a counter); part 5 replaces the stand-in with the real body
  and needs D1's container and D2's builders (ours).
  - DONE (lane QY, 2026-10-30): `src/sg/cGeometryNodeBuild.cpp`, test `SG_GeometryNode_Build` (fakes for the vertex
    formats manager, the formats and the device buffers; random containers; node members, buffer bytes and the call
    stream compared). Stand-in left for part 6: the device reset 0xa419e0. Untestable: the "Failed initialization"
    throws (the harness cannot throw). The original adds a vertex-format element only for known identities
    (0x18da20), reads bone data unchecked when the container has no transforms, and carries the element type
    uninitialised across groups (ORIGINAL-BUG in the port). The viz tail word [1] is never written.
- **For part 6 (render 0xa41ee0 / 0xa429b0 / 0xa435d0, device reset 0xa419e0):** the render slots take the +0x40
  face (cIGeometryRender), so `this - 0x40` is the node. Part 4 names only what its own slots touch (the element
  words slot 7 forwards, the device objects, mDeviceReady +0x128, mResourcesReady +0x129); what the draw reads beyond
  that is part 6's to name. The device event (OnEvent) calls 0xa419e0 `(this, 0, 0)` through OriginalFunction.
- Vectors that the original grows by copying (its growth paths 0x18c0c0 for the parts, 0x2b5710 for the bounds: they
  construct the new element first, then copy the old ones and release them) hold cGeometryNodeCopiedRef, a reference
  with a throwing copy and no move, so MSVC 14.40's vector copies too and the AddRef / Release order matches.
- Pitfalls:
  - The settings key passed to the property set is a GZServiceString (std::string layout), not a cIGZString:
    drawfx kind "S" calls through it and crashes; declare it "W0W0" and note `c_str()` (as QS).
  - The old part (iid 0x7ce7e022) is a cGeometryBuilderV pointer whose vbptr is at +0: the original reaches the
    builder through vbtable[1]. Fakes need a vbptr face (the test's VFace) or the original crashes at 0xa3e7b7.
  - SetBounds asks each object through its **slot 3** (0xc) for 0x5ce57549, not QueryInterface.
  - The constructor with flag 0 (base part) needs the vbptr at +0x30 preset, as a derived constructor does.
  - drawfx ranges persist across sides: a heap address reused by the second side keeps the first side's tag, so
    give each side its own buffers.
  - The subset iterators (slots 42 / 44) come from two cRZFastFixedAllocator2 pools (0x10f6024 for the 16-byte name
    iterators, 0x10f6030 for the 12-byte part iterators); the test points both at 0x800-aligned test pages.
  - Reader slots: 23 (0x5c) a raw skip (2 bytes for version 11, 0xc below 10), 24 (0x60) the parts (kIidPart), 25
    (0x64) GetObjectRef for the container.

### D3 part 6: cGeometryNode's render face, device reset, registry, subset iterators (lane QZ, 2026-10-30)

- **Family check:** the render slots 0xa41ee0 (IsSupported, 13 vtable slots), 0xa429b0 (Prepare, 17) and 0xa435d0
  (Render, 12) are cGeometryNode's own bodies, inherited: every vtable holding them belongs to a class RTTI derives from
  cGeometryNode (nTSSG's world-lot nodes). Derived overrides call the base body directly (0x2eeed0, 0x33d3a0, and
  shared_vf_320720, only cTerrainSnow / cLightSnow). Not folded across unrelated classes, so hookable as the class's own
  (as QU found for Read / Write). 0xa419e0 (ResetDeviceState) is a base helper the nTSSG derived nodes call directly.
  The registry 0xa42f30's map 0x10f6074 is touched only by it and its atexit 0xd95ce0; its holder 0x10f6088 only by
  0xa3cf50. The iterator pools 0x10f6024 / 0x10f6030 are used only by the allocators 0xa3e540 / 0xa3e570, and the two
  iterator classes are built only by GetSubsetNames / GetSubsetParts and their own Clones (0xa3f080 / 0xa3f0b0).
- **Layout the render face reads** (the face is node + 0x40): the element's +0x0 vertex count, +0x4 primitive count, +0xc
  the vertex format, +0x34 (byte) "hardware declaration built", +0x4c primitive type, +0x50 start index, +0x54 / +0x58
  software declarations (format, stream-0 only), +0x5c / +0x60 hardware declarations, +0x64 / +0x68 state blocks
  (both streams / stream 0), +0x6c base vertex, +0x70 the base the state blocks were built for, +0x74 / +0x78 the
  strides of streams 0 / 1. The node: +0xb8 indices, +0xbc stream 0, +0xc0 stream 1's source, +0xc4 the converted
  stream 1 (IsSupported fills it through 0xc627e0), +0x128 "failed", +0x129 "ready".
- **Device helpers not ours (OriginalFunction):** 0x18daa0 (a declaration from a format), 0x18d990 (its stream-0
  declaration), 0xc627e0 (the vertex convert), the node registry 0x264010 and the node family's base events 0x2570a0.
- DONE (lane QZ, 2026-10-30): `src/sg/cGeometryNodeRender.cpp` (IsSupported, Prepare, Render, ResetDeviceState, hooked),
  `src/sg/cGeometryNodeIterators.{h,cpp}` (the two iterator classes with pool operator new / delete; in effect through
  GetSubsetNames / GetSubsetParts, trapped), the registry in `cGeometryNodeClass.cpp` (hooked: its first-use holder
  0xa3cf50 calls back into 0xa42f30, so callers.py leaves the pair open; the holder is trapped behind the hook). Every
  cGeometryNode entry's `original_calls` is now game code outside the class (0x18daa0, 0x18d990, 0xc627e0, 0x264010 and
  part 5's fillers). Tests: `SG_GeometryNode_Render` (real build then 2-6 random device events and render calls over
  `tests/sg_geometry_node_device_fx.h`, the device call stream compared: milestone O acceptance (a); O-b can reuse the
  devfx World), `SG_GeometryNode_Registry` (tests/sg_geometry_node_registry.cpp), the iterators in `SG_GeometryNode_Subsets`.
  ORIGINAL-BUGs kept: the state blocks read the declarations unchecked (stateBlocks without declarations before any
  declaration faults; the test only asks for that once they exist), the first-stream size divides unchecked, the
  director, the new array and the second array's lock unchecked. Pitfalls: the iterator interface needs a virtual
  destructor in slot 7 (Release's delete otherwise frees pool blocks with the global delete: heap corruption); the
  original registry's first-use build calls the CRT's thread-safe init (a trap in the harness), so the test plants the
  state it leaves. The reset and registry stand-ins stay in the other cGeometryNode tests to note the callers' flags.

## cMaterialManager and the material iterators (cMaterialHandleIterator, cRenderShaderContextIterator), GetDetailObject

### M1a result (lane RD, 2026-10-30)

nGZSceneGraph::cMaterialManager (`src/sg/cMaterialManager.h`, 0x110 bytes, vtable 0xe78514, 39 slots: cIMaterialManager
15-37 on our cAbstractBehaviorTypeST template) is our class; the group is `db/status/cMaterialManager.jsonl` (23 entries,
all `verified`), the tests `tests/sg_material_manager.cpp`. cSceneGraphSystem Init, Shutdown and OnTick call our getter.
- **Hooked:** the getter 0xacc040 (a function-local static), MixHash3 0x98bf70 (lookup2's mix; used outside the
  manager too), the behaviour registry 0xacc550 (the game's cAbstractBehaviorTypeT instantiation 0xaca910 / 0xacab80 /
  0xacc4c0 still names it; its first-use holder 0xac8940 is in effect through it) and ResetEntry 0xacb170 (part 2's
  0xacbaf0 calls it).
- **In effect through callers** (trap `Hooks.nGZSceneGraph_cSceneGraphSystem_Create=1`, what callers.py rests on): the
  constructor 0xac9810, destructor 0xac9ee0, deleting destructor 0xaca420, QI 0xacc510, OnEvent 0xacba20, OnDeviceEvent
  0xacb780, CreateDeviceObjects 0xacbe90, Start 0xacbae0, Shutdown 0xacd8b0, OnTick 0xacda40, FlushPending 0xacb630,
  AddRefIndex 0xacb400, ReleaseIndex 0xaccb20, Reset 0xacb260, MarkAllStale 0xacac60, IsLoaded 0xacba60, GetInfo
  0xacc410, DestroyEntry 0xacb560.
- **Findings.** OnDeviceEvent's vtable call is slot 24 (+0x60, 0xaccfb0, args (0, 1)), the reload, not Reset (slot 25,
  +0x64); Shutdown and MarkAllStale call Reset. 0xacadd0 (the variant builder) is `__stdcall(Entry *, const VariantKey *,
  uint32_t flag)`, `ret 0xc`, no `this` (OnTick passes 0, 0xacce00 passes 1). The entry's set at +0x3c holds refcounted
  object pointers (released by the tree erase 0x16f520 and the entry destructor 0xac9df0): `std::set<Held>`, ordered by
  address (ours; only part 2 fills it). Entry +0x44 is a plain vector of words.
- **Tests.** SG_MaterialManager_Life (400 seeds: each side's object built by its own constructor in a static block or on
  the heap, state planted through the typed view, 4-27 random slot calls with the whole state noted after each, then the
  destructor or the last Release), SG_MaterialManager_Registry (600 seeds), SG_MixHash3 (20000 triples),
  SG_MaterialManager_Get (two-sided as lane RB's getter test: the original getter with its guard set hands out the image's static; ours one object each call with our vtable and the calls and contents of a manager the original constructor builds; 3 getter mutants killed). 25 mutants (build/rd_mm_mutants.json), all killed; before them the generators were checked to reach every value the original branches on (device codes, ready and Create failures at each index, jobs across the 25 ms budget, waiting entries with and without flag 2, references -1 to 2, stale flag, Reset by name / unknown / null with flags 0-3, QI ids).
- **render_object gate.** 134 -> 151 image references: 0xacc040 is gone; added are part 2's 15 forwarded bodies (the 13 slots, 0xacadd0, 0xae8750; about 7.4 KB, now linked because our manager is) and the registry's missing-attribute record 0x106fad0 / 0x106fae0 (as the other behaviour registries' records). M1a part 2 takes the 13 slots and 0xacadd0 out.

### M1a part 2 result (lane RE, 2026-10-31)

- **The in-game crash of RD's merge, and the thiscall correction.** 0xacadd0 (the variant builder) is `__thiscall` with
  `this` = the manager (`ret 0xc`: entry, key, flag). RD's OnTick called it `__stdcall(Entry*, key, flag)` and its test
  stand-in had the same wrong signature, so the test never saw it. In the game ecx was garbage, the original read a
  garbage "manager" for `mBuffersReady` and the 34 buffer pointers, and a string ("bnai", 0x69616E62) reached
  cDeviceStateDX::SetStreamSource / SetIndices. Fixed: `BuildVariant` is a member called with `this`, hooked at 0xacadd0;
  the new test has no stand-in for it (the original side runs the image's real one through the original object).
  Lesson for stand-ins: a stand-in's signature must be checked against the callee's `ret N` and the caller's ecx setup,
  not copied from the caller. The STL member layout was not the problem (same MSVC 14.40).
- **Done (verified, SG_MaterialManager_Life):** slots 19, 23 (0xacc630 is a 5-byte `jmp` to 0xacbaf0: not hooked
  itself, our slot 23 is `BuildEntry`'s body), 24, 26, 27, 29, 30, 32, 37, and the helpers 0xacadd0, 0xacce00, 0xacbaf0,
  0xacc640 (hooked). Slot 30's return value is junk (eax left by a vector `_Tidy`), noted as 0, not compared. Slot 27
  hashes the object itself in both branches (ORIGINAL-BUG marker); LoadStale (0xacb470) takes the text of the map
  *key* (node +0x10), not of `entry.name` (+0x18): found by planting entries whose name differs from their key.
- **Tests / mutants.** Life gained ops 16-27 (FindOrLoad, AddMaterial, ReloadAll, Register, Unregister, LoadStale,
  Slot30, IsAvailable, GetInfoByName), fakes with colliding hashes and hash-equal comparison, planted unnamed entries.
  41 mutants (build/re_mm_mutants.json, re_mm_mutants2.json): 40 killed, 1 equivalent (ReloadAll with a name ignores
  its flags, so passing 1 from Slot30 cannot differ).
- **Leftovers (M1a part 3):** slots 22 (0xacd2d0, 1282 bytes, five arguments), 34 (0xaca850), 35 (0xaccc00), 36 (0xacd7e0)
  still forward to the image (`OriginalFunction`); also called: 0xae8750 (shared getter; stays), 0xae8980 (the material
  parser's cached getter, M1b) and 0xae7340 (the "#" name strip, in FindOrLoad).
- **render_object gate.** 151 -> 143 image references.

### M1a part 3 result (lane RF, 2026-10-31)

- **Done (verified, SG_MaterialManager_Life):** slots 22 (0xacd2d0, thiscall `ret 0x14`: index, key*, queue, refA,
  refB), 34 (0xaca850, `ret 4`), 35 (0xaccc00, `ret 8`), 36 (0xacd7e0, `ret 4`). 34-36 return a 12-byte
  `{clone of begin, begin, end}` through a hidden pointer (callee pops); all four read ecx as the manager. No
  `OriginalFunction` left in cMaterialManager except 0xae8750, 0xae8980 and 0xae7340 (the brief's).
- **Slot 22.** Find; on a miss BuildVariant, and when the queue *byte* (`cmp byte`, not the dword) is set the keys the
  variant implies: for the key's own bits-11..14 field and then for each of the entry's words, the blob keys
  (running sums of blob words 1-12 packed as nibbles, each sum or-ed in whole), the flag keys (byte5c set: the key with
  mask `0xf0007ff9` cleared, then with related formats refA, refB and both put in) and the blob keys of those; the keys
  not already variants go into a `std::set` ordered as memory (memcmp), and one job each is pushed with the entry's name
  (a fresh cell assigned the name) and the vertex format `slot[key.w2>>4&0x7f]` of the formats manager. ORIGINAL-BUG: a
  key the parser fails on returns the variant table's head node's value word (never set); ours returns null.
- **Slots 34-36.** The iterator objects are the game's (vtables 0xe785b4 handle iterator, 0xe785d8 render shader
  context iterator = variants, 0xe785fc shader iterator = listeners B; 0x10/0xc/0xc bytes from the fast allocators
  0x10f6818/24/30). Ours allocate them from those pools in the original's order (end, then begin), set the vtable, the
  position (handle iterators skip holes) and hand out `Clone` of begin, then count cursor, begin, end. Their other
  slots (Clone, IsEqual, Get, Next, dtor; shared bodies) stay the image's and are called through the object, as the
  rule for originals that receive our objects asks.
- **Test.** Life gained ops 28-31 (EntryRange, VariantRange, ListenerRange, GetVariant) and a richer Plant (blobs with
  random words, words, byte5c, derived keys that coincide with planted variants, duplicate keys): the pools are test
  pages, each iterator's words are noted, a clone is walked with the iterators' own Clone/IsEqual/Get/Next and released.
  A listener without the face leaves an uninitialised stack word in the original's Get: not compared.
- **Mutants:** 24 (mask constants, flag-bit values, nibble shift and sum, blob position range, byte-vs-dword queue,
  memcmp order, byte5c, a/b tests, loop bound of the flagged keys, skip-existing, words loop, format index, empty
  guard, allocation order, hole skip, base key, bits shift, refA/refB swap): 23 killed, 1 survived (AddRef order of the
  clone/begin/end: different objects, unobservable, equivalent).
- **Lesson.** A temporary `fprintf` probe in a test whose sides compare heaps allocates a stdio buffer (0x1000) on the
  side that runs first: it looked like a leak in the original and cost an hour. Use the log, not stdio, in such tests.
- **Leftovers:** (the three iterator classes are ours since lane TI, see "H2r result") the three iterator classes (vtables, Clone/IsEqual/Get/Next/dtor, shared bodies: the pools and vtables
  are now referenced from our code); 0xae8750, 0xae8980 (M1b), 0xae7340.
- **render_object gate.** 143 -> 147 image references: the four forwarded slot bodies are gone (-4), but our code now
  holds the three iterator vtables, two data addresses beside them (0xe785e4, 0xe78608) and the three pool statics
  (+8). The next step for these is the iterator classes themselves.

### H2r result (lane TI, 2026-11-01)

- **GetDetailObject 0xaead80 hooked** (verified; src/sg/DetailObject.cpp, tests/sg_detail_object.cpp: SG_DetailObject_Get over cache x slot-9 answer, SG_DetailObject_Throws for the unwind). Callers in our code call ours; tests/sg_renderable_fx.h and tests/ts_nhood_sg_manager.cpp use `shape::StandIn`. No `original_calls` names it any more.
- **The M1a iterator classes are ours** (src/sg/cMaterialIterators.*; tests/sg_material_iterators.cpp, one test per class: the same seeded script on the game's objects and ours, every slot called through the vtable, noting the answer, the object's words and all three pool headers). 24 RVAs verified and in effect through callers (the only creators are cMaterialManager's range slots 34-36, which now build ours): constructors 0xac9610 / 0xac95e0 / 0xac9b20 / 0xac9b00 / 0xac9b60 / 0xac9b40, vtable-store bodies 0xac9de0 / 0xaca0c0 / 0xaca0d0, operator new 0xaca0e0 / 0xaca110 / 0xaca140, delete 0xaca170 / 0xaca1f0 / 0xaca270, Clone 0xacaca0 / 0xacacd0 / 0xacad00, deleting destructors 0xaca3e0 / 0xaca450 / 0xaca490, Get 0xacb130 / 0xacb150, Next 0xacc4b0.
- **Not hooked, shared with other classes:** QueryInterface 0x178b20, AddRef 0x148fb0, Release 0x1a84c0, IsEqual 0x17b680, handle Get 0x46390, handle Next 0xacc480 (also cTextureHandleIterator and cStringHandleIterator: it goes with them). Our slots behave identically and the vtables are ours.
- **Quirk:** the shader iterator's Get returns an uninitialised stack word when the listener's QI fails; ours returns null (the test's listeners always answer).
- **Equivalent mutant:** freeing a block through another pool's object changes nothing (Free works from the block's page).
- **Left in H2r:** cShapeRefNode rest, the material-block helper 0xa53cf0. TE's task 3 (cLightT copies 0x3c7cb0 / 0xa45a40, the SGLights spot hooks) not started.

## cMaterialParser / cMaterialAttributes

### M1b result (lane RH, 2026-10-31)

- **Class.** cMaterialParser is ours (`src/sg/cMaterialParser.{h,cpp}`, 0x304 bytes, virtual bases): the cBaseParser part at
  +0, vbptr at +8, faces at +0x2e8 (cIGZUnknown), +0x2f0 (cIBaseParser, 51 slots) and +0x2fc (cIMaterialParser, 9
  slots), vtordisps at 0x2e4, 0x2ec, 0x2f8. `cSceneGraphSystem::Init` now constructs it through our `Construct(1)`.
- **Done (verified, tests/sg_material_parser.cpp, 11 tests):** the constructor 0xacec90, destructor 0xacf040, deleting
  destructor 0xacfd90, Initialise 0xad8ba0 (9 commands + 14 functions by table), ResetBlocks 0xada9d0, ResetState
  0xad13c0, EraseCell 0xadc8a0 (calls the image's 0xae87d0 for real on both sides), QueryInterface (thunk 0xadc7e9)
  and IsStateOne (thunk 0xad8b6c). The other 47 shifted-face thunks (14 bytes, forward to cBaseParser adapters) and the
  8-byte own thunks are hooked rows in `src/hook/tables/cMaterialParser.cpp`, checked by shape and forwarded arguments
  (MaterialParser_FaceThunks, no mutants: forwarders).
- **Unhookable bodies.** 0xad8b80 (IsStateOne) and 0xadc800 (QueryInterface) are not function starts for hookspec: only
  their thunks are hooked, the bodies stay the image's and are reached only from those hooked thunks.
- **Mutants:** 16 tried, 16 killed. Weak spot: a block command made plain is caught only by MaterialParser_Whole (which
  registers into the real cBaseParser), not by MaterialParser_Initialise.
- **Test traps worth knowing.** A vtordisp of d moves the face by d: the object's save/restore window must not cover the
  face's vtable pointer or vbptr (vtordisps 0x20..0x3c, allocation kSize+0x80). The head word +0xc of each map has
  uninitialised upper half in both sides (compare 16 bits). The destructor's real cBaseParser::Destruct needs a
  CodePatch when the object holds junk.
- **render_object gate.** 147 -> 157 image references. Our constructor now holds the vtables of cMaterialParser (4
  addresses) and cIMaterialParser (3) and a few data addresses, the price of installing the original's vtables;
  the old constructor call is gone (-1). The next step is the vtables (the 64 slots below).
- **Leftovers:** the cached getter 0xae8980 (still original in cMaterialManager, cTSSGSystemLifecycle and
  cTSCommonSystem; not switched here).

### M1b part 2

| item | note |
|---|---|
| 0xacfe00 (ret 8), 0xad03e0 (ret 0x10), 0xad0d90 (ret 0x14) | slots 4, 6 and 7 of the material face (1.3-2.5 KB each), not started; disassembly via `classinfo.py --disasm`, the Ghidra server was down |
| 0xad8b80, 0xadc800 | bodies behind hooked thunks: need `missedfuncs` entries before they can be hooked |
| cMaterialAttributes | 0x28 bytes, vtable 0xe76188, 16 slots, factory 0xaaa3c0 -> constructor 0xaa9e40 (named by lane RH) |
| the 64 slots as our own vtables | removes the vtable address references from the constructor |

#### M1b part 2 result (lane RI, 2026-10-31)

- **Done (verified, 3 more tests, 14 in the file):** cIMaterialParser slots 0 SetPair 0xadcb90, 1 RemovePair 0xadc930, 2
  SetAlias 0xadca70 and their thunks (0xadcb84, 0xadc919, 0xadca5c). They work on the string-pool cells (`OwnedCell`,
  now with the original's copy/assign semantics: addref on copy, Assign = slot 6 on the source, slot 7 on the old value)
  in the sorted `vector<CellPair>` lists (group 0 = `mCells2d0`, groups 1.. = `mMap2c8[group]`, made when missing) and the
  alias map `mMap2dc`. They call nothing in the image (the cells go through our service access).
- **Tests:** MaterialParser_SetPair / RemovePair (600 rounds) and SetAlias (400), over prepared lists (duplicates, the
  empty cell's key, spare and exact capacity, front/middle/end insert, found/not-found, name 0, group 0, absent group);
  they compare the service log, frees, list sizes/capacities/cells, the second map and the rest of the object.
- **Mutants:** 6 tried, 6 killed (erase the last of equal keys, alias target opened with flag 1, assign the name instead
  of the value, RemovePair name-0 test, insert position, group-0 test).
- **render_object gate:** 157, unchanged (the constructor still installs the image's vtables).
- **Left:** the three large slots above (the cell-list machinery and 0xacf3f0/0xacebc0 helpers they need is now in
  `cMaterialParser.cpp`, reuse it), the vtables as ours, cMaterialAttributes.

#### M1b part 3 result (lane RL, 2026-10-31)

- **Done (verified, 3 builder tests and 2 body passes, 17 tests in the file):** material face slot 4 ParseDefinition
  0xacfe00 (thunk 0xacfdee), slot 6 BuildShader 0xad03e0 (thunk 0xad03d5), slot 7 BuildErrorShader 0xad0d90 (thunk
  0xad0d79), and the bodies 0xad8b80 IsStateOne and 0xadc800 QueryInterface (missed-functions entries added; their
  Ghidra names wait for the coordinator's reload, `annotate` refuses an address with no function). Every builder test
  round runs either the thunk with a random vtordisp or, every third round, the body directly (`RunSlot`).
- **Facts (confirmed by the tests):** the three builders' EH try maps catch only `nGZCommandParser::cError` by
  reference (0xad024b and 0xad110a continue, 0xad0b8f sets status 3); any other exception propagates with the locals
  unwound, so the fakes throw both a real cError (game vtable 0xda5060) and a foreign type the test catches.
  ParseDefinition reads definition slot 22's *answer* (value pair fetched first, then the name pair, sets (name[0],
  value[1])); BuildShader's pair loop reads slot 22's *out buffer* instead, which the original leaves uninitialised when
  the definition doesn't write it (our fakes always write it). BuildShader statuses: 2 no script, 1 state 6, 3 nothing
  built or cError, 0 built (+0x200 handed out with AddRef); no usable device (0x18ae40, slot 35) gives the framework's
  stand-in. BuildErrorShader reasons 1..4 pick a Visualize* script (jump table 0xad12b0); it AddRefs +0x200 with no
  null test (ORIGINAL-BUG, kept). All three still call 0xae87d0 through `OriginalFunction` (`original_calls`).
- **Mutants:** 20 tried, 20 killed (18 on the builders and the cell-variable helper, incl. `catch (...)` for cError;
  one each on IsStateOne and QueryInterface's AddRef).
- **Smoke failure and fix:** the first merge crashed in game (15 of 18 runs, null read in CreateLine under BuildShader).
  The definition's slot 8 object is a cSGResource and its slot 22 is CopyKeyTo, which writes a whole 16-byte
  `cGZPersistResourceKey` (the +0x1d4 word is its group). Our buffer was 8 bytes; the fake wrote only 8, so the tests
  never overran it, while the game's key overwrote the script text pointer next to it. The original's frame leaves 16+
  bytes. Fixed (buffer is the key), the fake writes the whole key, mutants key-8-bytes and key-word-type killed.
  Lesson: a fake's out-parameter writes must be the real callee's full size, not what the caller reads.
- **render_object gate:** 145, unchanged (the constructor still installs the image's vtables, so the slots stay
  referenced; the vtables as ours is what moves it).
- **Left:** the 64 slots as our own vtables, then cMaterialAttributes (not started: budget), 0xae87d0.

### M1b part 4 result (lane RM, 2026-10-31)

- **Done:** cMaterialParser's vtables are ours: the primary (5 slots), the cIGZUnknown face (3), the cIBaseParser face (51) and
  the cIMaterialParser face (9), plus the three vbtables, hand-built in `src/sg/cMaterialParser.cpp` (`kOwn*`). Each face
  slot is a `VtordispThunk<Adapter>` (this class's bodies) or a `ParserShiftedThunk<Adapter>` (cBaseParser's and
  cLODLevelsTable's bodies, face less vtordisp less 0x138); the cIBaseParser face's slots 32 and 33 share EvalInt as in the
  game. The primary's slots 2 and 3 are the folded field-0x158 setter and getter (`SetField158`/`GetField158`). The
  constructor and destructor store our tables; the virtual bases' own constructors are not called (their stores are all
  overwritten before anything reads). Primary slot 1 and 4 are cBaseParser's `SetVariable` and `DropDefinitionsOfLevel`.
  Every slot body is ours; no slot is an `OriginalFunction` forwarder. The Ghidra names IsStateOne (0xad8b80) and
  QueryInterface (0xadc800) are recorded.
- **Tests:** `MaterialParser_VTables` (tests/sg_material_parser.cpp): an object built by our constructor, every slot of every
  face called through the table with random vtordisps, compared with the thunk and body rows (hook, target, forwarded
  arguments); slots 2 and 3 by behavioural set/get dispatch; the vbtable words and the vtable words of the whole object
  checked. Construct/Destruct tests normalise our table addresses to the image's.
- **Mutants:** 5 tried (slots 30/31 swapped, SetPair/RemovePair swapped in the material face, vbtable offset wrong, setter
  adds 1, AddRef/Release swapped); the first four killed, the last (5) was not re-run after the cleanup (see feedback).
- **render_object gate: 145 -> 172 (worse).** Owning the vtables roots every slot, so /OPT:REF keeps the whole cBaseParser
  and command-parser code that nothing used to reach, and those bodies reference 38 more original-image addresses
  (cparser*.obj, directoryscan, expressionevaluator data; cCommand and cError vtables). 11 references went away (the
  cMaterialParser/cIMaterialParser vtables and four data words). Original functions 45 -> 46 (0xae87d0, called by the
  BuildErrorShader body). The gate rises until those classes are ours too: cBaseParser's data tables and the command
  classes are the next group.
- **Left:** cMaterialAttributes (item 2): done in part 5.

### M1b part 5 result (lane RO, 2026-10-31)

- **Done:** cMaterialAttributes (0x28 bytes, vtable 0xe76188, 16 slots) is our class (`src/sg/cMaterialAttributes.*`,
  hook table `src/hook/tables/cMaterialAttributes.cpp`, status `db/status/cMaterialAttributes.jsonl`). The factory
  0xaaa3c0 is hooked (`CreateMaterialAttributes`); the constructor 0xaa9e40 and destructor 0xaa9fe0 are folded into the
  class; slots 3-14 and 15 (deleting destructor) are our methods. 0xae87d0 is ours (`OpenNameCell`, hooked) and
  cMaterialParser's two call sites call it; its `original_calls` fields are gone.
- **Layout decision: our own containers, not the original's node layout.** Nothing outside the class reads the members:
  the vtable is referenced only by the constructor and the destructor, the constructor is called only from the factory,
  the destructor only from slot 15, and the factory's address is stored only in the cDllCOMDirector table. So the members
  are `std::string`, `std::map<uint32_t, std::vector<Attribute>>`. The MSVC release layouts of those happen to coincide
  with the original's (map head at +0x20, size at +0x24, node key at +0x10, vector at +0x14/+0x18, a 0x28-byte Attribute
  with the width at +0x18 and the data vector at +0x1c), and the test uses that to walk the game's object raw and compare
  it with ours; static_asserts pin the sizes and offsets.
- **Slots:** 4 AddAttribute calls the source's key, width, size, fill in that order and sorts the group by name ignoring
  case with `std::sort` (the image's introsort and ours order ties the same; `stable-sort` was killed, so ties are
  tested). 14 ReadAttribute hands the stored data to the source's slot 2 and returns its result. 9 GetAttributeCount
  returns count + 1 for a present group (0 for none). AddRef, Release and the count are the folded shared bodies
  (`shared_vf_148fb0`, `16e890`, `15e970`): ours implement them for our objects, the game's bodies are unclaimed and not
  hooked.
- **Documented differences (original undefined):** absent keys in ReadAttribute, GetGroupKey past the count and
  GetAttribute past the group's end dereference bad memory in the original; ours return 0 or zeroed output. The tests stay
  inside the defined range for those.
- **Tests:** `SG_MaterialAttributes_Slots` (tests/sg_material_attributes.cpp): random op sequences through the outer
  slots on the game's object (original's vtable, built by the original constructor) and ours, comparing every result and a
  raw dump of the state after each step; the sources are fakes that log every call and write the whole out-buffer.
  `SG_MaterialAttributes_Create`: fresh state and the allocation blocks/bytes against the original, final release frees.
  `SG_OpenNameCell`: the original against ours for a nonzero and a zero name, with a fake service cell.
- **Mutants:** 25 tried (QueryInterface iid and addref, Release result and delete, GetName empty, SetName result, IsEmpty,
  sort case/stable/none, width order, GetGroupKey, GetAttributeCount, GetValueCount, ValuesBefore, FindAttribute
  position/name check/zeroing/case, GetAttribute position/index, ReadAttribute index/result, OpenNameCell flag/invalid);
  25 killed, 0 survived, 0 invalid.
- **Left:** cBaseParser's data tables and the command classes (the gate rises until those are ours; see part 4).

## cMaterialDefinition

### M2a result (lane RP, 2026-10-31): cMaterialDefinition, the structure and the object's bodies

- **Done:** nGZSceneGraph::cMaterialDefinition (the TXMT resource, class 0x49596978, 0x58 bytes from RZFastFixed0x58
  0x10f6704) is our class: `src/sg/cMaterialDefinition.*`, hooks `src/hook/tables/cMaterialDefinition.cpp`, status
  `db/status/cMaterialDefinition.jsonl`. Shape `cMaterialDefinition : cSGResource, cIMaterialDefinition` (face +0x2c,
  vtable 0xe76278, iid 0x49596972; the primary vtable 0xe76240; the virtual base cISGResource at +0x54 with its vtordisp
  at +0x50, vtable 0xe762e8). Members: name cell +0x30 (case-insensitive, 0xae87d0), type cell +0x34 (case-sensitive,
  0xae88d0), `std::vector<MaterialProperty>` +0x38 ({name, value} cells sorted by strcmp of the name), and
  `std::vector<MaterialTexture>` +0x44 ({name cell, texture handle} sorted by strcmp). QueryInterface: 0x4db89b60 gives
  the +0 part uncounted, the face iid gives the face counted. Records 18 / 19 answer "_txmt" / "5tm".
- **Switch:** the director's factory 0xaadf20 is hooked to `Create`; the constructor 0xaad1c0 is called only by the
  factory and the game's Clone (slot 7, held only by the class's own vtable), so no game object carries the class's
  vtables and every slot, the constructor, the destructors and the pool new / free are ours through the factory
  (`in_effect: callers`, trap `Hooks.nGZSceneGraph_cMaterialDefinition_Create=1`). Also hooked: 0xae88d0
  (`OpenNameCellCaseSensitive`, the flag-1 twin of RO's OpenNameCell) and 0xaaef20 GetTypeInfo (`GetTypeInfoAt`, a
  thiscall with a hidden out, ret 4, `this` unused), because the game's Write 0xaaefc0 calls it directly.
  0xaad4b0 (pool free) has one more reference, code:0xd75b04: the EH unwind funclet of the pool new inside the factory /
  Clone, both of which are ours, so it is unreachable.
- **Not ours (the entry points, the next part):** Read 0xaae280 (slot 3, 2574 bytes; adjustor 0xaae270), Write 0xaaefc0
  (slot 5, 425 bytes; adjustor 0xaaefae) and slot 14 0xaad850 (1677 bytes, `MergeVectorProperties` in our header: it
  rebuilds the properties, calling the sort 0xaac4e0 and OpenNameCellCaseSensitive). Ours forward to them through
  `OriginalFunction` with `this` = the face; the layout is the game's, so they work on our object. The texture handle's
  operations (0xae8240 copy, 0xae82e0 assign, 0xae82b0 destroy, 0xae8710 reset, 0xae8260 from id) and the manager getter
  0xae8750 stay the game's (shared with every class holding a texture; `original_calls`).
- **The STL:** insert 0xaaab80 / 0xaaf270 and equal_range 0xaacea0 are the toolchain's instantiations (library code, no
  entries); ours use `std::vector::insert` and `std::lower_bound`.
- **Tests** (`tests/sg_material_definition.cpp`): `SG_MaterialDefinition_Life`, 200 seeds of random slot sequences on
  the game's object (factory 0xaadf20) and ours (`Create`), comparing every result and raw state dumps, then the final
  Release, the pool's free list, the string pool's and the texture manager's per-handle counts, and that ours leaves no
  heap blocks; fakes for the string pool 0x10f9070 (interns by flag, so a wrong-case open shows), the texture manager
  0x10f8f94 and the 0x58 pool. Vector moves copy and destroy elements in a different order between the two STLs, so the
  order of the count calls is not compared, only the final counts and that none went negative.
  `SG_OpenNameCellCaseSensitive` (400 cases) and `SG_MaterialDefinition_GetTypeInfoAt` (the adapter against 0xaaef20).
- **Mutants:** 55 (QueryInterface face / self / null out, records 18 / 19, type info name / version / adapter result,
  Clone's name / type / textures, ClearProperties, name and type case, SetProperty's open order / replace / append /
  case, AddTextureById's found entry / handle / case, AddTextureByName's reset / case, IsEqual and IsEqualProperties
  field by field, the getters' null text, GetProperty, Find / FindNoCase, the counts, the sort order, 0xae88d0's flag and
  null case, the delete, AddRef, owner info, GetResourceConst, the constructor, the destructor, the pool new): all killed.
  Three (the count checks of IsEqual / IsEqualProperties and IsEqual's texture-name check) survived the first run:
  random objects almost never agree on everything else. A scripted case on every fifth seed now builds those near-equal
  pairs (a cleared object against one holding a null-name, null-value property, whose cells equal the reset cells past
  the other's end; two objects differing in one texture's name; textures against none), and the second run killed them.
- **Gate** (render_object, `-DTS2_NOIMAGE_FORCE=ON`, same tree): before 165 references (88 data, 36 vtables, 2 original
  data, 39 original functions, 7191 bytes), after the same. The class is not linked into render_object yet:
  `standalone/render_object.cpp` line 116 still maps class 0x49596978 to `NotOursYet<0xaadf20>` (87 bytes, the only
  reference this lane touches). Switching it to `Create` drops that, but links the class's own original calls (Read,
  Write, 0xaad850, the texture-handle helpers 0xae8240 / 0xae82e0 / 0xae82b0 / 0xae8710 / 0xae8260 and the manager getter
  0xae8750), so the gate goes down only once the next part and the texture handle are ours.
- **Next part (a Sonnet lane can take it):**
  1. Read 0xaae280: version-dependent (the type-info version is 0xb; below 10 the textures come through 0xaac880, a
     recursive 924-byte body next to the sort 0xaac4e0: check `tools/libmatch.py` first, both look like std::sort
     instantiations, i.e. library code). It reads the cSGResource part, the name, the type, the property pairs and the
     texture names, and calls 0xae7340 (a shared 286-byte string helper used by about 40 functions: not this class's,
     leave it the game's or take it as its own lane). Test: feed recorded TXMT streams (and fuzzed ones) through a fake
     cIGZPersistReader on both sides, compare the dumps the Life test already has (`Dump` in the test file).
  2. Write 0xaaefc0: the type info (ours, 0xaaef20), then the members; test with a recording fake writer, both sides,
     and a round trip Read(Write(x)).
  3. Slot 14 0xaad850: random property sets through the Life test's ops, then the slot, compare the dumps.
  4. Then drop the forwarding slots' `original_calls`, and the GetTypeInfo hook can stay (it is the adapter at its RVA).
  5. `standalone/render_object.cpp` line 116 (`{0x49596978, &NotOursYet<0xaadf20>}`, not this lane's file) can switch to
     `&nGZSceneGraph::cMaterialDefinition::Create`.
  Note (RP follow-up, SG_MaterialDefinition_ReadWrite): slot 14 0xaad850 loops to `count - 2` unsigned: under 2 properties it
  wraps (~4 billion empty passes). Keep it as `// ORIGINAL-BUG:`; the test calls slot 14 only with 2 or more. That test
  (originals on our object, 300 seeds) is the ready-made test for steps 1-3.

### M2a part 2 result (lane RR, 2026-10-31): cMaterialDefinition's Read, Write and slot 14 are ours

- **Done:** Read 0xaae280, Write 0xaaefc0 and MergeVectorProperties 0xaad850 are ours (`src/sg/cMaterialDefinition.cpp`);
  Create's forwarding `original_calls` are gone. The class's remaining original calls: the texture manager and handle
  operations (0xae8750, 0xae8240, 0xae82e0, 0xae82b0, 0xae8710, 0xae8260) and 0xae7340 (cdecl `(cRZString *out,
  const char *text, uint32 group, uint32 0)`: resolves a group-prefixed text; Read uses it for "textureName" values
  when the reader's group is nonzero, Write for every value with group 0).
- **Reader/writer slots used:** reader 0x28 GetUint32, 0x58 GetGZStr, 0x68 ReadTypeInfo, 0x70 GetGroup (no arguments;
  the header stops at slot 26, so slot 28 is called by raw index); writer 0x28 SetUint32, 0x5c WriteRZCharStr, 0x70
  WriteTypeInfo.
- **Read:** version < 8 reads and drops a string; value cell opened before the name cell; version < 11 rewrites an old
  vector text ("(1 2 3)" or "[1 2 3]" without a comma, length >= 5: brackets removed, the first space after each word
  that another word follows becomes ','); version < 10 sorts properties and textures after reading; version > 8 has
  the texture list. **Slot 14:** suffix "XYZ", a run of up to three names differing only by the last letter merges into
  one "base" property "v1,v2,v3" unless "base" exists (then only the answer is false when its value differs); skipped at
  exactly 2 properties. The unsigned `count - 2` bound is kept (ORIGINAL-BUG); under 2 properties it wraps.
- **Sorting:** the original's 0xaac4e0 / 0xaac880 are std::sort instantiations; ours use std::sort with the same key
  (strcmp of the name text). The test now generates up to 62 properties with duplicate names and different values
  (past the 32-element insertion-sort cut), and the order of equal keys matches in all 300 seeds.
- **Test:** SG_MaterialDefinition_ReadWrite now runs the game's Read/Write/slot 14 on the game's object against ours
  on ours; slot 14 runs in both read rounds. Mutants killed: see notes/feedback/RR.md.

## cShader / cShaderInit

### M2b: cShader part 1 (lane RT, 2026-10-31): 8 body hooks verified; cShaderInit not started
Done as the body-hook split (game vtable, constructor and factory stay; src/sg/cShader.*, tests/sg_shader.cpp,
db/status/cShader.jsonl): slots 0 (0xaa6950), 3 (0xaa6b30), 7 (0xaa6470), 11 (0xaa69e0), the pass helpers Reset
(0xaa6b60), Equals (0xaa6510), EndState (0xaa6e50) and the vector comparison 0xaa3d20. All verified by differential
tests (8 run, 0 failed; 41 mutants, 41 killed). Pass Reset still calls the original 0xae8730 for valid texture handles
(original_calls); its test plants a fake texture manager at the image cache 0x10f8f94.
Quirks reproduced: slot 7's QueryInterface out slot is its own argument slot, so an unwritten answer still holds `other`;
slot 11's flag dword is the ctx pointer's stack slot with the low byte replaced; ORIGINAL-BUG: a 0-pass shader loops 2^32
times in slot 11 (reproduced, not tested). Not hooked on purpose: base QI 0xaa6920 and Release 0xaa69a0 (shared template,
M3), slot 2 (5-byte jmp), slot 9 (7 bytes), slots 1/4/5/6 (shared bodies).
Left after part 1: see part 2 below.

### M2b: cShader part 2 (lane RV, 2026-10-31): 11 more body hooks verified; slot 8 Build and cShaderInit left
Hooked and verified (src/sg/cShader.*, tests/sg_shader.cpp): slot 10 (0xaa6c80), ShaderPass::Validate (0xaa6e80), slot 12
Dump (0xaa65e0), deleting dtor slot 13 (0xaa4e60), dtor body (0xaa4d70, writes the base vptr 0xe758a0), pass-vector
destroy (0xaa4ce0), Pass dtor (0xaa4ea0, original_calls 0xae82b0 texture handle destroy), class operator new/delete
(0xaa4d90, 0xaa4dc0, pool 0x10f6654), ctor (0xaa4a40) and factory (0xaa4fa0). Mutants: destroy path 9/10, ctor and
factory 6/6, slot 10 and Validate 8/8, Dump 5/6. Survivors are unobservable: the sized-delete size argument, and the
local cRZString Destruct in Dump (the cRZString allocator is outside the harness's counting).
Quirks: Validate's EH catch funclet (0xaa70e0, line 0x23a) is not reproduced (nothing in our code throws there); Dump's eax
at return is undefined and is treated as void; the pool's empty-list page refill is game code, not reached by tests.
The ctor and factory are ours, so render_object's class 0xebfb3834 can switch from NotOursYet<0xaa4fa0> (the game vtable
and slot 8 stay original until Build is done).
Left: slot 8 Build (0xaa5030, 939 B, ret 0x14; callees 0xaa53e0, 0xaa45e0, 0xaa47f0 and the Pass copy ctor 0xaa4ae0 may
stay OriginalFunction), the Pass copy ctor 0xaa4ae0, then cShaderInit (27 slots).
Original analysis (written before the code; its "cannot split" reasoning applies only to a full-class takeover):
Facts gathered (from the binary; re-derive offsets with tools/classinfo.py --disasm):
- cShader (size 0x98, pool 0x10f6654, page-mask free): +4 refcount, +8 enabled byte, +0xc..0x14 vector of 0xa4-byte
  Pass, +0x18 range table (13 pairs of uint, all 0xffffffff after the ctor, hashed by 0x98bff0 into +0x94), +0x80 first
  pass pointer, +0x84 dword, +0x88 pass count, +0x8c..0x8f flag bytes, +0x90 byte. The closure is self-contained: only
  the factory 0xaa4fa0 (referenced by the cDllCOMDirector ctor) creates it; vtable 0xe758dc is referenced only by the ctor
  0xaa4a40 and the dtor body 0xaa4d70. Base template cShaderComponentT<cIShader> (vtable 0xe758a0: QI 0xaa6920, Release
  0xaa69a0, deleting dtor 0xaa4e40) belongs to M3: inline its behaviour. Slots 1, 4, 5, 6 are shared bodies (implement, never hook).
- Pass (no vtable, 0xa4 bytes): +0 apply function pointer, 8 counted pointers, 4 counted vectors, 16 handles at +0x64
  (MaterialTextureHandle). Copy ctor 0xaa4ae0, dtor 0xaa4ea0, Reset 0xaa6b60, equality 0xaa6510 / 0xaa3d20; vector
  growth matches MSVC _Resize_reallocate with a copy-only element.
- Slot 8 builds passes from the cShaderInit descriptor vector (0xc8-byte elements at init+0x30) via 0xaa53e0 (4.2 KB
  jump table, only caller is slot 8) and validates through 0xaa6e80 (608 B, also called by slot 10); keep both as
  OriginalFunction until their own lane.
- cShaderInit (0xa4, plain game operator new): vtable 0xe760a4, 27 slots, ctor 0xaa7b90 -> sub-ctor 0xaa7bc0, dtor body
  0xaa7d70, deleting dtor slot 26 0xaa7f60, factory 0xaa82a0, index-grow helper 0xaa8460. Slots not yet read in full.
Plan: lane 1 = Pass struct + cShader class + factory with pool new/delete + ctor/dtor + slots 0-7 and 9-13, with
0xaa6e50 ported and slot 8's 0xaa53e0/0xaa6e80 left original; lane 2 = cShaderInit. Then switch the OriginalFunction
call sites in cMaterialParser (BuildShader) and cMaterialManager.

### M2b: cShader part 3 (lane RX, 2026-11-01): the class is ours end to end except cShaderInit
Hooked (src/sg/cShader.*, tests/sg_shader.cpp): slot 8 Build (0xaa5030), Pass copy ctor (0xaa4ae0), part-vector copy
(0xaa4980) and the two hash helpers (0x98bf40 mix, 0x98c220 combine). Build's callees 0xaa53e0 (descriptor to pass
translation, 4.2 KB jump table), 0xaa45e0 and 0xaa47f0 stay OriginalFunction. The harness cannot reach 0xaa53e0's success
path (it needs live device state), so tests force the failure path and the structural tail (SG_ShaderBuildTail runs the
hash, flag and m84 logic from prepared passes). Build is therefore `reimplemented` with that gap stated.
Own vtables: `cShader::kOwnVTable[14]` (stored by Construct, game 0xe758dc) and `kOwnBaseVTable[14]` (stored by the
destructor body, game 0xe758a0), built as dynamic statics like cMaterialParser. Shared-body slots are members of ours,
each tested against the image body (SG_ShaderTables compares the whole table slot by slot). Nothing compares either vtable
pointer: 0xe758dc is referenced only at 0xaa4a4f (ctor) and 0xaa4d78 (dtor body); 0xe758a0 only at 0xaa4962 and 0xaa4cd2
(0xaa4960, inlined into the ctor and the dtor body). Both are stores.
standalone/owned_statics.cpp needs aliases for 0xe758dc and 0xe758a0 (outside this lane's file list).
render_object's class 0xebfb3834 can switch from `NotOursYet<0xaa4fa0>` to `cShader::Create`.
Left: cShaderInit (27 slots, vtable 0xe760a4), and 0xaa53e0 / 0xaa45e0 / 0xaa47f0.

### M2b part 4: cShaderInit (lane RZ, 2026-11-01)
The whole class is ours (src/sg/cShaderInit.*, src/hook/tables/cShaderInit.cpp, tests/sg_shader_init.cpp): 0xa4 bytes from
plain game operator new, ctor 0xaa7b90 and sub-ctor 0xaa7bc0, destructor body 0xaa7d70, descriptor-vector destroy 0xaa7cf0,
deleting destructor (slot 26) 0xaa7f60, factory 0xaa82a0, element helper 0xaa8460 and slots 0, 3, 5..20, 22..25. Layout: +0 vptr,
+4 count, +8 ShaderInitData (0x9c: attribute, object vector, name pool, descriptor vector, 26 range dwords). A descriptor is
0xc8 bytes. Slot 21 returns this+8, which is what cShader::Build reads (ShaderInitView).
Own vtable `cShaderInit::kOwnVTable[27]`; the shared-body slots 1 (0x148fb0), 2 (0x292890), 4 (0x14a260) and 21 (0x142840) are
members of ours tested against the image bodies, not hooked.
Quirks reproduced: the part/object adders count the object before looking the pass up (the lookup may reallocate) and count it
once more in the vector; PartIs/ObjectIs dereference every element (a null element crashes the original, so tests never append
nulls); an empty descriptor's texture value is uninitialised memory in the original (tests mask it when the handle is -1).
The name pool (0xc62f50 family) holds polymorphic objects in a char buffer and stays original (0xc62f90, 0xc63030, 0xc63120), as do
the object-vector assign/grow (0x20c700, 0x255dd0), descriptor copy/destroy/realloc/fill (0xaa7130, 0xaa7500, 0xaa75e0, 0xaa7a90)
and the texture-handle functions (0xae8280, 0xae82e0, 0xae82b0, 0xae8730).
render_object's class 0x7c17ff25 can switch from `NotOursYet<0xaa82a0>` to `cShaderInit::Create`.
standalone/owned_statics.cpp needs an alias for the game vtable 0xe760a4 (outside this lane's file list).
Left: the pool and vector helpers above; 0xaa53e0 (Build's translator) can now be tested on its success path with a real
cShaderInit built by our factory, but it still needs live device state.

### HS result, part 2: the pass apply functions and cShaderInit's helpers (lane VB, 2026-11-02)

- cShader (src/sg/cShaderPassApply.cpp, src/sg/cShader.cpp): the 8 pass apply functions 0xaa44d0, 0xaa3d70, 0xaa3e90,
  0xaa3f70, 0xaa40a0, 0xaa4180, 0xaa42b0, 0xaa43a0 (one template over preprocess / hardware / mapped; the pass stores ours,
  ShaderPass::ApplyFor), PassVec::FillDefault 0xaa47f0 and PassVec::Reallocate 0xaa45e0. Tests: tests/sg_shader_pass_apply.cpp,
  tests/sg_shader_pass_vec.cpp. The draw call's third argument (Render) is a byte: every image implementer reads only the
  low byte, so the apply tests compare that byte only (a correction of the test, both sides).
- cShaderInit's name pool (src/sg/cShaderNamePool.cpp, tests/sg_shader_name_pool.cpp): Construct 0xc62f50,
  CopyConstruct 0xc62ee0, Clear 0xc630e0, Destroy 0xc62f90, Assign 0xc63030, Add 0xc63120, on std::vector<char> plus
  std::vector<uint32_t> (MSVC 14.40 layout). Entries are cIShaderName (slot 0 destroy, slot 1 copy to, size word at +6).
  0x751740 (Count) stays unhooked (shared_vf).
- cShaderInit's descriptors (src/sg/cShaderInit.cpp, tests/sg_shader_descriptors.cpp): DestroyRange 0xaa7500,
  FillDefault 0xaa7a90, ShaderTextureSlot::CopyConstruct 0xaa7c80, CopyRange 0xaa77c0, CopyCount 0xaa7930, AssignCount
  0xaa72f0, ShaderDescriptor::Assign 0xaa7db0, Reallocate 0xaa75e0, the vector Assign 0xaa7130. The descriptor's copy
  constructor is inlined in 0xaa77c0 / 0xaa7930 (ShaderDescriptor::CopyConstruct, no RVA). FillDefault leaves the texture
  slots' values as they were: after a Reallocate those are uninitialised heap, so the test does not compare them.
- Left: 0x20c700 (object-vector assign) and 0x255dd0 (object-vector grow), shared template instantiations, still called
  through OriginalFunction by Clone, the descriptor Assign functions and Append.

## Shader controllers and attributes: cShaderComponentT, cShaderControllerT, cAttributeFFTextureMatrix and the other cAttribute* classes

### M3b: shader controllers and attributes (lane TG, 2026-11-01), partial

- **Templates:** cShaderComponentT / cShaderControllerT, one template per family over the 18 copies
  (src/sg/SGShaderControllers.*): 188 entries, 152 hooked (QI, IsEquivalent, Init, the deleting destructors, the
  template ctor/dtor, the folded Release bodies), component ctor/dtor in effect through callers. 7 difftests, 16/16
  mutants.
- **Attributes, whole** (src/sg/SGShaderAttributes.*): cFFDepthOffset, cFFApplyTransforms, cAnimColorScalarAlpha,
  cAttributeFFScaleModelToViewTransform, cAttributeFFMaterial, cAnimatedTiledTexture. Each factory (the COM director's
  class entry) is hooked and makes ours; every body is in effect through callers (trap `Hooks.nGZSceneGraph_<cls>_Create`).
  AnimationWave 0xac6820 is hooked (cAnimatedTextureMatrix 0xac23a0 still calls it). 59 entries, about 11.6 KB with the
  templates. Folded or shared bodies get no entry: 0xac6300 (shared_vf, SetTarget), 0xac6180 / 0xac6160 (shared with
  cAttributeColorScalar), 0x4daa20, 0x15e510, 0x300a0, 0x1b9a50.
- **Facts:** cFFMaterial's colour words are alpha first, then r, g, b (RenderStates.h names them r,g,b,a). The material
  is dynamic (slot 9 skips, slot 10 returns 1) when the colour event or any parameter index is >= 0; Apply scales
  diffuse and ambient by the item's event source (+0x2c, slot 10 GetEvent for 0x8c798726). cAnimatedTiledTexture:
  tile = floor(time * rate) mod (columns * rows), unsigned; target 0 sets cFFTextureTransformTypeState (count 2) and a
  texture matrix {w,0,u,0},{0,h,v,0},0,{0,0,0,1}; targets 1 / 2 write the two rows to vertex / pixel constants
  (device slots 19 / 22, start = the index, count 2); slot 12 runs the frame at time 10.0. The image leaves
  cFFTextureTransformTypeState's +0xe..+0xf unwritten (the test masks them).
- **Tests:** tests/sg_shader_attributes.cpp, one behaviour world for all classes (120 seeds: parts from each side's
  factories, every slot through the object's own vtable, a device state fake, drawn item and clock, twins for
  HashParams / EqualParams), plus the placement ctor/dtor test and sg_animation_wave. Run with `mutate.py
  --force-filter`: the folded-slot overrides are reached through our vtable, so the filter can't infer the tests.
  Mutants: 111 over four runs, 109 killed; the alpha EqualParams-short survivor was killed after the twins went in
  (run 4), and "wave: sine in float" survives, near-equivalent.
- **Lane TJ (2026-11-02):** cAttributeColorScalar (factory 0xac3240) and cAnimatedTextureMatrix (factory 0xac30e0, 12
  entries, 22 mutants killed; the 2*pi mutant needed 6.283186f since 6.2831853f == 6.2831855f as floats) are whole.
- **Lane TK (2026-11-02): cAttributeFFTextureMatrix whole** (src/sg/cAttributeFFTextureMatrix.*, status group
  cAttributeFFTextureMatrix, 23 entries; only the factory 0xac32a0 is hooked, every body and the four helpers
  0xac4bd0 / 0xac2bc0 / 0xab79e0 / 0xac6090 in effect through it). Two image calls are left, both pure: 0x19c0e0 (the
  frame to a 4x4, Apply source 1) and 0x1a5660 (Euler to 3x3, slot 12; calls libm sin / cos), in original_calls.
  Facts the decode below lacked or got wrong: slot 10 returns 1 and drops the cache when any parameter is indexed,
  else 0 for source 0, else caches M and returns 1; slot 9 sends anything only for source 0 with no index, and only
  when M is not near the identity or the swizzle is not 0x123; Apply sends the type command first, then the matrix
  (slots 9 and 12 too, except slot 9 sends the matrix first); after a swizzle, M multiplies rows 0..1 with rows 0, 1, 3
  (2D) or rows 0..2 (axis turn); source 3's basis is (n x up, n, (n x up) x n) for the item's offset direction n and
  up (0, 0, 1) at 0x106f428; the 2D M is the product chain toCenter * scale * turn(about up) * translation *
  fromCenter (MatrixProduct, in that order), the axis M the quaternion turn by angle * 2pi / 2 about the normalised
  axis, times the scale. 1 / |v| is computed as 1.0f / (float)sqrt((double)len2). The context's +0xdc..+0x10f is a
  13-word frame (cShaderApplyContext::mFrame; tentatively the view's). Slot 12 case 4's fixed rows are
  {1,0,0,0},{0,0,1,0},{0,1,0,-0.1},{0,1,0,0} (the Ghidra decompile mislabels the case; trust the disassembly).
  Swizzle nibbles above 3 make the image read past its stack copy; ours writes a zero row there (the tests keep
  nibbles 0..3). Tests: tests/sg_ff_texture_matrix.cpp (behaviour, a 1500-seed apply script, 3000-seed BuildMatrix and
  IsDynamic direct, the two helpers alone, ctor/dtor); the world and script moved to tests/sg_shader_attr_fx.h, with a
  `gWorldExtra` hook for a file's own world parts and a `tailMask` for trailing padding.
- **The decode lane TJ left for it** (kept for the facts; superseded by the bullet above where they differ):
  factory 0xac32a0, ctor 0xabdc00, 0x88 bytes, dtor 0xabe2f0, deleting 0xabea10; vtable 0xe77578. Layout (+0x10 start; HashParams 0xac35e0 hashes 0x50 bytes from +0x10,
  EqualParams 0xac5470 compares the same): +0x10 stage (slot 14 0x1eeb80, folded, no entry); +0x14 u16 source 0..5
  (slot 15 0xac6600; default 0), +0x16 u16 swizzle (slot 16 0xac65f0; default 0x123); +0x18 bool has-transform, +0x19
  bool rotation-is-quaternion-axis (slot 18 sets both = 1), +0x1a bool invert (slot 23 0xac62c0, also sets +0x18);
  parameter slots hold an event index (-1 = none) then the constant: +0x1c/+0x20 rotation angle (slot 17 0xac64d0;
  slot 18 0xac64a0 also sets +0x24 index and +0x28..+0x30 an axis, 0xac64a0 writes +0x18 = 0x101), +0x34/+0x38 scale
  (slot 19 0xac65b0), +0x3c/+0x40..+0x44 scale2 (20 0xac65d0), +0x48/+0x4c.. translation (21 0xac6700), +0x54/+0x58..
  (22 0xac6400); cached 3x3 at +0x60 (identity), +0x84 bool cache valid. Defaults after the ctor: +0x1c -1, +0x20 0,
  +0x24 -1, +0x28 1.0, +0x2c 0, +0x30 0, +0x34 -1, +0x38 1.0, +0x3c -1, +0x40 1.0, +0x44 1.0, +0x48 -1, +0x4c -1,
  +0x50 0, +0x54 -1, +0x58 -1, +0x5c 0. Helpers: 0xac4bd0 (42 bytes: true if any of the six event indices at
  +0x1c/+0x24/+0x34/+0x3c/+0x48/+0x54 is >= 0), 0xac2bc0 (1279: build the 3x3 from the parameters, a quaternion
  rotation, scale and translation products, optionally inverted), 0xab79e0 (737: is a 3x3 within 32 ulp-steps of
  another, sign-folded integer compare), 0xac6090 (196: reorder the rows of a 4x4 by the swizzle nibbles), 0x1a5660
  (369: Euler angles to 3x3; slot 12 uses (pi/2, pi, pi/4)), 0x19c0e0 (216: the context's 13-word frame to a 4x4).
  Slots 9 0xac20b0, 10 0xac2b70, 12 0xac1a50, Apply 0xabfd60 (5950 bytes; six sources: 0 own matrix, 1 world frame,
  2 context 3x3 at +0xe0, 3 item direction cross products, 4 context projection at +0x144, 5 inverse of the item
  frame; the optional parameter products have fixed float orders, which the tests compare bit for bit). Needs
  QuaternionToMatrix, MatrixProduct, InverseMatrix3 and FramePlacement::Invert from compgeom (check which are ours).

## Apply and validate strategies: cApplyShaderProgramConstants, cApplyFFLightStates, cValidateRenderShaderContext, cApplyRenderTarget; the program constants' sources

### M3a result: the apply and validate strategies (lane TM, 2026-11-02)

All four classes are whole, one commit each. Each factory is hooked and makes ours, and every body is in effect through
it (trap `Hooks.nGZSceneGraph_<cls>_Create=1`; a body two classes share names both hooks, joined with `&`). Tests:
tests/sg_apply_render_target.cpp, sg_validate_render_shader_context.cpp, sg_apply_ff_light_states.cpp,
sg_apply_shader_program_constants.cpp, all on the tests/sg_shader_attr_fx.h world.
- **cApplyRenderTarget** (17 entries, 26 mutants): factory 0xac31a0. The Apply chain is also reached from cResourceNode
  code, so its trap names both hooks. Its context fields are in SGShaderAttributes.h.
- **cValidateRenderShaderContext** (12 entries, 34 mutants): factory 0xac3370; LightTypeName 0xaeed90 hooked (three
  other image callers). The Release stub 0xac6060 is shared with cApplyRenderTarget only, so it has an entry with both
  hooks in its trap. Type 0's light count would read word 0x1fffffff of the key (ORIGINAL-BUG comment); nothing sets an
  ambient requirement.
- **cApplyFFLightStates** (7 entries, 23 mutants): factory 0xac3140, vtable 0xe76ea0. Init checks the caps' slot 20
  (hardware vertex processing) and slot 21 (0x19: most enabled lights) against the key's span (report lines 0x943 and
  0x94b). Apply sends the enabled-lights command (the light count, and the 12 bytes before the item's light table:
  the ambient colour, tentative), then one cFFLight command per light, in the order directional, point, spot. The item's
  table (+0x3c, cShaderApplyItem::mModulators) holds *holders*: the first word is the cIStandardLight (slot 50,
  GetParams(out, item)); the direction is minus words 9/12/15 and the position is words 16-18. The colour products
  are `mulss colour, intensity`, and MSVC commutes a plain `*`, so ours uses compgeom's MulS/AddS (which NaN a NaN pair
  gives). Slot12 sends the defaults: a white ambient and white lights with parameters (0.2, pi/10, pi/3, 10).
  RenderStates.h's tentative cFFLight field names (+8..+0x34) don't fit this; the cpp's local LightState view names
  them by what is stored. Release stub 0xac6050 is shared with cApplyStencilStateForOverlappingReflections (no entry).
  The factory and constructor next door (0xac3160, 0xabd7b0) are cApplyForceDepthTestFailCondition's.
- **cApplyShaderProgramConstants** (11 entries, 36 mutants): factory 0xac3200, vtable 0xe76afc, 0x4c bytes: six
  Range{begin 0x7fffffff, end 0} at +0x10, std::vector<Binding> at +0x40 (Binding: source, file, channel, dimension,
  register, count, offset, value, 3 more words; 0x20 bytes). Init picks the vertex limits by the third argument's low
  byte (0x10/0x12/0x14, else 0x11/0x13/0x15) and asks the pixel ones (0x16..0x18) only as the checks reach them, then
  checks each binding's count by its source (lines 0x1b5..0x260). Apply returns at once when the context's +0x298
  byte (cShaderApplyContext::mSamePass) is set. Otherwise it locks the used files on the device state (slots 19-24, in
  the order vertex float, pixel float, vertex int, pixel int, vertex bool, pixel bool) and fills each binding. A
  boolean is `value != 0` (NaN gives 1). The float and integer files go through the image's 0xaba8f0 and 0xab7cd0
  (10-11 KB each, cdecl (dst, binding, ctx, item), Apply their only caller). Those two **are left**, called through
  OriginalFunction and listed in original_calls: the next lane's work (the source enum and its ~45 cases). Slot12
  locks and zeroes each used range. AddConstant 0xabeae0 is push_back (0x2f23c0 is the STL realloc) and widens the
  range. Hash is 0 when empty, else JenkinsHash. Release stub 0xac6040 is in seven classes (no entry).

### M3a result: the program constants' sources (lane TP, 2026-11-02)

FillFloatConstants 0xaba8f0 and FillIntConstants 0xab7cd0 are ours (src/sg/ShaderConstantSources.{h,cpp}, one
template `Fill<T>` for both). Apply calls ours, and both are in effect through it (Apply is their only caller). Test:
tests/sg_shader_constant_sources.cpp, its own world (6000 drawn worlds per function, every source 0..0x2a plus some
above it; every fake call and the whole 1024-word destination are compared). The Apply test now stands in for both on
both sides with shape::StandIn.
- **One template made twice.** The integer file gets the same float values truncated (cvttss2si / cvttps2dq: out of
  range or NaN gives 0x80000000). Ours uses `_mm_cvtt_ss2si`, because MSVC folds `int(float(double(unsigned)))` back
  to the unsigned word. Some words are copied raw, not converted: the matrix sources' bytes (6..0xe, 0x12, 0x13), a
  frame row's three rotation words (0..5, 0xf) and the bone rows (0x10). Sources 0x28/0x29 hold counts: floats in the
  float file, the raw integers in the int one. Source 0x14's frame number (ctx+4) goes uint -> double -> float, also
  in the int file.
- **Binding +0x10** is four floats (`mValues[4]`): source 0x18's vector (and 0x19's when the item has no values), 0xe's
  offset in [0], and a boolean binding's value in [0].
- **Context** (beyond cShaderApplyContext): +4 the frame number (tentative), +0x110 a second frame (tentative: the
  frame's inverse), +0x184 the view (also read through mOuter for source 0xa), +0x1c4 another matrix. **Item**: +0xc a
  bone source (slot 5: GetBoneSet(uint16 at +0x14); +0x14 of the answer is an int32 index array into the transforms),
  +0x24 the scalars, +0x28 an array of 13-word frames, +0x2c an event source that also answers slot 54 (a placement),
  +0x30 the run nibbles (n0..n3: directional, point, spot lights; n3..n4: modulators; n4 alone: a depth range whose
  slot 42 takes **no** arguments), +0x38 counts (bits 15..21 bones, 22..27 scalars).
- **Light layouts 0x1d..0x2a**: see the case comments in the cpp. The spot cosines are `(float)cos((double)(x*0.5f))`
  of q7 and q7+q6. 0x1d's spot row has 1/(outer - inner); the others have 1/(inner - outer).
- Callees: FramePlacement::Invert/Compose, QuaternionToMatrix and GetDetailObject are ours, and since lane TR so are
  the 4x4 helpers (src/compgeom/Matrix4.*: frame and rigid to 4x4, multiply, inverse, rotation-scale-translation
  matrix, rigid rotate vector; see "Lane TR" below).
- ORIGINAL-BUG: frame sources 0..5 with count 0 loop on (a do-while on --count).

## cTextureManager / cTextureImage

### M4a: cTextureManager (lane SA)

**The texture handles, ours and verified (9):** the manager getter 0xae8750 (`nGZSceneGraph::TextureManager`, cache
0x10f8f94 owned through `TS2_OWNED_STATIC`), the handle ops 0xae8240 CopyFrom, 0xae8260 InitFromId, 0xae8280
InitFromName, 0xae82e0 Assign, 0xae82b0 Destroy, 0xae8710 Reset, 0xae8730 Get, and 0xae7340 ResolveResourceName
(src/sg/cTextureHandle.cpp, tests/sg_texture_handle.cpp, 21 mutants). Every src/sg caller now calls ours
(cMaterialDefinition, cMaterialManager, cShader, cShaderInit, cTSSGSystem capture / lifecycle, cViewerRefNodeBaseImpl);
the nine RVAs are gone from their `original_calls`. The SG tests that stood the handle ops in over the image now let
the real ones run on both sides, with a fake manager in 0x10f8f94 (slots 17-19 / 23 / 25 logged). cSceneGraphSystem.cpp
still has its own local call to 0xade2e0 (the manager's static getter, not ours yet).

**The manager, half-state (10 slots ours and verified, hooked on the original's object):** src/sg/cTextureManager.h
declares the object (0x38 bytes): +0 vptr, +4 refs, +8 TextureFilterParams {a, mode, bias} (3, 3, -0.2 at start),
+0x14 `std::map<Cell, Record>` (0x28-byte nodes: key cell +0x10, Record +0x14 = {name cell, texture (counted), count,
id, flags}), +0x1c `vector<Record *>` by id, +0x28 `vector<uint32_t>` free ids, +0x34 started. Flags: 1 = drop the
record at its last release (else the texture's slot 4), 4 = slot 21 reloads it (texture slot 3). Ours: slots 15 Start
(0xade290, 5 bytes), 16 Shutdown (0xade970), 18 AddRefId (0xaddc00), 19 ReleaseId (0xade7e0), 21 ReloadStale
(0xade2a0), 22 SetFilterParams (0xade930), 23 GetTexture (0xadeb10), 24 GetInfo (0xadeb20), 25 Has (0xade230), 26
GetFilterParams (0xadeb70); tests/sg_texture_manager.cpp (SG_TextureManagerSlots: drawn call sequences on two managers
built alike in our STL, the image's 0x824632 / 0x824624 stood in; 21 mutants). The map erase / clear (0xadebb0,
0xadcf80) and the vector push (0x1bdb73) are library instantiations: ours uses our STL.

Facts: the static is 0x10f8c08 (vtable 0xe7c778, 30 slots), made by the thread-safe getter 0xade2e0; ctor 0xadd170
makes the default white A1 texture through class 0x6b81dfe9 and registers for device event 0xca7ccbbc (slot 3,
0xade1f0, calls 0xaddd40 on it); dtor 0xadd480, deleting 0xadd6e0. Slot 0 QI (0xade3c0): null out false; iid
0x4b05e995 this + AddRef; else the behaviour's 0x148310 (tail call). Slot 17 FindOrLoad (0xaddd60, 1091 bytes) makes
classes 0x2a956dad / 0x4a956dd2 by resource type, falling back to 0x6b81dfe9 at 128x128; slot 20 Add (0xade4e0), 27
SamplerStates (0xade9e0), 28 Range (0xadd700). Slot 23 has no checks (a -1 id reads before the vector); the last
release of a record without flag 1 and without a texture crashes the original (texture slot 4 on null), as ours.

**render_object's texture classes:** 0x2a956dad (0xab4e50), 0x4a956dd2 (0xab4d30) and 0x6b81dfe9 (0xab4d90) are made
by the manager (slot 17; 0x6b81dfe9 also by the ctor) and by other callers: 0x2a956dad by cLightingManager slot 3,
cTSUIPaintingService::CreateTexture (ours) and eight unnamed functions (0x2c5340, 0x2c5a60, 0xaec1a0-0xaece50,
0xb40560); 0x4a956dd2 by 0xaebd70, 0xaec4a0, 0xb40560; 0x6b81dfe9 by 0x294c90, 0x29dc70, 0x3bcfa0, 0xaf3860, 0xbab290,
0xbad8f0. 0x2b96cc2b (0xab4df0, cRenderTargetTexture) is not the manager's: cShadowManager slots 8 / 10, cPoolManager
slot 3, cBlurredRenderTarget slot 6, cRenderTargetCommand slot 4, 0x2e7d10, 0x373250, 0xcbeec0 (tools/immsearch.py).
So render_object reaches the first three as soon as it constructs the manager or loads a texture by name; 0x2b96cc2b
only through the render-target users.

Left for M4a: slots 0 (QI), 3 (event), 17 FindOrLoad, 20 Add, 27 SamplerStates, 28 Range; the getter 0xade2e0, ctor
0xadd170 and dtors (then our own vtable and the whole class); cTextureImage (0xab4400 family) not started. M4b
(cImageData, cLevelInfo) waits on it.

#### M4a part 3 result: the whole cTextureManager (lane SE, 2026-11-01)

**The class is ours, on our own vtable.** `nGZSceneGraph::cTextureManager` (src/sg/cTextureManager.h / .cpp) is now
built the material manager's way: a cAbstractBehaviorTypeST behaviour with its own vtable (QI, AddRef / Release,
OnEvent, slots 15-28), the getter 0xade2e0 as a function-local static, and the behaviour registry 0xade400
(`cTextureManagerBehaviorTraits::RegistryFor`, with its first-use build 0xadce80 folded in; the device event's record
ends in 4, as the code stores it over the image record's 8). Hooked: the getter 0xade2e0 and the registry 0xade400.
The ten slot hooks SA put on the original's object are removed. `tools/callers.py` (assuming those two) finds no
reference left to the ctor 0xadd170, dtors 0xadd480 / 0xadd6e0, QI 0xade3c0, OnEvent 0xade1f0, its device handler
0xaddd40, slots 15-28 or 0xadce80, so they are in effect through callers (trap: the getter hook). The static 0x10f8c08
and the image's vtable 0xe7c778 are no longer reached. cSceneGraphSystem slot 6 and its Init / Shutdown now call ours.

New ours: QI 0xade3c0 (null out false; 0x4b05e995, 1 and the behaviour's second id this + AddRef; the type id this
without one), OnEvent 0xade1f0 (the behaviour's handler, then the device event: data word 1 == 1 reloads the stale
records, slot 21), Add 0xade4e0 (a new record per name, an id from the end of the free list or a new one; the texture
counted-assigned; flags set, not or'd), FindOrLoad 0xaddd60 (found: its id; else the name's "_txtr" resource in its
group, then group 0x1c0532fa; loaded: class 0x4a956dd2 for kind 6, else 0x2a956dad, slot 9 loads it, Add flags 1;
missing: class 0x6b81dfe9 at 128x128 drawn by a cMissingTextureUpdateStrategy, Add flags 1), SamplerStates 0xade9e0
(texture not ready: fixed states; else per filter mode, the bias, and the kind's applier 0x18cc40 for 4 / 7 or 0x18e320
for 6), Range 0xadd700 (end then begin from the image's cTextureHandleIterator pool 0x10f8bd4 / vtable 0xe7c7f4, moved
to the first live id; cursor = begin's Clone). The ctor (AddRef, reserves 100 / 50, filter 3 / 3 / -0.2, the white
1x1 texture with its cTextureUpdateStrategy when a device is up, Add flags 2, the graphics system's device event
connected) and the dtor (members in reverse).

Still the game's (original_calls): the update strategies' constructors 0xaebd30 and 0xadd030 (their objects are freed
by their own deleting destructors with the game's delete, so ours makes them in operator new memory), the samplers'
appliers 0x18cc40 / 0x18e320, and the iterator class (its pool and its vtable's slots). The texture classes are made
through the director's COM.

Tests (tests/sg_texture_manager.cpp): SG_TextureManagerSlots now runs the whole object life on each side (the
original's ctor 0xadd170 against ours, 500 seeds, random calls through every slot, the state and the heap noted after
each, then the dtor or the last Release reaching 0xadd6e0); SG_TextureManager_Registry (0xade400, as the material
manager's); SG_TextureManager_Get (one-sided: the original's function-local static cannot be built in the harness).
Fixture fix in tests/sgfx.h: Pool::Handle's text storage is now `difftest::NotCounted` (Reset keeps each text's
buffer, so a text past the small-string buffer, "DefaultWhiteA1Texture", counted only on the side that interned it
first).

Left for M4a: cTextureImage (0xab4400 family) and the update strategies (0xaebd30, 0xadd030); then M4b (done by lane
SJ, below).

#### M4a part 4 result: cTextureImage whole and the update strategies (lane SJ, 2026-11-01)

- **render_object NOIMAGE gate: 138 distinct references.** That is 67 address data, 21 address vtables, 1 original
  data and 49 original functions. It was measured in this worktree: `-DTS2_NOIMAGE_FORCE=ON`, the map without
  noimage_trap.obj lines, then FORCE off again. There was no same-tree before run; main's last figure was 132. The
  lane's new original is the drawing 0xae8310 (1022 bytes); the rest of the rise is the slots our own vtables root.

**cTextureImage is ours, on our own vtable** (src/sg/cTextureImage.h / .cpp; group cTextureImage, 13 entries).
- **Shape:** `cITexture` (slots 3-8) and `cITextureImage` (9-12) are the interfaces, and `cTextureImage final` is 0x28
  bytes: refs +4, kind +8, levels +0xc, image +0x10, key[4] +0x14, device texture +0x24.
- **Hooks:** the director's two factories, 0xab4e50 (kind 4) and 0xab4d30 (kind 6, a cube), are hooked and make ours.
  The ctor 0xab4400, the dtor 0xab45d0 and the deleting dtor 0xab4680 are then in effect through callers, and so are
  QI 0xab5aa0, Restore 0xab5610, ReleaseResource 0xab5e00, Load 0xab5d20, SetImage 0xab6430, GetImage 0xab5e30 and
  Upload 0xab4780. The game's vtable 0xe766c4 is stored only by the ctor and dtor. The trap is the two factory hooks.
- **Shared slots:** 2 (shared_vf_244060 Release), 6 (shared_vf_ab46a0 IsCreated) and 8 (shared_vf_ab4b50
  GetDeviceTexture) are reproduced as members and never hooked.
- **CreateDeviceTexture:** 0xab57d0 (cdecl texture, desc, quality, kind) is hooked (both Restores that call it
  are ours since lane TH).

**Facts:**
- **The constructor** asks the director's COM for class 0xbb892198 (kind 4) or 0xe944343d (kind 6) with the
  cITextureBase iid.
- **QueryInterface** answers 1, 0x2a956da8 and 0x4a95f211.
- **Restore** runs only when the device is up and ready. It chooses the description with 0xab5f50 (ours and hooked since
  lane TH, src/sg/TextureDescription.cpp), creates the device texture and
  uploads. When the image is a resource with a key and its flag is clear, it keeps only the key (type, group,
  instance, resource): the resource service forgets the image (slot 10) and the image is released.
- **GetImage** loads the image back through the resource service's slot 3 (iid 0xfc49f6ee), from the key's group,
  then from the game's group 0x1c0532fa when the first try failed and the group was not already that one. It tests
  only the low byte of the result.
- **Upload**, for a 2D texture: a cMemBlockAsTexture of the image's levels, then the manipulator's copy (slot 8), or
  its normal map (slot 13) for image kinds 6 / 7. For a cube, each face comes from the image's entry min(face, last).
- **The four helpers the task named:**
  - 0x18ae40 is GetCurrentDeviceCached (ours).
  - 0x1466e0 is RZAutoRef::ReleaseObject (ours).
  - 0x20db69 is the 37-byte resource-manager getter (the framework's service 0x056b906e / 0x656b8efc), written out
    in place as the other lanes do.
  - 0x14323f is GetFrameworkThunk.
- **ORIGINAL-BUG:** the device-texture calls are unchecked. They run through a null device texture when the device
  is down.

**The update strategies are ours** (src/sg/cTextureUpdateStrategy.h / .cpp; group cTextureUpdateStrategy, 4 entries).
- **The plain strategy:** nRZSceneGraph::cTextureUpdateStrategy, vtable 0xe7d084, 8 bytes {vtable, refs}.
- **The missing-texture strategy:** nGZSceneGraph::cMissingTextureUpdateStrategy, vtable 0xe7c818, 0x28 bytes, with a
  cRZString name at +8.
- **The texture manager** now makes ours. Its entries 0xadd170 and 0xaddd60 no longer call 0xaebd30 / 0xadd030.
- **The slots** as cProceduralTexture calls them:
  - 3 is GetDescription(&w, &h, &levels, &quality); the shared 0x147110 answers false in al only.
  - 4 is Update(surface, face, first).
  - 5 is Finish; the shared 0x140fe0 is an empty ret.
  - 6 is the deleting dtor.
  - 0, 1 and 2 are the shared QI 0x178b20, AddRef 0x148fb0 and Release 0x4b1d00.
- **The plain Update 0xaed030** locks the surface (slot 26), sets every row's rowBytes to 0xff at bits + pitch * row,
  then ends the lock record: Unlock, and the surface released when the unlock left it set. It is **hooked**, because
  the video strategy's Update 0x405130 calls it directly.
- **The missing strategy's constructor 0xadd030** stores "(empty)" for an empty name and upper-cases any other name
  (0x1ad17a is `_strupr` of the buffer). Its Update 0xadeb90 calls the game's text drawing 0xae8310 (cdecl surface,
  &name; 1 KB, an original_call). Its deleting dtor is 0xadd620. All three are in effect through callers.
- **Still the game's:** the base ctor 0xaebd30, the base dtor 0xaebd40 and the plain deleting dtor 0xaebd50. The
  game's derived strategies (terrain paint 0x2948a0, neighbourhood terrain paint 0x29ac70, video 0x404f50, text
  0xaf3560, 0xbaa4e0) still run the base ctor, and that ctor stores the vtable holding 0xaebd50. Ours reproduces all
  three for its own objects.

**Tests:**
- **tests/sg_texture_image.cpp:**
  - SG_TextureImageSlots: 1500 seeds, the whole life of the object on each side.
  - SG_TextureImage_CreateDeviceTexture: 400 seeds.
  - Mutants: 22, 21 killed. The survivor is equivalent: `<` against `<=` inside a min.
- **tests/sg_texture_update_strategy.cpp:**
  - SG_TextureUpdateStrategies: 1500 seeds.
  - SG_TextureUpdateStrategy_FillWhite: 600 seeds.
  - The fake surface's lock sometimes fails, holds the surface or not, and its unlock sometimes lets the surface go
    itself.
  - Mutants: 15, all killed after one strengthening (the AddRef / Release step now takes 1-4 references).

Left for M4: cProceduralTexture (0xab4230, 15 slots) and cRenderTargetTexture (0xab42b0, 20 slots), which share
0xab5f50 with cTextureImage; then M4b.

## cImageData / cLevelInfo / cProceduralTexture / cRenderTargetTexture

- **cLevelInfo** (lane SX, 2026-11-01): whole, in the standalone shape (19 verified, 4 hooked: Create 0xab15f0, both
  constructors, Assign 0xab0120; tests/sg_level_info.cpp). Its vf80 body is 0xab2cf0.
- **cImageData's read helpers** 0xab23d0 (the read body), 0xab2da0 and 0xab0250 were inferred to
  cGZPersistBinaryReader; they are cImageData's vf108 (annotated, lane SS).
- 0xadebb0 (the RecordMap tree) is `std::map<uint32, V>::erase(const key &)` (equal-range walk, then the
  library-matched iterator++ 0x15dc50 and _Extract 0x15feb0): STL, left alone (lane SX).
- **cRenderTargetTexture** (lane TN, 2026-11-02): ours except Restore 0xab5240, slot 12 0xab5b10 and the clear helper
  0xab4b80; cImageData slots 14, 16-21, 38-40 done by the same lane.

### M4b part 2: cImageData whole (lane SZ, 2026-11-01)

- **Shape.** src/sg/cImageData.{h,cpp}: cIImageData +0 (41 slots), cSGResource +4 (13), vbptr +0x20; mFormat +0x30,
  mScale +0x34, `std::vector<ImageEntry>` +0x38 (40 bytes: `std::deque<cLevelInfo *>`, colour[4] +0x14, scale +0x24),
  mCurrent +0x44, the name cRZString +0x48, vtordisp +0x68, the cISGResource virtual base +0x6c; 0x70 bytes. The factory
  0xab1590 returns +0 uncounted; the constructor 0xaafd60 is thiscall(flag) (hooked as SZ_cImageData_Constructor, so
  cTSTextureRecipeImageData's base part is ours too).
- **Verified and hooked (13 new, 6.3 KB):** Create, the constructor, the destructor 0xab0070, QI 0xab2330, Clone 0xab1390,
  Init 0xab1890, BuildLevels 0xab1960, MipChainBytes 0xab0250 (cdecl), GetTypeInfo 0xab3980, Read 0xab23d0 (with the
  imageDataSizeReductionOnLoad / ignoreImageDataSizeReductionOnLoad config reads), ReadLevels 0xab2da0, Write 0xab3b20,
  PixelFormat 0xab2070. The 22 earlier entries (lane hook-shape slots) now hook adapters over the same members.
- **ORIGINAL-BUG kept:** ReadLevels' v<9 block is sized by the *current* entry's pixel bytes (on Read's one-level path with
  levels skipped that is the reduced image, so the copy reads past the block); Write writes the current entry's level
  count for every entry; DropTopLevels (vf15) with n >= count.
- **Not ours yet:** slots 14, 16-21 and 38-40 (vf14 0xab1680, vf16 0xab03c0, vf17 0xab0e50, vf18 0xab08b0, vf19-21
  0xab36d0 / 0xab3390 / 0xab3530, vf38-40 0xab2150 / 0xab1ca0 / 0xab1ec0; vf16-18 are 1.2-1.4 KB each): our vtable calls the original bodies
  (`original_calls` of the constructor's entry). DrawMissingTextureName 0xae8310 (from Read's failure path) is original.
  STL bodies in the range left as library: 0xaaf7c0, 0xaaf800, 0xaaf8a0, 0xaafab0, 0xaafb30, 0xaafbe0, 0xaaf3b0, 0xaaf590,
  0xab3fc0, 0xab4030, 0xab4130, 0xab0100.
- **Tests (tests/sg_image_data_rw.cpp):** Construct (three destroy routes), MipChainBytes (11520 direct cases), Init (120
  seeds, BuildLevels direct), ReadWrite (240 seeds of generated typed item streams through raw-vtable reader and writer
  fakes; the test fails unless every Read path and every fake was reached). Fakes set and restore the cells: string pool,
  framework 0x10c9378, allocator 0x10c95d4 / 0x10c95d8, pixel formats 0x10ca830, image manip 0x10ca990, the keys cell
  0x10f8f2c (nulled), VirtualAlloc / VirtualFree over one arena. Pitfalls: cLevelInfo's pixel blocks under 0x1000 bytes come
  from `new[]` uncleared (fill heap blocks through `difftest::WatchHeap`); compare the name's length, not the std::string's
  heap pointer; out pointers relative to the object; 0xae8310 is a `shape::CodePatch` that catches both sides.
- **Mutants:** 23, all killed (the first run's 2 survivors led to two test additions: a DXT case whose skipped-to level is one
  block at every smaller size, the only sound way into the one-level path's resize with levels skipped for v<9; and
  SetCurrentEntry / DropTopLevels before Write, since Read's Init always leaves entry 0 current with equal level counts).

### M4b part 3: ChooseTextureDescription and cProceduralTexture (lane TH, 2026-11-01)

- **ChooseTextureDescription 0xab5f50** (src/sg/TextureDescription.{h,cpp}, hooked, 972 B): `{width, height, levels,
  format}` for a texture. It takes the format as given (X8R8G8B8 for a normal map) when the device enum (slot 11)
  supports it for the kind and the texture manager's filter flags allow it (DXT needs the DXT flag; more than 16 bits
  needs the true-colour flag). Otherwise: a DXT image's own format when allowed and supported (alpha 1 for DXT1, else
  8; colour 24 or 12), else FindClosestColorFormat over the enum's colour formats (slot 9) with the ARGB bits (clamped
  to 16), then half the bits. Then 0x18c990 fits a 2D size, or 0x18e1d0 fits a cube's mean edge (both still the
  game's, its `original_calls`). The range copy 0x167180 is inlined as a local CopyRange (as in src/gz/cDeviceDX.cpp).
  tests/sg_texture_image.cpp now stands 0xab5f50 in on both sides (`shape::StandIn`).
- **cProceduralTexture** (src/sg/cProceduralTexture.{h,cpp}, 0x28 bytes, vtable 0xe76750, 15 slots): the standalone
  shape. Its layout is +8 strategy (counted), +0xc kind (0xe until set up), +0x10 / +0x14 / +0x18 width, height and
  levels, +0x1c quality (1.0), +0x20 format and +0x24 the device texture. The factory 0xab4d90 is hooked. The ctor,
  dtor, deleting dtor, QI, Restore, ReleaseResource (8 B) and slots 9-12 are in effect through it (callers.py clean;
  trap `Hooks.nGZSceneGraph_cProceduralTexture_New=1`). The folded slots 1, 2 (shared_vf_151920), 5, 6, 7, 8 and 13
  are reproduced and never hooked. Facts:
  - Restore needs the device ready and no device texture created. The strategy's GetDescription writes the members
    (its result is unused). If the chooser succeeds, it calls CreateDeviceTexture and, when that created the texture,
    DrawLevels(first). The strategy's Finish runs always.
  - DrawLevels: kind 4 calls the 2D face's GetTargetLockableSurface(0) into Update(surface, 0, first); kind 6 does
    faces 0-5, level 0. When the device texture has more than one level, the image manipulator's slot 12 generates
    the mipmaps.
  - Create (9) uses class 0xbb892198; CreateCube (11) uses 0xe944343d. CreateClosest (10) and CreateCube choose the
    format with the formats manager's slot 3 (iid 0x9ac38991) into FindClosestColorFormat. The COM is asked for before
    the old device texture is released.
  - ORIGINAL-BUG kept: Restore, ReleaseResource, IsCreated and Redraw use the device texture with no null check.
- **Tests:** SG_ChooseTextureDescription (tests/sg_texture_description.cpp, 3000 seeds) and SG_ProceduralTextureSlots
  (tests/sg_procedural_texture.cpp, 2000 seeds of random slot calls). Mutants: 18, all killed (two chooser
  survivors of the first run led to 12 and 15 bits per pixel in the generated formats, between its thresholds).
- **cRenderTargetTexture (lane TN, 2026-11-02): ours except three bodies** (15 verified, factory 0xab4df0 hooked, the rest in effect through callers; GetKind 0xab4b40 and GetAspect 0xab5a30 are folded or under 5 bytes, reproduced and not claimed; tests/sg_render_target_texture.cpp, 14 mutants killed). Left, still the game's and called from our slots: Restore 0xab5240 (975 B), slot 12 0xab5b10 (517 B), clear helper 0xab4b80 (420 B). Slot 7 is the folded 0x147770, which returns the count (+0x2c), used as a bool. From the decompiles:
  - The ctor zeroes +4..+0x2c, sets +0x30 = 1.0f, copies the 4 words at 0x10f675c to +0x34..+0x40, and holds 4
    RZAutoRefs at +0x44..+0x50. With a device, each gets COM class 0xbb892198 with iid 0x5b891e1e, and +0x54 gets class
    0x7b75b73a with iid 0xfb75b72b. The dtor 0xab4560 releases +0x54, then the 4 refs.
  - Restore 0xab5240 (975 B) uses the device's slot 35 and its enum (slot 16). The colour format comes from enum slot 9
    with three FindClosestColorFormat tries, or, when flag +0x28 & 4, from formats-manager slot 8 and 0x18ad50. The
    depth format comes from enum slot 13 and FindDepthFormat. When +0x28 < 0, the size is GetDeviceTargetSize scaled by
    the floats +0x20 / +0x24. Then AdjustTextureSize(..., 1), an aspect fix for flag 2, CreateRenderTarget (texture slot
    23) for each of the +0x2c targets, and +0x54's slot 25 when flag 1.
  - Several of these helpers are still the game's.
- **Also left:** cImageData slots 14, 16-21, 38-40 (part 2's list).
- **Also left:** nothing from part 2 (cImageData slots done by lane TN).

## cShape

### H1a result: cShape, structure and read path (lane RY, 2026-11-01)

- **Ours:** `nGZSceneGraph::cShape` (src/sg/cShape.h/.cpp), its four vtables built by the compiler: cSGResource face
  +0, node face +0x2c (cIShape, slots 0-60), persist face +0x34, the virtual base. It is `cSGResource` plus
  `cReferentNodeT<cIShape, cShapeBehaviorTraits>`, size 0x9c. Members (struct `cShapeLayout` in Ghidra): parts +0x4c
  (0x20 each), bindings +0x58 (a map from subset to material, group mask and word), the five first-part words +0x60,
  the level deque +0x74, the part names +0x88 ((cell, level) pairs, 8 bytes each). Ported: the factory 0xa5d3b0
  (hooked, 32 bytes), constructor 0xa5bd40, destructor 0xa5c140, QueryInterface 0xa5e4f0, Clone 0xa5d070 and
  CopyContentTo 0xa5d8a0, IsKindOf 0xa5f9f0, the object-name helper 0xa5d470, Read 0xa5e580 (versions 3-8), GetTypeInfo
  0xa5fb00 and Write 0xa5fef0. The bodies the factory hook stops reaching are in effect through callers (trap
  `Hooks.nGZSceneGraph_cShape_Create=1`, from callers.py).
- **Still the game's** (OriginalFunction, listed in `original_calls`): the cIShape slots 34-48, 51-57, 59 and 60, and
  OnEvent 0xa5d710. Read's helpers: 0xae7340 (resource-key lookup), 0xa5c840 (the one-part name step), 0xa5e280 ->
  0xa5de40 -> 0xa5fa10 (choosing the level), 0xa5fbb0 (binding the parts' materials, also used by Copy), and the
  registry 0xa5f010. Slots 49/50 (0xa5d420) and 58 (0xa5e170) go through SGShapeGeometry's hooked bodies.
- **render_object:** `{0xfc6eb1f7, &NotOursYet<0xa5d3b0>}` can become
  `reinterpret_cast<Factory>(&nGZSceneGraph::cShape::Create)` (the cMaterialDefinition row's form). The class uses the
  original's statics (the type-info cells 0x10f6324 and 0x10f6310/14, the event registry 0x10f8fcc, the allocator cache
  0x10c95d4), and the OriginalFunction bodies above still count against the NOIMAGE gate until H1b.
- **Tests** (tests/sg_shape.cpp): `SG_Shape_Factory` (8 seeds: words, counts, QueryInterface and IsKindOf over ten
  iids, type info), `SG_Shape_Parts` (64 seeds: parts through slots 41/42 and the binding slots, Clone in three modes,
  Write) and `SG_Shape_Persist` (128 seeds: Read through either face on streams the test builds, versions 3-8, by
  name or by reference, then Write and Clone). Both sides run the image's slot bodies on our object.
- **Allocator fake:** Read reaches a GZServiceString (0xa5e280's "systemSpecifiedPreferredLOD"), and without a service
  that falls back to the image's allocator path and corrupts the harness heap (0xc0000374). The test plants a fake
  allocator (slots 3 alloc, 4 free) at the cache 0x10c95d4 with the guard 0x10c95d8 set.
- **Original bug, avoided by the tests and not reproduced:** with two or more parts, 0xa5fa10 looks the chosen level
  up through 0xa5db60. That lookup returns end() when no part has the level, and the code then reads end()+4 (fault at
  0xa5fa52). The level comes from 0xa5de40, which picks among the part names' levels. So reading into a shape that
  already has parts, mixing by-name and by-reference parts, or reading two or more unnamed reference parts crashes
  the game's Read. Game files don't do this (one fresh shape per load), and our Read leaves it to the same helpers.
- **Part-name copies:** Read pushes each name as the original does: the opened cell, a (cell, level) pair, push_back's
  copy, then the vector's. That is three handle counts up and three down per name. The string-pool call order is part
  of the comparison, so a copy fewer shows up as a diff.

#### H1b plan: the render-facing slots

Port the remaining slots through the same tests (add cases rather than new worlds). Sizes in bytes:

| Slot | RVA | Size | Role (tentative) |
|------|-----|------|------------------|
| 34 BindMaterial | a5f5b0 | 107 | subset -> material binding (Read's material step) |
| 35 | a5f620 | 227 | binding lookup by subset |
| 36 AddRenderGroup | a5cad0 | 49 | |
| 37 SetRenderGroupMask | a5cb10 | 102 | -1 is all groups |
| 38, 39 | a5f180, a5f1c0 | 49, 104 | |
| 40 SetSubsetWord | a5f7d0 | 100 | binding +0x1c |
| 41 AddPart | a5c520 | 463 | |
| 42 RemovePart | a5f230 | 362 | |
| 43 | a5dd50 | 231 | |
| 44 UpdateLods | a5efe0 | 43 | 0xa5e280 -> 0xa5de40 -> 0xa5fa10 |
| 45 | a5d9c0 | 196 | |
| 46 | a5c6f0 | 326 | six arguments |
| 47, 48 | a5d430 | 50 | one body |
| 51 | a5f540 | 112 | the levels |
| 52 | a5d7f0 | 161 | returns out |
| 53 MaterialNameOf | a5cfd0 | 150 | null without one |
| 54 | a5f840 | 426 | returns out |
| 55, 56, 57 | a5e1d0, a5dca0, a5dc60 | 67, 165, 63 | |
| 59, 60 | a5d590, a5d4f0 | 312, 147 | |
| OnEvent | a5d710 | 209 | registry 0xa5f010 (216) |

Callees: a5fbb0 BindPartMaterials 814, a5c840 630, a5e280 PreferredLod 491, a5dfa0 460, a5af90 349, a5de40
ChooseNamedLod 346, a5db60 FindPartRange 247, a5c350 241, a5fa10 KeepLodPart 206, a5daa0 172, a5f710 162, a5c090
155, a34370 145, a5e470 102, a5c1f0 44, a5bea0 25. That makes about 5.4 KB of slots and 4.3 KB of callees.
Do the binding slots (34-40) together with 0xa5fbb0 first, then parts and levels (41-46, 0xa5c520 / 0xa5f230 /
0xa5e280 / 0xa5de40 / 0xa5fa10). Whether our 0xa5fa10 keeps the end()+4 read needs a decision (`// ORIGINAL-BUG:`
if kept, input never reaches it from game files). Lessons from the fakes: declare every slot an original calls with
its arity (undeclared slots unbalance the stack and crash somewhere unrelated later); QueryInterface for 0xfb859324
answers uncounted; any test that reaches a GZServiceString needs the allocator cache.

### H1b result: cShape, binding slots and small readers (lane SB, 2026-11-01), partial
Ours now (tests/sg_shape.cpp, SG_Shape_Parts / Persist): slots 34 and 36-40 (binding, render-group mask set / remove /
clear, bounds), BindPartMaterials 0xa5fbb0 (whole, with the subset binding 0xa5f710), 47 and 48 (0xa5d430), 52 (0xa5d7f0),
55 (0xa5e1d0), 56 (0xa5dca0, the levels vector) and FindPartRange 0xa5db60. Left on OriginalFunction: 35 (0xa5f620, needs a
subset-name iterator fake), 41 AddPart 0xa5c520, 42 RemovePart 0xa5f230, 43 0xa5dd50, 44 UpdateLods and its chain (0xa5e280,
0xa5de40, 0xa5fa10, 0xa5c840), 45 0xa5d9c0, 46 0xa5c6f0, 51 0xa5f540, 53 0xa5cfd0, 54 0xa5f840, 57 0xa5dc60, 59 0xa5d590,
60 0xa5d4f0 and OnEvent 0xa5d710.
- **Facts:** BindPartMaterials takes the shape at +0 (the face slots take +0x2c). The handle Assign releases the old index
  unconditionally, the destructor only when it is not -1. Material manager slots 19 FindOrLoad, 20 AddRefIndex, 21
  ReleaseIndex, 33 GetInfo. Slot 52's last word is word 3 of the first part's slot-49 record; slot 55 counts the
  none-cell too (0xaeb710 always calls the string pool). Slot 51 resizes mLods to the count and copies the words in.
  OnEvent: type 0x1d05597b fires 0xdd041f30, the material-bindings-updated body (id 0x3d44b140, flag 1), then 0xedf79966;
  type -0x644dbd7e runs BindPartMaterials.
- **Test lessons:** allocation inside the original goes through the game's allocator fake (#762): any container the original
  grows must be reserved up front on the test side (slot 56), and a deque (slot 51) can't be, so slot 51 needs the
  allocator fake routed to the toolchain's before it can be compared.
- **Gaps:** the LOD auto-bind and groups-cleared mutants of BindPartMaterials survive until AddPart is ours (the original
  AddPart runs the original BindPartMaterials); the event argument needs a listener fake. 0xa5fbb0 is status `mapped` until
  0xa5c520, 0xa5f230 and 0xa5d710 are ours (they call it at its RVA).
- **Decision kept for the rest:** reproduce 0xa5fa10's end()+4 read through 0xa5db60 with `// ORIGINAL-BUG:`; tests avoid it.

### H1b part 2 result: AddPart and RemovePart (lane SC, 2026-11-01), partial

- **Ours:** slot 41 AddPart (0xa5c520) and slot 42 RemovePart (0xa5f230), with the part machinery they use: the
  insertion sort of 0xa5b600 (up to 32 parts; more fall back to `std::sort`, whose moves differ but whose result is
  the same since levels are unique), the ShapePart copy, move and destructor (0xa5bbb0, 0xa5c090) and the vector
  growth. Bodies only our vtable reaches need no hook. The call order is the test: the copy constructor AddRefs the
  object before it copies the materials; move assignment AddRefs the new object then Releases the old (only when
  different); the destructor frees groups, then materials, then Releases the object. A plain `std::vector` and
  `std::sort` port logs different AddRef/Release orders and fails SG_Shape_Parts/Persist at once.
- **Passing:** SG_Shape_Factory, SG_Shape_Parts (64 seeds), SG_Shape_Persist (128 seeds), shapes with 0-3 parts.
- **Not done (still the game's bodies, in `original_calls`):** the level chain (UpdateLods slot 44 0xa5efe0, slot 43
  0xa5dd50, and 0xa5c840 / 0xa5e280 / 0xa5de40 / 0xa5fa10 with the end()+4 read to mark `// ORIGINAL-BUG:`), OnEvent
  0xa5d710, slots 35, 45, 46, 51, 53, 54, 57, 59, 60. The level chain was read through (preferred level from a
  preferences-service property cached in statics 0x10f6310/0x10f6314, a named-part pass, a keep-part pass); the
  statics need the thread-safe-init pattern of src/app/AppDebugger.cpp and the tests need fakes for the preferences
  service, the level cell and part-name resolution.
- **BindPartMaterials (0xa5fbb0)** stays `mapped`: OnEvent still calls it at its RVA, so it is not yet in effect
  through callers. Its "no upper LOD" and "groups kept" survivors can be rerun once OnEvent is ours.

### H1b part 3 result: OnEvent, test fixes (lane SD, 2026-11-01), partial

- **Ours:** OnEvent (0xa5d710): the behaviour's, then on the subordinate-list event 0x1d05597b the three listener
  notifications (null event, cMaterialBindingsUpdated with flag 1, null event) and on 0x9bb24282 BindPartMaterials.
  BindPartMaterials (0xa5fbb0) has no image caller left (callers.py), so it, AddPart and RemovePart are `verified` in effect
  through callers.
- **Tests:** SG_Shape_Parts slot 56 (0xa5dca0) uses a vector on the game heap with no reserve under `svcfx::AllocatorScope`;
  registry bits of 16 and up are handed out (a `& 0x0f` mutant dies); listener fakes are connected to shapes and compare the
  bodies of the three notifications (the three padding bytes of the materials-updated body are uninitialised in the
  original, so size and flag are compared). Mutants killed: flag 0, first or last notification dropped, no rebind, no base
  call, adding/removing/added/removed notifications dropped, no upper LOD, groups kept, group mask 0x0f.
- **The FreeInternal crash:** not reproduced. Cause found in our source: slot 56's growth of the caller's vector went to
  RZMemory while the original grows it through the allocator service, and the test's fake service sat on operator
  new/delete, so a block crossed heaps on the free. The slot now allocates and frees through
  `as2::AllocateViaAllocatorService` / `FreeViaAllocatorService`, and the test's fake service allocates from RZMemory like
  the game's.
- **Not done (still the game's bodies, in `original_calls`):** the level chain (0xa5efe0 slot 44, 0xa5dd50 slot 43, 0xa5c840,
  0xa5e280, 0xa5de40, 0xa5fa10, plus 0xa5dfa0 and 0xa5e470 which they call), slots 35, 45, 46, 51, 53, 54, 57, 59, 60.
  Read for the next lane: 0xa5e280 asks the framework for the preferences service, property "systemSpecifiedPreferredLOD",
  with a fallback cached in statics 0x10f6310/0x10f6314 (thread-safe init, AppDebugger.cpp pattern); 0xa5c840 is the
  heaviest (string formatting, level deque iteration at +0x74/+0x80/+0x84, pushes (cell, level) to the part names).
  The preferences fake (PrefsProperty, w.preferredLod), ManagerGet and the allocator fake already exist in the test.

### H1b part 4 result: the level chain, slots 43/44/51/53/57 (lane SF, 2026-11-01), partial

- **Ours (verified, in effect through callers):** UpdateLods slot 44 (0xa5efe0), slot 43 (0xa5dd50), ChooseNamedLod
  (0xa5de40), KeepLodPart (0xa5fa10), LoadNamedPart (0xa5dfa0), the name touch (0xa5e470), the single-part level names
  (0xa5c840), slot 51 (0xa5f540), slot 53 (0xa5cfd0) and slot 57 (0xa5dc60 with 0xa5daa0 inlined). 0xae7340 is ours now
  (`ResolveResourceName`). PreferredLod (0xa5e280) is `reimplemented` only: slot 46 (0xa5c6f0, still original) calls it.
  Its fallback statics 0x10f6310/0x10f6314 are `TS2_OWNED_STATIC`s with the MSVC thread-safe init; the test never takes
  the init path (the guard stays at INT_MIN with the cell pre-seeded).
- **ORIGINAL-BUG:** 0xa5fa10 reads the object of the word after the last part when the level has no part (`end()+4`,
  0xa5fa52). Reproduced and marked in src; the tests only run the chain when every part name's level has a part
  (`SafeToUpdate` in tests/sg_shape.cpp), so the faulting case is not exercised (the status note says so).
- **Tests:** SG_Shape_Parts operate cases 20-24 (slots 43, 44, 51, 53, 57), a fallback-object fake for 0xa5e280's cached
  service, and SG_Shape_Persist `AfterRead` (the same slots on a shape the stream has filled, which is where parts have
  names). Preferred levels of 0..3, 7, 15; stream levels include 30, 31, 40.
- **Mutants (19, all killed):** fallback -1 mapping, search compare, step-back test, drop choice, bounds-copy condition, size
  guard, slot 51 resize, slot 57 step-back, slot 43 and 44 names guards, level bound 0x1f, no second key, no name touch,
  slot 43 no release, no AddPart. Four survived the first test set (slot 57 step-back, the two `> 1` guards, bound 0x1f)
  because the Operate shapes have no part names and the stream levels never held 31; AfterRead and the added levels killed them.
- **Not done (still `OriginalFunction`):** slots 35 (0xa5f620; needs a subset-name iterator fake), 45 (0xa5d9c0, calls
  0xaeaf30), 46 (0xa5c6f0), 54 (0xa5f840), 59 (0xa5d590), 60 (0xa5d4f0), the event registry 0xa5f010, and the name
  formatter 0xaed160 inside 0xa5c840. Slots 46/54/59 were not read.

### H1b part 5 result: slots 46/35/59/60, the name formatter (lane SG, 2026-11-01), partial

- **Ours (verified, in effect through callers):** slot 46 (0xa5c6f0: the shown part's render, materials and groups to a
  sink's slot 28, then the two position words moved on by the first part info), slot 60 (0xa5d4f0) and slot 59
  (0xa5d590) (a box grown by the first part's usable bounds boxes; 59 carries each by its index's rigid transform),
  slot 35 (0xa5f620: BindMaterial per subset name through the part's subset-name iterators), the name formatter
  0xaed160 (`FormatLodName`; only caller 0xa5c840), PartToShow (0xa5daa0) and PreferredLod (0xa5e280, now verified: its
  last caller, slot 46, is ours). 0xa5c840 has no original calls left.
- **Tests:** SG_Shape_Parts cases 25-28 (slots 60, 59, 46, 35) with fakes for the part object's slots 30 and 42, the
  subset-name iterators, the bounds record (slot 49) and the sink; SG_Shape_Persist covers the formatter through
  single-part streams.
- **Mutants:** 15 first set (14 killed; "46 posA uses word 0" survived until the position words were made nonzero more
  often), 3 rerun, all killed.
- **original_calls left in cShape.cpp:** 0xa5d9c0 (slot 45), 0xa5f840 (slot 54) and the event registry 0xa5f010. So cShape.cpp
  still calls originals; nothing here is complete.
- **Whose is 0xa5f010:** the shape behaviour type's (the `cAbstractBehaviorTypeT<cIShape>` registry methods, map 0x10f6318),
  not cShape's; it depends on 0xa59480 and 0xa480c0, which are not ported. Left.
- **Left and why:** slot 45 needs 0xaeaf30 (material manager singleton 0xae8900, an iterator and a map find) plus
  GetCachedQueriedObject fakes. Slot 54 needs a new iterator class family (pool objects 0xa5bea0 / 0xa5c1f0) for the
  binding-range iterators. The dead static 0x10f6308 / guard 0x10f630c that slot 46 writes is skipped (nothing reads it).

### H1b part 6 result: slots 45 and 54, the subset-binding iterator (lane SH, 2026-11-01)

- **Ours (verified):** slot 45 (0xa5d9c0, `PrepareSubsetVariants`: for each part, each material whose subset is bound
  to the asked subset, or every material when the subset is null, asks for its draw variants), its only callee
  0xaeaf30 (ported as the free function `RequestDrawVariants`, not a class: its only caller is slot 45; it walks the
  material's variant iterators from slot 35, builds a key from the render element's format index (`& 0x7f) << 4` and
  flag byte +0x34, checks IsSupported when the record's first word is set, then GetVariant on the material manager
  0xae8900; the std::_Tree find 0x1463b0 is our `std::map::find`), slot 54 (0xa5f840, `GetSubsetBindings`: an
  equal_range over mBindings for a name, or the whole map for a null name, as two pool iterators), and the iterator
  class `cSubsetBindingIterator` (vtable 0xe6f6d8: ctor 0xa5bea0, copy 0xa5be80, dtor 0xa5c1e0, deleting dtor
  0xa5c4e0, pool new/delete 0xa5c1f0 / 0xa5c220 on cRZFastFixedAllocator2<0xc> 0x10f62f0, Clone 0xa5d0f0, Get 0xa5d1e0;
  QI accepts riid 1 only, Next/Prev step the map node). All in effect through callers (vtables 0xe6f54c and 0xe6f6d8
  are referenced only from our cShape). The name-cell helper 0xaed300 (`MakeNameCell`: an empty or null name gives
  0xffffffff, otherwise the cell opens the word) has about 20 outside callers, so it is **hooked**.
- **cShape is complete apart from 0xa5f010** (the behaviour type's event registry, not cShape's; left).
- **Slot 45's result:** the original leaves eax from the last step: the last part's material count when there are
  parts, 0 on the early outs (null subset with no parts, unbound subset), and whatever Close/Reset left otherwise
  (undefined). Ours returns the defined values; the test notes the result only where it is defined.
- **Tests:** SG_Shape_Parts and SG_Shape_Persist cases 30 (slot 45 over random subsets: bound, unbound, empty, null)
  and 31 (slot 54: range, QI riids 1/2/other and null out, AddRef/Release, forward walk with Get, Clone of the end and
  a backward walk, then the pool's live count); new SG_Shape_NameCell (0xaed300 against ours over null, empty, known
  and fresh names). Fakes: render element format and flags, the material's variant iterators and records, and a
  `BindingPool` that points the pool's current page at a test arena.
- **Mutants:** 17 (variant key, IsSupported, cursor, GetVariant; slot 45 filters and result; slot 54 bounds, cursor
  count, order; the empty name cell; iterator Get, Prev, Clone, QI, Release), all killed on the first run.

## Behaviour event registries (shape, object graph, lights, nodes, builders, TSSG)

### H1b part 7 result: the shape behaviour's event registry (lane SK, 2026-11-01)

- **Ours (verified):** 0xa5f010 (`cShapeBehaviorTraits::RegistryFor(id, attribute)`, cdecl, 216 bytes) and its holder
  0xa59480 (folded into it). Hooked: the cAbstractBehaviorTypeT<cIShape> template slots 0xa5cba0, 0xa5ce10 and 0xa5e230
  still call it. The map is a function-local static `std::optional<cEventAttributeMap>` built on first use.
- **What the first-use build is:** the holder runs 0x2570a0's work (the base events into 0x10c94f4, the connection and
  subordinates events into the object-graph registry 0x10cc9f4), gives the parent's shared map (0x3a5d00, map 0x10da02c)
  the record 0xdd041f30 (image data 0x106ba08), then 0xa5f010 copies that shared map (0xa480c0 is `0x3a5d00(0)`) and the
  holder stores four shape events: 0x9bb24282 (record 0x106ba50, last word 4), 0x2a7e3a48 (0x106ba68, last word 0xc),
  0x3d44b140 (0x106ba20) and 0xedf79966 (0x106ba38). An id other than 0 stores the passed record under that id; every
  call returns the same map.
- **0xa480c0 stays the game's** (65 bytes): 0xa49210 (another registry) calls it too. Not folded, not hooked.
- **cShape's remaining original calls** are what the first-use build reaches: 0x3a5d00 (the simple lights' shared map,
  also used by the fog and draw-state lights) and 0x264010 (the object graph's registry). Create's entry lists exactly
  those two.
- **Test:** SG_Shape_Registry, 600 seeds of 1-6 calls; the original side starts from the state the build leaves (the
  harness cannot run the thread-safe static init), ours from nothing. Four registries compared after every call.
  The existing sg_shape tests now stand ours in over 0xa5f010 (`shape::StandIn` with `pdfx::StandRegistry`).
- **Mutants (5, all killed on the first run):** a shape event id changed; the last word 4 changed to 5; the
  id-storing step disabled; the 0x2a7e3a48 record taken from the wrong image record; the 0xdd041f30 parent record skipped.

### H1b part 8 result: the object-graph and shared light event registries (lane SL, 2026-11-01)

- **Ours (verified):** 0x264010 (`cObjectGraphBehaviorTraits::RegistryFor`, src/sg/cObjectGraphNodeImpl.cpp, 216 bytes) with
  its holder 0x255910 folded in and its id-0 form 0x25f030 (also called by the node registry 0x263f30, so it stays original; only compared here); 0x3a5d00
  (`cSimpleLightBehaviorTraits::RegistryFor`, src/sg/cLinearFogLight.cpp, 216 bytes) with its holders 0x3a3e60 and 0x3a4090
  folded in, and its id-0 form 0xa480c0 (65 bytes, hooked: the standard lights' registry 0xa49210 and the holder 0xa457f0 still
  reach it). Hooked: 0x264010, 0x3a5d00, 0xa480c0 (the game's template slots, holders and registries call them).
- **What they are:** both are function-local static maps built on first use. The object-graph map is a copy of the base
  registry (after the base events) plus the connection (0x7b54c99c, record 0xff832c) and subordinates (0x1d05597b, 0xff8344)
  events. The shared map ("simple light" registry: cAbstractBehaviorTypeT<cILight>, the parent of the standard lights' and the
  shape's) is a copy of the object-graph map plus 0xdd041f30 (record 0x100b6e8; the shape's holder and 0xa457f0 store their
  own records under the same id later). A non-zero id stores the passed record. The holders' small objects (0x10cca20,
  0x10da040) are read by nothing.
- **Callers switched to ours:** cShape.cpp, cGeometryNodeClass.cpp and cLinearFogLight.cpp no longer name 0x3a5d00 / 0x264010
  (cShape's, cGeometryNode's and the lights' original_calls for them are gone). 0xa49210 (the standard lights' registry, its
  holder 0xa457f0) stays the game's: next candidate, with cStandardLight's `StandardLightEventRegistry`.
- **Test:** tests/sg_event_registries.cpp, SG_ObjectGraph_Registry and SG_SimpleLight_Registry, 600 seeds of 1-6 calls each; the
  original side starts from the state its first-use build leaves (the thread-safe static cannot run in the harness), made with
  the real 0x1464f0 / 0x2570a0 / 0x3a4090; 0x25f030 and 0xa480c0 called on the original side must return the same map.
  The older registry tests (sg_shape_registry, sg_geometry_node_registry, sg_lights_fog_drawstate) now stand ours in over
  0x264010 / 0x3a5d00 on both sides over the planted maps.

### H1b part 9 result: the node, light, builder and TSSG event registries (lane SM, 2026-11-01)

- **Ported (each beside its class, hooked, its holder folded in as first-use code):** 0x263f30 (composition tree node, holder
  0x255860), 0x2640f0 (transform node, holder 0x2559c0), 0xa49210 (standard light, holder 0xa457f0), 0xa453a0 (geometry
  builder, holder 0xa44a20), 0x253de0 (nTSSG::cTSSGSystem, holder 0x2455e0). All five are function-local static maps built on
  first use as a copy of the parent's map plus their own records (0xff835c/0xff8374, 0xff838c/0xff83d4/0xff83a4/0xff83bc,
  0x106acf8 on top of the simple-light map, 0x106ab4c, 0xff7890/0xff78a8 with the last word 4).
- **Left original on purpose:** 0x2570a0, 0x257030, 0x257110 (base+object-graph, +node, +transform event sets): other game
  holders still call them, so ours repeats their work in `RegisterEvents()` helpers rather than hooking them. 0x25f030 stays
  original too (other callers).
- **Callers switched:** `StandardLightEventRegistry`, the builder's `Registry()` and the TSSG system's readers call ours; the
  five RVAs are gone from every `original_calls`.
- **Test:** tests/sg_event_registries.cpp, SG_CompositionTree_Registry, SG_TransformNode_Registry, SG_StandardLight_Registry,
  SG_GeometryBuilder_Registry, SG_TSSGSystem_Registry (600 seeds, the maps compared after every call; mutants: record swapped,
  id store skipped, parent copy skipped: all killed). tests/sgfx.h `RegistryScope` now also stands ours in over 0x263f30,
  0xa49210 and 0x2640f0 on both sides.
- **Not ported:** the other holders' registries (see the report); 0xaeb710 looked like a string-pool cell constructor, not a
  registry.
- **render_object gate (before -> after):** 149 -> 159 image references. Original functions 48 -> 46 (11617 -> 11185 bytes);
  address data 79 -> 91 (the registries' records at 0xff7890.. / 0x106ab4c.. are read from the image until they get owned
  statics); vtables 21 unchanged. Moving those record tables into owned statics removes the 12.

## cShapeRefNode / cShapeRefNodeT / cScreenShapeRef

### H2: cShapeRefNode (lane SN, 2026-11-01)

The node a CRES's shape reference makes (director factory 0xa55270): 0xd0 bytes, cShapeRefNodeT over
cRenderableNodeT / cBoundedNodeT / cTransformNodeT / cCompositionTreeNodeT / cObjectGraphNodeT, Impl
cShapeRefNodeImpl (0xc4, from +0xc, derived from cRenderableNodeImpl). Vtables 0xe6ef70 (101 slots) and the +8
persist face 0xe6f108 (7). Ours: src/sg/cShapeRefNode.h/.cpp, src/sg/SGRenderableTemplates.h (the renderable
level, reusable for the viewer and light references and cTSParticlesRenderable).

#### The slot map

- **Template bodies we already own (reused through SGNodeTemplates.h / SGRenderableTemplates.h; all shared_vf, so
  ours only in our vtable, never hooked):** 1, 11, 16-24, 26-29, 31-49, 52-59, 61-69, 76-81, 84-90, 98 and face 1 / 6.
  The renderable level (0 via 0xa58020, 77-81, 84-86) is new in this lane; 87-90 are one-line shared constants
  (GetRefKind 2 and three zero/false returns).
- **shared_vf bodies never to hook:** every one in the list above (0xa58000, slot 98, is also cTSCatalog's
  GetTerrainBrushCount). The status file names none of them.
- **cShapeRefNode's own bodies:** 0, 2-10, 12-15, 25, 30/50, 51, 60, 70-75, 82, 83, 91-97, 99, 100, face 0 and 2-5,
  plus their Impl bodies (sizes): OnEvent 0xa560f0 (175), OnQuery 0xa561a0 (71), SetAttribute 0xa575a0 (1492),
  GetAttribute 0xa56b00 (396), slot 51's 0xa55830 (1690), slot 60's 0xa572f0 (677), the bounds and hit tests
  0xa569e0 / 0xa56930 / 0xa56230 / 0xa564b0 / 0xa56720 (275 / 172 / 637 / 618 / 519), Draw 0xa555d0 (370), slot 83's
  0xa55430 (403), AddShape 0xa55750 (223), RemoveShape 0xa570f0 (293), 0xa561f0 (61), Read 0xa56c90 (1115),
  GetTypeInfo 0xa57240 (172), Write 0xa57b80 (361), CloneInto 0xa57d00 (220) with 0xa53aa0 (584) and 0xa580f0 (1198),
  the material pass 0xa589a0 (2417) and 0xa59340 (89), the behaviour's 0xa54990 (617), 0xa55f00 (491), 0xa55030 (118),
  0xa552f0, 0xa55230, 0xa58850, and the registry 0xa585a0 (216) with its holder 0xa53800 (671). About 16.5 KB.

#### The finding that shapes the plan: cScreenShapeRef shares the vtable

cScreenShapeRef (vtables 0xe91708 / 0xe918a0, built by 0xc09820: operator new 0xd0, the game's constructor 0xa541e0,
then its own vtables) has every cShapeRefNode slot but 70 (0x144910 for 0xa57cf0), the deleting destructor 0xa54880
included. The game's Clone 0xa54ec0 (in both vtables) builds game cShapeRefNodes. So hooking the factory makes only
the factory's nodes ours; every body above stays live through cScreenShapeRef and the game's clones, and none can be
"in effect through callers". Part 1 therefore records one verified entry (Create, hooked) and the rest `mapped`
with their tests. To make the class count, the next lane hooks, as in-place Construct / Destruct (the pattern of
src/hook/tables/cBoundedNodeImpl.cpp): the constructor 0xa541e0 (cScreenShapeRef then lays its vtables over our
layout, which matches word for word), the destructors 0xa543d0 / 0xa544f0, and the shape-ref-only bodies the two
vtables share (each referenced only by the shape-ref family: check with `tools/callers.py`). Clone 0xa54ec0 (240
bytes, both vtables only) is hookable once the constructor is.

#### What 0xa589a0 needs

OnEvent's events 0x1d05597b (iid 0x3c6eb1f0, sent by AddShape / RemoveShape) and 0x3d44b140 run 0xa589a0: the
material manager by GetCachedQueriedObject 0xae8900 (cache 0x10f8fcc), each shape's slot 52 (+0xd0), the records at
+0x7c, 0xa580f0, and thread-safe statics at 0x10f62a0..0x10f62bc. A test that adds shapes needs that manager as a
stand-in; part 1's tests plant the shape list instead. Note the ORIGINAL quirk: RemoveShape of a shape not in the
list reads past the records.

#### The remaining work, in lanes (each 10-15 bodies)

| Lane | Bodies | About |
|---|---|---|
| H2b | the constructor / destructor hooks (in-place), Clone 0xa54ec0, AddShape 0xa55750 + 94, RemoveShape 0xa570f0 + 95, OnEvent 0xa560f0 + 3, OnQuery 0xa561a0 + 4, 0xa589a0, 0xa59340, 0xa561f0 + 97 | 4.4 KB; the material-manager stand-in; makes the part 1 bodies count |
| H2c | persist: Read 0xa56c90 + face 3, GetTypeInfo 0xa57240 + face 4, Write 0xa57b80 + face 5, CloneInto 0xa57d00, 0xa53aa0, 0xa580f0 | 3.7 KB; stream-only state checked after a Read |
| H2d | bounds and hits: 0xa569e0, 0xa56930, 0xa56230, 0xa564b0, 0xa56720 and adapters 71-75, 0xa572f0 + 60 | 2.9 KB; the bounded node world (lane PX's fixtures) |
| H2e | Draw 0xa555d0 + 82, 0xa55430 + 83, SetAttribute 0xa575a0 + 7, GetAttribute 0xa56b00 + 10, 0xa55830 + 51, 0xa57220 + 25 | 4.4 KB; the sgfx device world |
| H2f | the behaviour: 5, 6, 8, 9, 12, 13 (0xa54990), 14 (0xa54ea0 / 0xa55f00), 0xa55030, 0xa552f0, the registry 0xa585a0 with 0xa53800 | 2.3 KB; as H1b part 7 (RegistryScope over 0xa585a0) |

#### H2 part 1 result (lane SN, 2026-11-01)

- **Ported:** the layout (node 0xd0, Impl 0xc4, static_asserts), the constructors of every level (0xa541e0,
  0xa54070, 0xa54010, Impl 0xa54200), the destructors (0xa543d0, Impl 0xa544f0, deleting 0xa54880), the factory
  (0xa55270, hooked), QueryInterface (0xa58060, renderable 0xa58020), IsKindOf, Clone 0xa54ec0, Accept 0xa58950,
  slot 70, the shape-list readers 92/93/96/98/99, the cIShapeRefNode accessors and the renderable level. Our own
  vtables; every unported slot forwards to its game body through OriginalFunction.
- **Tests:** tests/sg_shape_ref_node.cpp, SG_ShapeRefNode_Factory (8), _Slots (64 seeds x 24 operations over planted
  shape lists), _Clone (64: modes, contexts, iids). tests/sgfx.h's traversal fake now settles slot 8 (VisitShapeRef).
- **Status:** Create verified (hooked); 14 others mapped with their tests (not in effect, cScreenShapeRef above).
- **Left:** the persist slots (H2c); the destructor with a non-empty list and Add/RemoveShape (H2b, need 0xa589a0's
  world).
- **Mutants:** 26 + 3: 25 killed (the destructor's block free after a strengthening: a block planted at +0xbc). Survivors:
  the Impl destructor's shape releases (equivalent: ~cShapeRefNode has emptied the list) and ~cShapeRefNode's
  front-first order (untested until H2b can destroy a node with shapes).
- **render_object gate (before -> after):** 158 -> 158 distinct references, nothing removed or exposed (original
  functions 45, 11993 bytes; address data 91; vtables 21). render_object does not link the shape-ref node yet: it moves
  when the CRES clone path (N3 part 3) reaches the factory.

#### H2b result (lane SP, 2026-11-01)

- **Hooked:** the constructor 0xa541e0 (in-place `Construct`), the destructors 0xa543d0 / Impl 0xa544f0 (`Destruct`, the
  qualified destructor) and the deleting destructor 0xa54880, Clone 0xa54ec0, and part 1's slot bodies (QueryInterface
  0xa58060, Accept 0xa58950, 92 0xa588e0, 99 0xa55400). Each slot hook runs a one-statement qualified adapter
  (`Hook*`): cScreenShapeRef's vtable 0xe91708 holds these slot RVAs, so in game they run ours on its objects.
- **Ported and hooked:** AddShape 0xa55750 (+ 94 0xa548f0), RemoveShape 0xa570f0 (+ 95 0xa58710), OnEvent 0xa560f0 (+ 3
  0xa55370), OnQuery 0xa561a0 (+ 4 0xa553b0), RebuildMaterialBlock 0xa589a0, RefreshDetailKeys 0xa59340,
  AnyShapeSlot52 0xa561f0 (+ 97 0xa553f0). 0xa589a0's three function-local vectors (0x10f62a0 / b0 / c0) are statics of
  ours. 0xa580f0 (H2c), 0xae7aa0 / 0xae7910 (the root's limits walk) and the detail object 0xaead80 stay the game's
  (OriginalFunction).
- **Tests:** SG_ShapeRefNode_Shapes (64 seeds x 32 operations): the fake material manager in cell 0x10f8fcc, materials,
  binding cursors and the shapes' slot 52 / 54. Every block is noted whole, with pointers as offsets. Nodes are released
  still holding shapes. **The game-shaped route:** in half the seeds of Slots / Clone / Shapes, our nodes are built by
  Construct in fresh memory and get a vtable copy holding the hook replacements, so the adapters and destructor hooks
  run as they do in game. In half of Factory's seeds, 0xa541e0 is run against Construct, in place.
- **Status:** 26 newly verified (the group's 27 all verified). 0xa54070 / 0xa54010 / 0xa54200 are in effect through callers (trap: the Construct hook).
  0xa57cf0 likewise (trap: Construct, Destruct and the slot-100 hook). The folded 0xa58020 counts through our vtables.
  The rest are hooked.
- **Mutants:** 48, all killed after three strengthenings:
  - the stale flags are cleared before half the events (the transform event);
  - the fake material leaves some vector parameters unset (the vector events start zeroed);
  - half the nodes' listener answers query 0x1234 (the slot-4 adapter's behaviour half).

  A fourth strengthening covers in-place construction: 0xa541e0 runs on the original side of Factory. SN's surviving
  front-first removal-order mutant is killed.
- **Not exercised:** a material named by an index list but by no binding (the original reads past its offsets vector; the
  generator keeps bindings a superset).
- **render_object gate (before -> after):** 158 -> 158 distinct references, nothing removed or exposed (original
  functions 45, 11993 bytes). The shape-ref node is still not linked by render_object.

#### H2c / H2d result (lane TB, 2026-11-01)

- **H2c, hooked:** Read 0xa56c90 (+ face 3), GetTypeInfo 0xa57240 (+ face 4), Write 0xa57b80 (+ face 5), CloneInto
  0xa57d00, RebuildMorphs 0xa580f0, MakeMorphName 0xa53aa0 and AllNearZero 0xa54930. SG_ShapeRefNode_Persist round-trips
  streams by version (0x13 colour, 0x15 names, the weights count checked) and compares the stream-only state after a
  Read. 17 of 17 mutants killed.
- **H2d, hooked:** OnContentChanged 0xa572f0 (+ 60 0xa59330), GetBoundBoxIn 0xa569e0 (+ 71 0xa57f60), GetBoundBox
  0xa56930 (+ 72 0xa57f80), IntersectsBound 0xa56230 (+ 73 0xa57de0), IntersectsLinear 0xa564b0 (+ 74 0xa57e00) and
  FirstHitLinear 0xa56720 (+ 75 0xa57f30). Impl +0x88 / +0x94 are the half box and its centre (mHalf / mCentre).
  OnContentChanged fills every block transform with record 0 and then each in-range record. The box grows from
  FLT_MAX through shape slot 60, or through slot 59 (the inverted world and the block's transforms) when the block has
  transforms; an empty box gives half 0 and the world translation as its centre. FirstHit does no box test and asks
  every part. The parts are the level info's word +0x1c. With block transforms each part sits at its own transform
  (min of the info's and the block's counts); otherwise part 0 sits at the cached frame. RigidInvert 0x19cd80 stays
  the game's.
- **H2d test:** SG_ShapeRefNode_Bounds (64 seeds x 40 operations). Fake shapes grow the box (slots 59 / 60, some inside
  out), and fake bounds / linears answer from bit patterns. 11 of 11 mutants killed.
- **Left:** H2e (Draw / attributes), H2f (the behaviour), cShapeRefAttributeNameIterator, the M1a iterators
  0xac95e0-0xacc480 and GetDetailObject 0xaead80. cScreenShapeRef needs no vtable of its own: its vtable 0xe91708 holds
  the template's slot RVAs, so every slot hooked at its RVA runs ours on its objects; only slot 70 differs (0xe918a0).

#### H2e result (lane TD, 2026-11-01)

- **Hooked:** Draw 0xa555d0 (+ 82 0xa548d0), Slot83 0xa55430 (+ 83 0xa548b0), SetAttribute 0xa575a0 (+ 7 0xa593a0),
  GetAttribute 0xa56b00 (+ 10 0xa580a0), MakeIterator51 0xa55830 (+ 51 0xa54970) and IsKindOf 0xa57220 (+ 25 0xa58930).
  cShapeRefAttributeNameIterator (src/sg/cShapeRefAttributeNameIterator.*: 0xa541a0, 0xa54160, 0xa54480, 0xa54840,
  0xa54fb0, 0xa550b0, 0xa57f50, 0xa58010, 0xa54920, 0xa550f0), its pool allocate 0xa545e0 and the counted name list
  0xa54740 are ours, in effect through callers (only 0xa55830 and the iterator's Clone build them). The pool free
  0xa54610 stays reachable from unwind code (0xd727xx), so it is not claimed.
- **Facts:** the name pool's object is 0x10f625c, its current-page word at +4. Cursor slot 5 writes a 0x10-byte
  cSubsetBinding. Draw skips the frustum test when the shape flag 0x100 (slot 70's bit) is set or the mask has all
  planes; it updates only the mask's low byte. Slot83 counts mByteA2 down from 10 after a content change; at 0 it
  checks each shape's rest transforms (composed with the block's first) against the block's and sets the stale flag
  0x10, then parks the counter at 11. The display list's slot 25 0xa9f4c0 stays the game's (the cut).
- **Tests:** SG_ShapeRefNode_Attributes and SG_ShapeRefNode_Draw (64 seeds each). 28 of 28 mutants killed (two after
  strengthening Draw: the 0x100 flag flipped before draws, and a content change followed by eleven 83 calls).
- **For H2f, the registry 0xa585a0:** a function-local static map at 0x10f62d0 (guard 0x10f62d8), copy-constructed on
  first use from the transform registry (0xa57e10 calls 0x2640f0(0, defaults); ours is
  cTransformNodeBehaviorTraits::RegistryFor). It runs the holder 0xa53800 first: a once byte at 0x10f62e0 (guard
  0x10f62e8) and a zeroed 0x14-byte object at 0x10f62e4 that nothing reads. The holder registers the transform events
  (0x257110), then these ten records (id, name, words; the last word for the first four is 8 in the image and the
  holder sets it to 4; the others keep 0xc):
  0x106b76c 0xdd041f30 BoundsUpdated (1, 0x7b256782, 2); 0x106b79c 0x2a7e3a48 HandleGeometryNodeUpdated
  (1, 0x7b256782, 1); 0x106b7cc 0x3d44b140 MaterialBindingsUpdated (1, 0x2c27f3de, 2); 0x106b7e4 0xedf79966
  PartCountUpdated (1, 0x7b256782, 2); 0x106b7fc 0x69c7eb40 MorphWeight (0, 0xbb22dd17, 3); 0x106b814 0x09f5fdb0
  MaterialFloat1 (0, 0xbb22dd17, 3); 0x106b82c 0x09f5fdb6 MaterialFloat2 (0, 0x9b5b3300, 3); 0x106b844 0x09f5fdbc
  MaterialFloat3 (0, 0x7b5b330c, 3); 0x106b85c 0x09f5fdc0 MaterialFloat4 (0, 0x5b5b331a, 3); 0x106b874 0x8c798726
  ShapeColor (1, 0x5b5b331a, 3). The pattern is cShapeBehaviorTraits::RegistryFor (src/sg/cShape.cpp); the test
  pattern is tests/sg_event_registries.cpp, and tests/sg_behavior_copies.cpp already names 0xa585a0 / 0x10f62dc.
- **Left:** H2f whole (behaviour slots 5 0xa58850, 6 0xa55230, 8, 9, 12 0xa57fa0, 13 0xa54990, 14 0xa54ea0 /
  0xa55f00; Release 0xa586e0 / 0xa586e5; face0 0xa58092; the registry and holder above, then the OriginalFunction in
  src/sg/SGBehaviorCopies.cpp and cShapeRefNodeBehaviorTraits::Registry() switched to ours), the M1a iterators
  0xac95e0-0xacc480 and GetDetailObject 0xaead80.

#### H2f result (lane TF, 2026-11-01)

- **Hooked:** NotifyListeners 5 0xa58850, AskListeners 6 0xa55230, GetEventCount 12 0xa57fa0, GetEventIds 13 0xa54990,
  GetEventAttribute 14 0xa54ea0 and the Impl's 0xa55f00, the +8 face's QueryInterface 0xa58092 and Release 0xa586e5
  (`sub ecx,8` thunks: __fastcall static adapters), and the registry 0xa585a0 (cShapeRefNodeBehaviorTraits::RegistryFor,
  the RegistryFor pattern of lanes SE-SR: owned record statics, gMissingRecord for the missing record). The holder
  0xa53800 is folded into RegistryFor, in effect through callers (its only caller is 0xa585a0). 0xa586e0 is a 5-byte
  jump to SV's 0xa58680 (no entry). Slots 8 / 9 (0xa55010 / 0xa550d0) were already verified. SGBehaviorCopies.cpp's
  ShapeRefRegistryFor now calls ours; sgfx::RegistryScope stands 0xa585a0 in on both sides (map 0x10f62d0).
- **Facts:** 0xa55f00 returns the image's missing record 0x106b620 for an unknown event; the morph weight 0x69c7eb40
  gets word 2 = the node's morph count (byte) with or without a block; with a block, MaterialFloat1-4 get word 2 = the
  block's parameter list k's count (0 when the list is null); everything else is the record as found. The registry's
  first use runs 0x257110's work (the node events, then BoneID / BoneTransform / BoneTranslation / BoneOrientation
  through the transform registry), copies the transform map, then stores the ten records listed under H2e.
- **Tests:** SG_ShapeRefNode_Registry (tests/sg_shape_ref_registry.cpp, 600 seeds: the original side planted from the
  state its first-use build leaves, ours built from nothing; the five registries compared after every call) and
  SG_ShapeRefNode_Behavior (64 seeds, through our vtable and the game-shaped route, the face hooks laid in a copy of
  the +8 vtable). Mutants: 22, 20 killed, 1 equivalent (gMissingRecord for DefaultAttribute(): same content), 1 did
  not build (replaced by the slot 5 argument mutant, killed). Killed: registry record words, name, last word, a bone
  event, the node events, id 0 stored, emplace for assign; the Impl's morph count, morph needing a block, list index,
  missing list, missing record's id; slots 5, 6, 12, 13 and 14; the face Release and QueryInterface.
- **Left:** the M1a iterators 0xac95e0-0xacc480 and GetDetailObject 0xaead80 (114 B: when the cell 0x10f9028 is null,
  asks the service object's slot 5 for (0xe5074b6b, 0x25074b4d), keeps slot 9 of the result in the cell, releases the
  reference through 0x1cbd79). Its callers are other lanes' files (cTSSGSystemFrame.cpp, cTSSGSystemMessages.cpp,
  cTSSGSystemSlots.cpp, nTSSGNHoodOccupantManager.cpp, cRenderableNodeImpl.cpp) and tests/sg_renderable_fx.h,
  tests/ts_nhood_sg_manager.cpp and tests/sg_tssg_system_messages.cpp patch the cell or the function on the game's
  side only: switch them to shape::StandIn on both sides with the hook.

## cDisplayList (with cRenderGroupManager and cFrameCache)

### DLa result: cDisplayList's structure and its sort block (lane TQ, 2026-11-02)

cDisplayList is ours: a real class with the game's 0x160-byte layout and its own two vtables
(src/sg/cDisplayList.{h,cpp}, the interface in src/sg/cIDisplayList.h; cSceneGraphSystem.h now includes it instead of
its empty stand-in). The factory 0xaa0e00 is hooked (the COM director's creator for class 0x49d02f15 is its only
reference), so the constructor, destructor and every slot are in effect through it. 50 status entries: 43 verified
(14.5 KB), 7 reimplemented (1.4 KB). The layout is in the header comment.
- **The sort block 0xa9a120-0xa9d420 belongs to the class.** Every reference comes from cDisplayList code or from
  inside the block. It is three MSVC STL sort instantiations: DrawItem with DrawItemOrder, DrawItem with
  DrawItemLayerOrder, and float with less<>.
  - The 15 functions (introsort, insertion, partition, med3, make_heap, pop_heap_hole, and the float guess-median) are
    hooked forwarders to our STL's templates with the class's comparators (src/sg/DisplayListSort.{h,cpp}).
  - Our MSVC 14.40 templates give the same element order as the originals for every input, ties included.
    tests/sg_display_list_sort.cpp (15 tests) compares the sorted bytes and the results on adversarial arrays: ties,
    equal keys, high-bit keys and pointers, +-0 and NaN depths, counts 0-47 and up to 1500, and a small `ideal` to
    force the heap path.
  - Not recorded: 0xa9a120, the Pass vector's destroy range. Its one reference is 0xa9df20, referenced from 0xd75185
    (an EH funclet, original), so it stays open.
- **The comparators** are in cDisplayList.h.
  - DrawItemOrder orders by sort key. Even keys then go by material, geometry and the sub key's low 16 bits; odd keys
    go back to front (a larger depth first).
  - DrawItemLayerOrder: by sort key, then material and sub key; odd keys by layer (descending), then depth
    (ascending).
  - InsertDrawItem 0xaa1290 is a lower-bound insert into the first mSortedCount items. It is hooked, because DLb's
    vf24 still calls it.
- **Constructor 0xa9d460 leaves bytes uninitialised:** pad byte +0x33, +0xe4 and the vectors at +0x140..+0x157. Ours
  zeroes them. The lifecycle test checks that the original leaves them untouched (0xcd) and masks them.
- **Slot return types:**
  - IsDrawing (shared_vf_6175c0, `mov al`) and HasPass (`setne al`) set only al, so they are bool.
  - TakeStats 0xaa2d90 leaves `out` in eax, and GetBufferSizes 0xaa0890 leaves the index pointer there. These are
    declared to return that pointer.
- **Folded getters, no entries here:** IsDrawing shared_vf_6175c0, GetRenderStrategy 0x142870, GetFrame 0x147800
  (their defining classes' groups). The +8 QueryInterface thunk 0xaa2192 has no entry either.
- **Tests:**
  - tests/sg_display_list.cpp: slots, passes, insert, query, config, slot order, and lifecycle. Both sides go through
    their own vtables; ours is read from an object that our constructor built.
  - The world is svcfx fakes: the framework, the graphic-system cache (device or none, readiness drawn), the COM
    director handing out four fake dynamic arrays, and the graphics behaviour's Connect.
  - The lifecycle test runs 400 seeds. Every fourth seed builds through the factories (0xaa0e00 against ours). The
    other seeds build with the constructor on a 0xcd block against placement new. Then up to 11 vtable steps, and
    the deleting destructor 0xa9e580 (slot 48) with flags 1. Compared: the object after every step and every fake
    call.
- **Reimplemented, not difftested yet:**
  - Init 0xaa0fd0 and Shutdown 0xaa2d10: the settings service and the material manager.
  - OpenLog 0xaa07f0: file I/O.
  - AddGeometry 0xa9f460: calls the original item builder 0xaeae00.
  - DoMessage 0xaa06c0 (the +8 face), OnEvent 0xaa0f80 and DeviceChanged 0xaa0eb0: a cDynamicGeometryRect per new
    device.
- **DLb, still the image's:**
  - Our vtable forwards slots 21 0xa9fae0, 22 0xaa08b0, 23 0xa9fb40, 24 0xaa0920, 25 0xa9f4c0, 26 0xa9f3c0, 28
    0xa9e700 and 30 0xa9f140 to the originals. They are the constructor entry's original_calls.
  - vf23 DrawViewer returns `uint32_t`: the whole of eax, from the viewer's texture count loop.
  - vf24 and vf28 call the hooked sorts (0xa9cf70, 0xa9d100, 0xa9cc30) and InsertDrawItem. Those are the sorts'
    remaining open references, which is why the sorts stay hooked.
  - The default strategy's constructor 0xa9d450 is called as an original (cDefaultDisplayListRenderStrategy, 8 bytes).
  - Also left for DLb: cRenderGroupManager, the frame cache's 0xa9e3f0, GetRenderGroupRegistry 0xaeb370, and the G0b
    rest.
- Mutants: 20, all killed. They cover the comparators (ties, layer, geometry, depth), the Med3 / ideal / float forwarders, BeginFrame, AllocSlots, FreeSlots, SetPassCount links, TakeStats, RecreateBuffers, the constructor's array order, QI, the insert bound, and the two pointer returns. The Med3 mutant first survived: the test only used spans of at most 6, and it now has spans over 40.

### DLb result, part 1: cRenderGroupManager and the scene slots 21, 22, 25, 26 (lane TT, 2026-11-02)

12 verified, 0 reimplemented-only. The rest of DLb (slots 23, 24, 28, 30 and what they own) is still the image's; see
"DLb, left" below.
- **cRenderGroupManager is ours** (src/sg/cRenderGroupManager.{h,cpp}, 8 entries in db/status/cRenderGroupManager.jsonl).
  - It is the render-group registry: render-group names mapped to layer-mask bits. The first time a name is asked for,
    it gets the next free bit. There are at most 32 names. Names compare without case (_stricmp order in a
    std::map). The constructor registers "Practical" as bit 0.
  - Layout: 0x1c bytes, vtable 0xe7585c, the names by bit in a vector at +8, and the map at +0x14.
  - The factory 0xaa37f0 is hooked. It is the COM director's creator and its only reference, so the class is in effect
    through it.
  - RenderGroupRegistry 0xaeb370 is hooked. It is the scene graph service's slot 7, cached in 0x10f905c.
  - Callers switched to ours: cRenderGroupNameIterator, cRenderableNodeImpl and nTSSGNHoodOccupantManagerMessages.
    Their 0xaeb370 original_calls are gone. tests/sg_renderable_fx.h and tests/ts_nhood_sg_manager.cpp call the
    image's getter on the original side and ours on ours.
  - Test: tests/sg_render_group_manager.cpp. Mutants: 10/10 killed. One of them ("QI no addref") is caught as a hang.
- **Slots 21, 22, 25 and 26 are ours** (src/sg/cDisplayList.cpp). The names are now in cIDisplayList.h.
  - BeginScene 0xa9fae0 (slot 21) calls the device's BeginScene (slot 29), sets drawing, and clears the pass stack.
    With a log open it writes "-----Scene-----\n", "Frame number: ", the frame number (log slot 9) and "\n"
    (g_DescribeNewline 0xd9e644).
  - EndScene 0xaa08b0 (slot 22) clears the stack and the drawing flag. It then resets the device state with flags 8
    (device slot 20, state slot 9) before calling the device's EndScene (slot 30). The log is released when its frame
    count reaches 0, and only if the count was non-zero.
  - DrawNode 0xa9f4c0 (slot 25) appends a node record to the top pass. The vector grows by one when count == size.
    - The record comes from the node's slots: 86 gives the two layer-state words, 88 the frame the node was last
      drawn, and 90 is stamped with this frame. The current flag (+0xc) is set when the node's last frame equals ours.
    - The record also holds the plane mask byte (+0xd), the placement (+0x10) and the bounds (+0x44).
  - AddLightNode 0xa9f3c0 (slot 26) only adds when switch 0 is on. The vector grows when size <= count. It appends
    {node, plane mask byte}.
- **Pass layout names (cDisplayList.h):** these were DLa's "state" fields.
  - NodeRecord is 0x5c bytes.
  - A pass's +0x288 is the current record pointer. It points at the viewer record (+0x2f4).
  - +0x2e4 / +0x2e8 are the node count and std::vector<NodeRecord>.
- **Record growth leaves garbage in two bytes.** 0xa9d420, the original's default construction for vector<NodeRecord>,
  sets only the placement's first word (0x1f) and leaves everything else. The pad bytes at +0xe and +0xf therefore
  keep heap garbage on the original's side. The scene test masks those two bytes. Every other byte is compared.
- **Test sg_display_list_scene** (tests/sg_display_list.cpp, 1500 seeds).
  - The operations are random sequences of BeginScene, EndScene, pushed passes (with preset counts), DrawNode,
    AddLightNode, reconfiguration and frame changes. Both sides go through their own vtables.
  - The world is the svcfx device, the device state and a log fake with text logging. The nodes are three fakes on a
    hand-made vtable, whose slots 86, 88 and 90 log their calls.
  - Compared after every step: the object, each pass's records, and the node states. At the end: every fake call in
    order, and the log's refs.
  - Mutants: see the status notes.
- **The 15 sort hooks stay.** vf24 0xaa0920 and vf28 0xa9e700 are still the image's, and they call 0xa9cf70, 0xa9d100,
  0xa9cc30 and InsertDrawItem 0xaa1290. Once both slots are ours and call our sorts, the 15 sort instantiations
  (0xa9a120-0xa9d420) and InsertDrawItem have no reference left from original code. They can then become in effect
  through callers: drop the hooks and trap them.
- **DLb, left: nothing.** Since part 5 (lane TY) no cDisplayList entry has original_calls: vf28 (part 4), vf30 and the
  default strategy (part 3), the sort hooks (now traps, part 4), the light selection 0xaa0130 and the item log 0xaa1640
  (part 5) are ours. What the display list still reaches in the image is other classes' (the geometry, material,
  shader, light and node interfaces, through their vtables). cDisplayListDynamicRenderStrategy (0x36f220) is a
  different class, not part of this one.

### DLb result, part 2: the view slots 23 and 24 and the frame cache (lane TV, 2026-11-02)

9 verified (7 new, AddGeometry 0xa9f460 moved up from reimplemented, the frame cache's deleting destructor 0xa9e5b0
re-verified by test), in src/sg/cDisplayListDraw.cpp and src/sg/cFrameCache.cpp (cFrameCache moved out of
src/misc/ClassesRound3.* with its two IC entries):
- 0xaa24c0 RenderWithStrategy (strategy BeginPass and Render over the top pass inside `__try`/`__except(1)`),
  0xaa2540 SetupTopPass (the pass reset), 0xaeae00 BuildDrawItem, vf23 DrawViewer 0xa9fb40, vf24 DrawPass 0xaa0920,
  the frame cache's Flip 0xaa2e10 and destructor body 0xa9e3f0. All in effect through callers (our vtable slots; the
  frame cache's body only from the hooked 0xa9e5b0, trap Hooks.nGZSceneGraph_cFrameCache_vf3).
- Facts:
  - DrawViewer returns the pushed pass (a pointer, the whole eax). The texture loop is not bounded by the pass's four
    texture words (ORIGINAL-BUG marked). The viewport rectangle rounds with the 1.5 * 2^23 bias and a tie rule
    (RoundViewport); the holder's QueryInterface hands out the frame cache, which is flipped twice when more than a
    frame behind and released at the end (neither null-checked).
  - The item's word 0 is uninitialised by the builder (the caller's stack): tests mask it.
- Tests (tests/sg_display_list_draw.cpp, world shared with sg_display_list.cpp through tests/sg_display_list_world.h):
  build_item 3000 seeds, pass_helpers 1500, frame_cache 3000, draw 2500 (random sequences of DrawViewer, AddGeometry,
  entries, nodes, light records, DrawPass, reconfiguration and frames on both sides, every viewer, device, strategy,
  material, frame-cache and holder call compared with arguments and order, the pass stack, passes, items, stats and
  refs), and strategy_exception (one-sided).
- **The harness cannot run the original's SEH.** An exception raised under the original 0xaa24c0 kills the test
  process (the original image's frame handlers are not registered with the harness's dispatcher). The exception path
  is tested one-sided: ours raising at the first or second strategy call against the calls the original makes up to
  that point, then returning normally.
- 0xaa1640 is patched to a logging stub on both sides (CodePatch) so the log's call is compared without running it.

### DLb result, part 3: the default render strategy and the lights (lane TW, 2026-11-02)

7 verified, in src/sg/cDefaultDisplayListRenderStrategy.* (new class) and src/sg/cDisplayListScene.cpp:
- cDefaultDisplayListRenderStrategy (8 bytes, a refcount): constructor 0xa9d450, Render slot 4 0xaa2350, the deleting
  destructor 0xa9e560 and the destructor 0xa9e080. Its other slots (QueryInterface, AddRef, Release, BeginPass) are
  ours too, covered by the life test (no status entries of their own).
  In effect through callers: our SetRenderStrategy builds ours, the game's vtable 0xe754e0
  is never installed. Render: the view's handle texture (slot 12) or a reset of 8, the projection, viewport and depth
  range commands, the viewer's slot 104 (GetClearInfo, cViewerClearInfo, colour preset {1,0,0,0}) and Clear with
  `(flags&2 ? 6 : 0) | (flags&1)`, the item count added to the pass's first stat, each item's variant slot 11.
- AddLight slot 30 0xa9f140, its growth 0xaa2890 (GrowLights) and the record constructor 0xa9d320 (inlined into ours).
  LightRecord (0x80: light, node, type, layer state, flag, current, frame, inverse frame) and AmbientRecord (0x10) are
  in cDisplayList.h; Pass::Shows is the pass's layer test (also used by the scene slots).
- Facts:
  - A new light record has only the two frames' first words set (0x1f) by 0xa9d320; AddLight writes the rest except
    the two pad bytes after the current flag (the heap's).
  - GrowLights rebases the current-light list when the block moves: `((p - old) & ~0x7f) + new`, with old taken as 0
    while no light is in use (count 0x54 == 0), so numbers in the list are rebased as offsets from 0. The game never has
    current lights while none is in use; the add-light test seeds the same, the growth test covers the numbers too.
  - AddLight's ambient path (type 0) appends to the ambient records and counts current ones in word 0x84; the typed
    path calls the node's slot 96 with the record index before filling it and lists the record as current when the
    node's detail limit equals the object's frame.
- Tests: tests/sg_default_render_strategy.cpp (render 600 seeds, life 400) and tests/sg_display_list_lights.cpp
  (add_light 3000, grow_lights 3000): every call with its arguments in order and the state left. Render has no SEH (C++
  EH only).
- Left of DLb: vf28 0xa9e700 (2059 bytes, Render28 still forwards: helpers 0xaa0130, 0xaa2920, 0xaa34c0, 0xaf2200,
  0xaf2340, the material manager slots, the draw item building and the depth sort), then the 15 sort hooks and
  InsertDrawItem 0xaa1290 to traps (vf28 is their last original caller), and the item log 0xaa1640.

### DLb result, part 4: slot 28 and the sorts' traps (lane TX, 2026-11-02)

4 verified, in src/sg/cDisplayListSubsets.cpp (with the GeometryInfo / GeometrySubset / cIGeometryFormat views in
src/sg/cIGeometryRender.h, and mBoneFrames / mDepths in cDisplayList.h):
- AddGeometrySubsets, slot 28 0xa9e700: a geometry's subsets as draw items in the current pass. It is the image body's
  only path; our vtable no longer forwards (Render28 removed, the constructor entry has no original_calls left).
  GrowBoneFrames 0xaa2920 (the bone frame block, rebasing items that point into it, rounded down to whole 0x34-byte
  placements, from 0 while none is in use), CentreDepth 0xaf2200 (returns out) and NearestCornerDepth 0xaf2340 (both
  __cdecl; depth = z/w of the view-space point, 0 when w <= 1e-5; the nearest corner starts at 1e6 and a NaN keeps the
  least so far).
- Facts:
  - Flags: 1 no lights, 2 own lights per subset (and sub key bit 19), 0x10 bones (with poses and switch 3), 0x20 blend
    (with word24 and switch 4), 0x40 centre depth, 0x80 cull by the frustum, 0x100 sub key bit 18.
  - One item is reused across subsets and written field by field, so the light selection 0xaa0130 sees the previous
    subset's material, sort key and depth. Without either light flag the items copy the light state of the item at
    mWordE4 (the node's first) when there is one, else select against the record's box.
  - When any subset's shader has m84 bit 0, the new items are sorted in layer order (0xa9d100), and the depth-sorted
    ones get their depths (each plus index * 1e-5) sorted ascending (0xa9cc30) and written back in order.
  - 0xaa34c0 is std::vector<DrawItem>::resize (library, no entry); an stlprobe for the 0x44-byte element would confirm it.
- The 15 DisplayListSort hooks and InsertDrawItem 0xaa1290 are now traps (in effect through callers, the factory hook
  cDisplayList_Create): slots 24, 28 and 29 are their only callers. The InsertDrawItem adapter is gone; the hook table
  keeps the factory and the frame cache's two slots.
- Tests: tests/sg_display_list_subsets.cpp: subsets 3000 seeds (1-6 calls on one list with the inputs changed between
  them), grow_bone_frames 3000, depths 20000 (+-0, NaN, inf, huge). 0xaa0130 is CodePatched to a logging stub on both
  sides; every fake call with its arguments in order, and the items, bone frames, depths, records and refs left. Slot 28
  has no SEH frame of its own (the vector growth's allocation failure is the library's).
- Left of DLb: the light selection 0xaa0130 and its tree (still an original_call of slot 28), and the item log 0xaa1640.

### DLb result, part 5: the light selection and the item log (lane TY, 2026-11-02)

8 verified; cDisplayList has no original calls left.
- Light selection, in src/sg/cDisplayListLights.cpp and src/sg/cFrameCache.cpp. SelectLights 0xaa0130 picks an item's
  lights, either fresh or reused from last frame. It returns al only. Its helpers:
  - FindCachedLights 0xaa2f40: last frame's lights, when still valid;
  - InsertLight 0xaa1430: strength order, ties by node address, a full list refuses weaker lights;
  - GrowLightBytes 0xaa27e0: resizes the light bytes and rebases the sorted items' blocks;
  - cFrameCache::CopyLastFrame 0xa9e5d0 and StoreLights 0xa9ef10.

  Slot 28 calls ours. Tests: tests/sg_display_list_select_lights.cpp (select 6000 seeds, grow 6000) and tests/sg_display_list_lights.h
  (the light world, shared with the subsets test, which now runs the selection for real).
- The item log, in src/sg/cDisplayListLog.cpp. LogDrawItems 0xaa1640 (cdecl: log, items, count) writes every sorted
  item through cIDisplayListLog:
  - the index, sort key, variant, material name and index, geometry, user data and sub index (SetUint16);
  - the sub flags in the order 4, 2, 1, 8;
  - layer, depth, node and the node's slot 88;
  - the shader context;
  - a seven-row light table, each column padded to one more than the widest;
  - the ambient colour (the three floats before the light block, zero without one).

  DescribeShaderContext 0xaf4c90 (cdecl: string, state words) builds the shader context text:
  - the vertex format's Describe and three counts from state word 2;
  - the flags, where the last one set goes without its separator;
  - the lights per type, when state word 1 has its header nibble. The separator depends on how many are left.

  All the texts are game strings (GameAddress). Formatting goes through cRZString::Sprintf. In the harness the
  original's side uses the image's own CRT, and the text matched character for character, including NaN, infinities,
  denormals and huge values.

  Tests: tests/sg_display_list_log.cpp (log_items 4000 seeds, describe_context 4000). The draw test now runs the log
  for real: the log, material manager and formats manager are fakes, and the viewer and nodes have slot 88.
- Test fix: the light pointer normalisation (LightPointers in tests/sg_display_list_lights.h) remembers each item's
  record from before a call. The reason is that the heap can give a new light block an old block's address on one
  side only, and then a stale pointer reads differently on the two sides.

## Milestone N: the image-free GMDC decoder (done)

Definition: `docs/roadmap.md` (N). Done 2026-10-30: `gmdc_decode` links normally and decodes the 32 resources of the
acceptance list as the original decoder does (plain and ASan builds), and the hooked game's 705 decoded GMDCs all match
(`tools/gmdc_crosscheck.py`, coordinator, smoke-20261030-103951). Lanes QA, QD, QF, QI, QJ, QK, QN, QQ and QV; their
per-lane link counts and the lane plan are in the archive.

The program is `standalone/gmdc_decode.cpp`, target `gmdc_decode` (EXCLUDE_FROM_ALL). It follows the game's path:
segment Construct / Init / SetPath / Open, the key list through a type filter, OpenRecord, the record's cIGZIStream face,
cGZPersistBinaryReader::ReadRoot into cGeometryDataContainer, then a summary. It links `ts2core_noimage.lib` (ts2core
less gamebase.obj and origsyms.obj) and never the harness. `StandUp()` stands up a small function-table framework (slot 5
answers the service query with our `gz::cGZPersistResourceManager`, slot 23 gives the COM object over the real
`rz::cGZCOMDirector`); the real `gz::cCTGFramework` was rejected because its vtable roots every slot (lane QI).
Owning a vtable roots every one of its slots in `ts2_own_vtables[]`, so counts can rise before they fall (lane QF).

- **The CMake decision.** MSVC resolves every symbol a pulled-in object names, also in sections /OPT:REF drops later, so
  dead code naming an image symbol still failed the link. The fix keeps the link honest instead of forcing it: a
  PRE_LINK step (`tools/noimage.py --write-aliases <obj> <lib> <program objects>`) writes an object whose `.drectve`
  sends every image symbol left undefined (`_ts2_rva_0`, `OriginalEntry<RVA>`, unowned `_ts2_rva_<hex>`) through
  `/alternatename` to `ts2_noimage_trap` (`standalone/noimage_trap.cpp`, its own `/Gy` COMDAT). The link uses
  `/OPT:REF /INCREMENTAL:NO`. A POST_BUILD gate (`noimage.py --check --delete`) reads the map and keeps the exe only if
  the trap was dropped (nothing live reaches an image symbol) and no image reference is left; otherwise the build fails
  and the exe is deleted, so no runnable exe that could reach the image exists. noimage.py's counts now skip a
  reference an alternatename satisfies (so the old stand-in no longer shows as live), and the gate caught one the
  static scan had missed. `standalone/crash_report.cpp` prints a crash's offset into the exe, which the map names.
- **0x9b35f0 ported.** `cGZPersistCacheStrategyBasic` (`src/gz/`) is a whole class now: the constructor and its 16
  slots, `verified` by `tests/gz_persist_cache_strategy_basic.cpp` and in effect through callers (cTSPersistSystem's
  Init and the resource manager twin's constructor call ours). The stand-in in `owned_statics.cpp` is gone.
- **The closure.** Neither cTSBinCatalog's key filter nor cGZDBSegmentDirectoryFiles's remaining `OriginalFunction`
  users are reached from gmdc_decode's live code (the gate proves it), so they stay for their own lanes.
- **Runtime fixes the link could not show.** Two lifetime bugs (both found by ASan): the program released the resource
  manager one more time than it took it (now `AddRef` in the stand-up), and the record was closed after its segment was
  gone (now the program holds the segment until the end).
- **The adaptor.** 15 of 32 resources first failed with `bad_alloc`: the program handed the record's own `cIGZIStream`
  to ReadRoot. The game's path is `cSceneGraphFactory::Read` (0xa4d750), which wraps the record's stream in a
  `cIStreamStringBehaviourAdaptor` (vtable 0xe6c648, 0x14 bytes, inner stream at +0x10) so that every getter, above
  all GetGZStr, is cRZIStream's (packed length, then the characters). The record's own GetGZStr misreads a name of 32 or
  more characters. Read also passes the record's slot 15 (id 0x656b8f0c) as ReadRoot's context. Now ours:
  `src/gz/cIStreamStringBehaviourAdaptor` (owned vtable face, the inlined constructor as `Construct`), `verified` by
  `tests/gz_istream_string_adaptor.cpp`, slots 0, 3 and 21 hooked; 17 and 18 are folded with
  cOStreamStringBehaviourAdaptor's and not hooked. gmdc_decode builds its stream as Read does. **For the scene-graph
  lanes:** port Read 0xa4d750 with this adaptor; sg's `cStringPool` still names the image's 0xd9b9d0 and 0x10f8c60,
  which is why gmdc_decode keeps the NamePool and scene-graph-system stand-ins.
- **Acceptance.** `standalone/gmdc_summary.h` defines the canonical summary (per section a count and a 64-bit FNV-1a
  hash, floats by bit pattern, the decoder's own order). `tests/gmdc_decode_acceptance.cpp` (test
  `gmdc_decode_acceptance`) reads 32 GMDC resources from `local/game`'s packages at test time with its own DBPF / QFS /
  RCOL reader, decodes each with the original decoder and ours, compares, and prints the original's summary as a
  `gmdc-expected <package> <instance> <group> <resource> <summary>` line. The procedure: run that difftest, then run
  `gmdc_decode <package> <instance> <group> <resource>` for every expected line and compare its `summary` line (both
  32 of 32). ASan is feasible: a separate build directory configured with `/fsanitize=address` in the C and C++ flags
  (Win32, v143 14.40) builds gmdc_decode, and the run needs the MSVC `bin/Hostx86/x86` directory on PATH for the ASan
  runtime DLL. Not done (lanes never run the game): the comparison with the hooked game's own decode.

**N-f, the hooked-game check.** `TS2HOST_GMDC=1` makes the host log one `gmdc-expected - <instance> <group> <resource>
<summary>` line per GMDC the game decodes (`src/hook/gmdc_diag.cpp`; `cGeometryDataContainer::Read` calls
`nGZSceneGraph::gDecodeObserver`, null when off). `tools/gmdc_compare.py` pairs the lines with the acceptance list;
`tools/gmdc_crosscheck.py <host log>` runs `gmdc_decode` on every key from the packages holding it (an expansion package
may override a Base key, so a match in any package holding it counts). Procedure and result: the archive.

**Measuring it.** The LNK2019 list overcounts: it names every function of every pulled object, including ones /OPT:REF
drops later. The real list comes from a forced link and the map:
`cmake -DTS2_NOIMAGE_FORCE=ON build`, build `gmdc_decode`, then
`tools/py -I tools/noimage.py build/Release/gmdc_decode.map build/ts2core.dir/Release build/gmdc_decode.dir/Release --log <build log> [--by-function]`.
Turn the option off again afterwards, and never run the forced exe. Its last lines compare with the previous run
(saved in the checkout's `build/noimage-gmdc_decode.last.jsonl`, or `--against <earlier --json>`): references removed
and exposed, each named, so a lane sees which references owning a vtable brought into view. The tool reads our objects' relocations to
`_ts2_rva_0` (the addend is the RVA) and the OriginalEntry names, for the functions the map says were linked, and names
each RVA from db/.

## Milestone O: one object rendered image-free

Definition and acceptance: `docs/roadmap.md` (O). Lane VJ's assessment (2026-11-03, with lanes VK-VX's results) is archived in
`docs/archive/scenegraph-O-assessment.md`; its open rows are under **Open** below and in the owning campaigns' docs. What follows is the exe, how
acceptance is checked and the harness test; the 2026-10-30 link list, its virtual-reach table, the O lane list and each
lane's gate counts are in the archive.

### The exe (lane QX, O-a)

`standalone/render_object.cpp`, target `render_object` (EXCLUDE_FROM_ALL, so the default build is unaffected). It uses
gmdc_decode's link: ts2core_noimage.lib, the PRE_LINK trap aliases, `/MAP /OPT:REF`, and the POST_BUILD gate that
deletes an exe still referring to the image. To build it: `cmake --build build --config Release --target render_object`.
Today the gate fails and deletes the exe, which is the expected state until the lanes below are done. To measure:
configure with `-DTS2_NOIMAGE_FORCE=ON`, build, then filter and run as described in the noimage.py note below. Never
run the forced exe.

The usage is `render_object <Sims3D dir> [cres-name [out.bmp]]`. The defaults are `sculptureChinaVase_cres`
(group 0x1C0532FA: CRES in Objects05, SHPE in 06, GMND in 04, GMDC in 03, TXMT in 02, TXTR in 06, LIFO in 09) and
`render_object.bmp`. It follows the thumbnail factory's path. The game draws catalog thumbnails through
cTSUIThumbnailFactorySceneBased (vtable 0xe022d4). Its slots are named in Ghidra (lane QX, tentative):

| slot | RVA | name |
|---|---|---|
| vf14 | 0x7cc088 | SetUpScene |
| vf24 | 0x7cc350 | LoadModel |
| vf16 | 0x7cc8a3 | RenderFrame |
| vf17 | 0x7cbd67 | ReadBackPixels |
| vf27 | 0x7cb381 | AttachAnimation |

The exe runs these steps:
1. **Start-up.** A function-table framework stand-in, as gmdc_decode's. Slot 3 adds a service, slot 5 queries it,
   slot 9 is AddToTick, slot 23 is the COM stand-in over our cGZCOMDirector. The director registers the classes the
   path makes. Ours: cSceneNode, cTransformNode, cResourceNode, cGeometryNode, cGeometryDataContainer,
   cCompositionTreeNode, cLODLevelsStore and cLODLevelsTable, cSceneGraphSystem, the file parser, cGZBuffer, and the
   four cSurfaceDX classes. The rest go through `NotOursYet<rva>`, an `OriginalFunction` of the director's factory:
   - viewer 0xa65fb0, shape ref node 0xa55270;
   - SHPE 0xa5d3b0, TXMT 0xaadf20, TXTR 0xab1590, LIFO 0xab15f0;
   - display list 0xaa0e00, render group manager 0xaa37f0;
   - shader 0xaa4fa0, shader initialiser 0xaa82a0, material attributes 0xaaa3c0;
   - textures 0xab4d30 / 0xab4d90 / 0xab4df0 / 0xab4e50;
   - scene-graph factory 0xa4d600, display monitor 0x1545c0.

   Then the services, all ours: cEventSystemService, cGZMessageServer2, cCanvasW32 (256 x 256, hidden), cGZCanvas,
   cGraphicSystemDX, cGZGraphicSystem, cGZPersistResourceManager (with Objects01-09 as packed-file segments), and
   cSceneGraphSystem.
2. **Device.** `GetDevice(0)`, then GetSettings, one back buffer, ApplySettings (CreateDevice), and GetD3DDevice.
3. **Scene.** A cSceneNode, the viewer (`kViewerIid` 0x5ca76db6) and a cTransformNode, made through the COM director.
   The viewer and the transform are the scene's children. The viewer's set-up is the thumbnail factory's: slots 124,
   110 (viewport), 130 and 132 (perspective, 20 degrees), 131 (aspect), 128, and 91 (clear to black, depth 1).
4. **Model.** `StringKeyParser::ParseKey` gives the CRES key (type 0xe519c933). The resource manager's slot 3 returns
   the cResourceNode. It is cloned (cIObjectGraphNode slot 15, mode 1) and put under the transform at the origin. If
   the CRES carries its own viewer, the exe uses that one.
5. **Frame.** BeginScene, the viewer's render slot 123, EndScene.
6. **Image.** GetBackBuffer, then GetRenderTargetData into a system-memory surface, written as a 24-bit BMP.

What the exe still lacks for acceptance:
- **Light rig.** It has none yet. The thumbnail factory adds an ambient light and a directional light, so N2's two
  classes are on the list below.
- **Call log.** It does not log its device calls yet, which acceptance (c) needs; that is part of lane O-d.
- **Camera distance.** It is fixed. The thumbnail factory computes it from the model's bound; acceptance only needs it
  the same on both sides.

### How acceptance is checked

- **(a) Call stream.** `tests/sg_render_object.cpp` (O-b) on the d3dfx / drawfx fakes (`tests/d3dfx.h`). Per seed, one
  object of at least three kinds: plain (sculptureChinaVase), one with alpha, and one with several materials or LODs.
  The test's own DBPF reader gives both sides the same resource bytes. Each side builds the scene from its own factories:
  scene node, viewer with the thumbnail factory's set-up, transform, and the CRES through the resource manager and
  Clone. It then renders one frame through viewer slot 123. The comparison covers:
  - the full device log: states, shader and constant set-up, declarations, streams, and draw calls;
  - vertex, index and texture bytes, by hash (drawfx normalises pointers into registered ranges);
  - results, reference counts and blocks left.

  Until every row is ours, our side calls the remaining originals through `NotOursYet`, so the test measures progress.
  Acceptance is the run with none left. Mutants on the rows' code must be killed by this test as well as by their own.
- **(b) Image-free.** `cmake --build build --config Release --target render_object` succeeds: the POST_BUILD gate
  passes and keeps the exe. With the noimage.py fix (or the map filter above), the forced map lists nothing. The exe
  then runs from a staging directory holding only render_object.exe, d3d9.dll (system), the system DLLs and the
  chosen Objects packages.
- **(c) The picture.** The BMP is checked by the user (O-d). The exe's own device-call log, in the same format as the
  harness's, equals (a)'s log for the same object.
- **(d) The hooked game.** A host diagnostic logs the per-draw call stream while the game draws the same object's
  catalog thumbnail (cTSUIThumbnailFactorySceneBased renders it alone, on its own target). The coordinator compares it
  with (a) and (c). If no screen isolates the object, the reason and the nearest comparison are documented.

### O-b result: the geometry slice (lane RA, 2026-10-30)

`tests/sg_render_object.cpp`, test SG_Render_Object (15 cases, about 1 s). It is scoped down to the geometry slice of
(a). The rest of the object's path (materials, textures, shaders, cShape, the CRES, the viewer and the display list)
is not ours yet. Run end to end today, both sides would run the same original code for most of it, so it would compare
little, and it needs start-up state the harness does not stand up. The test grows with the rows (next steps below).

**Objects.** Real GMDCs from `Base\TSData\Res\Sims3D\Objects03.package` in the game copy, read only by the test's own
DBPF/QFS reader (`tests/gmdc_package_fx.h`). Without the copy the test prints SKIP. All three have type 0xac4f8687 and
group 0x1c0532fa:

| kind | object | instance | resource | bytes |
|---|---|---|---|---:|
| plain | sculptureChinaVase | 0xffddec53 | 0xf890e1b7 | 11309 |
| alpha | sculptureUrn | 0xff9d1031 | 0xd7b02a85 | 71201 |
| several materials | sculptureVenus | 0xff67dab5 | 0x63e93daf | 85464 |

**Device setups.** Each object runs under five setups:
- caps 0x1f;
- hardware only;
- software only;
- caps 0x1f with a stream stride limit of 8, so the device has no format;
- device not ready.

**Per side.** Each side makes its own geometry node (the factory 0xa42590, or ours) and keys it to the object's GMDC.
The steps:
1. **Load.** Rebuild 0xa3ebe0 loads the key through the resource manager fake. Its load decodes the real resource with
   the side's own container (0xa70aa0, or ours) through `Read`.
2. **Build.** BuildGeometry 0xa3f180 builds the node with the real vertex formats manager (ours, on both sides).
3. **Draw.** The render face draws every element:
   - IsSupported 0xa41ee0: device buffers, the vertex copy, and declarations and state blocks through ResetDeviceState
     0xa419e0;
   - Render 0xa435d0 with and without state blocks (the hardware path);
   - Prepare 0xa429b0 then Render (the software path);
   - then a lost device and IsSupported again.
4. **Release.** The node, the container and the triangle sets are released.

**Compared, in order, at the drawfx / devfx fakes:**
- the device's ProcessVertices and DrawIndexedPrimitive;
- the device state's ApplyStateBlock, SetVertexDeclaration, SetStreamSource, SetIndices, CreateStateBlock and Reset;
- the declarations manager, the device enum's caps and limits;
- the dynamic arrays and the device buffers (Init, Lock, Unlock);
- the node's members after the build;
- the vertex and index bytes, hashed over each buffer's size;
- the blocks freed and every fake's references.

There are no texture, shader or constant calls in this slice, so none are compared yet.

**Stood in on both sides:**
- the event registry 0xa42f30 (a test map, as in sg_geometry_node.cpp);
- the director getter 0x1ae18d (a fake). It hands out the device buffers and each side's own triangle sets: the factory
  0x196560, or our cTriangleSetShape::Create, as the hooked game's director does.

**The gauge** (`tests/reach_census.h`) answers how much of this path runs our code in the game. For each side's run,
every game function start in db/functions.jsonl gets a breakpoint byte, except library code and the two stand-ins. The
first entry into each function is recorded. The test then prints three sets per object, for load, draw and the total:
- **path:** every game function either side entered;
- **still original:** the ones our side still enters (through `OriginalFunction`, or code those reach) that no hook
  replaces in the game;
- **ours:** the rest. These are the original side's functions that our code no longer needs, plus the hooked ones our
  side reaches.

"Ours in game" is ours / path, in bytes. A function moves from still original to ours when its row lands and nothing of
ours calls it any more. The test also lists the largest still-original functions.

Today all three objects reach the same functions:

| phase | path | still original | ours in game |
|---|---|---|---:|
| load | 161 fns, 35657 B | 16 fns, 2285 B | 93.6% |
| draw | 9 fns, 3613 B | 2 fns, 446 B | 87.7% |
| total | 164 fns, 37225 B | 16 fns, 2285 B | 93.9% |

The still-original functions are the `original_calls` of cGeometryNode's build and render:
- the element header and range fillers 0xaee030, 0xaee820 and 0xaee9f0, and two cIndexedIterator slots (0xaed730,
  0xaed7d0);
- the device format lookup 0x18daa0, its variant 0x18d990, and the known-identity test 0x18da20;
- smaller helpers.

**Mutants.** Five on our code that the path reaches; all were killed:

| mutant | killed at |
|---|---|
| Render's base vertex + 1 | DrawIndexedPrimitive |
| Render's software stream offset + 4 | SetStreamSource |
| BuildGeometry's narrowed index ^ 1 | the index buffer's hash |
| StoreRange stores 3 of 4 bytes | the build's bound |
| the container's index lists, last = first | the vertex buffer's Init size |

**Next steps.** Each step widens the slice as its rows land, and moves the gauge's path from the geometry node up
toward the viewer:
1. H1 (cShape) and M1-M2 (materials, shaders): load the object's SHPE and TXMT, and draw the node through the shape's
   material. This brings shader and constant set-up into the stream.
2. M4 (textures): the TXTR's bytes, hashed, at SetTexture.
3. H2, N3 part 3 and DL: the CRES through the resource manager and Clone, then the viewer's render slot 123. That is
   (a) as written above.

### Image-free work in the graphics layer (lanes RB, RJ, RN)

#### O-c result: the graphics layer's image references (lane RB, 2026-10-30)

Measured with the render_object build's NOIMAGE list (forced link, after /OPT:REF), grouped by the objects that use
each reference. "Graphics" means every user is a graphics-layer object (device, device state and enumeration, canvas,
multimon, image manipulation, graphic system, surfaces, shaders, vertex formats, graphic services, MiscHelpersA, and the
RZ pieces only they pull in: block pools, the stopwatch, cRZThreadSignal). "Other" is the scenegraph, the parser, the
property sets and render_object.cpp itself.

| share | before | after | after, by kind |
|---|---:|---:|---|
| graphics | 272 | 26 | 16 original functions, 6 data (one is the image base), 4 vtables |
| shared with other layers | 3 | 1 | the event-id helper 0x1a9942 |
| other | 115 | 122 | not this lane's (the rows below); +7 because owned statics and faces root more of their code |
| total | 390 | 149 | address data 50, original data 3, original function 70, vtable 26 |

How (one commit each, `git log` on the branch):
- **D3DX9 / DxErr9 (16).** standalone/owned_graphics.cpp aliases the `OriginalFunction` entries to the DirectX SDK's
  (Summer 2004) functions; render_object links d3dx9.lib and DxErr9.lib with /SAFESEH:NO and
  legacy_stdio_definitions, and without VC 7.1's libcp/libc.
- **Import slots (62).** `src/game/import_slots.h` names each IAT slot the graphics layer calls through as an owned
  static: the game's slot in hooked builds (so harness fakes still apply), render_object's own import image-free.
- **Statics (about 170).** Report texts (own wording, same printf arguments), IIDs, the device, state and enumeration
  tables, behaviour records and cells, the canvas and multimon cells, the eleven fixed-size pools, the graphic services
  caches, MiscHelpersA's texts and mode byte, the surface copy's stretch kinds: owned statics, defined by
  standalone/owned_graphics.cpp and owned_statics.cpp.
- **Vtables.** nGZEvent::cNullEvent, cScalarEvent<unsigned long> and cConnectionChangedEvent are owned vtables with our
  faces (`src/gz/cEventFaces.h`, tests/gz_event_faces.cpp). The other 26 in the total belong to other layers.
- **Originals, verified (hooked at their RVAs, difftests, mutants):** the fixed-size pool allocator 0x149040
  (cGZMessagePool::Allocate), the current device's enumeration 0x18b470, the GetSystemMetrics stub 0x154a80,
  cShaderProgramsManagerDX::ReleaseLinker 0x176e40, the pixel formats manager getter 0x15eb10 and the multisample clamp
  0x18b7d0.

#### O-c2 result (lane RJ, 2026-10-31)

Nine originals are ours, hooked and verified, with 44 mutants, all killed:
- the graphic system's searches and the device switch, in src/gz/GraphicModeSearch.cpp: 0x18b490 FindClosestDisplayMode,
  0x18a9b0 FindClosestColorFormat, 0x18ab60 FindClosestDepthFormat, 0x18b250 SetDeviceMode and 0x18b440
  SetDeviceDesktopMode (all cdecl);
- the device's event registry 0x16a170 (src/gz/cDeviceDXEvents.cpp, cdecl `(id, attribute)`, map 0x10ca220);
- the state dump 0x17da90 (src/gz/cDeviceStateDXDump.cpp, thiscall);
- the texture filter checks 0x18c7f0 / 0x18df30 (src/gz/TextureSupportDX.cpp, cdecl).

Each has its own difftest: tests/gz_graphic_mode_search.cpp, gz_device_event_registry.cpp, gz_device_state_dump.cpp and
gz_texture_support.cpp. Our callers now call ours directly, so the older tests that patched these originals use
`shape::StandIn` instead (gz_graphic_system, gz_device_create, gz_device_dx_class, gz_device_state_dx). A replacement
that a test stands in has to live in its own TU, so it isn't inlined into our caller.

The cSceneGraphFactory wiring is done, all in standalone:
- the creator 0xa4d600 is ours in render_object's COMDirectorData;
- the O-adapter vtable 0xe6c5e4 is `/alternatename`d to `rzface::cOStreamStringBehaviourAdaptor`'s face;
- the factory static 0x10f61b0 is defined in owned_statics.cpp.

The texture checks' two file-name strings 0xda0e78 / 0xda10d8 are in owned_graphics.cpp.

render_object NOIMAGE gate: 157 image references before, 145 after.

#### O-c2: the memory-block classes (lane RN, 2026-10-31)

The two MemBlock rows above are done: `nRZGraphic4::cMemBlockAsSurface` / `cMemBlockAsTexture` and their Direct3D
sides `cMemBlockAsSurfaceImpl` / `cMemBlockAsTextureImpl` are ours (src/gz/cMemBlock.h/.cpp, db/status/cMemBlock.jsonl,
tests/gz_memblock.cpp). The Impls derive from the real `IDirect3DSurface9` / `IDirect3DTexture9`, with the virtual
destructor appended (slots 17 / 22), so `GetSurfaceLevel` and the engine's Direct3D queries hand out real COM objects.
- Hooked (9): the constructors 0x18d1a0 / 0x18d1e0 / 0x18c300, the destructors 0x18d2c0 / 0x18c350, the members
  Init 0x18d500, SetTopLevel 0x18c5b0, AddLevel 0x18c3e0 and the level getter 0x18c700 (slots 25-28, and called
  directly by the game's image code 0xab23d0). Every other class-specific slot (38) is in effect through callers.
- cImageManipDX::ComputeNormalMap, cImageManipDXMemory (0x184710) and cDraw2D::SetRecordPixels build ours; their
  `original_calls` lost the six entries.
- Shared bodies left as the game's (folded across classes, not hooked or counted): 0x178b40 Release, 0x18c7d0
  ReleaseResource, 0x18c4d0 GetPool, 0x18c7a0 GetQuality, 0x18c4c0 the Impls' AddRef, 0x16a8xx / 0x16a9xx and the
  other constant Impl slots. Our classes implement them; the game's copies stay for their other users.
- Still original, outside this lane: the game's own users of the constructors (cImageData vf16 0xab03c0, 0xab23d0,
  0xab4780, 0x367c20, 0x36b3e0, cTSUIThumbnailFactorySceneBased vf17) now get our objects through the hooks.
- render_object NOIMAGE gate: 145 before, 139 after (45 to 39 original functions, 7820 to 7191 bytes; the six MemBlock entries gone).

The command parser's image references (lanes RQ, RS, RU) and lane RW's cRZRefCount table, cLODLevels and shared
message-server getter references were per-lane gate work: their records are in the archive; the gate's current list is
in the O assessment.

## Open

Built 2026-11-03 (lane VL) from `tools/classinfo.py` over every nGZSceneGraph class with a vtable (106 classes; 99
have at least one function not `verified`). classinfo counts every function its vtables and members reach, including
bodies folded with or shared by classes outside the cluster (the nTSSG lights and nodes, `shared_vf_*`, template copies
of classes that are not ours yet), so the counts are an upper bound on the cluster's own work, not a lane list. Rerun
`tools/classinfo.py <class> --todo` before taking any of them.

- **Largest to-do counts (shared or template-copy bodies, mostly):** cPointLight 85, cDirectionalLight 80, cAmbientLight
  80, cStandardLightBase 77, cShape 66 (8 reimplemented), cExplicitBoundNode 63, cSceneNode 59, cViewerRefNode 56,
  cShapeRefNode 53, cLightRefNode 53, cSpotLight 52, cDrawStateLight 52, cTransformNode 48, cLinearFogLight 48,
  cLooseOctreePartitionNode 44, cCompositionTreeNode 41, cGeometryNode 40, cResourceNode 39, cMaterialDefinition 36,
  cImageData 35, cTagExtension 33, cEnvironmentCubeLight 30, cLevelInfo 27, cBoneDataExtension 26.
- **Builders not taken:** cOptimizedGeometryBuilder 25, cAlphaSortBuilder 24, cProcessDeformationsBuilder 23,
  cGeometryBuilder 19, cCompactorBuilder 17, cTangentSpaceBuilder 15, cBoundingVolumeBuilder 6.
- **Medium:** cViewer_LEGACY 23, cPrintCompositionTree 17, cDisplayList 14 (5 reimplemented), cViewerRefNodeRecursive 13,
  cGeometryDataContainer 13, cSGResource 9, cFindAttributeNodesTraversal 9, cExtension 9, the attribute-name iterators
  (viewer-ref and light-ref 8, shape-ref 3), cApplyNonStandardLightState 8.
- **Small (7 or fewer):** cShaderDynamicRectPreRenderStrategy, cShaderControllerNotify, cQueryAnyStrategy,
  cApplyForceDepthTestFailCondition, cApplyColorScalarRadiance (7); cTextureHandleIterator, cSkinExtension,
  cMaterialManager, cFindBonesTraversal (6); cTextureManager, cSubsetBindingIterator, cSceneNodeIterator,
  cRenderTargetTexture (1 mapped), cDataListExtension (5); and about 45 classes with 1-4 each (iterators, event
  messages, attributes, apply strategies, cShaderInit, cShader (1 reimplemented), cMaterialParser, cLODLevelsTable /
  Store, cRenderGroupManager).

**Milestone O's open rows (lane VJ's assessment and lanes VK-VX after it, absorbed by lane VY, 2026-11-03).** The
assessment itself is in `docs/archive/scenegraph-O-assessment.md` (verbatim, with each lane's result). The image-free
gate stood at 7 references after lane VX. The rows that belong to foundations (the stopwatch timebase, cRZStd 0xda3e6c,
cRZOFStream 0xda2c14, cVectorEvent<cVector4f> 0xdb07d4) and rendering (cGraphicSystemDX's strings and cError 0xd9cd70,
the cGZBufferStub pieces) are in those docs' Next lanes. Scene graph's own:
- **vtable cTextureHandleIterator 0xe7c7f4** (cTextureManager NewHandleIterator, gate reference). Its slots are bodies
  folded outside the class family (AddRef 0x148fb0 and 0x1a84c0, 0x17b680, 0x46390), so each slot needs the
  `shared_vf_*` reference audit (`docs/verification.md`) before the table can be ours.
- **`GameAddress(0)` users** (gate reference "0x0, 4 users"): cDisplayList::Init and
  cShapeRefNodeImpl::RebuildMaterialBlock (scene graph), and the cMaterialParserAdapters::Initialise vtordisp thunk
  (scene graph with command-parser). Each needs its own owned constant in place of `GameAddress(rva)`.
- **N2 light rig** (assessment 2.5): cAmbientLight (factory 0xa47ab0), cDirectionalLight (0xa47b10),
  cStandardLightBase, cLightRefNode; 124 slot bodies, about 5.1 KB, many of them shared or template copies. It is the
  Lights cluster above; render_object links none of it yet.
- **Acceptance infrastructure for O(a), (c), (d)** (assessment section 4; coordinator or a host lane, not a scene
  graph code lane): the light rig in render_object; a device-call log in the exe in the host's devlog format; the
  camera distance from the model bound; the end-to-end harness world for (a) (a real scene-graph Init on both sides,
  a director fake registering both sides' factories, the resource fake extended to SHPE / CRES / TXMT / TXTR / LIFO,
  the drawfx fakes extended to textures, shaders and render states; assessment 2.6); a `TS2HOST_DEVLOG` window around
  one thumbnail render (cTSUIThumbnailFactorySceneBased vf16 0x7cc8a3 to vf17 0x7cbd67, or viewer slot 123 0xa68f90);
  the ASan build (gmdc_decode's recipe) once (b) passes; a per-campaign grouping in `noimage.py --json`.
- **`GameAddress(cRZString::kVTable)` outside the render_object path**: 16 uses in 10 files under src/ (app, gz, ts;
  `grep -rn "GameAddress(cRZString::kVTable)" src`). Image-free they need `cRZString::VTable()`, as lane VJ did for the
  scene graph (assessment section 3). Each owning campaign changes its own when its program goes image-free; no
  behaviour or byte change in hooked builds.

Known open items carried from the lane notes:
- cShaderInit's object vector 0x20c700 / 0x255dd0 (shared template instantiations: take with their other users);
  0xaad320, 0xa7b0f0, 0xa5af90, 0xa845c0, 0xa84a60 and 0x42ffc0 are shared outside their classes (lane VG checked: take
  with their other users) (lanes UA, VB, VE, VG).
- compgeom: the G0b leftover 0x19be70 was done by lane TR (FramePlacement::ComposeRigid); what compgeom still has open
  is in `docs/subsystems/compgeom.md`.

**Already unreachable.** A further 37.8 KB of done classes' inferred helpers have no original caller left
(`callers.py`: all references ours): cGeometryDataContainer 7.1, cIndexedMeshBuilder 6.5, cShape 6.4, cMaterialManager
4.7, cMaterialDefinition 4.1, cMaterialAttributes 2.6, cGeometryNode 2.2, cSkinExtension 1.4, cShader 0.9,
cLODLevelsStore 0.6. They need no code, only a check that our callers' tests cover their work and a status record; how
they count is the coordinator's call. To recheck: `callers.py` over the group's executed not-done RVAs.

**Tests that patch an original RVA on the game's side only** (`shape::CodePatch`), for an RVA in this plan. Today both
sides reach it, because our code calls the image's function through `OriginalFunction`; the lane that replaces the RVA
switches the test to `shape::StandIn`:
- 0xaeb370 GetRenderGroupRegistry (DLb): tests/sg_renderable_fx.h, tests/ts_nhood_sg_manager.cpp;
- 0xaead80 GetDetailObject (H2r): done (lane TI), tests/sg_renderable_fx.h and tests/ts_nhood_sg_manager.cpp stand ours in on both sides;
- 0xae8980 (HS): tests/sg_material_manager.cpp, tests/ts_common_system.cpp, tests/ts_common_system_objects.cpp;
- 0x18daa0, 0x18d990, 0xc627e0 (HS): done (lane UA), tests/sg_geometry_node.cpp stands ours in on both sides.

(0xab5f50 ChooseTextureDescription, M4b: switched to `shape::StandIn` by lane TH.)

## Later clusters: nTSSG (lane NX's plan, 2026-10-26)

The nGZSceneGraph rows of NX's plan are done (archive). Its nTSSG rows stand as the later clusters (KB = executed
not-done bytes, run-1023; the bracketed labels are `rendering.md`'s):

| lane | groups | KB | needs | test | risk |
|---|---|---:|---|---|---|
| **TS: nTSSG system** | | | | | |
| TS1 | cDisplayListDynamicRenderStrategy (3% done), cDisplayListCensorRenderStrategy, the render surfaces | 29 | DL | sgfx device world | medium: 0x36e400 builds the censor strategy inline [T1] |
| **LG: nTSSG lighting** | | | | | |
| LG0 | structure pass: owners of cLightingManager (0x2c9000-0x2e6000 and the second file 0x3bd000-0x3c4100, 27 KB unnamed next to cLerpLightSet) and of cDiffusionManager; the LG1-LG5 split | - | - | - | - [T0] |
| LG1-LG3 | cLightingManager (class and the helpers LG0 assigns) | 94 | LG0, N2 | plain; float maths | high: size, float exactness [T2-T4] |
| LG4, LG5 | cDiffusionManager | 49 | LG1 | plain | high: float exactness [T5, T6] |
| LG6 | cLightMapManager, cShadowManager (9% done) | 30 | LG1 | sgfx device world | medium [T7] |
| LG7 | cShadow, cLightSettings, cLightRigManager, cDiffusionLightExtension rest, cAmbientFromLightMapLight, cHeightMapShadowLight, cLightingParserState, the other nTSSG lights | 29 | LG6 | plain; cChoreoQueue original | medium [T8] |
| **LT: lot geometry** | | | | | |
| LT1 | cLotSkirt, cWaterGeometryBuilder, cBlurBrush, cOverlayNode | 32 | D3 | plain; world-lot and cDraw2D callees original | medium: 21.7 of 25.6 KB inferred [T9] |
| LT2 | cLotImposterDisplay, cLotImposter, cLotImposterManager | 30 | D3, DL | sgfx (render to texture) | medium [T10] |
| LT3 | cMergedGeometryNode, cSeasonManager, the composited-texture update strategies, cGableRoof and the gables | 39 | M4, D3 | plain | medium; split if the gables are large [T11, T13] |
| **TS (rest)** | | | | | |
| TS2 | cTSSGSystem rest (and its retrofit to the standalone shape), cSceneManager, cTSWinSceneGraph | 35 | most of the above | plain + service fakes | medium [T12] |
| TS3 | cVideoBroadcastManager, c3DUIManager, cObjectModule, cQueryStrategyManager, cAnimRequest, cConfigParserState, cCustomChoreoQueue, cTSUIThumbnailFactoryObject | 26 | DL | plain | low [T13] |
| **CM: camera** | | | | | |
| CM1 | cTSCameraSmoothSims1Controller, cTSCameraControllerManager | 31 | N3 | plain; float maths | medium: seven vtables [C1] |
| CM2 | cTSCameraSmoothFirstPersonController, cCameraFirstPersonController, cCameraCameraManController, cCameraCreateASimController, cTSCameraGenericController, cTSCameraParserState, cTSConfigTargets | 19 | CM1 | plain | medium [C2] |
| CM3 | cCameraLive, cCameraBuild, cCameraNeighborhood, cTSCameraEffect, cCameraEffectsSupport | 21 | CM1 | plain | medium [C2] |

Each lane starts with an owner check on its groups (`coverage.py queue ... --group <class>`, the `~owner` lines):
cDisplayList, cGeometryNode, cIndexedMeshBuilder, cShapeRefNodeT, cLotSkirt and cLotImposterDisplay are 65-95%
owner-inferred, so their bytes may move between lanes.

Stages and in-game acceptance (coordinator, `docs/ui-testing.md`; device call logs as in `rendering.md`'s stages):
1. Nodes and geometry (G0, N, D): the lot loads and the smoke batch passes; device stream identical at the lot frames.
2. Materials, shape, display list (M, H, DL, TS1): the same, frame hashes per lot frame.
3. Lighting and lot geometry (LG, LT): the same at day and night lot frames, with a build-mode frame.
4. System and camera (TS2, TS3, CM): a lot-live run with camera moves (quiet input), neighbourhood view.

## Left (from the backlog, 2026-11-03)

- P08 rest (IR): cVideoBroadcastManager (15 KB; game std::map trees: needs the tree node layout), cTSSGSystem vf17/vf20/vf27/vf28 (large), vf84/vf85, vf99 0x248100, template slots 12-14, NhoodEntered, constructor/destructor; the managers' constructors and non-deleting destructors; Lighting vf36/vf37.
- cTSSGSystem standalone shape (JD): ctor 0x246110, dtor 0x246f00 (deleting 0x247aa0 verified); our vtables would carry the shared_vf getters/thunks (0xdae264 slots 1, 4, 7/10, 11, 32-44, 46-54, 58-60, 62-65; Release 0x254470 5-byte jmp; 0xdae3f8 slot 1; 0xdae40c slots 1, 6, 7). Stage A (KE.md): ~100 primary slots need adapters or callers entries; existing SG tests turn vacuous once members are virtual. IR's cTSSG header comments and cTSSGSystemResources.cpp may quote game text: check against the no-game-content rule.
- Six vtordisp thunk forwarders into the cSGResource service slots (cAnimResource vf196_10, vf196_13; cAnimResourceConst vf88_10, 12, 13, 14): add DIFF_TESTs in tests/sg_resource_helpers.cpp (thunk `this` = body this + adjustment + vtordisp at this - 4). SGResource_Slots misses a QueryInterface that skips AddRef (only GameDerived crashes): compare the refcount after QI.
- cSceneNode::Compact boundary untested (coordinator mutant `>` -> `>=` survived, 2026-10-28): free 3 / size 20 (also 6 / 40) is exactly equal in float (3 * 6.6666665f rounds to 20.0f) where the original does not compact: add a SG_SceneNode case removing 3 of 20 (and 6 of 40) nodes and flushing (OM).
- tests/sg_material_manager.cpp FlushPending runs with one entry since RG (destroy order follows heap addresses): compare handle calls as per-handle net counts. Name 0xad8b80 (nGZSceneGraph::cMaterialParser::IsStateOne) and 0xadc800 (::QueryInterface) in Ghidra for the next cMaterialParser lane. cImageData::GetRecord18/19 (src/sg/cImageData.cpp:880) have no test of their own (a mutant returning the other text survived, VK): add to the image-data slot test.
- callers.py `callers_handled` kind for functions reached only through an .rdata pointer table whose reader is ours would adopt NW's 909 span-table functions 0x8c9ce0..0x909e70 (notes/feedback/NW.md).
