// Differential tests for the reference-counting slots (QueryInterface, AddRef, Release) of the audio
// classes cHitChannel, cHitTrackPlayer and cGZSndEa (cGZSndEaRenderer's are in tests/gz_snd_ea_renderer.cpp, the
// class in the standalone shape). Objects are raw buffers
// carrying a copy of the class's primary vtable, in which AddRef and the deleting-destructor slot
// record their calls (and a deletion frees the buffer), so the comparison covers return values, the
// pointer QueryInterface writes (relative to its object), the count, and the calls made.
#include "difftest.h"
#include "gz/cGZSndEa.h"
#include "gz/cHitChannel.h"
#include "gz/cHitTrackPlayer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr size_t kBufferBytes = 0x200; // above every modelled count (the largest is at +0x10c)
constexpr uintptr_t kUnchanged = 0xFACE0000u; // initial *ppvObj; never dereferenced

using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using ReleaseFn = uint32_t(__thiscall *)(void *);
using AddRefFn = uint32_t(__thiscall *)(void *);

int gAddRefCalls = 0;
void *gAddRefSelf = nullptr;
uint32_t __fastcall RecordAddRef(void *self, void * /*edx*/)
{
    ++gAddRefCalls;
    gAddRefSelf = self;
    return 0;
}

int gDeleteCalls = 0;
uint32_t gDeleteFlags = 0;
void *__fastcall RecordDelete(void *self, void * /*edx*/, uint32_t flags)
{
    ++gDeleteCalls;
    gDeleteFlags = flags;
    if (flags & 1) ::operator delete(self);
    return self;
}

// Copy of an original primary vtable in which slot 1 (AddRef) and deleteSlot (-1: none) record.
struct RecordingVTable {
    std::vector<void *> slots;
    RecordingVTable(const difftest::Context &ctx, uint32_t vtableRva, size_t count, int deleteSlot) : slots(count)
    {
        std::memcpy(slots.data(), ctx.image.At(vtableRva), count * sizeof(void *));
        slots[1] = reinterpret_cast<void *>(&RecordAddRef);
        if (deleteSlot >= 0) slots[deleteSlot] = reinterpret_cast<void *>(&RecordDelete);
    }
};

uint8_t *NewObject(const RecordingVTable &vt, uint32_t refOffset, uint32_t refs)
{
    auto *p = static_cast<uint8_t *>(::operator new(kBufferBytes));
    std::memset(p, 0, kBufferBytes);
    const void *vtable = vt.slots.data();
    std::memcpy(p, &vtable, sizeof(vtable));
    std::memcpy(p + refOffset, &refs, sizeof(refs));
    return p;
}

int32_t ReadCount(const uint8_t *object, uint32_t refOffset)
{
    int32_t count;
    std::memcpy(&count, object + refOffset, sizeof(count));
    return count;
}

// A QueryInterface result as an offset from its object; "unchanged" and "null" are kept distinct.
int64_t Relative(const void *value, const uint8_t *object)
{
    if (reinterpret_cast<uintptr_t>(value) == kUnchanged) return INT64_MIN;
    if (!value) return INT64_MIN + 1;
    return static_cast<int64_t>(reinterpret_cast<uintptr_t>(value) - reinterpret_cast<uintptr_t>(object));
}

// Every interface ID the four classes compare against, plus a single-bit flip of each, IID 0 and 1,
// and random values.
std::vector<uint32_t> QueryIids(std::mt19937 &rng)
{
    static const uint32_t kKnown[] = {
        0x00000000, 0x00000001, 0x00000002, 0x8359908e,
        0x206077d2, 0x306077d2, 0x5b34f0af, 0xfd584467, 0xaa11a64f, 0x09d002d2, 0xec3a1141, 0x0c60e969,
        0xbb1be367, 0xdd583e2d, 0xe9d00a9a, 0x4b79c1a9, 0x406077d2, 0x5b30e7b3, 0xc9d25f29, 0x899b182d,
        0x287fb697, 0xab79e31c,
    };
    std::vector<uint32_t> out;
    for (uint32_t known : kKnown) {
        out.push_back(known);
        for (int bit = 0; bit < 32; ++bit) out.push_back(known ^ (1u << bit));
    }
    for (int i = 0; i < 2000; ++i) out.push_back(rng());
    return out;
}

