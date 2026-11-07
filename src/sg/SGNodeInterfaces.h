#pragma once
// The scene graph's node interfaces (lane NZ, scenegraph campaign N0): what every node class of the scene graph derives
// from (nGZSceneGraph::cIObjectGraphNode and the node interfaces built on it), the extension interfaces a node holds a
// list of, and the persistence face (cIGZPersistSerializableReferent) every node carries at +8. Declared in slot order
// from the vtables of cCompositionTreeNode (0xe68b2c, 62 slots), cTransformNode (0xe692ac, 67) and cExplicitBoundNode
// (0xe6c2a4, 80) and from the calls the node code makes; names past the behaviour's are ours (the RTTI has none) and
// say what the composition-tree node's body does. Slot numbers are vtable slots of the primary face.
//
// No interface here declares a destructor: a class's deleting destructor is the slot after its interface's last one
// (slot 61 for cICompositionTreeNode), which is where a C++ class deriving from it puts its virtual destructor.
// Overloads would be laid out out of order by MSVC, so const / non-const pairs have distinct names.
#include "gz/cGZEventReflector.h" // nGZEvent::cIAbstractBehavior
#include "gz/cIGZUnknown.h"

#include <cstdint>

class cIGZString;

namespace ifaces {
class cIGZPersistReader;
class cIGZPersistWriter;
} // namespace ifaces

namespace nGZCompGeom { // compgeom/CompGeom.h
class cIShape;
class cIBound;
class cILinear3;
} // namespace nGZCompGeom

// The record a persisted object's type is written and read as (cIGZPersistWriter slot 28 / reader slot 26): the class
// name (a counted cIGZString, a heap cRZString in what nodes hand out), a word nodes leave 0 and the class's version.
struct cPersistTypeInfo {
    cIGZUnknown *mName; // +0 counted
    uint32_t mWord4;    // +4
    uint32_t mVersion;  // +8
};
static_assert(sizeof(cPersistTypeInfo) == 0xc, "cPersistTypeInfo size");

// cIGZPersistSerializableReferent: the face a persist writer / reader serialises an object through (every node's +8).
class cIGZPersistSerializableReferent : public cIGZUnknown {
public:
    virtual void Read(ifaces::cIGZPersistReader *reader) = 0;                    // 3
    virtual cPersistTypeInfo GetTypeInfo() = 0;                                   // 4 (hidden pointer)
    virtual void Write(ifaces::cIGZPersistWriter *writer) = 0;                    // 5
    virtual uint32_t GetOwnerInfo() = 0; // 6: nodes ask their parent's face (the root's class answers)
};

