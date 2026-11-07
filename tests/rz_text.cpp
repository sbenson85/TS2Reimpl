// Differential tests for the text encoding conversion (src/rz/RZTextConvert.*): every pair of
// encodings (ASCII, UTF-8, UTF-16, code pages 1252/932/437/950, the -1/-2 aliases), on valid,
// invalid, truncated and over-long UTF-8, random UTF-16 including surrogates, small and zero output
// capacities, and in-place (src == dst) calls. Outputs are compared in full, canaries included.
#include "difftest.h"
#include "rz/RZTextConvert.h"
#include "rz/cRZString.h"

#include <cstring>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

namespace {

using ConvertFn = uint32_t(__cdecl *)(const void *, uint32_t, int32_t, void *, uint32_t *, int32_t);

const int32_t kEncodings[] = {0, 8, 16, -1, -2, 1252, 932, 437, 950};

void Push(std::vector<uint8_t> &v, std::initializer_list<uint32_t> bytes)
{
    for (uint32_t b : bytes) v.push_back(static_cast<uint8_t>(b));
}

void AppendUtf8(std::vector<uint8_t> &v, std::mt19937 &rng)
{
    switch (rng() % 12) {
    case 0: case 1: case 2: v.push_back(static_cast<uint8_t>(0x20 + rng() % 0x5f)); break; // ASCII
    case 3: { uint32_t c = 0x80 + rng() % 0x780; v.push_back(static_cast<uint8_t>(0xc0 | (c >> 6))); v.push_back(static_cast<uint8_t>(0x80 | (c & 0x3f))); break; }
    case 4: { uint32_t c = 0x800 + rng() % 0xf800; Push(v, {0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f)}); break; }
    case 5: { uint32_t c = 0x10000 + rng() % 0x100000; Push(v, {0xf0 | (c >> 18), 0x80 | ((c >> 12) & 0x3f), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f)}); break; }
    case 6: { // old 5- and 6-byte forms, sometimes overlong
        int n = 5 + rng() % 2;
        v.push_back(n == 5 ? 0xf8 | (rng() % 4) : 0xfc | (rng() % 2));
        for (int i = 1; i < n; ++i) v.push_back(0x80 | (rng() % 0x40));
        break;
    }
    case 7: v.push_back(static_cast<uint8_t>(0x80 + rng() % 0x80)); break; // stray continuation / lead
    case 8: v.push_back(static_cast<uint8_t>(0xc0 + rng() % 2)); v.push_back(0x80 | (rng() % 0x40)); break; // overlong 2-byte
    case 9: v.push_back(0xe0); v.push_back(0x80 + rng() % 0x20); v.push_back(0x80 | (rng() % 0x40)); break; // overlong 3-byte
    case 10: v.push_back(0xf0); v.push_back(0x80 + rng() % 0x10); v.push_back(0x80); v.push_back(0x80); break; // overlong 4-byte
    default: v.push_back(static_cast<uint8_t>(rng())); break;
    }
}

std::vector<uint8_t> RandomInput(std::mt19937 &rng, int32_t encoding, uint32_t &length)
{
    std::vector<uint8_t> v;
    int kind = rng() % 4;
    uint32_t pieces = rng() % 4 == 0 ? rng() % 400 : rng() % 24;
    if (encoding == 16) {
        for (uint32_t i = 0; i < pieces; ++i) {
            uint16_t u = kind == 0 ? static_cast<uint16_t>(rng() % 0x80)
                       : rng() % 5 == 0 ? static_cast<uint16_t>(0xd800 + rng() % 0x800)
                       : static_cast<uint16_t>(rng());
            v.push_back(static_cast<uint8_t>(u));
            v.push_back(static_cast<uint8_t>(u >> 8));
        }
        length = static_cast<uint32_t>(v.size() / 2);
        return v;
    }
    for (uint32_t i = 0; i < pieces; ++i) {
        if (kind == 0) v.push_back(static_cast<uint8_t>(rng() % 0x80));
        else if (kind == 1) v.push_back(static_cast<uint8_t>(rng()));
        else AppendUtf8(v, rng);
    }
    if (kind == 3 && !v.empty()) v.pop_back(); // truncate the last sequence sometimes
    length = static_cast<uint32_t>(v.size());
    return v;
}

uint32_t PickCapacity(std::mt19937 &rng, uint32_t length)
{
    switch (rng() % 5) {
    case 0: return 0;
    case 1: return rng() % 4;
    case 2: return length / 2;
    case 3: return length;
    default: return length * 3 + 8;
    }
}

} // namespace

