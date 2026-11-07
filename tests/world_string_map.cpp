// nTSWorld::cStringMap<unsigned short> (lane VQ, campaign world-lot WL-0c): differential tests, original (by RVA) against
// ours on equal tables: the constructor 0x813b30, the deleting destructor 0x8141f0, GetGZCLSID 0x815980, slots 7-9
// (0x819d20, 0x816050, 0x816680), Write 0x81a740 and Read 0x816d90.
// - Tables are built with our layout and the game's vtable (or a copy of it whose slot 5 answers another class id), the
//   names taken from a static pool, so a name is dumped as where it points (pool entry, an entry Read made, or other) and
//   its text's hash. Maps are dumped as trees (colour, parent link, key, value in preorder; the head's extremes).
// - Streams: gridfx's slot-tagged fakes (tests/world_grid_fx.h), mode A, and mode B only when the header and the first
//   entry are whole (the original's id / use count locals are uninitialised until the first entry is read), a throw at
//   every stream call in turn.
// - Read's catalog: svcfx fakes (tests/ts_services_fixture.h) for the cached app singleton (slot 16: the catalog), the
//   catalog (slot 42: the lookup, answering a pool name for some names; slot 10: filing a new entry) and the director
//   framework (slot 3: GetClassObject). The entry the framework makes is a test object with a vbtable pointer at +4 whose
//   virtual bases 1 and 2 are fakes; its slots 0 / 1 / 7 log and keep the name in a buffer of the test's.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "shape.h"
#include "ts_services_fixture.h"
#include "world/cStringMap.h"
#include "world_grid_fx.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {
using gridfx::Begin;
using gridfx::Hash;
using gridfx::InStream;
using gridfx::Item;
using gridfx::OutStream;
using gridfx::S;
using gridfx::U;
using nTSWorld::cStringMap;
using nTSWorld::cStringMapAdapters;
using svcfx::F;
using svcfx::Obj;
using Blob = std::vector<uint32_t>;

constexpr uint32_t kCtor = 0x813b30, kDtor = 0x8141f0, kClsid = 0x815980, kNameOf = 0x816050, kNextId = 0x816680,
                   kRead = 0x816d90, kIdOf = 0x819d20, kWrite = 0x81a740;
constexpr uint32_t kOtherClsid = 0x5eed0002;
constexpr uint32_t kMadeTag = 0x7e000000;

// The fakes: the app singleton, the catalog, the entry the catalog answers, the framework, the made entry's virtual bases.
enum : unsigned { kApp = 1, kCatalog, kEntry, kFramework, kBase1, kBase2 };

// Names that differ only in case (one name to _stricmp), around '_' / '[' / '`', empty, long; each its own buffer.
const char kPool[][40] = {"", "a", "A", "alpha", "Alpha", "ALPHA", "beta", "Beta", "_under", "[bracket", "`tick", "zeta",
    "Zeta2", "a name well over fifteen chars", "A NAME WELL OVER FIFTEEN CHARS", "mid", "MiD", "0digit", "floor_tile",
    "Floor_Tile"};
constexpr unsigned kPoolCount = unsigned(std::size(kPool));

// Buffers for the names Read files under entries it made (one per made entry, numbered per run).
char gMadeNames[64][48];
unsigned gMadeCount, gMadeCurrent;
uint32_t gSalt; // which pool names the catalog has, per run

uint32_t NameCode(const char *p)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(p);
    if (!p) return 0;
    const uintptr_t pool = reinterpret_cast<uintptr_t>(kPool), made = reinterpret_cast<uintptr_t>(gMadeNames);
    if (at >= pool && at < pool + sizeof(kPool) && (at - pool) % sizeof(kPool[0]) == 0) return 0x1000 + uint32_t((at - pool) / sizeof(kPool[0]));
    if (at >= made && at < made + sizeof(gMadeNames) && (at - made) % sizeof(gMadeNames[0]) == 0) return 0x2000 + uint32_t((at - made) / sizeof(gMadeNames[0]));
    return 0x3000;
}
void DumpName(Blob &out, const char *p)
{
    out.push_back(NameCode(p));
    out.push_back(p ? Hash(p) : 0);
}

// ---- the entry Read makes when the catalog lacks a name ----

struct Made {
    void **vt;
    const int32_t *vbptr; // +4
};
Made gMade;
void *gMadeVT[8];
int32_t gMadeBases[3];

