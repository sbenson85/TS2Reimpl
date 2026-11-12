#pragma once
// nTSSG::cOverlayNode (lane 261108d, scenegraph row LT1; docs/subsystems/scenegraph.md): the renderable node that draws a
// lot's overlays (floor and skirt decals, shadows): a generator fills its list of overlay entries each frame, a geometry
// builds the triangles from that list, and the node hands the geometry to the view's display list with one material per
// entry. 0x19c bytes; vtables 0xdbe084 (92 slots), 0xdbe1f8 at +8 (the node family's thunks) and 0xdbe218 for the face
// nTSSG::cIOverlayNode at +0x7c (15 slots).
//
// The game makes it in cFloor (0x291a60, 0x294e90, 0x29ed20) and cLotSkirt (0x2fc5f0): the game's operator new (0x19c) and
// the constructor 0x3ae470, which is hooked, so every object carries our vtables and every slot body is ours (in effect
// through callers; db/status/cOverlayNode.jsonl). The destructor 0x3ae890 is hooked too.
//
// The node level is the particles node's template instantiation (ts/cTSParticlesRenderable.h; in the game both classes use
// the same cRenderableNodeT<cIRenderableNode, cRenderableNodeImpl> copies), so slots 15, 30 / 50, 43 / 44 / 51, 60 and
// 87..90 are the same shared bodies as the particles node's. Slot 0 (0x3af640), 1 and 2 (0x3af690) jump to the template's
// QueryInterface, AddRef and Release: the face's id is not answered (callers keep the pointer they made).
#include "gz/cIGZUnknown.h"
#include "sg/cShape.h" // ShapeMaterialHandle
#include "rz/GameAllocator.h" // GZServiceAllocator
#include "ts/cTSParticlesRenderable.h" // the template instantiation, cITSParticlesRenderable

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

namespace nGZSceneGraph {
class cIGeometryRender;
}

namespace nTSSG {

// One overlay (0xb8 bytes): what the generator writes; the node reads the material and the enabled flag.
struct OverlayEntry {
    uint32_t mWords[41];   // +0x00 the generator's and the geometry's
    int32_t mWordA4 = -1;  // +0xa4
    uint8_t mMaterial;     // +0xa8 which of the node's materials (no bound check where it is read)
    uint8_t mPadA9[11];
    uint8_t mEnabled;      // +0xb4 drawn with its material, else none (-1)
    uint8_t mPadB5[3];
    OverlayEntry() : mWords{}, mMaterial(0), mPadA9{}, mEnabled(0), mPadB5{} {}
};
static_assert(sizeof(OverlayEntry) == 0xb8 && offsetof(OverlayEntry, mMaterial) == 0xa8 &&
                  offsetof(OverlayEntry, mEnabled) == 0xb4,
              "OverlayEntry layout");

// The entries, allocated through the allocator service (the generator grows the vector itself).
using OverlayEntries = std::vector<OverlayEntry, GZServiceAllocator<OverlayEntry>>;

class cIOverlayNode;

// What fills the entries (slot 11; true when it changed them). No class of the game's is known by name yet.
class cIOverlayGenerator : public cIGZUnknown {
public:
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    virtual void Slot8() = 0;
    virtual void Slot9() = 0;
    virtual void Slot10() = 0;
    virtual bool Generate(OverlayEntries *entries) = 0; // 11
};

// The geometry the entries become: a cIGeometryRender (slots 3..8; the display list takes it as one) with three slots of
// its own.
class cIOverlayGeometry : public cIGZUnknown {
public:
    virtual bool IsSupported(uint32_t what) = 0;           // 3
    virtual const uint32_t *GetFlags() = 0;                // 4: its info block; the bound is at +0x14 (six floats)
    virtual uint32_t *GetDrawInfo(uint32_t what) = 0;      // 5
    virtual uint32_t Prepare(uint32_t a, uint32_t b, uint32_t c, uint32_t d) = 0; // 6
    virtual void Begin(uint32_t index, void *context, uint32_t first) = 0;                     // 7 (cIGeometryRender's)
    virtual uint32_t Render(uint32_t unused0, void *context, uint8_t flag, uint32_t unused3) = 0; // 8 (cIGeometryRender's)
    virtual void Attach(cIOverlayNode *node) = 0;          // 9
    virtual void Detach() = 0;                             // 10
    virtual void Build(OverlayEntries *entries) = 0;       // 11
};

// The face (vtable 0xdbe218 at +0x7c).
class cIOverlayNode : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                        // 3 (0x3af1b0)
    virtual bool Shutdown() = 0;                                    // 4 (0x3b16f0)
    virtual void SetGenerator(cIOverlayGenerator *generator) = 0;   // 5 (0x3afdf0)
    virtual void SetGeometry(cIOverlayGeometry *geometry) = 0;      // 6 (0x3afdb0)
    virtual void SetWord84(uint32_t value) = 0;                     // 7 (shared_vf_14a260)
    virtual bool Update() = 0;                                      // 8 (0x3b1820)
    virtual void SetMaterial(uint32_t index, const char *name) = 0; // 9 (0x3afd50)
    virtual int32_t GetEntryCount() = 0;                            // 10 (0x3af420)
    virtual const OverlayEntry *GetEntry(int32_t index, uint8_t *built) = 0; // 11 (0x3af440)
    virtual uint32_t GetWord84() = 0;                               // 12 (shared_vf_46390)
    virtual void *GetNode() = 0;                                    // 13 (0x3aeff0)
    virtual uint32_t GetMode() = 0;                                 // 14 (shared_vf_1cc660)
};

