// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 43: "can the object stand where it is"
// (src/simantics/PrimGenericSimCallL.cpp, GenericSimCall::Case43).
//
// The object under test, its attribute interface, the interface's owners, their footprint interfaces and the owner list are
// recording fakes: every slot logs (object, slot, arguments) and returns scripted values, AddRef/Release count per fake. The two
// original callees that need the world (the blocked-position test 0x7e7e00 and the placement worker 0x7e8c60) are redirected to
// logging stand-ins in the image, so the original and our code reach the same stand-ins; the stand-ins log the position bytes and
// the handle they are handed (this is what caught the swapped arguments of the worker call). The original and ours run on
// identical worlds; the log, the result (or the thrown code) and the final reference counts must match.
#include "difftest.h"
#include "game/game.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallMisc.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kOwners = 12;
constexpr int kFootprints = 12;
constexpr int kMaxListed = 8;

struct Footprint {
    void **vt;                // +0x00 primary vtable: 0 blocked, 6, 13 (float), 22
    int32_t *vbptr;           // +0x04 virtual-base table
    int32_t vbtable[2];       // +0x08 {0, displacement of the sub-object from base + 4}
    void **subVt;             // +0x10 the virtual base sub-object: slot 2 releases
    int32_t refs;
    int32_t blocked, slot6, slot22;
    float slot13;
};

struct Owner;

struct Iface {
    void **vt; // slot 1 AddRef, 2 Release, 8 owner, 21 kind, 48 and 49 door-like tests
    int32_t refs;
    Owner *owner;
    int32_t kind;
    uint32_t door48, door49;
};

struct ListObject {
    void **vt; // slot 45
    void *vector[2];
};

struct Owner {
    void **vt; // 9 query, 33 cell, 42 position, 43 turns, 63 (21, 48, 49 are the interface's slots: the owner's are decoys)
    Footprint *footprint; // null: the query fails
    int32_t cell[2];
    float position[3];
    int32_t turns;
    uint32_t unused;
    ListObject *list;
};


struct World {
    PrimGscObject self;
    struct {
        void **vt;
    } context;
    Iface selfIface;
    Iface others[kMaxListed];
    Owner owners[kOwners];
    Footprint footprints[kFootprints];
    ListObject list;
    Iface *listed[kMaxListed + 2];
    int16_t args[8];
    uint32_t argHigh;
    bool giveInterface;
    // stand-in scripts
    uint32_t blockedSeed, blockedPercent, placeRefusePercent;
    uint32_t blockedCalls, placeCalls;
};

World gWorld;
std::vector<uint32_t> gTrace;

void *gVtSelf[32];
void *gVtContext[32];
void *gVtIface[64];
void *gVtOwner[80];
void *gVtList[64];
void *gVtFootprint[32];
void *gVtFootprintSub[4];

uint32_t Norm(const void *p)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    const uint8_t *w = reinterpret_cast<const uint8_t *>(&gWorld);
    if (b >= w && b < w + sizeof(World)) return static_cast<uint32_t>(b - w);
    return p ? 0xfffffff0u : 0u;
}

void Log(const void *self, uint32_t slot, std::initializer_list<uint32_t> args = {})
{
    gTrace.push_back(Norm(self));
    gTrace.push_back(slot | static_cast<uint32_t>(args.size()) << 16);
    for (uint32_t a : args) gTrace.push_back(a);
}

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

// ---- the object under test -----------------------------------------------------------------------------------------

uint32_t __fastcall SelfQuery(void *self, void *, uint32_t iid, void **out)
{
    Log(self, 0, {iid, out ? 1u : 0u});
    if (!gWorld.giveInterface) return 0;
    ++gWorld.selfIface.refs;
    *out = &gWorld.selfIface;
    return 1;
}

uint32_t __fastcall SelfArg(void *self, void *, int32_t index)
{
    Log(self, 19, {static_cast<uint32_t>(index)});
    if (index < 0 || index >= 8) return gWorld.argHigh;
    return (gWorld.argHigh & 0xffff0000u) | static_cast<uint16_t>(gWorld.args[index]);
}

void __fastcall SelfSetArg(void *self, void *, int32_t index, uint32_t value)
{
    Log(self, 20, {static_cast<uint32_t>(index), value});
}

void *__fastcall ContextSlot(void *self, void *)
{
    Log(self, 0x100);
    return &gWorld.context;
}

template <int Slot> void *__fastcall ContextSlotN(void *self, void *)
{
    Log(self, Slot);
    return &gWorld.context;
}

