#pragma once
// nGZSceneGraph::cApplyFFLightStates (lane TM, scenegraph M3a): the shader pass part that hands the drawn item's lights
// to the fixed-function pipeline: one enabled-lights command (the count and the ambient colour), then one light command
// per directional, point and spot light. No parameters of its own (the template's 0x10 bytes). Whole in the standalone
// shape: the material's shader makes it through the COM director (clsid 0xec0819ae, the factory 0xac3140 hooked to make
// ours); the game reaches its bodies only through our vtable, so the image's constructor, destructor and slot bodies
// are in effect through the factory hook (db/status/cApplyFFLightStates.jsonl).
//
// The light counts come from a key word, nibble t the end of light type t's run in the item's light table (ambient 0,
// directional 1, point 2, spot 3; the ranges as cValidateRenderShaderContext reads them): word 0 of Init's and Slot12's
// key, the item's +0x30 in Apply.
#include "sg/SGShaderAttributes.h"

#include <cstdint>

namespace nGZSceneGraph {

// vtable 0xe76ea0, 0x10 bytes, factory 0xac3140.
class cApplyFFLightStates : public cShaderControllerT<cIApplyFFLightStates> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac3140

    // The light command's kinds (its +0xc).
    enum : uint32_t { kDirectional = 0, kPoint = 1, kSpot = 2 };

    cApplyFFLightStates() = default;           // 0xabd790
    ~cApplyFFLightStates() override = default; // 0xabe200 (deleting 0xabe8f0, slot 14)

    // 8 (0xac3b70): with `check`'s low byte set, the device must do fixed-function vertex processing in hardware and
    // allow as many enabled lights as the key's directional, point and spot lights (a report line for each failure
    // when `report`'s low byte is set; false); then the template's hash.
    bool Init(void *caps, void *key, void *check, void *report) override;
    uint32_t Slot10() override { return 1; }    // 10 (shared ReturnOne 0x15e510)
    void Apply(void *ctx, void *item) override; // 11 (0xabee20): the item's lights
    // 12 (0xac16a0): the same commands for the key's light counts, every light a white default.
    void Slot12(void *state, const void *key) override;
    uint32_t HashParams() override { return 0; }                         // 15 (shared ReturnZero 0x6c6b0)
    bool EqualParams(cIApplyFFLightStates *) override { return true; } // 16 (shared 0x148260)
};
static_assert(sizeof(cApplyFFLightStates) == 0x10);

} // namespace nGZSceneGraph