namespace nGZSceneGraph {

struct cRenderGroupNameRange; // cRenderGroupNameIterator.h

class cIObjectGraphNode;
class cICompositionTreeNode;
class cINodeCloneContext;

// A node's local transform as slots 53 / 54 hand it out (7 floats; the composition node's identity is six zeros, then
// 1.0f: a translation and a rotation quaternion).
struct cRigidTransform {
    float mValues[7];
    float *Translation() { return mValues; }                    // x, y, z
    const float *Translation() const { return mValues; }
    float *Rotation() { return mValues + 3; }                   // the quaternion: x, y, z, w
    const float *Rotation() const { return mValues + 3; }
};
static_assert(sizeof(cRigidTransform) == 0x1c, "cRigidTransform size");

// The 0x34-byte transform form slots 56 / 58 convert (13 floats).
struct cTransform34 {
    float mValues[13];
};
static_assert(sizeof(cTransform34) == 0x34, "cTransform34 size");

// The record the transform nodes' attribute slots (7 and 10) read and write: a type word, then up to seven floats (the
// limit's value is one word; a translation three floats, a rotation four, the whole transform seven).
struct cAttributeValue {
    uint32_t mType;      // +0 (never touched by the nodes)
    float mValues[7];    // +4
};
static_assert(sizeof(cAttributeValue) == 0x20, "cAttributeValue size");

// What slots 71 / 72 of a bounded node hand out: a box as its two corners (nGZCompGeom's bound queries fill the same
// 0x18 bytes). A node without a shape answers its position twice.
struct cBoundBox {
    float mMin[3];
    float mMax[3];
};
static_assert(sizeof(cBoundBox) == 0x18, "cBoundBox size");

// Slots 41 / 42: the children as a cursor range {first, first, 0}.
struct cChildRange {
    cICompositionTreeNode *mCurrent;
    cICompositionTreeNode *mBegin;
    cICompositionTreeNode *mEnd;
};
static_assert(sizeof(cChildRange) == 0xc, "cChildRange size");

// nGZSceneGraph::cIExtension (interface id 0xfb06dba0, cExtension's face; 27 slots): what a node's extension list holds.
// The node keeps the first one counted; each extension keeps its owner and the next one. Slots 16, 21, 23 and 26 as
// cExtension's bodies show them (lane OY, src/sg/cExtension.h).
class cIExtension : public nGZEvent::cIAbstractBehavior {
public:
    static constexpr uint32_t kIid = 0xfb06dba0;
    virtual cIExtension *Clone() = 0;                                       // 15: a copy (uncounted)
    virtual cIGZPersistSerializableReferent *GetPersistReferent() = 0;      // 16: the persist face (uncounted)
    virtual cIObjectGraphNode *GetOwnerConst() = 0;                         // 17
    virtual cIObjectGraphNode *GetOwner() = 0;                              // 18
    virtual cIExtension *GetNextConst() = 0;                                // 19
    virtual cIExtension *GetNext() = 0;                                     // 20
    virtual cIObjectGraphNode *SetOwner(cIObjectGraphNode *owner) = 0;      // 21: returns the owner
    // 22: counted. eax (lane OY, 0xa3c0f0) is the last count answer: the old one's Release, else the new one's AddRef;
    // unchanged, the original leaves the caller's eax (ours 0).
    virtual uint32_t SetNext(cIExtension *next) = 0;
    virtual bool IsKindOf(uint32_t iid) = 0;                                // 23: an interface the class has
    virtual bool IsPersistent() = 0;                                        // 24: written with the node
    virtual const char *GetTypeName() = 0;                                  // 25: the kind ("data_list"; lane OY)
    virtual void GetDescription(cIGZString *text) = 0;                      // 26: a line of text about it
};

// nGZSceneGraph::cITagExtension (interface id 0x9a80964e): the legacy way a node's tag was kept, an extension with the
// tag text. A virtual base: the face has its own three slots and a vbptr at +4 to the cIExtension.
class cITagExtension : public virtual cIExtension {
public:
    static constexpr uint32_t kIid = 0x9a80964e;
    virtual void TagSlot0() = 0;          // 0
    virtual const char *GetTagText() = 0; // 1
    virtual void TagSlot2() = 0;          // 2
};

// nGZSceneGraph::cINodeCloneContext: a clone in progress (cloning a node that was already cloned answers the copy).
class cINodeCloneContext : public cIGZUnknown {
public:
    // 3: the copy of `node` already made (counted in *out), viewed as `iid`; or null.
    virtual void FindClone(cIObjectGraphNode *node, uint32_t iid, void *out) = 0;
};

// nGZSceneGraph::cICompositionTreeTraversal / cICompositionTreeReadOnlyTraversal: a visitor (slot 4 visits one node).
class cICompositionTreeTraversal : public cIGZUnknown {
public:
    virtual uint32_t Traverse(cICompositionTreeNode *node) = 0;  // 3: the template's `node->Accept(this)` (0xa95eb0; one argument, `ret 4`)
    virtual uint32_t Visit(cICompositionTreeNode *node) = 0;    // 4
    // The visitor has a slot per node class (tentative names): the transform nodes' Accept calls 5, the partition nodes'
    // 13 (shared 0xa4f700), the explicit-bound nodes' 15. Slots 6-11 and 14 are other classes' (nothing here calls them).
    virtual uint32_t VisitTransform(cICompositionTreeNode *node) = 0;   // 5
    virtual uint32_t TraversalSlot6(cICompositionTreeNode *node) = 0;
    virtual uint32_t TraversalSlot7(cICompositionTreeNode *node) = 0;
    virtual uint32_t TraversalSlot8(cICompositionTreeNode *node) = 0;
    virtual uint32_t TraversalSlot9(cICompositionTreeNode *node) = 0;
    virtual uint32_t TraversalSlot10(cICompositionTreeNode *node) = 0;
    virtual uint32_t TraversalSlot11(cICompositionTreeNode *node) = 0;
    virtual uint32_t VisitResource(cICompositionTreeNode *node) = 0; // 12: the resource nodes' Accept (0xa50b20)
    virtual uint32_t VisitPartition(cICompositionTreeNode *node) = 0; // 13: the partition nodes' Accept (0xa4f700)
    virtual uint32_t VisitScene(cICompositionTreeNode *node) = 0;     // 14: cSceneNode's Accept (0xa4f710)
    virtual uint32_t VisitExplicitBound(cICompositionTreeNode *node) = 0; // 15
};

// nGZSceneGraph::cIObjectGraphNode (interface id 0x5aef607b): a node with a tag and a list of extensions.
class cIObjectGraphNode : public nGZEvent::cIAbstractBehavior {
public:
    static constexpr uint32_t kIid = 0x5aef607b;
    // 15: a copy of this node viewed as `iid` in *out (counted); mode 2 copies no content. The result is not a value.
    virtual void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) = 0;
    virtual cIGZPersistSerializableReferent *GetReferentConst() = 0; // 16: the +8 face (null for a null node)
    virtual cIGZPersistSerializableReferent *GetReferent() = 0;      // 17
    virtual void SetTagHandle(uint32_t handle) = 0;                  // 18: a string-pool handle
    virtual void SetTag(const char *tag) = 0;                        // 19
    virtual cIExtension *GetFirstExtensionConst() = 0;               // 20
    virtual cIExtension *GetFirstExtension() = 0;                    // 21
    virtual void AddExtension(cIExtension *extension) = 0;           // 22: at the front
    virtual void RemoveExtension(cIExtension *extension) = 0;        // 23
    virtual bool FindExtension(uint32_t iid, void **out, cIExtension *start) = 0; // 24
    virtual bool IsKindOf(uint32_t iid) = 0;                         // 25
    virtual const char *GetTag() = 0;                                // 26: null when empty
    virtual uint32_t GetTagHandle() = 0;                             // 27
    virtual bool HasExtension(cIExtension *extension) = 0;           // 28
};

