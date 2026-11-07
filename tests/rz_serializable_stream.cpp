// Differential tests for cRZSerializableStream (layout and members in src/rz/cRZSerializableStream.h).
// Each case builds two identical worlds from a seed: a heap object of 0x38 random bytes carrying fake
// vtables whose called slots record their calls (AddRef, GetError, SetError, the deleting destructor,
// the owners' Release, and the output/input stream slots Write and Read use), and a buffer allocated
// with operator new[] when the stream has capacity. The original runs on one world and ours on the
// other; the return value, the recorded calls (pointers as offsets or flags, never raw), the whole
// object (data pointer reduced to null or not) and the buffer contents must match.
// Allocation failure is not tested: the original's operator new[] throws and the harness cannot
// simulate it.
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZSerializableStream.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Th0 = uint32_t(__thiscall *)(void *);
using Th1 = uint32_t(__thiscall *)(void *, uint32_t);
using Th1P = uint32_t(__thiscall *)(void *, void *);
using Th2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using Th2P = uint32_t(__thiscall *)(void *, const void *, uint32_t);
using ThQI = uint32_t(__thiscall *)(void *, uint32_t, void **);

constexpr size_t kObjBytes = 0x38;

std::vector<uint32_t> gTrace;
uintptr_t gBase = 0;
uint8_t *gData = nullptr;
uint32_t gNoise = 0;
uint32_t gReadSize = 0;
bool gReadFail = false;
uint8_t *gStash = nullptr; // data pointer hidden by FakeOutSint32, restored so the world can free it

uint32_t Addr32(uint32_t rva)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress(rva)));
}

struct IdEntry {
    const void *p;
    uint32_t id;
};
std::vector<IdEntry> gIds;

uint32_t IdOf(const void *p)
{
    for (const auto &e : gIds)
        if (e.p == p) return e.id;
    return 0xffff;
}

void Note(uint32_t kind, const void *self)
{
    gTrace.push_back(kind);
    gTrace.push_back(IdOf(self));
}

// Primary-object slots. Called with ecx = the subobject the original used, so the offset is recorded.
uint32_t __fastcall FakeAddRef(void *self, void *)
{
    gTrace.push_back(1);
    gTrace.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self) - gBase));
    return ++*reinterpret_cast<uint32_t *>(gBase + 4);
}

uint32_t __fastcall FakeGetError(void *self, void *)
{
    Note(18, self);
    return (gNoise & 0xffffff00u) | *reinterpret_cast<uint8_t *>(gBase + 0x30);
}

uint32_t __fastcall FakeDelete(void *self, void *, uint32_t flags)
{
    Note(21, self);
    gTrace.push_back(flags);
    return gNoise;
}

uint32_t __fastcall FakeSetError(void *self, void *, uint32_t flag)
{
    Note(24, self);
    gTrace.push_back(flag & 0xff);
    *reinterpret_cast<uint8_t *>(gBase + 0x30) = static_cast<uint8_t>(flag);
    return gNoise;
}

uint32_t __fastcall FakeOwnerRelease(void *self, void *)
{
    Note(2, self);
    // The base destructors set their vtables before releasing: record what the object looks like now.
    uint32_t vt0, vt1;
    std::memcpy(&vt0, reinterpret_cast<void *>(gBase), 4);
    std::memcpy(&vt1, reinterpret_cast<void *>(gBase + 0x10), 4);
    gTrace.push_back(vt0 == Addr32(0xda29bc));
    gTrace.push_back(vt1 == Addr32(0xda2a20));
    return gNoise;
}

// Output stream (Write): slot 9 takes the size, slot 17 the bytes.
uint32_t __fastcall FakeOutSint32(void *self, void *, uint32_t v)
{
    Note(0x109, self);
    gTrace.push_back(v);
    // Some cases change the stream under Write's feet: it must test size and data again afterwards.
    if (gNoise & 0x100) *reinterpret_cast<uint32_t *>(gBase + 0x24) = 0;
    if (gNoise & 0x200) {
        gStash = *reinterpret_cast<uint8_t **>(gBase + 0x34);
        *reinterpret_cast<uint8_t **>(gBase + 0x34) = nullptr;
    }
    return gNoise;
}

