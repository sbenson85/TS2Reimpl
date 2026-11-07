// Differential tests for nTSCASCore::cTSCASBodyPartSystem, lane JY: the per-age name maps (slots 6 0xcf6ee0, 7 0xcf3280),
// the snapshots (slots 12 0xcf74c0, 13 0xcf73e0), the age (slot 71 0xcf85e0) and the morph-record purge (face-0 slot 13
// 0xcf6ff0). See cas_body_part_system.h for the fixture: the callees that reach far (the system's refreshes, the
// constructor helpers, other slots) are recording fakes; the containers' own code runs for real on both sides.
#include "cas_body_part_system.h"
#include "code_patch.h"

#include <map>

namespace {

using namespace bpt;
using nTSCASCore::BodyPartSnapshot;
using nTSCASCore::CasAlloc;
using nTSCASCore::CasNameLess;
using nTSCASCore::SnapshotMap;
using nTSCASCore::SurfaceName;
using nTSCASCore::TreeHeader;
using System = nTSCASCore::cTSCASBodyPartSystem;

template <typename T> void PutAt(Scenario &sc, int ifaceOffset, T value)
{
    std::memcpy(sc.bytes + kIfaceOffset + ifaceOffset, &value, sizeof(value));
}

uint32_t Hash(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}

uint32_t Words(const void *p, int k)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(p) + 4 * k, 4);
    return w;
}

void __fastcall F_ce9530(void *self, void *) { gEvents.push_back({0xce9530, Rel(self)}); }
void __fastcall F_cebb70(void *self, void *) { gEvents.push_back({0xcebb70, Rel(self)}); }
void __fastcall F_cf8670(void *self, void *) { gEvents.push_back({0xcf8670, Rel(self)}); }
void __fastcall F_ce9690(void *self, void *) { gEvents.push_back({0xce9690, Rel(self)}); }
void __fastcall F_cf8fd0(void *self, void *) { gEvents.push_back({0xcf8fd0, Rel(self)}); }
void __fastcall F_cf9340(void *self, void *) { gEvents.push_back({0xcf9340, Rel(self)}); }
void __fastcall F_erase_cdbc10(void *self, void *, void *tree, void *root)
{
    gEvents.push_back({0xcdbc10, Rel(self), Rel(tree), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root))});
}
template <int kSlot> void __fastcall SlotVoid(void *self, void *) { gEvents.push_back({static_cast<uint32_t>(kSlot), Rel(self)}); }
void __fastcall Slot71(void *self, void *, uint32_t code) { gEvents.push_back({71, Rel(self), code}); }
template <int kSlot> void __fastcall SlotBlock(void *self, void *, const void *p)
{
    gEvents.push_back({static_cast<uint32_t>(kSlot), Rel(self), Words(p, 0), Words(p, 1), Words(p, 2), Words(p, 3)});
}

// ---- Per-age name maps (slots 6, 7) ---------------------------------------------------------------------------------

// The maps as the test builds them: the same node layouts as AgeNameMap (name -> {dword -> map}), with the innermost maps
// left empty (a head node from the allocator service), so the image's erase 0xcdb830 frees only nodes the test made.
struct Level2 {
    void *head;
    uint32_t size;
};
using InnerMap = std::map<uint32_t, Level2, std::less<uint32_t>, CasAlloc<std::pair<const uint32_t, Level2>>>;
using TestAgeMap = std::map<SurfaceName, InnerMap, CasNameLess, CasAlloc<std::pair<const SurfaceName, InnerMap>>>;
static_assert(sizeof(InnerMap) == 8 && sizeof(TestAgeMap) == 8, "map headers");

Level2 EmptyLevel2()
{
    auto *head = static_cast<uint8_t *>(ServiceAlloc(0x60));
    std::memset(head, 0, 0x60);
    void *self = head;
    for (int k = 0; k < 3; ++k) std::memcpy(head + 4 * k, &self, 4);
    head[0xc] = 1; // black
    head[0xd] = 1; // the head
    return {head, 0};
}

