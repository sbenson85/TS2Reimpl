#include "rz/RZTextTrail.h"

#include "rz/RZTextChars.h"
#include "rz/RZTextConvert.h"

namespace RZText {

uint32_t Utf8TrailCount(uint32_t byte)
{
    const uint8_t b = static_cast<uint8_t>(byte);
    if (b >= 0xc2 && b <= 0xdf) return 1;
    if (b >= 0xe0 && b <= 0xef) return 2;
    if (b >= 0xf0 && b <= 0xf7) return 3;
    if (b >= 0xf8 && b <= 0xfb) return 4;
    if (b == 0xfc || b == 0xfd) return 5;
    return 0;
}

uint32_t CodePageTrailCount(uint32_t byte, int32_t codePage) { return IsLeadByte(byte, codePage) ? 1 : 0; }

uint32_t TrailCount(uint32_t byte, int32_t encoding)
{
    if (encoding == kDefault)
        encoding = DefaultEncoding();
    else if (encoding == kSystemCodePage)
        encoding = SystemCodePage();
    if (encoding == kUtf8) return Utf8TrailCount(byte);
    if (encoding >= 100) return CodePageTrailCount(byte, encoding);
    return 0;
}

} // namespace RZText
