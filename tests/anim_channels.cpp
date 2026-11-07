// Differential tests for the QueryInterface slot (slot 0) of nGZSceneDBAnim::cChannelUber, cChannelUberQ,
// cChannelUberXYZ, cChannelUberAttribute and cEventKeyConst.
//
// Each trial builds one object three times. The original runs on the first side. Ours runs on the second side,
// through the object's vtable copy in which slot 0 is the original, and on the third side through a copy in which
// slot 0 points at our code. Slot 1 (AddRef) of every copy is a fake that records its call with the object's offset
// from its side, so the sides see the same callee and the calls can be compared. The compared state is the result
// (al only, see the source), the *ppv target, the recorded calls, the object bytes and the canaries around it.
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "sg/AnimChannels.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr int kObjectSize = 0x200;
constexpr int kMaxSlots = 15;
constexpr uint32_t kSentinel = 0x5a5a5a5au;

template <typename T> const void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// ---- Recording fake for AddRef -----------------------------------------------------------------------------------

struct Event {
    uint32_t kind;
    uint32_t object;
    bool operator==(const Event &o) const { return kind == o.kind && object == o.object; }
    bool operator!=(const Event &o) const { return !(*this == o); }
};
std::vector<Event> gEvents;
const void *gCur = nullptr; // the side whose offsets the fake records

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    gEvents.push_back({1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self) -
                                                 reinterpret_cast<uintptr_t>(gCur))});
    return 1;
}

// ---- Sides ----------------------------------------------------------------------------------------------------------

struct Side {
    uint8_t front[32];
    uint8_t object[kObjectSize];
    uint8_t back[32];
    const void *vt[kMaxSlots];
    uint32_t out; // *ppv target
};

struct Slots {
    const void *slots[kMaxSlots];
};

// The original vtable's slots, with slot 1 replaced by the recording fake.
Slots PlainSlots(const void *original, int count)
{
    Slots s{};
    std::memcpy(s.slots, original, sizeof(void *) * count);
    s.slots[1] = Addr(&FakeAddRef);
    return s;
}

// Object bytes with the vtable word expressed as an offset from its side, so the same object compares equal.
std::vector<uint8_t> Snapshot(const Side &s)
{
    uint8_t obj[kObjectSize];
    std::memcpy(obj, s.object, sizeof(obj));
    const uint32_t vt = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.vt) - reinterpret_cast<uintptr_t>(&s));
    std::memcpy(obj, &vt, sizeof(vt));
    return std::vector<uint8_t>(obj, obj + sizeof(obj));
}

bool CanariesIntact(const Side &s)
{
    for (uint8_t b : s.front)
        if (b != 0xA5) return false;
    for (uint8_t b : s.back)
        if (b != 0x5A) return false;
    return true;
}

// Fills one side: random object bytes (the same on every side), the side's own copy of the vtable, and canaries.
// The first dword of the object is the side's vtable pointer.
void Build(Side &s, const uint8_t *bytes, const void *const *vt, int count)
{
    std::memset(s.front, 0xA5, sizeof(s.front));
    std::memset(s.back, 0x5A, sizeof(s.back));
    std::memcpy(s.object, bytes, sizeof(s.object));
    std::memset(s.vt, 0, sizeof(s.vt));
    std::memcpy(s.vt, vt, sizeof(void *) * count);
    const void *self = s.vt;
    std::memcpy(s.object, &self, sizeof(self));
    s.out = kSentinel;
}

// What QueryInterface stored through ppv, as a small code: 1 the object, 2 untouched, 3 anything else.
uint64_t OutCode(const Side &s)
{
    if (s.out == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.object))) return 1;
    if (s.out == kSentinel) return 2;
    return 3;
}

// The riid is one of the accepted values, a neighbour of one, 0, 2 or a random value.
uint32_t PickRiid(std::mt19937 &rng, const uint32_t *ids, int count)
{
    switch (rng() % 4) {
    case 0: return static_cast<uint32_t>(rng());
    case 1: return rng() % 2 ? 0u : 2u;
    case 2: return ids[rng() % count] + (rng() % 2 ? 1u : static_cast<uint32_t>(-1));
    default: return ids[rng() % count];
    }
}

// Same-object comparison across the three sides. The object bytes are random, so both sides start from the same
// bytes, and the vtable pointer is set per side.
template <typename Obj, int kCount>
void CheckQueryInterface(difftest::Context &ctx, std::mt19937 &rng, uint32_t vtableRva, uint32_t origRva,
                         const uint32_t *ids, int idCount, int trials)
{
    const void *original = ctx.image.At(vtableRva);
    Slots plain = PlainSlots(original, kCount);
    Slots routed = plain;
    routed.slots[0] = Addr(&Obj::QueryInterface);
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(origRva);

    static Side a, b, c;
    for (int i = 0; i < trials; ++i) {
        uint8_t bytes[kObjectSize];
        for (auto &x : bytes) x = static_cast<uint8_t>(rng());
        const uint32_t riid = PickRiid(rng, ids, idCount);
        const bool nullPpv = rng() % 5 == 0;

        Build(a, bytes, plain.slots, kCount);
        Build(b, bytes, plain.slots, kCount);
        Build(c, bytes, routed.slots, kCount);

        uint64_t result[3];
        std::vector<Event> events[3];
        Side *sides[3] = {&a, &b, &c};
        for (int w = 0; w < 3; ++w) {
            Side &s = *sides[w];
            gCur = &s;
            gEvents.clear();
            void **ppv = nullPpv ? nullptr : reinterpret_cast<void **>(&s.out);
            bool r;
            if (w == 0) {
                r = orig(s.object, riid, ppv);
            } else if (w == 1) {
                r = reinterpret_cast<Obj *>(s.object)->QueryInterface(riid, ppv);
            } else {
                r = VirtualSlot<bool(__thiscall *)(void *, uint32_t, void **)>(s.object, 0)(s.object, riid, ppv);
            }
            result[w] = (r ? 1u : 0u) | (OutCode(s) << 8);
            events[w] = gEvents;
        }

        ++ctx.cases;
        if (result[0] != result[1] || result[0] != result[2]) {
            ctx.Fail("QueryInterface riid %08x: result original %llu, ours %llu, via slot %llu", riid,
                     static_cast<unsigned long long>(result[0]), static_cast<unsigned long long>(result[1]),
                     static_cast<unsigned long long>(result[2]));
        }
        if (events[0] != events[1] || events[0] != events[2]) {
            ctx.Fail("QueryInterface riid %08x: AddRef calls differ (original %zu, ours %zu, via slot %zu)", riid,
                     events[0].size(), events[1].size(), events[2].size());
        }
        if (Snapshot(a) != Snapshot(b) || Snapshot(a) != Snapshot(c)) {
            ctx.Fail("QueryInterface riid %08x: object state differs", riid);
        }
        if (!CanariesIntact(a) || !CanariesIntact(b) || !CanariesIntact(c)) {
            ctx.Fail("QueryInterface riid %08x: canaries overwritten", riid);
        }
    }
}

} // namespace

