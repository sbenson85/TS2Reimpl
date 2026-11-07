// Differential tests for cRZString::Sprintf/SprintfVaList. Needs the original's CRT initialised
// (the harness does this), since the original formats with its own statically linked _vsnprintf_l.
// Covers all three buffer paths: output under 256 chars, exactly 256, and longer (grow-and-retry,
// which also depends on the string's current size).
#include "difftest.h"
#include "rz/cRZString.h"

#include <cstdarg>
#include <random>
#include <string>

namespace {

constexpr uint32_t kVTable = 0xd9b470;
using OrigSprintf = void *(__cdecl *)(void *, const char *, ...);
using OrigSprintfVa = void *(__thiscall *)(void *, const char *, va_list);

cRZString Make(const difftest::Context &ctx, const std::string &s)
{
    cRZString r{};
    r.mVTable = ctx.image.At(kVTable);
    r.mString = s;
    r.mRefCount = 1;
    return r;
}

std::string Text(std::mt19937 &rng, size_t len)
{
    std::string s;
    for (size_t i = 0; i < len; ++i) s += static_cast<char>('a' + rng() % 26);
    return s;
}

// Our SprintfVaList, reached through a va_list like the original Sprintf reaches its own.
void OurVa(cRZString &b, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    b.SprintfVaList(fmt, args);
    va_end(args);
}

} // namespace

DIFF_TEST(cRZString_Sprintf, 0x1ad6f7)
{
    auto orig = ctx.Original<OrigSprintf>(0x1ad6f7);
    auto origVa = ctx.Original<OrigSprintfVa>(0x1ad713);
    std::mt19937 rng(31);
    std::uniform_int_distribution<int> anyInt(INT32_MIN, INT32_MAX);
    std::uniform_real_distribution<double> real(-1e7, 1e7);

    for (int i = 0; i < 6000; ++i) {
        // Starting contents matter: the retry path sizes its first attempt from the current length.
        std::string start = Text(rng, rng() % 4 == 0 ? rng() % 2000 : rng() % 40);
        cRZString a = Make(ctx, start), b = Make(ctx, start);
        void *ret = nullptr;
        int k = static_cast<int>(rng() % 9);
        if (i % 200 == 0) k = 9; // rare: huge output near the 1,000,000 retry cap
        std::string s1 = Text(rng, rng() % 3 == 0 ? rng() % 20000 : rng() % 300), s2 = Text(rng, rng() % 300);
        int x = anyInt(rng), w = static_cast<int>(rng() % 400);
        double d = real(rng);
        std::string exact(256 - 2, 'q'); // with "<>" around it: exactly 256 characters
        std::string longFmt = Text(rng, 200 + rng() % 2000) + " %d";
        switch (k) {
        case 0: ret = orig(&a, "%d items", x); OurVa(b, "%d items", x); break;
        case 1: ret = orig(&a, "%s/%s", s1.c_str(), s2.c_str()); OurVa(b, "%s/%s", s1.c_str(), s2.c_str()); break;
        case 2: ret = orig(&a, "%08X:%u", x, x); OurVa(b, "%08X:%u", x, x); break;
        case 3: ret = orig(&a, "%5.2f%% %g %e", d, d, d); OurVa(b, "%5.2f%% %g %e", d, d, d); break;
        case 4: ret = orig(&a, "%c%c%c", 'a' + k, 'B', '0' + (x & 7)); OurVa(b, "%c%c%c", 'a' + k, 'B', '0' + (x & 7)); break;
        case 5: ret = orig(&a, "%-*s|%*d", w, s2.c_str(), w, x); OurVa(b, "%-*s|%*d", w, s2.c_str(), w, x); break;
        case 6: ret = orig(&a, "<%s>", exact.c_str()); OurVa(b, "<%s>", exact.c_str()); break;
        case 7: ret = orig(&a, longFmt.c_str(), x); OurVa(b, longFmt.c_str(), x); break;
        case 9: {
            // Output of 300k-990k chars with a long format and empty start: whether the largest
            // attempt (from 1.25 * strlen(fmt), doubling, < 1,000,000) fits decides the result.
            std::string fmt = Text(rng, 800 + rng() % 400) + "%s";
            std::string big = Text(rng, 300000 + rng() % 690000);
            a.mString.clear();
            b.mString.clear();
            ret = orig(&a, fmt.c_str(), big.c_str());
            OurVa(b, fmt.c_str(), big.c_str());
            break;
        }
        default:
            // The original SprintfVaList through a va_list, against ours.
            ret = &a;
            {
                auto viaVa = [&](cRZString &t, const char *fmt, ...) {
                    va_list args;
                    va_start(args, fmt);
                    origVa(&t, fmt, args);
                    va_end(args);
                };
                viaVa(a, "%s %d %s", s1.c_str(), x, s2.c_str());
            }
            OurVa(b, "%s %d %s", s1.c_str(), x, s2.c_str());
            break;
        }
        ++ctx.cases;
        if (ret != &a) ctx.Fail("case %d: original return value is not this", k);
        if (a.mString != b.mString)
            ctx.Fail("case %d (start len %zu): original len %zu '%.40s', ours len %zu '%.40s'", k, start.size(),
                     a.mString.size(), a.mString.c_str(), b.mString.size(), b.mString.c_str());
    }

    // Our Sprintf itself: formats through the object's slot 32 (here the original's) and returns this.
    for (int i = 0; i < 200; ++i) {
        cRZString a = Make(ctx, "x"), b = Make(ctx, "x");
        int x = anyInt(rng);
        void *ret = orig(&a, "%d-%s", x, "y");
        cRZString *got = b.Sprintf("%d-%s", x, "y");
        ++ctx.cases;
        if (ret != &a || got != &b || a.mString != b.mString) ctx.Fail("Sprintf wrapper differs for %d", x);
    }
}
