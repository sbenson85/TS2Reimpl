// Differential tests for cRZFastCompression3 (decompression side) and the RefPack header helpers.
#include "difftest.h"
#include "free_recorder.h"
#include "rz/RefPack.h"
#include "rz/cRZFastCompression3.h"
#include "rz/cRZString.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

cRZFastCompression3 *NewCompressor(const difftest::Context &ctx, uint32_t refs)
{
    auto *c = new cRZFastCompression3{};
    c->mVTable = ctx.image.At(cRZFastCompression3::kVTable);
    c->mRefCount = refs;
    return c;
}

// A small valid RefPack stream with a 4-byte length prefix: literal run + reference + end.
std::vector<uint8_t> Packed(std::mt19937 &rng, uint32_t &size, bool corruptHeader)
{
    std::vector<uint8_t> s = {0, 0, 0, 0, 0x10, 0xFB, 0, 0, 0};
    uint32_t runs = rng() % 4;
    size = 0;
    for (uint32_t r = 0; r < runs; ++r) {
        uint32_t n = rng() % 28;
        s.push_back(static_cast<uint8_t>(0xE0 | n));
        for (uint32_t i = 0; i < (n + 1) * 4; ++i) s.push_back(static_cast<uint8_t>(rng()));
        size += (n + 1) * 4;
    }
    if (size >= 8) { s.push_back(0x08); s.push_back(0x03); size += 5; } // 2-byte op: length 5, offset 4
    s.push_back(0xFC);
    uint32_t declared = corruptHeader ? (rng() % 2 ? 0 : rng() & 0xFFFFFF) : size;
    s[6] = static_cast<uint8_t>(declared >> 16);
    s[7] = static_cast<uint8_t>(declared >> 8);
    s[8] = static_cast<uint8_t>(declared);
    if (rng() % 4 == 0) {
        // Odd signatures: Decompress picks the header form from bit 0 of the first byte, while
        // GetDecompressedSize compares the whole magic, so these make the two disagree.
        s[4] = static_cast<uint8_t>(rng() & 0xFE);
        s[5] = static_cast<uint8_t>(rng());
    }
    uint32_t total = static_cast<uint32_t>(s.size());
    memcpy(s.data(), &total, 4);
    s.resize(s.size() + 8, 0);
    return s;
}

} // namespace

// Slot 1 (AddRef, 0x148fb0, folded with about 600 unrelated classes): an unlocked increment that returns
// the new 32-bit count. The whole object is compared (12 bytes).
DIFF_TEST(cRZFastCompression3_AddRef, 0x148fb0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x148fb0);
    const uint32_t edges[] = {0, 1, 0x7fffffff, 0xfffffffe, 0xffffffff};
    std::mt19937 rng(0x148f);
    for (int i = 0; i < 4000; ++i) {
        uint32_t count = i < 5 ? edges[i] : static_cast<uint32_t>(rng());
        uint8_t a[12], b[12];
        for (auto &x : a) x = static_cast<uint8_t>(rng());
        std::memcpy(a + 4, &count, sizeof(count));
        std::memcpy(b, a, sizeof(a));
        uint32_t ra = orig(a);
        uint32_t rb = reinterpret_cast<cRZFastCompression3 *>(b)->AddRef();
        ++ctx.cases;
        if (ra != rb || std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("AddRef from %u: %u vs %u", count, ra, rb);
    }
}

DIFF_TEST(RefPack_Helpers, 0x9cca38)
{
    auto size = ctx.Original<uint32_t(__cdecl *)(const uint8_t *)>(0x9cca38);
    auto recognize = ctx.Original<uint32_t(__cdecl *)(const uint8_t *)>(0x9cc5c6);
    auto writeBe = ctx.Original<void(__cdecl *)(uint8_t *, uint32_t, uint32_t)>(0x9cc597);
    std::mt19937 rng(5);
    for (int i = 0; i < 50000; ++i) {
        uint8_t h[8];
        for (auto &b : h) b = static_cast<uint8_t>(rng());
        if (rng() % 3 == 0) { h[0] = rng() % 2 ? 0x10 : 0x11; h[1] = 0xFB; }
        ctx.cases += 2;
        if (size(h) != RefPack::GetDecompressedSize(h)) ctx.Fail("GetDecompressedSize %02x%02x", h[0], h[1]);
        if (recognize(h) != RefPack::RecognizeHeader(h)) ctx.Fail("RecognizeHeader %02x%02x", h[0], h[1]);
        uint8_t a[8] = {}, b[8] = {};
        uint32_t v = rng(), n = rng() % 5;
        writeBe(a, v, n);
        RefPack::WriteBigEndian(b, v, n);
        ++ctx.cases;
        if (memcmp(a, b, 8) != 0) ctx.Fail("WriteBigEndian(0x%08x, %u)", v, n);
    }
}

