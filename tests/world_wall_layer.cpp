// nTSWorld::cWallLayer (lane VQ, campaign world-lot WL-0c): differential tests, original (by RVA) against ours on equal
// layers: the constructor 0x81ff50, the destructor body 0x81ffa0, the deleting destructor 0x820060, QueryInterface
// 0x8200d0, GetGZCLSID 0x8200c0, the members Clear 0x8200b0, Erase 0x820240, SetHalf 0x820290, Has 0x820300, Half
// 0x820340, First 0x8203a0, Write 0x8203f0 and Read 0x820100.
// - Layers are built with our layout and the game's vtable, or a copy of it whose slot 5 answers another class id and
//   whose slot 1 (AddRef) adds 0x100 (so QueryInterface's AddRef through the vtable shows). The map is dumped as a tree
//   (colour, parent link, key, both words in preorder; the head's extremes).
// - Streams: gridfx's slot-tagged fakes (tests/world_grid_fx.h), mode A, and mode B only when the header and the first
//   entry are whole (the original's entry locals are uninitialised until the first entry is read), a throw at every
//   stream call in turn.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "shape.h"
#include "world/cWallLayer.h"
#include "world_grid_fx.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {
using gridfx::Begin;
using gridfx::InStream;
using gridfx::Item;
using gridfx::OutStream;
using gridfx::S;
using gridfx::U;
using nTSWorld::cWallLayer;
using nTSWorld::cWallLayerAdapters;
using Blob = std::vector<uint32_t>;

constexpr uint32_t kCtor = 0x81ff50, kBody = 0x81ffa0, kDtor = 0x820060, kClear = 0x8200b0, kClsid = 0x8200c0,
                   kQuery = 0x8200d0, kRead = 0x820100, kErase = 0x820240, kSetHalf = 0x820290, kHas = 0x820300,
                   kHalf = 0x820340, kFirst = 0x8203a0, kWrite = 0x8203f0;
constexpr uint32_t kOtherClsid = 0x5eed0003;

uint32_t __fastcall OtherClsid(void *, void *) { return kOtherClsid; }
uint32_t __fastcall BigAddRef(cWallLayer *self, void *) { return self->mRefCount += 0x100; }

// A copy of the game's table: slot 5 answers kOtherClsid, slot 1 adds 0x100.
void *const *AltTable()
{
    static void *table[7];
    std::memcpy(table, shape::GameVTable(cWallLayer::kVTable), sizeof(table));
    table[1] = reinterpret_cast<void *>(&BigAddRef);
    table[5] = reinterpret_cast<void *>(&OtherClsid);
    return table;
}

int32_t Key(std::mt19937 &rng) { return rng() % 4 ? int32_t(rng() % 24) - 4 : int32_t(rng()); }
uint32_t Word(std::mt19937 &rng) { return rng() % 3 ? rng() % 5 : uint32_t(rng()); }

cWallLayer *Make(uint32_t seed, uint32_t entries, bool alt = false)
{
    std::mt19937 rng(seed);
    void *block = ::operator new(sizeof(cWallLayer));
    auto *m = new (block) cWallLayer();
    *reinterpret_cast<void *const **>(m) = alt ? AltTable() : shape::GameVTable(cWallLayer::kVTable);
    m->mRefCount = 1 + seed % 3;
    for (uint32_t i = 0; i < entries; ++i) m->mEntries[Key(rng)] = {Word(rng), Word(rng)};
    return m;
}

void Destroy(cWallLayer *m)
{
    m->mEntries.~map();
    ::operator delete(m, sizeof(cWallLayer));
}

