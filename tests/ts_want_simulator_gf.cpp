// Lane GF: cTSWantSimulator slot 52 (0x489210, the scored-wants dump) and the original functions it is built on
// (WantSimContext::Assign 0x485e40, WantMemoryStore::AdvanceToTick 0x551320 (lane GG), IndexOfMatch 0x48ce50, WantFloatText 0x499000).
//
// Slot 52 has no caller in the image but the vtable (decision 2026-10-18: no adapter route), so it runs on two routes:
// route 0 the original RVA on a simulator carrying copies of the game's vtables, route 1 our vtable's slot on a simulator
// carrying ours (tests/ts_want_simulator_shape.h). Recording stand-ins replace every callee that is game code doing work
// of its own (the update steps 0x4978e0 / 0x492160 / 0x495650 / 0x4865b0, the memory listing and sort, the text helpers
// 0x54add0 / 0x496af0 / 0x498d20, the file stream, the shell); the vector helpers (0x1d89b0, 0x4992c0, 0x483930,
// 0x1d6520, 0x4850c0) and our callees (Assign, AdvanceClock, IndexOfMatch, WantFloatText, RefPtrVector::Clear,
// Sprintf, Insert, FormatDecimal) run for real on both sides. WantSimContext::Construct and GetMemoryStore stand in on
// both sides. Compared: every call the stand-ins and fakes see (in order, with their arguments and the text written to
// the file), the reference counts of every fake, the simulator's fields slot 52 writes, and the memory store's clock.
#include "cas_body_part_system.h" // bpt::Env: the game allocator (torn down with ShutdownAndRelease) and service cache
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZString.h"
#include "ts/cTSWantSimulator.h"
#include "ts_persist_world.h" // pworld::Patches (Heap: the image's operator new / delete to ours)
#include "ts_want_simulator_shape.h"

#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <windows.h>

