// Differential tests for lane GG's part of cTSWantSimulator (src/ts/cTSWantSimulatorGG.cpp): DoMessage (0x487dd0, the
// message face's slot 3), Import (0x492fd0, the data container's slot 5), FindWantObject (0x48df40) and the memory store's
// AdvanceToTick (0x551320) and CollectMemories (0x550d80).
//
// The simulator runs the three routes of tests/ts_want_simulator_shape.h. Every object the functions reach is a recording
// fake (FakeObj: one vtable for messages, refs, records, want instances; the globals, tables, context and mapping), the
// non-virtual callees DeleteSim / ReadSimRecord / GetMemoryStore are stood in for on both sides (shape::StandIn), and the
// memory stores are real (their Clear / Destruct / CollectMemories run: the image's on the original route, ours on the
// others). Results, the call log, every fake's state and a digest of the simulator (maps, counted members, stores) must
// agree across the routes.
#include "difftest.h"
#include "game/game.h"
#include "cas_body_part_system.h" // Env: the game allocator and allocator-service cache
#include "code_patch.h"
#include "ts_want_simulator_shape.h"
#include "ifaces/cITSWantInstance.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZString.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace wantgg {

using namespace wantshape;

std::vector<uint32_t> gLog;
void Log(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0)
{
    gLog.insert(gLog.end(), {a, b, c, d});
}

// ---- one fake for every counted object ----
constexpr unsigned kObjs = 48;
struct FakeObj {
    void **vt;
    uint32_t tag;
    int32_t refs;
    uint32_t simId;       // slot 11 / 31
    uint32_t paramSim;    // slot 15 / 30
    uint8_t flag;         // slot 28
    uint32_t id47;        // slot 47 (an object's id)
    FakeObj *parent27;    // slot 27 (a record's master)
    FakeObj *object18;    // slot 18 (a record's object)
    // a message
    uint32_t type;        // slot 4
    uint8_t hasSim;       // slot 8
    uint32_t simWord;     // slot 9
    uint8_t qiOk;         // slot 0
    FakeObj *qiOut;
};
FakeObj gObj[kObjs];
void *gObjVt[64];
uint32_t Tag(const void *p) { return p ? static_cast<const FakeObj *>(p)->tag : 0; }

uint32_t __fastcall O_QI(FakeObj *o, void *, uint32_t iid, void **out)
{
    Log(500, o->tag, iid);
    if (o->qiOut) {
        *out = o->qiOut;
        ++o->qiOut->refs;
    }
    return o->qiOk ? 0x12345601u : 0x12345600u;
}
uint32_t __fastcall O_AddRef(FakeObj *o, void *) { Log(501, o->tag); return uint32_t(++o->refs); }
uint32_t __fastcall O_Release(FakeObj *o, void *) { Log(502, o->tag); return uint32_t(--o->refs); }
uint32_t __fastcall O_Type(FakeObj *o, void *) { Log(504, o->tag); return o->type; }
uint32_t __fastcall O_HasSim(FakeObj *o, void *) { Log(508, o->tag); return 0xabcdef00u | o->hasSim; }
uint32_t __fastcall O_SimWord(FakeObj *o, void *) { Log(509, o->tag); return o->simWord; }
uint32_t __fastcall O_SimId(FakeObj *o, void *) { Log(511, o->tag); return 0x77770000u | (o->simId & 0xffff); }
uint32_t __fastcall O_ParamSim(FakeObj *o, void *) { Log(515, o->tag); return 0x55550000u | (o->paramSim & 0xffff); }
uint32_t __fastcall O_Object18(FakeObj *o, void *) { Log(518, o->tag); return reinterpret_cast<uint32_t>(o->object18); }
uint32_t __fastcall O_Parent27(FakeObj *o, void *) { Log(527, o->tag); return reinterpret_cast<uint32_t>(o->parent27); }
uint32_t __fastcall O_Flag(FakeObj *o, void *) { Log(528, o->tag); return 0x3300u | o->flag; }
void __fastcall O_SetParamSim(FakeObj *o, void *, uint32_t v) { Log(530, o->tag, v); o->paramSim = v; }
void __fastcall O_SetSim(FakeObj *o, void *, uint32_t v) { Log(531, o->tag, v); o->simId = v; }
uint32_t __fastcall O_Id47(FakeObj *o, void *) { Log(547, o->tag); return o->id47; }

// ---- the globals (0x10cc288) and what they lead to ----
struct Plain {
    void **vt;
};
void *gGlobalsVt[64], *gWantSvcVt[64], *gObjTableVt[64], *gDefTableVt[64], *gGscVt[64];
Plain gGlobals, gWantSvc, gObjTable, gDefTable, gGsc;
uint32_t gTick;
FakeObj *gFoundObject, *gFoundRecord;

uint32_t __fastcall G_WantSvc(Plain *, void *) { Log(604); return reinterpret_cast<uint32_t>(&gWantSvc); }
uint32_t __fastcall G_Gsc(Plain *, void *) { Log(623); return reinterpret_cast<uint32_t>(&gGsc); }
uint32_t __fastcall G_DefTable(Plain *, void *) { Log(624); return reinterpret_cast<uint32_t>(&gDefTable); }
uint32_t __fastcall G_ObjTable(Plain *, void *) { Log(626); return reinterpret_cast<uint32_t>(&gObjTable); }
uint32_t __fastcall WS_Tick(Plain *, void *) { Log(626 + 1000); return gTick; }
uint32_t __fastcall OT_Find(Plain *, void *, uint32_t guid) { Log(642, guid); return reinterpret_cast<uint32_t>(gFoundObject); }
uint32_t __fastcall DT_Find(Plain *, void *, uint32_t guid) { Log(614, guid); return reinterpret_cast<uint32_t>(gFoundRecord); }
uint32_t __fastcall GS_Slot19(Plain *, void *) { Log(619); return 0x19191919u; }

