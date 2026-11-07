// Differential tests for nTSCASCore::cTSCASBodyPartSystem's CopyFrom (slot 29) and ApplyPartGuid (slot 37), lane JA.
// The source object, the skin base filter and the skin catalog are fakes answering from a script (a hash of the
// case's seed and the number of calls recorded so far), so both sides get the same answers while they make the same
// calls. See cas_body_part_system.h for the fixture.
#include "cas_body_part_system.h"
#include "simantics/ServiceRefVector.h"

#include <string>

namespace {

using namespace bpt;

using ServiceString = std::basic_string<char, std::char_traits<char>, nTSCASCore::CasAlloc<char>>;

uint32_t gScriptSeed;

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

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

constexpr int kVSlots = 64;
using VSlots = std::array<const void *, kVSlots>;

// ---- CopyFrom (slot 29) --------------------------------------------------------------------------------------------

struct Source {
    const void *const *vtable;
};
Source gSource;
uint32_t gGuids[2][4]; // what the source's slots 30 and 31 hand out
uint8_t gSourceData[8][8]; // what its pointer getters hand out (the addresses are recorded as indexes)

uint32_t DataIndex(const void *p)
{
    for (int i = 0; i < 8; ++i)
        if (p == gSourceData[i]) return 0x100u + i;
    return reinterpret_cast<uintptr_t>(p) == 0 ? 0 : 0xdead;
}

int32_t __fastcall SrcSlot24(Source *, void *)
{
    gEvents.push_back({0x2018});
    return Pick() % 5 == 0 ? 1 : static_cast<int32_t>(Pick() % 4) + 2;
}
template <int kSlot> void *__fastcall SrcPointer(Source *, void *)
{
    gEvents.push_back({0x2000u + kSlot});
    return gSourceData[(kSlot + Pick()) % 8];
}
uint32_t __fastcall SrcWord(Source *, void *)
{
    gEvents.push_back({0x2021});
    return Pick();
}
const void *__fastcall SrcKey(Source *, void *, uint32_t index)
{
    gEvents.push_back({0x2020, index});
    return gSourceData[index + (Pick() % 2) * 4];
}
template <int kWhich> const uint32_t *__fastcall SrcGuid(Source *, void *)
{
    gEvents.push_back({0x201e + kWhich});
    return gGuids[kWhich];
}

// 0x4fdd30: the GUID as text, into the service string.
void __fastcall F_4fdd30(const uint32_t *guid, void *, ServiceString *out)
{
    gEvents.push_back({0x4fdd30, guid[0], guid[1], guid[2], guid[3]});
    static const char *const kTexts[] = {"", "abc", "0123456789abcdef-long-enough-for-the-heap"};
    *out = kTexts[Pick() % 3];
}

template <int kSlot> void __fastcall SelfVoid(void *self, void *)
{
    gEvents.push_back({static_cast<uint32_t>(kSlot), Rel(self)});
}
template <int kSlot> void __fastcall SelfPointer(void *self, void *, const void *p)
{
    uint32_t words[4] = {};
    const uint32_t index = DataIndex(p);
    if (index == 0xdead) std::memcpy(words, p, 16); // a GUID: its value
    gEvents.push_back({static_cast<uint32_t>(kSlot), Rel(self), index, words[0], words[1], words[2], words[3]});
}
void __fastcall SelfWord(void *self, void *, uint32_t value)
{
    gEvents.push_back({60, Rel(self), value});
}
void __fastcall SelfKey(void *self, void *, uint32_t index, const void *key)
{
    gEvents.push_back({58, Rel(self), index, DataIndex(key)});
}
void __fastcall SelfText(void *self, void *, const char *text, uint32_t a, uint32_t b)
{
    gEvents.push_back({38, Rel(self), TextHash(text), a, b});
}

VSlots gSourceVt;

VTable CopyVTableFakes(const difftest::Context &ctx)
{
    VTable v = CopyVTable(ctx);
    v.slots[16] = reinterpret_cast<const void *>(&SelfVoid<16>);
    v.slots[54] = reinterpret_cast<const void *>(&SelfPointer<54>);
    v.slots[74] = reinterpret_cast<const void *>(&SelfPointer<74>);
    v.slots[56] = reinterpret_cast<const void *>(&SelfPointer<56>);
    v.slots[60] = reinterpret_cast<const void *>(&SelfWord);
    v.slots[58] = reinterpret_cast<const void *>(&SelfKey);
    v.slots[35] = reinterpret_cast<const void *>(&SelfPointer<35>);
    v.slots[41] = reinterpret_cast<const void *>(&SelfPointer<41>);
    v.slots[43] = reinterpret_cast<const void *>(&SelfPointer<43>);
    v.slots[45] = reinterpret_cast<const void *>(&SelfPointer<45>);
    v.slots[33] = reinterpret_cast<const void *>(&SelfPointer<33>);
    v.slots[38] = reinterpret_cast<const void *>(&SelfText);
    gSourceVt = {};
    gSourceVt[24] = reinterpret_cast<const void *>(&SrcSlot24);
    gSourceVt[30] = reinterpret_cast<const void *>(&SrcGuid<0>);
    gSourceVt[31] = reinterpret_cast<const void *>(&SrcGuid<1>);
    gSourceVt[32] = reinterpret_cast<const void *>(&SrcKey);
    gSourceVt[33] = reinterpret_cast<const void *>(&SrcWord);
    gSourceVt[34] = reinterpret_cast<const void *>(&SrcPointer<34>);
    gSourceVt[35] = reinterpret_cast<const void *>(&SrcPointer<35>);
    gSourceVt[36] = reinterpret_cast<const void *>(&SrcPointer<36>);
    gSourceVt[37] = reinterpret_cast<const void *>(&SrcPointer<37>);
    gSourceVt[38] = reinterpret_cast<const void *>(&SrcPointer<38>);
    gSource.vtable = gSourceVt.data();
    return v;
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_CopyFrom, 0xcf8700)
{
    std::mt19937 rng(0xcf8700);
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t)>(0xcf8700);
    gEvents.reserve(4096);
    const VTable vt = CopyVTableFakes(ctx);
    Env env(ctx);
    Redirect fake(ctx, 0x4fdd30, reinterpret_cast<const void *>(&F_4fdd30));
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        for (auto &w : gGuids[0]) w = rng() % 3 == 0 ? 0 : static_cast<uint32_t>(rng());
        switch (rng() % 4) {
        case 0: std::memset(gGuids[1], 0, 16); break;               // zero second GUID
        case 1: std::memcpy(gGuids[1], gGuids[0], 16); break;       // the same as the first
        case 2: std::memcpy(gGuids[1], gGuids[0], 16); gGuids[1][rng() % 4] ^= 1u << (rng() % 32); break;
        default: for (auto &w : gGuids[1]) w = static_cast<uint32_t>(rng()); break;
        }
        if (rng() % 3 == 0) gGuids[1][rng() % 4] = 0; // one word zero, the rest not
        uint8_t flags[6];
        for (auto &f : flags) f = rng() % 2 ? 0 : static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1);
        Compare(ctx, "CopyFrom", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), &gSource, flags[0], flags[1], flags[2], flags[3], flags[4], flags[5]);
            else w.S()->CopyFrom(&gSource, flags[0], flags[1], flags[2], flags[3], flags[4], flags[5]);
            return 0;
        });
    }
}

