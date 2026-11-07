// nTSWorld::cWallEdge (lane VR, campaign world-lot WL-0d): differential tests, original (by RVA) against ours on equal
// edge rigs: the constructor 0x820be0, the destructor body 0x820c10, the deleting destructor 0x820c20, and every slot of
// the vtable 0xe053f4: the bodies 0x821070 (3), 0x821080 (5), 0x821050 (6, 7), 0x820cf0 (8, 9), 0x821090 (10, 11), the
// compositions 0x820d90 0x820d70 0x820c60 0x820c40 0x820ca0 0x820c80 0x820d30 0x820d10 0x820d60 0x820d50 0x821010
// 0x820ff0 0x821040 0x821030 (14-27), the angles 0x820db0 (30), 0x820cd0 (31), 0x820ee0 (32), 0x820ce0 (33), 0x820cc0
// (36), and the folded one-word bodies 0x20daf0 (0), 0x14a260 (1), 0x1eeb80 (2), 0x149c60 (4), 0x145770 (12, 13),
// 0x149d00 (28), 0x46390 (29), 0x1458b0 (34), 0x149cf0 (35), which are ours only in our vtable.
// - A rig is 16 edges (four sections of four), six fake vertices (their slot 5 answers the position) and the same random
//   links on each side. Three kinds: the game's vtable (the original runs by RVA), ours (each slot called through our own
//   vtable), and the game's vtable with a trap in the slot under test (the adapter must not dispatch its own slot).
// - After each call the whole rig is dumped (pointers as edge / vertex numbers) and compared with the result.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "shape.h"
#include "world/cWallEdge.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include <random>
#include <vector>

namespace {
using nTSWorld::cIWallEdge;
using nTSWorld::cIWallVertex;
using nTSWorld::cWallEdge;
using nTSWorld::cWallEdgeAdapters;
using Blob = std::vector<uint32_t>;

constexpr uint32_t kVTable = cWallEdge::kVTable;
constexpr int kEdges = 16, kVerts = 6, kSlots = 38;

// ---- fake vertices: slot 5 answers the position ----

struct FakeVertex {
    void *const *vt;
    float pos[3];
};
float *__fastcall FakePosition(FakeVertex *self, void *) { return self->pos; }
uint32_t __fastcall FakeOther(void *, void *) { return 0xdead0001; }
void *const *FakeVTable()
{
    static void *table[10];
    for (void *&slot : table) slot = reinterpret_cast<void *>(&FakeOther);
    table[5] = reinterpret_cast<void *>(&FakePosition);
    return table;
}

// ---- traps ----

int gTrapHits = 0;
uint32_t __fastcall Trap0(void *, void *) { ++gTrapHits; return 0; }
uint32_t __fastcall Trap1(void *, void *, uint32_t) { ++gTrapHits; return 0; }

// ---- rigs ----

enum Kind { kGame, kOurs, kTrapped };

struct Rig {
    Kind kind;
    cWallEdge *e = nullptr; // 16 edges in a row
    FakeVertex v[kVerts];
    void *trapTable[kSlots];

    uint32_t Enc(const void *p) const
    {
        const auto *b = static_cast<const uint8_t *>(p);
        const auto *eb = reinterpret_cast<const uint8_t *>(e);
        if (b >= eb && b < eb + kEdges * sizeof(cWallEdge)) return uint32_t(b - eb) / sizeof(cWallEdge);
        for (int i = 0; i < kVerts; ++i)
            if (p == &v[i]) return 100 + i;
        return shape::P(p);
    }
    uint32_t VtCode(void *const *vt) const
    {
        if (vt == shape::GameVTable(kVTable) || vt == static_cast<void *const *>(trapTable)) return 1;
        static const uint32_t kFace[] = {0};
        if (vt == shape::Ours<cWallEdge>(kFace).face[0]) return 1;
        return shape::P(vt);
    }
};

float Coord(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return std::numeric_limits<float>::quiet_NaN();
    case 1: return float(int(rng() % 5) - 2) * 0.5f;
    case 2: return float(rng() % 1000) / 7.0f - 70.0f;
    default: return float(int(rng() % 7) - 3);
    }
}