namespace gfdump {

using W = cTSWantSimulator;
using wantshape::gPatch;
using wantshape::gRoute;

// ---- the log ----
std::vector<std::string> gLog;
void Log(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    gLog.emplace_back(buf);
}
std::string TextOf(const void *s) { return s ? reinterpret_cast<const cRZString *>(s)->mString : std::string("<null>"); }

// ---- fakes: one struct, a vtable per role ----
struct GfFake {
    void **vt;
    int tag;
    int refs;
    // seeded answers (meaning by role)
    uint32_t a, b, c, d, e;
    float f;
    GfFake *p1, *p2, *p3;
    WantInstanceParam param;
};
constexpr int kFakes = 64;
GfFake gFakes[kFakes];
int TagOf(const void *p)
{
    for (const GfFake &f : gFakes)
        if (&f == p) return f.tag;
    return p ? -2 : -1;
}
GfFake *F(void *self) { return static_cast<GfFake *>(self); }

uint32_t __fastcall AddRef(void *s, void *) { return ++F(s)->refs; }
uint32_t __fastcall ReleaseF(void *s, void *) { return --F(s)->refs; }

// Want instance (and predecessor): 3 match, 6 description, 12 definition, 13 guid, 14 param, 15 param sim, 16 value, 17 object,
// 20 context, 21 predecessor, 23 has id, 24 id, 25 priority, 26 kind, 27 flag b, 28 flag a.
uint8_t __fastcall W_Match(void *s, void *, void *want)
{
    Log("match %d %d", F(s)->tag, TagOf(want));
    return s == want || (F(s)->e & 1) != 0;
}
uint32_t __fastcall W_Desc(void *s, void *, void *out, int32_t a, int32_t b, int32_t c)
{
    Log("desc %d %d %d %d", F(s)->tag, a, b, c);
    reinterpret_cast<cRZString *>(out)->mString.assign("d" + std::to_string(F(s)->tag));
    return 1;
}
void *__fastcall W_Def(void *s, void *) { return F(s)->p1; }
uint32_t __fastcall W_Guid(void *s, void *) { return 0x1000 + F(s)->tag; }
void *__fastcall W_Param(void *s, void *)
{
    Log("param %d %u", F(s)->tag, F(s)->param.kind);
    return &F(s)->param;
}
int16_t __fastcall W_ParamSim(void *s, void *) { return int16_t(F(s)->param.value); }
uint32_t __fastcall W_Value(void *s, void *) { return F(s)->a; }
void *__fastcall W_Object(void *s, void *) { return F(s)->p3; }
void *__fastcall W_Context(void *s, void *) { return F(s)->p2; }
void *__fastcall W_Pred(void *s, void *) { return (F(s)->e & 2) ? &gFakes[F(s)->b % 8 + 40] : nullptr; }
uint8_t __fastcall W_HasId(void *s, void *) { return (F(s)->e & 4) != 0; }
int32_t __fastcall W_Id(void *s, void *) { return int32_t(F(s)->c); }
float __fastcall W_Priority(void *s, void *) { return F(s)->f; }
uint32_t __fastcall W_Kind(void *s, void *) { return F(s)->d; }
uint8_t __fastcall W_FlagB(void *s, void *) { return (F(s)->e & 8) != 0; }
uint8_t __fastcall W_FlagA(void *s, void *) { return (F(s)->e & 16) != 0; }
void *gWantVt[40];

// Definition: 3 guid, 8 is fear, 9 category, 13 sub-object, 17 op. Sub-object: 9 test (text, -1, 1).
uint32_t __fastcall D_Guid(void *s, void *) { return F(s)->a; }
uint8_t __fastcall D_Fear(void *s, void *) { return uint8_t(F(s)->b & 1); }
uint32_t __fastcall D_Category(void *s, void *) { return F(s)->c; }
void *__fastcall D_Sub(void *s, void *)
{
    gFakes[63].e = F(s)->e;
    return &gFakes[63];
}
uint8_t __fastcall D_Test(void *s, void *, const char *text, int32_t a, int32_t b)
{
    Log("dtest %d '%s' %d %d", F(s)->tag, text, a, b);
    return (F(s)->e & 1) != 0;
}
uint32_t __fastcall D_Op(void *s, void *) { return F(s)->d; }
void *gDefVt[40], *gSubVt[20];

// Want context: 9 tree, 10 node, 21 lifetime, 25 range, 27 excluded(i), 28 weight(i).
uint32_t __fastcall C_Tree(void *s, void *) { return F(s)->a; }
uint32_t __fastcall C_Node(void *s, void *) { return F(s)->b; }
int32_t __fastcall C_Lifetime(void *s, void *) { return int32_t(F(s)->c); }
void __fastcall C_Range(void *s, void *, int32_t *lo, int32_t *hi)
{
    Log("range %d %d %d", F(s)->tag, *lo, *hi);
    if (F(s)->e & 1) *lo = int32_t(F(s)->d % 50), *hi = int32_t(F(s)->d % 50 + 7);
}
uint8_t __fastcall C_Excluded(void *s, void *, int32_t i) { return ((F(s)->e >> (i + 1)) & 1) != 0; }
int32_t __fastcall C_Weight(void *s, void *, int32_t i) { return int32_t(F(s)->d) * 3 + i; }
void *gCtxVt[40];

// Person: 8 data (92 name), 13 mood(3). Neighbor: 10 named (49), 12 relationships. Relationships: 5 count(id), 9 value(id, 2).
// Named object: 49 name(out). Found object: 8 id, 10 named, 27 other, 49 name, 61 named.
void *__fastcall P_Data(void *s, void *) { return s; }
const char *__fastcall P_Name(void *s, void *) { return F(s)->e & 1 ? "" : "Bella"; }
float __fastcall P_Mood(void *s, void *, int32_t which)
{
    Log("mood %d %d", F(s)->tag, which);
    return F(s)->f;
}
void *__fastcall N_Named(void *, void *) { return &gFakes[39]; }
void *__fastcall N_Rel(void *s, void *) { return F(s)->p1; }
uint32_t __fastcall R_Count(void *s, void *, int32_t id)
{
    Log("relcount %d %d", F(s)->tag, id);
    return F(s)->a % 5;
}
int32_t __fastcall R_Value(void *s, void *, int32_t id, int32_t kind)
{
    Log("relvalue %d %d %d", F(s)->tag, id, kind);
    return int32_t(F(s)->b % 200) - 100;
}
uint32_t __fastcall O_Name(void *s, void *, void *out)
{
    Log("name %d", F(s)->tag);
    reinterpret_cast<cRZString *>(out)->mString.assign("n" + std::to_string(F(s)->tag));
    return 1;
}
int16_t __fastcall O_Id(void *s, void *) { return int16_t(F(s)->a); }
void *__fastcall O_Self(void *s, void *) { return s; }
void *__fastcall O_Other(void *s, void *) { return F(s)->p1; }
void *gPersonVt[100], *gNbrVt[40], *gRelVt[40], *gObjVt[70];

// Globals: 4 want service (26 tick), 10 object table (16 find), 26 registry (42 find). C: 74 age. Debugger: 5. Definitions: 6.
void *__fastcall G_Service(void *, void *) { return &gFakes[60]; }
void *__fastcall G_Table(void *, void *) { return &gFakes[61]; }
void *__fastcall G_Registry(void *, void *) { return &gFakes[62]; }
uint32_t gTick;
uint32_t __fastcall S_Tick(void *, void *)
{
    gTick += gFakes[60].a % 9;
    Log("tick %u", gTick);
    return gTick;
}
void *__fastcall T_Find(void *s, void *, int32_t id, int32_t flag)
{
    Log("find %d %d", id, flag);
    return (F(s)->a >> (id & 15)) & 1 ? &gFakes[30 + (id & 7)] : nullptr;
}
void *__fastcall RG_Find(void *s, void *, uint32_t guid)
{
    Log("registry %u", guid);
    return (F(s)->a >> (guid & 15)) & 1 ? &gFakes[30 + (guid & 7)] : nullptr;
}
int32_t __fastcall C_Age(void *, void *, int32_t value)
{
    Log("age %d", value);
    return value;
}
uint8_t __fastcall DB_Off(void *s, void *) { return F(s)->a & 1; }
uint32_t gF8Value;
uint32_t *gF8Cell = &gF8Value;
void *__fastcall DM_Get(void *, void *) { return &gF8Cell; }
void *gGlobalsVt[40], *gServiceVt[40], *gTableVt[40], *gRegistryVt[50], *gCVt[80], *gDebugVt[10], *gDefMgrVt[10];

// The stream (vtable at the start of the caller's 0x18 bytes).
uint8_t __fastcall ST_I16(void *, void *, int32_t v)
{
    Log("I16 %d", int16_t(v));
    return 1;
}
uint8_t __fastcall ST_I32(void *, void *, int32_t v)
{
    Log("I32 %d", v);
    return 1;
}
uint8_t __fastcall ST_U32(void *, void *, uint32_t v)
{
    Log("U32 %u", v);
    return 1;
}
uint8_t __fastcall ST_Text(void *, void *, const char *t)
{
    Log("T %s", t);
    return 1;
}
uint8_t __fastcall ST_Str(void *, void *, void *s)
{
    Log("S %s", TextOf(s).c_str());
    return 1;
}
void *gStreamVt[20];

// ---- the world for one case ----
struct World {
    int16_t sim;
    bool person, neighbor, rel;
    uint8_t withMemories, plainName, keepOptions;
    bool fail;
    int debugger; // 0 none, 1 says no, 2 says yes
    uint32_t loops;
    uint32_t factorCount[40];
    uint32_t seed;
};
World gWorld;
alignas(4) uint8_t gAttributes[0x80];
WantMemoryStore *gStore;
uint32_t gLoopCalls;

uint32_t RandomBits(std::mt19937 &rng) { return rng() % 3 ? rng() % 64 : rng(); }

void SetupVtables()
{
    auto init = [](void **vt, size_t n) {
        for (size_t i = 0; i < n; ++i) vt[i] = nullptr;
        vt[1] = reinterpret_cast<void *>(&AddRef);
        vt[2] = reinterpret_cast<void *>(&ReleaseF);
    };
    init(gWantVt, 40);
    gWantVt[3] = reinterpret_cast<void *>(&W_Match);
    gWantVt[6] = reinterpret_cast<void *>(&W_Desc);
    gWantVt[12] = reinterpret_cast<void *>(&W_Def);
    gWantVt[13] = reinterpret_cast<void *>(&W_Guid);
    gWantVt[14] = reinterpret_cast<void *>(&W_Param);
    gWantVt[15] = reinterpret_cast<void *>(&W_ParamSim);
    gWantVt[16] = reinterpret_cast<void *>(&W_Value);
    gWantVt[17] = reinterpret_cast<void *>(&W_Object);
    gWantVt[20] = reinterpret_cast<void *>(&W_Context);
    gWantVt[21] = reinterpret_cast<void *>(&W_Pred);
    gWantVt[23] = reinterpret_cast<void *>(&W_HasId);
    gWantVt[24] = reinterpret_cast<void *>(&W_Id);
    gWantVt[25] = reinterpret_cast<void *>(&W_Priority);
    gWantVt[26] = reinterpret_cast<void *>(&W_Kind);
    gWantVt[27] = reinterpret_cast<void *>(&W_FlagB);
    gWantVt[28] = reinterpret_cast<void *>(&W_FlagA);
    init(gDefVt, 40);
    gDefVt[3] = reinterpret_cast<void *>(&D_Guid);
    gDefVt[8] = reinterpret_cast<void *>(&D_Fear);
    gDefVt[9] = reinterpret_cast<void *>(&D_Category);
    init(gSubVt, 20);
    gSubVt[9] = reinterpret_cast<void *>(&D_Test);
    gDefVt[13] = reinterpret_cast<void *>(&D_Sub);
    gDefVt[17] = reinterpret_cast<void *>(&D_Op);
    init(gCtxVt, 40);
    gCtxVt[9] = reinterpret_cast<void *>(&C_Tree);
    gCtxVt[10] = reinterpret_cast<void *>(&C_Node);
    gCtxVt[21] = reinterpret_cast<void *>(&C_Lifetime);
    gCtxVt[25] = reinterpret_cast<void *>(&C_Range);
    gCtxVt[27] = reinterpret_cast<void *>(&C_Excluded);
    gCtxVt[28] = reinterpret_cast<void *>(&C_Weight);
    init(gPersonVt, 100);
    gPersonVt[8] = reinterpret_cast<void *>(&P_Data);
    gPersonVt[13] = reinterpret_cast<void *>(&P_Mood);
    gPersonVt[92] = reinterpret_cast<void *>(&P_Name);
    init(gNbrVt, 40);
    gNbrVt[10] = reinterpret_cast<void *>(&N_Named);
    gNbrVt[12] = reinterpret_cast<void *>(&N_Rel);
    init(gRelVt, 40);
    gRelVt[5] = reinterpret_cast<void *>(&R_Count);
    gRelVt[9] = reinterpret_cast<void *>(&R_Value);
    init(gObjVt, 70);
    gObjVt[8] = reinterpret_cast<void *>(&O_Id);
    gObjVt[10] = reinterpret_cast<void *>(&O_Self);
    gObjVt[27] = reinterpret_cast<void *>(&O_Other);
    gObjVt[49] = reinterpret_cast<void *>(&O_Name);
    gObjVt[61] = reinterpret_cast<void *>(&O_Self);
    init(gGlobalsVt, 40);
    gGlobalsVt[4] = reinterpret_cast<void *>(&G_Service);
    gGlobalsVt[10] = reinterpret_cast<void *>(&G_Table);
    gGlobalsVt[26] = reinterpret_cast<void *>(&G_Registry);
    init(gServiceVt, 40);
    gServiceVt[26] = reinterpret_cast<void *>(&S_Tick);
    init(gTableVt, 40);
    gTableVt[16] = reinterpret_cast<void *>(&T_Find);
    init(gRegistryVt, 50);
    gRegistryVt[42] = reinterpret_cast<void *>(&RG_Find);
    init(gCVt, 80);
    gCVt[74] = reinterpret_cast<void *>(&C_Age);
    init(gDebugVt, 10);
    gDebugVt[5] = reinterpret_cast<void *>(&DB_Off);
    init(gDefMgrVt, 10);
    gDefMgrVt[6] = reinterpret_cast<void *>(&DM_Get);
    for (auto &s : gStreamVt) s = nullptr;
    gStreamVt[6] = reinterpret_cast<void *>(&ST_I16);
    gStreamVt[8] = reinterpret_cast<void *>(&ST_I32);
    gStreamVt[9] = reinterpret_cast<void *>(&ST_U32);
    gStreamVt[14] = reinterpret_cast<void *>(&ST_Text);
    gStreamVt[15] = reinterpret_cast<void *>(&ST_Str);
}

// Fakes 0..19 wants (definitions 20..27 by tag), 28 person, 29 neighbour, 30..37 found objects, 38 relationships,
// 39 spare, 40..47 predecessors, 48..55 contexts, 56 C, 57 debugger, 58 definitions manager, 59 globals, 60 service,
// 61 table, 62 registry, 63 spare.
void ResetFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 7919 + 1);
    for (int i = 0; i < kFakes; ++i) {
        GfFake &f = gFakes[i];
        f = GfFake();
        f.tag = 100 + i;
        f.refs = 1;
        f.a = RandomBits(rng), f.b = rng(), f.c = RandomBits(rng), f.d = rng() % 9, f.e = rng();
        f.f = float(int32_t(rng() % 20000) - 5000) / float(1 + rng() % 64);
    }
    for (int i = 0; i < 20; ++i) {
        GfFake &f = gFakes[i];
        f.vt = gWantVt;
        f.p1 = &gFakes[20 + rng() % 8];
        f.p2 = rng() % 4 ? &gFakes[48 + rng() % 8] : nullptr;
        f.p3 = rng() % 3 ? &gFakes[30 + rng() % 8] : nullptr;
        f.param.kind = rng() % 9;
        f.param.value = rng() % 3 ? rng() % 20 : rng();
        f.param.extra = int16_t(rng() % 3 ? int32_t(rng() % 5) - 1 : int32_t(rng()));
        f.c = rng() % 4 ? rng() % 500 : rng();
    }
    for (int i = 20; i < 28; ++i) {
        gFakes[i].vt = gDefVt;
        gFakes[i].a = rng();
        gFakes[i].c = rng() % 4;
        gFakes[i].d = rng() % 7;
    }
    gFakes[28].vt = gPersonVt;
    gFakes[29].vt = gNbrVt;
    gFakes[29].p1 = rng() % 4 ? &gFakes[38] : nullptr;
    for (int i = 30; i < 38; ++i) {
        gFakes[i].vt = gObjVt;
        gFakes[i].p1 = rng() % 2 ? &gFakes[30 + rng() % 8] : nullptr;
    }
    gFakes[38].vt = gRelVt;
    gFakes[39].vt = gObjVt; // the neighbour's named face
    for (int i = 40; i < 48; ++i) {
        gFakes[i].vt = gWantVt;
        gFakes[i].p1 = &gFakes[20];
        gFakes[i].c = rng() % 4 ? rng() % 3000 : rng();
    }
    for (int i = 48; i < 56; ++i) {
        gFakes[i].vt = gCtxVt;
        gFakes[i].c = rng() % 3 == 0 ? uint32_t(-int32_t(rng() % 3)) : (rng() % 4 ? rng() % 100000 : rng());
    }
    gFakes[56].vt = gCVt;
    gFakes[57].vt = gDebugVt;
    gFakes[58].vt = gDefMgrVt;
    gFakes[59].vt = gGlobalsVt;
    gFakes[60].vt = gServiceVt;
    gFakes[61].vt = gTableVt;
    gFakes[62].vt = gRegistryVt;
    gFakes[63].vt = gSubVt;
    gF8Value = rng();
    gTick = rng() % 4 ? 5000 + rng() % 100 : rng();

    World &w = gWorld;
    w.seed = seed;
    w.sim = int16_t(rng() % 4 ? 1 + rng() % 30 : rng());
    w.neighbor = rng() % 6 != 0;
    w.person = w.neighbor && rng() % 3 != 0;
    w.withMemories = uint8_t(rng() % 2 ? rng() % 2 : rng() % 256);
    w.plainName = uint8_t(rng() % 2 ? rng() % 2 : rng() % 256);
    w.keepOptions = uint8_t(rng() % 2 ? rng() % 2 : rng() % 256);
    w.fail = rng() % 8 == 0;
    w.debugger = int(rng() % 3);
    w.loops = 1 + rng() % 3;
    for (auto &n : w.factorCount) n = rng() % 4 == 0 ? 0 : rng() % 12;
    for (size_t i = 0; i < sizeof gAttributes; i += 2) *reinterpret_cast<int16_t *>(gAttributes + i) = int16_t(rng() % 3 ? rng() % 1000 : rng());
    const int16_t ages[] = {0, 1, 2, 3, 4, 13, 5, 12, 14, -1};
    *reinterpret_cast<int16_t *>(gAttributes + 0x74) = ages[rng() % 10];
    const int16_t asps[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 0, 3, 512, -2};
    *reinterpret_cast<int16_t *>(gAttributes + 0x5c) = asps[rng() % 13];
    // A sim without a neighbour finds no person objects (the original crashes on the neighbour's relationships otherwise).
    if (!w.neighbor) gFakes[61].a = 0;
}

