// Differential test for nTSCASCore::cTSCASBodyPartSystem, lane KB: slot 63 (CheckRegionPairs, 0xce1ff0). The system's
// own slots 52 and 31 (patched into the copied vtable), the part (slot 56), the two outfits (slots 15 and 13), the part
// entry and region lookups (0xcf04b0, 0xcf29b0), the region helpers (0xce4590, 0xce3010, 0xce9710, 0xce1600, 0xce7430)
// and atoi (0x830972) are recording fakes; the lines added to `out`, the part entries and the calls are compared.
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "rz/cRZString.h"

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>

namespace bpt_regionpairs {

using namespace bpt;
using Vt = std::array<const void *, 24>;
using nTSCASCore::IdVector;
using nTSCASCore::ServiceString;
using nTSCASCore::ServiceStringVector;
using nTSCASCore::SurfaceRecord;
using RecordVector = std::vector<SurfaceRecord, nTSCASCore::CasAlloc<SurfaceRecord>>;

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}
uint32_t Hash(uint32_t h, const void *p, size_t n)
{
    for (size_t k = 0; k < n; ++k) h = (h ^ static_cast<const uint8_t *>(p)[k]) * 16777619u;
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

struct Obj {
    const void *const *vt;
    uint32_t id;
};
Vt gOutfitVt, gDummyVt;
Obj gPart, gOutfitA, gOutfitB, gDummyService;

uint8_t gEntries[3][0x60];
uint8_t gWeights[4][0x50];
uint32_t EntryIndex(const void *p)
{
    for (uint32_t k = 0; k < 3; ++k)
        if (p == gEntries[k]) return k;
    return 0xee;
}
uint32_t WeightsIndex(const void *p)
{
    for (uint32_t k = 0; k < 4; ++k)
        if (p == gWeights[k]) return k;
    return 0xee;
}
uint32_t OutfitIndex(const void *p) { return p == &gOutfitA ? 1u : p == &gOutfitB ? 2u : 0xeeu; }

struct Script {
    int partOdds = 4;               // slot 52 answers null one time in this many
    std::vector<uint32_t> partIds;  // the part's slot 56
    float heights[6] = {};          // 0xce9710's z answers
} gScript;

void *__fastcall Slot52(void *self, void *, uint32_t kind, uint32_t key, uint32_t id)
{
    gEvents.push_back({52, Rel(self), kind, key, id});
    return Pick() % gScript.partOdds == 0 ? nullptr : &gPart;
}
void __fastcall Slot31(void *self, void *, uint32_t kind, uint32_t key, uint32_t id, uint32_t partId)
{
    gEvents.push_back({31, Rel(self), kind, key, id, partId});
}
void __fastcall PartIds(Obj *self, void *, IdVector *out)
{
    gEvents.push_back({0x6038, self->id, out->empty() ? 1u : 0u});
    const size_t n = gScript.partIds.size();
    if (n == 0) return;
    auto *block = static_cast<uint32_t *>(ServiceAlloc(n * 4));
    for (size_t k = 0; k < n; ++k) block[k] = gScript.partIds[k];
    auto **words = reinterpret_cast<uint32_t **>(out);
    words[0] = block;
    words[1] = words[2] = block + n;
}
uint32_t __fastcall OutfitPart(Obj *self, void *, uint32_t kind, uint32_t key, uint32_t partId, uint32_t id, uint32_t flag)
{
    gEvents.push_back({0x700f, self->id, kind, key, partId, id, flag});
    return Pick();
}
uint32_t __fastcall OutfitHas(Obj *self, void *, uint32_t ref)
{
    gEvents.push_back({0x700d, self->id, ref});
    return Pick() % 5 == 0 ? 0u : (Pick() | 0x100u) & 0xffffff00u; // non-zero answers with a zero low byte
}

uint8_t *__fastcall F_cf04b0(void *self, void *, uint32_t kind, uint32_t key, uint32_t id, uint32_t partId)
{
    gEvents.push_back({0xcf04b0, Rel(self), kind, key, id, partId});
    return gEntries[partId % 3];
}
void __fastcall F_ce4590(void *self, void *, uint8_t *entry) { gEvents.push_back({0xce4590, Rel(self), EntryIndex(entry), entry[0x50]}); }
void __fastcall F_ce7430(void *self, void *, uint8_t *entry) { gEvents.push_back({0xce7430, Rel(self), EntryIndex(entry), entry[0x50]}); }
uint8_t *__fastcall F_cf29b0(void *self, void *, uint32_t kind, uint32_t key, uint32_t id, uint32_t partId, const char *region)
{
    gEvents.push_back({0xcf29b0, Rel(self), kind, key, id, partId, TextHash(region)});
    return gWeights[TextHash(region) % 4];
}
uint32_t __fastcall F_ce3010(void *self, void *, const uint8_t *weights, uint8_t *entry, void *outfit)
{
    gEvents.push_back({0xce3010, Rel(self), WeightsIndex(weights), EntryIndex(entry), OutfitIndex(outfit)});
    return Pick() % 6 == 0 ? 0xabcd00u : 0xabcd01u; // the answer is al
}
uint32_t __fastcall F_ce9710(void *self, void *, const uint8_t *weights, float w, uint8_t *entry, float *out)
{
    gEvents.push_back({0xce9710, Rel(self), WeightsIndex(weights), FloatBits(w), EntryIndex(entry), FloatBits(out[2])});
    const uint32_t p = Pick();
    if (p % 5 == 0) return 0x1200u;
    out[0] = 1.5f;
    out[1] = -2.5f;
    out[2] = gScript.heights[p % 6];
    return 0x1201u;
}
uint32_t __fastcall F_ce1600(void *self, void *, const uint8_t *weights, uint8_t *entry, float amount)
{
    gEvents.push_back({0xce1600, Rel(self), WeightsIndex(weights), EntryIndex(entry), FloatBits(amount)});
    return Pick() % 4 == 0 ? 0x5500u : 0x5501u;
}
// atoi on the original side (ours calls the CRT's; not recorded, as only one side reaches the stand-in).
int __cdecl StandAtoi(const char *s) { return std::atoi(s); }

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

// The offset limits table at iface +4, built as the game's (std::map nodes: the key at +0x10, the value after it).
struct NoCaseLess {
    bool operator()(const std::string &a, const std::string &b) const { return _stricmp(a.c_str(), b.c_str()) < 0; }
};
struct Limits {
    float low, high;
};
using ByKey = std::map<int32_t, const Limits *>;
using BySecond = std::map<std::string, ByKey, NoCaseLess>;
using ByFirst = std::map<std::string, BySecond, NoCaseLess>;
static_assert(sizeof(ByFirst) == 8, "a game map header");

const char *const kRegions[] = {"hair", "HAIR", "head_top", "a_long_region_name_over15", "neck", "", "Neck"};
const char *const kFlagTexts[] = {"hair,1", "HEAD_TOP,0", "neck", "a_long_region_name_over15,1", "neck,1", "x,2", "Hair,0", ",1",
                                  "hair,2", "NECK,-1", "head_top,1"};
const float kHeights[] = {0.0f, 1.0f, -1.0f, 2.5f, -0.75f, std::numeric_limits<float>::quiet_NaN(), 1e-7f};
const float kLimits[] = {-1.0f, 0.0f, 1.0f, -0.5f, 2.0f, -3.0f};

} // namespace bpt_regionpairs

