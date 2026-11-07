// Differential tests for the small slots of cHitTrackPlayer: primary slots 5, 12, 16 and 25, and the
// cIGZSnd subobject (+12) slots 6, 16, 19, 20, 23, 24, 26, 28, 31 and 33. Most of them forward to the
// vtable of another subobject or to an embedded member's vtable. Those vtables are replaced by copies
// in which the called slot records its receiver and arguments, so each comparison covers the return
// value, every call made (receiver, arguments, order) and the object's bytes (pointers normalised to
// offsets, so the two objects can differ in address).
#include "difftest.h"
#include "gz/cHitTrackPlayer.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kSize = 0x200;

// One recorded call: which slot ran, the receiver relative to the object, and the arguments.
struct Rec {
    int kind;
    int64_t self;
    uint32_t args[4];
    bool operator==(const Rec &o) const
    {
        return kind == o.kind && self == o.self && std::memcmp(args, o.args, sizeof(args)) == 0;
    }
};

std::vector<Rec> gRecs;
uintptr_t gBase = 0;   // the object under test; recorded receivers are relative to it
uint32_t gRet = 0;     // what the recording slots return

int64_t Rel(const void *p)
{
    return static_cast<int64_t>(reinterpret_cast<uintptr_t>(p) - gBase);
}

enum Kind { kRegisterSetSlot4 = 1, kSndSlot5, kSndSlot7, kTargetSlot5, kEmbeddedSlot17 };

// Recording slots, as member functions of a stand-in class so that they get the __thiscall convention:
// the receiver arrives in ecx and the callee pops its stack arguments, as the original's callees do.
struct Recorder {
    uint32_t RegisterSetSlot4(uint32_t id)
    {
        gRecs.push_back({kRegisterSetSlot4, Rel(this), {id, 0, 0, 0}});
        return gRet;
    }
    uint32_t SndSlot5(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
    {
        gRecs.push_back({kSndSlot5, Rel(this), {a, b, c, d}});
        return gRet;
    }
    void SndSlot7()
    {
        gRecs.push_back({kSndSlot7, Rel(this), {0, 0, 0, 0}});
    }
    void TargetSlot5()
    {
        gRecs.push_back({kTargetSlot5, Rel(this), {0, 0, 0, 0}});
    }
    uint32_t EmbeddedSlot17(uint32_t arg)
    {
        gRecs.push_back({kEmbeddedSlot17, Rel(this), {arg, 0, 0, 0}});
        return gRet;
    }
};

// Code address of a member function, as the vtable entry needs it.
template <typename T> void *Address(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// Vtables the slots under test reach. Each is zero except for the recording entries.
struct Tables {
    void *registerSet[9] = {}; // cIGZSndRegisterSet (object+4): slot 4
    void *snd[8] = {};         // cIGZSnd (object+12): slots 5 and 7
    void *embedded[18] = {};   // member object at object+0x20: slot 17
    void *target[6] = {};      // object reached through the pointer at object+0xb4: slot 5

    Tables()
    {
        registerSet[4] = Address(&Recorder::RegisterSetSlot4);
        snd[5] = Address(&Recorder::SndSlot5);
        snd[7] = Address(&Recorder::SndSlot7);
        embedded[17] = Address(&Recorder::EmbeddedSlot17);
        target[5] = Address(&Recorder::TargetSlot5);
    }
};

// A cHitTrackPlayer-shaped buffer: vtable pointers for the subobjects that are called, the chain
// object+0xb4 -> cell at +0x108 -> target object at +0x100, and room for the fields the slots read.
struct Object {
    alignas(8) uint8_t bytes[kSize];

    explicit Object(const Tables &t)
    {
        std::memset(bytes, 0, sizeof(bytes));
        Put(4, t.registerSet);
        Put(12, t.snd);
        Put(0x20, t.embedded);
        Put(0xb4, bytes + 0x108);
        Put(0x108, bytes + 0x100);
        Put(0x100, t.target);
    }

    void Put(uint32_t offset, const void *value)
    {
        std::memcpy(bytes + offset, &value, sizeof(value));
    }
};

void Store32(uint8_t *object, uint32_t offset, uint32_t value)
{
    std::memcpy(object + offset, &value, sizeof(value));
}

// Object contents with pointers into the object replaced by offsets from it, so two objects at
// different addresses compare equal when they hold the same data.
std::vector<uint32_t> Normalized(const Object &o)
{
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(o.bytes));
    std::vector<uint32_t> out(kSize / 4);
    for (size_t i = 0; i < out.size(); ++i) {
        uint32_t v;
        std::memcpy(&v, o.bytes + 4 * i, sizeof(v));
        uint32_t rel = v - base;
        out[i] = rel < kSize ? 0x80000000u | rel : v;
    }
    return out;
}

using Invoke = std::function<uint64_t(uint8_t *object)>;

// Pointer results as offsets from the object under test; null is kept distinct.
uint64_t PointerResult(const void *p)
{
    return p ? static_cast<uint64_t>(Rel(p)) : ~0ull;
}

// Runs the original and the reimplementation on identical objects and compares everything observable.
void Compare(difftest::Context &ctx, const char *name, const Tables &t, uint32_t ret,
             const std::function<void(uint8_t *)> &setup, const Invoke &orig, const Invoke &ours)
{
    Object a(t), b(t);
    setup(a.bytes);
    setup(b.bytes);

    gRet = ret;
    gRecs.clear();
    gBase = reinterpret_cast<uintptr_t>(a.bytes);
    uint64_t ra = orig(a.bytes);
    std::vector<Rec> callsA = std::move(gRecs);
    gRecs.clear();

    gBase = reinterpret_cast<uintptr_t>(b.bytes);
    uint64_t rb = ours(b.bytes);
    std::vector<Rec> callsB = std::move(gRecs);
    gRecs.clear();

    ++ctx.cases;
    if (ra != rb || callsA != callsB || Normalized(a) != Normalized(b))
        ctx.Fail("%s (ret %u): original %llx with %zu calls, ours %llx with %zu calls", name, ret,
                 static_cast<unsigned long long>(ra), callsA.size(), static_cast<unsigned long long>(rb),
                 callsB.size());
}

// Field values worth covering: zero, one, all-ones, a high bit, and random values.
uint32_t Edge(std::mt19937 &rng, int i)
{
    switch (i % 5) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 0xffffffffu;
    case 3: return 0x80000000u;
    default: return rng();
    }
}

} // namespace