// ---- stand-ins ----
WantSimContext *__fastcall StandContext(WantSimContext *c, void *, int16_t sim)
{
    Log("context %d", sim);
    c->simId = sim;
    c->person = gWorld.person ? &gFakes[28] : nullptr;
    c->neighbor = gWorld.neighbor ? reinterpret_cast<ifaces::cITSNeighbor *>(&gFakes[29]) : nullptr;
    if (c->person) ++gFakes[28].refs;
    if (c->neighbor) ++gFakes[29].refs;
    c->attributes = reinterpret_cast<uint32_t>(gAttributes);
    return c;
}
WantMemoryStore *__fastcall StandStore(W *, void *, int16_t sim)
{
    Log("store %d", sim);
    return gStore;
}
using AssignFn = void(__thiscall *)(WantRefList *, void **, uint32_t);
uint8_t __fastcall StandLoop(W *w, void *, int32_t steps)
{
    ++gLoopCalls;
    Log("loop %d %u", steps, gLoopCalls);
    if (gLoopCalls == 1) {
        std::mt19937 rng(gWorld.seed + 5);
        void *items[6];
        const uint32_t n = rng() % 6;
        for (uint32_t i = 0; i < n; ++i) items[i] = rng() % 5 ? &gFakes[rng() % 20] : nullptr;
        reinterpret_cast<AssignFn>(GameAddress<void>(0x483930))(&w->mVecEC, items, n);
    }
    return gLoopCalls >= gWorld.loops;
}
void __fastcall StandRank(W *w, void *, WantSimContext *c, WantRefList *list)
{
    Log("rank %d %d %d", c->simId, list == &w->mVecEC, int(list->Size()));
}
void __fastcall StandChoose(W *w, void *, WantSimContext *c, WantRefList *pool, WantRefList *wants, WantRefList *fears, uint32_t a,
                            uint32_t b, uint32_t d)
{
    Log("choose %d %d %u %u %u %u %u %u", c->simId, pool == &w->mVecEC, wants->Size(), fears->Size(), a, b, d,
        uint32_t(w->mCurrentSim));
    std::mt19937 rng(gWorld.seed + 9);
    for (WantRefList *l : {wants, fears})
        for (void **it = l->mFirst; it != l->mLast; ++it)
            if (!*it && rng() % 2) {
                void *pick = pool->Size() && rng() % 2 ? pool->mFirst[rng() % pool->Size()] : &gFakes[rng() % 20];
                if (pick) {
                    *it = pick;
                    ++F(pick)->refs;
                }
            }
}
void __fastcall StandMemories(WantMemoryStore *s, void *, MemoryList *list, uint32_t flag)
{
    Log("memories %d %u", s == gStore, flag);
    if (!gWorld.withMemories) return;
    std::mt19937 rng(gWorld.seed + 11);
    for (uint32_t i = 0, n = rng() % 4; i < n; ++i) list->Append(reinterpret_cast<ifaces::cITSWantInstance *>(&gFakes[rng() % 20]));
}
void __cdecl StandSort(void **first, void **last, int32_t count, void *pred)
{
    Log("sort %d %d %d", int(last - first), count, pred == GameAddress<void>(0x487bc0));
    for (void **a = first, **b = last; a < b && a < --b; ++a) std::swap(*a, *b);
}
int16_t __cdecl StandSecondary(void *person)
{
    Log("secondary %d", TagOf(person));
    const int16_t values[] = {0, 1, 2, 4, 16, 32, 64, 128, 256, -1, 8, 3};
    return values[(gWorld.seed + 3) % 12];
}
float __fastcall StandFactors(W *, void *, WantSimContext *c, void *want, std::vector<float> *factors)
{
    Log("factors %d %d", c->simId, TagOf(want));
    const uint32_t n = gWorld.factorCount[TagOf(want) % 40];
    std::mt19937 rng(gWorld.seed * 3 + TagOf(want));
    for (uint32_t i = 0; i < n; ++i) factors->push_back(rng() % 3 == 0 ? 0.0f : float(int32_t(rng() % 4000) - 1000) / 7.0f);
    return 1.5f;
}
cRZString *__cdecl StandSkill(cRZString *out, uint32_t value)
{
    Log("skill %u", value);
    out->Construct(value % 3 == 0 ? "" : ("sk" + std::to_string(value)).c_str());
    return out;
}
cRZString *__cdecl StandQuote(cRZString *out, cRZString *in)
{
    Log("quote %s", in->mString.c_str());
    out->Construct(("[" + in->mString + "]").c_str());
    return out;
}
void *__fastcall StandOpen(uint8_t *self, void *, cRZString *path, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Log("open %s %u %u %u %u", path->mString.c_str(), a, b, c, d);
    *reinterpret_cast<void ***>(self) = gStreamVt;
    self[4] = gWorld.fail;
    return self;
}
uint8_t __fastcall StandFail(uint8_t *self, void *)
{
    Log("fail");
    return self[4];
}
void __fastcall StandFlush(uint8_t *, void *) { Log("flush"); }
void __fastcall StandClose(uint8_t *, void *) { Log("close"); }
HINSTANCE __stdcall StandShell(HWND window, const char *verb, const char *file, const char *params, const char *dir, int show)
{
    Log("shell %p %s %s %p %p %d", static_cast<void *>(window), verb, file, static_cast<const void *>(params),
        static_cast<const void *>(dir), show);
    return reinterpret_cast<HINSTANCE>(42);
}

