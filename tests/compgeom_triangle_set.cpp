// Differential tests for nGZCompGeom's triangle sets (lane PC, src/compgeom/TriangleSet.h).
// - CompGeomTriangleSet_Data: a shape from the game's factory driven by the game's set functions (Resize, SetVertex,
//   SetTriangle, Build, Refit) against a shape from our Create driven by the hook table's replacements, op for op: after
//   each op the whole shape (vtables as codes, every vector's size, capacity and contents) and every allocation and free
//   (the allocator service's and operator new's, by size and block) since the last.
// - CompGeomTriangleSet_Slots: filled shapes (as above), then random slots through each object's own vtable: the game's
//   factory's object against our Create's. The intersection tests the slots forward to are stand-ins that log their
//   arguments (frames the slot built by value); the fake bound and linear log the calls the slots make on them. The box
//   slots run the game's cuboid and rigid-transform helpers for real. Clone's copies are dumped and released; at the
//   end the shape is released (the destructors, the frees).
// - CompGeomTriangleSet_Boxes: BoundBox::Grow 0x19d430, Cuboid::BoxAt 0x1902e0 (the hook table's replacements) and
//   TriangleBounds (0x1a26d0, in effect through the tree code), with NaNs.
//
// Functions in effect through their callers, reached as follows (the game's side runs them, ours folds them into our
// tree, builder and vector code):
// - by Build (Data): the tree's Build 0x195c80 and Reset 0x1965e0; the builder's constructor 0x1954e0, Reserve
//   0x196650, AddTriangles 0x195d90, Split 0x196060, CopyInto 0x196310, NewNode 0x196480; the node constructors
//   0x195530 / 0x195550 and their fills 0x195480 / 0x1954b0; the vectors' clear 0x196d30 / 0x196d40, reserve 0x195010,
//   insert 0x195180 / 0x195270 and resize 0x1971f0;
// - by Refit (Data): the tree's Refit 0x196e20;
// - by Clone (Slots): the vertex and node vector copies 0x1957e0 / 0x195900;
// - by Release (Slots, and Data's end): the deleting destructor 0x195ad0, the destructor 0x1955f0, the tree's body
//   0x195670. The tree's deleting destructor 0x195a40 (its vtable's only slot) is not reached: the tree is always a
//   member, destroyed by the set's destructor.

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
    uint32_t fills = 0;
    bool overflow = false;
    void Reset()
    {
        if (words.capacity() == 0) words.reserve(1u << 20);
        if (blocks.capacity() == 0) blocks.reserve(1u << 16);
        words.clear();
        blocks.clear();
        fills = 0;
        overflow = false;
    }
};
SideLog gSides[2];
SideLog *gCur; // where events go (null: nowhere)
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

// A pointer into a block allocated while logging, as a code: the block's number and the offset; others unchanged.
uint32_t BlockCode(uint32_t w)
{
    if (!gCur || !w) return w;
    const std::vector<Block> &b = gCur->blocks;
    for (size_t i = b.size(); i-- > 0;)
        if (w >= b[i].p && w - b[i].p < (b[i].size ? b[i].size : 1) && w - b[i].p < 0x10000)
            return 0xc0000000u | uint32_t(i) << 16 | (w - b[i].p);
    return w;
}

// Every block handed out while logging is filled with a pattern (the same on both sides when the allocations match;
// no byte pattern makes a NaN or an infinity), as the game's heap would hold old data.
void *NewBlock(uint32_t tag, size_t n)
{
    uint8_t *p = static_cast<uint8_t *>(std::malloc(n ? n : 1));
    if (Logging()) {
        SideLog &s = *gCur;
        for (size_t i = 0; i < n; ++i) p[i] = uint8_t(0x40 | ((s.fills + i) & 0xf));
        ++s.fills;
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

// The allocator service the sets' vectors use (cache 0x10c95d4, guard 0x10c95d8 as after its first use): slot 3
// allocates, slot 4 frees.
uint32_t __fastcall ServiceNop(void *, void *) { return 0; }
void *__fastcall ServiceAlloc(void *, void *, uint32_t n) { return NewBlock(0xa1000000u, n); }
void __fastcall ServiceFree(void *, void *, void *p) { FreeBlock(0xf1000000u, p); }
void *gServiceVTable[5] = {reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop),
                           reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceAlloc),
                           reinterpret_cast<void *>(&ServiceFree)};
