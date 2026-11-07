// Differential tests for the translated and oriented cuboids, cConvexT<cITranslatedCuboidShape> and
// cConvexT<cIOrientedCuboidShape> (lane PP, src/compgeom/ConvexShapes.h, ConvexCuboid.cpp, ConvexBoxes.cpp).
// - CompGeomConvexCuboid_TranslatedSlots / _OrientedSlots: shapes with random data, then random slots through each
//   object's own vtable. Three sides: the game's factory object (the game's slots); our Create's object (our overrides,
//   through our vtable); and the game's object with each slot that has a hook run through the hook's replacement (the
//   embedded light shapes' case, src/sg/SGLightShapes.h). The cases' results, out parameters and every call the slots
//   make on the fake bound and linear (frames built by the slot by value) are logged and compared with the game's.
//   The helpers the slots call run for real on each side. Clone's copies are dumped and released; at the end the shape
//   is released.
// - CompGeomConvexCuboid_Helpers: the four box helpers directly, with NaNs: the frame's axis scales (0x19cf80), the
//   cuboid's support corner (0x191ea0), the fifteen-axis test (0x19e070) and the two-box overlap (0x19dcb0).
// All run at the harness's 53-bit x87 precision and at the game's 24-bit.
// Verified RVAs, translated cuboid slots (all three sides): 0x190f10 0x190f60 0x191120 0x190f80 0x191030 0x191580
// 0x191820 0x191700 0x191610 0x1918a0 0x191480 0x190ef0 0x191380 0x1912a0 0x191500 0x191360 0x191340 0x191320 0x1916e0
// 0x1916c0 0x1916a0 0x192030 0x192010 0x191ff0 0x192090 0x192070 0x192050 0x18fe90 0x18ff00 0x18ff70 0x191c60 0x191d10
// 0x191e10; the factory 0x190450.
// Oriented cuboid slots: 0x1904e0 0x190550 0x190750 0x190590 0x190650 0x190bf0 0x190e30 0x190c60 0x190d50 0x190ca0
// 0x190e80 0x190ae0 0x1904a0 0x1909c0 0x1908e0 0x190b70 0x1909a0 0x190980 0x190960 0x190d30 0x190d10 0x190cf0 0x191f30
// 0x191f10 0x191ef0 0x191fb0 0x191f90 0x191f70 0x18fd80 0x18fe00 0x18fe70 0x1919f0 0x191ac0 0x191bb0; the factory
// 0x190420. Helpers: 0x19cf80 0x191ea0 0x19e070 0x19dcb0.
// Wrapper instantiations their slots call (in effect through callers, run inside the slot cases): translated 0x19ed10
// 0x19eca0 0x19ec30 0x19ec10 0x19ebf0 0x19ebd0 0x19f5e0 0x19f570 0x19f500 0x19f4e0 0x19f4c0 0x19f4a0; oriented 0x19eb50
// 0x19ead0 0x19ea50 0x19ea30 0x19ea10 0x19e9f0 0x19f420 0x19f3a0 0x19f320 0x19f300 0x19f2e0 0x19f2c0.

#include "compgeom/ConvexShapes.h"
#include "compgeom/TriangleSet.h"
#include "difftest.h"
#include "hook/hooks.h"

#include "code_patch.h"
#include "shape.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <vector>

#include <windows.h>

using namespace nGZCompGeom;

