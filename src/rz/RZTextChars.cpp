#include "rz/RZTextChars.h"

#include "rz/RZTextConvert.h"

namespace RZText {

bool IsLeadByte(uint32_t byte, int32_t codePage)
{
    const uint8_t b = static_cast<uint8_t>(byte);
    switch (codePage) {
    case 932: return (b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xfc);
    case 936: return b >= 0xa1 && b <= 0xfc;
    case 949: return b >= 0xb0 && b <= 0xc8;
    case 950: return b >= 0x81 && b <= 0xfe;
    default: return false;
    }
}

uint32_t CharLength(const void *text, uint32_t length, int32_t encoding)
{
    if (length == 0) return 0;
    if (encoding == kDefault) encoding = DefaultEncoding();
    else if (encoding == kSystemCodePage) encoding = SystemCodePage();
    if (encoding == kAscii || encoding == kUtf16) return 1;
    const uint8_t *const bytes = static_cast<const uint8_t *>(text);
    if (encoding == kUtf8) {
        const uint8_t lead = bytes[0];
        if (lead < 0x80 || static_cast<uint8_t>(lead + 0x40) > 0x3d || length <= 1 || static_cast<uint8_t>(bytes[1] + 0x80) > 0x3f)
            return 1;
        uint32_t units;
        if (lead < 0xe0) units = 2;
        else if (lead < 0xf0) units = 3;
        else if (lead < 0xf8) units = 4;
        else units = lead < 0xfc ? 5 : 6;
        return units > length ? length : units;
    }
    return length > 1 && IsLeadByte(bytes[0], encoding) ? 2 : 1;
}

uint32_t CharsLength(const void *text, uint32_t count, int32_t encoding)
{
    uint32_t total = 0;
    if (encoding == kDefault) encoding = DefaultEncoding();
    else if (encoding == kSystemCodePage) encoding = SystemCodePage();
    const uint8_t *at = static_cast<const uint8_t *>(text);
    for (; count != 0; --count) {
        const uint32_t units = CharLength(at, 0x3fffffff, encoding);
        total += units;
        at += (encoding == kUtf16 ? 2 : 1) * units;
    }
    return total;
}

bool IsIdeograph(uint32_t ch)
{
    return (ch >= 0x2e80 && ch <= 0xe000) || (ch >= 0xf900 && ch <= 0xfaff) || (ch >= 0xfe30 && ch <= 0xfe4f) ||
           ch - 0xff10 <= 0xde;
}

bool IsHangul(uint32_t ch)
{
    return (ch >= 0x3130 && ch <= 0x318f) || ch - 0xac00 <= 0x2ba3;
}

bool IsThaiBlock(uint32_t ch)
{
    return ch - 0xe00 <= 0x7f;
}

} // namespace RZText