using nGZSceneDBAnim::cChannelUber;
using nGZSceneDBAnim::cChannelUberAttribute;
using nGZSceneDBAnim::cChannelUberQ;
using nGZSceneDBAnim::cChannelUberXYZ;
using nGZSceneDBAnim::cEventKeyConst;

// Accepted riids (the cases the riid picker aims at): 1, plus the class's own IIDs.
DIFF_TEST(AnimChannels_ChannelUber_QueryInterface, 0xa266d0)
{
    static const uint32_t kIds[] = {1, 0xec70498eu};
    std::mt19937 rng(0x2b01);
    CheckQueryInterface<cChannelUber, 9>(ctx, rng, cChannelUber::kVTable, 0xa266d0, kIds, 2, 4000);
}

DIFF_TEST(AnimChannels_ChannelUberQ_QueryInterface, 0x9efd10)
{
    static const uint32_t kIds[] = {1, 0xac704952u, 0xec70498eu};
    std::mt19937 rng(0x2b02);
    CheckQueryInterface<cChannelUberQ, 9>(ctx, rng, cChannelUberQ::kVTable, 0x9efd10, kIds, 3, 4000);
}

DIFF_TEST(AnimChannels_ChannelUberXYZ_QueryInterface, 0x9f0840)
{
    static const uint32_t kIds[] = {1, 0xcc70496eu, 0xec70498eu};
    std::mt19937 rng(0x2b03);
    CheckQueryInterface<cChannelUberXYZ, 9>(ctx, rng, cChannelUberXYZ::kVTable, 0x9f0840, kIds, 3, 4000);
}

DIFF_TEST(AnimChannels_ChannelUberAttribute_QueryInterface, 0x9f0c70)
{
    static const uint32_t kIds[] = {1, 0x6ce2e072u, 0xec70498eu};
    std::mt19937 rng(0x2b04);
    CheckQueryInterface<cChannelUberAttribute, 9>(ctx, rng, cChannelUberAttribute::kVTable, 0x9f0c70, kIds, 3,
                                                  4000);
}

DIFF_TEST(AnimChannels_EventKeyConst_QueryInterface, 0xa29200)
{
    static const uint32_t kIds[] = {1, 0x5b0aaa74u, 0x5b42776du};
    std::mt19937 rng(0x2b05);
    CheckQueryInterface<cEventKeyConst, 15>(ctx, rng, cEventKeyConst::kVTable, 0xa29200, kIds, 3, 4000);
}

// =====================================================================================================================
// Everything below: destructors, the weight evaluator, the attribute slots, cEventKeyConst's data slots, and the
// shared trivial slots. The collaborators the channels call (target, context, curve, setters, data object, released
// references) are fakes that record their calls; the original and ours see the same fakes, so the calls can be
// compared. A fake is 0x40 bytes: a primary vtable, a vbptr at +4, a vbase-1 subobject at +8 (QueryInterface in slot 0,
// Release in slot 2) and a vbase-2 subobject at +0xc (a float getter in slot 44), the way the game's objects with a
// virtual base are laid out.
// =====================================================================================================================

namespace {

struct Ev {
    uint32_t kind, id, a, b;
    bool operator==(const Ev &o) const { return kind == o.kind && id == o.id && a == o.a && b == o.b; }
    bool operator!=(const Ev &o) const { return !(*this == o); }
};
std::vector<Ev> gEv;
const void *gBase = nullptr; // the channel of the side being run; pointers into it are recorded relative to it
enum : uint32_t {
    kEvRelease = 1, kEvData, kEvCurve, kEvSlot8, kEvContext, kEvSet, kEvFloat, kEvKey, kEvQI, kEvDelete, kEvAddRef, kEvSetData
};

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
float FromBits(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}
uint32_t RelTo(const void *p, const void *base)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(base));
}

struct Fake {
    void *const *vt;       // +0x00
    const int32_t *vbptr;  // +0x04
    void *const *unkVt;    // +0x08  vbase 1
    void *const *rdVt;     // +0x0c  vbase 2
    uint32_t id;           // +0x10
    float fValue;          // returned by the float getters and the curve
    float kValue;          // returned by the key function
    bool boolValue;        // returned by QueryInterface and slot 18
    Fake *ptrValue;        // returned by slots 8 and 12
    Fake *qiValue;         // stored through QueryInterface's ppv
    void *data;            // returned by slot 1
    int32_t vbtable[3];
    uint32_t pad[3];
};
static_assert(sizeof(Fake) <= 0x60, "fake size");

