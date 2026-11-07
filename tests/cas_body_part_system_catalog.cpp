// Differential test for nTSCASCore::cTSCASBodyPartSystem, lane KA: slot 40 (IsGuidApplied, 0xcf45e0). The overlay
// filter (iface +0x148) and its setters (0xb654f0 ... 0xb65750), the two argument maps (0xb5fc80, 0xb5fd20), the skin
// base lookup (0xceefb0) and the resource catalog (cache 0x10faf74, slot 21) are recording fakes. The catalog answers
// from a per-case table keyed by the filter's GUID and whether the filter's +0xc8 block is the iface's (first query)
// or the skin base's (the retry); its matches have types 4 / 5 / other, missing interfaces, GUIDs and values.
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "simantics/ServiceRefVector.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>

namespace bpt_catalog {

using namespace bpt;
using Vt = std::array<const void *, 32>;
using Guid = std::array<uint32_t, 4>;

uint32_t Hash16(const void *p)
{
    uint32_t h = 2166136261u;
    for (int k = 0; k < 16; ++k) h = (h ^ static_cast<const uint8_t *>(p)[k]) * 16777619u;
    return h;
}

// A catalog match: its type, whether it has the interface its type asks for, its GUID and value.
struct Match {
    const void *const *vt;
    uint32_t id;
    uint32_t type;
    bool hasFace;
    struct Face {
        const void *const *vt;
        uint32_t id;
        Guid guid;
        uint32_t value;
    } face;
};
Vt gMatchVt, gFaceVt, gFilterVt, gCatalogVt, gSkinBaseVt;

struct Answer {
    std::vector<Match *> first, retry; // with the iface's +0xc8 block / with the skin base's
};
struct Case {
    std::vector<Guid> pool;
    std::vector<Answer> answers; // per pool GUID
    std::deque<Match> matches;
    int baseMode = 0;            // 0 none, 1 the iface's +0xc8, 2 another GUID
    Guid baseGuid{};
} gCase;

struct Filter {
    const void *const *vt;
    uint8_t pad[0x60];
    Guid guid{};
    uint8_t c8[16]{};
} gFilter;
struct Obj {
    const void *const *vt;
} gCatalog, gSkinBase;

const uint8_t *gIfaceC8; // the running side's +0xc8 block

void __fastcall FilterReset(Filter *self, void *) { gEvents.push_back({0x7007, self == &gFilter ? 1u : 0u}); }
void __fastcall F_b654f0(Filter *, void *, uint32_t v) { gEvents.push_back({0xb654f0, v}); }
void __fastcall F_b655f0(Filter *, void *, uint32_t v) { gEvents.push_back({0xb655f0, v}); }
void __fastcall F_b65750(Filter *, void *, uint32_t v) { gEvents.push_back({0xb65750, v}); }
void __fastcall F_b656f0(Filter *, void *, const uint8_t *p) { gEvents.push_back({0xb656f0, Rel(p), Hash16(p)}); }
void __fastcall F_b65610(Filter *self, void *, const uint8_t *p)
{
    gEvents.push_back({0xb65610, Hash16(p)});
    std::memcpy(self->c8, p, 16);
}
void __fastcall F_b65550(Filter *self, void *, const uint32_t *p)
{
    gEvents.push_back({0xb65550, p[0], p[1], p[2], p[3]});
    std::memcpy(self->guid.data(), p, 16);
}
uint32_t __cdecl F_b5fc80(uint32_t a)
{
    gEvents.push_back({0xb5fc80, a});
    return a * 3 + 1;
}
uint32_t __cdecl F_b5fd20(uint32_t b)
{
    gEvents.push_back({0xb5fd20, b});
    return b ^ 0x55;
}
const void *__fastcall BaseGuid(Obj *, void *)
{
    gEvents.push_back({0x8005});
    return gCase.baseGuid.data();
}
void *__fastcall F_ceefb0(void *system, void *)
{
    gEvents.push_back({0xceefb0, Rel(system)});
    if (gCase.baseMode == 0) return nullptr;
    if (gCase.baseMode == 1) std::memcpy(gCase.baseGuid.data(), gIfaceC8, 16);
    return &gSkinBase;
}

void __fastcall CatalogQuery(Obj *, void *, ServiceBlockVector *out, void *face, uint32_t x, uint32_t y)
{
    const bool first = std::memcmp(gFilter.c8, gIfaceC8, 16) == 0;
    const std::vector<Match *> *found = nullptr;
    for (size_t k = 0; k < gCase.pool.size(); ++k)
        if (gCase.pool[k] == gFilter.guid) found = first ? &gCase.answers[k].first : &gCase.answers[k].retry;
    const uint32_t n = found ? static_cast<uint32_t>(found->size()) : 0u;
    gEvents.push_back({0x9021, face == reinterpret_cast<uint8_t *>(&gFilter) + 0x54 ? 1u : 0u, x, y, n,
                       out->mFirst == out->mLast ? 1u : 0u});
    if (n == 0) return;
    void **block = static_cast<void **>(ServiceAlloc(n * 4));
    for (uint32_t k = 0; k < n; ++k) block[k] = (*found)[k];
    out->mFirst = block;
    out->mLast = out->mEnd = block + n;
}
uint32_t __fastcall MatchType(Match *self, void *)
{
    gEvents.push_back({0x900c, self->id});
    return self->type;
}
uint8_t __fastcall MatchQuery(Match *self, void *, uint32_t iid, void **out)
{
    gEvents.push_back({0x9000, self->id, iid});
    *out = self->hasFace ? &self->face : nullptr;
    return self->hasFace ? 1 : 0;
}
const void *__fastcall FaceGuid(Match::Face *self, void *)
{
    gEvents.push_back({0xa005, self->id});
    return self->guid.data();
}
uint32_t __fastcall FaceValue6(Match::Face *self, void *)
{
    gEvents.push_back({0xa006, self->id});
    return self->value;
}
uint32_t __fastcall FaceValue7(Match::Face *self, void *)
{
    gEvents.push_back({0xa007, self->id});
    return self->value;
}
uint32_t __fastcall FaceRelease(Match::Face *self, void *)
{
    gEvents.push_back({0xa002, self->id});
    return 0;
}

Guid RandomGuid(std::mt19937 &rng)
{
    Guid g;
    for (auto &w : g) w = rng() % 4 == 0 ? static_cast<uint32_t>(rng() % 16) : static_cast<uint32_t>(rng());
    return g;
}

// A GUID as text in the layouts the game's parser reads alike: braces or not, either case.
std::string GuidText(const Guid &g, std::mt19937 &rng)
{
    char text[64];
    const bool upper = rng() % 2 != 0;
    std::snprintf(text, sizeof(text), upper ? "%08X-%04X-%04X-%04X-%04X%08X" : "%08x-%04x-%04x-%04x-%04x%08x", g[0], g[1] >> 16,
                  g[1] & 0xffff, g[2] >> 16, g[2] & 0xffff, g[3]);
    std::string s = text;
    if (rng() % 3 == 0) s = "{" + s + "}";
    return s;
}

void BuildCase(std::mt19937 &rng)
{
    gCase.pool.clear();
    gCase.answers.clear();
    gCase.matches.clear();
    const int n = 1 + static_cast<int>(rng() % 4);
    for (int k = 0; k < n; ++k) gCase.pool.push_back(RandomGuid(rng));
    uint32_t id = 1;
    for (int k = 0; k < n; ++k) {
        Answer a;
        for (auto *list : {&a.first, &a.retry}) {
            const int count = rng() % 3 == 0 ? 0 : 1 + static_cast<int>(rng() % 2);
            for (int m = 0; m < count; ++m) {
                Match x{gMatchVt.data(), id++, 0, true, {gFaceVt.data(), 0, {}, 0}};
                static const uint32_t kTypes[] = {4, 5, 4, 5, 3, 6};
                x.type = kTypes[rng() % 6];
                x.hasFace = rng() % 8 != 0;
                x.face.id = id++;
                x.face.guid = rng() % 3 != 0 ? gCase.pool[0] : rng() % 2 ? gCase.pool[rng() % n] : RandomGuid(rng);
                x.face.value = static_cast<uint32_t>(rng() % 4);
                gCase.matches.push_back(x);
                list->push_back(&gCase.matches.back());
            }
        }
        gCase.answers.push_back(a);
    }
    gCase.baseMode = static_cast<int>(rng() % 3);
    gCase.baseGuid = rng() % 2 ? gCase.pool[rng() % n] : RandomGuid(rng);
}

} // namespace bpt_catalog

