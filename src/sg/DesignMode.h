// The design-mode module (lane 261111o, scenegraph "Design-mode module" in docs/subsystems/scenegraph.md): free cdecl
// helpers that find an object's recolourable ("design mode") material subsets in its composition tree. Each shape's
// geometry (cIShape slot 50) carries data lists: "tsDesignModeEnabled" (the subsets that may be recoloured, as item
// names) and "tsMaterialsMeshName" (subset -> mesh name); the geometry's slot 42 walks its subset names.
//
// All hooked at their RVAs (each is its own body, none under 5 bytes). Strings the callers pass are the game's
// cRZString (hook-shaped); their virtual slots are called through their own vtables as the original does.
#pragma once

#include "rz/cRZString.h"
#include "ts/CountedRef.h"

#include <cstdint>
#include <list>
#include <set>

class cIGZString;
class cIGZUnknown;

namespace nGZSceneGraph {
class cICompositionTreeNode;
class cIShape;
} // namespace nGZSceneGraph

namespace DesignMode {

using nGZSceneGraph::cICompositionTreeNode;
using nGZSceneGraph::cIShape;

// The shape list 0x3aa260 fills: the game's std::list of counted shape pointers (MSVC layout {head, size}, 12-byte
// nodes {next, prev, shape}), the same as ours.
using ShapeList = std::list<fx::CountedRef<cIShape>>;

// 0x3abe70: the object the cached singleton's (0x20ddfb) slot 26 table holds under `id` (its slot 29, the id sign
// extended).
void *ObjectById(int16_t id);

// 0x3aa260: for each child of `node` (slot 42 range, slot 38 next), in order: nothing when the child answers
// QueryInterface 0xfa1c39ed (a face of another object); otherwise, when it answers the shape-ref face (0xa516cd10),
// every shape of that face's slot 93 range appended (AddRef'd); then the child's own children likewise. The faces
// written are Released (the portal face through its virtual base) after the child's subtree.
void CollectChildShapes(cICompositionTreeNode *node, ShapeList *shapes);

// 0x3abe90: the shape's geometry (slot 50; null: false) has a "tsDesignModeEnabled" list holding `subset` (each item
// name, asked before the subset's text, compared case-blind).
bool IsDesignModeSubset(cIShape *shape, cIGZString *subset);

// 0x3ab850: the geometry's "tsMaterialsMeshName" GetString(subset text, out); when found and `out` does not end with
// "_cres" (case-sensitive), "_cres" is appended (out's slot 19). False when there is no geometry or no list.
bool MaterialsMeshName(cIShape *shape, cIGZString *subset, cRZString *out);

// 0x3aab80: the object's first two design-mode subsets and their mesh names. The four strings are emptied first;
// nothing more for a null node. Over the shapes CollectChildShapes finds, in order, each geometry's subset names: an
// enabled subset without "shadow" (case-sensitive) becomes `primary` (with MaterialsMeshName into primaryMesh) while
// primary is empty; later, while `secondary` is empty, one not equal to primary (primary's slot 11, case-sensitive)
// becomes secondary (MaterialsMeshName into secondaryMesh) and that shape's walk ends.
void GetObjectDesignModeNames(cICompositionTreeNode *node, cRZString *primary, cRZString *primaryMesh,
                              cRZString *secondary, cRZString *secondaryMesh);

// 0x3aaec0: GetObjectDesignModeNames on the object's portal face (its slot 9 with 0xfa1c39f7, 0xfa1c39ed and a cell);
// when the object is null or does not answer, the four strings are emptied. The face is Released through its virtual
// base.
void GetPortalDesignModeNames(cIGZUnknown *object, cRZString *primary, cRZString *primaryMesh, cRZString *secondary,
                              cRZString *secondaryMesh);

// 0x3ab990: the first shape, depth first from `node` (its own shape-ref face's shapes when it answers and `subset` is
// not empty, null shapes skipped, then each child's subtree), whose geometry has a subset named `subset` (case-blind);
// uncounted. Null for a null node or none.
cIShape *FindObjectDesignMode(cICompositionTreeNode *node, cIGZString *subset);

// 0x3ab2b0: as FindObjectDesignMode, returning the shape's geometry; but an empty subset returns null before anything
// is asked, and null shapes are not skipped.
cICompositionTreeNode *FindGeometryWithSubset(cICompositionTreeNode *node, cIGZString *subset);

// 0x3ab570: over a shape container (slot 15 the count, slot 5 the shape at an index, null ones skipped), the first
// geometry with a subset named `subset` (case-blind). Null for a null container, an empty subset or none.
cICompositionTreeNode *FindListedGeometryWithSubset(cIGZUnknown *shapes, cIGZString *subset);

// 0x3abc40: as FindListedGeometryWithSubset, returning the shape.
cIShape *FindListedShapeWithSubset(cIGZUnknown *shapes, cIGZString *subset);

// ---- lane 261111p ----

// The subset lists the game passes in: std::list<cRZString> (12-byte header {head, size}, 0x28-byte nodes {next, prev,
// cRZString}) and std::set<cRZString> ordered by the strings' CompareTo (slot 15, case-blind) through the left one's
// vtable (0x30-byte nodes); both the same layout as ours (MSVC 14.40).
using SubsetList = std::list<cRZString>;
struct SubsetNameLess {
    bool operator()(const cRZString &left, const cRZString &right) const;
};
using SubsetNameSet = std::set<cRZString, SubsetNameLess>;

// 0x3aafa0: the geometry's "tsDesignModeSlaveSubsets" GetString(subset, text), split at each ',' and appended to
// `out` in order. Nothing for a null geometry, a null or empty subset, no list or no entry. A piece that comes out
// empty (the text starts with ',') takes the whole remaining text instead (ORIGINAL-BUG in the .cpp).
void SlaveSubsets(void *geometry, const char *subset, SubsetList *out);

// 0x3ab1d0: SlaveSubsets on the first listed geometry with the subset (FindListedGeometryWithSubset over a cRZString
// of `subset`), held (slot 1 / 2) around the call. Nothing for a null container or subset.
void ListedSlaveSubsets(cIGZUnknown *shapes, const char *subset, SubsetList *out);

// 0x3aa720: the material override for a subset, from the framework's service 0xac6965bc / 0x6c69658c: a candidate
// vector from its slot 8 (mesh text, subset text) when `mesh` is not empty, else its slot 9 (key, container name,
// subset; each null when empty). The first candidate whose name (slot 6) equals the subset's text (case-blind) and
// whose slot 7 is non-zero is returned uncounted; every candidate is Released and the vector's block freed through
// the allocator service. Null when there is no service or no match.
cIGZUnknown *FindMaterialOverride(uint32_t key, cIGZString *mesh, cRZString *containerName, cRZString *subset);

// 0x3abf70: applies the object's design-mode materials found in `container`: for its primary and then its secondary
// subset (GetPortalDesignModeNames), when the subset is not empty, the object's slot 292 says 0 for it and the
// container has a shape with it, the override FindMaterialOverride finds (key from slot 28 of the object's slot 61 /
// slot 27 chain, the container's slot 13 name) goes to the object's slot 290. Nothing for a null argument or a null
// link in that chain.
void ApplyDesignModeMaterials(cIGZUnknown *container, cIGZUnknown *object);

// 0x3ab7a0 (a cRZString returned by value): "<object slot 124>LOD<lod>", "_cres" appended when `cres`; empty for a
// null object. Called from 0x3f7a50.
cRZString *FormatMaterialName(cRZString *out, cIGZUnknown *object, int32_t lod, bool cres);

// 0x3aa420: every subset name of every geometry under `node` (CollectChildShapes, each geometry held while its names
// are walked) inserted into `names`. Called from 0x3f7f40.
void CollectSubsetNames(cICompositionTreeNode *node, SubsetNameSet *names);

} // namespace DesignMode