uint32_t __fastcall FkReleaseD(Fake *self, void *)
{
    gEv.push_back({kEvRelease, self->id, 0, 0});
    return 0;
}
uint32_t __fastcall FkReleaseV(uint8_t *unknown, void *)
{
    const Fake *f = reinterpret_cast<const Fake *>(unknown - 8);
    gEv.push_back({kEvRelease, f->id, 1, 0});
    return 0;
}
void *__fastcall FkData(Fake *self, void *)
{
    gEv.push_back({kEvData, self->id, 0, 0});
    return self->data;
}
float __fastcall FkCurve(Fake *self, void *, int arg)
{
    gEv.push_back({kEvCurve, self->id, static_cast<uint32_t>(arg), 0});
    return self->fValue;
}
Fake *__fastcall FkSlot8(Fake *self, void *)
{
    gEv.push_back({kEvSlot8, self->id, 0, 0});
    return self->ptrValue;
}
Fake *__fastcall FkContext(Fake *self, void *)
{
    gEv.push_back({kEvContext, self->id, 0, 0});
    return self->ptrValue;
}
// Slot 18 takes the key and the two arguments pushed before the context's slot 62 (the key field's address, the data).
bool __fastcall FkSet(Fake *self, void *, float v, const void *key, Fake *arg)
{
    gEv.push_back({kEvSet, self->id, Bits(v), RelTo(key, gBase)});
    gEv.push_back({kEvSetData, self->id, arg ? arg->id : 0xffffu, 0});
    return self->boolValue;
}
float __fastcall FkFloat(Fake *self, void *)
{
    gEv.push_back({kEvFloat, self->id, 0, 0});
    return self->fValue;
}
float __fastcall FkFloatRd(uint8_t *reader, void *)
{
    const Fake *f = reinterpret_cast<const Fake *>(reader - 0xc);
    gEv.push_back({kEvFloat, f->id, 1, 0});
    return f->fValue;
}
float __fastcall FkKey(Fake *self, void *) // slot 62: no arguments
{
    gEv.push_back({kEvKey, self->id, 0, 0});
    return self->kValue;
}
bool __fastcall FkQI(uint8_t *unknown, void *, uint32_t riid, void **ppv)
{
    Fake *f = reinterpret_cast<Fake *>(unknown - 8);
    gEv.push_back({kEvQI, f->id, riid, 0});
    if (ppv) *ppv = f->qiValue;
    return f->boolValue;
}

void *const *PrimaryVTable()
{
    static void *vt[64];
    static bool init = false;
    if (!init) {
        vt[1] = reinterpret_cast<void *>(&FkData);
        vt[2] = reinterpret_cast<void *>(&FkReleaseD);
        vt[6] = reinterpret_cast<void *>(&FkCurve);
        vt[8] = reinterpret_cast<void *>(&FkSlot8);
        vt[12] = reinterpret_cast<void *>(&FkContext);
        vt[18] = reinterpret_cast<void *>(&FkSet);
        vt[44] = reinterpret_cast<void *>(&FkFloat);
        vt[62] = reinterpret_cast<void *>(&FkKey);
        init = true;
    }
    return vt;
}
void *const *UnknownVTable()
{
    static void *vt[3];
    static bool init = false;
    if (!init) {
        vt[0] = reinterpret_cast<void *>(&FkQI);
        vt[2] = reinterpret_cast<void *>(&FkReleaseV);
        init = true;
    }
    return vt;
}
void *const *ReaderVTable()
{
    static void *vt[48];
    static bool init = false;
    if (!init) {
        vt[44] = reinterpret_cast<void *>(&FkFloatRd);
        init = true;
    }
    return vt;
}

void InitFake(Fake &f, uint32_t id)
{
    std::memset(&f, 0, sizeof(f));
    f.vt = PrimaryVTable();
    f.vbtable[0] = 0;
    f.vbtable[1] = 4; // (object + 4) + 4 = the subobject at +8
    f.vbtable[2] = 8; // (object + 4) + 8 = the subobject at +0xc
    f.vbptr = f.vbtable;
    f.unkVt = UnknownVTable();
    f.rdVt = ReaderVTable();
    f.id = id;
}

float PickFloat(std::mt19937 &rng)
{
    static const uint32_t pool[] = {0x00000000, 0x80000000, 0x3f800000, 0x3f000000, 0x3e800000, 0x40000000,
                                    0xbf800000, 0x3fc00000, 0x42c80000, 0x7fc00000, 0x7f800000, 0xff800000,
                                    0x00000001, 0x3f7fffff, 0x3f800001, 0xc2c80000, 0x3dcccccd, 0x3f4ccccd};
    if (rng() % 3 != 0) return FromBits(pool[rng() % (sizeof(pool) / sizeof(pool[0]))]);
    uint32_t u = static_cast<uint32_t>(rng());
    if ((u & 0x7f800000u) == 0x7f800000u) u |= 0x00400000u; // keep NaNs quiet: x87 loads would quiet them anyway
    return FromBits(u);
}

void StoreWord(void *at, uintptr_t v)
{
    std::memcpy(at, &v, sizeof(void *));
}

// ---- Destructors ----------------------------------------------------------------------------------------------------

struct RefField {
    uint32_t offset;
    bool virtualBase; // a "V" reference; otherwise a "D" reference
};

constexpr RefField kBaseRefs[] = {{0x08, true},  {0x14, true},  {0x20, false}, {0x28, true}, {0x2c, false},
                                  {0x48, false}, {0x4c, false}, {0x64, false}, {0x68, false}, {0xc0, false}};
constexpr uint32_t kBaseRawFields[] = {0x18, 0x1c, 0x88, 0xa8};

struct DtorWorld {
    alignas(16) uint8_t object[0x170];
    Fake fakes[16];
    uint32_t size;
};

struct ClassSpec {
    const char *name;
    uint32_t size;
    uint32_t vtable;          // the class's vtable RVA
    uint32_t destructRva;     // the destructor body
    uint32_t deleteRva;       // the deleting destructor (slot 3)
    uint32_t poolFreeRva;     // the free-list push, 0 for the base
    uint32_t poolHeadRva;
    RefField extra[2];        // the class's own references
    int extraCount;
    void (*destruct)(void *);
    void *(*deleting)(void *, uint32_t);
    void (*poolFree)(void *);
};

template <typename T> void DestructOf(void *p)
{
    static_cast<T *>(p)->Destruct();
}
template <typename T> void *DeletingOf(void *p, uint32_t flags)
{
    return static_cast<T *>(p)->DeletingDestructor(flags);
}
template <typename T> void PoolFreeOf(void *p)
{
    T::PoolFree(p);
}
void NoPoolFree(void *) {}

