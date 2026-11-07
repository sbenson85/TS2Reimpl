// Differential tests for cTSWantSimulatorTuningLoader (lane IL, src/ts/cTSWantSimulatorTuningLoader.h).
//
// WantTuningLoader_Load (0x494030): the simulator's LoadTuning end to end on a simulator stand-in (the fields LoadTuning and
// the loader touch, built in a buffer: the counted curve fields +0x1c/+0x20, the category wants +0x60, the default wants
// +0x68, the level tables +0x74, the category map +0x7c; a fake vtable for slots 5 AddCategoryWant and 23
// GetWantDefinition). The resource manager, record, stream, framework and parser are recording fakes; the parser's Parse
// replays a random element script onto the handler it was given, so the original side runs the game's loader (vtable
// 0xdcae44: StartElement 0x48ece0, EndElement 0x48e390, Release 0x494830) and our side ours, through our vtable. Compared:
// every fake call (arguments normalised), the handler's return values, the simulator's fields (curve objects by id with
// their points, level tables, category map, default wants), the fakes' counts, and the set of unknown element names (the
// image's function-local sets 0x10daa7c / 0x10daa88 against ours). The category map's third word is compared in its low
// 16 bits only: the original stores the string set with a word store (0x48fb5a, 0x48fbcf, 0x48fc99) into a stack dword
// whose high half is never written, then copies the dword (0x48fd3e).
// Callees stood in on both sides: the curve constructor 0x54b890, the want instance pool 0x54e570, parameter 0x54e500 and
// constructor 0x54e2b0, the skill name 0x496af0; the CRT's strtol / strtoul / atoi and ParseHex32 (their image copies use
// the CRT's locale through imports) jump to the library's and our verified ParseHex32; atexit 0x824cbe is a no-op.
//
// WantTuningLoader_Slots: QueryInterface, AddRef, slots 5-11 and Release on a hand-built game loader against ours.
// WantTuningLoader_Collect (0x48de50, 0x48d470): the two collectors on random sets and object lists.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "misc/HexParse.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSWantSimulator.h"
#include "ts/cTSWantSimulatorTuningLoader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace wanttune {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Norm;
using svcfx::Obj;
using Loader = cTSWantSimulatorTuningLoader;

uint32_t Hash(const char *s, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ uint8_t(s[i])) * 16777619u;
    return h;
}
uint32_t Hash(const std::string &s) { return Hash(s.data(), s.size()); }
const char *Text(uint32_t cell) { return *GameAddress<const char *>(cell); }

// Fakes by index.
enum : unsigned {
    kGlobals = 0, kTable = 1, kResources = 2, kRecord = 3, kStream = 4, kFramework = 5, kParser = 6, kDefinition = 7,
    kCareer0 = 8, kCareers = 6, kCareerDef0 = 14, kWant0 = 16, kWants = 6, kOld0 = 22,
};
constexpr uint32_t kResourceCell = 0x10cc2d8; // 0x20deb5's cache
constexpr uint32_t kMarkBuffer = 0xb0f00000, kMarkHandler = 0xb0f10000;

// ---- the plan of one case (both sides run the same) ----
struct Event {
    bool start;
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
};
struct Plan {
    bool recordSet, found, qiOk, qiSetsOut, createOk, createSetsOut;
    uint32_t streamSize;
    std::vector<Event> script;
    int16_t kinds[kCareers];
    std::vector<unsigned> list; // object list entries (career indexes)
    uint32_t definitionKind;
    unsigned blocks;            // want blocks the pool hands out before it fails
    uint32_t oldCount;          // default wants already there (0..2)
};
Plan gPlan;

