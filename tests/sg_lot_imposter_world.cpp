// Differential tests for the lot imposter manager's world-database leaves (lane 261111r, scenegraph row LT2b2a;
// src/sg/cLotImposterManager.h): GroundHeightAt 0x369070, IsFlat 0x369460, ComputeLotBounds 0x36c7a0, CurrentLotName
// 0x368de0, KeepHighest 0x363120, AddHeight 0x363250, ClearAtlases 0x369430; and the class ids 0x3677f0 / 0x36d100 and the
// definitions command's name 0x369500.
//
// LotImposterManager_World: a manager image (0x1c4 bytes of pattern, the +0x1bc height map and the +0x154 atlas vector
// built in place, the lot rectangle and insets set by the test) whose +0x18 is a scripted world database fake (its own
// vtable: slots 62 / 64 / 87 / 95 return floats in st0, 81-84 / 88 / 89 ints; every call logged with its arguments, the
// tile record's three words for slot 62). The ground height (slot 64) comes from a per-seed table of NaN-dense floats
// by (x, y); on a chosen call it rewrites the manager's +0x24 or +0x2c (the loops must read them as the original does).
// The cached slot-23 service (cell 0x10cc2a8) is a recording fake: its kind (slot 5) answers from a per-seed sequence (so
// asking once or each time differs), slot 9 the lot fake or null; the lot's slot 4 the view fake, whose slot 14 answers
// per side asked (sometimes 0x100: true only in the high bytes); slot 19 a word the out string's slot 17 is given.
// Random steps on both sides, compared after each: the result bits, the manager's words +0x18..+0x38 and +0x178..+0x188,
// the height map (pre-order: colour, key bits, value bits, so the tree's shape too), the atlas vector, the event log, the
// fakes' counts; at the end each side's heap blocks (allocated minus freed).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "hook/hooks.h"
#include "sg/cLotImposterManager.h"

#include <bit>
#include <climits>
#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <vector>

