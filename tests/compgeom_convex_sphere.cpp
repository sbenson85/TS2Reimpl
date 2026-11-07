// Differential tests for the translated sphere, cConvexT<cITranslatedSphereShape> (lane PM, src/compgeom/ConvexShapes.h).
// - CompGeomConvexSphere_Slots: shapes with random data, then random slots through each object's own vtable. Three
//   sides: the game's factory object (the game's slots); our Create's object (our overrides, through our vtable); and
//   the game's object with each slot that has a hook run through the hook's replacement (the embedded light shapes'
//   case, src/sg/SGLightShapes.h). The cases' results, out parameters and every call the slots make on the fake bound
//   and linear (frames built by the slot by value) are logged and compared with the game's. The helpers the slots call
//   (the frame members, the point-to-box distance, the pair tests) run for real on each side: the game's own on the
//   game's, ours on ours. Clone's copies are dumped and released; at the end the shape is released.
// - CompGeomConvexSphere_Helpers: the sphere's boxes and support point (0x192250, 0x192380, 0x193380), the frame's
//   transposed product (0x19caa0) and the point-to-box distance (0x19f820), directly, with NaNs.
// Both run at the harness's 53-bit x87 precision and at the game's 24-bit.
// Slot bodies the Slots test runs on all three sides (RVAs as the status file names them): 0x1924a0 0x1924c0 0x192510
// 0x192530 0x1927f0 0x192670 0x192960 0x192a60 0x192a80 0x192aa0 0x192ac0 0x192bc0 0x192c40 0x192cc0 0x192d50 0x192e60
// 0x192e80 0x192ea0 0x192ec0 0x192fd0 0x193050 0x193140 0x1931f0 0x1932f0 0x192150 0x1921c0 0x192230 0x193460 0x193480
// 0x1934a0 0x1934c0 0x1934e0 0x193500.
// Wrapper instantiations the slots call (in effect through callers, run inside the slot cases): 0x19eec0 0x19ee50 0x19ede0
// 0x19edc0 0x19eda0 0x19ed80 0x19f790 0x19f720 0x19f6b0 0x19f690 0x19f670 0x19f650.

#include "compgeom/ConvexShapes.h"
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