void *gServiceObject = gServiceVTable;

// operator new and delete, the game's (0x824632 / 0x824624, whose CRT the harness never initialises) and ours.
void *__cdecl HookNew(size_t n) { return NewBlock(0xa2000000u, n); }
void __cdecl HookDelete(void *p)
{
    if (p) FreeBlock(0xf2000000u, p);
}
void __cdecl HookSizedDelete(void *p, size_t) { HookDelete(p); }

struct Fixture {
    void **cache;
    int32_t *guard;
    void *savedCache;
    int32_t savedGuard;
    shape::CodePatch gameNew, gameDelete, ourNew, ourDelete, ourSizedDelete;
    explicit Fixture(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int32_t>(0x10c95d8)), savedCache(*cache),
          savedGuard(*guard), gameNew(0x824632, reinterpret_cast<void *>(&HookNew)),
          gameDelete(0x824624, reinterpret_cast<void *>(&HookDelete)),
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
        gSides[0].Reset();
        gSides[1].Reset();
    }
    ~Fixture()
    {
        gCur = nullptr;
        *cache = savedCache;
        *guard = savedGuard;
    }
};

// ---- the two sides ----

const HookSpec *HookAt(void *address, difftest::Context &ctx)
{
    for (const HookSpec *h : RegisteredHooks())
        if (ctx.image.At(h->rva) == address) return h;
    return nullptr;
}

struct Api {
    void *(__cdecl *create)();
    void(__thiscall *resize)(void *, uint32_t, uint32_t);
    void(__thiscall *setVertex)(void *, uint32_t, const Vec3 *);
    void(__thiscall *setTriangle)(void *, int32_t, uint32_t, uint32_t, uint32_t);
    void(__thiscall *build)(void *);
    void(__thiscall *refit)(void *, uint32_t);
};

constexpr uint32_t kCreate = 0x196560, kResize = 0x195af0, kSetVertex = 0x196d00, kSetTriangle = 0x196cc0,
                   kBuild = 0x196300, kRefit = 0x1970f0;

template <class Fn> bool Pick(difftest::Context &ctx, bool ours, uint32_t rva, Fn &fn)
{
    void *at = ctx.image.At(rva);
    if (ours) {
        const HookSpec *h = HookAt(at, ctx);
        if (!h) {
            ctx.Fail("no hook at 0x%x", rva);
            return false;
        }
        at = h->replacement;
    }
    fn = reinterpret_cast<Fn>(at);
    return true;
}
bool ApiFor(difftest::Context &ctx, bool ours, Api &api)
{
    return Pick(ctx, ours, kCreate, api.create) && Pick(ctx, ours, kResize, api.resize) &&
           Pick(ctx, ours, kSetVertex, api.setVertex) && Pick(ctx, ours, kSetTriangle, api.setTriangle) &&
           Pick(ctx, ours, kBuild, api.build) && Pick(ctx, ours, kRefit, api.refit);
}

// ---- vtables as codes ----

void **gOurVt[3]; // cTriangleSetShape, cTriangleSetBound, cAABBNoLeafTree
void ReadOurVtables()
{
    alignas(8) static uint8_t buffer[0x30];
    auto *s = ::new (buffer) cTriangleSetShape;
    gOurVt[0] = *reinterpret_cast<void ***>(s);
    gOurVt[2] = *reinterpret_cast<void ***>(buffer + 0x1c);
    s->~cTriangleSetShape();
    auto *b = ::new (buffer) cTriangleSetBound;
    gOurVt[1] = *reinterpret_cast<void ***>(b);
    b->~cTriangleSetBound();
}
constexpr uint32_t kGameVt[3] = {0xda1e0c, 0xda1d5c, 0xda1d54};
uint32_t VtCode(uint32_t w)
{
    for (unsigned i = 0; i < 3; ++i)
        if (w == shape::P(shape::GameVTable(kGameVt[i])) || w == shape::P(gOurVt[i])) return 0xd0000000u | i;
    return w;
}

