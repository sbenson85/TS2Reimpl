// Differential test for nTSCASCore::cTSCASBodyPartSystem, lane KB: slot 88 (ReportBones, 0xced540). The caller's string,
// the skin base filter (iface +0x140) and its setters (0xb65750, 0xb654f0, 0xb655f0), the resource catalog (cache
// 0x10faf74, slots 34 / 32) and its items, the part entry lookups (0xcf07a0, 0xcec500, 0xcf04b0), the id names
// (0xb5fd70) and id categories (0xb5fd30) are recording fakes; the lines appended are compared by text.
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <limits>

namespace bpt_bonereport {

using namespace bpt;
using Vt = std::array<const void *, 40>;
using nTSCASCore::CasGuid;
using nTSCASCore::LocalRZString;

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

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

Vt gOutVt, gFilterVt, gCatalogVt, gItemVt, gDescVt;

struct Obj {
    const void *const *vt;
} gOut, gCatalog;
struct Filter {
    const void *const *vt;
    uint8_t pad[0x60];
} gFilter;

// A catalog item: its two GUIDs (slots 10 and 6), flags (slot 9) and description (slot 3: category mask slot 22, name
// slot 13).
struct Item {
    const void *const *vt;
    uint32_t id;
    CasGuid modifierGuid, archetypeGuid;
    uint8_t flags;
    struct Desc {
        const void *const *vt;
        uint32_t id;
        uint32_t mask;
        std::string name;
    } desc;
};

// A bone record as the part entries hold them (0x3c bytes: a cRZString, a position, the rest unread).
struct Bone {
    LocalRZString name;
    float pos[3];
    uint8_t rest[0x10];
    explicit Bone(const char *n) : name(n), pos{}, rest{} {}
};
static_assert(sizeof(Bone) == 0x3c, "Bone layout");

struct Case {
    std::deque<Item> items;
    std::vector<Item *> modifiers, archetypes;
    std::deque<Bone> bones;
    uint8_t entries[4][0x100]; // part entries (0xcf07a0 / 0xcec500)
    uint8_t bases[3][0x40];    // base entries (0xcf04b0)
} gCase;

void __fastcall OutClear(Obj *, void *, uint32_t a, uint32_t b) { gEvents.push_back({0x5026, a, b}); }
void *__fastcall OutAppend(Obj *self, void *, const cRZString *text)
{
    gEvents.push_back({0x5013, TextHash(text->mString.c_str()), static_cast<uint32_t>(text->mString.size())});
    return self;
}
void __fastcall FilterReset(Filter *self, void *) { gEvents.push_back({0x7007, self == &gFilter ? 1u : 0u}); }
void __fastcall F_b65750(Filter *self, void *, uint32_t v) { gEvents.push_back({0xb65750, self == &gFilter ? 1u : 0u, v}); }
void __fastcall F_b654f0(Filter *self, void *, uint32_t v) { gEvents.push_back({0xb654f0, self == &gFilter ? 1u : 0u, v}); }
void __fastcall F_b655f0(Filter *self, void *, uint32_t v) { gEvents.push_back({0xb655f0, self == &gFilter ? 1u : 0u, v}); }

template <uint32_t kSlot> void __fastcall CatalogQuery(Obj *, void *, ServiceBlockVector *out, void *face, uint32_t x, uint32_t y)
{
    const std::vector<Item *> &list = kSlot == 34 ? gCase.modifiers : gCase.archetypes;
    gEvents.push_back({0x9000 + kSlot, face == reinterpret_cast<uint8_t *>(&gFilter) + 0x54 ? 1u : 0u, x, y,
                       out->mFirst == out->mLast ? 1u : 0u, static_cast<uint32_t>(list.size())});
    if (list.empty()) return;
    void **block = static_cast<void **>(ServiceAlloc(list.size() * 4));
    for (size_t k = 0; k < list.size(); ++k) block[k] = list[k];
    out->mFirst = block;
    out->mLast = out->mEnd = block + list.size();
}

const CasGuid *__fastcall ItemModifierGuid(Item *self, void *)
{
    gEvents.push_back({0xa00a, self->id});
    return &self->modifierGuid;
}
const CasGuid *__fastcall ItemArchetypeGuid(Item *self, void *)
{
    gEvents.push_back({0xa006, self->id});
    return &self->archetypeGuid;
}
uint32_t __fastcall ItemFlags(Item *self, void *)
{
    gEvents.push_back({0xa009, self->id});
    return 0xabcdef00u | self->flags; // only the low byte is the answer
}
Item::Desc *__fastcall ItemDesc(Item *self, void *)
{
    gEvents.push_back({0xa003, self->id});
    return &self->desc;
}
uint32_t __fastcall DescMask(Item::Desc *self, void *)
{
    gEvents.push_back({0xb016, self->id});
    return self->mask;
}
const char *__fastcall DescName(Item::Desc *self, void *)
{
    gEvents.push_back({0xb00d, self->id});
    return self->name.c_str();
}

const char *__cdecl F_b5fd70(uint32_t id)
{
    gEvents.push_back({0xb5fd70, id});
    static const char *const kNames[] = {"Head", "a_rather_long_id_name_for_the_heap", "", "Torso"};
    return kNames[id % 4];
}
uint32_t __cdecl F_b5fd30(uint32_t id)
{
    gEvents.push_back({0xb5fd30, id});
    static const uint32_t kMasks[] = {1, 2, 4, 6, 0, 0xffffffffu};
    return kMasks[id % 6];
}

uint8_t *__fastcall F_cf07a0(void *self, void *, const char *name, uint32_t a, uint32_t b, uint32_t id, uint32_t c, uint32_t d)
{
    gEvents.push_back({0xcf07a0, Rel(self), TextHash(name), a, b, id, c, d});
    return gCase.entries[Pick() % 4];
}
uint8_t *__fastcall F_cec500(void *self, void *, const char *name, uint32_t a, uint32_t b, uint32_t id, uint32_t c, uint32_t d, uint32_t e)
{
    gEvents.push_back({0xcec500, Rel(self), TextHash(name), a, b, id, c, d, e});
    return gCase.entries[Pick() % 4];
}
uint8_t *__fastcall F_cf04b0(void *self, void *, uint32_t a, uint32_t b, uint32_t id, uint32_t c)
{
    gEvents.push_back({0xcf04b0, Rel(self), a, b, id, c});
    return gCase.bases[Pick() % 3];
}

// cRZString::Sprintf (0x1ad6f7 and ours) on both sides: the image's formatting reaches CRT state the harness has not
// initialised.
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    self->mString = text;
    return self;
}
void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

