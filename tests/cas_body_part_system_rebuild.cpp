// Differential test for nTSCASCore::cTSCASBodyPartSystem, lane KA: RebuildPartMaterials (slot 25, 0xcfb830). The
// material table at iface +0x2c is built per side from one spec with containers of the same layout (std::map /
// std::multimap / std::multiset nodes); the part entries 0xcf04b0, the material builder 0xcfb1d0, the link GUID
// 0xcff0c0 and the zero key 0x4f8e50 are recording fakes; slot 52 is faked through the iface vtable; the parts and
// linked objects are fake objects (slot 34 records the names, slot 56 hands out ids). The image's operator new / sized
// delete (0x824632 / 0x824624) stand in for ours (the original's string assign allocates with them). Compared: the
// result, every call, and the table afterwards (keys, sizes, every material name).
#include "cas_body_part_system.h"

#include <map>
#include <set>

namespace bpt_rebuild {

using namespace bpt;
using nTSCASCore::CasAlloc;
using nTSCASCore::IdVector;

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

const void *gStringVt; // the game's cRZString vtable (0xd9b470)

// A cRZString of the same layout, built here.
struct TString {
    const void *vt;
    std::string s;
    uint32_t ref = 0;
    TString() : vt(gStringVt) {}
    explicit TString(const char *text) : vt(gStringVt), s(text) {}
};
static_assert(sizeof(TString) == 0x20, "TString");

struct TUser {
    uint32_t kind, key, id;
    TString subset;
    uint32_t guid[4];
};
static_assert(sizeof(TUser) == 0x3c, "TUser");
struct NoOrder {
    bool operator()(const TUser &, const TUser &) const { return false; } // insertion order
};
struct TMat {
    TString name;
    uint32_t rebuildable;
    int32_t level;
    uint32_t tag; // +0x28: which material this is (neither side reads it; the builder fake reports it)
    std::multiset<TUser, NoOrder> users;
    TString text34;
};
static_assert(sizeof(TMat) == 0x54 && offsetof(TMat, users) == 0x2c, "TMat");
using TMatMap = std::multimap<uint32_t, TMat, std::less<uint32_t>, CasAlloc<std::pair<const uint32_t, TMat>>>;
using TTable = std::map<int32_t, TMatMap, std::less<int32_t>, CasAlloc<std::pair<const int32_t, TMatMap>>>;

const char *const kNames[] = {"skin_material", "", "a_material_name_longer_than_fifteen", "hair", "skin_materiaL"};
// zero, near misses of each other and of zero (first and last words)
const uint32_t kGuids[5][4] = {{0, 0, 0, 0}, {1, 2, 3, 4}, {1, 2, 3, 5}, {0x10000, 0, 0, 0}, {0, 0, 0, 1}};
const uint32_t kZero[4] = {};

struct UserSpec {
    uint32_t kind, key, id;
    int subset, guid;
};
struct MatSpec {
    uint32_t inner;
    int name, text;
    uint32_t rebuildable;
    int32_t level;
    std::vector<UserSpec> users;
};
struct TableSpec {
    std::vector<std::pair<int32_t, std::vector<MatSpec>>> parts;
};

void Build(TTable &t, const TableSpec &spec)
{
    uint32_t tag = 0;
    for (const auto &[key, mats] : spec.parts) {
        TMatMap &m = t[key];
        for (const MatSpec &ms : mats) {
            auto it = m.emplace(std::piecewise_construct, std::forward_as_tuple(ms.inner), std::forward_as_tuple());
            TMat &mat = it->second;
            mat.name.s = kNames[ms.name];
            mat.text34.s = kNames[ms.text];
            mat.rebuildable = ms.rebuildable;
            mat.level = ms.level;
            mat.tag = tag++;
            for (const UserSpec &us : ms.users) {
                TUser u{us.kind, us.key, us.id, TString(kNames[us.subset]), {}};
                std::memcpy(u.guid, kGuids[us.guid], 16);
                mat.users.insert(u);
            }
        }
    }
}

uint32_t TableHash(const TTable &t)
{
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix(static_cast<uint32_t>(t.size()));
    for (const auto &[key, mats] : t) {
        mix(static_cast<uint32_t>(key));
        mix(static_cast<uint32_t>(mats.size()));
        for (const auto &[inner, mat] : mats) {
            mix(inner);
            mix(mat.tag);
            mix(TextHash(mat.name.s.c_str()));
            mix(static_cast<uint32_t>(mat.name.s.size()));
            mix(TextHash(mat.text34.s.c_str()));
            mix(static_cast<uint32_t>(mat.users.size()));
        }
    }
    return h;
}

// ---- fakes ----
struct Link {
    uint8_t pad[8];
    uint8_t enabled;
    uint8_t pad9[3];
    void *object;
    uint32_t guid[4];
    uint32_t index;
};
static_assert(offsetof(Link, object) == 0xc, "Link");
Link gLinks[6];
struct Entry {
    uint8_t pad[0x68];
    Link **first, **last;
};
Entry gEntries[4];
Link *gLinkPtrs[4][5];

struct FakeObject {
    const void *const *vtable;
    uint32_t id;
};
FakeObject gParts[3], gLinked[3];
std::array<const void *, 60> gObjVt;

uint32_t ObjId(const void *p)
{
    return p == nullptr ? 0xffu : static_cast<const FakeObject *>(p)->id;
}

void __fastcall ObjSetMaterial(FakeObject *self, void *, const char *material, const char *subset)
{
    gEvents.push_back({0xa034, self->id, TextHash(material), TextHash(subset)});
}
void __fastcall ObjIds(FakeObject *self, void *, IdVector *out)
{
    gEvents.push_back({0xa038, self->id, static_cast<uint32_t>(out->size())});
    const uint32_t n = Pick() % 4;
    for (uint32_t k = 0; k < n; ++k) out->push_back(Pick() % 7);
}

void *__fastcall Slot52(void *self, void *, uint32_t a, uint32_t b, uint32_t id)
{
    gEvents.push_back({52, Rel(self), a, b, id});
    const uint32_t p = Pick() % 5;
    return p >= 3 ? nullptr : &gParts[p];
}

Entry *__fastcall F_cf04b0(void *self, void *, uint32_t a, uint32_t b, uint32_t id, uint32_t part)
{
    gEvents.push_back({0xcf04b0, Rel(self), a, b, id, part});
    return &gEntries[Pick() % 4];
}

const char *gBuilt[6];
const char *__fastcall F_cfb1d0(void *self, void *, const TMat *mat, uint32_t kind, uint32_t key, uint32_t age, uint32_t arg)
{
    gEvents.push_back({0xcfb1d0, Rel(self), mat->tag, kind, key, age, arg, TextHash(mat->name.s.c_str())});
    return gBuilt[Pick() % 6];
}

const uint32_t *__fastcall F_cff0c0(const Link *link, void *)
{
    gEvents.push_back({0xcff0c0, link->index});
    return link->guid;
}
const uint32_t *__cdecl F_4f8e50()
{
    gEvents.push_back({0x4f8e50});
    return kZero;
}

void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

UserSpec RandomUser(std::mt19937 &rng)
{
    return {rng() % 3 == 0 ? 7u : static_cast<uint32_t>(rng() % 5), static_cast<uint32_t>(rng() % 4), static_cast<uint32_t>(rng() % 5),
            static_cast<int>(rng() % 5), static_cast<int>(rng() % 5)};
}

} // namespace bpt_rebuild

