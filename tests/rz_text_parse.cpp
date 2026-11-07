// Differential tests for src/rz/RZTextParse (lane LS): the shared text parsers against the image's on generated texts.
//
// Texts are ASCII only: the parsers pass sign-extended characters to the CRT's isspace / isdigit / isalpha / toupper,
// and a character above 0x7f is a negative index into the CRT's table (each CRT reads its own memory there), so high
// characters are left out of every text (as HexParse_Hex32 does for its leading position).
//
// RZTextParse_Hex64 / _Int64: white space, signs, prefixes, digits and letters of every base, terminators; the result.
// RZTextParse_Double / _ReadDouble: digits, fractions, exponents (also past the 32-bit wrap), signs; the result's bits
// and (ReadDouble) the end pointer as an offset, with a null end too.
// RZTextParse_PowerOf: base 10 and others over a wide exponent range; the result's bits.
// RZTextParse_Split: texts of blanks, quotes, separators and letters, every separator kind, 0-6 parts of room, parts
// holding old text (short and heap-sized) before the call; compared: the result, the count, every part's text, size
// and capacity (the parts past the count must be untouched), and the 0x20-byte strides' other words.
#include "difftest.h"
#include "rz/RZTextParse.h"
#include "rz/cRZString.h"

#include <cstring>
#include <random>
#include <string>

namespace {

constexpr uint32_t kParseHex64 = 0x1b0ec2;
constexpr uint32_t kParseInt64 = 0x1b101c;
constexpr uint32_t kPowerOf = 0x1afaa3;
constexpr uint32_t kReadDouble = 0x1b1ab5;
constexpr uint32_t kParseDouble = 0x1b1008;
constexpr uint32_t kSplitText = 0x1b1557;

uint64_t Bits(double d)
{
    uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}

std::string Blanks(std::mt19937 &rng)
{
    static const char kSpaces[] = " \t\n\v\f\r";
    std::string s;
    for (int i = rng() % 4; i > 0; --i) s += kSpaces[rng() % 6];
    return s;
}

char Ascii(std::mt19937 &rng) { return static_cast<char>(1 + rng() % 127); }

std::string NumberText(std::mt19937 &rng, const char *alphabet, int maxDigits)
{
    const int n = static_cast<int>(std::strlen(alphabet));
    std::string s = Blanks(rng);
    switch (rng() % 5) {
    case 0: s += '-'; break;
    case 1: s += '+'; break;
    default: break;
    }
    switch (rng() % 6) {
    case 0: s += "0x"; break;
    case 1: s += "0X"; break;
    case 2: s += "0"; break;
    default: break;
    }
    for (int i = rng() % (maxDigits + 1); i > 0; --i) s += alphabet[rng() % n];
    if (rng() % 3 == 0) s += Ascii(rng);
    if (rng() % 4 == 0) s += alphabet[rng() % n];
    if (!s.empty() && rng() % 16 == 0) s[rng() % s.size()] = Ascii(rng);
    return s;
}

std::string DoubleText(std::mt19937 &rng)
{
    static const char kDigits[] = "0123456789";
    std::string s = Blanks(rng);
    switch (rng() % 5) {
    case 0: s += '-'; break;
    case 1: s += '+'; break;
    default: break;
    }
    for (int i = rng() % 22; i > 0; --i) s += kDigits[rng() % 10];
    if (rng() % 2) {
        s += '.';
        for (int i = rng() % 22; i > 0; --i) s += kDigits[rng() % 10];
    }
    if (rng() % 2) {
        s += rng() % 2 ? 'e' : 'E';
        switch (rng() % 4) {
        case 0: s += '-'; break;
        case 1: s += '+'; break;
        default: break;
        }
        for (int i = rng() % (rng() % 8 == 0 ? 14 : 4); i > 0; --i) s += kDigits[rng() % 10];
    }
    if (rng() % 3 == 0) s += Ascii(rng);
    if (!s.empty() && rng() % 12 == 0) s[rng() % s.size()] = "+-.eE0x "[rng() % 8];
    return s;
}

} // namespace