// Simulator slots the dump calls through its vtable.
void __fastcall SL_Flush(W *, void *) { Log("flushupdates"); }
uint32_t __fastcall SL_ScoreMotive(W *, void *, void *who, uint8_t apply)
{
    Log("scoremotive %d %u", TagOf(who), apply);
    return gWorld.seed % 7;
}
uint32_t __fastcall SL_ScoreAttribute(W *, void *, void *who, uint8_t apply)
{
    Log("scoreattribute %d %u", TagOf(who), apply);
    return gWorld.seed % 11 + 3;
}
int32_t __fastcall SL_ScoreForValue(W *, void *, int32_t key, int32_t value)
{
    Log("scoreforvalue %d %d", key, value);
    return key * 100 + value;
}
uint32_t __fastcall SL_WantText(W *, void *, uint32_t key, uint32_t index, uint32_t out)
{
    Log("wanttext %u %u", key, index);
    reinterpret_cast<cRZString *>(out)->mString.assign("wt" + std::to_string(key));
    return 1;
}

// ---- the simulator per side ----
alignas(8) unsigned char gSimBuf[sizeof(W)];
WantRefList *NewRefs(std::mt19937 &rng, WantRefList &l, uint32_t maxCount)
{
    const uint32_t n = rng() % (maxCount + 1);
    const uint32_t cap = n + rng() % 3;
    if (!cap) return &l;
    l.mFirst = static_cast<void **>(bpt::ServiceAlloc(cap * 4));
    l.mLast = l.mFirst + n;
    l.mEnd = l.mFirst + cap;
    for (uint32_t i = 0; i < n; ++i) {
        void *p = rng() % 5 ? &gFakes[rng() % 20] : nullptr;
        l.mFirst[i] = p;
        if (p) ++F(p)->refs;
    }
    return &l;
}
W *BuildSim()
{
    std::mt19937 rng(gWorld.seed * 13 + 5);
    std::memset(static_cast<void *>(gSimBuf), 0x5c, sizeof gSimBuf);
    W *w = wantshape::Construct(gSimBuf);
    w->mDefinitions.object = &gFakes[58];
    w->mPendingPriority = rng();
    w->mD0 = rng();
    w->mF8 = rng();
    w->m104 = int32_t(rng());
    if (rng() % 2) w->mRefC0.object = &gFakes[28], ++gFakes[28].refs;
    if (rng() % 2) w->mRefC4.object = &gFakes[29], ++gFakes[29].refs;
    NewRefs(rng, w->mWants[gWorld.sim], 6);
    NewRefs(rng, w->mFears[gWorld.sim], 5);
    if (rng() % 2) NewRefs(rng, w->mWants[int16_t(gWorld.sim + 1)], 3);
    if (rng() % 2) NewRefs(rng, w->mMap50[gWorld.sim], 3);
    if (rng() % 2) NewRefs(rng, w->mMap58[3], 3);
    NewRefs(rng, w->mVecD4, 3);
    NewRefs(rng, w->mVecEC, 3);
    return w;
}
void DumpRefs(std::vector<int64_t> &d, const WantRefList &l)
{
    d.push_back(l.Size());
    for (void **it = l.mFirst; it != l.mLast; ++it) d.push_back(TagOf(*it));
}
std::vector<int64_t> Snapshot(W *w)
{
    std::vector<int64_t> d;
    d.push_back(w->m104);
    d.push_back(w->mPendingPriority);
    d.push_back(w->mCurrentSim), d.push_back(w->mBE);
    d.push_back(TagOf(w->mRefC0.object)), d.push_back(TagOf(w->mRefC4.object));
    d.push_back(w->mC8 == reinterpret_cast<uint32_t>(gAttributes) ? 1 : int64_t(w->mC8));
    d.push_back(w->mCurrentStore == gStore), d.push_back(w->mD0), d.push_back(w->mF8);
    DumpRefs(d, w->mVecD4), DumpRefs(d, w->mVecE0), DumpRefs(d, w->mVecEC);
    for (auto *m : {&w->mWants, &w->mFears, &w->mMap50, &w->mMap58}) {
        d.push_back(int64_t(m->size()));
        for (auto &e : *m) d.push_back(e.first), DumpRefs(d, e.second);
    }
    d.push_back(gStore->lastTick), d.push_back(gStore->nextId), d.push_back(gTick);
    for (const GfFake &f : gFakes) d.push_back(f.refs);
    return d;
}

