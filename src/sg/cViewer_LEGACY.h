#pragma once
// nGZSceneGraph::cViewer_LEGACY (lane VD, scenegraph N3r): the viewer resource of old scene-graph streams, class
// 0xbca76ddc of the scene graph's COM director. It holds one view (a cViewerRefEntry, sg/cViewerRefNodeImpl.h) and is a
// scene-graph resource whose node is a plain object-graph node. Viewer-reference records before version 12 refer to one
// (interface 0x9ca76dd7) and copy its view (cViewerRefNodeImpl::Read). Standalone shape:
//
//   class cViewer_LEGACY : public cSGResource,
//                          public cObjectGraphNodeT<cIObjectGraphNode, cObjectGraphNodeImpl, cViewerLegacyBehaviorTraits>
//
//   +0x00 the cSGResource part (vtable 0xe7066c, 13 slots: QueryInterface, AddRef / Release (the node's), the key slots,
//         GetRefCount 11 (the node's count), the deleting destructor 12); its vbptr +0x1c (vbtable 0xe707a4)
//   +0x2c the node: its face cIObjectGraphNode (vtable 0xe706a4, 30 slots; 29 the deleting destructor), +0x30 its count,
//         +0x34 its persist face (vtable 0xe70720, 7 slots), +0x38 the cObjectGraphNodeImpl
//   +0x40 mView, the view (0xcc bytes)
//   +0x10c the vtordisp, +0x110 the virtual base cISGResource (vtable 0xe70740, 25 slots); 0x114 bytes
//
// MSVC gives that layout from the declaration (static_asserts in the .cpp). QueryInterface / AddRef / Release are the one
// final overriders of every face; Read / GetTypeInfo / Write / GetOwnerInfo override the node's persist face and the
// virtual base's slots 3-6 (the game's virtual-base thunks jump to the +0x34 bodies).
//
// The switch: the factory 0xa66010 (the director's) is hooked to Create, the constructor 0xa64520 to Construct, the
// destructor 0xa64860 to Destruct and the deleting destructor 0xa64c00 (primary slot 12; the node face's 29 is a thunk
// to it) to DeletingDestructor. The constructor's one caller is the factory and the destructor's the deleting
// destructor, so once the factory is ours no object carries the game's vtables and the slot bodies only they reach are in
// effect through callers.
#include "sg/SGNodeTemplates.h"
#include "sg/cObjectGraphNodeImpl.h"
#include "sg/cSGResource.h"
#include "sg/cViewerRefNodeImpl.h" // cViewerRefEntry

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// The behaviour cells of cAbstractBehaviorTypeT<cIObjectGraphNode, cRefSingleThreaded>: the connection-manager cache
// 0x10f63dc (an owned static, sg/SGBehaviorCopies.h: the game's copy uses the same cell) and the object-graph registry.
struct cViewerLegacyBehaviorTraits {
    static nGZEvent::cIConnectionManager *&EventManagerCell();
    // The object-graph registry with id 0 (0x264010 with the default record, as slots 12-14 ask it).
    static nGZEvent::cEventAttributeMap *Registry();
};

class cViewer_LEGACY
    : public cSGResource,
      public cObjectGraphNodeT<cIObjectGraphNode, cObjectGraphNodeImpl, cViewerLegacyBehaviorTraits> {
    using Node = cObjectGraphNodeT<cIObjectGraphNode, cObjectGraphNodeImpl, cViewerLegacyBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x114;
    static constexpr uint32_t kIid = 0x9ca76dd7;     // what QueryInterface answers with the object itself
    static constexpr uint32_t kClassId = 0xbca76ddc; // the director's class id, and the type-info record's word
    static constexpr uint32_t kVersion = 0x11;       // the type-info record's; older streams carry seven more words

    // 0xa64520: the cSGResource part, the node, the view as the entry's constructor (0xa64270) makes it.
    cViewer_LEGACY() = default;
    // 0xa64860: the view (its materials), the node (its extensions removed: 0xa64670), the cSGResource part.
    ~cViewer_LEGACY() override = default;

    // 0xa66010 (cdecl; the director's factory, hooked): a new viewer's node face, uncounted.
    static cIObjectGraphNode *__cdecl Create();

    // ---- the game's bodies as hookable adapters (fastcall standing in for thiscall: the object in ecx) ----
    // (Member pointers of a class with a virtual base carry a vbtable index, so the hook table takes these statics.)
    // 0xa64520 (ret 4): the complete constructor; its one caller (the factory) passes the most-derived flag 1.
    static cViewer_LEGACY *__fastcall Construct(cViewer_LEGACY *self, void *, uint32_t mostDerived);
    static void __fastcall Destruct(cViewer_LEGACY *self, void *); // 0xa64860
    // 0xa64c00 (ret 4): sized delete of 0x114 on bit 0.
    static cViewer_LEGACY *__fastcall DeletingDestructor(cViewer_LEGACY *self, void *, uint32_t flags);

    // ---- cIGZUnknown (every face) ----
    // 0xa68930 (ret 8; thunks 0xa6898b / 0xa68993): a null out gives false; kIid is the object itself (the cSGResource
    // part), counted; then the node's answers (0x3a5ae0); then the cSGResource part's (0xa3c4d0).
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override { return Node::AddRef(); }   // (shared 0x3a46c0: the node's)
    uint32_t Release() override { return Node::Release(); } // 0xa68f70 -> 0xa68f30: the node's; the last one deletes

    // ---- cIObjectGraphNode ----
    // 15 (shared 0x16a930, ret 0x10): nothing (no clone, *out untouched).
    void Clone(uint32_t, uint32_t, void **, cINodeCloneContext *) override {}
    // 25: the Impl's (cIObjectGraphNode and the referent ids).
    bool IsKindOf(uint32_t iid) override { return mImpl.IsKindOf(iid); }

    // ---- cIGZPersistSerializableReferent (+0x34, and the virtual base's 3-6) ----
    // 0xa68a00 (ret 4): the record, the cSGResource part (0xa3c540), the node (0xa345f0); before version 0x11 seven
    // words read and dropped; the view's materials (as a viewer entry's), its fields (viewport, sub-rectangle, near, far,
    // fov, pixel aspect, depth range, offsets), a flag that makes it orthographic, and the orthographic box.
    void Read(ifaces::cIGZPersistReader *reader) override;
    cPersistTypeInfo GetTypeInfo() override; // 0xa69750: {"cViewer", 0xbca76ddc, 0x11}
    void Write(ifaces::cIGZPersistWriter *) override {} // shared 0x300a0 (ret 4): nothing is written
    // shared 0x3a5fb0 -> 0xa3c750: the cSGResource part's (the virtual base).
    uint32_t GetOwnerInfo() override { return cSGResource::GetOwnerInfo(); }

    // ---- cIGZPersistResource (the primary) ----
    // shared 0x3a56b0 -> 0x3a56a0: the node's count.
    uint32_t GetRefCount() override { return Node::mRefCount & nGZEvent::behavior_mt::kCountMask; }

    // ---- cISGResource (the virtual base) ----
    const void *GetRecord18() override { return "_view"; } // 0xa68720 (vtordisp thunk 0xa68716)
    const void *GetRecord19() override { return "5vr"; }   // 0xa68710 (vtordisp thunk 0xa68704)

    cViewerRefEntry mView; // +0x40
};

} // namespace nGZSceneGraph