// ---- interfaces ----------------------------------------------------------------------------------------------------------

uint32_t __fastcall IfaceAddRef(Iface *self, void *)
{
    Log(self, 1);
    return static_cast<uint32_t>(++self->refs);
}

uint32_t __fastcall IfaceRelease(Iface *self, void *)
{
    Log(self, 2);
    return static_cast<uint32_t>(--self->refs);
}

int32_t __fastcall IfaceKind(Iface *self, void *)
{
    Log(self, 21);
    return self->kind;
}

uint32_t __fastcall IfaceDoor48(Iface *self, void *)
{
    Log(self, 48);
    return self->door48;
}

uint32_t __fastcall IfaceDoor49(Iface *self, void *)
{
    Log(self, 49);
    return self->door49;
}

void *__fastcall IfaceOwner(Iface *self, void *)
{
    Log(self, 8);
    return self->owner;
}

// ---- owners ----------------------------------------------------------------------------------------------------------------

uint32_t __fastcall OwnerQuery(Owner *self, void *, uint32_t a, uint32_t b, void **out)
{
    Log(self, 9, {a, b, out ? 1u : 0u});
    if (!self->footprint) return 0xabcd0000u;
    ++self->footprint->refs;
    *out = self->footprint;
    return 0xabcd0001u;
}

int32_t __fastcall OwnerKind(Owner *self, void *)
{
    Log(self, 21);
    return 8; // decoy: the kind is asked of the interface itself
}

const int32_t *__fastcall OwnerCell(Owner *self, void *)
{
    Log(self, 33);
    return self->cell;
}

float *__fastcall OwnerPosition(Owner *self, void *, float *out)
{
    Log(self, 42, {out ? 1u : 0u});
    std::memcpy(out, self->position, sizeof(self->position));
    return out;
}

int32_t __fastcall OwnerTurns(Owner *self, void *)
{
    Log(self, 43);
    return self->turns;
}

uint32_t __fastcall OwnerDoor48(Owner *self, void *)
{
    Log(self, 48);
    return 1; // decoy
}

uint32_t __fastcall OwnerDoor49(Owner *self, void *)
{
    Log(self, 49);
    return 1; // decoy
}

void *__fastcall OwnerList(Owner *self, void *)
{
    Log(self, 63);
    return self->list;
}

void *__fastcall ListVector(ListObject *self, void *)
{
    Log(self, 45);
    return self->vector;
}

// ---- footprints --------------------------------------------------------------------------------------------------------------

uint32_t __fastcall FpBlocked(Footprint *self, void *)
{
    Log(self, 0);
    return static_cast<uint32_t>(self->blocked);
}

uint32_t __fastcall FpSlot6(Footprint *self, void *)
{
    Log(self, 6);
    return static_cast<uint32_t>(self->slot6);
}

float __fastcall FpSlot13(Footprint *self, void *)
{
    Log(self, 13);
    return self->slot13;
}

uint32_t __fastcall FpSlot22(Footprint *self, void *)
{
    Log(self, 22);
    return static_cast<uint32_t>(self->slot22);
}

Footprint *FootprintOfSub(void *sub)
{
    return reinterpret_cast<Footprint *>(static_cast<uint8_t *>(sub) - offsetof(Footprint, subVt));
}

uint32_t __fastcall FpSubAddRef(void *sub, void *)
{
    Footprint *fp = FootprintOfSub(sub);
    Log(fp, 0x201);
    return static_cast<uint32_t>(++fp->refs);
}

uint32_t __fastcall FpSubRelease(void *sub, void *)
{
    Footprint *fp = FootprintOfSub(sub);
    Log(fp, 0x202);
    return static_cast<uint32_t>(--fp->refs);
}

// ---- stand-ins for the two world-dependent original callees ------------------------------------------------------------------

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 13;
    return h;
}

// 0x7e7e00 (cdecl, result in al): is the position blocked within the radius.
uint32_t __cdecl StubPositionBlocked(const float *position, float radius, int32_t a, int32_t b)
{
    gTrace.push_back(0x7e7e00u);
    gTrace.insert(gTrace.end(), {Bits(position[0]), Bits(position[1]), Bits(position[2]), Bits(radius), static_cast<uint32_t>(a), static_cast<uint32_t>(b)});
    ++gWorld.blockedCalls;
    return Mix(gWorld.blockedSeed, gWorld.blockedCalls) % 100 < gWorld.blockedPercent ? 0x1u : 0xaabbcc00u;
}

