// nGZSceneGraph's free helpers (lane 261111b, scenegraph row group SU): the cdecl functions the lighting, shadow,
// cinematic, CAS and effects code calls on scene-graph nodes and on the command parser's settings. All hooked at their
// RVAs (none under 5 bytes, none shared).
//
// Families:
//   property readers  0xaf7190 / 0xaf7210 / 0xaf7110 / 0xaf7290: a parser variable evaluated as an expression
//   tree helpers      0xae78f0 / 0xae7900 / 0xae7490 / 0xae7820 / 0xaead10 / 0xaeb0e0 / 0xaed420: tags, searches by
//                     tag, flag tests and per-node actions over a composition subtree (children: slot 42 / 38, or
//                     41 / 37 for the const walk); 0xae7d00 the shape-ref nodes' bounds
//   extension helpers 0xae1ed0 / 0xae42c0 / 0xaeb5b0 / 0xaeb3f0: a node's animated face, the face's channel owner and
//                     the node's data-list extensions by name
//   transforms        0xaeb2c0 / 0xaeee30 / 0xaf22d0
#pragma once

#include "misc/SmallVirtuals.h" // the branch filters
#include "rz/GameAllocator.h"
#include "rz/StaticGetters2.h" // ServiceCell

#include <cstdint>
#include <vector>

class cIGZUnknown;
class cIGZBuffer;
class cRZString;

