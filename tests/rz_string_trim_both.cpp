// Differential test for cRZString::TrimBoth 0x1ad83c (ours: cRZIniStringHelpers::TrimBoth, src/rz/cRZIniFileRead.size.cpp):
// the original (which calls the image's TrimLeading 0x1ad11d and TrimTrailing 0x1ad307) and ours on the same strings,
// blank-heavy and some past the small-string buffer; the std::string's size, capacity and bytes through the terminator are
// compared. Left out: the bytes past the terminator. The original's trims erase through std::string::erase (a memmove of
// the kept tail, then the terminator written at the new size); the stale bytes after it are where the erase's memmove
// stopped, and no std::string operation reads them (every read is bounded by the size, every append writes before it
// reads), so they are not state.
#include "difftest.h"

#include "rz/cRZIniFile.h"
#include "rz/cRZString.h"

#include <cstring>
#include <random>
#include <string>

namespace {

union Str {
    cRZString s;
    explicit Str(const std::string &text)
    {
        s.Construct();
        s.mString = text;
    }
    ~Str() { s.Destruct(); }
};

std::string State(const cRZString &s)
{
    std::string out = std::to_string(s.mString.size()) + "/" + std::to_string(s.mString.capacity()) + " '";
    out.append(s.mString.data(), s.mString.size());
    return out + "'";
}

} // namespace

DIFF_TEST(cRZString_TrimBoth, 0x1ad83c)
{
    using TrimFn = void(__thiscall *)(cRZString *);
    const TrimFn original = ctx.Original<TrimFn>(0x1ad83c);
    std::mt19937 g(0x1ad83c);
    static const char kPieces[] = {' ', '\t', ' ', '\t', 'a', 'b', '\n', '\r', '\0', 'x'};
    for (int i = 0; i < 20000; ++i) {
        std::string text;
        const unsigned n = g() % (i % 4 == 0 ? 60 : 12);
        for (unsigned k = 0; k < n; ++k) text += kPieces[g() % sizeof(kPieces)];
        Str a(text), b(text);
        original(&a.s);
        reinterpret_cast<cRZIniStringHelpers *>(&b.s)->TrimBoth();
        ++ctx.cases;
        const bool same = a.s.mString.size() == b.s.mString.size() && a.s.mString.capacity() == b.s.mString.capacity() &&
                          std::memcmp(a.s.mString.data(), b.s.mString.data(), a.s.mString.size() + 1) == 0;
        if (!same) ctx.Fail("case %d: original %s, ours %s", i, State(a.s).c_str(), State(b.s).c_str());
    }
}
