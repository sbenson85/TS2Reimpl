// Differential tests for nGZCompGeom's linear shapes (src/compgeom/Linear3.h, lane NY): cLinear3T<cILine3 / cIRay3 /
// cISegment3> and cLine3, cRay3, cSegment3.
//
// Three routes, each against the original on the same inputs:
// - CompGeomLinear3_Slots: our object (built by our constructor) called through our own vtable, slot s against the game's
//   slot s of the same kind (a slot-order mistake calls the wrong function);
// - CompGeomLinear3_Adapters: the hook table's replacement at each slot RVA (tools: RegisteredHooks, so the table's wiring
//   is what runs) on an object carrying a copy of the game's vtable with an arity-matched trap in the slot under test;
// - CompGeomLinear3_Lifetime / _Contains: constructors, factories, destructors, the assignment and the containment helpers.
//
// The intersection tests the slots forward to (0x197d70-0x19b330) are stood in for by recorders patched over the image's
// functions (both sides call the image's), as is the bound's double dispatch (a fake bound). The frame functions run for
// real (0x19c820 is ours on our side, verified). Floats are compared as bit patterns; the inputs carry at most one NaN
// (with two, the operand order decides which payload survives, and the containment tests compare their products' bits:
// "nearly equal" orders the bit patterns, NaNs included).
#include "compgeom/Linear3.h"
#include "difftest.h"
#include "hook/hooks.h"

#include "code_patch.h"
#include "shape.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include <windows.h>

using namespace nGZCompGeom;

