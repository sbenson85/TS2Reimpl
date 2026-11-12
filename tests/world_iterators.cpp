// Differential tests for the tile, quadrant and point iterators of nTSWorld's helper module (lane RZ,
// src/world/WorldIterators.*). Each case fills a buffer with random bytes, copies it for the other side, runs the original
// on one copy and ours on the other with the object 16 bytes in and the output 0x80 bytes in, and compares the whole
// buffers (so stray writes past the object show), the returned value (pointers as offsets into the side's own buffer,
// bools as al) and the world-DB fake's call log. The diamonds are also walked from their constructors to the end, a step
// at a time.
#include "difftest.h"
#include "worldfx.h"

#include "world/WorldIterators.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace worlditer;

namespace {

constexpr unsigned kObj = 16, kOut = 0x80, kSize = 0x100;

struct Buf {
    uint8_t b[kSize];
    uint8_t *obj() { return b + kObj; }
    uint8_t *out() { return b + kOut; }
    int32_t &I(unsigned off) { return *reinterpret_cast<int32_t *>(obj() + off); }
    uint8_t &B(unsigned off) { return obj()[off]; }
};

struct Outcome {
    Buf buf;
    uint32_t ret = 0;
    std::vector<uint32_t> log;
};

void Fill(std::mt19937 &r, Buf &b)
{
    for (unsigned i = 0; i < kSize; i += 4) {
        const uint32_t w = r();
        std::memcpy(b.b + i, &w, 4);
    }
}

// A returned pointer as its offset in the side's buffer (0xffffffff when outside it), a bool as its low byte.
uint32_t Ptr(const void *p, const Buf &b)
{
    const uintptr_t off = uintptr_t(p) - uintptr_t(b.b);
    return off < kSize ? uint32_t(off) : 0xffffffffu;
}

template <class F> Outcome Run(const Buf &init, F f)
{
    Outcome o;
    o.buf = init;
    worldfx::gLog.clear();
    o.ret = f(o.buf);
    o.log = worldfx::gLog;
    worldfx::gLog.clear();
    return o;
}

// Runs both sides from `init`; false (and one failure) when they differ.
template <class A, class B> bool Same(difftest::Context &ctx, const char *what, int i, const Buf &init, A orig, B ours,
                                      Outcome *after = nullptr)
{
    ++ctx.cases;
    const Outcome a = Run(init, orig);
    const Outcome b = Run(init, ours);
    if (after) *after = b;
    if (a.ret == b.ret && std::memcmp(a.buf.b, b.buf.b, kSize) == 0 && a.log == b.log) return true;
    unsigned at = 0;
    while (at < kSize && a.buf.b[at] == b.buf.b[at]) ++at;
    ctx.Fail("%s case %d: ret %08x vs %08x, first differing byte %u (%02x vs %02x), log %u vs %u words", what, i, a.ret,
             b.ret, at, at < kSize ? a.buf.b[at] : 0, at < kSize ? b.buf.b[at] : 0, unsigned(a.log.size()),
             unsigned(b.log.size()));
    return false;
}

int32_t Small(std::mt19937 &r, int32_t span) { return int32_t(r() % uint32_t(2 * span + 1)) - span; }

// A coordinate: mostly small, sometimes anywhere in the int range.
int32_t Coord(std::mt19937 &r)
{
    const uint32_t k = r() % 8;
    if (k == 0) return int32_t(r());
    if (k == 1) return Small(r, 1 << 24);
    return Small(r, 40);
}

using Ctor2 = void *(__thiscall *)(void *, const void *, const void *);
using Step = void *(__thiscall *)(void *);
using Pred = bool(__thiscall *)(void *);
using Getter = void *(__thiscall *)(void *, void *);
using Proc = void(__thiscall *)(void *);

// ---- the diamonds ----

void DiamondState(std::mt19937 &r, Buf &b, bool quad)
{
    Fill(r, b);
    // startX, startY, lowTurnX, highTurnX, low, high, firstLevel, lastLevel: close together so every turn and edge comes up.
    const int32_t base = r() % 6 == 0 ? int32_t(r()) : Small(r, 30);
    for (unsigned off = 0; off < 0x20; off += 4) b.I(off) = base + Small(r, 4);
    const unsigned xAt = quad ? 0x24 : 0x20;
    for (unsigned off = xAt; off < xAt + 12; off += 4) b.I(off) = base + Small(r, 4);
    if (r() % 16 == 0) b.I(xAt + 4) = b.I(0x14) + Small(r, 1); // y at high, just below or above
    if (quad) {
        // The bit: 1..63 (0, or a doubling past 0x40, loops forever in the original); the mask any byte.
        static const int8_t kBits[] = {1, 2, 4, 8, 16, 32};
        b.B(0x30) = uint8_t(r() % 4 == 0 ? int8_t(1 + r() % 63) : kBits[r() % 6]);
    }
}

void TileCoordAt(uint8_t *p, int32_t x, int32_t y, int32_t level)
{
    const int32_t v[3] = {x, y, level};
    std::memcpy(p, v, sizeof(v));
}

} // namespace

