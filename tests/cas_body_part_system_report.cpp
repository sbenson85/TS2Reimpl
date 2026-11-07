// Differential tests for nTSCASCore::cTSCASBodyPartSystem, lane JY: the vertex-id reports (slots 85 0xcee3f0, 86
// 0xcee680), and lane JZ: the region mask report (slot 87, 0xcee930). The caller's string, the part-entry lookups
// (0xcec500, 0xcf07a0, 0xcf29b0) and the id names (0xb5fd70) are fakes; the report text handed over is compared.
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "rz/cRZString.h"

#include <cstdarg>
#include <cstdio>

namespace {

using namespace bpt;

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

struct OutString {
    const void *const *vtable;
} gOut;
std::array<const void *, 40> gOutVt;

void __fastcall OutClear(OutString *, void *, uint32_t a, uint32_t b) { gEvents.push_back({0x5026, a, b}); }
void __fastcall OutSet(OutString *, void *, const cRZString *text)
{
    gEvents.push_back({0x5010, TextHash(text->mString.c_str()), static_cast<uint32_t>(text->mString.size())});
}

const char *__cdecl F_b5fd70(uint32_t id)
{
    gEvents.push_back({0xb5fd70, id});
    static const char *const kNames[] = {"Head", "a_rather_long_id_name_for_the_heap", "", "Torso"};
    return kNames[id % 4];
}

uint8_t gParts[4][0x80];

uint8_t *__fastcall F_cec500(void *self, void *, const char *name, uint32_t a, uint32_t b, uint32_t id, uint32_t c, uint32_t d, uint32_t e)
{
    gEvents.push_back({0xcec500, Rel(self), TextHash(name), a, b, id, c, d, e});
    return gParts[Pick() % 4];
}
uint8_t *__fastcall F_cf07a0(void *self, void *, const char *name, uint32_t a, uint32_t b, uint32_t id, uint32_t c, uint32_t d)
{
    gEvents.push_back({0xcf07a0, Rel(self), TextHash(name), a, b, id, c, d});
    return gParts[Pick() % 4];
}

uint32_t gIds[6];

void SetupParts(std::mt19937 &rng, uint32_t base, uint32_t data)
{
    for (auto &p : gParts) {
        for (auto &b : p) b = static_cast<uint8_t>(rng());
        auto put = [&](uint32_t off, uint32_t v) { std::memcpy(p + base + off, &v, 4); };
        const uint32_t first = static_cast<uint32_t>(rng());
        put(data - base, first); // the data vector's first / last
        put(data - base + 4, rng() % 3 == 0 ? first : first + 12);
        const uint32_t b = rng() % 5, c = rng() % 3 == 0 ? b : rng() % 5;
        put(0, b);                                        // the counts the line prints
        put(8, c);
        put(4, rng() % 2 ? 7 : static_cast<uint32_t>(rng()));
    }
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_ReportVertexIds, 0xcee3f0)
{
    std::mt19937 rng(0xcee3f0);
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *, void *)>(0xcee3f0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    gOutVt = {};
    gOutVt[26] = reinterpret_cast<const void *>(&OutClear);
    gOutVt[16] = reinterpret_cast<const void *>(&OutSet);
    gOut.vtable = gOutVt.data();
    Redirect r1(ctx, 0xcec500, reinterpret_cast<const void *>(&F_cec500));
    Redirect r2(ctx, 0xb5fd70, reinterpret_cast<const void *>(&F_b5fd70));
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        SetupParts(rng, 0x34, 0x40);
        for (auto &p : gParts) p[0x31] = rng() % 3 == 0 ? 0 : static_cast<uint8_t>(rng() | 1);
        const int n = static_cast<int>(rng() % 6);
        for (int k = 0; k < n; ++k) gIds[k] = rng() % 8;
        const uint32_t age = rng() % 4 == 0 ? 0 : static_cast<uint32_t>(rng() % 5);
        std::memcpy(sc.bytes + kIfaceOffset + 0x118, &age, 4);
        uint32_t *first = gIds, *last = gIds + n;
        std::memcpy(sc.bytes + kIfaceOffset + 0x78, &first, 4);
        std::memcpy(sc.bytes + kIfaceOffset + 0x7c, &last, 4);
        const char *name = RandomName(rng);
        Compare(ctx, "ReportVertexIds", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), name, &gOut);
            else w.S()->ReportVertexIds(name, &gOut);
            return 0;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_ReportGroupVertexIds, 0xcee680)
{
    std::mt19937 rng(0xcee680);
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *, uint32_t, void *)>(0xcee680);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    gOutVt = {};
    gOutVt[26] = reinterpret_cast<const void *>(&OutClear);
    gOutVt[16] = reinterpret_cast<const void *>(&OutSet);
    gOut.vtable = gOutVt.data();
    Redirect r1(ctx, 0xcf07a0, reinterpret_cast<const void *>(&F_cf07a0));
    Redirect r2(ctx, 0xb5fd70, reinterpret_cast<const void *>(&F_b5fd70));
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        const uint32_t group = rng() % 3;
        SetupParts(rng, 0x48 + 12 * group, 0x60 + 12 * group);
        const int n = static_cast<int>(rng() % 6);
        for (int k = 0; k < n; ++k) gIds[k] = rng() % 8;
        const uint32_t age = rng() % 4 == 0 ? 0 : static_cast<uint32_t>(rng() % 5);
        std::memcpy(sc.bytes + kIfaceOffset + 0x118, &age, 4);
        uint32_t *first = gIds, *last = gIds + n;
        std::memcpy(sc.bytes + kIfaceOffset + 0x78, &first, 4);
        std::memcpy(sc.bytes + kIfaceOffset + 0x7c, &last, 4);
        const char *name = RandomName(rng);
        Compare(ctx, "ReportGroupVertexIds", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), name, group, &gOut);
            else w.S()->ReportGroupVertexIds(name, group, &gOut);
            return 0;
        });
    }
}