namespace {

constexpr uint32_t kVt[3] = {0xda126c, 0xda139c, 0xda14cc};         // cLine3, cRay3, cSegment3
constexpr uint32_t kTemplateVt[3] = {0xda11d4, 0xda1304, 0xda1434}; // cLinear3T<cILine3 / cIRay3 / cISegment3>
constexpr uint32_t kClassIds[3] = {0x3ce57cc0, 0x7ce57eff, 0x3ce57f31};
const char *const kKind[3] = {"line", "ray", "segment"};
constexpr unsigned kSlots = 37;

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

// ---- our vtables ----

void *BuildOurs(unsigned k, void *at)
{
    switch (k) {
    case 0: return ::new (at) cLine3;
    case 1: return ::new (at) cRay3;
    default: return ::new (at) cSegment3;
    }
}
void *BuildOurTemplate(unsigned k, void *at)
{
    switch (k) {
    case 0: return ::new (at) cLinear3T<cILine3>;
    case 1: return ::new (at) cLinear3T<cIRay3>;
    default: return ::new (at) cLinear3T<cISegment3>;
    }
}
void **OurVt(unsigned k)
{
    static void **vt[3];
    if (!vt[k]) {
        alignas(8) uint8_t buffer[0x20];
        vt[k] = *static_cast<void ***>(BuildOurs(k, buffer));
    }
    return vt[k];
}
void **OurTemplateVt(unsigned k)
{
    static void **vt[3];
    if (!vt[k]) {
        alignas(8) uint8_t buffer[0x20];
        vt[k] = *static_cast<void ***>(BuildOurTemplate(k, buffer));
    }
    return vt[k];
}

void *gAdapterVt[kSlots]; // the adapter route's copy of the game's vtable (trap in the slot under test)
unsigned gAdapterKind;    // whose (the copy stands for that kind's concrete vtable)

// A vtable word as a code: the kind's concrete vtable (game's, ours, the adapter copy) or its template's.
uint32_t VtCode(uint32_t word)
{
    for (unsigned k = 0; k < 3; ++k) {
        if (word == shape::P(shape::GameVTable(kVt[k])) || word == shape::P(OurVt(k))) return 0xd0000000u | k;
        if (word == shape::P(shape::GameVTable(kTemplateVt[k])) || word == shape::P(OurTemplateVt(k)))
            return 0xd0000010u | k;
    }
    if (word == shape::P(gAdapterVt)) return 0xd0000000u | gAdapterKind;
    return word;
}

// ---- shared buffers (the same addresses on every side) ----

struct alignas(16) Object {
    uint32_t pre[4];
    uint32_t words[8]; // +0 vtable, +4 data (6 floats), +0x1c the reference count
    uint32_t post[4];
};
Object gObject;
alignas(16) uint8_t gHeap[0x40]; // what the redirected operator new hands out
alignas(16) uint8_t gShape[0x80]; // the other shape (opaque to the slots)
AffineTransform3 gFrame1, gFrame2;
Vec3 gPoint;
float gT;
uint32_t gOutBuf[5]; // [1..3]: a Vec3 result
void *gOutVar;       // QueryInterface / Clone's out

// Our-side pointers the slots pass on, as codes: into the object or the heap block by offset.
uint32_t Norm(uint32_t w)
{
    const uint32_t obj = shape::P(gObject.words), heap = shape::P(gHeap);
    if (w >= obj && w < obj + 0x20) return 0xe0000000u | (w - obj);
    if (w >= heap && w < heap + 0x40) return 0xe1000000u | (w - heap);
    return w;
}

// ---- the log the stand-ins write ----

uint32_t gLog[64];
unsigned gLogN;
uint32_t gRet; // what the stand-ins return
void Log(uint32_t w)
{
    if (gLogN < std::size(gLog)) gLog[gLogN] = w;
    ++gLogN;
}

// The intersection tests (cdecl): log which, their arguments, the linear data they were given; return gRet.
struct Helper {
    uint32_t rva;
    unsigned words;
};
// Slots 8-12, 14-17 (three arguments), then 19-23, 25-28 (four); per slot line, ray, segment.
constexpr Helper kHelpers[] = {
    {0x197fa0, 3}, {0x198080, 3}, {0x198170, 3}, {0x198270, 3}, {0x198320, 3}, {0x1983f0, 3}, {0x199230, 3},
    {0x199310, 3}, {0x1993f0, 3}, {0x199f90, 3}, {0x19a070, 3}, {0x19a160, 3}, {0x19a260, 3}, {0x19a310, 3},
    {0x19a3e0, 3}, {0x19ae00, 3}, {0x19aea0, 3}, {0x19af40, 3}, {0x197d70, 3}, {0x197e10, 3}, {0x197ed0, 3},
    {0x198fc0, 3}, {0x199090, 3}, {0x199160, 3}, {0x199d60, 3}, {0x199e00, 3}, {0x199ec0, 3}, {0x198750, 4},
    {0x198820, 4}, {0x198930, 4}, {0x198a50, 4}, {0x198b00, 4}, {0x198be0, 4}, {0x199740, 4}, {0x1997f0, 4},
    {0x1998d0, 4}, {0x19a740, 4}, {0x19a810, 4}, {0x19a920, 4}, {0x19aa40, 4}, {0x19aaf0, 4}, {0x19abd0, 4},
    {0x19b150, 4}, {0x19b1f0, 4}, {0x19b290, 4}, {0x1984f0, 4}, {0x198590, 4}, {0x198660, 4}, {0x1994e0, 4},
    {0x199580, 4}, {0x199650, 4}, {0x19a4e0, 4}, {0x19a580, 4}, {0x19a650, 4},
};
constexpr unsigned kHelperCount = unsigned(std::size(kHelpers));

void LogLinear(uint32_t w)
{
    // The linear data the helper was given, by value too (it must be the object's own).
    const uint32_t code = Norm(w);
    Log(code);
    if (code == (0xe0000000u | 4))
        for (unsigned i = 0; i < 6; ++i) Log(gObject.words[1 + i]);
}
template <unsigned N> uint32_t __cdecl Helper3(uint32_t a, uint32_t b, uint32_t c)
{
    Log(0xa0000000u | N);
    Log(Norm(a));
    Log(Norm(b));
    LogLinear(c);
    return gRet;
}
template <unsigned N> uint32_t __cdecl Helper4(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Log(0xa0000000u | N);
    Log(Norm(a));
    Log(Norm(b));
    Log(Norm(c));
    LogLinear(d);
    return gRet;
}
template <unsigned N> void *HelperStand()
{
    if constexpr (N < 27)
        return reinterpret_cast<void *>(&Helper3<N>);
    else
        return reinterpret_cast<void *>(&Helper4<N>);
}
template <unsigned... N> void HelperStands(void **out, std::integer_sequence<unsigned, N...>)
{
    ((out[N] = HelperStand<N>()), ...);
}
struct HelperPatches {
    std::vector<std::unique_ptr<shape::CodePatch>> patches;
    HelperPatches()
    {
        void *stands[kHelperCount];
        HelperStands(stands, std::make_integer_sequence<unsigned, kHelperCount>());
        for (unsigned i = 0; i < kHelperCount; ++i) {
            patches.push_back(std::make_unique<shape::CodePatch>(kHelpers[i].rva, stands[i]));
            // A helper whose replacement is ours (the slots call it directly): the stand-in goes over ours as well.
            for (const HookSpec *h : RegisteredHooks())
                if (h->rva == kHelpers[i].rva && h->replacement)
                    patches.push_back(std::make_unique<shape::CodePatch>(h->replacement, stands[i]));
        }
    }
};

// The fake bound (cIBound, 40 slots): each slot logs its index and arguments and returns gRet. Stack words per slot.
constexpr unsigned kBoundWords[40] = {2, 0, 0, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,
                                      3, 2, 2, 2, 2, 1, 1, 1, 0, 3, 3, 3, 3, 2, 2, 2, 0, 2, 2, 1};
template <unsigned S, unsigned... I> uint32_t __fastcall BoundSlot(void *, void *, shape::StackWord<I>... w)
{
    Log(0xb0000000u | S);
    (LogLinear(w), ...);
    return gRet;
}
template <unsigned S, unsigned... I> void *BoundSlotAddress(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<void *>(&BoundSlot<S, I...>);
}
template <unsigned... S> void FillBound(void **vt, std::integer_sequence<unsigned, S...>)
{
    ((vt[S] = BoundSlotAddress<S>(std::make_integer_sequence<unsigned, kBoundWords[S]>())), ...);
}
void *gBoundVt[40];
struct {
    void **vt;
} gBound;

// ---- the allocator, redirected around the calls that allocate or free ----

uint32_t gAllocs, gAllocSize, gFrees, gFreePtr, gFreeSize, gFreeSnap[8];
void *__cdecl Allocate(size_t size)
{
    ++gAllocs;
    gAllocSize = uint32_t(size);
    return gHeap;
}
void __cdecl Free(void *p, size_t size)
{
    ++gFrees;
    gFreePtr = Norm(shape::P(p));
    gFreeSize = uint32_t(size);
    std::memcpy(gFreeSnap, p, sizeof(gFreeSnap));
    gFreeSnap[0] = VtCode(gFreeSnap[0]);
}
struct AllocatorRedirects {
    shape::CodePatch gameNew, ourNew, gameDelete, ourDelete;
    AllocatorRedirects()
        : gameNew(0x824632, reinterpret_cast<void *>(&Allocate)),
          ourNew(reinterpret_cast<void *>(static_cast<void *(__cdecl *)(size_t)>(&::operator new)),
                 reinterpret_cast<void *>(&Allocate)),
          gameDelete(0x824624, reinterpret_cast<void *>(&Free)),
          ourDelete(reinterpret_cast<void *>(static_cast<void(__cdecl *)(void *, size_t)>(&::operator delete)),
                    reinterpret_cast<void *>(&Free))
    {
    }
};

// ---- inputs ----

constexpr uint32_t kEdges[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x00000001, 0x80000001, 0x007fffff, 0x00800000, 0x7f7fffff,
    0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001, 0x7fa00000, 0x3f000000, 0x40400000, 0x34000000,
};
int gNaNBudget;
bool gOneNaN;

float Num(std::mt19937 &rng)
{
    float f;
    switch (rng() % 10) {
    case 0:
    case 1:
    case 2: f = float(int(rng() % 9) - 4); break;
    case 3:
    case 4:
    case 5: f = float(int(rng() % 20001) - 10000) / 100.0f; break;
    case 6: f = Float((rng() & 0x80000000u) | ((96 + rng() % 64) << 23) | (rng() & 0x7fffff)); break;
    case 7: f = Float(kEdges[rng() % std::size(kEdges)]); break;
    case 8: f = Float(rng()); break;
    default: f = Float(rng() & 0x807fffffu); break; // denormal
    }
    if (std::isnan(f)) {
        if (gNaNBudget <= 0) return 2.5f;
        --gNaNBudget;
    } else if (gOneNaN && !(std::fabs(f) < 1e6f)) {
        return float(int(rng() % 9) - 4); // no infinity or overflow: no NaN made inside (it would be the default NaN)
    }
    return f;
}
// A case's NaN mode: none in the inputs (the NaNs the code makes from infinities are all the default NaN), or one, with no
// infinities and no values that can overflow.
void NaNMode(std::mt19937 &rng)
{
    gOneNaN = rng() % 3 == 0;
    gNaNBudget = gOneNaN ? 1 : 0;
}
float Ulps(float f, int n) // never makes a NaN (a NaN stays the same NaN)
{
    const float r = Float(Bits(f) + uint32_t(n));
    return std::isnan(r) ? f : r;
}

void RandomFrame(AffineTransform3 &f, std::mt19937 &rng)
{
    static const uint32_t kFlags[] = {0, 1, 2, 4, 6, 8, 0x10, 0x12, 0x14, 0x1e, 0x1f};
    f.mUnknown0 = rng() % 8 ? kFlags[rng() % std::size(kFlags)] : rng();
    for (float &m : f.mMatrix) m = rng() % 3 ? Num(rng) : float(rng() % 2);
    for (float &o : f.mOffset) o = Num(rng);
}

// A point for the containment tests: the origin (start), near it, on the line (within the segment, ahead of the ray or
// not), near the line, or anywhere.
Vec3 PointFor(unsigned k, const float *data, std::mt19937 &rng)
{
    const Vec3 o{data[0], data[1], data[2]};
    const Vec3 d = k == 2 ? Vec3{data[3] - data[0], data[4] - data[1], data[5] - data[2]} : Vec3{data[3], data[4], data[5]};
    Vec3 p;
    float s;
    switch (rng() % 8) {
    case 0: return o;
    case 1: p = o; (&p.x)[rng() % 3] = Ulps((&o.x)[rng() % 3], int(rng() % 5) - 2); return p;
    case 2:
    case 3:
    case 4:
        s = rng() % 2 ? float(int(rng() % 7) - 3) : float(int(rng() % 2001) - 1000) / 500.0f;
        p = {o.x + s * d.x, o.y + s * d.y, o.z + s * d.z};
        if (rng() % 3 == 0) (&p.x)[rng() % 3] = Ulps((&p.x)[rng() % 3], int(rng() % 9) - 4);
        return p;
    case 5: s = float(rng() % 1001) / 1000.0f; return {o.x + s * d.x, o.y + s * d.y, o.z + s * d.z};
    default: return {Num(rng), Num(rng), Num(rng)};
    }
}

void RandomData(unsigned k, float *data, std::mt19937 &rng)
{
    for (unsigned i = 0; i < 6; ++i) data[i] = Num(rng);
    if (rng() % 4 == 0) // small integers: exact products
        for (unsigned i = 0; i < 6; ++i) data[i] = float(int(rng() % 7) - 3);
    if (rng() % 10 == 0) // a degenerate line (direction 0) or segment (both ends the same)
        for (unsigned i = 0; i < 3; ++i) data[3 + i] = k == 2 ? data[i] : 0.0f;
}

const uint32_t kIds[] = {1, 0x1ce54fd0, 0xfce591cb, 0x3ce57cc0, 0x7ce57eff, 0x3ce57f31, 0, 0x12345678};

struct Inputs {
    float data[6];
    uint32_t refCount;
    AffineTransform3 frame1, frame2;
    Vec3 point;
    float t;
    uint8_t shape[0x80];
    uint32_t id;
    bool outArg;
    uint32_t ret;
};

Inputs RandomInputs(unsigned k, unsigned slot, std::mt19937 &rng)
{
    Inputs in{};
    (void)slot;
    NaNMode(rng);
    RandomData(k, in.data, rng);
    static const uint32_t kCounts[] = {0, 1, 2, 3, 0xffffffffu, 0x80000000u};
    in.refCount = rng() % 2 ? kCounts[rng() % std::size(kCounts)] : rng();
    RandomFrame(in.frame1, rng);
    RandomFrame(in.frame2, rng);
    in.point = PointFor(k, in.data, rng);
    in.t = Num(rng);
    for (uint8_t &b : in.shape) b = uint8_t(rng());
    in.id = kIds[rng() % std::size(kIds)];
    in.outArg = rng() % 6 != 0;
    in.ret = (rng() & ~0xffu) | (rng() % 2);
    return in;
}

// ---- one call and what it left ----

struct Outcome {
    uint32_t ret;
    uint32_t object[16];
    uint32_t heap[16];
    uint8_t frames[2 * sizeof(AffineTransform3)];
    uint32_t point[3], t, outBuf[5], outVar;
    uint32_t log[64], logN;
    uint32_t allocs, allocSize, frees, freePtr, freeSize, freeSnap[8];
    uint32_t traps;
};

unsigned gTraps;
uint32_t TrapHit(void *, unsigned)
{
    ++gTraps;
    return 0;
}

enum Ret { kVoid, kBool, kWord, kOut, kSelfOffset };
struct SlotInfo {
    unsigned words;
    Ret ret;
};
constexpr SlotInfo kSlotInfo[36] = {
    {2, kBool}, {0, kWord}, {0, kWord}, {2, kBool}, {0, kWord},                                         // 0-4
    {2, kVoid}, {1, kVoid}, {1, kVoid},                                                                 // 5-7
    {1, kBool}, {1, kBool}, {1, kBool}, {1, kBool}, {1, kBool}, {1, kBool},                             // 8-13
    {2, kBool}, {2, kBool}, {2, kBool}, {2, kBool}, {2, kBool},                                         // 14-18
    {2, kBool}, {2, kBool}, {2, kBool}, {2, kBool}, {2, kBool}, {2, kBool},                             // 19-24
    {3, kBool}, {3, kBool}, {3, kBool}, {3, kBool}, {3, kBool},                                         // 25-29
    {1, kOut},  {1, kOut},  {1, kBool}, {2, kOut},  {0, kSelfOffset}, {0, kSelfOffset},                 // 30-35
};

void Arguments(unsigned slot, const Inputs &in, uint32_t *w)
{
    const uint32_t shapeArg = shape::P(gShape), bound = shape::P(&gBound), f1 = shape::P(&gFrame1),
                   f2 = shape::P(&gFrame2), t = shape::P(&gT);
    if (slot == 0 || slot == 3) {
        w[0] = in.id;
        w[1] = in.outArg ? shape::P(&gOutVar) : 0;
    } else if (slot == 5) {
        w[0] = f1;
        w[1] = f2;
    } else if (slot == 6 || slot == 7) {
        w[0] = f1;
    } else if (slot >= 8 && slot <= 12) {
        w[0] = shapeArg;
    } else if (slot == 13) {
        w[0] = bound;
    } else if (slot >= 14 && slot <= 17) {
        w[0] = shapeArg;
        w[1] = f1;
    } else if (slot == 18) {
        w[0] = bound;
        w[1] = f1;
    } else if (slot >= 19 && slot <= 23) {
        w[0] = t;
        w[1] = shapeArg;
    } else if (slot == 24) {
        w[0] = t;
        w[1] = bound;
    } else if (slot >= 25 && slot <= 28) {
        w[0] = t;
        w[1] = shapeArg;
        w[2] = f1;
    } else if (slot == 29) {
        w[0] = t;
        w[1] = bound;
        w[2] = f1;
    } else if (slot == 30 || slot == 31) {
        w[0] = shape::P(&gOutBuf[1]);
    } else if (slot == 32) {
        w[0] = shape::P(&gPoint);
    } else if (slot == 33) {
        w[0] = shape::P(&gOutBuf[1]);
        w[1] = Bits(in.t);
    }
}

uint32_t CallThis(void *fn, void *self, const uint32_t *w, unsigned n)
{
    switch (n) {
    case 0: return reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self);
    case 1: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(self, w[0]);
    case 2: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(fn)(self, w[0], w[1]);
    default:
        return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(fn)(self, w[0], w[1], w[2]);
    }
}