// nGZSceneGraph::cICompositionTreeNode (interface id 0x4512dd13): an object-graph node in a tree. Each node holds its
// first child and its next sibling counted, its parent and previous sibling raw.
class cICompositionTreeNode : public cIObjectGraphNode {
public:
    static constexpr uint32_t kIid = 0x4512dd13;
    enum LinkKind : uint32_t { kParentLink = 0, kPrevLink = 1, kNextLink = 2 };
    enum : uint32_t { kReadFlag = 12 }; // set by Read, cleared by slot 60

    virtual void SetFlag(uint32_t bit, bool value) = 0;                       // 29
    virtual uint32_t Accept(cICompositionTreeTraversal *traversal) = 0;       // 30
    virtual uint32_t GetFromParent31() = 0;                                   // 31: the parent's slot 31, or 0
    virtual uint32_t GetFromParent32() = 0;                                   // 32: the parent's slot 32, or 0
    virtual cICompositionTreeNode *GetParentConst() = 0;                      // 33
    virtual cICompositionTreeNode *GetParent() = 0;                           // 34
    virtual cICompositionTreeNode *GetPrevSiblingConst() = 0;                 // 35
    virtual cICompositionTreeNode *GetPrevSibling() = 0;                      // 36
    virtual cICompositionTreeNode *GetNextSiblingConst() = 0;                 // 37
    virtual cICompositionTreeNode *GetNextSibling() = 0;                      // 38
    virtual cICompositionTreeNode *GetFirstChildConst() = 0;                  // 39
    virtual cICompositionTreeNode *GetFirstChild() = 0;                       // 40
    virtual cChildRange GetChildrenConst() = 0;                               // 41
    virtual cChildRange GetChildren() = 0;                                    // 42
    virtual void *MakeIterator43(void *out, uint32_t a, uint32_t b) = 0;      // 43: returns out
    virtual void *MakeIterator44(void *out, uint32_t a, uint32_t b) = 0;      // 44: returns out
    virtual void AddChild(cICompositionTreeNode *child) = 0;                  // 45: at the front
    virtual void RemoveChild(cICompositionTreeNode *child) = 0;               // 46
    virtual void SetLink(uint32_t kind, cICompositionTreeNode *node) = 0;     // 47: LinkKind
    virtual bool TestFlag(uint32_t bit) = 0;                                  // 48
    virtual uint32_t GetFlags() = 0;                                          // 49
    virtual uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) = 0; // 50
    virtual void *MakeIterator51(void *out, uint32_t a) = 0;                  // 51: returns out
    virtual bool HasChildren() = 0;                                           // 52
    virtual cRigidTransform GetLocalTransform() = 0;                          // 53
    virtual cRigidTransform GetWorldTransform() = 0;                          // 54
    virtual uint32_t GetLimit55() = 0;                                        // 55
    virtual void ToParent34(cTransform34 *out, const cTransform34 *in) = 0;   // 56
    virtual void ToParent1c(cRigidTransform *out, const cRigidTransform *in) = 0; // 57
    virtual void FromParent34(cTransform34 *out, const cTransform34 *in) = 0; // 58
    virtual void FromParent1c(cRigidTransform *out, const cRigidTransform *in) = 0; // 59
    virtual void OnContentChanged(uint32_t a, uint32_t b, uint32_t c) = 0;    // 60: clears kReadFlag
};

