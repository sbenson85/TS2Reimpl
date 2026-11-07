// The two speed-built functions of the FSH back end's decode side (lane MX; the rest of the group is size-built, in
// GimexFshRecords.size.cpp): 0x885d30, a float rounded to a byte, and 0x886680, the extra record's word into the info.
#include "gz/GimexFshDecode.h"

#include "gz/GimexFshLeaves.h"

#include <emmintrin.h>
#include <xmmintrin.h>

namespace gimex {
namespace fsh {

void __cdecl ReadShapeExtra(const Directory *directory, int32_t frame, uint8_t *info, int32_t platform)
{
    const uint8_t *extra = directory->entries[frame].userData;
    if (extra != nullptr) *reinterpret_cast<uint32_t *>(info + 0x2a78) = ReadWord(extra + 0xc, 4, platform);
}

} // namespace fsh
} // namespace gimex