uint32_t __fastcall MadeMode(Made *, void *, uint32_t mode)
{
    svcfx::Ev(0x80), svcfx::Ev(mode);
    return 0;
}
uint32_t __fastcall MadeName(Made *, void *, const char *name)
{
    svcfx::Ev(0x81), svcfx::Ev(name ? Hash(name) : 0xdead);
    gMadeCurrent = gMadeCount++ % std::size(gMadeNames);
    strncpy_s(gMadeNames[gMadeCurrent], name ? name : "", _TRUNCATE);
    return 0;
}
const char *__fastcall MadeKey(Made *, void *)
{
    svcfx::Ev(0x87), svcfx::Ev(gMadeCurrent);
    return gMadeNames[gMadeCurrent];
}
uint32_t __fastcall MadeOther(Made *, void *)
{
    svcfx::Ev(0x8f);
    return 0;
}

uint32_t CatalogFind(Obj *, uint32_t *args)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(args[1]));
    svcfx::Ev(0x90), svcfx::Ev(name ? Hash(name) : 0xdead);
    if (!name) return 0;
    for (unsigned i = 0; i < kPoolCount; ++i) {
        if (_stricmp(kPool[i], name) != 0) continue;
        if ((i + gSalt) % 3 == 0) return 0; // the catalog lacks this one
        F(kEntry).ret[7] = svcfx::P(kPool[i]);
        return svcfx::P(&F(kEntry));
    }
    return 0;
}
uint32_t FrameworkMake(Obj *, uint32_t *args)
{
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &gMade;
    return 1;
}

// Fakes for one run (both sides start alike). `salt` picks which pool names the catalog has.
void BuildFakes(uint32_t salt)
{
    svcfx::ResetAll();
    gSalt = salt;
    gMadeCount = gMadeCurrent = 0;
    std::memset(gMadeNames, 0, sizeof(gMadeNames));
    for (void *&slot : gMadeVT) slot = reinterpret_cast<void *>(&MadeOther);
    gMadeVT[0] = reinterpret_cast<void *>(&MadeMode);
    gMadeVT[1] = reinterpret_cast<void *>(&MadeName);
    gMadeVT[7] = reinterpret_cast<void *>(&MadeKey);
    gMade.vt = gMadeVT;
    gMade.vbptr = gMadeBases;
    const auto vbptrAt = reinterpret_cast<intptr_t>(&gMade.vbptr);
    gMadeBases[0] = -4;
    gMadeBases[1] = int32_t(reinterpret_cast<intptr_t>(&F(kBase1)) - vbptrAt);
    gMadeBases[2] = int32_t(reinterpret_cast<intptr_t>(&F(kBase2)) - vbptrAt);
    svcfx::AddRange(&gMade, sizeof(gMade), kMadeTag);

    F(kApp).Set(16, 0, &F(kCatalog));
    F(kCatalog).Set(42, "dxd", 0, &CatalogFind);
    F(kCatalog).Set(10, "dp", 0x1010);
    F(kEntry).Set(7, 0, 0u);
    F(kFramework).Set(3, "ddp", 0, &FrameworkMake);
    F(kBase2).Set(6, "d", 0x66);
    F(kBase2).Set(1, "d", 0x11);
    svcfx::gFrameworkIndex = kFramework;
}

// ---- tables ----

uint32_t __fastcall OtherClsid(void *, void *) { return kOtherClsid; }

// A copy of the game's table with slot 5 answering kOtherClsid.
void *const *AltTable()
{
    static void *table[10];
    std::memcpy(table, shape::GameVTable(cStringMap::kVTable), sizeof(table));
    table[5] = reinterpret_cast<void *>(&OtherClsid);
    return table;
}

// Map A never holds a null name in the game (its helpers and Write build std::strings from the names); map B may.
const char *PoolName(std::mt19937 &rng) { return kPool[rng() % kPoolCount]; }
const char *PoolNameOrNull(std::mt19937 &rng) { return rng() % 13 ? kPool[rng() % kPoolCount] : nullptr; }
uint16_t NewId(std::mt19937 &rng) { return uint16_t(rng() % 4 ? rng() % 12 : rng()); }