enum Side { kOriginal, kOurs, kAdapter };

// Places the inputs in the shared buffers and builds the side's object.
void Place(unsigned k, unsigned slot, Side side, const Inputs &in)
{
    std::memset(&gObject, 0xa5, sizeof(gObject));
    void *object = gObject.words;
    if (side == kOurs)
        BuildOurs(k, object);
    else if (side == kOriginal)
        gObject.words[0] = shape::P(shape::GameVTable(kVt[k]));
    else {
        std::memcpy(gAdapterVt, shape::GameVTable(kVt[k]), sizeof(gAdapterVt));
        gAdapterVt[slot] = shape::TrapFor<TrapHit>(kSlotInfo[slot < 36 ? slot : 0].words);
        gAdapterKind = k;
        gObject.words[0] = shape::P(gAdapterVt);
    }
    std::memcpy(&gObject.words[1], in.data, sizeof(in.data));
    gObject.words[7] = in.refCount;
    std::memset(gHeap, 0x5a, sizeof(gHeap));
    std::memcpy(gShape, in.shape, sizeof(gShape));
    gFrame1 = in.frame1;
    gFrame2 = in.frame2;
    gPoint = in.point;
    gT = in.t;
    for (uint32_t &w : gOutBuf) w = 0xc0c0c0c0u;
    gOutVar = reinterpret_cast<void *>(0x13579bdu);
    gLogN = 0;
    std::memset(gLog, 0, sizeof(gLog));
    gRet = in.ret;
    gAllocs = gAllocSize = gFrees = gFreePtr = gFreeSize = 0;
    std::memset(gFreeSnap, 0, sizeof(gFreeSnap));
    gTraps = 0;
}

