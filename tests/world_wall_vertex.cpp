// nTSWorld::cWallVertex (lane VR, campaign world-lot WL-0d): differential tests, original (by RVA) against ours on equal
// vertices and edge rigs: the constructors 0x81fd90 (position) and 0x81fdd0 (default), the destructor body 0x81fe10, the
// deleting destructor 0x81fe20, SetPosition 0x81ff30 (slot 1), AddEdge 0x81fe40 (2), RemoveEdge 0x81fef0 (3), EdgeForAngle
// 0x81fe90 (8), and the shared one-word bodies 0x1489e0 (0), 0x147a30 (4), 0x142840 (5), 0x148ba0 (6), 0x149d00 (7), which
// are ours only in our vtable.
// - Three routes per case: the original by RVA on a vertex with the game's vtable; ours through our own vtable; the adapter
//   on a game-vtable vertex whose vtable has a trap in the slot under test (the adapter must not dispatch its own slot).
// - The edges are 16 cWallEdge records (four sections of four) over six fake vertices whose slot 5 answers a position, with
//   the game's vtable on the original and adapter routes and ours on the vtable route; a ring of edges (Onext round to the
//   first) is linked for the vertex. After each call the vertex and every edge are dumped and compared.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "shape.h"
#include "world/cWallEdge.h"
#include "world/cWallVertex.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <vector>

namespace {
using nTSWorld::cIWallEdge;
using nTSWorld::cIWallVertex;
using nTSWorld::cWallEdge;
using nTSWorld::cWallVertex;
using nTSWorld::cWallVertexAdapters;
using Blob = std::vector<uint32_t>;

constexpr int kEdges = 16, kVerts = 6;
constexpr uint32_t kSize = sizeof(cWallVertex);

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

int gTrapHits = 0;
uint32_t __fastcall Trap(void *, void *, uint32_t) { ++gTrapHits; return 0; }

enum Kind { kGame, kOurs, kTrapped };

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}
float FromBits(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

float Coord(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return std::numeric_limits<float>::quiet_NaN();
    case 1: return float(int(rng() % 5) - 2) * 0.5f;
    case 2: return float(rng() % 1000) / 7.0f - 70.0f;
    default: return float(int(rng() % 7) - 3);
    }
}

struct Rig {
    Kind kind;
    cWallEdge *e = nullptr;
    FakeVertex v[kVerts];
    uint8_t *vertex = nullptr; // the vertex under test
    void *trapTable[10];
    int ring[5];
    int ringSize = 0;

    uint32_t Enc(const void *p) const
    {
        const auto *b = reinterpret_cast<const uint8_t *>(p);
        const auto *eb = reinterpret_cast<const uint8_t *>(e);
        if (p == nullptr) return 0;
        if (b >= eb && b < eb + kEdges * sizeof(cWallEdge)) return uint32_t(b - eb) / sizeof(cWallEdge);
        for (int i = 0; i < kVerts; ++i)
            if (p == &v[i]) return 100 + i;
        if (p == vertex) return 200;
        return shape::P(p);
    }
    uint32_t VtCode(const void *object, uint32_t gameVt) const
    {
        static const uint32_t kFace[] = {0};
        const uint32_t word = shape::Word(object, 0);
        if (word == shape::P(shape::GameVTable(gameVt)) || word == shape::P(trapTable)) return 1;
        if (gameVt == cWallEdge::kVTable && word == shape::P(shape::Ours<cWallEdge>(kFace).face[0])) return 1;
        if (gameVt == cWallVertex::kVTable && word == shape::P(shape::Ours<cWallVertex>(kFace).face[0])) return 1;
        return word;
    }
};