// 0x7e8c60 (cdecl, seven stack arguments, result in al): the placement worker. The original passes the object, the position
// (3 floats) and the address of the direction handle.
uint32_t __cdecl StubPlace(void *object, const uint32_t *second, const int32_t *third, float f1, float f2, int32_t mode, int32_t last)
{
    gTrace.push_back(0x7e8c60u);
    gTrace.insert(gTrace.end(), {Norm(object), second[0], second[1], second[2], static_cast<uint32_t>(*third), Bits(f1), Bits(f2),
                                 static_cast<uint32_t>(mode), static_cast<uint32_t>(last)});
    ++gWorld.placeCalls;
    return Mix(gWorld.blockedSeed + 1, gWorld.placeCalls) % 100 < gWorld.placeRefusePercent ? 0x0u : 0xddee0001u;
}

struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(difftest::Context &ctx, uint32_t rva, void *stand) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

void InitVtables()
{
    gVtSelf[0] = reinterpret_cast<void *>(&SelfQuery);
    gVtSelf[19] = reinterpret_cast<void *>(&SelfArg);
    gVtSelf[20] = reinterpret_cast<void *>(&SelfSetArg);
    gVtContext[9] = reinterpret_cast<void *>(&ContextSlotN<9>);
    gVtContext[10] = reinterpret_cast<void *>(&ContextSlotN<10>);
    gVtContext[26] = reinterpret_cast<void *>(&ContextSlotN<26>);
    gVtContext[24] = reinterpret_cast<void *>(&ContextSlotN<24>);
    gVtIface[1] = reinterpret_cast<void *>(&IfaceAddRef);
    gVtIface[2] = reinterpret_cast<void *>(&IfaceRelease);
    gVtIface[8] = reinterpret_cast<void *>(&IfaceOwner);
    gVtIface[21] = reinterpret_cast<void *>(&IfaceKind);
    gVtIface[48] = reinterpret_cast<void *>(&IfaceDoor48);
    gVtIface[49] = reinterpret_cast<void *>(&IfaceDoor49);
    gVtOwner[9] = reinterpret_cast<void *>(&OwnerQuery);
    gVtOwner[21] = reinterpret_cast<void *>(&OwnerKind);
    gVtOwner[33] = reinterpret_cast<void *>(&OwnerCell);
    gVtOwner[42] = reinterpret_cast<void *>(&OwnerPosition);
    gVtOwner[43] = reinterpret_cast<void *>(&OwnerTurns);
    gVtOwner[48] = reinterpret_cast<void *>(&OwnerDoor48);
    gVtOwner[49] = reinterpret_cast<void *>(&OwnerDoor49);
    gVtOwner[63] = reinterpret_cast<void *>(&OwnerList);
    gVtList[45] = reinterpret_cast<void *>(&ListVector);
    gVtFootprint[0] = reinterpret_cast<void *>(&FpBlocked);
    gVtFootprint[6] = reinterpret_cast<void *>(&FpSlot6);
    gVtFootprint[13] = reinterpret_cast<void *>(&FpSlot13);
    gVtFootprint[22] = reinterpret_cast<void *>(&FpSlot22);
    gVtFootprintSub[1] = reinterpret_cast<void *>(&FpSubAddRef);
    gVtFootprintSub[2] = reinterpret_cast<void *>(&FpSubRelease);
}

// ---- worlds --------------------------------------------------------------------------------------------------------------------

struct Constants {
    float footprintMinimum, nearLimit, scale, half;
    std::vector<std::pair<int32_t, int32_t>> exactPairs; // cell offsets whose distance rounds to exactly the limit
};

float PickFloat(std::mt19937 &r)
{
    switch (r() % 10) {
    case 0: return 0.0f;
    case 1: return -2.5f;
    case 2: return 1000.0f;
    case 3: return static_cast<float>(static_cast<int>(r() % 4001) - 2000) / 16.0f;
    default: return static_cast<float>(static_cast<int>(r() % 400001) - 200000) / 1000.0f;
    }
}

int32_t PickTurns(std::mt19937 &r)
{
    const int32_t e[] = {0, 1, 2, 3, 4, -1, -3, 7, 8, 100, 360, 12345};
    return r() % 4 == 0 ? static_cast<int32_t>(r() % 2000) - 1000 : e[r() % 12];
}

int32_t PickKind(std::mt19937 &r)
{
    const int32_t e[] = {0, 1, 2, 2, 4, 4, 8, 8, 3, 5, 16, -1};
    return e[r() % 12];
}

