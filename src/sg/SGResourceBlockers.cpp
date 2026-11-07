#include "sg/SGResourceBlockers.h"

namespace SGResourceBlockers {

uint8_t cInvalidDwordTag::IsInvalid() const
{
    // cmp dword ptr [ecx], -1; sete al. The byte is 0 or 1; eax's upper bits are not written.
    return *reinterpret_cast<const uint32_t *>(this) == 0xffffffffu ? 1 : 0;
}

} // namespace SGResourceBlockers
