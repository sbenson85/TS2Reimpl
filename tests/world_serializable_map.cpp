// nTSWorld::cSerializableMap<K, V, Less, cProxyManager> (lanes VP and VQ, campaign world-lot WL-0b / WL-0c): differential
// tests of both copies, original (by RVA) against ours on equal maps.
// - GUID copy (VP): slot 3 0x81ac60, Write 0x81d0f0, Read 0x81bc20.
// - String copy (VQ): slot 3 0x81aca0, Write 0x81d190, Read 0x81bd90 and its out-of-line insert 0x81a8a0.
// - The bodies both copies share (QueryInterface 0x81bb90, its face thunk 0x81bbcf, the face's GetGZCLSID 0x81adb0), run
//   through each copy's own objects and tables.
// Streams: gridfx's slot-tagged fakes (tests/world_grid_fx.h), mode A and (whole headers) mode B, a throw at every stream
// call in turn. Maps are dumped as trees (colour, parent link, key, value in preorder; the head's flags and extremes), so
// the insert position and rebalancing show, not only the contents; a string is dumped as its vtable, text hash, capacity
// and count. The face's class id is read through its table: some runs use a copy of the game's face table whose slot 5
// answers another id.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "world/cSerializableMap.h"
#include "world_grid_fx.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {
using namespace gridfx;
using nTSWorld::cGuidMap;
using nTSWorld::cMapString;
using nTSWorld::cStringPairMap;

constexpr uint32_t kQuery = 0x81bb90, kFaceQuery = 0x81bbcf, kClsid = 0x81adb0, kStringInsert = 0x81a8a0,
                   kRZUnknownVTable = 0xda2fd0;
constexpr uint32_t kOtherClsid = 0x5eed0001;

uint32_t __fastcall OtherClsid(void *, void *) { return kOtherClsid; }

// ---- the two copies ----

struct GuidCopy {
    using Map = cGuidMap;
    static constexpr const char *kName = "GUID";
    static constexpr uint32_t kVTable = nTSWorld::kGuidMapVTable, kFace = nTSWorld::kGuidMapFaceVTable, kDtor = 0x81ac60,
                              kWrite = 0x81d0f0, kRead = 0x81bc20;
    static uint32_t NewKey(std::mt19937 &rng) { return rng() % 3 ? rng() % 24 : uint32_t(rng()); }
    static uint32_t NewValue(std::mt19937 &rng) { return rng(); }
    static Item KeyItem(std::mt19937 &rng) { return U(NewKey(rng)); }
    static Item ValueItem(std::mt19937 &rng) { return U(rng()); }
    static void DumpEntry(Blob &out, const char *pair)
    {
        uint32_t words[2];
        std::memcpy(words, pair, 8);
        out.insert(out.end(), {words[0], words[1]});
    }
};

// Keys from a small pool that differ only in case (so that _stricmp makes them one key), around '_' / '[' / '`' (between
// the upper- and lower-case letters), empty, and over 15 characters (a heap buffer); some random.
const char *const kKeyPool[] = {"", "a", "A", "alpha", "Alpha", "ALPHA", "beta", "Beta", "_under", "[bracket", "`tick",
    "zeta", "Zeta2", "a key well over fifteen chars", "A KEY WELL OVER FIFTEEN CHARS", "mid", "MiD", "0digit"};
std::string RandomText(std::mt19937 &rng, uint32_t maxLength)
{
    std::string s(rng() % (maxLength + 1), ' ');
    for (char &c : s) c = char('A' + rng() % 58); // letters and the six characters between the cases
    return s;
}

struct StringCopy {
    using Map = cStringPairMap;
    static constexpr const char *kName = "string";
    static constexpr uint32_t kVTable = nTSWorld::kStringPairMapVTable, kFace = nTSWorld::kStringPairMapFaceVTable,
                              kDtor = 0x81aca0, kWrite = 0x81d190, kRead = 0x81bd90;
    static std::string KeyText(std::mt19937 &rng)
    {
        return rng() % 4 ? std::string(kKeyPool[rng() % std::size(kKeyPool)]) : RandomText(rng, 20);
    }
    static cMapString Text(const std::string &text)
    {
        cMapString s;
        s.mString = text;
        return s;
    }
    static cMapString NewKey(std::mt19937 &rng) { return Text(KeyText(rng)); }
    static cMapString NewValue(std::mt19937 &rng) { return Text(RandomText(rng, 40)); }
    static Item KeyItem(std::mt19937 &rng) { return rng() % 9 ? S(KeyText(rng).c_str()) : U(rng()); } // a number reads as ""
    static Item ValueItem(std::mt19937 &rng) { return S(RandomText(rng, 40).c_str()); }
    static void DumpString(Blob &out, const cMapString *s)
    {
        const uint32_t vt = s->mVTable == GameAddress<void>(cRZString::kVTable) ? 1 : shape::P(s->mVTable);
        out.insert(out.end(), {vt, Hash(s->mString), uint32_t(s->mString.capacity()), s->mRefCount});
    }
    static void DumpEntry(Blob &out, const char *pair)
    {
        DumpString(out, reinterpret_cast<const cMapString *>(pair));
        DumpString(out, reinterpret_cast<const cMapString *>(pair + 0x20));
    }
};