// ---- the simulator's own slot 55 ----
void __fastcall W_Flush(W *, void *) { Log(155); }

// ---- stand-ins for the non-virtual callees (both sides) ----
const unsigned char *gSimBase;
uint32_t Offset(const void *w) { return uint32_t(static_cast<const unsigned char *>(w) - gSimBase); }

void __fastcall SD_DeleteSim(W *w, void *, uint32_t sim) { Log(700, Offset(w), sim & 0xffff); }

struct ImportPlan {
    uint8_t readOk[8];      // ReadSimRecord's answer per sim
    uint8_t storeNull[8];   // GetMemoryStore answers null
};
ImportPlan gPlan;
uint32_t __fastcall SR_Read(W *w, void *, uint32_t sim, const WantResourceKey *key)
{
    Log(710, Offset(w), sim & 0xffff, key->type);
    Log(711, key->group, key->instance, key->resource);
    return 0xee00u | gPlan.readOk[sim & 7];
}
WantMemoryStore *__fastcall SG_Store(W *w, void *, uint32_t sim)
{
    Log(720, Offset(w), sim & 0xffff);
    if (gPlan.storeNull[sim & 7]) return nullptr;
    const auto found = w->mMemoryStores.find(int16_t(sim));
    return found == w->mMemoryStores.end() ? nullptr : found->second;
}

// ---- the import context and its mapping ----
void *gContextVt[64], *gMappingVt[64];
struct FakeContext {
    void **vt;
    uint8_t qiOk, segOk;
    FakeObj *mapping, *segment;
    uint32_t group;
};
struct FakeMapping {
    void **vt;
    uint32_t tag;
    int32_t refs;
    std::vector<uint32_t> ids;
    uint32_t map[16]; // id & 15 -> new id word (0: unknown)
};
FakeContext gContext;
FakeMapping gMapping;
uint32_t __fastcall C_QI(FakeContext *c, void *, uint32_t iid, void **out)
{
    Log(800, iid);
    if (c->mapping) *out = c->mapping;
    if (c->mapping) ++gMapping.refs;
    return 0x5500u | c->qiOk;
}
uint32_t __fastcall C_Segment(FakeContext *c, void *, uint32_t iid, void **out)
{
    Log(812, iid);
    if (c->segment) {
        *out = c->segment;
        ++c->segment->refs;
    }
    return 0x4400u | c->segOk;
}
uint32_t __fastcall C_Group(FakeContext *c, void *) { Log(814); return c->group; }
uint32_t __fastcall M_Release(FakeMapping *m, void *) { Log(802, m->tag); return uint32_t(--m->refs); }
void __fastcall M_CopyIds(FakeMapping *m, void *, ServiceBlockVector *v)
{
    Log(830, v->mFirst != nullptr);
    if (m->ids.empty()) return;
    auto *block = static_cast<uint32_t *>(bpt::ServiceAlloc((m->ids.size() + 1) * 4));
    std::memcpy(block, m->ids.data(), m->ids.size() * 4);
    v->mFirst = block;
    v->mLast = block + m->ids.size();
    v->mEnd = block + m->ids.size() + 1;
}
uint32_t __fastcall M_Map(FakeMapping *m, void *, uint32_t id, uint32_t *out)
{
    Log(832, id, *out);
    const uint32_t v = m->map[id & 15];
    if (!v) return 0x9900u;
    *out = v;
    return 0x9901u;
}

