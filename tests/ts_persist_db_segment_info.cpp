// Differential tests for cTSPersistDBSegmentInfo (field map in src/ts/cTSPersistDBSegmentInfo.h): the creator
// (0x1df210), the deleting destructor (0x1da210 and its +4 thunk 0x1da04b), QueryInterface (0x1e2e10, +4 thunk
// 0x1e2e48), the folded AddRef/Release thunks of both vtables, the accessors, flag operations, the two
// string forwarders, and the two slots that call back into the object's own vtable (vf11 and vf15).
//
// Each run gets a fresh world built from one seed: the object (0x5c bytes plus canary space, random fill),
// with a copy of the primary vtable whose slots 12 and 14 are recorders, a copy of the +4 vtable whose slots
// 1 and 3 are recorders, and a copy of the game's cRZString vtable at +0x30 whose slots 4 and 16 are
// recorders. The original and ours run in separate worlds and are compared on the return value, the recorded
// calls with their normalised arguments, and the final bytes of the object.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/vtable.h"
#include "heap_poison.h"
#include "ts/cTSPersistDBSegmentInfo.h"

#include <Windows.h>

#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr size_t kObjBytes = 0x80; // the object is 0x5c bytes; the rest is canary space
constexpr size_t kFakeBytes = 0x40;
constexpr size_t kOutBytes = 0x10;
constexpr size_t kTextBytes = 0x40;

std::vector<uint32_t> gTrace;
std::vector<std::pair<uint32_t, uint32_t>> gIds; // address, id
uint32_t gRet = 0;
const void *gGameStringVT = nullptr;
void *gText1 = nullptr;

