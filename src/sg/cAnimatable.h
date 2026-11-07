#pragma once
#include <cstdint>

// nTSSG::cAnimatable's secondary subobjects (vtables at object +424, +432 and +444) have thunks in some slots:
//   sub ecx,[ecx-4]   ; this -= vtordisp (the dword at this - 4)
//   sub ecx,N         ; only in some thunks: this -= N
//   jmp body          ; a two-instruction getter on the adjusted this
// The bodies are `mov eax,[ecx-M]`, `fld dword ptr [ecx-M]`, `mov al,[ecx-M]` or `lea eax,[ecx-M]`, each followed by
// ret 0 (no stack arguments). The replacements compute the same adjusted this and read the same field directly
// instead of jumping to the body: the bodies are unnamed and shared with other classes, and they have no state
// of their own. Each function is named by the slot it fills and gives the thunk's RVA.
namespace nTSSG {

class cAnimatableSubobject {
public:
    uint32_t Vf432_55();   // slot 55 of +432, thunk 0x26e018 (14 bytes): N 0x134, mov eax,[S-0x50]
    uint32_t Vf444_25();   // slot 25 of +444, thunk 0x26fbdf (8 bytes): N 0, mov eax,[S-0x74]
    uint32_t Vf432_20();   // slot 20 of +432, thunk 0x26fc04 (14 bytes): N 0x134, mov eax,[S-0x6c]
    float Vf432_126();     // slot 126 of +432, thunk 0x26fda5 (11 bytes): N 0x6c, fld dword [S-0x4c], st0 result
    float Vf444_26();      // slot 26 of +444, thunk 0x26fdb0 (8 bytes): N 0, fld dword [S-0x70], st0 result
    uint32_t Vf432_39();   // slot 39 of +432, thunk 0x270558 (14 bytes): N 0x134, mov eax,[S-0x58]
    uint8_t Vf432_134();   // slot 134 of +432, thunk 0x271dac (11 bytes): N 0x6c, mov al,[S-0x2d]; eax upper bits kept
    uint32_t Vf432_49();   // slot 49 of +432, thunk 0x272197 (14 bytes): N 0x134, mov eax,[S-0x54]
    uint32_t Vf432_37();   // slot 37 of +432, thunk 0x2721a5 (14 bytes): N 0x134, mov eax,[S-0x5c]
    void Vf424_10();       // slot 10 of +424, thunk 0x27253f (11 bytes): N 0x6c, then ret 0 (body 0x140fe0). No result.
    uint32_t Vf432_33();   // slot 33 of +432, thunk 0x273339 (14 bytes): N 0x134, mov eax,[S-0x64]
    uint32_t Vf432_68();   // slot 68 of +432, thunk 0x2737dc (14 bytes): N 0x134, mov eax,[S-0x30]
    uint32_t Vf432_76();   // slot 76 of +432, thunk 0x2737ea (14 bytes): N 0x134, mov eax,[S-0x2c]
    uint32_t Vf432_35();   // slot 35 of +432, thunk 0x274062 (14 bytes): N 0x134, mov eax,[S-0x60]
    void *Vf444_27();      // slot 27 of +444, thunk 0x274070 (8 bytes): N 0, lea eax,[S-0x6c] (an address, not a read)
};

} // namespace nTSSG
