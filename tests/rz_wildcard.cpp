// Differential tests for the game's wildcard match (src/rz/RZWildcard.cpp): Match (0x9ccc0d) and MatchCase
// (0x9ccea3) on random texts and patterns over a small alphabet, so '*' and '?' meet both cases of letters, runs
// and empty strings often.
#include "difftest.h"
#include "rz/RZWildcard.h"

#include <random>
#include <string>

namespace {

constexpr uint32_t kMatch = 0x9ccc0d, kMatchCase = 0x9ccea3;

std::string Draw(std::mt19937 &rng, const char *alphabet, size_t letters, size_t maxLength)
{
    std::string s;
    size_t n = rng() % (maxLength + 1);
    for (size_t k = 0; k < n; ++k) s += alphabet[rng() % letters];
    return s;
}

// A pattern often derived from the text (so matches are common): characters replaced by '?', runs by '*'.
std::string PatternFor(std::mt19937 &rng, const std::string &text)
{
    if (rng() % 3 == 0) return Draw(rng, "aAbB*?", 6, 7);
    std::string p;
    for (size_t k = 0; k < text.size(); ++k) {
        switch (rng() % 7) {
        case 0: p += '?'; break;
        case 1: p += '*'; k += rng() % 3; break;
        case 2: p += static_cast<char>(text[k] ^ 0x20); break; // the other case
        default: p += text[k];
        }
    }
    if (rng() % 4 == 0) p += '*';
    if (rng() % 6 == 0) p = '*' + p;
    return p;
}

} // namespace

DIFF_TEST(RZWildcard_Match, kMatch)
{
    auto original = ctx.Original<bool(__cdecl *)(const char *, const char *)>(kMatch);
    std::mt19937 rng(kMatch);
    int matched = 0;
    for (int i = 0; i < 20000; ++i) {
        std::string text = Draw(rng, "aAbB", 4, 7);
        std::string pattern = PatternFor(rng, text);
        ++ctx.cases;
        bool ra = original(text.c_str(), pattern.c_str()), rb = RZWildcard::Match(text.c_str(), pattern.c_str());
        if (ra != rb) ctx.Fail("'%s' ~ '%s': %d / %d", text.c_str(), pattern.c_str(), ra, rb);
        matched += ra;
    }
    if (matched < 3000) ctx.Fail("only %d matches", matched);
}

DIFF_TEST(RZWildcard_MatchCase, kMatchCase)
{
    auto original = ctx.Original<bool(__cdecl *)(const char *, const char *, uint8_t)>(kMatchCase);
    std::mt19937 rng(kMatchCase);
    int matched = 0, onlyCaseless = 0;
    for (int i = 0; i < 6000; ++i) {
        std::string text = Draw(rng, "aAbB", 4, 7);
        // Long texts now and then, so the copies leave the strings' small buffers.
        if (i % 50 == 0) text += std::string(20, 'a');
        std::string pattern = PatternFor(rng, text);
        static const uint8_t kFlags[] = {0, 1, 2, 0x80, 0xff};
        uint8_t flag = kFlags[rng() % 5];
        ++ctx.cases;
        bool ra = original(text.c_str(), pattern.c_str(), flag), rb = RZWildcard::MatchCase(text.c_str(), pattern.c_str(), flag);
        if (ra != rb) ctx.Fail("'%s' ~ '%s' (%u): %d / %d", text.c_str(), pattern.c_str(), flag, ra, rb);
        matched += ra;
        if (ra && !flag && !original(text.c_str(), pattern.c_str(), 1)) ++onlyCaseless;
    }
    if (matched < 1000 || onlyCaseless < 200) ctx.Fail("coverage: %d matches, %d only without case", matched, onlyCaseless);
}