void Build(Rig &r, Kind kind, uint32_t seed, int trapSlot = -1)
{
    r.kind = kind;
    std::mt19937 rng(seed);
    r.e = static_cast<cWallEdge *>(::operator new(kEdges * sizeof(cWallEdge)));
    for (int i = 0; i < kVerts; ++i) {
        r.v[i].vt = FakeVTable();
        for (float &c : r.v[i].pos) c = Coord(rng);
        if (rng() % 4 == 0) r.v[i].pos[1] = r.v[i].pos[1 - 1]; // same y and x as a grid neighbour more often
    }
    for (int i = 0; i < kEdges; ++i) {
        cWallEdge *edge = new (&r.e[i]) cWallEdge();
        edge->mFlag = uint8_t(rng());
        edge->mIndex = i % 4;
        edge->mOnext = &r.e[rng() % kEdges];
        edge->mVertex = reinterpret_cast<cIWallVertex *>(&r.v[rng() % kVerts]);
        edge->mOwner = reinterpret_cast<void *>(uintptr_t(0x1000 + rng() % 8));
    }
    if (kind != kOurs) {
        void **vt = shape::GameVTable(kVTable);
        if (kind == kTrapped) {
            std::memcpy(r.trapTable, vt, sizeof(r.trapTable));
            const bool oneArg = trapSlot <= 5 || trapSlot == 37;
            r.trapTable[trapSlot] = oneArg ? reinterpret_cast<void *>(&Trap1) : reinterpret_cast<void *>(&Trap0);
            vt = r.trapTable;
        }
        for (int i = 0; i < kEdges; ++i) shape::VTableAt(&r.e[i], 0) = vt;
    }
}

void Free(Rig &r)
{
    ::operator delete(r.e, kEdges * sizeof(cWallEdge));
    r.e = nullptr;
}

void Dump(Blob &out, const Rig &r)
{
    for (int i = 0; i < kEdges; ++i) {
        const cWallEdge &e = r.e[i];
        out.insert(out.end(), {r.VtCode(shape::VTableAt(const_cast<cWallEdge *>(&e), 0)), e.mFlag, uint32_t(e.mIndex), r.Enc(e.mOnext),
                               r.Enc(e.mVertex), r.Enc(e.mOwner)});
    }
}

