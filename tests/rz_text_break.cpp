// Differential tests for the line-breaking character classes and clusters (src/rz/RZTextBreak.*; lane OF, for cGZFont's line
// breaker): the four classifiers 0x1b2293, 0x1b23d5, 0x1b0959 and 0x1b0a60 (lazily built hash sets in the game), the Thai
// mark test 0x1b258f, the Thai syllable length 0x1b27ae, the cluster length 0x1b215d and the character length 0x1b05ed.
#include "difftest.h"
#include "game/game.h"
#include "rz/RZTextBreak.h"
#include "code_patch.h"

#include <windows.h>

#include <cstdint>
#include <random>
#include <vector>

namespace {

// The image's function-local statics (the sets) are built on first use under the thread's epoch: -1 makes a zero guard take
// the slow path (the same as the harness' own statics test).
void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }
int __cdecl NoAtExit(void *) { return 0; } // the registration would allocate on the CRT heap, and nothing exits

struct Statics {
    // The sets allocate through the image's operator new and delete: ours serve.
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::CodePatch atExit{0x824cbe, reinterpret_cast<const void *>(&NoAtExit)};
    int *tls;
    int saved;
    explicit Statics(difftest::Context &ctx)
    {
        const uint32_t index = *ctx.image.At<uint32_t>(0x10df814);
        auto **array = reinterpret_cast<int **>(__readfsdword(0x2c));
        tls = array[index];
        saved = tls[1];
        tls[1] = -1;
    }
    ~Statics() { tls[1] = saved; }
};

// Characters worth meeting: members of the sets, their neighbours, spaces, breaks, ideographs, Hangul, Thai and plain ones.
const uint16_t kPool[] = {
    0x41,   0x61,   0x30,   0x20,   0x21,   0x28,   0x29,   0x2d,   0x2e,   0x24,   0x0a,   0x0d,   0x09,   0x00,   0x1f,   0x82,   0x85,
    0xa3,   0xa5,   0xb0,   0xb7,   0x2002, 0x2003, 0x200b, 0x200c, 0x2010, 0x2011, 0x2012, 0x2015, 0x2018, 0x2019, 0x201c, 0x201d,
    0x2028, 0x2029, 0x203e, 0x3000, 0x3001, 0x3002, 0x3008, 0x3009, 0x3042, 0x3041, 0x3063, 0x301c, 0x30fc, 0x4e00, 0x4e8c, 0x9fa5,
    0xac00, 0xd7a3, 0x3131, 0xfe31, 0xfe32, 0xfe58, 0xfe5b, 0xff04, 0xff08, 0xff09, 0xff0c, 0xff21, 0xff61, 0xffe1, 0xffe5, 0xe01,
    0xe02,  0xe31,  0xe33,  0xe34,  0xe37,  0xe38,  0xe3a,  0xe40,  0xe46,  0xe47,  0xe48,  0xe4d,  0xe4e,  0xe4f,  0xe50, 0xe2f,
};

uint16_t PoolChar(std::mt19937 &rng)
{
    return rng() % 5 == 0 ? static_cast<uint16_t>(rng()) : kPool[rng() % (sizeof(kPool) / sizeof(kPool[0]))];
}

void CompareClass(difftest::Context &ctx, uint32_t rva, bool (*ours)(uint32_t), const char *name, bool wholeKey, bool wholeArgument)
{
    Statics statics(ctx);
    using Fn = uint32_t(__cdecl *)(uint32_t);
    const auto original = ctx.Original<Fn>(rva);
    for (uint32_t ch = 0; ch < 0x10000; ++ch) {
        ++ctx.cases;
        const bool a = (original(ch) & 0xff) != 0, b = ours(ch);
        if (a != b) ctx.Fail("%s %x: %u / %u", name, ch, a, b);
    }
    if (!wholeArgument) return;
    std::mt19937 rng(rva);
    for (int i = 0; i < 20000; ++i) {
        ++ctx.cases;
        // A character above 16 bits: the sets of the first two classifiers look at the low half only.
        const uint32_t ch = static_cast<uint32_t>(rng());
        const bool a = (original(ch) & 0xff) != 0, b = ours(ch);
        if (a != b) ctx.Fail("%s %x: %u / %u", name, ch, a, b);
    }
    (void)wholeKey;
}

} // namespace

DIFF_TEST(RZTextBreak_CanStartLine, 0x1b2293) { CompareClass(ctx, 0x1b2293, &RZText::CanStartLine, "CanStartLine", false, true); }
DIFF_TEST(RZTextBreak_CanEndLine, 0x1b23d5) { CompareClass(ctx, 0x1b23d5, &RZText::CanEndLine, "CanEndLine", false, true); }
DIFF_TEST(RZTextBreak_IsLineBreakChar, 0x1b0959) { CompareClass(ctx, 0x1b0959, &RZText::IsLineBreakChar, "IsLineBreakChar", true, true); }
// The space test is called with a zero-extended unit only (the cluster length): the callers' contract.
DIFF_TEST(RZTextBreak_IsBreakSpace, 0x1b0a60) { CompareClass(ctx, 0x1b0a60, &RZText::IsBreakSpace, "IsBreakSpace", true, false); }
DIFF_TEST(RZTextBreak_IsThaiMark, 0x1b258f) { CompareClass(ctx, 0x1b258f, &RZText::IsThaiMark, "IsThaiMark", true, true); }

