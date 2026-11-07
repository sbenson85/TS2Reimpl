#pragma once
// The effects' component base (lane WO, rendering row E1): what a description's slot 1 (cITSComponentDescription::
// CreateEffect) makes. The game's RTTI: cTSEffectBase over cITSComponentEffect +0 and cRZRefCount +4; every effect's
// constructor and destructor store its vtables (0xe8bd68, 14 slots; 0xe8bda4, 5 slots): particles, meta-particles, sound,
// water, sequence, model, camera, screen, light, decal, and cTSVisualEffect at +4 of its own object.
//
// Layout (0x14 bytes): +0 cITSComponentEffect's vtable, +4 cRZRefCount (its vtable, the count at +8), +0xc the description,
// +0x10 the running flag. Slots 0, 1 and 5..11 have no-op defaults, shared with unrelated classes in the image; 2..4 are
// pure in the base, 12 / 13 count through the +4 face. RVAs 1.18.0.187; all /O2.
//
// Slot names past Update are ours and tentative: the callers are still the original cTSVisualEffect.
#include "gz/cIGZUnknown.h"
#include "rz/RZRefCountBase.h"
#include "sg/SGNodeInterfaces.h"
#include "ts/CountedRef.h"
#include "ts/EffectsTransform.h"

#include <cstddef>
#include <cstdint>

class cITSComponentEffect {
public:
    // Out of line, so a derived destructor stores this class's vtable as the game's does (an inline empty one is dead-store
    // eliminated, and the destroyed object would keep its own table).
    ~cITSComponentEffect();
    virtual void Attach(cIGZUnknown *owner) = 0;                    // 0 (ret 4): the owner; a node effect makes its node
    virtual void Detach() = 0;                                      // 1
    virtual void Start(uint32_t a) = 0;                             // 2 (ret 4)
    virtual void Stop(uint32_t a) = 0;                              // 3 (ret 4)
    virtual bool IsRunning() = 0;                                   // 4
    virtual void AddToScene(void *a, void *b) = 0;                  // 5 (ret 8)
    // 6 (ret 8): the effect placed by `local` inside `parent`.
    virtual void Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent) = 0;
    virtual bool HandleEvent(uint32_t kind, void *a, void *b) = 0;  // 7 (ret 0xc)
    virtual bool Query(void *a, void *b) = 0;                       // 8 (ret 8)
    virtual void SetFlagged(bool value) = 0;                        // 9 (ret 4): the light: its node's persisted flag 10
    virtual void Slot10(uint32_t a) = 0;                            // 10 (ret 4)
    virtual void Slot11(uint32_t a) = 0;                            // 11 (ret 4)
    virtual uint32_t AddRef() = 0;                                  // 12
    virtual uint32_t Release() = 0;                                 // 13
};

// The base. An abstract class: Start, Stop and IsRunning stay pure. A concrete effect overrides what the table in
// docs/subsystems/rendering.md says it overrides, and needs no vtable of this class (ours differs from 0xe8bd68 only in its
// bodies' addresses: a destroyed effect's vtable words are compared through tests/shape.h's VtCode).
class cTSEffectBase : public cITSComponentEffect, public rz::cRZRefCount {
public:
    explicit cTSEffectBase(const void *description) : mDescription(description), mRunning(false) {}
    // 0xbb5270's work inlined in every effect's destructor (the base vtables, then the count's destructor). The deleting
    // destructor 0xbb5830 and its +4 thunk 0xbb5759 are reached by no object: the class is abstract.
    ~cTSEffectBase() override;

    void Attach(cIGZUnknown *) override {}                 // shared 0x300a0 (under 5 bytes)
    void Detach() override {}                              // shared 0x140fe0 (under 5 bytes)
    void AddToScene(void *, void *) override {}            // shared 0x1b9a50 (under 5 bytes)
    void Update(const fx::EffectsTransform *, const fx::EffectsTransform *) override {} // shared 0x1b9a50
    bool HandleEvent(uint32_t, void *, void *) override { return false; }               // shared 0x1474a0
    bool Query(void *, void *) override { return false; }  // shared 0x1474b0
    void SetFlagged(bool) override {}                      // shared 0x300a0
    void Slot10(uint32_t) override {}                      // shared 0x300a0
    void Slot11(uint32_t) override {}                      // shared 0x300a0
    uint32_t AddRef() override { return rz::cRZRefCount::AddRef(); }   // shared 0x1dab20
    uint32_t Release() override { return rz::cRZRefCount::Release(); } // shared 0x5b7980

    const void *mDescription; // +0xc the description the effect was made from (not owned)
    bool mRunning;            // +0x10
};
static_assert(offsetof(cTSEffectBase, mDescription) == 0xc && offsetof(cTSEffectBase, mRunning) == 0x10 &&
                  sizeof(cTSEffectBase) == 0x14,
              "cTSEffectBase layout");

// What cTSLightEffect and cTSModelEffect have in common (no class of the game's: the linker folded their bodies, referenced
// from these two vtables alone, 0xe918dc and 0xe9139c): an owner and a scene-graph node the effect shows itself with.
class cTSNodeEffectBase : public cTSEffectBase {
public:
    explicit cTSNodeEffectBase(const void *description) : cTSEffectBase(description) {}
    ~cTSNodeEffectBase() override = default; // the node, then the owner; then the base

    void Start(uint32_t a) override;                     // shared 0xc06bb0: running set
    void Stop(uint32_t a) override;                      // shared 0xc06bc0: running cleared, the node out of its parent
    bool IsRunning() override { return mRunning; }       // shared 0x150f10 (under 5 bytes)
    void AddToScene(void *a, void *b) override;          // shared 0xc05ad0: the node under the owner's, when it has no parent
    // shared 0xc069e0: the node's rotation (the product of the two), then its translation (`local`'s through `parent`).
    void Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent) override;

protected:
    // The owner taken (a no-op for the same one) and the node recreated from class `classId` asked for `iid`, 0xc0a080's
    // work; cTSModelEffect's slot 0 starts the same way.
    void AttachNode(cIGZUnknown *owner, uint32_t classId, uint32_t iid);

public:
    fx::CountedRef<cIGZUnknown> mOwner;                    // +0x14 slot 42 gives the node the effect's node goes under
    fx::CountedRef<nGZSceneGraph::cITransformNode> mNode;  // +0x18
};
static_assert(offsetof(cTSNodeEffectBase, mOwner) == 0x14 && offsetof(cTSNodeEffectBase, mNode) == 0x18 &&
                  sizeof(cTSNodeEffectBase) == 0x1c,
              "cTSNodeEffectBase layout");
