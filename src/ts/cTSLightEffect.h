#pragma once
// cTSLightEffect (lane WO, rendering row E1): the effect a light description makes (0x2c bytes, vtables 0xe918dc, 0xe91918):
// a light-reference node put under the owner's node and moved with the effect. RVAs 1.18.0.187; all /O2.
//
// Standalone shape (docs/standalone-shape.md): only Create 0xc0a140 builds one (cTSLightDescription::CreateEffect, ours), so
// every light effect carries our vtables and the slots need no adapters: they are in effect through the Create hook.
#include "ts/cTSEffectBase.h"

#include <cstddef>
#include <cstdint>

struct cTSLightDescription;

class cTSLightEffect : public cTSNodeEffectBase {
public:
    static constexpr uint32_t kNodeClass = 0x253d2018; // the director's cLightRefNode
    static constexpr uint32_t kNodeIid = 0x053d200a;   // cILightRefNode

    explicit cTSLightEffect(const cTSLightDescription *description); // 0xc09f40
    ~cTSLightEffect() override = default;                            // 0xc09fd0; deleting 0xc0a060, +4 thunk 0xc0a049
    static cITSComponentEffect *__cdecl Create(const cTSLightDescription *description); // 0xc0a140: null on failure

    void Attach(cIGZUnknown *owner) override;                     // 0: 0xc0a080
    void Detach() override;                                       // 1: 0xc0a120: the node dropped
    bool HandleEvent(uint32_t kind, void *a, void *b) override;   // 7: 0xc0a0e0: true for kinds 3 and 4
    void SetFlagged(bool value) override;                         // 9: 0xc0a100

    float mColor[4]; // +0x1c all 1 from the constructor, read by no slot
};
static_assert(offsetof(cTSLightEffect, mColor) == 0x1c && sizeof(cTSLightEffect) == 0x2c, "cTSLightEffect layout");