uint32_t __fastcall FakeOutVoid(void *self, void *, const uint8_t *p, uint32_t n)
{
    Note(0x117, self);
    gTrace.push_back(p == gData);
    gTrace.push_back(n);
    for (uint32_t i = 0; i < n; ++i) gTrace.push_back(p[i]);
    return gNoise;
}

// Input stream (Read): slot 9 stores the scripted size, slot 17 fills the destination.
uint32_t __fastcall FakeInSint32(void *self, void *, uint32_t *out)
{
    Note(0x209, self);
    gTrace.push_back(out != nullptr);
    if (!gReadFail) *out = gReadSize;
    return gNoise;
}

uint32_t __fastcall FakeInVoid(void *self, void *, uint8_t *dst, uint32_t n)
{
    Note(0x217, self);
    gTrace.push_back(dst == gData);
    gTrace.push_back(n);
    for (uint32_t i = 0; i < n; ++i) dst[i] = static_cast<uint8_t>(i * 7 + 3);
    return gNoise;
}

struct World {
    uint8_t *obj = nullptr; // 0x38 bytes on the heap (operator delete'd by the deleting destructor)
    bool freed = false;
    alignas(8) uint8_t ownerIn[16];
    alignas(8) uint8_t ownerOut[16];
    alignas(8) uint8_t outS[16];
    alignas(8) uint8_t inS[16];
    void *vtP[25];
    void *vtIS[24];
    void *vtSS[14];
    void *vtOwner[4];
    void *vtOut[20];
    void *vtIn[20];
    uint32_t noise = 0;

