#include "rz/RZTextConvert.h"

#include "game/game.h"
#include "game/vtable.h"

#include <windows.h>

#include <cstring>
#include <new>
#include <vector>

namespace {

// cIGZString slots.
constexpr int kStringFromChar = 3;
constexpr int kStringToChar = 5;
constexpr int kStringData = 6;
constexpr int kStringStrlen = 7;

uint32_t Strlen(const cIGZString &s)
{
    void *p = const_cast<cIGZString *>(&s);
    return VirtualCall<uint32_t(__thiscall *)(void *), kStringStrlen>(p);
}
const char *Data(const cIGZString &s, int slot = kStringData)
{
    void *p = const_cast<cIGZString *>(&s);
    return VirtualSlot<const char *(__thiscall *)(void *)>(p, slot)(p);
}
void FromChar(cIGZString &s, const char *text, uint32_t length)
{
    VirtualCall<void(__thiscall *)(void *, const char *, uint32_t), kStringFromChar>(&s, text, length);
}

int32_t Resolve(int32_t encoding)
{
    if (encoding == RZText::kDefault) return RZText::DefaultEncoding();
    if (encoding == RZText::kSystemCodePage) return RZText::SystemCodePage();
    return encoding;
}

// UTF-8 to UTF-16 (the game's decoder). Stops at the first invalid or truncated sequence or when
// dst is full; returns bytes consumed and sets *dstLength to units written.
uint32_t DecodeUtf8(const uint8_t *src, uint32_t srcLength, uint16_t *dst, uint32_t *dstLength)
{
    const uint8_t *p = src, *end = src + srcLength;
    uint16_t *out = dst, *outEnd = dst + *dstLength;
    int32_t left = static_cast<int32_t>(srcLength);
    while (out < outEnd) {
        uint8_t c = p[0];
        uint16_t value;
        if (c < 0x80) {
            value = c;
            p += 1;
            left -= 1;
        } else {
            // Continuation bytes, as (byte ^ 0x80), each required to be < 0x40.
            auto cont = [&](int i, uint8_t &v) {
                v = static_cast<uint8_t>(p[i] ^ 0x80);
                return v < 0x40;
            };
            uint8_t b1, b2, b3, b4, b5;
            if (c < 0xc2) break;
            if (c < 0xe0) {
                if (left < 2 || !cont(1, b1)) break;
                value = static_cast<uint16_t>(((c & 0x1f) << 6) | b1);
                p += 2;
                left -= 2;
            } else if (c < 0xf0) {
                if (left < 3 || !cont(1, b1) || !cont(2, b2)) break;
                if (c < 0xe1 && p[1] < 0xa0) break; // overlong
                value = static_cast<uint16_t>((c << 12) | (b1 << 6) | b2);
                p += 3;
                left -= 3;
            } else if (c < 0xf8) {
                if (left < 4 || !cont(1, b1) || !cont(2, b2) || !cont(3, b3)) break;
                if (c < 0xf1 && p[1] < 0x90) break;
                // Only the low 16 bits survive: (raw second byte, b2, b3).
                value = static_cast<uint16_t>((p[1] << 12) | (b2 << 6) | b3);
                p += 4;
                left -= 4;
            } else if (c < 0xfc) {
                if (left < 5 || !cont(1, b1) || !cont(2, b2) || !cont(3, b3) || !cont(4, b4)) break;
                if (c < 0xf9 && p[1] < 0x88) break;
                value = static_cast<uint16_t>((p[2] << 12) | (b3 << 6) | b4);
                p += 5;
                left -= 5;
            } else if (c < 0xfe) {
                if (left < 6 || !cont(1, b1) || !cont(2, b2) || !cont(3, b3) || !cont(4, b4) || !cont(5, b5)) break;
                if (c < 0xfd && p[1] < 0x84) break;
                value = static_cast<uint16_t>((p[3] << 12) | (b4 << 6) | b5);
                p += 6;
                left -= 6;
            } else {
                break;
            }
            (void)b1;
        }
        *out++ = value;
        if (p >= end) break;
    }
    *dstLength = static_cast<uint32_t>(out - dst);
    return static_cast<uint32_t>(p - src);
}

// UTF-16 to UTF-8, each unit on its own (surrogates become 3-byte sequences). A unit that doesn't
// fit still counts as consumed.
uint32_t EncodeUtf8(const uint16_t *src, uint32_t srcLength, uint8_t *dst, uint32_t *dstLength)
{
    const uint16_t *p = src, *end = src + srcLength;
    uint8_t *out = dst, *outEnd = dst + *dstLength;
    while (p < end) {
        uint32_t c = *p++;
        uint32_t n = c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
        if (out + n > outEnd) break;
        if (n == 1) {
            out[0] = static_cast<uint8_t>(c);
        } else if (n == 2) {
            out[1] = static_cast<uint8_t>((c & 0x3f) | 0x80);
            out[0] = static_cast<uint8_t>((c >> 6) | 0xc0);
        } else {
            out[2] = static_cast<uint8_t>((c & 0x3f) | 0x80);
            out[1] = static_cast<uint8_t>(((c >> 6) & 0x3f) | 0x80);
            out[0] = static_cast<uint8_t>((c >> 12) | 0xe0);
        }
        out += n;
    }
    *dstLength = static_cast<uint32_t>(out - dst);
    return static_cast<uint32_t>(p - src);
}

} // namespace