// ---- curves (cTSPiecewiseFunction stand-ins, built by the constructor stand-in into the 0x24 bytes) ----
struct CurvePoint {
    float x, y;
};
struct FakeCurve {
    void **vt;
    uint32_t id;
    int32_t refs;
};
struct CurveData {
    std::vector<CurvePoint> points;
    FakeCurve *self;
};
std::map<uint32_t, CurveData> gCurves;
uint32_t gNextCurve;
void *gCurveVt[16];
FakeCurve gOldCurve[2];

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
uint32_t __fastcall C_AddRef(FakeCurve *c, void *) { Ev(0x501), Ev(c->id); return uint32_t(++c->refs); }
uint32_t __fastcall C_Release(FakeCurve *c, void *) { Ev(0x502), Ev(c->id); return uint32_t(--c->refs); }
void __fastcall C_Reset(FakeCurve *c, void *) { Ev(0x503), Ev(c->id); gCurves[c->id].points.clear(); }
void __fastcall C_Range(FakeCurve *c, void *, uint32_t range) { Ev(0x504), Ev(c->id), Ev(range); }
void __fastcall C_Add(FakeCurve *c, void *, const CurvePoint *p)
{
    Ev(0x505), Ev(c->id), Ev(Norm(svcfx::P(p))), Ev(Bits(p->x)), Ev(Bits(p->y));
    gCurves[c->id].points.push_back(*p);
}
void __fastcall C_Parse(FakeCurve *c, void *, const char *text)
{
    const uint32_t h = Hash(text, std::strlen(text));
    Ev(0x506), Ev(c->id), Ev(h);
    auto &points = gCurves[c->id].points;
    for (uint32_t i = 0, n = h % 6; i < n; ++i) points.push_back({float(i) - 1.5f, float((h >> (i * 4)) & 0xff) * 0.25f});
}
int32_t __fastcall C_Count(FakeCurve *c, void *) { Ev(0x509), Ev(c->id); return int32_t(gCurves[c->id].points.size()); }
void __fastcall C_Point(FakeCurve *c, void *, CurvePoint *out, int32_t i)
{
    Ev(0x50a), Ev(c->id), Ev(Norm(svcfx::P(out))), Ev(uint32_t(i));
    *out = gCurves[c->id].points.at(size_t(i));
}
void *__fastcall CurveConstruct(void *memory, void *)
{
    auto *c = static_cast<FakeCurve *>(memory);
    c->vt = gCurveVt;
    c->id = 0x300 + gNextCurve++;
    c->refs = 0;
    gCurves[c->id] = {{}, c};
    Ev(0x500), Ev(c->id);
    return c;
}

// ---- want instances ----
unsigned gBlocksGiven;
void *__cdecl WantBlock(uint32_t size)
{
    Ev(0x510), Ev(size);
    if (gBlocksGiven >= gPlan.blocks || gBlocksGiven >= kWants) return nullptr;
    return &F(kWant0 + gBlocksGiven++);
}
WantInstanceParam *__fastcall ParamConstruct(WantInstanceParam *p, void *)
{
    Ev(0x511), Ev(Norm(svcfx::P(p)));
    p->kind = 0xa1a1a1a1;
    p->value = 0xb2b2b2b2;
    p->extra = 0x1313;
    p->pad0a = 0x4545;
    return p;
}
void *__fastcall WantConstruct(void *block, void *, int32_t sim, uint32_t guid, WantInstanceParam *p, void *a, void *b)
{
    Ev(0x512), Ev(Norm(svcfx::P(block))), Ev(uint32_t(sim)), Ev(guid), Ev(p->kind), Ev(p->value), Ev(uint16_t(p->extra)),
        Ev(p->pad0a), Ev(svcfx::P(a)), Ev(svcfx::P(b));
    return block;
}

cRZString *__cdecl SkillName(cRZString *out, uint32_t skill)
{
    Ev(0x520), Ev(skill);
    out->Construct();
    out->mString.assign(1, char('a' + skill % 26));
    return out;
}

long __cdecl LibStrtol(const char *s, char **end, int base) { return std::strtol(s, end, base); }
unsigned long __cdecl LibStrtoul(const char *s, char **end, int base) { return std::strtoul(s, end, base); }
int __cdecl LibAtoi(const char *s) { return std::atoi(s); }
int __cdecl NoAtexit(void (*)()) { return 0; }

// ---- the simulator stand-in ----
void *gSimVt[64];
uint32_t __fastcall S_AddCategoryWant(void *, void *, uint32_t category, uint32_t guid)
{
    Ev(0x530), Ev(category), Ev(guid);
    return 0x5a5a;
}
uint32_t __fastcall S_GetWantDefinition(void *, void *, uint32_t guid)
{
    Ev(0x531), Ev(guid);
    return guid % 3 ? svcfx::P(&F(kDefinition)) : 0;
}

