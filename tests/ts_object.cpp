// Differential tests for the cTSObject functions reimplemented so far (field map in src/ts/cTSObject.h).
// Each run gets a fresh copy of a fake world: a primary object with random bytes and four fake subobjects
// (+0x40 and +0x1c4 of the primary point at two of them; the others are returned by slot calls). Every
// vtable slot the functions call is a recorder that logs its object and arguments, so the calls made, their
// arguments and the bytes that changed must match. Return values are compared in full (eax) where the
// original's eax is defined; void functions leave eax as the last callee's result, which is not compared.
#include "difftest.h"
#include "ts/cTSObject.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using OneArgRet32 = uint32_t(__thiscall *)(void *, uint32_t);
using NoArgVoid = void(__thiscall *)(void *);
using OneArgVoid = void(__thiscall *)(void *, uint32_t);
using FloatArgVoid = void(__thiscall *)(void *, float);
using MixedArgVoid = void(__thiscall *)(void *, uint32_t, float);
using FiveArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using SixArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using FloatTailRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, float);

constexpr size_t kDataBytes = 0x380;
// Offsets of the objects within the world's byte buffer.
constexpr size_t kOffPrimary = 0x000;
constexpr size_t kOffSubA = 0x300;  // +0x40 of the primary: slot 18 returns an object, or null
constexpr size_t kOffSubB = 0x320;  // +0x1c4 of the primary: slot 74 is called
constexpr size_t kOffResA = 0x340;  // returned by subobject A's slot 18: slot 20 is called
constexpr size_t kOffResB = 0x360;  // returned by the primary's slot 91: slot 8 is called

struct World {
    alignas(8) uint8_t data[kDataBytes];
    void *vtPrimary[360];
    void *vtA[19];
    void *vtB[75];
    void *vtResA[21];
    void *vtResB[9];
};

// Recorded calls: a kind, the object's id, then the arguments (words; floats as their bits).
std::vector<uint32_t> gTrace;
World *gW = nullptr;

// Return values for this seed, set by Build (the same in both worlds of a seed).
uint32_t gRet13 = 0, gRet47 = 0, gRet93 = 0, gRet104 = 0, gRet152 = 0, gRet153 = 0, gRet74 = 0, gRet8 = 0;
uint32_t gRet14 = 0, gRet182 = 0, gRet238 = 0, gRet324 = 0, gRet325 = 0;
bool gA18Null = false;

uint32_t IdOf(const void *p)
{
    if (!gW) return 0xff;
    auto a = reinterpret_cast<uintptr_t>(p);
    auto base = reinterpret_cast<uintptr_t>(gW->data);
    if (a == base + kOffPrimary) return 1;
    if (a == base + kOffSubA) return 2;
    if (a == base + kOffSubB) return 3;
    if (a == base + kOffResA) return 4;
    if (a == base + kOffResB) return 5;
    return 0xfe;
}

void Note(uint32_t kind, const void *self)
{
    gTrace.push_back(kind);
    gTrace.push_back(IdOf(self));
}

uint32_t Bits(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, sizeof(v));
    return v;
}

// The fake slots are __fastcall so they pop their stack arguments the way the game's callees do.
uint32_t __fastcall P13(void *self, void *, uint32_t a)
{
    Note(13, self);
    gTrace.push_back(a);
    return gRet13;
}

uint32_t __fastcall P47(void *self, void *)
{
    Note(47, self);
    return gRet47;
}

void *__fastcall P91(void *self, void *)
{
    Note(91, self);
    return gW->data + kOffResB;
}

uint32_t __fastcall P93(void *self, void *, uint32_t a)
{
    Note(93, self);
    gTrace.push_back(a);
    return gRet93;
}

uint32_t __fastcall P104(void *self, void *)
{
    Note(104, self);
    return gRet104;
}

uint32_t __fastcall P152(void *self, void *)
{
    Note(152, self);
    return gRet152;
}

uint32_t __fastcall P153(void *self, void *)
{
    Note(153, self);
    return gRet153;
}