struct Fixture {
    shape::CodePatch gameNew, gameDelete, ourNew, ourDelete, ourSizedDelete;
    explicit Fixture(difftest::Context &)
        : gameNew(0x824632, reinterpret_cast<void *>(&HookNew)), gameDelete(0x824624, reinterpret_cast<void *>(&HookDelete)),
          ourNew(reinterpret_cast<void *>(static_cast<void *(__cdecl *)(size_t)>(&::operator new)),
                 reinterpret_cast<void *>(&HookNew)),
          ourDelete(reinterpret_cast<void *>(static_cast<void(__cdecl *)(void *)>(&::operator delete)),
                    reinterpret_cast<void *>(&HookDelete)),
          ourSizedDelete(reinterpret_cast<void *>(static_cast<void(__cdecl *)(void *, size_t)>(&::operator delete)),
                         reinterpret_cast<void *>(&HookSizedDelete))
    {
        gMainThread = GetCurrentThreadId();
        for (SideLog &s : gSides) s.Reset();
    }
    ~Fixture() { gCur = nullptr; }
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

constexpr uint32_t kCreate = 0x192460;
constexpr uint32_t kGameVt[2] = {0xda1970, 0xda18b4}; // concrete, bound
constexpr uint32_t kObjectBytes = 0x18, kCountOffset = 0x14, kDataBytes = 0x10;
constexpr uint32_t kClassId = 0x7ce57a3c;

void **gOurVt[2]; // cTranslatedSphereShape, cTranslatedSphereBound
void ReadOurVtables()
{
    alignas(8) static uint8_t buffer[0x20];
    auto *s = ::new (buffer) cTranslatedSphereShape;
    gOurVt[0] = *reinterpret_cast<void ***>(s);
    s->~cTranslatedSphereShape();
    auto *b = ::new (buffer) cTranslatedSphereBound;
    gOurVt[1] = *reinterpret_cast<void ***>(b);
    b->~cTranslatedSphereBound();
}
uint32_t VtCode(uint32_t w)
{
    for (unsigned i = 0; i < 2; ++i)
        if (w == shape::P(shape::GameVTable(kGameVt[i])) || w == shape::P(gOurVt[i])) return 0xd0000000u | i;
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
    DumpBytes(o + 4, kDataBytes);
    Log(Word(o, kCountOffset));
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
    if (w >= o && w < o + kObjectBytes) {
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
    gBoundVt[13] = FakeSlotAddress<0xb0000000u, 13>(std::make_integer_sequence<unsigned, 2>());
    gBoundVt[18] = FakeSlotAddress<0xb0000000u, 18>(std::make_integer_sequence<unsigned, 3>());
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
    // The shape's data: the radius, then the centre.
    float data[4];
    data[0] = Extent(rng);
    for (unsigned i = 1; i < 4; ++i) data[i] = Num(rng);
    std::memcpy(static_cast<uint8_t *>(gObject) + 4, data, sizeof(data));
}

constexpr uint32_t kIds[] = {1, cIShape::kInterfaceId, cIBound::kInterfaceId, cIConvex::kInterfaceId, kClassId, 0x3ce57cc0, 0};

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
        w[0] = rng() % 8 ? kIds[rng() % std::size(kIds)] : rng();
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
    case 11:
    case 12: w[0] = opaque, w[1] = f1; return 2;
    case 13: w[0] = sphere, w[1] = f1; return 2;
    case 14: w[0] = translated, w[1] = f1; return 2;
    case 15: w[0] = bound, w[1] = f1; return 2;
    case 16:
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
void SweepRadius(unsigned slot, const uint32_t *w, unsigned n)
{
    float *radius = reinterpret_cast<float *>(static_cast<uint8_t *>(gObject) + 4);
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
        if (SweepableSlot(slot) && rng() % 3 == 0) SweepRadius(slot, w, n);
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
    creates[0] = creates[2] = ctx.Original<void *(__cdecl *)()>(kCreate);
    if (!PickOurs(ctx, kCreate, creates[1])) return;
    unsigned failures = 0;
    for (uint32_t seed = firstSeed; seed <= lastSeed && failures < 4; ++seed) {
        for (unsigned side = 0; side < kSides; ++side) {
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

DIFF_TEST(CompGeomConvexSphere_Slots, 0x192460)
{
    Fixture fixture(ctx);
    ReadOurVtables();
    FillFakes();
    // The hooks side: the replacement of each slot of the game's vtable that has one.
    for (unsigned slot = 0; slot < 46; ++slot) {
        void *body = shape::GameVTable(kGameVt[0])[slot];
        const HookSpec *h = HookAt(body, ctx);
        gSlotOverride[slot] = h ? h->replacement : nullptr;
    }
    RunAll(ctx, "slots", 1, 40);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    RunAll(ctx, "slots at 24-bit", 101, 140);
}

namespace {

// ---- the helpers, directly ----

struct HelperBuf {
    float radius;
    float pad[3];
    AffineTransform3 frame;
    Vec3 a, b;
    float half[3];
};

void RandomHelperBuf(HelperBuf &b, std::mt19937 &rng)
{
    gNaNBudget = rng() % 2 == 0 ? 1 + rng() % 5 : 0; // several NaNs: which payload an operation keeps shows
    b.radius = Extent(rng);
    RandomFrame(b.frame, rng);
    if (rng() % 3 == 0) b.frame.mUnknown0 &= ~0x18u; // the axis-aligned branch is the common one in the game
    b.a = {Num(rng), Num(rng), Num(rng)};
    b.b = {Num(rng), Num(rng), Num(rng)};
    for (float &h : b.half) h = Extent(rng);
}

template <class Call> void RunHelper(difftest::Context &ctx, const char *name, uint32_t seed, Call call)
{
    std::mt19937 rng(seed);
    unsigned failures = 0;
    for (unsigned i = 0; i < 40000 && failures < 6; ++i) {
        HelperBuf in;
        RandomHelperBuf(in, rng);
        uint32_t ra[8], rb[8];
        std::memset(ra, 0x5c, sizeof(ra));
        std::memset(rb, 0x5c, sizeof(rb));
        HelperBuf a = in, b = in;
        const uint64_t va = call(false, a, ra), vb = call(true, b, rb);
        ++ctx.cases;
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
    using BoxInFrameFn = BoundBox *(__thiscall *)(const void *, void *, const AffineTransform3 *);
    using BoxAtFn = BoundBox *(__thiscall *)(const void *, void *, const Vec3 *);
    using Vec3Fn = Vec3 *(__thiscall *)(const void *, void *, const Vec3 *);
    using DistanceFn = double(__cdecl *)(const float *, const float *);
    BoxInFrameFn boxInFrame[2];
    BoxAtFn boxAt[2];
    Vec3Fn support[2], transposed[2];
    DistanceFn distance[2];
    boxInFrame[0] = ctx.Original<BoxInFrameFn>(0x192250);
    boxAt[0] = ctx.Original<BoxAtFn>(0x192380);
    support[0] = ctx.Original<Vec3Fn>(0x193380);
    transposed[0] = ctx.Original<Vec3Fn>(0x19caa0);
    distance[0] = ctx.Original<DistanceFn>(0x19f820);
    if (!PickOurs(ctx, 0x192250, boxInFrame[1]) || !PickOurs(ctx, 0x192380, boxAt[1]) ||
        !PickOurs(ctx, 0x193380, support[1]) || !PickOurs(ctx, 0x19caa0, transposed[1]) ||
        !PickOurs(ctx, 0x19f820, distance[1]))
        return;
    const auto name = [&](const char *n) {
        static char buffer[8][96];
        static unsigned next;
        char *b = buffer[next++ % 8];
        std::snprintf(b, 96, "%s%s", n, suffix);
        return b;
    };
    RunHelper(ctx, name("Sphere::BoxInFrame"), 11, [&](bool ours, HelperBuf &b, uint32_t *out) {
        BoundBox *r = boxInFrame[ours](&b.radius, out, &b.frame);
        return uint64_t(r == reinterpret_cast<BoundBox *>(out));
    });
    RunHelper(ctx, name("Sphere::BoxAt"), 12, [&](bool ours, HelperBuf &b, uint32_t *out) {
        BoundBox *r = boxAt[ours](&b.radius, out, &b.a);
        return uint64_t(r == reinterpret_cast<BoundBox *>(out));
    });
    RunHelper(ctx, name("Sphere::SupportAlong"), 13, [&](bool ours, HelperBuf &b, uint32_t *out) {
        Vec3 *r = support[ours](&b.radius, out, &b.a);
        return uint64_t(r == reinterpret_cast<Vec3 *>(out));
    });
    RunHelper(ctx, name("ConvexFrame::TransposedVectorTo"), 14, [&](bool ours, HelperBuf &b, uint32_t *out) {
        Vec3 *r = transposed[ours](&b.frame, out, &b.a);
        return uint64_t(r == reinterpret_cast<Vec3 *>(out));
    });
    // In place (out is in): the new x is read by the next rows.
    RunHelper(ctx, name("ConvexFrame::TransposedVectorTo in place"), 15, [&](bool ours, HelperBuf &b, uint32_t *out) {
        Vec3 *r = transposed[ours](&b.frame, &b.a, &b.a);
        std::memcpy(out, &b.a, sizeof(Vec3));
        return uint64_t(r == &b.a);
    });
    RunHelper(ctx, name("PointBoxDistanceSquared"), 16, [&](bool ours, HelperBuf &b, uint32_t *out) {
        const double d = distance[ours](b.half, &b.a.x);
        std::memcpy(out, &d, sizeof(d));
        return uint64_t(Bits(float(d))) | uint64_t(std::isnan(d) ? 1 : 0) << 32;
    });
    // The degenerate-direction cut-off of SupportAlong sits at length 1.1920929e-10: the lengths one ulp either side
    // of it and on it, along each axis and either sign, must pick the same branch as the original.
    {
        const uint32_t tiny = Bits(1.1920929e-10f);
        unsigned seed = 0;
        for (int axis = 0; axis < 3; ++axis) {
            for (int sign = 0; sign < 2; ++sign) {
                for (int step = -3; step <= 3; ++step) {
                    const float x = Float((sign ? 0x80000000u : 0u) | uint32_t(int(tiny) + step));
                    HelperBuf in;
                    std::memset(&in, 0, sizeof(in));
                    in.radius = 2.5f;
                    (&in.a.x)[axis] = x;
                    uint32_t ra[8], rb[8];
                    std::memset(ra, 0x5c, sizeof(ra));
                    std::memset(rb, 0x5c, sizeof(rb));
                    HelperBuf a = in, b = in;
                    support[0](&a.radius, ra, &a.a);
                    support[1](&b.radius, rb, &b.a);
                    ++ctx.cases;
                    ++seed;
                    if (std::memcmp(ra, rb, sizeof(ra)) != 0)
                        ctx.Fail("Sphere::SupportAlong cut-off case %u (axis %d sign %d step %d)%s differs", seed, axis, sign, step, suffix);
                }
            }
        }
    }
}

} // namespace

DIFF_TEST(CompGeomConvexSphere_Helpers, 0x192250)
{
    RunHelpers(ctx, "");
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    RunHelpers(ctx, " at 24-bit");
}