class cOverlayNode
    : public nGZSceneGraph::cRenderableNodeT<nGZSceneGraph::cITSParticlesRenderable, nGZSceneGraph::cRenderableNodeImpl,
                                             nGZSceneGraph::cTSParticlesRenderableBehaviorTraits>,
      public cIOverlayNode {
    using Base = nGZSceneGraph::cRenderableNodeT<nGZSceneGraph::cITSParticlesRenderable, nGZSceneGraph::cRenderableNodeImpl,
                                                 nGZSceneGraph::cTSParticlesRenderableBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x19c;
    static constexpr uint32_t kMaterialCount = 8;
    static constexpr uint32_t kDebugMaterial = 7;

    // A counted reference (released when it goes, unless null).
    template <class T> struct Held {
        T *mObject = nullptr;
        Held() = default;
        Held(const Held &) = delete;
        Held &operator=(const Held &) = delete;
        ~Held()
        {
            if (mObject) mObject->Release();
        }
    };

    cOverlayNode() = default; // 0x3ae470
    ~cOverlayNode() override = default; // 0x3ae890 (the deleting form, slot 91 0x3aeb80)

    // 0 / 1 / 2: the template's.
    bool QueryInterface(uint32_t iid, void **out) override { return Base::QueryInterface(iid, out); }
    uint32_t AddRef() override { return Base::AddRef(); }
    uint32_t Release() override { return Base::Release(); }

    // 15 (shared 0xa3a3b0) and the rest of the shared node slots, as the particles node has them.
    void Clone(uint32_t mode, uint32_t iid, void **out, nGZSceneGraph::cINodeCloneContext *context) override;
    uint32_t Accept(nGZSceneGraph::cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot7(this); }
    uint32_t AcceptReadOnly(nGZSceneGraph::cICompositionTreeTraversal *traversal) override
    {
        return traversal->TraversalSlot7(this);
    }
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator51(void *out, uint32_t a) override;
    void OnContentChanged(uint32_t transform, uint32_t b, uint32_t c) override;
    // 71 (0x3af3a0) / 72 (0x3af3c0): the geometry's bound (in `frame`, or as it is); with no geometry an empty box (the
    // largest float as the minimum, its negation as the maximum).
    nGZSceneGraph::cBoundBox GetBoundBoxIn(const nGZSceneGraph::cTransform34 *frame) override;
    nGZSceneGraph::cBoundBox GetBoundBox() override;
    // 82 (0x3aed00): a new display-list frame makes the materials stale; Update rebuilds the per-entry materials (and the
    // item slots); with entries and a geometry, the node goes to the display list in its world frame (view space) unless
    // the geometry's bound is wholly outside the view.
    void Draw(void *viewer, uint32_t planeMask) override;
    // 83 (0x3aec90): with a generator and a geometry, the geometry's subsets go to the display list (flags 0x81) with the
    // per-entry materials, from the node's first item slot.
    void Slot83(void *viewer, void *unused) override;

    // 87..90 (shared).
    uint8_t GetRenderableKind() override { return 2; }
    uint32_t GetDetailLimit() override { return mImpl.mDetailLimit; }
    uint32_t GetWord6c() override { return mImpl.mZero6c; }
    void SetWord6c(uint32_t value) override { mImpl.mZero6c = value; }

    // ---- cIOverlayNode ----
    // 3: once (until Shutdown), the eight materials by name.
    bool Init() override;
    // 4: once Init has run: the entries and the list emptied, the generator dropped, the geometry detached and dropped.
    bool Shutdown() override;
    void SetGenerator(cIOverlayGenerator *generator) override;
    // ORIGINAL-BUG: the geometry is attached (its slot 9) unconditionally, so a null geometry is dereferenced (0x3afdb0).
    void SetGeometry(cIOverlayGeometry *geometry) override;
    void SetWord84(uint32_t value) override { mWord84 = value; }
    // 8: the generator fills the entries; when it reports a change the geometry rebuilds from them.
    bool Update() override;
    // 9: materials 0..6 by name (the debug material, 7, is Init's only).
    void SetMaterial(uint32_t index, const char *name) override;
    int32_t GetEntryCount() override { return static_cast<int32_t>(mEntries.size()); }
    // 11: an entry (the default one out of range); `built` gets whether this frame's materials are built.
    const OverlayEntry *GetEntry(int32_t index, uint8_t *built) override;
    uint32_t GetWord84() override { return mWord84; }
    void *GetNode() override { return this; }
    uint32_t GetMode() override { return mMode; }

    bool mInitialised = false;                      // +0x80
    uint8_t mPad81[3];                              // (not written)
    uint32_t mWord84 = 0;                           // +0x84 (face slots 7 / 12)
    Held<cIOverlayGenerator> mGenerator;            // +0x88
    Held<cIOverlayGeometry> mGeometry;              // +0x8c
    nGZSceneGraph::ShapeMaterialHandle mHandles[kMaterialCount]; // +0x90 decal, additive, modulate, decal inverse depth, draw no
                                                    //       overlap, shadow, soft shadow, debug
    OverlayEntries mEntries;                        // +0xb0
    std::list<uint32_t, GZServiceAllocator<uint32_t>> mList; // +0xbc (only emptied here)
    OverlayEntry mDefault;                          // +0xc4 what GetEntry answers out of range
    bool mMaterialsBuilt = false;                   // +0x17c
    uint8_t mPad17d[3];                             // (not written)
    std::vector<int32_t> mMaterials;                // +0x180 one material index per entry, -1 for none
    uint32_t mFrame = 0;                            // +0x18c the display list's frame the materials were built in
    nGZSceneGraph::cTSParticlesRenderable::SlotRuns mSlots; // +0x190 one item slot per entry, grown as needed
    uint32_t mMode = 0;                             // +0x198 over 1: every enabled entry takes the debug material
};
static_assert(sizeof(cOverlayNode) == cOverlayNode::kSize, "cOverlayNode size");
static_assert(offsetof(cOverlayNode, mInitialised) == 0x80 && offsetof(cOverlayNode, mGenerator) == 0x88 &&
                  offsetof(cOverlayNode, mHandles) == 0x90 && offsetof(cOverlayNode, mEntries) == 0xb0 &&
                  offsetof(cOverlayNode, mList) == 0xbc && offsetof(cOverlayNode, mDefault) == 0xc4 &&
                  offsetof(cOverlayNode, mMaterialsBuilt) == 0x17c && offsetof(cOverlayNode, mMaterials) == 0x180 &&
                  offsetof(cOverlayNode, mFrame) == 0x18c && offsetof(cOverlayNode, mSlots) == 0x190 &&
                  offsetof(cOverlayNode, mMode) == 0x198,
              "cOverlayNode layout");

// The hooks' entry points (src/hook/tables/cOverlayNode.cpp): original calling convention, our bodies.
struct cOverlayNodeAdapters {
    using Self = cOverlayNode;
    static Self *__fastcall Construct(Self *memory, void *);
    static void __fastcall Destruct(Self *self, void *);
};

} // namespace nTSSG