// ---- dumps ----

void DumpBytes(const uint8_t *p, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, p + i, bytes - i < 4 ? bytes - i : 4);
        Log(w);
    }
}
// A vector (begin, end, capacity end): its block, size and capacity in bytes, and its contents.
void DumpVector(const uint8_t *v)
{
    const uint32_t b = Word(v, 0), e = Word(v, 4), c = Word(v, 8);
    Log(BlockCode(b));
    Log(e - b);
    Log(c - b);
    if (b && e >= b && e - b < (1u << 22)) DumpBytes(reinterpret_cast<const uint8_t *>(uintptr_t(b)), e - b);
}
// The shape: vtable, the set's three vectors, the tree's vtable, the count.
void DumpShape(const void *object)
{
    const uint8_t *o = static_cast<const uint8_t *>(object);
    Log(0x5ea00000u);
    Log(VtCode(Word(o, 0)));
    DumpVector(o + 4);
    DumpVector(o + 0x10);
    Log(VtCode(Word(o, 0x1c)));
    DumpVector(o + 0x20);
    Log(Word(o, 0x2c));
}

uint32_t CallSlot(void *object, unsigned slot, const uint32_t *w, unsigned n)
{
    void *fn = (*static_cast<void ***>(object))[slot];
    switch (n) {
    case 0: return reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(object);
    case 1: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(object, w[0]);
    case 2: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(fn)(object, w[0], w[1]);
    default:
        return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(fn)(object, w[0], w[1],
                                                                                                 w[2]);
    }
}

// The first difference of the two sides' logs, with the last op marker before it.
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
    ctx.Fail("%s seed %u: logs differ at word %zu (%zu after op %08x; sizes %zu / %zu): %08x / %08x", test, seed, i,
             i - at, op, a.size(), b.size(), i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
    return false;
}

// ---- inputs ----

constexpr uint32_t kEdges[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x00000001, 0x80000001, 0x007fffff, 0x00800000, 0x7f7fffff,
    0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001, 0x7fa00000, 0x3f000000, 0x40400000, 0x34000000,
};
int gNaNBudget;

// Small integers and decimals (ties and repeats, which the tree's split sees), wide values, edges, any bits.
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
    case 10: f = Float(rng() & 0x807fffffu); break; // denormal
    default: f = float(int(rng() % 5)) * 0.5f; break;
    }
    if (std::isnan(f)) {
        if (gNaNBudget <= 0) return 1.5f;
        --gNaNBudget;
    }
    return f;
}
// A case's NaN mode: none (most cases), or up to `most` NaNs among the inputs.
void NaNMode(std::mt19937 &rng, int most)
{
    gNaNBudget = rng() % 3 == 0 ? most : 0;
}
Vec3 RandomVec(std::mt19937 &rng)
{
    return Vec3{Num(rng), Num(rng), Num(rng)};
}

// Fills a created shape with `vertices` and `triangles` (corners below `vertices`) and builds it.
void Fill(const Api &api, void *object, std::mt19937 &rng, uint32_t vertices, uint32_t triangles)
{
    void *set = static_cast<uint8_t *>(object) + 4;
    api.resize(set, vertices, triangles);
    for (uint32_t i = 0; i < vertices; ++i) {
        const Vec3 p = RandomVec(rng);
        api.setVertex(set, i, &p);
    }
    for (uint32_t k = 0; k < triangles; ++k)
        api.setTriangle(set, int32_t(k), rng() % vertices, rng() % vertices, rng() % vertices);
    api.build(set);
}

// ---- the data test ----

void Op(uint32_t op)
{
    Log(0x0b000000u | op);
}