    ~World()
    {
        if (!freed && obj) {
            auto *data = *reinterpret_cast<uint8_t **>(obj + 0x34);
            delete[] data;
            ::operator delete(obj);
        }
    }
};

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uintptr_t Addr(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

struct Shape {
    uint32_t capMax = 70;
    bool withOwners = true;
    bool allowPosPastSize = false;
    bool dataNullWithSize = false;
};

// Builds a world from a seed: random object bytes, valid buffer state (size <= capacity, data present
// when the capacity is not 0), fake vtables installed.
void Build(World &w, const difftest::Context &ctx, uint32_t seed, const Shape &shape)
{
    std::mt19937 rng(seed);
    w.obj = static_cast<uint8_t *>(::operator new(kObjBytes));
    for (size_t i = 0; i < kObjBytes; ++i) w.obj[i] = static_cast<uint8_t>(rng());
    std::memset(w.ownerIn, 0, sizeof(w.ownerIn));
    std::memset(w.ownerOut, 0, sizeof(w.ownerOut));

    std::memcpy(w.vtP, ctx.image.At(cRZSerializableStream::kVTable), sizeof(w.vtP));
    std::memcpy(w.vtIS, ctx.image.At(cRZSerializableStream::kVTableIStream), sizeof(w.vtIS));
    std::memcpy(w.vtSS, ctx.image.At(cRZSerializableStream::kVTableSerializable), sizeof(w.vtSS));
    w.vtP[1] = w.vtIS[1] = w.vtSS[1] = reinterpret_cast<void *>(&FakeAddRef);
    w.vtP[18] = reinterpret_cast<void *>(&FakeGetError);
    w.vtP[21] = reinterpret_cast<void *>(&FakeDelete);
    w.vtP[24] = reinterpret_cast<void *>(&FakeSetError);
    std::memset(w.vtOwner, 0, sizeof(w.vtOwner));
    w.vtOwner[2] = reinterpret_cast<void *>(&FakeOwnerRelease);
    std::memset(w.vtOut, 0, sizeof(w.vtOut));
    w.vtOut[9] = reinterpret_cast<void *>(&FakeOutSint32);
    w.vtOut[17] = reinterpret_cast<void *>(&FakeOutVoid);
    std::memset(w.vtIn, 0, sizeof(w.vtIn));
    w.vtIn[9] = reinterpret_cast<void *>(&FakeInSint32);
    w.vtIn[17] = reinterpret_cast<void *>(&FakeInVoid);
    Put(w.ownerIn, 0, Addr(w.vtOwner));
    Put(w.ownerOut, 0, Addr(w.vtOwner));
    Put(w.outS, 0, Addr(w.vtOut));
    Put(w.inS, 0, Addr(w.vtIn));

    Put(w.obj, 0x00, Addr(w.vtP));
    Put(w.obj, 0x10, Addr(w.vtIS));
    Put(w.obj, 0x20, Addr(w.vtSS));
    Put(w.obj, 0x04, uint32_t(rng() % 5));
    Put(w.obj, 0x0c, shape.withOwners && rng() % 3 ? Addr(w.ownerOut) : uintptr_t(0));
    Put(w.obj, 0x1c, shape.withOwners && rng() % 3 ? Addr(w.ownerIn) : uintptr_t(0));
    static const uint8_t errs[] = {0, 0, 1, 2, 0x80, 0xff};
    w.obj[0x30] = errs[rng() % 6];

    uint32_t cap = rng() % 4 == 0 ? 0 : rng() % (shape.capMax + 1);
    uint32_t size = rng() % (cap + 1);
    if (rng() % 8 == 0) size = cap;
    uint32_t pos = rng() % (size + 1);
    if (shape.allowPosPastSize && rng() % 4 == 0) pos = size + 1 + rng() % 5;
    Put(w.obj, 0x24, size);
    Put(w.obj, 0x28, cap);
    Put(w.obj, 0x2c, pos);
    uint8_t *data = nullptr;
    if (cap) {
        data = new uint8_t[cap];
        for (uint32_t i = 0; i < cap; ++i) data[i] = static_cast<uint8_t>(rng());
    }
    Put(w.obj, 0x34, Addr(data));
    if (shape.dataNullWithSize) {
        // size and data pointer both inconsistent with the capacity: the Write path checks only these two.
        if (rng() % 2) {
            delete[] data;
            Put(w.obj, 0x34, uintptr_t(0));
            Put(w.obj, 0x24, uint32_t(rng() % 5));
            Put(w.obj, 0x28, uint32_t(0));
            Put(w.obj, 0x2c, uint32_t(0));
        }
    }
    gNoise = w.noise = static_cast<uint32_t>(rng());
}

struct Outcome {
    uint64_t ret = 0;
    std::vector<uint32_t> trace;
    std::vector<uint8_t> object;
    std::vector<uint8_t> data;
    bool hadData = false;
    bool deleted = false;
};

template <typename Call> Outcome Run(World &w, Call call)
{
    gBase = Addr(w.obj);
    gData = *reinterpret_cast<uint8_t **>(w.obj + 0x34);
    gNoise = w.noise;
    gIds = {{w.obj, 4},      {w.obj + 0x10, 5}, {w.obj + 0x20, 6}, {w.ownerIn, 7},
            {w.ownerOut, 8}, {w.outS, 9},       {w.inS, 10}};
    gTrace.clear();
    Outcome out;
    out.ret = call(w);
    out.trace = gTrace;
    if (!w.freed) {
        out.object.assign(w.obj, w.obj + kObjBytes);
        uint8_t *data = *reinterpret_cast<uint8_t **>(w.obj + 0x34);
        out.hadData = data != nullptr;
        // The data pointer differs between worlds: keep only whether it is set.
        std::memset(out.object.data() + 0x34, 0, 4);
        // The fake vtables are per-world copies: a pointer to one becomes a marker, anything else
        // (the game's vtables, after a constructor or destructor) is compared as it is.
        const void *fakes[3] = {w.vtP, w.vtIS, w.vtSS};
        for (int k = 0; k < 3; ++k) {
            size_t off = size_t(k) * 0x10;
            uintptr_t p;
            std::memcpy(&p, out.object.data() + off, sizeof(p));
            if (p == Addr(fakes[k])) {
                uint32_t marker = 0xfa4e0000u + k;
                std::memcpy(out.object.data() + off, &marker, 4);
            }
        }
        // The owner pointers are per-world, too.
        for (size_t off : {size_t(0x0c), size_t(0x1c)}) {
            uintptr_t p;
            std::memcpy(&p, out.object.data() + off, sizeof(p));
            uint32_t id = IdOf(reinterpret_cast<void *>(p));
            std::memcpy(out.object.data() + off, &id, 4);
        }
        if (data) out.data.assign(data, data + *reinterpret_cast<uint32_t *>(w.obj + 0x28));
    } else {
        out.deleted = true;
    }
    return out;
}

template <typename OrigCall, typename OursCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, const Shape &shape, OrigCall origCall, OursCall oursCall)
{
    World a, b;
    Build(a, ctx, seed, shape);
    Build(b, ctx, seed, shape);
    gStash = nullptr;
    Outcome oa = Run(a, origCall);
    if (gStash) Put(a.obj, 0x34, Addr(gStash));
    gStash = nullptr;
    Outcome ob = Run(b, oursCall);
    if (gStash) Put(b.obj, 0x34, Addr(gStash));
    gStash = nullptr;
    ++ctx.cases;
    if (oa.ret != ob.ret) ctx.Fail("%s seed %u: original returned 0x%llx, ours 0x%llx", name, seed, (unsigned long long)oa.ret, (unsigned long long)ob.ret);
    else if (oa.trace != ob.trace) ctx.Fail("%s seed %u: recorded calls differ (%zu vs %zu words)", name, seed, oa.trace.size(), ob.trace.size());
    else if (oa.deleted != ob.deleted) ctx.Fail("%s seed %u: deletion differs", name, seed);
    else if (oa.object != ob.object) {
        size_t i = 0;
        while (i < oa.object.size() && oa.object[i] == ob.object[i]) ++i;
        ctx.Fail("%s seed %u: object bytes differ first at +0x%zx (original 0x%02x, ours 0x%02x)", name, seed, i, oa.object[i], ob.object[i]);
    } else if (oa.hadData != ob.hadData || oa.data != ob.data) ctx.Fail("%s seed %u: buffer contents differ", name, seed);
    if (oa.deleted) a.freed = true;
    if (ob.deleted) b.freed = true;
}