namespace {

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float Float(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}
uint32_t Word(const void *p, uint32_t offset)
{
    return shape::Word(p, offset);
}

// ---- the event log, one per side ----

struct Block {
    uint32_t p, size;
};
struct SideLog {
    std::vector<uint32_t> words;
    std::vector<Block> blocks;
    bool overflow = false;
    void Reset()
    {
        if (words.capacity() == 0) words.reserve(1u << 21);
        if (blocks.capacity() == 0) blocks.reserve(1u << 12);
        words.clear();
        blocks.clear();
        overflow = false;
    }
};
constexpr unsigned kSides = 3; // the game's slots, ours, the game's object through the hooks
SideLog gSides[kSides];
SideLog *gCur;
DWORD gMainThread;

void Log(uint32_t w)
{
    if (!gCur) return;
    if (gCur->words.size() < gCur->words.capacity()) gCur->words.push_back(w);
    else gCur->overflow = true;
}
bool Logging()
{
    return gCur && GetCurrentThreadId() == gMainThread;
}

uint32_t BlockCode(uint32_t w)
{
    if (!gCur || !w) return w;
    const std::vector<Block> &b = gCur->blocks;
    for (size_t i = b.size(); i-- > 0;)
        if (w >= b[i].p && w - b[i].p < (b[i].size ? b[i].size : 1) && w - b[i].p < 0x10000)
            return 0xc0000000u | uint32_t(i) << 16 | (w - b[i].p);
    return w;
}

void *NewBlock(uint32_t tag, size_t n)
{
    uint8_t *p = static_cast<uint8_t *>(std::malloc(n ? n : 1));
    if (Logging()) {
        SideLog &s = *gCur;
        for (size_t i = 0; i < n; ++i) p[i] = uint8_t(0x40 | (i & 0xf));
        Log(tag | uint32_t(n));
        if (s.blocks.size() < s.blocks.capacity()) s.blocks.push_back({shape::P(p), uint32_t(n)});
        else s.overflow = true;
    }
    return p;
}
void FreeBlock(uint32_t tag, void *p)
{
    if (Logging()) {
        Log(tag);
        Log(BlockCode(shape::P(p)));
    }
    std::free(p);
}

void *__cdecl HookNew(size_t n) { return NewBlock(0xa2000000u, n); }
void __cdecl HookDelete(void *p)
{
    if (p) FreeBlock(0xf2000000u, p);
}
void __cdecl HookSizedDelete(void *p, size_t) { HookDelete(p); }

// The allocator service the triangle set's vectors use (cache 0x10c95d4, guard 0x10c95d8 as after its first use): slot 3
// allocates, slot 4 frees. Not logged: the set is built outside the log.
uint32_t __fastcall ServiceNop(void *, void *) { return 0; }
void *__fastcall ServiceAlloc(void *, void *, uint32_t n) { return std::malloc(n ? n : 1); }
void __fastcall ServiceFree(void *, void *, void *p) { std::free(p); }
void *gServiceVTable[5] = {reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop),
                           reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceAlloc),
                           reinterpret_cast<void *>(&ServiceFree)};
void *gServiceObject = gServiceVTable;

struct Fixture {
    void **cache;
    int32_t *guard;
    void *savedCache;
    int32_t savedGuard;
    shape::CodePatch gameNew, gameDelete, ourNew, ourDelete, ourSizedDelete;
    explicit Fixture(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int32_t>(0x10c95d8)), savedCache(*cache),
          savedGuard(*guard), gameNew(0x824632, reinterpret_cast<void *>(&HookNew)), gameDelete(0x824624, reinterpret_cast<void *>(&HookDelete)),
          ourNew(reinterpret_cast<void *>(static_cast<void *(__cdecl *)(size_t)>(&::operator new)),
                 reinterpret_cast<void *>(&HookNew)),
          ourDelete(reinterpret_cast<void *>(static_cast<void(__cdecl *)(void *)>(&::operator delete)),
                    reinterpret_cast<void *>(&HookDelete)),
          ourSizedDelete(reinterpret_cast<void *>(static_cast<void(__cdecl *)(void *, size_t)>(&::operator delete)),
                         reinterpret_cast<void *>(&HookSizedDelete))
    {
        *cache = &gServiceObject;
        *guard = INT32_MIN;
        gMainThread = GetCurrentThreadId();
        for (SideLog &s : gSides) s.Reset();
    }
    ~Fixture()
    {
        gCur = nullptr;
        *cache = savedCache;
        *guard = savedGuard;
    }
};

const HookSpec *HookAt(void *address, difftest::Context &ctx)
{
    for (const HookSpec *h : RegisteredHooks())
        if (ctx.image.At(h->rva) == address) return h;
    return nullptr;
}
template <class Fn> bool PickOurs(difftest::Context &ctx, uint32_t rva, Fn &fn)
{
    const HookSpec *h = HookAt(ctx.image.At(rva), ctx);
    if (!h) {
        ctx.Fail("no hook at 0x%x", rva);
        return false;
    }
    fn = reinterpret_cast<Fn>(h->replacement);
    return true;
}

// ---- the shape under test ----

// The two shapes under test, one descriptor each.
struct Desc {
    const char *name;
    uint32_t create;
    uint32_t gameVt[2]; // concrete, bound
    uint32_t objectBytes, countOffset, dataBytes;
    uint32_t classId;
    bool oriented;
};
const Desc kTranslated = {"translated cuboid", 0x190450, {0xda1640, 0xda1584}, 0x20, 0x1c, 0x18, 0x3ce553fa, false};
const Desc kOriented = {"oriented cuboid", 0x190420, {0xda17b8, 0xda16fc}, 0x30, 0x2c, 0x28, 0x7ce6d2e1, true};
const Desc *gDesc;

