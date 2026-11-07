// cRZString's last two slots (lane GY): slot 11, the reference form of IsEqual that forwards to slot 12 through the
// string's own vtable (0x1ad030, folded with five other classes' slot 11: not hooked), and slot 33, the vector
// deleting destructor (0x1cbea8).
//
// Original side: strings built by the original constructor 0x14116d on a heap block, slots called at their RVAs
// (the destructor body 0x141193 and the CRT's `eh vector destructor iterator` 0x824e6f are the image's).
// Our side: strings built by our constructor, our members called. Each block is filled with the same junk before
// construction, so bytes a constructor or destructor leaves alone compare equal. What a comparison leaves out:
//   * bytes 1-3 of a destroyed long string's union: the destructor writes only its first byte (the terminator;
//     the inlined _Tidy_deallocate in 0x1cbefc..0x1cbf05 calls 0x141455, which stores size 0, capacity 15 and
//     `mov byte ptr [esi], 0`), so the rest of the freed buffer's pointer stays, and each side has its own buffer.
#include "difftest.h"
#include "free_recorder.h"
#include "rz/cRZString.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kVTable = 0xd9b470;
constexpr uint32_t kSlots = 34;
constexpr uint32_t kCtorChar = 0x14116d, kIsEqualForward = 0x1ad030, kDeletingDestruct = 0x1cbea8;

std::string Text(std::mt19937 &rng)
{
    static const char alphabet[] = "abAB_z09";
    size_t len = rng() % 4 == 0 ? 14 + rng() % 4 : rng() % 40;
    std::string s;
    for (size_t i = 0; i < len; ++i) s += alphabet[rng() % 8];
    return s;
}

// ---- slot 11 ----

struct Slot12Call {
    const void *self;
    const void *other;
    uint8_t caseSensitive;
};
std::vector<Slot12Call> gSlot12Calls;
uint32_t gSlot12Answer;

uint32_t __fastcall RecordingSlot12(const void *self, void *, const void *other, uint32_t caseSensitive)
{
    gSlot12Calls.push_back({self, other, static_cast<uint8_t>(caseSensitive)});
    return gSlot12Answer;
}

} // namespace

// Slot 11 on strings whose vtable is a copy of the game's with slot 12 recording its call and answering a chosen
// dword (al compared: 0, 1, 0x80, junk above): the same object, argument and flag reach slot 12, once, and its al
// comes back. Then on real strings (the image's vtable): the same answer as the original's for random pairs.
DIFF_TEST(cRZString_IsEqualForward, 0x1ad030)
{
    using Fn = uint8_t(__thiscall *)(const void *, const void *, bool);
    auto original = ctx.Original<Fn>(kIsEqualForward);
    std::mt19937 rng(11);
    const void *const *gameTable = ctx.image.At<const void *const>(kVTable);
    const void *fake[kSlots];
    std::memcpy(fake, gameTable, sizeof(fake));
    fake[12] = reinterpret_cast<const void *>(&RecordingSlot12);
    for (int i = 0; i < 2000; ++i) {
        cRZString s{}, o{};
        s.Construct(Text(rng).c_str());
        o.Construct(Text(rng).c_str());
        s.mVTable = fake;
        const bool cs = rng() % 2 != 0;
        static const uint32_t answers[] = {0, 1, 0x80, 0xffffff00, 0x12345601};
        gSlot12Answer = answers[rng() % 5];
        ++ctx.cases;
        gSlot12Calls.clear();
        uint8_t want = original(&s, &o, cs);
        std::vector<Slot12Call> wantCalls = gSlot12Calls;
        gSlot12Calls.clear();
        bool got = s.IsEqualForward(*o.AsInterface(), cs);
        uint8_t gotByte;
        std::memcpy(&gotByte, &got, 1);
        if (want != gotByte) ctx.Fail("answer %08x: original al %02x, ours %02x", gSlot12Answer, want, gotByte);
        if (wantCalls.size() != 1 || gSlot12Calls.size() != 1) {
            ctx.Fail("slot 12 called %u times by the original, %u by ours", static_cast<unsigned>(wantCalls.size()),
                     static_cast<unsigned>(gSlot12Calls.size()));
        } else if (wantCalls[0].self != gSlot12Calls[0].self || wantCalls[0].other != gSlot12Calls[0].other ||
                   wantCalls[0].caseSensitive != gSlot12Calls[0].caseSensitive || wantCalls[0].self != &s ||
                   wantCalls[0].other != &o) {
            ctx.Fail("slot 12 arguments differ (cs %d)", cs);
        }
        s.mVTable = gameTable;
        s.Destruct();
        o.Destruct();
    }
    for (int i = 0; i < 20000; ++i) {
        std::string a = Text(rng), b = rng() % 2 ? a : Text(rng);
        if (rng() % 3 == 0)
            for (char &c : b) c = static_cast<char>(c >= 'a' && c <= 'z' && rng() % 2 ? c - 0x20 : c);
        cRZString s{}, o{};
        s.Construct(a.c_str());
        o.Construct(b.c_str());
        const bool cs = rng() % 2 != 0;
        ++ctx.cases;
        uint8_t want = original(&s, &o, cs);
        bool got = s.IsEqualForward(*o.AsInterface(), cs);
        if ((want != 0) != got) ctx.Fail("'%s' vs '%s' cs %d: original %d, ours %d", a.c_str(), b.c_str(), cs, want, got);
        s.Destruct();
        o.Destruct();
    }
}

