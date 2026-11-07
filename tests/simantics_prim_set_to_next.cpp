// Differential test for the SimAntics Set To Next primitive (opcode 0x1f, handler 0x5a12c0).
//
// The handler reaches the world only through virtual calls, so each test world is a set of fake objects
// (the "environment", the list manager A, the lookups B, C, D, candidate objects, entries, records, a
// path-walker interface, ...) whose vtable slots are recorders: they log the object, the slot and every
// argument, and serve scripted data (ids, flags, attributes, lists). The original and ours run on two
// identical worlds built from one seed; the results, the thrown code, the whole call trace, the
// destination, the fields written to the object and the reference counts must match.
//
// Not covered (their original callees reach game singletons or the game's allocator, which the harness
// has no state for): modes 8 (Edith tile iterator), 9 and 0x22 (tile neighbours). The rest of the 0x00..0x33 modes
// are exercised, including the unknown-mode throw; 0x16 and 0x17 (DT) through E slots 6 and 5, whose fakes fill the
// id vector the handler then frees.
#include "difftest.h"
#include "simantics/PrimSetToNext.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "ts_services_fixture.h"       // AllocatorScope

#include <algorithm>
#include <cstring>
#include <map>
#include <random>
#include <vector>

namespace {

using std::int16_t;
using std::int32_t;
using std::uint16_t;
using std::uint32_t;
using std::uint8_t;

constexpr int kSlots = 192;
constexpr int NO = 6;   // candidate objects
constexpr int NE = 5;   // entries of the list manager
constexpr int NI = 4;   // interface objects
constexpr int NC = 4;   // container elements
constexpr int NV = 6;   // elements of a container vector
constexpr int NL = 6;   // list entries

struct Dest {
    uint32_t u0;
    int16_t *target;
    int16_t mn, mx;
};

struct Rec {
    void **vt;
    int32_t val[0x100];
};
struct Def {
    void **vt;
    Rec *rec;
    int32_t v70;
};
struct Y {
    void **vt;
    int32_t has[8];
    int32_t val[8];
};
struct R {
    void **vt;
    int32_t cnt[8];
    int32_t v24[8];
    int32_t v30;
    int32_t v34[8];
};
struct ListEnt {
    void **vt;
    int32_t ids[NL];
    int32_t count;
};
struct EObj {
    void **vt;
    int32_t ok[4];
    uint32_t out[4];
    uint16_t ids[2][10]; // slots 5 and 6 (modes 0x17, 0x16): the id vector they fill
    uint32_t idN[2], idCap[2];
};
struct Obj {
    void **vt;
    uint16_t id;
    uint32_t flags;
    int32_t attr[0x100];
    int32_t s98;
    int32_t s131;
    int32_t e4;
    int32_t v70;
    bool b1a0, b2f0;
    Rec *rec;
    Def *def;
    R *r;
    void *qi; // an Iface, or null
    bool qiOk;
    int refs;
};
struct Entry {
    void **vt;
    Obj *obj;
    Def *x;
    Y *y;
    int32_t ref;
    int32_t id20;
    int32_t st[0x100];
};
struct Cont {
    void **vt;
    Obj *obj;
    int32_t st[4];
};
struct Iface {
    void **vt;
    int refs;
    Iface *i1c;
    Iface *i18;
    Obj *i2c;
    Obj *i20;
    int32_t v28[2];
    int16_t seq[8];
    int seqN, seqPos;
    float fa, fb;
    bool okAfter;
};
struct Env {
    void **vt;
};
struct SubEnv {
    void **vt;
};
struct PtrVecRaw {
    void **first;
    void **last;
};
struct ListNode {
    ListNode *next, *prev;
    void *value;
};
struct ListObjRaw {
    ListNode *head;
};
struct Owner {
    void **vt;
    ListObjRaw list;
};

struct World {
    alignas(16) SetToNextObject self;
    void *vtSelf[kSlots], *vtSub[kSlots], *vtEnv[kSlots], *vtX[kSlots], *vtY2[kSlots], *vtWorld[kSlots],
        *vtFrame[kSlots], *vtA[kSlots], *vtB[kSlots], *vtC[kSlots], *vtD[kSlots], *vtObj[kSlots], *vtEntry[kSlots],
        *vtDef[kSlots], *vtRec[kSlots], *vtCont[kSlots], *vtIface[kSlots], *vtYr[kSlots], *vtL[kSlots], *vtE[kSlots],
        *vtR[kSlots], *vtOwner[kSlots];
    void *subPtr;   // sub-object vtable holder is self.mTreeSimVTable
    void *envVT;    // Env object: just a vtable pointer
    void *xVT, *y2VT, *worldVT, *frameVT, *aVT, *bVT, *cVT, *dVT;
    Owner owner;
    Obj objs[NO];
    Entry ents[NE];
    Cont conts[NC];
    Iface ifaces[NI];
    Def defs[NO + 1];
    Rec recs[NO + 2];
    Y ys[NE];
    R rs[NO];
    ListEnt lists[2];
    EObj eobjs[NE];
    PtrVecRaw vecs[4];
    void *vecElems[4][NV];
    ListNode nodes[NL + 1];
    int16_t destSlot;
    uint16_t canary;
    int worldRefs;
};

// Script of this seed (identical for both worlds).
struct Script {
    bool hasTarget = true;
    int16_t mn = -32768, mx = 32767;
    uint32_t unknown0 = 0;
    uint32_t selfParam[4] = {0, 0, 0, 0};
    uint32_t frame30 = 0, frame78 = 0, frame7c[4] = {0, 0, 0, 0};
    uint32_t frameCount = 0;
    bool selfQiOk = true;
    bool owner2c0 = false;
    int nextJump = 0;
    bool entNull[NE + 2] = {};
    uint32_t aSeed = 0;
    int32_t bCount = 3;
    int vecSel = 0;
    uint32_t objLookupMask = 0; // bit i: C->vf29 ignores object i
    uint32_t refMap[8] = {};
    uint32_t keyMask = 0;
    bool listNull[NL] = {};
    int worldPick = 0;
    int dLookupNull = 0;
    int kLookupNull = 0;
    bool bZeroEntry = false;
};
Script gS;
World *gW = nullptr;
std::vector<uint32_t> gTrace;
uint32_t gCalls = 0;

uint32_t Mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
uint32_t Rel(const void *p)
{
    if (!p) return 0;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gW));
}
void Log(const void *self, uint32_t off, std::initializer_list<uint32_t> args)
{
    ++gCalls;
    if (gTrace.size() > 200000) throw 12345; // a runaway loop: stop both sides at the same point
    gTrace.push_back(Rel(self));
    gTrace.push_back(off);
    for (uint32_t a : args) gTrace.push_back(a);
    gTrace.push_back(0xfeedu);
}
uint32_t JunkHi(uint32_t v16) // 16-bit result with junk above (the original reads ax)
{
    return (Mix(gCalls * 977u + 13u) & 0xffff0000u) | (v16 & 0xffffu);
}
uint32_t JunkBool(bool b) // al = 0/1 with junk above
{
    return (Mix(gCalls * 131u + 7u) & 0xffffff00u) | (b ? 1u : 0u);
}