struct SimBuffer {
    alignas(8) uint8_t bytes[sizeof(cTSWantSimulator)];
    cTSWantSimulator *Sim() { return reinterpret_cast<cTSWantSimulator *>(bytes); }
};

cTSWantSimulator *BuildSim(SimBuffer &buffer)
{
    std::memset(buffer.bytes, 0, sizeof buffer.bytes);
    *reinterpret_cast<void ***>(buffer.bytes) = gSimVt;
    cTSWantSimulator *w = buffer.Sim();
    ::new (&w->mCategoryWants) decltype(w->mCategoryWants)();
    ::new (&w->mLevelTables) std::map<int32_t, std::vector<int32_t>>();
    ::new (&w->mMap7c) std::map<int16_t, WantInstancePair>();
    w->mCategoryWants[5];
    w->mCategoryWants[-2];
    w->mLevelTables[7] = {1, 2};
    w->mLevelTables[0] = {9};
    w->mMap7c[3] = {1, 2, 3};
    for (int i = 0; i < 2; ++i) {
        gOldCurve[i] = {gCurveVt, 0x2f0u + i, 5};
        gCurves[gOldCurve[i].id] = {{{1.0f, 2.0f}}, &gOldCurve[i]};
    }
    w->mRef1c.object = &gOldCurve[0];
    w->mRef20.object = &gOldCurve[1];
    for (uint32_t i = 0; i < gPlan.oldCount; ++i) reinterpret_cast<ServiceRefVector *>(&w->mVec68)->PushBack(&F(kOld0 + i));
    return w;
}

void TearDown(cTSWantSimulator *w)
{
    w->mRef1c.object = nullptr;
    w->mRef20.object = nullptr;
    reinterpret_cast<ServiceRefVector *>(&w->mVec68)->ReleaseAndFree();
    std::destroy_at(&w->mCategoryWants);
    std::destroy_at(&w->mLevelTables);
    std::destroy_at(&w->mMap7c);
}

uint32_t CurveWord(void *p)
{
    if (!p) return 0;
    return static_cast<FakeCurve *>(p)->id;
}

void DigestSim(Blob &d, cTSWantSimulator *w)
{
    d.push_back(0x600);
    d.push_back(CurveWord(w->mRef1c.object));
    d.push_back(CurveWord(w->mRef20.object));
    for (auto &[id, data] : gCurves) {
        d.push_back(id);
        d.push_back(uint32_t(data.self->refs));
        d.push_back(uint32_t(data.points.size()));
        for (const CurvePoint &p : data.points) d.push_back(Bits(p.x)), d.push_back(Bits(p.y));
    }
    d.push_back(0x601);
    d.push_back(uint32_t(w->mCategoryWants.size()));
    for (auto &[key, list] : w->mCategoryWants) d.push_back(uint32_t(key)), d.push_back(list.Size());
    d.push_back(0x602);
    d.push_back(w->mVec68.Size());
    for (void **p = w->mVec68.mFirst; p != w->mVec68.mLast; ++p) d.push_back(Norm(svcfx::P(*p)));
    d.push_back(0x603);
    d.push_back(uint32_t(w->mLevelTables.size()));
    for (auto &[key, table] : w->mLevelTables) {
        d.push_back(uint32_t(key));
        d.push_back(uint32_t(table.size()));
        for (int32_t v : table) d.push_back(uint32_t(v));
    }
    d.push_back(0x604);
    d.push_back(uint32_t(w->mMap7c.size()));
    for (auto &[key, e] : w->mMap7c) d.push_back(uint32_t(key)), d.push_back(e.first), d.push_back(e.second), d.push_back(e.third & 0xffff);
}