DIFF_TEST(WorldIterators_DiamondBounds, 0x7f1ae0)
{
    auto bounds = ctx.Original<void(__cdecl *)(int32_t, int32_t, int32_t, int32_t, int32_t *, int32_t *, int32_t *,
                                               int32_t *)>(0x7f1ae0);
    auto rotInt = ctx.Original<void(__cdecl *)(int32_t, int32_t, int32_t, int32_t, int32_t *, int32_t *, int32_t *,
                                               int32_t *)>(0x7f1b80);
    auto rotFlt = ctx.Original<void(__cdecl *)(float, float, float, float, float *, float *, float *, float *)>(0x7f1c60);
    std::mt19937 r(0x7f1ae0);
    for (int i = 0; i < 60000; ++i) {
        Buf init;
        Fill(r, init);
        const int32_t x1 = Coord(r), y1 = Coord(r);
        const int32_t x2 = r() % 2 ? x1 + Small(r, 12) : Coord(r), y2 = r() % 2 ? y1 + Small(r, 12) : Coord(r);
        const unsigned which = unsigned(i % 3);
        float f[4];
        for (float &v : f) {
            const uint32_t k = r() % 4;
            v = k == 0 ? float(Small(r, 100)) : k == 1 ? float(Small(r, 1000)) * 0.125f
                                                : float(int32_t(r())) / float(1 + r() % 1000);
            if (r() % 50 == 0) v = -v;
        }
        auto side = [&](bool orig) {
            return [&, orig](Buf &b) {
                int32_t *o = reinterpret_cast<int32_t *>(b.out());
                float *fo = reinterpret_cast<float *>(b.out());
                if (which == 0) (orig ? bounds : DiamondBounds)(x1, y1, x2, y2, o, o + 1, o + 2, o + 3);
                else if (which == 1) (orig ? rotInt : RotateCornersInt)(x1, y1, x2, y2, o, o + 1, o + 2, o + 3);
                else (orig ? rotFlt : RotateCornersFloat)(f[0], f[1], f[2], f[3], fo, fo + 1, fo + 2, fo + 3);
                return 0u;
            };
        };
        if (!Same(ctx, which == 0 ? "DiamondBounds" : which == 1 ? "RotateCornersInt" : "RotateCornersFloat", i, init,
                  side(true), side(false)))
            return;
    }
}

