#pragma once
// cTSVisualEffect (lane XD, rendering row E10): the pool's effect, 0x190 bytes, made by the effects manager's pool factory
// 0xbc9ab0 (operator new(400), then the constructor 0xbe86a0). It carries an effects description (a particle, sound, model... tree
// of component effects, its children) and runs them together under one placement: four placement blocks, a table of event
// values the description's events fill, four resource pointers (the table is made with four), the camera the billboards face, the owner (the effects manager).
//
// Three vtables: face 0 0xe8f7e0 (cITSVisualEffect: the 24 slots below), +4 0xe8f844 (cITSComponentEffect, the 14 slots the
// descriptions' effects have), +8 0xe8f880 (cRZRefCount; its deleting destructor 0xbe8be0, thunk 0xbe8bcf). It is NOT built on
// cTSEffectBase's data (0xc description, 0x10 running): the constructor stores that class's vtables first, but the object's own
// count is at +0x10, the rz count (+0xc) never used. Its AddRef / Release are one body for the three faces (0x146a40, 0xbe9560;
// the +4 and +8 faces' are thunks with this-4 and this-8).
//
// Layout, from the object's start; slots called through the +4 face run with `this`+4 (the decompiled offsets there are 4 low).
// Update (0xbe99c0: the placement maths), slot 14 (0xbea700: the resources), the camera follow (0xbead40) and the event index
// walk (0xbe9590) are ours; only Start's sound object (service 0x9c5a89, constructor 0x1c033d) is still the original's.
#include "gz/cIGZUnknown.h"
#include "rz/RZRefCountBase.h"
#include "sg/cTextureManager.h"
#include "ts/CountedRef.h"
#include "ts/EffectsTransform.h"
#include "ts/cTSEffectBase.h"
#include "ts/cTSEffectsManager.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The description a visual effect was made from: only its flag word is read here (bit 2 camera, 4 stop at once, 6 ignore events,
// 14 starts a sound object, ...); the rest belongs to the description's class.
struct cTSVisualDescription {
    uint32_t flags;
};

struct EffectEntry;

// One child of the effect (0x54 bytes): a component effect with the placement it runs at inside the effect's.
struct cTSVisualChild {
    fx::EffectsTransform placement; // +0 the child's placement in the effect's
    cITSComponentEffect *effect;    // +0x38 counted: Detached when the effect is, released with the record
    uint32_t unknown3c;
    float weight;                   // +0x40 scales the time the child is added to the scene with
    uint32_t flags;                 // +0x44 bit 3: takes event indexes, bit 5: left out of events
    std::vector<int32_t> indexes;   // +0x48

    // The walk's new child (0xbcb4c0): the identity placement, no effect, no indexes. The byte at +1 unset, the weight zero
    // (unset there, and written before it is read).
    cTSVisualChild();
    // 0xbc4540: the placement's version zero and its byte at +1 left alone, the effect counted, the indexes copied.
    cTSVisualChild(const cTSVisualChild &other);
    // 0xbc41e0 is the range of these (for a middle insert the vector never does: at its end it copies, 0xbc30e0, as this may
    // throw): as the copy, with the indexes taken. The source keeps its effect, counted once more.
    cTSVisualChild(cTSVisualChild &&other);
    // 0xbc57a0: every placement byte (the version too), the effect counted, the indexes assigned.
    cTSVisualChild &operator=(const cTSVisualChild &other);
    // 0xbc5380: the indexes freed, then the effect released.
    ~cTSVisualChild();
};
static_assert(sizeof(cTSVisualChild) == 0x54, "cTSVisualChild layout");

// The event table's entry for one kind (kinds 0..0x10): where the kind's values are in the value vector, and how many.
struct cTSVisualEntry {
    uint16_t offset;
    uint16_t count;
};
static_assert(sizeof(cTSVisualEntry) == 4);

class cTSVisualEffect : public cITSVisualEffect, public cITSComponentEffect, public rz::cRZRefCount {
public:
    static constexpr uint32_t kSize = 0x190;
    static constexpr uint32_t kOriginalVTable = 0xe8f7e0;
    static constexpr uint32_t kOriginalVTable4 = 0xe8f844;
    static constexpr uint32_t kOriginalVTable8 = 0xe8f880;
    static constexpr uint32_t kInterfaceId = 0x4c7af090;

    cTSVisualEffect();           // 0xbe86a0
    ~cTSVisualEffect() override; // 0xbe8a40; deleting 0xbe8be0 (the +8 face's slot 0, thunk 0xbe8bcf)