class cISGResource; // sg/cSGResource.h

// nGZSceneGraph::cIResourceNode (interface id 0xe519c92c; cResourceNode's, vtable 0xe6d698): a composition node that is
// also a scene-graph resource, and that may instance another resource node (its source: the source's children are
// copied in when the node is read). Slots 63 / 64 set and get a word of the node's (+0x58) that Read, Write and Clone
// carry along; what it means is not known.
class cIResourceNode : public cICompositionTreeNode {
public:
    static constexpr uint32_t kIid = 0xe519c92c;

    virtual cISGResource *GetResourceConst() = 0; // 61: the resource face (null for a null node)
    virtual cISGResource *GetResource() = 0;      // 62
    virtual void Slot63(uint32_t value) = 0;      // 63: the word
    virtual uint32_t Slot64() = 0;                // 64
};

// nGZSceneGraph::cITransformNode (interface id 0xe51c10f5): a tree node with a rigid transform (a translation and a
// rotation quaternion, 7 floats) relative to its parent and a limit. Slots 7 and 10 of the behaviour (nothing in the
// base) are its SetAttribute / GetAttribute: attribute 0xc9de8b96 is the limit, 0x9b59fb00 the whole transform,
// 0x9b5b3506 the rotation, 0xfb5b3383 the translation. Setting the limit or the transform runs the tree code's
// propagation (still game code) over the node.
class cITransformNode : public cICompositionTreeNode {
public:
    static constexpr uint32_t kIid = 0xe51c10f5;
    enum : uint32_t { kLimitAttribute = 0xc9de8b96, kTransformAttribute = 0x9b59fb00, kRotationAttribute = 0x9b5b3506,
                      kTranslationAttribute = 0xfb5b3383 };

    virtual void SetLimit(uint32_t limit) = 0;                                              // 61
    virtual void SetTransformParts(const float *translation, const float *rotation) = 0;   // 62: 3 floats, 4 floats
    virtual void SetTransform(const cRigidTransform *transform) = 0;                        // 63
    virtual void SetTranslation(const float *translation) = 0;                              // 64: keeps the rotation
    virtual void SetRotation(const float *rotation) = 0;                                    // 65: keeps the translation
};

// nGZSceneGraph::cIBoundedNode (interface id 0x7d14eb66): a transform node that caches its world transform (kept
// current by the tree code: flag 12 marks the cache stale) and answers bound queries from it. Names past 66 are ours
// and tentative. Flag bit 1 of a bounded node tells the tree code's propagation (0xa39820) to call slot 69.
class cIBoundedNode : public cITransformNode {
public:
    static constexpr uint32_t kIid = 0x7d14eb66;
    enum : uint32_t { kBoundedFlag = 1, kBoundFlag = 11 }; // node flag bits

    virtual void SetBoundFlag(bool value) = 0;                                  // 66: flag 11; announced when it changes
    virtual void SetHolder(void *holder, uint32_t data) = 0;                    // 67: the two words the partition keeps
    virtual void *GetHolderConst() = 0;                                         // 68
    virtual void *GetHolder() = 0;                                              // 69
    virtual bool IsEverywhere() = 0;                                            // 70 (false here): the node reaches everywhere (a scene node; a light with no bound shape), so it is not re-placed by extent
    virtual cBoundBox GetBoundBoxIn(const cTransform34 *frame) = 0;             // 71: the node's box in `frame`
    virtual cBoundBox GetBoundBox() = 0;                                        // 72: the node's box
    virtual bool IntersectsBound(uint32_t bound, uint32_t frame) = 0;                   // 73 (false here): a cIBound placed by an AffineTransform3
    virtual bool IntersectsLinear(nGZCompGeom::cILinear3 *linear) = 0;          // 74 (false here)
    virtual bool FirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear) = 0;  // 75 (false here)
    virtual uint32_t GetHolderData() = 0;                                       // 76
};

