// Differential tests for the part slots of nTSCASCore::cTSCASBodyPartSystem's interface subobject (lane JA): slots 30,
// 43, 45, 52, 81 and 83. The objects they talk to (parts, their children, scene nodes, the names of a vector) are fakes
// whose answers come from a script: a hash of the scenario's seed and the number of calls recorded so far, so both
// sides get the same answers as long as they make the same calls. See cas_body_part_system.h for the fixture.
#include "cas_body_part_system.h"
#include "simantics/ServiceRefVector.h"

#include <cstdio>

namespace {

using namespace bpt;
using nTSCASCore::StringVector;

uint32_t gScriptSeed;

// The scripted answer of the call just recorded.
uint32_t Pick()
{
    uint32_t x = gScriptSeed ^ (static_cast<uint32_t>(gEvents.size()) * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// A byte answer with random upper bits (callers read al only); zero one time in `zeroOneIn`.
uint32_t PickByte(uint32_t zeroOneIn = 3)
{
    const uint32_t x = Pick();
    if (x % zeroOneIn == 0) return x & 0xffffff00u;
    return (x & 0xffffff00u) | (((x >> 8) % 4 == 0) ? 0x80u : 1u + ((x >> 4) & 0x7eu));
}

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

// ---- Fake objects ----------------------------------------------------------------------------------------------------

// Every fake object starts with its vtable and carries a number the events record instead of its address.
struct FakeObject {
    const void *const *vtable;
    uint32_t id;
};

uint32_t IdOf(const void *p)
{
    if (p == nullptr) return 0;
    return static_cast<const FakeObject *>(p)->id;
}

constexpr int kVSlots = 64;
using VSlots = std::array<const void *, kVSlots>;

// Generic recorders: id = 0x1000 + slot, the receiver's id, then the arguments.
template <int kSlot> uint32_t __fastcall Rec0(void *self, void *)
{
    gEvents.push_back({0x1000u + kSlot, IdOf(self)});
    return 0;
}

// ---- Names (slots 43 / 45): 0x20-byte elements whose slot 5 gives the text ------------------------------------------
struct FakeName {
    const void *const *vtable;
    const char *text;
    uint8_t pad[0x18];
};
static_assert(sizeof(FakeName) == 0x20, "a cRZString's size");

const char *__fastcall NameText(FakeName *self, void *)
{
    gEvents.push_back({0x2005, TextHash(self->text)});
    return self->text;
}
VSlots gNameVt;

void __fastcall Slot17(void *self, void *, int32_t value, int32_t category)
{
    gEvents.push_back({17, Rel(self), static_cast<uint32_t>(value), static_cast<uint32_t>(category)});
}
void __fastcall Slot38(void *self, void *, const char *name, uint32_t a, uint32_t b)
{
    gEvents.push_back({38, Rel(self), TextHash(name), a, b});
}

// ---- Parts, children, child parts (slot 81) ------------------------------------------------------------------------

// A child part: an interface whose cIGZUnknown is a virtual base (vbptr at +4, base offset in the vbtable's 2nd entry).
struct FakeChildPart {
    const void *const *vtable;   // +0 (unused)
    const int32_t *vbptr;        // +4
    uint32_t pad;                // +8
    FakeObject base;             // +0xc: the virtual base, id = the child part's id
};
const int32_t kChildPartVbtable[2] = {-4, 8}; // base = vbptr address + 8

FakeObject gParts[4];
FakeObject gChildren[8];
FakeChildPart gChildParts[8];
FakeObject gSystemFaces[2]; // what the system answers to its QueryInterface
int gChildChain;            // children handed out in the current part

void *__fastcall Slot52(void *self, void *, uint32_t a, uint32_t b, uint32_t id)
{
    gEvents.push_back({52, Rel(self), a, b, id});
    const uint32_t x = Pick();
    if (x % 4 == 0) return nullptr;
    gChildChain = 0;
    return &gParts[x % 4];
}
uint32_t __fastcall Slot82(void *self, void *, uint32_t a, uint32_t b, uint32_t id, void *target)
{
    gEvents.push_back({82, Rel(self), a, b, id, IdOf(target)});
    return PickByte(4);
}

void *NextChild()
{
    const uint32_t x = Pick();
    if (gChildChain >= 4 || x % 3 == 0) return nullptr;
    return &gChildren[gChildChain++];
}
void *__fastcall PartFirstChild(FakeObject *self, void *)
{
    gEvents.push_back({0x3015, self->id});
    return NextChild();
}
void *__fastcall ChildNext(FakeObject *self, void *)
{
    gEvents.push_back({0x4014, self->id});
    return NextChild();
}
uint32_t __fastcall ChildQuery(FakeObject *self, void *, uint32_t iid, void **out)
{
    gEvents.push_back({0x4000, self->id, iid});
    const uint32_t x = Pick();
    const uint32_t answer = PickByte(3);
    // A yes always hands out an object (the original dereferences it); a no sometimes leaves one to release anyway.
    if ((answer & 0xff) != 0 || x % 5 == 0) *out = &gChildParts[self->id % 8];
    return answer;
}
uint32_t __fastcall ChildPartBaseSlot18(FakeObject *self, void *)
{
    gEvents.push_back({0x5012, self->id});
    const uint32_t x = Pick();
    return x % 3 == 0 ? 0u : x;
}
uint32_t __fastcall ChildPartBaseRelease(FakeObject *self, void *)
{
    gEvents.push_back({0x5002, self->id});
    return 0;
}
uint32_t __fastcall SystemQuery(void *self, void *, uint32_t iid, void **out)
{
    gEvents.push_back({0x6000, Rel(self), iid});
    const uint32_t x = Pick();
    const uint32_t answer = PickByte(3);
    if ((answer & 0xff) != 0 || x % 5 == 0) *out = (x >> 3) % 6 == 0 ? nullptr : &gSystemFaces[x % 2];
    return answer;
}
uint32_t __fastcall SystemFaceRelease(FakeObject *self, void *)
{
    gEvents.push_back({0x7002, self->id});
    return 0;
}

// ---- Slot 83: the system's slot-14 object, its scene node ----------------------------------------------------------
FakeObject gSceneObject = {nullptr, 0x51};
FakeObject gNodes[2];

void *__fastcall SystemSlot14(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    gEvents.push_back({0x600e, Rel(self), a, b, c});
    return Pick() % 4 == 0 ? nullptr : &gSceneObject;
}
uint32_t __fastcall SceneAddRef(FakeObject *self, void *)
{
    gEvents.push_back({0x8001, self->id});
    return 0;
}
uint32_t __fastcall SceneRelease(FakeObject *self, void *)
{
    gEvents.push_back({0x8002, self->id});
    return 0;
}
void *__cdecl FindNodeByName(const char *name, void *object, uint32_t type)
{
    gEvents.push_back({0xae7490, TextHash(name), IdOf(object), type});
    const uint32_t x = Pick();
    return x % 3 == 0 ? nullptr : &gNodes[x % 2];
}
float *__fastcall NodeTransform(FakeObject *self, void *, float *out)
{
    gEvents.push_back({0x9035, self->id});
    for (int k = 0; k < 7; ++k) {
        gEvents.push_back({0x9036, static_cast<uint32_t>(k)});
        const uint32_t bits = Pick();
        std::memcpy(&out[k], &bits, 4);
    }
    return out;
}

// ---- Slot 30: the target, the system's helpers ---------------------------------------------------------------------
FakeObject gTarget = {nullptr, 0x77};

uint32_t __fastcall F_ce5830(void *self, void *, void *target, uint32_t a, uint32_t b, uint32_t c, uint32_t f1, uint32_t f2)
{
    gEvents.push_back({0xce5830, Rel(self), IdOf(target), a, b, c, f1, f2});
    return PickByte(5);
}
void __fastcall F_cfa2d0(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    gEvents.push_back({0xcfa2d0, Rel(self), a, b, c, d});
}
const uint8_t *__fastcall F_cf1670(void *self, void *, uint32_t a, uint32_t b, uint32_t id)
{
    gEvents.push_back({0xcf1670, Rel(self), a, b, id});
    static uint8_t entries[2][0x10];
    const uint32_t x = Pick();
    uint8_t *e = entries[x % 2];
    const uint32_t value = Pick() ^ 0x5a5a5a5au;
    std::memcpy(e + 0xc, &value, 4);
    return e;
}

VSlots gPartVt, gChildVt, gChildPartBaseVt, gSystemFaceVt, gSceneVt, gNodeVt, gTargetVt;
std::array<const void *, 15> gSystemVt; // the +0 face

void InitFakes()
{
    gEvents.reserve(4096);
    gNameVt = {};
    gNameVt[5] = reinterpret_cast<const void *>(&NameText);
    gPartVt = {};
    gPartVt[21] = reinterpret_cast<const void *>(&PartFirstChild);
    gChildVt = {};
    gChildVt[0] = reinterpret_cast<const void *>(&ChildQuery);
    gChildVt[20] = reinterpret_cast<const void *>(&ChildNext);
    gChildPartBaseVt = {};
    gChildPartBaseVt[2] = reinterpret_cast<const void *>(&ChildPartBaseRelease);
    gChildPartBaseVt[18] = reinterpret_cast<const void *>(&ChildPartBaseSlot18);
    gSystemFaceVt = {};
    gSystemFaceVt[2] = reinterpret_cast<const void *>(&SystemFaceRelease);
    gSceneVt = {};
    gSceneVt[1] = reinterpret_cast<const void *>(&SceneAddRef);
    gSceneVt[2] = reinterpret_cast<const void *>(&SceneRelease);
    gNodeVt = {};
    gNodeVt[53] = reinterpret_cast<const void *>(&NodeTransform);
    gTargetVt = {};
    gTargetVt[18] = reinterpret_cast<const void *>(&Rec0<18>);
    gSystemVt = {};
    gSystemVt[0] = reinterpret_cast<const void *>(&SystemQuery);
    gSystemVt[14] = reinterpret_cast<const void *>(&SystemSlot14);
    for (int i = 0; i < 4; ++i) gParts[i] = {gPartVt.data(), 0x100u + i};
    for (int i = 0; i < 8; ++i) {
        gChildren[i] = {gChildVt.data(), 0x200u + i};
        gChildParts[i].vtable = nullptr;
        gChildParts[i].vbptr = kChildPartVbtable;
        gChildParts[i].base = {gChildPartBaseVt.data(), 0x300u + i};
    }
    for (int i = 0; i < 2; ++i) gSystemFaces[i] = {gSystemFaceVt.data(), 0x400u + i};
    gSceneObject.vtable = gSceneVt.data();
    for (int i = 0; i < 2; ++i) gNodes[i] = {gNodeVt.data(), 0x500u + i};
    gTarget.vtable = gTargetVt.data();
}

VTable PartsVTable(const difftest::Context &ctx)
{
    VTable v = CopyVTable(ctx);
    v.slots[17] = reinterpret_cast<const void *>(&Slot17);
    v.slots[38] = reinterpret_cast<const void *>(&Slot38);
    v.slots[52] = reinterpret_cast<const void *>(&Slot52);
    v.slots[82] = reinterpret_cast<const void *>(&Slot82);
    return v;
}

constexpr int kIface = kIfaceOffset;

template <typename T> void PutAt(Scenario &sc, int ifaceOffset, T value)
{
    std::memcpy(sc.bytes + kIface + ifaceOffset, &value, sizeof(value));
}

// The three id lists (A +0x60, B +0x6c, C +0x78) in static arrays both sides share; 0 to `maxCount` ids each.
uint32_t gIds[3][8];
void SetLists(Scenario &sc, std::mt19937 &rng, int maxCount)
{
    const int offsets[3] = {0x60, 0x6c, 0x78};
    for (int l = 0; l < 3; ++l) {
        for (auto &id : gIds[l]) id = rng() % 2 ? static_cast<uint32_t>(rng() % 16) : static_cast<uint32_t>(rng());
        const int n = static_cast<int>(rng() % (maxCount + 1));
        PutAt<uint32_t *>(sc, offsets[l], gIds[l]);
        PutAt<uint32_t *>(sc, offsets[l] + 4, gIds[l] + n);
    }
}

void SetAge(Scenario &sc, std::mt19937 &rng)
{
    PutAt<uint32_t>(sc, 0x118, rng() % 3 == 0 ? 0u : (rng() % 2 ? static_cast<uint32_t>(rng() % 5) : static_cast<uint32_t>(rng())));
}

// The +0 face's vtable word (system offset 0) set to the fake system vtable.
void SetSystemVTable(Scenario &sc)
{
    const void *vt = gSystemVt.data();
    std::memcpy(sc.bytes, &vt, sizeof(vt));
}

uint32_t RandomArg(std::mt19937 &rng)
{
    switch (rng() % 3) {
    case 0: return static_cast<uint32_t>(rng() % 4);
    default: return static_cast<uint32_t>(rng());
    }
}

} // namespace

// ---- Slots 20 and 53: parts with ids, linked objects -----------------------------------------------------------------

namespace {

using nTSCASCore::CasGuid;
using nTSCASCore::GuidVector;
using nTSCASCore::IdVector;

FakeObject gEntries[4];   // what a part's slot 48 hands out
FakeObject gLinked[6];    // linked objects (slot 53's results, the links' objects)

uint32_t __fastcall ObjAddRef(FakeObject *self, void *)
{
    gEvents.push_back({0xa001, self->id});
    return 0;
}
uint32_t __fastcall ObjRelease(FakeObject *self, void *)
{
    gEvents.push_back({0xa002, self->id});
    return 0;
}
void __fastcall ObjIds(FakeObject *self, void *, IdVector *out)
{
    gEvents.push_back({0xa038, self->id, static_cast<uint32_t>(out->size())});
    const uint32_t n = Pick() % 5;
    for (uint32_t k = 0; k < n; ++k) {
        gEvents.push_back({0xa039, k});
        const uint32_t x = Pick();
        out->push_back(x % 3 == 0 ? 0u : x % 7);
    }
}
void *__fastcall ObjEntry(FakeObject *self, void *, uint32_t id)
{
    gEvents.push_back({0xa030, self->id, id});
    const uint32_t x = Pick();
    if (self->id >= 0x600 && x % 4 == 0) return nullptr; // only linked objects may lack an entry
    return &gEntries[x % 4];
}
void __fastcall EntryUpdate(FakeObject *self, void *)
{
    gEvents.push_back({0xa022, self->id});
}
uint32_t __fastcall Slot69(void *self, void *)
{
    gEvents.push_back({69, Rel(self)});
    return PickByte(2);
}
void __fastcall Slot53(void *self, void *, uint32_t a, uint32_t b, uint32_t id, ServiceRefVector *objects, IdVector *values,
                       GuidVector *guids)
{
    gEvents.push_back({53, Rel(self), a, b, id, static_cast<uint32_t>(objects->Size()), values != nullptr, guids != nullptr});
    const uint32_t n = Pick() % 4;
    for (uint32_t k = 0; k < n; ++k) {
        gEvents.push_back({0xa053, k});
        objects->PushBack(&gLinked[Pick() % 6]);
    }
}

VSlots gObjVt, gEntryVt;

// The links of the entry 0xcf04b0 hands back (rebuilt per case; the same for both sides).
struct FakeLink {
    uint8_t pad[8];
    uint8_t enabled;
    uint8_t pad9[3];
    void *object;
    uint32_t id;
};
FakeLink gLinks[6];
FakeLink *gLinkPtrs[8];
struct FakeEntry {
    uint8_t pad[0x68];
    FakeLink **first, **last;
} gEntry;

const FakeEntry *__fastcall F_cf04b0(void *self, void *, uint32_t a, uint32_t b, uint32_t id, uint32_t first)
{
    gEvents.push_back({0xcf04b0, Rel(self), a, b, id, first});
    return &gEntry;
}
uint32_t __fastcall F_cfeba0(FakeLink *self, void *)
{
    gEvents.push_back({0xcfeba0, self->id});
    return Pick();
}
int32_t __fastcall F_d025f0(FakeLink *self, void *)
{
    gEvents.push_back({0xd025f0, self->id});
    const uint32_t x = Pick();
    return x % 2 ? 8 : static_cast<int32_t>(x % 10);
}
const CasGuid *__fastcall F_cff0c0(FakeLink *self, void *)
{
    gEvents.push_back({0xcff0c0, self->id});
    static CasGuid g[2];
    CasGuid &out = g[gEvents.size() % 2];
    for (auto &w : out.mWords) w = Pick() + (&w - out.mWords);
    return &out;
}

void InitObjects()
{
    gObjVt = {};
    gObjVt[1] = reinterpret_cast<const void *>(&ObjAddRef);
    gObjVt[2] = reinterpret_cast<const void *>(&ObjRelease);
    gObjVt[48] = reinterpret_cast<const void *>(&ObjEntry);
    gObjVt[56] = reinterpret_cast<const void *>(&ObjIds);
    gEntryVt = {};
    gEntryVt[34] = reinterpret_cast<const void *>(&EntryUpdate);
    for (int i = 0; i < 4; ++i) gParts[i] = {gObjVt.data(), 0x100u + i};
    for (int i = 0; i < 4; ++i) gEntries[i] = {gEntryVt.data(), 0x700u + i};
    for (int i = 0; i < 6; ++i) gLinked[i] = {gObjVt.data(), 0x600u + i};
}

VTable LinkedVTable(const difftest::Context &ctx)
{
    VTable v = PartsVTable(ctx);
    v.slots[53] = reinterpret_cast<const void *>(&Slot53);
    v.slots[69] = reinterpret_cast<const void *>(&Slot69);
    return v;
}

void ServiceFree(void *p)
{
    if (p == nullptr) return;
    void *service = Env::Current()->object;
    auto free = reinterpret_cast<void(__thiscall *)(void *, void *)>((*reinterpret_cast<void ***>(service))[4]);
    free(service, p);
}

void RandomLinks(std::mt19937 &rng)
{
    const int n = static_cast<int>(rng() % 7);
    for (int i = 0; i < 6; ++i) {
        gLinks[i] = {};
        gLinks[i].enabled = rng() % 4 == 0 ? 0 : static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1);
        gLinks[i].object = rng() % 5 == 0 ? nullptr : &gLinked[rng() % 6];
        gLinks[i].id = 0x800u + i;
    }
    for (int i = 0; i < n; ++i) gLinkPtrs[i] = rng() % 6 == 0 ? nullptr : &gLinks[rng() % 6];
    gEntry.first = gLinkPtrs;
    gEntry.last = gLinkPtrs + n;
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_RefreshPart, 0xce32b0)
{
    std::mt19937 rng(0xce32b0);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0xce32b0);
    InitFakes();
    InitObjects();
    const VTable vt = LinkedVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 2500; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        const uint32_t a = RandomArg(rng), b = RandomArg(rng), id = RandomArg(rng);
        Compare(ctx, "RefreshPart", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), a, b, id);
            else w.S()->RefreshPart(a, b, id);
            return 0;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_CollectLinkedParts, 0xcf4130)
{
    std::mt19937 rng(0xcf4130);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, ServiceRefVector *, IdVector *, GuidVector *)>(0xcf4130);
    InitFakes();
    InitObjects();
    const VTable vt = LinkedVTable(ctx);
    Env env(ctx);
    Redirect f1(ctx, 0xcf04b0, reinterpret_cast<const void *>(&F_cf04b0));
    Redirect f2(ctx, 0xcfeba0, reinterpret_cast<const void *>(&F_cfeba0));
    Redirect f3(ctx, 0xd025f0, reinterpret_cast<const void *>(&F_d025f0));
    Redirect f4(ctx, 0xcff0c0, reinterpret_cast<const void *>(&F_cff0c0));
    for (int i = 0; i < 2500; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        RandomLinks(rng);
        const uint32_t a = RandomArg(rng), b = RandomArg(rng), id = RandomArg(rng);
        const int preObjects = static_cast<int>(rng() % 3), spareObjects = static_cast<int>(rng() % 3);
        const bool withValues = rng() % 3 != 0, withGuids = rng() % 3 != 0;
        const int reserveValues = static_cast<int>(rng() % 3), reserveGuids = static_cast<int>(rng() % 3);
        Compare(ctx, "CollectLinkedParts", sc, vt, [&](World &w, bool original) -> uint64_t {
            ServiceRefVector objects{};
            if (preObjects + spareObjects > 0) {
                objects.mFirst = static_cast<void **>(ServiceAlloc(4 * (preObjects + spareObjects)));
                for (int k = 0; k < preObjects; ++k) objects.mFirst[k] = &gLinked[k];
                objects.mLast = objects.mFirst + preObjects;
                objects.mEnd = objects.mLast + spareObjects;
            }
            IdVector values;
            values.reserve(reserveValues);
            GuidVector guids;
            guids.reserve(reserveGuids);
            if (original) orig(w.S(), a, b, id, &objects, withValues ? &values : nullptr, withGuids ? &guids : nullptr);
            else w.S()->CollectLinkedParts(a, b, id, &objects, withValues ? &values : nullptr, withGuids ? &guids : nullptr);
            gEvents.push_back({0xfff0, static_cast<uint32_t>(objects.mLast - objects.mFirst), static_cast<uint32_t>(objects.mEnd - objects.mFirst)});
            for (void **p = objects.mFirst; p != objects.mLast; ++p) gEvents.push_back({0xfff1, IdOf(*p)});
            gEvents.push_back({0xfff2, static_cast<uint32_t>(values.size()), static_cast<uint32_t>(values.capacity())});
            for (uint32_t v : values) gEvents.push_back({0xfff3, v});
            gEvents.push_back({0xfff4, static_cast<uint32_t>(guids.size()), static_cast<uint32_t>(guids.capacity())});
            for (const CasGuid &g : guids) gEvents.push_back({0xfff5, g.mWords[0], g.mWords[1], g.mWords[2], g.mWords[3]});
            ServiceFree(objects.mFirst);
            return 0;
        });
    }
}

