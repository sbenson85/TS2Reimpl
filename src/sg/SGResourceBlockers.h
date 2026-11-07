#pragma once
#include <cstdint>

// Small helpers from the nGZSceneGraph cluster near RVA 0xa3c000 and 0x18c000 that block the last cSGResource
// subobject slots (0xa3ca00 and 0xa3cbf0). 0x18c080 is a leaf; 0xae6e60 is the string-key parser they reach.
namespace SGResourceBlockers {

// 0x18c080 (7 bytes, `cmp [ecx],-1; sete al; ret`): a __thiscall test with no stack arguments. It returns 1 when the
// dword at `this` is 0xffffffff (the invalid value) and 0 otherwise. Only al is written: the upper bits of eax keep
// whatever the caller left there, so the result is a byte. The name is provisional.
class cInvalidDwordTag {
public:
    uint8_t IsInvalid() const;
};

} // namespace SGResourceBlockers
