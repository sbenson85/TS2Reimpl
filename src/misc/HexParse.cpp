// ParseGuidText (0x4f8eb0): in a speed-built module of the original (src/misc/HexParse.h).
#include "misc/HexParse.h"

#include <cstring>

namespace hexparse {

void *__cdecl ParseGuidText(void *out16, const char *text)
{
    char group[9] = {};
    uint32_t words[4] = {};
    uint32_t *next = words;
    int count = 0;
    for (; *text; ++text) {
        const char c = *text;
        if (c == '-' || c == '{' || c == '}') continue;
        group[count++] = c;
        if (count == 8) {
            group[8] = 0;
            // A fifth full group overwrote the original's stack past its four words (its return address); every text
            // the game passes has at most four. Ours drops it rather than writing past the array.
            if (next != words + 4) *next++ = ParseHex32(group);
            count = 0;
        }
    }
    std::memcpy(out16, words, sizeof(words));
    return out16;
}

} // namespace hexparse