uint32_t Addr(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

uint32_t Id(uint32_t value)
{
    for (const auto &e : gIds)
        if (e.first == value) return e.second;
    return value;
}

void Note(uint32_t kind, const void *self)
{
    gTrace.push_back(kind);
    gTrace.push_back(Id(Addr(self)));
}

void Arg(uint32_t value)
{
    gTrace.push_back(Id(value));
}

// A cRZString passed to a recorder: its vtable (as a flag), size, capacity, contents and reference count.
// The string objects are on the caller's stack, so their addresses are never recorded.
void Digest(const void *p)
{
    const auto *s = static_cast<const cRZString *>(p);
    gTrace.push_back(s->mVTable == gGameStringVT);
    gTrace.push_back(static_cast<uint32_t>(s->mString.size()));
    gTrace.push_back(static_cast<uint32_t>(s->mString.capacity()));
    for (char c : s->mString) gTrace.push_back(static_cast<uint8_t>(c));
    gTrace.push_back(s->mRefCount);
}

// __fastcall recorders pop their stack arguments like __thiscall callees do; the second parameter takes edx.
uint32_t __fastcall SelfSlot12(void *self, void *, uint32_t a1, const void *s1, const void *s2, uint32_t a4, uint32_t a5)
{
    Note(12, self);
    Arg(a1);
    Digest(s1);
    Digest(s2);
    Arg(a4);
    Arg(a5);
    return gRet;
}

uint32_t __fastcall SelfSlot14(void *self, void *, const char *text, uint32_t a2)
{
    Note(14, self);
    Arg(Addr(text));
    Arg(a2);
    return gRet;
}

uint32_t __fastcall SubAddRef(void *self, void *)
{
    Note(21, self);
    return gRet;
}

void *__fastcall SubDelete(void *self, void *, uint32_t flags)
{
    Note(23, self);
    Arg(flags);
    return self;
}

uint32_t __fastcall Str2Slot4(void *self, void *, const char *text)
{
    Note(34, self);
    Arg(Addr(text));
    return gRet;
}

uint32_t __fastcall Str2Slot16(void *self, void *, const void *other)
{
    Note(36, self);
    Arg(Addr(other));
    return gRet;
}

const char *__fastcall ArgSlot5(void *self, void *)
{
    Note(45, self);
    return static_cast<const char *>(gText1);
}

struct World {
    alignas(8) uint8_t self[kObjBytes];
    alignas(8) uint8_t arg[kFakeBytes]; // the string-like argument of vf15
    alignas(8) uint8_t out[kOutBytes];
    alignas(8) char text1[kTextBytes];
    alignas(8) char text2[kTextBytes];
    void *selfVT[22];
    void *subVT[6];
    void *str2VT[34];
    void *argVT[34];
};

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uint32_t Load32(const uint8_t *base, size_t off)
{
    uint32_t v;
    std::memcpy(&v, base + off, sizeof(v));
    return v;
}

void Build(World &w, const difftest::Context &ctx, uint32_t seed)
{
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    fill(w.self, sizeof(w.self));
    fill(w.arg, sizeof(w.arg));
    fill(w.out, sizeof(w.out));
    // Texts: random printable characters of varied length (up to 39), zero terminated.
    uint32_t len1 = rng() % 40, len2 = rng() % 40;
    for (uint32_t i = 0; i < kTextBytes; ++i) {
        w.text1[i] = i < len1 ? static_cast<char>('a' + rng() % 26) : 0;
        w.text2[i] = i < len2 ? static_cast<char>('a' + rng() % 26) : 0;
    }

    std::memcpy(w.selfVT, ctx.image.At(cTSPersistDBSegmentInfo::kVTable), sizeof(w.selfVT));
    w.selfVT[12] = reinterpret_cast<void *>(&SelfSlot12);
    w.selfVT[14] = reinterpret_cast<void *>(&SelfSlot14);
    std::memcpy(w.subVT, ctx.image.At(cTSPersistDBSegmentInfo::kUnknownVTable), sizeof(w.subVT));
    w.subVT[1] = reinterpret_cast<void *>(&SubAddRef);
    w.subVT[3] = reinterpret_cast<void *>(&SubDelete);
    std::memcpy(w.str2VT, ctx.image.At(cRZString::kVTable), sizeof(w.str2VT));
    w.str2VT[4] = reinterpret_cast<void *>(&Str2Slot4);
    w.str2VT[16] = reinterpret_cast<void *>(&Str2Slot16);
    std::memset(w.argVT, 0, sizeof(w.argVT));
    w.argVT[5] = reinterpret_cast<void *>(&ArgSlot5);

    static constexpr uint32_t kCounts[] = {0, 1, 2, 3, 0xffffffffu, 0x80000000u};
    Put(w.self, 0x00, Addr(w.selfVT));
    Put(w.self, 0x04, Addr(w.subVT));
    Put(w.self, 0x08, rng() % 3 ? kCounts[rng() % 6] : static_cast<uint32_t>(rng()));
    Put(w.self, 0x30, Addr(w.str2VT));
    Put(w.arg, 0, Addr(w.argVT));
}

struct Outcome {
    uint32_t ret = 0;
    std::vector<uint32_t> trace;
};

void SetIds(const World &w)
{
    gIds = {{Addr(w.self), 0x100},   {Addr(w.self + 4), 0x101}, {Addr(w.self + 0x10), 0x102}, {Addr(w.self + 0x30), 0x103},
            {Addr(w.arg), 0x104},    {Addr(w.out), 0x105},      {Addr(w.text1), 0x106},      {Addr(w.text2), 0x107},
            {Addr(w.selfVT), 0x110}, {Addr(w.subVT), 0x111},    {Addr(w.str2VT), 0x112},     {Addr(w.argVT), 0x113}};
    gText1 = const_cast<char *>(w.text1);
}

void Snapshot(const World &w, std::vector<uint32_t> &t)
{
    for (size_t i = 0; i < kObjBytes; i += 4) t.push_back(Id(Load32(w.self, i)));
    for (size_t i = 0; i < kOutBytes; i += 4) t.push_back(Id(Load32(w.out, i)));
}

// Where the world lives. Original and ours run on this one world, rebuilt from the seed before each, so they see it
// at the same address and Id is the same function on both sides: equal words stay equal and different words stay
// different, whatever the call wrote. Two worlds at two addresses made Id depend on the heap layout (a random word
// equal to one world's address was renamed in that world only; lane NB,
// tests/gz_persist_db_segment_multi_packed_files.cpp).
World &SharedWorld()
{
    static World *const w = new World;
    return *w;
}

template <typename Call> Outcome Run(World &w, Call call)
{
    SetIds(w);
    gTrace.clear();
    Outcome out;
    out.ret = call(w);
    Snapshot(w, gTrace);
    out.trace = gTrace;
    return out;
}

cTSPersistDBSegmentInfo *Self(World &w)
{
    return reinterpret_cast<cTSPersistDBSegmentInfo *>(w.self);
}

cTSPersistDBSegmentInfoThunks *Sub(World &w)
{
    return reinterpret_cast<cTSPersistDBSegmentInfoThunks *>(w.self + 4);
}

// Runs both sides on worlds built from `seed` and compares them. The return value is compared under retMask
// (0 skips it, for results the original leaves undefined).
template <typename OrigCall, typename OursCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, OrigCall origCall, OursCall oursCall, uint32_t retMask = 0xffffffffu)
{
    World &w = SharedWorld();
    Build(w, ctx, seed);
    Outcome oa = Run(w, origCall);
    Build(w, ctx, seed);
    Outcome ob = Run(w, oursCall);
    ++ctx.cases;
    if ((oa.ret & retMask) != (ob.ret & retMask) || oa.trace != ob.trace)
        ctx.Fail("%s seed %u: original returned %u (%zu words), ours %u (%zu)", name, seed, oa.ret, oa.trace.size(), ob.ret,
                 ob.trace.size());
}