namespace RZText {

int32_t SystemCodePage()
{
    if (ts2_rva_ff23a4 == -3) ts2_rva_ff23a4 = static_cast<int32_t>(GetACP());
    return ts2_rva_ff23a4;
}

uint32_t Convert(const void *src, uint32_t srcLength, int32_t srcEncoding, void *dst, uint32_t *dstLength, int32_t dstEncoding)
{
    if (srcLength == 0) {
        *dstLength = 0;
        return 0;
    }
    int32_t from = Resolve(srcEncoding);
    int32_t to = Resolve(dstEncoding);
    auto *bytes = static_cast<const uint8_t *>(src);

    // Anything but UTF-16 that is pure 7-bit counts as ASCII.
    if (from != kUtf16) {
        uint32_t i = 0;
        while (i < srcLength && bytes[i] <= 0x7f) ++i;
        if (i == srcLength) from = kAscii;
    }

    bool copy = from == to || (from == kAscii && to != kUtf16) || (from != kUtf16 && to == kAscii);
    if (!copy && from == kUtf16 && to == kAscii) {
        // Narrowing: the low byte of each unit (no src == dst check on this path).
        uint32_t n = srcLength < *dstLength ? srcLength : *dstLength;
        auto *units = static_cast<const uint16_t *>(src);
        auto *out = static_cast<uint8_t *>(dst);
        for (uint32_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(units[i]);
        *dstLength = n;
        return n;
    }
    if (copy) {
        if (src == dst) {
            *dstLength = srcLength;
            return srcLength;
        }
        uint32_t n = srcLength < *dstLength ? srcLength : *dstLength;
        std::memcpy(dst, src, to == kUtf16 ? n * 2 : n);
        *dstLength = n;
        return n;
    }

    if (src == dst) return 0; // *dstLength untouched
    if (from == kUtf8 && to == kUtf16) return DecodeUtf8(bytes, srcLength, static_cast<uint16_t *>(dst), dstLength);
    if (from == kUtf16 && to == kUtf8) return EncodeUtf8(static_cast<const uint16_t *>(src), srcLength, static_cast<uint8_t *>(dst), dstLength);
    if (from == kAscii && to == kUtf16) {
        uint32_t n = srcLength < *dstLength ? srcLength : *dstLength;
        auto *out = static_cast<uint16_t *>(dst);
        for (uint32_t i = 0; i < n; ++i) out[i] = bytes[i];
        *dstLength = n;
        return n;
    }
    if (to == kUtf8 || (from == kUtf8 && to != kUtf16)) {
        return ConvertViaUtf16(src, srcLength, from, dst, dstLength, to);
    }
    int result;
    if (from == kUtf16) {
        if (to == kUtf16) {
            *dstLength = 0;
            return 0;
        }
        char defaultChar[2] = {' ', 0};
        BOOL usedDefault = FALSE;
        result = WideCharToMultiByte(static_cast<UINT>(to), 0, static_cast<LPCWCH>(src), static_cast<int>(srcLength),
                                     static_cast<LPSTR>(dst), static_cast<int>(*dstLength), defaultChar, &usedDefault);
    } else if (to == kUtf16) {
        result = MultiByteToWideChar(static_cast<UINT>(from), 0, static_cast<LPCCH>(src), static_cast<int>(srcLength),
                                     static_cast<LPWSTR>(dst), static_cast<int>(*dstLength));
    } else {
        *dstLength = 0;
        return 0;
    }
    *dstLength = static_cast<uint32_t>(result);
    return result ? srcLength : 0;
}

uint32_t ConvertViaUtf16(const void *src, uint32_t srcLength, int32_t srcEncoding, void *dst, uint32_t *dstLength, int32_t dstEncoding)
{
    // The temporary is srcLength * 2 + 4 *bytes*, offered to Convert as that many UTF-16 units
    // (enough in practice: decoding never produces more units than input bytes).
    uint32_t units = srcLength * 2 + 4;
    char stackBuffer[0x400];
    std::vector<char> heapBuffer;
    char *temp = stackBuffer;
    if (units > 0x400) {
        heapBuffer.resize(units);
        temp = heapBuffer.data();
    }
    uint32_t result = srcLength;
    if (Convert(src, srcLength, srcEncoding, temp, &units, kUtf16) != srcLength
        || Convert(temp, units, kUtf16, dst, dstLength, dstEncoding) != units) {
        *dstLength = 0;
        result = 0;
    }
    return result;
}

uint32_t ConvertString(const cIGZString &src, void *dst, uint32_t *dstLength, int32_t dstEncoding)
{
    int32_t from = DefaultEncoding();
    int32_t to = dstEncoding == kDefault ? from : dstEncoding == kSystemCodePage ? SystemCodePage() : dstEncoding;
    if (from == to) {
        uint32_t n = *dstLength;
        if (n > Strlen(src)) {
            n = Strlen(src);
            *dstLength = n;
        }
        std::memcpy(dst, Data(src), n);
        return *dstLength;
    }
    uint32_t length = Strlen(src);
    const char *data = Data(src);
    return Convert(data, length, from, dst, dstLength, to);
}

bool ConvertStringToString(const cIGZString &src, cIGZString &dst, int32_t dstEncoding)
{
    int32_t from = DefaultEncoding();
    int32_t to = dstEncoding;
    if (to != kDefault) {
        if (to == kSystemCodePage) to = SystemCodePage();
        if (from != to) {
            if (from == kUtf16) return false;
            uint32_t length = Strlen(src);
            uint32_t capacity = length * 6;
            char stackBuffer[0x200];
            char *heap = capacity < 0x200 ? nullptr : static_cast<char *>(::operator new[](capacity));
            char *buffer = heap ? heap : stackBuffer;
            buffer[0] = 0;
            const char *data = Data(src);
            bool ok = Convert(data, length, from, buffer, &capacity, to) == length;
            FromChar(dst, buffer, capacity);
            ::operator delete[](heap);
            return ok;
        }
    }
    if (&src != &dst) {
        uint32_t length = Strlen(src);
        const char *text = Data(src, kStringToChar);
        FromChar(dst, text, length);
    }
    return true;
}

std::u16string *ToUtf16(std::u16string *out, const cIGZString &src)
{
    new (out) std::u16string();
    uint32_t units = Strlen(src) * 2;
    if (units > 0x200) {
        std::vector<char16_t> buffer(units);
        ConvertString(src, buffer.data(), &units, kUtf16);
        out->assign(buffer.data(), units);
    } else {
        char16_t buffer[0x200];
        units = 0x200;
        ConvertString(src, buffer, &units, kUtf16);
        out->assign(buffer, units);
    }
    return out;
}

} // namespace RZText