// ---------------------------------------------------------------------------------- slot fakes

// Self
uint32_t __fastcall SelfQI(void *self, void *, uint32_t iid, void **out)
{
    Log(self, 0, {iid, out ? 1u : 0u});
    if (!gS.selfQiOk) return JunkBool(false);
    // self's QI hands out interface 0
    if (out) {
        Iface *f = &gW->ifaces[0];
        ++f->refs;
        *out = f;
    }
    return JunkBool(true);
}
uint32_t __fastcall SelfS4c(void *self, void *, uint32_t n)
{
    Log(self, 0x4c, {n});
    return JunkHi(gS.selfParam[n & 3]);
}
void __fastcall SelfS50(void *self, void *, uint32_t which, int32_t v)
{
    Log(self, 0x50, {which, static_cast<uint32_t>(v)});
}
void *__fastcall SelfSfc(void *self, void *)
{
    Log(self, 0xfc, {});
    return &gW->worldVT;
}
uint32_t __fastcall SelfS2c0(void *self, void *, void *o)
{
    Log(self, 0x2c0, {Rel(o)});
    return JunkBool(gS.owner2c0);
}
// The operand reader (slot 7 of the sub vtable)
uint32_t __fastcall Read7(void *self, void *, uint32_t scope, uint32_t id, Dest *dest, float **fd, uint32_t a5, uint32_t a6, uint32_t a7)
{
    Log(self, 0x1c, {scope, id, dest ? 1u : 0u, fd ? 1u : 0u, a5, a6, a7});
    if (dest) {
        dest->u0 = gS.unknown0;
        dest->target = gS.hasTarget ? &gW->destSlot : nullptr;
        dest->mn = gS.mn;
        dest->mx = gS.mx;
    }
    return 0x12340000u;
}

// Environment
void *__fastcall Env18(void *self, void *)
{
    Log(self, 0x18, {});
    return &gW->xVT;
}
void *__fastcall Env28(void *self, void *)
{
    Log(self, 0x28, {});
    return &gW->aVT;
}
void *__fastcall Env60(void *self, void *)
{
    Log(self, 0x60, {});
    return &gW->bVT;
}
void *__fastcall Env68(void *self, void *)
{
    Log(self, 0x68, {});
    return &gW->cVT;
}
void *__fastcall Env74(void *self, void *)
{
    Log(self, 0x74, {});
    return &gW->dVT;
}
void *__fastcall X34(void *self, void *)
{
    Log(self, 0x34, {});
    return &gW->y2VT;
}
uint32_t __fastcall Y254(void *self, void *, int32_t v)
{
    Log(self, 0x54, {static_cast<uint32_t>(v)});
    return JunkHi(Mix(static_cast<uint32_t>(v) * 3u + 5u) % 5u);
}

Obj *FindObj(int16_t id)
{
    for (int i = 0; i < NO; ++i)
        if (!(gS.objLookupMask & (1u << i)) && static_cast<int16_t>(gW->objs[i].id) == id) return &gW->objs[i];
    return nullptr;
}
// World (self slot 63's result): lookup by id, containers.
void *__fastcall World74(void *self, void *, int32_t id)
{
    Log(self, 0x74, {static_cast<uint32_t>(id)});
    return FindObj(static_cast<int16_t>(id));
}
uint32_t __fastcall WorldAddRef(void *self, void *)
{
    Log(self, 4, {});
    return static_cast<uint32_t>(++gW->worldRefs);
}
uint32_t __fastcall WorldRelease(void *self, void *)
{
    Log(self, 8, {});
    return static_cast<uint32_t>(--gW->worldRefs);
}
void *VecFor(int sel)
{
    return &gW->vecs[sel & 3];
}
void *__fastcall World88(void *self, void *, void *so)
{
    Log(self, 0x88, {Rel(so)});
    return VecFor((gS.vecSel + 1) % 3);
}
void *__fastcall WorldB4(void *self, void *)
{
    Log(self, 0xb4, {});
    return &gW->vecs[3]; // vector of containers
}
void *__fastcall WorldBC(void *self, void *)
{
    Log(self, 0xbc, {});
    return VecFor(gS.vecSel);
}

// Frame
uint32_t __fastcall Frame30(void *self, void *)
{
    Log(self, 0x30, {});
    return JunkHi(gS.frame30);
}
uint32_t __fastcall Frame6c(void *self, void *)
{
    Log(self, 0x6c, {});
    return (Mix(gCalls) & 0xffffff00u) | gS.frameCount;
}
uint32_t __fastcall Frame78(void *self, void *, uint32_t a)
{
    Log(self, 0x78, {a});
    return JunkHi(gS.frame78);
}
uint32_t __fastcall Frame7c(void *self, void *, uint32_t a)
{
    Log(self, 0x7c, {a});
    return JunkHi(gS.frame7c[a & 3]);
}