const ClassSpec kClasses[] = {
    {"cChannelUber", 0xc4, 0xe67c44, 0xa264a0, 0xa266a0, 0, 0, {}, 0, &DestructOf<cChannelUber>, &DeletingOf<cChannelUber>,
     &NoPoolFree},
    {"cChannelUberQ", 0x164, 0xe6431c, 0x9eef80, 0x9ef0d0, 0x9ef0b0, cChannelUberQ::kPoolHeadRva,
     {{0xc4, true}, {0x150, false}}, 2, &DestructOf<cChannelUberQ>, &DeletingOf<cChannelUberQ>,
     &PoolFreeOf<cChannelUberQ>},
    {"cChannelUberXYZ", 0xd4, 0xe64354, 0x9efe60, 0x9eff80, 0x9eff60, cChannelUberXYZ::kPoolHeadRva, {{0xc4, true}}, 1,
     &DestructOf<cChannelUberXYZ>, &DeletingOf<cChannelUberXYZ>, &PoolFreeOf<cChannelUberXYZ>},
    {"cChannelUberAttribute", 0xdc, 0xe6438c, 0x9f08f0, 0x9f0a10, 0x9f09f0, cChannelUberAttribute::kPoolHeadRva,
     {{0xc8, true}}, 1, &DestructOf<cChannelUberAttribute>, &DeletingOf<cChannelUberAttribute>,
     &PoolFreeOf<cChannelUberAttribute>},
};

// Random object bytes, then each reference field either null or a fake whose id is the field's offset.
void SetupDtorWorld(DtorWorld &w, uint8_t *object, const ClassSpec &spec, uint32_t seed)
{
    std::mt19937 rng(seed);
    for (uint32_t i = 0; i < spec.size; ++i) object[i] = static_cast<uint8_t>(rng());
    int next = 0;
    auto place = [&](const RefField &f) {
        if (rng() % 4 == 0) {
            StoreWord(object + f.offset, 0);
            return;
        }
        Fake &fake = w.fakes[next++];
        InitFake(fake, f.offset);
        StoreWord(object + f.offset, reinterpret_cast<uintptr_t>(&fake));
    };
    for (const RefField &f : kBaseRefs) place(f);
    for (int i = 0; i < spec.extraCount; ++i) place(spec.extra[i]);
    w.size = spec.size;
}

// The object with each reference field replaced by its fake's id (or 0), so two sides compare equal.
std::vector<uint8_t> NormalizedObject(const uint8_t *object, const ClassSpec &spec)
{
    std::vector<uint8_t> bytes(object, object + spec.size);
    auto norm = [&](const RefField &f) {
        uintptr_t p;
        std::memcpy(&p, bytes.data() + f.offset, sizeof(p));
        uint32_t id = 0;
        if (p != 0) std::memcpy(&id, reinterpret_cast<const uint8_t *>(p) + 0x10, sizeof(id));
        std::memcpy(bytes.data() + f.offset, &id, sizeof(id));
    };
    for (const RefField &f : kBaseRefs) norm(f);
    for (int i = 0; i < spec.extraCount; ++i) norm(spec.extra[i]);
    return bytes;
}

} // namespace

DIFF_TEST(AnimChannels_Destructors, 0xa264a0)
{
    static DtorWorld a, b;
    std::mt19937 rng(0x3c01);
    for (const ClassSpec &spec : kClasses) {
        auto orig = ctx.Original<void(__thiscall *)(void *)>(spec.destructRva);
        for (int i = 0; i < 1500; ++i) {
            const uint32_t seed = static_cast<uint32_t>(rng());
            SetupDtorWorld(a, a.object, spec, seed);
            SetupDtorWorld(b, b.object, spec, seed);
            gEv.clear();
            orig(a.object);
            const std::vector<Ev> evA = gEv;
            gEv.clear();
            spec.destruct(b.object);
            ++ctx.cases;
            if (evA != gEv) ctx.Fail("%s Destruct (seed %08x): release calls differ (original %zu, ours %zu)", spec.name, seed, evA.size(), gEv.size());
            if (NormalizedObject(a.object, spec) != NormalizedObject(b.object, spec)) {
                ctx.Fail("%s Destruct (seed %08x): object bytes differ", spec.name, seed);
            }
            const void *vt = *reinterpret_cast<void *const *>(b.object);
            if (vt != ctx.image.At(kClasses[0].vtable)) ctx.Fail("%s Destruct: vtable is not the base class vtable", spec.name);
        }
    }
}

// The free-list push each pooled class uses as operator delete.
DIFF_TEST(AnimChannels_PoolFree, 0x9ef0b0)
{
    std::mt19937 rng(0x3c02);
    for (const ClassSpec &spec : kClasses) {
        if (spec.poolFreeRva == 0) continue;
        auto orig = ctx.Original<void(__cdecl *)(void *)>(spec.poolFreeRva);
        uint32_t *head = static_cast<uint32_t *>(ctx.image.At(spec.poolHeadRva));
        const uint32_t saved = *head;
        for (int i = 0; i < 200; ++i) {
            uint32_t blockA[4] = {1, 2, 3, 4}, blockB[4] = {1, 2, 3, 4};
            const uint32_t start = static_cast<uint32_t>(rng());
            void *pa = i % 10 == 0 ? nullptr : blockA;
            void *pb = i % 10 == 0 ? nullptr : blockB;
            *head = start;
            orig(pa);
            const uint32_t headA = *head;
            const uint32_t nextA = blockA[0];
            *head = start;
            spec.poolFree(pb);
            const uint32_t headB = *head;
            ++ctx.cases;
            if (pa == nullptr) {
                if (headA != start || headB != start) ctx.Fail("%s PoolFree(null) changed the head", spec.name);
            } else {
                if (headA != static_cast<uint32_t>(reinterpret_cast<uintptr_t>(blockA)) || headB != static_cast<uint32_t>(reinterpret_cast<uintptr_t>(blockB))) {
                    ctx.Fail("%s PoolFree: head is not the freed block", spec.name);
                }
                if (nextA != start || blockB[0] != start) ctx.Fail("%s PoolFree: block's first word is not the old head", spec.name);
                if (blockA[1] != 2 || blockB[1] != 2 || blockA[3] != 4 || blockB[3] != 4) ctx.Fail("%s PoolFree wrote past the first word", spec.name);
            }
        }
        *head = saved;
    }
}