DIFF_TEST(cTSCASBodyPartSystem_RebuildPartMaterials, 0xcfb830)
{
    using namespace bpt_rebuild;
    std::mt19937 rng(0xcfb830);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint8_t)>(0xcfb830);
    VTable vt = CopyVTable(ctx);
    vt.slots[52] = reinterpret_cast<const void *>(&Slot52);
    Env env(ctx);
    gStringVt = ctx.image.At<void>(0xd9b470);
    Redirect r1(ctx, 0xcf04b0, reinterpret_cast<const void *>(&F_cf04b0));
    Redirect r2(ctx, 0xcfb1d0, reinterpret_cast<const void *>(&F_cfb1d0));
    Redirect r3(ctx, 0xcff0c0, reinterpret_cast<const void *>(&F_cff0c0));
    Redirect r4(ctx, 0x4f8e50, reinterpret_cast<const void *>(&F_4f8e50));
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    gObjVt = {};
    gObjVt[34] = reinterpret_cast<const void *>(&ObjSetMaterial);
    gObjVt[56] = reinterpret_cast<const void *>(&ObjIds);
    for (uint32_t i = 0; i < 3; ++i) gParts[i] = {gObjVt.data(), 0x100u + i};
    for (uint32_t i = 0; i < 3; ++i) gLinked[i] = {gObjVt.data(), 0x200u + i};
    uint32_t ids[6];
    gEvents.reserve(1024);
    for (int i = 0; i < 4000; ++i) {
        gScriptSeed = static_cast<uint32_t>(rng());
        gBuilt[0] = nullptr;
        gBuilt[1] = nullptr;
        for (int k = 2; k < 6; ++k) gBuilt[k] = kNames[rng() % 5];
        for (int k = 0; k < 6; ++k) {
            Link &l = gLinks[k];
            l.enabled = rng() % 4 == 0 ? 0 : static_cast<uint8_t>(rng() | 1);
            l.object = rng() % 5 == 0 ? nullptr : &gLinked[rng() % 3];
            std::memcpy(l.guid, kGuids[rng() % 5], 16);
            l.index = static_cast<uint32_t>(k);
        }
        for (int e = 0; e < 4; ++e) {
            const int n = static_cast<int>(rng() % 5); // 0..4 links, some null
            for (int k = 0; k < n; ++k) gLinkPtrs[e][k] = rng() % 6 == 0 ? nullptr : &gLinks[rng() % 6];
            gEntries[e].first = gLinkPtrs[e];
            gEntries[e].last = gLinkPtrs[e] + n;
        }
        // The part asked for, and a table holding its key or not (then the original inserts it).
        const uint32_t kind = i < 16 ? static_cast<uint32_t>(i % 8) : rng() % 4 == 0 ? 7u : rng() % 6 == 0 ? 0x80u + rng() % 4 : rng() % 6;
        const uint32_t key = static_cast<uint32_t>(rng());
        const uint32_t age = rng() % 3 == 0 ? 0 : static_cast<uint32_t>(rng() % 4);
        const uint32_t k3 = kind == 7 ? 3 : kind;
        const int32_t partKey = static_cast<int32_t>((k3 & 0x7f) * 2 | (age == 0 ? key & 1 : 0));
        TableSpec spec;
        const int parts = static_cast<int>(rng() % 4);
        for (int p = 0; p < parts; ++p) {
            std::vector<MatSpec> mats;
            const int nm = static_cast<int>(rng() % 4);
            for (int m = 0; m < nm; ++m) {
                MatSpec ms{static_cast<uint32_t>(rng() % 3), static_cast<int>(rng() % 5), static_cast<int>(rng() % 5),
                           rng() % 4 == 0 ? 0u : static_cast<uint32_t>(rng() | 1), static_cast<int32_t>(rng() % 6) - 1, {}};
                const int nu = rng() % 4 == 0 ? 0 : static_cast<int>(rng() % 4);
                for (int u = 0; u < nu; ++u) ms.users.push_back(RandomUser(rng));
                mats.push_back(ms);
            }
            const uint32_t r = rng() % 4;
            const int32_t k = r == 0 ? partKey ^ 1 : r == 1 ? static_cast<int32_t>(rng() % 20) - 2 : partKey; // near, other, the part's
            spec.parts.push_back({k, mats});
        }
        Scenario sc = RandomScenario(rng);
        const int n = i < 7 ? i % 7 : static_cast<int>(rng() % 7); // 0..6 ids
        for (int k = 0; k < n; ++k) ids[k] = rng() % 9;
        uint32_t *first = ids, *last = ids + n;
        std::memcpy(sc.bytes + kIfaceOffset + 0x78, &first, 4);
        std::memcpy(sc.bytes + kIfaceOffset + 0x7c, &last, 4);
        std::memcpy(sc.bytes + kIfaceOffset + 0x118, &age, 4);
        const uint32_t arg = static_cast<uint32_t>(rng());
        const uint8_t apply = rng() % 4 == 0 ? 0 : static_cast<uint8_t>(rng() | 1);
        Compare(ctx, "RebuildPartMaterials", sc, vt, [&](World &w, bool original) -> uint64_t {
            TTable table;
            Build(table, spec);
            TTable *ptr = &table;
            std::memcpy(w.Base() + kIfaceOffset + 0x2c, &ptr, 4);
            const uint8_t r = original ? orig(w.S(), kind, key, arg, apply) : w.S()->RebuildPartMaterials(kind, key, arg, apply);
            std::memcpy(w.Base() + kIfaceOffset + 0x2c, sc.bytes + kIfaceOffset + 0x2c, 4);
            return static_cast<uint64_t>(r) | static_cast<uint64_t>(TableHash(table)) << 8;
        });
    }
}