void **gOurVt[2]; // the concrete shape, the bound
template <class Shape, class Bound> void ReadVtables()
{
    alignas(8) static uint8_t buffer[0x40];
    auto *s = ::new (buffer) Shape;
    gOurVt[0] = *reinterpret_cast<void ***>(s);
    s->~Shape();
    auto *b = ::new (buffer) Bound;
    gOurVt[1] = *reinterpret_cast<void ***>(b);
    b->~Bound();
}
void ReadOurVtables()
{
    if (gDesc->oriented) ReadVtables<cOrientedCuboidShape, cOrientedCuboidBound>();
    else ReadVtables<cTranslatedCuboidShape, cTranslatedCuboidBound>();
}
uint32_t VtCode(uint32_t w)
{
    for (unsigned i = 0; i < 2; ++i)
        if (w == shape::P(shape::GameVTable(gDesc->gameVt[i])) || w == shape::P(gOurVt[i])) return 0xd0000000u | i;
    return w;
}

void DumpBytes(const uint8_t *p, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, p + i, bytes - i < 4 ? bytes - i : 4);
        Log(w);
    }
}
void DumpShape(const void *object)
{
    const uint8_t *o = static_cast<const uint8_t *>(object);
    Log(0x5ea00000u);
    Log(VtCode(Word(o, 0)));
    DumpBytes(o + 4, gDesc->dataBytes);
    Log(Word(o, gDesc->countOffset));
}

// The inputs the slots get (every pointer argument points in here, the same address on all sides).
struct Inputs {
    AffineTransform3 frame1, frame2;
    TranslatedSphere sphere;      // the other sphere
    TranslatedCuboid translated;  // the other cuboids
    OrientedCuboid oriented;
    float linear[8];              // a segment, ray or line: six floats
    float rigid[7];               // translation, quaternion
    float direction[3];
    uint8_t opaque[0x40];         // cones, a triangle set: only passed on (read by the cone slots' dead code)
    float t;
    BoundBox out;
    Vec3 outVec;
    uint32_t pad[2];
    void *outVar;
};
Inputs gIn;
void *gObject; // the shape under test (the current side's)
uint32_t gRet; // what the stand-ins return

void LogArg(uint32_t w)
{
    const uint32_t o = shape::P(gObject), in = shape::P(&gIn);
    if (w >= o && w < o + gDesc->objectBytes) {
        Log(0xe0000000u | (w - o));
    } else if (w >= in && w < in + sizeof(gIn)) {
        Log(w - in);
    } else if (BlockCode(w) != w) {
        Log(BlockCode(w));
    } else {
        Log(0xee000000u);
        DumpBytes(reinterpret_cast<const uint8_t *>(uintptr_t(w)), sizeof(AffineTransform3));
    }
}

// ---- the fake bound (cIBound) and linear (cILinear3): the slots a sphere calls log, the rest trap ----

uint32_t TrapHit(void *, unsigned words)
{
    Log(0xdead0000u | words);
    return 0;
}
template <unsigned Tag, unsigned S, unsigned... I> uint32_t __fastcall FakeSlot(void *self, void *, shape::StackWord<I>... w)
{
    Log(Tag | S);
    Log(shape::P(self) - shape::P(&gIn));
    (LogArg(w), ...);
    return gRet;
}
template <unsigned Tag, unsigned S, unsigned... I> void *FakeSlotAddress(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<void *>(&FakeSlot<Tag, S, I...>);
}
struct Fake {
    void **vt;
};
void *gBoundVt[46], *gLinearVt[46];
Fake gBound{gBoundVt}, gLinear{gLinearVt};
void FillFakes()
{
    for (unsigned s = 0; s < 46; ++s) gBoundVt[s] = gLinearVt[s] = shape::TrapFor<&TrapHit>(2);
    gBoundVt[14] = FakeSlotAddress<0xb0000000u, 14>(std::make_integer_sequence<unsigned, 2>());
    gBoundVt[19] = FakeSlotAddress<0xb0000000u, 19>(std::make_integer_sequence<unsigned, 3>());
    gLinearVt[13] = FakeSlotAddress<0xb1000000u, 13>(std::make_integer_sequence<unsigned, 1>());
    gLinearVt[18] = FakeSlotAddress<0xb1000000u, 18>(std::make_integer_sequence<unsigned, 2>());
    gLinearVt[24] = FakeSlotAddress<0xb1000000u, 24>(std::make_integer_sequence<unsigned, 2>());
    gLinearVt[29] = FakeSlotAddress<0xb1000000u, 29>(std::make_integer_sequence<unsigned, 3>());
}

// ---- inputs ----

constexpr uint32_t kEdges[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x00000001, 0x80000001, 0x007fffff, 0x00800000, 0x7f7fffff,
    0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001, 0x7fa00000, 0x3f000000, 0x40400000, 0x34000000,
};
int gNaNBudget;