uint32_t FloatBits(float f)
{
    if (std::isnan(f)) return 0x7fc00000; // a NaN is a NaN
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// ---- the operations ----

using Rd = std::function<uint32_t(Rig &, cWallEdge *, uint32_t)>;

struct Op {
    const char *name;
    uint32_t slot, rva;
    Rd orig, vt, adapter; // each answers the result as a word
};

cIWallEdge *OrigPtr0(difftest::Context &ctx, uint32_t rva, cWallEdge *self)
{
    return ctx.Original<cIWallEdge *(__thiscall *)(void *)>(rva)(self);
}

std::vector<Op> Ops(difftest::Context &ctx)
{
    std::vector<Op> ops;
    auto ptr0 = [&](const char *name, uint32_t slot, uint32_t rva, cIWallEdge *(*via)(cIWallEdge *),
                    cIWallEdge *(__fastcall *adapter)(cWallEdge *, void *)) {
        ops.push_back({name, slot, rva,
                       [=, &ctx](Rig &r, cWallEdge *s, uint32_t) { return r.Enc(OrigPtr0(ctx, rva, s)); },
                       [=](Rig &r, cWallEdge *s, uint32_t) { return r.Enc(via(s)); },
                       [=](Rig &r, cWallEdge *s, uint32_t) { return r.Enc(adapter(s, nullptr)); }});
    };
#define PTR0(name, slot, rva, call)                                                                                    \
    ptr0(#name, slot, rva, [](cIWallEdge *p) -> cIWallEdge * { return p->call(); }, &cWallEdgeAdapters::name)
    PTR0(Rot, 6, 0x821050, Rot);
    PTR0(InvRot, 8, 0x820cf0, InvRot);
    PTR0(Sym, 10, 0x821090, Sym);
    PTR0(RotOnextRot, 14, 0x820d90, RotOnextRot);
    PTR0(RotOnextRotC, 15, 0x820d70, RotOnextRotC);
    PTR0(SymOnextSym, 16, 0x820c60, SymOnextSym);
    PTR0(SymOnextSymC, 17, 0x820c40, SymOnextSymC);
    PTR0(InvRotOnextInvRot, 18, 0x820ca0, InvRotOnextInvRot);
    PTR0(InvRotOnextInvRotC, 19, 0x820c80, InvRotOnextInvRotC);
    PTR0(InvRotOnextRot, 20, 0x820d30, InvRotOnextRot);
    PTR0(InvRotOnextRotC, 21, 0x820d10, InvRotOnextRotC);
    PTR0(OnextSym, 22, 0x820d60, OnextSym);
    PTR0(OnextSymC, 23, 0x820d50, OnextSymC);
    PTR0(RotOnextInvRot, 24, 0x821010, RotOnextInvRot);
    PTR0(RotOnextInvRotC, 25, 0x820ff0, RotOnextInvRotC);
    PTR0(SymOnext, 26, 0x821040, SymOnext);
    PTR0(SymOnextC, 27, 0x821030, SymOnextC);
#undef PTR0
    ops.push_back({"DestVertex", 36, 0x820cc0,
                   [&ctx](Rig &r, cWallEdge *s, uint32_t) { return r.Enc(ctx.Original<void *(__thiscall *)(void *)>(0x820cc0)(s)); },
                   [](Rig &r, cWallEdge *s, uint32_t) { return r.Enc(static_cast<cIWallEdge *>(s)->DestVertex()); },
                   [](Rig &r, cWallEdge *s, uint32_t) { return r.Enc(cWallEdgeAdapters::DestVertex(s, nullptr)); }});
    ops.push_back({"SetDestVertex", 3, 0x821070,
                   [&ctx](Rig &r, cWallEdge *s, uint32_t a) { return r.Enc(ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x821070)(s, a)); },
                   [](Rig &r, cWallEdge *s, uint32_t a) { return r.Enc(static_cast<cIWallEdge *>(s)->SetDestVertex(reinterpret_cast<cIWallVertex *>(uintptr_t(a)))); },
                   [](Rig &r, cWallEdge *s, uint32_t a) { return r.Enc(cWallEdgeAdapters::SetDestVertex(s, nullptr, reinterpret_cast<cIWallVertex *>(uintptr_t(a)))); }});
    ops.push_back({"SetFlag", 5, 0x821080,
                   [&ctx](Rig &, cWallEdge *s, uint32_t a) { ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x821080)(s, a); return 0u; },
                   [](Rig &, cWallEdge *s, uint32_t a) { static_cast<cIWallEdge *>(s)->SetFlag(a); return 0u; },
                   [](Rig &, cWallEdge *s, uint32_t a) { cWallEdgeAdapters::SetFlag(s, nullptr, a); return 0u; }});
    auto angle = [&](const char *name, uint32_t slot, uint32_t rva, float (*via)(cIWallEdge *), float(__fastcall *adapter)(cWallEdge *, void *)) {
        ops.push_back({name, slot, rva,
                       [=, &ctx](Rig &, cWallEdge *s, uint32_t) { return FloatBits(ctx.Original<float(__thiscall *)(void *)>(rva)(s)); },
                       [=](Rig &, cWallEdge *s, uint32_t) { return FloatBits(via(s)); },
                       [=](Rig &, cWallEdge *s, uint32_t) { return FloatBits(adapter(s, nullptr)); }});
    };
    angle("Angle", 30, 0x820db0, [](cIWallEdge *p) { return p->Angle(); }, &cWallEdgeAdapters::Angle);
    angle("ReverseAngle", 31, 0x820cd0, [](cIWallEdge *p) { return p->ReverseAngle(); }, &cWallEdgeAdapters::ReverseAngle);
    auto degrees = [&](const char *name, uint32_t slot, uint32_t rva, int32_t (*via)(cIWallEdge *), int32_t(__fastcall *adapter)(cWallEdge *, void *)) {
        ops.push_back({name, slot, rva,
                       [=, &ctx](Rig &, cWallEdge *s, uint32_t) { return uint32_t(ctx.Original<int32_t(__thiscall *)(void *)>(rva)(s)); },
                       [=](Rig &, cWallEdge *s, uint32_t) { return uint32_t(via(s)); },
                       [=](Rig &, cWallEdge *s, uint32_t) { return uint32_t(adapter(s, nullptr)); }});
    };
    degrees("AngleDegrees", 32, 0x820ee0, [](cIWallEdge *p) { return p->AngleDegrees(); }, &cWallEdgeAdapters::AngleDegrees);
    degrees("ReverseAngleDegrees", 33, 0x820ce0, [](cIWallEdge *p) { return p->ReverseAngleDegrees(); }, &cWallEdgeAdapters::ReverseAngleDegrees);
    return ops;
}