Outcome Capture(Ret kind, uint32_t r)
{
    Outcome o;
    std::memset(&o, 0, sizeof(o));
    switch (kind) {
    case kVoid: break;
    case kBool: o.ret = r & 0xff; break;
    case kWord: o.ret = r; break;
    case kOut: o.ret = r == shape::P(&gOutBuf[1]) ? 1 : r | 0x80000000u; break;
    case kSelfOffset: o.ret = Norm(r); break;
    }
    std::memcpy(o.object, &gObject, sizeof(o.object));
    o.object[4] = VtCode(o.object[4]);
    std::memcpy(o.heap, gHeap, sizeof(o.heap));
    o.heap[0] = VtCode(o.heap[0]);
    std::memcpy(o.frames, &gFrame1, sizeof(AffineTransform3));
    std::memcpy(o.frames + sizeof(AffineTransform3), &gFrame2, sizeof(AffineTransform3));
    std::memcpy(o.point, &gPoint, sizeof(o.point));
    o.t = Bits(gT);
    std::memcpy(o.outBuf, gOutBuf, sizeof(o.outBuf));
    o.outVar = Norm(shape::P(gOutVar));
    std::memcpy(o.log, gLog, sizeof(o.log));
    o.logN = gLogN;
    o.allocs = gAllocs;
    o.allocSize = gAllocSize;
    o.frees = gFrees;
    o.freePtr = gFreePtr;
    o.freeSize = gFreeSize;
    std::memcpy(o.freeSnap, gFreeSnap, sizeof(o.freeSnap));
    o.traps = gTraps;
    return o;
}