// 1-2 kinds, keys and ids; the part present or not with 0-3 part ids; entries with or without groups; 0-4 records over
// region names equal without case, empty and long; flag texts with 1, 0, 2, -1, none and an empty name; ids around the
// key's 8-bit id field (0x7f..0x81, 0x100..); centre heights that
// make offsets equal to, inside and outside the limits (and NaN); the limits table off (+0x129 clear), on with the
// pair's key present, its regions present but another key, or absent.
DIFF_TEST(cTSCASBodyPartSystem_CheckRegionPairs, 0xce1ff0)
{
    using namespace bpt_regionpairs;
    std::mt19937 rng(0xce1ff0);
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *, const ServiceStringVector *, ServiceStringVector *)>(0xce1ff0);
    VTable vt = CopyVTable(ctx);
    vt.slots[52] = reinterpret_cast<const void *>(&Slot52);
    vt.slots[31] = reinterpret_cast<const void *>(&Slot31);
    Env env(ctx);
    void **const serviceCache = ctx.image.At<void *>(0x10faf6c);
    void *const savedService = *serviceCache;
    *serviceCache = &gDummyService;
    Redirect r1(ctx, 0xcf04b0, reinterpret_cast<const void *>(&F_cf04b0));
    Redirect r2(ctx, 0xce4590, reinterpret_cast<const void *>(&F_ce4590));
    Redirect r3(ctx, 0xce7430, reinterpret_cast<const void *>(&F_ce7430));
    Redirect r4(ctx, 0xcf29b0, reinterpret_cast<const void *>(&F_cf29b0));
    Redirect r5(ctx, 0xce3010, reinterpret_cast<const void *>(&F_ce3010));
    Redirect r6(ctx, 0xce9710, reinterpret_cast<const void *>(&F_ce9710));
    Redirect r7(ctx, 0xce1600, reinterpret_cast<const void *>(&F_ce1600));
    Redirect r8(ctx, 0x830972, reinterpret_cast<const void *>(&StandAtoi));
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    shape::StandIn sprintfStand(0x1ad6f7, &cRZString::Sprintf, &StandSprintf);
    gOutfitVt = {};
    gOutfitVt[15] = reinterpret_cast<const void *>(&OutfitPart);
    gOutfitVt[13] = reinterpret_cast<const void *>(&OutfitHas);
    gOutfitA = {gOutfitVt.data(), 1};
    gOutfitB = {gOutfitVt.data(), 2};
    gDummyVt = {};
    gDummyService = {gDummyVt.data(), 9};
    static std::array<const void *, 60> partVt;
    partVt = {};
    partVt[56] = reinterpret_cast<const void *>(&PartIds);
    gPart = {partVt.data(), 7};
    static uint32_t kinds[2], keys[2], ids[2];
    static const Limits kTable[] = {{-1.0f, 1.0f}, {0.0f, 0.0f}, {-0.5f, 2.0f}, {1.0f, -1.0f}};
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        gScript.partOdds = rng() % 3 == 0 ? 1000000 : 4;
        gScript.partIds.resize(rng() % 4);
        for (uint32_t &p : gScript.partIds) p = static_cast<uint32_t>(rng() % 5);
        for (float &h : gScript.heights) h = kHeights[rng() % 7];
        for (auto &e : gEntries) {
            for (auto &b : e) b = static_cast<uint8_t>(rng());
            if (rng() % 3 == 0) std::memcpy(e + 4, e, 4); // no groups
        }
        for (auto &w : gWeights) {
            for (auto &b : w) b = static_cast<uint8_t>(rng());
            if (rng() % 4 == 0) std::memcpy(w + 0x48, w + 0x44, 4); // no weights
        }
        const int nk = 1 + static_cast<int>(rng() % 2), ny = 1 + static_cast<int>(rng() % 2), ni = static_cast<int>(rng() % 3);
        for (auto &k : kinds) k = rng() % 3 == 0 ? 7u : static_cast<uint32_t>(rng() % 130);
        for (auto &y : keys) y = static_cast<uint32_t>(rng() % 4);
        // ids 0x7f..0x81 and 0x100.. tell the key's 8-bit id mask from a 7-bit one and a wider one
        for (auto &d : ids) {
            const uint32_t r = static_cast<uint32_t>(rng() % 6);
            d = r == 0 ? 0x100 + static_cast<uint32_t>(rng() % 3) : r == 1 ? 0x7f + static_cast<uint32_t>(rng() % 3) : static_cast<uint32_t>(rng() % 4);
        }
        auto put = [&](uint32_t off, const void *p) { std::memcpy(sc.bytes + kIfaceOffset + off, &p, 4); };
        put(0x60, kinds);
        put(0x64, kinds + nk);
        put(0x6c, keys);
        put(0x70, keys + ny);
        put(0x78, ids);
        put(0x7c, ids + ni);
        sc.bytes[kIfaceOffset + 0x129] = rng() % 3 == 0 ? 0 : static_cast<uint8_t>(rng() | 1);

        // Records, flag texts and the limits table.
        std::vector<std::array<int, 4>> records(rng() % 5);
        for (auto &r : records)
            for (int &k : r) k = static_cast<int>(rng() % 7);
        std::vector<int> flagTexts(rng() % 5);
        for (int &k : flagTexts) k = static_cast<int>(rng() % 11);
        ByFirst table;
        for (const auto &r : records) {
            if (rng() % 3 == 0) continue;
            ByKey &byKey = table[kRegions[rng() % 3 == 0 ? rng() % 7 : r[0]]][kRegions[rng() % 3 == 0 ? rng() % 7 : r[1]]];
            const int n = 1 + static_cast<int>(rng() % 3);
            for (int k = 0; k < n; ++k) {
                const uint32_t kind = kinds[rng() % nk], key = keys[rng() % ny], id = ni ? ids[rng() % ni] : 0;
                int32_t at = static_cast<int32_t>(((id & 0xff) << 7 | (kind & 0x7f)) << 1 | (key & 1));
                if (rng() % 4 == 0) at += 1 + static_cast<int32_t>(rng() % 3);
                byKey[at] = &kTable[rng() % 4];
            }
        }
        put(0x04, &table);
        std::vector<int> existing(rng() % 3);
        for (int &k : existing) k = static_cast<int>(rng() % 7);
        const size_t reserve = rng() % 3 == 0 ? 0 : rng() % 6;

        Compare(ctx, "CheckRegionPairs", sc, vt, [&](World &w, bool original) -> uint64_t {
            for (auto &e : gEntries) e[0x50] = 0xcd;
            auto *recs = new RecordVector();
            for (auto &r : records) {
                SurfaceRecord rec;
                for (int k = 0; k < 4; ++k) rec.mText[k] = kRegions[r[k]];
                recs->push_back(rec);
            }
            w.S()->mRecords = recs;
            ServiceStringVector flags;
            for (int k : flagTexts) flags.push_back(ServiceString(kFlagTexts[k]));
            ServiceStringVector out;
            out.reserve(reserve);
            for (int k : existing) out.push_back(ServiceString(kRegions[k]));
            if (original) orig(w.S(), &gOutfitA, &gOutfitB, &flags, &out);
            else w.S()->CheckRegionPairs(&gOutfitA, &gOutfitB, &flags, &out);
            uint32_t h = 2166136261u;
            for (const ServiceString &s : out) {
                const uint32_t cap = static_cast<uint32_t>(s.capacity());
                h = Hash(h, s.c_str(), s.size() + 1);
                h = Hash(h, &cap, 4);
            }
            uint32_t e = Hash(2166136261u, gEntries, sizeof(gEntries));
            const uint64_t state = (static_cast<uint64_t>(h ^ e) << 32) | (out.size() << 16) | out.capacity();
            w.S()->mRecords = nullptr;
            delete recs;
            return state;
        });
    }
    *serviceCache = savedService;
}