struct AgeSpec {
    std::vector<std::string> names[4];
    std::vector<uint32_t> inner[4]; // inner key count per name, as a flat list (index = name index)
};

AgeSpec RandomAgeSpec(std::mt19937 &rng)
{
    AgeSpec spec;
    for (int k = 0; k < 4; ++k) {
        const int n = static_cast<int>(rng() % 6);
        for (int i = 0; i < n; ++i) {
            spec.names[k].push_back(RandomName(rng));
            spec.inner[k].push_back(rng() % 3);
        }
    }
    return spec;
}

void BuildAgeMaps(World &w, const AgeSpec &spec)
{
    for (int k = 0; k < 4; ++k) {
        auto *map = new (&w.S()->mByAge[k]) TestAgeMap();
        for (size_t i = 0; i < spec.names[k].size(); ++i) {
            const std::string &name = spec.names[k][i];
            auto it = map->try_emplace(SurfaceName(name.data(), name.size())).first;
            for (uint32_t j = 0; j < spec.inner[k][i]; ++j) it->second.try_emplace(0x100u * j + static_cast<uint32_t>(i), EmptyLevel2());
        }
    }
}

// The maps' contents (names in order, inner keys), then the headers cleared so the object bytes can be compared.
uint64_t AgeMapsState(World &w)
{
    uint32_t h = 2166136261u;
    for (int k = 0; k < 4; ++k) {
        auto *map = reinterpret_cast<TestAgeMap *>(&w.S()->mByAge[k]);
        const uint32_t size = static_cast<uint32_t>(map->size());
        h = Hash(h, &size, 4);
        for (const auto &[name, inner] : *map) {
            h = Hash(h, name.mText.mString.data(), name.mText.mString.size() + 1);
            for (const auto &[key, value] : inner) h = Hash(h, &key, 4);
        }
    }
    std::memset(&w.S()->mByAge[0], 0, sizeof(w.S()->mByAge));
    return h;
}

const char *QueryName(std::mt19937 &rng)
{
    return rng() % 8 == 0 ? "nothing_like_it" : RandomName(rng);
}

} // namespace

// Ages 0..3 only: a larger age indexes past the four maps (+0x58 holds a map of another node type), which the original
// does not check either.
DIFF_TEST(cTSCASBodyPartSystem_HasAgeName, 0xcf3280)
{
    std::mt19937 rng(0xcf3280);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, const char *)>(0xcf3280);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        PutAt<uint32_t>(sc, 0x118, rng() % 4);
        const AgeSpec spec = RandomAgeSpec(rng);
        const char *name = QueryName(rng);
        Compare(ctx, "HasAgeName", sc, vt, [&](World &w, bool original) -> uint64_t {
            BuildAgeMaps(w, spec);
            const uint8_t r = original ? orig(w.S(), name) : static_cast<uint8_t>(w.S()->HasAgeName(name));
            return r | (AgeMapsState(w) << 8);
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_EraseAgeName, 0xcf6ee0)
{
    std::mt19937 rng(0xcf6ee0);
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *)>(0xcf6ee0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        PutAt<uint32_t>(sc, 0x118, rng() % 4);
        const AgeSpec spec = RandomAgeSpec(rng);
        const char *name = QueryName(rng);
        Compare(ctx, "EraseAgeName", sc, vt, [&](World &w, bool original) -> uint64_t {
            BuildAgeMaps(w, spec);
            if (original) orig(w.S(), name);
            else w.S()->EraseAgeName(name);
            return AgeMapsState(w);
        });
    }
}

// ---- Snapshots (slots 12, 13) ---------------------------------------------------------------------------------------

namespace {

struct SnapSpec {
    int32_t id;
    uint32_t words[0x64 / 4];
    std::vector<Spec> stack;
    int spare;
};

int32_t RandomId(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return static_cast<int32_t>(rng());
    case 1: return -static_cast<int32_t>(rng() % 4);
    default: return static_cast<int32_t>(rng() % 6);
    }
}