DIFF_TEST(RZTextParse_Hex64, 0x1b0ec2)
{
    auto image = ctx.Original<uint64_t(__cdecl *)(const char *)>(kParseHex64);
    std::mt19937 rng(0x1b0ec2);
    const char *fixed[] = {"", "0", "0x", "0X", "x1", " 0x", "ffffffffffffffff", "FFFFFFFFFFFFFFFF0", "12345678901234567",
                           "  \t0x1aB", "0x0x1", "g", "-1", "+1", "\x7f" "1", "0000000000000006", "0x10000000000000001"};
    for (const char *t : fixed) {
        ++ctx.cases;
        uint64_t a = image(t), b = rztext::ParseHex64(t);
        if (a != b) ctx.Fail("\"%s\": the image's %016llx, ours %016llx", t, a, b);
    }
    for (int i = 0; i < 20000; ++i) {
        const std::string t = NumberText(rng, "0123456789abcdefABCDEFgG", 20);
        ++ctx.cases;
        uint64_t a = image(t.c_str()), b = rztext::ParseHex64(t.c_str());
        if (a != b) ctx.Fail("\"%s\": the image's %016llx, ours %016llx", t.c_str(), a, b);
    }
}

DIFF_TEST(RZTextParse_Int64, 0x1b101c)
{
    auto image = ctx.Original<uint64_t(__cdecl *)(const char *)>(kParseInt64);
    std::mt19937 rng(0x1b101c);
    const char *fixed[] = {"", "0", "-0", "+", "-", "0x", "0X1f", "0x-1", "077", "089", "18446744073709551615",
                           "18446744073709551616", "-9223372036854775808", "  +42z", "0xFFFFFFFFFFFFFFFFF", "1e5",
                           "zz", "-0x10", "00x1", "\t\n-12"};
    for (const char *t : fixed) {
        ++ctx.cases;
        uint64_t a = image(t), b = rztext::ParseInt64(t);
        if (a != b) ctx.Fail("\"%s\": the image's %016llx, ours %016llx", t, a, b);
    }
    for (int i = 0; i < 30000; ++i) {
        const std::string t = NumberText(rng, "0123456789012345678901234567abcdefxyzABCDEFXZ_", 24);
        ++ctx.cases;
        uint64_t a = image(t.c_str()), b = rztext::ParseInt64(t.c_str());
        if (a != b) ctx.Fail("\"%s\": the image's %016llx, ours %016llx", t.c_str(), a, b);
    }
}

DIFF_TEST(RZTextParse_PowerOf, 0x1afaa3)
{
    auto image = ctx.Original<double(__cdecl *)(int, int)>(kPowerOf);
    std::mt19937 rng(0x1afaa3);
    for (int i = 0; i < 20000; ++i) {
        int base = 10, exponent;
        switch (i % 4) {
        case 0: exponent = static_cast<int>(rng() % 801) - 400; break;
        case 1: exponent = static_cast<int>(rng()); break;
        case 2: exponent = static_cast<int>(rng() % 41) - 20; base = static_cast<int>(rng() % 21) - 10; break;
        default: exponent = static_cast<int>(rng() % 2001) - 1000; base = static_cast<int>(rng()); break;
        }
        ++ctx.cases;
        const double a = image(base, exponent), b = rztext::PowerOf(base, exponent);
        if (Bits(a) != Bits(b)) ctx.Fail("%d ^ %d: the image's %.17g, ours %.17g", base, exponent, a, b);
    }
}

DIFF_TEST(RZTextParse_ReadDouble, 0x1b1ab5)
{
    auto image = ctx.Original<double(__cdecl *)(const char *, const char **)>(kReadDouble);
    std::mt19937 rng(0x1b1ab5);
    const char *fixed[] = {"", ".", "-", "+.", "1", "-0", "1.5", ".5", "5.", "1e", "1e+", "1e-3", "1E400", "1e-400",
                           "0e999", "-0e999", "12345678901234567890.123456789", "1e4294967297", "1e-4294967297",
                           "  -3.25e+2x", "e5", ".e5", "1.2.3", "9" "e" "9999999999"};
    for (int i = 0; i < 40000 + static_cast<int>(std::size(fixed)); ++i) {
        const std::string t = i < static_cast<int>(std::size(fixed)) ? std::string(fixed[i]) : DoubleText(rng);
        const bool withEnd = i % 5 != 4;
        const char *endA = reinterpret_cast<const char *>(1), *endB = endA;
        ++ctx.cases;
        const double a = image(t.c_str(), withEnd ? &endA : nullptr);
        const double b = rztext::ReadDouble(t.c_str(), withEnd ? &endB : nullptr);
        if (Bits(a) != Bits(b)) ctx.Fail("\"%s\": the image's %.17g, ours %.17g", t.c_str(), a, b);
        if (endA != endB) {
            ctx.Fail("\"%s\": end at %d in the image's, %d in ours", t.c_str(), static_cast<int>(endA - t.c_str()),
                     static_cast<int>(endB - t.c_str()));
        }
    }
}

