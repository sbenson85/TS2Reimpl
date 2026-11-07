#pragma once
#include <cstdint>

// nTSSG::cAnimRequest: an animation request object. Its secondary subobject at +248 (vtable 0xdb3dc4, 59 slots) is
// reached through thunks of the form
//   sub ecx,[ecx-4]   ; this = subobject - vtordisp (the dword at subobject - 4)
//   jmp body
// The bodies work on that adjusted pointer with negative offsets (fields at adjusted - 0xec and so on). The object's
// full layout (virtual base table at +4, the constructor 0x2b6063) is not mapped yet, so the functions here are
// named after the field offset they touch, relative to the adjusted pointer, and not after what the field means.
//
// Each member below is the thunk: `this` is the subobject, and the member computes the adjusted pointer and does
// what the body does. Slot N is at byte offset 4N in the subobject vtable. RVAs are for 1.18.0.187.
namespace nTSSG {

class cAnimRequestSubobject {
public:
    static constexpr uint32_t kVTable = 0xdb3dc4; // secondary vtable at subobject +0 (slot 0 is at 0x2b64c7)

    void SetDwordEc(uint32_t value);                  // slot 2, thunk 0x2b6c31, body 0x2b6c40: ret 4
    void SetDwordA8(uint32_t value);                  // slot 5, thunk 0x2b6e7d, body 0x2b6e90: ret 4
    void SetWord98(uint16_t value);                   // slot 8, thunk 0x2b6daa, body 0x2b6dc0: ret 4
    void SetWord96(uint16_t value);                   // slot 10, thunk 0x2b6bef, body 0x2b6c00: ret 4
    void SetByte94(uint8_t value);                    // slot 12, thunk 0x2b6c4d, body 0x2b6c60: ret 4
    void SetByte84(uint8_t value);                    // slot 16, thunk 0x2b6e3e, body 0x2b6e50: ret 4
    void SetByte83(uint8_t value);                    // slot 17, thunk 0x2b6e5d, body 0x2b6e70: ret 4
    void SetDword80(uint32_t value);                  // slot 18, thunk 0x2b6a6d, body 0x2b6a80: ret 4
    void SetByte58(uint8_t value);                    // slot 22, thunk 0x2b6d78, body 0x2b6d80: ret 4
    void SetDword4C(uint32_t value);                  // slot 24, thunk 0x2b6c6d, body 0x2b6c80: ret 4
    void SetDwords24And20(uint32_t first, uint32_t second); // slot 26, thunk 0x2b6d0a, body 0x2b6d20: ret 8
    uint32_t GetDwordEc();                            // slot 28, thunk 0x2b6447, body 0x2b6450: ret
    uint32_t GetDwordA8();                            // slot 31, thunk 0x2b6ed4, body 0x2b6ee0: ret
    uint32_t GetDword80();                            // slot 47, thunk 0x2b6283, body 0x2b6290: ret
    void SetDwords18_14_10(uint32_t a, uint32_t b, uint32_t c); // slot 57, thunk 0x2b6d4f, body 0x2b6d60: ret 0xc
};

} // namespace nTSSG