float Num(std::mt19937 &rng)
{
    float f;
    switch (rng() % 12) {
    case 0:
    case 1:
    case 2: f = float(int(rng() % 9) - 4); break;
    case 3:
    case 4:
    case 5:
    case 6: f = float(int(rng() % 20001) - 10000) / 100.0f; break;
    case 7: f = Float((rng() & 0x80000000u) | ((96 + rng() % 64) << 23) | (rng() & 0x7fffff)); break;
    case 8: f = Float(kEdges[rng() % std::size(kEdges)]); break;
    case 9: f = Float(rng()); break;
    case 10: f = Float(rng() & 0x807fffffu); break;
    default: f = float(int(rng() % 5)) * 0.5f; break;
    }
    if (std::isnan(f)) {
        if (gNaNBudget <= 0) return 1.5f;
        --gNaNBudget;
        f = Float((rng() & 0x80000000u) | 0x7f800000u | (rng() & 0x7fffffu) | 1u); // any payload, signalling or quiet
    }
    return f;
}
// A positive-ish extent or radius most of the time.
float Extent(std::mt19937 &rng)
{
    return rng() % 4 ? std::fabs(Num(rng)) : Num(rng);
}
void RandomFrame(AffineTransform3 &f, std::mt19937 &rng)
{
    static const uint32_t kFlags[] = {0, 1, 2, 4, 6, 8, 0x10, 0x12, 0x14, 0x1e, 0x1f};
    f.mUnknown0 = rng() % 8 ? kFlags[rng() % std::size(kFlags)] : rng();
    for (float &m : f.mMatrix) m = rng() % 3 ? Num(rng) : float(rng() % 2);
    // Rotations stay rotations most of the time (the scale is the first column's length: a scale flag makes it).
    for (float &o : f.mOffset) o = Num(rng);
}

void RandomInputs(std::mt19937 &rng)
{
    gNaNBudget = rng() % 3 == 0 ? (rng() % 4 == 0 ? 2 : 1) : 0;
    RandomFrame(gIn.frame1, rng);
    RandomFrame(gIn.frame2, rng);
    gIn.sphere.radius = Extent(rng);
    for (float *p = &gIn.sphere.centre.x; p != &gIn.sphere.centre.x + 3; ++p) *p = Num(rng);
    for (float &f : reinterpret_cast<float(&)[3]>(gIn.translated.cuboid.half)) f = Extent(rng);
    for (float &f : reinterpret_cast<float(&)[3]>(gIn.translated.position)) f = Num(rng);
    for (float &f : reinterpret_cast<float(&)[3]>(gIn.oriented.cuboid.half)) f = Extent(rng);
    for (float &f : reinterpret_cast<float(&)[3]>(gIn.oriented.frame.translation)) f = Num(rng);
    for (float &f : gIn.oriented.frame.rotation) f = Num(rng);
    for (float &f : gIn.linear) f = Num(rng);
    for (float &f : gIn.rigid) f = Num(rng);
    for (float &f : gIn.direction) f = Num(rng);
    for (uint8_t &b : gIn.opaque) b = uint8_t(rng());
    gIn.t = Num(rng);
    std::memset(&gIn.out, 0x5c, sizeof(gIn.out) + sizeof(gIn.outVec) + sizeof(gIn.pad));
    gIn.outVar = reinterpret_cast<void *>(uintptr_t(0x0bad0bad));
    gRet = rng() % 3 == 0 ? rng() : rng() % 2;
    // The shape's data: the half extents, then the position (translated) or the translation and the quaternion (oriented).
    float data[10];
    for (unsigned i = 0; i < 3; ++i) data[i] = Extent(rng);
    for (unsigned i = 3; i < 6; ++i) data[i] = Num(rng);
    const bool tidy = rng() % 2 == 0;
    for (unsigned i = 6; i < 10; ++i) data[i] = tidy ? float(int(rng() % 5) - 2) * 0.5f : Num(rng);
    // Now and then a half extent and the position (translation) component of the same axis are both NaN, with different
    // payloads: the support point adds them, and which payload survives shows the operand order.
    if (rng() % 6 == 0) {
        const unsigned k = rng() % 3;
        data[k] = Float(0x7fc00000u | (rng() & 0x3fffffu) | 1u);
        data[3 + k] = Float(0xffc00000u | (rng() & 0x3fffffu) | 2u);
    }
    std::memcpy(static_cast<uint8_t *>(gObject) + 4, data, gDesc->dataBytes);
}