cRZSerializableStream *Ours(World &w)
{
    return reinterpret_cast<cRZSerializableStream *>(w.obj);
}
cRZSerializableStreamIS *OursIS(World &w)
{
    return reinterpret_cast<cRZSerializableStreamIS *>(w.obj + 0x10);
}
cRZSerializableStreamSS *OursSS(World &w)
{
    return reinterpret_cast<cRZSerializableStreamSS *>(w.obj + 0x20);
}

// Interesting values for sizes and counts around a buffer.
uint32_t PickCount(std::mt19937 &rng, const World &w)
{
    uint32_t cap = *reinterpret_cast<const uint32_t *>(w.obj + 0x28);
    uint32_t size = *reinterpret_cast<const uint32_t *>(w.obj + 0x24);
    uint32_t n;
    switch (rng() % 8) {
    case 0: n = 0; break;
    case 1: n = cap - size; break;
    case 2: n = cap - size + 1; break;
    case 3: n = cap - size - 1; break;
    case 4: n = cap; break;
    case 5: n = cap + 1; break;
    default: n = rng() % 100; break;
    }
    return n > 1000 ? rng() % 50 : n; // cap == size makes the "one less" case wrap: never allocate 4 GB
}

constexpr int kCases = 3000;

} // namespace

DIFF_TEST(cRZSerializableStream_Construct, 0x9cbcf3)
{
    auto orig = ctx.Original<Th1>(0x9cbcf3);
    std::mt19937 rng(1);
    for (int i = 0; i < kCases; ++i) {
        uint32_t order = i < 4 ? uint32_t(i) * 0x1000000u + (i & 1) : uint32_t(rng());
        Shape shape;
        shape.capMax = 0;
        // The original's initial bytes are garbage: the constructor must overwrite every field.
        Compare(ctx, "Construct", i, shape,
                [&](World &w) {
                    gTrace.clear();
                    uint32_t r = orig(w.obj, order);
                    return uint64_t(r - Addr(w.obj));
                },
                [&](World &w) { return uint64_t(Addr(Ours(w)->Construct(order)) - Addr(w.obj)); });
    }
}