// The image's std::set<cRZString> at `rva` (head, size), sorted.
void CollectImageSet(const uint8_t *node, const uint8_t *head, std::vector<std::string> &out)
{
    if (node == head || node[0xd]) return;
    CollectImageSet(*reinterpret_cast<const uint8_t *const *>(node), head, out);
    out.push_back(reinterpret_cast<const cRZString *>(node + 0x10)->mString);
    CollectImageSet(*reinterpret_cast<const uint8_t *const *>(node + 8), head, out);
}
void DigestTags(Blob &d, bool original)
{
    for (int which = 0; which < 2; ++which) {
        std::vector<std::string> tags;
        if (original) {
            const uint8_t *head = *GameAddress<const uint8_t *>(which ? 0x10daa88 : 0x10daa7c);
            if (head) CollectImageSet(*reinterpret_cast<const uint8_t *const *>(head + 4), head, tags);
        } else {
            for (const std::string &s : which ? Loader::UnknownEndTags() : Loader::UnknownStartTags()) tags.push_back(s);
        }
        std::sort(tags.begin(), tags.end());
        d.push_back(0x610 + which);
        d.push_back(uint32_t(tags.size()));
        for (const std::string &s : tags) d.push_back(Hash(s));
    }
}

// ---- fakes' handlers ----
void *gHandler;
uint8_t *gBuffer;
struct ListNode {
    ListNode *next, *prev;
    void *object;
};
ListNode gHead, gNodes[16];
ListNode *gHolder;

uint32_t ResourcesFind(Obj *, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    if (gPlan.recordSet) *reinterpret_cast<void **>(uintptr_t(args[1])) = &F(kRecord);
    return gPlan.found;
}
uint32_t RecordQuery(Obj *, uint32_t *args)
{
    if (gPlan.qiOk || gPlan.qiSetsOut) *reinterpret_cast<void **>(uintptr_t(args[1])) = &F(kStream);
    return gPlan.qiOk;
}
uint32_t StreamRead(Obj *, uint32_t *args)
{
    gBuffer = reinterpret_cast<uint8_t *>(uintptr_t(args[0]));
    svcfx::gEv[svcfx::gEv.size() - 2] = kMarkBuffer;
    std::memset(gBuffer, 0x5c, args[1]);
    return 1;
}
uint32_t FrameworkCreate(Obj *, uint32_t *args)
{
    if (gPlan.createOk || gPlan.createSetsOut) *reinterpret_cast<void **>(uintptr_t(args[2])) = &F(kParser);
    return gPlan.createOk;
}
uint32_t ParserSetHandler(Obj *, uint32_t *args)
{
    gHandler = reinterpret_cast<void *>(uintptr_t(args[0]));
    svcfx::gEv.back() = kMarkHandler;
    return 1;
}
uint32_t ParserParse(Obj *, uint32_t *args)
{
    if (reinterpret_cast<uint8_t *>(uintptr_t(args[0])) == gBuffer) svcfx::gEv[svcfx::gEv.size() - 3] = kMarkBuffer;
    for (const Event &e : gPlan.script) {
        Ev(0x540);
        if (e.start) {
            std::vector<const char *> attrs;
            for (auto &[k, v] : e.attrs) attrs.push_back(k.c_str()), attrs.push_back(v.c_str());
            attrs.push_back(nullptr);
            const uint32_t count = uint32_t(attrs.size() - 1);
            Ev(VirtualCall<bool(__thiscall *)(void *, const char *, const char **, uint32_t, uint32_t), 3>(
                gHandler, e.name.c_str(), attrs.data(), count, 0));
        } else {
            Ev(VirtualCall<bool(__thiscall *)(void *, const char *), 4>(gHandler, e.name.c_str()));
        }
    }
    return 1;
}
uint32_t TableFindObject(Obj *, uint32_t *args)
{
    const uint32_t guid = args[0];
    switch (guid % 8) {
    case 6: return 0;
    case 7: return svcfx::P(&F(kDefinition));
    default: return guid % 8 < kCareers ? svcfx::P(&F(kCareer0 + guid % 8)) : 0;
    }
}