// The triangle set the "triangle set" slots get: small, finite, built (outside the log, so the allocations are not
// events) once per seed, the same on every side.
TriangleSet *gSet;
void BuildSet(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 3);
    if (!gSet) gSet = new TriangleSet;
    constexpr unsigned kVertices = 9, kTriangles = 6;
    gSet->Resize(kVertices, kTriangles);
    for (unsigned i = 0; i < kVertices; ++i) {
        const Vec3 v = {float(int(rng() % 2001) - 1000) / 100.0f, float(int(rng() % 2001) - 1000) / 100.0f,
                        float(int(rng() % 2001) - 1000) / 100.0f};
        gSet->SetVertex(uint16_t(i), &v);
    }
    for (unsigned t = 0; t < kTriangles; ++t)
        gSet->SetTriangle(int32_t(t), uint16_t(rng() % kVertices), uint16_t(rng() % kVertices), uint16_t(rng() % kVertices));
    gSet->Build();
}

const uint32_t kIdsTranslated[] = {1, cIShape::kInterfaceId, cIBound::kInterfaceId, cIConvex::kInterfaceId, 0x3ce553fa,
                                   0x7ce6d2e1, 0x7ce57a3c, 0};

// The slot's stack words; the number of them.
unsigned Arguments(unsigned slot, std::mt19937 &rng, uint32_t *w)
{
    const uint32_t f1 = shape::P(&gIn.frame1), f2 = shape::P(&gIn.frame2), sphere = shape::P(&gIn.sphere),
                   translated = shape::P(&gIn.translated), oriented = shape::P(&gIn.oriented),
                   opaque = shape::P(gIn.opaque), lin = shape::P(gIn.linear), t = shape::P(&gIn.t),
                   bound = shape::P(&gBound), linear = shape::P(&gLinear), out = shape::P(&gIn.out),
                   outVec = shape::P(&gIn.outVec), rigid = shape::P(gIn.rigid), dir = shape::P(gIn.direction);
    switch (slot) {
    case 0:
    case 3:
        w[0] = rng() % 8 ? kIdsTranslated[rng() % std::size(kIdsTranslated)] : rng();
        w[1] = rng() % 16 ? shape::P(&gIn.outVar) : 0;
        return 2;
    case 1:
    case 2:
    case 4:
    case 43:
    case 44: return 0;
    case 5:
    case 6: w[0] = f1, w[1] = opaque; return 2;
    case 7: w[0] = f1, w[1] = sphere; return 2;
    case 8: w[0] = f1, w[1] = oriented; return 2;
    case 9: w[0] = f1, w[1] = translated; return 2;
    case 10: w[0] = f1, w[1] = bound; return 2;
    case 11: w[0] = shape::P(gSet), w[1] = f1; return 2;
    case 12: w[0] = opaque, w[1] = f1; return 2;
    case 13: w[0] = sphere, w[1] = f1; return 2;
    case 14: w[0] = translated, w[1] = f1; return 2;
    case 15: w[0] = bound, w[1] = f1; return 2;
    case 16: w[0] = f1, w[1] = shape::P(gSet), w[2] = f2; return 3;
    case 17: w[0] = f1, w[1] = opaque, w[2] = f2; return 3;
    case 18: w[0] = f1, w[1] = sphere, w[2] = f2; return 3;
    case 19: w[0] = f1, w[1] = translated, w[2] = f2; return 3;
    case 20: w[0] = f1, w[1] = bound, w[2] = f2; return 3;
    case 21:
    case 22:
    case 23: w[0] = f1, w[1] = lin; return 2;
    case 24: w[0] = f1, w[1] = linear; return 2;
    case 25:
    case 26:
    case 27: w[0] = lin; return 1;
    case 28: w[0] = linear; return 1;
    case 29:
    case 30:
    case 31: w[0] = t, w[1] = f1, w[2] = lin; return 3;
    case 32: w[0] = t, w[1] = f1, w[2] = linear; return 3;
    case 33:
    case 34:
    case 35: w[0] = t, w[1] = lin; return 2;
    case 36: w[0] = t, w[1] = linear; return 2;
    case 37: w[0] = out, w[1] = f1; return 2;
    case 38: w[0] = out, w[1] = rigid; return 2;
    case 39: w[0] = out; return 1;
    case 40: w[0] = outVec, w[1] = f1, w[2] = dir; return 3;
    case 41: w[0] = outVec, w[1] = rigid, w[2] = dir; return 3;
    default: w[0] = outVec, w[1] = dir; return 2; // 42
    }
}

// How a side calls a slot: through the object's vtable, or (the hooks side) the hook's replacement when there is one.
void *gSlotOverride[46]; // the hooks side: the replacement for the game's body of each slot, or null
bool gHooksSide;

