// Differential tests for cBaseParser's constructor and what it builds (gz/cBaseParser.h, lane HP):
// cBaseParser::Construct 0xafd800 (the whole object, and the base of a derived parser whose vbtable puts the virtual
// bases further out), ParserNamespace::Construct 0xafdc40, and the 8-byte element vector's _Emplace_reallocate
// 0x14a800. Objects start as the same random junk on both sides; they are compared as bytes with the heap pointers
// replaced by what they lead to (tests/gz_base_parser_form.h).
#include "difftest.h"
#include "gz_base_parser_form.h"

#include <cstring>
#include <random>
#include <vector>

using nGZCommandParser::cBaseParser;
using nGZCommandParser::ParserNamespace;

namespace {

void Junk(std::mt19937 &gen, uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(gen());
}

} // namespace

DIFF_TEST(cBaseParser_ConstructNamespace, 0xafdc40)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xafdc40);
    std::mt19937 gen(0xafdc40);
    for (int round = 0; round < 30; ++round) {
        alignas(8) uint8_t a[sizeof(ParserNamespace)], b[sizeof(ParserNamespace)];
        Junk(gen, a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        void *ra = orig(a);
        void *rb = reinterpret_cast<ParserNamespace *>(b)->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("returned %p / %p, expected this", ra, rb);
        if (parserform::NamespaceForm(a, sizeof(a)) != parserform::NamespaceForm(b, sizeof(b))) ctx.Fail("round %d: the namespaces differ", round);
        parserform::TearDownNamespace(*reinterpret_cast<ParserNamespace *>(a));
        parserform::TearDownNamespace(*reinterpret_cast<ParserNamespace *>(b));
    }
}

// The whole object (mostDerived 1, with random high bits too) and a parser constructed as the base of a derived
// class (mostDerived 0): the derived class's vbtable, stored at +8 first, places the virtual bases `extra` bytes
// further out, so the vtordisps get `extra` and the faces' vtables land past the cBaseParser part.
DIFF_TEST(cBaseParser_Construct, 0xafd800)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xafd800);
    std::mt19937 gen(0xafd800);
    constexpr size_t kSize = sizeof(cBaseParser) + 0x40;
    for (int round = 0; round < 40; ++round) {
        const bool whole = round % 2 == 0;
        const uint32_t flag = whole ? (round % 4 == 0 ? 1u : gen() | 1u) : 0u;
        const int32_t extra = whole ? 0 : 4 * static_cast<int32_t>(round % 8);
        alignas(8) static int32_t derivedVbTable[3];
        derivedVbTable[0] = -8;
        derivedVbTable[1] = cBaseParser::kUnknownOffset + extra;
        derivedVbTable[2] = cBaseParser::kParserOffset + extra;
        alignas(8) uint8_t a[kSize], b[kSize];
        Junk(gen, a, kSize);
        if (!whole) {
            const uint32_t vb = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(derivedVbTable));
            std::memcpy(a + 8, &vb, 4);
        }
        std::memcpy(b, a, kSize);
        void *ra = orig(a, flag);
        void *rb = reinterpret_cast<cBaseParser *>(b)->Construct(flag);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("returned %p / %p, expected this", ra, rb);
        const auto fa = parserform::ParserForm(a, kSize), fb = parserform::ParserForm(b, kSize);
        if (fa != fb) ctx.Fail("round %d (flag %08x, extra %d): the parsers differ from byte %ld", round, flag, extra, parserform::FirstDiff(fa, fb));
        // Sanity on the original's side: what the layout says.
        const auto *pa = reinterpret_cast<const cBaseParser *>(a);
        if (pa->mVersion != -1 || pa->mConditions.size() != 1 || pa->mConditions[0].active != 1 || pa->mFunctions.mEntries.size() != 27)
            ctx.Fail("round %d: the original's parser does not have the layout's fields", round);
        parserform::TearDownParser(*reinterpret_cast<cBaseParser *>(a));
        parserform::TearDownParser(*reinterpret_cast<cBaseParser *>(b));
    }
}
