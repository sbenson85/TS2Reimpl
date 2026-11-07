// FSH's RefPack compressor (lane MR; src/gz/GimexFshBlocks.h, 0x89698a and 0x896db9). The same hash-chain coder as
// cRZFastCompression3's RefPack::Encode (src/rz/RefPack.size.cpp, whose helpers it shares) with these differences, all from
// the last four bytes: matches are searched only while at least 8 bytes remain and may not reach into the last 4; the final 4
// bytes (plus pending literals) go out as literal runs and the end opcode; and the tables come from the codecs' allocator.
#include "gz/GimexFshBlocks.h"
#include "gz/GimexFshLeaves.h"

#include <cstring>

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kWindowMask = 0x1ffff;      // chain index mask: the 128 KB window
constexpr uint32_t kMaxLength = 0x404;
constexpr uint32_t kHeadsBytes = 0x40000;     // 0x10000 entries of 4 bytes, one per hash
constexpr uint32_t kLinksBytes = 0x80000;     // 0x20000 entries
constexpr int32_t kTail = 4;                  // bytes never matched
constexpr int32_t kMinMatchRemaining = 3;     // a match needs more than this many bytes beyond the tail
constexpr uint32_t kShortOffset = 0x400;
constexpr uint32_t kShortLength = 10;
constexpr uint32_t kMediumOffset = 0x4000;
constexpr uint32_t kMediumLength = 0x43;
constexpr uint32_t kLongestRun = 0x70;
constexpr uint8_t kEndOpcode = 0xfc;
constexpr uint8_t kHeaderHigh = 0xfb;
constexpr uint8_t kHeaderNarrow = 0x10;
constexpr uint8_t kHeaderWide = 0x90;
constexpr int32_t kNarrowLimit = 0xffffff;
constexpr int32_t kNarrowHeaderBytes = 5;
constexpr int32_t kWideHeaderBytes = 6;
constexpr int32_t kWideSizeBytes = 4;
constexpr int32_t kWindow = 0x20000;

} // namespace

int32_t __cdecl RefPackEncode(const uint8_t *source, int32_t length, uint8_t *dest, int32_t, int32_t quick)
{
    auto *heads = static_cast<int32_t *>(Allocate(kHeadsBytes));
    if (heads == nullptr) return 0;
    auto *links = static_cast<int32_t *>(Allocate(kLinksBytes));
    if (links == nullptr) return 0; // ORIGINAL-BUG: `heads` leaks here (reproduced)
    std::memset(heads, 0xff, kHeadsBytes);

    uint8_t *out = dest;
    const uint8_t *cur = source;
    const uint8_t *run = source; // pending literals start here
    uint32_t literals = 0;
    int32_t remaining = length - kTail;
    while (remaining >= 0) {
        const uint32_t maxLength = static_cast<uint32_t>(remaining) < kMaxLength ? static_cast<uint32_t>(remaining) : kMaxLength;
        const uint32_t hash = RefPack::Hash(cur);
        const int32_t head = heads[hash];
        int32_t index = static_cast<int32_t>(cur - source);
        const int32_t minIndex = index - kWindowMask > 0 ? index - kWindowMask : 0;

        uint32_t bestLength = 2;
        uint32_t bestSize = 2;
        uint32_t bestOffset = 0;
        for (int32_t candidate = head; candidate >= minIndex; candidate = links[candidate & kWindowMask]) {
            const uint8_t *match = source + candidate;
            if (cur[bestLength] != match[bestLength]) continue;
            const uint32_t matchLength = RefPack::MatchLength(cur, match, maxLength);
            if (matchLength <= bestLength) continue;
            const uint32_t offset = static_cast<uint32_t>(cur - match) - 1;
            const uint32_t size = offset < kShortOffset && matchLength <= kShortLength               ? 2
                                  : offset < kMediumOffset && matchLength <= kMediumLength ? 3
                                                                                           : 4;
            // Only take it if it saves more than the current best after paying for its opcode.
            if (matchLength - size + 4 <= bestLength - bestSize + 4) continue;
            bestSize = size;
            bestLength = matchLength;
            bestOffset = offset;
            if (matchLength >= kMaxLength) break;
        }

        if (bestSize >= bestLength || remaining <= kMinMatchRemaining) {
            // No profitable match: one more literal.
            links[index & kWindowMask] = head;
            heads[hash] = index;
            ++literals;
            ++cur;
            --remaining;
            continue;
        }

        RefPack::FlushLiterals(out, run, literals); // leaves 0-3, which ride along in the match opcode
        if (bestSize == 2) {
            *out++ = static_cast<uint8_t>(((bestOffset >> 3) & 0xe0) + (bestLength << 2) - 0x0c + literals);
            *out++ = static_cast<uint8_t>(bestOffset);
        } else if (bestSize == 3) {
            *out++ = static_cast<uint8_t>(bestLength + 0x7c);
            *out++ = static_cast<uint8_t>((literals << 6) + (bestOffset >> 8));
            *out++ = static_cast<uint8_t>(bestOffset);
        } else {
            *out++ = static_cast<uint8_t>((((bestLength - 5) >> 6) & 0xfc) + ((bestOffset >> 12) & 0xf0) - 0x40 + literals);
            *out++ = static_cast<uint8_t>(bestOffset >> 8);
            *out++ = static_cast<uint8_t>(bestOffset);
            *out++ = static_cast<uint8_t>(bestLength - 5);
        }
        if (literals != 0) {
            std::memcpy(out, run, literals);
            out += literals;
            literals = 0;
        }

        if (quick != 0) {
            links[index & kWindowMask] = head;
            heads[hash] = index;
            cur += bestLength;
        } else {
            for (uint32_t i = 0; i < bestLength; ++i, ++cur, ++index) {
                const uint32_t h = RefPack::Hash(cur);
                links[index & kWindowMask] = heads[h];
                heads[h] = index;
            }
        }
        remaining -= static_cast<int32_t>(bestLength);
        run = cur;
    }

    // What is left: the pending literals and the unmatched tail.
    uint32_t tail = literals + kTail + remaining;
    RefPack::FlushLiterals(out, run, tail);
    *out++ = static_cast<uint8_t>(kEndOpcode + tail);
    if (tail != 0) {
        std::memcpy(out, run, tail);
        out += tail;
    }
    FreeMemory(links);
    FreeMemory(heads);
    return static_cast<int32_t>(out - dest);
}

int32_t __stdcall RefPackEncodeWithHeader(uint8_t *dest, const uint8_t *source, int32_t length, int32_t)
{
    dest[1] = kHeaderHigh;
    int32_t headerBytes;
    if (length > kNarrowLimit) {
        dest[0] = kHeaderWide;
        PutBigEndian(dest + 2, static_cast<uint32_t>(length), kWideSizeBytes);
        headerBytes = kWideHeaderBytes;
    } else {
        dest[0] = kHeaderNarrow;
        uint8_t *size = dest + 2;
        *size++ = static_cast<uint8_t>(static_cast<uint32_t>(length) >> 16);
        *size++ = static_cast<uint8_t>(static_cast<uint32_t>(length) >> 8);
        *size = static_cast<uint8_t>(length);
        headerBytes = kNarrowHeaderBytes;
    }
    return RefPackEncode(source, length, dest + headerBytes, kWindow, 0) + headerBytes;
}

} // namespace fsh

} // namespace gimex