// 1-4 GUIDs with 0-2 matches each for the first query and the retry; 0-8 records of kinds 3 (mostly) and others whose
// names are those GUIDs in either case (so names equal without case decide together), braces, or no GUID at all, with
// weights at 0.5, just above, below, 1, 0 and NaN; a skin base absent, equal to +0xc8 (no retry) or another GUID.
DIFF_TEST(cTSCASBodyPartSystem_IsGuidApplied, 0xcf45e0)
{
    using namespace bpt_catalog;
    std::mt19937 rng(0xcf45e0);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, const char *)>(0xcf45e0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    void **const catalogCache = ctx.image.At<void *>(0x10faf74);
    void *const savedCatalog = *catalogCache;
    *catalogCache = &gCatalog;
    Redirect r1(ctx, 0xceefb0, reinterpret_cast<const void *>(&F_ceefb0));
    Redirect r2(ctx, 0xb654f0, reinterpret_cast<const void *>(&F_b654f0));
    Redirect r3(ctx, 0xb655f0, reinterpret_cast<const void *>(&F_b655f0));
    Redirect r4(ctx, 0xb65750, reinterpret_cast<const void *>(&F_b65750));
    Redirect r5(ctx, 0xb656f0, reinterpret_cast<const void *>(&F_b656f0));
    Redirect r6(ctx, 0xb65610, reinterpret_cast<const void *>(&F_b65610));
    Redirect r7(ctx, 0xb65550, reinterpret_cast<const void *>(&F_b65550));
    Redirect r8(ctx, 0xb5fc80, reinterpret_cast<const void *>(&F_b5fc80));
    Redirect r9(ctx, 0xb5fd20, reinterpret_cast<const void *>(&F_b5fd20));
    gFilterVt = {};
    gFilterVt[7] = reinterpret_cast<const void *>(&FilterReset);
    gFilter.vt = gFilterVt.data();
    gCatalogVt = {};
    gCatalogVt[21] = reinterpret_cast<const void *>(&CatalogQuery);
    gCatalog.vt = gCatalogVt.data();
    gSkinBaseVt = {};
    gSkinBaseVt[5] = reinterpret_cast<const void *>(&BaseGuid);
    gSkinBase.vt = gSkinBaseVt.data();
    gMatchVt = {};
    gMatchVt[0] = reinterpret_cast<const void *>(&MatchQuery);
    gMatchVt[12] = reinterpret_cast<const void *>(&MatchType);
    gFaceVt = {};
    gFaceVt[2] = reinterpret_cast<const void *>(&FaceRelease);
    gFaceVt[5] = reinterpret_cast<const void *>(&FaceGuid);
    gFaceVt[6] = reinterpret_cast<const void *>(&FaceValue6);
    gFaceVt[7] = reinterpret_cast<const void *>(&FaceValue7);
    const float kWeights[] = {0.5f, std::nextafter(0.5f, 1.0f), 0.49f, 1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()};
    static const int kKinds[] = {3, 3, 3, 3, 0, 1, 5};
    for (int i = 0; i < 4000; ++i) {
        Scenario sc = RandomScenario(rng);
        BuildCase(rng);
        sc.stack.resize(rng() % 9);
        for (auto &s : sc.stack) {
            s.kind = kKinds[rng() % (sizeof(kKinds) / sizeof(kKinds[0]))];
            s.weight = kWeights[rng() % (sizeof(kWeights) / sizeof(kWeights[0]))];
            switch (rng() % 8) {
            case 0: s.name = ""; break;
            case 1: s.name = "not_a_guid"; break;
            default: s.name = GuidText(gCase.pool[rng() % gCase.pool.size()], rng); break;
            }
        }
        void *const filter = &gFilter;
        std::memcpy(sc.bytes + kIfaceOffset + 0x148, &filter, 4);
        const std::string guidText = rng() % 6 == 0 ? std::string("zz") : GuidText(gCase.pool[rng() % 2 ? 0 : rng() % gCase.pool.size()], rng);
        const uint32_t a = rng() % 6, b = rng() % 6;
        Compare(ctx, "IsGuidApplied", sc, vt, [&](World &w, bool original) -> uint64_t {
            gFilter.guid = {};
            std::memset(gFilter.c8, 0, 16);
            gIfaceC8 = reinterpret_cast<const uint8_t *>(w.S()) + 0xc8;
            return original ? orig(w.S(), a, b, guidText.c_str()) : w.S()->IsGuidApplied(a, b, guidText.c_str());
        });
    }
    *catalogCache = savedCatalog;
}