// ---- Slots 43 and 45 -----------------------------------------------------------------------------------------------

namespace {

template <typename Ours> void CheckApplyNames(difftest::Context &ctx, const char *label, uint32_t rva, Ours ours)
{
    std::mt19937 rng(rva);
    auto orig = ctx.Original<void(__thiscall *)(void *, const StringVector *)>(rva);
    InitFakes();
    const VTable vt = PartsVTable(ctx);
    Env env(ctx);
    static const char *const kTexts[] = {"", "a", "00000000-0000-0000-0000-000000000000", "face", "x1"};
    static FakeName names[6];
    for (int i = 0; i < 1500; ++i) {
        Scenario sc = RandomScenario(rng);
        SetAge(sc, rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        const int n = i < 6 ? i : static_cast<int>(rng() % 7); // 0..6 names, every count first
        for (int k = 0; k < 6; ++k) names[k] = {gNameVt.data(), kTexts[rng() % 5], {}};
        StringVector v{reinterpret_cast<cRZString *>(names), reinterpret_cast<cRZString *>(names + n),
                       reinterpret_cast<cRZString *>(names + 6)};
        Compare(ctx, label, sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), &v);
            else (w.S()->*ours)(&v);
            return 0;
        });
    }
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_ApplyNamesValue0B, 0xcf80e0)
{
    CheckApplyNames(ctx, "ApplyNamesValue0B", 0xcf80e0, &Iface::ApplyNamesValue0B);
}