struct RawNode {
    RawNode *left, *parent, *right;
    uint8_t color, isnil;
    uint8_t pad[2];
    int32_t key;
    uint32_t first, second;
};
void DumpNode(Blob &out, const RawNode *node, const RawNode *parent)
{
    if (node->isnil) {
        out.push_back(0xeeee0000u | node->color);
        return;
    }
    out.insert(out.end(), {uint32_t(node->color), uint32_t(node->parent == parent), uint32_t(node->key), node->first, node->second});
    DumpNode(out, node->left, node);
    DumpNode(out, node->right, node);
}
void DumpTree(Blob &out, const void *map)
{
    const RawNode *head = *reinterpret_cast<RawNode *const *>(map);
    out.insert(out.end(), {reinterpret_cast<const uint32_t *>(map)[1], head->color, head->isnil});
    const RawNode *root = head->parent;
    DumpNode(out, root, head);
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
    // The game's table or ours: "a cWallLayer table" (the original's constructor stores the one, ours the other).
    if (vt == shape::GameVTable(cWallLayer::kVTable) || vt == shape::Ours<cWallLayer>(kFace).face[0]) return 1;
    if (vt == AltTable()) return 3;
    return shape::P(vt);
}

void Capture(Blob &out, const cWallLayer *m)
{
    out.insert(out.end(), {VTableCode(*reinterpret_cast<void *const *>(m)), m->mRefCount});
    DumpTree(out, &m->mEntries);
}

// ---- construction and destruction ----

void CtorCases(difftest::Context &ctx)
{
    for (uint32_t fill : {0u, 0xcdu, 0x5au}) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            void *block = ::operator new(sizeof(cWallLayer));
            std::memset(block, int(fill), sizeof(cWallLayer));
            auto *m = static_cast<cWallLayer *>(block);
            cWallLayer *r = side ? cWallLayerAdapters::Construct(m, nullptr) : ctx.Original<cWallLayer *(__thiscall *)(void *)>(kCtor)(block);
            blobs[side] = {uint32_t(r == m)};
            Capture(blobs[side], m);
            Destroy(m);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("constructor over 0x%02x: layer differs", fill);
        ++ctx.cases;
    }
}

void Frees(Blob &out, difftest::FreeRecorder &rec, const void *block)
{
    size_t sizes[256]{};
    const size_t freedCount = rec.SortedSizes(sizes, 256);
    out.insert(out.end(), {uint32_t(rec.Freed(block)), uint32_t(freedCount)});
    for (size_t i = 0; i < freedCount && i < 256; ++i) out.push_back(uint32_t(sizes[i]));
}

void BodyCases(difftest::Context &ctx)
{
    for (uint32_t entries : {0u, 1u, 2u, 7u, 30u}) for (int alt = 0; alt < 2; ++alt) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            cWallLayer *m = Make(entries * 3 + alt, entries, alt);
            difftest::FreeRecorder rec;
            if (side) cWallLayerAdapters::DestructBody(m, nullptr);
            else ctx.Original<void(__thiscall *)(void *)>(kBody)(m);
            rec.Stop();
            Frees(blobs[side], rec, m);
            // The map is gone: the vtable and the count are looked at, then the block goes back.
            blobs[side].insert(blobs[side].end(), {VTableCode(*reinterpret_cast<void *const *>(m)), m->mRefCount});
            ::operator delete(m, sizeof(cWallLayer));
        }
        if (blobs[0] != blobs[1]) ctx.Fail("destructor body entries %u alt %d: frees / fields differ", entries, alt);
        ++ctx.cases;
    }
}

void DtorCases(difftest::Context &ctx)
{
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (uint32_t entries : {0u, 1u, 2u, 7u, 30u}) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            cWallLayer *m = Make(flags * 31 + entries, entries);
            difftest::FreeRecorder rec;
            void *r = side ? cWallLayerAdapters::DeletingDestructor(m, nullptr, flags)
                           : ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDtor)(m, flags);
            rec.Stop();
            blobs[side] = {uint32_t(r == m)};
            Frees(blobs[side], rec, m);
            if (!rec.Freed(m)) {
                blobs[side].insert(blobs[side].end(), {VTableCode(*reinterpret_cast<void *const *>(m)), m->mRefCount});
                ::operator delete(m, sizeof(cWallLayer));
            }
        }
        if (blobs[0] != blobs[1]) ctx.Fail("DeletingDestructor flags %u entries %u: frees / fields differ", flags, entries);
        ++ctx.cases;
    }
}

