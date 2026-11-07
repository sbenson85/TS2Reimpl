#pragma once
#include <cstdint>

// nTSSG::cDiffusionLightExtension (a lighting extension of scene-graph nodes). Its secondary subobject
// nTSSG::cIDiffusionLightExtension (vtable 0xdb6bdc) is the `this` of the functions here, as the game passes it.
// Each of these is a vtable slot entered through a vtordisp thunk: the dword before the subobject's vtable
// pointer (this - 4) is the displacement to the full object F, so F = this - [this - 4]. The bodies then touch
// the full object's fields at the negative offsets below.
//
// Fields (offsets from F; "flag" is a byte that is 0 until the matching setter runs):
//   -0x64/-0x60  8-byte value P1, flag -0x5c   (SetPairP1; GetPointerP1OrD11 returns it when the flag is set)
//   -0x58/-0x54  8-byte value P2, flag -0x50   (SetPairP2 - not reimplemented yet)
//   -0x4c        float F3, flag -0x48          (SetFloatF3; GetFloatF3OrDefault returns it when set, else -0x8c)
//   -0x44/-0x3c  12-byte value V4, flag -0x38  (SetVec3V4; GetVec3V4OrD14 returns &V4 when set, else &-0x88)
//   -0x37        byte B5, flag -0x36           (SetByteB5; GetByteB5OrD15 returns B5 when set, else -0x7c)
//   -0x34        float F6, flag -0x30          (SetFloatF6 - not yet)
//   -0x2c        float F7, flag -0x28          (SetFloatF7 - not yet)
//   -0x24        float F8, flag -0x20          (SetFloatF8)
//   -0x1c        float F9, flag -0x18          (SetFloatF9 - not yet)
//   -0x14        float F10, flag -0x10         (SetFloatF10)
//   -0x9c/-0x98  8-byte default D11            (SetPairD11; the GetPointerP1OrD11 fallback)
//   -0x94/-0x90  8-byte default D12            (SetPairD12 - not yet)
//   -0x88/-0x80  12-byte default D14           (SetVec3D14; the GetVec3V4OrD14 fallback)
//   -0x7c        byte default D15              (SetByteD15; the GetByteB5OrD15 fallback)
// Floats are passed and stored as their 32-bit patterns, so NaN payloads survive as they do in the original.
namespace nTSSG {

class cIDiffusionLightExtensionSub {
public:
    // 0x2ea27b (`ret`, 62 bytes incl. the thunk): clears the flags of P1, V4, B5, F6, F7, F8, F9, F10, P2 and F3.
    void ResetFlags();

    // 0x2ea948 (`ret 4`, 30 bytes): copies the 8-byte value at `value` to P1 and sets the P1 flag.
    void SetPairP1(const void *value);

    // 0x2ea842 (`ret 4`, 26 bytes): stores the float bits `value` in F3 and sets its flag.
    void SetFloatF3(uint32_t value);

    // 0x2ea749 (`ret 4`, 32 bytes): copies 12 bytes from `value` (8 bytes, then the dword at +8) to V4 and sets its flag.
    void SetVec3V4(const void *value);

    // 0x2ea8ba (`ret 4`, 22 bytes): stores the low byte of `value` in B5 and sets its flag.
    void SetByteB5(uint32_t value);

    // 0x2ea966 (`ret 4`, 26 bytes): stores the float bits `value` in F8 and sets its flag.
    void SetFloatF8(uint32_t value);

    // 0x2ea87e (`ret 4`, 26 bytes): stores the float bits `value` in F10 and sets its flag.
    void SetFloatF10(uint32_t value);

    // 0x2ea926 (`ret 4`, 32 bytes): copies the 8-byte value at `value` to the default D11, without a flag.
    void SetPairD11(const void *value);

    // 0x2ea8de (`ret 4`, 32 bytes): copies the 8-byte value at `value` to the default D12, without a flag.
    void SetPairD12(const void *value);

    // 0x2ea728 (`ret 4`, 33 bytes): copies 12 bytes from `value` to the default D14, without a flag.
    void SetVec3D14(const void *value);

    // 0x2ea8a2 (`ret 4`, 18 bytes): stores the low byte of `value` in the default D15, without a flag.
    void SetByteD15(uint32_t value);

    // 0x2ea531 (`ret`, 25 bytes): pointer to P1 when its flag is set, else to the default D11.
    void *GetPointerP1OrD11();

    // 0x2ea4ae (`ret`, 25 bytes): F3 when its flag is set, else the default float at -0x8c. Returned in st(0).
    float GetFloatF3OrDefault();

    // 0x2ea4ee (`ret`, 22 bytes): B5 when its flag is set, else the default byte at -0x7c. Only al is defined.
    uint8_t GetByteB5OrD15();

    // 0x2ea436 (`ret`, 25 bytes): pointer to V4 when its flag is set, else to the default D14.
    void *GetVec3V4OrD14();
};

} // namespace nTSSG