// GUIDs that order differently by memcmp than by their words (low bytes first), and equal ones.
CasGuid RandomGuid(std::mt19937 &rng)
{
    CasGuid g;
    for (auto &w : g.mWords) {
        switch (rng() % 4) {
        case 0: w = 0x100u << (8 * (rng() % 3)); break;
        case 1: w = rng() % 3; break;
        default: w = static_cast<uint32_t>(rng()); break;
        }
    }
    return g;
}

// Bone names from a small pool with near misses (a prefix, one character longer, the last character changed), some
// longer than 15 characters.
const char *BoneName(std::mt19937 &rng)
{
    static const char *const kPool[] = {"spine", "spine1", "spin", "spinf", "head", "l_upperarm_long_bone", "l_upperarm_long_bonf", ""};
    return kPool[rng() % 8];
}

float RandomFloat(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return std::numeric_limits<float>::quiet_NaN();
    case 3: return 1e-8f;
    default: return static_cast<float>(static_cast<int32_t>(rng() % 20000) - 10000) / 997.0f;
    }
}

// The bones of one vector are contiguous: each vector gets a run of storage of its own, destroyed with the case.
std::deque<std::array<uint8_t, sizeof(Bone) * 3>> gRuns;
std::vector<Bone *> gBones;
void ClearBones()
{
    for (Bone *b : gBones) b->~Bone();
    gBones.clear();
    gRuns.clear();
}

// A vector header at `offset` of `entry` over 0-3 new bones.
void PutBones(std::mt19937 &rng, uint8_t *entry, uint32_t offset)
{
    const int n = rng() % 4 == 0 ? 0 : 1 + static_cast<int>(rng() % 3);
    Bone *first = nullptr;
    if (n > 0) {
        gRuns.emplace_back();
        first = reinterpret_cast<Bone *>(gRuns.back().data());
        for (int k = 0; k < n; ++k) {
            Bone *b = ::new (first + k) Bone(BoneName(rng));
            gBones.push_back(b);
            for (float &f : b->pos) f = RandomFloat(rng);
        }
    }
    Bone *last = first + n;
    std::memcpy(entry + offset, &first, 4);
    std::memcpy(entry + offset + 4, &last, 4);
}
// A vector header that is only tested for emptiness.
void PutDummy(std::mt19937 &rng, uint8_t *entry, uint32_t offset)
{
    const uint32_t first = static_cast<uint32_t>(rng());
    const uint32_t last = rng() % 3 == 0 ? first : first + 0x3c;
    std::memcpy(entry + offset, &first, 4);
    std::memcpy(entry + offset + 4, &last, 4);
}

void BuildCase(std::mt19937 &rng)
{
    ClearBones();
    gCase.items.clear();
    gCase.modifiers.clear();
    gCase.archetypes.clear();
    std::vector<CasGuid> pool;
    const int g = 1 + static_cast<int>(rng() % 3);
    for (int k = 0; k < g; ++k) pool.push_back(RandomGuid(rng));
    const int n = static_cast<int>(rng() % 6);
    static const uint32_t kMasks[] = {1, 2, 4, 0, 0xff, 8};
    static const char *const kItemNames[] = {"mod_a", "a_modifier_with_a_long_name", "", "x"};
    for (int k = 0; k < n; ++k) {
        Item it{gItemVt.data(), static_cast<uint32_t>(k + 1), pool[rng() % g], pool[rng() % g], static_cast<uint8_t>(rng() % 4),
                {gDescVt.data(), static_cast<uint32_t>(k + 1), kMasks[rng() % 6], kItemNames[rng() % 4]}};
        if (rng() % 5 == 0) it.flags |= 0xf0;
        gCase.items.push_back(it);
    }
    for (auto &it : gCase.items) {
        if (rng() % 3 != 0) gCase.modifiers.push_back(&it);
        if (rng() % 3 != 0) gCase.archetypes.push_back(&it);
    }
    for (auto &e : gCase.entries) {
        for (auto &b : e) b = static_cast<uint8_t>(rng());
        e[0x38] = rng() % 2 ? 0 : static_cast<uint8_t>(rng() | 1);
        PutDummy(rng, e, 0x60);
        PutDummy(rng, e, 0x6c);
        PutBones(rng, e, 0x78);
        PutBones(rng, e, 0x84);
        PutDummy(rng, e, 0x40);
        PutBones(rng, e, 0x4c);
    }
    for (auto &b : gCase.bases) {
        for (auto &x : b) x = static_cast<uint8_t>(rng());
        PutBones(rng, b, 0x24);
    }
}

} // namespace bpt_bonereport