namespace nGZSceneGraph {

class cICompositionTreeNode;
class cITransformNode;
class cIRenderableNode;
class cIDataListExtension;
class cIGeometryNode;

// ---- property readers -------------------------------------------------------------------------------------------
// cdecl (parser, name, value, changed): the parser's variable `name` (its slot 27, the text) evaluated by its function
// table (slot 23, asked after the text) through the expression evaluator. false, with nothing written, when the parser
// is null, there is no such variable or the value has the wrong element count; evaluation errors throw through.
// Otherwise the value is stored, *changed (when `changed` is set) says whether it differs from what was there, and
// true. Only al is read by the callers.

// 0xaf7190: one element; changed is !(new == old) (a NaN on either side counts as a change).
bool ReadFloatProperty(void *parser, const char *name, float *value, uint8_t *changed);
// 0xaf7210: one element, rounded to an integer by the 1.5 * 2^23 addition (nearest, ties to even, for magnitudes
// below 2^22; the low word of the sum's bits otherwise).
bool ReadIntProperty(void *parser, const char *name, int32_t *value, uint8_t *changed);
// 0xaf7110: one element; ONE byte stored, 1 when the value is not 0.0 (NaN too).
bool ReadBoolProperty(void *parser, const char *name, uint8_t *value, uint8_t *changed);
// 0xaf7290: three or four elements, the first three stored; changed when any of the three is more than 31 units in
// the last place away from the old one (the sign-magnitude bits mapped onto a two's complement line, so -0 == +0).
bool ReadVector3Property(void *parser, const char *name, float *value, uint8_t *changed);

// ---- tree helpers ------------------------------------------------------------------------------------------------

// 0xae78f0: node->SetTag(tag) (slot 19).
void SetNodeTag(cICompositionTreeNode *node, const char *tag);
// 0xae7900: node->GetTag() (slot 26).
const char *GetNodeTag(cICompositionTreeNode *node);

// 0xae7490: the first node, depth first from `node`, that is a cITransformNode and whose tag matches `name` (case
// blind), uncounted; a child with flag `skipFlag` set (when it is not negative) is not searched. Null when none.
cITransformNode *FindNodeByName(const char *name, cICompositionTreeNode *node, int32_t skipFlag);

// 0xae7820: the same search for a node of kind `iid` (slot 25) tagged `name`; the first such node's QueryInterface
// answers (counted in *out), and its subtree is not searched further. false when there is none.
bool FindNodeOfKindByName(const char *name, cICompositionTreeNode *node, int32_t skipFlag, uint32_t iid, void **out);

// 0xaead10: whether any node in the subtree has flag `bit` (the const walk, slots 41 / 37).
bool SubtreeHasFlag(cICompositionTreeNode *node, uint32_t bit);

// 0xaeb0e0: every node in the subtree with flag `bit` gets render layer `add` added (slot 77) and `remove` removed
// (slot 79), each when given; a child with flag `skipFlag` (when not negative) is skipped with its subtree.
void SetLayersByFlag(cICompositionTreeNode *node, uint32_t bit, const char *add, const char *remove, int32_t skipFlag);

// 0xaed420: every shape of every shape-ref node in the subtree (cIShapeRefNode, slot 93's range) gets its slot 34
// called with (a, b) (the lot imposter's terrain hookup: the terrain and "terrain").
void ForEachShapeBind(cICompositionTreeNode *node, void *a, const char *b);

// 0xae7d00: `box` (two corners) grown by the box, in `frameNode`'s frame (its world transform inverted, as a rotation
// frame), of every shape-ref node in the subtree (flag 4) that is not everywhere (slot 70) and has a shape whose slot
// 52 info is set (slot 97); a child with flag `skipFlag` (when not negative) is skipped with its subtree.
void GrowByShapeBounds(float *box, int32_t skipFlag, cICompositionTreeNode *frameNode, cICompositionTreeNode *node);

// ---- extension helpers -------------------------------------------------------------------------------------------

// 0xae1ed0: the node's extension of interface 0x1aff3bd4 (the animated face), uncounted, or null.
void *AnimatedFaceOf(cICompositionTreeNode *node);

// 0xae42c0: the animated face's extension 0x48ece762 (its slot 59, from the first), then that extension's answer for
// 0x6a6fd04d (slot 12 of its second virtual base); that answer, uncounted, or null when either step says false.
void *ChannelOwnerOf(void *animated);

// 0xaeb5b0: the node's next data-list extension after `after` (from the first when null) whose list name matches
// `name` (case blind), uncounted; null when none, or when `after` is the last extension.
cIDataListExtension *FindDataList(cICompositionTreeNode *node, const char *name, cIDataListExtension *after);

// 0xaeb3f0: FindDataList(node, name, null) when `reuse` and one is there; otherwise a new cDataListExtension (class
// 0x6a836d56 from the framework) added to the node and named `name`. Uncounted.
cIDataListExtension *GetOrAddDataList(cICompositionTreeNode *node, const char *name, bool reuse);

// ---- tree walks and lookups (lane 261111f, src/sg/SGUtilitiesTree.cpp) -----------------------------------------------

// 0xae6870: 0 for a null or empty name, else StringKeyParser::Crc32(name, lower = 1).
uint32_t NameCrc(const char *name);

// 0xae4470: the object's slot 77 (no arguments; receiver class not settled) answers something other than 0x2fa18737.
bool Slot77IsNotDefault(void *object);

// 0xaeb6a0: every data list named `name` on the node (FindDataList from the first, each next found before the
// current one is removed) is removed with slot 23, passed its cIExtension base.
void RemoveDataLists(cICompositionTreeNode *node, const char *name);

// 0xaeb520: the first data list named `name` on the node, else on its subtree (const walk, slots 41 / 37, depth first;
// a child with flag `skipFlag` (when not negative) is skipped with its subtree). Uncounted; null when none.
cIDataListExtension *FindDataListInTree(cICompositionTreeNode *node, const char *name, int32_t skipFlag);

// 0xaeb070: every node in the subtree with flag 5 (renderable) gets slot 81 (SetLayerTag) `tag`; node before children.
void SetLayerTagInTree(cICompositionTreeNode *node, uint32_t tag);

// 0xaeb190: every flag-5 node gets AddLayerBits(add) (slot 78), then RemoveLayerBits(remove) (slot 80); node first.
void SetLayerBitsInTree(cICompositionTreeNode *node, uint32_t add, uint32_t remove);

// 0xaeb210: with flags = node->GetFlags(): nothing (not even the children) when (maskB & flags) && !(maskD & flags);
// else, when !(maskA & flags) && (maskC & flags) and flag 1 is set, slot 66 (SetBoundFlag) gets the raw word `value`;
// then each child likewise.
void SetBoundFlagInTree(cICompositionTreeNode *node, uint32_t value, uint32_t maskA, uint32_t maskB, uint32_t maskC,
                        uint32_t maskD);

// 0xae7e60: every shape (slot 93's range, on nodes with flag 4) of every node in the subtree gets slot 45 with 0.
void ResetShapesInTree(cICompositionTreeNode *node);

// 0xaeb8a0: the node's shape-ref face (QueryInterface 0xa516cd10, decided by its answer), else the first direct child's
// (children by slot 42 / 38; the face held from the previous child is Released before the next ask). Released again
// before it is returned, so uncounted; null for none or a null node.
cIGZUnknown *ShapeRefOfNodeOrChild(cICompositionTreeNode *node);

// 0xaeb9a0: the first direct child whose QueryInterface 0xe51c10f5 writes a face (decided by the written pointer, not by
// the answer), uncounted; each earlier child's face is Released first. Null for none or a null node.
cIGZUnknown *TransformOfChild(cICompositionTreeNode *node);

// 0xaebb00: the first direct child writing a shape-ref face (as above, QueryInterface 0xa516cd10), else the answer of
// this function on each child in turn (that answer is AddRef'd into the held cell, the previous one Released, then the
// cell Released and returned). Uncounted; null for none or a null node.
cIGZUnknown *ShapeRefInSubtree(cICompositionTreeNode *node);

// 0xaeba70: the node's own shape-ref face when QueryInterface 0xa516cd10 writes one, else ShapeRefInSubtree(node);
// the cell is Released after, so uncounted. Null for a null node.
cIGZUnknown *ShapeRefOfNodeOrSubtree(cICompositionTreeNode *node);

// 0xaebc70: the node's slot 50 object (null: false) is AddRef'd, given slot 39 (argument), Released; true.
bool PassToSlot50Object(cICompositionTreeNode *node, uint32_t argument);

// 0xaed340: nothing for a null builder or part; else the builder's slot 42 range of counted iterators {cur, mid, end}:
// while !cur->slot4(end), part->slot34(name, cur->slot5()) then cur->slot6(); then end, mid and cur are Released.
void BindNamedBuilderItems(const char *name, cIGZUnknown *builder, cIGZUnknown *part);

// 0xaea980: appends the node (null: nothing), then each child's subtree in order (slot 42 range, slot 38 next), to `out`.
// The vector is the caller's (three pointers, the game's layout); the growth is our std::vector's, on the process heap
// the game's own growth uses too.
void CollectNodes(cICompositionTreeNode *node, std::vector<void *> &out);

// 0xaea9f0: for the node (null: nothing): QueryInterface(shape-ref) into a cell, whose pointer (not the answer) decides
// whether each shape of its slot 93 range is appended to `out`; then each child's subtree in order; the cell's pointer is
// Released last.
void CollectShapes(cICompositionTreeNode *node, std::vector<void *> &out);

// 0xae6900: a name of two or more characters that starts with "l_" appends "r_" to the builder (its slot 4) and then the rest
// of the name (slot 20, text and length); "r_" appends "l_" likewise; anything else appends the name whole (slot 4).
void AppendSideSwappedName(const char *name, cIGZUnknown *builder);

// 0xaea840: the owner's slot 51 range of counted iterators {cur, mid, end} for `key`; the index of the first item whose
// second string equals `second` and, asked again, whose first string equals `first` (both case-insensitive), else -1;
// the iterators are Released (end, mid, cur) either way.
int32_t IndexOfNamedPair(cIGZUnknown *owner, uint32_t key, const char *first, const char *second);

// 0xaeab90: a table held in `*cell` (created by the framework's class 0x9d054f52 when null, then given setupArg by slot
// 16); when its slot 19 mode is not `mode` it is cleared (18), given the mode (17) and classArg (3). Each of its slot 20
// entries (slot 21) with an IndexOfNamedPair(entry, mode, first, second) hit gets slot 7 (mode, index, value). True when
// any entry did.
bool ApplyNamedPairs(uint32_t classArg, uint32_t setupArg, uint32_t mode, const char *first, const char *second,
                     uint32_t value, cIGZUnknown **cell);

// ---- choreography (lane 261111c, src/sg/SGUtilitiesChoreo.cpp) ----------------------------------------------------
// The animated face (0x1aff3bd4) is an nGZSceneDBAnim::cChoreoQueue's cIChoreoQueue face: its own vtable at +0 (slots
// 0 (1 argument), 17 AddHandler, 40 (1), 54 the anim manager, 59 FindHandler (iid, out, start)) and a vbptr at +4
// whose second entry leads to its cIExtension. Its handlers (cChoreoHandlerAnim 0xa9de6c97, cChoreoHandlerEvent
// 0xc8ebf854) are faces with only virtual bases: a vbptr at +0, the cIExtension in its second entry and the handler
// interface the queue takes in its third.

// 0xae2dc0: a new choreography queue (framework class 0xdaff3bda) added to the node as an extension; the queue,
// uncounted (null when creation fails: the node is then handed null).
void *CreateChoreoQueue(cICompositionTreeNode *node);

// 0xae39c0: the node's queue set up (the cTSSGSystem loads): a camera node (0x5ca76db6) tagged "cinecam"; the queue
// (made with CreateChoreoQueue and given to `animManager`'s slot 6 when there is none, else its slot 0 (1)), slot 40
// (1), and an animation and an event handler made and added when it has none. The queue, uncounted.
void *AttachChoreoQueue(cICompositionTreeNode *node, void *animManager);

// 0xae3c00: the node's queue, if any, taken down: slot 0 (1), removed from its owner node, and from its anim manager
// (slot 54) when that one has it (slot 15 >= 0, then slot 7).
void DetachChoreoQueue(cICompositionTreeNode *node);

// 0xae4ef0 (cTSSGSystem::PlayCinematic): a new blend (framework class 0x8b969d64) into *out (cleared first), set up by
// its slot 2 (1, mode, a9, a10, a11, blendIn, blendOut, length, then five zeros). length: 2147483392 when loops is -1;
// else the animation's duration (slot 36) times the span (end - start) and loops - 1 more (at least 0), or, for a
// zero duration, loops (less one when start > end - 1e-6), divided by speed, made positive. The blend times (their sum
// for mode 0, half of it for mode 1, else 0) are scaled to 0.9999 of the length when they pass it by 1e-6; each under 1
// becomes 0. Returns false only when they passed it by more than 1e-5.
bool MakeAnimationBlend(void **out, void *animation, float speed, int32_t loops, float start, float end, int32_t mode,
                        float blendIn, float blendOut, float a9, float a10, float a11);

// 0xae1f50 (cTSSGSystem::PlayCinematic): a new animation instance (framework class 0xc9de69b6) for `animation`, counted
// into *out: placed in `scene` (its slot 0 (scene, placement)) when given, its controller face (vbptr at +4, entry 2)
// given each argument (slots 9-20, 24, 25; slot 15 gets the blend's slot 19 for now - start instead of a11 when there
// is a blend and now is not 0x80000100), its own slot 2 a20, slot 26 the rate (duration (slot 36) times a7, or 1 - a7
// times it when a15 is negative), slot 21 (a17, a18) when a17 is set and the animation's slot 24 object has kind
// 0xe6e97a71 or 0x7af75043 (group 0xfb5b3383); then attached to `target` (its slot 10 (controller)). Returns true.
bool AttachAnimation(void **out, void *target, void *scene, const void *placement, void *animation, int32_t now,
                     int32_t start, float a7, float a8, uint32_t a9, uint32_t a10, float a11, void *blend, uint32_t a13,
                     uint32_t a14, float a15, float a16, uint32_t a17, uint32_t a18, void *a19, uint32_t a20,
                     uint32_t a21);

// ---- light bound meshes (SGUtilitiesMesh.cpp) --------------------------------------------------------------------

// 0xaef950 (the spot light's bound, from 0xaeeea0): a new geometry node (class 0x7ba3838c) into *out (cleared first)
// holding an indexed mesh, a bounding volume and a compactor builder (classes 0x9bffc10d, 0x1cfeceb8, 0xdcdda078, added
// in that order). The mesh, group "subset0": a cone with its apex at the origin opening down -Z to `length`, base
// radius `radius`, as `segments` spokes of `segments` points each (point i at i / (segments - 1) of the way out, spoke
// k at angle k * 2pi / segments, normal (cos, sin, radius / length)); with `cap`, a face of the spokes' last points
// (from the last spoke down) with one (0, 0, -1) normal; then a quad (kinds 3) between each pair of neighbouring points
// of each spoke and the next (the last spoke's next is the first). Returns out.
cIGeometryNode **MakeConeMesh(cIGeometryNode **out, float radius, float length, bool cap, uint32_t segments);

// 0xaf05b0 (the point light's bound, from 0xaeeea0): a new geometry node (class 0x7ba3838c) into *out (cleared first)
// holding an indexed mesh builder (class 0x9bffc10d) and, with `tangents`, a tangent space builder (class 0x5d054225)
// given `name`. The mesh, group `name`: a sphere of `radius` from the +Z pole down to the -Z pole as segments / 2 + 1
// rings of segments + 1 points (position, unit normal, texture coordinate (i / segments, 2j / segments)), then a face
// (kinds 7) for each ring's point and the next (triangles at the first and last rows, quads between). Returns out.
cIGeometryNode **MakeSphereMesh(cIGeometryNode **out, float radius, uint32_t segments, const char *name, bool tangents);

// 0xaeeea0 (cdecl (scene, flags); the lighting manager's rebuilds): the light bounds of the scene rebuilt. Every
// shape-ref node below `scene` (slot 44's walk, filter 0xa516cd10) tagged exactly "@@BoundingLights@@" is removed from
// its parent. Then, when flags has 4 (point lights) or 8 (spot lights), each light-ref node below it (filter 0x053d200a)
// gets a new shape-ref node (class 0x65245517) tagged "@@BoundingLights@@" and a new shape (0xfc6eb1f7); for a node
// with a light whose range (attribute 0x0a5622e9) is not <= 0 (NaN too): a point light (type 2, flag 4) a 32-segment
// sphere of the range, material "VisBound_PointLight"; a spot light (type 3, flag 8) a 32-segment cone of the range,
// radius tan((attributes 0x29de5328 + 0x49de5331) / 2) * range, "VisBound_SpotLight"; the shape added to the shape-ref
// node, which becomes the light node's child when it holds a shape. A light with a range <= 0 gets none.
void RebuildLightBounds(cICompositionTreeNode *scene, uint32_t flags);

// 0xaf16f0 (the shadow's quad, cShadow 0x3c9ed0's paths): a new geometry node (class 0x7ba3838c) into *out (cleared
// first) holding an indexed mesh builder (class 0x9bffc10d). The mesh, group "layer": a quad in the Z = center[2] + 0
// plane centred on (center[0], center[1]), size[0] wide and size[1] high, its corners from (-, -) counter-clockwise, with
// texture coordinates (uvLow[0], uvHigh[1]), uvHigh, (uvHigh[0], uvLow[1]), uvLow, one (0, 0, 1) normal and one face
// (kinds 5) of the four corners. Returns out.
cIGeometryNode **MakeQuadMesh(cIGeometryNode **out, const float *center, const float *size, const float *uvLow,
                              const float *uvHigh);

// 0xaf1420 (the text billboard's quad, 0xaf3860; lane 261111e): the geometry node and mesh builder as MakeQuadMesh, group
// "layer": the quad in the XZ plane at y = `y`, half `halfSize` each way (corners (-h, y, -h), (h, y, -h), (h, y, h),
// (-h, y, h), the negation a sign flip), texture coordinates u0 = scale * uScale, v0 = (vOffset + 1) * scale,
// u1 = (uScale + 1) * scale, v1 = scale * vOffset, in the order (u0, v0), (u1, v0), (u1, v1), (u0, v1); no normal; one
// face (kinds 5) of the four corners. Returns out.
cIGeometryNode **MakeBillboardQuadMesh(cIGeometryNode **out, float halfSize, float y, float scale, float uScale,
                                       float vOffset);

// 0xaf1ad0 (the deck foundation thumbnail's quad): MakeQuadMesh's corners and texture coordinates in the XZ plane at
// y = center[1] + 0 (x spread by half size[0], z by half size[1]), the one normal (0, -1, 0); one face (kinds 5) of
// the four corners and normal 0.
cIGeometryNode **MakeQuadMeshXZ(cIGeometryNode **out, const float *center, const float *size, const float *uvLow,
                                const float *uvHigh);

// 0xaf1eb0 (the family thumbnail's and the screen effect's quad): the corners of MakeQuadMesh (z = center[2] + 0), no
// texture coordinates; when `normal` (a byte) is set the one normal (0, 0, 1) (the game's global 0x01073e98) and its
// index after the corners'. Group "layer", one face of the four corner indices (AddPositionIndex).
cIGeometryNode **MakePlainQuadMesh(cIGeometryNode **out, const float *center, const float *size, bool normal);

// 0xaf09e0 (cTSCommonPoolWater's water surface, lane 261111n): a geometry node (class 0x7ba3838c) into *out (cleared
// first) holding four parts, added in this order: an indexed mesh builder (0x9bffc10d), a compactor (0xdcdda078), a
// bounding volume builder (0x1cfeceb8) and the old-style part 0x5ce7e026 (iid 0x7ce7e022, held through its virtual base
// alone); only the mesh is used (unchecked), the other three may be null. The mesh is a `columns` x `rows` grid of
// vertices (row-major) in the XY plane: vertex (c, r) at (c / cd - offX, r / rd - offY, 0) with cd, rd = columns - 1,
// rows - 1 when `normalised`, else 1; offX, offY = 0.5 (normalised) or 0.5 * (count - 1) when `centred`, else 0. Each
// vertex gets the normal (0, 0, 1) and texture coordinates (c / (columns - 1), 1 - r / (rows - 1)). `morphs` morph
// targets ("rectMesh", "bump_<m>") are bumps on a side x side lattice (side the rounded-up square root of `morphs`):
// each vertex's morph position is its own raised by k = 0.5 / (1 + dx^2 + dz^2), and when `morphNormals` is set its
// normal is the bump's slope, normalised. The faces (group "subset0") over the (columns - 1) x (rows - 1) cells: a quad
// (mode 0), two triangles split one way (1) or the other (else; mode 3 alternates by (row ^ col) & 1), index kinds
// 5 | (morphNormals << 1) (the flag byte as it is), and normal index 0 per face when there are no morph normals. The
// flags are bytes. Returns out.
cIGeometryNode **MakePoolWaterRectMesh(cIGeometryNode **out, int32_t columns, int32_t rows, int32_t morphs,
                                       uint8_t morphNormals, uint8_t normalised, uint8_t centred, int32_t mode);

// 0xaef5b0 (cTSModelEffect's box, lane 261111d): a new geometry node (class 0x7ba3838c) into *out (cleared first)
// holding an indexed mesh builder (class 0x9bffc10d). The mesh, group "subset0": the axis-aligned box from `low` to
// `high` (3 floats each): its 8 corners (corner k's x from high when bit 0 is set, y bit 1, z bit 2; bit copies), the
// normals +X, -X, +Y, -Y, +Z, -Z (the negatives with -0 elements), and per face i its normal i and 4 corners (MarkUnknown,
// AddNormalIndex, AddPositionIndices, CommitGroup). Returns out.
cIGeometryNode **MakeBoxMesh(cIGeometryNode **out, const float *low, const float *high);

// 0xae6990 (lane 261111k; MakeLocomotionRequest's path display): a new geometry node (class 0x7ba3838c) holding an
// indexed mesh builder (class 0x9bffc10d), stored into *out with one reference (out is not cleared first). `path` is a
// locomotion path (class unsettled: slot 16 knot count, 10 knot parameter, 18 point at t, 20 heading at t). For each
// knot k but the last, group "part<k>": the span to the next knot cut into trunc(span / 0.1 + 1) steps (none when
// that is not positive), each a quad 0.02 either side of the heading (cos, sin) and 0.04 up from the two ends' points:
// a + side, a - side, b - side, b + side, its indices CountPositions() + 3, 2, 1, 0.
void MakePathRibbonMesh(void *path, cIGeometryNode **out);

// 0xae2280 (lane 261111k; cLocomotable vf52): a new locomotion request (class 0x9c1d008c, iid 0xdc1d0085), stored into
// *out with one reference; always true. Its own face and its settings (virtual base 2) get the arguments (slots in
// SGUtilitiesLoco.cpp); `blend` is path->slot22() * a11, or (1 - a11) * that when a24 < 0. a8 and a20 (when set and
// a9 is not 0x80000100) replace a7 (x87: their slot 19 at a9 - a10, times 1000) and a19. With a node, the node gets the
// settings (slot 10); with `display` (low byte) and the node's owner a cITransformNode with a parent, a shape-ref node
// under that parent shows the paths (path, refPath1, refPath2: a ribbon each, its parts bound to
// "Anim_ChoreoLocoRequestPath" / "...RefPath1" / "...RefPath2") and the first path's knots as 0.08 boxes
// ("Anim_ChoreoLocoRequestWayPoint") turned to the heading; the request then gets that node (slot 16).
bool MakeLocomotionRequest(void **out, void *node, float a3, void *path, float a5, float a6, float a7, void *a8,
                           int32_t a9, int32_t a10, float a11, float a12, float a13, float a14, float a15, float a16,
                           uint32_t a17, uint32_t a18, float a19, void *a20, const float *a21, uint32_t a22, uint32_t a23,
                           float a24, void *a25, uint32_t a26, uint32_t a27, uint32_t a28, uint32_t display,
                           uint32_t a30, void *refPath1, void *refPath2);

// 0xae3050 (lane 261111k; src/sg/SGUtilitiesAnimDump.cpp): the debug dump of an animatable's channels. The node it
// animates (virtual base 1, slot 18) gives its tag and its animation owner's clip; then for each channel (slot 58 count,
// slot 9 channel) a line "*** %d [%d] ", "look_at %s" or "anim %s", "loco %4.2f %4.2f" and the channel's numbers. The
// sink is compiled out: the lines are built and dropped, so only the getters and the Sprintf text are observable.
void DumpAnimationChannels(void *animatable);

// 0xae5840 (lane 261111k; src/sg/SGUtilitiesDynamics.cpp; callers 0x28e060, 0x28f890, 0x627e26): a skeleton's
// dynamics. *out = a new dynamics object (class 0xfb00791d) holding a new behaviour set (class 0xa9de45ba); the
// skeleton's kind is the first name of the static table at 0x1071460 it answers to (slot 52 with NameCrc), its scale
// the skeleton's slot 67 times the kind's. With a non-zero scale, every tagged transform node under the skeleton's owner
// with a "tsDynamicsBehavior.s" data list of skin type 10 gets a behaviour (class 0x8c1eb8dc) with its rest pose, joint
// limits and a spring (0x1b115b24) or damper (0x5b0aff38). `enable` and `freeze` are bytes; the original passes
// `freeze`'s whole word on to the behaviour's slot 9. Returns the behaviour set's slot 31 (false without a scale or tree).
bool MakeDynamicsBehaviors(void *skeleton, uint32_t enable, uint32_t freeze, void **out);

// ---- transforms --------------------------------------------------------------------------------------------------

// 0xaeb2c0: `bone`'s world transform relative to `node`'s (the bone's composed with the inverse of the node's; the
// node's is asked first), 7 floats into `out`. Returns out.
const float *RelativeTransform(float *out, cICompositionTreeNode *bone, cICompositionTreeNode *node);

// 0xaeee30: the node's rotation set (slot 65) to the shortest one taking -Z to `direction`.
void AimNode(cITransformNode *node, const float *direction);

// ---- look-at and IK (lane 261111i, src/sg/SGUtilitiesIK.cpp) -----------------------------------------------------
// All cdecl. Quaternions are {x, y, z, w}; every input is read before the first store, so `out` may alias them.

// 0xc63600: the inverse of q: the conjugate divided by the squared norm ((x*x + y*y) + z*z) + w*w (the negated parts
// times 1 / norm). Returns out.
float *QuatInverse(float *out, const float *q);

// 0xc63690: the Hamilton product a * b. Returns out.
float *QuatMultiply(float *out, const float *a, const float *b);

// 0xc63380 (lane 261111k; MakeDynamicsBehaviors' frames): v rotated by the unit quaternion q, q v q* expanded:
// out[0] = (((w*w + x*x) - y*y) - z*z) vx + (y*x - w*z) 2vy + (w*y + z*x) 2vz, and so on (SSE, the original's operand
// order). All of v and q are read before the first store. Returns out.
float *QuatRotate(float *out, const float *v, const float *q);

// 0xc63510 (lane 261111k; MakeLocomotionRequest's waypoint markers): the rotation by `angle` about the axis axis[0],
// axis[stride], axis[2 * stride] (normalised: 1.0 / the double root of the float sum of squares, in double, rounded,
// times the sine): {axis * sin(angle / 2), cos(angle / 2)}, the sine and cosine from the x87's fsincos. Returns out.
float *QuatFromAxisAngle(float *out, const float *axis, int32_t stride, float angle);

// 0xc63b20 (lane 261111j): the rotation matrix of q into m, rows `stride` floats apart: m[0] = (w*w + x*x) - y*y - z*z,
// m[stride] = 2 (w*z + y*x), m[1] = 2 (y*x - w*z) and so on. All of q is read before the first store.
void QuatToMatrix3(float *m, int32_t stride, const float *q);

// 0xc63c50 (lane 261111j): Euler angles of the 3x3 matrix m (rows 3 floats apart) in the packed `order`: byte 0 the
// first axis i, byte 1 the second j, bits 16-17 the third k (equal to i's low bits: a repeated axis, then k is bits
// 20-21), byte 3 a signed factor every angle is multiplied by. out[3] = order. With the axis-pair length (double root of
// the float sum) at most 6.08e-5 (a NaN length takes the other branch) the gimbal branch: out[0] = atan2(-m[j][k], m[j][j]), out[2] = 0. The
// angles through the CRT's atan2, each rounded to float. Reads m after each store into out (they may alias). Returns
// out.
float *Matrix3ToEuler(float *out, const float *m, uint32_t order);

// 0xc63af0 (lane 261111j): QuatToMatrix3 (stride 3) into a local, then Matrix3ToEuler. Returns out.
float *QuatToEuler(float *out, const float *q, uint32_t order);

// 0xc63790 (lane 261111l; the look-at and IK strategies' blends): the spherical blend from a to b at t. The dot
// ((b0*a0 + b1*a1) + b2*a2) + b3*a3 and its sign (1 when its bits as an int are >= 0, else -1). When 0.95 > |dot| (a NaN
// takes the other branch): c = b - a * dot, angle = atan2(root(1 - dot * dot), |dot|) in double (the CRT's), the x87's
// fsincos of float(angle * t), out = sign * cos * a + sin / |c| * c (|c|: 1.0 / the double root of the float sum
// ((c1² + c0²) + c2²) + c3², rounded). Otherwise the plain blend n = a * ((1 - t) * sign) + b * t, out = n / |n| (the
// same way). Every input is read before the first store. Returns out.
float *QuatSlerp(float *out, float t, const float *a, const float *b);

// 0xc64040 (lane 261111l): the rotation by `angle` about X into m, rows `stride` floats apart: rows (1, 0, 0),
// (0, c, -s), (0, s, c) stored by column (m[0], m[stride], m[2 stride], m[1], ...); s and c from the x87's fsincos.
void Matrix3RotationX(float *m, int32_t stride, float angle);

// 0xc640c0 (lane 261111l): the rotation about Y likewise: rows (c, 0, s), (0, 1, 0), (-s, 0, c).
void Matrix3RotationY(float *m, int32_t stride, float angle);

// 0xc64140 (lane 261111l): out = b * a for row-major 3x3 matrices (out[3r + c] = sum over k of b[3r + k] * a[3k + c],
// each sum ((p0 + 0) + p1) + p2 from a +0 start, the products in the original's operand order). Every input is read
// before the first store (out[8] first, down to out[0]), so out may alias a or b.
void Matrix3Multiply(float *out, const float *a, const float *b);

// 0xc643a0 (lane 261111l; Matrix3ToEuler's threshold, entry 8): entry `bits` (0..23) of the .data table at 0x10859e8,
// (2^bits - 1) * 2^-22, returned on the x87. The original reads past the table for other indices; ours has only these.
float ToleranceForBits(int32_t bits);

// 0xae43c0: `in` (three angles: pitch, yaw, roll) copied to `out`, each then held to its limit: pitch to
// [-0.8901179, 0.6283185], yaw to +-1.2217305, roll to +-0.6981317 (51, 70 and 40 degrees). True when any was held
// (roll answers at once); a NaN is never held.
bool ClampLookAngles(const float *in, float *out);

// 0xae4780: the pitch and yaw that look along `d` (z forward and y up when `yUp`: atan2(-y, |xz|), atan2(x, z); else
// y forward and z up: atan2(-z, |xy|), atan2(-x, y)); roll 0. The length in double from the float sum, rounded to
// float; atan2 in double, each angle rounded to float.
void LookAngles(bool yUp, const float *d, float *out);

// 0xae4490 (nTSSG::cAnimatable's look-at test): whether `point` (in `frame`'s space) can be looked at by `subject`'s
// head: the subject's slot 49 node for 0xd08f50db, its parent and grandparent as transform nodes, the neck frame (the
// grandparent's world transform at the parent's position, turned by the subject's slot 76 rotation) inverted; the
// point's direction in it within ClampLookAngles' limits. False when slot 73 says no or a node is missing.
bool CanLookAt(void *subject, cICompositionTreeNode *frame, const float *point);

// 0xae75c0: FindNodeByName over the const walk (slots 41 / 37).
cITransformNode *FindNodeByNameConst(const char *name, cICompositionTreeNode *node, int32_t skipFlag);

// 0xae76f0: the first transform node, depth first, whose tag's CRC (the string pool's slot 11 of its tag handle) is
// `crc`, uncounted; a child with flag `skipFlag` (when not negative) is not searched (non-const walk, slots 42 / 38).
cITransformNode *FindNodeByTagCrc(uint32_t crc, cICompositionTreeNode *node, int32_t skipFlag);

// 0xae37c0: an IK target's node and bone data. `target`'s slot 15 kind: 2 the rig's slot 5 (0) node (false when its
// slot 7 says none), 1 `self`. With no node, true. The target's name id (slot 23 when the rig's second virtual base's
// slot 46 says so, else 22): 0 the node itself; 0x6e80a11a the node itself, id stored. Otherwise the id is asked again
// and stored, and the node replaced by the node's animated face's slot 51 answer for (the first id, bone), or (no face)
// by FindNodeByTagCrc(id, node, the second virtual base's slot 28 object's slot 66 flag). Otherwise the node's bone data
// extension (0xe9075bbe) goes to *bone (slot 24). True when a node is left in *node.
bool FindIKTargetNode(cICompositionTreeNode *self, void *rig, void *target, uint32_t *id, cICompositionTreeNode **node,
                      void **bone);

// 0xae3fb0 (cAnimatable's IK check): whether every IK target of `chains` can be found. `chains` slot 51 count, slot
// 20(i) a chain; its slot 36 count, slot 20(j) a link; the link's slot 43 (`alt`) or 42 id must be a node of `subject`
// (its slot 49), else false. Then each of the link's targets (slot 47 count, slot 23(k)): kind (slot 15) 2 looks in
// `other` (false with *otherMissing set when there is none) through its animated face's slot 49, or (no face) by name
// (slot 17 / 16) with FindNodeByNameConst under `other` (skip flag: the subject's slot 66); kind 1 looks in the
// subject's own slot 49 when the subject has a model node (its first virtual base's slot 18, asked for
// cICompositionTreeNode); other kinds are passed over. The id is slot 23 / 22: 0 passed over, 0x6e80a11a ends the
// check with true at once. A miss: false, *otherMissing = (kind 2). All found: true; *otherMissing is written only on
// false.
bool CanFindIKTargets(void *subject, cICompositionTreeNode *other, void *chains, bool alt, bool *otherMissing);

// 0xae2e70 (the choreography debug display): `node`'s animated face (FindExtension 0x1aff3bd4, counted) and, when it
// has one, `display` cleared for the node (0xaf42a0) and given the node's tag, then the name of every queue entry's
// (slot 58 count, slot 9(i), counted) animation (asked of entries whose slot 56 says yes and slot 60 no: interface
// 0xc9de69ad, its second virtual base's slot 34 object, that object's first virtual base's slot 17 name), non-empty
// names only (AddNodeTextLabel, colour 0xffffff00 each). `display` is the text output's name (ClearNodeTextLabels).
void ShowPlayingAnimations(cICompositionTreeNode *node, void *display);

// 0xae6290 (lane 261111j): IK targets for `animatable`'s named nodes. A new set (framework class 0xfb00791d) into *out
// and a target list (0xa9de45ba, its slot 9 (1)) given to it (slot 18), then its slots 3 (0) and 14 (2). When the
// subject's first virtual base's slot 18 holder answers the model (interface 0x4512dd13): two space objects (classes
// 0x5b0aff38 and 0x1b115b24, vbptr at +0), the root (the subject's slot 74), then each node of the subject's walk (slot
// 45 / 46 (cursor out)) whose tag is non-empty and ends in none of _grip, target_surface, _ikpole, _hair, _ikctr,
// control0, control1, control0rot, control1rot (case blind) gets a new target (0x5d35d710): slot 7 (the root space's face,
// 0xfb5b3383) and the local translation for the root; else (the bone space's face, 0x9b5b3506) and the local rotation,
// relative to the cursor's slot 9 quaternion when the walk gives one, as QuatToEuler (order 0x1020100) degrees; then
// slot 9 (tag), 29 (1, axis) for each axis, 30 (0, value, 0, 0, axis, 1) for each, 39 (1), 12 (), added to the list
// (slot 10). Returns whether the list's slot 31 count is non-zero; false when the model is missing.
bool MakeIKTargetsFromNames(void *animatable, void **out);

// ---- controller blends (lane 261111j, src/sg/SGUtilitiesBlend.cpp) ------------------------------------------------
// The blend objects are framework class 0x8b969d64 (iid 0x8b969d59; their own slot 2 is the 13-word set-up, see
// MakeAnimationBlend). The controllers (classes not settled) are named by the slots the bodies ask.

// 0xae50e0: how a blend's fade overlaps: 0 when its slot 26 (0) object is null or its slot 10 time is not under
// -1e-6; else 2 when 1e-6 minus that object's slot 7 value (x87) is over the time, 1 otherwise.
int32_t BlendOverlapMode(void *blend);

// 0xae4d10: a new blend into *out (cleared first) for a source (null: from the range). length: 2147483392 unless loops
// is not -1 and rate is over 0: then the source's slot 14 (x87) or max(0, (end - start) * rate + (loops - 1) * rate)
// over a4 * a5 * 0.001, made non-negative. Mode 0: blends over length + 1e-6 are scaled by length / total * 0.9999;
// mode 1: half the total over length + 1e-6 makes mode 2. Slot 2 (1, mode, a12, a13, a14, blendIn, blendOut, length,
// five zeros). Returns false when the blends were over length + 1e-5.
bool MakeBlendForSource(void **out, float rate, void *source, float a4, float a5, int32_t loops, float start,
                        float end, int32_t mode, float blendIn, float blendOut, float a12, float a13, float a14);

// 0xae4900: a transition blend into *out (cleared first): a9, a10, a11, a15, a16 in milliseconds; four phase lengths
// (a12's low byte picks the formulas) scaled together by the square root of length / total (a10, a15, a16 also by
// 0.98) when they overrun length + 1e-6; then slot 2 (2, 0, a9, a10, a11, a7 / |a6|, a8 / |a6|, the hold length,
// a12, a13 / |a6|, a14 / |a6|, a15, a16). Returns false when the total was over length + 1e-5.
bool MakeTransitionBlend(void **out, float rate, int32_t loops, float start, float end, float a6, float a7, float a8,
                         float a9, float a10, float a11, uint32_t a12, float a13, float a14, float a15, float a16);

// 0xae5140: starts `controller`'s blend toward `mode` (0 full, 1 half): false when its slot 60 says busy, or (after
// slot 7 (a1, slot 30, 1)) when slot 32 says so. Otherwise an instant switch (slots 13 (2), 15 (0.001), 16 (null), 7
// (a1, 3, 1)) when a5's low byte is 0, a6 is not over 0 and slot 56 is false or slot 44 not over 0.001; else the
// controller's blend (slot 45, counted) or a new one, set up (1, 2, ...) from the elapsed ticks and a4, a6, given to
// slot 16 and announced through slot 7. Returns true unless refused.
bool StartControllerBlend(uint32_t a1, void *controller, int32_t mode, float a4, uint32_t a5, float a6);

// 0xae5470: re-times `controller`'s blends for the rate `a2`: false when its slot 39 tick is 0x80000100. The queued
// blend (interface 0xdc1d0085's slot 25) and the controller's own (slot 45) are set up again for the elapsed time
// (BlendOverlapMode picks the own blend's mode); then slot 19 (a2). Returns true.
bool RetimeControllerBlends(void *controller, float a2);

// 0xae2b50: a new controller instance (framework class 0x2a6f2e97) set up from the arguments (a5: 3 floats, the
// position of an otherwise identity frame), handed to a2's slot 10 when a2 is given, into *out (counted). Returns true.
bool MakeControllerInstance(void **out, void *a2, uint32_t a3, uint32_t a4, const float *a5, float a6, float a7,
                            uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13,
                            uint32_t a14, uint32_t a15);

// 0xae3cd0: scales the skeleton `skeleton` (an extension face, vbptr at +4; class not settled) to `scale`: every node
// a traversal (class 0x5ca10ca7, filter: the skeleton's slot 66) finds in its owner's model (the owner's interface
// 0x4512dd13; the model itself skipped when skipModel's low byte is set) has its local translation scaled by scale /
// the skeleton's slot 67 scale, or along the axis (and with the weight) its 0xe9075bbe extension gives (axis 3: left
// alone, 4: all three). Then the skeleton's slot 31 (scale). False when the owner, the model or every node is missing.
bool ScaleSkeleton(void *skeleton, float scale, uint32_t skipModel);

// 0xaf22d0: the box's corners as projected by `matrix` (16 floats; orthographic when elements 14 and 13 are both 0)
// through BoxProjection::Project, into lo and hi (3 floats each).
void ProjectBox(float *lo, float *hi, const void *box, const float *matrix);

// ProjectBox's scratch (0x2c bytes on its stack; lane 261111d, src/sg/SGUtilitiesProject.cpp).
struct BoxProjection {
    uint8_t mOrthographic; // set by the caller
    uint8_t mPad[3];
    float mLo[3];   // +4
    float mHi[3];   // +0x10
    float mUnused[2]; // +0x1c, never touched
    float mDepthMin; // +0x24
    float mDepthMax; // +0x28

