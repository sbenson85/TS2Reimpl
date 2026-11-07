// Differential tests for cRZString::StartsWith (0x1acb54 text/length/flag, 0x1acb2c other string/flag).
// Both objects carry the original vtable, so ours reaches the original slots for the nested calls.
#include "difftest.h"
#include "rz/cRZString.h"

#include <cstring>
#include <random>
#include <string>

namespace {

constexpr uint32_t kVTable = 0xd9b470;
constexpr uint32_t kStartsWithText = 0x1acb54, kStartsWithObject = 0x1acb2c;

using TextFn = uint32_t(__thiscall *)(const void *, const char *, uint32_t, bool);
using ObjectFn = uint32_t(__thiscall *)(const void *, const void *, bool);

cRZString *New(const difftest::Context &ctx, const std::string &s)
{
    auto *r = new cRZString{};
    r->mVTable = ctx.image.At(kVTable);
    r->mString = s;
    r->mRefCount = 1;
    return r;
}

std::string RandomString(std::mt19937 &rng, int maxLen)
{
    static const char alphabet[] = {'a', 'A', 'b', 'B', 'z', 'Z', '0', '\0', '\xC9', '\xE9', ' '};
    int len = std::uniform_int_distribution<int>(0, maxLen)(rng);
    if (rng() % 4 == 0) len = std::uniform_int_distribution<int>(14, 17)(rng);
    std::string s;
    for (int i = 0; i < len; ++i) s += alphabet[rng() % (rng() % 5 == 0 ? 11 : 7)];
    return s;
}

std::string FlipCase(std::mt19937 &rng, std::string s)
{
    for (char &c : s) {
        if (c >= 'a' && c <= 'z' && rng() % 2) c = static_cast<char>(c - 32);
        else if (c >= 'A' && c <= 'Z' && rng() % 2) c = static_cast<char>(c + 32);
    }
    return s;
}

std::string Show(const std::string &s)
{
    std::string out;
    for (char c : s) out += c ? c : '~';
    return out;
}

// The text to test against: usually a (case-flipped) prefix of the string, sometimes unrelated, sometimes longer.
std::string PickPrefix(std::mt19937 &rng, const std::string &start)
{
    switch (rng() % 5) {
    case 0: return RandomString(rng, 20);
    case 1: return FlipCase(rng, start) + RandomString(rng, 2); // may be longer than the string
    default: {
        std::string p = start.substr(0, rng() % (start.size() + 1));
        if (rng() % 3 == 0 && !p.empty()) p.back() = static_cast<char>(p.back() ^ 1); // near miss
        return rng() % 2 ? FlipCase(rng, p) : p;
    }
    }
}

} // namespace

DIFF_TEST(cRZString_StartsWith, kStartsWithText)
{
    auto text = ctx.Original<TextFn>(kStartsWithText);
    auto object = ctx.Original<ObjectFn>(kStartsWithObject);
    std::mt19937 rng(0x1acb54);
    for (int i = 0; i < 30000; ++i) {
        std::string start = RandomString(rng, 30);
        std::string prefix = PickPrefix(rng, start);
        cRZString *self = New(ctx, start);
        cRZString *other = New(ctx, prefix);
        for (int flag = 0; flag < 2; ++flag) {
            bool cs = flag != 0;
            uint32_t len = static_cast<uint32_t>(prefix.size());
            bool a = (text(self, prefix.c_str(), len, cs) & 0xff) != 0;
            bool b = self->StartsWith(prefix.c_str(), len, cs);
            bool c = (object(self, other, cs) & 0xff) != 0;
            bool d = self->StartsWith(*other->AsInterface(), cs);
            // A shorter length than the text's own, to reach prefix checks of cut text.
            uint32_t cut = rng() % (len + 1);
            bool e = (text(self, prefix.c_str(), cut, cs) & 0xff) != 0;
            bool f = self->StartsWith(prefix.c_str(), cut, cs);
            ctx.cases += 3;
            if (a != b) ctx.Fail("StartsWith(text) '%s' vs '%s' cs %d: %d/%d", Show(start).c_str(), Show(prefix).c_str(), cs, a, b);
            if (c != d) ctx.Fail("StartsWith(obj) '%s' vs '%s' cs %d: %d/%d", Show(start).c_str(), Show(prefix).c_str(), cs, c, d);
            if (e != f) ctx.Fail("StartsWith(text, %u) '%s' vs '%s' cs %d: %d/%d", cut, Show(start).c_str(), Show(prefix).c_str(), cs, e, f);
            if (a != c) ctx.Fail("original text/object forms disagree on '%s' vs '%s'", Show(start).c_str(), Show(prefix).c_str());
        }
        if (self->mString != start || self->mRefCount != 1 || other->mString != prefix || other->mRefCount != 1)
            ctx.Fail("StartsWith modified an operand");
        delete self;
        delete other;
    }
}