// The deleting destructors: flag bit 0 frees (the base through operator delete, the pooled classes through the free
// list), bit 2 turns the pooled free into a no-op.
DIFF_TEST(AnimChannels_DeletingDestructors, 0xa266a0)
{
    std::mt19937 rng(0x3c03);
    constexpr uint32_t kStart = 0x00c0ffeeu;
    for (const ClassSpec &spec : kClasses) {
        auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(spec.deleteRva);
        uint32_t *head = spec.poolHeadRva ? static_cast<uint32_t *>(ctx.image.At(spec.poolHeadRva)) : nullptr;
        const uint32_t saved = head ? *head : 0;
        for (int i = 0; i < 1500; ++i) {
            const uint32_t seed = static_cast<uint32_t>(rng());
            const uint32_t flags = i % 5 == 0 ? static_cast<uint32_t>(rng()) : rng() % 8;
            static DtorWorld wa, wb;
            uint8_t *pa = static_cast<uint8_t *>(::operator new(spec.size));
            uint8_t *pb = static_cast<uint8_t *>(::operator new(spec.size));
            SetupDtorWorld(wa, pa, spec, seed);
            SetupDtorWorld(wb, pb, spec, seed);

            if (head) *head = kStart;
            gEv.clear();
            difftest::FreeRecorder recA;
            void *ra = orig(pa, flags);
            recA.Stop();
            const std::vector<Ev> evA = gEv;
            const bool freedA = (flags & 1) && !head && recA.Freed(pa);
            const uint32_t headA = head ? *head : 0;
            const uint32_t firstA = head ? *reinterpret_cast<uint32_t *>(pa) : 0;

            if (head) *head = kStart;
            gEv.clear();
            difftest::FreeRecorder recB;
            void *rb = spec.deleting(pb, flags);
            recB.Stop();
            const std::vector<Ev> evB = gEv;
            const bool freedB = (flags & 1) && !head && recB.Freed(pb);
            const uint32_t headB = head ? *head : 0;
            const uint32_t firstB = head ? *reinterpret_cast<uint32_t *>(pb) : 0;
            ++ctx.cases;

            if (ra != pa || rb != pb) ctx.Fail("%s deleting destructor (flags %x): return value", spec.name, flags);
            if (evA != evB) ctx.Fail("%s deleting destructor (flags %x, seed %08x): release calls differ", spec.name, flags, seed);
            if (!head) {
                if (((flags & 1) != 0) != freedA || freedA != freedB) {
                    ctx.Fail("%s deleting destructor (flags %x): freed original %d ours %d", spec.name, flags, freedA, freedB);
                }
            } else {
                const bool pushed = (flags & 1) && !(flags & 4);
                const uint32_t expectA = pushed ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pa)) : kStart;
                const uint32_t expectB = pushed ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pb)) : kStart;
                if (headA != expectA || headB != expectB) ctx.Fail("%s deleting destructor (flags %x): free list head (original %08x, ours %08x)", spec.name, flags, headA, headB);
                if (pushed && (firstA != kStart || firstB != kStart)) ctx.Fail("%s deleting destructor (flags %x): block's next link", spec.name, flags);
            }
            // Bytes: skip the first word when the block was pushed on the free list (or the block was freed).
            if (!(flags & 1) || head) {
                std::vector<uint8_t> na = NormalizedObject(pa, spec), nb = NormalizedObject(pb, spec);
                if (head && (flags & 1) && !(flags & 4)) {
                    std::memset(na.data(), 0, 4);
                    std::memset(nb.data(), 0, 4);
                }
                if (na != nb) ctx.Fail("%s deleting destructor (flags %x, seed %08x): object bytes differ", spec.name, flags, seed);
                if (!(flags & 1) || (flags & 4)) {
                    if (*reinterpret_cast<void *const *>(pb) != ctx.image.At(kClasses[0].vtable)) ctx.Fail("%s deleting destructor: vtable is not the base class vtable", spec.name);
                }
            }
            if (!(flags & 1)) {
                ::operator delete(pa);
                ::operator delete(pb);
            } else if (head) {
                ::operator delete(pa);
                ::operator delete(pb);
            }
        }
        if (head) *head = saved;
    }
}

// ---- Weight evaluator, attribute slots -------------------------------------------------------------------------------

namespace {

struct EvalWorld {
    alignas(16) cChannelUberAttribute ch;
    Fake target, context, found, inner, f2c, f4c, f28, f20, ref, f14;
    float src[8];
    float dest[24];
    float blend;
    bool hasBlend;
    float record[24]; // slot 7 and 8 write at +0x28 bytes
};

constexpr int kModes[] = {-1, 0, 1, 2, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 6, 7, 8, 9, 9, 10};

void SetupEval(EvalWorld &w, uint32_t seed)
{
    std::mt19937 rng(seed);
    uint8_t *bytes = reinterpret_cast<uint8_t *>(&w.ch);
    for (size_t i = 0; i < sizeof(w.ch); ++i) bytes[i] = static_cast<uint8_t>(rng());
    InitFake(w.target, 1);
    InitFake(w.context, 2);
    InitFake(w.found, 3);
    InitFake(w.inner, 4);
    InitFake(w.f2c, 5);
    InitFake(w.f4c, 6);
    InitFake(w.f28, 7);
    InitFake(w.f20, 8);
    InitFake(w.ref, 9);
    InitFake(w.f14, 10);
    for (float &f : w.src) f = PickFloat(rng);
    for (float &f : w.dest) f = FromBits(static_cast<uint32_t>(rng()));
    for (float &f : w.record) f = FromBits(static_cast<uint32_t>(rng()));
    w.blend = FromBits(static_cast<uint32_t>(rng()));
    w.hasBlend = rng() % 3 != 0;

    cChannelUberAttribute &c = w.ch;
    c.mode_ = kModes[rng() % (sizeof(kModes) / sizeof(kModes[0]))];
    c.flag30_ = static_cast<uint8_t>(rng() % 2);
    c.flag8c_ = static_cast<uint8_t>(rng() % 2);
    c.index90_ = static_cast<int32_t>(rng() % 8);
    c.count_ = static_cast<int32_t>(rng() % 5);
    for (float &f : c.values_) f = PickFloat(rng);

    // Target: null in a tenth of the trials; its context slot returns the context or null.
    c.target_ = rng() % 10 == 0 ? nullptr : &w.target;
    // The original uses the context without a null check in mode 4 (Fetch), so mode 4 always has one.
    w.target.ptrValue = (rng() % 4 == 0 && c.mode_ != 4) ? nullptr : &w.context;
    // A QueryInterface that succeeds stores an object (the original dereferences it unchecked); a failing one may
    // still store one, which is released.
    w.target.boolValue = rng() % 4 != 0;
    w.target.qiValue = (w.target.boolValue && rng() % 4 != 0) || rng() % 2 ? &w.found : nullptr;
    if (w.target.boolValue) w.target.qiValue = &w.found;
    w.found.ptrValue = rng() % 4 == 0 ? nullptr : &w.inner;  // slot 8 of the found interface
    w.inner.fValue = PickFloat(rng);
    w.context.fValue = PickFloat(rng);
    w.context.kValue = PickFloat(rng);
    w.f28.fValue = PickFloat(rng); // the curve
    w.f2c.boolValue = true;
    w.f4c.boolValue = true;
    w.f20.boolValue = rng() % 4 != 0;
    w.ref.data = w.src;

    c.ref2c_ = rng() % 5 == 0 ? nullptr : &w.f2c;
    c.ref4c_ = rng() % 4 == 0 ? nullptr : &w.f4c;
    c.ref28_ = &w.f28;
    c.ref20_ = &w.f20;
    c.ref_ = &w.ref;
    c.ref14_ = rng() % 3 == 0 ? nullptr : &w.f14;
}

std::vector<uint8_t> RawBytes(const EvalWorld &w)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&w.ch);
    return std::vector<uint8_t>(p, p + sizeof(w.ch));
}