uint32_t CallSlot(void *object, unsigned slot, const uint32_t *w, unsigned n)
{
    void *fn = (*static_cast<void ***>(object))[slot];
    if (gHooksSide && gSlotOverride[slot]) fn = gSlotOverride[slot];
    switch (n) {
    case 0: return reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(object);
    case 1: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(object, w[0]);
    case 2: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(fn)(object, w[0], w[1]);
    default:
        return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(fn)(object, w[0], w[1],
                                                                                                 w[2]);
    }
}

void Op(uint32_t op)
{
    Log(0x0b000000u | op);
}

constexpr unsigned kCasesPerShape = 700;

// The boolean tests against a sphere, cuboid or (translated) shape: those whose answer turns on a comparison of squared
// reaches. Random data almost never lands within an ulp of the comparison, so each side bisects this shape's radius for
// the answer's flip point with its own answers (a side that disagrees anywhere leaves a different log) and probes the
// neighbouring radii.
bool SweepableSlot(unsigned slot)
{
    return slot == 7 || slot == 8 || slot == 9 || slot == 13 || slot == 14 || slot == 18 || slot == 19;
}
// The value to bisect: the other shape's size, or this shape's first half extent.
float *SweepTarget(unsigned slot, bool own)
{
    if (own) return reinterpret_cast<float *>(static_cast<uint8_t *>(gObject) + 4);
    switch (slot) {
    case 7:
    case 13:
    case 18: return &gIn.sphere.radius;
    case 8: return &gIn.oriented.cuboid.half.x;
    default: return &gIn.translated.cuboid.half.x;
    }
}
void SweepRadius(unsigned slot, const uint32_t *w, unsigned n, bool own)
{
    float *radius = SweepTarget(slot, own);
    const float saved = *radius;
    uint32_t lo = 0, hi = Bits(1.0e7f);
    while (hi - lo > 1) {
        const uint32_t mid = lo + (hi - lo) / 2;
        *radius = Float(mid);
        if (CallSlot(gObject, slot, w, n) & 0xff) hi = mid;
        else lo = mid;
    }
    Log(hi);
    for (int k = -3; k <= 3; ++k) {
        *radius = Float(hi + k);
        Log(CallSlot(gObject, slot, w, n) & 0xff);
    }
    *radius = saved;
}

void RunSlots(void *(__cdecl *create)(), uint32_t seed)
{
    std::mt19937 rng(seed);
    gObject = create();
    Op(0);
    CallSlot(gObject, 1, nullptr, 0); // AddRef
    DumpShape(gObject);
    for (unsigned i = 0; i < kCasesPerShape; ++i) {
        RandomInputs(rng);
        const unsigned slot = rng() % 45;
        if (slot == 1 || slot == 2) continue; // the count: at the end
        uint32_t w[3] = {};
        const unsigned n = Arguments(slot, rng, w);
        Op(0x100000u | slot << 12 | (i & 0xfff));
        const uint32_t r = CallSlot(gObject, slot, w, n);
        if (slot == 4 || slot == 43 || slot == 44) {
            Log(slot == 4 ? r : r - shape::P(gObject));
        } else if (slot >= 37) {
            LogArg(r);
            DumpBytes(reinterpret_cast<const uint8_t *>(&gIn.out), 24);
            DumpBytes(reinterpret_cast<const uint8_t *>(&gIn.outVec), 12);
        } else {
            Log(r & 0xff);
        }
        if (SweepableSlot(slot) && rng() % 3 == 0) SweepRadius(slot, w, n, rng() % 2 == 0);
        Log(Bits(gIn.t));
        DumpBytes(reinterpret_cast<const uint8_t *>(&gIn), sizeof(gIn) - sizeof(gIn.outVar));
        DumpShape(gObject);
        if (slot == 0 || slot == 3) {
            const uint32_t got = shape::P(gIn.outVar);
            if (got == 0x0bad0bad) {
                Log(got);
            } else {
                LogArg(got);
                if (slot == 3 && (r & 0xff)) {
                    DumpShape(gIn.outVar);
                    CallSlot(gIn.outVar, 2, nullptr, 0); // the copy's one reference: destroyed
                } else if (slot == 0 && (r & 0xff)) {
                    Log(CallSlot(gObject, 2, nullptr, 0)); // QueryInterface's reference
                }
            }
        }
    }
    DumpShape(gObject);
    Op(2);
    Log(CallSlot(gObject, 1, nullptr, 0));
    Log(CallSlot(gObject, 2, nullptr, 0));
    Log(CallSlot(gObject, 2, nullptr, 0)); // destroyed
    Op(3);
}