struct Outcome {
    std::vector<std::string> log;
    std::vector<int64_t> state;
};

// One case per seed, routes 0 (original) and 1 (our vtable).
void RunDump(difftest::Context &ctx, uint32_t seeds)
{
    using Fn = void(__thiscall *)(W *, uint32_t, uint32_t, uint32_t, uint32_t);
    bpt::Env env(ctx);
    pworld::Patches p;
    p.Heap();
    p.Both(0x4854b0, &WantSimContext::Construct, &StandContext);
    p.Both(0x48d650, &W::GetMemoryStore, &StandStore);
    p.Image(0x4978e0, &StandLoop);
    p.Image(0x492160, &StandRank);
    p.Image(0x495650, &StandChoose);
    p.Both(0x550d80, &WantMemoryStore::CollectMemories, &StandMemories); // ours calls our CollectMemories
    p.Image(0x484e00, &StandSort);
    p.Image(0x54add0, &StandSecondary);
    p.Image(0x4865b0, &StandFactors);
    p.Image(0x496af0, &StandSkill);
    p.Image(0x498d20, &StandQuote);
    p.Both(0x1b4dd2, static_cast<cRZOFStreamFromString>(&cRZOFStream::Construct), &StandOpen); // the file stream is ours
    p.Both(0x1b5941, &cRZOFStream::GetError, &StandFail);
    p.Both(0x1b5939, &cRZOFStream::Flush, &StandFlush);
    p.Both(0x1b52d8, &cRZOFStream::Destruct, &StandClose);
    // The shell: the image's import cell, and the function our import reaches.
    void **cell = ctx.image.At<void *>(0xd9a484);
    DWORD old;
    VirtualProtect(cell, 4, PAGE_READWRITE, &old);
    void *const savedCell = *cell;
    *cell = reinterpret_cast<void *>(&StandShell);
    shape::CodePatch shell(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("shell32.dll"), "ShellExecuteA")),
                           reinterpret_cast<const void *>(&StandShell));
    SetupVtables();
    void **const caches[] = {ctx.image.At<void *>(0x10cc288), ctx.image.At<void *>(0x10cc290), ctx.image.At<void *>(0x10cc294)};
    void *saved[3];
    for (int i = 0; i < 3; ++i) saved[i] = *caches[i];

    wantshape::ClearPatches();
    gPatch[0][11] = reinterpret_cast<void *>(&SL_ScoreMotive);
    gPatch[0][12] = reinterpret_cast<void *>(&SL_ScoreAttribute);
    gPatch[0][27] = reinterpret_cast<void *>(&SL_WantText);
    gPatch[0][42] = reinterpret_cast<void *>(&SL_ScoreForValue);
    gPatch[0][55] = reinterpret_cast<void *>(&SL_Flush);
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        ++ctx.cases;
        Outcome out[2];
        for (int side = 0; side < 2; ++side) {
            gRoute = side;
            ResetFakes(seed);
            *caches[0] = &gFakes[59];
            *caches[1] = &gFakes[56];
            *caches[2] = gWorld.debugger ? &gFakes[57] : nullptr;
            gFakes[57].a = gWorld.debugger == 1;
            gStore = static_cast<WantMemoryStore *>(::operator new(sizeof(WantMemoryStore)));
            gStore->Construct();
            std::mt19937 rng(seed * 3 + 1);
            gStore->lastTick = rng() % 2 ? 0x3fffffff : 4990 + rng() % 30;
            gStore->nextId = int32_t(rng() % 3 ? rng() % 1000 : rng());
            gLoopCalls = 0;
            W *w = BuildSim();
            gLog.clear();
            wantshape::Call<Fn>(ctx, w, 0x489210, 0, nullptr, uint32_t(uint16_t(gWorld.sim)) | (rng() % 2 ? 0x77770000u : 0u),
                                uint32_t(gWorld.withMemories), uint32_t(gWorld.plainName), uint32_t(gWorld.keepOptions));
            out[side].log = gLog;
            out[side].state = Snapshot(w);
            wantshape::Destroy(w);
            gStore->Destruct();
            ::operator delete(gStore, sizeof(WantMemoryStore));
        }
        gRoute = 0;
        const Outcome &a = out[0], &b = out[1];
        if (a.log != b.log) {
            size_t i = 0;
            while (i < a.log.size() && i < b.log.size() && a.log[i] == b.log[i]) ++i;
            ctx.Fail("dump seed %u: call %zu differs (%zu vs %zu): '%s' vs '%s'", seed, i, a.log.size(), b.log.size(),
                     i < a.log.size() ? a.log[i].c_str() : "-", i < b.log.size() ? b.log[i].c_str() : "-");
        }
        if (a.state != b.state) {
            size_t i = 0;
            while (i < a.state.size() && i < b.state.size() && a.state[i] == b.state[i]) ++i;
            ctx.Fail("dump seed %u: state differs at %zu: %lld vs %lld", seed, i, i < a.state.size() ? (long long)a.state[i] : 0LL,
                     i < b.state.size() ? (long long)b.state[i] : 0LL);
        }
    }
    for (int i = 0; i < 3; ++i) *caches[i] = saved[i];
    *cell = savedCell;
    VirtualProtect(cell, 4, old, &old);
}