// Edges, fake vertices and a ring (the same for every route of one seed).
void BuildEdges(Rig &r, Kind kind, uint32_t seed)
{
    r.kind = kind;
    std::mt19937 rng(seed);
    r.e = static_cast<cWallEdge *>(::operator new(kEdges * sizeof(cWallEdge)));
    for (int i = 0; i < kVerts; ++i) {
        r.v[i].vt = FakeVTable();
        for (float &c : r.v[i].pos) c = Coord(rng);
    }
    for (int i = 0; i < kEdges; ++i) {
        cWallEdge *edge = new (&r.e[i]) cWallEdge();
        edge->mFlag = uint8_t(rng());
        edge->mIndex = i % 4;
        edge->mOnext = (rng() % 5 == 0) ? &r.e[i] : &r.e[rng() % kEdges];
        edge->mVertex = reinterpret_cast<cIWallVertex *>(&r.v[rng() % kVerts]);
        edge->mOwner = reinterpret_cast<void *>(uintptr_t(0x1000 + rng() % 8));
    }
    // A ring of 1-5 distinct edges: Onext round, back to the first.
    r.ringSize = 1 + int(rng() % 5);
    bool used[kEdges] = {};
    for (int j = 0; j < r.ringSize; ++j) {
        int pick;
        do pick = int(rng() % kEdges); while (used[pick]);
        used[pick] = true;
        r.ring[j] = pick;
    }
    for (int j = 0; j < r.ringSize; ++j) r.e[r.ring[j]].mOnext = &r.e[r.ring[(j + 1) % r.ringSize]];
    if (kind != kOurs)
        for (int i = 0; i < kEdges; ++i) shape::VTableAt(&r.e[i], 0) = shape::GameVTable(cWallEdge::kVTable);
}

struct VertexState {
    int32_t id;
    uint32_t pos[3];
    int first; // -1 none, else an edge index
    int32_t count;
};

VertexState StateFor(const Rig &r, uint32_t seed)
{
    std::mt19937 rng(seed * 77 + 5);
    VertexState s;
    s.id = int32_t(rng());
    for (uint32_t &p : s.pos) p = Bits(Coord(rng));
    switch (rng() % 3) {
    case 0: s.first = -1; s.count = 0; break;
    // (an empty vertex is skipped by EdgeForAngle below: the original dereferences the first edge)
    case 1: s.first = r.ring[0]; s.count = r.ringSize; break;
    default: s.first = r.ring[0]; s.count = int32_t(rng() % 7); break;
    }
    return s;
}

void BuildVertex(Rig &r, const VertexState &s, int trapSlot = -1)
{
    r.vertex = static_cast<uint8_t *>(::operator new(kSize));
    std::memset(r.vertex, 0xab, kSize);
    if (r.kind == kOurs) new (r.vertex) cWallVertex();
    else {
        shape::VTableAt(r.vertex, 0) = shape::GameVTable(cWallVertex::kVTable);
        if (r.kind == kTrapped) {
            std::memcpy(r.trapTable, shape::GameVTable(cWallVertex::kVTable), sizeof(r.trapTable));
            r.trapTable[trapSlot] = reinterpret_cast<void *>(&Trap);
            shape::VTableAt(r.vertex, 0) = r.trapTable;
        }
    }
    auto *v = reinterpret_cast<cWallVertex *>(r.vertex);
    v->mId = s.id;
    for (int i = 0; i < 3; ++i) std::memcpy(&v->mPos[i], &s.pos[i], 4);
    v->mFirst = s.first < 0 ? nullptr : &r.e[s.first];
    v->mCount = s.count;
}

void Free(Rig &r)
{
    ::operator delete(r.e, kEdges * sizeof(cWallEdge));
    ::operator delete(r.vertex, kSize);
    r.e = nullptr;
    r.vertex = nullptr;
}

void DumpVertex(Blob &out, const Rig &r)
{
    out.push_back(r.VtCode(r.vertex, cWallVertex::kVTable));
    out.push_back(shape::Word(r.vertex, 4));
    for (uint32_t off : {8u, 0xcu, 0x10u}) out.push_back(shape::Word(r.vertex, off));
    out.push_back(r.Enc(reinterpret_cast<const void *>(uintptr_t(shape::Word(r.vertex, 0x14)))));
    out.push_back(shape::Word(r.vertex, 0x18));
}

