
// Differential tests for the frustum shape and its cull tests (lane PV, src/compgeom/Frustum.cpp).
// CompGeomFrustum_Slots: shapes with random planes, then random slots through each object's own vtable. Two sides: the
// game's factory object (the game's slots) and our Create's object (our overrides, through our vtable; the generic slots
// are not hooked). Results, out parameters, the whole object (vtable code, planes, count) and the allocation log (every
// operator new / delete the slots make) are compared, Clone's copies and the non-deleting destructor included.
// CompGeomFrustum_Cull: FromView, the three Classify tests and the two plane helpers, original against the hook's
// replacement on random inputs (frames of every flag kind, masks with high bits, NaN pairs on one axis with differing
// payloads), with a bisection of the answer's flip point along a coordinate on each side and a probe of the neighbouring
// floats. Both at the harness's 53-bit x87 precision and at the game's 24-bit.
// Bodies run on both sides: 0x197740 (Create) 0x197780 0x1972b0 0x1977b0 0x1972c0 0x197770 0x18ea60 0x197290 0x197280
// 0x1975a0 0x197520 0x1974a0 0x197330 0x1a28c0 0x1a2810.

#include "compgeom/Frustum.h"
#include "difftest.h"
#include "hook/hooks.h"

#include "code_patch.h"
#include "shape.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
constexpr unsigned kSides = 2; // the game's, ours
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

// ---- random numbers ----

constexpr uint32_t kEdges[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x00000001, 0x80000001, 0x007fffff, 0x00800000, 0x7f7fffff,
    0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001, 0x7fa00000, 0x3f000000, 0x40400000, 0x34000000,
};
int gNaNBudget;

float NaNOf(std::mt19937 &rng)
{
    return Float((rng() & 0x80000000u) | 0x7f800000u | (rng() & 0x7fffffu) | 1u); // any payload, signalling or quiet
}
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
        f = NaNOf(rng);
    }
    return f;
}
// A positive-ish extent most of the time.
float Extent(std::mt19937 &rng)
{
    return rng() % 4 ? std::fabs(Num(rng)) : Num(rng);
}
// A moderate value, for the boxes that should land closeIn the frustum.
float Near(std::mt19937 &rng, float scale)
{
    return (float(int(rng() % 4001) - 2000) / 1000.0f) * scale;
}
void RandomFrame(AffineTransform3 &f, std::mt19937 &rng)
{
    static const uint32_t kFlags[] = {0, 1, 2, 4, 6, 8, 0x10, 0x12, 0x14, 0x1e, 0x1f};
    f.mUnknown0 = rng() % 8 ? kFlags[rng() % std::size(kFlags)] : rng();
    for (float &m : f.mMatrix) m = rng() % 3 ? Num(rng) : float(rng() % 2);
    for (float &o : f.mOffset) o = Num(rng);
}
// A frame that mostly keeps the box in view: rotation-ish matrices with moderate values.
void NearFrame(AffineTransform3 &f, std::mt19937 &rng, float scale)
{
    static const uint32_t kFlags[] = {0, 1, 2, 3, 4, 6, 8, 0x10, 0x12, 0x14, 0x1e, 0x1f};
    f.mUnknown0 = kFlags[rng() % std::size(kFlags)];
    for (unsigned i = 0; i < 9; ++i) f.mMatrix[i] = (i % 4 == 0 ? 1.0f : 0.0f) + float(int(rng() % 801) - 400) / 1000.0f;
    for (float &o : f.mOffset) o = Near(rng, scale);
}

// ---- the shape under test ----

constexpr uint32_t kCreate = 0x197740;
constexpr uint32_t kGameVt[2] = {0xda1f18, 0xda1ef4}; // concrete, bound
constexpr uint32_t kObjectBytes = 0x68, kCountOffset = 0x64, kDataBytes = 0x60;
constexpr uint32_t kClassId = 0x0a6e6298;