    // 0xaf25a0 (thiscall (box, matrix), ret 8; `box` a nGZCompGeom::BoundBox, `matrix` 16 floats, row-major rows of
    // 4). All SSE single precision, each operation in the original's operand order; min / max are minss / maxss
    // (p < m ? p : m, p > m ? p : m: a NaN on either side gives the running value). lo / hi start at 1e6 / -1e6, depth
    // min / max at 1e6 / -1e6. Each corner i (BoundBox::Corner order) as (x, y, z, 1) gets w = Dot4(corner, row) (row
    // 2 when orthographic, else 3) folded into the depth range.
    //  - orthographic: (Dot4 row 0, Dot4 row 1, w) of each corner folded into lo / hi.
    //  - perspective: fails unless depth max > 1e-5. Each corner's clip point (rows 0, 1, 2 and w) is in front when
    //    w > 1e-5 (NaN behind). All behind: fails. All in front: each corner's x, y, z times float(1.0 / double(w)) unless
    //    that is exactly 1, folded in. Otherwise, along each of the 12 box edges (a, b) (0x00e7d380): a in front gives a
    //    scaled by divss(1, w_a) (unless exactly 1) folded in; a and b on opposite sides give the crossing point
    //    (a + (b - a) t) * 1e5, t = (1e-5 - w_a) / (w_b - w_a) when |w_b - w_a| > 1e-5, else 0 (a in front) or 1, folded
    //    in.
    // Then, unless hi.x >= lo.x (NaN fails), fails; else lo clamped to at least -1e6 and hi to at most 1e6 (maxss /
    // minss with the constant first: a NaN stays), and true. Failing sets lo and hi to -1 (depth kept) and is false.
    bool Project(const void *box, const float *matrix);
};
static_assert(sizeof(BoxProjection) == 0x2c, "the projector's scratch");

// ---- geometry data container helpers (lane 261111g, src/sg/SGUtilitiesGeometry.cpp) -----------------------------
// The element and group index lists the builders make are GeometrySelection's members (sg/GeometrySelection.h).

class cIGeometryDataContainer;
struct cGeometryElementHeader;
template <int N> struct cGeometryVector;

// 0xaeda40: the header (copied to *out, which is returned) of the first element in linkage `linkage`'s element list
// (its active count read once from slot 40's answer; each element through LinkedElementHeader 0xaee030) with this
// identity, repetition and block format. None: out->mIndex is -1, the rest of *out the last element's (untouched when
// the list is empty).
cGeometryElementHeader *FindLinkedElement(cGeometryElementHeader *out, cIGeometryDataContainer *container, uint32_t linkage,
                                          uint32_t identity, uint32_t repetition, uint32_t blockFormat);
// 0xaedfa0: the same search, answering the element's position in the list, or -1.
uint32_t FindLinkedElementPosition(cIGeometryDataContainer *container, uint32_t linkage, uint32_t identity,
                                   uint32_t repetition, uint32_t blockFormat);

// 0xaee2c0 / 0xaee570: the values of the element at `position` of linkage `linkage` (LinkedElementHeader) as the
// linkage's alias list (slot 42, the element's set format) reaches them. A table from each vertex number to its
// place in the element's index list (slot 38; sized by slot 32's vertex count, -1 for none) is built first; both out
// vectors are emptied; then the values' range (slot 33 or 35) is taken and, for each alias k whose vertex has a place,
// k is appended to `aliases` and the value at that place (the range's cursor slot 9) to `values`. Every vertex and
// alias number must be below the vertex count (ORIGINAL-BUG: the table is not bounds-checked).
void ReadLinkedFloats(cIGeometryDataContainer *container, uint32_t linkage, uint32_t position,
                      std::vector<uint16_t> *aliases, std::vector<float> *values);
void ReadLinkedVectors3(cIGeometryDataContainer *container, uint32_t linkage, uint32_t position,
                        std::vector<uint16_t> *aliases, std::vector<cGeometryVector<3>> *values);

// ---- generated textures (lane 261111g, src/sg/SGUtilitiesTexture.cpp) -------------------------------------------------
// The generators cTextureCommand vf4 0xad6f30 runs. Each makes an image (COM class 0x1c4a276c), fills it, then a texture
// (class 0x2a956dad, a cube map 0x4a956dd2) that loads it (slot 9) and goes to *out, counted; the pointer *out held
// before is overwritten, not released. The image (and the noise) are released at the end.

// 0x18e0b0: the direction (not unit) through texel (x, y) of cube face `face` (0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z) of
// `size` texels: the face's axis +-1, the others (x - size / 2) / (size / 2) and (y - size / 2) / (size / 2). Another
// face: zero.
void __cdecl CubeDirection(float *out, int32_t face, int32_t x, int32_t y, int32_t size);
// 0xaebd00: pow(x, y) in double, answered on the x87 stack.
double __cdecl Pow(float x, int32_t y);
// 0xc631e0: the colour of `hue` degrees at saturation and value (the fifth argument unused), written to out and returned.
float *__cdecl HueColour(float *out, float hue, float saturation, float value, float unused);
// 0xaec1a0: a 32-level image (format 2), level i filled with the hue start + i * step (summed step by step).
void __cdecl MakeDensityTexture(int32_t width, int32_t height, float hue, float step, void **out);
// 0xaec310: the same image, level i filled with the hue 360 * i / levels (integer division).
void __cdecl MakeMipTestTexture(int32_t width, int32_t height, void **out);
// 0xaec4a0: a six-face cube map (format 1, one level), each face filled with its own colour.
void __cdecl MakeNormalCubeTexture(uint32_t size, void **out);
// 0xaec6b0: a one-level image (format 3) of 2D Perlin noise (COM class 0x9d332114, seeded with `seed`): the texel (x,
// height - 1 - y) holds Fractal2(x / scaleX, y / scaleY, 1, 0.5, 1) * 0.5 + 0.5.
void __cdecl MakeNoiseTexture(int32_t width, int32_t height, int32_t seed, float scaleX, float scaleY, void **out);
// 0xaec8e0 / 0xaecba0: a one-level image (format 2, 3 bytes a texel, z y x) ramping from `from` to `to` across the
// columns (the first row copied down) or down the rows (each row's first texel repeated).
void __cdecl MakeRampHTexture(int32_t width, int32_t height, const float *from, const float *to, void **out);
void __cdecl MakeRampVTexture(int32_t width, int32_t height, const float *from, const float *to, void **out);
// 0xaebd70: a six-face cube map (format 3, one byte a texel): each texel range[0] + (range[1] - range[0]) *
// |pow(dot(colour, unit direction), power)|, clamped to 0..255 and truncated (NaN gives 0).
void __cdecl MakeFalloffCubeTexture(uint32_t size, const float *range, const float *colour, int32_t power, void **out);
// 0xaece50: a texture of the buffer's pixels (nothing when buffer or out is null, or the image is not made). A one-level
// image (format 1) of the buffer's size, named `name` when given; the buffer's rows (bytes per pixel x width each) are
// copied in while it is locked (flag 0x40; nothing copied when the lock fails); the texture loads it, goes to the texture
// manager under `name` (flag 1) when `add`, and to *out counted (the pointer *out held before is not released).
void __cdecl MakeTextureFromBuffer(cIGZBuffer *buffer, void **out, const char *name, uint8_t add);

// ---- skins and CAS models (lane 261111h, src/sg/SGUtilitiesSkin.cpp) ---------------------------------------------------
// A skin is a cSkinExtension (class 0xfca0e8b3, sg/cSkinExtension.h) on the model's root: its pairs are {shape-ref node,
// shape} (each shape also added to that node), its refs the dynamics data lists cloned onto bones. "Bones" are the tagged
// transform nodes a cFindBonesTraversal (class 0x5ca10ca7) collects. All cdecl; every count below is the original's.

class cIObjectGraphNode;
class cIResourceNode;
class cISkinExtension;
class cIFindBonesTraversal;

// The skin list SetUpSkinning takes: the game's vector of raw (uncounted) skins, grown and freed through the allocator
// service.
using SkinVector = std::vector<cISkinExtension *, GZServiceAllocator<cISkinExtension *>>;

// 0xae9f70: the node's next skin extension after `after` (from the first when null) whose word (slot 14) is `word`,
// uncounted; null when none, or when `after` is the last extension.
cISkinExtension *FindSkinExtension(cIObjectGraphNode *node, uint32_t word, cISkinExtension *after);
// 0xaea100: the same, matching the skin's name (slot 13) to `name` case blind.
cISkinExtension *FindSkinExtensionByName(cIObjectGraphNode *node, const char *name, cISkinExtension *after);

// 0xae9da0: a new skin added to the node, named `name` with word `word` (the creation is not checked); then for each bone
// (filter `filter`, tagged only) that is a shape-ref node, a pair {that node, shape j} for each of its shapes. The skin,
// uncounted.
cISkinExtension *AddSkinExtension(cICompositionTreeNode *node, const char *name, uint32_t word, int32_t filter);

// 0xaea1f0: every pair's shape still on its node gets `material` (subset `subset`, or all subsets when null); a shape
// with more than one referrer is first replaced on the node and in the pair by a clone (slot 15 mode 1), which gets it.
// Then MapName(material, subset).
void RebindSkin(cISkinExtension *skin, const char *material, const char *subset);

// 0xaea360: the skin taken down: its pairs from the last (each erased, its shape removed from its node when there), its
// refs from the last (each removed from its owner when it has one, then erased), then the skin removed from its owner.
void UndoSkin(cISkinExtension *skin);
// 0xaea4b0: UndoSkin on each skin extension of the node whose word is `word`.
void UndoSkinsWithWord(cIObjectGraphNode *node, uint32_t word);
// 0xaea5c0: UndoSkin on the node's first skin extension until there is none.
void UndoAllSkins(cIObjectGraphNode *node);

// 0xae8fd0: the skins' bones set up from `tree`'s data lists: "bone_adjust" lists move their tagged bone; each transform
// node under `tree` (filter: `filter` as a mask bit when not negative) with a bone of its tag gets its
// "tsDynamicsBehavior.s" lists cloned onto the bone (and pushed on the first skin of their type), then its
// "IKTargetMorphBase" / "IKTargetMorphDelta" lists replace the bone's. false (nothing done) for a null node, tree or
// bones or no skins.
bool SetUpSkinning(cICompositionTreeNode *node, cICompositionTreeNode *tree, SkinVector *skins,
                   cIFindBonesTraversal *bones, int32_t filter);

// 0xae8a20: a model's skin: `resource` (when null, the "<name>_cres" resource from the resource manager, its word taken
// from slot 64; null returned when it is not there) has each tagged transform node's shapes added to the node's bone of
// the same tag (lowercased; a shape-ref node made under the bone when it has none) and paired in a new skin named `name`.
// Then SetUpSkinning(node, resource, {skin}). The skin, uncounted.
cISkinExtension *LoadObjectModel(cICompositionTreeNode *node, const char *name, cIResourceNode *resource, uint32_t word,
                                 int32_t filter);

// 0xae97a0: each source skin copied onto the node as a new skin (name, word, name map), each of its pairs' shapes added to
// the node's bone tagged as the pair's node (or its parent) and paired; then SetUpSkinning with the first source's owner
// tree. false for a null node or no sources, else SetUpSkinning's answer.
bool LoadPersonPart(cICompositionTreeNode *node, SkinVector *sources, int32_t filter);

// ---- leftovers of the helper block (lane 261111l) -------------------------------------------------------------------

// 0xae43a0 (SGUtilities.cpp): ChannelOwnerOf(animated)'s slot 8 answer (the owner's inner object), or null when there is
// no owner.
void *ChannelSourceOf(void *animated);

// 0xae68a0 (SGUtilitiesTree.cpp): a name of two or more characters that starts with "l_" (StartsWith, case blind) has
// it replaced by "r_" (slot 25, Replace(0, "r_", 2)); one that starts with "r_" gets "l_" likewise; else nothing.
void SwapSidePrefix(cRZString *name);

// 0xaeaae0 (SGUtilitiesTree.cpp): whether `ancestor` is reached from `node` by GetParentConst (slot 33) steps, at least
// one: false when the chain ends (null) first. A node is not its own ancestor, unless its parent chain loops back.
bool IsAncestorOf(cICompositionTreeNode *ancestor, cICompositionTreeNode *node);

// 0xaeab10 (SGUtilitiesTree.cpp): the node kind's name ("TransformNode" .. "ExplicitBoundNode" for 0..9), "UNKNOWN"
// for any other (unsigned) value.
const char *NodeKindName(uint32_t kind);

// 0xaeac50 (SGUtilitiesTree.cpp): SetFlag(bit, value) (slot 29, the raw word passed) on the node, then on each child's
// subtree (slots 42 / 38), node first.
void SetFlagInTree(cICompositionTreeNode *node, uint32_t bit, uint32_t value);

// The element of the name vectors 0xae8090 fills (the game's vector instance 0xa53cf0 / 0xa59400 over it): a
// ServiceCell copied with ConstructCopy (0xaeb6f0, counted again) and destroyed with Close (0xaeb760). No move: a
// growing vector constructs the new element, copies the old ones, then closes them, as the game's does.
struct CountedServiceCell : ServiceCell {
    // The cell opened on `text` (ServiceCell::Open(text, 0)).
    explicit CountedServiceCell(const char *text);
    CountedServiceCell(const CountedServiceCell &other) { ConstructCopy(&other); }
    CountedServiceCell &operator=(const CountedServiceCell &) = delete;
    ~CountedServiceCell() { Close(); }
};
static_assert(sizeof(CountedServiceCell) == 4, "CountedServiceCell: one word");

// 0xae8090 (SGUtilitiesTree.cpp): for a node with flag 4 (a shape-ref node), every shape's (GetShapesConst, slot 92)
// subset bindings (shape slot 54, all subsets): each binding's material (the material manager's GetInfo of its index,
// the manager fetched again per binding) has each texture name (slots 25 / 26) appended to `out` as a counted cell;
// the binding cursors are Released (end, middle, cursor) after each shape. Then each child's subtree (slots 41 / 37).
void CollectTextureNames(std::vector<CountedServiceCell> *out, cICompositionTreeNode *node);

// What 0xaf2450 reads from a viewer and 0xaf23f0 gives back to one (0x24 bytes, on the callers' stacks).
struct ViewerTextureState {
    uint32_t mCount;               // +0x00 GetTextureCount (slot 105)
    float mScale;                  // +0x04 GetTargetSize's (slot 110) third word, bits
    cIGZUnknown *mTextures[4];     // +0x08 GetTexture(i) (slot 106), counted; a count above 4 writes past them
    cIGZUnknown *mRef94;           // +0x18 GetTextureRef94 (slot 107), counted
    uint32_t mHandle;              // +0x1c a cTextureHandle: Slot108's texture-manager id
    uint8_t mDeviceView;           // +0x20 Slot113
};
static_assert(sizeof(ViewerTextureState) == 0x24, "ViewerTextureState: 0x24 bytes");

class cIViewerRefNodeBase;

// 0xaf2450 (SGUtilitiesViewer.cpp): the state of `viewer` into `out`: count 0 and scale 1.0, then mDeviceView = slot
// 113; when not set, the handle is assigned a handle made from Slot108's id (InitFromId, Assign, Destroy); when that
// handle is none, the count, the target size's scale, the ref94 texture and each of the textures (count re-read every
// step) are taken, each counted cell set as AddRef new / store / Release old when the pointer differs (the old value read
// after the getter). The other cells are left as they were.
void CaptureViewerTextures(ViewerTextureState *out, cIViewerRefNodeBase *viewer);

// 0xaf23f0 (SGUtilitiesViewer.cpp): the state given to `viewer`: device view: Slot92(null, null, 1.0); else a handle
// that is not none: Slot94(handle); else Slot93(count, textures, ref94, 1.0).
void ApplyViewerTextures(cIViewerRefNodeBase *viewer, const ViewerTextureState *state);

// ---- text billboards (lane 261111m, src/sg/SGUtilitiesText.cpp) ------------------------------------------------------
// A node's text output is a shape-ref child tagged "@@TextOutput_<name>" showing a material and a procedural texture
// both named "@@TextOutput_<node pointer as %d>_<name>", the texture drawn by a cTextureTextUpdateStrategy (a ring of
// lines). All cdecl.

class cIProceduralTexture;
class cTextureTextUpdateStrategy;

// What FindNodeTextOutput answers: both uncounted, both null when there is no such texture.
struct TextOutput {
    cIProceduralTexture *mTexture;
    cTextureTextUpdateStrategy *mStrategy;
};

// 0xaf42e0: the texture manager's "@@TextOutput_%d_%s" (node, name) texture when it Has one: FindOrLoad's id, the
// texture GetTexture answers asked for cIProceduralTexture (unchecked), and when it answers, its strategy (slot 13)
// asked for 0xcb86c1bd; both written to *out, then released (strategy first). Otherwise {null, null}. Returns out.
TextOutput *FindNodeTextOutput(TextOutput *out, cICompositionTreeNode *node, const char *name);

// 0xaf4250: when the node has the text output `name`, the line {*colour, text} pushed to its strategy and the texture
// redrawn (slot 12).
void AddNodeTextLabel(cICompositionTreeNode *node, const char *name, const uint32_t *colour, const char *text);

// 0xaf42a0: when the node has the text output `name`, its strategy's lines cleared and the texture redrawn.
void ClearNodeTextLabels(cICompositionTreeNode *node, const char *name);

// 0xaf4120: the first child of `node` (slots 42 / 38) tagged "@@TextOutput_<name>" removed (slot 46).
void RemoveBillboardTextOutput(cICompositionTreeNode *node, const char *name);

// 0xaf3860: nothing when a child of `node` is already tagged "@@TextOutput_<name>". Otherwise a material (class
// 0x49596978) named "@@TextOutput_%d_%s" (node, name), type "BillboardTextOutputMaterial", properties width, height
// ("%d"), textureName (the same name), enableAlphaBlend ("true" unless colour[0] == 1.0, NaN true) and backgroundColor
// ("(%g,%g,%g,%g)" of colour[1], [2], [3], [0]), added to the material manager (flags 2) and held by a handle; when the
// texture manager has no texture of that name, a new cTextureTextUpdateStrategy (height, fontSize) drawing a new
// procedural texture (class 0x6b81dfe9; CreateClosest (width, height, 1 level, 16 bits, 4 alpha bits, quality 1)) added
// to the texture manager (flags 1) and to the material by id. Then a shape-ref node (class 0x65245517) at `translation`
// (slot 64) tagged as above, a shape (0xfc6eb1f7) added to it, holding MakeBillboardQuadMesh (0.5, 0, 1, 0, 0) at lod 0
// bound to the material for subset "layer"; the shape-ref node added to `node`.
void MakeBillboardTextOutput(cICompositionTreeNode *node, const float *translation, const char *name, uint32_t width,
                             uint32_t height, const float *colour, uint32_t fontSize);

} // namespace nGZSceneGraph

// The branch filters' constructors (src/sg/SGUtilitiesSkin.cpp), as members of stand-ins over the filters' layouts.
namespace nRZSceneGraph {

// 0xaea7a0 (thiscall (iid), ret 4): the iid, then vtable 0xe7cf54; returns this. Ghidra's library match named it
// std::codecvt_base's constructor: the bytes coincide (db/library-exclusions.jsonl).
struct IidFilterConstructor : cBranchNodesIIDFilter {
    cBranchNodesIIDFilter *Construct(uint32_t iid);
};

// 0xaea7c0 (thiscall (maskBit, iid), ret 8): the mask bit, the iid, then vtable 0xe7cf60; returns this.
struct MaskFilterConstructor : cBranchNodesMaskBitAndIIDFilter {
    cBranchNodesMaskBitAndIIDFilter *Construct(uint32_t maskBit, uint32_t iid);
};

} // namespace nRZSceneGraph