// A: the list manager
uint32_t __fastcall A3c(void *self, void *, int32_t idx, int32_t flag)
{
    Log(self, 0x3c, {static_cast<uint32_t>(idx), static_cast<uint32_t>(flag)});
    if (idx < 0 || idx >= NE) return JunkHi(0);
    uint32_t n = static_cast<uint32_t>(idx) + 1 + (Mix(gS.aSeed + idx) % 4 == 0 ? 1 : 0);
    if (n > NE) n = 0;
    return JunkHi(n);
}
void *__fastcall A40(void *self, void *, int32_t idx, int32_t flag)
{
    Log(self, 0x40, {static_cast<uint32_t>(idx), static_cast<uint32_t>(flag)});
    if (idx < 1 || idx > NE) return nullptr;
    if (gS.entNull[idx]) return nullptr;
    return &gW->ents[idx - 1];
}

// B
int32_t __fastcall B28(void *self, void *)
{
    Log(self, 0x28, {});
    return gS.bCount;
}
void *__fastcall B34(void *self, void *, int32_t i)
{
    Log(self, 0x34, {static_cast<uint32_t>(i)});
    if (gS.bZeroEntry && i == 1) return nullptr;
    if (i < 0 || i >= NO) return nullptr;
    return &gW->objs[i];
}
void *__fastcall B38(void *self, void *, uint32_t key)
{
    Log(self, 0x38, {key});
    uint32_t k = Mix(key ^ gS.keyMask) % (NO + 2);
    if (k >= NO) return nullptr;
    return &gW->objs[k];
}

// C
void *__fastcall C74(void *self, void *, int32_t id)
{
    Log(self, 0x74, {static_cast<uint32_t>(id)});
    return FindObj(static_cast<int16_t>(id));
}
void *__fastcall CA8(void *self, void *, int32_t ref)
{
    Log(self, 0xa8, {static_cast<uint32_t>(ref)});
    uint32_t k = static_cast<uint32_t>(ref) % (NO + 2);
    if (ref < 0 || k >= NO) return nullptr;
    return &gW->objs[k];
}
void *__fastcall C14c(void *self, void *, int32_t k)
{
    Log(self, 0x14c, {static_cast<uint32_t>(k)});
    if (gS.kLookupNull) return nullptr;
    return &gW->lists[k & 1];
}

// D
void *__fastcall D1c(void *self, void *, uint32_t id)
{
    Log(self, 0x1c, {id});
    if (gS.dLookupNull) return nullptr;
    return &gW->eobjs[id % NE];
}
uint32_t __fastcall E0c(void *self, void *, int32_t which, uint32_t *out)
{
    Log(self, 0xc, {static_cast<uint32_t>(which), out ? 1u : 0u});
    EObj *e = static_cast<EObj *>(self);
    uint32_t okv = static_cast<uint32_t>(e->ok[which & 3]);
    if (okv && out) *out = e->out[which & 3];
    return (Mix(gCalls) & 0xffffff00u) | (okv & 0xff);
}
// Slots 5 and 6 (0x14, 0x18; modes 0x17 and 0x16, DT): fill the caller's std::vector<uint16_t> (begin, end, limit) with
// the script's ids, in a block from the game's allocator service (the vector is a SimIdVector: the original's
// destructor 0x4f2110 and ours free through the service; AllocatorScope in the test). Spare capacity holds junk.
uint32_t FillIds(void *self, uint32_t slot, uint16_t **vec)
{
    Log(self, slot, {vec ? 1u : 0u});
    EObj *e = static_cast<EObj *>(self);
    const uint32_t k = slot == 0x14 ? 0 : 1;
    const uint32_t okv = static_cast<uint32_t>(e->ok[k]);
    if (okv && vec && e->idCap[k] != 0) {
        uint16_t *block = GZServiceAllocator<uint16_t>().allocate(e->idCap[k]);
        for (uint32_t i = 0; i < e->idCap[k]; ++i) block[i] = i < e->idN[k] ? e->ids[k][i] : static_cast<uint16_t>(0xbeef + i);
        vec[0] = block;
        vec[1] = block + e->idN[k];
        vec[2] = block + e->idCap[k];
    }
    return (Mix(gCalls) & 0xffffff00u) | (okv & 0xff);
}
uint32_t __fastcall E14(void *self, void *, uint16_t **vec) { return FillIds(self, 0x14, vec); }
uint32_t __fastcall E18(void *self, void *, uint16_t **vec) { return FillIds(self, 0x18, vec); }

// Objects
uint32_t __fastcall ObjQI(void *self, void *, uint32_t iid, void **out)
{
    Log(self, 0, {iid, out ? 1u : 0u});
    Obj *o = static_cast<Obj *>(self);
    if (!o->qiOk) return JunkBool(false);
    if (out) {
        Iface *f = static_cast<Iface *>(o->qi);
        if (f) ++f->refs;
        *out = f;
    }
    return JunkBool(true);
}
uint32_t __fastcall ObjAddRef(void *self, void *)
{
    Log(self, 4, {});
    return static_cast<uint32_t>(++static_cast<Obj *>(self)->refs);
}
uint32_t __fastcall ObjRelease(void *self, void *)
{
    Log(self, 8, {});
    return static_cast<uint32_t>(--static_cast<Obj *>(self)->refs);
}
void *__fastcall Obj10(void *self, void *)
{
    Log(self, 0x10, {});
    return static_cast<Obj *>(self)->rec;
}
uint32_t __fastcall Obj34(void *self, void *, int32_t a)
{
    Log(self, 0x34, {static_cast<uint32_t>(a)});
    Obj *o = static_cast<Obj *>(self);
    uint32_t v = static_cast<uint32_t>(o->attr[static_cast<uint32_t>(a) & 0xff]);
    return (Mix(gCalls * 17u + 3u) & 0xffff0000u) | (v & 0xffffu);
}
uint32_t __fastcall Obj64(void *self, void *, uint32_t m)
{
    Log(self, 0x64, {m});
    Obj *o = static_cast<Obj *>(self);
    return JunkBool((o->flags & m) != 0);
}
int32_t __fastcall Obj70(void *self, void *)
{
    Log(self, 0x70, {});
    return static_cast<Obj *>(self)->v70;
}
uint32_t __fastcall ObjBC(void *self, void *)
{
    Log(self, 0xbc, {});
    return JunkHi(static_cast<Obj *>(self)->id);
}
int32_t __fastcall ObjE4(void *self, void *)
{
    Log(self, 0xe4, {});
    return static_cast<Obj *>(self)->e4;
}
void *__fastcall ObjF4(void *self, void *)
{
    Log(self, 0xf4, {});
    return static_cast<Obj *>(self)->def;
}
int32_t __fastcall Obj188(void *self, void *)
{
    Log(self, 0x188, {});
    return static_cast<Obj *>(self)->s98;
}
uint32_t __fastcall Obj1a0(void *self, void *)
{
    Log(self, 0x1a0, {});
    return JunkBool(static_cast<Obj *>(self)->b1a0);
}
void *__fastcall Obj1b4(void *self, void *)
{
    Log(self, 0x1b4, {});
    return static_cast<Obj *>(self)->r;
}
uint32_t __fastcall Obj20c(void *self, void *)
{
    Log(self, 0x20c, {});
    return JunkHi(static_cast<Obj *>(self)->s131);
}
uint32_t __fastcall Obj2f0(void *self, void *)
{
    Log(self, 0x2f0, {});
    return JunkBool(static_cast<Obj *>(self)->b2f0);
}