// A copy of the game's face table with slot 5 answering kOtherClsid.
template <class C> void *const *AltFaceTable()
{
    static void *table[6];
    std::memcpy(table, GameAddress<void>(C::kFace), sizeof(table));
    table[5] = reinterpret_cast<void *>(&OtherClsid);
    return table;
}

template <class C> typename C::Map *Make(uint32_t seed, uint32_t entries, bool altFace = false)
{
    using Map = typename C::Map;
    std::mt19937 rng(seed);
    auto *m = static_cast<Map *>(::operator new(sizeof(Map)));
    m->mVTable = GameAddress<void>(C::kVTable);
    m->mRefCount = 1 + seed % 3;
    m->mSerializable = altFace ? static_cast<const void *>(AltFaceTable<C>()) : GameAddress<void>(C::kFace);
    new (&m->mMap) typename Map::Tree();
    m->mWord = rng();
    for (uint32_t i = 0; i < entries; ++i) {
        auto key = C::NewKey(rng);
        auto value = C::NewValue(rng);
        m->mMap.emplace(key, value);
    }
    return m;
}

template <class Map> void Destroy(Map *m)
{
    m->mMap.~Tree();
    ::operator delete(m, sizeof(Map));
}

void *FaceOf(void *m) { return static_cast<char *>(m) + 8; }

struct RawNode {
    RawNode *left, *parent, *right;
    uint8_t color, isnil;
    uint8_t pad[2];
    const char *Pair() const { return reinterpret_cast<const char *>(this) + 0x10; }
};
template <class C> void DumpNode(Blob &out, const RawNode *node, const RawNode *parent)
{
    if (node->isnil) {
        out.push_back(0xeeee0000u | node->color);
        return;
    }
    out.insert(out.end(), {uint32_t(node->color), uint32_t(node->parent == parent)});
    C::DumpEntry(out, node->Pair());
    DumpNode<C>(out, node->left, node);
    DumpNode<C>(out, node->right, node);
}
template <class C> void CaptureMap(Blob &out, const typename C::Map *m)
{
    const void *words[2] = {m->mVTable, m->mSerializable};
    for (const void *w : words) {
        uint32_t code = shape::P(w);
        if (w == GameAddress<void>(C::kVTable)) code = 1;
        else if (w == GameAddress<void>(C::kFace)) code = 2;
        else if (w == AltFaceTable<C>()) code = 3;
        out.push_back(code);
    }
    out.insert(out.end(), {m->mRefCount, m->mWord});
    const RawNode *head = *reinterpret_cast<RawNode *const *>(&m->mMap);
    out.insert(out.end(), {uint32_t(m->mMap.size()), head->color, head->isnil});
    const RawNode *root = head->parent;
    DumpNode<C>(out, root, head);
    const RawNode *least = head, *most = head;
    if (!root->isnil) {
        for (least = root; !least->left->isnil;) least = least->left;
        for (most = root; !most->right->isnil;) most = most->right;
    }
    out.push_back(uint32_t(head->left == least) | uint32_t(head->right == most) << 1 | uint32_t(root->isnil ? root == head : 1) << 2);
}

// ---- QueryInterface, the face thunk, the class id (shared by both copies) ----