void __fastcall P170(void *self, void *, uint32_t a, uint32_t b)
{
    Note(170, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
}

void __fastcall P171(void *self, void *, uint32_t a, float f, uint32_t b)
{
    Note(171, self);
    gTrace.push_back(a);
    gTrace.push_back(Bits(f));
    gTrace.push_back(b);
}

void __fastcall P208(void *self, void *)
{
    Note(208, self);
}

void *__fastcall A18(void *self, void *)
{
    Note(18, self);
    return gA18Null ? nullptr : gW->data + kOffResA;
}

uint32_t __fastcall B74(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Note(74, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
    gTrace.push_back(c);
    return gRet74;
}

void __fastcall ResA20(void *self, void *, uint32_t w)
{
    Note(20, self);
    gTrace.push_back(w);
}

uint32_t __fastcall ResB8(void *self, void *, uint32_t a)
{
    Note(8, self);
    gTrace.push_back(a);
    return gRet8;
}

// Callee ret N as the game's slots have them: slot 14 ret 8, slot 182 ret 0x18, slot 238 ret 0x14,
// slot 324 ret 0xc, slot 325 ret 4 (read from their implementations in the binary).
uint32_t __fastcall P14(void *self, void *, uint32_t a, uint32_t b)
{
    Note(14, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
    return gRet14;
}

uint32_t __fastcall P182(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    Note(182, self);
    for (uint32_t v : {a, b, c, d, e, f}) gTrace.push_back(v);
    return gRet182;
}

uint32_t __fastcall P238(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    Note(238, self);
    for (uint32_t v : {a, b, c, d, e}) gTrace.push_back(v);
    return gRet238;
}

uint32_t __fastcall P324(void *self, void *, uint32_t a, uint32_t b, float f)
{
    Note(324, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
    gTrace.push_back(Bits(f));
    return gRet324;
}

uint32_t __fastcall P325(void *self, void *, uint32_t a)
{
    Note(325, self);
    gTrace.push_back(a);
    return gRet325;
}

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uintptr_t Addr(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

// A callee's eax: the predicates look at al or ax, so sometimes the low byte or the low word is zero.
uint32_t PickRet(std::mt19937 &r)
{
    uint32_t v = r();
    switch (r() % 4) {
    case 0: return v & 0xffffff00u;
    case 1: return v & 0xffff0000u;
    default: return v;
    }
}

void Build(World &w, uint32_t seed)
{
    std::mt19937 rng(seed * 2246822519u + 7);
    for (auto &b : w.data) b = static_cast<uint8_t>(rng());

    gRet13 = PickRet(rng);
    gRet47 = PickRet(rng);
    gRet93 = PickRet(rng);
    gRet104 = PickRet(rng);
    gRet152 = PickRet(rng);
    gRet153 = PickRet(rng);
    gRet74 = PickRet(rng);
    gRet8 = PickRet(rng);
    gRet14 = PickRet(rng);
    gRet182 = PickRet(rng);
    gRet238 = PickRet(rng);
    gRet324 = PickRet(rng);
    gRet325 = PickRet(rng);
    gA18Null = rng() % 4 == 0;

    for (auto &s : w.vtPrimary) s = nullptr;
    w.vtPrimary[13] = reinterpret_cast<void *>(&P13);
    w.vtPrimary[47] = reinterpret_cast<void *>(&P47);
    w.vtPrimary[91] = reinterpret_cast<void *>(&P91);
    w.vtPrimary[93] = reinterpret_cast<void *>(&P93);
    w.vtPrimary[104] = reinterpret_cast<void *>(&P104);
    w.vtPrimary[152] = reinterpret_cast<void *>(&P152);
    w.vtPrimary[153] = reinterpret_cast<void *>(&P153);
    w.vtPrimary[170] = reinterpret_cast<void *>(&P170);
    w.vtPrimary[171] = reinterpret_cast<void *>(&P171);
    w.vtPrimary[208] = reinterpret_cast<void *>(&P208);
    w.vtPrimary[14] = reinterpret_cast<void *>(&P14);
    w.vtPrimary[182] = reinterpret_cast<void *>(&P182);
    w.vtPrimary[238] = reinterpret_cast<void *>(&P238);
    w.vtPrimary[324] = reinterpret_cast<void *>(&P324);
    w.vtPrimary[325] = reinterpret_cast<void *>(&P325);

    for (auto &s : w.vtA) s = nullptr;
    w.vtA[18] = reinterpret_cast<void *>(&A18);
    for (auto &s : w.vtB) s = nullptr;
    w.vtB[74] = reinterpret_cast<void *>(&B74);
    for (auto &s : w.vtResA) s = nullptr;
    w.vtResA[20] = reinterpret_cast<void *>(&ResA20);
    for (auto &s : w.vtResB) s = nullptr;
    w.vtResB[8] = reinterpret_cast<void *>(&ResB8);

    Put(w.data, kOffPrimary, Addr(w.vtPrimary));
    Put(w.data, kOffSubA, Addr(w.vtA));
    Put(w.data, kOffSubB, Addr(w.vtB));
    Put(w.data, kOffResA, Addr(w.vtResA));
    Put(w.data, kOffResB, Addr(w.vtResB));
    // The subobject pointers the primary's functions use (+0x40 and +0x1c4).
    Put(w.data, 0x40, Addr(w.data + kOffSubA));
    Put(w.data, 0x1c4, Addr(w.data + kOffSubB));
}

struct Outcome {
    uint64_t ret = 0;
    std::vector<uint32_t> trace;
    // Bytes of the world that changed, with their new values.
    std::vector<std::pair<size_t, uint8_t>> changes;
};

template <typename Call> Outcome Run(World &w, Call call)
{
    gW = &w;
    uint8_t before[kDataBytes];
    std::memcpy(before, w.data, kDataBytes);

    gTrace.clear();
    Outcome out;
    out.ret = call(w);
    out.trace = gTrace;
    for (size_t i = 0; i < kDataBytes; ++i)
        if (before[i] != w.data[i]) out.changes.push_back({i, w.data[i]});
    gW = nullptr;
    return out;
}

// Runs both sides on identical worlds and compares them. `returnDefined` is false for void functions.
template <typename OrigCall, typename OursCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, bool returnDefined, OrigCall origCall,
             OursCall oursCall)
{
    World a, b;
    Build(a, seed);
    Build(b, seed);
    Outcome oa = Run(a, [&](World &w) { return origCall(w); });
    Outcome ob = Run(b, [&](World &w) { return oursCall(w); });
    ++ctx.cases;
    if ((returnDefined && oa.ret != ob.ret) || oa.trace != ob.trace) {
        ctx.Fail("%s seed %u: original returned 0x%llx (%zu trace words), ours 0x%llx (%zu)", name, seed,
                 static_cast<unsigned long long>(oa.ret), oa.trace.size(), static_cast<unsigned long long>(ob.ret),
                 ob.trace.size());
    }
    if (oa.changes.size() != ob.changes.size()) {
        ctx.Fail("%s seed %u: original changed %zu bytes, ours %zu", name, seed, oa.changes.size(), ob.changes.size());
    } else {
        for (size_t i = 0; i < oa.changes.size(); ++i) {
            if (oa.changes[i] != ob.changes[i]) {
                ctx.Fail("%s seed %u: byte +0x%zx differs (original %u, ours %u)", name, seed, oa.changes[i].first,
                         oa.changes[i].second, ob.changes[i].second);
                break;
            }
        }
    }
}

// A dword argument, sometimes with a zero low word or zero high bits, as the field readers see it.
uint32_t PickWord(std::mt19937 &r)
{
    uint32_t v = r();
    switch (r() % 6) {
    case 0: return v & 0xffff0000u;
    case 1: return v & 0xffffu;
    default: return v;
    }
}

float PickFloat(std::mt19937 &r)
{
    uint32_t bits = r();
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

uint32_t FlagsOf(const World &w)
{
    uint32_t v;
    std::memcpy(&v, w.data + 0x230, sizeof(v));
    return v;
}

} // namespace

DIFF_TEST(TSObject_vf31, 0x53d780)
{
    auto orig = ctx.Original<OneArgRet32>(0x53d780);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        // A quarter of the cases keep the flags' low five bits (no change); the rest are random.
        World probe;
        Build(probe, seed);
        uint32_t flags = FlagsOf(probe);
        uint32_t a = (seed & 3) == 0 ? ((flags & 0x1f) | (r() & ~0x1fu)) : PickWord(r);
        Compare(ctx, "vf31", seed, true,
                [&](World &w) { return orig(w.data, a); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf31(a); });
    }
}

DIFF_TEST(TSObject_vf37, 0x5456b0)
{
    auto orig = ctx.Original<OneArgRet32>(0x5456b0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 2);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf37", seed, true,
                [&](World &w) { return orig(w.data, a); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf37(a); });
    }
}

DIFF_TEST(TSObject_vf314, 0x545b80)
{
    auto orig = ctx.Original<FloatArgVoid>(0x545b80);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 3);
        float f = PickFloat(r);
        Compare(ctx, "vf314", seed, false,
                [&](World &w) {
                    orig(w.data, f);
                    return uint64_t(0);
                },
                [&](World &w) {
                    reinterpret_cast<cTSObject *>(w.data)->vf314(f);
                    return uint64_t(0);
                });
    }
}