void Dump(Blob &out, const Rig &r)
{
    DumpVertex(out, r);
    for (int i = 0; i < kEdges; ++i) {
        const cWallEdge &e = r.e[i];
        out.insert(out.end(), {r.VtCode(&e, cWallEdge::kVTable), e.mFlag, uint32_t(e.mIndex), r.Enc(e.mOnext), r.Enc(e.mVertex), shape::P(e.mOwner)});
    }
}

enum OpKind { kAdd, kRemove, kAngle };

// One op on the three routes of one seed; returns false on a mismatch (already reported).
void RunOne(difftest::Context &ctx, OpKind op, uint32_t seed)
{
    std::mt19937 rng(seed + 1000 * op);
    Rig game, ours, trapped;
    BuildEdges(game, kGame, seed);
    BuildEdges(ours, kOurs, seed);
    BuildEdges(trapped, kTrapped, seed);
    VertexState state = StateFor(game, seed);
    if (op == kAngle && state.first < 0) state.first = game.ring[0]; // the original walks from the first edge
    const int slot = op == kAdd ? 2 : op == kRemove ? 3 : 8;
    BuildVertex(game, state);
    BuildVertex(ours, state);
    BuildVertex(trapped, state, slot);

    const int edgePick = rng() % kEdges;
    // The angle: random, a ring edge's exact angle, the special values.
    float angle;
    switch (rng() % 6) {
    case 0: angle = std::numeric_limits<float>::quiet_NaN(); break;
    case 1: angle = ours.e[game.ring[rng() % game.ringSize]].Angle(); break;
    case 2: angle = FromBits(rng() % 2 ? 0x40490fdbu : 0xc0490fdbu); break;
    case 3: angle = std::numeric_limits<float>::infinity() * (rng() % 2 ? 1 : -1); break;
    default: angle = float(int(rng() % 700) - 350) / 100.0f; break;
    }

    uint32_t res[3] = {0, 0, 0};
    gTrapHits = 0;
    Rig *rigs[3] = {&game, &ours, &trapped};
    auto *oursVertex = static_cast<cIWallVertex *>(reinterpret_cast<cWallVertex *>(ours.vertex));
    switch (op) {
    case kAdd:
        ctx.Original<void(__thiscall *)(void *, void *)>(0x81fe40)(game.vertex, &game.e[edgePick]);
        oursVertex->AddEdge(&ours.e[edgePick]);
        cWallVertexAdapters::AddEdge(reinterpret_cast<cWallVertex *>(trapped.vertex), nullptr, &trapped.e[edgePick]);
        break;
    case kRemove:
        ctx.Original<void(__thiscall *)(void *, void *)>(0x81fef0)(game.vertex, &game.e[edgePick]);
        oursVertex->RemoveEdge(&ours.e[edgePick]);
        cWallVertexAdapters::RemoveEdge(reinterpret_cast<cWallVertex *>(trapped.vertex), nullptr, &trapped.e[edgePick]);
        break;
    case kAngle:
        res[0] = game.Enc(ctx.Original<void *(__thiscall *)(void *, float)>(0x81fe90)(game.vertex, angle));
        res[1] = ours.Enc(oursVertex->EdgeForAngle(angle));
        res[2] = trapped.Enc(cWallVertexAdapters::EdgeForAngle(reinterpret_cast<cWallVertex *>(trapped.vertex), nullptr, angle));
        break;
    }
    Blob d[3];
    for (int k = 0; k < 3; ++k) Dump(d[k], *rigs[k]);
    static const char *const names[] = {"AddEdge", "RemoveEdge", "EdgeForAngle"};
    ++ctx.cases;
    if (gTrapHits) ctx.Fail("%s seed %u: the adapter dispatched its own slot %d", names[op], seed, slot);
    if (res[0] != res[1]) ctx.Fail("%s seed %u: result original %u, vtable %u (angle bits %08x)", names[op], seed, res[0], res[1], Bits(angle));
    if (res[0] != res[2]) ctx.Fail("%s seed %u: result original %u, adapter %u (angle bits %08x)", names[op], seed, res[0], res[2], Bits(angle));
    if (d[0] != d[1]) {
        size_t at = 0;
        while (at < d[0].size() && d[0][at] == d[1][at]) ++at;
        ctx.Fail("%s seed %u: state differs after the vtable call (word %u: original %08x, ours %08x)", names[op], seed, unsigned(at), d[0][at], d[1][at]);
    }
    if (d[0] != d[2]) ctx.Fail("%s seed %u: state differs after the adapter call", names[op], seed);
    for (Rig *r : rigs) Free(*r);
}