cStringMap *Make(uint32_t seed, uint32_t entries, bool alt = false, uint32_t names = ~0u)
{
    std::mt19937 rng(seed);
    void *block = ::operator new(sizeof(cStringMap));
    auto *m = new (block) cStringMap();
    *reinterpret_cast<void *const **>(m) = alt ? AltTable() : shape::GameVTable(cStringMap::kVTable);
    m->mRefCount = 1 + seed % 3;
    m->mWord8 = rng() % 3 ? rng() % 2 : uint32_t(rng());
    m->mByteC = uint8_t(rng() % 4 ? rng() % 2 : rng());
    for (uint32_t i = 0; i < entries; ++i) m->mUses[PoolName(rng)] = {NewId(rng), rng() % 3 ? rng() % 9 : uint32_t(rng())};
    for (uint32_t i = 0; i < (names == ~0u ? entries : names); ++i) m->mNames[NewId(rng)] = PoolNameOrNull(rng);
    return m;
}

void Destroy(cStringMap *m)
{
    m->mUses.~map();
    m->mNames.~map();
    ::operator delete(m, sizeof(cStringMap));
}

struct RawNode {
    RawNode *left, *parent, *right;
    uint8_t color, isnil;
    uint8_t pad[2];
    const char *Value() const { return reinterpret_cast<const char *>(this) + 0x10; }
};
// Map A's value: name +0x10, id +0x14, use count +0x18; map B's: id +0x10, name +0x14.
void DumpUse(Blob &out, const char *v)
{
    DumpName(out, *reinterpret_cast<const char *const *>(v));
    out.push_back(*reinterpret_cast<const uint16_t *>(v + 4));
    out.push_back(*reinterpret_cast<const uint32_t *>(v + 8));
}
void DumpId(Blob &out, const char *v)
{
    out.push_back(*reinterpret_cast<const uint16_t *>(v));
    DumpName(out, *reinterpret_cast<const char *const *>(v + 4));
}
void DumpNode(Blob &out, const RawNode *node, const RawNode *parent, void (*value)(Blob &, const char *))
{
    if (node->isnil) {
        out.push_back(0xeeee0000u | node->color);
        return;
    }
    out.insert(out.end(), {uint32_t(node->color), uint32_t(node->parent == parent)});
    value(out, node->Value());
    DumpNode(out, node->left, node, value);
    DumpNode(out, node->right, node, value);
}
void DumpTree(Blob &out, const void *map, void (*value)(Blob &, const char *))
{
    const RawNode *head = *reinterpret_cast<RawNode *const *>(map);
    out.insert(out.end(), {reinterpret_cast<const uint32_t *>(map)[1], head->color, head->isnil});
    const RawNode *root = head->parent;
    DumpNode(out, root, head, value);
    const RawNode *least = head, *most = head;
    if (!root->isnil) {
        for (least = root; !least->left->isnil;) least = least->left;
        for (most = root; !most->right->isnil;) most = most->right;
    }
    out.push_back(uint32_t(head->left == least) | uint32_t(head->right == most) << 1 | uint32_t(root->isnil ? root == head : 1) << 2);
}

uint32_t VTableCode(const void *vt)
{
    static const uint32_t kFace[] = {0};
    // The game's table or ours: "a cStringMap table" (the original's constructor stores the one, ours the other).
    if (vt == shape::GameVTable(cStringMap::kVTable) || vt == shape::Ours<cStringMap>(kFace).face[0]) return 1;
    if (vt == AltTable()) return 3;
    return shape::P(vt);
}

void Capture(Blob &out, const cStringMap *m)
{
    out.insert(out.end(), {VTableCode(*reinterpret_cast<void *const *>(m)), m->mRefCount, m->mWord8, m->mByteC});
    DumpTree(out, &m->mUses, &DumpUse);
    DumpTree(out, &m->mNames, &DumpId);
}

// ---- the constructor and the deleting destructor ----

void CtorCases(difftest::Context &ctx)
{
    for (uint32_t fill : {0u, 0xcdu, 0x5au}) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            void *block = ::operator new(sizeof(cStringMap));
            std::memset(block, int(fill), sizeof(cStringMap));
            auto *m = static_cast<cStringMap *>(block);
            cStringMap *r = side ? cStringMapAdapters::Construct(m, nullptr) : ctx.Original<cStringMap *(__thiscall *)(void *)>(kCtor)(block);
            blobs[side] = {uint32_t(r == m)};
            Capture(blobs[side], m);
            Destroy(m);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("constructor over 0x%02x: table differs", fill);
        ++ctx.cases;
    }
}