using U32Fn = uint32_t(__thiscall *)(void *);
using U32ArgFn = uint32_t(__thiscall *)(void *, uint32_t);
using QiFn = bool(__thiscall *)(void *, uint32_t, void **);

uint32_t PickIid(std::mt19937 &rng)
{
    static constexpr uint32_t kIds[] = {1, 0x51509de7, 0x51509de6, 0x51509de8, 0, 2};
    return rng() % 3 ? kIds[rng() % 6] : static_cast<uint32_t>(rng());
}

// Fills the object's strings with `len` characters through the verified FromChar, so deletion has real
// buffers to free. Returns the heap block each string holds (null when it fits inline).
void *StringBlock(const cRZString &s)
{
    if (s.mString.capacity() <= 15) return nullptr;
    void *ptr = const_cast<char *>(s.mString.data());
    if (s.mString.capacity() + 1 >= 0x1000) return *reinterpret_cast<void **>(static_cast<char *>(ptr) - 4);
    return ptr;
}

} // namespace

DIFF_TEST(cTSPersistDBSegmentInfo_Create, 0x1df210)
{
    auto orig = ctx.Original<cTSPersistDBSegmentInfo *(__cdecl *)()>(0x1df210);
    for (int i = 0; i < 100; ++i) {
        // Bytes 0x56 and 0x57 are padding the original never writes: every heap block is poisoned on both sides
        // (heap_poison.h, DT), so they hold the pattern on both sides and every byte is compared.
        heappoison::Scope poison(ctx, i % 2 ? 0x5a : 0xcd);
        cTSPersistDBSegmentInfo *a = orig();
        cTSPersistDBSegmentInfo *b = cTSPersistDBSegmentInfo::Create();
        ++ctx.cases;
        if (!a || !b) {
            ctx.Fail("Create returned null");
            continue;
        }
        const auto *pa = reinterpret_cast<const uint8_t *>(a);
        const auto *pb = reinterpret_cast<const uint8_t *>(b);
        for (size_t k = 0; k < sizeof(*a); ++k) {
            if (pa[k] != pb[k]) {
                ctx.Fail("Create: byte 0x%zx differs (original %02x, ours %02x)", k, pa[k], pb[k]);
                break;
            }
        }
        if (a->mVTable != GameAddress(cTSPersistDBSegmentInfo::kVTable) || a->mUnknown.mVTable != GameAddress(cTSPersistDBSegmentInfo::kUnknownVTable)
            || a->mField54 != 0xffff || a->mFlags != 1)
            ctx.Fail("Create: unexpected initial state");
        ::operator delete(a, sizeof(*a));
        ::operator delete(b, sizeof(*b));
    }
}