const HookSpec *HookAt(void *address, difftest::Context &ctx)
{
    for (const HookSpec *h : RegisteredHooks())
        if (ctx.image.At(h->rva) == address) return h;
    return nullptr;
}

Outcome RunSlot(difftest::Context &ctx, unsigned k, unsigned slot, Side side, const Inputs &in)
{
    Place(k, slot, side, in);
    void *original = shape::GameVTable(kVt[k])[slot];
    void *fn = side == kOriginal ? original : side == kOurs ? OurVt(k)[slot] : HookAt(original, ctx)->replacement;
    uint32_t w[3] = {};
    Arguments(slot, in, w);
    uint32_t r;
    if (slot == 2 || slot == 3) {
        AllocatorRedirects redirects;
        r = CallThis(fn, gObject.words, w, kSlotInfo[slot].words);
    } else {
        r = CallThis(fn, gObject.words, w, kSlotInfo[slot].words);
    }
    return Capture(kSlotInfo[slot].ret, r);
}

void Compare(difftest::Context &ctx, const Outcome &a, const Outcome &b, const char *route, unsigned k, unsigned slot,
             unsigned i)
{
    ++ctx.cases;
    if (std::memcmp(&a, &b, sizeof(a)) == 0) return;
    const char *what = a.ret != b.ret                                                ? "return"
                       : std::memcmp(a.object, b.object, sizeof(a.object))           ? "object"
                       : std::memcmp(a.heap, b.heap, sizeof(a.heap))                 ? "heap"
                       : std::memcmp(a.frames, b.frames, sizeof(a.frames))           ? "frames"
                       : std::memcmp(a.outBuf, b.outBuf, sizeof(a.outBuf))           ? "out"
                       : a.outVar != b.outVar                                        ? "out var"
                       : a.logN != b.logN || std::memcmp(a.log, b.log, sizeof(a.log)) ? "calls"
                       : a.traps != b.traps                                          ? "trap"
                                                                                     : "allocation";
    ctx.Fail("%s %s slot %u case %u: %s differs (ret %08x / %08x)", route, kKind[k], slot, i, what, a.ret, b.ret);
}

constexpr unsigned kCasesPerSlot = 300;