// ---- the helpers ----

void TestAssign(difftest::Context &ctx)
{
    using Fn = WantSimContext *(__thiscall *)(WantSimContext *, const WantSimContext *);
    SetupVtables();
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        ++ctx.cases;
        std::vector<int64_t> out[2];
        for (int side = 0; side < 2; ++side) {
            ResetFakes(seed);
            std::mt19937 rng(seed);
            gLog.clear();
            WantSimContext pair[2];
            for (WantSimContext &c : pair) {
                std::memset(&c, 0x6d, sizeof c);
                c.simId = int16_t(rng());
                c.person = rng() % 3 ? &gFakes[28 + rng() % 2] : nullptr;
                c.neighbor = rng() % 3 ? reinterpret_cast<ifaces::cITSNeighbor *>(&gFakes[28 + rng() % 2]) : nullptr;
                c.attributes = rng();
            }
            gFakes[28].vt = gFakes[29].vt = gWantVt;
            WantSimContext &to = pair[0];
            const WantSimContext &from = rng() % 6 == 0 ? pair[0] : pair[1];
            WantSimContext *r = side == 0 ? ctx.Original<Fn>(0x485e40)(&to, &from) : to.Assign(from);
            auto &d = out[side];
            d.push_back(r == &to);
            for (const WantSimContext &c : pair) {
                const uint32_t *words = reinterpret_cast<const uint32_t *>(&c);
                for (int i = 0; i < 4; ++i) d.push_back(words[i]);
            }
            d.push_back(gFakes[28].refs), d.push_back(gFakes[29].refs);
        }
        if (out[0] != out[1]) ctx.Fail("Assign seed %u differs", seed);
    }
}