DIFF_TEST(WorldIterators_TileDiamond, 0x7f17a0)
{
    auto ctor = ctx.Original<Ctor2>(0x7f0f00);
    auto next = ctx.Original<Step>(0x7f17a0);
    auto done = ctx.Original<Pred>(0x7f18d0);
    auto get = ctx.Original<Getter>(0x7f1950);
    auto start = ctx.Original<Proc>(0x7f1d10);
    auto startLevel = ctx.Original<Proc>(0x7f1da0);
    std::mt19937 r(0x7f17a0);
    // Single steps from random states.
    for (int i = 0; i < 60000; ++i) {
        Buf init;
        DiamondState(r, init, false);
        const unsigned which = unsigned(i % 5);
        auto side = [&](bool orig) {
            return [&, orig](Buf &b) -> uint32_t {
                auto *it = reinterpret_cast<TileDiamondIterator *>(b.obj());
                switch (which) {
                case 0: return Ptr(orig ? next(it) : it->Next(), b);
                case 1: return uint8_t(orig ? done(it) : it->Done());
                case 2: return Ptr(orig ? get(it, b.out()) : it->Get(reinterpret_cast<TileCoord *>(b.out())), b);
                case 3: orig ? start(it) : it->Start(); return 0;
                default: orig ? startLevel(it) : it->StartLevel(); return 0;
                }
            };
        };
        if (!Same(ctx, "TileDiamond step", i, init, side(true), side(false))) return;
    }
    // Whole walks from the constructor.
    for (int i = 0; i < 3000; ++i) {
        Buf init;
        Fill(r, init);
        const int32_t x1 = Small(r, 20), y1 = Small(r, 20);
        TileCoordAt(init.out(), x1, y1, Small(r, 2));
        TileCoordAt(init.out() + 12, x1 + Small(r, 9), y1 + Small(r, 9), Small(r, 3));
        Outcome now;
        auto build = [&](bool orig) {
            return [&, orig](Buf &b) {
                auto *it = reinterpret_cast<TileDiamondIterator *>(b.obj());
                const auto *a = reinterpret_cast<const TileCoord *>(b.out());
                return Ptr(orig ? ctor(it, a, a + 1) : it->Construct(a, a + 1), b);
            };
        };
        if (!Same(ctx, "TileDiamond ctor", i, init, build(true), build(false), &now)) return;
        for (int step = 0; step < 400 && !reinterpret_cast<TileDiamondIterator *>(now.buf.obj())->Done(); ++step) {
            const Buf at = now.buf;
            auto adv = [&](bool orig) {
                return [&, orig](Buf &b) {
                    auto *it = reinterpret_cast<TileDiamondIterator *>(b.obj());
                    return Ptr(orig ? next(it) : it->Next(), b);
                };
            };
            if (!Same(ctx, "TileDiamond walk", i * 1000 + step, at, adv(true), adv(false), &now)) return;
        }
    }
}

DIFF_TEST(WorldIterators_InnerDiamond, 0x7f1740)
{
    auto ctor = ctx.Original<Ctor2>(0x7f0ea0);
    auto next = ctx.Original<Step>(0x7f1740);
    auto start = ctx.Original<Proc>(0x7f1d00);
    auto startLevel = ctx.Original<Proc>(0x7f1d50);
    std::mt19937 r(0x7f1740);
    for (int i = 0; i < 60000; ++i) {
        Buf init;
        DiamondState(r, init, false);
        if (r() % 4 == 0) init.I(0) = init.I(8) - 1;  // startX + 1 on lowTurnX
        if (r() % 4 == 0) init.I(0) = init.I(12) - 1; // startX + 1 on highTurnX
        const unsigned which = unsigned(i % 3);
        auto side = [&](bool orig) {
            return [&, orig](Buf &b) -> uint32_t {
                auto *it = reinterpret_cast<InnerDiamondIterator *>(b.obj());
                switch (which) {
                case 0: return Ptr(orig ? next(it) : it->Next(), b);
                case 1: orig ? start(it) : it->Start(); return 0;
                default: orig ? startLevel(it) : it->StartLevel(); return 0;
                }
            };
        };
        if (!Same(ctx, "InnerDiamond step", i, init, side(true), side(false))) return;
    }
    for (int i = 0; i < 3000; ++i) {
        Buf init;
        Fill(r, init);
        const int32_t x1 = Small(r, 20), y1 = Small(r, 20);
        TileCoordAt(init.out(), x1, y1, Small(r, 2));
        TileCoordAt(init.out() + 12, x1 + Small(r, 9), y1 + Small(r, 9), Small(r, 3));
        Outcome now;
        auto build = [&](bool orig) {
            return [&, orig](Buf &b) {
                auto *it = reinterpret_cast<InnerDiamondIterator *>(b.obj());
                const auto *a = reinterpret_cast<const TileCoord *>(b.out());
                return Ptr(orig ? ctor(it, a, a + 1) : it->Construct(a, a + 1), b);
            };
        };
        if (!Same(ctx, "InnerDiamond ctor", i, init, build(true), build(false), &now)) return;
        for (int step = 0; step < 400; ++step) {
            const auto *it = reinterpret_cast<const InnerDiamondIterator *>(now.buf.obj());
            if (it->level > it->lastLevel) break;
            const Buf at = now.buf;
            auto adv = [&](bool orig) {
                return [&, orig](Buf &b) {
                    auto *p = reinterpret_cast<InnerDiamondIterator *>(b.obj());
                    return Ptr(orig ? next(p) : p->Next(), b);
                };
            };
            if (!Same(ctx, "InnerDiamond walk", i * 1000 + step, at, adv(true), adv(false), &now)) return;
        }
    }
}