void DtorCases(difftest::Context &ctx)
{
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (uint32_t entries : {0u, 1u, 2u, 7u, 30u}) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            cStringMap *m = Make(flags * 31 + entries, entries);
            difftest::FreeRecorder rec;
            void *r = side ? cStringMapAdapters::DeletingDestructor(m, nullptr, flags)
                           : ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDtor)(m, flags);
            rec.Stop();
            size_t sizes[256]{};
            const size_t freedCount = rec.SortedSizes(sizes, 256);
            blobs[side] = {uint32_t(r == m), uint32_t(rec.Freed(m)), uint32_t(freedCount)};
            for (size_t i = 0; i < freedCount && i < 256; ++i) blobs[side].push_back(uint32_t(sizes[i]));
            if (!rec.Freed(m)) {
                // The maps are gone: only the plain fields are looked at, then the block goes back.
                blobs[side].insert(blobs[side].end(), {m->mRefCount, m->mWord8, m->mByteC});
                ::operator delete(m, sizeof(cStringMap));
            }
        }
        if (blobs[0] != blobs[1]) ctx.Fail("DeletingDestructor flags %u entries %u: frees / fields differ", flags, entries);
        ++ctx.cases;
    }
}

void ClassIdCases(difftest::Context &ctx)
{
    for (int alt = 0; alt < 2; ++alt) {
        cStringMap *m = Make(5, 2, alt);
        const uint32_t original = ctx.Original<uint32_t(__thiscall *)(void *)>(kClsid)(m);
        const uint32_t ours = cStringMapAdapters::GetGZCLSID(m, nullptr);
        if (original != ours) ctx.Fail("GetGZCLSID alt %d: 0x%08x against ours 0x%08x", alt, original, ours);
        Destroy(m);
        ++ctx.cases;
    }
}

// ---- slots 7-9 ----

void IdOfCases(difftest::Context &ctx)
{
    for (uint32_t entries : {0u, 1u, 3u, 9u, 20u}) for (uint32_t seed = 0; seed < 6; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            cStringMap *m = Make(seed * 7 + entries, entries);
            std::mt19937 ask(seed);
            for (int q = 0; q < 24; ++q) {
                // A pool name, its text in another buffer (an other case of it or not), null, or a name nobody has.
                std::string copy = kPool[ask() % kPoolCount];
                if (ask() % 2) for (char &c : copy) c = char(ask() % 2 ? toupper(c) : tolower(c));
                const unsigned pick = ask() % 4;
                const char *name = pick == 0 ? kPool[ask() % kPoolCount] : pick == 1 ? copy.c_str() : pick == 2 ? nullptr : "nobody";
                const uint16_t r = side ? cStringMapAdapters::IdOf(m, nullptr, name)
                                        : ctx.Original<uint16_t(__thiscall *)(void *, const char *)>(kIdOf)(m, name);
                blobs[side].push_back(r);
            }
            Capture(blobs[side], m);
            Destroy(m);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("IdOf entries %u seed %u: ids / table differ", entries, seed);
        ++ctx.cases;
    }
}

void NameOfCases(difftest::Context &ctx)
{
    for (uint32_t names : {0u, 1u, 3u, 9u, 20u}) for (uint32_t seed = 0; seed < 6; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            cStringMap *m = Make(seed * 11 + names, 2, false, names);
            std::mt19937 ask(seed + 100);
            for (int q = 0; q < 24; ++q) {
                const uint16_t id = q < 3 ? uint16_t(0xffff - q) : NewId(ask);
                const char *r = side ? cStringMapAdapters::NameOf(m, nullptr, &id)
                                     : ctx.Original<const char *(__thiscall *)(void *, const uint16_t *)>(kNameOf)(m, &id);
                DumpName(blobs[side], r);
            }
            Capture(blobs[side], m);
            Destroy(m);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("NameOf names %u seed %u: names / table differ", names, seed);
        ++ctx.cases;
    }
}