DIFF_TEST(cTSPersistDBSegmentInfo_DeletingDestructor, 0x1da210)
{
    // The thunk 0x1da04b steps back four bytes and jumps to 0x1da210: both entries are tested.
    auto origDirect = ctx.Original<cTSPersistDBSegmentInfo *(__thiscall *)(void *, uint32_t)>(0x1da210);
    auto origThunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1da04b);
    auto create = ctx.Original<cTSPersistDBSegmentInfo *(__cdecl *)()>(0x1df210);
    std::mt19937 rng(0x41);
    for (int i = 0; i < 400; ++i) {
        uint32_t flags = i % 4 == 0 ? 0 : i % 4 == 1 ? 1 : static_cast<uint32_t>(rng());
        bool viaThunk = (i / 4) % 2 != 0;
        cTSPersistDBSegmentInfo *objs[2] = {create(), create()};
        // Strings of several sizes: inline, heap, and large enough for the over-aligned allocation.
        static constexpr uint32_t kLens[] = {0, 5, 15, 16, 40, 5000};
        uint32_t len1 = kLens[rng() % 6], len2 = kLens[rng() % 6];
        std::string t1(len1, 'x'), t2(len2, 'y');
        for (auto *o : objs) {
            o->mString1.FromChar(t1.c_str());
            o->mString2.FromChar(t2.c_str());
            o->mUnknown.mRefCount = 7;
            o->mField0C = 0x1234;
            o->mField50 = 0x55;
            // Anything in the vtable fields that the destructor rewrites must be overwritten.
            o->mVTable = reinterpret_cast<void *>(0x111);
            o->mUnknown.mVTable = reinterpret_cast<void *>(0x222);
            o->mString1.mVTable = reinterpret_cast<void *>(0x333);
            o->mString2.mVTable = reinterpret_cast<void *>(0x444);
        }
        cTSPersistDBSegmentInfo *a = objs[0], *b = objs[1];
        void *blocksA[2] = {StringBlock(a->mString1), StringBlock(a->mString2)};
        void *blocksB[2] = {StringBlock(b->mString1), StringBlock(b->mString2)};
        difftest::FreeRecorder recA;
        void *ra = viaThunk ? origThunk(reinterpret_cast<char *>(a) + 4, flags) : origDirect(a, flags);
        recA.Stop();
        bool freedA[3] = {recA.Freed(a), !blocksA[0] || recA.Freed(blocksA[0]), !blocksA[1] || recA.Freed(blocksA[1])};
        auto *subB = reinterpret_cast<cTSPersistDBSegmentInfoThunks *>(reinterpret_cast<char *>(b) + 4);
        difftest::FreeRecorder recB;
        void *rb = viaThunk ? subB->DeletingDestructor4(flags) : b->DeletingDestructor(flags);
        recB.Stop();
        bool freedB[3] = {recB.Freed(b), !blocksB[0] || recB.Freed(blocksB[0]), !blocksB[1] || recB.Freed(blocksB[1])};
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("flags %u thunk %d: wrong return", flags, viaThunk);
        if (std::memcmp(freedA, freedB, sizeof(freedA)) != 0 || freedB[0] != ((flags & 1) != 0) || !freedB[1] || !freedB[2])
            ctx.Fail("flags %u thunk %d: freed object/strings original %d%d%d ours %d%d%d", flags, viaThunk, freedA[0], freedA[1],
                     freedA[2], freedB[0], freedB[1], freedB[2]);
        if (!(flags & 1)) {
            // Fields the destructor leaves: vtables, refcounts, string size/capacity/first byte, other fields.
            bool same = a->mVTable == b->mVTable && a->mVTable == GameAddress(cTSPersistDBSegmentInfo::kVTable)
                && a->mUnknown.mVTable == b->mUnknown.mVTable && a->mUnknown.mRefCount == b->mUnknown.mRefCount
                && a->mField0C == b->mField0C && a->mField50 == b->mField50 && a->mField54 == b->mField54 && a->mFlags == b->mFlags;
            for (auto pr : {std::make_pair(&a->mString1, &b->mString1), std::make_pair(&a->mString2, &b->mString2)}) {
                const uint8_t *x = reinterpret_cast<const uint8_t *>(pr.first);
                const uint8_t *y = reinterpret_cast<const uint8_t *>(pr.second);
                // +0x14..+0x23 is the string buffer: the first byte is zeroed; the rest of a freed heap pointer is stale
                // (different between the worlds), so only the first byte is compared.
                same = same && pr.first->mVTable == pr.second->mVTable && pr.first->mVTable == GameAddress(cRZString::kVTable)
                    && x[4] == y[4] && x[4] == 0 && std::memcmp(x + 0x14, y + 0x14, 0xc) == 0 && pr.first->mRefCount == pr.second->mRefCount;
            }
            if (!same) ctx.Fail("flags %u thunk %d: state after destruction differs", flags, viaThunk);
            ::operator delete(a, sizeof(*a));
            ::operator delete(b, sizeof(*b));
        }
    }
}