void TestAdvanceClock(difftest::Context &ctx)
{
    using Fn = int32_t(__thiscall *)(WantMemoryStore *);
    SetupVtables();
    void **cache = ctx.image.At<void *>(0x10cc288);
    void *const saved = *cache;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        ++ctx.cases;
        int64_t out[2][4];
        for (int side = 0; side < 2; ++side) {
            ResetFakes(seed);
            *cache = &gFakes[59];
            std::mt19937 rng(seed);
            gFakes[60].a = rng() % 4 ? rng() % 9 : rng();
            gTick = rng() % 3 ? rng() % 100 : rng();
            alignas(8) unsigned char raw[sizeof(WantMemoryStore)] = {};
            WantMemoryStore &s = *reinterpret_cast<WantMemoryStore *>(raw); // only the clock fields are touched
            s.lastTick = rng() % 3 ? gTick + rng() % 20 - 10 : rng();
            s.nextId = int32_t(rng() % 3 ? rng() % 100 : rng());
            const int32_t r = side == 0 ? ctx.Original<Fn>(0x551320)(&s) : s.AdvanceToTick();
            out[side][0] = r, out[side][1] = s.lastTick, out[side][2] = s.nextId, out[side][3] = gTick;
        }
        if (std::memcmp(out[0], out[1], sizeof out[0])) ctx.Fail("AdvanceClock seed %u: %lld/%lld/%lld vs %lld/%lld/%lld", seed, out[0][0],
                                                                  out[0][1], out[0][2], out[1][0], out[1][1], out[1][2]);
    }
    *cache = saved;
}