void SetupFakes(difftest::Context &ctx)
{
    gLog.reserve(1 << 16);
    for (auto &s : gObjVt) s = nullptr;
    gObjVt[0] = reinterpret_cast<void *>(&O_QI);
    gObjVt[1] = reinterpret_cast<void *>(&O_AddRef);
    gObjVt[2] = reinterpret_cast<void *>(&O_Release);
    gObjVt[4] = reinterpret_cast<void *>(&O_Type);
    gObjVt[8] = reinterpret_cast<void *>(&O_HasSim);
    gObjVt[9] = reinterpret_cast<void *>(&O_SimWord);
    gObjVt[11] = reinterpret_cast<void *>(&O_SimId);
    gObjVt[15] = reinterpret_cast<void *>(&O_ParamSim);
    gObjVt[18] = reinterpret_cast<void *>(&O_Object18);
    gObjVt[27] = reinterpret_cast<void *>(&O_Parent27);
    gObjVt[28] = reinterpret_cast<void *>(&O_Flag);
    gObjVt[30] = reinterpret_cast<void *>(&O_SetParamSim);
    gObjVt[31] = reinterpret_cast<void *>(&O_SetSim);
    gObjVt[47] = reinterpret_cast<void *>(&O_Id47);
    for (auto *vt : {gGlobalsVt, gWantSvcVt, gObjTableVt, gDefTableVt, gGscVt, gContextVt, gMappingVt})
        for (unsigned i = 0; i < 64; ++i) vt[i] = nullptr;
    gGlobalsVt[4] = reinterpret_cast<void *>(&G_WantSvc);
    gGlobalsVt[23] = reinterpret_cast<void *>(&G_Gsc);
    gGlobalsVt[24] = reinterpret_cast<void *>(&G_DefTable);
    gGlobalsVt[26] = reinterpret_cast<void *>(&G_ObjTable);
    gWantSvcVt[26] = reinterpret_cast<void *>(&WS_Tick);
    gObjTableVt[42] = reinterpret_cast<void *>(&OT_Find);
    gDefTableVt[14] = reinterpret_cast<void *>(&DT_Find);
    gGscVt[19] = reinterpret_cast<void *>(&GS_Slot19);
    gContextVt[0] = reinterpret_cast<void *>(&C_QI);
    gContextVt[12] = reinterpret_cast<void *>(&C_Segment);
    gContextVt[14] = reinterpret_cast<void *>(&C_Group);
    gMappingVt[2] = reinterpret_cast<void *>(&M_Release);
    gMappingVt[30] = reinterpret_cast<void *>(&M_CopyIds);
    gMappingVt[32] = reinterpret_cast<void *>(&M_Map);
    gGlobals.vt = gGlobalsVt;
    gWantSvc.vt = gWantSvcVt;
    gObjTable.vt = gObjTableVt;
    gDefTable.vt = gDefTableVt;
    gGsc.vt = gGscVt;
    gContext.vt = gContextVt;
    gMapping.vt = gMappingVt;
    *ctx.image.At<void *>(0x10cc288) = &gGlobals;
    ClearPatches();
    gPatch[0][55] = reinterpret_cast<void *>(&W_Flush);
}

// Every fake object to its seeded state (the same on each route).
void ResetObjs(std::mt19937 &rng)
{
    for (uint32_t i = 0; i < kObjs; ++i) {
        FakeObj &o = gObj[i];
        o = FakeObj();
        o.vt = gObjVt;
        o.tag = 1000 + i;
        o.refs = 1 + int32_t(rng() % 3);
        o.simId = rng() % 3 ? rng() % 8 : rng();
        o.paramSim = rng() % 3 ? rng() % 8 : rng();
        o.flag = uint8_t(rng() % 3 == 0);
        o.id47 = rng() % 2 ? 0x12340000u | (rng() % 4) : rng();
    }
}
FakeObj *AnyObj(std::mt19937 &rng, uint32_t nullOneIn = 5) { return rng() % nullOneIn == 0 ? nullptr : &gObj[8 + rng() % (kObjs - 8)]; }

// A list of counted fake objects (some null) in an allocator-service block.
void FillRefs(WantRefList &l, std::mt19937 &rng)
{
    const uint32_t n = rng() % 5;
    if (!n) return;
    const uint32_t cap = n + rng() % 3;
    l.mFirst = static_cast<void **>(bpt::ServiceAlloc(cap * 4));
    l.mLast = l.mFirst + n;
    l.mEnd = l.mFirst + cap;
    for (uint32_t i = 0; i < n; ++i) l.mFirst[i] = AnyObj(rng, 4);
}

// A real store with memories of fake objects (lists counted as the game's are: one count each).
WantMemoryStore *NewStore(std::mt19937 &rng)
{
    auto *store = static_cast<WantMemoryStore *>(::operator new(sizeof(WantMemoryStore)));
    store->Construct();
    store->lastTick = rng() % 2 ? 0x3fffffff : rng() % 900;
    store->nextId = rng() % 3 ? int32_t(rng() % 100) : int32_t(rng());
    for (uint32_t g = 0, n = rng() % 4; g < n; ++g) {
        MemoryList &list = store->byGuid[rng() % 5];
        // Never null: every path that files a memory has a live instance (CollectMemories asks each for its flag).
        for (uint32_t i = 0, m = rng() % 4; i < m; ++i)
            list.Append(reinterpret_cast<ifaces::cITSWantInstance *>(&gObj[8 + rng() % (kObjs - 8)]));
    }
    return store;
}
void FreeStore(WantMemoryStore *store)
{
    store->Clear();
    store->Destruct();
    ::operator delete(store, sizeof(WantMemoryStore));
}

// The simulator for a seed (our constructor, the route's vtables, contents from the seed).
void Build(W *w, std::mt19937 &rng)
{
    std::memset(static_cast<void *>(w), 0x5c, sizeof(W));
    wantshape::Construct(w);
    gSimBase = reinterpret_cast<const unsigned char *>(w);
    w->mTick = rng() % 2 ? rng() % 100 : rng();
    for (int i = 0, n = rng() % 4; i < n; ++i) {
        const int16_t sim = int16_t(rng() % 8);
        if (!w->mMemoryStores.count(sim)) w->mMemoryStores[sim] = NewStore(rng);
    }
    w->mCurrentStore = nullptr;
    if (!w->mMemoryStores.empty() && rng() % 2) {
        auto it = w->mMemoryStores.begin();
        std::advance(it, rng() % w->mMemoryStores.size());
        w->mCurrentStore = it->second;
    }
    for (int i = 0, n = rng() % 5; i < n; ++i) FillRefs(w->mWants[int16_t(rng() % 8)], rng);
    for (int i = 0, n = rng() % 5; i < n; ++i) FillRefs(w->mFears[int16_t(rng() % 8)], rng);
    for (int i = 0, n = rng() % 3; i < n; ++i) FillRefs(w->mMap50[int16_t(rng() % 8)], rng);
    for (int i = 0, n = rng() % 3; i < n; ++i) FillRefs(w->mMap58[int16_t(rng() % 8)], rng);
    for (int i = 0, n = rng() % 5; i < n; ++i) {
        WantRef &slot = w->mLifetimeWants[int16_t(rng() % 8)];
        if (!slot.object) slot.object = AnyObj(rng, 4);
    }
    for (int i = 0, n = rng() % 4; i < n; ++i) w->mMap48[int16_t(rng() % 8)] = int16_t(rng());
    w->mRef90.object = AnyObj(rng, 3);
    w->mRef94.object = AnyObj(rng, 3);
}