DIFF_TEST(cTSPersistDBSegmentInfo_QueryInterface, 0x1e2e10)
{
    auto orig = ctx.Original<QiFn>(0x1e2e10);
    auto origSub = ctx.Original<QiFn>(0x1e2e48);
    std::mt19937 rng(0x42);
    for (int i = 0; i < 1000; ++i) {
        uint32_t seed = rng();
        uint32_t riid = PickIid(rng);
        bool nullOut = rng() % 8 == 0;
        gRet = rng();
        bool viaSub = i % 2 != 0;
        auto outPtr = [&](World &w) { return nullOut ? static_cast<void **>(nullptr) : reinterpret_cast<void **>(w.out); };
        if (!viaSub) {
            Compare(
                ctx, "QueryInterface", seed, [&](World &w) { return uint32_t(orig(w.self, riid, outPtr(w))) & 0xff; },
                [&](World &w) { return uint32_t(Self(w)->QueryInterface(riid, outPtr(w))); }, 0xff);
        } else {
            Compare(
                ctx, "QueryInterface (+4)", seed, [&](World &w) { return uint32_t(origSub(w.self + 4, riid, outPtr(w))) & 0xff; },
                [&](World &w) { return uint32_t(Sub(w)->QueryInterface4(riid, outPtr(w))); }, 0xff);
        }
    }
}

DIFF_TEST(cTSPersistDBSegmentInfo_RefCountThunks, 0x1dab20)
{
    auto addRef = ctx.Original<U32Fn>(0x1dab20);
    auto addRef4 = ctx.Original<U32Fn>(0x1dab28);
    auto release = ctx.Original<U32Fn>(0x1e3970);
    auto release4 = ctx.Original<U32Fn>(0x1e3978);
    std::mt19937 rng(0x43);
    for (int i = 0; i < 800; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        switch (i % 4) {
        case 0:
            Compare(ctx, "AddRef", seed, [&](World &w) { return addRef(w.self); }, [&](World &w) { return Self(w)->AddRef(); });
            break;
        case 1:
            Compare(ctx, "AddRef4", seed, [&](World &w) { return addRef4(w.self + 4); }, [&](World &w) { return Sub(w)->AddRef4(); });
            break;
        case 2:
            Compare(ctx, "Release", seed, [&](World &w) { return release(w.self); }, [&](World &w) { return Self(w)->Release(); });
            break;
        default:
            Compare(ctx, "Release4", seed, [&](World &w) { return release4(w.self + 4); }, [&](World &w) { return Sub(w)->Release4(); });
            break;
        }
    }
}