    // ---- face 0 (cITSVisualEffect) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0xbe9530: the effect's id or interface 1, counted
    uint32_t AddRef() override;                              // 1: shared 0x146a40 (all three faces)
    uint32_t Release() override;                             // 2: 0xbe9560 (thunks 0xbe9578 +4, 0xbe9580 +8)
    void Slot3(uint32_t a) override;                         // 3: 0xbeab90 Start: the children, a sound object, running
    void Slot4(uint32_t a) override;                         // 4: 0xbeacd0 Stop
    bool Slot5() override { return mRunning != 0; }          // 5: 0xbe92c0 IsRunning
    bool Slot6() override { return mStopping != 0; }         // 6: shared 0x78e3d0: a stop is wanted once the effects end
    void SetSuspended(bool on) override;                     // 7: 0xbe9910: effects flag bit 0
    void SetHidden(bool on) override;                        // 8: 0xbe98e0: through the +4 face's SetFlagged, inverted
    // 9 (0xbe96e0, ret 4): Update with this effect's block B placed in `parent`; 10 (0xbe99a0): block A under `local`.
    virtual void UpdateInside(const fx::EffectsTransform *parent);
    virtual void UpdateFrom(const fx::EffectsTransform *local);
    virtual void SetParameters(uint32_t a, uint32_t b);      // 11: shared 0x332750 (ret 8): +0x48, +0x4c
    virtual bool EventAlias(uint32_t kind, const float *values, uint32_t count); // 12: 0xbea9e0: slot 13 with the count (of vec3s) times 3
    // 13: 0xbe9700 (ret 0xc): kind's values stored (the same count every time), the children told. Kind 0x101: the two blend
    // values (+0x184, +0x188). True when a child took the event.
    virtual bool Event(uint32_t kind, const float *values, uint32_t count);
    // 14: 0xbea700 (ret 8): kind 2 the camera node (when the description does not forbid it), kind 0 also the texture named by
    // the resource's string; the resource is kept in the table and offered to the children. True when one took it.
    virtual bool SetResource(uint32_t kind, cIGZUnknown *resource);
    virtual uint32_t *Values(uint32_t kind, void *unused, uint32_t *count);  // 15: 0xbe90e0: the values when their count is a multiple of 3 (count / 3)
    virtual uint32_t *ValuesRaw(uint32_t kind, void *unused, uint32_t *count); // 16: 0xbe9010: the values and their count
    virtual cIGZUnknown *Resource(uint32_t kind);            // 17: 0xbe90b0: kind < 5
    virtual fx::EffectsTransform *GetBlockA(fx::EffectsTransform *out);     // 18: 0xbe8fe0
    virtual fx::EffectsTransform *GetBlockB(fx::EffectsTransform *out);     // 19: 0xbe9070
    virtual bool IsFlag0();                                  // 20: 0xbe92b0: effects flag bit 0
    virtual bool IsFlag1();                                  // 21: 0xbe92a0: effects flag bit 1
    virtual bool IsEventSet();                               // 22: 0xbe9260 (see the body)
    virtual void SetEvent16(bool on);                        // 23: 0xbe9640: event 0x10 with 0 or 1.0

