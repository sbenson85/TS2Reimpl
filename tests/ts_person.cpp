// Differential tests for the cITSPerson subobject slots of cTSPerson reimplemented so far (field map and
// call notes in src/ts/cTSPerson.h). Each run gets a fresh world: a 0x910-byte buffer of random bytes holding the
// cITSPerson subobject at offset 0x2f0 (its vtable pointer is set up by Build; the fields are sub-relative), and a
// fake vtable with recorders at slots 10, 11, 123 and 125. Every recorder logs its slot, object and arguments, so
// the calls, their arguments and the bytes that changed must match. Return values are compared in full (eax)
// where the original's eax is defined, and only in the low byte or word where the original returns a byte or word.
#include "difftest.h"
#include "ts/cTSPerson.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using NoArgRet8 = uint8_t(__thiscall *)(void *);
using NoArgRet16 = uint16_t(__thiscall *)(void *);
using NoArgRet32 = uint32_t(__thiscall *)(void *);
using NoArgPtr = void *(__thiscall *)(void *);
using NoArgVoid = void(__thiscall *)(void *);
using OneArgRet32 = uint32_t(__thiscall *)(void *, uint32_t);
using OneArgVoid = void(__thiscall *)(void *, uint32_t);
using TwoArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);

constexpr size_t kDataBytes = 0x910;
constexpr size_t kSub = 0x2f0; // the cITSPerson subobject within the primary object

struct World {
    alignas(8) uint8_t data[kDataBytes];
    void *vt[172];
};

// Recorded calls: a slot number, the subobject's id, then the arguments.
std::vector<uint32_t> gTrace;
World *gW = nullptr;

// Return values for this seed (the same in both worlds of a seed).
uint32_t gRet10 = 0, gRet11 = 0, gRet123 = 0, gRet125 = 0;

uint32_t IdOf(const void *p)
{
    if (!gW) return 0xff;
    auto a = reinterpret_cast<uintptr_t>(p);
    auto base = reinterpret_cast<uintptr_t>(gW->data);
    if (a == base + kSub) return 1;
    return 0xfe;
}

void Note(uint32_t slot, const void *self)
{
    gTrace.push_back(slot);
    gTrace.push_back(IdOf(self));
}

// The fake slots are __fastcall so they pop their stack arguments the way the game's callees do.
uint32_t __fastcall P10(void *self, void *, uint32_t a, uint32_t b)
{
    Note(10, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
    return gRet10;
}

uint32_t __fastcall P11(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Note(11, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
    gTrace.push_back(c);
    return gRet11;
}

uint32_t __fastcall P123(void *self, void *, uint32_t a)
{
    Note(123, self);
    gTrace.push_back(a);
    return gRet123;
}

uint32_t __fastcall P125(void *self, void *, uint32_t a)
{
    Note(125, self);
    gTrace.push_back(a);
    return gRet125;
}

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uintptr_t Addr(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

// A callee's eax: the word test in vf159 and the bit tests in vf124 see the low byte or word, so sometimes those
// are zero or negative.
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
    std::mt19937 rng(seed * 2246822519u + 11);
    for (auto &b : w.data) b = static_cast<uint8_t>(rng());

    gRet10 = PickRet(rng);
    gRet11 = PickRet(rng);
    gRet123 = PickRet(rng);
    gRet125 = PickRet(rng);

    for (auto &s : w.vt) s = nullptr;
    w.vt[10] = reinterpret_cast<void *>(&P10);
    w.vt[11] = reinterpret_cast<void *>(&P11);
    w.vt[123] = reinterpret_cast<void *>(&P123);
    w.vt[125] = reinterpret_cast<void *>(&P125);
    Put(w.data, kSub, Addr(w.vt));
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

// A dword argument, sometimes with a zero low word or zero high bits.
uint32_t PickWord(std::mt19937 &r)
{
    uint32_t v = r();
    switch (r() % 6) {
    case 0: return v & 0xffff0000u;
    case 1: return v & 0xffffu;
    default: return v;
    }
}

} // namespace

DIFF_TEST(TSPerson_vf10, 0x51aa40)
{
    auto orig = ctx.Original<TwoArgRet32>(0x51aa40);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        // The address is index*2 + 0x356 with 32-bit wrap, so only bit 31 of the index can vary without moving
        // the read out of the buffer: the high word of the result then varies in that bit. The unused second
        // argument is random.
        uint32_t index = (r() % 0x100) | (r() & 0x80000000u);
        uint32_t unused = r();
        Compare(ctx, "vf10", seed, true,
                [&](World &w) { return orig(w.data + kSub, index, unused); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf10(index, unused); });
    }
}

DIFF_TEST(TSPerson_vf30, 0x51c210)
{
    auto orig = ctx.Original<NoArgRet8>(0x51c210);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf30", seed, true,
                [&](World &w) { return orig(w.data + kSub); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf30(); });
    }
}