// ---- the interface slots ----

void ClassIdCases(difftest::Context &ctx)
{
    for (int alt = 0; alt < 2; ++alt) {
        cWallLayer *m = Make(5, 2, alt);
        const uint32_t original = ctx.Original<uint32_t(__thiscall *)(void *)>(kClsid)(m);
        const uint32_t ours = cWallLayerAdapters::GetGZCLSID(m, nullptr);
        if (original != ours) ctx.Fail("GetGZCLSID alt %d: 0x%08x against ours 0x%08x", alt, original, ours);
        Destroy(m);
        ++ctx.cases;
    }
}

void QueryCases(difftest::Context &ctx)
{
    const uint32_t riids[] = {0u, 1u, 2u, cWallLayer::kClassId, cWallLayer::kClassId + 1, cWallLayer::kClassId ^ 0x80000000u, kOtherClsid, 0xffffffffu};
    for (uint32_t riid : riids) for (int alt = 0; alt < 2; ++alt) for (int withOut = 0; withOut < 2; ++withOut) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            cWallLayer *m = Make(riid % 7 + uint32_t(alt), 3, alt);
            void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
            void **where = withOut ? &out : nullptr;
            const uint8_t r = side ? cWallLayerAdapters::QueryInterface(m, nullptr, riid, where)
                                   : ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(kQuery)(m, riid, where);
            blobs[side] = {r, uint32_t(out == m), uint32_t(out == reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a)))};
            Capture(blobs[side], m);
            Destroy(m);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("QueryInterface riid 0x%08x alt %d out %d: result / layer differ", riid, alt, withOut);
        ++ctx.cases;
    }
}

// ---- the members ----

// One run of queries and changes on a random layer: each member in turn on keys present and absent, halves 0 / 1 and
// other indexes; the results, then the layer.
Blob RunMembers(difftest::Context &ctx, bool ours, uint32_t seed, uint32_t entries)
{
    cWallLayer *m = Make(seed, entries);
    std::mt19937 ask(seed + 1000);
    Blob out;
    for (int q = 0; q < 48; ++q) {
        const int32_t key = Key(ask);
        const unsigned op = ask() % 6;
        const int32_t which = ask() % 5 ? int32_t(ask() % 2) : int32_t(ask() % 4) - 1;
        const uint16_t half = uint16_t(ask() % 3 ? ask() % 5 : ask());
        out.push_back(op);
        switch (op) {
        case 0:
            out.push_back(ours ? cWallLayerAdapters::Has(m, nullptr, key) : ctx.Original<uint8_t(__thiscall *)(void *, int32_t)>(kHas)(m, key));
            break;
        case 1:
            out.push_back(ours ? cWallLayerAdapters::First(m, nullptr, key) : ctx.Original<uint32_t(__thiscall *)(void *, int32_t)>(kFirst)(m, key));
            break;
        case 2:
            out.push_back(ours ? cWallLayerAdapters::Half(m, nullptr, key, which)
                               : ctx.Original<uint16_t(__thiscall *)(void *, int32_t, int32_t)>(kHalf)(m, key, which));
            break;
        case 3:
            if (ours) cWallLayerAdapters::SetHalf(m, nullptr, key, which, &half);
            else ctx.Original<void(__thiscall *)(void *, int32_t, int32_t, const uint16_t *)>(kSetHalf)(m, key, which, &half);
            break;
        case 4:
            if (ask() % 3) break; // erase less often than the rest
            if (ours) cWallLayerAdapters::Erase(m, nullptr, key);
            else ctx.Original<void(__thiscall *)(void *, int32_t)>(kErase)(m, key);
            break;
        default:
            if (ask() % 12) break; // a rare clear
            if (ours) cWallLayerAdapters::Clear(m, nullptr);
            else ctx.Original<void(__thiscall *)(void *)>(kClear)(m);
            break;
        }
        // Lookups by the keys present too, not only random ones.
        if (!m->mEntries.empty() && q % 3 == 0) {
            auto it = m->mEntries.begin();
            std::advance(it, ask() % m->mEntries.size());
            const int32_t present = it->first;
            out.push_back(ours ? cWallLayerAdapters::First(m, nullptr, present) : ctx.Original<uint32_t(__thiscall *)(void *, int32_t)>(kFirst)(m, present));
            out.push_back(ours ? cWallLayerAdapters::Half(m, nullptr, present, which)
                               : ctx.Original<uint16_t(__thiscall *)(void *, int32_t, int32_t)>(kHalf)(m, present, which));
            if (ours) cWallLayerAdapters::SetHalf(m, nullptr, present, which, &half);
            else ctx.Original<void(__thiscall *)(void *, int32_t, int32_t, const uint16_t *)>(kSetHalf)(m, present, which, &half);
            out.push_back(ours ? cWallLayerAdapters::Has(m, nullptr, present) : ctx.Original<uint8_t(__thiscall *)(void *, int32_t)>(kHas)(m, present));
        }
    }
    Capture(out, m);
    Destroy(m);
    return out;
}