void **gOurVt[2]; // cFrustumShape, cFrustumBound
void ReadOurVtables()
{
    alignas(8) static uint8_t buffer[0x70];
    auto *s = ::new (buffer) cFrustumShape;
    gOurVt[0] = *reinterpret_cast<void ***>(s);
    s->~cFrustumShape();
    auto *b = ::new (buffer) cFrustumBound;
    gOurVt[1] = *reinterpret_cast<void ***>(b);
    b->~cFrustumBound();
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

void *gObject; // the shape under test (the current side's)
uint32_t gOut; // QueryInterface's and Clone's out parameter

constexpr uint32_t kIds[] = {1, cIShape::kInterfaceId, cIBound::kInterfaceId, kClassId, 0x3ce57cc0, 0, 0x0a6e6299, 2};

uint32_t CallSlot(void *object, unsigned slot, const uint32_t *w, unsigned n)
{
    void *fn = (*static_cast<void ***>(object))[slot];
    switch (n) {
    case 0: return reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(object);
    case 1: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(object, w[0]);
    default: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(fn)(object, w[0], w[1]);
    }
}

void Op(uint32_t op)
{
    Log(0x0b000000u | op);
}

constexpr unsigned kCasesPerShape = 500;

void RandomPlanes(std::mt19937 &rng)
{
    gNaNBudget = rng() % 3 == 0 ? 2 : 0;
    float data[24];
    for (float &f : data) f = Num(rng);
    std::memcpy(static_cast<uint8_t *>(gObject) + 4, data, sizeof(data));
}

void RunSlots(void *(__cdecl *create)(), uint32_t seed)
{
    std::mt19937 rng(seed);
    gObject = create();
    Op(0);
    CallSlot(gObject, 1, nullptr, 0); // AddRef
    DumpShape(gObject);
    for (unsigned i = 0; i < kCasesPerShape; ++i) {
        RandomPlanes(rng);
        static const unsigned kSlots[] = {0, 0, 0, 1, 3, 3, 3, 4, 5, 6};
        const unsigned slot = kSlots[rng() % std::size(kSlots)];
        uint32_t w[2] = {};
        unsigned n = 0;
        gOut = 0x0bad0bad;
        if (slot == 0 || slot == 3) {
            w[0] = rng() % 8 ? kIds[rng() % std::size(kIds)] : rng();
            w[1] = rng() % 12 ? shape::P(&gOut) : 0;
            n = 2;
        }
        Op(0x100000u | slot << 12 | (i & 0xfff));
        const uint32_t r = CallSlot(gObject, slot, w, n);
        if (slot == 5 || slot == 6) Log(r - shape::P(gObject));
        else if (slot == 0 || slot == 3) Log(r & 0xff);
        else Log(r);
        if (slot == 1) Log(CallSlot(gObject, 2, nullptr, 0)); // back to the one reference
        DumpShape(gObject);
        if (slot == 0 || slot == 3) {
            if (gOut == 0x0bad0bad) {
                Log(gOut);
            } else {
                const uint32_t got = gOut;
                Log(got == shape::P(gObject) ? 0xe0000000u : BlockCode(got));
                if (slot == 3 && (r & 0xff)) {
                    DumpShape(reinterpret_cast<void *>(uintptr_t(got)));
                    Log(CallSlot(reinterpret_cast<void *>(uintptr_t(got)), 2, nullptr, 0)); // the copy's one reference
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
    // The destructor slot without the delete bit: the vtable falls back to the bound's, nothing is freed; then deleted.
    void *second = create();
    DumpShape(second);
    const uint32_t keep[1] = {0};
    CallSlot(second, 7, keep, 1);
    DumpShape(second);
    const uint32_t del[1] = {1};
    CallSlot(second, 7, del, 1);
    Op(4);
}

bool CompareLogs(difftest::Context &ctx, const char *test, uint32_t seed)
{
    ++ctx.cases;
    const std::vector<uint32_t> &a = gSides[0].words, &b = gSides[1].words;
    if (gSides[0].overflow || gSides[1].overflow) {
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
    ctx.Fail("%s seed %u: logs differ at word %zu (%zu after op %08x = slot %u; sizes %zu / %zu): %08x / %08x", test, seed, i,
             i - at, op, (op >> 12) & 0xff, a.size(), b.size(), i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
    return false;
}

void RunAll(difftest::Context &ctx, const char *label, unsigned firstSeed, unsigned lastSeed)
{
    void *(__cdecl * creates[kSides])();
    creates[0] = ctx.Original<void *(__cdecl *)()>(kCreate);
    if (!PickOurs(ctx, kCreate, creates[1])) return;
    unsigned failures = 0;
    for (uint32_t seed = firstSeed; seed <= lastSeed && failures < 4; ++seed) {
        for (unsigned side = 0; side < kSides; ++side) {
            gSides[side].Reset();
            gCur = &gSides[side];
            RunSlots(creates[side], seed);
            gCur = nullptr;
        }
        if (!CompareLogs(ctx, label, seed)) ++failures;
    }
}

// ---- the cull tests ----

using FromViewFn = void(__thiscall *)(Frustum *, const float *);
using AtFn = int(__thiscall *)(const Frustum *, uint8_t *, const Vec3 *, const Vec3 *);
using InFrameFn = int(__thiscall *)(const Frustum *, uint8_t *, const Vec3 *, const AffineTransform3 *);
using BoxFn = int(__thiscall *)(const Frustum *, uint8_t *, const BoundBox *, const AffineTransform3 *);
using PlaneAtFn = int(__cdecl *)(const float *, const Vec3 *, const Vec3 *);
using PlaneFrameFn = int(__cdecl *)(const float *, const Vec3 *, const AffineTransform3 *);

struct Side {
    FromViewFn from;
    AtFn at;
    InFrameFn inFrame;
    BoxFn box;
    PlaneAtFn planeAt;
    PlaneFrameFn planeFrame;
};
Side gCull[2];

struct Input {
    float matrix[16];
    Frustum frustum;
    float plane[4];
    Vec3 half, centre;
    AffineTransform3 frame;
    BoundBox box;
    uint32_t maskWord; // the mask byte and the bytes after it
};

// A frustum around the origin: the planes of a cube of half size `size` (positive outside), perturbed on some cases.
void CubeFrustum(Frustum &f, std::mt19937 &rng, float size)
{
    static const float kDir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (unsigned i = 0; i < 6; ++i) {
        for (unsigned k = 0; k < 3; ++k) f.plane[i][k] = kDir[i][k];
        f.plane[i][3] = -size;
        if (rng() % 3 == 0)
            for (unsigned k = 0; k < 4; ++k) f.plane[i][k] += Near(rng, 0.2f);
    }
}

void RandomInput(Input &in, std::mt19937 &rng)
{
    std::memset(&in, 0, sizeof(in));
    gNaNBudget = rng() % 2 == 0 ? 1 + rng() % 5 : 0; // several NaNs: which payload an operation keeps shows
    const bool closeIn = rng() % 2 == 0;
    const float size = 0.5f + float(rng() % 40) / 8.0f;
    for (float &m : in.matrix) m = Num(rng);
    if (closeIn) {
        CubeFrustum(in.frustum, rng, size);
        for (float &p : in.plane) p = Near(rng, 1.0f);
        for (float &h : reinterpret_cast<float(&)[3]>(in.half)) h = std::fabs(Near(rng, size * 0.4f));
        for (float &c : reinterpret_cast<float(&)[3]>(in.centre)) c = Near(rng, size * 1.6f);
        NearFrame(in.frame, rng, size * 1.4f);
        for (float &c : reinterpret_cast<float(&)[3]>(in.box.min)) c = Near(rng, size * 1.5f);
        for (unsigned k = 0; k < 3; ++k) (&in.box.max.x)[k] = (&in.box.min.x)[k] + std::fabs(Near(rng, size * 0.5f));
        if (rng() % 4 == 0) // a box as a view of the same numbers other tests give: min above max too
            for (unsigned k = 0; k < 3; ++k) (&in.box.max.x)[k] = Near(rng, size);
    } else {
        for (float &f : reinterpret_cast<float(&)[24]>(in.frustum)) f = Num(rng);
        for (float &p : in.plane) p = Num(rng);
        for (float &h : reinterpret_cast<float(&)[3]>(in.half)) h = Extent(rng);
        for (float &c : reinterpret_cast<float(&)[3]>(in.centre)) c = Num(rng);
        RandomFrame(in.frame, rng);
        for (float &c : reinterpret_cast<float(&)[6]>(in.box)) c = Num(rng);
    }
    if (rng() % 4 == 0) { // a plane that is an axis: the sweep's flip points are exact
        const unsigned axis = rng() % 3;
        for (unsigned k = 0; k < 3; ++k) in.plane[k] = k == axis ? (rng() % 2 ? 1.0f : -1.0f) : 0.0f;
        in.plane[3] = Near(rng, 2.0f);
    }
    if (rng() % 5 == 0) { // the same axis NaN in the plane, the extent and the position, payloads differing
        const unsigned axis = rng() % 3;
        in.plane[axis] = NaNOf(rng);
        if (rng() % 2) (&in.half.x)[axis] = NaNOf(rng);
        if (rng() % 2) (&in.centre.x)[axis] = NaNOf(rng);
        if (rng() % 2) in.frame.mOffset[axis] = NaNOf(rng);
        if (rng() % 2) in.plane[(axis + 1) % 3] = NaNOf(rng);
        if (rng() % 2) in.plane[3] = NaNOf(rng);
        for (unsigned i = 0; i < 6; ++i) {
            if (rng() % 3 == 0) in.frustum.plane[i][axis] = NaNOf(rng);
            if (rng() % 4 == 0) in.frustum.plane[i][(axis + 1) % 3] = NaNOf(rng);
        }
    }
    if (rng() % 5 == 0) { // the matrix: two NaNs that meet in one plane's sums
        const unsigned k = rng() % 4;
        const unsigned row = rng() % 3;
        in.matrix[row * 4 + k] = NaNOf(rng);
        in.matrix[12 + k] = NaNOf(rng);
        if (rng() % 2) in.matrix[((row + 1) % 3) * 4 + k] = NaNOf(rng);
    }
    switch (rng() % 6) {
    case 0: in.maskWord = 0; break;
    case 1: in.maskWord = 0x3f; break;
    case 2: in.maskWord = 0x3f | (rng() & 0xc0); break;
    case 3: in.maskWord = rng() & 0x3f; break;
    case 4: in.maskWord = (0x3f & ~(1u << (rng() % 6))) | (rng() % 3 == 0 ? 0xc0 : 0); break;
    default: in.maskWord = rng(); break;
    }
}

// The answer's flip point along the coordinate `x` (bisected over the non-negative floats below 1e7, then its sign), with
// the answers of the neighbouring floats: each side finds its own point from its own answers.
template <class Call> void Sweep(float &x, Call call)
{
    const float saved = x;
    for (int sign = 0; sign < 2; ++sign) {
        const uint32_t signBit = sign ? 0x80000000u : 0u;
        x = Float(signBit);
        const int low = call();
        x = Float(signBit | Bits(1.0e7f));
        const int high = call();
        Log(uint32_t(low));
        Log(uint32_t(high));
        if (low == high) continue;
        uint32_t lo = 0, hi = Bits(1.0e7f);
        while (hi - lo > 1) {
            const uint32_t mid = lo + (hi - lo) / 2;
            x = Float(signBit | mid);
            if (call() == low) lo = mid;
            else hi = mid;
        }
        Log(hi);
        for (int k = -3; k <= 3; ++k) {
            x = Float(signBit | uint32_t(int(hi) + k));
            Log(uint32_t(call()));
        }
    }
    x = saved;
}

// Everything the cull code does with one input on one side, into the log.
void RunCull(const Side &s, const Input &original, std::mt19937 &rng)
{
    Input in = original;
    Frustum made;
    std::memset(&made, 0x5c, sizeof(made));
    s.from(&made, in.matrix);
    DumpBytes(reinterpret_cast<const uint8_t *>(&made), sizeof(made));

    // The plane helpers.
    Log(uint32_t(s.planeAt(in.plane, &in.half, &in.centre)));
    Log(uint32_t(s.planeFrame(in.plane, &in.half, &in.frame)));
    const unsigned axis = rng() % 3;
    Sweep((&in.centre.x)[axis], [&] { return s.planeAt(in.plane, &in.half, &in.centre); });
    Sweep(in.frame.mOffset[axis], [&] { return s.planeFrame(in.plane, &in.half, &in.frame); });
    Sweep((&in.half.x)[axis], [&] { return s.planeAt(in.plane, &in.half, &in.centre); });

    // The classify tests on the frustum from the input, with the mask byte as given.
    uint8_t mask[4];
    const auto reset = [&] { std::memcpy(mask, &in.maskWord, 4); };
    reset();
    Log(uint32_t(s.at(&in.frustum, mask, &in.half, &in.centre)));
    DumpBytes(mask, 4);
    reset();
    Log(uint32_t(s.inFrame(&in.frustum, mask, &in.half, &in.frame)));
    DumpBytes(mask, 4);
    reset();
    Log(uint32_t(s.box(&in.frustum, mask, &in.box, &in.frame)));
    DumpBytes(mask, 4);
    // ...and on the frustum FromView made, from a clear mask.
    in.maskWord = 0;
    reset();
    Log(uint32_t(s.at(&made, mask, &in.half, &in.centre)));
    DumpBytes(mask, 4);
    reset();
    Log(uint32_t(s.box(&made, mask, &in.box, &in.frame)));
    DumpBytes(mask, 4);
    // The first plane that outs the box moves with the mask: the same box under each single-plane mask.
    for (unsigned plane = 0; plane < 6; ++plane) {
        in.maskWord = 0x3f & ~(1u << plane);
        reset();
        Log(uint32_t(s.inFrame(&in.frustum, mask, &in.half, &in.frame)));
        Log(mask[0]);
    }
    // The flip point of the whole test along a coordinate.
    in.maskWord = 0;
    Sweep((&in.centre.x)[axis], [&] {
        reset();
        return s.at(&in.frustum, mask, &in.half, &in.centre);
    });
    Sweep(in.frame.mOffset[axis], [&] {
        reset();
        return s.inFrame(&in.frustum, mask, &in.half, &in.frame);
    });
    Sweep((&in.box.max.x)[axis], [&] {
        reset();
        return s.box(&in.frustum, mask, &in.box, &in.frame);
    });
    DumpBytes(reinterpret_cast<const uint8_t *>(&in), sizeof(in));
}

void RunCullSeeds(difftest::Context &ctx, const char *label, unsigned firstSeed, unsigned lastSeed, unsigned casesPerSeed)
{
    unsigned failures = 0;
    for (uint32_t seed = firstSeed; seed <= lastSeed && failures < 4; ++seed) {
        for (unsigned i = 0; i < casesPerSeed && failures < 4; ++i) {
            std::mt19937 gen(seed * 7919u + i);
            Input in;
            RandomInput(in, gen);
            for (unsigned side = 0; side < kSides; ++side) {
                gSides[side].Reset();
                gCur = &gSides[side];
                std::mt19937 rng(seed * 104729u + i);
                RunCull(gCull[side], in, rng);
                gCur = nullptr;
            }
            ++ctx.cases;
            const std::vector<uint32_t> &a = gSides[0].words, &b = gSides[1].words;
            size_t at = 0;
            const size_t n = a.size() < b.size() ? a.size() : b.size();
            while (at < n && a[at] == b[at]) ++at;
            if (at != n || a.size() != b.size()) {
                ctx.Fail("%s seed %u case %u: logs differ at word %zu (sizes %zu / %zu): %08x / %08x", label, seed, i, at,
                         a.size(), b.size(), at < a.size() ? a[at] : 0, at < b.size() ? b[at] : 0);
                ++failures;
            }
        }
    }
}

bool PickCull(difftest::Context &ctx)
{
    gCull[0] = {ctx.Original<FromViewFn>(0x1975a0), ctx.Original<AtFn>(0x197520), ctx.Original<InFrameFn>(0x1974a0),
                ctx.Original<BoxFn>(0x197330), ctx.Original<PlaneAtFn>(0x1a28c0), ctx.Original<PlaneFrameFn>(0x1a2810)};
    return PickOurs(ctx, 0x1975a0, gCull[1].from) && PickOurs(ctx, 0x197520, gCull[1].at) &&
           PickOurs(ctx, 0x1974a0, gCull[1].inFrame) && PickOurs(ctx, 0x197330, gCull[1].box) &&
           PickOurs(ctx, 0x1a28c0, gCull[1].planeAt) && PickOurs(ctx, 0x1a2810, gCull[1].planeFrame);
}

} // namespace

DIFF_TEST(CompGeomFrustum_Slots, 0x197740)
{
    Fixture fixture(ctx);
    ReadOurVtables();
    RunAll(ctx, "slots", 1, 40);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    RunAll(ctx, "slots at 24-bit", 101, 120);
}

DIFF_TEST(CompGeomFrustum_Cull, 0x1975a0)
{
    if (!PickCull(ctx)) return;
    for (SideLog &s : gSides) s.Reset();
    RunCullSeeds(ctx, "cull", 1, 40, 250);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    RunCullSeeds(ctx, "cull at 24-bit", 101, 120, 250);
}
