#pragma once
// cTSParticlesRenderable (lane WQ, rendering row E3): the scene-graph node of a particles effect (RTTI 0x10c1964, vtables
// 0xe8c768 with 92 slots and 0xe8c8dc at +8; 0xd0 bytes). A renderable node on the plain cRenderableNodeImpl: it is drawn
// when the effect has live particles and its bound meets the view, and it hands the display list the effect's dynamic
// geometry (cTSParticlesGeometry) or a loaded shape's.
//
// The game makes it at one site, 0xbba860 (new 0xd0, the constructor 0xbb5010, Setup 0xbbad30), a member of the effect that
// is not ours yet. Standalone shape: cRenderableNodeT<cITSParticlesRenderable, cRenderableNodeImpl, ...> plus this class.
// The constructor and the destructor are hooked, so no object carries the game's most derived vtables and this class's slot
// bodies are in effect through their callers (db/status/cTSParticlesRenderable.jsonl). The template levels' copies under it
// (constructors, destructors, level vtables, 0xbb4xxx..0xbbdxxx) were kept live by the original nTSSG::cOverlayNode 0x3ae470;
// that constructor is ours since lane 261108d, so they are unreachable and not trapped yet (notes/backlog.md).
#include "rz/RZAutoRef.h"
#include "sg/SGRenderableTemplates.h"
#include "sg/cShape.h" // ShapeMaterialHandle
#include "simantics/cTSBehaviorTree.h" // RefPtrVector

#include <cstddef>
#include <cstdint>

class cTSParticlesEffect;
class cTSParticlesGeometry;
class cTSParticlesDescription;

namespace nGZSceneGraph {

// The four slots between cIRenderableNode and the deleting destructor (every renderable instantiation has them; the shape,
// viewer and light references declare theirs on their own faces).
class cITSParticlesRenderable : public cIRenderableNode {
public:
    virtual uint8_t GetRenderableKind() = 0;        // 87 (shared 0x559c60): 2
    virtual uint32_t GetDetailLimit() = 0;          // 88 (shared 0x251bf0: the Impl's +0x68)
    virtual uint32_t GetWord6c() = 0;               // 89 (shared 0x24bdc0: the Impl's +0x6c)
    virtual void SetWord6c(uint32_t value) = 0;     // 90 (shared 0x4daa20)
};

// The behaviour cells of cAbstractBehaviorTypeT<cIRenderableNode, cRefSingleThreaded>: the cell 0x10fc820 (an owned static,
// sg/SGBehaviorCopies.h) and, as in the game, the transform family's registry.
struct cTSParticlesRenderableBehaviorTraits {
    static nGZEvent::cIConnectionManager *&EventManagerCell();
    static nGZEvent::cEventAttributeMap *Registry();
};

class cTSParticlesRenderable
    : public cRenderableNodeT<cITSParticlesRenderable, cRenderableNodeImpl, cTSParticlesRenderableBehaviorTraits> {
    using Base = cRenderableNodeT<cITSParticlesRenderable, cRenderableNodeImpl, cTSParticlesRenderableBehaviorTraits>;

public:
    static constexpr size_t kSize = 0xd0;

    // A reference (slot 2 when it goes, unless null).
    struct Held {
        cIGZUnknown *mObject = nullptr;
        Held() = default;
        Held(const Held &) = delete;
        Held &operator=(const Held &) = delete;
        ~Held()
        {
            if (mObject) mObject->Release();
        }
    };
    // One run of the display list's item slots, -1 for none (freed when it goes).
    struct SlotRun {
        int32_t mStart = -1;
        SlotRun() = default;
        SlotRun(const SlotRun &) = delete;
        SlotRun &operator=(const SlotRun &) = delete;
        ~SlotRun();
    };
    // A run of `mCount` slots, none at first (the start is then not written).
    struct SlotRuns {
        int32_t mStart;
        uint32_t mCount = 0;
        SlotRuns() {}
        SlotRuns(const SlotRuns &) = delete;
        SlotRuns &operator=(const SlotRuns &) = delete;
        ~SlotRuns();
    };
    // The list at +0xb8: released when it goes (0x14c060).
    struct HeldList : RefPtrVector {
        HeldList() : RefPtrVector{nullptr, nullptr, nullptr} {}
        HeldList(const HeldList &) = delete;
        HeldList &operator=(const HeldList &) = delete;
        ~HeldList() { Destroy(); }
    };

    // 0xbb5010 / (0xbb54f0): the effect is not counted; the fields stay empty until Setup.
    explicit cTSParticlesRenderable(cTSParticlesEffect *effect) : mEffect(effect)
    {
        mParams[0] = mParams[1] = mParams[2] = mParams[3] = mParams[4] = 0;
    }
    ~cTSParticlesRenderable() override = default;

    // 15 (shared 0xa3a3b0, cTransformNode's): the clone of a particles node is a plain transform node.
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;
    // 30 / 50 (shared 0xa58940): the traversal's slot 7.
    uint32_t Accept(cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot7(this); }
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot7(this); }
    // 43 / 44 / 51 (shared 0xa37380, 0xa37360, 0xa37260).
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator51(void *out, uint32_t a) override;
    // 60 (shared 0xa59320 -> 0xae04b0): the renderable Impl's SetWorldTransform.
    void OnContentChanged(uint32_t transform, uint32_t b, uint32_t c) override
    {
        mImpl.SetWorldTransform(reinterpret_cast<const cRigidTransform *>(uintptr_t(transform)), b, c);
    }
    // 71 (0xbbca40) / 72 (0xbbcae0): the effect's bound, in `frame` or as it is.
    cBoundBox GetBoundBoxIn(const cTransform34 *frame) override;
    cBoundBox GetBoundBox() override;
    // 82 (0xbb65a0): with live particles and the bound not wholly outside the view (the mask the test leaves), the node
    // goes to the view's display list.
    void Draw(void *viewer, uint32_t planeMask) override;
    // 83 (0xbb58e0): the geometry and the meshes go to the view's display list, each with its material and item slots.
    void Slot83(void *viewer, void *unused) override;