void MemberCases(difftest::Context &ctx, const char *what)
{
    for (uint32_t entries : {0u, 1u, 3u, 9u, 30u}) for (uint32_t seed = 0; seed < 8; ++seed) {
        const uint32_t s = seed * 13 + entries;
        if (RunMembers(ctx, false, s, entries) != RunMembers(ctx, true, s, entries))
            ctx.Fail("%s: entries %u seed %u: results / layer differ", what, entries, seed);
        ++ctx.cases;
    }
}

// Each member on its own over keys present and absent: results and the layer.
template <class Call> void OneMember(difftest::Context &ctx, const char *what, Call call)
{
    for (uint32_t entries : {0u, 1u, 3u, 9u, 30u}) for (uint32_t seed = 0; seed < 6; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            cWallLayer *m = Make(seed * 7 + entries, entries);
            std::mt19937 ask(seed + 77);
            std::vector<int32_t> keys;
            for (const auto &e : m->mEntries) keys.push_back(e.first);
            for (int q = 0; q < 24; ++q) {
                const int32_t key = !keys.empty() && ask() % 2 ? keys[ask() % keys.size()] : Key(ask);
                const int32_t which = ask() % 5 ? int32_t(ask() % 2) : int32_t(ask() % 4) - 1;
                const uint16_t half = uint16_t(ask() % 3 ? ask() % 5 : ask());
                blobs[side].push_back(call(side != 0, m, key, which, &half));
            }
            Capture(blobs[side], m);
            Destroy(m);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("%s entries %u seed %u: results / layer differ", what, entries, seed);
        ++ctx.cases;
    }
}

// ---- Write ----

Blob RunWrite(difftest::Context &ctx, bool ours, uint32_t seed, uint32_t entries, bool alt, int fail, int *total)
{
    Begin({}, fail);
    cWallLayer *m = Make(seed, entries, alt);
    uint8_t returned = 0;
    bool threw = false;
    try {
        returned = ours ? cWallLayerAdapters::Write(m, nullptr, OutStream())
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
            if (original != ours) ctx.Fail("Write entries %u seed %u alt %d fail %d: result / calls / layer differ", entries, seed, alt, fail);
            ++ctx.cases;
        }
    }
    std::printf("  Write: exceptions injected %llu\n", gridfx::gThrown - before);
}

// ---- Read ----