bool CompareLogs(difftest::Context &ctx, const char *test, uint32_t seed, unsigned side)
{
    ++ctx.cases;
    const std::vector<uint32_t> &a = gSides[0].words, &b = gSides[side].words;
    if (gSides[0].overflow || gSides[side].overflow) {
        ctx.Fail("%s seed %u: log overflow", test, seed);
        return false;
    }
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    if (i == n && a.size() == b.size()) return true;
    uint32_t op = 0;
    size_t at = 0;
    for (size_t j = 0; j < i; ++j)
        if ((a[j] & 0xff000000u) == 0x0b000000u) op = a[j], at = j;
    ctx.Fail("%s side %u seed %u: logs differ at word %zu (%zu after op %08x = slot %u; sizes %zu / %zu): %08x / %08x", test,
             side, seed, i, i - at, op, (op >> 12) & 0xff, a.size(), b.size(), i < a.size() ? a[i] : 0,
             i < b.size() ? b[i] : 0);
    return false;
}

void RunAll(difftest::Context &ctx, const char *label, unsigned firstSeed, unsigned lastSeed)
{
    void *(__cdecl * creates[kSides])();
    creates[0] = creates[2] = ctx.Original<void *(__cdecl *)()>(gDesc->create);
    if (!PickOurs(ctx, gDesc->create, creates[1])) return;
    unsigned failures = 0;
    for (uint32_t seed = firstSeed; seed <= lastSeed && failures < 4; ++seed) {
        for (unsigned side = 0; side < kSides; ++side) {
            BuildSet(seed);
            gSides[side].Reset();
            gCur = &gSides[side];
            gHooksSide = side == 2;
            RunSlots(creates[side], seed);
            gCur = nullptr;
        }
        for (unsigned side = 1; side < kSides; ++side)
            if (!CompareLogs(ctx, label, seed, side)) ++failures;
    }
}

} // namespace

static void RunShape(difftest::Context &ctx, const Desc &desc)
{
    gDesc = &desc;
    Fixture fixture(ctx);
    ReadOurVtables();
    FillFakes();
    // The hooks side: the replacement of each slot of the game's vtable that has one.
    for (unsigned slot = 0; slot < 46; ++slot) {
        void *body = shape::GameVTable(desc.gameVt[0])[slot];
        const HookSpec *h = HookAt(body, ctx);
        gSlotOverride[slot] = h ? h->replacement : nullptr;
    }
    RunAll(ctx, desc.name, 1, 40);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    RunAll(ctx, desc.name, 101, 140);
    delete gSet; // while the allocator service is ours
    gSet = nullptr;
}

DIFF_TEST(CompGeomConvexCuboid_TranslatedSlots, 0x190450)
{
    RunShape(ctx, kTranslated);
}

DIFF_TEST(CompGeomConvexCuboid_OrientedSlots, 0x190420)
{
    RunShape(ctx, kOriented);
}

namespace {

// ---- the helpers, directly ----

struct HelperBuf {
    float a[3], b[3];
    float R[9];
    float T[3];
    AffineTransform3 frameA, frameB;
    Vec3 dir;
};

// Entries of a rotation matrix or an offset: mostly small, so that the separating tests get past their early exits.
float Small(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return Num(rng);
    case 1: return float(int(rng() % 3) - 1);
    default: return float(int(rng() % 201) - 100) / 100.0f;
    }
}
// A frame near a pure scale of the identity, or a random one.
void NearIdentityFrame(AffineTransform3 &f, std::mt19937 &rng)
{
    static const uint32_t kFlags[] = {0, 1, 4, 5, 12, 13, 0x14, 0x1c, 0x1e};
    if (rng() % 3 == 0) {
        RandomFrame(f, rng);
        return;
    }
    f.mUnknown0 = kFlags[rng() % std::size(kFlags)];
    for (float &m : f.mMatrix) m = 0.0f;
    f.mMatrix[0] = std::fabs(Extent(rng)) + 0.25f;
    f.mMatrix[4] = std::fabs(Extent(rng)) + 0.25f;
    f.mMatrix[8] = std::fabs(Extent(rng)) + 0.25f;
    if (rng() % 3 == 0) {
        f.mMatrix[1] = Small(rng) * 0.5f;
        f.mMatrix[3] = Small(rng) * 0.5f;
        f.mMatrix[5] = Small(rng) * 0.5f;
        f.mMatrix[7] = Small(rng) * 0.5f;
    }
    for (float &o : f.mOffset) o = Small(rng) * 3.0f;
}

void RandomHelperBuf(HelperBuf &b, std::mt19937 &rng)
{
    gNaNBudget = rng() % 2 == 0 ? 1 + rng() % 5 : 0; // several NaNs: which payload an operation keeps shows
    for (float &x : b.a) x = rng() % 3 ? std::fabs(Small(rng)) + 0.25f : Extent(rng);
    for (float &x : b.b) x = rng() % 3 ? std::fabs(Small(rng)) + 0.25f : Extent(rng);
    for (float &x : b.R) x = Small(rng);
    for (float &x : b.T) x = rng() % 3 ? Small(rng) * 2.0f : Num(rng);
    NearIdentityFrame(b.frameA, rng);
    NearIdentityFrame(b.frameB, rng);
    b.dir = {Num(rng), Num(rng), Num(rng)};
}