DIFF_TEST(TSObject_vf119, 0x541690)
{
    auto orig = ctx.Original<NoArgRet32>(0x541690);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf119", seed, true,
                [&](World &w) { return orig(w.data); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf119(); });
    }
}

DIFF_TEST(TSObject_vf124, 0x533bd0)
{
    auto orig = ctx.Original<NoArgRet32>(0x533bd0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf124", seed, true,
                [&](World &w) { return orig(w.data); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf124(); });
    }
}

DIFF_TEST(TSObject_vf125, 0x531250)
{
    auto orig = ctx.Original<NoArgRet32>(0x531250);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf125", seed, true,
                [&](World &w) { return orig(w.data); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf125(); });
    }
}

DIFF_TEST(TSObject_vf127, 0x53dc00)
{
    auto orig = ctx.Original<NoArgRet32>(0x53dc00);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf127", seed, true,
                [&](World &w) { return orig(w.data); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf127(); });
    }
}

DIFF_TEST(TSObject_vf177, 0x546810)
{
    auto orig = ctx.Original<OneArgVoid>(0x546810);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 4);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf177", seed, false,
                [&](World &w) {
                    orig(w.data, a);
                    return uint64_t(0);
                },
                [&](World &w) {
                    reinterpret_cast<cTSObject *>(w.data)->vf177(a);
                    return uint64_t(0);
                });
    }
}