// 0-5 catalog items over 1-3 GUIDs (memcmp order differing from the word order), each a modifier and / or an archetype,
// with category masks and flags (bits 1 and 2, high bits set); 0-4 ids; part entries with each group present or not and
// signed or not, 0-3 bones per vector with names that match, nearly match or are long; age 0 (nothing but the clear),
// category masks 0, 0x3f and random.
DIFF_TEST(cTSCASBodyPartSystem_ReportBones, 0xced540)
{
    using namespace bpt_bonereport;
    std::mt19937 rng(0xced540);
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0xced540);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    void **const catalogCache = ctx.image.At<void *>(0x10faf74);
    void *const savedCatalog = *catalogCache;
    *catalogCache = &gCatalog;
    Redirect r1(ctx, 0xb65750, reinterpret_cast<const void *>(&F_b65750));
    Redirect r2(ctx, 0xb654f0, reinterpret_cast<const void *>(&F_b654f0));
    Redirect r3(ctx, 0xb655f0, reinterpret_cast<const void *>(&F_b655f0));
    Redirect r4(ctx, 0xb5fd70, reinterpret_cast<const void *>(&F_b5fd70));
    Redirect r5(ctx, 0xb5fd30, reinterpret_cast<const void *>(&F_b5fd30));
    Redirect r6(ctx, 0xcf07a0, reinterpret_cast<const void *>(&F_cf07a0));
    Redirect r7(ctx, 0xcec500, reinterpret_cast<const void *>(&F_cec500));
    Redirect r8(ctx, 0xcf04b0, reinterpret_cast<const void *>(&F_cf04b0));
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    shape::StandIn sprintfStand(0x1ad6f7, &cRZString::Sprintf, &StandSprintf);
    gOutVt = {};
    gOutVt[26] = reinterpret_cast<const void *>(&OutClear);
    gOutVt[19] = reinterpret_cast<const void *>(&OutAppend);
    gOut.vt = gOutVt.data();
    gFilterVt = {};
    gFilterVt[7] = reinterpret_cast<const void *>(&FilterReset);
    gFilter.vt = gFilterVt.data();
    gCatalogVt = {};
    gCatalogVt[34] = reinterpret_cast<const void *>(&CatalogQuery<34>);
    gCatalogVt[32] = reinterpret_cast<const void *>(&CatalogQuery<32>);
    gCatalog.vt = gCatalogVt.data();
    gItemVt = {};
    gItemVt[10] = reinterpret_cast<const void *>(&ItemModifierGuid);
    gItemVt[6] = reinterpret_cast<const void *>(&ItemArchetypeGuid);
    gItemVt[9] = reinterpret_cast<const void *>(&ItemFlags);
    gItemVt[3] = reinterpret_cast<const void *>(&ItemDesc);
    gDescVt = {};
    gDescVt[22] = reinterpret_cast<const void *>(&DescMask);
    gDescVt[13] = reinterpret_cast<const void *>(&DescName);
    static uint32_t ids[4];
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        BuildCase(rng);
        const int n = static_cast<int>(rng() % 5);
        for (int k = 0; k < n; ++k) ids[k] = rng() % 7;
        const uint32_t age = rng() % 6 == 0 ? 0 : static_cast<uint32_t>(1 + rng() % 4);
        std::memcpy(sc.bytes + kIfaceOffset + 0x118, &age, 4);
        const uint32_t mask = rng() % 4 == 0 ? 0u : rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : 0x3fu;
        std::memcpy(sc.bytes + kIfaceOffset + 0x11c, &mask, 4);
        void *const filter = &gFilter;
        std::memcpy(sc.bytes + kIfaceOffset + 0x140, &filter, 4);
        uint32_t *first = ids, *last = ids + n;
        std::memcpy(sc.bytes + kIfaceOffset + 0x78, &first, 4);
        std::memcpy(sc.bytes + kIfaceOffset + 0x7c, &last, 4);
        Compare(ctx, "ReportBones", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), &gOut);
            else w.S()->ReportBones(&gOut);
            return 0;
        });
    }
    ClearBones();
    gCase.items.clear();
    *catalogCache = savedCatalog;
}
