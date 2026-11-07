#pragma once
// cTSWaterEffect (lane WP, rendering row E2): the effect a water description makes (0x2c bytes, vtables 0xe91594, 0xe915d0).
// Bit 0 of the description's flags picks the kind: set, a water body (a director object under the owner's tree, built from the
// description's numbers and put at the effect's position); clear, a splash sent to the message server every period (or once,
// with a period of 0). RVAs 1.18.0.187; all /O2.
//
// Standalone shape (docs/standalone-shape.md): only Create 0xc09210 builds one (cTSWaterDescription::CreateEffect, ours), so
// every water effect carries our vtables and the slots need no adapters.
#include "ts/cTSEffectBase.h"

#include <cstddef>
#include <cstdint>

struct cTSWaterDescription;

class cTSWaterEffect : public cTSEffectBase {
public:
    static constexpr uint32_t kBodyClass = 0x4c181525; // the director's water body
    static constexpr uint32_t kBodyIid = 0x4c18151d;
    static constexpr uint32_t kSplashMessage = 0x4c8edbe5;

    explicit cTSWaterEffect(const cTSWaterDescription *description); // 0xc08bf0
    ~cTSWaterEffect() override;                                      // 0xc08c70; deleting 0xc08d00, +4 thunk 0xc08cf4
    static cITSComponentEffect *__cdecl Create(const cTSWaterDescription *description); // 0xc09210: null on failure

    void Attach(cIGZUnknown *owner) override;         // 0: shared 0x405100 (also another class's): the owner taken
    void Start(uint32_t a) override;                  // 2: 0xc091c0: running; a body is made now
    void Stop(uint32_t a) override;                   // 3: 0xc091f0: the body taken down
    bool IsRunning() override { return mRunning; }    // shared 0x150f10 (under 5 bytes)
    // 5: 0xc08f40. The first argument is the time step (a float's bits); the second is unused. A body: the step goes to it;
    // a splash: one message per period passed, or one and the effect stops when the period is 0.
    void AddToScene(void *step, void *unused) override;
    // 6: 0xc09080: the position (src/ts/EffectsPosition.h).
    void Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent) override;

    float mElapsed;                         // +0x14 time since the last splash
    float mPosition[3];                     // +0x18 never set by the constructor (from Update)
    fx::CountedRef<cIGZUnknown> mOwner;     // +0x24 slot 42 gives the tree the body goes under
    fx::CountedRef<cIGZUnknown> mBody;      // +0x28 the water body, while running with a body

private:
    void MakeBody();       // 0xc08d20
    void TakeBodyDown();   // 0xc09010: out of its parent, stopped, released
    void Splash(float a, float b); // 0xc08df0 (ret 8): a message with the position and the two numbers, in hundredths
};
static_assert(offsetof(cTSWaterEffect, mElapsed) == 0x14 && offsetof(cTSWaterEffect, mPosition) == 0x18 &&
                  offsetof(cTSWaterEffect, mOwner) == 0x24 && offsetof(cTSWaterEffect, mBody) == 0x28 &&
                  sizeof(cTSWaterEffect) == 0x2c,
              "cTSWaterEffect layout");