    // 0xbbad30: the shape, the materials and the shader's parameters of a description.
    void Setup(cTSParticlesDescription *description);
    // 0xbc03d0: the material the description's texture, composite, draw mode and layer make; built once, by name.
    void BuildMaterial(cTSParticlesDescription *description);
    // 0xbb6670: with a shape, each particle has a child node: a new shape instance (the director's factory), layer 0x15 and
    // the settings' second, when it has one.
    void AddParticleChild();
    // 0xbc1d10: each frame, the particles' places, colours and sizes to those children, in step along both lists. Bumps the
    // effect's drawn count, unless the description's orientation kind is one with no case (those leave at once).
    void UpdateParticleChildren(cTSParticlesDescription *description);
    // 0xbbcef0: one such child goes.
    void RemoveParticleChild();

    // ---- cITSParticlesRenderable ----
    uint8_t GetRenderableKind() override { return 2; }
    uint32_t GetDetailLimit() override { return mImpl.mDetailLimit; }
    uint32_t GetWord6c() override { return mImpl.mZero6c; }
    void SetWord6c(uint32_t value) override { mImpl.mZero6c = value; }

    cTSParticlesEffect *mEffect;   // +0x7c not counted
    Held mShape;                       // +0x80 what a description naming a shape loaded
    ShapeMaterialHandle mMaterial;     // +0x84 the geometry's
    uint32_t mParams[7];               // +0x88 the shape's shader parameters; the constructor writes the first five
    Held mGeometry;                    // +0xa4 a cTSParticlesGeometry
    SlotRun mUnusedRun;                // +0xa8 nothing in the game fills it; the destructor frees it
    Held mMesh;                        // +0xac slot 30 answers a geometry
    ShapeMaterialHandle mVisBound;     // +0xb0 "VisBound_Particles"
    SlotRun mDrawSlot;                 // +0xb4 made on the first draw
    HeldList mMeshes;                  // +0xb8 each like mMesh
    ShapeMaterialHandle mVisCollider;  // +0xc4 "VisCollider"
    SlotRuns mMeshSlots;               // +0xc8 grown to the meshes' subsets

private:
    void LoadShape(cTSParticlesDescription *description);
    void ReadShapeParameters();
    friend struct cTSParticlesRenderableAdapters;
};
static_assert(sizeof(cTSParticlesRenderable) == cTSParticlesRenderable::kSize, "cTSParticlesRenderable size");
static_assert(offsetof(cTSParticlesRenderable, mEffect) == 0x7c && offsetof(cTSParticlesRenderable, mMaterial) == 0x84 &&
                  offsetof(cTSParticlesRenderable, mGeometry) == 0xa4 && offsetof(cTSParticlesRenderable, mVisBound) == 0xb0 &&
                  offsetof(cTSParticlesRenderable, mMeshes) == 0xb8 && offsetof(cTSParticlesRenderable, mMeshSlots) == 0xc8,
              "cTSParticlesRenderable layout");

// The hooks' entry points (src/hook/tables/cTSParticlesRenderable.cpp): original calling convention, our bodies.
struct cTSParticlesRenderableAdapters {
    using Self = cTSParticlesRenderable;
    static Self *__fastcall Construct(Self *memory, void *, cTSParticlesEffect *effect);
    static void __fastcall Destruct(Self *self, void *);
    static void __fastcall Setup(Self *self, void *, cTSParticlesDescription *description);
    static void __fastcall BuildMaterial(Self *self, void *, cTSParticlesDescription *description);
};

} // namespace nGZSceneGraph