// The body each game vtable slot holds (db/classes.jsonl), checked before the slots run: the status entries name these
// RVAs, and the check ties each one to the slot this test exercises. Slot 36 (the deleting destructor) is in kDestroys.
constexpr uint32_t kSlotBody[3][36] = {
    {
        0x18f510, 0x18e960, 0x18f5d0, 0x18e970, 0x18eb60, 0x18fa30, 0x18fab0, 0x18f370, 0x18ef70,
        0x18ef90, 0x18eff0, 0x18efb0, 0x18efd0, 0x18f070, 0x18f050, 0x18ef50, 0x18f030, 0x18f010,
        0x18f090, 0x18f630, 0x18f650, 0x18f6b0, 0x18f670, 0x18f690, 0x18f730, 0x18f710, 0x18f610,
        0x18f6f0, 0x18f6d0, 0x18f750, 0x18ea70, 0x18f5f0, 0x18ed20, 0x18f430, 0x18ea60, 0x18ea60,
    },
    {
        0x18f550, 0x18e960, 0x18f5d0, 0x18e9c0, 0x18eb70, 0x18fa30, 0x18fab0, 0x18f370, 0x18f0d0,
        0x18f0f0, 0x18f150, 0x18f110, 0x18f130, 0x18f1d0, 0x18f1b0, 0x18f0b0, 0x18f190, 0x18f170,
        0x18f1f0, 0x18f790, 0x18f7b0, 0x18f810, 0x18f7d0, 0x18f7f0, 0x18f890, 0x18f870, 0x18f770,
        0x18f850, 0x18f830, 0x18f8b0, 0x18ea70, 0x18f5f0, 0x18ed30, 0x18f430, 0x18ea60, 0x18ea60,
    },
    {
        0x18f590, 0x18e960, 0x18f5d0, 0x18ea10, 0x18eb80, 0x18fb10, 0x18fb90, 0x18f3d0, 0x18f230,
        0x18f250, 0x18f2b0, 0x18f270, 0x18f290, 0x18f330, 0x18f310, 0x18f210, 0x18f2f0, 0x18f2d0,
        0x18f350, 0x18f8f0, 0x18f910, 0x18f970, 0x18f930, 0x18f950, 0x18f9f0, 0x18f9d0, 0x18f8d0,
        0x18f9b0, 0x18f990, 0x18fa10, 0x18ea90, 0x18f5f0, 0x18ee80, 0x18f490, 0x18ea60, 0x18ea60,
    },
};

} // namespace

// Every slot of the three kinds, ours through our vtable against the game's.
DIFF_TEST(CompGeomLinear3_Slots, 0x18f510)
{
    std::mt19937 rng(0x11a3);
    FillBound(gBoundVt, std::make_integer_sequence<unsigned, 40>());
    gBound.vt = gBoundVt;
    HelperPatches helpers;
    unsigned contains[3][2] = {};
    unsigned rayOrigin = 0;
    for (unsigned k = 0; k < 3; ++k)
        for (unsigned slot = 0; slot < 36; ++slot)
            if (shape::GameVTable(kVt[k])[slot] != ctx.image.At(kSlotBody[k][slot]))
                ctx.Fail("%s slot %u is not 0x%x", kKind[k], slot, kSlotBody[k][slot]);
    for (unsigned k = 0; k < 3; ++k)
        for (unsigned slot = 0; slot < 36; ++slot)
            for (unsigned i = 0; i < kCasesPerSlot; ++i) {
                const Inputs in = RandomInputs(k, slot, rng);
                const Outcome a = RunSlot(ctx, k, slot, kOriginal, in);
                const Outcome b = RunSlot(ctx, k, slot, kOurs, in);
                Compare(ctx, a, b, "slots", k, slot, i);
                if (slot == 32) {
                    ++contains[k][a.ret & 1];
                    if (k == 1 && (a.ret & 1) && Bits(in.point.x) == Bits(in.data[0])) ++rayOrigin;
                }
            }
    for (unsigned k = 0; k < 3; ++k) {
        std::printf("  %s contains: %u true, %u false\n", kKind[k], contains[k][1], contains[k][0]);
        if (contains[k][0] < 20 || contains[k][1] < 20) ctx.Fail("%s containment: too few true/false cases", kKind[k]);
    }
    if (rayOrigin < 5) ctx.Fail("ray containment: too few origin cases");
}

// The hook table's replacement at each slot RVA, on an object carrying the game's vtable with a trap in that slot.
DIFF_TEST(CompGeomLinear3_Adapters, 0x18f550)
{
    std::mt19937 rng(0x11a4);
    FillBound(gBoundVt, std::make_integer_sequence<unsigned, 40>());
    gBound.vt = gBoundVt;
    HelperPatches helpers;
    unsigned hooked = 0, unhooked = 0;
    for (unsigned k = 0; k < 3; ++k)
        for (unsigned slot = 0; slot < 36; ++slot) {
            if (!HookAt(shape::GameVTable(kVt[k])[slot], ctx)) {
                // AddRef 0x18e960 (cRZString's, folded) and ShapeData 0x18ea60 (folded across classes) belong elsewhere.
                if (slot != 1 && slot != 34 && slot != 35) ctx.Fail("%s slot %u has no hook", kKind[k], slot);
                ++unhooked;
                continue;
            }
            ++hooked;
            for (unsigned i = 0; i < kCasesPerSlot / 3; ++i) {
                const Inputs in = RandomInputs(k, slot, rng);
                const Outcome a = RunSlot(ctx, k, slot, kOriginal, in);
                const Outcome b = RunSlot(ctx, k, slot, kAdapter, in);
                Compare(ctx, a, b, "adapter", k, slot, i);
                if (b.traps) ctx.Fail("adapter %s slot %u reached its own slot", kKind[k], slot);
            }
        }
    std::printf("  adapters: %u slot entries hooked, %u not ours\n", hooked, unhooked);
}

// ---- lifetime: constructors, factories, destructors, the assignment ----

