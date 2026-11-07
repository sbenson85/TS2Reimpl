#pragma once
#include <cstdint>

// Getters, setters and constant returns that the compiler folded into one copy each, so dozens of
// unrelated classes share the same address in their vtables (RVAs 1.18.0.187; db/names.jsonl lists
// them as shared_vf_*). They are named here by what they touch: `self` is the subobject pointer the
// vtable slot is called with (ecx), the offset is from it. The cRZSystemService default slots use
// them: service id (+0xc), priority (+0x10), tick priority (+0x14, already verified as 0x149d00),
// running flag (+0x18), and the default Init/Shutdown/OnTick/OnIdle that just return true.
//
// Each is written as __fastcall with a dummy edx so a stack argument is popped the way __thiscall
// does. None can be hooked (shared, and several are under 5 bytes); they are here so the classes
// that use them have every slot accounted for.
namespace acc {

void __fastcall DoNothing(void *self, void *edx);                            // 0x140fe0: ret 0 (empty destructor of a trivial member)
uint32_t __fastcall GetDword08(const void *self, void *edx);                 // 0x46390: [self + 8]
uint32_t __fastcall GetDword0C(const void *self, void *edx);                 // 0x145770: [self + 0xc] (GetServiceID)
void __fastcall SetDword0C(void *self, void *edx, uint32_t value);           // 0x149c60: [self + 0xc] = value (SetServiceID), ret 4
uint32_t __fastcall GetDword10(const void *self, void *edx);                 // 0x149cf0: [self + 0x10] (GetServicePriority)
void __fastcall SetDword18(void *self, void *edx, uint32_t value);           // 0x47fc80: [self + 0x18] = value, ret 4
void __fastcall SetDword30(void *self, void *edx, uint32_t value);           // 0x5506e0: [self + 0x30] = value, ret 4
uint8_t __fastcall GetByte18(const void *self, void *edx);                   // 0x156110: [self + 0x18] as a byte (IsServiceRunning)
uint8_t __fastcall GetByte09(const void *self, void *edx);                   // 0x4e9cc0: [self + 9] as a byte (the callback thread's ownership flag)
void __fastcall SetByte09(void *self, void *edx, uint32_t value);            // 0x4ea470: [self + 9] = the low byte of value, ret 4
void __fastcall SetByte18(void *self, void *edx, uint32_t value);            // 0x1b9690: [self + 0x18] = the low byte of value (SetServiceRunning), ret 4
bool __fastcall ReturnTrue(const void *self, void *edx);                     // 0x144910: al = 1 (default Init/Shutdown)
bool __fastcall ReturnTrueIgnoringArgument(const void *self, void *edx, uint32_t ignored); // 0x148260: al = 1, ret 4 (default OnTick/OnIdle)

} // namespace acc