// Def, Rec
void *__fastcall Def10(void *self, void *)
{
    Log(self, 0x10, {});
    return static_cast<Def *>(self)->rec;
}
int32_t __fastcall Def70(void *self, void *)
{
    Log(self, 0x70, {});
    return static_cast<Def *>(self)->v70;
}
int32_t __fastcall Rec0c(void *self, void *, int32_t n)
{
    Log(self, 0xc, {static_cast<uint32_t>(n)});
    return static_cast<Rec *>(self)->val[static_cast<uint32_t>(n) & 0xff];
}

// Entry
uint32_t __fastcall Ent20(void *self, void *)
{
    Log(self, 0x20, {});
    return JunkHi(static_cast<Entry *>(self)->id20);
}
void *__fastcall Ent28(void *self, void *)
{
    Log(self, 0x28, {});
    return static_cast<Entry *>(self)->x;
}
void *__fastcall Ent30(void *self, void *)
{
    Log(self, 0x30, {});
    return static_cast<Entry *>(self)->y;
}
int32_t __fastcall Ent3c(void *self, void *)
{
    Log(self, 0x3c, {});
    return static_cast<Entry *>(self)->ref;
}
uint32_t __fastcall Ent40(void *self, void *, int32_t a)
{
    Log(self, 0x40, {static_cast<uint32_t>(a)});
    return JunkHi(static_cast<uint32_t>(static_cast<Entry *>(self)->st[static_cast<uint32_t>(a) & 0xff]));
}

// Cont (mode 1 elements)
void *__fastcall Cont20(void *self, void *)
{
    Log(self, 0x20, {});
    return static_cast<Cont *>(self)->obj;
}
uint32_t __fastcall Cont28(void *self, void *, int32_t a, int32_t b)
{
    Log(self, 0x28, {static_cast<uint32_t>(a), static_cast<uint32_t>(b)});
    return JunkHi(static_cast<uint32_t>(static_cast<Cont *>(self)->st[(static_cast<uint32_t>(b) + static_cast<uint32_t>(a)) & 3]));
}

// Iface
uint32_t __fastcall IfAddRef(void *self, void *)
{
    Log(self, 4, {});
    return static_cast<uint32_t>(++static_cast<Iface *>(self)->refs);
}
uint32_t __fastcall IfRelease(void *self, void *)
{
    Log(self, 8, {});
    return static_cast<uint32_t>(--static_cast<Iface *>(self)->refs);
}
void *__fastcall If18(void *self, void *)
{
    Log(self, 0x18, {});
    return static_cast<Iface *>(self)->i18;
}
void *__fastcall If1c(void *self, void *)
{
    Log(self, 0x1c, {});
    return static_cast<Iface *>(self)->i1c;
}
void *__fastcall If20(void *self, void *)
{
    Log(self, 0x20, {});
    return static_cast<Iface *>(self)->i20;
}
uint32_t __fastcall If28(void *self, void *, int32_t a, int32_t b)
{
    Log(self, 0x28, {static_cast<uint32_t>(a), static_cast<uint32_t>(b)});
    return JunkHi(static_cast<uint32_t>(static_cast<Iface *>(self)->v28[(a & 1)]));
}
void *__fastcall If2c(void *self, void *)
{
    Log(self, 0x2c, {});
    return static_cast<Iface *>(self)->i2c;
}
uint32_t __fastcall If64(void *self, void *, int16_t *tgt, float *fa, float *fb)
{
    Iface *f = static_cast<Iface *>(self);
    Log(self, 0x64, {tgt ? static_cast<uint32_t>(static_cast<uint16_t>(*tgt)) : 0xffffffffu});
    if (f->seqPos < f->seqN) {
        if (tgt) *tgt = f->seq[f->seqPos];
        ++f->seqPos;
        *fa = f->fa;
        *fb = f->fb;
        return JunkBool(true);
    }
    return JunkBool(false);
}
uint32_t __fastcall If68(void *self, void *, int16_t *tgt, uint32_t w)
{
    Iface *f = static_cast<Iface *>(self);
    Log(self, 0x68, {tgt ? static_cast<uint32_t>(static_cast<uint16_t>(*tgt)) : 0xffffffffu, w});
    if (f->seqPos < f->seqN) {
        if (tgt) *tgt = f->seq[f->seqPos];
        ++f->seqPos;
        return JunkBool(true);
    }
    return JunkBool(false);
}