template <typename Ours>
void QueryTest(difftest::Context &ctx, const char *name, uint32_t qiRva, uint32_t vtableRva, size_t slotCount,
               uint32_t refOffset, Ours ours, std::mt19937 &rng)
{
    auto orig = ctx.Original<QueryFn>(qiRva);
    RecordingVTable vt(ctx, vtableRva, slotCount, -1);
    for (uint32_t riid : QueryIids(rng)) {
        uint32_t refs = rng();
        uint8_t *a = NewObject(vt, refOffset, refs), *b = NewObject(vt, refOffset, refs);
        void *pa = reinterpret_cast<void *>(kUnchanged), *pb = reinterpret_cast<void *>(kUnchanged);

        gAddRefCalls = 0;
        gAddRefSelf = nullptr;
        bool ra = orig(a, riid, &pa);
        int callsA = gAddRefCalls;
        int64_t selfA = callsA ? Relative(gAddRefSelf, a) : 0;

        gAddRefCalls = 0;
        gAddRefSelf = nullptr;
        bool rb = ours(b, riid, &pb);
        int callsB = gAddRefCalls;
        int64_t selfB = callsB ? Relative(gAddRefSelf, b) : 0;

        ++ctx.cases;
        if (ra != rb || Relative(pa, a) != Relative(pb, b) || callsA != callsB || selfA != selfB ||
            ReadCount(a, refOffset) != ReadCount(b, refOffset))
            ctx.Fail("%s QueryInterface(0x%08x) refs %u: original %d -> %lld calls %d, ours %d -> %lld calls %d", name,
                     riid, refs, ra, static_cast<long long>(Relative(pa, a)), callsA, rb,
                     static_cast<long long>(Relative(pb, b)), callsB);
        ::operator delete(a);
        ::operator delete(b);
    }
}

template <typename Ours>
void AddRefTest(difftest::Context &ctx, const char *name, uint32_t addRefRva, uint32_t vtableRva, size_t slotCount,
                uint32_t refOffset, Ours ours, std::mt19937 &rng)
{
    auto orig = ctx.Original<AddRefFn>(addRefRva);
    RecordingVTable vt(ctx, vtableRva, slotCount, -1);
    std::vector<uint32_t> starts = {0, 1, 2, 0x7ffffffe, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff};
    for (int i = 0; i < 20000; ++i) starts.push_back(rng());
    for (uint32_t refs : starts) {
        uint8_t *a = NewObject(vt, refOffset, refs), *b = NewObject(vt, refOffset, refs);
        gAddRefCalls = 0;
        uint32_t ra = orig(a);
        int callsA = gAddRefCalls;
        gAddRefCalls = 0;
        uint32_t rb = ours(b);
        int callsB = gAddRefCalls;
        ++ctx.cases;
        if (ra != rb || callsA != callsB || ReadCount(a, refOffset) != ReadCount(b, refOffset) || callsA != 0)
            ctx.Fail("%s AddRef refs %d: original %u (calls %d, count %d), ours %u (calls %d, count %d)", name,
                     static_cast<int>(refs), ra, callsA, ReadCount(a, refOffset), rb, callsB,
                     ReadCount(b, refOffset));
        ::operator delete(a);
        ::operator delete(b);
    }
}

