#pragma once
#include <cstdint>

namespace nTSSG {

// Thunked slots of nTSSG::cLocomotable's secondary subobjects (the vtables at object +860 and +872). Each slot is a
// 14-byte thunk of the same shape:
//   sub ecx, dword ptr [ecx - 4]   ; this -= the vtordisp stored just before this
//   sub ecx, N                     ; N is the distance from the subobject to the field's owner
//   jmp leaf                       ; a 3 to 4 byte leaf that reads one field at [ecx + disp] and returns it
// Each replacement below does the same adjustment and reads the field itself, so the leaf never runs. The leaves
// are plain field getters (the 4-byte functions in db/functions.jsonl that the candidates tool counts as done),
// so nothing here calls other code. The slot numbers are for the vtable named in each comment: slot N is at byte
// offset 4N of that vtable.
//
// The thunk does not write eax beyond what its leaf does. Byte leaves (mov al, [...]) set only al, and the caller
// reads only al, so the replacement returns uint8_t and leaves the upper bits to the compiler.
class cLocomotableSubobject {
public:
    // Adjusted pointer for a thunk with the given N: this - [this - 4] - N, in wrapping unsigned arithmetic.
    uintptr_t AdjustedBy(uint32_t n) const;

    uint32_t Vf860_55();  // thunk 0x280d35, N 0x2e0: mov eax, [ecx - 0x50]   (vtable 860)
    uint32_t Vf872_25();  // thunk 0x28270c, N 0x1ac: mov eax, [ecx - 0x74]   (vtable 872)
    uint32_t Vf872_24();  // thunk 0x282a1c, N 0x1ac: mov eax, [ecx - 0x78]   (vtable 872)
    uint32_t Vf860_20();  // thunk 0x2837ec, N 0x2e0: mov eax, [ecx - 0x6c]   (vtable 860)
    float Vf860_126();    // thunk 0x2837fa, N 0x218: fld dword [ecx - 0x4c] (st0; vtable 860)
    float Vf872_26();     // thunk 0x283808, N 0x1ac: fld dword [ecx - 0x70] (st0; vtable 872)
    uint32_t Vf860_39();  // thunk 0x2839d3, N 0x2e0: mov eax, [ecx - 0x58]   (vtable 860)
    uint8_t Vf860_134();  // thunk 0x28651d, N 0x218: mov al, [ecx - 0x2d]    (vtable 860)
    uint32_t Vf860_49();  // thunk 0x286fd9, N 0x2e0: mov eax, [ecx - 0x54]   (vtable 860)
    uint32_t Vf860_37();  // thunk 0x286fe7, N 0x2e0: mov eax, [ecx - 0x5c]   (vtable 860)
    uint32_t Vf860_33();  // thunk 0x2874e8, N 0x2e0: mov eax, [ecx - 0x64]   (vtable 860)
    uint32_t Vf860_68();  // thunk 0x2874f6, N 0x2e0: mov eax, [ecx - 0x30]   (vtable 860)
    uint32_t Vf860_76();  // thunk 0x287504, N 0x2e0: mov eax, [ecx - 0x2c]   (vtable 860)
    uint32_t Vf860_35();  // thunk 0x287683, N 0x2e0: mov eax, [ecx - 0x60]   (vtable 860)
    void *Vf872_27();     // thunk 0x287691, N 0x1ac: lea eax, [ecx - 0x6c]   (vtable 872): the adjusted pointer
};

} // namespace nTSSG
