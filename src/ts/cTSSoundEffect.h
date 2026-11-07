#pragma once
// cTSSoundEffect (lane WP, rendering row E2): the effect a sound description makes (0x30 bytes, vtables 0xe9132c, 0xe91368):
// one sound-renderer message to start the sound, one per repeat period, one to stop it, and the position the effect is at.
// RVAs 1.18.0.187; all /O2.
//
// Not a cTSEffectBase: the game's class is, but its position (+0x10..+0x18) lies over the base's running flag, and the
// running flag is its own (+0x1c). So the class derives from the two bases the base class does and repeats its defaults.
//
// Standalone shape (docs/standalone-shape.md): only Create 0xc05670 builds one (cTSSoundDescription::CreateEffect, ours), so
// every sound effect carries our vtables and the slots need no adapters.
#include "gz/cIGZUnknown.h"
#include "rz/RZRefCountBase.h"
#include "ts/cTSEffectBase.h"

#include <cstddef>
#include <cstdint>

struct cTSSoundDescription;

class cTSSoundEffect : public cITSComponentEffect, public rz::cRZRefCount {
public:
    explicit cTSSoundEffect(const cTSSoundDescription *description); // 0xc05090
    ~cTSSoundEffect() override = default;                           // 0xc05110: the service released; deleting 0xc05180, +4 thunk 0xc05176
    static cITSComponentEffect *__cdecl Create(const cTSSoundDescription *description); // 0xc05670: null on failure

    void Attach(cIGZUnknown *) override {}                 // shared 0x300a0 (under 5 bytes)
    void Detach() override {}                              // shared 0x140fe0 (under 5 bytes)
    void Start(uint32_t a) override;                       // 2: 0xc05410: the start message
    void Stop(uint32_t a) override;                        // 3: 0xc05580: the stop message, the service dropped
    bool IsRunning() override { return mRunning; }         // shared 0x2279f0 (under 5 bytes)
    // 5: 0xc051a0. The first argument is the time step (a float's bits); the second is unused. Adds the step to the elapsed
    // time, sends the repeat message when the period has passed, and stops the sound (slot 3, flag 1) after the duration.
    void AddToScene(void *step, void *unused) override;
    // 6: 0xc052d0: the position, `parent`'s rotation applied to its scale times `local`'s translation, plus `parent`'s.
    void Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent) override;
    bool HandleEvent(uint32_t, void *, void *) override { return false; } // shared 0x1474a0
    bool Query(void *, void *) override { return false; }                  // shared 0x1474b0
    void SetFlagged(bool) override {}                                      // shared 0x300a0
    void Slot10(uint32_t) override {}                                      // shared 0x300a0
    void Slot11(uint32_t) override {}                                      // shared 0x300a0
    uint32_t AddRef() override { return rz::cRZRefCount::AddRef(); }       // shared 0x1dab20
    uint32_t Release() override { return rz::cRZRefCount::Release(); }     // shared 0x5b7980

    const cTSSoundDescription *mDescription; // +0x0c
    float mPosition[3];                      // +0x10 never set by the constructor (the sound's place, from Update)
    bool mRunning;                           // +0x1c
    uint32_t mHandle;                        // +0x20 the effect's own address, the sound's id in every message
    float mElapsed;                          // +0x24 time since Start
    float mNextRepeat;                       // +0x28 the elapsed time the next repeat message is due after
    fx::CountedRef<cIGZUnknown> mService;    // +0x2c the sound renderer's probe, while running
};
static_assert(offsetof(cTSSoundEffect, mDescription) == 0xc && offsetof(cTSSoundEffect, mPosition) == 0x10 &&
                  offsetof(cTSSoundEffect, mRunning) == 0x1c && offsetof(cTSSoundEffect, mService) == 0x2c &&
                  sizeof(cTSSoundEffect) == 0x30,
              "cTSSoundEffect layout");