// Release over counts -2..5, with and without the deletion path. Deletions go through the recording
// deleting-destructor slot, which frees the buffer, so a survivor on one side is leaked, not freed.
template <typename Ours>
void ReleaseTest(difftest::Context &ctx, const char *name, uint32_t releaseRva, uint32_t vtableRva, size_t slotCount,
                 int deleteSlot, uint32_t refOffset, Ours ours, std::mt19937 &rng)
{
    auto orig = ctx.Original<ReleaseFn>(releaseRva);
    RecordingVTable vt(ctx, vtableRva, slotCount, deleteSlot);
    for (int i = 0; i < 20000; ++i) {
        uint32_t refs = static_cast<uint32_t>(static_cast<int>(rng() % 8) - 2);
        uint8_t *a = NewObject(vt, refOffset, refs), *b = NewObject(vt, refOffset, refs);

        gDeleteCalls = 0;
        gDeleteFlags = 0;
        uint32_t ra = orig(a);
        int deletedA = gDeleteCalls;
        uint32_t flagsA = gDeleteFlags;

        gDeleteCalls = 0;
        gDeleteFlags = 0;
        uint32_t rb = ours(b);
        int deletedB = gDeleteCalls;
        uint32_t flagsB = gDeleteFlags;

        ++ctx.cases;
        if (ra != rb || deletedA != deletedB || flagsA != flagsB || deletedA > 1)
            ctx.Fail("%s Release refs %d: original %u (deletes %d, flags %u), ours %u (deletes %d, flags %u)", name,
                     static_cast<int>(refs), ra, deletedA, flagsA, rb, deletedB, flagsB);
        if (deletedA != deletedB) continue;
        if (!deletedA) {
            if (ReadCount(a, refOffset) != ReadCount(b, refOffset))
                ctx.Fail("%s Release refs %d: count %d vs %d", name, static_cast<int>(refs), ReadCount(a, refOffset),
                         ReadCount(b, refOffset));
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

} // namespace

DIFF_TEST(cHitChannel_QueryInterface, 0xb32945)
{
    std::mt19937 rng(101);
    QueryTest(ctx, "cHitChannel::QueryInterface", 0xb32945, cHitChannel::kVTable, 54, offsetof(cHitChannel, mRefCount),
              [](uint8_t *o, uint32_t riid, void **pp) { return reinterpret_cast<cHitChannel *>(o)->QueryInterface(riid, pp); },
              rng);
}

DIFF_TEST(cHitChannel_Release, 0xb32a01)
{
    std::mt19937 rng(102);
    ReleaseTest(ctx, "cHitChannel::Release", 0xb32a01, cHitChannel::kVTable, 54, 37, offsetof(cHitChannel, mRefCount),
                [](uint8_t *o) { return reinterpret_cast<cHitChannel *>(o)->Release(); }, rng);
}

DIFF_TEST(cHitTrackPlayer_QueryInterface, 0xb366e2)
{
    std::mt19937 rng(201);
    QueryTest(ctx, "cHitTrackPlayer::QueryInterface", 0xb366e2, cHitTrackPlayer::kVTable, 34,
              offsetof(cHitTrackPlayer, mRefCount),
              [](uint8_t *o, uint32_t riid, void **pp) {
                  return reinterpret_cast<cHitTrackPlayer *>(o)->QueryInterface(riid, pp);
              },
              rng);
}

DIFF_TEST(cHitTrackPlayer_AddRef, 0xb337e7)
{
    std::mt19937 rng(202);
    AddRefTest(ctx, "cHitTrackPlayer::AddRef", 0xb337e7, cHitTrackPlayer::kVTable, 34, offsetof(cHitTrackPlayer, mRefCount),
               [](uint8_t *o) { return reinterpret_cast<cHitTrackPlayer *>(o)->AddRef(); }, rng);
}

DIFF_TEST(cHitTrackPlayer_Release, 0xb367c0)
{
    std::mt19937 rng(203);
    ReleaseTest(ctx, "cHitTrackPlayer::Release", 0xb367c0, cHitTrackPlayer::kVTable, 34, 20,
                offsetof(cHitTrackPlayer, mRefCount), [](uint8_t *o) { return reinterpret_cast<cHitTrackPlayer *>(o)->Release(); },
                rng);
}

DIFF_TEST(cGZSndEa_QueryInterface, 0xb146c1)
{
    std::mt19937 rng(301);
    QueryTest(ctx, "cGZSndEa::QueryInterface", 0xb146c1, cGZSndEa::kVTable, 48, offsetof(cGZSndEa, mRefCount),
              [](uint8_t *o, uint32_t riid, void **pp) { return reinterpret_cast<cGZSndEa *>(o)->QueryInterface(riid, pp); },
              rng);
}

DIFF_TEST(cGZSndEa_AddRef, 0xb14128)
{
    std::mt19937 rng(302);
    AddRefTest(ctx, "cGZSndEa::AddRef", 0xb14128, cGZSndEa::kVTable, 48, offsetof(cGZSndEa, mRefCount),
               [](uint8_t *o) { return reinterpret_cast<cGZSndEa *>(o)->AddRef(); }, rng);
}

DIFF_TEST(cGZSndEa_Release, 0xb14787)
{
    std::mt19937 rng(303);
    ReleaseTest(ctx, "cGZSndEa::Release", 0xb14787, cGZSndEa::kVTable, 48, 37, offsetof(cGZSndEa, mRefCount),
                [](uint8_t *o) { return reinterpret_cast<cGZSndEa *>(o)->Release(); }, rng);
}