void Teardown(W *w)
{
    for (auto &e : w->mMemoryStores) FreeStore(e.second);
    w->mMemoryStores.clear();
    w->mCurrentStore = nullptr;
    Destroy(w);
}

void DumpStore(std::vector<int64_t> &d, const WantMemoryStore *s)
{
    d.push_back(s->lastTick);
    d.push_back(s->nextId);
    d.push_back(int64_t(s->byGuid.size()));
    for (auto &e : s->byGuid) {
        d.push_back(e.first);
        d.push_back(e.second.size);
        for (MemoryNode *n = e.second.head->next; n != e.second.head; n = n->next) d.push_back(Tag(n->memory));
    }
}

std::vector<int64_t> Digest(W *w)
{
    std::vector<int64_t> d;
    auto refs = [&](auto &m) {
        d.push_back(int64_t(m.size()));
        for (auto &e : m) {
            d.push_back(e.first);
            d.push_back(int64_t(e.second.Size()) << 32 | uint32_t(e.second.mEnd - e.second.mFirst));
            for (void **it = e.second.mFirst; it != e.second.mLast; ++it) d.push_back(Tag(*it));
        }
    };
    d.push_back(int64_t(w->mMemoryStores.size()));
    int64_t current = w->mCurrentStore ? -2 : -1;
    int64_t index = 0;
    for (auto &e : w->mMemoryStores) {
        d.push_back(e.first);
        DumpStore(d, e.second);
        if (e.second == w->mCurrentStore) current = index;
        ++index;
    }
    d.push_back(current);
    refs(w->mWants);
    refs(w->mFears);
    refs(w->mMap50);
    refs(w->mMap58);
    d.push_back(int64_t(w->mLifetimeWants.size()));
    for (auto &e : w->mLifetimeWants) d.push_back(e.first), d.push_back(Tag(e.second.object));
    d.push_back(int64_t(w->mMap48.size()));
    for (auto &e : w->mMap48) d.push_back(e.first), d.push_back(e.second);
    d.push_back(Tag(w->mRef90.object));
    d.push_back(Tag(w->mRef94.object));
    d.push_back(w->mTick);
    d.push_back(w->m110);
    for (const FakeObj &o : gObj) d.push_back(o.refs), d.push_back(o.simId), d.push_back(o.paramSim);
    d.push_back(gMapping.refs);
    return d;
}

struct Outcome {
    uint32_t result;
    std::vector<uint32_t> log;
    std::vector<int64_t> digest;
};

// Runs `body(w, rng)` on the three routes for each seed: `prepare(rng)` seeds the fakes (the same stream on each route).
template <typename Prepare, typename Body>
void Compare(difftest::Context &ctx, const char *name, uint32_t seeds, Prepare prepare, Body body, int routes = kRoutes)
{
    bpt::Env env(ctx);
    void *const savedGlobals = *ctx.image.At<void *>(0x10cc288);
    SetupFakes(ctx);
    alignas(8) static unsigned char buf[kRoutes][sizeof(W)];
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        ++ctx.cases;
        Outcome out[kRoutes];
        for (int route = 0; route < routes; ++route) {
            gRoute = route;
            gTrapHits = 0;
            std::mt19937 rng(seed * 2654435761u + 17);
            ResetObjs(rng);
            prepare(rng);
            W *w = reinterpret_cast<W *>(buf[route]);
            Build(w, rng);
            gLog.clear();
            out[route].result = body(w, rng);
            out[route].log = gLog;
            out[route].digest = Digest(w);
            if (gTrapHits) ctx.Fail("%s seed %u route %d: the adapter dispatched through the vtable", name, seed, route);
            Teardown(w);
        }
        for (int route = 1; route < routes; ++route) {
            if (out[0].result != out[route].result)
                ctx.Fail("%s seed %u route %d: result %x vs %x", name, seed, route, out[0].result, out[route].result);
            if (out[0].log != out[route].log) {
                size_t i = 0;
                while (i < out[0].log.size() && i < out[route].log.size() && out[0].log[i] == out[route].log[i]) ++i;
                ctx.Fail("%s seed %u route %d: calls differ at word %zu (%zu vs %zu words)", name, seed, route, i, out[0].log.size(),
                         out[route].log.size());
            }
            if (out[0].digest != out[route].digest) ctx.Fail("%s seed %u route %d: state differs", name, seed, route);
        }
    }
    gRoute = 0;
    *ctx.image.At<void *>(0x10cc288) = savedGlobals;
}

} // namespace wantgg
using namespace wantgg;

