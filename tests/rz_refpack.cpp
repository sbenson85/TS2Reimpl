// Differential tests for the RefPack (QFS) decompressor. Streams are generated randomly from all
// opcode types (and both header forms), so every branch is covered without needing real game data.
// Output buffers are preceded by the same 140 KB of random history in both runs, so back-references
// reaching before the output start (allowed by the original, which doesn't check) compare equal.
#include "difftest.h"
#include "rz/RefPack.h"

#include <algorithm>
#include <random>
#include <vector>

namespace {

using OrigDecompress = int32_t(__cdecl *)(const uint8_t *, uint32_t *, uint8_t *, int32_t);
constexpr size_t kHistory = 140 * 1024;

struct Stream {
    std::vector<uint8_t> bytes;
    uint32_t outputSize = 0; // bytes the opcodes produce
};

Stream Generate(std::mt19937 &rng)
{
    Stream s;
    std::vector<uint8_t> body;
    auto lit = [&](uint32_t n) { for (uint32_t i = 0; i < n; ++i) body.push_back(static_cast<uint8_t>(rng())); s.outputSize += n; };
    int ops = static_cast<int>(rng() % 60);
    for (int i = 0; i < ops; ++i) {
        switch (rng() % 4) {
        case 0: { // 2-byte
            uint32_t literals = rng() % 4, length = rng() % 8, offset = rng() % 1024;
            body.push_back(static_cast<uint8_t>(((offset >> 3) & 0x60) | (length << 2) | literals));
            body.push_back(static_cast<uint8_t>(offset));
            lit(literals);
            s.outputSize += length + 3;
            break;
        }
        case 1: { // 3-byte
            uint32_t literals = rng() % 4, length = rng() % 64, offset = rng() % 16384;
            body.push_back(static_cast<uint8_t>(0x80 | length));
            body.push_back(static_cast<uint8_t>((literals << 6) | (offset >> 8)));
            body.push_back(static_cast<uint8_t>(offset));
            lit(literals);
            s.outputSize += length + 4;
            break;
        }
        case 2: { // 4-byte
            uint32_t literals = rng() % 4, length = rng() % 1024, offset = rng() % 131072;
            body.push_back(static_cast<uint8_t>(0xC0 | ((offset >> 12) & 0x10) | ((length >> 6) & 0x0C) | literals));
            body.push_back(static_cast<uint8_t>(offset >> 8));
            body.push_back(static_cast<uint8_t>(offset));
            body.push_back(static_cast<uint8_t>(length));
            lit(literals);
            s.outputSize += length + 5;
            break;
        }
        default: { // literal run
            uint32_t n = rng() % 28;
            body.push_back(static_cast<uint8_t>(0xE0 | n));
            lit((n + 1) * 4);
            break;
        }
        }
    }
    uint32_t tail = rng() % 4;
    body.push_back(static_cast<uint8_t>(0xFC | tail));
    lit(tail);

    // Header; the declared size is usually right but not always (it's returned, not checked).
    uint32_t declared = rng() % 8 ? s.outputSize : static_cast<uint32_t>(rng() & 0xFFFFFF);
    bool longHeader = rng() % 2;
    s.bytes.push_back(static_cast<uint8_t>(longHeader ? 0x11 : 0x10));
    s.bytes.push_back(0xFB);
    if (longHeader) { s.bytes.push_back(0); s.bytes.push_back(0); s.bytes.push_back(0); }
    s.bytes.push_back(static_cast<uint8_t>(declared >> 16));
    s.bytes.push_back(static_cast<uint8_t>(declared >> 8));
    s.bytes.push_back(static_cast<uint8_t>(declared));
    s.bytes.insert(s.bytes.end(), body.begin(), body.end());
    s.bytes.resize(s.bytes.size() + 16, 0); // padding: never read by a well-formed stream
    return s;
}

} // namespace

DIFF_TEST(RefPack_Decompress, 0x9cc369)
{
    auto orig = ctx.Original<OrigDecompress>(0x9cc369);
    std::mt19937 rng(77);
    std::vector<uint8_t> history(kHistory);
    for (auto &b : history) b = static_cast<uint8_t>(rng());

    for (int i = 0; i < 4000; ++i) {
        Stream s = Generate(rng);
        // Capacity: usually enough, sometimes short (the original returns -1 part-way through).
        int32_t capacity = rng() % 5 ? static_cast<int32_t>(s.outputSize + rng() % 16)
                                     : static_cast<int32_t>(rng() % (s.outputSize + 1));
        std::vector<uint8_t> a(history), b(history);
        a.resize(kHistory + s.outputSize + 64, 0xAB);
        b.resize(kHistory + s.outputSize + 64, 0xAB);
        uint32_t consumedA = 0xDEAD, consumedB = 0xDEAD;
        bool track = rng() % 4 != 0; // the consumed pointer is optional
        int32_t ra = orig(s.bytes.data(), track ? &consumedA : nullptr, a.data() + kHistory, capacity);
        int32_t rb = RefPack::Decompress(s.bytes.data(), track ? &consumedB : nullptr, b.data() + kHistory, capacity);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("case %d: returned %d vs %d (output %u, capacity %d)", i, ra, rb, s.outputSize, capacity);
        if (consumedA != consumedB) ctx.Fail("case %d: consumed %u vs %u", i, consumedA, consumedB);
        if (a != b) ctx.Fail("case %d: output bytes differ", i);
    }
}