template <class C> void QueryCases(difftest::Context &ctx, bool face)
{
    using Map = typename C::Map;
    for (uint32_t riid : {Map::kSerializableIid, 1u, 0u, 0xe4fda3d5u, 0x12345678u}) for (int nullOut = 0; nullOut < 2; ++nullOut) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Map *m = Make<C>(riid ^ 7, 3);
            void *got = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
            void **out = nullOut ? nullptr : &got;
            bool r;
            if (face) {
                auto *f = reinterpret_cast<typename Map::Face *>(FaceOf(m));
                r = side ? f->QueryInterface(riid, out) : ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(kFaceQuery)(f, riid, out);
            } else {
                r = side ? m->QueryInterface(riid, out) : ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(kQuery)(m, riid, out);
            }
            const uintptr_t at = reinterpret_cast<uintptr_t>(got), base = reinterpret_cast<uintptr_t>(m);
            blobs[side] = {uint32_t(r), uint32_t(at == 0x5a5a5a5a), uint32_t(at == 0), uint32_t(at == 0x5a5a5a5a || at == 0 ? 0 : at - base)};
            CaptureMap<C>(blobs[side], m);
            Destroy(m);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("%s %s riid 0x%08x out %s: result / out / count differ", C::kName, face ? "Face::QueryInterface" : "QueryInterface", riid, nullOut ? "null" : "set");
        ++ctx.cases;
    }
}

template <class C> void ClassIdCases(difftest::Context &ctx)
{
    using Map = typename C::Map;
    Map *m = Make<C>(5, 2);
    auto *f = reinterpret_cast<typename Map::Face *>(FaceOf(m));
    const uint32_t original = ctx.Original<uint32_t(__thiscall *)(void *)>(kClsid)(f);
    if (original != f->GetGZCLSID()) ctx.Fail("%s GetGZCLSID: 0x%08x against ours 0x%08x", C::kName, original, f->GetGZCLSID());
    // Through the face's own table (the game's slot 5, hooked to ours in the game).
    const uint32_t viaTable = VirtualCall<uint32_t(__thiscall *)(void *), 5>(f);
    if (viaTable != original) ctx.Fail("%s GetGZCLSID through the table: 0x%08x against 0x%08x", C::kName, viaTable, original);
    Destroy(m);
    ++ctx.cases;
}

// ---- slot 3 ----

template <class C> void DtorCases(difftest::Context &ctx)
{
    using Map = typename C::Map;
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (uint32_t entries : {0u, 1u, 2u, 7u, 40u}) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Map *m = Make<C>(flags * 31 + entries, entries);
            m->mVTable = nullptr; // the original stores both tables itself
            m->mSerializable = nullptr;
            difftest::FreeRecorder rec;
            void *r = side ? m->DeletingDestructor(flags) : ctx.Original<void *(__thiscall *)(void *, uint32_t)>(C::kDtor)(m, flags);
            rec.Stop();
            size_t sizes[256]{};
            const size_t freedCount = rec.SortedSizes(sizes, 256);
            blobs[side] = {uint32_t(r == m), uint32_t(rec.Freed(m)), uint32_t(freedCount)};
            for (size_t i = 0; i < freedCount && i < 256; ++i) blobs[side].push_back(uint32_t(sizes[i]));
            if (!rec.Freed(m)) {
                // The tree is gone: only the words, the count and the field are looked at, then the block goes back.
                const void *vt = m->mVTable, *face = m->mSerializable;
                blobs[side].insert(blobs[side].end(), {uint32_t(vt == GameAddress<void>(kRZUnknownVTable)),
                    uint32_t(face == GameAddress<void>(C::kFace)), m->mRefCount, m->mWord});
                ::operator delete(m, sizeof(Map));
            }
        }
        if (blobs[0] != blobs[1]) ctx.Fail("%s DeletingDestructor flags %u entries %u: frees / state differ", C::kName, flags, entries);
        ++ctx.cases;
    }
}

// ---- Write ----

template <class C> Blob RunWrite(difftest::Context &ctx, bool ours, uint32_t seed, uint32_t entries, bool altFace, int fail, int *total)
{
    using Map = typename C::Map;
    Begin({}, fail);
    Map *m = Make<C>(seed, entries, altFace);
    auto *f = reinterpret_cast<typename Map::Face *>(FaceOf(m));
    uint8_t returned = 0;
    bool threw = false;
    try {
        returned = ours ? f->Write(OutStream()) : ctx.Original<uint8_t(__thiscall *)(void *, void *)>(C::kWrite)(f, OutStream());
    } catch (int) { threw = true; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, uint32_t(threw)};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    CaptureMap<C>(blob, m);
    Destroy(m);
    return blob;
}

