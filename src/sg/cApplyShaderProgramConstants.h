#pragma once
// nGZSceneGraph::cApplyShaderProgramConstants (lane TM, scenegraph M3a): the shader pass part that fills the vertex and
// pixel program constants the material binds (bindShaderProgramConstants). Each binding names a source (what the
// constants hold), a register file and a register range; the part keeps the bindings and, per file, the lowest and
// highest register any binding touches. Apply locks each used file's range once on the device state and fills it, the
// float and integer bindings through the two source functions (FillFloatConstants 0xaba8f0, FillIntConstants
// 0xab7cd0, sg/ShaderConstantSources.h), the boolean ones itself. Whole in the standalone shape: the material's shader makes it through the COM
// director (the factory 0xac3200 hooked to make ours); the game reaches its bodies only through our vtable, so the
// image's constructor, destructor and slot bodies are in effect through the factory hook
// (db/status/cApplyShaderProgramConstants.jsonl).
#include "sg/SGShaderAttributes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

// vtable 0xe76afc, 0x4c bytes, factory 0xac3200.
class cApplyShaderProgramConstants : public cShaderControllerT<cIApplyShaderProgramConstants> {
public:
    static cIGZUnknown *__cdecl Create(); // 0xac3200

    // The register files (a binding's mFile): the vertex program's float, integer and boolean constants, then the
    // pixel program's.
    enum : uint8_t { kVertexFloat = 0, kVertexInt = 1, kVertexBool = 2, kPixelFloat = 3, kPixelInt = 4, kPixelBool = 5 };
    static constexpr uint32_t kFiles = 6;

    // One binding (0x20 bytes, compared and hashed whole).
    struct Binding {
        uint8_t mSource;    // +0: what the constants hold (Init checks the count each source allows)
        uint8_t mFile;      // +1
        uint8_t mChannel;   // +2: source 0x19's first channel
        uint8_t mDimension; // +3: source 0x19's data dimension
        uint32_t mRegister; // +4: the first register
        uint32_t mCount;    // +8: registers
        int32_t mOffset;    // +0xc: source 0x19's attribute data offset (negative: none)
        float mValues[4];   // +0x10: source 0x18's four values (and 0x19's without item values, 0xe's [0]); a
                            // boolean binding's value is [0] (true when nonzero)
    };
    static_assert(sizeof(Binding) == 0x20);

    // A register file's range: the lowest first register and the highest end of the bindings in it (empty: end 0).
    struct Range {
        uint32_t mBegin = 0x7fffffff;
        uint32_t mEnd = 0;
    };

    cApplyShaderProgramConstants() = default;           // 0xabd8c0
    ~cApplyShaderProgramConstants() override = default; // 0xabe250 (deleting 0xabe970, slot 15)

    // 8 (0xac4180): the register files' ranges against the caps' limits (the vertex ones by `software`, the
    // software or hardware vertex processing limits; tentative), then each binding's count against its source's (a
    // report line for the first failure when `report`'s low byte is set; false), then the template's hash.
    bool Init(void *caps, void *key, void *software, void *report) override;
    uint32_t Slot10() override;                 // 10 (0xac2b10): 1 when any file has a range
    void Apply(void *ctx, void *item) override; // 11 (0xabf500)
    void Slot12(void *state, const void *key) override; // 12 (0xac1930): every used range zeroed
    void AddConstant(const void *binding) override;     // 14 (0xabeae0)
    uint32_t HashParams() override;                                  // 16 (0xac3560)
    bool EqualParams(cIApplyShaderProgramConstants *other) override; // 17 (0xac5380)

    Range mRanges[kFiles];          // +0x10
    std::vector<Binding> mBindings; // +0x40
};
static_assert(sizeof(cApplyShaderProgramConstants) == 0x4c);
static_assert(offsetof(cApplyShaderProgramConstants, mRanges) == 0x10);
static_assert(offsetof(cApplyShaderProgramConstants, mBindings) == 0x40);

} // namespace nGZSceneGraph
