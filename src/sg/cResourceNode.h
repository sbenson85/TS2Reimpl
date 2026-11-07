#pragma once
// nGZSceneGraph::cResourceNode (lane OG, scenegraph N1): class 0xe519c933 of the scene graph's COM director, a
// composition node that is also a scene-graph resource (the first class built on our cSGResource). A resource node may
// instance another one, its source: reading a node that names a source copies the source's children in, and writing it
// writes the source's resource instead of its own resource part. Standalone shape:
//
//   class cResourceNode : public cCompositionTreeNodeT<cIResourceNode, cCompositionTreeNodeImpl, Traits>,
//                         public cSGResource
//
//   +0x00 the node (vtable 0xe6d698, 66 slots: 61-64 cIResourceNode's, 65 the deleting destructor), +0x04 its count
//   +0x08 the persist face (vtable 0xe6d7a4, 7 slots), +0x0c the composition Impl (0x1c bytes)
//   +0x28 the cSGResource part (vtable 0xe6d7c4, 13 slots; vbptr +0x44, name +0x48, segment +0x4c, flags +0x50 / +0x51)
//   +0x54 mSource      the resource node this one instances (counted), or null
//   +0x58 mWord        slots 63 / 64 (read, written and cloned along; meaning unknown)
//   +0x5c the vtordisp, +0x60 the virtual base cISGResource (vtable 0xe6d7fc, 25 slots)
//
// MSVC gives that layout from the declaration (static_asserts in the .cpp). Each override below is the one final
// overrider of slots several faces share: QueryInterface / AddRef / Release (all four faces), Read / GetTypeInfo / Write /
// GetOwnerInfo (the +8 face and the virtual base's 3-6: the game's virtual-base thunks jump to the +8 bodies).
//
// The switch: the factory 0xa503a0 is hooked to Create, so every resource node the director makes is ours. The game's
// constructor 0xa4fd10 has two callers, that factory and the game's Clone 0xa50090 (only its own vtable holds it), so
// once the factory is ours the game's constructor, destructor and slot bodies never run (in effect through callers).
#include "game/owned.h"
#include "sg/SGNodeTemplates.h"
#include "sg/cSGResource.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// The behaviour cells of cAbstractBehaviorTypeT<cIResourceNode, cRefSingleThreaded>; the registry is the node family's.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f61fc); // the cell below (game/owned.h)
struct cResourceNodeBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f61fc; // 0xa501a0's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f61fc; }
    static nGZEvent::cEventAttributeMap *Registry() { return NodeEventRegistry(); }
};

class cResourceNode
    : public cCompositionTreeNodeT<cIResourceNode, cCompositionTreeNodeImpl, cResourceNodeBehaviorTraits>,
      public cSGResource {
    using Node = cCompositionTreeNodeT<cIResourceNode, cCompositionTreeNodeImpl, cResourceNodeBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x64;
    static constexpr uint32_t kClassId = 0xe519c933; // the director's class id, and the type-info record's word
    static constexpr uint32_t kVersion = 7;          // the type-info record's version; older streams carry a string

    // 0xa4fd10: the composition node and the cSGResource part, no source, the word 0, flag 3 set.
    cResourceNode();
    // 0xa4fe50: the source released (cleared first), then the cSGResource part, then the node (its children removed).
    ~cResourceNode() override;

    // 0xa503a0 (cdecl; the director's factory, hooked): a new node, uncounted.
    static cResourceNode *__cdecl Create();
    // 0xa50400 (cdecl, hooked): the class id cSceneGraphSystem's Init registers the factory under.
    static uint32_t __cdecl GetClassId();

    // ---- cIGZUnknown (every face) ----
    // 0xa50620 (ret 8): a null out gives false; cIResourceNode is this node, counted; then the composition node's
    // answers; then the cSGResource part's (0xa3c4d0).
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override;  // shared 0x3aecf0: the node's count
    uint32_t Release() override; // 0xa508c0 -> 0xa50850: the node's count; the last one deletes

    // ---- cIObjectGraphNode / cICompositionTreeNode ----
    // 15 (0xa50090): the context's copy if it has one, else a new node (counted); its content copied (CopyContentTo)
    // unless mode is 2; viewed as `iid` in *out.
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;
    // 25 (0xa50af0): cIResourceNode, cISGResource and cIGZPersistResource2, else the composition node's.
    bool IsKindOf(uint32_t iid) override;
    // 30 / 50 (0xa50b20): the traversal's slot 12.
    uint32_t Accept(cICompositionTreeTraversal *traversal) override;
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override;
    // 43 / 44 / 51 (shared 0xa37380 / 0xa37360 / 0xa37260): the Impl's iterator builders (ours).
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator51(void *out, uint32_t a) override;

    // ---- cIResourceNode ----
    cISGResource *GetResourceConst() override; // 61 (0xa4ff90): the virtual base
    cISGResource *GetResource() override;      // 62 (0xa4ff90)
    void Slot63(uint32_t value) override;      // 63 (shared 0x1fc130): the word
    uint32_t Slot64() override;                // 64 (shared 0x1dff70)

    // ---- cIGZPersistSerializableReferent (+8, and the virtual base's 3-6) ----
    // 0xa50690 (ret 4): the record; a flag: set, the cSGResource part (0xa3c540) and the composition part (0xa38180),
    // clear, the object-graph part alone (0xa345f0); the source (an object reference, iid cIResourceNode), whose
    // children are cloned (mode 1) and added; the word; before version 7 a string, read and dropped.
    void Read(ifaces::cIGZPersistReader *reader) override;
    cPersistTypeInfo GetTypeInfo() override; // 0xa50b40: {"cResourceNode", 0xe519c933, 7}
    // 0xa50c00 (ret 4): the record (0xa50b40 directly); a flag, whether there is no source; without one the cSGResource
    // part (0xa3ced0) and the composition part (0xa38430), with one the object-graph part alone (0xa34d70); the source's
    // resource (writer slot 27, null without a source); the word.
    void Write(ifaces::cIGZPersistWriter *writer) override;
    // 0xa508f0: with a source the composition node's (the parent's referent), else the cSGResource part's (0xa3c750).
    uint32_t GetOwnerInfo() override;

    // ---- cIGZPersistResource (+0x28) ----
    uint32_t GetRefCount() override; // 0xa50410 -> 0x3a56a0: the node's count

    // ---- cISGResource (the virtual base) ----
    bool SetSegment(cIGZUnknown *segment) override; // 15 (0xa50ac0): the source released first, then cSGResource's
    const void *GetRecord18() override;            // 18 (0xa50600): "_cres"
    const void *GetRecord19() override;            // 19 (0xa505f0): "5cr"

    // 0xa504b0 (ret 0xc): the Impl's CloneInto (CloneCompositionInto) into the clone's Impl; the clone's source set
    // to this node's source, or to this node when it has none; the word copied.
    void CopyContentTo(cResourceNode *clone, uint32_t mode, cINodeCloneContext *context);

    cIResourceNode *mSource = nullptr; // +0x54 counted
    uint32_t mWord = 0;                // +0x58
};

} // namespace nGZSceneGraph