    // ---- face +4 (cITSComponentEffect) ----
    void Attach(cIGZUnknown *owner) override;                // 0: 0xbe9150
    void Detach() override;                                  // 1: 0xbeaa00
    void Start(uint32_t a) override { Slot3(a); }            // 2: thunk 0xbeacc5
    void Stop(uint32_t a) override { Slot4(a); }             // 3: thunk 0xbead2d
    bool IsRunning() override { return mRunning != 0; }      // 4: thunk 0xbe92c4
    void AddToScene(void *time, void *counters) override;   // 5: 0xbe8c10 (a float and a counter pair)
    void Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent) override; // 6: 0xbe99c0
    bool HandleEvent(uint32_t kind, void *values, void *count) override { return Event(kind, static_cast<const float *>(values), reinterpret_cast<uintptr_t>(count)); } // 7: thunk 0xbe98cb
    bool Query(void *kind, void *resource) override;         // 8: thunk 0xbea9d8 to slot 14
    void SetFlagged(bool value) override;                    // 9: 0xbe9930
    void Slot10(uint32_t) override {}                        // 10, 11: shared 0x300a0
    void Slot11(uint32_t) override {}

    // 0xbead40 (this = the face 0): the camera's placement into block B and the children updated, when it moved or the
    // description asks for it every frame. The effects manager calls it when it gets a camera.
    void Follow();
    // 0xbe9590: the event index walk: every child that takes indexes gets the description at `index` in its list.
    void WalkIndex(int32_t index);
    // 0xbe8c80: the stored events and resources told to the children again (the effects manager does it after it rebuilt the
    // children, 0xbcb4c0); the stored index (event 0xf) walked on top.
    void ReplayEvents();
    // 0xbe9670 (ret 4): the effect made from the manager's entry: the description, the entry's message pair (+0xc, +0x10), and
    // with description bit 2 the owner's camera. The manager's entries are a cTSVisualDescription by their flag word.
    void Describe(const EffectEntry *entry);
    // 0xbe8de0: not running (or with no index) it is done; otherwise not while it waits for its stop (description bit 3) or
    // a child that counts (flag bit 0 clear) still runs. Once done it stops, and its message goes out (id +0x48, value +0x4c).
    bool IsDone();

    // The object's fields.
    uint32_t mRefs;                          // +0x10 the effect's count (not the rz count at +0xc)
    std::string mName;                       // +0x14
    uint32_t mUnknown2c;                     // +0x2c
    uint32_t mUnknown30;                     // +0x30
    int32_t mEffectIndex;                    // +0x34 the manager's index of the effect (-1 for none)
    uint8_t mUnknown38;                      // +0x38 (the manager tests it)
    uint8_t mUpdated;                        // +0x39 set at the end of Update, cleared by Start
    uint8_t mFlag3a;                         // +0x3a set by Start, cleared by AddToScene; the constructor leaves it alone
    uint8_t mRunning;                        // +0x3b
    uint8_t mStopping;                       // +0x3c
    uint8_t mPadding3d[3];                   // +0x3d left alone by the constructor
    const cTSVisualDescription *mDescription; // +0x40 null until the effect is made from one
    uint32_t mFlags;                         // +0x44 bit 0: owner kind 2 or 3, bit 1: kind 3 / hidden, bit 2: an index step waits
    uint32_t mParameter48;                   // +0x48
    uint32_t mParameter4c;                   // +0x4c
    fx::EffectsTransform mBlockA;            // +0x50
    fx::EffectsTransform mBlockB;            // +0x88
    fx::EffectsTransform mBlockC;            // +0xc0
    fx::EffectsTransform mBlockD;            // +0xf8
    std::vector<cTSVisualChild> mChildren;   // +0x130
    std::vector<cTSVisualEntry> mEntries;    // +0x13c 17 entries once an event arrived
    std::vector<uint32_t> mValues;           // +0x148 the event values, entry after entry
    std::vector<fx::CountedRef<cIGZUnknown>> mResources; // +0x154 five once a resource arrived
    nGZSceneGraph::cTextureHandle mTexture;  // +0x160
    fx::CountedRef<cIGZUnknown> mCamera;     // +0x164 the camera node
    float mFollow[7];                        // +0x168 the camera's last placement (Follow's cache: x, y, z, then the rotation x y z w)
    float mBlendA;                           // +0x184 -1.0 until event 0x101
    float mBlendB;                           // +0x188 -1.0
    fx::CountedRef<cITSEffectsManager> mOwner; // +0x18c
};
static_assert(offsetof(cTSVisualEffect, mRefs) == 0x10 && offsetof(cTSVisualEffect, mName) == 0x14 &&
                  offsetof(cTSVisualEffect, mEffectIndex) == 0x34 && offsetof(cTSVisualEffect, mRunning) == 0x3b &&
                  offsetof(cTSVisualEffect, mDescription) == 0x40 && offsetof(cTSVisualEffect, mBlockA) == 0x50 &&
                  offsetof(cTSVisualEffect, mBlockD) == 0xf8 && offsetof(cTSVisualEffect, mChildren) == 0x130 &&
                  offsetof(cTSVisualEffect, mEntries) == 0x13c && offsetof(cTSVisualEffect, mValues) == 0x148 &&
                  offsetof(cTSVisualEffect, mResources) == 0x154 && offsetof(cTSVisualEffect, mTexture) == 0x160 &&
                  offsetof(cTSVisualEffect, mCamera) == 0x164 && offsetof(cTSVisualEffect, mFollow) == 0x168 && offsetof(cTSVisualEffect, mBlendA) == 0x184 &&
                  offsetof(cTSVisualEffect, mOwner) == 0x18c && sizeof(cTSVisualEffect) == 0x190,
              "cTSVisualEffect layout");

// The RVA hook for the constructor (the pool factory 0xbc9ab0 is its only caller): every effect the pool makes is ours.
struct cTSVisualEffectAdapters {
    static cTSVisualEffect *__fastcall Construct(void *memory, void *);
    static void __fastcall DestructChild(cTSVisualChild *child, void *); // 0xbc5380
};