template <class C> void WriteCases(difftest::Context &ctx)
{
    const uint64_t before = gThrown;
    for (uint32_t entries : {0u, 1u, 2u, 5u, 9u, 60u}) for (uint32_t seed : {1u, 2u, 3u}) for (int alt = 0; alt < 2; ++alt) {
        int total = 0;
        for (int fail = -1; fail < (entries <= 9 ? total : 0); ++fail) {
            const uint32_t s = entries * 17 + seed;
            Blob original = RunWrite<C>(ctx, false, s, entries, alt, fail, fail < 0 ? &total : nullptr);
            Blob ours = RunWrite<C>(ctx, true, s, entries, alt, fail, nullptr);
            if (original != ours) ctx.Fail("%s Write entries %u seed %u alt %d fail %d: result / calls / map differ", C::kName, entries, seed, alt, fail);
            ++ctx.cases;
        }
    }
    std::printf("  %s Write: exceptions injected %llu\n", C::kName, gThrown - before);
}

// ---- Read ----

// A record: class id, version, word, count, then `pairs` key / value pairs (keys from a small range so that some repeat
// each other and the map's own keys).
template <class C> std::vector<Item> Record(uint32_t seed, int32_t count, uint32_t pairs, uint32_t classId = C::Map::kClassId, uint32_t version = 1)
{
    std::mt19937 rng(seed);
    std::vector<Item> s{U(classId), U(version), U(rng()), U(uint32_t(count))};
    for (uint32_t i = 0; i < pairs; ++i) {
        s.push_back(C::KeyItem(rng));
        s.push_back(C::ValueItem(rng));
    }
    return s;
}

template <class C> Blob RunRead(difftest::Context &ctx, bool ours, const std::vector<Item> &script, uint32_t seed, uint32_t entries,
    bool altFace, bool keep, int fail, int *total)
{
    using Map = typename C::Map;
    Begin(script, fail, keep);
    Map *m = Make<C>(seed, entries, altFace);
    auto *f = reinterpret_cast<typename Map::Face *>(FaceOf(m));
    uint8_t returned = 0;
    bool threw = false;
    try {
        returned = ours ? f->Read(InStream()) : ctx.Original<uint8_t(__thiscall *)(void *, void *)>(C::kRead)(f, InStream());
    } catch (int) { threw = true; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, uint32_t(threw), uint32_t(gCursor)};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    CaptureMap<C>(blob, m);
    Destroy(m);
    return blob;
}

template <class C> void ReadCases(difftest::Context &ctx)
{
    using Map = typename C::Map;
    const uint64_t before = gThrown;
    struct Script {
        const char *what;
        std::vector<Item> items;
        bool alt;
    };
    std::vector<Script> scripts;
    for (uint32_t seed : {1u, 2u, 3u}) {
        for (uint32_t n : {0u, 1u, 2u, 6u, 30u}) scripts.push_back({"whole", Record<C>(seed * 7 + n, int32_t(n), n), false});
        scripts.push_back({"short", Record<C>(seed + 40, 5, 3), false});
        scripts.push_back({"short pair", [&] { auto s = Record<C>(seed + 41, 4, 4); s.pop_back(); return s; }(), false});
        scripts.push_back({"long", Record<C>(seed + 42, 2, 5), false});
        scripts.push_back({"negative count", Record<C>(seed + 43, -3, 2), false});
        scripts.push_back({"class id", Record<C>(seed + 44, 2, 2, Map::kClassId + 1), false});
        scripts.push_back({"version 0", Record<C>(seed + 45, 2, 2, Map::kClassId, 0), false});
        scripts.push_back({"version 2", Record<C>(seed + 46, 2, 2, Map::kClassId, 2), false});
        scripts.push_back({"other face", Record<C>(seed + 47, 2, 2, kOtherClsid), true});
        scripts.push_back({"other face, our id", Record<C>(seed + 48, 2, 2), true});
        scripts.push_back({"header only", {U(Map::kClassId), U(1)}, false});
        scripts.push_back({"empty", {}, false});
    }
    for (const Script &script : scripts) for (uint32_t entries : {0u, 3u, 12u}) for (int keep = 0; keep < 2; ++keep) {
        if (keep && script.items.size() < 4) continue; // mode B only with a whole header
        int total = 0;
        const bool sweep = script.items.size() <= 24;
        for (int fail = -1; fail < (sweep ? total : 0); ++fail) {
            const uint32_t seed = uint32_t(script.items.size()) * 5 + entries;
            Blob original = RunRead<C>(ctx, false, script.items, seed, entries, script.alt, keep, fail, fail < 0 ? &total : nullptr);
            Blob ours = RunRead<C>(ctx, true, script.items, seed, entries, script.alt, keep, fail, nullptr);
            if (original != ours) ctx.Fail("%s Read %s entries %u mode %c fail %d: result / calls / map differ", C::kName, script.what, entries, keep ? 'B' : 'A', fail);
            ++ctx.cases;
        }
    }
    std::printf("  %s Read: exceptions injected %llu\n", C::kName, gThrown - before);
}