// A cell pair for an object at distance about `fraction` of the threshold from the origin cell.
void PickCell(std::mt19937 &r, const Constants &c, int32_t *cell, const int32_t *origin)
{
    const float steps[] = {0.0f, 0.25f, 0.5f, 0.9f, 0.99f, 1.0f, 1.01f, 1.5f, 3.0f};
    const float f = steps[r() % 9];
    const float units = c.nearLimit / c.scale * f;
    const int32_t dx = static_cast<int32_t>(units * ((r() % 3) / 2.0f)) * (r() % 2 ? 1 : -1);
    const int32_t dy = static_cast<int32_t>(units * ((r() % 3) / 2.0f)) * (r() % 2 ? 1 : -1);
    cell[0] = origin[0] + dx;
    cell[1] = origin[1] + dy;
    if (!c.exactPairs.empty() && r() % 5 == 0) {
        const auto &pair = c.exactPairs[r() % c.exactPairs.size()];
        cell[0] = origin[0] + (r() % 2 ? pair.first : -pair.first);
        cell[1] = origin[1] + (r() % 2 ? pair.second : -pair.second);
    } else if (r() % 4 == 0) {
        // Right at the limit along one axis (several neighbouring cell counts give the same rounded distance).
        const int32_t edge = static_cast<int32_t>(c.nearLimit / c.scale) + static_cast<int32_t>(r() % 5) - 2;
        const int32_t signedEdge = r() % 2 ? edge : -edge;
        const int axis = static_cast<int>(r() % 2);
        cell[axis] = origin[axis] + signedEdge;
        cell[1 - axis] = origin[1 - axis];
    }
}

void FillFootprint(std::mt19937 &r, const Constants &c, Footprint &fp)
{
    fp.vt = gVtFootprint;
    fp.vbtable[0] = 0;
    fp.vbtable[1] = static_cast<int32_t>(offsetof(Footprint, subVt)) - 4;
    fp.vbptr = fp.vbtable;
    fp.subVt = nullptr; // set below: the sub-object's first word is its vtable pointer
    fp.refs = 1;
    fp.blocked = r() % 5 == 0 ? static_cast<int32_t>(0x100 | (r() % 3)) : 0;
    if (r() % 11 == 0) fp.blocked = 0x100; // the low byte is 0: not blocked, whatever the other bytes hold
    fp.slot6 = r() % 4 == 0 ? static_cast<int32_t>(r() % 3) : 0;
    fp.slot22 = r() % 4 == 0 ? static_cast<int32_t>(r() % 3) : 0;
    const float e[] = {0.0f, 1.0f, c.footprintMinimum, c.footprintMinimum - 0.001f, c.footprintMinimum + 0.001f, 2.0f, 10.0f, -1.0f};
    fp.slot13 = r() % 6 == 0 ? PickFloat(r) : e[r() % 8];
}

void BuildWorld(uint32_t seed, const Constants &c)
{
    std::mt19937 r(seed);
    World &w = gWorld;
    std::memset(&w, 0, sizeof(w));
    w.self.mVTable = gVtSelf;
    w.self.mSecondary = gVtSelf;
    w.self.mContext = &w.context;
    w.context.vt = gVtContext;
    w.selfIface.vt = gVtIface;
    w.selfIface.refs = 1;
    w.giveInterface = r() % 12 != 0;
    for (int i = 0; i < 8; ++i) w.args[i] = static_cast<int16_t>(r());
    w.args[0] = static_cast<int16_t>(r() % 3 == 0 ? 1 : (r() % 2 ? 0 : static_cast<int>(r() % 4) + 1));
    w.argHigh = r() % 3 == 0 ? (r() & 0xffff0000u) : 0u;
    w.blockedSeed = r();
    const uint32_t percents[] = {0, 10, 30, 60, 100};
    w.blockedPercent = percents[r() % 5];
    w.placeRefusePercent = percents[r() % 5];

    for (int i = 0; i < kFootprints; ++i) {
        FillFootprint(r, c, w.footprints[i]);
        // The virtual base sub-object: slot at base + 4 + displacement holds the vtable pointer.
        w.footprints[i].subVt = gVtFootprintSub;
    }
    w.list.vt = gVtList;
    for (int i = 0; i < kOwners; ++i) {
        Owner &o = w.owners[i];
        o.vt = gVtOwner;
        o.footprint = r() % 9 == 0 ? nullptr : &w.footprints[i];
        for (float &p : o.position) p = PickFloat(r);
        o.turns = PickTurns(r);
        o.list = &w.list;
    }
    // Owner 0 belongs to the object under test; the others to the listed objects (owner i + 1 for others[i]).
    w.owners[0].cell[0] = static_cast<int32_t>(r() % 400) - 200;
    w.owners[0].cell[1] = static_cast<int32_t>(r() % 400) - 200;
    w.selfIface.owner = &w.owners[0];
    w.selfIface.kind = PickKind(r);
    w.selfIface.door48 = r() % 3 == 0 ? (0x100 | (r() % 2)) : (r() % 2);
    w.selfIface.door49 = r() % 3 == 0 ? (0x100 | (r() % 2)) : (r() % 2);
    const int count = static_cast<int>(r() % 7); // 0..6 others in the list
    int used = 0;
    for (int i = 0; i < kMaxListed - 1; ++i) {
        Iface &o = w.others[i];
        o.vt = gVtIface;
        o.refs = 1;
        o.owner = &w.owners[1 + i];
        PickCell(r, c, o.owner->cell, w.owners[0].cell);
    }
    for (int i = 0; i < count; ++i) {
        // Sometimes the object under test itself is listed (it must be skipped).
        w.listed[used++] = r() % 5 == 0 ? &w.selfIface : &w.others[i];
    }
    w.list.vector[0] = w.listed;
    w.list.vector[1] = w.listed + used;
    if (r() % 20 == 0) w.list.vector[1] = w.list.vector[0]; // an empty range
}