void RunData(const Api &api, uint32_t seed)
{
    std::mt19937 rng(seed);
    NaNMode(rng, 2);
    void *object = api.create();
    Op(0);
    DumpShape(object);
    CallSlot(object, 1, nullptr, 0); // AddRef
    void *set = static_cast<uint8_t *>(object) + 4;
    uint32_t vertices = 0;
    const unsigned rounds = 1 + rng() % 3;
    for (unsigned round = 0; round < rounds; ++round) {
        const uint32_t nv = 1 + rng() % (rng() % 4 ? 24 : 400);
        const uint32_t nt = 1 + rng() % (rng() % 4 ? 20 : 260);
        Op(0x100 | round);
        api.resize(set, nv, nt);
        DumpShape(object);
        // New vertices set; old ones too in the first round, else some.
        for (uint32_t i = 0; i < nv; ++i)
            if (round == 0 || i >= vertices || rng() % 2) {
                const Vec3 p = RandomVec(rng);
                // The index is 16 bits: the stack word's upper half is not read.
                api.setVertex(set, i | (rng() % 4 == 0 ? rng() << 16 : 0), &p);
            }
        Op(0x200 | round);
        DumpShape(object);
        for (uint32_t k = 0; k < nt; ++k) {
            const uint32_t garbage = rng() % 4 == 0 ? rng() << 16 : 0;
            api.setTriangle(set, int32_t(k), (rng() % nv) | garbage, rng() % nv, (rng() % nv) | garbage);
        }
        Op(0x300 | round);
        DumpShape(object);
        api.build(set);
        Op(0x400 | round);
        DumpShape(object);
        if (rng() % 3 == 0) {
            api.build(set); // again, over a tree of the right size
            Op(0x500 | round);
            DumpShape(object);
        }
        if (rng() % 2) {
            for (unsigned m = 1 + rng() % 8; m-- > 0;) {
                const Vec3 p = RandomVec(rng);
                api.setVertex(set, rng() % nv, &p);
            }
            api.refit(set, rng());
            Op(0x600 | round);
            DumpShape(object);
        }
        vertices = nv;
    }
    CallSlot(object, 2, nullptr, 0); // Release: the shape destroyed
    Op(0x700);
}

} // namespace

DIFF_TEST(CompGeomTriangleSet_Data, 0x195af0)
{
    Fixture fixture(ctx);
    ReadOurVtables();
    Api apis[2];
    if (!ApiFor(ctx, false, apis[0]) || !ApiFor(ctx, true, apis[1])) return;
    unsigned failures = 0;
    for (uint32_t seed = 1; seed <= 400 && failures < 5; ++seed) {
        for (unsigned side = 0; side < 2; ++side) {
            gSides[side].Reset();
            gCur = &gSides[side];
            RunData(apis[side], seed);
            gCur = nullptr;
        }
        if (!CompareLogs(ctx, "data", seed)) ++failures;
    }
}

