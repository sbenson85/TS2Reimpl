// Differential tests for the character helpers of the text encodings (src/rz/RZTextChars.*; lane OF, for cGZFont's text slots):
// the double-byte lead bytes (0x1b0de2), the length of a character (0x1b0545) and of a count of characters (0x1b0d66) in every
// encoding, and the three character-block predicates (0x1b2518, 0x1b2558, 0x1b257c).
#include "difftest.h"
#include "game/game.h"
#include "rz/RZTextChars.h"

#include <cstdint>
#include <random>
#include <vector>

namespace {

// The system code page global (0xff23a4) is preset: the original would call the GetACP import.
struct CodePage {
    int32_t *global = GameAddress<int32_t>(0xff23a4);
    int32_t saved = *global;
    explicit CodePage(int32_t page) { *global = page; }
    ~CodePage() { *global = saved; }
};

const int32_t kSystemPages[] = {1252, 932, 936, 949, 950, 1251};
const int32_t kEncodings[] = {0, 8, 16, -1, -2, 932, 936, 949, 950, 1252, 874, 437, 12345, 3, -3};

} // namespace

DIFF_TEST(RZTextChars_IsLeadByte, 0x1b0de2)
{
    using Fn = uint32_t(__cdecl *)(uint32_t, int32_t);
    const auto original = ctx.Original<Fn>(0x1b0de2);
    static const int32_t kPages[] = {932, 936, 949, 950, 1252, 0, 8, 16, -1, -2, 874, 951, 931, 937, 948, 12345};
    for (int32_t page : kPages)
        for (uint32_t byte = 0; byte < 0x200; ++byte) {
            ++ctx.cases;
            const bool a = (original(byte, page) & 0xff) != 0, b = RZText::IsLeadByte(byte, page);
            if (a != b) ctx.Fail("lead byte %x in page %d: %u / %u", byte, page, a, b);
        }
    std::mt19937 rng(11);
    for (int i = 0; i < 2000; ++i) {
        ++ctx.cases;
        const uint32_t byte = static_cast<uint32_t>(rng());
        const int32_t page = static_cast<int32_t>(rng() % 3 ? kPages[rng() % 16] : rng());
        const bool a = (original(byte, page) & 0xff) != 0, b = RZText::IsLeadByte(byte, page);
        if (a != b) ctx.Fail("lead byte %x in page %d: %u / %u", byte, page, a, b);
    }
}

DIFF_TEST(RZTextChars_CharLength, 0x1b0545)
{
    CodePage page(1252);
    using Fn = uint32_t(__cdecl *)(const void *, uint32_t, int32_t);
    const auto original = ctx.Original<Fn>(0x1b0545);
    std::mt19937 rng(23);
    static const uint8_t kLeads[] = {0x00, 0x20, 0x7f, 0x80, 0x81, 0x9f, 0xa0, 0xa1, 0xb0, 0xc0, 0xc1, 0xc2, 0xdf, 0xe0, 0xef, 0xf0,
                                     0xf7, 0xf8, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, 0xc8, 0xc9, 0xfc, 0xe3};
    for (int i = 0; i < 60000; ++i) {
        ++ctx.cases;
        uint8_t text[12];
        for (uint8_t &b : text) {
            const unsigned r = rng() % 4;
            b = r == 0 ? kLeads[rng() % sizeof(kLeads)] : r == 1 ? static_cast<uint8_t>(0x80 + rng() % 0x40) : static_cast<uint8_t>(rng());
        }
        text[0] = rng() % 2 ? kLeads[rng() % sizeof(kLeads)] : text[0];
        const uint32_t length = rng() % 8 == 0 ? 0xffffffffu : rng() % 9;
        *page.global = kSystemPages[rng() % 6]; // what encoding -1 means
        const int32_t enc = kEncodings[rng() % (sizeof(kEncodings) / sizeof(kEncodings[0]))];
        const uint32_t a = original(text, length, enc), b = RZText::CharLength(text, length, enc);
        if (a != b) ctx.Fail("CharLength %02x %02x len %u enc %d: %u / %u", text[0], text[1], length, enc, a, b);
    }
}

DIFF_TEST(RZTextChars_CharsLength, 0x1b0d66)
{
    CodePage page(1252);
    using Fn = uint32_t(__cdecl *)(const void *, uint32_t, int32_t);
    const auto original = ctx.Original<Fn>(0x1b0d66);
    std::mt19937 rng(29);
    static const uint8_t kPieces[][4] = {{'a'}, {' '}, {0x80}, {0xa1}, {0xc3, 0xa9}, {0xe2, 0x82, 0xac}, {0xf0, 0x90, 0x80, 0x80},
                                         {0x83, 0x41}, {0xb0, 0xa1}, {0xff}, {0xfc, 0x80, 0x80, 0x80}, {0xc3}, {0x00}};
    for (int i = 0; i < 20000; ++i) {
        ++ctx.cases;
        std::vector<uint8_t> text;
        for (unsigned n = rng() % 8; n > 0; --n) {
            const uint8_t *piece = kPieces[rng() % 13];
            for (unsigned b = 0; b < 4 && (b == 0 || piece[b]); ++b) text.push_back(piece[b]);
        }
        text.resize(text.size() + 12, 0x41); // room for the look-ahead past the end
        const uint32_t count = rng() % 10;
        *page.global = kSystemPages[rng() % 6];
        const int32_t enc = kEncodings[rng() % (sizeof(kEncodings) / sizeof(kEncodings[0]))];
        const uint32_t a = original(text.data(), count, enc), b = RZText::CharsLength(text.data(), count, enc);
        if (a != b) ctx.Fail("CharsLength count %u enc %d: %u / %u", count, enc, a, b);
    }
}

namespace {

void CompareBlock(difftest::Context &ctx, uint32_t rva, bool (*ours)(uint32_t), const char *name)
{
    using Fn = uint32_t(__cdecl *)(uint32_t);
    const auto original = ctx.Original<Fn>(rva);
    for (uint32_t ch = 0; ch < 0x20000; ++ch) {
        ++ctx.cases;
        const bool a = (original(ch) & 0xff) != 0, b = ours(ch);
        if (a != b) ctx.Fail("%s %x: %u / %u", name, ch, a, b);
    }
    std::mt19937 rng(rva);
    for (int i = 0; i < 20000; ++i) {
        ++ctx.cases;
        const uint32_t ch = static_cast<uint32_t>(rng());
        const bool a = (original(ch) & 0xff) != 0, b = ours(ch);
        if (a != b) ctx.Fail("%s %x: %u / %u", name, ch, a, b);
    }
}

} // namespace

DIFF_TEST(RZTextChars_IsIdeograph, 0x1b2518) { CompareBlock(ctx, 0x1b2518, &RZText::IsIdeograph, "IsIdeograph"); }
DIFF_TEST(RZTextChars_IsHangul, 0x1b2558) { CompareBlock(ctx, 0x1b2558, &RZText::IsHangul, "IsHangul"); }
DIFF_TEST(RZTextChars_IsThaiBlock, 0x1b257c) { CompareBlock(ctx, 0x1b257c, &RZText::IsThaiBlock, "IsThaiBlock"); }