DIFF_TEST(WantSimulator_DoMessage, 0x487dd0)
{
    shape::StandIn deleteSim(0x487be0, &W::DeleteSim, &SD_DeleteSim);
    static FakeObj outer;
    FakeObj *message = nullptr;
    Compare(ctx, "DoMessage", 1500,
        [&](std::mt19937 &rng) {
            gTick = rng() % 3 ? 500 + rng() % 600 : rng();
            gFoundObject = AnyObj(rng, 3);
            gFoundRecord = AnyObj(rng, 4);
            for (unsigned i = 8; i < kObjs; ++i) {
                gObj[i].parent27 = rng() % 2 ? nullptr : AnyObj(rng, 3);
                gObj[i].object18 = AnyObj(rng, 3);
            }
            outer = FakeObj();
            outer.vt = gObjVt;
            outer.tag = 999;
            outer.refs = 1;
            FakeObj &typed = gObj[1];
            static const uint32_t kTypes[] = {0x82, 0x82, 0x9d, 0x9e, 0xb3, 0xb3, 0xb4, 0xb5, 0xb5, 0x83, 0x9f, 0xb2, 0x81, 0xb6, 0};
            typed.type = rng() % 12 ? kTypes[rng() % 15] : rng();
            typed.hasSim = uint8_t(rng() % 4 != 0);
            typed.simWord = (rng() % 2 ? rng() & 0xffff0000u : 0) | (rng() % 3 ? rng() % 8 : rng() & 0xffff);
            outer.qiOk = uint8_t(rng() % 8 != 0);
            outer.qiOut = outer.qiOk || rng() % 2 ? &typed : nullptr;
            message = rng() % 20 == 0 ? nullptr : &outer;
        },
        [&](W *w, std::mt19937 &rng) -> uint32_t {
            if (w->mRef94.object && rng() % 2) gObj[1].simWord = (gObj[1].simWord & 0xffff0000u) | (Tag(w->mRef94.object) ? static_cast<FakeObj *>(w->mRef94.object)->id47 & 0xffff : 0);
            using Fn = uint32_t(__thiscall *)(void *, void *);
            return Call<Fn>(ctx, w, 0x487dd0, 1, WANT_NA(DoMessage), static_cast<void *>(message)) & 0xff;
        });
}

DIFF_TEST(WantSimulator_FindWantObject, 0x48df40)
{
    Compare(ctx, "FindWantObject", 400,
        [&](std::mt19937 &rng) { gFoundObject = AnyObj(rng, 3); },
        [&](W *w, std::mt19937 &) -> uint32_t {
            using Fn = uint32_t(__thiscall *)(void *);
            return Tag(reinterpret_cast<void *>(Call<Fn>(ctx, w, 0x48df40, kNoFace, WANT_ADAPTER(FindWantObject))));
        });
}

DIFF_TEST(WantSimulator_Import, 0x492fd0)
{
    shape::StandIn read(0x492ea0, &W::ReadSimRecord, &SR_Read);
    shape::StandIn store(0x48d650, &W::GetMemoryStore, &SG_Store);
    FakeContext *context = nullptr;
    Compare(ctx, "Import", 1500,
        [&](std::mt19937 &rng) {
            for (unsigned i = 0; i < 8; ++i) {
                gPlan.readOk[i] = uint8_t(rng() % 4 != 0);
                gPlan.storeNull[i] = uint8_t(rng() % 5 == 0);
            }
            gMapping.tag = 3000;
            gMapping.refs = 1;
            gMapping.ids.clear();
            for (uint32_t i = 0, n = rng() % 6; i < n; ++i) gMapping.ids.push_back(rng() % 3 ? rng() % 16 : rng());
            for (uint32_t &m : gMapping.map)
                m = rng() % 4 == 0 ? 0 : (rng() % 3 ? 0 : rng() & 0xffff0000u) | (rng() % 4 ? rng() % 8 : rng() & 0xffff);
            gContext.qiOk = uint8_t(rng() % 8 != 0);
            gContext.segOk = uint8_t(rng() % 8 != 0);
            gContext.mapping = gContext.qiOk || rng() % 2 ? reinterpret_cast<FakeObj *>(&gMapping) : nullptr;
            gContext.segment = rng() % 6 ? &gObj[3] : nullptr;
            gContext.group = rng();
            context = rng() % 25 == 0 ? nullptr : &gContext;
        },
        [&](W *w, std::mt19937 &) -> uint32_t {
            using Fn = uint32_t(__thiscall *)(void *, void *);
            return Call<Fn>(ctx, w, 0x492fd0, 2, WANT_NA(Import), static_cast<void *>(context)) & 0xff;
        });
}

// The two store members on stores built alike: the image's (route 0) and ours (route 1, called directly).
DIFF_TEST(WantMemoryStore_AdvanceToTick, 0x551320)
{
    Compare(ctx, "AdvanceToTick", 300,
        [&](std::mt19937 &rng) { gTick = rng() % 3 ? rng() % 1200 : rng(); },
        [&](W *w, std::mt19937 &) -> uint32_t {
            uint32_t sum = 0;
            for (auto &e : w->mMemoryStores) {
                WantMemoryStore *s = e.second;
                sum = sum * 31 + (gRoute == 0 ? ctx.Original<uint32_t(__thiscall *)(WantMemoryStore *)>(0x551320)(s) : uint32_t(s->AdvanceToTick()));
            }
            return sum;
        });
}

