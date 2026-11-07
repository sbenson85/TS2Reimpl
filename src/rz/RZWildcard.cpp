#include "rz/RZWildcard.h"

#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"

namespace RZWildcard {

namespace {
constexpr char kAnyRun = '*';
constexpr char kAnyOne = '?';

// A lowercased copy of `text` in a cRZString, as the original builds it.
struct LoweredText {
    cRZString string;
    explicit LoweredText(const char *text)
    {
        string.Construct();
        string.FromChar(text);
        reinterpret_cast<cRZStringHelpers *>(&string)->LowerCase();
    }
    ~LoweredText() { string.Destruct(); }
};
} // namespace

bool __cdecl Match(const char *text, const char *pattern)
{
    for (;;) {
        const char c = *pattern;
        const char *next = pattern + 1;
        if (c == kAnyRun && *next == 0) return true;
        if (*text == 0) return c == 0;
        if (c == kAnyRun) {
            if (Match(text, next)) return true;
            next = pattern; // the '*' takes this character and stays
        } else if (c != kAnyOne && *text != c) {
            return false;
        }
        ++text;
        pattern = next;
    }
}

bool __cdecl MatchCase(const char *text, const char *pattern, uint8_t caseSensitive)
{
    if (caseSensitive) return Match(text, pattern);
    LoweredText loweredText(text);
    LoweredText loweredPattern(pattern);
    return Match(loweredText.string.ToChar(), loweredPattern.string.ToChar());
}

} // namespace RZWildcard