DIFF_TEST(cRZSerializableStream_Destruct, 0x9cbd43)
{
    auto orig = ctx.Original<Th0>(0x9cbd43);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        Compare(ctx, "Destruct", i, shape, [&](World &w) { orig(w.obj); return uint64_t(0); },
                [&](World &w) { Ours(w)->Destruct(); return uint64_t(0); });
    }
}

DIFF_TEST(cRZSerializableStream_DeletingDestructor, 0x9cbd79)
{
    auto orig = ctx.Original<Th1>(0x9cbd79);
    auto origIS = ctx.Original<Th1>(0x9cbd71);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        uint32_t flags = i % 4 == 0 ? 0 : i % 4 == 1 ? 1 : i % 4 == 2 ? 2 : 0xfffffffd;
        Compare(ctx, "DeletingDestructor", i, shape,
                [&](World &w) {
                    uint32_t r = orig(w.obj, flags);
                    if (flags & 1) w.freed = true;
                    return uint64_t(r == Addr(w.obj));
                },
                [&](World &w) {
                    cRZSerializableStream *r = Ours(w)->DeletingDestructor(flags);
                    if (flags & 1) w.freed = true;
                    return uint64_t(Addr(r) == Addr(w.obj));
                });
        // The input subobject's thunk (slot 21 of its vtable).
        Compare(ctx, "DeletingDestructorIS", i, shape,
                [&](World &w) {
                    uint32_t r = origIS(w.obj + 0x10, flags);
                    if (flags & 1) w.freed = true;
                    return uint64_t(r == Addr(w.obj));
                },
                [&](World &w) {
                    cRZSerializableStream *r = OursIS(w)->vf16_21(flags);
                    if (flags & 1) w.freed = true;
                    return uint64_t(Addr(r) == Addr(w.obj));
                });
    }
}

DIFF_TEST(cRZSerializableStream_QueryInterface, 0x9cbe22)
{
    auto orig = ctx.Original<ThQI>(0x9cbe22);
    auto origIS = ctx.Original<ThQI>(0x9cbe75);
    auto origSS = ctx.Original<ThQI>(0x9cbe7d);
    const uint32_t riids[] = {0x853d26deu, 0xe4fda3d4u, 1u, 0xc51b11cbu, 0x051b11bdu, 0u, 0x853d26dfu, 0xe4fda3d3u};
    std::mt19937 rng(2);
    for (int i = 0; i < kCases; ++i) {
        uint32_t riid = i < 8 ? riids[i] : i % 3 == 0 ? riids[rng() % 8] : uint32_t(rng());
        Shape shape;
        auto run = [&](const char *name, int which) {
            uint32_t outA[2] = {0xdeadbeefu, 0x5a5a5a5au}, outB[2] = {0xdeadbeefu, 0x5a5a5a5au};
            uint32_t markA = 0, markB = 0;
            Compare(ctx, name, i, shape,
                    [&](World &w) {
                        void *self = w.obj + (which == 0 ? 0 : which == 1 ? 0x10 : 0x20);
                        uint32_t r = (which == 0 ? orig : which == 1 ? origIS : origSS)(self, riid, reinterpret_cast<void **>(outA));
                        markA = outA[0] == 0xdeadbeefu ? 1u : outA[0] == 0 ? 2u : static_cast<uint32_t>(outA[0] - Addr(w.obj));
                        return uint64_t(r & 0xff);
                    },
                    [&](World &w) {
                        void *self = w.obj + (which == 0 ? 0 : which == 1 ? 0x10 : 0x20);
                        bool r = which == 0 ? Ours(w)->QueryInterface(riid, reinterpret_cast<void **>(outB))
                                 : which == 1 ? reinterpret_cast<cRZSerializableStreamIS *>(self)->QueryInterface(riid, reinterpret_cast<void **>(outB))
                                              : reinterpret_cast<cRZSerializableStreamSS *>(self)->QueryInterface(riid, reinterpret_cast<void **>(outB));
                        markB = outB[0] == 0xdeadbeefu ? 1u : outB[0] == 0 ? 2u : static_cast<uint32_t>(outB[0] - Addr(w.obj));
                        return uint64_t(r);
                    });
            ++ctx.cases;
            if (markA != markB || outA[1] != outB[1]) ctx.Fail("%s riid 0x%08x: out original 0x%x, ours 0x%x", name, riid, markA, markB);
        };
        run("QueryInterface", 0);
        run("QueryInterfaceIS", 1);
        run("QueryInterfaceSS", 2);
    }
}