uint32_t ArgFor(const Rig &r, std::mt19937 &rng)
{
    return rng() % 3 ? shape::P(&r.v[rng() % kVerts]) : uint32_t(rng());
}

// One op, every edge of a few random rigs: the three kinds' results and rigs compared.
void RunOp(difftest::Context &ctx, const Op &op, uint32_t rigs)
{
    for (uint32_t seed = 0; seed < rigs; ++seed) {
        Rig game, ours, trapped;
        Build(game, kGame, seed);
        Build(ours, kOurs, seed);
        Build(trapped, kTrapped, seed, int(op.slot));
        // The vertex pointers the argument may take differ by rig: the same index in each.
        for (int i = 0; i < kEdges; ++i) {
            std::mt19937 rng(seed * 64 + i);
            const uint32_t pick = rng() % (kVerts + 2);
            auto arg = [&](const Rig &r) { return pick < kVerts ? shape::P(&r.v[pick]) : 0x77000000u + pick; };
            const uint32_t flagArg = uint32_t(rng());
            const bool isFlag = op.slot == 5;
            Blob d[3];
            uint32_t res[3];
            Rig *rigsArr[3] = {&game, &ours, &trapped};
            gTrapHits = 0;
            res[0] = op.orig(game, &game.e[i], isFlag ? flagArg : arg(game));
            res[1] = op.vt(ours, &ours.e[i], isFlag ? flagArg : arg(ours));
            res[2] = op.adapter(trapped, &trapped.e[i], isFlag ? flagArg : arg(trapped));
            if (gTrapHits) ctx.Fail("%s: the adapter dispatched its own slot %u", op.name, op.slot);
            for (int k = 0; k < 3; ++k) Dump(d[k], *rigsArr[k]);
            ++ctx.cases;
            if (res[0] != res[1]) ctx.Fail("%s seed %u edge %d: original 0x%08x, vtable 0x%08x", op.name, seed, i, res[0], res[1]);
            if (res[0] != res[2]) ctx.Fail("%s seed %u edge %d: original 0x%08x, adapter 0x%08x", op.name, seed, i, res[0], res[2]);
            if (d[0] != d[1]) ctx.Fail("%s seed %u edge %d: rig differs after the vtable call", op.name, seed, i);
            if (d[0] != d[2]) ctx.Fail("%s seed %u edge %d: rig differs after the adapter call", op.name, seed, i);
        }
        Free(game);
        Free(ours);
        Free(trapped);
    }
}

void RunByName(difftest::Context &ctx, const char *name, uint32_t rigs)
{
    for (const Op &op : Ops(ctx))
        if (!std::strcmp(op.name, name)) RunOp(ctx, op, rigs);
}

