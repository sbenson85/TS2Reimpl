#include "rz/RefPack.h"
#include "gz/GimexFshBlocks.h"

#include <cstdlib>
#include <cstring>

namespace RefPack {

namespace {

// Copies literals then a back-reference, byte by byte: references may overlap their own output,
// which is how runs are encoded.
inline void Emit(const uint8_t *&src, uint8_t *&dst, uint32_t literals, uint32_t length, uint32_t offset)
{
    while (literals--) *dst++ = *src++;
    const uint8_t *from = dst - offset;
    while (length--) *dst++ = *from++;
}

} // namespace

// The opcode loop, shared with the FSH back end's decoder (src/gz/GimexFshRefPack.cpp, 0x895620).
bool DecodeBody(const uint8_t *&in, uint8_t *&dst, int32_t room)
{
    for (;;) {
        uint8_t b0 = *in;
        if (b0 < 0x80) {
            // 2 bytes: 0-3 literals, length 3-10, offset up to 1024.
            uint8_t b1 = in[1];
            in += 2;
            uint32_t literals = b0 & 3, length = ((b0 >> 2) & 7) + 3;
            uint32_t offset = ((b0 & 0x60) << 3) + b1 + 1;
            if ((room -= static_cast<int32_t>(literals + length)) < 0) return false;
            Emit(in, dst, literals, length, offset);
        } else if (b0 < 0xC0) {
            // 3 bytes: 0-3 literals, length 4-67, offset up to 16384.
            uint8_t b1 = in[1], b2 = in[2];
            in += 3;
            uint32_t literals = b1 >> 6, length = (b0 & 0x3F) + 4;
            uint32_t offset = ((b1 & 0x3F) << 8) + b2 + 1;
            if ((room -= static_cast<int32_t>(literals + length)) < 0) return false;
            Emit(in, dst, literals, length, offset);
        } else if (b0 < 0xE0) {
            // 4 bytes: 0-3 literals, length 5-1028, offset up to 131072.
            uint8_t b1 = in[1], b2 = in[2], b3 = in[3];
            in += 4;
            uint32_t literals = b0 & 3, length = ((b0 & 0x0C) << 6) + b3 + 5;
            uint32_t offset = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1;
            if ((room -= static_cast<int32_t>(literals + length)) < 0) return false;
            Emit(in, dst, literals, length, offset);
        } else if (b0 < 0xFC) {
            // Literal run of 4-112 bytes.
            ++in;
            uint32_t literals = ((b0 & 0x1F) + 1) * 4;
            if ((room -= static_cast<int32_t>(literals)) < 0) return false;
            Emit(in, dst, literals, 0, 0);
        } else {
            // End: 0-3 trailing literals.
            ++in;
            uint32_t literals = b0 & 3;
            if ((room -= static_cast<int32_t>(literals)) < 0) return false;
            Emit(in, dst, literals, 0, 0);
            break;
        }
    }
    return true;
}

int32_t __cdecl Decompress(const uint8_t *src, uint32_t *consumed, uint8_t *dst, int32_t dstCapacity)
{
    const uint8_t *in = src;
    // Header: flags, 0xFB, [3-byte compressed size if flags & 1], 3-byte big-endian decompressed size.
    in += (in[0] & 1) ? 5 : 2;
    int32_t size = (in[0] << 16) | (in[1] << 8) | in[2];
    in += 3;
    if (!DecodeBody(in, dst, dstCapacity)) return -1;
    if (consumed) *consumed = static_cast<uint32_t>(in - src);
    return size;
}

uint32_t Hash(const uint8_t *p)
{
    return ((static_cast<uint32_t>(p[0]) << 8) | p[2]) ^ (static_cast<uint32_t>(p[1]) << 4);
}

// Literal runs: opcode 0xE0 + n/4 - 1 followed by n bytes, n a multiple of 4 up to 112.
void FlushLiterals(uint8_t *&out, const uint8_t *&run, uint32_t &literals)
{
    while (literals > 3) {
        uint32_t n = literals & ~3u;
        if (n > 0x70) n = 0x70;
        literals -= n;
        *out++ = static_cast<uint8_t>((n >> 2) - 0x21);
        std::memcpy(out, run, n);
        run += n;
        out += n;
    }
}

int32_t __cdecl Encode(const uint8_t *src, int32_t length, uint8_t *dst, int32_t /*window*/, int32_t quick)
{
    constexpr int32_t kWindowMask = 0x1FFFF;
    constexpr uint32_t kMaxLength = 0x404;
    auto *heads = static_cast<int32_t *>(std::malloc(0x10000 * sizeof(int32_t)));
    if (!heads) return 0;
    auto *links = static_cast<int32_t *>(std::malloc(0x20000 * sizeof(int32_t)));
    if (!links) return 0; // ORIGINAL-BUG: `heads` leaks here (reproduced)
    std::memset(heads, 0xFF, 0x10000 * sizeof(int32_t));

    uint8_t *out = dst;
    const uint8_t *cur = src;
    const uint8_t *run = src; // pending literals start here
    uint32_t literals = 0;
    int32_t remaining = length;
    while (remaining > 0) {
        uint32_t maxLength = static_cast<uint32_t>(remaining) < kMaxLength ? static_cast<uint32_t>(remaining) : kMaxLength;
        uint32_t hash = Hash(cur);
        int32_t head = heads[hash];
        int32_t index = static_cast<int32_t>(cur - src);
        int32_t minIndex = index - kWindowMask > 0 ? index - kWindowMask : 0;

        uint32_t bestLength = 2, bestSize = 2, bestOffset = 0;
        for (int32_t candidate = head; candidate >= minIndex; candidate = links[candidate & kWindowMask]) {
            const uint8_t *match = src + candidate;
            if (cur[bestLength] != match[bestLength]) continue;
            uint32_t matchLength = MatchLength(cur, match, maxLength);
            if (matchLength <= bestLength) continue;
            uint32_t offset = static_cast<uint32_t>(cur - match) - 1;
            uint32_t size = offset < 0x400 && matchLength <= 10 ? 2 : offset < 0x4000 && matchLength <= 0x43 ? 3 : 4;
            // Only take it if it saves more than the current best after paying for its opcode.
            if (matchLength - size + 4 <= bestLength - bestSize + 4) continue;
            bestSize = size;
            bestLength = matchLength;
            bestOffset = offset;
            if (matchLength >= kMaxLength) break;
        }

        if (bestSize >= bestLength) {
            // No profitable match: one more literal.
            links[index & kWindowMask] = head;
            heads[hash] = index;
            ++literals;
            ++cur;
            --remaining;
            continue;
        }

        FlushLiterals(out, run, literals); // leaves 0-3, which ride along in the match opcode
        if (bestSize == 2) {
            *out++ = static_cast<uint8_t>(((bestOffset >> 3) & 0xE0) + (bestLength << 2) - 0x0C + literals);
            *out++ = static_cast<uint8_t>(bestOffset);
        } else if (bestSize == 3) {
            *out++ = static_cast<uint8_t>(bestLength + 0x7C);
            *out++ = static_cast<uint8_t>((literals << 6) + (bestOffset >> 8));
            *out++ = static_cast<uint8_t>(bestOffset);
        } else {
            *out++ = static_cast<uint8_t>((((bestLength - 5) >> 6) & 0xFC) + ((bestOffset >> 12) & 0xF0) - 0x40 + literals);
            *out++ = static_cast<uint8_t>(bestOffset >> 8);
            *out++ = static_cast<uint8_t>(bestOffset);
            *out++ = static_cast<uint8_t>(bestLength - 5);
        }
        if (literals) {
            std::memcpy(out, run, literals);
            out += literals;
            literals = 0;
        }

        if (quick) {
            links[index & kWindowMask] = head;
            heads[hash] = index;
            cur += bestLength;
        } else {
            for (uint32_t i = 0; i < bestLength; ++i, ++cur, ++index) {
                uint32_t h = Hash(cur);
                links[index & kWindowMask] = heads[h];
                heads[h] = index;
            }
        }
        remaining -= static_cast<int32_t>(bestLength);
        run = cur;
    }

    FlushLiterals(out, run, literals);
    *out++ = static_cast<uint8_t>(0xFC + literals); // end, with the last 0-3 literals
    if (literals) {
        std::memcpy(out, run, literals);
        out += literals;
    }
    std::free(links);
    std::free(heads);
    return static_cast<int32_t>(out - dst);
}

int32_t __cdecl Compress(const uint8_t *src, int32_t length, uint8_t *dst, const int32_t *flags)
{
    int32_t quick = *flags & 1;
    WriteBigEndian(dst, 0x10FB, 2);
    WriteBigEndian(dst + 2, static_cast<uint32_t>(length), 3);
    return Encode(src, length, dst + 5, 0x20000, quick) + 5;
}

uint32_t __cdecl GetDecompressedSize(const uint8_t *src)
{
    uint32_t magic = (src[0] << 8) | src[1];
    const uint8_t *size = src + (magic == 0x10FB ? 2 : 5);
    return (size[0] << 16) | (size[1] << 8) | size[2];
}

uint32_t __cdecl RecognizeHeader(const uint8_t *src)
{
    uint32_t magic = (src[0] << 8) | src[1];
    return magic == 0x10FB || magic == 0x11FB ? 100 : 0;
}

void __cdecl WriteBigEndian(uint8_t *dst, uint32_t value, uint32_t bytes)
{
    value <<= ((4 - bytes) * 8) & 31; // x86 masks shift counts to 5 bits
    for (uint32_t i = 0; i < bytes; ++i, value <<= 8) *dst++ = static_cast<uint8_t>(value >> 24);
}

} // namespace RefPack