DIFF_TEST(cRZSerializableStream_Release, 0x9cbed8)
{
    auto orig = ctx.Original<Th0>(0x9cbed8);
    auto origIS = ctx.Original<Th0>(0x9cbedd);
    auto origSS = ctx.Original<Th0>(0x9cbee5);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        // Counts around the delete threshold come from the seed; force edge values on some cases.
        auto prep = [i](World &w) {
            static const uint32_t counts[] = {0, 1, 2, 3, 0xffffffffu, 0x80000000u};
            if (i % 2 == 0) Put(w.obj, 4, counts[(i / 2) % 6]);
        };
        auto compare = [&](const char *name, int which) {
            Compare(ctx, name, i, shape,
                    [&](World &w) {
                        prep(w);
                        void *self = w.obj + (which == 0 ? 0 : which == 1 ? 0x10 : 0x20);
                        return uint64_t((which == 0 ? orig : which == 1 ? origIS : origSS)(self));
                    },
                    [&](World &w) {
                        prep(w);
                        return uint64_t(which == 0 ? Ours(w)->Release() : which == 1 ? OursIS(w)->Release() : OursSS(w)->Release());
                    });
        };
        compare("Release", 0);
        compare("ReleaseIS", 1);
        compare("ReleaseSS", 2);
    }
}

DIFF_TEST(cRZSerializableStream_SetVoid, 0x9cbeed)
{
    auto orig = ctx.Original<Th2P>(0x9cbeed);
    std::mt19937 rng(3);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.capMax = 300;
        std::vector<uint8_t> src(1001);
        for (auto &b : src) b = static_cast<uint8_t>(rng());
        // Peek the count the same way on both sides: it depends only on the seed's world.
        World probe;
        Build(probe, ctx, i, shape);
        uint32_t n = PickCount(rng, probe);
        bool nullSrc = i % 11 == 0;
        probe.freed = false;
        const void *s = nullSrc ? nullptr : src.data();
        Compare(ctx, "SetVoid", i, shape,
                [&](World &w) { return uint64_t(orig(w.obj, s, n) & 0xff); },
                [&](World &w) { return uint64_t(Ours(w)->SetVoid(s, n)); });
    }
}

DIFF_TEST(cRZSerializableStream_SetByteOrder, 0x9cbdee)
{
    auto orig = ctx.Original<Th1>(0x9cbdee);
    auto origIS = ctx.Original<Th1>(0x9cbe12);
    auto origSS = ctx.Original<Th1>(0x9cbe1a);
    std::mt19937 rng(4);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        uint32_t order = i % 5 == 0 ? 0 : i % 5 == 1 ? 1 : uint32_t(rng());
        Compare(ctx, "SetByteOrder", i, shape, [&](World &w) { return uint64_t(orig(w.obj, order) & 0xff); },
                [&](World &w) { return uint64_t(Ours(w)->SetByteOrder(order)); });
        Compare(ctx, "SetByteOrderIS", i, shape, [&](World &w) { return uint64_t(origIS(w.obj + 0x10, order) & 0xff); },
                [&](World &w) { return uint64_t(OursIS(w)->vf16_22(order)); });
        Compare(ctx, "SetByteOrderSS", i, shape, [&](World &w) { return uint64_t(origSS(w.obj + 0x20, order) & 0xff); },
                [&](World &w) { return uint64_t(OursSS(w)->vf32_8(order)); });
    }
}