template <typename T> std::vector<uint32_t> BitsOf(const T *p, size_t n)
{
    std::vector<uint32_t> v(n);
    std::memcpy(v.data(), p, n * sizeof(uint32_t));
    return v;
}

} // namespace

DIFF_TEST(AnimChannels_ChannelUber_Evaluate, 0xa267b0)
{
    static EvalWorld a, b;
    auto orig = ctx.Original<float(__thiscall *)(void *, float *)>(0xa267b0);
    std::mt19937 rng(0x3c04);
    for (int i = 0; i < 20000; ++i) {
        const uint32_t seed = static_cast<uint32_t>(rng());
        SetupEval(a, seed);
        SetupEval(b, seed);
        const std::vector<uint8_t> beforeA = RawBytes(a), beforeB = RawBytes(b);
        gBase = &a.ch;
        gEv.clear();
        const float ra = orig(&a.ch, a.hasBlend ? &a.blend : nullptr);
        const std::vector<Ev> evA = gEv;
        gBase = &b.ch;
        gEv.clear();
        const float rb = b.ch.Evaluate(b.hasBlend ? &b.blend : nullptr);
        ++ctx.cases;
        if (Bits(ra) != Bits(rb)) ctx.Fail("Evaluate (seed %08x, mode %d): result original %08x ours %08x", seed, a.ch.mode_, Bits(ra), Bits(rb));
        if (Bits(a.blend) != Bits(b.blend)) ctx.Fail("Evaluate (seed %08x, mode %d): *blend original %08x ours %08x", seed, a.ch.mode_, Bits(a.blend), Bits(b.blend));
        if (evA != gEv) ctx.Fail("Evaluate (seed %08x, mode %d): calls differ (original %zu, ours %zu)", seed, a.ch.mode_, evA.size(), gEv.size());
        if (RawBytes(a) != beforeA || RawBytes(b) != beforeB) ctx.Fail("Evaluate (seed %08x): channel modified", seed);
    }
}

DIFF_TEST(AnimChannels_ChannelUberAttribute_Fetch, 0x9f0a50)
{
    static EvalWorld a, b;
    auto orig = ctx.Original<bool(__thiscall *)(void *, float, float *)>(0x9f0a50);
    std::mt19937 rng(0x3c05);
    for (int i = 0; i < 20000; ++i) {
        const uint32_t seed = static_cast<uint32_t>(rng());
        SetupEval(a, seed);
        SetupEval(b, seed);
        const float time = PickFloat(rng);
        const std::vector<uint8_t> beforeA = RawBytes(a), beforeB = RawBytes(b);
        gBase = &a.ch;
        gEv.clear();
        const bool ra = orig(&a.ch, time, a.dest);
        const std::vector<Ev> evA = gEv;
        gBase = &b.ch;
        gEv.clear();
        const bool rb = b.ch.Fetch(time, b.dest);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("Fetch (seed %08x, mode %d, count %d): result original %d ours %d", seed, a.ch.mode_, a.ch.count_, ra, rb);
        if (BitsOf(a.dest, 24) != BitsOf(b.dest, 24)) ctx.Fail("Fetch (seed %08x, mode %d, count %d): destination differs", seed, a.ch.mode_, a.ch.count_);
        if (evA != gEv) ctx.Fail("Fetch (seed %08x, mode %d): calls differ (original %zu, ours %zu)", seed, a.ch.mode_, evA.size(), gEv.size());
        if (RawBytes(a) != beforeA || RawBytes(b) != beforeB) ctx.Fail("Fetch (seed %08x): channel modified", seed);
    }
}

DIFF_TEST(AnimChannels_ChannelUberAttribute_Slot7, 0x9f0bb0)
{
    static EvalWorld a, b;
    auto orig = ctx.Original<float(__thiscall *)(void *, float, uint8_t *, float *)>(0x9f0bb0);
    std::mt19937 rng(0x3c06);
    for (int i = 0; i < 20000; ++i) {
        const uint32_t seed = static_cast<uint32_t>(rng());
        SetupEval(a, seed);
        SetupEval(b, seed);
        // The record is read as bytes: slot 7 and 8 write the values at +0x28.
        alignas(4) uint8_t recA[0x28 + 16 * 4], recB[0x28 + 16 * 4];
        for (int k = 0; k < static_cast<int>(sizeof(recA)); ++k) recA[k] = recB[k] = static_cast<uint8_t>(seed >> (k % 24)) ^ static_cast<uint8_t>(k);
        const float time = PickFloat(rng);
        gBase = &a.ch;
        gEv.clear();
        const float ra = orig(&a.ch, time, recA, a.hasBlend ? &a.blend : nullptr);
        const std::vector<Ev> evA = gEv;
        gBase = &b.ch;
        gEv.clear();
        const float rb = b.ch.Slot7(time, recB, b.hasBlend ? &b.blend : nullptr);
        ++ctx.cases;
        if (Bits(ra) != Bits(rb)) ctx.Fail("Slot7 (seed %08x, mode %d): result original %08x ours %08x", seed, a.ch.mode_, Bits(ra), Bits(rb));
        if (Bits(a.blend) != Bits(b.blend)) ctx.Fail("Slot7 (seed %08x): *blend differs", seed);
        if (std::memcmp(recA, recB, sizeof(recA)) != 0) ctx.Fail("Slot7 (seed %08x, mode %d): record differs", seed, a.ch.mode_);
        if (evA != gEv) ctx.Fail("Slot7 (seed %08x, mode %d): calls differ (original %zu, ours %zu)", seed, a.ch.mode_, evA.size(), gEv.size());
    }
}