struct Outcome {
    uint32_t ret = 0;
    int thrown = 0;
    std::vector<uint32_t> trace;
    std::vector<int32_t> refs;
};

// A crash inside either side is reported with the last log entries instead of ending the run.
const char *gSide = "";
uintptr_t gImageBase;
DWORD gCrashCode;
uintptr_t gCrashAddress, gCrashAccess;
int CrashFilter(EXCEPTION_POINTERS *info)
{
    if (info->ExceptionRecord->ExceptionCode == 0xE06D7363u) return EXCEPTION_CONTINUE_SEARCH; // C++ exceptions pass
    gCrashCode = info->ExceptionRecord->ExceptionCode;
    gCrashAddress = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    gCrashAccess = *reinterpret_cast<uintptr_t *>(info->ContextRecord->Esp) - gImageBase; // the return address when a null function was called
    return EXCEPTION_EXECUTE_HANDLER;
}

template <typename Call> uint32_t Guarded(Call &call, const PrimParamView *param)
{
    __try {
        return call(param);
    } __except (CrashFilter(GetExceptionInformation())) {
        std::printf("      CRASH (%s) %lx at module+%zx accessing %zx after %zu log words, last:", gSide, gCrashCode, gCrashAddress, gCrashAccess, gTrace.size());
        for (size_t i = gTrace.size() > 8 ? gTrace.size() - 8 : 0; i < gTrace.size(); ++i) std::printf(" %x", gTrace[i]);
        std::printf("\n");
        return 0xdead;
    }
}

template <typename Call> Outcome Run(Call call)
{
    gTrace.clear();
    gWorld.blockedCalls = 0;
    gWorld.placeCalls = 0;
    PrimParamView param;
    std::memset(&param, 0, sizeof(param));
    param.mWord4 = 43;
    Outcome o;
    try {
        o.ret = Guarded(call, &param);
    } catch (short code) {
        o.thrown = code;
    }
    o.trace = gTrace;
    o.refs.push_back(gWorld.selfIface.refs);
    for (const Iface &i : gWorld.others) o.refs.push_back(i.refs);
    for (const Footprint &f : gWorld.footprints) o.refs.push_back(f.refs);
    return o;
}

