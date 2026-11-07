// FSH's RefPack decoder (lane MR; src/gz/GimexFshBlocks.h, 0x895620): the header (with the wide forms 0x90fb / 0x91fb) and then
// cRZFastCompression3's opcode loop (RefPack::DecodeBody, src/rz/RefPack.size.cpp) with no room check.
#include "gz/GimexFshBlocks.h"

#include <climits>

// How many leading bytes of a and b agree, up to max (0x896960). Speed-built in the original, so it lives here rather than in
// the size-profile RefPack.size.cpp that uses it.
uint32_t RefPack::MatchLength(const uint8_t *a, const uint8_t *b, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && a[n] == b[n]) ++n;
    return n;
}

namespace gimex {

namespace fsh {

namespace {

constexpr uint8_t kHasCompressedSize = 1;   // first header byte: a compressed-size field precedes the decoded size
constexpr uint8_t kWideSize = 0x80;         // first header byte: the sizes are 4 bytes, not 3
constexpr int32_t kHeaderPrefix = 2;        // the type bytes
constexpr int32_t kNarrowSizeBytes = 3;
constexpr int32_t kWideSizeBytes = 4;
constexpr int32_t kNoRoomLimit = INT_MAX;

} // namespace

int32_t __stdcall DecodeRefPack(uint8_t *dest, const uint8_t *source, int32_t *consumed)
{
    int32_t size = 0;
    const uint8_t *in = nullptr;
    if (source != nullptr) {
        const int32_t sizeBytes = (source[0] & kWideSize) != 0 ? kWideSizeBytes : kNarrowSizeBytes;
        in = source + kHeaderPrefix;
        if ((source[0] & kHasCompressedSize) != 0) in += sizeBytes;
        size = static_cast<int32_t>(ReadBigEndian(in, sizeBytes));
        in += sizeBytes;
        RefPack::DecodeBody(in, dest, kNoRoomLimit);
    }
    if (consumed != nullptr) *consumed = static_cast<int32_t>(in - source);
    return size;
}

} // namespace fsh

} // namespace gimex