// nGZSceneGraph::cIExplicitBoundNode (interface id 0x7d3a2385): a bounded node whose bound is a shape it keeps.
class cIExplicitBoundNode : public cIBoundedNode {
public:
    static constexpr uint32_t kIid = 0x7d3a2385;
    virtual void SetShape(nGZCompGeom::cIShape *shape) = 0; // 77: keeps a copy (queried as nGZCompGeom::cIBound)
    virtual nGZCompGeom::cIBound *GetShape() = 0;           // 78
};

// nGZSceneGraph::cIRenderableNode (cRenderableNodeT's face, e.g. cTSParticlesRenderable's vtable 0xe8c768): a bounded
// node that draws. Only what the partitions call is settled; 77-80 set and clear bits of a word of the Impl's (+0x60;
// 77 / 79 by a name some registry (0xaeb370) maps to a bit, 78 / 80 by mask), 81 sets a word of the node's (+0x70).
// Names are ours and tentative (lane OI).
class cIRenderableNode : public cIBoundedNode {
public:
    virtual void AddLayer(const char *name) = 0;     // 77: by render-group name
    virtual void AddLayerBits(uint32_t bits) = 0;    // 78
    virtual void RemoveLayer(const char *name) = 0;  // 79
    virtual void RemoveLayerBits(uint32_t bits) = 0; // 80
    // 81: the Impl's +0x64 word (persisted), the second word of GetLayerState: a pass draws the node only when it
    // matches the viewer's own tag as the viewer's GetLayerTagMatch says (cDisplayList.h, Pass::Shows).
    virtual void SetLayerTag(uint32_t value) = 0;
    // 82: the node is in view of `viewer` (a partition's cull found it); `planeMask` says which of the view's planes
    // the node's cell is already known to be inside of.
    virtual void Draw(void *viewer, uint32_t planeMask) = 0;
    // 83: per class, so the name stays a placeholder (the shape reference queues its shapes' draws, the light reference culls;
    // the viewer reference's takes two words; the light and shape references take their own arguments,
    // so a class with another signature declares its own).
    virtual void Slot83(void *a, void *b) = 0;
    virtual uint32_t CountLayers() = 0;                                            // 84
    virtual cRenderGroupNameRange *MakeLayerNames(cRenderGroupNameRange *out) = 0; // 85
    virtual uint32_t *GetLayerState(uint32_t *out) = 0; // 86: {mask, word} (Impl +0x60, +0x64) by hidden pointer; returns out
};

// The visitors the partitions' queries hand content nodes to (cIGZUnknown faces; slot 3 on). Names are ours (lane OI).
// nGZSceneGraph::cIIntersectionQueryStrategy (e.g. cQueryAnyStrategy 0xac6990 / 0xac69c0): asks the node whether it
// meets the shape (its slot 73) or the line (74); 1 stops the query.
class cIIntersectionQueryStrategy : public cIGZUnknown {
public:
    virtual uint32_t VisitShape(cIBoundedNode *node, nGZCompGeom::cIBound *shape, uint32_t arg) = 0; // 3
    virtual uint32_t VisitLinear(cIBoundedNode *node, nGZCompGeom::cILinear3 *linear) = 0;          // 4
};
// nGZSceneGraph::cINearerIntersectionQueryStrategy (e.g. nTSSG::cRayQueryNearerStrategy 0x2c0dc0): a node the line may
// hit nearer than *t; the strategy lowers *t on a hit (the node's slot 75) and keeps the node in its context.
class cINearerIntersectionQueryStrategy : public cIGZUnknown {
public:
    virtual bool Visit(void *context, float *t, cIBoundedNode *node, nGZCompGeom::cILinear3 *linear) = 0; // 3
};
// nGZSceneGraph::cIVisibilityQueryFilter (a viewer's +0x20): whether a node in view is drawn.
class cIVisibilityQueryFilter : public cIGZUnknown {
public:
    virtual bool Accept(cIBoundedNode *node, void *viewer) = 0; // 3
};