namespace {

struct Lifetime {
    uint32_t object[16], heap[16], ret, allocs, allocSize, frees, freePtr, freeSize, freeSnap[8];
};
Lifetime CaptureLifetime(uint32_t r)
{
    Lifetime l;
    std::memset(&l, 0, sizeof(l));
    std::memcpy(l.object, &gObject, sizeof(l.object));
    l.object[4] = VtCode(l.object[4]);
    std::memcpy(l.heap, gHeap, sizeof(l.heap));
    l.heap[0] = VtCode(l.heap[0]);
    l.ret = Norm(r);
    l.allocs = gAllocs;
    l.allocSize = gAllocSize;
    l.frees = gFrees;
    l.freePtr = gFreePtr;
    l.freeSize = gFreeSize;
    std::memcpy(l.freeSnap, gFreeSnap, sizeof(l.freeSnap));
    return l;
}
void CompareLifetime(difftest::Context &ctx, const Lifetime &a, const Lifetime &b, const char *what, uint32_t rva,
                     unsigned i)
{
    ++ctx.cases;
    if (std::memcmp(&a, &b, sizeof(a)))
        ctx.Fail("%s 0x%x case %u differs (ret %08x / %08x, object +0 %08x / %08x)", what, rva, i, a.ret, b.ret,
                 a.object[4], b.object[4]);
}

void FillObject(uint32_t vt, const float *data, uint32_t ref)
{
    std::memset(&gObject, 0xa5, sizeof(gObject));
    gObject.words[0] = vt;
    std::memcpy(&gObject.words[1], data, 24);
    gObject.words[7] = ref;
    std::memset(gHeap, 0x5a, sizeof(gHeap));
    gAllocs = gAllocSize = gFrees = gFreePtr = gFreeSize = 0;
    std::memset(gFreeSnap, 0, sizeof(gFreeSnap));
}

struct DestroyCase {
    uint32_t rva;
    unsigned k;
    bool concrete, deleting;
};
const DestroyCase kDestroys[] = {
    {0x18e820, 0, false, false}, {0x18e830, 1, false, false}, {0x18e840, 2, false, false},
    {0x18e850, 0, true, false},  {0x18e860, 1, true, false},  {0x18e870, 2, true, false},
    {0x18e8a0, 0, false, true},  {0x18e8c0, 1, false, true},  {0x18e8e0, 2, false, true},
    {0x18e900, 0, true, true},   {0x18e920, 1, true, true},   {0x18e940, 2, true, true},
};

} // namespace

DIFF_TEST(CompGeomLinear3_Lifetime, 0x18e720)
{
    std::mt19937 rng(0x11a5);
    NaNMode(rng);
    // Constructors (the hooks' placement-new adapters run our constructors).
    struct Ctor {
        uint32_t rva;
        unsigned args;
    };
    const Ctor ctors[] = {{0x18e720, 2}, {0x18e6b0, 2}, {0x18e780, 1}, {0x18e7c0, 2}};
    for (const Ctor &c : ctors) {
        const HookSpec *hook = HookAt(ctx.image.At(c.rva), ctx);
        if (!hook) {
            ctx.Fail("constructor 0x%x has no hook", c.rva);
            continue;
        }
        for (unsigned i = 0; i < 300; ++i) {
            float data[6], init[6];
            NaNMode(rng); // 0x18e6b0 subtracts
            RandomData(1, data, rng);
            RandomData(1, init, rng);
            const uint32_t ref = rng();
            Lifetime out[2];
            for (int side = 0; side < 2; ++side) {
                FillObject(0x77777777u, init, ref);
                static Vec3 a, b;
                a = {data[0], data[1], data[2]};
                b = {data[3], data[4], data[5]};
                const uint32_t w[2] = {c.args == 1 ? shape::P(data) : shape::P(&a), shape::P(&b)};
                void *fn = side == 0 ? ctx.image.At(c.rva) : hook->replacement;
                out[side] = CaptureLifetime(CallThis(fn, gObject.words, w, c.args));
            }
            CompareLifetime(ctx, out[0], out[1], "constructor", c.rva, i);
        }
    }
    // The factories (operator new redirected; the null allocation returns null).
    const uint32_t factories[3] = {0x18ead0, 0x18eb00, 0x18eb30};
    using Factory = void *(__cdecl *)();
    const Factory ours[3] = {reinterpret_cast<Factory>(&cLine3::Create), reinterpret_cast<Factory>(&cRay3::Create),
                             reinterpret_cast<Factory>(&cSegment3::Create)};
    for (unsigned k = 0; k < 3; ++k) {
        Lifetime out[2];
        for (int side = 0; side < 2; ++side) {
            FillObject(0, reinterpret_cast<const float *>(kEdges), 0);
            void *r;
            {
                AllocatorRedirects redirects;
                r = side == 0 ? ctx.Original<Factory>(factories[k])() : ours[k]();
            }
            out[side] = CaptureLifetime(shape::P(r));
        }
        CompareLifetime(ctx, out[0], out[1], "factory", factories[k], 0);
        if (out[0].allocSize != 0x20 || out[0].ret != 0xe1000000u) ctx.Fail("factory 0x%x: unexpected allocation", factories[k]);
        // The real allocator: the complete object (vtable as a code, the data left as allocated is not compared: the
        // factories write only +0 and +0x1c, the mov pair at 0x18eae2 / 0x18eae9).
        auto *a = static_cast<uint32_t *>(ctx.Original<Factory>(factories[k])());
        auto *b = static_cast<uint32_t *>(ours[k]());
        ++ctx.cases;
        if (!a || !b || VtCode(a[0]) != VtCode(b[0]) || VtCode(a[0]) != (0xd0000000u | k) || a[7] != 0 || b[7] != 0)
            ctx.Fail("factory 0x%x with the real allocator differs", factories[k]);
        ::operator delete(a, 0x20);
        ::operator delete(b, 0x20);
    }
    // Destructors and deleting destructors; ours also through our vtables' slot 36.
    for (const DestroyCase &c : kDestroys) {
        const HookSpec *hook = HookAt(ctx.image.At(c.rva), ctx);
        if (!hook) {
            ctx.Fail("destructor 0x%x has no hook", c.rva);
            continue;
        }
        for (unsigned i = 0; i < 8; ++i) {
            float data[6];
            RandomData(c.k, data, rng);
            const uint32_t flags = c.deleting ? (i % 4 == 3 ? rng() : i % 2) : 0;
            const unsigned sides = c.deleting ? 3 : 2;
            Lifetime out[3];
            for (unsigned side = 0; side < sides; ++side) {
                const uint32_t vt = c.concrete ? shape::P(shape::GameVTable(kVt[c.k])) : shape::P(shape::GameVTable(kTemplateVt[c.k]));
                FillObject(vt, data, 1);
                if (side == 2) {
                    (c.concrete ? BuildOurs : BuildOurTemplate)(c.k, gObject.words);
                    std::memcpy(&gObject.words[1], data, 24);
                    gObject.words[7] = 1;
                }
                void *fn = side == 0 ? ctx.image.At(c.rva) : side == 1 ? hook->replacement
                                                                       : (*reinterpret_cast<void ***>(gObject.words))[36];
                uint32_t r;
                {
                    AllocatorRedirects redirects;
                    r = CallThis(fn, gObject.words, &flags, c.deleting ? 1 : 0);
                }
                out[side] = CaptureLifetime(c.deleting ? r : 0);
            }
            CompareLifetime(ctx, out[0], out[1], "destructor adapter", c.rva, i);
            if (c.deleting) CompareLifetime(ctx, out[0], out[2], "deleting destructor (our vtable)", c.rva, i);
        }
    }
    // The assignment 0x18e880 (folded for the three kinds).
    const HookSpec *assign = HookAt(ctx.image.At(0x18e880), ctx);
    if (!assign) ctx.Fail("assignment has no hook");
    for (unsigned i = 0; assign && i < 200; ++i) {
        float data[6], other[6];
        RandomData(i % 3, data, rng);
        RandomData(i % 3, other, rng);
        static uint32_t source[8];
        Lifetime out[2];
        for (int side = 0; side < 2; ++side) {
            FillObject(shape::P(shape::GameVTable(kVt[i % 3])), data, 7);
            source[0] = 0x99999999u;
            std::memcpy(&source[1], other, 24);
            source[7] = 0x31313131u;
            const uint32_t w = shape::P(source);
            void *fn = side == 0 ? ctx.image.At(0x18e880) : assign->replacement;
            out[side] = CaptureLifetime(CallThis(fn, gObject.words, &w, 1));
        }
        CompareLifetime(ctx, out[0], out[1], "assignment", 0x18e880, i);
    }
}

