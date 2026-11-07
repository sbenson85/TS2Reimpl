// ParseHex32 (0x1b0e2c): in a size-built module of the original (src/misc/HexParse.h).
#include "misc/HexParse.h"

#include <cctype>

namespace hexparse {

uint32_t __cdecl ParseHex32(const char *text)
{
    while (isspace(*text)) ++text;
    const int start = text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 2 : 0;
    // The digits' values, then summed from the last one up (the original keeps them in an array the same way).
    int digits[8];
    int count = 0;
    for (int i = start; i < start + 8; ++i) {
        const int c = text[i];
        int digit;
        if (static_cast<unsigned>(c - '0') <= 9) digit = c - '0';
        else if (static_cast<unsigned>(c - 'a') <= 5) digit = c - 'a' + 10;
        else if (static_cast<unsigned>(c - 'A') <= 5) digit = c - 'A' + 10;
        else break;
        digits[count++] = digit;
    }
    uint32_t value = 0, scale = 1;
    for (int i = count - 1; i >= 0; --i) {
        value += static_cast<uint32_t>(digits[i]) * scale;
        scale <<= 4;
    }
    return value;
}

} // namespace hexparse