namespace lotimpworld_test {

using namespace svcfx;
using nTSSG::cLotImposterManager;
using Mgr = cLotImposterManager;

enum : unsigned { fService = 0, fLot, fView, fOut, fFramework };
enum : uint32_t { evWorld = 0x7700 };

// The test's own order for the height map (the same as the class's, written from the original's compares).
struct Key {
    float x, y;
};
struct KeyLess {
    bool operator()(const Key &a, const Key &b) const { return a.x < b.x || (!(b.x < a.x) && a.y < b.y); }
};
using HeightMap = std::map<Key, float, KeyLess>;
static_assert(sizeof(HeightMap) == 8);

float Fb(uint32_t bits) { return std::bit_cast<float>(bits); }
uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

// NaN-dense floats.
const uint32_t kFloatBits[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x3f000000, 0xbf000000, 0x3dcccccd, 0x3dcccccc, 0x3dccccce, 0xbdcccccd,
    0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000, 0x7fc12345, 0x7f800001, 0x00000001, 0x40200000, 0x40600000, 0x4b000001,
    0x7f7fffff, 0x3fc00000, 0x4b400000, 0xcb000000,
};
constexpr unsigned kFloatCount = sizeof(kFloatBits) / 4;
// Ground coordinates whose quotient by a tile of 3, 10, -10 or the tiny tile gives another index when divided in
// double rather than float (GroundHeightAt divides in float, then adds 0.5 in double): 1.4999999 / 3 rounds to 0.5
// in float, below it in double; the others are large quotients near a float spacing of 1.
const uint32_t kRoundBits[] = {0x3fbfffff, 0x4ca00001, 0x4bc00001, 0xcca00001, 0x4d1ffed7, 0x4c3ffed6, 0x19224261,
                               0x19a24134, 0x40400000, 0x3fc00000};
constexpr unsigned kRoundCount = sizeof(kRoundBits) / 4;

// ---- the world database fake ----
struct WorldScript {
    float tile, wall, elevation;
    int32_t minX, maxX, minY, maxY, minLevel, maxLevel;
    float heights[16];
    int changeAt;       // the ground call that rewrites a manager field (-1: none)
    uint32_t changeOff; // +0x24 or +0x2c
    int32_t changeTo;
    int swapAt;         // the tile-size call that points the manager's +0x18 at the other world fake (-1: none)
};
WorldScript gW;
int gGroundCalls, gTileCalls;
uint8_t *gMgr;
struct WorldFake {
    void **vt;
} gWorld, gWorld2;
void *gWorldVT[96];
// Which world fake (GroundHeightAt reads +0x18 again after the tile size: the original's 0x36912b).
uint32_t WhichWorld(const void *self) { return self == &gWorld ? 1 : self == &gWorld2 ? 2 : 0; }

void Log(uint32_t slot) { Ev(evWorld), Ev(slot); }
float __fastcall WElevation(void *self, void *, const int32_t *tile)
{
    Log(62), Ev(WhichWorld(self)), Ev(tile[0]), Ev(tile[1]), Ev(tile[2]);
    return gW.elevation;
}
float __fastcall WGround(void *self, void *, int32_t x, int32_t y)
{
    Log(64), Ev(WhichWorld(self)), Ev(uint32_t(x)), Ev(uint32_t(y));
    if (gGroundCalls++ == gW.changeAt) std::memcpy(gMgr + gW.changeOff, &gW.changeTo, 4);
    return gW.heights[(uint32_t(x) * 5u + uint32_t(y) * 3u) & 15];
}
int32_t __fastcall WMinX(void *, void *) { return Log(81), gW.minX; }
int32_t __fastcall WMinY(void *, void *) { return Log(82), gW.minY; }
int32_t __fastcall WMaxX(void *, void *) { return Log(83), gW.maxX; }
int32_t __fastcall WMaxY(void *, void *) { return Log(84), gW.maxY; }
float __fastcall WTile(void *self, void *)
{
    Log(87), Ev(WhichWorld(self));
    if (gTileCalls++ == gW.swapAt) {
        void *other = self == &gWorld ? static_cast<void *>(&gWorld2) : static_cast<void *>(&gWorld);
        std::memcpy(gMgr + 0x18, &other, 4);
    }
    return gW.tile;
}
int32_t __fastcall WMinLevel(void *, void *) { return Log(88), gW.minLevel; }
int32_t __fastcall WMaxLevel(void *, void *) { return Log(89), gW.maxLevel; }
float __fastcall WWall(void *, void *) { return Log(95), gW.wall; }

// ---- the service, lot, view and out-string fakes ----
uint32_t gKinds[4];
uint32_t gViewBits;
bool gHasLot;
uint32_t ServiceKind(Obj *self, uint32_t *) { return gKinds[self->value[0]++ % 4]; }
uint32_t ServiceLot(Obj *, uint32_t *) { return gHasLot ? P(&F(fLot)) : 0; }
uint32_t ViewSide(Obj *, uint32_t *args)
{
    const uint32_t pick = gViewBits >> (args[0] & 3) * 4 & 0xf;
    return pick == 0 ? 0 : pick == 1 ? 0x100 : pick == 2 ? 0xffffff01 : pick & 1;
}

void BeginSide()
{
    ResetAll();
    F(fService).Set(5, "", 0, &ServiceKind);
    F(fService).Set(9, "", 0, &ServiceLot);
    F(fService).Values("d");
    F(fLot).Set(4, "", &F(fView));
    F(fLot).Set(19, "", 0x5eed0013u);
    F(fView).Set(14, "d", 0, &ViewSide);
    F(fOut).Set(17, "d", 0x44u);
    gGroundCalls = 0;
    gTileCalls = 0;
}

// ---- the manager image ----
void DumpMap(Blob &b, const uint8_t *o)
{
    const auto *head = *reinterpret_cast<const uint8_t *const *>(o + 0x1bc);
    uint32_t size;
    std::memcpy(&size, o + 0x1c0, 4);
    b.push_back(0x3a90), b.push_back(size);
    // pre-order from the root (head's parent), nil children logged as 0xeeee
    std::vector<const uint8_t *> stack{*reinterpret_cast<const uint8_t *const *>(head + 4)};
    unsigned guard = 0;
    while (!stack.empty() && ++guard < 4096) {
        const uint8_t *n = stack.back();
        stack.pop_back();
        if (n[0xd]) {
            b.push_back(0xeeee);
            continue;
        }
        uint32_t w[3];
        std::memcpy(w, n + 0x10, 12);
        b.push_back(n[0xc]), b.push_back(w[0]), b.push_back(w[1]), b.push_back(w[2]);
        stack.push_back(*reinterpret_cast<const uint8_t *const *>(n + 8));
        stack.push_back(*reinterpret_cast<const uint8_t *const *>(n));
    }
}
void DumpManager(Blob &b, const uint8_t *o)
{
    void *world;
    std::memcpy(&world, o + 0x18, 4);
    b.push_back(WhichWorld(world));
    DumpWords(b, o + 0x1c, 0x1c, "d dddd dd"); // +0x1c (the fill), the lot rectangle, the height range
    DumpWords(b, o + 0x178, 0x14, "ddddd");
    DumpMap(b, o);
    const auto &atlases = *reinterpret_cast<const std::vector<nGZGraphic4::cTextureAtlas> *>(o + 0x154);
    b.push_back(uint32_t(atlases.size()));
    uint32_t cap[2];
    std::memcpy(cap, o + 0x154, 4), std::memcpy(cap + 1, o + 0x15c, 4);
    b.push_back(cap[1] - cap[0]);
    for (const auto &a : atlases) b.push_back(a.mRegions), b.push_back(uint32_t(a.mNodes.size()));
}

const int32_t kInsets[] = {0, 1, -1, 3, 0, 2};
const int32_t kMins[] = {0, -3, 2, 1, 0x7ffffff0, -1};

struct Case {
    WorldScript w;
    uint32_t kinds[4];
    uint32_t viewBits;
    bool hasLot;
    int32_t rect[4], insets[4];
    unsigned atlases;
    std::vector<std::pair<Key, float>> heights;
};

Case MakeCase(std::mt19937 &gen)
{
    auto draw = [&](uint32_t n) { return uint32_t(gen() % n); };
    auto anyFloat = [&] {
        return draw(3) ? Fb(kFloatBits[draw(kFloatCount)]) : float(int32_t(draw(2001)) - 1000) * 0.25f;
    };
    Case c{};
    WorldScript &w = c.w;
    static const uint32_t kTiles[] = {0x3f800000, 0x41200000, 0x3f000000, 0, 0xc1200000, 0x7fc00000, 0x40400000, 0x0da24260};
    w.tile = Fb(kTiles[draw(8)]);
    w.wall = anyFloat();
    w.elevation = anyFloat();
    w.minX = kMins[draw(6)];
    w.maxX = w.minX + int32_t(draw(6)) - 1;
    w.minY = kMins[draw(6)];
    w.maxY = w.minY + int32_t(draw(6)) - 1;
    w.minLevel = int32_t(draw(5)) - 1;
    w.maxLevel = int32_t(draw(6)) - 1;
    // flat (one value, sometimes with neighbours of +-0.1 around it) or mixed
    const unsigned mode = draw(4);
    const float base = anyFloat();
    for (float &h : w.heights) {
        if (mode == 0) h = base;
        else if (mode == 1) {
            static const float kNear[] = {0.0f, 0.1f, -0.1f, 0.099999994f, 0.10000001f, 0.05f, -0.0999999f, 0.2f};
            h = base + kNear[draw(8)];
        } else h = anyFloat();
    }
    w.changeAt = draw(3) ? -1 : int(draw(12));
    w.changeOff = draw(2) ? 0x24 : 0x2c;
    w.changeTo = int32_t(draw(5)) - 2;
    w.swapAt = draw(3) ? -1 : int(draw(3));
    static const uint32_t kKindList[] = {0x0bea28c2, 0x2c1805b0, 0xcc181346, 0x1234, 0, 0x0bea28c3};
    for (uint32_t &k : c.kinds) k = kKindList[draw(6)];
    c.viewBits = uint32_t(gen());
    c.hasLot = draw(4) != 0;
    for (int32_t &r : c.rect) r = int32_t(draw(5)) - 1;
    for (int32_t &i : c.insets) i = kInsets[draw(6)];
    c.atlases = draw(4);
    static const float kGrid[] = {0.0f, 1.0f, -1.0f, 2.5f, -0.0f};
    for (unsigned i = draw(12); i; --i) {
        const Key k{kGrid[draw(5)], draw(16) ? kGrid[draw(5)] : Fb(0x7fc00000)};
        c.heights.push_back({k, anyFloat()});
    }
    return c;
}

uint8_t *BuildManager(const Case &c)
{
    auto *o = static_cast<uint8_t *>(::operator new(Mgr::kSize));
    std::memset(o, 0xc5, Mgr::kSize);
    void *world = &gWorld;
    std::memcpy(o + 0x18, &world, 4);
    std::memcpy(o + 0x20, c.rect, 16);
    std::memcpy(o + 0x178, c.insets, 16);
    auto *map = new (o + 0x1bc) HeightMap();
    for (const auto &[k, v] : c.heights) map->emplace(k, v);
    auto *atlases = new (o + 0x154) std::vector<nGZGraphic4::cTextureAtlas>();
    for (unsigned i = 0; i < c.atlases; ++i) {
        auto &a = atlases->emplace_back();
        a.mRegions = 0x30 + i;
        a.mNodes.resize(i + 1);
        a.mFreeNodes.assign(i, 7u);
    }
    return o;
}
void FreeManager(uint8_t *o)
{
    std::destroy_at(reinterpret_cast<HeightMap *>(o + 0x1bc));
    std::destroy_at(reinterpret_cast<std::vector<nGZGraphic4::cTextureAtlas> *>(o + 0x154));
    ::operator delete(o);
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

} // namespace lotimpworld_test

using namespace lotimpworld_test;

DIFF_TEST(LotImposterManager_World, 0x36c7a0)
{
    AllocatorScope allocator;
    Globals globals;
    globals.Set(kSlot23Cache, &F(fService));
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    gWorld.vt = gWorldVT;
    gWorld2.vt = gWorldVT;
    gWorldVT[62] = reinterpret_cast<void *>(&WElevation);
    gWorldVT[64] = reinterpret_cast<void *>(&WGround);
    gWorldVT[81] = reinterpret_cast<void *>(&WMinX);
    gWorldVT[82] = reinterpret_cast<void *>(&WMinY);
    gWorldVT[83] = reinterpret_cast<void *>(&WMaxX);
    gWorldVT[84] = reinterpret_cast<void *>(&WMaxY);
    gWorldVT[87] = reinterpret_cast<void *>(&WTile);
    gWorldVT[88] = reinterpret_cast<void *>(&WMinLevel);
    gWorldVT[89] = reinterpret_cast<void *>(&WMaxLevel);
    gWorldVT[95] = reinterpret_cast<void *>(&WWall);

    using GroundFn = float(__thiscall *)(void *, const float *);
    using FlatFn = uint32_t(__thiscall *)(void *);
    using VoidFn = void(__thiscall *)(void *);
    using KeepFn = void(__thiscall *)(void *, const void *, float);
    using AddFn = void(__thiscall *)(void *, const float *, uint32_t);
    using NameFn = uint32_t(__stdcall *)(void *);
    const GroundFn groundO = ctx.Original<GroundFn>(0x369070),
                   groundU = reinterpret_cast<GroundFn>(MemberAddress(&Mgr::GroundHeightAt));
    const FlatFn flatO = ctx.Original<FlatFn>(0x369460), flatU = reinterpret_cast<FlatFn>(MemberAddress(&Mgr::IsFlat));
    const VoidFn boundsO = ctx.Original<VoidFn>(0x36c7a0),
                 boundsU = reinterpret_cast<VoidFn>(MemberAddress(&Mgr::ComputeLotBounds));
    const VoidFn clearO = ctx.Original<VoidFn>(0x369430),
                 clearU = reinterpret_cast<VoidFn>(MemberAddress(&Mgr::ClearAtlases));
    const KeepFn keepO = ctx.Original<KeepFn>(0x363120), keepU = reinterpret_cast<KeepFn>(MemberAddress(&Mgr::KeepHighest));
    const AddFn addO = ctx.Original<AddFn>(0x363250), addU = reinterpret_cast<AddFn>(MemberAddress(&Mgr::AddHeight));
    const NameFn nameO = ctx.Original<NameFn>(0x368de0), nameU = reinterpret_cast<NameFn>(&Mgr::CurrentLotName);

    enum : unsigned { opGround, opFlat, opBounds, opClear, opKeep, opAdd, opName, kOps };
    static const char *const kNames[] = {"ground", "flat", "bounds", "clear", "keep", "add", "name"};
    int reached[kOps] = {}, flatTrue = 0, flatFalse = 0, inserted = 0, raised = 0;

    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed * 2654435761u + 0x36c7a0u);
        auto draw = [&](uint32_t n) { return uint32_t(gen() % n); };
        const Case c = MakeCase(gen);
        struct Step {
            unsigned op;
            float a, b, h;
            uint32_t word;
        };
        std::vector<Step> steps(1 + draw(5));
        for (Step &s : steps) {
            s.op = draw(kOps);
            auto f = [&] {
                return draw(3) ? Fb(kFloatBits[draw(kFloatCount)]) : float(int32_t(draw(4001)) - 2000) * 0.125f;
            };
            static const float kGrid[] = {0.0f, 1.0f, -1.0f, 2.5f, -0.0f};
            if (s.op == opKeep || (s.op == opAdd && draw(2))) s.a = kGrid[draw(5)], s.b = draw(12) ? kGrid[draw(5)] : Fb(0x7fc00000);
            else if (s.op == opGround && draw(2)) s.a = Fb(kRoundBits[draw(kRoundCount)]), s.b = Fb(kRoundBits[draw(kRoundCount)]);
            else s.a = f(), s.b = f();
            s.h = f();
            static const uint32_t kGiven[] = {0, 1, 0x100, 0xffffff00, 0xff, 0x5a5a5a01};
            s.word = kGiven[draw(6)];
        }