DIFF_TEST(cRZFastCompression3_Decompression, 0x9cc174)
{
    using Bool5 = bool(__thiscall *)(void *, const void *, uint32_t, void *, uint32_t, uint32_t *);
    auto decompress = ctx.Original<Bool5>(0x9cc174);
    auto maxLen = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x9cc2bd);
    auto lenCompressed = ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x9cc229);
    auto lenDecompressed = ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x9cc26f);
    auto query = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x9cc2d1);
    auto release = ctx.Original<uint32_t(__thiscall *)(void *)>(0x9cc309);
    auto version = ctx.Original<void(__thiscall *)(void *, void *)>(0x9cc1ed);

    std::mt19937 rng(11);
    cRZFastCompression3 *a = NewCompressor(ctx, 1), *b = NewCompressor(ctx, 1);
    for (int i = 0; i < 20000; ++i) {
        uint32_t size = 0;
        auto packed = Packed(rng, size, rng() % 6 == 0);
        uint32_t capacity = rng() % 5 ? size + rng() % 8 : rng() % (size + 1);
        std::vector<uint8_t> da(size + 16, 0xEE), db(size + 16, 0xEE);
        uint32_t la = 0x1234, lb = 0x1234;
        bool ra = decompress(a, packed.data(), static_cast<uint32_t>(packed.size()), da.data(), capacity, &la);
        bool rb = b->DecompressData(packed.data(), static_cast<uint32_t>(packed.size()), db.data(), capacity, &lb);
        ++ctx.cases;
        if (ra != rb || la != lb || da != db) ctx.Fail("DecompressData case %d: %d/%u vs %d/%u", i, ra, la, rb, lb);
        ctx.cases += 3;
        if (lenCompressed(a, packed.data()) != b->GetLengthOfCompressedData(packed.data())) ctx.Fail("GetLengthOfCompressedData");
        if (lenDecompressed(a, packed.data()) != b->GetLengthOfDecompressedData(packed.data())) ctx.Fail("GetLengthOfDecompressedData");
        uint32_t n = rng();
        if (maxLen(a, n) != b->GetMaxLengthRequiredForCompressedData(n)) ctx.Fail("GetMaxLength(%u)", n);
    }
    for (int i = 0; i < 2000; ++i) {
        uint32_t riid = i % 3 == 0 ? 1 : i % 3 == 1 ? cRZFastCompression3::kGZIID_cIGZCompressor : rng();
        void *pa = nullptr, *pb = nullptr;
        bool ra = query(a, riid, &pa), rb = b->QueryInterface(riid, &pb);
        ++ctx.cases;
        if (ra != rb || (pa == a) != (pb == b) || a->mRefCount != b->mRefCount) ctx.Fail("QueryInterface(0x%08x)", riid);
    }
    {
        cRZString va{}, vb{};
        va.mVTable = vb.mVTable = ctx.image.At(0xd9b470);
        va.mString = vb.mString = "previous";
        version(a, &va);
        b->GetCompressionVersionString(*vb.AsInterface());
        ++ctx.cases;
        if (va.mString != vb.mString) ctx.Fail("GetCompressionVersionString: '%s' vs '%s'", va.mString.c_str(), vb.mString.c_str());
    }
    // Release down to deletion (through slot 9).
    while (a->mRefCount > 1) {
        uint32_t ra = release(a), rb = b->Release();
        ++ctx.cases;
        if (ra != rb) ctx.Fail("Release %u vs %u", ra, rb);
    }
    ++ctx.cases;
    if (release(a) != b->Release()) ctx.Fail("final Release differs");
}

namespace {

// A vtable whose slots 6 and 7 (the two lengths) record that they were called, in which order, and answer from globals.
int gCallLog[4];
int gCallCount;
uint32_t gAnswer6, gAnswer7;
uint32_t __fastcall FakeLength6(void *, void *, const void *)
{
    if (gCallCount < 4) gCallLog[gCallCount++] = 6;
    return gAnswer6;
}
uint32_t __fastcall FakeLength7(void *, void *, const void *)
{
    if (gCallCount < 4) gCallLog[gCallCount++] = 7;
    return gAnswer7;
}
const void *const kFakeVTable[10] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                     reinterpret_cast<const void *>(&FakeLength6), reinterpret_cast<const void *>(&FakeLength7),
                                     nullptr, nullptr};

} // namespace

