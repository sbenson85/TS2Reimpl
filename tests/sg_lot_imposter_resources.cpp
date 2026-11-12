// Differential tests of the lot imposter's resource helpers (src/sg/cLotImposterResources.cpp; lane 261108j, scenegraph
// row LT2a).
//
//   LotImposterResources_LotGroup  GetLotGroup 0x401790: the neighbourhood (GetCachedSingleton's cell 0x10cc288, its
//       slot 6, sometimes none) asked for the lot (slot 57; found or not), or without one the cached slot-23 object (cell
//       0x10cc2a8, its slot 9; found or not); the lot's name (slot 8: a string fake, slots 7 and 6; random text, empty
//       too) handed to the persist service (cell 0x10faf6c, slot 11) as a string. The result, the group written (over a
//       sentinel) and every count.
//   LotImposterResources_Load      LoadImposterResources 0xb40560 over random key lists (materials, textures, scene
//       graphs, other types; groups the global group 0x1c0532fa or others): the resource database (the persist service's
//       slot 3, its slot 3 finding a resource by key, interface and group or not, so the global-group retry is taken or
//       not), each material's name resolved (ResolveResourceName 0xae7340 stood in for) and set, each texture image's
//       name (or none) and kind (6: the cube class) looked up in the texture manager (cell 0x10f8f94: Has, FindOrLoad,
//       GetTexture, Add), a texture made through the director framework (GetDirectorFrameworkObject 0x1ae18d stood in
//       for) or an existing one asked for its image face (there or not); the scene node written over a held one or
//       null. The result and every count.
#include "code_patch.h"
#include "sgfx.h"

#include "rz/StaticGetters.h"
#include "sg/cLotImposter.h"
#include "sg/cTextureManager.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