namespace {

// Test inputs of different character: random, repetitive with variation, text-like, zeros.
std::vector<uint8_t> Input(std::mt19937 &rng, size_t length)
{
    std::vector<uint8_t> v(length);
    switch (rng() % 4) {
    case 0: for (auto &b : v) b = static_cast<uint8_t>(rng()); break;
    case 1: {
        std::vector<uint8_t> pattern(1 + rng() % 40);
        for (auto &b : pattern) b = static_cast<uint8_t>(rng());
        for (size_t i = 0; i < length; ++i) v[i] = rng() % 50 ? pattern[i % pattern.size()] : static_cast<uint8_t>(rng());
        break;
    }
    case 2: {
        static const char *words[] = {"the ", "sim ", "is ", "hungry ", "cRZString ", "neighborhood ", "lot ", "\n", "0", "1"};
        size_t i = 0;
        while (i < length) for (const char *w = words[rng() % 10]; *w && i < length; ++w) v[i++] = static_cast<uint8_t>(*w);
        break;
    }
    default: break; // zeros
    }
    return v;
}

} // namespace

DIFF_TEST(RefPack_Compress, 0x9cc54d)
{
    using OrigCompress = int32_t(__cdecl *)(const uint8_t *, int32_t, uint8_t *, const int32_t *);
    auto orig = ctx.Original<OrigCompress>(0x9cc54d);
    std::mt19937 rng(2025);
    for (int i = 0; i < 400; ++i) {
        // Mostly small inputs; some cross the 128 KB window.
        size_t length = i % 40 == 0 ? 0x20000 + rng() % 0x30000 : rng() % 6 == 0 ? rng() % 20000 : rng() % 600;
        auto input = Input(rng, length);
        input.resize(length + 4, 0x5A); // the encoder hashes up to 2 bytes past the end
        int32_t flags = static_cast<int32_t>(rng() % 4);
        size_t capacity = length * 15 / 10 + 0x400;
        std::vector<uint8_t> a(capacity, 0xCD), b(capacity, 0xCD);
        int32_t ra = orig(input.data(), static_cast<int32_t>(length), a.data(), &flags);
        int32_t rb = RefPack::Compress(input.data(), static_cast<int32_t>(length), b.data(), &flags);
        ++ctx.cases;
        if (ra != rb || a != b) {
            size_t at = 0;
            while (at < capacity && a[at] == b[at]) ++at;
            ctx.Fail("case %d (length %zu, flags %d): sizes %d vs %d, first difference at byte %zu", i, length, flags, ra, rb, at);
            continue;
        }
        // Sanity: it round-trips through the decompressor.
        std::vector<uint8_t> back(length + 16);
        int32_t n = RefPack::Decompress(a.data(), nullptr, back.data(), static_cast<int32_t>(length));
        back.resize(length);
        ++ctx.cases;
        if (n != static_cast<int32_t>(length) || std::equal(back.begin(), back.end(), input.begin()) == false)
            ctx.Fail("case %d: round trip failed", i);
    }
}

// Window edge: a block repeated at exactly the maximum distance (and one either side), in random
// data where nothing else matches. Whether the encoder can use it depends on the exact window bound.
DIFF_TEST(RefPack_CompressWindowEdge, 0x9cc611)
{
    using OrigCompress = int32_t(__cdecl *)(const uint8_t *, int32_t, uint8_t *, const int32_t *);
    auto orig = ctx.Original<OrigCompress>(0x9cc54d);
    std::mt19937 rng(64);
    for (int distance = 0x1FFFE; distance <= 0x20001; ++distance) {
        for (int32_t flags = 0; flags < 2; ++flags) {
            size_t length = distance + 200;
            std::vector<uint8_t> input(length + 4);
            for (auto &b : input) b = static_cast<uint8_t>(rng());
            std::copy(input.begin() + 50, input.begin() + 114, input.begin() + 50 + distance);
            size_t capacity = length * 15 / 10 + 0x400;
            std::vector<uint8_t> a(capacity, 0xCD), b(capacity, 0xCD);
            int32_t ra = orig(input.data(), static_cast<int32_t>(length), a.data(), &flags);
            int32_t rb = RefPack::Compress(input.data(), static_cast<int32_t>(length), b.data(), &flags);
            ++ctx.cases;
            if (ra != rb || a != b) ctx.Fail("distance 0x%x flags %d: sizes %d vs %d", distance, flags, ra, rb);
        }
    }
}