// Rot / InvRot / Sym with any index: only the pointer is compared (offset from this), nothing is read.
void IndexCases(difftest::Context &ctx, const char *name, uint32_t rva)
{
    const int32_t indices[] = {std::numeric_limits<int32_t>::min(), -1000, -2, -1, 0, 1, 2, 3, 4, 5, 1000, std::numeric_limits<int32_t>::max()};
    for (const Op &op : Ops(ctx)) {
        if (std::strcmp(op.name, name)) continue;
        for (int32_t index : indices) {
            Rig game, ours, trapped;
            Build(game, kGame, 7);
            Build(ours, kOurs, 7);
            Build(trapped, kTrapped, 7, int(op.slot));
            game.e[8].mIndex = ours.e[8].mIndex = trapped.e[8].mIndex = index;
            const uint32_t a = op.orig(game, &game.e[8], 0), b = op.vt(ours, &ours.e[8], 0), c = op.adapter(trapped, &trapped.e[8], 0);
            // Enc of a pointer outside the rig is the raw address: compare as byte offsets from the edge.
            const uint32_t base[3] = {shape::P(&game.e[8]), shape::P(&ours.e[8]), shape::P(&trapped.e[8])};
            const uint32_t raw[3] = {a, b, c};
            uint32_t off[3];
            for (int k = 0; k < 3; ++k) off[k] = raw[k] < 100 + kVerts ? raw[k] * 0x18 - 8 * 0x18 : raw[k] - base[k];
            ++ctx.cases;
            if (off[0] != off[1] || off[0] != off[2]) ctx.Fail("%s (0x%x) index %d: offsets original %d, vtable %d, adapter %d", name, rva, index, int(off[0]), int(off[1]), int(off[2]));
            Free(game);
            Free(ours);
            Free(trapped);
        }
    }
}

// ---- the folded one-word slots: called through our vtable against the shared body in the image ----

void FoldedCases(difftest::Context &ctx)
{
    struct One {
        const char *name;
        uint32_t slot, rva;
        int kind; // 0 setter (answers the value), 1 getter
    };
    const One ones[] = {{"SetOwner", 0, 0x20daf0, 0}, {"SetIndex", 1, 0x14a260, 0}, {"SetVertex", 2, 0x1eeb80, 0}, {"SetOnext", 4, 0x149c60, 0},
                        {"Onext", 12, 0x145770, 1},   {"OnextC", 13, 0x145770, 1}, {"Owner", 28, 0x149d00, 1}, {"Index", 29, 0x46390, 1},
                        {"Flag", 34, 0x1458b0, 1},    {"Vertex", 35, 0x149cf0, 1}};
    for (const One &one : ones) for (uint32_t seed = 0; seed < 8; ++seed) {
        Rig game, ours;
        Build(game, kGame, seed);
        Build(ours, kOurs, seed);
        std::mt19937 rng(seed + one.slot);
        const int i = int(rng() % kEdges);
        const uint32_t pick = rng() % kVerts;
        uint32_t a = 0, b = 0;
        cIWallEdge *p = &ours.e[i];
        const int32_t small = int32_t(rng() % 9) - 4;
        auto word = [&](const Rig &r, int slot) -> uint32_t {
            switch (slot) {
            case 0: return 0x1004;
            case 1: return uint32_t(small);
            case 2: return shape::P(&r.v[pick]);
            default: return shape::P(&r.e[(pick * 3) % kEdges]);
            }
        };
        if (one.kind == 0) {
            a = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(one.rva)(&game.e[i], word(game, one.slot));
            switch (one.slot) {
            case 0: b = shape::P(p->SetOwner(reinterpret_cast<void *>(uintptr_t(word(ours, 0))))); break;
            case 1: b = uint32_t(p->SetIndex(int32_t(word(ours, 1)))); break;
            case 2: b = shape::P(p->SetVertex(reinterpret_cast<cIWallVertex *>(uintptr_t(word(ours, 2))))); break;
            default: b = shape::P(p->SetOnext(reinterpret_cast<cIWallEdge *>(uintptr_t(word(ours, 4))))); break;
            }
            if (one.slot != 1) {
                a = game.Enc(reinterpret_cast<void *>(uintptr_t(a)));
                b = ours.Enc(reinterpret_cast<void *>(uintptr_t(b)));
            }
        } else {
            a = ctx.Original<uint32_t(__thiscall *)(void *)>(one.rva)(&game.e[i]);
            switch (one.slot) {
            case 12: b = ours.Enc(p->Onext()); a = game.Enc(reinterpret_cast<void *>(uintptr_t(a))); break;
            case 13: b = ours.Enc(p->OnextC()); a = game.Enc(reinterpret_cast<void *>(uintptr_t(a))); break;
            case 28: b = shape::P(p->Owner()); break;
            case 29: b = uint32_t(p->Index()); break;
            case 34: b = p->Flag(); a &= 0xff; break;
            default: b = ours.Enc(p->Vertex()); a = game.Enc(reinterpret_cast<void *>(uintptr_t(a))); break;
            }
        }
        Blob dg, dow;
        Dump(dg, game);
        Dump(dow, ours);
        ++ctx.cases;
        if (a != b) ctx.Fail("%s (slot %u) seed %u: original 0x%08x, ours 0x%08x", one.name, one.slot, seed, a, b);
        if (dg != dow) ctx.Fail("%s (slot %u) seed %u: rig differs", one.name, one.slot, seed);
        Free(game);
        Free(ours);
    }
}