DIFF_TEST(AnimChannels_ChannelUberAttribute_Slot8, 0x9f0b60)
{
    static EvalWorld a, b;
    auto orig = ctx.Original<float(__thiscall *)(void *, float, uint8_t *)>(0x9f0b60);
    std::mt19937 rng(0x3c07);
    for (int i = 0; i < 20000; ++i) {
        const uint32_t seed = static_cast<uint32_t>(rng());
        SetupEval(a, seed);
        SetupEval(b, seed);
        alignas(4) uint8_t recA[0x28 + 16 * 4], recB[0x28 + 16 * 4];
        for (int k = 0; k < static_cast<int>(sizeof(recA)); ++k) recA[k] = recB[k] = static_cast<uint8_t>(seed >> (k % 24)) ^ static_cast<uint8_t>(k);
        const float time = PickFloat(rng);
        gBase = &a.ch;
        gEv.clear();
        const float ra = orig(&a.ch, time, recA);
        const std::vector<Ev> evA = gEv;
        gBase = &b.ch;
        gEv.clear();
        const float rb = b.ch.Slot8(time, recB);
        ++ctx.cases;
        if (Bits(ra) != Bits(rb)) ctx.Fail("Slot8 (seed %08x, mode %d): result original %08x ours %08x", seed, a.ch.mode_, Bits(ra), Bits(rb));
        if (std::memcmp(recA, recB, sizeof(recA)) != 0) ctx.Fail("Slot8 (seed %08x, mode %d): record differs", seed, a.ch.mode_);
        if (evA != gEv) ctx.Fail("Slot8 (seed %08x, mode %d): calls differ (original %zu, ours %zu)", seed, a.ch.mode_, evA.size(), gEv.size());
    }
}

// ---- Reference counting slots (shared bodies) ------------------------------------------------------------------------

namespace {

void *__fastcall FkDelete(uint8_t *self, void *, uint32_t flags)
{
    gEv.push_back({kEvDelete, 0, flags, 0});
    return self;
}

// An object as the reference-count slots see it: the vtable word and the count.
struct CountedObject {
    const void *vt;
    uint32_t refCount;
    uint8_t rest[0x40];
};

} // namespace

DIFF_TEST(AnimChannels_ChannelUber_Release, 0x2386b0)
{
    static const void *vt[16];
    vt[3] = reinterpret_cast<const void *>(&FkDelete);
    const uint32_t rvas[] = {0x2386b0, 0x9efd40};
    std::mt19937 rng(0x3c08);
    for (uint32_t rva : rvas) {
        auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(rva);
        for (int i = 0; i < 400; ++i) {
            static const uint32_t counts[] = {0, 1, 2, 3, 0xffffffffu, 0x80000000u};
            const uint32_t count = i % 3 == 0 ? static_cast<uint32_t>(rng()) : counts[rng() % 6];
            CountedObject a, b;
            std::memset(&a, 0x5a, sizeof(a));
            std::memset(&b, 0x5a, sizeof(b));
            a.vt = b.vt = vt;
            a.refCount = b.refCount = count;
            gEv.clear();
            const uint32_t ra = orig(&a);
            const std::vector<Ev> evA = gEv;
            gEv.clear();
            const uint32_t rb = reinterpret_cast<cChannelUber *>(&b)->Release();
            ++ctx.cases;
            if (ra != rb) ctx.Fail("Release (%x, count %u): result original %u ours %u", rva, count, ra, rb);
            if (a.refCount != b.refCount) ctx.Fail("Release (%x, count %u): count after original %u ours %u", rva, count, a.refCount, b.refCount);
            if (evA != gEv) ctx.Fail("Release (%x, count %u): delete calls differ (original %zu, ours %zu)", rva, count, evA.size(), gEv.size());
            if (std::memcmp(a.rest, b.rest, sizeof(a.rest)) != 0) ctx.Fail("Release: object bytes differ");
            if ((count == 1) != (!evA.empty())) ctx.Fail("Release (count %u): deleting destructor called wrongly", count);
        }
    }
}

DIFF_TEST(AnimChannels_ChannelUber_AddRef, 0x146a50)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x146a50);
    std::mt19937 rng(0x3c09);
    for (int i = 0; i < 400; ++i) {
        const uint32_t count = i % 4 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 4);
        CountedObject a, b;
        std::memset(&a, 0x5a, sizeof(a));
        std::memset(&b, 0x5a, sizeof(b));
        a.refCount = b.refCount = count;
        const uint32_t ra = orig(&a);
        const uint32_t rb = reinterpret_cast<cChannelUber *>(&b)->AddRef();
        ++ctx.cases;
        if (ra != rb || a.refCount != b.refCount) ctx.Fail("AddRef (count %u): result original %u ours %u, count %u/%u", count, ra, rb, a.refCount, b.refCount);
        if (std::memcmp(a.rest, b.rest, sizeof(a.rest)) != 0) ctx.Fail("AddRef: object bytes differ");
    }
}

DIFF_TEST(AnimChannels_ReturnThis, 0x1413e0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x1413e0);
    uint8_t buf[16] = {};
    for (int i = 0; i < 16; ++i) {
        void *p = buf + i;
        ++ctx.cases;
        if (orig(p) != p) ctx.Fail("original does not return this");
        if (reinterpret_cast<cChannelUberQ *>(p)->ReturnThis() != p) ctx.Fail("ReturnThis does not return this");
    }
}