// Y (entry record), L (id list), R
int32_t __fastcall Y14(void *self, void *, int32_t a)
{
    Log(self, 0x14, {static_cast<uint32_t>(a)});
    return static_cast<Y *>(self)->has[a & 7];
}
int32_t __fastcall Y24(void *self, void *, int32_t a, int32_t b)
{
    Log(self, 0x24, {static_cast<uint32_t>(a), static_cast<uint32_t>(b)});
    return static_cast<Y *>(self)->val[(a + b) & 7];
}
uint32_t __fastcall L1c(void *self, void *, int32_t i)
{
    Log(self, 0x1c, {static_cast<uint32_t>(i)});
    ListEnt *l = static_cast<ListEnt *>(self);
    if (i < 0 || i >= NL) return JunkHi(0x7777);
    return JunkHi(static_cast<uint32_t>(l->ids[i]));
}
int32_t __fastcall L20(void *self, void *)
{
    Log(self, 0x20, {});
    return static_cast<ListEnt *>(self)->count;
}
uint32_t __fastcall R14(void *self, void *, uint32_t a)
{
    Log(self, 0x14, {a});
    return static_cast<uint32_t>(static_cast<R *>(self)->cnt[a & 7]);
}
uint32_t __fastcall R24(void *self, void *, uint32_t a, int32_t b)
{
    Log(self, 0x24, {a, static_cast<uint32_t>(b)});
    return static_cast<uint32_t>(static_cast<R *>(self)->v24[(a + b) & 7]);
}
uint32_t __fastcall R30(void *self, void *)
{
    Log(self, 0x30, {});
    return JunkHi(static_cast<uint32_t>(static_cast<R *>(self)->v30));
}
uint32_t __fastcall R34(void *self, void *, uint32_t a)
{
    Log(self, 0x34, {a});
    return JunkHi(static_cast<uint32_t>(static_cast<R *>(self)->v34[a & 7]));
}
void *__fastcall Owner7c(void *self, void *)
{
    Log(self, 0x7c, {});
    return &static_cast<Owner *>(self)->list;
}

// ---------------------------------------------------------------------------------- world building

void Fill(void **vt, std::initializer_list<std::pair<int, void *>> slots)
{
    for (int i = 0; i < kSlots; ++i) vt[i] = nullptr;
    for (auto &s : slots) vt[s.first / 4] = s.second;
}
#define SL(off, fn) {off, reinterpret_cast<void *>(&fn)}

// The mode bytes the test exercises (8, 9 and 0x22 are left out, see the file comment).
const uint8_t kModes[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
                          0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x23, 0x24,
                          0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,
                          0x34, 0x40, 0x7f};

struct Case {
    uint8_t param[0x18];
    uint8_t mode;
};