// The containment helpers called directly (each hooked to ours): the line's 0x18ec30, the segment's 0x18ee90 and the
// ray's 0x1977d0.
DIFF_TEST(CompGeomLinear3_Contains, 0x18ec30)
{
    std::mt19937 rng(0x11a6);
    unsigned outcomes[3][2] = {};
    auto line = ctx.Original<uint32_t(__thiscall *)(const void *, const Vec3 *)>(0x18ec30);
    auto segment = ctx.Original<uint32_t(__thiscall *)(const void *, const Vec3 *)>(0x18ee90);
    auto ahead = ctx.Original<uint32_t(__cdecl *)(const Vec3 *, const Vec3 *)>(0x1977d0);
    for (unsigned i = 0; i < 30000; ++i) {
        const unsigned k = i % 3;
        NaNMode(rng);
        float data[6];
        RandomData(k, data, rng);
        const Vec3 p = PointFor(k, data, rng);
        bool a, b;
        if (k == 0) {
            Line3 shapeData;
            std::memcpy(&shapeData, data, 24);
            a = uint8_t(line(&shapeData, &p)) != 0;
            b = shapeData.Contains(p);
        } else if (k == 2) {
            Segment3 shapeData;
            std::memcpy(&shapeData, data, 24);
            a = uint8_t(segment(&shapeData, &p)) != 0;
            b = shapeData.Contains(p);
        } else {
            const Vec3 v{p.x - data[0], p.y - data[1], p.z - data[2]}, d{data[3], data[4], data[5]};
            a = uint8_t(ahead(&v, &d)) != 0;
            b = AheadAlong(&v, &d);
        }
        ++ctx.cases;
        ++outcomes[k][a];
        if (a != b)
            ctx.Fail("%s containment case %u: %d / %d (%08x %08x %08x %08x %08x %08x p %08x %08x %08x)", kKind[k], i, int(a),
                     int(b), Bits(data[0]), Bits(data[1]), Bits(data[2]), Bits(data[3]), Bits(data[4]), Bits(data[5]),
                     Bits(p.x), Bits(p.y), Bits(p.z));
    }
    for (unsigned k = 0; k < 3; ++k) {
        std::printf("  %s: %u true, %u false\n", kKind[k], outcomes[k][1], outcomes[k][0]);
        if (outcomes[k][0] < 100 || outcomes[k][1] < 100) ctx.Fail("%s containment: too few true/false cases", kKind[k]);
    }
}
