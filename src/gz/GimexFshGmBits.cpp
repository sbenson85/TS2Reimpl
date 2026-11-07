// FSH's GM decoder: the bit reader's peek (lane MR; src/gz/GimexFshGm.h, 0x895ee0), speed-built in the original.
#include "gz/GimexFshGm.h"

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kByteBits = 3;       // bit position >> 3 is the byte position
constexpr int32_t kBitInByteMask = 7;
constexpr int32_t kWordBits = 32;

} // namespace

uint32_t __cdecl PeekBits(int32_t count)
{
    const uint8_t *bytes = gGmState.data + (static_cast<uint32_t>(gGmState.bitPosition) >> kByteBits);
    const uint32_t word = static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 |
                          static_cast<uint32_t>(bytes[2]) << 8 | bytes[3];
    const int32_t shift = kWordBits - ((gGmState.bitPosition & kBitInByteMask) + count);
    return (word >> shift) & ((1u << count) - 1);
}

} // namespace fsh

} // namespace gimex