// 0x9cc320: the buffer plausibility check. The two length slots are the test's own (so the order and number of calls are
// observable, and the 16 MB limits reachable); sizes around 7, headers that are and are not RefPack, answers around
// 0x1000000.
DIFF_TEST(cRZFastCompression3_HasValidHeader, 0x9cc320)
{
    using Check = uint8_t(__thiscall *)(void *, const void *, uint32_t);
    auto orig = ctx.Original<Check>(0x9cc320);
    const uint32_t answers[] = {0, 1, 0xffffff, 0x1000000, 0x1000001, 0x80000000, 0xffffffff};
    std::mt19937 rng(0x9cc3);
    for (int i = 0; i < 20000; ++i) {
        uint8_t buffer[24];
        for (auto &b : buffer) b = static_cast<uint8_t>(rng());
        if (rng() % 3 != 0) {
            buffer[4] = rng() % 2 ? 0x10 : 0x11; // a RefPack signature
            buffer[5] = 0xFB;
        } else if (rng() % 4 == 0) {
            buffer[5] = 0xFB; // half of one
        }
        uint32_t size = rng() % 4 == 0 ? static_cast<uint32_t>(rng() % 12) : 7 + rng() % 4;
        if (rng() % 9 == 0) size = answers[rng() % 7];
        const uint32_t a6 = rng() % 3 ? answers[rng() % 7] : static_cast<uint32_t>(rng());
        const uint32_t a7 = rng() % 3 ? answers[rng() % 7] : static_cast<uint32_t>(rng());

        cRZFastCompression3 a{}, b{};
        a.mVTable = b.mVTable = kFakeVTable;
        gAnswer6 = a6;
        gAnswer7 = a7;
        gCallCount = 0;
        uint8_t ra = orig(&a, buffer, size);
        const int countA = gCallCount;
        int logA[4];
        std::memcpy(logA, gCallLog, sizeof(logA));
        gCallCount = 0;
        bool rb = b.HasValidHeader(buffer, size);
        ++ctx.cases;
        if ((ra != 0) != rb || countA != gCallCount || std::memcmp(logA, gCallLog, countA * sizeof(int)) != 0 ||
            std::memcmp(&a, &b, sizeof(a)) != 0)
            ctx.Fail("size %u, answers %u/%u: original %d (%d calls), ours %d (%d calls)", size, a6, a7, ra, countA, rb, gCallCount);
    }
    // With the game's own length slots, on real streams.
    std::mt19937 rng2(0x9cc4);
    cRZFastCompression3 *a = NewCompressor(ctx, 1), *b = NewCompressor(ctx, 1);
    for (int i = 0; i < 4000; ++i) {
        uint32_t size = 0;
        auto packed = Packed(rng2, size, rng2() % 4 == 0);
        const uint32_t n = rng2() % 5 == 0 ? static_cast<uint32_t>(rng2() % 9) : static_cast<uint32_t>(packed.size());
        uint8_t ra = orig(a, packed.data(), n);
        bool rb = b->HasValidHeader(packed.data(), n);
        ++ctx.cases;
        if ((ra != 0) != rb) ctx.Fail("real stream %d, size %u: %d vs %d", i, n, ra, rb);
    }
}