DIFF_TEST(cRZSerializableStream_Reset, 0x9cbf2f)
{
    auto orig = ctx.Original<Th0>(0x9cbf2f);
    auto origIS = ctx.Original<Th0>(0x9cbf61);
    auto origSS = ctx.Original<Th0>(0x9cbf69);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        Compare(ctx, "Reset", i, shape, [&](World &w) { return uint64_t(orig(w.obj) & 0xff); },
                [&](World &w) { return uint64_t(Ours(w)->Reset()); });
        Compare(ctx, "ResetIS", i, shape, [&](World &w) { return uint64_t(origIS(w.obj + 0x10) & 0xff); },
                [&](World &w) { return uint64_t(OursIS(w)->vf16_23()); });
        Compare(ctx, "ResetSS", i, shape, [&](World &w) { return uint64_t(origSS(w.obj + 0x20) & 0xff); },
                [&](World &w) { return uint64_t(OursSS(w)->vf32_9()); });
    }
}

DIFF_TEST(cRZSerializableStream_Remaining, 0x9cbff6)
{
    auto orig = ctx.Original<Th0>(0x9cbff6);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.allowPosPastSize = true;
        Compare(ctx, "Remaining", i, shape, [&](World &w) { return uint64_t(orig(w.obj)); },
                [&](World &w) { return uint64_t(Ours(w)->Remaining()); });
    }
    // Unsigned wrap-around with extreme values.
    for (uint32_t size : {0u, 1u, 0xffffffffu, 0x80000000u}) {
        for (uint32_t pos : {0u, 1u, 0xffffffffu, 0x7fffffffu}) {
            World a;
            Build(a, ctx, 1, Shape());
            Put(a.obj, 0x24, size);
            Put(a.obj, 0x2c, pos);
            uint32_t want = orig(a.obj), got = Ours(a)->Remaining();
            ++ctx.cases;
            if (want != got) ctx.Fail("Remaining(size 0x%x, pos 0x%x): original 0x%x, ours 0x%x", size, pos, want, got);
            // Keep the destructor from freeing a data pointer the test never owned.
            Put(a.obj, 0x34, uintptr_t(0));
        }
    }
}

DIFF_TEST(cRZSerializableStream_Reserve, 0x9cbffd)
{
    auto orig = ctx.Original<Th1>(0x9cbffd);
    std::mt19937 rng(5);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.capMax = 200;
        World probe;
        Build(probe, ctx, i, shape);
        uint32_t cap = *reinterpret_cast<uint32_t *>(probe.obj + 0x28);
        uint32_t n = i % 4 == 0 ? cap : i % 4 == 1 ? cap + 1 : i % 4 == 2 ? cap - 1 : rng() % 300;
        if (cap == 0 && i % 4 == 2) n = 0;
        Compare(ctx, "Reserve", i, shape, [&](World &w) { return uint64_t(orig(w.obj, n) & 0xff); },
                [&](World &w) { return uint64_t(Ours(w)->Reserve(n)); });
    }
}

DIFF_TEST(cRZSerializableStream_Grow, 0x9cc050)
{
    auto orig = ctx.Original<Th1>(0x9cc050);
    std::mt19937 rng(6);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.capMax = 200;
        World probe;
        Build(probe, ctx, i, shape);
        uint32_t n = PickCount(rng, probe);
        Compare(ctx, "Grow", i, shape, [&](World &w) { return uint64_t(orig(w.obj, n) & 0xff); },
                [&](World &w) { return uint64_t(Ours(w)->Grow(n)); });
    }
}