void SetupFakes()
{
    svcfx::ResetAll();
    gCurves.clear();
    gNextCurve = 0;
    gBlocksGiven = 0;
    gHandler = nullptr;
    gBuffer = nullptr;
    gCurveVt[1] = reinterpret_cast<void *>(&C_AddRef);
    gCurveVt[2] = reinterpret_cast<void *>(&C_Release);
    gCurveVt[3] = reinterpret_cast<void *>(&C_Reset);
    gCurveVt[4] = reinterpret_cast<void *>(&C_Range);
    gCurveVt[5] = reinterpret_cast<void *>(&C_Add);
    gCurveVt[6] = reinterpret_cast<void *>(&C_Parse);
    gCurveVt[9] = reinterpret_cast<void *>(&C_Count);
    gCurveVt[10] = reinterpret_cast<void *>(&C_Point);
    gSimVt[5] = reinterpret_cast<void *>(&S_AddCategoryWant);
    gSimVt[23] = reinterpret_cast<void *>(&S_GetWantDefinition);

    F(kGlobals).Set(26, 0, &F(kTable));
    F(kTable).Set(31, 0, &gHolder);
    F(kTable).Set(42, "d", 0, &TableFindObject);
    F(kResources).Set(39, "pp-", 0, &ResourcesFind);
    F(kRecord).Set(0, "dp", 0, &RecordQuery);
    F(kRecord).Set(6, 0);
    F(kStream).Set(52, 0, gPlan.streamSize);
    F(kStream).Set(27, "pd", 0, &StreamRead);
    F(kFramework).Set(3, "ddp", 0, &FrameworkCreate);
    F(kParser).Set(8, "p", 0, &ParserSetHandler);
    F(kParser).Set(3, "pdd", 0, &ParserParse);
    F(kDefinition).Set(16, 0, gPlan.definitionKind);
    for (unsigned i = 0; i < kCareers; ++i) {
        F(kCareer0 + i).Set(13, "d", 0x77770000u | uint16_t(gPlan.kinds[i]));
        F(kCareer0 + i).Set(61, 0, &F(kCareerDef0 + i % 2));
    }
    for (unsigned i = 0; i < 2; ++i) {
        F(kCareerDef0 + i).Set(28, 0, 0x2800 + i);
        F(kCareerDef0 + i).Set(49, "p");
    }
    gHead.next = gHead.prev = &gHead;
    gHolder = &gHead;
    for (size_t i = 0; i < gPlan.list.size(); ++i) {
        ListNode &node = gNodes[i];
        node.object = &F(kCareer0 + gPlan.list[i]);
        node.prev = gHead.prev;
        node.next = &gHead;
        gHead.prev->next = &node;
        gHead.prev = &node;
    }
}

// ---- random scripts ----
constexpr uint32_t kElementCells[] = {0x1012d28, 0x1012d2c, 0x1012d30, 0x1012d34, 0x1012d38, 0x1012d3c, 0x1012d40,
                                      0x1012d44, 0x1012d48, 0x1012d4c, 0x1012d50, 0x1012d54, 0x1012d58, 0x1012d5c,
                                      0x1012d60, 0x1012d64, 0x1012d68, 0x1012d6c, 0x1012d70};
constexpr uint32_t kBusy[] = {0x1012d30, 0x1012d34, 0x1012d38, 0x1012d3c, 0x1012d40, 0x1012d40, 0x1012d40, 0x1012d48,
                              0x1012d4c, 0x1012d4c, 0x1012d54, 0x1012d54, 0x1012d5c, 0x1012d5c, 0x1012d6c, 0x1012d6c};
constexpr uint32_t kAttrCells[] = {0x1012d74, 0x1012d78, 0x1012d7c, 0x1012d80, 0x1012d84, 0x1012d88,
                                   0x1012d8c, 0x1012d90, 0x1012d94, 0x1012d98, 0x1012d9c};
constexpr uint32_t kAspirationValues[] = {0x1012da0, 0x1012da4, 0x1012da8, 0x1012dac, 0x1012db0,
                                          0x1012db4, 0x1012db8, 0x1012dbc, 0x1012dc0, 0x1012dc4};
constexpr uint32_t kLevelValues[] = {0x1012dc8, 0x1012dcc, 0x1012dd0, 0x1012dd4, 0x1012dd8, 0x1012ddc};
constexpr uint32_t kThresholdValues[] = {0x1012de0, 0x1012de4, 0x1012de8, 0x1012dec, 0x1012df0, 0x1012df4, 0x1012df8};
const char *const kUnknownTags[] = {"mystery", "zebra", "aardvark", "Influence", ""};

