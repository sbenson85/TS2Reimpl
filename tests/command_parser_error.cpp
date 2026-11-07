// Differential tests for the parser's error machinery: the helper that throws a cError (0xace7d0, "throw
// unless"; the constructor it uses, 0x1d099e, is verified in tests/rz_framework_helpers.cpp). What the
// original throws is read from the SEH exception record (tests/command_parser.h, Thrown): the object and the
// type names and sizes.

#include "command_parser.h"
#include "gz/CommandParser.h"

#include <cstring>
#include <random>

namespace cp = nGZCommandParser;
using namespace cptest;

namespace {

constexpr uint32_t kErrorBufferRva = 0x10cb840;
constexpr uint32_t kThrowBufferRva = 0x10f6bc8;

using ThrowFn = void(__cdecl *)(uint32_t, const char *, ...);

// A format string and the four words it can consume (small integers, or pointers to strings).
struct FormatCase {
    const char *fmt;
    uint32_t words[4];
};

const char *const kLong = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-the quick brown fox jumps over the lazy dog";

FormatCase MakeCase(std::mt19937 &gen, int index, const std::string &big)
{
    auto str = [&](const char *s) { return reinterpret_cast<uint32_t>(s); };
    auto num = [&]() { return gen() % 4 == 0 ? gen() : gen() % 300; };
    switch (index) {
    case 0: return {"plain text", {num(), num(), num(), num()}};
    case 1: return {"", {num(), num(), num(), num()}};
    case 2: return {"%s", {str("hello"), 0, 0, 0}};
    case 3: return {"%s", {str(""), 0, 0, 0}};
    case 4: return {"%s %d", {str("count"), num(), 0, 0}};
    case 5: return {"%s: %s (%d)", {str("file"), str("a.txt"), num(), 0}};
    case 6: return {"%d:%x:%c:%u", {num(), num(), 'q' + gen() % 4, num()}};
    case 7: return {"%08x-%-6d|%+d", {num(), num(), num(), 0}};
    case 8: return {"%s", {str(kLong), 0, 0, 0}};
    case 9: return {"%s%s%s", {str(kLong), str(kLong), str(kLong), 0}};
    case 10: return {"%s", {str(big.c_str()), 0, 0, 0}};
    case 11: return {"%%d is a percent and %s", {str("done"), 0, 0, 0}};
    case 12: {
        double d = (gen() % 2000) / 8.0 - 100.0;
        uint32_t w[2];
        std::memcpy(w, &d, 8);
        return {"%.3f and %d", {w[0], w[1], num(), 0}};
    }
    // The message goes through a format string again (ThrowUnless): '%' in an argument.
    case 13: return {"%s", {str("100%"), 0, 0, 0}};
    case 14: return {"%s", {str("50%d"), 0, 0, 0}};
    case 15: return {"%s", {str("a%d b%x c%u"), 0, 0, 0}};
    case 16: return {"value %s here", {str("%d%d"), 0, 0, 0}};
    case 17: return {"%s", {str("%s"), 0, 0, 0}};
    case 18: return {"%s", {str("%x %d"), 0, 0, 0}};
    // Conversions that skip the first words with a zero precision, to read the later ones: the
    // half-built error object (its vtable word, zeros, the capacity). The fourth word is the
    // argument pointer, which differs between the sides, so it is skipped, not printed.
    case 19: return {"%s", {str("v%.0s%.0s%.0s%.0s%x"), 0, 0, 0}};
    case 20: return {"%s", {str("%.0s%.0s%.0s%.0s%.0s%d%d%d%d%d%x"), 0, 0, 0}};
    case 21: return {"%s", {str("%.0s%.0s%.0s%.0s%.0s%d%d%d%d%d%d"), 0, 0, 0}};
    default: return {"%s", {str("a%%b"), 0, 0, 0}};
    }
}

constexpr int kCases = 23;
// Lengths of the long message (case 10). The buffer holds 2048 bytes: 2048 and more fill it with no
// terminator (the bytes after the buffer in the game image are zero, which ends the strlen).
const std::vector<size_t> kLengths = {100, 1500, 2046, 2047, 2048, 2500};

} // namespace

DIFF_TEST(CommandParser_ThrowUnless, 0xace7d0)
{
    auto orig = ctx.Original<ThrowFn>(0xace7d0);
    std::mt19937 gen(0xace7d0);
    const uint32_t flags[] = {0, 1, 0x100, 0xff, 0x01000001, 0x12345600, 2};
    for (int round = 0; round < 6; ++round) {
        for (int k = 0; k < kCases; ++k) {
            const std::string text(k == 10 ? kLengths[round] : 2000, 'q');
            FormatCase c = MakeCase(gen, k, text);
            uint32_t ok = flags[gen() % (sizeof(flags) / sizeof(flags[0]))];
            uint8_t *b1 = ctx.image.At<uint8_t>(kErrorBufferRva), *b2 = ctx.image.At<uint8_t>(kThrowBufferRva);
            for (int b = 0; b < 0x800; ++b) b1[b] = static_cast<uint8_t>(0x41 + gen() % 26), b2[b] = static_cast<uint8_t>(0x41 + gen() % 26);
            char what[100];
            std::snprintf(what, sizeof(what), "ThrowUnless case %d.%d flag %x", round, k, ok);
            Differential(
                ctx, what, {{b1, 0x800}, {b2, 0x800}},
                [&] {
                    return RunGuarded([&] { orig(ok, c.fmt, c.words[0], c.words[1], c.words[2], c.words[3]); });
                },
                [&] {
                    // Called through the original's signature, so that the word on the stack is the
                    // whole of `ok` and the byte test is ours.
                    return RunGuarded([&] { reinterpret_cast<ThrowFn>(&cp::ThrowUnless)(ok, c.fmt, c.words[0], c.words[1], c.words[2], c.words[3]); });
                });
        }
    }
}