void NextIdCases(difftest::Context &ctx)
{
    // Dense ids from 0 or 1 (the walk's only gap-finding case), gaps, the ids {0, 1} the byte-clear start counts twice,
    // and random sets; each with the byte clear, set and other values.
    std::vector<std::vector<uint16_t>> sets{{}, {0}, {1}, {0, 1}, {1, 2}, {0, 1, 2, 3}, {1, 2, 3, 4}, {0, 2, 3}, {1, 3},
        {2, 3, 4}, {0, 1, 2, 5, 6}, {5}, {0xfffe, 0xffff}, {0, 0xffff}};
    std::mt19937 rng(9);
    for (int i = 0; i < 20; ++i) {
        std::vector<uint16_t> s;
        for (uint32_t n = rng() % 12; n; --n) s.push_back(uint16_t(rng() % 4 ? rng() % 10 : rng()));
        sets.push_back(s);
    }
    for (size_t k = 0; k < sets.size(); ++k) for (uint8_t byte : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(0x80)}) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            cStringMap *m = Make(uint32_t(k), 0, false, 0);
            m->mByteC = byte;
            for (uint16_t id : sets[k]) m->mNames[id] = kPool[id % kPoolCount];
            const uint16_t r = side ? cStringMapAdapters::NextId(m, nullptr) : ctx.Original<uint16_t(__thiscall *)(void *)>(kNextId)(m);
            blobs[side] = {r};
            Capture(blobs[side], m);
            Destroy(m);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("NextId set %zu byte %u: id %u against ours %u", k, byte, blobs[0][0], blobs[1][0]);
        ++ctx.cases;
    }
}

// ---- Write ----

Blob RunWrite(difftest::Context &ctx, bool ours, uint32_t seed, uint32_t entries, bool alt, int fail, int *total)
{
    Begin({}, fail);
    cStringMap *m = Make(seed, entries, alt);
    uint8_t returned = 0;
    bool threw = false;
    try {
        returned = ours ? cStringMapAdapters::Write(m, nullptr, OutStream())
                        : ctx.Original<uint8_t(__thiscall *)(void *, void *)>(kWrite)(m, OutStream());
    } catch (int) { threw = true; }
    gridfx::gFailAt = -1;
    if (total) *total = gridfx::gTick;
    Blob blob{returned, uint32_t(threw)};
    blob.insert(blob.end(), gridfx::gLog.begin(), gridfx::gLog.end());
    Capture(blob, m);
    Destroy(m);
    return blob;
}

void WriteCases(difftest::Context &ctx)
{
    const uint64_t before = gridfx::gThrown;
    for (uint32_t entries : {0u, 1u, 2u, 5u, 9u, 40u}) for (uint32_t seed : {1u, 2u, 3u}) for (int alt = 0; alt < 2; ++alt) {
        int total = 0;
        for (int fail = -1; fail < (entries <= 9 ? total : 0); ++fail) {
            const uint32_t s = entries * 17 + seed;
            Blob original = RunWrite(ctx, false, s, entries, alt, fail, fail < 0 ? &total : nullptr);
            Blob ours = RunWrite(ctx, true, s, entries, alt, fail, nullptr);
            if (original != ours) ctx.Fail("Write entries %u seed %u alt %d fail %d: result / calls / table differ", entries, seed, alt, fail);
            ++ctx.cases;
        }
    }
    std::printf("  Write: exceptions injected %llu\n", gridfx::gThrown - before);
}

// ---- Read ----

// A record: class id, version, name, count, then `entries` name / id / use count triples (names mostly from the pool, in
// any case; some the pool lacks; some numbers, which read as "").
std::vector<Item> Record(uint32_t seed, int32_t count, uint32_t entries, uint32_t classId = cStringMap::kClassId, uint32_t version = 1)
{
    std::mt19937 rng(seed);
    std::vector<Item> s{U(classId), U(version), rng() % 4 ? S("cStringMap") : S("other"), U(uint32_t(count))};
    for (uint32_t i = 0; i < entries; ++i) {
        const unsigned pick = rng() % 10;
        if (pick < 7) s.push_back(S(kPool[rng() % kPoolCount]));
        else if (pick < 9) s.push_back(S(("new name " + std::to_string(rng() % 4)).c_str()));
        else s.push_back(U(rng()));
        s.push_back(U(rng() % 4 ? rng() % 12 : rng()));
        s.push_back(U(rng() % 3 ? rng() % 9 : uint32_t(rng())));
    }
    return s;
}