DIFF_TEST(WantMemoryStore_CollectMemories, 0x550d80)
{
    Compare(ctx, "CollectMemories", 400, [&](std::mt19937 &) {},
        [&](W *w, std::mt19937 &rng) -> uint32_t {
            uint32_t sum = 0;
            for (auto &e : w->mMemoryStores) {
                MemoryList out;
                if (rng() % 2) out.Append(reinterpret_cast<ifaces::cITSWantInstance *>(AnyObj(rng, 4)));
                const bool all = rng() % 2 != 0;
                if (gRoute == 0)
                    ctx.Original<void(__thiscall *)(WantMemoryStore *, MemoryList *, uint32_t)>(0x550d80)(e.second, &out, all ? 0xabcd01u : 0xabcd00u);
                else
                    e.second->CollectMemories(&out, all);
                sum = sum * 33 + out.size;
                for (MemoryNode *n = out.head->next; n != out.head; n = n->next) Log(900, Tag(n->memory));
            }
            return sum;
        });
}

// ---- the tick: Update (slot 38), RestartTick, the stores' id setters, SetMemoryIds / AddMemoryIds (slots 47 / 48) ----
// Slots 38, 47 and 48 are referenced by the vtable only: the original (route 0) against our vtable's slot (route 1), no
// adapter route (coordinator decision 2026-10-18).

namespace wantgg {

void *gClockVt[64];
FakeObj gClock;
bool gHasClock;
uint8_t gClockPaused, gClockFlags;
uint32_t __fastcall G_Clock(Plain *, void *) { Log(603); return gHasClock ? reinterpret_cast<uint32_t>(&gClock) : 0; }
uint32_t __fastcall CK_Paused(FakeObj *, void *) { Log(1626); return 0x4400u | gClockPaused; }
uint32_t __fastcall CK_Flags(FakeObj *, void *) { Log(1614); return 0x12345600u | gClockFlags; }
void __fastcall SU_RunUpdate(W *w, void *, uint32_t steps, uint32_t sim)
{
    Log(740, Offset(w), steps, sim & 0xffff);
    Log(741, w->m110, w->mTick);
}
void SetupClock()
{
    for (auto &s : gClockVt) s = nullptr;
    gClockVt[26] = reinterpret_cast<void *>(&CK_Paused);
    gClockVt[14] = reinterpret_cast<void *>(&CK_Flags);
    gClock.vt = gClockVt;
    gGlobalsVt[3] = reinterpret_cast<void *>(&G_Clock);
}

// WantSimContext::Construct stood in: the planned person / neighbour (counted, released by the inline destructor).
FakeObj *gCtxPerson, *gCtxNeighbor;
WantSimContext *__fastcall SC_Construct(WantSimContext *c, void *, uint32_t sim)
{
    // The original passes the caller's whole dword, ours the int16; Construct (ours, hooked) reads only the low half.
    Log(750, sim & 0xffff);
    c->simId = int16_t(sim);
    c->person = gCtxPerson;
    c->neighbor = reinterpret_cast<ifaces::cITSNeighbor *>(gCtxNeighbor);
    if (gCtxPerson) ++gCtxPerson->refs;
    if (gCtxNeighbor) ++gCtxNeighbor->refs;
    c->attributes = 0;
    return c;
}
uint8_t __fastcall SR_Restore(W *, void *, uint32_t sim) { Log(707, sim); return 0x71; }
// GetMemoryStore stood in: the sim's store, else the route's spare store (the real one makes a store).
alignas(8) unsigned char gSpareBytes[sizeof(WantMemoryStore)];
WantMemoryStore *gSpare;
WantMemoryStore *__fastcall SG_StoreOrSpare(W *w, void *, uint32_t sim)
{
    Log(721, Offset(w), sim & 0xffff);
    const auto found = w->mMemoryStores.find(int16_t(sim));
    return found == w->mMemoryStores.end() ? gSpare : found->second;
}

} // namespace wantgg

DIFF_TEST(WantSimulator_Update, 0x491e00)
{
    shape::StandIn run(0x4880b0, &W::RunUpdate, &SU_RunUpdate);
    Compare(ctx, "Update", 600,
        [&](std::mt19937 &rng) {
            gTick = rng() % 2000;
            gHasClock = rng() % 6 != 0;
            gClockPaused = uint8_t(rng() % 4 == 0);
            gClockFlags = uint8_t(rng());
        },
        [&](W *w, std::mt19937 &rng) -> uint32_t {
            SetupClock();
            if (rng() % 3 == 0) w->mTick = 0x3fffffff - rng() % 4;
            else if (rng() % 5 == 0) w->mTick = rng();
            if (rng() % 6 == 0) w->m110 = uint16_t(rng() % 2 ? 0x100 : rng());
            using Fn = void(__thiscall *)(void *);
            Call<Fn>(ctx, w, 0x491e00, 0, nullptr);
            return 0;
        },
        2);
}