DIFF_TEST(cTSCASBodyPartSystem_ApplyNamesValue5, 0xcf8070)
{
    CheckApplyNames(ctx, "ApplyNamesValue5", 0xcf8070, &Iface::ApplyNamesValue5);
}

// ---- Slot 52 -------------------------------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_GetPart, 0xcf43d0)
{
    std::mt19937 rng(0xcf43d0);
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0xcf43d0);
    InitFakes();
    const VTable vt = PartsVTable(ctx);
    Env env(ctx);
    Redirect fake(ctx, 0xcf1670, reinterpret_cast<const void *>(&F_cf1670));
    for (int i = 0; i < 1000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        const uint32_t a = RandomArg(rng), b = RandomArg(rng), id = RandomArg(rng);
        Compare(ctx, "GetPart", sc, vt, [&](World &w, bool original) -> uint64_t {
            return reinterpret_cast<uintptr_t>(original ? orig(w.S(), a, b, id) : w.S()->GetPart(a, b, id));
        });
    }
}

// ---- Slot 30 -------------------------------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_ApplyToAllParts, 0xce5a50)
{
    std::mt19937 rng(0xce5a50);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0xce5a50);
    InitFakes();
    const VTable vt = PartsVTable(ctx);
    Env env(ctx);
    Redirect f1(ctx, 0xce5830, reinterpret_cast<const void *>(&F_ce5830));
    Redirect f2(ctx, 0xcfa2d0, reinterpret_cast<const void *>(&F_cfa2d0));
    for (int i = 0; i < 2500; ++i) {
        Scenario sc = RandomScenario(rng);
        SetLists(sc, rng, i < 200 ? 1 : 3);
        SetAge(sc, rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        void *target = rng() % 8 == 0 ? nullptr : &gTarget;
        Compare(ctx, "ApplyToAllParts", sc, vt, [&](World &w, bool original) -> uint64_t {
            return original ? orig(w.S(), target) : w.S()->ApplyToAllParts(target);
        });
    }
}