void OpCases(difftest::Context &ctx, OpKind op, uint32_t count)
{
    for (uint32_t seed = 0; seed < count; ++seed) RunOne(ctx, op, seed);
}

// ---- SetPosition: any source, including inside the vertex ----

void SetPositionCases(difftest::Context &ctx)
{
    std::mt19937 rng(0x81ff30);
    for (int i = 0; i < 3000; ++i) {
        constexpr uint32_t kBuf = 0x80;
        uint8_t seedBytes[kBuf];
        for (uint8_t &b : seedBytes) b = uint8_t(rng());
        const uint32_t selfOff = 0x20 + rng() % 0x20; // room before and after
        uint32_t srcOff;
        switch (i % 4) {
        case 0: srcOff = selfOff + 4 + rng() % 0x18; break; // inside the vertex, past the vtable word: the overlap order matters
        case 1: srcOff = selfOff + 8; break;
        case 2: srcOff = selfOff - 12 + rng() % 8; break; // ends inside it (never reading the vtable word)
        default: srcOff = rng() % 8 == 0 ? selfOff + 0x1c : rng() % 0x14; break; // outside it
        }
        if (srcOff < selfOff && srcOff + 12 > selfOff) srcOff = selfOff - 12; // never read the vtable word
        uint8_t buf[3][kBuf];
        uint32_t res[3];
        for (int route = 0; route < 3; ++route) {
            std::memcpy(buf[route], seedBytes, kBuf);
            uint8_t *self = buf[route] + selfOff;
            const void *src = buf[route] + srcOff;
            if (route == 1) {
                // Ours: the object is built in place, then the bytes the test controls are put back (not the vtable word).
                uint8_t saved[kSize];
                std::memcpy(saved, self, kSize);
                new (self) cWallVertex();
                std::memcpy(self + 4, saved + 4, kSize - 4);
                res[1] = static_cast<cIWallVertex *>(reinterpret_cast<cWallVertex *>(self))->SetPosition(src);
            } else if (route == 0) {
                shape::VTableAt(self, 0) = shape::GameVTable(cWallVertex::kVTable);
                res[0] = ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x81ff30)(self, src);
            } else {
                static void *trapTable[10];
                std::memcpy(trapTable, shape::GameVTable(cWallVertex::kVTable), sizeof(trapTable));
                trapTable[1] = reinterpret_cast<void *>(&Trap);
                shape::VTableAt(self, 0) = trapTable;
                res[2] = cWallVertexAdapters::SetPosition(reinterpret_cast<cWallVertex *>(self), nullptr, src);
            }
            std::memset(buf[route] + selfOff, 0, 4); // the vtable word differs by route
        }
        ++ctx.cases;
        if (res[0] != res[1] || res[0] != res[2]) ctx.Fail("case %d: result original %08x, vtable %08x, adapter %08x", i, res[0], res[1], res[2]);
        if (std::memcmp(buf[0], buf[1], kBuf)) ctx.Fail("case %d: buffer differs after the vtable call (self +%x, src +%x)", i, selfOff, srcOff);
        if (std::memcmp(buf[0], buf[2], kBuf)) ctx.Fail("case %d: buffer differs after the adapter call (self +%x, src +%x)", i, selfOff, srcOff);
    }
    if (gTrapHits) ctx.Fail("the adapter dispatched its own slot");
}

// ---- construction and destruction ----