namespace {

// Slot 87's weights lookup: one of four part entries (+0x30, +0x34..+0x3c counts, +0x42, the +0x44/+0x48 vector).
uint8_t *__fastcall F_cf29b0(void *self, void *, uint32_t a, uint32_t b, uint32_t id, uint32_t c, const char *region)
{
    gEvents.push_back({0xcf29b0, Rel(self), a, b, id, c, TextHash(region)});
    return gParts[Pick() % 4];
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

} // namespace

// Region lists from the image's own region prefixes (with and without suffixes, upper-cased, cut short), unrelated
// names, empty segments (which end the walk) and a trailing delimiter; 0-5 ids of 0-5.
DIFF_TEST(cTSCASBodyPartSystem_ReportRegionMasks, 0xcee930)
{
    std::mt19937 rng(0xcee930);
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *, void *)>(0xcee930);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    gOutVt = {};
    gOutVt[26] = reinterpret_cast<const void *>(&OutClear);
    gOutVt[16] = reinterpret_cast<const void *>(&OutSet);
    gOut.vtable = gOutVt.data();
    Redirect r1(ctx, 0xcf29b0, reinterpret_cast<const void *>(&F_cf29b0));
    Redirect r2(ctx, 0xb5fd70, reinterpret_cast<const void *>(&F_b5fd70));
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    shape::StandIn sprintfStand(0x1ad6f7, &cRZString::Sprintf, &StandSprintf);
    const char delimiter = *ctx.image.At<const char>(0xdab158);
    static const uint32_t kPrefixes[] = {0xdf70ac, 0xdce374, 0xeba7cc, 0xdf70bc, 0xe87f6c, 0xdf70b4, 0xdbcf58, 0xdf70a4};
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        for (auto &p : gParts) {
            for (auto &b : p) b = static_cast<uint8_t>(rng());
            const uint32_t first = static_cast<uint32_t>(rng());
            std::memcpy(p + 0x44, &first, 4);
            const uint32_t last = rng() % 4 == 0 ? first : first + 4;
            std::memcpy(p + 0x48, &last, 4);
            p[0x42] = rng() % 2 ? 0 : static_cast<uint8_t>(rng() | 1);
            p[0x30] = rng() % 3 == 0 ? 0 : static_cast<uint8_t>(rng() | 1);
            const uint32_t b = rng() % 5, c = rng() % 3 == 0 ? b : rng() % 5;
            std::memcpy(p + 0x34, &b, 4);
            std::memcpy(p + 0x3c, &c, 4);
        }
        std::string regions;
        const int segments = static_cast<int>(rng() % 6);
        for (int k = 0; k < segments; ++k) {
            if (k != 0) regions += delimiter;
            std::string seg;
            switch (rng() % 6) {
            case 0: seg = "unrelated_region"; break;
            case 1: seg = rng() % 4 == 0 ? "" : "ab"; break;
            default: {
                seg = ctx.image.At<const char>(kPrefixes[rng() % 8]);
                const int pick = static_cast<int>(rng() % 4);
                if (pick == 1) seg += "_left_and_a_long_tail";
                if (pick == 2) for (char &ch : seg) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (pick == 3 && seg.size() > 1) seg.resize(seg.size() - 1);
            }
            }
            regions += seg;
        }
        if (rng() % 5 == 0) regions += delimiter;
        const int n = static_cast<int>(rng() % 6);
        for (int k = 0; k < n; ++k) gIds[k] = rng() % 6;
        const uint32_t age = rng() % 4 == 0 ? 0 : static_cast<uint32_t>(1 + rng() % 4);
        std::memcpy(sc.bytes + kIfaceOffset + 0x118, &age, 4);
        uint32_t *first = gIds, *last = gIds + n;
        std::memcpy(sc.bytes + kIfaceOffset + 0x78, &first, 4);
        std::memcpy(sc.bytes + kIfaceOffset + 0x7c, &last, 4);
        Compare(ctx, "ReportRegionMasks", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), regions.c_str(), &gOut);
            else w.S()->ReportRegionMasks(regions.c_str(), &gOut);
            return 0;
        });
    }
}