// ---- construction and destruction ----

void CtorCases(difftest::Context &ctx)
{
    for (uint32_t fill : {0u, 0xcdu, 0x5au}) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            void *block = ::operator new(sizeof(cWallEdge));
            std::memset(block, int(fill), sizeof(cWallEdge));
            auto *e = static_cast<cWallEdge *>(block);
            void *r = side ? static_cast<void *>(cWallEdgeAdapters::Construct(e, nullptr))
                           : ctx.Original<void *(__thiscall *)(void *)>(0x820be0)(block);
            blobs[side] = {uint32_t(r == block), e->mFlag, uint32_t(e->mIndex), shape::P(e->mOnext), shape::P(e->mVertex), shape::P(e->mOwner)};
            static const uint32_t kFace[] = {0};
            const uint32_t word = *reinterpret_cast<uint32_t *>(e);
            blobs[side].push_back(word == shape::P(shape::GameVTable(kVTable)) || word == shape::P(shape::Ours<cWallEdge>(kFace).face[0]) ? 1u : word);
            ::operator delete(block, sizeof(cWallEdge));
        }
        if (blobs[0] != blobs[1]) ctx.Fail("constructor over 0x%02x: the edge differs", fill);
        ++ctx.cases;
    }
}

void DtorCases(difftest::Context &ctx)
{
    static const uint32_t kFace[] = {0};
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (int body = 0; body < 2; ++body) for (uint32_t seed = 0; seed < 3; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Rig r;
            Build(r, kGame, seed);
            cWallEdge *e = &r.e[5];
            // Detach the edge from the rig's array so a free of it is its own block.
            auto *own = static_cast<cWallEdge *>(::operator new(sizeof(cWallEdge)));
            std::memcpy(static_cast<void *>(own), e, sizeof(cWallEdge));
            difftest::FreeRecorder rec;
            void *ret = nullptr;
            if (body) {
                if (side) cWallEdgeAdapters::DestructBody(own, nullptr);
                else ctx.Original<void(__thiscall *)(void *)>(0x820c10)(own);
                ret = own;
            } else {
                ret = side ? static_cast<void *>(cWallEdgeAdapters::DeletingDestructor(own, nullptr, flags))
                           : ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x820c20)(own, flags);
            }
            rec.Stop();
            blobs[side] = {uint32_t(ret == own), uint32_t(rec.Freed(own))};
            size_t sizes[8]{};
            const size_t n = rec.SortedSizes(sizes, 8);
            blobs[side].push_back(uint32_t(n));
            for (size_t k = 0; k < n && k < 8; ++k) blobs[side].push_back(uint32_t(sizes[k]));
            if (!rec.Freed(own)) {
                const uint32_t word = *reinterpret_cast<uint32_t *>(own);
                blobs[side].push_back(word == shape::P(shape::GameVTable(kVTable)) || word == shape::P(shape::Ours<cWallEdge>(kFace).face[0]) ? 1u : word);
                blobs[side].insert(blobs[side].end(), {own->mFlag, uint32_t(own->mIndex), r.Enc(own->mOnext), r.Enc(own->mVertex), r.Enc(own->mOwner)});
                ::operator delete(own, sizeof(cWallEdge));
            }
            Free(r);
        }
        if (blobs[0] != blobs[1]) {
            char text[2][160]{};
            for (int side = 0; side < 2; ++side)
                for (size_t k = 0, at = 0; k < blobs[side].size() && at < sizeof(text[side]) - 12; ++k)
                    at += snprintf(text[side] + at, sizeof(text[side]) - at, "%x ", blobs[side][k]);
            ctx.Fail("%s flags %u seed %u: frees / fields differ: game [%s] ours [%s]", body ? "destructor body" : "deleting destructor",
                     flags, seed, text[0], text[1]);
        }
        ++ctx.cases;
    }
}

} // namespace