// ---- slot 33 ----

namespace {

constexpr uint8_t kJunk = 0xcd;

// One side's run: the block's bytes after the call (empty when it was freed), the return value as an offset into
// the block, and the frees in order, each named by what it freed (-1 the block, else the element whose buffer).
struct DtorRun {
    std::vector<uint8_t> bytes;
    intptr_t returned = 0;
    std::vector<int> frees;
    std::vector<size_t> freedSizes;
};

// count < 0: a single string at the block's start; else a `new[]` block: the count, then the elements.
template <typename Build, typename Destroy>
DtorRun RunDtor(const std::vector<std::string> &texts, int count, uint32_t flags, Build build, Destroy destroy)
{
    const bool array = count >= 0;
    const size_t elements = array ? static_cast<size_t>(count) : 1;
    const size_t size = (array ? 4 : 0) + elements * sizeof(cRZString);
    uint8_t *block = static_cast<uint8_t *>(::operator new(size ? size : 1));
    std::memset(block, kJunk, size);
    if (array) std::memcpy(block, &count, 4);
    cRZString *first = reinterpret_cast<cRZString *>(block + (array ? 4 : 0));
    std::vector<const void *> buffers(elements, nullptr);
    for (size_t i = 0; i < elements; ++i) {
        build(&first[i], texts[i].c_str());
        first[i].mRefCount = 0x5000 + static_cast<uint32_t>(i);
        if (first[i].mString.capacity() > 15) buffers[i] = first[i].mString.data();
    }
    DtorRun r;
    difftest::FreeRecorder rec;
    void *ret = destroy(first, flags);
    rec.Stop();
    r.returned = reinterpret_cast<uint8_t *>(ret) - block;
    for (size_t k = 0; k < rec.Count(); ++k) {
        const void *p = rec.Pointer(k);
        int who = -2;
        if (p == block) who = -1;
        for (size_t i = 0; i < elements; ++i)
            if (buffers[i] == p) who = static_cast<int>(i);
        r.frees.push_back(who);
        r.freedSizes.push_back(rec.Size(k));
    }
    if (!(flags & 1)) {
        r.bytes.assign(block, block + size);
        for (size_t i = 0; i < elements; ++i)
            if (buffers[i]) std::memset(r.bytes.data() + (array ? 4 : 0) + i * sizeof(cRZString) + 5, 0, 3);
        ::operator delete(block);
    }
    return r;
}

} // namespace

// Single strings (short and long) and `new[]` blocks of 0-5 strings, with every combination of the two flag bits
// and junk in the others: the return value, the frees (which blocks, in which order, their sizes) and, when nothing
// is freed, every byte of the block left behind (vtable words included: both sides store the game's vtable).
DIFF_TEST(cRZString_DeletingDestruct, 0x1cbea8)
{
    using Ctor = void *(__thiscall *)(void *, const char *);
    using Dtor = void *(__thiscall *)(void *, uint32_t);
    auto originalCtor = ctx.Original<Ctor>(kCtorChar);
    auto original = ctx.Original<Dtor>(kDeletingDestruct);
    std::mt19937 rng(33);
    for (int i = 0; i < 4000; ++i) {
        const int count = i % 2 ? static_cast<int>(rng() % 6) : -1;
        uint32_t flags = rng() % 4;
        if (rng() % 4 == 0) flags |= rng() & ~3u;
        if (count >= 0) flags |= 2;
        else flags &= ~2u;
        std::vector<std::string> texts;
        for (int k = 0; k < (count < 0 ? 1 : count); ++k) texts.push_back(Text(rng));
        DtorRun a = RunDtor(texts, count, flags, [&](cRZString *s, const char *t) { originalCtor(s, t); },
                            [&](cRZString *s, uint32_t f) { return original(s, f); });
        DtorRun b = RunDtor(texts, count, flags, [](cRZString *s, const char *t) { s->Construct(t); },
                            [](cRZString *s, uint32_t f) { return s->DeletingDestruct(f); });
        ++ctx.cases;
        if (a.returned != b.returned)
            ctx.Fail("count %d flags %08x: returned block%+d, ours block%+d", count, flags, static_cast<int>(a.returned),
                     static_cast<int>(b.returned));
        if (a.frees != b.frees || a.freedSizes != b.freedSizes)
            ctx.Fail("count %d flags %08x: %u frees by the original, %u by ours, or different blocks/order", count, flags,
                     static_cast<unsigned>(a.frees.size()), static_cast<unsigned>(b.frees.size()));
        if (a.bytes != b.bytes) ctx.Fail("count %d flags %08x: the block left behind differs", count, flags);
    }
}