std::vector<SnapSpec> RandomSnaps(std::mt19937 &rng)
{
    std::vector<SnapSpec> v(rng() % 5);
    for (auto &s : v) {
        s.id = RandomId(rng);
        for (auto &w : s.words) w = static_cast<uint32_t>(rng());
        s.stack = RandomSpecs(rng, 4);
        s.spare = static_cast<int>(rng() % 3);
    }
    return v;
}

SnapshotMap *BuildSnaps(difftest::Context &ctx, const std::vector<SnapSpec> &specs)
{
    auto *map = new SnapshotMap();
    for (const SnapSpec &s : specs) {
        BodyPartSnapshot &snap = (*map)[s.id];
        if (snap.mStack.mFirst != nullptr) continue; // a repeated id keeps the first
        std::memcpy(&snap, s.words, sizeof(snap));
        BodyPartEntryVector *hdr = BuildStack(ctx, s.stack, s.spare);
        snap.mStack = *hdr;
        delete hdr;
    }
    return map;
}

uint64_t SnapsState(World &w)
{
    SnapshotMap *map = w.S()->mSnapshots;
    uint32_t h = 2166136261u;
    for (auto &[id, snap] : *map) {
        h = Hash(h, &id, 4);
        h = Hash(h, &snap.mAgeMask, 4 + 48);
        const std::vector<uint8_t> stack = Snapshot(&snap.mStack);
        h = Hash(h, stack.data(), stack.size());
        h = Hash(h, snap.mKeys, 32 + 4);
    }
    const uint32_t size = static_cast<uint32_t>(map->size());
    w.S()->mSnapshots = nullptr;
    return (static_cast<uint64_t>(h) << 32) | size;
}