DIFF_TEST(WallEdge_Constructor, 0x820be0) { CtorCases(ctx); }
DIFF_TEST(WallEdge_Destructors, 0x820c20) { DtorCases(ctx); }
DIFF_TEST(WallEdge_Folded, 0x145770) { FoldedCases(ctx); }
DIFF_TEST(WallEdge_Rot, 0x821050) { RunByName(ctx, "Rot", 40); IndexCases(ctx, "Rot", 0x821050); }
DIFF_TEST(WallEdge_InvRot, 0x820cf0) { RunByName(ctx, "InvRot", 40); IndexCases(ctx, "InvRot", 0x820cf0); }
DIFF_TEST(WallEdge_Sym, 0x821090) { RunByName(ctx, "Sym", 40); IndexCases(ctx, "Sym", 0x821090); }
DIFF_TEST(WallEdge_SetDestVertex, 0x821070) { RunByName(ctx, "SetDestVertex", 40); }
DIFF_TEST(WallEdge_SetFlag, 0x821080) { RunByName(ctx, "SetFlag", 40); }
DIFF_TEST(WallEdge_RotOnextRot, 0x820d90) { RunByName(ctx, "RotOnextRot", 40); }
DIFF_TEST(WallEdge_RotOnextRotC, 0x820d70) { RunByName(ctx, "RotOnextRotC", 40); }
DIFF_TEST(WallEdge_SymOnextSym, 0x820c60) { RunByName(ctx, "SymOnextSym", 40); }
DIFF_TEST(WallEdge_SymOnextSymC, 0x820c40) { RunByName(ctx, "SymOnextSymC", 40); }
DIFF_TEST(WallEdge_InvRotOnextInvRot, 0x820ca0) { RunByName(ctx, "InvRotOnextInvRot", 40); }
DIFF_TEST(WallEdge_InvRotOnextInvRotC, 0x820c80) { RunByName(ctx, "InvRotOnextInvRotC", 40); }
DIFF_TEST(WallEdge_InvRotOnextRot, 0x820d30) { RunByName(ctx, "InvRotOnextRot", 40); }
DIFF_TEST(WallEdge_InvRotOnextRotC, 0x820d10) { RunByName(ctx, "InvRotOnextRotC", 40); }
DIFF_TEST(WallEdge_OnextSym, 0x820d60) { RunByName(ctx, "OnextSym", 40); }
DIFF_TEST(WallEdge_OnextSymC, 0x820d50) { RunByName(ctx, "OnextSymC", 40); }
DIFF_TEST(WallEdge_RotOnextInvRot, 0x821010) { RunByName(ctx, "RotOnextInvRot", 40); }
DIFF_TEST(WallEdge_RotOnextInvRotC, 0x820ff0) { RunByName(ctx, "RotOnextInvRotC", 40); }
DIFF_TEST(WallEdge_SymOnext, 0x821040) { RunByName(ctx, "SymOnext", 40); }
DIFF_TEST(WallEdge_SymOnextC, 0x821030) { RunByName(ctx, "SymOnextC", 40); }
DIFF_TEST(WallEdge_Angle, 0x820db0) { RunByName(ctx, "Angle", 400); }
DIFF_TEST(WallEdge_ReverseAngle, 0x820cd0) { RunByName(ctx, "ReverseAngle", 200); }
DIFF_TEST(WallEdge_AngleDegrees, 0x820ee0) { RunByName(ctx, "AngleDegrees", 400); }
DIFF_TEST(WallEdge_ReverseAngleDegrees, 0x820ce0) { RunByName(ctx, "ReverseAngleDegrees", 200); }
DIFF_TEST(WallEdge_DestVertex, 0x820cc0) { RunByName(ctx, "DestVertex", 40); }