// The string copy reads its count into the stream argument's stack slot: in mode B a failed count read leaves the
// stream's address (the stub's, a few million) as the count. Each run ends with a throw a few entries in (no clean run:
// it would read millions of empty entries); the entries read before it (empty strings, one insert) must match.
void StringCountFromStreamCases(difftest::Context &ctx)
{
    const std::vector<Item> header{U(cStringPairMap::kClassId), U(1), U(0x1234)};
    if (int32_t(reinterpret_cast<uintptr_t>(InStream())) <= 0) ctx.Fail("the input stub's address is not a positive count");
    for (uint32_t entries : {0u, 3u}) for (int fail = 3; fail < 12; ++fail) {
        Blob original = RunRead<StringCopy>(ctx, false, header, 77 + entries, entries, false, true, fail, nullptr);
        Blob ours = RunRead<StringCopy>(ctx, true, header, 77 + entries, entries, false, true, fail, nullptr);
        if (original != ours) ctx.Fail("string Read, count from the stream, entries %u fail %d: result / calls / map differ", entries, fail);
        ++ctx.cases;
    }
}

// ---- the string copy's insert 0x81a8a0 (this = the map at +0xc) ----

struct InsertResult {
    void *node;
    uint8_t inserted;
};

void StringInsertCases(difftest::Context &ctx)
{
    using Tree = cStringPairMap::Tree;
    for (uint32_t entries : {0u, 1u, 2u, 5u, 17u, 40u}) for (uint32_t seed = 0; seed < 12; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            cStringPairMap *m = Make<StringCopy>(entries * 101 + seed, entries);
            std::mt19937 rng(seed * 7 + entries);
            for (int step = 0; step < 6; ++step) {
                const Tree::value_type entry(StringCopy::NewKey(rng), StringCopy::NewValue(rng));
                const char *pair;
                uint8_t inserted;
                if (side) {
                    auto r = m->mMap.Insert(entry);
                    pair = reinterpret_cast<const char *>(&*r.first);
                    inserted = r.second;
                } else {
                    InsertResult result{reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a)), 0x77};
                    InsertResult *r = ctx.Original<InsertResult *(__thiscall *)(void *, InsertResult *, const void *)>(kStringInsert)(&m->mMap, &result, &entry);
                    if (r != &result) ctx.Fail("string insert: returned 0x%p, not the result pointer", static_cast<void *>(r));
                    pair = static_cast<const char *>(result.node) + 0x10;
                    inserted = result.inserted;
                }
                blobs[side].push_back(inserted);
                StringCopy::DumpEntry(blobs[side], pair);
                // The entry passed in is left as it was.
                StringCopy::DumpEntry(blobs[side], reinterpret_cast<const char *>(&entry));
            }
            CaptureMap<StringCopy>(blobs[side], m);
            Destroy(m);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("string insert entries %u seed %u: results / map differ", entries, seed);
        ++ctx.cases;
    }
}
} // namespace

DIFF_TEST(SerializableMap_QueryInterface, 0x81bb90) { QueryCases<GuidCopy>(ctx, false); QueryCases<StringCopy>(ctx, false); }
DIFF_TEST(SerializableMap_FaceQueryInterface, 0x81bbcf) { QueryCases<GuidCopy>(ctx, true); QueryCases<StringCopy>(ctx, true); }
DIFF_TEST(SerializableMap_GetGZCLSID, 0x81adb0) { ClassIdCases<GuidCopy>(ctx); ClassIdCases<StringCopy>(ctx); }
DIFF_TEST(SerializableMap_GuidDestructor, 0x81ac60) { DtorCases<GuidCopy>(ctx); }
DIFF_TEST(SerializableMap_GuidWrite, 0x81d0f0) { WriteCases<GuidCopy>(ctx); }
DIFF_TEST(SerializableMap_GuidRead, 0x81bc20) { ReadCases<GuidCopy>(ctx); }
DIFF_TEST(SerializableMap_StringDestructor, 0x81aca0) { DtorCases<StringCopy>(ctx); }
DIFF_TEST(SerializableMap_StringWrite, 0x81d190) { WriteCases<StringCopy>(ctx); }
DIFF_TEST(SerializableMap_StringRead, 0x81bd90) { ReadCases<StringCopy>(ctx); StringCountFromStreamCases(ctx); }
DIFF_TEST(SerializableMap_StringInsert, 0x81a8a0) { StringInsertCases(ctx); }
