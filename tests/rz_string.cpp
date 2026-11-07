#include "difftest.h"
#include "rz/cRZString.h"

#include <random>
#include <string>

namespace {

constexpr uint32_t kVTable = 0xd9b470; // cRZString vftable (1.18.0.187)

// Random strings over a small alphabet (so equal/case-equal pairs are common), with embedded NULs
// and lengths on both sides of the 15-char small-string boundary.
struct Gen {
    std::mt19937 rng{2024};
    std::string Next()
    {
        static const char alphabet[] = {'a', 'A', 'b', 'B', 'z', '0', '\0', '_'};
        int len = std::uniform_int_distribution<int>(0, 40)(rng);
        if (rng() % 4 == 0) len = std::uniform_int_distribution<int>(14, 17)(rng);
        std::string s;
        for (int i = 0; i < len; ++i) s += alphabet[rng() % (rng() % 3 == 0 ? 8 : 6)];
        return s;
    }
    std::string Variant(const std::string &s)
    {
        switch (rng() % 4) {
        case 0: return s;
        case 1: { std::string t = s; for (auto &c : t) if (rng() % 2) c = static_cast<char>(c ^ (c >= 'A' && c <= 'z' ? 0x20 : 0)); return t; }
        case 2: return s.substr(0, s.size() / 2);
        default: return Next();
        }
    }
    bool Coin() { return rng() % 2 != 0; }
};

cRZString Make(const difftest::Context &ctx, const std::string &s)
{
    cRZString r{};
    r.mVTable = ctx.image.At(kVTable);
    r.mString = s;
    return r;
}

std::string Show(const std::string &s)
{
    std::string out;
    for (char c : s) out += c ? c : '~'; // NUL shown as '~'
    return out;
}

} // namespace

DIFF_TEST(cRZString_Getters, 0x1acd4f)
{
    auto toChar = ctx.Original<const char *(__thiscall *)(const void *)>(0x1acd4f);
    auto strlenFn = ctx.Original<uint32_t(__thiscall *)(const void *)>(0x149d00);
    auto isEmpty = ctx.Original<bool(__thiscall *)(const void *)>(0x1ad026);
    Gen g;
    for (int i = 0; i < 20000; ++i) {
        cRZString s = Make(ctx, g.Next());
        ++ctx.cases;
        if (toChar(&s) != s.ToChar()) ctx.Fail("ToChar/Data differ for '%s'", Show(s.mString).c_str());
        if (strlenFn(&s) != s.Strlen()) ctx.Fail("Strlen differs for '%s'", Show(s.mString).c_str());
        if (isEmpty(&s) != s.IsEmpty()) ctx.Fail("IsEmpty differs for '%s'", Show(s.mString).c_str());
    }
}

DIFF_TEST(cRZString_IsEqual, 0x1ad035)
{
    auto withLen = ctx.Original<bool(__thiscall *)(const void *, const char *, uint32_t, bool)>(0x1ad035);
    auto cstr = ctx.Original<bool(__thiscall *)(const void *, const char *, bool)>(0x1ad096);
    auto str = ctx.Original<bool(__thiscall *)(const void *, const void *, bool)>(0x1ad0b9);
    Gen g;
    for (int i = 0; i < 50000; ++i) {
        std::string a = g.Next(), b = g.Variant(a);
        bool cs = g.Coin();
        cRZString s = Make(ctx, a), o = Make(ctx, b);
        uint32_t len = g.Coin() ? UINT32_MAX : static_cast<uint32_t>(b.size());
        ++ctx.cases;
        if (withLen(&s, b.c_str(), len, cs) != s.IsEqual(b.c_str(), len, cs))
            ctx.Fail("IsEqual(char*,len=%d,cs=%d) '%s' vs '%s'", (int)len, cs, Show(a).c_str(), Show(b).c_str());
        if (cstr(&s, b.c_str(), cs) != s.IsEqual(b.c_str(), cs))
            ctx.Fail("IsEqual(char*,cs=%d) '%s' vs '%s'", cs, Show(a).c_str(), Show(b).c_str());
        if (str(&s, &o, cs) != s.IsEqual(*reinterpret_cast<const cIGZString *>(&o), cs))
            ctx.Fail("IsEqual(cIGZString,cs=%d) '%s' vs '%s'", cs, Show(a).c_str(), Show(b).c_str());
        if (s.mString != a || o.mString != b) ctx.Fail("IsEqual modified a string");
    }
}

DIFF_TEST(cRZString_CompareTo, 0x1acc74)
{
    auto withLen = ctx.Original<int32_t(__thiscall *)(const void *, const char *, uint32_t, bool)>(0x1acc74);
    auto cstr = ctx.Original<int32_t(__thiscall *)(const void *, const char *, bool)>(0x1accdb);
    auto str = ctx.Original<int32_t(__thiscall *)(const void *, const void *, bool)>(0x1acc0c);
    Gen g;
    for (int i = 0; i < 50000; ++i) {
        std::string a = g.Next(), b = g.Variant(a);
        bool cs = g.Coin();
        cRZString s = Make(ctx, a), o = Make(ctx, b);
        uint32_t len = g.Coin() ? UINT32_MAX : static_cast<uint32_t>(b.size());
        ++ctx.cases;
        int32_t want = withLen(&s, b.c_str(), len, cs), got = s.CompareTo(b.c_str(), len, cs);
        if (want != got)
            ctx.Fail("CompareTo(char*,len=%d,cs=%d) '%s' vs '%s': original %d, ours %d", (int)len, cs, Show(a).c_str(), Show(b).c_str(), want, got);
        want = cstr(&s, b.c_str(), cs), got = s.CompareTo(b.c_str(), cs);
        if (want != got)
            ctx.Fail("CompareTo(char*,cs=%d) '%s' vs '%s': original %d, ours %d", cs, Show(a).c_str(), Show(b).c_str(), want, got);
        want = str(&s, &o, cs), got = s.CompareTo(*reinterpret_cast<const cIGZString *>(&o), cs);
        if (want != got)
            ctx.Fail("CompareTo(cIGZString,cs=%d) '%s' vs '%s': original %d, ours %d", cs, Show(a).c_str(), Show(b).c_str(), want, got);
    }
}