DIFF_TEST(TSObject_vf178, 0x546890)
{
    auto orig = ctx.Original<MixedArgVoid>(0x546890);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 5);
        uint32_t a = PickWord(r);
        float f = PickFloat(r);
        Compare(ctx, "vf178", seed, false,
                [&](World &w) {
                    orig(w.data, a, f);
                    return uint64_t(0);
                },
                [&](World &w) {
                    reinterpret_cast<cTSObject *>(w.data)->vf178(a, f);
                    return uint64_t(0);
                });
    }
}

DIFF_TEST(TSObject_vf221, 0x543050)
{
    auto orig = ctx.Original<NoArgVoid>(0x543050);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf221", seed, false,
                [&](World &w) {
                    orig(w.data);
                    return uint64_t(0);
                },
                [&](World &w) {
                    reinterpret_cast<cTSObject *>(w.data)->vf221();
                    return uint64_t(0);
                });
    }
}

DIFF_TEST(TSObject_vf245, 0x52f060)
{
    auto orig = ctx.Original<OneArgRet32>(0x52f060);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 6);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf245", seed, true,
                [&](World &w) { return orig(w.data, a); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf245(a); });
    }
}

DIFF_TEST(TSObject_vf346, 0x5aacd0)
{
    auto orig = ctx.Original<NoArgRet32>(0x5aacd0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf346", seed, true,
                [&](World &w) { return orig(w.data); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf346(); });
    }
}

// The predicates and byte reads that leave only al defined: the upper bits of the original's eax are the
// caller's, so only al is compared (masked on both sides).
DIFF_TEST(TSObject_vf121, 0x533c20)
{
    auto orig = ctx.Original<NoArgRet32>(0x533c20);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf121", seed, true,
                [&](World &w) { return uint64_t(orig(w.data) & 0xff); },
                [&](World &w) { return uint64_t(reinterpret_cast<cTSObject *>(w.data)->vf121() & 0xff); });
    }
}