DIFF_TEST(WorldIterators_QuadDiamond, 0x7f1690)
{
    auto ctor = ctx.Original<Ctor2>(0x7f0e40);
    auto next = ctx.Original<Step>(0x7f1690);
    auto done = ctx.Original<Pred>(0x7f18c0);
    auto get = ctx.Original<Getter>(0x7f1930);
    auto start = ctx.Original<Proc>(0x7f1cf0);
    auto startColumn = ctx.Original<Proc>(0x7f1d30);
    auto startRow = ctx.Original<Proc>(0x7f1dc0);
    std::mt19937 r(0x7f1690);
    for (int i = 0; i < 80000; ++i) {
        Buf init;
        DiamondState(r, init, true);
        const unsigned which = unsigned(i % 8);
        auto side = [&](bool orig) {
            return [&, orig](Buf &b) -> uint32_t {
                auto *it = reinterpret_cast<QuadDiamondIterator *>(b.obj());
                switch (which) {
                case 0: case 1: case 2: return Ptr(orig ? next(it) : it->Next(), b);
                case 3: return uint8_t(orig ? done(it) : it->Done());
                case 4: return Ptr(orig ? get(it, b.out()) : it->Get(reinterpret_cast<TileCoordQuadrant *>(b.out())), b);
                case 5: orig ? start(it) : it->Start(); return 0;
                case 6: orig ? startColumn(it) : it->StartColumn(); return 0;
                default: orig ? startRow(it) : it->StartRow(); return 0;
                }
            };
        };
        if (!Same(ctx, "QuadDiamond step", i, init, side(true), side(false))) return;
    }
    for (int i = 0; i < 3000; ++i) {
        Buf init;
        Fill(r, init);
        const int32_t x1 = Small(r, 20), y1 = Small(r, 20);
        TileCoordAt(init.out(), x1, y1, Small(r, 2));
        TileCoordAt(init.out() + 12, x1 + Small(r, 9), y1 + Small(r, 9), Small(r, 3));
        Outcome now;
        auto build = [&](bool orig) {
            return [&, orig](Buf &b) {
                auto *it = reinterpret_cast<QuadDiamondIterator *>(b.obj());
                const auto *a = reinterpret_cast<const TileCoord *>(b.out());
                return Ptr(orig ? ctor(it, a, a + 1) : it->Construct(a, a + 1), b);
            };
        };
        if (!Same(ctx, "QuadDiamond ctor", i, init, build(true), build(false), &now)) return;
        for (int step = 0; step < 1200 && !reinterpret_cast<QuadDiamondIterator *>(now.buf.obj())->Done(); ++step) {
            const Buf at = now.buf;
            auto adv = [&](bool orig) {
                return [&, orig](Buf &b) {
                    auto *it = reinterpret_cast<QuadDiamondIterator *>(b.obj());
                    return Ptr(orig ? next(it) : it->Next(), b);
                };
            };
            if (!Same(ctx, "QuadDiamond walk", i * 10000 + step, at, adv(true), adv(false), &now)) return;
        }
    }
}

