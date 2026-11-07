// Differential test for FindToken (0x534d90), the marker search of the string-token expansion (cTSObject::vf262).
//
// Texts are built from filler words and markers in random case, followed by none, one or two ":number" suffixes (and
// malformed ones: lone colons, letters, enormous numbers, a NUL in the middle of the text); searches start at random
// positions, including past the end. Compared: the returned position and the three outputs (set to a sentinel first,
// so an output the original leaves alone must stay alone).
#include "difftest.h"
#include "simantics/TokenFinder.h"

#include <cstring>
#include <random>
#include <string>

namespace {

struct Answer {
    int32_t position;
    int32_t length, first, second;
};

constexpr int32_t kUntouched = 0x5a5a5a5a;

std::string Pick(std::mt19937 &r, std::initializer_list<const char *> choices)
{
    return *(choices.begin() + r() % choices.size());
}

std::string RandomCase(std::mt19937 &r, std::string s)
{
    for (char &c : s) {
        if (r() % 3 == 0 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) c = static_cast<char>(c ^ 0x20);
    }
    return s;
}

std::string RandomText(std::mt19937 &r, const std::string &marker)
{
    std::string text;
    const int pieces = static_cast<int>(r() % 6);
    for (int i = 0; i < pieces; ++i) {
        switch (r() % 5) {
        case 0:
            text += Pick(r, {"hello ", "a", " ", "$", "$Loc", "$Local", "x:12 ", "::", "5 "});
            break;
        case 1:
            text += RandomCase(r, marker);
            text += Pick(r, {"", "", ":1", ":23", ":4:5", ":", "::", ":6:", ":x", ":7:y", ":99999999999", ":0:00012", ":12:34:56", ":-3", ":1 :2", ": 3"});
            break;
        case 2:
            text += RandomCase(r, marker);
            break;
        case 3:
            if (r() % 6 == 0) text += std::string("ab\0cd", 5);
            else text += Pick(r, {"text", "\xe9\xc9", "Tab\t", "line\n"});
            break;
        default:
            text += RandomCase(r, marker) + Pick(r, {":8", ":8:9", "", ":10"});
            break;
        }
    }
    return text;
}

Answer Run(int32_t (*find)(const std::string *, const std::string *, uint32_t, int32_t *, int32_t *, int32_t *), const std::string &text, const std::string &token,
           uint32_t start, bool withFirst, bool withSecond)
{
    Answer a{0, kUntouched, kUntouched, kUntouched};
    a.position = find(&text, &token, start, &a.length, withFirst ? &a.first : nullptr, withSecond ? &a.second : nullptr);
    return a;
}

} // namespace

DIFF_TEST(TokenFinder_FindToken, 0x534d90)
{
    using Fn = int32_t(__cdecl *)(const std::string *, const std::string *, uint32_t, int32_t *, int32_t *, int32_t *);
    const Fn original = ctx.Original<Fn>(0x534d90);
    const Fn ours = reinterpret_cast<Fn>(&FindToken);
    std::mt19937 r(0x70c);
    for (int i = 0; i < 40000; ++i) {
        const std::string marker = Pick(r, {"$Local", "$Neighbor", "$GlobalString", "$a", "$FullNameLocal", "$Money", "x", ""});
        const std::string token = (r() % 40 == 0) ? std::string() : (r() % 9 == 0 ? RandomCase(r, marker) : marker);
        const std::string text = RandomText(r, marker);
        uint32_t start = 0;
        switch (r() % 5) {
        case 0:
            break;
        case 1:
            start = static_cast<uint32_t>(r() % (text.size() + 2));
            break;
        case 2:
            start = static_cast<uint32_t>(text.size());
            break;
        case 3:
            start = static_cast<uint32_t>(text.size() + r() % 3);
            break;
        default:
            start = static_cast<uint32_t>(r() % 4);
            break;
        }
        const bool withFirst = r() % 3 != 0;
        const bool withSecond = r() % 3 != 0;
        const Answer a = Run(original, text, token, start, withFirst, withSecond);
        const Answer b = Run(ours, text, token, start, withFirst, withSecond);
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0) {
            ctx.Fail("FindToken case %d (text '%s' size %zu, token '%s', start %u, outputs %d%d): original (%d, len %d, %d, %d), ours (%d, len %d, %d, %d)", i, text.c_str(),
                     text.size(), token.c_str(), start, withFirst, withSecond, a.position, a.length, a.first, a.second, b.position, b.length, b.first, b.second);
            break;
        }
    }
}