unsigned gTrues; // how often the boolean helpers said yes

template <class Call> void RunHelper(difftest::Context &ctx, const char *name, uint32_t seed, Call call)
{
    std::mt19937 rng(seed);
    unsigned failures = 0;
    gTrues = 0;
    for (unsigned i = 0; i < 40000 && failures < 6; ++i) {
        HelperBuf in;
        RandomHelperBuf(in, rng);
        uint32_t ra[8], rb[8];
        std::memset(ra, 0x5c, sizeof(ra));
        std::memset(rb, 0x5c, sizeof(rb));
        HelperBuf a = in, b = in;
        const uint64_t va = call(false, a, ra), vb = call(true, b, rb);
        ++ctx.cases;
        gTrues += (va & 1) != 0 && (vb & 1) != 0;
        if (va != vb || std::memcmp(ra, rb, sizeof(ra)) != 0 || std::memcmp(&a, &b, sizeof(a)) != 0) {
            ctx.Fail("%s case %u differs: %llx / %llx; out %08x %08x %08x %08x %08x %08x / %08x %08x %08x %08x %08x %08x",
                     name, i, (unsigned long long)va, (unsigned long long)vb, ra[0], ra[1], ra[2], ra[3], ra[4], ra[5],
                     rb[0], rb[1], rb[2], rb[3], rb[4], rb[5]);
            ++failures;
        }
    }
}

void RunHelpers(difftest::Context &ctx, const char *suffix)
{
    using ScalesFn = Vec3 *(__thiscall *)(const void *, void *);
    using CornerFn = Vec3 *(__thiscall *)(const void *, void *, const Vec3 *);
    using AxesFn = uint8_t(__cdecl *)(const float *, const float *, const float *, const float *);
    using OverlapFn = uint8_t(__cdecl *)(const void *, const void *, const void *, const void *);
    ScalesFn scales[2];
    CornerFn corner[2];
    AxesFn axes[2];
    OverlapFn overlap[2];
    scales[0] = ctx.Original<ScalesFn>(0x19cf80);
    corner[0] = ctx.Original<CornerFn>(0x191ea0);
    axes[0] = ctx.Original<AxesFn>(0x19e070);
    overlap[0] = ctx.Original<OverlapFn>(0x19dcb0);
    if (!PickOurs(ctx, 0x19cf80, scales[1]) || !PickOurs(ctx, 0x191ea0, corner[1]) || !PickOurs(ctx, 0x19e070, axes[1]) ||
        !PickOurs(ctx, 0x19dcb0, overlap[1]))
        return;
    const auto name = [&](const char *n) {
        static char buffer[8][96];
        static unsigned next;
        char *b = buffer[next++ % 8];
        std::snprintf(b, 96, "%s%s", n, suffix);
        return b;
    };
    RunHelper(ctx, name("ConvexFrame::AxisScales"), 21, [&](bool ours, HelperBuf &b, uint32_t *out) {
        Vec3 *r = scales[ours](&b.frameA, out);
        return uint64_t(r == reinterpret_cast<Vec3 *>(out));
    });
    RunHelper(ctx, name("ConvexCuboid::SupportCorner"), 22, [&](bool ours, HelperBuf &b, uint32_t *out) {
        Vec3 *r = corner[ours](b.a, out, &b.dir);
        return uint64_t(r == reinterpret_cast<Vec3 *>(out));
    });
    RunHelper(ctx, name("BoxesMeetOnAxes15"), 23, [&](bool ours, HelperBuf &b, uint32_t *) {
        return uint64_t(axes[ours](b.a, b.b, b.R, b.T));
    });
    if (gTrues < 500) ctx.Fail("BoxesMeetOnAxes15%s: only %u overlapping cases", suffix, gTrues);
    RunHelper(ctx, name("CuboidsOverlap"), 24, [&](bool ours, HelperBuf &b, uint32_t *) {
        return uint64_t(overlap[ours](b.a, &b.frameA, b.b, &b.frameB));
    });
    if (gTrues < 500) ctx.Fail("CuboidsOverlap%s: only %u overlapping cases", suffix, gTrues);
}

} // namespace

DIFF_TEST(CompGeomConvexCuboid_Helpers, 0x19dcb0)
{
    RunHelpers(ctx, "");
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    RunHelpers(ctx, " at 24-bit");
}