        Blob outcome[2][8];
        long long blocks[2] = {};
        for (int side = 0; side < 2; ++side) {
            gW = c.w;
            std::memcpy(gKinds, c.kinds, sizeof(gKinds));
            gViewBits = c.viewBits;
            gHasLot = c.hasLot;
            BeginSide();
            ctx.Mark(side ? "ours" : "original");
            difftest::AllocationCounter counter;
            uint8_t *o = BuildManager(c);
            gMgr = o;
            for (size_t i = 0; i < steps.size(); ++i) {
                const Step &s = steps[i];
                uint32_t result = 0;
                const size_t before = reinterpret_cast<HeightMap *>(o + 0x1bc)->size();
                switch (s.op) {
                case opGround: {
                    const float xy[2] = {s.a, s.b};
                    result = Bits(side ? groundU(o, xy) : groundO(o, xy));
                    break;
                }
                case opFlat:
                    result = (side ? flatU(o) : flatO(o)) & 0xff;
                    if (side) (result ? flatTrue : flatFalse)++;
                    break;
                case opBounds: (side ? boundsU : boundsO)(o); break;
                case opClear: (side ? clearU : clearO)(o); break;
                case opKeep: {
                    const Key k{s.a, s.b};
                    (side ? keepU : keepO)(o, &k, s.h);
                    break;
                }
                case opAdd: {
                    const float p[3] = {s.a, s.b, s.h};
                    (side ? addU : addO)(o, p, s.word);
                    break;
                }
                case opName: result = (side ? nameU(&F(fOut)) : nameO(&F(fOut))) & 0xff; break;
                default: break;
                }
                if (side) {
                    ++reached[s.op];
                    const size_t after = reinterpret_cast<HeightMap *>(o + 0x1bc)->size();
                    if (s.op == opKeep || s.op == opAdd) (after > before ? inserted : raised)++;
                }
                difftest::NotCounted bookkeeping;
                Blob &b = outcome[side][i];
                b = gEv;
                gEv.clear();
                b.push_back(0xeeee), b.push_back(result), b.push_back(uint32_t(gGroundCalls)), b.push_back(uint32_t(gTileCalls));
                DumpManager(b, o);
                CaptureFakes(b);
            }
            FreeManager(o);
            gMgr = nullptr;
            blocks[side] = counter.Blocks();
        }
        for (size_t i = 0; i < steps.size(); ++i)
            if (!Same(ctx, outcome[0][i], outcome[1][i], kNames[steps[i].op], seed)) break;
        if (blocks[0] != blocks[1]) ctx.Fail("seed %u: heap blocks left %lld vs %lld", seed, blocks[0], blocks[1]);
        if (ctx.failures.size() >= 6) return;
    }
    for (unsigned k = 0; k < kOps; ++k)
        if (reached[k] < 300) ctx.Fail("coverage: %s ran %d times", kNames[k], reached[k]);
    if (flatTrue < 30 || flatFalse < 30) ctx.Fail("coverage: IsFlat true %d, false %d", flatTrue, flatFalse);
    if (inserted < 100 || raised < 100) ctx.Fail("coverage: height map inserts %d, existing %d", inserted, raised);
}

// Verifies the class ids 0x3677f0 / 0x36d100 and the definitions command's name 0x369500.
DIFF_TEST(LotImposterManager_ClassIds, 0x3677f0)
{
    const uint32_t a0 = ctx.Original<uint32_t(__cdecl *)()>(0x3677f0)(), a1 = Mgr::ClassId();
    const uint32_t b0 = ctx.Original<uint32_t(__cdecl *)()>(0x36d100)(), b1 = nTSSG::LotImposterClassId();
    if (a0 != a1) ctx.Fail("manager class id %#x vs %#x", a0, a1);
    if (b0 != b1) ctx.Fail("lot imposter class id %#x vs %#x", b0, b1);
    uint32_t self = 0;
    const char *n0 = ctx.Original<const char *(__thiscall *)(void *)>(0x369500)(&self);
    const char *n1 = reinterpret_cast<nTSSG::cObjectImposterCommandName *>(&self)->GetName();
    if (std::strcmp(n0, n1) != 0) ctx.Fail("command name '%s' vs '%s'", n0, n1);
}