void Build(World &w, uint32_t seed, Case &c)
{
    std::mt19937 r(seed * 2246822519u + 97u);
    auto rnd = [&](uint32_t n) { return static_cast<uint32_t>(r() % n); };
    // Bulk arrays come from a cheap hash of the seed, the structure from the generator.
    auto bulk = [&](uint32_t salt, uint32_t i, uint32_t n) { return static_cast<int32_t>(Mix(seed * 0x9e3779b1u + salt * 0x85ebca6bu + i) % n); };
    std::memset(&w, 0xA5, sizeof(World));
    gCalls = 0;

    Fill(w.vtSelf, {SL(0x00, SelfQI), SL(0x4c, SelfS4c), SL(0x50, SelfS50), SL(0xfc, SelfSfc), SL(0x2c0, SelfS2c0)});
    Fill(w.vtSub, {SL(0x1c, Read7)});
    Fill(w.vtEnv, {SL(0x18, Env18), SL(0x28, Env28), SL(0x60, Env60), SL(0x68, Env68), SL(0x74, Env74)});
    Fill(w.vtX, {SL(0x34, X34)});
    Fill(w.vtY2, {SL(0x54, Y254)});
    Fill(w.vtWorld, {SL(0x04, WorldAddRef), SL(0x08, WorldRelease), SL(0x74, World74), SL(0x88, World88), SL(0xb4, WorldB4), SL(0xbc, WorldBC)});
    Fill(w.vtFrame, {SL(0x30, Frame30), SL(0x6c, Frame6c), SL(0x78, Frame78), SL(0x7c, Frame7c)});
    Fill(w.vtA, {SL(0x3c, A3c), SL(0x40, A40)});
    Fill(w.vtB, {SL(0x28, B28), SL(0x34, B34), SL(0x38, B38)});
    Fill(w.vtC, {SL(0x74, C74), SL(0xa8, CA8), SL(0x14c, C14c)});
    Fill(w.vtD, {SL(0x1c, D1c)});
    Fill(w.vtObj, {SL(0x00, ObjQI), SL(0x04, ObjAddRef), SL(0x08, ObjRelease), SL(0x10, Obj10), SL(0x34, Obj34), SL(0x64, Obj64), SL(0x70, Obj70), SL(0xbc, ObjBC),
                   SL(0xe4, ObjE4), SL(0xf4, ObjF4), SL(0x188, Obj188), SL(0x1a0, Obj1a0), SL(0x1b4, Obj1b4),
                   SL(0x20c, Obj20c), SL(0x2f0, Obj2f0)});
    Fill(w.vtEntry, {SL(0x20, Ent20), SL(0x28, Ent28), SL(0x30, Ent30), SL(0x3c, Ent3c), SL(0x40, Ent40)});
    Fill(w.vtDef, {SL(0x10, Def10), SL(0x70, Def70)});
    Fill(w.vtRec, {SL(0x0c, Rec0c)});
    Fill(w.vtCont, {SL(0x20, Cont20), SL(0x28, Cont28)});
    Fill(w.vtIface, {SL(0x04, IfAddRef), SL(0x08, IfRelease), SL(0x18, If18), SL(0x1c, If1c), SL(0x20, If20), SL(0x28, If28),
                     SL(0x2c, If2c), SL(0x64, If64), SL(0x68, If68)});
    Fill(w.vtYr, {SL(0x14, Y14), SL(0x24, Y24)});
    Fill(w.vtL, {SL(0x1c, L1c), SL(0x20, L20)});
    Fill(w.vtE, {SL(0x0c, E0c), SL(0x14, E14), SL(0x18, E18)});
    Fill(w.vtR, {SL(0x14, R14), SL(0x24, R24), SL(0x30, R30), SL(0x34, R34)});
    Fill(w.vtOwner, {SL(0x7c, Owner7c)});

    w.self.mVTable = w.vtSelf;
    w.self.mTreeSimVTable = w.vtSub;
    w.envVT = w.vtEnv;
    w.self.mEnvironment = &w.envVT;
    w.xVT = w.vtX;
    w.y2VT = w.vtY2;
    w.worldVT = w.vtWorld;
    w.frameVT = w.vtFrame;
    w.aVT = w.vtA;
    w.bVT = w.vtB;
    w.cVT = w.vtC;
    w.dVT = w.vtD;
    w.owner.vt = w.vtOwner;
    w.self.mObjectListOwner = &w.owner;

    // ids: a base that sometimes has the sign bit set
    const uint16_t bases[] = {1, 0x0101, 0x7ff9, 0x8001, 0xfff8};
    const uint16_t base = bases[rnd(5)];
    const uint16_t idxAttrs[] = {0x10, 0x11, 0x12};
    for (int i = 0; i < NO; ++i) {
        Obj &o = w.objs[i];
        o.vt = w.vtObj;
        o.id = static_cast<uint16_t>(base + i * (1 + rnd(2)));
        if (i > 0 && o.id == w.objs[i - 1].id) o.id = static_cast<uint16_t>(o.id + 7);
        o.flags = (rnd(8) == 0 ? 0x10000u : 0u) | (rnd(8) == 0 ? 0x20000u : 0u);
        for (uint32_t k = 0; k < 0x100; ++k) o.attr[k] = bulk(10 + i, k, 3);
        const int32_t a38[] = {0, 0, 1, 4, 5, 8};
        o.attr[0x38] = a38[rnd(6)];
        o.attr[0x6b] = rnd(4) == 0 ? 1 : 0;
        o.attr[0x2a] = rnd(4) == 0 ? 0x2000 : 0;
        o.attr[0x4c] = rnd(3) == 0 ? 0 : 1;
        o.attr[0x4d] = rnd(3) == 0 ? 0 : 1;
        o.s98 = 1 + static_cast<int32_t>(rnd(3));
        o.s131 = static_cast<int32_t>(rnd(3));
        o.e4 = static_cast<int32_t>(rnd(4)) - 1;
        o.v70 = rnd(2) ? static_cast<int32_t>(rnd(5)) : static_cast<int32_t>(rnd(0x60000)) - 0x30000;
        o.b1a0 = rnd(4) != 0;
        o.b2f0 = rnd(4) != 0;
        o.rec = &w.recs[i];
        o.def = &w.defs[i];
        o.r = &w.rs[i];
        o.qi = rnd(5) == 0 ? nullptr : &w.ifaces[rnd(NI)];
        o.qiOk = rnd(5) != 0;
        o.refs = 1;
    }
    w.worldRefs = 1;
    for (int i = 0; i < NO + 1; ++i) {
        w.defs[i].vt = w.vtDef;
        w.defs[i].rec = &w.recs[rnd(NO + 2)];
        w.defs[i].v70 = static_cast<int32_t>(rnd(6));
    }
    for (int i = 0; i < NO + 2; ++i) {
        w.recs[i].vt = w.vtRec;
        for (uint32_t k = 0; k < 0x100; ++k) w.recs[i].val[k] = bulk(30 + i, k, 4);
        w.recs[i].val[9] = static_cast<int32_t>(rnd(3) == 0 ? 0x10 : rnd(3));
        w.recs[i].val[0xcb] = static_cast<int32_t>(rnd(6));
        w.recs[i].val[0x4b] = static_cast<int32_t>(rnd(3));
        w.recs[i].val[0x4c] = static_cast<int32_t>(rnd(3));
    }
    for (int i = 0; i < NO; ++i) {
        w.rs[i].vt = w.vtR;
        for (auto &v : w.rs[i].cnt) v = static_cast<int32_t>(rnd(4));
        for (auto &v : w.rs[i].v24) v = rnd(2) ? static_cast<int32_t>(rnd(9)) : static_cast<int32_t>(rnd(0x20000)) - 0x4000;
        w.rs[i].v30 = static_cast<int32_t>(rnd(7)) - (rnd(6) == 0 ? 1 : 0) * 2;
        // a chain that always ends: index k leads to a later index, 7 leads to -1
        for (int k = 0; k < 8; ++k) w.rs[i].v34[k] = (k == 7) ? -1 : static_cast<int32_t>(std::min<uint32_t>(7u, k + 1 + rnd(2)));
    }
    for (int i = 0; i < NE; ++i) {
        Entry &e = w.ents[i];
        e.vt = w.vtEntry;
        e.obj = &w.objs[rnd(NO)];
        e.x = &w.defs[rnd(NO + 1)];
        e.y = &w.ys[i];
        e.ref = static_cast<int32_t>(rnd(NO + 3)) - (rnd(6) == 0 ? 3 : 0);
        e.id20 = e.obj->id;
        if (rnd(6) == 0) e.id20 = static_cast<int32_t>(rnd(0x10000));
        for (uint32_t k = 0; k < 0x100; ++k) e.st[k] = bulk(50 + i, k, 4);
        w.ys[i].vt = w.vtYr;
        for (auto &v : w.ys[i].has) v = static_cast<int32_t>(rnd(3));
        for (auto &v : w.ys[i].val) v = rnd(2) ? static_cast<int32_t>(rnd(8)) : static_cast<int32_t>(rnd(0x20000)) - 0x9000;
        w.eobjs[i].vt = w.vtE;
        for (int k = 0; k < 4; ++k) {
            w.eobjs[i].ok[k] = static_cast<int32_t>(rnd(3) != 0);
            w.eobjs[i].out[k] = rnd(2) ? static_cast<uint32_t>(rnd(NE + 2)) : (rnd(0x10000) | (rnd(2) ? 0xabcd0000u : 0u));
        }
        for (int k = 0; k < 2; ++k) {
            EObj &e = w.eobjs[i];
            e.idN[k] = rnd(5) == 0 ? 0 : static_cast<uint32_t>(rnd(8) + 1);
            e.idCap[k] = e.idN[k] + (rnd(2) ? static_cast<uint32_t>(rnd(3)) : 0);
            for (auto &v : e.ids[k]) v = rnd(6) ? static_cast<uint16_t>(rnd(NE + 2)) : static_cast<uint16_t>(rnd(0x10000));
        }
    }
    for (int i = 0; i < NC; ++i) {
        w.conts[i].vt = w.vtCont;
        w.conts[i].obj = &w.objs[rnd(NO)];
        for (auto &s : w.conts[i].st) s = static_cast<int32_t>(rnd(4));
    }
    for (int i = 0; i < NI; ++i) {
        Iface &f = w.ifaces[i];
        f.vt = w.vtIface;
        f.refs = 1;
        f.i1c = rnd(3) == 0 ? nullptr : &w.ifaces[rnd(NI)];
        f.i18 = &w.ifaces[rnd(NI)]; // never null: mode 3 would call through it
        f.i2c = &w.objs[rnd(NO)];
        f.i20 = &w.objs[rnd(NO)];
        f.v28[0] = static_cast<int32_t>(rnd(0x10000));
        f.v28[1] = static_cast<int32_t>(rnd(0x10000));
        f.seqN = static_cast<int>(rnd(6));
        f.seqPos = 0;
        for (auto &s : f.seq) s = rnd(4) == 0 ? static_cast<int16_t>(rnd(0x10000)) : static_cast<int16_t>(w.objs[rnd(NO)].id);
        f.fa = static_cast<float>(rnd(2000)) / 7.0f - 100.0f;
        f.fb = static_cast<float>(rnd(2000)) / 3.0f - 200.0f;
    }
    for (int i = 0; i < 2; ++i) {
        w.lists[i].vt = w.vtL;
        w.lists[i].count = static_cast<int32_t>(rnd(NL + 1));
        for (auto &id : w.lists[i].ids) id = rnd(5) == 0 ? static_cast<int32_t>(rnd(0x10000)) : w.objs[rnd(NO)].id;
    }
    // vectors: 0..2 hold objects, 3 holds containers
    for (int v = 0; v < 4; ++v) {
        int n = static_cast<int>(rnd(NV + 1));
        for (int i = 0; i < NV; ++i) {
            if (v == 3) w.vecElems[v][i] = &w.conts[rnd(NC)];
            else w.vecElems[v][i] = &w.objs[rnd(NO)];
        }
        w.vecs[v].first = reinterpret_cast<void **>(&w.vecElems[v][0]);
        w.vecs[v].last = reinterpret_cast<void **>(&w.vecElems[v][0]) + n;
    }
    // the std::list of all objects
    {
        int n = static_cast<int>(rnd(NL + 1));
        ListNode *head = &w.nodes[NL];
        ListNode *prev = head;
        for (int i = 0; i < n; ++i) {
            ListNode *nd = &w.nodes[i];
            nd->prev = prev;
            prev->next = nd;
            nd->value = &w.objs[rnd(NO)]; // null entries would crash the original in most modes
            prev = nd;
        }
        prev->next = head;
        head->prev = prev;
        head->value = nullptr;
        w.owner.list.head = head;
    }
    w.self.mField96 = static_cast<int16_t>(w.objs[rnd(NO)].id);
    if (rnd(5) == 0) w.self.mField96 = static_cast<int16_t>(rnd(0x10000));
    w.self.mField98 = static_cast<int16_t>(rnd(8));
    w.destSlot = 0x1234;
    w.canary = 0xA55A;

    Script s;
    s.hasTarget = rnd(12) != 0;
    switch (rnd(6)) {
    case 0: s.mn = 1; s.mx = 32767; break;
    case 1: s.mn = -32768; s.mx = 0x7fff; break;
    case 2: s.mn = 0; s.mx = static_cast<int16_t>(rnd(10)); break;
    case 3: s.mn = static_cast<int16_t>(rnd(100)) - 50; s.mx = static_cast<int16_t>(rnd(100)) - 50; break;
    default: s.mn = -32768; s.mx = 32767; break;
    }
    s.unknown0 = r();
    auto pickVal = [&]() -> uint32_t {
        switch (rnd(5)) {
        case 0: return rnd(3);
        case 1: return w.objs[rnd(NO)].id;
        case 2: return rnd(NE + 2);
        case 3: return rnd(0x10000);
        default: return rnd(4);
        }
    };
    for (auto &p : s.selfParam) p = pickVal();
    s.frame30 = pickVal();
    s.frame78 = pickVal();
    for (auto &p : s.frame7c) p = rnd(3) == 0 ? rnd(0x10000) : w.objs[rnd(NO)].id;
    s.frameCount = rnd(4);
    s.selfQiOk = rnd(5) != 0;
    s.owner2c0 = rnd(2) != 0;
    s.aSeed = r();
    for (int i = 0; i < NE + 2; ++i) s.entNull[i] = rnd(6) == 0;
    s.bCount = static_cast<int32_t>(rnd(NO + 2));
    s.vecSel = static_cast<int>(rnd(3));
    s.objLookupMask = rnd(5) == 0 ? (1u << rnd(NO)) : 0u;
    s.keyMask = r();
    s.dLookupNull = rnd(8) == 0;
    s.kLookupNull = rnd(8) == 0;
    s.bZeroEntry = rnd(8) == 0;
    gS = s;

    // The destination's start value: usually an id, an index, 0 or a stray number.
    switch (rnd(6)) {
    case 0: w.destSlot = 0; break;
    case 1: w.destSlot = static_cast<int16_t>(w.objs[rnd(NO)].id); break;
    case 2: w.destSlot = static_cast<int16_t>(rnd(NE + 2)); break;
    case 3: w.destSlot = static_cast<int16_t>(rnd(0x10000)); break;
    case 4: w.destSlot = static_cast<int16_t>(w.ents[rnd(NE)].id20); break;
    default: w.destSlot = static_cast<int16_t>(w.objs[rnd(NO)].id); break;
    }

    // The node's parameter block.
    std::memset(&c, 0, sizeof(c));
    for (auto &b : c.param) b = static_cast<uint8_t>(r());
    c.mode = kModes[rnd(sizeof(kModes))];
    c.param[8] = static_cast<uint8_t>((c.param[8] & 0x80) | c.mode);
    c.param[0xa] = static_cast<uint8_t>(rnd(4));
    c.param[0xc] = static_cast<uint8_t>(r() & 7);
    uint16_t idxWord = rnd(5) == 0 ? static_cast<uint16_t>(0x8000 | rnd(8)) : static_cast<uint16_t>(0x10 + rnd(3));
    uint16_t cmpWord = static_cast<uint16_t>(rnd(3));
    std::memcpy(c.param + 0xd, &idxWord, 2);
    std::memcpy(c.param + 0xf, &cmpWord, 2);
    uint32_t key = rnd(2) ? static_cast<uint32_t>(r()) : (w.objs[rnd(NO)].id | (w.objs[rnd(NO)].id << 16));
    std::memcpy(c.param + 4, &key, 4);

    // Keep the world inside what the original survives: it dereferences some lookups without a null check.
    const uint8_t m = c.mode;
    const bool entNullOk = m == 0x07 || m == 0x0f || m == 0x15 || m == 0x1d || m == 0x1f || m == 0x27;
    if (!entNullOk)
        for (auto &n : gS.entNull) n = false;
    if (m == 0x1c || m == 0x23 || m == 0x24)
        for (auto &e : w.ents) e.id20 = 1 + static_cast<int32_t>(rnd(NE));
    if (m == 0x07 && rnd(3) == 0) { // a key some definition holds (DT): the match is otherwise too rare to be reached
        const int32_t v70 = w.defs[rnd(NO + 1)].v70;
        std::memcpy(c.param + 4, &v70, 4);
    }
    if (m == 0x03) // the QueryInterface result is used without a check
        for (auto &o : w.objs) {
            o.qiOk = true;
            if (!o.qi) o.qi = &w.ifaces[rnd(NI)];
        }
    if (m == 0x0e) {
        gS.objLookupMask = 0;
        gS.hasTarget = true;
        gS.mn = -32768;
        gS.mx = 32767;
        w.destSlot = static_cast<int16_t>(w.objs[rnd(NO)].id);
    }
}