// A record: class id, version, name, count, then `entries` key / first word / high half / low half quads (some halves
// wider than 16 bits, which the stream truncates; keys repeat, so a later entry replaces an earlier one).
std::vector<Item> Record(uint32_t seed, int32_t count, uint32_t entries, uint32_t classId = cWallLayer::kClassId, uint32_t version = 1)
{
    std::mt19937 rng(seed);
    std::vector<Item> s{U(classId), U(version), rng() % 4 ? S("cWallLayer") : S("other"), U(uint32_t(count))};
    for (uint32_t i = 0; i < entries; ++i) {
        s.push_back(U(uint32_t(Key(rng))));
        s.push_back(U(Word(rng)));
        s.push_back(U(Word(rng)));
        s.push_back(U(Word(rng)));
    }
    return s;
}

Blob RunRead(difftest::Context &ctx, bool ours, const std::vector<Item> &script, uint32_t seed, uint32_t entries, bool alt,
    bool keep, int fail, int *total)
{
    Begin(script, fail, keep);
    cWallLayer *m = Make(seed, entries, alt);
    uint8_t returned = 0;
    bool threw = false;
    try {
        returned = ours ? cWallLayerAdapters::Read(m, nullptr, InStream())
                        : ctx.Original<uint8_t(__thiscall *)(void *, void *)>(kRead)(m, InStream());
    } catch (int) { threw = true; }
    gridfx::gFailAt = -1;
    if (total) *total = gridfx::gTick;
    Blob blob{returned, uint32_t(threw), uint32_t(gridfx::gCursor)};
    blob.insert(blob.end(), gridfx::gLog.begin(), gridfx::gLog.end());
    Capture(blob, m);
    Destroy(m);
    return blob;
}

void ReadCases(difftest::Context &ctx)
{
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
        for (int cut = 1; cut <= 3; ++cut)
            scripts.push_back({"short entry", [&] { auto s = Record(seed + 41, 3, 3); s.resize(s.size() - cut); return s; }(), false, true});
        for (int cut = 1; cut <= 4; ++cut)
            scripts.push_back({"short first", [&] { auto s = Record(seed + 42, 2, 1); s.resize(s.size() - cut); return s; }(), false, false});
        scripts.push_back({"long", Record(seed + 43, 2, 5), false, true});
        scripts.push_back({"negative count", Record(seed + 44, -3, 2), false, true});
        scripts.push_back({"class id", Record(seed + 45, 2, 2, cWallLayer::kClassId + 1), false, true});
        scripts.push_back({"version 0", Record(seed + 46, 2, 2, cWallLayer::kClassId, 0), false, true});
        scripts.push_back({"version 2", Record(seed + 47, 2, 2, cWallLayer::kClassId, 2), false, true});
        scripts.push_back({"version -1", Record(seed + 50, 2, 2, cWallLayer::kClassId, ~0u), false, true});
        scripts.push_back({"other table", Record(seed + 48, 2, 2, kOtherClsid), true, true});
        scripts.push_back({"other table, our id", Record(seed + 49, 2, 2), true, true});
        scripts.push_back({"header only", {U(cWallLayer::kClassId), U(1), S("cWallLayer")}, false, false});
        scripts.push_back({"empty", {}, false, false});
    }
    for (const Script &script : scripts) for (uint32_t entries : {0u, 3u, 12u}) for (int keep = 0; keep < 2; ++keep) {
        if (keep && !script.firstWhole) continue;
        int total = 0;
        const bool sweep = script.items.size() <= 28;
        for (int fail = -1; fail < (sweep ? total : 0); ++fail) {
            const uint32_t seed = uint32_t(script.items.size()) * 5 + entries;
            Blob original = RunRead(ctx, false, script.items, seed, entries, script.alt, keep, fail, fail < 0 ? &total : nullptr);
            Blob ours = RunRead(ctx, true, script.items, seed, entries, script.alt, keep, fail, nullptr);
            if (original != ours) ctx.Fail("Read %s entries %u mode %c fail %d: result / calls / layer differ", script.what, entries, keep ? 'B' : 'A', fail);
            ++ctx.cases;
        }
    }
    std::printf("  Read: exceptions injected %llu\n", gridfx::gThrown - before);
}
} // namespace