DIFF_TEST(WantSimulator_RestartTick, 0x494a00)
{
    Compare(ctx, "RestartTick", 300,
        [&](std::mt19937 &rng) { gTick = rng() % 3 ? rng() % 1200 : rng(); },
        [&](W *w, std::mt19937 &rng) -> uint32_t {
            using Fn = void(__thiscall *)(void *, uint32_t);
            const uint32_t arg = (rng() % 2 ? 0x1234500u : 0xabcd00u) | (rng() % 2);
            // Its adapter is gone (lane GT: only original code reached through the game's vtables calls 0x494a00): our
            // member directly on routes 1 and 2 (the original reads the flag's low byte).
            if (gRoute == 0) ctx.Original<Fn>(0x494a00)(w, arg);
            else w->RestartTick((arg & 0xff) != 0);
            return 0;
        });
}

template <uint32_t Rva> void MemoryIdsTest(difftest::Context &ctx, const char *name)
{
    shape::StandIn context(0x4854b0, &WantSimContext::Construct, &SC_Construct);
    shape::StandIn store(0x48d650, &W::GetMemoryStore, &SG_StoreOrSpare);
    Compare(ctx, name, 600,
        [&](std::mt19937 &rng) {
            gTick = rng() % 3 ? rng() % 1200 : rng();
            gCtxPerson = AnyObj(rng, 2);
            gCtxNeighbor = AnyObj(rng, 3);
            gSpare = ::new (gSpareBytes) WantMemoryStore;
            gSpare->Construct();
            gSpare->lastTick = rng();
            gSpare->nextId = int32_t(rng() % 50);
        },
        [&](W *w, std::mt19937 &rng) -> uint32_t {
            gCopy[0][7] = reinterpret_cast<void *>(&SR_Restore); // the object's own vtable copy, on both routes
            const uint32_t sim = (rng() % 3 ? 0 : rng() & 0xffff0000u) | (rng() % 4 ? rng() % 8 : rng() & 0xffff);
            const int32_t value = rng() % 2 ? int32_t(rng() % 100) - 20 : int32_t(rng());
            using Fn = void(__thiscall *)(void *, uint32_t, int32_t);
            Call<Fn>(ctx, w, Rva, 0, nullptr, sim, value);
            Log(760, gSpare->lastTick, uint32_t(gSpare->nextId));
            gSpare->Destruct();
            return 0;
        },
        2);
}

DIFF_TEST(WantSimulator_SetMemoryIds, 0x496660) { MemoryIdsTest<0x496660>(ctx, "SetMemoryIds"); }
DIFF_TEST(WantSimulator_AddMemoryIds, 0x490520) { MemoryIdsTest<0x490520>(ctx, "AddMemoryIds"); }

DIFF_TEST(WantMemoryStore_IdSetters, 0x5516a0)
{
    Compare(ctx, "IdSetters", 300,
        [&](std::mt19937 &rng) { gTick = rng(); },
        [&](W *w, std::mt19937 &rng) -> uint32_t {
            for (auto &e : w->mMemoryStores) {
                WantMemoryStore *s = e.second;
                const int32_t v = int32_t(rng());
                if (rng() % 2) {
                    if (gRoute == 0) ctx.Original<void(__thiscall *)(WantMemoryStore *, int32_t)>(0x5516a0)(s, v);
                    else s->RestartIds(v);
                } else {
                    if (gRoute == 0) ctx.Original<void(__thiscall *)(WantMemoryStore *, int32_t)>(0x551350)(s, v);
                    else s->AddToIds(v);
                }
            }
            return 0;
        });
}

// ---- WriteDebugXml (slot 53, 0x488820; vtable-only: the original against our vtable's slot) ----
// The game's text file stream (constructor 0x1b4dd2, destructor 0x1b52d8) is a recording fake: every piece written is
// logged by kind and content hash, so the file's text and its order must agree. The skill name (0x496af0, the game's on
// both routes) and cRZString::Sprintf (0x1ad6f7) are stood in for; career objects come from the object table's list
// (slot 31) as fakes with a kind (slot 13), a definition (slot 61) with an id (slot 28) and a name (slot 49). Strings stay
// under 16 characters where a stand-in writes them, so neither side frees the other's heap.

namespace wantgg {

uint32_t HashText(const char *s, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) h = (h ^ uint8_t(s[i])) * 16777619u;
    return h;
}
uint32_t HashString(const cRZString *s) { return HashText(s->mString.data(), uint32_t(s->mString.size())); }
void SetText(cRZString *s, const char *text) { s->mString.assign(text); }

void *gStreamVt[64];
void *__fastcall TF_Construct(void *self, void *, cRZString *path, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Log(960, HashString(path), a | b << 8 | c << 16 | d << 24);
    *static_cast<void ***>(self) = gStreamVt;
    return self;
}
void __fastcall TF_Destruct(void *self, void *) { Log(961, *static_cast<void ***>(self) == gStreamVt); }
void *__fastcall TF_Int(void *self, void *, int32_t v) { Log(968, uint32_t(v)); return self; }
void *__fastcall TF_Text(void *self, void *, const char *s) { Log(974, HashText(s, uint32_t(std::strlen(s)))); return self; }
void *__fastcall TF_String(void *self, void *, cRZString *s) { Log(975, HashString(s)); return self; }

cRZString *__cdecl SN_SkillName(cRZString *out, uint32_t skill)
{
    Log(970, skill & 0xffff);
    out->Construct();
    char text[16];
    std::snprintf(text, sizeof text, "skill%u", skill & 0xffff);
    SetText(out, text);
    return out;
}
cRZString *__cdecl SP_Sprintf(cRZString *self, const char *fmt, uint32_t id)
{
    Log(971, HashText(fmt, uint32_t(std::strlen(fmt))), id);
    char text[16];
    std::snprintf(text, sizeof text, "id%08x", id);
    SetText(self, text);
    return self;
}