struct Outcome {
    uint32_t ret = 0;
    int thrown = -1;
    std::vector<uint32_t> trace;
    int16_t dest = 0;
    uint16_t canary = 0;
    int16_t f9e = 0, fa0 = 0;
    int refs[NI] = {};
    int seqPos[NI] = {};
    int objRefs[NO] = {};
    int worldRefs = 0;
};

template <typename Call> Outcome RunCase(World &w, Call call)
{
    gW = &w;
    gTrace.clear();
    gCalls = 0;
    Outcome o;
    try {
        o.ret = call(w);
    } catch (short code) {
        o.thrown = code;
    } catch (...) {
        o.thrown = 0x7777; // another type of exception
    }
    o.trace = gTrace;
    o.dest = w.destSlot;
    o.canary = w.canary;
    o.f9e = w.self.mField9e;
    o.fa0 = w.self.mFieldA0;
    for (int i = 0; i < NI; ++i) {
        o.refs[i] = w.ifaces[i].refs;
        o.seqPos[i] = w.ifaces[i].seqPos;
    }
    for (int i = 0; i < NO; ++i) o.objRefs[i] = w.objs[i].refs;
    o.worldRefs = w.worldRefs;
    gW = nullptr;
    return o;
}

bool Same(const Outcome &a, const Outcome &b)
{
    // A call that ends in an exception leaves eax undefined; compare the result only on a normal return.
    if (a.thrown != b.thrown || a.trace != b.trace || a.dest != b.dest || a.canary != b.canary || a.f9e != b.f9e || a.fa0 != b.fa0)
        return false;
    if (std::memcmp(a.refs, b.refs, sizeof(a.refs)) != 0 || std::memcmp(a.seqPos, b.seqPos, sizeof(a.seqPos)) != 0) return false;
    if (std::memcmp(a.objRefs, b.objRefs, sizeof(a.objRefs)) != 0 || a.worldRefs != b.worldRefs) return false;
    return a.thrown >= 0 || a.ret == b.ret;
}

} // namespace