void CtorCases(difftest::Context &ctx)
{
    std::mt19937 rng(0x81fd90);
    for (uint32_t fill : {0u, 0xcdu, 0x5au, 0xffu}) for (int withPos = 0; withPos < 2; ++withPos) for (int rep = 0; rep < 3; ++rep) {
        float pos[3];
        for (float &p : pos) p = FromBits(rng() % 4 ? uint32_t(rng()) : 0x7fc12345u);
        Blob d[3];
        uint32_t returned[3];
        for (int route = 0; route < 3; ++route) {
            void *block = ::operator new(kSize);
            std::memset(block, int(fill), kSize);
            void *r;
            if (route == 0) {
                r = withPos ? ctx.Original<void *(__thiscall *)(void *, const float *)>(0x81fd90)(block, pos)
                            : ctx.Original<void *(__thiscall *)(void *)>(0x81fdd0)(block);
            } else if (route == 1) {
                r = static_cast<cIWallVertex *>(withPos ? new (block) cWallVertex(pos) : new (block) cWallVertex());
            } else {
                r = withPos ? static_cast<void *>(cWallVertexAdapters::ConstructAt(static_cast<cWallVertex *>(block), nullptr, pos))
                            : static_cast<void *>(cWallVertexAdapters::Construct(static_cast<cWallVertex *>(block), nullptr));
            }
            returned[route] = uint32_t(r == block);
            static const uint32_t kFace[] = {0};
            const uint32_t word = shape::Word(block, 0);
            d[route].push_back(word == shape::P(shape::GameVTable(cWallVertex::kVTable)) || word == shape::P(shape::Ours<cWallVertex>(kFace).face[0]) ? 1u : word);
            for (uint32_t off = 4; off < kSize; off += 4) d[route].push_back(shape::Word(block, off));
            ::operator delete(block, kSize);
        }
        ++ctx.cases;
        if (d[0] != d[1]) ctx.Fail("%s over 0x%02x: ours differs from the original", withPos ? "constructor(pos)" : "constructor()", fill);
        if (d[0] != d[2]) ctx.Fail("%s over 0x%02x: the adapter differs from the original", withPos ? "constructor(pos)" : "constructor()", fill);
        if (returned[0] != returned[1] || returned[0] != returned[2]) ctx.Fail("%s over 0x%02x: result is this: original %u, ours %u, adapter %u", withPos ? "constructor(pos)" : "constructor()", fill, returned[0], returned[1], returned[2]);
    }
}

void DtorCases(difftest::Context &ctx)
{
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (int body = 0; body < 2; ++body) for (uint32_t seed = 0; seed < 3; ++seed) {
        Blob d[2];
        for (int side = 0; side < 2; ++side) {
            Rig r;
            BuildEdges(r, kGame, seed);
            r.kind = side ? kOurs : kGame;
            const VertexState s = StateFor(r, seed);
            BuildVertex(r, s);
            void *block = r.vertex;
            difftest::FreeRecorder rec;
            void *ret = block;
            if (body) {
                if (side) cWallVertexAdapters::DestructBody(reinterpret_cast<cWallVertex *>(block), nullptr);
                else ctx.Original<void(__thiscall *)(void *)>(0x81fe10)(block);
            } else {
                ret = side ? static_cast<void *>(cWallVertexAdapters::DeletingDestructor(reinterpret_cast<cWallVertex *>(block), nullptr, flags))
                           : ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x81fe20)(block, flags);
            }
            rec.Stop();
            d[side] = {uint32_t(ret == block), uint32_t(rec.Freed(block))};
            size_t sizes[8]{};
            const size_t n = rec.SortedSizes(sizes, 8);
            d[side].push_back(uint32_t(n));
            for (size_t k = 0; k < n && k < 8; ++k) d[side].push_back(uint32_t(sizes[k]));
            if (!rec.Freed(block)) {
                DumpVertex(d[side], r);
            } else {
                r.vertex = nullptr;
            }
            ::operator delete(r.e, kEdges * sizeof(cWallEdge));
            if (r.vertex) ::operator delete(r.vertex, kSize);
        }
        ++ctx.cases;
        if (d[0] != d[1]) ctx.Fail("%s flags %u seed %u: frees / fields differ", body ? "destructor body" : "deleting destructor", flags, seed);
    }
    for (int i = 0; i < 3; ++i) {
        difftest::FreeRecorder rec;
        auto *vertex = static_cast<cIWallVertex *>(new cWallVertex());
        void *block = vertex;
        delete vertex;
        rec.Stop();
        size_t sizes[4]{};
        const size_t n = rec.SortedSizes(sizes, 4);
        ++ctx.cases;
        if (!rec.Freed(block) || n != 1 || sizes[0] != kSize) ctx.Fail("delete through the vtable freed %u blocks (first %u bytes), want one of %u", unsigned(n), unsigned(sizes[0]), unsigned(kSize));
    }
}