DIFF_TEST(RZText_Convert, 0x1afd58)
{
    auto orig = ctx.Original<ConvertFn>(0x1afd58);
    auto via = ctx.Original<ConvertFn>(0x1b0455);
    std::mt19937 rng(0x7e47);
    for (int i = 0; i < 120000; ++i) {
        int32_t from = kEncodings[rng() % std::size(kEncodings)];
        int32_t to = kEncodings[rng() % std::size(kEncodings)];
        bool viaUtf16 = i % 8 == 7;
        uint32_t length = 0;
        std::vector<uint8_t> input = RandomInput(rng, from, length);
        uint32_t capacity = PickCapacity(rng, length);
        // Output buffer: room for capacity units of up to 2 bytes, canaries after.
        size_t bytes = capacity * 2 + 32;
        std::vector<uint8_t> outA(bytes, 0xA5), outB(bytes, 0xA5);
        uint32_t lenA = capacity, lenB = capacity;
        uint32_t ra, rb;
        if (i % 50 == 0 && !input.empty()) {
            // In place: src == dst.
            outA.assign(input.begin(), input.end());
            outA.resize(bytes + input.size(), 0xA5);
            outB = outA;
            ra = orig(outA.data(), length, from, outA.data(), &lenA, to);
            rb = RZText::Convert(outB.data(), length, from, outB.data(), &lenB, to);
        } else if (viaUtf16) {
            ra = via(input.data(), length, from, outA.data(), &lenA, to);
            rb = RZText::ConvertViaUtf16(input.data(), length, from, outB.data(), &lenB, to);
        } else {
            ra = orig(input.data(), length, from, outA.data(), &lenA, to);
            rb = RZText::Convert(input.data(), length, from, outB.data(), &lenB, to);
        }
        ++ctx.cases;
        if (ra != rb || lenA != lenB || outA != outB) {
            ctx.Fail("%s(%d -> %d, %u units, capacity %u): original %u/%u, ours %u/%u%s", viaUtf16 ? "ViaUtf16" : "Convert", from, to,
                     length, capacity, ra, lenA, rb, lenB, outA != outB ? " (output differs)" : "");
        }
    }
}

DIFF_TEST(RZText_Strings, 0x1afc1f)
{
    auto convertString = ctx.Original<uint32_t(__cdecl *)(const cIGZString &, void *, uint32_t *, int32_t)>(0x1afc1f);
    auto toString = ctx.Original<bool(__cdecl *)(const cIGZString &, cIGZString &, int32_t)>(0x1b025d);
    auto toUtf16 = ctx.Original<std::u16string *(__cdecl *)(std::u16string *, const cIGZString &)>(0x1c2ad8);
    std::mt19937 rng(0x5781);
    for (int i = 0; i < 20000; ++i) {
        uint32_t length = 0;
        std::vector<uint8_t> input = RandomInput(rng, 8, length);
        if (i % 7 == 0) { // long strings take the heap paths (> 0x100 units, >= 0x200/6 bytes)
            for (int k = 0; k < 300; ++k) AppendUtf8(input, rng);
        }
        cRZString s{};
        s.Construct();
        s.mString.assign(reinterpret_cast<const char *>(input.data()), input.size());
        int32_t to = kEncodings[rng() % std::size(kEncodings)];

        // ConvertString
        uint32_t capacity = PickCapacity(rng, static_cast<uint32_t>(input.size()));
        std::vector<uint8_t> outA(capacity * 2 + 32, 0xA5), outB = outA;
        uint32_t lenA = capacity, lenB = capacity;
        uint32_t ra = convertString(*s.AsInterface(), outA.data(), &lenA, to);
        uint32_t rb = RZText::ConvertString(*s.AsInterface(), outB.data(), &lenB, to);
        ++ctx.cases;
        if (ra != rb || lenA != lenB || outA != outB) ctx.Fail("ConvertString(%zu bytes -> %d): %u/%u vs %u/%u", input.size(), to, ra, lenA, rb, lenB);

        // ConvertStringToString (also onto itself)
        cRZString da{}, db{};
        da.Construct("previous");
        db.Construct("previous");
        bool self = i % 10 == 0;
        cRZString sa{}, sb{};
        sa.Construct();
        sb.Construct();
        sa.mString = sb.mString = s.mString;
        bool ba = toString(*sa.AsInterface(), self ? *sa.AsInterface() : *da.AsInterface(), to);
        bool bb = RZText::ConvertStringToString(*sb.AsInterface(), self ? *sb.AsInterface() : *db.AsInterface(), to);
        ++ctx.cases;
        if (ba != bb || da.mString != db.mString || sa.mString != sb.mString) ctx.Fail("ConvertStringToString(%zu bytes -> %d)%s", input.size(), to, self ? " in place" : "");

        // ToUtf16
        alignas(std::u16string) unsigned char ua[sizeof(std::u16string)], ub[sizeof(std::u16string)];
        std::memset(ua, 0xCD, sizeof(ua));
        std::memset(ub, 0xCD, sizeof(ub));
        auto *pa = toUtf16(reinterpret_cast<std::u16string *>(ua), *s.AsInterface());
        auto *pb = RZText::ToUtf16(reinterpret_cast<std::u16string *>(ub), *s.AsInterface());
        ++ctx.cases;
        if (pa != reinterpret_cast<std::u16string *>(ua) || pb != reinterpret_cast<std::u16string *>(ub) || *pa != *pb || pa->capacity() != pb->capacity()) {
            ctx.Fail("ToUtf16(%zu bytes): %zu/%zu units", input.size(), pa->size(), pb->size());
        }
        pa->~basic_string();
        pb->~basic_string();
        s.Destruct();
        da.Destruct();
        db.Destruct();
        sa.Destruct();
        sb.Destruct();
    }
}

DIFF_TEST(RZText_SystemCodePage, 0x1b0690)
{
    auto orig = ctx.Original<int32_t(__cdecl *)()>(0x1b0690);
    int32_t *cached = ctx.image.At<int32_t>(0xff23a4);
    for (int i = 0; i < 4; ++i) {
        int32_t start = i % 2 ? -3 : 1250 + i;
        *cached = start;
        int32_t a = orig();
        int32_t afterA = *cached;
        *cached = start;
        int32_t b = RZText::SystemCodePage();
        ++ctx.cases;
        if (a != b || afterA != *cached) ctx.Fail("SystemCodePage from %d: %d vs %d", start, a, b);
    }
    *cached = -3;
}