// ---- the boxes ----

namespace {

void BoxState(std::mt19937 &r, Buf &b)
{
    Fill(r, b);
    const int32_t base = r() % 6 == 0 ? int32_t(r()) : Small(r, 30);
    for (unsigned off = 0; off < 0x24; off += 4) b.I(off) = base + Small(r, 3);
    if (r() % 4 == 0) b.I(0x1c) = b.I(0x10); // cur.y on max.y
    if (r() % 4 == 0) b.I(0x18) = b.I(0x0c); // cur.x on max.x
    const uint32_t k = r() % 4;
    if (k == 0) b.I(0x24) = int32_t(1u << (r() % 8));
    else if (k == 1) b.B(0x24) = uint8_t(1u << (r() % 8));
    else if (k == 2) b.I(0x24) = int32_t(r() % 0x48);
}

} // namespace

DIFF_TEST(WorldIterators_TileBox, 0x7f1840)
{
    auto ctor = ctx.Original<Ctor2>(0x7f12a0);
    auto ctorQ = ctx.Original<Ctor2>(0x7f0f60);
    auto ctorB = ctx.Original<Ctor2>(0x7f1620);
    auto next = ctx.Original<Step>(0x7f1840);
    auto nextQ = ctx.Original<Step>(0x7f1800);
    auto nextB = ctx.Original<Step>(0x7f1880);
    auto done = ctx.Original<Pred>(0x7f18e0);
    auto get = ctx.Original<Getter>(0x7f1a10);
    auto getQ = ctx.Original<Getter>(0x7f1970);
    auto getB = ctx.Original<Getter>(0x7f1ac0);
    auto reset = ctx.Original<Proc>(0x7f1d20);
    std::mt19937 r(0x7f1840);
    for (int i = 0; i < 110000; ++i) {
        Buf init;
        BoxState(r, init);
        const unsigned which = unsigned(i % 11);
        auto side = [&](bool orig) {
            return [&, orig](Buf &b) -> uint32_t {
                auto *it = reinterpret_cast<TileBoxIterator *>(b.obj());
                const auto *c = reinterpret_cast<const TileCoord *>(b.out());
                switch (which) {
                case 0: return Ptr(orig ? ctor(it, c, c + 1) : it->Construct(c, c + 1), b);
                case 1: return Ptr(orig ? ctorQ(it, c, c + 1) : it->ConstructQuadrants(c, c + 1), b);
                case 2: return Ptr(orig ? ctorB(it, c, c + 1) : it->ConstructBits(c, c + 1), b);
                case 3: return Ptr(orig ? next(it) : it->Next(), b);
                case 4: return Ptr(orig ? nextQ(it) : it->NextQuadrant(), b);
                case 5: return Ptr(orig ? nextB(it) : it->NextBit(), b);
                case 6: return uint8_t(orig ? done(it) : it->Done());
                case 7: return Ptr(orig ? get(it, b.out()) : it->Get(reinterpret_cast<TileCoord *>(b.out())), b);
                case 8:
                    return Ptr(orig ? getQ(it, b.out()) : it->GetQuadrant(reinterpret_cast<TileCoordQuadrant *>(b.out())), b);
                case 9: return Ptr(orig ? getB(it, b.out()) : it->GetBit(reinterpret_cast<TileCoordBit *>(b.out())), b);
                default: orig ? reset(it) : it->Reset(); return 0;
                }
            };
        };
        if (!Same(ctx, "TileBox", i, init, side(true), side(false))) return;
    }
}