DIFF_TEST(TSObject_vf214, 0x52df50)
{
    auto orig = ctx.Original<NoArgRet32>(0x52df50);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf214", seed, true,
                [&](World &w) { return uint64_t(orig(w.data) & 0xff); },
                [&](World &w) { return uint64_t(reinterpret_cast<cTSObject *>(w.data)->vf214() & 0xff); });
    }
}

DIFF_TEST(TSObject_vf219, 0x542fa0)
{
    auto orig = ctx.Original<NoArgRet32>(0x542fa0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf219", seed, true,
                [&](World &w) { return uint64_t(orig(w.data) & 0xff); },
                [&](World &w) { return uint64_t(reinterpret_cast<cTSObject *>(w.data)->vf219() & 0xff); });
    }
}

DIFF_TEST(TSObject_vf213, 0x53d6e0)
{
    auto orig = ctx.Original<OneArgVoid>(0x53d6e0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 8);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf213", seed, false,
                [&](World &w) {
                    orig(w.data, a);
                    return uint64_t(0);
                },
                [&](World &w) {
                    reinterpret_cast<cTSObject *>(w.data)->vf213(a);
                    return uint64_t(0);
                });
    }
}

DIFF_TEST(TSObject_vf222, 0x5456c0)
{
    auto orig = ctx.Original<OneArgVoid>(0x5456c0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 9);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf222", seed, false,
                [&](World &w) {
                    orig(w.data, a);
                    return uint64_t(0);
                },
                [&](World &w) {
                    reinterpret_cast<cTSObject *>(w.data)->vf222(a);
                    return uint64_t(0);
                });
    }
}

// Full eax: these return a callee's eax unchanged.
DIFF_TEST(TSObject_vf211, 0x5343d0)
{
    auto orig = ctx.Original<NoArgRet32>(0x5343d0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf211", seed, true,
                [&](World &w) { return orig(w.data); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf211(); });
    }
}

DIFF_TEST(TSObject_vf212, 0x534160)
{
    auto orig = ctx.Original<NoArgRet32>(0x534160);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf212", seed, true,
                [&](World &w) { return orig(w.data); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf212(); });
    }
}

DIFF_TEST(TSObject_vf224, 0x5455d0)
{
    auto orig = ctx.Original<OneArgRet32>(0x5455d0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 10);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf224", seed, true,
                [&](World &w) { return orig(w.data, a); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf224(a); });
    }
}

DIFF_TEST(TSObject_vf315, 0x545f60)
{
    auto orig = ctx.Original<FloatTailRet32>(0x545f60);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 11);
        uint32_t a1 = PickWord(r), a2 = PickWord(r);
        float a3 = PickFloat(r);
        Compare(ctx, "vf315", seed, true,
                [&](World &w) { return orig(w.data, a1, a2, a3); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf315(a1, a2, a3); });
    }
}

DIFF_TEST(TSObject_vf316, 0x545cd0)
{
    auto orig = ctx.Original<OneArgRet32>(0x545cd0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 12);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf316", seed, true,
                [&](World &w) { return orig(w.data, a); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf316(a); });
    }
}

DIFF_TEST(TSObject_vf328, 0x5ac9e0)
{
    auto orig = ctx.Original<NoArgRet32>(0x5ac9e0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        // Only al is defined on the flag path, but the callee's eax is returned in full, so compare it all.
        Compare(ctx, "vf328", seed, true,
                [&](World &w) { return orig(w.data); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf328(); });
    }
}

DIFF_TEST(TSObject_vf183, 0x5406f0)
{
    auto orig = ctx.Original<FiveArgRet32>(0x5406f0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 13);
        uint32_t a1 = PickWord(r), a2 = PickWord(r), a3 = PickWord(r), a4 = PickWord(r), a5 = PickWord(r);
        Compare(ctx, "vf183", seed, true,
                [&](World &w) { return orig(w.data, a1, a2, a3, a4, a5); },
                [&](World &w) { return reinterpret_cast<cTSObject *>(w.data)->vf183(a1, a2, a3, a4, a5); });
    }
}
