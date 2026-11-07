#pragma once
// cTSSequenceEffect (lane WP, rendering row E2): the effect a sequence description makes (0xb0 bytes, vtables 0xe91494,
// 0xe914d0): a list of named effects started one after another, each after a (random) wait or after the last one has ended.
// The named effects come from the effects manager's factory (the owner the effect is attached to: slot 14 of what Attach
// gets, a name in, a counted effect out), and the effect passes its two placements on to each. RVAs 1.18.0.187; all /O2.
//
// Not a cTSEffectBase: the game's class is, but its flag at +0x11 lies in the base's tail padding, which MSVC would not let
// a derived member use. So the class derives from the two bases the base class does and repeats its defaults.
//
// The effects it starts are objects of a class this file only names by slots (cIGZUnknown plus): 3 start(flag), 4 stop(flag),
// 5 is-running, 8 set-flag, 9 / 10 take a placement.
//
// Standalone shape (docs/standalone-shape.md): only Create 0xc07490 builds one (cTSSequenceDescription::CreateEffect, ours),
// so every sequence effect carries our vtables and the slots need no adapters.
#include "gz/cIGZUnknown.h"
#include "rz/RZRefCountBase.h"
#include "ts/cTSEffectBase.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct cTSSequenceDescription;

class cTSSequenceEffect : public cITSComponentEffect, public rz::cRZRefCount {
public:
    explicit cTSSequenceEffect(const cTSSequenceDescription *description); // 0xc06c60
    // 0xc06db0: the effects stopped (slot 3, 1) and every counted pointer dropped; deleting 0xc06e50, +4 thunk 0xc06e41
    ~cTSSequenceEffect() override;
    static cITSComponentEffect *__cdecl Create(const cTSSequenceDescription *description); // 0xc07490: null on failure

    void Attach(cIGZUnknown *factory) override;           // 0: 0xc071d0: the factory counted
    void Detach() override {}                             // shared 0x140fe0 (under 5 bytes)
    void Start(uint32_t a) override;                      // 2: 0xc07310
    void Stop(uint32_t how) override;                     // 3: 0xc07360
    bool IsRunning() override;                            // 4: 0xc07210
    void AddToScene(void *step, void *unused) override;   // 5: 0xc06e80 (the first argument is the time step: a float's bits)
    void Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent) override; // 6: 0xc07290
    bool HandleEvent(uint32_t, void *, void *) override { return false; } // shared 0x1474a0
    bool Query(void *, void *) override { return false; }                  // shared 0x1474b0
    void SetFlagged(bool value) override;                 // 9: 0xc07230
    void Slot10(uint32_t) override {}                     // shared 0x300a0
    void Slot11(uint32_t) override {}                     // shared 0x300a0
    uint32_t AddRef() override { return rz::cRZRefCount::AddRef(); }       // shared 0x1dab20
    uint32_t Release() override { return rz::cRZRefCount::Release(); }     // shared 0x5b7980

    const cTSSequenceDescription *mDescription; // +0x0c
    bool mRunning;                              // +0x10
    bool mRestart;                              // +0x11 the next Start begins again from the first entry
    float mElapsed;                             // +0x14 time since the sequence began
    uint32_t mIndex;                            // +0x18 the entry in turn
    float mNext;                                // +0x1c the elapsed time the next entry starts at
    bool mWaitForEffect;                        // +0x20 the entry in turn waits for its predecessor to end, not for a time
    bool mHidden;                               // +0x21 the flag slot 9 was given (the effects are told its opposite)
    fx::EffectsTransform mLocal;                // +0x24 the placement slot 6 was given
    fx::EffectsTransform mParent;               // +0x5c
    uint32_t mLoop;                             // +0x94 bit 1 of the description's flags
    uint32_t mStopOld;                          // +0x98 bit 2 of the description's flags
    fx::CountedRef<cIGZUnknown> mCurrent;       // +0x9c the effect started last
    std::vector<fx::CountedRef<cIGZUnknown>> mFinishing; // +0xa0 the ones before it that still run
    fx::CountedRef<cIGZUnknown> mFactory;       // +0xac

private:
    void DropFinished(); // 0xc070d0: the ones that no longer run released, the rest kept in order
};
static_assert(offsetof(cTSSequenceEffect, mDescription) == 0xc && offsetof(cTSSequenceEffect, mRunning) == 0x10 &&
                  offsetof(cTSSequenceEffect, mElapsed) == 0x14 && offsetof(cTSSequenceEffect, mLocal) == 0x24 &&
                  offsetof(cTSSequenceEffect, mParent) == 0x5c && offsetof(cTSSequenceEffect, mLoop) == 0x94 &&
                  offsetof(cTSSequenceEffect, mCurrent) == 0x9c && offsetof(cTSSequenceEffect, mFinishing) == 0xa0 &&
                  offsetof(cTSSequenceEffect, mFactory) == 0xac && sizeof(cTSSequenceEffect) == 0xb0,
              "cTSSequenceEffect layout");