Blob RunRead(difftest::Context &ctx, bool ours, const std::vector<Item> &script, uint32_t seed, uint32_t entries, bool alt,
    bool keep, int fail, int *total)
{
    BuildFakes(seed);
    Begin(script, fail, keep);
    cStringMap *m = Make(seed, entries, alt);
    uint8_t returned = 0;
    bool threw = false;
    try {
        returned = ours ? cStringMapAdapters::Read(m, nullptr, InStream())
                        : ctx.Original<uint8_t(__thiscall *)(void *, void *)>(kRead)(m, InStream());
    } catch (int) { threw = true; }
    gridfx::gFailAt = -1;
    if (total) *total = gridfx::gTick;
    Blob blob{returned, uint32_t(threw), uint32_t(gridfx::gCursor), gMadeCount};
    blob.insert(blob.end(), gridfx::gLog.begin(), gridfx::gLog.end());
    blob.push_back(0xe0e0e0e0);
    blob.insert(blob.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    blob.insert(blob.end(), {uint32_t(F(kBase1).refs), uint32_t(F(kBase2).refs)});
    Capture(blob, m);
    Destroy(m);
    return blob;
}

void ReadCases(difftest::Context &ctx)
{
    svcfx::Globals globals;
    globals.Set(svcfx::kAppCell, &F(kApp));
    const uint64_t before = gridfx::gThrown;
    struct Script {
        const char *what;
        std::vector<Item> items;
        bool alt;
        bool firstWhole; // the header and the first entry are whole (or there is none): mode B too
    };
    std::vector<Script> scripts;
    for (uint32_t seed : {1u, 2u, 3u}) {
        for (uint32_t n : {0u, 1u, 2u, 5u, 24u}) scripts.push_back({"whole", Record(seed * 7 + n, int32_t(n), n), false, true});
        scripts.push_back({"short", Record(seed + 40, 5, 3), false, true});
        scripts.push_back({"short entry", [&] { auto s = Record(seed + 41, 3, 3); s.pop_back(); return s; }(), false, true});
        scripts.push_back({"short first", [&] { auto s = Record(seed + 42, 2, 1); s.pop_back(); return s; }(), false, false});
        scripts.push_back({"long", Record(seed + 43, 2, 5), false, true});
        scripts.push_back({"negative count", Record(seed + 44, -3, 2), false, true});
        scripts.push_back({"class id", Record(seed + 45, 2, 2, cStringMap::kClassId + 1), false, true});
        scripts.push_back({"version 0", Record(seed + 46, 2, 2, cStringMap::kClassId, 0), false, true});
        scripts.push_back({"version 2", Record(seed + 47, 2, 2, cStringMap::kClassId, 2), false, true});
        scripts.push_back({"other table", Record(seed + 48, 2, 2, kOtherClsid), true, true});
        scripts.push_back({"other table, our id", Record(seed + 49, 2, 2), true, true});
        scripts.push_back({"header only", {U(cStringMap::kClassId), U(1), S("cStringMap")}, false, false});
        scripts.push_back({"empty", {}, false, false});
    }
    for (const Script &script : scripts) for (uint32_t entries : {0u, 3u, 12u}) for (int keep = 0; keep < 2; ++keep) {
        if (keep && !script.firstWhole) continue;
        int total = 0;
        const bool sweep = script.items.size() <= 24;
        for (int fail = -1; fail < (sweep ? total : 0); ++fail) {
            const uint32_t seed = uint32_t(script.items.size()) * 5 + entries;
            Blob original = RunRead(ctx, false, script.items, seed, entries, script.alt, keep, fail, fail < 0 ? &total : nullptr);
            Blob ours = RunRead(ctx, true, script.items, seed, entries, script.alt, keep, fail, nullptr);
            if (original != ours) ctx.Fail("Read %s entries %u mode %c fail %d: result / calls / table differ", script.what, entries, keep ? 'B' : 'A', fail);
            ++ctx.cases;
        }
    }
    std::printf("  Read: exceptions injected %llu\n", gridfx::gThrown - before);
}
} // namespace

DIFF_TEST(StringMap_Constructor, 0x813b30) { CtorCases(ctx); }
DIFF_TEST(StringMap_DeletingDestructor, 0x8141f0) { DtorCases(ctx); }
DIFF_TEST(StringMap_GetGZCLSID, 0x815980) { ClassIdCases(ctx); }
DIFF_TEST(StringMap_IdOf, 0x819d20) { IdOfCases(ctx); }
DIFF_TEST(StringMap_NameOf, 0x816050) { NameOfCases(ctx); }
DIFF_TEST(StringMap_NextId, 0x816680) { NextIdCases(ctx); }
DIFF_TEST(StringMap_Write, 0x81a740) { WriteCases(ctx); }
DIFF_TEST(StringMap_Read, 0x816d90) { ReadCases(ctx); }