// Our own vtable (lane QD; the image-free program's): rzface::cRZFastCompression3's table, every slot called by number
// on an object carrying it against the original's object (the game's vtable, original bodies; slot 1 is the folded 0x148fb0).
DIFF_TEST(cRZFastCompression3_OurVTable, 0x9cc174)
{
    using Deleting = void *(__thiscall *)(void *, uint32_t);
    using Decompress = bool(__thiscall *)(void *, const void *, uint32_t, void *, uint32_t, uint32_t *);
    using Compress = bool(__thiscall *)(void *, const void *, uint32_t, void *, uint32_t *);
    using Length = uint32_t(__thiscall *)(void *, const void *);
    const void *const *ours = static_cast<const void *const *>(cRZFastCompression3::OwnVTable());
    for (int i = 0; i < 10; ++i) {
        ++ctx.cases;
        if (reinterpret_cast<uintptr_t>(ours[i]) - ctx.image.Base() < ctx.image.Size()) ctx.Fail("our vtable slot %d points into the image", i);
    }
    auto slot = [](void *object, int index) { return (*static_cast<void *const *const *>(object))[index]; };
    std::mt19937 rng(0x600d);
    cRZFastCompression3 *a = NewCompressor(ctx, 1000), *b = NewCompressor(ctx, 1000);
    b->mVTable = ours;
    for (int i = 0; i < 6000; ++i) {
        uint32_t size = 0;
        auto packed = Packed(rng, size, rng() % 6 == 0);
        const uint32_t psize = static_cast<uint32_t>(packed.size());
        uint32_t capacity = rng() % 5 ? size + rng() % 8 : rng() % (size + 1);
        std::vector<uint8_t> da(size + 16, 0xEE), db(size + 16, 0xEE);
        uint32_t la = 0x1234, lb = 0x1234;
        bool ra = reinterpret_cast<Decompress>(slot(a, 4))(a, packed.data(), psize, da.data(), capacity, &la);
        bool rb = reinterpret_cast<Decompress>(slot(b, 4))(b, packed.data(), psize, db.data(), capacity, &lb);
        ctx.cases += 4;
        if (ra != rb || la != lb || da != db) ctx.Fail("slot 4 case %d: %d/%u vs %d/%u", i, ra, la, rb, lb);
        if (reinterpret_cast<Length>(slot(a, 6))(a, packed.data()) != reinterpret_cast<Length>(slot(b, 6))(b, packed.data())) ctx.Fail("slot 6");
        if (reinterpret_cast<Length>(slot(a, 7))(a, packed.data()) != reinterpret_cast<Length>(slot(b, 7))(b, packed.data())) ctx.Fail("slot 7");
        uint32_t n = rng();
        if (reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(slot(a, 5))(a, n) !=
            reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(slot(b, 5))(b, n))
            ctx.Fail("slot 5 (%u)", n);
        // Compression, both modes, into a buffer of the maximum length.
        std::vector<uint8_t> input(rng() % 3000);
        for (auto &c : input) c = static_cast<uint8_t>(rng() % 6 == 0 ? rng() : 7);
        a->mFlag = b->mFlag = rng() % 2;
        std::vector<uint8_t> ca(input.size() * 2 + 2048, 0xCD), cb(ca);
        uint32_t sa = 0x55, sb = 0x55;
        bool qa = reinterpret_cast<Compress>(slot(a, 3))(a, input.data(), static_cast<uint32_t>(input.size()), ca.data(), &sa);
        bool qb = reinterpret_cast<Compress>(slot(b, 3))(b, input.data(), static_cast<uint32_t>(input.size()), cb.data(), &sb);
        ++ctx.cases;
        if (qa != qb || sa != sb || ca != cb) ctx.Fail("slot 3 case %d: %d/%u vs %d/%u", i, qa, sa, qb, sb);
    }
    for (int i = 0; i < 1000; ++i) {
        uint32_t riid = i % 3 == 0 ? 1 : i % 3 == 1 ? cRZFastCompression3::kGZIID_cIGZCompressor : rng();
        void *pa = nullptr, *pb = nullptr;
        bool ra = reinterpret_cast<bool(__thiscall *)(void *, uint32_t, void **)>(slot(a, 0))(a, riid, &pa);
        bool rb = reinterpret_cast<bool(__thiscall *)(void *, uint32_t, void **)>(slot(b, 0))(b, riid, &pb);
        ++ctx.cases;
        if (ra != rb || (pa == a) != (pb == b) || a->mRefCount != b->mRefCount) ctx.Fail("slot 0 (0x%08x)", riid);
    }
    {
        cRZString va{}, vb{};
        va.mVTable = vb.mVTable = ctx.image.At(0xd9b470);
        va.mString = vb.mString = "previous";
        reinterpret_cast<void(__thiscall *)(void *, void *)>(slot(a, 8))(a, &va);
        reinterpret_cast<void(__thiscall *)(void *, void *)>(slot(b, 8))(b, &vb);
        ++ctx.cases;
        if (va.mString != vb.mString) ctx.Fail("slot 8: '%s' vs '%s'", va.mString.c_str(), vb.mString.c_str());
    }
    // Release (slot 2) while the count is above one: both decrement.
    for (int i = 0; i < 100; ++i) {
        uint32_t ra = reinterpret_cast<uint32_t(__thiscall *)(void *)>(slot(a, 2))(a);
        uint32_t rb = reinterpret_cast<uint32_t(__thiscall *)(void *)>(slot(b, 2))(b);
        ++ctx.cases;
        if (ra != rb || a->mRefCount != b->mRefCount) ctx.Fail("slot 2: %u vs %u", ra, rb);
    }
    // The deleting destructor (slot 9): flags 0 destroys without freeing (each stores its class's vtable), flags 1 frees.
    for (uint32_t flags = 0; flags < 2; ++flags) {
        b->mVTable = ours;
        a->mVTable = ctx.image.At(cRZFastCompression3::kVTable);
        void *ra, *rb;
        bool freedA, freedB;
        {
            difftest::FreeRecorder rec;
            ra = reinterpret_cast<Deleting>(slot(a, 9))(a, flags);
            freedA = rec.Freed(a);
        }
        {
            difftest::FreeRecorder rec;
            rb = reinterpret_cast<Deleting>(slot(b, 9))(b, flags);
            freedB = rec.Freed(b);
        }
        ++ctx.cases;
        if (ra != a || rb != b || freedA != freedB || freedA != (flags == 1)) ctx.Fail("slot 9 (%u): returned %p/%p, freed %d/%d", flags, ra, rb, freedA, freedB);
        if (flags == 0) {
            ++ctx.cases;
            if (a->mVTable != ctx.image.At(cRZFastCompression3::kVTable) || b->mVTable != cRZFastCompression3::VTable())
                ctx.Fail("vtable after destruction");
        }
    }
}