VTable SnapVTable(const difftest::Context &ctx)
{
    VTable v = CopyVTable(ctx);
    v.slots[71] = reinterpret_cast<const void *>(&Slot71);
    v.slots[54] = reinterpret_cast<const void *>(&SlotBlock<54>);
    v.slots[74] = reinterpret_cast<const void *>(&SlotBlock<74>);
    v.slots[56] = reinterpret_cast<const void *>(&SlotBlock<56>);
    return v;
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_SaveSnapshot, 0xcf74c0)
{
    std::mt19937 rng(0xcf74c0);
    auto orig = ctx.Original<void(__thiscall *)(void *, int32_t)>(0xcf74c0);
    const VTable vt = SnapVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 1500; ++i) {
        Scenario sc = RandomScenario(rng);
        PutAt<uint32_t>(sc, 0x118, rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 6);
        const std::vector<SnapSpec> snaps = RandomSnaps(rng);
        const int32_t id = rng() % 2 && !snaps.empty() ? snaps[rng() % snaps.size()].id : RandomId(rng);
        Compare(ctx, "SaveSnapshot", sc, vt, [&](World &w, bool original) -> uint64_t {
            w.S()->mSnapshots = BuildSnaps(ctx, snaps);
            if (original) orig(w.S(), id);
            else w.S()->SaveSnapshot(id);
            return SnapsState(w);
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_RestoreSnapshot, 0xcf73e0)
{
    std::mt19937 rng(0xcf73e0);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, int32_t)>(0xcf73e0);
    const VTable vt = SnapVTable(ctx);
    Env env(ctx);
    Redirect r1(ctx, 0xce9530, reinterpret_cast<const void *>(&F_ce9530));
    Redirect r2(ctx, 0xcebb70, reinterpret_cast<const void *>(&F_cebb70));
    Redirect r3(ctx, 0xcf8670, reinterpret_cast<const void *>(&F_cf8670));
    for (int i = 0; i < 1500; ++i) {
        Scenario sc = RandomScenario(rng);
        const std::vector<SnapSpec> snaps = RandomSnaps(rng);
        const int32_t id = rng() % 2 && !snaps.empty() ? snaps[rng() % snaps.size()].id : RandomId(rng);
        Compare(ctx, "RestoreSnapshot", sc, vt, [&](World &w, bool original) -> uint64_t {
            w.S()->mSnapshots = BuildSnaps(ctx, snaps);
            const uint8_t r = original ? orig(w.S(), id) : static_cast<uint8_t>(w.S()->RestoreSnapshot(id));
            return SnapsState(w) * 3 + r;
        });
    }
}

// ---- The age (slot 71) ----------------------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_SetAge, 0xcf85e0)
{
    std::mt19937 rng(0xcf85e0);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xcf85e0);
    auto mapAge = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0xb5b700);
    VTable vt = CopyVTable(ctx);
    vt.slots[10] = reinterpret_cast<const void *>(&SlotVoid<10>);
    vt.slots[9] = reinterpret_cast<const void *>(&SlotVoid<9>);
    Env env(ctx);
    Redirect r1(ctx, 0xcf8670, reinterpret_cast<const void *>(&F_cf8670));
    Redirect r2(ctx, 0xcdbc10, reinterpret_cast<const void *>(&F_erase_cdbc10));
    shape::StandIn s1(0xce9690, &System::ResetKeys, &F_ce9690);
    shape::StandIn s2(0xcf8fd0, &System::InitCategoryLists, &F_cf8fd0);
    shape::StandIn s3(0xcf9340, &System::InitTargetSurfaceNames, &F_cf9340);
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        static const uint32_t kCodes[] = {0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 3, 0x100};
        const uint32_t code = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : kCodes[rng() % 11];
        const uint32_t other = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : kCodes[rng() % 11];
        PutAt<uint32_t>(sc, 0x118, rng() % 2 ? mapAge(code) : mapAge(other)); // the same age about half of the time
        const uint32_t token = static_cast<uint32_t>(rng() % 1000), size = static_cast<uint32_t>(rng() % 50);
        Compare(ctx, "SetAge", sc, vt, [&](World &w, bool original) -> uint64_t {
            auto *head = new TreeHeader::Node{reinterpret_cast<TreeHeader::Node *>(0x11110000u + token),
                                              reinterpret_cast<TreeHeader::Node *>(0x22220000u + token),
                                              reinterpret_cast<TreeHeader::Node *>(0x33330000u + token)};
            w.S()->mResources = {head, size};
            if (original) orig(w.S(), code);
            else w.S()->SetAge(code);
            const bool reset = head->mLeft == head && head->mParent == head && head->mRight == head;
            const uint64_t state = (reset ? 1u : 2u) | (static_cast<uint64_t>(w.S()->mResources.mSize) << 8) |
                                   (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(head->mParent) == 0x22220000u + token) << 40);
            const bool sameHead = w.S()->mResources.mHead == head;
            w.S()->mResources.mHead = nullptr;
            delete head;
            return state | (static_cast<uint64_t>(sameHead) << 41);
        });
    }
}

// ---- Face-0 slot 13: the morph-record purge -------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_RemoveMorphRecords, 0xcf6ff0)
{
    std::mt19937 rng(0xcf6ff0);
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xcf6ff0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    Redirect r1(ctx, 0xcebb70, reinterpret_cast<const void *>(&F_cebb70));
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        if (rng() % 3 == 0) sc.stack = RandomSpecs(rng, 10);
        static const int kKinds[] = {0, 1, 2, 7, 3, 5, 6, 8};
        for (auto &s : sc.stack)
            if (rng() % 2) s.kind = kKinds[rng() % 8];
        if (rng() % 4 == 0) for (auto &s : sc.stack) s.kind = kKinds[rng() % 4]; // all removed
        if (rng() % 6 == 0) for (auto &s : sc.stack) s.kind = kKinds[4 + rng() % 4]; // none removed
        if (!sc.stack.empty() && rng() % 3 == 0) sc.stack.back().items = 2; // items carried along by the assignment
        Compare(ctx, "RemoveMorphRecords", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.full);
            else nTSCASCore::cTSCASBodyPartSystemAdapters::RemoveMorphRecords(reinterpret_cast<System *>(w.full), nullptr);
            return 0;
        });
    }
}