namespace {

using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using nTSSG::ImposterKey;
using nTSSG::ImposterKeyRange;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

constexpr At kSingletonAt[] = {{6, {0, Ret::Int, true, "GetNeighborhood", ""}}};
constexpr auto kSingletonSlots = Table<7>(kSingletonAt);
constexpr drawfx::Iface kSingleton{"Singleton", 7, kSingletonSlots.data(), false, drawfx::kTestTable};

constexpr At kNhoodAt[] = {{57, {1, Ret::Int, true, "GetLot", "u"}}};
constexpr auto kNhoodSlots = Table<58>(kNhoodAt);
constexpr drawfx::Iface kNhood{"Neighborhood", 58, kNhoodSlots.data(), false, drawfx::kTestTable};

constexpr At kSlot23At[] = {{9, {0, Ret::Int, true, "GetLot", ""}}};
constexpr auto kSlot23Slots = Table<10>(kSlot23At);
constexpr drawfx::Iface kSlot23{"Slot23Object", 10, kSlot23Slots.data(), false, drawfx::kTestTable};

constexpr At kLotAt[] = {{8, {0, Ret::Int, true, "GetName", ""}}};
constexpr auto kLotSlots = Table<9>(kLotAt);
constexpr drawfx::Iface kLot{"Lot", 9, kLotSlots.data(), false, drawfx::kTestTable};

constexpr At kNameAt[] = {{6, {0, Ret::Int, true, "Data", ""}}, {7, {0, Ret::Int, true, "Strlen", ""}}};
constexpr auto kNameSlots = Table<8>(kNameAt);
constexpr drawfx::Iface kName{"NameString", 8, kNameSlots.data(), false, drawfx::kTestTable};

constexpr At kPersistAt[] = {{3, {0, Ret::Int, true, "GetDatabase", ""}}, {11, {1, Ret::Int, true, "GroupOf", "S"}}};
constexpr auto kPersistSlots = Table<12>(kPersistAt);
constexpr drawfx::Iface kPersist{"PersistService", 12, kPersistSlots.data(), false, drawfx::kTestTable};

constexpr At kDatabaseAt[] = {{3, {5, Ret::Int, true, "GetResource", "W4uauu"}}};
constexpr auto kDatabaseSlots = Table<4>(kDatabaseAt);
constexpr drawfx::Iface kDatabase{"ResourceDatabase", 4, kDatabaseSlots.data(), false, drawfx::kTestTable};

constexpr At kMaterialAt[] = {{10, {1, Ret::Int, true, "SetName", "s"}}, {19, {0, Ret::Int, true, "GetName", ""}}};
constexpr auto kMaterialSlots = Table<20>(kMaterialAt);
constexpr drawfx::Iface kMaterial{"Material", 20, kMaterialSlots.data(), false, drawfx::kTestTable};

constexpr At kImageAt[] = {{25, {0, Ret::Int, true, "GetKind", ""}}, {37, {0, Ret::Int, true, "GetName", ""}}};
constexpr auto kImageSlots = Table<38>(kImageAt);
constexpr drawfx::Iface kImage{"TextureImage", 38, kImageSlots.data(), false, drawfx::kTestTable};

constexpr At kTexturesAt[] = {{17, {1, Ret::Int, true, "FindOrLoad", "s"}},
                              {20, {3, Ret::Int, true, "Add", "asu"}},
                              {23, {1, Ret::Int, true, "GetTexture", "u"}},
                              {25, {1, Ret::Int, true, "Has", "s"}}};
constexpr auto kTexturesSlots = Table<26>(kTexturesAt);
constexpr drawfx::Iface kTextures{"TextureManager", 26, kTexturesSlots.data(), false, drawfx::kTestTable};

constexpr At kFrameworkAt[] = {{3, {3, Ret::Int, true, "Create", "uua"}}};
constexpr auto kFrameworkSlots = Table<4>(kFrameworkAt);
constexpr drawfx::Iface kFramework{"Framework", 4, kFrameworkSlots.data(), false, drawfx::kTestTable};

constexpr At kTextureAt[] = {{9, {2, Ret::Int, true, "SetImage", "au"}}};
constexpr auto kTextureSlots = Table<10>(kTextureAt);
constexpr drawfx::Iface kTexture{"Texture", 10, kTextureSlots.data(), false, drawfx::kTestTable};

constexpr At kPlainAt[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
constexpr auto kPlainSlots = Table<3>(kPlainAt);
constexpr drawfx::Iface kPlain{"Plain", 3, kPlainSlots.data(), false, drawfx::kTestTable};

constexpr uint32_t kSingletonCell = 0x10cc288, kSlot23Cell = 0x10cc2a8, kPersistCell = 0x10faf6c;
constexpr uint32_t kQueriedCell = 0x10f8fcc, kTexturesCell = 0x10f8f94;
constexpr uint32_t kMaterialType = 0x49596978, kTextureType = 0x1c4a276c, kSceneType = 0xe519c933;
constexpr uint32_t kMaterialIid = 0x49596972, kTextureIid = 0xfc49f6ee, kResourceNodeIid = 0xe519c92c;
constexpr uint32_t kTextureIfaceIid = 0x2a956da8;
constexpr uint32_t kGlobalGroup = 0x1c0532fa;
constexpr int kKeys = 8;

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }

struct World {
    Fake singleton, nhood, slot23, lot, name, persist, database, queried, textures, framework, node, held;
    Fake materials[kKeys], images[kKeys], created[kKeys], existing[2], existingTexture;
    char nameText[24] = {};
    char materialNames[kKeys][16] = {};
    char imageNames[kKeys][16] = {};
    uint32_t salt = 0;
    ImposterKey keys[kKeys] = {};
    ImposterKeyRange range = {};
    int createdCount = 0;
};
World gw;

int IndexOf(const ImposterKey *key)
{
    for (int k = 0; k < kKeys; ++k)
        if (gw.keys[k].mInstance == key->mInstance && gw.keys[k].mType == key->mType) return k;
    return 0;
}

// Found or not by the key's words, the interface asked for and the seed's salt (the global group a separate draw).
uint32_t GetResource(Fake &, const uint32_t *a)
{
    const auto *key = reinterpret_cast<const ImposterKey *>(uintptr_t(a[0]));
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    const uint32_t h = (key->mGroup ^ key->mInstance ^ a[1] ^ gw.salt) * 2654435761u;
    Fake *f = nullptr;
    const int k = IndexOf(key);
    if (a[1] == kMaterialIid) f = &gw.materials[k];
    else if (a[1] == kTextureIid) f = &gw.images[k];
    else if (a[1] == kResourceNodeIid) f = &gw.node;
    if ((h >> 29) < 3) {
        // A failed lookup sometimes leaves a counted object behind, so the caller's release before the retry shows.
        if (f && (h & 0x100) && !*out) {
            ++f->refs;
            *out = f;
        }
        return 0;
    }
    if (!f) return 0;
    ++f->refs;
    *out = f;
    return 1;
}
uint32_t Create(Fake &, const uint32_t *a)
{
    Fake &f = gw.created[gw.createdCount++ % kKeys];
    ++f.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &f;
    return 1;
}

bool __cdecl ResolveStand(cIGZString *out, const char *text, uint32_t group, const char *groupName)
{
    drawfx::Note("resolve", {group, drawfx::P(groupName)});
    sgfx::NoteText("resolve text", text);
    sgfx::NoteText("resolve out before", out->ToChar());
    const std::string name = std::string(text) + "#" + std::to_string(group & 0xffff);
    out->FromChar(name.c_str());
    return true;
}
void *__cdecl FrameworkStand()
{
    drawfx::Note("framework", 0);
    return &gw.framework;
}

void RandomText(std::mt19937 &g, char *out, size_t size)
{
    const size_t n = g() % (size - 1);
    for (size_t i = 0; i < n; ++i) out[i] = char('a' + g() % 26);
    out[n] = 0;
}

void BuildWorld(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 g(seed * 2246822519u + 7u);
    gw.salt = g();
    gw.createdCount = 0;
    gw.singleton.Reset<kSingleton>(800);
    gw.nhood.Reset<kNhood>(801);
    gw.slot23.Reset<kSlot23>(802);
    gw.lot.Reset<kLot>(803);
    gw.name.Reset<kName>(804);
    gw.persist.Reset<kPersist>(805);
    gw.database.Reset<kDatabase>(806);
    gw.queried.Reset<kPlain>(807);
    gw.textures.Reset<kTextures>(808);
    gw.framework.Reset<kFramework>(809);
    gw.node.Reset<kPlain>(810);
    gw.held.Reset<kPlain>(811);
    gw.singleton.Returns(6, g() % 3 == 0 ? nullptr : &gw.nhood);
    gw.nhood.Returns(57, g() % 4 == 0 ? nullptr : &gw.lot);
    gw.slot23.Returns(9, g() % 4 == 0 ? nullptr : &gw.lot);
    gw.lot.Returns(8, &gw.name);
    RandomText(g, gw.nameText, sizeof(gw.nameText));
    gw.name.Returns(6, gw.nameText);
    gw.name.Returns(7, uint32_t(std::strlen(gw.nameText)));
    gw.persist.Returns(3, &gw.database);
    gw.persist.Returns(11, uint32_t(g()));
    gw.database.On(3, &GetResource);
    gw.framework.On(3, &Create);
    gw.existing[0].Reset<kPlain>(812);
    gw.existing[1].Reset<kPlain>(813);
    gw.existingTexture.Reset<kTexture>(814);
    gw.existing[0].AddFace(kTextureIfaceIid, gw.existingTexture);
    gw.textures.Returns(17, uint32_t(g() % 50));
    gw.textures.Returns(20, uint32_t(g() % 50));
    gw.textures.Returns(23, &gw.existing[g() % 2]);
    gw.textures.Returns(25, g() % 2);
    for (int k = 0; k < kKeys; ++k) {
        gw.materials[k].Reset<kMaterial>(820 + k);
        RandomText(g, gw.materialNames[k], sizeof(gw.materialNames[k]));
        gw.materials[k].Returns(19, gw.materialNames[k]);
        gw.materials[k].Returns(10, g() % 2);
        gw.images[k].Reset<kImage>(830 + k);
        RandomText(g, gw.imageNames[k], sizeof(gw.imageNames[k]));
        gw.images[k].Returns(37, g() % 5 == 0 ? nullptr : gw.imageNames[k]);
        gw.images[k].Returns(25, Pick(g, {6u, 0u, 1u, 5u}));
        gw.created[k].Reset<kTexture>(840 + k);
        gw.keys[k] = {Pick(g, {kMaterialType, kTextureType, kSceneType, 0xcd8b6498u, 0x12345678u, kMaterialType, kTextureType}),
                      g() % 3 == 0 ? kGlobalGroup : uint32_t(g()), uint32_t(g() & ~7u) | uint32_t(k), uint32_t(g() % 3)};
    }
    drawfx::AddRange(gw.keys, sizeof(gw.keys), 0x54000000);
    drawfx::AddRange(&gw.range, sizeof(gw.range), 0x55000000);
    gw.range = {gw.keys, gw.keys + g() % (kKeys + 1)};
    cells.Set(kSingletonCell, &gw.singleton);
    cells.Set(kSlot23Cell, &gw.slot23);
    cells.Set(kPersistCell, &gw.persist);
    cells.Set(kQueriedCell, &gw.queried);
    cells.Set(kTexturesCell, &gw.textures);
}

void NoteRefs()
{
    std::vector<uint32_t> w;
    for (Fake *f : {&gw.singleton, &gw.nhood, &gw.slot23, &gw.lot, &gw.name, &gw.persist, &gw.database, &gw.queried,
                    &gw.textures, &gw.framework, &gw.node, &gw.held, &gw.existing[0], &gw.existing[1], &gw.existingTexture})
        w.push_back(uint32_t(f->refs));
    for (int k = 0; k < kKeys; ++k)
        w.push_back(uint32_t(gw.materials[k].refs + 100 * gw.images[k].refs + 10000 * gw.created[k].refs));
    drawfx::Call c;
    c.note = "refs";
    c.words = w;
    drawfx::gLog.push_back(std::move(c));
}

using LotGroupFn = bool(__cdecl *)(uint32_t, uint32_t *);
using LoadFn = bool(__cdecl *)(const ImposterKeyRange *, nGZSceneGraph::cIResourceNode **);

} // namespace