DIFF_TEST(TSPerson_vf32, 0x51c160)
{
    auto orig = ctx.Original<NoArgRet32>(0x51c160);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf32", seed, true,
                [&](World &w) { return orig(w.data + kSub); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf32(); });
    }
}

DIFF_TEST(TSPerson_vf33, 0x51c0a0)
{
    auto orig = ctx.Original<NoArgRet8>(0x51c0a0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf33", seed, true,
                [&](World &w) { return orig(w.data + kSub); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf33(); });
    }
}

DIFF_TEST(TSPerson_vf34, 0x51c080)
{
    auto orig = ctx.Original<NoArgRet8>(0x51c080);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf34", seed, true,
                [&](World &w) { return orig(w.data + kSub); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf34(); });
    }
}

DIFF_TEST(TSPerson_vf50, 0x5196e0)
{
    auto orig = ctx.Original<NoArgRet16>(0x5196e0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf50", seed, true,
                [&](World &w) { return orig(w.data + kSub); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf50(); });
    }
}

DIFF_TEST(TSPerson_vf126, 0x512e40)
{
    auto orig = ctx.Original<NoArgRet16>(0x512e40);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf126", seed, true,
                [&](World &w) { return orig(w.data + kSub); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf126(); });
    }
}

DIFF_TEST(TSPerson_vf139, 0x519790)
{
    auto orig = ctx.Original<NoArgRet8>(0x519790);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf139", seed, true,
                [&](World &w) { return orig(w.data + kSub); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf139(); });
    }
}

DIFF_TEST(TSPerson_vf148, 0x519c30)
{
    auto orig = ctx.Original<NoArgPtr>(0x519c30);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        // The result is an address inside each world's buffer, so compare its offset from the buffer.
        Compare(ctx, "vf148", seed, true,
                [&](World &w) { return Addr(orig(w.data + kSub)) - Addr(w.data); },
                [&](World &w) {
                    return Addr(reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf148()) - Addr(w.data);
                });
    }
}

DIFF_TEST(TSPerson_vf57, 0x519d80)
{
    auto orig = ctx.Original<NoArgRet32>(0x519d80);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf57", seed, true,
                [&](World &w) { return orig(w.data + kSub); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf57(); });
    }
}

DIFF_TEST(TSPerson_vf58, 0x520a50)
{
    auto orig = ctx.Original<OneArgRet32>(0x520a50);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 2);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf58", seed, true,
                [&](World &w) { return orig(w.data + kSub, a); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf58(a); });
    }
}

DIFF_TEST(TSPerson_vf121, 0x520a60)
{
    auto orig = ctx.Original<OneArgVoid>(0x520a60);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 3);
        uint32_t w16 = PickWord(r);
        Compare(ctx, "vf121", seed, false,
                [&](World &w) {
                    orig(w.data + kSub, w16);
                    return uint64_t(0);
                },
                [&](World &w) {
                    reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf121(w16);
                    return uint64_t(0);
                });
    }
}

DIFF_TEST(TSPerson_vf124, 0x512c00)
{
    auto orig = ctx.Original<OneArgRet32>(0x512c00);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 4);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf124", seed, true,
                [&](World &w) { return orig(w.data + kSub, a); },
                [&](World &w) { return reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf124(a); });
    }
}

DIFF_TEST(TSPerson_vf159, 0x51e840)
{
    auto orig = ctx.Original<NoArgVoid>(0x51e840);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf159", seed, false,
                [&](World &w) {
                    orig(w.data + kSub);
                    return uint64_t(0);
                },
                [&](World &w) {
                    reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf159();
                    return uint64_t(0);
                });
    }
}

DIFF_TEST(TSPerson_vf169, 0x512d50)
{
    auto orig = ctx.Original<OneArgVoid>(0x512d50);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 5);
        uint32_t unused = r();
        Compare(ctx, "vf169", seed, false,
                [&](World &w) {
                    orig(w.data + kSub, unused);
                    return uint64_t(0);
                },
                [&](World &w) {
                    reinterpret_cast<cTSPersonITS *>(w.data + kSub)->vf169(unused);
                    return uint64_t(0);
                });
    }
}