DIFF_TEST(WorldIterators_TileBoxWorld, 0x7f12f0)
{
    using CtorLevel = void *(__thiscall *)(void *, int32_t, void *);
    using CtorAll = void *(__thiscall *)(void *, void *);
    auto level = ctx.Original<CtorLevel>(0x7f12f0);
    auto levelQ = ctx.Original<CtorLevel>(0x7f0fc0);
    auto corners = ctx.Original<CtorLevel>(0x7f1530);
    auto all = ctx.Original<CtorAll>(0x7f13e0);
    auto allQ = ctx.Original<CtorAll>(0x7f10b0);
    worldfx::Scope scope;
    worldfx::gWorldDb.Reset(1);
    worldfx::Fake other;
    other.Reset(2);
    scope.Set(worldfx::kWorldDbCell, &worldfx::gWorldDb);
    std::mt19937 r(0x7f12f0);
    for (int i = 0; i < 20000; ++i) {
        // GetMinX, GetMinY, GetMaxX, GetMaxY, GetMinLevel, GetMaxLevel (slots 81-84, 88, 89), no arguments.
        static const unsigned kSlotsUsed[] = {81, 82, 83, 84, 88, 89};
        for (worldfx::Fake *f : {&worldfx::gWorldDb, &other})
            for (unsigned s : kSlotsUsed) f->Set(s, 0, uint32_t(Coord(r)));
        Buf init;
        Fill(r, init);
        const int32_t lv = Coord(r);
        void *db = r() % 2 ? nullptr : &other;
        const unsigned which = unsigned(i % 5);
        auto side = [&](bool orig) {
            return [&, orig](Buf &b) -> uint32_t {
                auto *it = reinterpret_cast<TileBoxIterator *>(b.obj());
                auto *w = static_cast<nTSWorld::cIWorldDB *>(db);
                switch (which) {
                case 0: return Ptr(orig ? level(it, lv, db) : it->ConstructLevel(lv, w), b);
                case 1: return Ptr(orig ? levelQ(it, lv, db) : it->ConstructLevelQuadrants(lv, w), b);
                case 2: return Ptr(orig ? corners(it, lv, db) : it->ConstructLevelCorners(lv, w), b);
                case 3: return Ptr(orig ? all(it, db) : it->ConstructAll(w), b);
                default: return Ptr(orig ? allQ(it, db) : it->ConstructAllQuadrants(w), b);
                }
            };
        };
        if (!Same(ctx, "TileBox world", i, init, side(true), side(false))) return;
    }
}

// ---- triangles, rings, corners, edges, quadrant bits ----

DIFF_TEST(WorldIterators_TileTriangle, 0x7f11b0)
{
    auto ctor = ctx.Original<void *(__thiscall *)(void *, const void *)>(0x7f11b0);
    auto destroy = ctx.Original<Proc>(0x7f1670);
    auto edge = ctx.Original<Getter>(0x7f1990);
    auto done = ctx.Original<Pred>(0x7f18f0);
    std::mt19937 r(0x7f11b0);
    for (int i = 0; i < 40000; ++i) {
        Buf init;
        Fill(r, init);
        const unsigned which = unsigned(i % 4);
        if (which == 0) {
            TileCoordAt(init.out(), Coord(r), Coord(r), Coord(r));
            init.out()[12] = r() % 3 == 0 ? uint8_t(r()) : uint8_t(1u << (r() % 4));
        }
        if (which == 2) init.I(0) = int32_t(r() % 3);
        if (which == 3) init.I(0) = r() % 2 ? Small(r, 5) : int32_t(r());
        auto side = [&](bool orig) {
            return [&, orig](Buf &b) -> uint32_t {
                auto *it = reinterpret_cast<TileTriangle *>(b.obj());
                switch (which) {
                case 0: {
                    const auto *t = reinterpret_cast<const TileCoordQuadrant *>(b.out());
                    return Ptr(orig ? ctor(it, t) : it->Construct(t), b);
                }
                case 1: orig ? destroy(it) : it->Destroy(); return 0;
                case 2: return Ptr(orig ? edge(it, b.out()) : it->Edge(reinterpret_cast<PointCoord *>(b.out())), b);
                default: return uint8_t(orig ? done(it) : it->Done());
                }
            };
        };
        if (!Same(ctx, "TileTriangle", i, init, side(true), side(false))) return;
    }
}