DIFF_TEST(cTSPersistDBSegmentInfo_Accessors, 0x147780)
{
    auto get1 = ctx.Original<U32Fn>(0x147780);
    auto get2 = ctx.Original<U32Fn>(0x1c6e40);
    auto get50 = ctx.Original<U32Fn>(0x1e0950);
    auto get54 = ctx.Original<U32Fn>(0x1e0540);
    auto getFlags = ctx.Original<U32Fn>(0x1dff70);
    auto test = ctx.Original<U32ArgFn>(0x1dcc40);
    auto change = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0x1e4d70);
    auto set50 = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x1e5370);
    auto set54 = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x1e5250);
    std::mt19937 rng(0x44);
    for (int i = 0; i < 2700; ++i) {
        uint32_t seed = rng();
        uint32_t mask = rng() % 4 == 0 ? 0 : static_cast<uint32_t>(rng() & (rng() | rng()));
        uint32_t value = rng();
        switch (i % 9) {
        case 0:
            Compare(ctx, "GetString1", seed, [&](World &w) { return Id(get1(w.self)); }, [&](World &w) { return Id(Addr(Self(w)->GetString1())); });
            break;
        case 1:
            Compare(ctx, "GetString2", seed, [&](World &w) { return Id(get2(w.self)); }, [&](World &w) { return Id(Addr(Self(w)->GetString2())); });
            break;
        case 2:
            Compare(ctx, "GetField50", seed, [&](World &w) { return get50(w.self); }, [&](World &w) { return Self(w)->GetField50(); });
            break;
        case 3:
            // The original loads only ax; the upper half of eax is whatever the caller had.
            Compare(ctx, "GetField54", seed, [&](World &w) { return get54(w.self); }, [&](World &w) { return Self(w)->GetField54(); }, 0xffff);
            break;
        case 4:
            Compare(ctx, "GetFlags", seed, [&](World &w) { return getFlags(w.self); }, [&](World &w) { return Self(w)->GetFlags(); });
            break;
        case 5:
            // sete al: only the low byte is defined.
            Compare(ctx, "TestFlags", seed, [&](World &w) { return test(w.self, mask); }, [&](World &w) { return uint32_t(Self(w)->TestFlags(mask)); }, 0xff);
            break;
        case 6: {
            uint32_t set = rng() & 1;
            // Returns nothing the callers can use (eax is a leftover on the set path, the new flags on the other).
            Compare(ctx, "ChangeFlags", seed, [&](World &w) { change(w.self, mask, set); return 0u; },
                    [&](World &w) { Self(w)->ChangeFlags(mask, set != 0); return 0u; });
            break;
        }
        case 7:
            Compare(ctx, "SetField50", seed, [&](World &w) { set50(w.self, value); return 0u; },
                    [&](World &w) { Self(w)->SetField50(value); return 0u; });
            break;
        default:
            Compare(ctx, "SetField54", seed, [&](World &w) { set54(w.self, value); return 0u; },
                    [&](World &w) { Self(w)->SetField54(static_cast<uint16_t>(value)); return 0u; });
            break;
        }
    }
}

DIFF_TEST(cTSPersistDBSegmentInfo_StringForwarders, 0x1e4dc0)
{
    auto fromChar = ctx.Original<uint32_t(__thiscall *)(void *, const char *)>(0x1e4dc0);
    auto assign = ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x1e4db0);
    std::mt19937 rng(0x45);
    for (int i = 0; i < 600; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        if (i % 2 == 0)
            Compare(ctx, "SetString2FromChar", seed, [&](World &w) { return fromChar(w.self, w.text1); },
                    [&](World &w) { return Self(w)->SetString2FromChar(w.text1); });
        else
            Compare(ctx, "AssignString2", seed, [&](World &w) { return assign(w.self, w.out); },
                    [&](World &w) { return Self(w)->AssignString2(w.out); });
    }
}

DIFF_TEST(cTSPersistDBSegmentInfo_CallsBack, 0x1e4f20)
{
    gGameStringVT = GameAddress(cRZString::kVTable);
    auto setPath = ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t)>(0x1e4f20);
    auto setFromChars = ctx.Original<void(__thiscall *)(void *, uint32_t, const char *, const char *, uint32_t, uint32_t)>(0x1e0c20);
    std::mt19937 rng(0x46);
    for (int i = 0; i < 800; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), b = rng(), c = rng();
        if (i % 2 == 0) {
            // vf15 returns slot 14's eax unchanged.
            Compare(ctx, "SetPathFromString", seed, [&](World &w) { return setPath(w.self, w.arg, a); },
                    [&](World &w) { return Self(w)->SetPathFromString(w.arg, a); });
        } else {
            // eax on return is a leftover of the string destructors: not compared.
            Compare(ctx, "SetFromChars", seed, [&](World &w) { setFromChars(w.self, a, w.text1, w.text2, b, c); return 0u; },
                    [&](World &w) { Self(w)->SetFromChars(a, w.text1, w.text2, b, c); return 0u; });
        }
    }
}
