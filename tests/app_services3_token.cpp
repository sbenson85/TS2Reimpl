// Differential test for cRZString's token reader 0x1ad676 (src/rz/cTSIniFile.cpp, cRZStringTokenizer::NextToken):
// random texts over a small alphabet that includes the delimiter, run through repeated calls on a string and a
// reused output string, comparing every result, both strings (text, size, capacity) and the call count.
#include "difftest.h"
#include "rz/cTSIniFile.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using NextTokenFn = bool(__thiscall *)(void *, uint32_t, void *);

struct Str {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    explicit Str(const std::string &text = "")
    {
        std::memset(raw, 0xcd, sizeof(raw));
        S().Construct();
        S().mString = text;
    }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(raw); }
};

bool SameString(const cRZString &a, const cRZString &b)
{
    return a.mVTable == b.mVTable && a.mString == b.mString && a.mString.capacity() == b.mString.capacity() && a.mRefCount == b.mRefCount;
}

std::string RandomText(std::mt19937 &rng, char delimiter)
{
    const char alphabet[] = {'a', 'b', delimiter, delimiter, delimiter, ' ', 'c'};
    std::string s;
    int n = static_cast<int>(rng() % 24);
    for (int i = 0; i < n; ++i) s += alphabet[rng() % sizeof(alphabet)];
    if (rng() % 7 == 0) s += std::string(rng() % 40, 'z'); // heap-sized strings
    return s;
}

} // namespace

DIFF_TEST(AppServices3_NextToken, 0x1ad676)
{
    auto original = ctx.Original<NextTokenFn>(0x1ad676);
    std::mt19937 rng(0x1ad676);
    int trues = 0, falses = 0, emptyDelimiterHits = 0;
    for (int i = 0; i < 4000; ++i) {
        char delimiter = i % 5 == 0 ? ',' : ';';
        std::string text = RandomText(rng, delimiter);
        if (i % 11 == 0) text = std::string(1 + rng() % 4, delimiter) + text; // leading runs
        if (i % 13 == 0) text += std::string(1 + rng() % 4, delimiter);        // trailing runs
        Str a(text), b(text);
        // The output string starts empty, with old text (to see appending and clearing) or with a heap buffer.
        std::string seed = rng() % 3 == 0 ? "" : (rng() % 2 ? "old" : "old text that is long enough to need a heap buffer");
        Str outA(seed), outB(seed);
        for (int step = 0; step < 8; ++step) {
            bool ra = (original(&a.S(), static_cast<uint32_t>(static_cast<unsigned char>(delimiter)), &outA.S()) & 0xff) != 0;
            bool rb = reinterpret_cast<cRZStringTokenizer *>(&b.S())->NextToken(delimiter, &outB.S());
            ++ctx.cases;
            if (ra) ++trues; else ++falses;
            if (ra != rb || !SameString(a.S(), b.S()) || !SameString(outA.S(), outB.S())) {
                ctx.Fail("NextToken(\"%s\", '%c') step %d: %d/%d, string \"%s\" / \"%s\" (cap %zu/%zu), out \"%s\" / \"%s\" (cap %zu/%zu)", text.c_str(), delimiter, step, ra, rb,
                         a.S().mString.c_str(), b.S().mString.c_str(), a.S().mString.capacity(), b.S().mString.capacity(), outA.S().mString.c_str(), outB.S().mString.c_str(),
                         outA.S().mString.capacity(), outB.S().mString.capacity());
                break;
            }
            if (!ra) break;
        }
        if (text.empty()) ++emptyDelimiterHits;
    }
    if (trues < 3000 || falses < 1000) ctx.Fail("only %d true / %d false results", trues, falses);
    (void)emptyDelimiterHits;
}