DIFF_TEST(WallLayer_Constructor, 0x81ff50) { CtorCases(ctx); }
DIFF_TEST(WallLayer_DestructorBody, 0x81ffa0) { BodyCases(ctx); }
DIFF_TEST(WallLayer_DeletingDestructor, 0x820060) { DtorCases(ctx); }
DIFF_TEST(WallLayer_GetGZCLSID, 0x8200c0) { ClassIdCases(ctx); }
DIFF_TEST(WallLayer_QueryInterface, 0x8200d0) { QueryCases(ctx); }
DIFF_TEST(WallLayer_Members, 0x820300) { MemberCases(ctx, "members"); }
DIFF_TEST(WallLayer_Has, 0x820300)
{
    OneMember(ctx, "Has", [&](bool ours, cWallLayer *m, int32_t key, int32_t, const uint16_t *) -> uint32_t {
        return ours ? cWallLayerAdapters::Has(m, nullptr, key) : ctx.Original<uint8_t(__thiscall *)(void *, int32_t)>(kHas)(m, key);
    });
}
DIFF_TEST(WallLayer_First, 0x8203a0)
{
    OneMember(ctx, "First", [&](bool ours, cWallLayer *m, int32_t key, int32_t, const uint16_t *) -> uint32_t {
        return ours ? cWallLayerAdapters::First(m, nullptr, key) : ctx.Original<uint32_t(__thiscall *)(void *, int32_t)>(kFirst)(m, key);
    });
}
DIFF_TEST(WallLayer_Half, 0x820340)
{
    OneMember(ctx, "Half", [&](bool ours, cWallLayer *m, int32_t key, int32_t which, const uint16_t *) -> uint32_t {
        return ours ? cWallLayerAdapters::Half(m, nullptr, key, which)
                    : ctx.Original<uint16_t(__thiscall *)(void *, int32_t, int32_t)>(kHalf)(m, key, which);
    });
}
DIFF_TEST(WallLayer_SetHalf, 0x820290)
{
    OneMember(ctx, "SetHalf", [&](bool ours, cWallLayer *m, int32_t key, int32_t which, const uint16_t *half) -> uint32_t {
        if (ours) cWallLayerAdapters::SetHalf(m, nullptr, key, which, half);
        else ctx.Original<void(__thiscall *)(void *, int32_t, int32_t, const uint16_t *)>(kSetHalf)(m, key, which, half);
        return m->mEntries.count(key) ? m->mEntries[key].second : 0xeeeeu;
    });
}
DIFF_TEST(WallLayer_Erase, 0x820240)
{
    OneMember(ctx, "Erase", [&](bool ours, cWallLayer *m, int32_t key, int32_t, const uint16_t *) -> uint32_t {
        if (ours) cWallLayerAdapters::Erase(m, nullptr, key);
        else ctx.Original<void(__thiscall *)(void *, int32_t)>(kErase)(m, key);
        return uint32_t(m->mEntries.size());
    });
}
DIFF_TEST(WallLayer_Clear, 0x8200b0)
{
    OneMember(ctx, "Clear", [&](bool ours, cWallLayer *m, int32_t key, int32_t, const uint16_t *) -> uint32_t {
        // Clear on every eighth key (the rest only adds entries back), so the layer is cleared at various sizes.
        if (key % 8 == 0) {
            if (ours) cWallLayerAdapters::Clear(m, nullptr);
            else ctx.Original<void(__thiscall *)(void *)>(kClear)(m);
        } else {
            m->mEntries[key] = {uint32_t(key), uint32_t(key) * 3};
        }
        return uint32_t(m->mEntries.size());
    });
}
DIFF_TEST(WallLayer_Write, 0x8203f0) { WriteCases(ctx); }
DIFF_TEST(WallLayer_Read, 0x820100) { ReadCases(ctx); }
