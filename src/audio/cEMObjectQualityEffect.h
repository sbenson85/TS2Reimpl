#pragma once
// cEMObjectQualityEffect, an audio event modifier (lane WO; vtables 0xdad63c, +4 0xdad65c; subclass of cEventModifierBase, with
// cEMAliasModifier, cEMDestFootstep, cEMObjectQuality and others). Two curves map the 0/1 quality the event alternates between to
// its two filter values. Only the two slots of its own are replaced: the object is built by the original constructor 0x23f285,
// called by cTSAudioEventModManager's slot 3, and the constructor and destructors 0x23f3f0, 0x23f4b3 store the game's vtables
// of a base that is not ours yet, so they go with cEventModifierBase.
#include <cstddef>
#include <cstdint>

class cEMObjectQualityEffect {
public:
    static constexpr uint32_t kQualityFilterId = 0xabd5d0a2; // asked of the event, handed to the lookup
    static constexpr uint32_t kLowValueId = 0xac2b8c1c;
    static constexpr uint32_t kHighValueId = 0xac2b8c25;
    static constexpr double kCurveEnd = 20000.0;
    static constexpr double kLowCurveStart = 900.0;
    static constexpr double kHighCurveStart = 700.0;
    static constexpr uint32_t kCurveText = 0xdada58; // the format text, "0.0, %.2f, 1.0, %.2f"
    static constexpr uint32_t kQualityToggle = 0xff6cb0;

    bool Init();                                    // slot 3, 0x240418: the base's, then the two curves
    bool Apply(void *event, void *unused);          // slot 5, 0x2410fe: the toggle flipped, the two values set on the event

    uint8_t mBase[0x14];   // cEventModifierBase
    uint8_t mLowCurve[0x10];  // +0x14 an audio curve (0xc0a6a1 evaluates it, 0xc0a86a fills it from text)
    uint8_t mHighCurve[0x10]; // +0x24
};
static_assert(offsetof(cEMObjectQualityEffect, mLowCurve) == 0x14 && offsetof(cEMObjectQualityEffect, mHighCurve) == 0x24 &&
                  sizeof(cEMObjectQualityEffect) == 0x34,
              "cEMObjectQualityEffect layout");