// nGZSceneGraph::cIPartitionNode (interface id 0x1c8e36aa; cSceneNode and cLooseOctreePartitionNode): a bounded node that
// holds other bounded nodes for spatial queries. It keeps two kinds: content nodes (InsertNode / UpdateNode / RemoveNode)
// and sub-partitions (AddPartition / RemovePartition), and sets itself as each one's holder (slot 67) with a word of its
// own (the octree: the content's list item, the sub-partition's index). The queries hand each content node that may
// meet the shape, the line or the view to a visitor. Names are ours and tentative (lane OI, from the octree's bodies).
class cIPartitionNode : public cIBoundedNode {
public:
    static constexpr uint32_t kIid = 0x1c8e36aa;
    virtual void Clear() = 0;                                    // 77: every sub-partition and content node removed
    virtual void RemovePartition(cIPartitionNode *partition) = 0;  // 78
    virtual bool RemoveNode(cIBoundedNode *node) = 0;            // 79
    virtual bool RemovePartitionFalse(cIPartitionNode *partition) = 0; // 80 (shared 0xa4ee60): 78, then false
    virtual bool AddPartition(cIPartitionNode *partition) = 0;     // 81: false when its box is outside this one
    virtual bool UpdateNode(cIBoundedNode *node) = 0;            // 82: removed, then inserted again
    virtual bool InsertNode(cIBoundedNode *node) = 0;            // 83: false when its box is outside this one
    // 84: the visitor's slot 3 (node, shape, arg) for each content node in a cell the shape meets (the bound's slot 19);
    // 1 from the visitor stops the walk and is returned.
    virtual uint32_t QueryShape(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cIBound *shape, uint32_t arg) = 0;
    // 85: the same for a line, ray or segment (the visitor's slot 4 (node, linear)).
    virtual uint32_t QueryLinear(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear) = 0;
    // 86: the visitor's slot 3 (context, t, node, linear) for each content node in the cells the line crosses, nearest
    // cell first; *t is the visitor's to lower, and cells past it are skipped.
    virtual void RayCast(float *t, void *context, cINearerIntersectionQueryStrategy *visitor,
                         nGZCompGeom::cILinear3 *linear) = 0;
    // 87: each content node (a cIRenderableNode) in a cell the viewer's frustum meets, drawable (flags 0x420 set, 0x4000
    // clear) and passing the viewer's filter (+0x20), is drawn (its slot 82 (viewer, the frustum's plane mask)).
    virtual void Cull(void *viewer) = 0;
};

// nGZSceneGraph::cILooseOctreePartitionNode (interface id 0xebfda963): a partition as a loose octree of a given size.
class cILooseOctreePartitionNode : public cIPartitionNode {
public:
    static constexpr uint32_t kIid = 0xebfda963;
    virtual void SetSize(float size) = 0; // 88: the octree emptied and resized (the edge of the root cell)
};

// nGZSceneGraph::cISceneNode (interface id 0x516c708): the root partition of a scene (lane OK, from cSceneNode's bodies;
// names ours and tentative). It holds content nodes and sub-partitions in two lists (no spatial index: every query asks
// each one) and keeps two queues of nodes whose bound changed (the tree code's NodeBoundFlagChanged queues them through slot 95), which
// the scene queries (90-93) bring into the lists first (94).
class cISceneNode : public cIPartitionNode {
public:
    static constexpr uint32_t kIid = 0x516c708;
    virtual cISGResource *GetResourceConst() = 0; // 88: the resource face (null for a null node)
    virtual cISGResource *GetResource() = 0;      // 89
    // 90-93: the queues flushed (94) when either holds a node, then 84-87 with the "in query" byte set around the call.
    virtual uint32_t SceneQueryShape(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cIBound *shape, uint32_t arg) = 0;
    virtual uint32_t SceneQueryLinear(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear) = 0; // 91
    // 92: RayCast (86) with a context word of its own (zeroed first) the visitor keeps its nearest node in; returns it.
    virtual uint32_t SceneRayCast(float *t, cINearerIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear) = 0;
    virtual void SceneCull(void *viewer) = 0; // 93
    // 94: each queued node placed again (a sub-partition: its holder's slot 80, else AddPartition here; a content node:
    // removed when its bound flag is clear, else its holder's UpdateNode or InsertNode here), the queues emptied, and
    // either list compacted once its free indices are more than 15% of it.
    virtual void FlushPending() = 0;
    virtual void QueueNode(cIBoundedNode *node) = 0;   // 95: onto the sub-partition queue (node flag 6) or the other
    virtual void UnqueueNode(cIBoundedNode *node) = 0; // 96: off its queue (it must be there)
};

} // namespace nGZSceneGraph