template <size_t N> std::string Pick(std::mt19937 &rng, const uint32_t (&cells)[N]) { return Text(cells[rng() % N]); }
std::string Format(const char *fmt, uint32_t v)
{
    char text[32];
    std::snprintf(text, sizeof text, fmt, v);
    return text;
}

std::string ValueFor(std::mt19937 &rng, uint32_t attrCell)
{
    switch (attrCell) {
    case 0x1012d74: // name
        switch (rng() % 4) {
        case 0: return Pick(rng, kAspirationValues);
        case 1: return Pick(rng, kLevelValues);
        case 2: return Pick(rng, kThresholdValues);
        default: return rng() % 2 ? "bogus" : Text(0x1012dfc);
        }
    case 0x1012d78: // guid
    case 0x1012d98: // object
        return rng() % 8 ? Format("0x%x", rng() % 4 ? rng() % 64 : rng()) : (rng() % 2 ? "0" : "zz");
    case 0x1012d7c: { // score
        static const char *const extremes[] = {"-2147483648", "99999999999", "0x7fffffff", "-327680", "327670", "327671"};
        if (rng() % 5 == 0) return extremes[rng() % 6];
        return Format("%d", uint32_t(int32_t(rng() % 800000) - 400000));
    }
    case 0x1012d80: // id
        return rng() % 3 ? Format("%d", uint32_t(int32_t(rng() % 46) - 5)) : Format("0x%x", rng() % 0x20000);
    case 0x1012d84:
    case 0x1012d88: return Format(rng() % 2 ? "0x%08x" : "%u", rng());
    case 0x1012d8c: return Format("%u", rng() % 4 ? rng() % 70000 : 0);
    case 0x1012d90: {
        switch (rng() % 4) {
        case 0: return Text(0x1012dfc);
        case 1: return Text(0x1012e00);
        case 2: return "Teen";
        default: return Pick(rng, kLevelValues);
        }
    }
    case 0x1012d94: return Format("c%u", rng());
    case 0x1012d9c: return Format("%d", uint32_t(int32_t(rng() % 140000) - 70000));
    default: return "?";
    }
}

Event RandomStart(std::mt19937 &rng, const std::string &name)
{
    Event e{true, name, {}};
    for (uint32_t i = 0, n = rng() % 6; i < n; ++i) {
        if (rng() % 12 == 0) {
            e.attrs.push_back({"junk", "1"});
            continue;
        }
        const uint32_t cell = kAttrCells[rng() % 11];
        e.attrs.push_back({Text(cell), ValueFor(rng, cell)});
    }
    return e;
}

void RandomPlan(std::mt19937 &rng)
{
    Plan &p = gPlan;
    p.found = rng() % 6 != 0;
    p.recordSet = p.found || rng() % 2;
    p.qiOk = rng() % 8 != 0;
    p.qiSetsOut = rng() % 2;
    p.createOk = rng() % 8 != 0;
    p.createSetsOut = rng() % 2;
    p.streamSize = rng() % 4 ? rng() % 200 : 0;
    static const int16_t kinds[] = {0x20, 0x2e, 0x61, 0x20, 0x21, 0x61, -1};
    for (auto &k : p.kinds) k = kinds[rng() % 7];
    p.list.clear();
    for (uint32_t i = 0, n = rng() % 12; i < n; ++i) p.list.push_back(rng() % kCareers);
    p.definitionKind = rng() % 9;
    p.blocks = rng() % 8;
    p.oldCount = rng() % 3;
    // The script: the wantSimulator, an aspiration and a level element first (the loader's aspiration, level key and careers
    // flag are unset until then in the original), then random elements.
    p.script.clear();
    p.script.push_back({true, Text(0x1012d28), {}});
    p.script.push_back({true, Text(0x1012d3c), {{Text(0x1012d74), Pick(rng, kAspirationValues)}}});
    p.script.push_back({true, Text(0x1012d48), {{Text(0x1012d74), Pick(rng, kLevelValues)}}});
    for (uint32_t i = 0, n = rng() % 60; i < n; ++i) {
        std::string name;
        const uint32_t r = rng() % 16;
        if (r < 8) name = Text(kBusy[rng() % 16]);
        else if (r < 15) name = Text(kElementCells[rng() % 19]);
        else name = kUnknownTags[rng() % 5];
        if (rng() % 3 == 0) p.script.push_back({false, name, {}});
        else p.script.push_back(RandomStart(rng, name));
    }
    if (rng() % 2) p.script.push_back({false, Text(0x1012d28), {}});
}