DIFF_TEST(WorldIterators_Rings, 0x7f1a80)
{
    auto ringCtor = ctx.Original<void *(__thiscall *)(void *, const void *)>(0x7f14e0);
    auto ringGet = ctx.Original<Getter>(0x7f1a80);
    auto ringDone = ctx.Original<Pred>(0x7f1920);
    auto cornerGet = ctx.Original<Getter>(0x7f19e0);
    auto cornerDone = ctx.Original<Pred>(0x7f1900);
    auto edgeGet = ctx.Original<Getter>(0x7f1a40);
    auto bitNext = ctx.Original<Step>(0x7f1870);
    auto bitDone = ctx.Original<Pred>(0x7f1910);
    std::mt19937 r(0x7f1a80);
    for (int i = 0; i < 80000; ++i) {
        Buf init;
        Fill(r, init);
        const unsigned which = unsigned(i % 8);
        if (which == 0) TileCoordAt(init.out(), Coord(r), Coord(r), Coord(r));
        if (which == 1) {
            init.I(0) = int32_t(r() % 8);
            const float c[2] = {float(Small(r, 200)) * 0.5f, r() % 4 ? float(Small(r, 200)) : float(int32_t(r()))};
            std::memcpy(init.obj() + 4, c, sizeof(c));
        }
        if (which == 3 || which == 5) init.I(0) = int32_t(r() % 4);
        if (which == 3 || which == 5) {
            init.I(4) = Coord(r);
            init.I(8) = Coord(r);
        }
        if (which == 2 || which == 4 || which == 7) {
            const uint32_t k = r() % 3;
            init.I(0) = k == 0 ? int32_t(r() % 12) : k == 1 ? int32_t(1u << (r() % 32)) : int32_t(r());
        }
        auto side = [&](bool orig) {
            return [&, orig](Buf &b) -> uint32_t {
                void *o = b.obj();
                switch (which) {
                case 0: {
                    const auto *t = reinterpret_cast<const TileCoord *>(b.out());
                    return Ptr(orig ? ringCtor(o, t) : static_cast<RingPointIterator *>(o)->Construct(t), b);
                }
                case 1:
                    return Ptr(orig ? ringGet(o, b.out())
                                    : static_cast<RingPointIterator *>(o)->Get(reinterpret_cast<PointCoord *>(b.out())),
                               b);
                case 2: return uint8_t(orig ? ringDone(o) : static_cast<RingPointIterator *>(o)->Done());
                case 3:
                    return Ptr(orig ? cornerGet(o, b.out())
                                    : static_cast<TileCornerIterator *>(o)->Get(reinterpret_cast<TileCoord *>(b.out())),
                               b);
                case 4: return uint8_t(orig ? cornerDone(o) : static_cast<TileCornerIterator *>(o)->Done());
                case 5:
                    return Ptr(orig ? edgeGet(o, b.out())
                                    : static_cast<TileEdgeIterator *>(o)->Get(reinterpret_cast<TileCoordQuadrant *>(b.out())),
                               b);
                case 6: return Ptr(orig ? bitNext(o) : static_cast<QuadrantBitIterator *>(o)->Next(), b);
                default: return uint8_t(orig ? bitDone(o) : static_cast<QuadrantBitIterator *>(o)->Done());
                }
            };
        };
        if (!Same(ctx, "Rings", i, init, side(true), side(false))) return;
    }
}