DIFF_TEST(RZTextBreak_ThaiClusterLength, 0x1b27ae)
{
    Statics statics(ctx);
    using Fn = uint32_t(__cdecl *)(const uint16_t *, uint32_t);
    const auto original = ctx.Original<Fn>(0x1b27ae);
    std::mt19937 rng(31);
    static const uint16_t kThai[] = {0xe01, 0xe33, 0xe31, 0xe34, 0xe38, 0xe47, 0xe4d, 0xe4e, 0xe32, 0xe40, 0x41, 0xe30, 0xe4f, 0xe35, 0x0};
    for (int i = 0; i < 40000; ++i) {
        ++ctx.cases;
        uint16_t text[8];
        for (uint16_t &c : text) c = kThai[rng() % (sizeof(kThai) / sizeof(kThai[0]))];
        const uint32_t count = rng() % 6;
        const uint32_t a = original(text, count), b = RZText::ThaiClusterLength(text, count);
        if (a != b) ctx.Fail("ThaiClusterLength %x %x %x %x count %u: %u / %u", text[0], text[1], text[2], text[3], count, a, b);
    }
}

DIFF_TEST(RZTextBreak_ClusterLength, 0x1b215d)
{
    Statics statics(ctx);
    using Fn = uint32_t(__cdecl *)(const uint16_t *, uint32_t);
    const auto original = ctx.Original<Fn>(0x1b215d);
    std::mt19937 rng(37);
    for (int i = 0; i < 80000; ++i) {
        ++ctx.cases;
        uint16_t text[12];
        for (uint16_t &c : text) c = PoolChar(rng);
        const uint32_t count = rng() % 12;
        const uint32_t a = original(text, count), b = RZText::ClusterLength(text, count);
        if (a != b) {
            ctx.Fail("ClusterLength count %u: %u / %u (%x %x %x %x %x)", count, a, b, text[0], text[1], text[2], text[3], text[4]);
        }
    }
}

DIFF_TEST(RZTextBreak_CharUnits, 0x1b05ed)
{
    Statics statics(ctx);
    int32_t *const page = GameAddress<int32_t>(0xff23a4);
    const int32_t savedPage = *page;
    *page = 1252;
    using Fn = uint32_t(__cdecl *)(const void *, uint32_t, int32_t);
    const auto original = ctx.Original<Fn>(0x1b05ed);
    std::mt19937 rng(41);
    static const int32_t kEncodings[] = {8, 8, 8, 16, 16, 16, 0, -1, -2, 932, 936, 949, 950, 1252, 874, 12345};
    static const uint16_t kThai[] = {0xe01, 0xe02, 0xe33, 0xe31, 0xe34, 0xe38, 0xe47, 0xe4d, 0xe4e, 0xe32, 0xe40, 0x41, 0x20, 0xe46,
                                     0x4e00, 0xe48, 0xe49, 0xe35};
    for (int i = 0; i < 60000; ++i) {
        ++ctx.cases;
        const int32_t enc = kEncodings[rng() % (sizeof(kEncodings) / sizeof(kEncodings[0]))];
        std::vector<uint8_t> bytes;
        if (enc == 16) {
            for (int k = 0; k < 8; ++k) {
                const uint16_t c = rng() % 5 ? kThai[rng() % (sizeof(kThai) / sizeof(kThai[0]))] : PoolChar(rng);
                bytes.push_back(static_cast<uint8_t>(c));
                bytes.push_back(static_cast<uint8_t>(c >> 8));
            }
        } else if (enc == 8) {
            for (int k = 0; k < 8; ++k) {
                const uint16_t c = rng() % 3 ? kThai[rng() % (sizeof(kThai) / sizeof(kThai[0]))] : PoolChar(rng);
                if (c < 0x80) bytes.push_back(static_cast<uint8_t>(c));
                else if (c < 0x800) {
                    bytes.push_back(static_cast<uint8_t>(0xc0 | (c >> 6)));
                    bytes.push_back(static_cast<uint8_t>(0x80 | (c & 0x3f)));
                } else {
                    bytes.push_back(static_cast<uint8_t>(0xe0 | (c >> 12)));
                    bytes.push_back(static_cast<uint8_t>(0x80 | ((c >> 6) & 0x3f)));
                    bytes.push_back(static_cast<uint8_t>(0x80 | (c & 0x3f)));
                }
            }
            if (rng() % 8 == 0) bytes[rng() % bytes.size()] = static_cast<uint8_t>(rng());
        } else {
            for (int k = 0; k < 16; ++k) bytes.push_back(static_cast<uint8_t>(rng() % 4 ? 0x41 + rng() % 8 : rng()));
        }
        bytes.resize(bytes.size() + 8, 0x41);
        const uint32_t count = rng() % 12;
        const uint32_t a = original(bytes.data(), count, enc), b = RZText::CharUnits(bytes.data(), count, enc);
        if (a != b) ctx.Fail("CharUnits count %u enc %d: %u / %u (%02x %02x %02x %02x %02x %02x)", count, enc, a, b, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    }
    *page = savedPage;
}