namespace {

// ---- the slots test ----

// The game's slot bodies (db/classes.jsonl; both vtables share slots 0-41), checked before the slots run: the status
// entries name these RVAs.
constexpr uint32_t kSlotBody[42] = {
    0x196c60, 0x18fd70, 0x196ca0, 0x1963b0, 0x1965d0, 0x1966d0, 0x196720, 0x196890, 0x1967b0, 0x196800, 0x1969e0,
    0x196b60, 0x196a00, 0x196a00, 0x196a80, 0x196be0, 0x1969a0, 0x1966b0, 0x1966b0, 0x196920, 0x1969c0, 0x196980,
    0x196960, 0x196940, 0x190c40, 0x196b40, 0x196b20, 0x196b00, 0x190ed0, 0x196da0, 0x196d80, 0x196d60, 0x191f50,
    0x196e00, 0x196de0, 0x196dc0, 0x191fd0, 0x195b70, 0x195be0, 0x195c60, 0x18ea60, 0x18ea60,
};

// The inputs the slots get (every pointer argument points in here, the same address on both sides).
struct Inputs {
    AffineTransform3 frame1, frame2;
    float cuboid[10];  // half extents, position, quaternion (an oriented cuboid; a translated one is the first six)
    float rigid[7];    // translation, quaternion
    uint8_t opaque[0x40]; // cones, spheres, lines, rays, segments, another set: only passed on
    float t;
    BoundBox out;
    uint32_t outPad[2];
    void *outVar;
};
Inputs gIn;
void *gObject; // the shape under test (the current side's)
uint32_t gRet;  // what the stand-ins return

// A pointer argument as a code: into the shape, into the inputs (as is), into a logged block, else a frame the slot
// built (by value).
void LogArg(uint32_t w)
{
    const uint32_t o = shape::P(gObject), in = shape::P(&gIn);
    if (w >= o && w < o + 0x30) {
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

// The intersection tests the slots forward to (cdecl).
struct Helper {
    uint32_t rva;
    unsigned words;
};
constexpr Helper kHelpers[] = {
    {0x19f8e0, 4}, {0x1a01c0, 4}, {0x19ae00, 3}, {0x19aea0, 3}, {0x19af40, 3}, {0x19ada0, 2}, {0x19adc0, 2},
    {0x19ade0, 2}, {0x19b150, 4}, {0x19b1f0, 4}, {0x19b290, 4}, {0x19afe0, 3}, {0x19b030, 3}, {0x19b0b0, 3},
};
constexpr unsigned kHelperCount = unsigned(std::size(kHelpers));
template <unsigned N, unsigned... I> uint32_t __cdecl HelperStand(shape::StackWord<I>... w)
{
    Log(0xa0000000u | N);
    (LogArg(w), ...);
    return gRet;
}
template <unsigned N, unsigned... I> void *HelperAddress(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<void *>(&HelperStand<N, I...>);
}
template <unsigned... N> void HelperStands(void **out, std::integer_sequence<unsigned, N...>)
{
    ((out[N] = HelperAddress<N>(std::make_integer_sequence<unsigned, kHelpers[N].words>())), ...);
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

// The fake bound (cIBound) and linear (cILinear3): the slots a triangle set calls log, the rest trap.
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
void *gBoundVt[42], *gLinearVt[42];
Fake gBound{gBoundVt}, gLinear{gLinearVt};
void FillFakes()
{
    for (unsigned s = 0; s < 42; ++s) gBoundVt[s] = gLinearVt[s] = shape::TrapFor<&TrapHit>(2);
    gBoundVt[11] = FakeSlotAddress<0xb0000000u, 11>(std::make_integer_sequence<unsigned, 2>());
    gBoundVt[16] = FakeSlotAddress<0xb0000000u, 16>(std::make_integer_sequence<unsigned, 3>());
    gLinearVt[13] = FakeSlotAddress<0xb1000000u, 13>(std::make_integer_sequence<unsigned, 1>());
    gLinearVt[18] = FakeSlotAddress<0xb1000000u, 18>(std::make_integer_sequence<unsigned, 2>());
    gLinearVt[24] = FakeSlotAddress<0xb1000000u, 24>(std::make_integer_sequence<unsigned, 2>());
    gLinearVt[29] = FakeSlotAddress<0xb1000000u, 29>(std::make_integer_sequence<unsigned, 3>());
}

void RandomFrame(AffineTransform3 &f, std::mt19937 &rng)
{
    static const uint32_t kFlags[] = {0, 1, 2, 4, 6, 8, 0x10, 0x12, 0x14, 0x1e, 0x1f};
    f.mUnknown0 = rng() % 8 ? kFlags[rng() % std::size(kFlags)] : rng();
    for (float &m : f.mMatrix) m = rng() % 3 ? Num(rng) : float(rng() % 2);
    for (float &o : f.mOffset) o = Num(rng);
}

constexpr uint32_t kIds[] = {1, cIShape::kInterfaceId, cIBound::kInterfaceId, cITriangleSetBound::kInterfaceId,
                             0x3ce57cc0, 0};

// The slot's stack words; false when the slot is not exercised.
unsigned Arguments(unsigned slot, std::mt19937 &rng, uint32_t *w)
{
    const uint32_t f1 = shape::P(&gIn.frame1), f2 = shape::P(&gIn.frame2), cuboid = shape::P(gIn.cuboid),
                   opaque = shape::P(gIn.opaque), t = shape::P(&gIn.t), bound = shape::P(&gBound),
                   linear = shape::P(&gLinear), out = shape::P(&gIn.out);
    switch (slot) {
    case 0:
    case 3:
        w[0] = rng() % 8 ? kIds[rng() % std::size(kIds)] : rng();
        w[1] = rng() % 16 ? shape::P(&gIn.outVar) : 0;
        return 2;
    case 1:
    case 2:
    case 4:
    case 40:
    case 41: return 0;
    case 5:
    case 6:
    case 7: w[0] = f1, w[1] = opaque; return 2;
    case 8:
    case 9: w[0] = f1, w[1] = cuboid; return 2;
    case 10: w[0] = f1, w[1] = bound; return 2;
    case 11:
    case 12:
    case 13: w[0] = opaque, w[1] = f1; return 2;
    case 14: w[0] = cuboid, w[1] = f1; return 2;
    case 15: w[0] = bound, w[1] = f1; return 2;
    case 16:
    case 17:
    case 18: w[0] = f1, w[1] = opaque, w[2] = f2; return 3;
    case 19: w[0] = f1, w[1] = cuboid, w[2] = f2; return 3;
    case 20: w[0] = f1, w[1] = bound, w[2] = f2; return 3;
    case 21:
    case 22:
    case 23: w[0] = f1, w[1] = opaque; return 2;
    case 24: w[0] = f1, w[1] = linear; return 2;
    case 25:
    case 26:
    case 27: w[0] = opaque; return 1;
    case 28: w[0] = linear; return 1;
    case 29:
    case 30:
    case 31: w[0] = t, w[1] = f1, w[2] = opaque; return 3;
    case 32: w[0] = t, w[1] = f1, w[2] = linear; return 3;
    case 33:
    case 34:
    case 35: w[0] = t, w[1] = opaque; return 2;
    case 36: w[0] = t, w[1] = linear; return 2;
    case 37: w[0] = out, w[1] = f1; return 2;
    case 38: w[0] = out, w[1] = shape::P(gIn.rigid); return 2;
    default: w[0] = out; return 1; // 39
    }
}

void RandomInputs(std::mt19937 &rng)
{
    NaNMode(rng, 1);
    RandomFrame(gIn.frame1, rng);
    RandomFrame(gIn.frame2, rng);
    for (float &f : gIn.cuboid) f = Num(rng);
    for (float &f : gIn.rigid) f = Num(rng);
    for (uint8_t &b : gIn.opaque) b = uint8_t(rng());
    gIn.t = Num(rng);
    std::memset(&gIn.out, 0x5c, sizeof(gIn.out) + sizeof(gIn.outPad));
    gIn.outVar = reinterpret_cast<void *>(uintptr_t(0x0bad0bad));
    gRet = rng() % 3 == 0 ? rng() : rng() % 2;
}

constexpr unsigned kCasesPerShape = 600;

void RunSlots(const Api &api, uint32_t seed)
{
    std::mt19937 rng(seed);
    gObject = api.create();
    Op(0);
    CallSlot(gObject, 1, nullptr, 0); // AddRef
    {
        NaNMode(rng, 1);
        const uint32_t vertices = 1 + rng() % 30, triangles = 1 + rng() % 24;
        Fill(api, gObject, rng, vertices, triangles);
    }
    Op(1);
    DumpShape(gObject);
    for (unsigned i = 0; i < kCasesPerShape; ++i) {
        RandomInputs(rng);
        const unsigned slot = rng() % 42;
        if (slot == 1 || slot == 2) continue; // the count: at the end
        uint32_t w[3] = {};
        const unsigned n = Arguments(slot, rng, w);
        Op(0x10000u | slot << 12 | (i & 0xfff));
        const uint32_t r = CallSlot(gObject, slot, w, n);
        if (slot == 4) {
            Log(r);
        } else if (slot >= 37) { // the box slots return their out; 40 and 41 the set
            LogArg(r);
            DumpBytes(reinterpret_cast<const uint8_t *>(&gIn.out), 24);
        } else {
            Log(r & 0xff);
        }
        Log(Bits(gIn.t));
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

} // namespace

DIFF_TEST(CompGeomTriangleSet_Slots, 0x196560)
{
    Fixture fixture(ctx);
    ReadOurVtables();
    for (unsigned v = 0; v < 2; ++v)
        for (unsigned slot = 0; slot < 42; ++slot)
            if (shape::GameVTable(kGameVt[v])[slot] != ctx.image.At(kSlotBody[slot]))
                ctx.Fail("vtable 0x%x slot %u is not 0x%x", kGameVt[v], slot, kSlotBody[slot]);
    std::printf("  slot 42: 0x%x (shape), 0x%x (bound)\n",
                unsigned(reinterpret_cast<uintptr_t>(shape::GameVTable(kGameVt[0])[42]) - shape::P(ctx.image.At(0))),
                unsigned(reinterpret_cast<uintptr_t>(shape::GameVTable(kGameVt[1])[42]) - shape::P(ctx.image.At(0))));
    Api apis[2];
    if (!ApiFor(ctx, false, apis[0]) || !ApiFor(ctx, true, apis[1])) return;
    FillFakes();
    HelperPatches helpers;
    unsigned failures = 0;
    for (uint32_t seed = 1; seed <= 60 && failures < 5; ++seed) {
        for (unsigned side = 0; side < 2; ++side) {
            gSides[side].Reset();
            gCur = &gSides[side];
            RunSlots(apis[side], seed);
            gCur = nullptr;
        }
        if (!CompareLogs(ctx, "slots", seed)) ++failures;
    }
}

namespace {

constexpr unsigned kBoxCases = 30000;

} // namespace

DIFF_TEST(CompGeomTriangleSet_Boxes, 0x19d430)
{
    using GrowFn = void(__thiscall *)(BoundBox *, const BoundBox *);
    using BoxAtFn = BoundBox *(__thiscall *)(const Cuboid *, BoundBox *, const Vec3 *);
    using BoundsFn = BoundBox *(__thiscall *)(const Vec3 *, BoundBox *);
    GrowFn grow[2];
    BoxAtFn boxAt[2];
    if (!Pick(ctx, false, 0x19d430, grow[0]) || !Pick(ctx, true, 0x19d430, grow[1]) ||
        !Pick(ctx, false, 0x1902e0, boxAt[0]) || !Pick(ctx, true, 0x1902e0, boxAt[1]))
        return;
    const BoundsFn bounds = ctx.Original<BoundsFn>(0x1a26d0);
    std::mt19937 rng(0x7e5);
    unsigned failures = 0;
    for (unsigned i = 0; i < kBoxCases && failures < 10; ++i) {
        NaNMode(rng, 2);
        BoundBox self{RandomVec(rng), RandomVec(rng)}, other{RandomVec(rng), RandomVec(rng)};
        BoundBox a = self, b = self;
        grow[0](&a, &other);
        grow[1](&b, &other);
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0) {
            ctx.Fail("Grow case %u differs", i);
            ++failures;
        }

        Cuboid cuboid{RandomVec(rng)};
        Vec3 centre = RandomVec(rng);
        if (rng() % 4 == 0) {
            // Two NaNs of different payloads meeting in one coordinate's sums: the operand order shows.
            float *const h = &cuboid.half.x + rng() % 3;
            float *const c = &centre.x + (h - &cuboid.half.x);
            *h = Float((rng() & 0x80000000u) | 0x7fc00000u | (1 + rng() % 0x3ffffe));
            *c = Float((rng() & 0x80000000u) | 0x7f800000u | (rng() % 2 ? 0x00400000u : 0) | (1 + rng() % 0x3ffffe));
        }
        BoundBox c, d;
        std::memset(&c, 0x5c, sizeof(c));
        std::memset(&d, 0x5c, sizeof(d));
        if (boxAt[0](&cuboid, &c, &centre) != &c || boxAt[1](&cuboid, &d, &centre) != &d ||
            std::memcmp(&c, &d, sizeof(c)) != 0) {
            ctx.Fail("BoxAt case %u differs", i);
            ++failures;
        }
        ++ctx.cases;

        Vec3 corners[3] = {RandomVec(rng), RandomVec(rng), RandomVec(rng)};
        if (rng() % 4 == 0) corners[rng() % 3] = corners[rng() % 3]; // ties
        BoundBox e;
        std::memset(&e, 0x5c, sizeof(e));
        const BoundBox f = TriangleBounds(corners[0], corners[1], corners[2]);
        if (bounds(corners, &e) != &e || std::memcmp(&e, &f, sizeof(e)) != 0) {
            ctx.Fail("TriangleBounds case %u differs", i);
            ++failures;
        }
        ++ctx.cases;
    }
}