DIFF_TEST(PrimSetToNext, 0x5a12c0)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *, const void *)>(0x5a12c0);
    svcfx::AllocatorScope allocator; // modes 0x16/0x17 free the id vector through the game's allocator service
    std::map<int, uint32_t> trues, falses, throws;
    const uint32_t seeds = 60000;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        World a, b;
        Case ca, cb;
        Build(a, seed, ca); // the script (gS) is global: identical for both builds of a seed
        Build(b, seed, cb);
        Outcome oa = RunCase(a, [&](World &w) { return original(&w.self, &w.frameVT, ca.param); });
        Outcome ob = RunCase(b, [&](World &w) { return w.self.SetToNext(&w.frameVT, cb.param); });
        ++ctx.cases;
        if (oa.thrown >= 0) ++throws[ca.mode];
        else if (oa.ret & 0xff) ++trues[ca.mode];
        else ++falses[ca.mode];
        if (!Same(oa, ob)) {
            ctx.Fail("seed %u mode 0x%02x: thrown %d/%d ret %u/%u dest %d/%d trace %zu/%zu refs %d,%d/%d,%d", seed, ca.mode, oa.thrown,
                     ob.thrown, oa.ret, ob.ret, oa.dest, ob.dest, oa.trace.size(), ob.trace.size(), oa.refs[0], oa.refs[1],
                     ob.refs[0], ob.refs[1]);
        }
    }
    // Coverage: every tested mode should both succeed and fail (or throw) at least once.
    for (uint8_t m : kModes) {
        if (m == 0x0a || m == 0x34 || m == 0x40 || m == 0x7f) continue;
        if (trues[m] == 0) ctx.Fail("coverage: mode 0x%02x never returned true", m);
        if (falses[m] + throws[m] == 0) ctx.Fail("coverage: mode 0x%02x never returned false or threw", m);
    }
    if (throws[0x34] + throws[0x40] + throws[0x7f] == 0) ctx.Fail("coverage: the unknown-mode throw never happened");
}