void TestIndexOfMatch(difftest::Context &ctx)
{
    using Fn = int32_t(__thiscall *)(W *, void *, WantRefList *, void **);
    SetupVtables();
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        ++ctx.cases;
        std::vector<std::string> logs[2];
        int64_t out[2][3];
        for (int side = 0; side < 2; ++side) {
            ResetFakes(seed);
            std::mt19937 rng(seed);
            void *items[8];
            const uint32_t n = rng() % 8;
            for (uint32_t i = 0; i < n; ++i) items[i] = rng() % 4 ? &gFakes[rng() % 20] : nullptr;
            WantRefList list;
            list.mFirst = items, list.mLast = items + n, list.mEnd = items + n;
            void *want = &gFakes[rng() % 20];
            void *found = reinterpret_cast<void *>(uintptr_t(0x1234));
            gLog.clear();
            W *self = reinterpret_cast<W *>(uintptr_t(rng()));
            const int32_t r = side == 0 ? ctx.Original<Fn>(0x48ce50)(self, want, &list, &found) : self->IndexOfMatch(want, &list, &found);
            list.mFirst = list.mLast = list.mEnd = nullptr;
            out[side][0] = r, out[side][1] = TagOf(found), out[side][2] = n;
            logs[side] = gLog;
        }
        if (std::memcmp(out[0], out[1], sizeof out[0]) || logs[0] != logs[1]) ctx.Fail("IndexOfMatch seed %u: %lld vs %lld", seed, out[0][0], out[1][0]);
    }
}

void TestFloatText(difftest::Context &ctx)
{
    using Fn = cRZString *(__cdecl *)(cRZString *, float);
    bpt::Env env(ctx);
    pworld::Patches p;
    p.Heap();
    std::mt19937 rng(77);
    const float fixed[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 0.00005f, 0.00004f, 123456.789f, -99999.99f, 1e10f, -1e-10f, 3.14159265f, 1e30f, -2.5f};
    for (uint32_t i = 0; i < 6000; ++i) {
        ++ctx.cases;
        float v;
        if (i < sizeof fixed / sizeof fixed[0]) v = fixed[i];
        else if (i % 3 == 0) v = std::bit_cast<float>(uint32_t(rng()) & 0xff7fffffu); // finite bit patterns
        else v = float(int32_t(rng() % 2000000) - 1000000) / float(1 + rng() % 10000);
        alignas(4) unsigned char a[sizeof(cRZString)], b[sizeof(cRZString)];
        std::memset(a, 0xcc, sizeof a), std::memset(b, 0xcc, sizeof b);
        cRZString *ra = ctx.Original<Fn>(0x499000)(reinterpret_cast<cRZString *>(a), v);
        cRZString *rb = WantFloatText(reinterpret_cast<cRZString *>(b), v);
        auto *sa = reinterpret_cast<cRZString *>(a), *sb = reinterpret_cast<cRZString *>(b);
        if (ra != sa || rb != sb || sa->mString != sb->mString || sa->mRefCount != sb->mRefCount ||
            sa->mVTable != ctx.image.At<void>(0xd9b470) || sb->mVTable != sa->mVTable)
            ctx.Fail("FloatText %08x: '%s' vs '%s'", std::bit_cast<uint32_t>(v), sa->mString.c_str(), sb->mString.c_str());
        sa->Destruct();
        sb->Destruct();
    }
}

} // namespace gfdump

DIFF_TEST(WantSimulator_DumpScoredWants, 0x489210) { gfdump::RunDump(ctx, 600); }
DIFF_TEST(WantSimContext_Assign, 0x485e40) { gfdump::TestAssign(ctx); }
DIFF_TEST(WantMemoryStore_AdvanceClock, 0x551320) { gfdump::TestAdvanceClock(ctx); }
DIFF_TEST(WantSimulator_IndexOfMatch, 0x48ce50) { gfdump::TestIndexOfMatch(ctx); }
DIFF_TEST(WantFloatText, 0x499000) { gfdump::TestFloatText(ctx); }