constexpr unsigned kCareers = 8;
FakeObj gCareer[kCareers], gDefinition[kCareers];
int16_t gKind[kCareers], gKindLater[kCareers];
uint32_t gKindCalls[kCareers];
void *gCareerVt[64], *gDefinitionVt[64];
struct ObjectNode {
    ObjectNode *next, *prev;
    void *object;
};
ObjectNode gListHead, gNodes[12];
ObjectNode *gListHolder;

uint32_t __fastcall CO_Kind(FakeObj *o, void *, uint32_t query)
{
    const unsigned i = unsigned(o - gCareer);
    Log(1013, o->tag, query);
    return 0x77770000u | uint16_t(gKindCalls[i]++ ? gKindLater[i] : gKind[i]);
}
uint32_t __fastcall CO_Definition(FakeObj *o, void *) { Log(1061, o->tag); return reinterpret_cast<uint32_t>(&gDefinition[o - gCareer]); }
uint32_t __fastcall CD_Id(FakeObj *o, void *) { Log(1028, o->tag); return o->id47; }
void __fastcall CD_Name(FakeObj *o, void *, cRZString *out)
{
    Log(1049, o->tag, HashString(out));
    char text[16];
    std::snprintf(text, sizeof text, "career%u", o->tag);
    SetText(out, text);
}
uint32_t __fastcall OT_List(Plain *, void *) { Log(631); return reinterpret_cast<uint32_t>(&gListHolder); }

void PrepareDebug(std::mt19937 &rng)
{
    for (auto &s : gStreamVt) s = nullptr;
    gStreamVt[8] = reinterpret_cast<void *>(&TF_Int);
    gStreamVt[14] = reinterpret_cast<void *>(&TF_Text);
    gStreamVt[15] = reinterpret_cast<void *>(&TF_String);
    for (auto &s : gCareerVt) s = nullptr;
    for (auto &s : gDefinitionVt) s = nullptr;
    gCareerVt[1] = reinterpret_cast<void *>(&O_AddRef);
    gCareerVt[2] = reinterpret_cast<void *>(&O_Release);
    gCareerVt[13] = reinterpret_cast<void *>(&CO_Kind);
    gCareerVt[61] = reinterpret_cast<void *>(&CO_Definition);
    gDefinitionVt[1] = reinterpret_cast<void *>(&O_AddRef);
    gDefinitionVt[2] = reinterpret_cast<void *>(&O_Release);
    gDefinitionVt[28] = reinterpret_cast<void *>(&CD_Id);
    gDefinitionVt[49] = reinterpret_cast<void *>(&CD_Name);
    gObjTableVt[31] = reinterpret_cast<void *>(&OT_List);
    static const int16_t kinds[] = {0x20, 0x2e, 0x61, 0x20, 0x2e, 0x61, 0x21, 0, 0x120, -1};
    for (unsigned i = 0; i < kCareers; ++i) {
        gCareer[i] = FakeObj();
        gCareer[i].vt = gCareerVt;
        gCareer[i].tag = 2000 + i;
        gDefinition[i] = FakeObj();
        gDefinition[i].vt = gDefinitionVt;
        gDefinition[i].tag = 3000 + i;
        gDefinition[i].id47 = rng();
        gKind[i] = kinds[rng() % 10];
        gKindLater[i] = rng() % 6 ? gKind[i] : kinds[rng() % 10]; // a kind that changes between the two asks
        gKindCalls[i] = 0;
    }
    // The table's list (some objects twice, none at all on some seeds).
    gListHead.next = gListHead.prev = &gListHead;
    gListHolder = &gListHead;
    for (unsigned i = 0, n = rng() % 13; i < n; ++i) {
        ObjectNode &node = gNodes[i];
        node.object = &gCareer[rng() % kCareers];
        node.prev = gListHead.prev;
        node.next = &gListHead;
        gListHead.prev->next = &node;
        gListHead.prev = &node;
    }
}

} // namespace wantgg

DIFF_TEST(WantSimulator_WriteDebugXml, 0x488820)
{
    // The file stream is ours (rz/RZFileStreams.h): the stand-ins cover the image's and ours.
    shape::StandIn construct(0x1b4dd2, static_cast<cRZOFStreamFromString>(&cRZOFStream::Construct), &TF_Construct);
    shape::StandIn destruct(0x1b52d8, &cRZOFStream::Destruct, &TF_Destruct);
    shape::CodePatch skillName(0x496af0, reinterpret_cast<const void *>(&SN_SkillName));
    shape::StandIn format(0x1ad6f7, &cRZString::Sprintf, &SP_Sprintf);
    Compare(ctx, "WriteDebugXml", 300, [&](std::mt19937 &rng) { PrepareDebug(rng); },
        [&](W *w, std::mt19937 &) -> uint32_t {
            using Fn = void(__thiscall *)(void *);
            Call<Fn>(ctx, w, 0x488820, 0, nullptr);
            uint32_t sum = 0;
            for (unsigned i = 0; i < kCareers; ++i) Log(1099, gCareer[i].refs, gDefinition[i].refs, gKindCalls[i]), sum += gKindCalls[i];
            return sum;
        },
        2);
}