// Verifies: 0x401790 (GetLotGroup).
DIFF_TEST(LotImposterResources_LotGroup, 0x401790)
{
    unsigned found = 0, missed = 0;
    sgfx::CheckSides(
        ctx, "lot group", 2000, [&](uint32_t seed, drawfx::Cells &cells) { BuildWorld(seed, cells); },
        [&](bool original, std::mt19937 &g) {
            uint32_t group = 0xdeadbeef;
            const uint32_t lot = g() % 4 == 0 ? 0 : uint32_t(g());
            const bool r = original ? ctx.Original<LotGroupFn>(0x401790)(lot, &group) : nTSSG::GetLotGroup(lot, &group);
            drawfx::Note("lot group", {uint32_t(r), group});
            if (!original) (r ? found : missed) += 1;
            NoteRefs();
            ++ctx.cases;
        });
    if (found == 0 || missed == 0) ctx.Fail("lot group: %u found, %u missed", found, missed);
}

// Verifies: 0xb40560 (LoadImposterResources).
DIFF_TEST(LotImposterResources_Load, 0xb40560)
{
    shape::StandIn resolve(0xae7340, &nGZSceneGraph::ResolveResourceName, &ResolveStand);
    shape::StandIn framework(0x1ae18d, &GetDirectorFrameworkObject, &FrameworkStand);
    unsigned loaded = 0, created = 0;
    sgfx::CheckSides(
        ctx, "imposter resources", 3000, [&](uint32_t seed, drawfx::Cells &cells) { BuildWorld(seed, cells); },
        [&](bool original, std::mt19937 &g) {
            nGZSceneGraph::cIResourceNode *node = nullptr;
            if (g() % 3 == 0) {
                ++gw.held.refs;
                node = reinterpret_cast<nGZSceneGraph::cIResourceNode *>(&gw.held);
            }
            const bool r = original ? ctx.Original<LoadFn>(0xb40560)(&gw.range, &node)
                                    : nTSSG::LoadImposterResources(&gw.range, &node);
            drawfx::Note("load", {uint32_t(r), drawfx::Ptr(node), uint32_t(gw.createdCount)});
            if (!original) {
                loaded += r;
                created += gw.createdCount != 0;
            }
            NoteRefs();
            ++ctx.cases;
        });
    if (loaded == 0 || created == 0) ctx.Fail("imposter resources: %u loaded, %u made textures", loaded, created);
}