// ---- the shared one-word slots through our vtable ----

void FoldedCases(difftest::Context &ctx)
{
    std::mt19937 rng(0x147a30);
    for (uint32_t seed = 0; seed < 24; ++seed) {
        Rig game, ours;
        BuildEdges(game, kGame, seed);
        BuildEdges(ours, kOurs, seed);
        const VertexState s = StateFor(game, seed);
        BuildVertex(game, s);
        BuildVertex(ours, s);
        auto *o = static_cast<cIWallVertex *>(reinterpret_cast<cWallVertex *>(ours.vertex));
        const int32_t value = int32_t(rng());
        const uint32_t a0 = ctx.Original<uint32_t(__thiscall *)(void *, int32_t)>(0x1489e0)(game.vertex, value);
        const uint32_t b0 = uint32_t(o->SetId(value));
        const int32_t a4 = ctx.Original<int32_t(__thiscall *)(void *)>(0x147a30)(game.vertex);
        const int32_t b4 = o->Id();
        const uint32_t a5 = shape::P(ctx.Original<void *(__thiscall *)(void *)>(0x142840)(game.vertex)) - shape::P(game.vertex);
        const uint32_t b5 = shape::P(o->Position()) - shape::P(ours.vertex);
        const int32_t a6 = ctx.Original<int32_t(__thiscall *)(void *)>(0x148ba0)(game.vertex);
        const int32_t b6 = o->EdgeCount();
        const uint32_t a7 = game.Enc(ctx.Original<void *(__thiscall *)(void *)>(0x149d00)(game.vertex));
        const uint32_t b7 = ours.Enc(o->FirstEdge());
        Blob dg, dow;
        Dump(dg, game);
        Dump(dow, ours);
        ++ctx.cases;
        if (a0 != b0) ctx.Fail("seed %u SetId: original 0x%08x, ours 0x%08x", seed, a0, b0);
        if (a4 != b4) ctx.Fail("seed %u Id: original %d, ours %d", seed, a4, b4);
        if (a5 != b5) ctx.Fail("seed %u Position: original +%x, ours +%x", seed, a5, b5);
        if (a6 != b6) ctx.Fail("seed %u EdgeCount: original %d, ours %d", seed, a6, b6);
        if (a7 != b7) ctx.Fail("seed %u FirstEdge: original %u, ours %u", seed, a7, b7);
        if (dg != dow) ctx.Fail("seed %u: the vertices differ", seed);
        Free(game);
        Free(ours);
    }
}

} // namespace

DIFF_TEST(WallVertex_Constructors, 0x81fd90) { CtorCases(ctx); }
DIFF_TEST(WallVertex_Destructors, 0x81fe20) { DtorCases(ctx); }
DIFF_TEST(WallVertex_SetPosition, 0x81ff30) { SetPositionCases(ctx); }
DIFF_TEST(WallVertex_AddEdge, 0x81fe40) { OpCases(ctx, kAdd, 300); }
DIFF_TEST(WallVertex_RemoveEdge, 0x81fef0) { OpCases(ctx, kRemove, 300); }
DIFF_TEST(WallVertex_EdgeForAngle, 0x81fe90) { OpCases(ctx, kAngle, 600); }
DIFF_TEST(WallVertex_Folded, 0x147a30) { FoldedCases(ctx); }
