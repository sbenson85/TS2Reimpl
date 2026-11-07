// Differential tests for src/misc/HexParse (lane HF): ParseHex32 (0x1b0e2c) and ParseGuidText (0x4f8eb0) against the
// image's on generated texts.
//
// HexParse_Hex32: leading white space of every kind the CRT's isspace knows (and characters it doesn't), "0x" / "0X"
// prefixes (also a lone "0", "0x" with nothing after), 0-12 digits of either case, then a terminator from the whole
// ASCII range or a character above 0x7f (never in the leading position: isspace of a negative char reads outside the
// CRT's table). Compared: the result.
// HexParse_GuidText: texts of hex digits, '-', '{', '}' and other characters, up to 32 counted characters (four full
// groups, the most the original's stack holds), from a 32-byte junk output. Compared: the 16 bytes written, the 8 bytes
// after them untouched, the returned pointer.
#include "difftest.h"
#include "misc/HexParse.h"

#include <cstring>
#include <random>
#include <string>

namespace {

constexpr uint32_t kParseHex32 = 0x1b0e2c, kParseGuidText = 0x4f8eb0;

std::string HexText(std::mt19937 &rng)
{
    static const char kSpaces[] = " \t\n\v\f\r";
    static const char kDigits[] = "0123456789abcdefABCDEF";
    std::string s;
    for (int i = rng() % 4; i > 0; --i) s += kSpaces[rng() % 6];
    switch (rng() % 6) {
    case 0: s += "0x"; break;
    case 1: s += "0X"; break;
    case 2: s += "0"; break;
    default: break;
    }
    for (int i = rng() % 13; i > 0; --i) s += kDigits[rng() % 22];
    switch (rng() % 4) {
    case 0: break;
    case 1: s += static_cast<char>(1 + rng() % 127); break;
    case 2: s += static_cast<char>(0x80 + rng() % 128); break;
    default: s += static_cast<char>(1 + rng() % 127); s += kDigits[rng() % 22]; break;
    }
    if (!s.empty() && rng() % 16 == 0) s[0] = static_cast<char>(1 + rng() % 127); // anything first, ASCII only
    return s;
}

} // namespace

DIFF_TEST(HexParse_Hex32, 0x1b0e2c)
{
    auto image = ctx.Original<uint32_t(__cdecl *)(const char *)>(kParseHex32);
    std::mt19937 rng(0x1b0e2c);
    const char *fixed[] = {"", "0", "0x", "0X", "x1", " 0x", "ffffffff", "FFFFFFFF0", "123456789", "  \t0x1aB", "0x0x1",
                           "g", "-1", "+1", "\x7f" "1", "1\x80" "2", "00000006"};
    for (const char *t : fixed) {
        ++ctx.cases;
        uint32_t a = image(t), b = hexparse::ParseHex32(t);
        if (a != b) ctx.Fail("\"%s\": the image's %08x, ours %08x", t, a, b);
    }
    for (int i = 0; i < 20000; ++i) {
        const std::string t = HexText(rng);
        ++ctx.cases;
        uint32_t a = image(t.c_str()), b = hexparse::ParseHex32(t.c_str());
        if (a != b) ctx.Fail("\"%s\": the image's %08x, ours %08x", t.c_str(), a, b);
    }
}

DIFF_TEST(HexParse_GuidText, 0x4f8eb0)
{
    auto image = ctx.Original<void *(__cdecl *)(void *, const char *)>(kParseGuidText);
    std::mt19937 rng(0x4f8eb0);
    static const char kChars[] = "0123456789abcdefABCDEF---{}}xg ";
    const char *fixed[] = {"", "{13ae91e7-b825-4559-82a3-0ead8e8dd7fd}", "00000005-0000-0000-0000-000000000000", "1234567",
                           "12345678", "1234567-8", "{}{}--", "ffffffffffffffffffffffffffffffff"};
    for (int i = 0; i < 20000 + static_cast<int>(std::size(fixed)); ++i) {
        std::string t;
        if (i < static_cast<int>(std::size(fixed))) {
            t = fixed[i];
        } else {
            int counted = 0;
            const int limit = rng() % 33;
            while (true) {
                const char c = rng() % 8 == 0 ? static_cast<char>(1 + rng() % 127) : kChars[rng() % (sizeof(kChars) - 1)];
                const bool skipped = c == '-' || c == '{' || c == '}';
                if (!skipped && counted == limit) break;
                if (!skipped) ++counted;
                t += c;
                if (t.size() > 60) break;
            }
        }
        uint8_t a[32], b[32];
        for (int k = 0; k < 32; ++k) a[k] = b[k] = static_cast<uint8_t>(rng());
        void *ra = image(a + 4, t.c_str());
        void *rb = hexparse::ParseGuidText(b + 4, t.c_str());
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0 || ra != a + 4 || rb != b + 4) ctx.Fail("\"%s\": the GUIDs differ", t.c_str());
    }
}