DIFF_TEST(RZTextParse_Double, 0x1b1008)
{
    auto image = ctx.Original<double(__cdecl *)(const char *)>(kParseDouble);
    std::mt19937 rng(0x1b1008);
    for (int i = 0; i < 20000; ++i) {
        const std::string t = DoubleText(rng);
        ++ctx.cases;
        const double a = image(t.c_str()), b = rztext::ParseDouble(t.c_str());
        if (Bits(a) != Bits(b)) ctx.Fail("\"%s\": the image's %.17g, ours %.17g", t.c_str(), a, b);
    }
}

namespace {

// A cRZString's footprint: the splitter writes only the std::string at +4.
struct Part {
    uint32_t vtable;
    std::string text;
    uint32_t refCount;
};
static_assert(sizeof(Part) == sizeof(cRZString), "cRZString footprint");

constexpr int kRoom = 8;

struct Parts {
    Part p[kRoom];
};

void Seed(Parts &parts, uint32_t seed)
{
    std::mt19937 rng(seed);
    for (Part &part : parts.p) {
        part.vtable = rng();
        part.refCount = rng();
        switch (rng() % 3) {
        case 0: part.text.clear(); break;
        case 1: part.text.assign(1 + rng() % 12, 'q'); break;
        default: part.text.assign(16 + rng() % 40, 'w'); break;
        }
    }
}

std::string SplitInput(std::mt19937 &rng, char separator)
{
    const char kAlphabet[] = {' ', '\t', '"', 'a', 'b', 'c', 'x', ',', ';', separator, separator, 'd'};
    std::string s;
    for (int i = rng() % 30; i > 0; --i) s += kAlphabet[rng() % sizeof(kAlphabet)];
    return s;
}

} // namespace

DIFF_TEST(RZTextParse_Split, 0x1b1557)
{
    using SplitFn = bool(__cdecl *)(const char *, uint32_t, char, cRZString *, uint32_t *);
    auto image = ctx.Original<SplitFn>(kSplitText);
    std::mt19937 rng(0x1b1557);
    const char kSeparators[] = {' ', ',', ';', '\t', '"', 'a'};
    const char *fixed[] = {"", "   ", "a", "a,b", "a, b ,c", "\"a,b\",c", "\"a\"", "\"\"", "\"", "a,,b", "a ,", ",",
                           " ,a", "a b\t c", "\"a b\" c", "a,b,c,d,e,f,g,h,i,j"};
    for (int i = 0; i < 30000 + static_cast<int>(std::size(fixed)) * 6; ++i) {
        const char separator = kSeparators[i % 6];
        const std::string t = i < static_cast<int>(std::size(fixed)) * 6 ? std::string(fixed[i / 6]) : SplitInput(rng, separator);
        uint32_t size = static_cast<uint32_t>(t.size());
        if (size && rng() % 8 == 0) size = rng() % size; // a prefix of the text
        const uint32_t room = rng() % 7;
        const uint32_t seed = rng();
        Parts pa, pb;
        Seed(pa, seed);
        Seed(pb, seed);
        uint32_t ca = room, cb = room;
        ++ctx.cases;
        const bool ra = image(t.c_str(), size, separator, reinterpret_cast<cRZString *>(pa.p), &ca);
        const bool rb = rztext::SplitText(t.c_str(), size, separator, reinterpret_cast<cRZString *>(pb.p), &cb);
        if (ra != rb || ca != cb) {
            ctx.Fail("\"%s\" (%u) sep '%c' room %u: the image's %d / %u parts, ours %d / %u", t.c_str(), size, separator,
                     room, ra, ca, rb, cb);
            continue;
        }
        for (int k = 0; k < kRoom; ++k) {
            const Part &a = pa.p[k], &b = pb.p[k];
            if (a.text != b.text || a.text.capacity() != b.text.capacity() || a.vtable != b.vtable ||
                a.refCount != b.refCount) {
                ctx.Fail("\"%s\" (%u) sep '%c' room %u: part %d the image's \"%s\" (cap %u), ours \"%s\" (cap %u)",
                         t.c_str(), size, separator, room, k, a.text.c_str(), unsigned(a.text.capacity()), b.text.c_str(),
                         unsigned(b.text.capacity()));
                break;
            }
        }
    }
}