Blob RunLoad(difftest::Context &ctx, bool original)
{
    SetupFakes();
    SimBuffer buffer;
    cTSWantSimulator *w = BuildSim(buffer);
    svcfx::AddRange(buffer.bytes, sizeof buffer.bytes, 0x7e000000);
    if (original) ctx.Original<void(__thiscall *)(void *)>(0x494030)(w);
    else cTSWantSimulatorTuningLoaderAdapters::LoadTuning(w, nullptr);
    Blob d = svcfx::gEv;
    DigestSim(d, w);
    svcfx::CaptureFakes(d);
    DigestTags(d, original);
    TearDown(w);
    return d;
}

} // namespace wanttune

using namespace wanttune;

DIFF_TEST(WantTuningLoader_Load, 0x494030)
{
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(svcfx::kAppCell, &F(kGlobals));
    globals.Set(kResourceCell, &F(kResources));
    svcfx::gFrameworkIndex = kFramework;
    shape::CodePatch curve(0x54b890, reinterpret_cast<const void *>(&CurveConstruct));
    shape::CodePatch block(0x54e570, reinterpret_cast<const void *>(&WantBlock));
    shape::CodePatch param(0x54e500, reinterpret_cast<const void *>(&ParamConstruct));
    shape::CodePatch want(0x54e2b0, reinterpret_cast<const void *>(&WantConstruct));
    shape::CodePatch skill(0x496af0, reinterpret_cast<const void *>(&SkillName));
    shape::CodePatch strtolPatch(0x836d08, reinterpret_cast<const void *>(&LibStrtol));
    shape::CodePatch strtoulPatch(0x836d4e, reinterpret_cast<const void *>(&LibStrtoul));
    shape::CodePatch atoiPatch(0x830972, reinterpret_cast<const void *>(&LibAtoi));
    shape::CodePatch hexPatch(0x1b0e2c, reinterpret_cast<const void *>(&hexparse::ParseHex32));
    shape::CodePatch atexitPatch(0x824cbe, reinterpret_cast<const void *>(&NoAtexit));
    const uint32_t seeds = std::getenv("TUNE_SEEDS") ? uint32_t(std::atoi(std::getenv("TUNE_SEEDS"))) : 1500;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        std::mt19937 rng(seed);
        RandomPlan(rng);
        const Blob a = RunLoad(ctx, true);
        const Blob b = RunLoad(ctx, false);
        if (!svcfx::Same(ctx, a, b, "LoadTuning", seed)) {
            if (std::getenv("TUNE_DUMP")) {
                size_t i = 0;
                while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
                for (size_t j = i > 40 ? i - 40 : 0; j < i + 20; ++j)
                    std::fprintf(stderr, "%4u %08x %08x\n", unsigned(j), j < a.size() ? a[j] : 0, j < b.size() ? b[j] : 0);
            }
            break;
        }
    }
}

namespace wanttune {

// A loader as LoadTuning's original builds it, in memory from the game's operator new.
void *BuildGameLoader(difftest::Context &ctx)
{
    void *memory = ctx.Original<void *(__cdecl *)(size_t)>(0x824632)(0x3c);
    std::memset(memory, 0, 0x3c);
    *static_cast<void **>(memory) = GameAddress<void>(0xdcae44); // the game's loader vtable
    auto *bytes = static_cast<uint8_t *>(memory);
    ::new (bytes + 0x18) std::set<int16_t>();
    for (uint32_t off : {0x20u, 0x28u, 0x30u}) ::new (bytes + off) std::set<void *>();
    return memory;
}

template <int Slot, class... A> uint32_t Slot32(void *object, A... args)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, A...), Slot>(object, args...);
}