DIFF_TEST(cRZSerializableStream_Skip, 0x9cbf71)
{
    auto orig = ctx.Original<Th1>(0x9cbf71);
    std::mt19937 rng(7);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.allowPosPastSize = true;
        World probe;
        Build(probe, ctx, i, shape);
        uint32_t size = *reinterpret_cast<uint32_t *>(probe.obj + 0x24);
        uint32_t pos = *reinterpret_cast<uint32_t *>(probe.obj + 0x2c);
        uint32_t left = size - pos;
        uint32_t n = i % 5 == 0 ? left : i % 5 == 1 ? left + 1 : i % 5 == 2 ? 0 : i % 5 == 3 ? uint32_t(rng()) : left - 1;
        Compare(ctx, "Skip", i, shape, [&](World &w) { return uint64_t(orig(w.obj + 0x10, n) & 0xff); },
                [&](World &w) { return uint64_t(OursIS(w)->Skip(n)); });
    }
}

DIFF_TEST(cRZSerializableStream_GetVoid, 0x9cbda0)
{
    auto orig = ctx.Original<Th2P>(0x9cbda0);
    std::mt19937 rng(8);
    for (int i = 0; i < kCases; ++i) {
        Shape shape; // position never passes the size here: the copy would read outside the buffer
        World probe;
        Build(probe, ctx, i, shape);
        uint32_t size = *reinterpret_cast<uint32_t *>(probe.obj + 0x24);
        uint32_t pos = *reinterpret_cast<uint32_t *>(probe.obj + 0x2c);
        uint32_t left = size - pos;
        uint32_t n = i % 5 == 0 ? left : i % 5 == 1 ? left + 1 : i % 5 == 2 ? 0 : i % 5 == 3 ? uint32_t(rng() % 120) : left - 1;
        uint8_t bufA[130], bufB[130];
        std::memset(bufA, 0xA5, sizeof(bufA));
        std::memset(bufB, 0xA5, sizeof(bufB));
        uint32_t m = n > 128 ? 128 : n;
        n = m;
        std::vector<uint8_t> gotA, gotB;
        Compare(ctx, "GetVoid", i, shape,
                [&](World &w) {
                    uint64_t r = orig(w.obj + 0x10, bufA, n) & 0xff;
                    gotA.assign(bufA, bufA + sizeof(bufA));
                    return r;
                },
                [&](World &w) {
                    uint64_t r = OursIS(w)->GetVoid(bufB, n);
                    gotB.assign(bufB, bufB + sizeof(bufB));
                    return r;
                });
        ++ctx.cases;
        if (gotA != gotB) ctx.Fail("GetVoid seed %d: destination bytes differ", i);
    }
}

DIFF_TEST(cRZSerializableStream_Write, 0x9cbfab)
{
    auto orig = ctx.Original<Th1P>(0x9cbfab);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.dataNullWithSize = true;
        Compare(ctx, "Write", i, shape, [&](World &w) { return uint64_t(orig(w.obj + 0x20, w.outS) & 0xff); },
                [&](World &w) { return uint64_t(OursSS(w)->Write(w.outS)); });
    }
}

DIFF_TEST(cRZSerializableStream_Read, 0x9cbe85)
{
    auto orig = ctx.Original<Th1P>(0x9cbe85);
    std::mt19937 rng(9);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.capMax = 100;
        World probe;
        Build(probe, ctx, i, shape);
        uint32_t cap = *reinterpret_cast<uint32_t *>(probe.obj + 0x28);
        uint32_t size = i % 5 == 0 ? cap : i % 5 == 1 ? cap + 1 : i % 5 == 2 ? 0 : i % 5 == 3 ? cap - 1 : rng() % 200;
        if (cap == 0 && i % 5 == 3) size = 0;
        gReadFail = i % 7 == 0; // the size getter writes nothing: the local stays 0
        uint32_t scripted = size;
        Compare(ctx, "Read", i, shape,
                [&](World &w) {
                    gReadSize = scripted;
                    return uint64_t(orig(w.obj + 0x20, w.inS) & 0xff);
                },
                [&](World &w) {
                    gReadSize = scripted;
                    return uint64_t(OursSS(w)->Read(w.inS));
                });
    }
    gReadFail = false;
}