// ---- ApplyPartGuid (slot 37) ---------------------------------------------------------------------------------------

namespace {

struct FakeFilter {
    const void *const *vtable;
    uint8_t body[0x54 - 4];
    uint8_t face[0x10]; // +0x54: what the catalog is handed
};
FakeFilter gFilter;
VSlots gFilterVt, gCatalogVt;
struct FakeCatalog {
    const void *const *vtable;
} gCatalog;

void __fastcall FilterSlot7(FakeFilter *, void *)
{
    gEvents.push_back({0x3007});
}
void __fastcall F_b65750(FakeFilter *self, void *, uint32_t mask)
{
    gEvents.push_back({0xb65750, self == &gFilter, mask});
}
void __fastcall F_b65550(FakeFilter *self, void *, const uint32_t *guid)
{
    gEvents.push_back({0xb65550, self == &gFilter, guid[0], guid[1], guid[2], guid[3]});
}
void __fastcall CatalogSlot21(FakeCatalog *, void *, ServiceBlockVector *out, void *face, uint32_t a, uint32_t b)
{
    gEvents.push_back({0x4015, face == gFilter.face, a, b, out->mFirst == nullptr});
    const uint32_t n = Pick() % 4 == 0 ? 0 : 1 + Pick() % 2;
    if (n == 0) return;
    auto *ids = static_cast<uint32_t *>(ServiceAlloc(4 * n));
    for (uint32_t k = 0; k < n; ++k) ids[k] = 0x1000 + Pick() % 16 + k;
    out->mFirst = ids;
    out->mLast = ids + n;
    out->mEnd = ids + n;
}
uint32_t __cdecl F_cf1c20(uint32_t id, uint32_t *category, uint32_t *value, uint32_t *layer)
{
    gEvents.push_back({0xcf1c20, id, *category, *value, *layer});
    const uint32_t x = Pick();
    *category = x % 4;
    *value = (x >> 4) % 4;
    *layer = (x >> 8) % 3;
    if ((x >> 12) % 8 == 0) *category = 0x12345; // upper bits dropped by the record's 16-bit field
    return (x >> 16) % 4 == 0 ? (x & 0xffffff00u) : ((x & 0xffffff00u) | 1);
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_ApplyPartGuid, 0xcf5a80)
{
    std::mt19937 rng(0xcf5a80);
    auto orig = ctx.Original<void(__thiscall *)(void *, const uint32_t *, uint8_t, uint8_t)>(0xcf5a80);
    gEvents.reserve(4096);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    gFilterVt = {};
    gFilterVt[7] = reinterpret_cast<const void *>(&FilterSlot7);
    gFilter.vtable = gFilterVt.data();
    gCatalogVt = {};
    gCatalogVt[21] = reinterpret_cast<const void *>(&CatalogSlot21);
    gCatalog.vtable = gCatalogVt.data();
    Redirect f1(ctx, 0xb65750, reinterpret_cast<const void *>(&F_b65750));
    Redirect f2(ctx, 0xb65550, reinterpret_cast<const void *>(&F_b65550));
    Redirect f3(ctx, 0xcf1c20, reinterpret_cast<const void *>(&F_cf1c20));
    void **const catalogCache = ctx.image.At<void *>(0x10faf74);
    void *const savedCatalog = *catalogCache;
    *catalogCache = &gCatalog;
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        const uint32_t age = rng() % 3 == 0 ? 0u : static_cast<uint32_t>(rng() % 5);
        std::memcpy(sc.bytes + kIfaceOffset + 0x118, &age, 4);
        FakeFilter *filter = &gFilter;
        std::memcpy(sc.bytes + kIfaceOffset + 0x140, &filter, 4);
        uint32_t guid[4];
        for (auto &w : guid) w = static_cast<uint32_t>(rng());
        const uint8_t on = rng() % 2 ? 0 : static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1);
        const uint8_t asNew = rng() % 3 == 0 ? static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1) : 0;
        Compare(ctx, "ApplyPartGuid", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), guid, on, asNew);
            else w.S()->ApplyPartGuid(guid, on, asNew);
            return 0;
        });
    }
    *catalogCache = savedCatalog;
}