Blob RunSlots(void *object, std::mt19937 &rng)
{
    Blob d;
    auto *bytes = static_cast<uint8_t *>(object);
    auto &skills = *reinterpret_cast<std::set<int16_t> *>(bytes + 0x18);
    for (uint32_t i = 0, n = rng() % 5; i < n; ++i) skills.insert(int16_t(rng()));
    for (uint32_t off : {0x20u, 0x28u, 0x30u})
        for (uint32_t i = 0, n = rng() % 4; i < n; ++i) reinterpret_cast<std::set<void *> *>(bytes + off)->insert(&F(i));
    for (uint32_t iid : {1u, 0x4b6b9c9bu, 2u, 0u, 0x4b6b9c9cu, rng()}) {
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        const bool ok = VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(object, iid, &out);
        d.push_back(ok);
        d.push_back(out == object ? 1 : svcfx::P(out));
        d.push_back(*reinterpret_cast<uint32_t *>(bytes + 8));
    }
    d.push_back(Slot32<1>(object));
    d.push_back(Slot32<5>(object, rng(), rng()) & 0xff);
    d.push_back(Slot32<6>(object) & 0xff);
    d.push_back(Slot32<7>(object) & 0xff);
    d.push_back(Slot32<8>(object, rng(), rng()) & 0xff);
    d.push_back(Slot32<9>(object, rng()) & 0xff);
    d.push_back(Slot32<10>(object, rng(), rng()) & 0xff);
    d.push_back(Slot32<11>(object, rng(), rng(), rng(), rng()) & 0xff);
    // Release down to the last reference (the last destroys the object).
    for (int32_t refs = *reinterpret_cast<int32_t *>(bytes + 8); refs >= 1; --refs) d.push_back(Slot32<2>(object));
    return d;
}

} // namespace wanttune

DIFF_TEST(WantTuningLoader_Slots, 0x494830)
{
    svcfx::ResetAll();
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        std::mt19937 rngA(seed), rngB(seed);
        const Blob a = RunSlots(BuildGameLoader(ctx), rngA);
        const Blob b = RunSlots(new Loader, rngB);
        if (!svcfx::Same(ctx, a, b, "Slots", seed)) break;
    }
}

namespace wanttune {

Blob RunCollect(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    Blob d;
    std::set<int16_t> skills;
    for (uint32_t i = 0, n = rng() % 6; i < n; ++i) skills.insert(int16_t(rng() % 24));
    if (original) ctx.Original<void(__cdecl *)(std::set<int16_t> *)>(0x48de50)(&skills);
    else Loader::CollectSkillIds(&skills);
    d.push_back(uint32_t(skills.size()));
    for (int16_t s : skills) d.push_back(uint32_t(s));

    SetupFakes();
    std::set<void *> sets[3];
    for (auto &s : sets)
        for (uint32_t i = 0, n = rng() % 3; i < n; ++i) s.insert(&F(kCareer0 + rng() % kCareers));
    if (original)
        ctx.Original<void(__cdecl *)(std::set<void *> *, std::set<void *> *, std::set<void *> *)>(0x48d470)(&sets[0], &sets[1],
                                                                                                         &sets[2]);
    else Loader::CollectCareerObjects(&sets[0], &sets[1], &sets[2]);
    for (auto &s : sets) {
        d.push_back(uint32_t(s.size()));
        for (void *o : s) d.push_back(Norm(svcfx::P(o)));
    }
    d.insert(d.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    return d;
}

} // namespace wanttune

DIFF_TEST(WantTuningLoader_Collect, 0x48d470)
{
    svcfx::Globals globals;
    globals.Set(svcfx::kAppCell, &F(kGlobals));
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        std::mt19937 planRng(seed);
        RandomPlan(planRng);
        std::mt19937 rngA(seed * 7), rngB(seed * 7);
        const Blob a = RunCollect(ctx, true, rngA);
        const Blob b = RunCollect(ctx, false, rngB);
        if (!svcfx::Same(ctx, a, b, "Collect", seed)) break;
    }
}