// ---- Slot 81 -------------------------------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_ApplyToChildren, 0xce0cc0)
{
    std::mt19937 rng(0xce0cc0);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void *)>(0xce0cc0);
    InitFakes();
    const VTable vt = PartsVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        SetLists(sc, rng, i < 200 ? 1 : 3);
        SetSystemVTable(sc);
        gScriptSeed = static_cast<uint32_t>(rng());
        const uint32_t a = RandomArg(rng), b = RandomArg(rng);
        void *target = rng() % 8 == 0 ? nullptr : &gTarget;
        Compare(ctx, "ApplyToChildren", sc, vt, [&](World &w, bool original) -> uint64_t {
            return original ? orig(w.S(), a, b, target) : w.S()->ApplyToChildren(a, b, target);
        });
    }
}

// ---- Slot 83 -------------------------------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_GetNodeTransform, 0xce3180)
{
    std::mt19937 rng(0xce3180);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, const char *, void *, void *)>(0xce3180);
    InitFakes();
    const VTable vt = PartsVTable(ctx);
    Env env(ctx);
    Redirect fake(ctx, 0xae7490, reinterpret_cast<const void *>(&FindNodeByName));
    static const char *const kNames[] = {"", "head", "neck_joint"};
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        SetSystemVTable(sc);
        gScriptSeed = static_cast<uint32_t>(rng());
        const uint32_t a = RandomArg(rng), b = RandomArg(rng);
        const char *name = kNames[rng() % 3];
        Compare(ctx, "GetNodeTransform", sc, vt, [&](World &w, bool original) -> uint64_t {
            uint8_t out[2][16 + 8];
            std::memset(out, 0xcc, sizeof(out));
            const uint8_t r = original ? orig(w.S(), a, b, name, out[0], out[1]) : (w.S()->GetNodeTransform(a, b, name, out[0], out[1]) ? 1 : 0);
            gEvents.push_back({0xfff0, r});
            for (int k = 0; k < 2; ++k)
                for (int j = 0; j < 24; j += 4) {
                    uint32_t word;
                    std::memcpy(&word, &out[k][j], 4);
                    gEvents.push_back({0xfff1, static_cast<uint32_t>(k), static_cast<uint32_t>(j), word});
                }
            return r;
        });
    }
}