// ---- cEventKeyConst ---------------------------------------------------------------------------------------------------

DIFF_TEST(AnimChannels_EventKeyConst_Nops, 0x300a0)
{
    // 0x300a0 (ret 4) and 0x1b9a50 (ret 8) do nothing and pop their stack arguments: a wrong pop count would unbalance
    // the stack and crash the run. Called with this and the arguments set to distinct values.
    auto orig4 = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x300a0);
    auto orig8 = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0x1b9a50);
    auto ours4 = reinterpret_cast<void(__thiscall *)(void *, uint32_t)>(Addr(&cEventKeyConst::Nop4));
    auto ours8 = reinterpret_cast<void(__thiscall *)(void *, uint32_t, uint32_t)>(Addr(&cEventKeyConst::Nop8));
    uint8_t objA[32], objB[32];
    std::mt19937 rng(0x3c0a);
    for (int i = 0; i < 100; ++i) {
        for (int k = 0; k < 32; ++k) objA[k] = objB[k] = static_cast<uint8_t>(rng());
        orig4(objA, static_cast<uint32_t>(rng()));
        ours4(objB, static_cast<uint32_t>(rng()));
        orig8(objA, static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()));
        ours8(objB, static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()));
        ++ctx.cases;
        if (std::memcmp(objA, objB, 32) != 0) ctx.Fail("a no-op slot changed the object");
    }
}

DIFF_TEST(AnimChannels_EventKeyConst_Getters, 0xa29240)
{
    auto origF = ctx.Original<float(__thiscall *)(void *)>(0xa29240);
    auto orig12 = ctx.Original<int32_t(__thiscall *)(void *)>(0xa29260);
    auto orig13 = ctx.Original<uint32_t(__thiscall *)(void *)>(0xa29230);
    std::mt19937 rng(0x3c0b);
    for (int i = 0; i < 2000; ++i) {
        alignas(4) uint8_t bytes[24];
        for (auto &x : bytes) x = static_cast<uint8_t>(rng());
        if (i % 7 == 0) { // the word edges
            bytes[4] = bytes[5] = bytes[6] = bytes[7] = (i % 2) ? 0xff : 0x00;
            if (i % 3 == 0) bytes[5] = bytes[7] = 0x80;
        }
        cEventKeyConst *o = reinterpret_cast<cEventKeyConst *>(bytes);
        ++ctx.cases;
        const float fa = origF(bytes), fb = o->GetFloat11();
        if (Bits(fa) != Bits(fb)) ctx.Fail("GetFloat11: original %08x ours %08x", Bits(fa), Bits(fb));
        if (orig12(bytes) != o->GetSigned12()) ctx.Fail("GetSigned12: original %d ours %d", orig12(bytes), o->GetSigned12());
        if (orig13(bytes) != o->GetDword13()) ctx.Fail("GetDword13: original %08x ours %08x", orig13(bytes), o->GetDword13());
    }
}

DIFF_TEST(AnimChannels_EventKeyConst_ConstructDestruct, 0xa29140)
{
    auto origCtor = ctx.Original<void *(__thiscall *)(void *)>(0xa29140);
    auto origDtor = ctx.Original<void(__thiscall *)(void *)>(0xa29150);
    std::mt19937 rng(0x3c0c);
    for (int i = 0; i < 200; ++i) {
        alignas(4) uint8_t a[32], b[32], c[32], d[32];
        for (int k = 0; k < 32; ++k) a[k] = b[k] = c[k] = d[k] = static_cast<uint8_t>(rng());
        void *ra = origCtor(a);
        void *rb = reinterpret_cast<cEventKeyConst *>(b)->Construct();
        origDtor(c);
        reinterpret_cast<cEventKeyConst *>(d)->Destruct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("Construct: return value");
        if (std::memcmp(a, b, 32) != 0) ctx.Fail("Construct: object bytes differ");
        if (std::memcmp(c, d, 32) != 0) ctx.Fail("Destruct: object bytes differ");
        if (*reinterpret_cast<void *const *>(b) != ctx.image.At(cEventKeyConst::kVTable)) ctx.Fail("Construct: vtable");
    }
}

DIFF_TEST(AnimChannels_EventKeyConst_GetTypeName, 0xa29270)
{
    HANDLE heap = GetProcessHeap();
    auto orig = ctx.Original<cEventKeyConst::ClassNameResult *(__thiscall *)(void *, cEventKeyConst::ClassNameResult *)>(0xa29270);
    uint8_t self[16] = {};
    for (int i = 0; i < 50; ++i) {
        cEventKeyConst::ClassNameResult a = {reinterpret_cast<void *>(0x11111111), 0x22222222, 0x33333333};
        cEventKeyConst::ClassNameResult b = a;
        auto *ra = orig(self, &a);
        auto *rb = reinterpret_cast<cEventKeyConst *>(self)->GetTypeName(&b);
        ++ctx.cases;
        if (ra != &a || rb != &b) ctx.Fail("GetTypeName: return value");
        if (a.typeId != b.typeId || a.count != b.count) ctx.Fail("GetTypeName: id or count differ");
        const uint8_t *sa = static_cast<const uint8_t *>(a.name), *sb = static_cast<const uint8_t *>(b.name);
        if (!sa || !sb || !HeapValidate(heap, 0, const_cast<uint8_t *>(sa)) || !HeapValidate(heap, 0, const_cast<uint8_t *>(sb))) {
            ctx.Fail("GetTypeName: string block missing");
        } else {
            // 0x20 bytes: vtable, characters, size, capacity, reference count (changed by the AddRef the call makes).
            if (std::memcmp(sa, sb, 0x20) != 0) ctx.Fail("GetTypeName: string object differs");
            if (HeapSize(heap, 0, sa) != HeapSize(heap, 0, sb)) ctx.Fail("GetTypeName: block sizes differ");
            if (*reinterpret_cast<const void *const *>(sb) != ctx.image.At(0xd9b470)) ctx.Fail("GetTypeName: string vtable");
            if (std::strcmp(reinterpret_cast<const char *>(sb + 4), "cEventKeyConst") != 0) ctx.Fail("GetTypeName: text");
        }
        ::operator delete(a.name);
        ::operator delete(b.name);
    }
}