// Primary vtable slots (this = object).

DIFF_TEST(cHitTrackPlayer_GetMultipleSourcesInterface, 0xb2f12b)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xb2f12b);
    Tables t;
    // Non-null object: the interface is at +0x18. Null: the result is null.
    Object o(t);
    gBase = reinterpret_cast<uintptr_t>(o.bytes);
    void *expect = orig(o.bytes);
    void *got = reinterpret_cast<cHitTrackPlayer *>(o.bytes)->GetMultipleSourcesInterface();
    ++ctx.cases;
    if (expect != got) ctx.Fail("GetMultipleSourcesInterface: original %p, ours %p", expect, got);
    ++ctx.cases;
    if (orig(nullptr) != nullptr || reinterpret_cast<cHitTrackPlayer *>(nullptr)->GetMultipleSourcesInterface() != nullptr)
        ctx.Fail("GetMultipleSourcesInterface(null) is not null");
    Compare(ctx, "GetMultipleSourcesInterface", t, 0, [](uint8_t *) {},
            [&](uint8_t *p) { return PointerResult(orig(p)); },
            [](uint8_t *p) {
                return PointerResult(reinterpret_cast<cHitTrackPlayer *>(p)->GetMultipleSourcesInterface());
            });
}

DIFF_TEST(cHitTrackPlayer_InvokeChainedSlot5, 0xb36857)
{
    auto orig = ctx.Original<bool (__thiscall *)(void *)>(0xb36857);
    Tables t;
    for (int i = 0; i < 50; ++i)
        Compare(ctx, "InvokeChainedSlot5", t, static_cast<uint32_t>(i), [](uint8_t *) {},
                [&](uint8_t *p) { return uint64_t(orig(p)); },
                [](uint8_t *p) { return uint64_t(reinterpret_cast<cHitTrackPlayer *>(p)->InvokeChainedSlot5()); });
}

DIFF_TEST(cHitTrackPlayer_SetFlagAtD4, 0xb368f4)
{
    auto orig = ctx.Original<void (__thiscall *)(void *)>(0xb368f4);
    Tables t;
    std::mt19937 rng(502);
    for (int i = 0; i < 200; ++i) {
        uint32_t before = Edge(rng, i);
        Compare(ctx, "SetFlagAtD4", t, 0,
                [&](uint8_t *o) { Store32(o, 0xd0, before); },
                [&](uint8_t *p) {
                    orig(p);
                    return uint64_t(0);
                },
                [](uint8_t *p) {
                    reinterpret_cast<cHitTrackPlayer *>(p)->SetFlagAtD4();
                    return uint64_t(0);
                });
    }
}

DIFF_TEST(cHitTrackPlayer_CallSndSlot7, 0xb35aec)
{
    auto orig = ctx.Original<bool (__thiscall *)(void *, uint32_t, uint32_t)>(0xb35aec);
    Tables t;
    std::mt19937 rng(503);
    for (int i = 0; i < 100; ++i) {
        uint32_t a = rng(), b = rng();
        Compare(ctx, "CallSndSlot7", t, 0, [](uint8_t *) {},
                [&](uint8_t *p) { return uint64_t(orig(p, a, b)); },
                [&](uint8_t *p) {
                    return uint64_t(reinterpret_cast<cHitTrackPlayer *>(p)->CallSndSlot7(a, b));
                });
    }
}

// cIGZSnd subobject slots (this = object+12).