bool Equal(const Outcome &a, const Outcome &b, std::string &why)
{
    char buf[200];
    if (a.thrown != b.thrown) {
        std::snprintf(buf, sizeof(buf), "thrown %d vs %d", a.thrown, b.thrown);
        why = buf;
        return false;
    }
    if (!a.thrown && a.ret != b.ret) {
        std::snprintf(buf, sizeof(buf), "result %u vs %u", a.ret, b.ret);
        why = buf;
        return false;
    }
    const size_t n = (std::min)(a.trace.size(), b.trace.size());
    for (size_t i = 0; i < n; ++i) {
        if (a.trace[i] != b.trace[i]) {
            std::snprintf(buf, sizeof(buf), "trace entry %zu: 0x%x vs 0x%x, next words 0x%x vs 0x%x (lengths %zu, %zu)", i, a.trace[i], b.trace[i], i + 1 < n ? a.trace[i + 1] : 0u, i + 1 < n ? b.trace[i + 1] : 0u, a.trace.size(), b.trace.size());
            why = buf;
            return false;
        }
    }
    if (a.trace.size() != b.trace.size()) {
        std::snprintf(buf, sizeof(buf), "trace length %zu vs %zu", a.trace.size(), b.trace.size());
        why = buf;
        return false;
    }
    if (a.refs != b.refs) {
        for (size_t i = 0; i < a.refs.size(); ++i) {
            if (a.refs[i] != b.refs[i]) {
                std::snprintf(buf, sizeof(buf), "reference count %zu: %d vs %d", i, a.refs[i], b.refs[i]);
                why = buf;
                return false;
            }
        }
    }
    return true;
}

} // namespace

DIFF_TEST(simantics_gsc_case43, 0x58bc50)
{
    InitVtables();
    gImageBase = reinterpret_cast<uintptr_t>(ctx.image.At<uint8_t>(0));
    CodePatch patchBlocked(ctx, 0x7e7e00, reinterpret_cast<void *>(&StubPositionBlocked));
    CodePatch patchPlace(ctx, 0x7e8c60, reinterpret_cast<void *>(&StubPlace));
    using Fn = uint32_t(__thiscall *)(void *, void *, const void *);
    const Fn original = ctx.Original<Fn>(0x58bc50);

    Constants c;
    c.footprintMinimum = *ctx.image.At<float>(0xdb32cc);
    c.nearLimit = *ctx.image.At<float>(0xdaf06c);
    c.scale = *ctx.image.At<float>(0xdc87b8);
    c.half = *ctx.image.At<float>(0xd9c694);

    // Cell offsets whose rounded distance equals the limit exactly (so ">" and ">=" differ), found with the verified helpers.
    const int32_t origin[2] = {0, 0};
    for (int32_t dx = 0; dx <= static_cast<int32_t>(c.nearLimit / c.scale) + 2; ++dx) {
        const double rest = static_cast<double>(c.nearLimit) * c.nearLimit - static_cast<double>(dx) * c.scale * dx * c.scale;
        const int32_t centre = rest > 0 ? static_cast<int32_t>(std::sqrt(rest) / c.scale) : 0;
        for (int32_t dy = centre - 2; dy <= centre + 2; ++dy) {
            if (dy < 0) continue;
            const int32_t cell[2] = {dx, dy};
            if (gscmisc::FloatSqrt(gscmisc::ScaledDistanceSquared(origin, cell, c.scale)) == c.nearLimit) c.exactPairs.emplace_back(dx, dy);
        }
    }
    std::printf("      %zu cell offsets sit exactly at the distance limit\n", c.exactPairs.size());
    std::printf("      constants: footprint minimum %g, distance limit %g, cell scale %g\n", c.footprintMinimum, c.nearLimit, c.scale);
    uint32_t returns[2] = {}, throws = 0, placeCalls = 0, blockedCalls = 0, skippedSelf = 0, tooClose = 0;
    for (uint32_t seed = 1; seed <= 6000; ++seed) {
        ++ctx.cases;
        BuildWorld(seed, c);
        gSide = "original";
        const Outcome a = Run([&](const PrimParamView *p) { return original(&gWorld.self, nullptr, p); });
        BuildWorld(seed, c);
        gSide = "ours";
        const Outcome b = Run([&](const PrimParamView *p) { return gWorld.self.GenericSimCall(nullptr, p); });
        std::string why;
        if (!Equal(a, b, why)) {
            ctx.Fail("seed %u: %s", seed, why.c_str());
            continue;
        }
        if (a.thrown) {
            ++throws;
        } else {
            ++returns[a.ret ? 1 : 0];
        }
        for (size_t i = 0; i + 1 < a.trace.size(); ++i) {
            if (a.trace[i] == 0x7e8c60u) ++placeCalls;
            if (a.trace[i] == 0x7e7e00u) ++blockedCalls;
        }
        (void)skippedSelf;
        (void)tooClose;
    }
    std::printf("      case 43: %u returned 0, %u returned 1, %u threw; placement worker reached %u times, blocked test %u times\n", returns[0],
                returns[1], throws, placeCalls, blockedCalls);
    if (!returns[0] || !returns[1] || !throws || !placeCalls || !blockedCalls) ctx.Fail("the worlds did not reach every outcome");
}