DIFF_TEST(cHitTrackPlayer_SndFlagAt40, 0xb2f914)
{
    auto orig = ctx.Original<bool (__thiscall *)(void *)>(0xb2f914);
    Tables t;
    std::mt19937 rng(601);
    for (int i = 0; i < 200; ++i) {
        uint32_t v = Edge(rng, i);
        Compare(ctx, "SndFlagAt40", t, 0, [&](uint8_t *o) { Store32(o, 0x4c, v); },
                [&](uint8_t *p) { return uint64_t(orig(p + 12)); },
                [](uint8_t *p) {
                    return uint64_t(reinterpret_cast<cHitTrackPlayer *>(p + 12)->SndFlagAt40());
                });
    }
}

DIFF_TEST(cHitTrackPlayer_SndFlagAt5C, 0xb2f91c)
{
    auto orig = ctx.Original<bool (__thiscall *)(void *)>(0xb2f91c);
    Tables t;
    std::mt19937 rng(602);
    for (int i = 0; i < 200; ++i) {
        uint32_t v = Edge(rng, i);
        Compare(ctx, "SndFlagAt5C", t, 0, [&](uint8_t *o) { Store32(o, 0x68, v); },
                [&](uint8_t *p) { return uint64_t(orig(p + 12)); },
                [](uint8_t *p) {
                    return uint64_t(reinterpret_cast<cHitTrackPlayer *>(p + 12)->SndFlagAt5C());
                });
    }
}

// The five id forwarders: each passes its id to the register set's slot 4 and returns the callee's eax.
#define SND_QUERY_TEST(TestName, Rva, Id, Method)                                                             \
    DIFF_TEST(cHitTrackPlayer_##TestName, Rva)                                                                 \
    {                                                                                                          \
        auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(Rva);                                         \
        Tables t;                                                                                              \
        std::mt19937 rng(700 + Id);                                                                            \
        for (int i = 0; i < 100; ++i) {                                                                        \
            uint32_t r = Edge(rng, i);                                                                         \
            Compare(ctx, #TestName, t, r, [](uint8_t *) {}, [&](uint8_t *p) { return uint64_t(orig(p + 12)); },\
                    [](uint8_t *p) {                                                                           \
                        return uint64_t(reinterpret_cast<cHitTrackPlayer *>(p + 12)->Method());                \
                    });                                                                                        \
        }                                                                                                      \
    }

SND_QUERY_TEST(SndQuery15, 0xb2f458, 0x15, SndQuery15)
SND_QUERY_TEST(SndQuery16, 0xb2f42f, 0x16, SndQuery16)
SND_QUERY_TEST(SndQuery17, 0xb2f419, 0x17, SndQuery17)
SND_QUERY_TEST(SndQuery49, 0xb2f424, 0x49, SndQuery49)
SND_QUERY_TEST(SndQuery13, 0xb2f44d, 0x13, SndQuery13)

DIFF_TEST(cHitTrackPlayer_SndTestId2A, 0xb2f904)
{
    auto orig = ctx.Original<bool (__thiscall *)(void *)>(0xb2f904);
    Tables t;
    std::mt19937 rng(801);
    for (int i = 0; i < 200; ++i) {
        uint32_t r = Edge(rng, i);
        Compare(ctx, "SndTestId2A", t, r, [](uint8_t *) {}, [&](uint8_t *p) { return uint64_t(orig(p + 12)); },
                [](uint8_t *p) { return uint64_t(reinterpret_cast<cHitTrackPlayer *>(p + 12)->SndTestId2A()); });
    }
}

DIFF_TEST(cHitTrackPlayer_SndForwardSlot5, 0xb3648b)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0xb3648b);
    Tables t;
    std::mt19937 rng(802);
    for (int i = 0; i < 200; ++i) {
        uint32_t a = Edge(rng, i), b = rng(), c = rng(), r = rng();
        Compare(ctx, "SndForwardSlot5", t, r, [](uint8_t *) {},
                [&](uint8_t *p) { return uint64_t(orig(p + 12, a, b, c)); },
                [&](uint8_t *p) {
                    return uint64_t(reinterpret_cast<cHitTrackPlayer *>(p + 12)->SndForwardSlot5(a, b, c));
                });
    }
}

DIFF_TEST(cHitTrackPlayer_SndNotifyEmbedded, 0xb3090e)
{
    auto orig = ctx.Original<bool (__thiscall *)(void *, uint32_t)>(0xb3090e);
    Tables t;
    std::mt19937 rng(803);
    for (int i = 0; i < 200; ++i) {
        uint32_t arg = Edge(rng, i), r = rng();
        Compare(ctx, "SndNotifyEmbedded", t, r, [](uint8_t *) {},
                [&](uint8_t *p) { return uint64_t(orig(p + 12, arg)); },
                [&](uint8_t *p) {
                    return uint64_t(reinterpret_cast<cHitTrackPlayer *>(p + 12)->SndNotifyEmbedded(arg));
                });
    }
}
