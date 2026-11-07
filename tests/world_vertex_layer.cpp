// nTSWorld::cVertexLayer and nTSWorld::cFencePostLayer (lane VO, src/world/cVertexLayer.h): differential tests of both
// layers. Every case runs three sides on equal layers (planted with std::map's own inserts, the same order on each side,
// so the trees match node for node):
// - the original: a layer with the game's vtable, the function called by RVA;
// - ours: our layer (our vtable); a virtual slot is called through our vtable at the slot where the game's vtable holds
//   the RVA (SlotOf), a member directly;
// - the adapter route: a layer with a copy of the game's vtable whose tested slot is a counting trap, and the hook's
//   adapter called (an adapter that dispatched virtually would land on the trap).
// Compared: the result, whether a stream call threw, the stream log, the script cursor, frees and allocations, the trap's
// count and the layer: vtable as a code, count, and each map as a tree (size, then preorder: colour, key and value,
// parent links, the head's leftmost / rightmost links). The head node's value field is planted alike on every side
// (ValueOf of a missing vertex answers it).
// Functions: cVertexLayer ctor 0xd146c0, destructor body 0xd14760, slot 6 0xd14780, Add 0xd147a0, GetGZCLSID 0xd149f0,
// QueryInterface 0xd14a00, Read 0xd14a30, Remove 0xd14b70, Write 0xd14c50; cFencePostLayer ctor 0x820530, destructor body
// 0x820580, slot 6 0x820590, Set 0x8205b0, slot 7 (Clear) 0x820710, GetGZCLSID 0x820720, QueryInterface 0x820880, Read
// 0x8208b0, Remove 0x8209f0, Write 0x820a80; the shared Vertices 0x820730, Contains 0x8207a0 and ValueOf 0x820810 (on
// either layer); the folded cIGZUnknown slots on our vtables against the shared bodies (AddRef 0x148fb0, Release 0x4b1d00).
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "world/cVertexLayer.h"
#include "ts_services_fixture.h"
#include "world_grid_fx.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace {
using namespace gridfx;
using shape::P;

constexpr const uint32_t kFace[] = {0};
constexpr unsigned kSlotWords[8] = {2, 0, 0, 1, 1, 0, 1, 0}; // stack words of slots 0-7

int gTrapHits;

// ---- the two layers ----

template <class L> struct Layer;
template <> struct Layer<cVertexLayer> {
    static constexpr const char *name = "VertexLayer", *record = "cVertexLayer";
    static constexpr unsigned slots = 7;
    static constexpr uint32_t ctor = 0xd146c0, body = 0xd14760, vf6 = 0xd14780, classId = 0xd149f0, qi = 0xd14a00,
                              read = 0xd14a30, write = 0xd14c50;
    using Adapters = cVertexLayerAdapters;
};
template <> struct Layer<cFencePostLayer> {
    static constexpr const char *name = "FencePostLayer", *record = "cFencePostLayer";
    static constexpr unsigned slots = 8;
    static constexpr uint32_t ctor = 0x820530, body = 0x820580, vf6 = 0x820590, classId = 0x820720, qi = 0x820880,
                              read = 0x8208b0, write = 0x820a80;
    using Adapters = cFencePostLayerAdapters;
};
constexpr uint32_t kVertices = 0x820730, kContains = 0x8207a0, kValueOf = 0x820810;
constexpr uint32_t kAdd = 0xd147a0, kRemoveVertex = 0xd14b70, kSet = 0x8205b0, kRemoveFence = 0x8209f0, kClear = 0x820710;

template <class L> void **OurVT() { return shape::Ours<L>(kFace).face[0]; }
template <class L> void **OurDestroyedVT() { return shape::Ours<L>(kFace).destroyed; }
template <class L> void *const *GameVT() { return shape::GameVTable(L::kVTable); }

// A copy of the game's vtable of L with `slot` a counting trap.
template <class L> void *const *CopyVT(unsigned slot)
{
    static void *vt[8];
    for (unsigned s = 0; s < Layer<L>::slots; ++s) vt[s] = GameVT<L>()[s];
    vt[slot] = shape::TrapFor<&shape::Count<gTrapHits>>(kSlotWords[slot]);
    return vt;
}

// A layer with our layout (empty maps) whose vtable word is `vtable` (null: ours).
template <class L> L *NewLayer(void *const *vtable = nullptr)
{
    auto *layer = new L();
    if (vtable) shape::VTableAt(layer, 0) = const_cast<void **>(vtable);
    return layer;
}
// The layer of a side: 0 the game's vtable, 1 ours, 2 the trap copy for `slot`.
template <class L> L *SideLayer(int side, unsigned slot)
{
    return side == 0 ? NewLayer<L>(GameVT<L>()) : side == 1 ? NewLayer<L>() : NewLayer<L>(CopyVT<L>(slot));
}
// Frees a layer without running a slot.
template <class L> void FreeLayer(L *layer)
{
    layer->L::~L();
    ::operator delete(layer, sizeof(L));
}

template <class Fn> Fn Slot(const void *object, unsigned slot)
{
    return reinterpret_cast<Fn>(shape::VTableAt(const_cast<void *>(object), 0)[slot]);
}
template <class L> unsigned SlotOfRva(difftest::Context &ctx, uint32_t rva)
{
    const unsigned slot = shape::SlotOf(L::kVTable, Layer<L>::slots, rva);
    if (slot == ~0u) ctx.Fail("0x%x is in no slot of vtable 0x%x", rva, L::kVTable);
    return slot;
}

// ---- vertices ----

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
// A small pool, so lookups hit often: levels around 0 and the extremes, coordinates on the half grid, -0 (equal to 0, its
// own bits) and now and then a NaN.
LayerVertex RandomVertex(std::mt19937 &rng, bool nan)
{
    static const float coords[] = {0.0f, 0.5f, 1.0f, 2.0f, 3.5f, -1.0f, -0.0f, 63.5f};
    static const int32_t levels[] = {0, 0, 1, 2, -1, 0x7fffffff, int32_t(0x80000000)};
    LayerVertex v;
    v.x = coords[rng() % 8];
    v.y = coords[rng() % 8];
    v.level = levels[rng() % 7];
    if (nan && rng() % 16 == 0) (rng() % 2 ? v.x : v.y) = std::numeric_limits<float>::quiet_NaN();
    return v;
}

// ---- planting and capture ----

struct RawNode {
    RawNode *left, *parent, *right;
    uint8_t color, isnil;
    uint8_t pad[2];
    uint32_t payload[4];
};
static_assert(sizeof(RawNode) == 0x20);

RawNode *Head(const void *map) { return *static_cast<RawNode *const *>(map); }
size_t Size(const void *map) { return static_cast<const size_t *>(map)[1]; }

void DumpNode(Blob &out, const RawNode *node, const RawNode *parent)
{
    if (node->isnil) {
        out.push_back(0xeeee0000u | node->color);
        return;
    }
    out.insert(out.end(), {uint32_t(node->color), uint32_t(node->parent == parent), node->payload[0], node->payload[1],
        node->payload[2], node->payload[3]});
    DumpNode(out, node->left, node);
    DumpNode(out, node->right, node);
}
// A map as a tree: size, the head's flags, the tree in preorder, and whether the head's links name the extremes.
void DumpMap(Blob &out, const void *map)
{
    const RawNode *head = Head(map);
    out.insert(out.end(), {uint32_t(Size(map)), head->color, head->isnil});
    const RawNode *root = head->parent;
    DumpNode(out, root, head);
    const RawNode *least = head, *most = head;
    if (!root->isnil) {
        for (least = root; !least->left->isnil;) least = least->left;
        for (most = root; !most->right->isnil;) most = most->right;
    }
    out.push_back(uint32_t(head->left == least) | uint32_t(head->right == most) << 1 | uint32_t(root->isnil ? root == head : 1) << 2);
}

template <class L> void Capture(Blob &out, const L *layer, unsigned slot)
{
    const void *word = shape::VTableAt(const_cast<L *>(layer), 0);
    uint32_t code = P(word);
    if (word == GameVT<L>() || word == OurVT<L>() || word == CopyVT<L>(slot)) code = 1;
    out.insert(out.end(), {code, layer->mRefCount});
    DumpMap(out, &layer->mMap);
    if constexpr (std::is_same_v<L, cVertexLayer>) DumpMap(out, &layer->mIds);
}

// The head node's value field (never written by the maps): the same word on every side.
template <class L> void PlantHead(L *layer, uint32_t seed) { Head(&layer->mMap)->payload[3] = 0x5eed0000u ^ seed; }

// Random contents. cVertexLayer: mostly matching pairs, plus ids whose vertex the vertex map gives another id (or lacks),
// as Read leaves them (it empties only the vertex map).
template <class L> void Plant(L *layer, uint32_t seed, unsigned n, bool nan)
{
    std::mt19937 rng(seed);
    layer->mRefCount = 1 + seed % 3;
    PlantHead(layer, seed);
    for (unsigned i = 0; i < n; ++i) {
        const LayerVertex v = RandomVertex(rng, nan);
        if constexpr (std::is_same_v<L, cVertexLayer>) {
            const int32_t id = int32_t(rng() % 12) - 2;
            switch (rng() % 6) {
            case 0: layer->mIds.emplace(id, v); break;                 // an id without its vertex
            case 1: layer->mMap.emplace(v, id); break;                 // a vertex without its id
            default:
                if (layer->mMap.emplace(v, id).second) layer->mIds.emplace(id, v);
            }
        } else {
            layer->mMap.emplace(v, rng() % 4 ? rng() % 9 : uint32_t(rng()));
        }
    }
}

const char *SideName(int side) { return side == 0 ? "original" : side == 1 ? "ours" : "adapter"; }

void Compare(difftest::Context &ctx, const Blob (&blobs)[3], const char *name, const char *what, int a, int b, int c)
{
    for (int side = 1; side < 3; ++side) {
        if (blobs[side] == blobs[0]) continue;
        size_t at = 0;
        while (at < blobs[0].size() && at < blobs[side].size() && blobs[0][at] == blobs[side][at]) ++at;
        const uint32_t want = at < blobs[0].size() ? blobs[0][at] : ~0u, got = at < blobs[side].size() ? blobs[side][at] : ~0u;
        ctx.Fail("%s %s (%d %d %d) %s: word %zu 0x%x != 0x%x", name, what, a, b, c, SideName(side), at, got, want);
    }
    ++ctx.cases;
}

// Frees and allocations of one call.
struct Heap {
    difftest::FreeRecorder frees;
    difftest::AllocationCounter allocs;
    void Stop(Blob &out)
    {
        frees.Stop();
        allocs.Stop();
        out.insert(out.end(), {uint32_t(frees.Count()), uint32_t(allocs.Allocations()), uint32_t(allocs.Bytes())});
    }
};

// ---- member sequences ----

// The shared bodies on either layer: Contains, ValueOf, Vertices (into a list with planted contents and capacity; its
// blocks come from the allocator service, which the heap counts do not see).
template <class L> void SharedOp(difftest::Context &ctx, int side, L *layer, std::mt19937 &rng, Blob &out, bool nan)
{
    using A = cFencePostLayerAdapters;
    auto *fence = reinterpret_cast<cFencePostLayer *>(layer); // the bodies use only the +8 map
    const LayerVertex v = RandomVertex(rng, nan);
    switch (rng() % 3) {
    case 0: {
        using Fn = uint8_t(__thiscall *)(void *, const LayerVertex *);
        const uint8_t r = side == 0 ? ctx.Original<Fn>(kContains)(layer, &v) : side == 1 ? layer->Contains(v) : A::Contains(fence, nullptr, &v);
        out.insert(out.end(), {0xc0, r});
        break;
    }
    case 1: {
        using Fn = uint32_t(__thiscall *)(void *, const LayerVertex *);
        const uint32_t r = side == 0 ? ctx.Original<Fn>(kValueOf)(layer, &v) : side == 1 ? uint32_t(layer->ValueOf(v)) : A::ValueOf(fence, nullptr, &v);
        out.insert(out.end(), {0xc1, r});
        break;
    }
    default: {
        LayerVertexList list;
        const unsigned start = rng() % 4, room = rng() % 3 ? 0 : rng() % 40;
        list.reserve(start + room);
        for (unsigned i = 0; i < start; ++i) list.push_back(RandomVertex(rng, false));
        Heap heap;
        using Fn = uint8_t(__thiscall *)(void *, LayerVertexList *);
        const uint8_t r = side == 0 ? ctx.Original<Fn>(kVertices)(layer, &list) : side == 1 ? layer->Vertices(list) : A::Vertices(fence, nullptr, &list);
        out.insert(out.end(), {0xc2, r});
        heap.Stop(out);
        out.insert(out.end(), {uint32_t(list.size()), uint32_t(list.capacity())});
        for (const LayerVertex &e : list) out.insert(out.end(), {Bits(e.x), Bits(e.y), uint32_t(e.level)});
    }
    }
}

// cVertexLayer: Add and Remove (ids from a small pool, so an id often has a vertex already), and the shared bodies.
void VertexOp(difftest::Context &ctx, int side, cVertexLayer *layer, std::mt19937 &rng, Blob &out, bool nan)
{
    using A = cVertexLayerAdapters;
    const unsigned kind = rng() % 5;
    if (kind >= 3) return SharedOp(ctx, side, layer, rng, out, nan);
    LayerVertex v = RandomVertex(rng, nan);
    int32_t id = int32_t(rng() % 12) - 2;
    // Now and then the arguments of an existing entry (Remove then finds its pair).
    if (rng() % 3 == 0 && !layer->mMap.empty()) {
        auto it = layer->mMap.begin();
        std::advance(it, rng() % layer->mMap.size());
        v = it->first;
        if (rng() % 2) id = it->second;
    }
    using Fn = void(__thiscall *)(void *, const LayerVertex *, int32_t);
    Heap heap;
    if (kind < 2) {
        if (side == 0) ctx.Original<Fn>(kAdd)(layer, &v, id);
        else if (side == 1) layer->Add(v, id);
        else A::Add(layer, nullptr, &v, id);
    } else {
        if (side == 0) ctx.Original<Fn>(kRemoveVertex)(layer, &v, id);
        else if (side == 1) layer->Remove(v, id);
        else A::Remove(layer, nullptr, &v, id);
    }
    out.insert(out.end(), {kind, uint32_t(id)});
    heap.Stop(out);
}

// cFencePostLayer: Set, Remove, slot 7 (Clear, rarely) and the shared bodies.
void FenceOp(difftest::Context &ctx, int side, cFencePostLayer *layer, std::mt19937 &rng, Blob &out, bool nan)
{
    using A = cFencePostLayerAdapters;
    const unsigned kind = rng() % 16;
    if (kind >= 10) return SharedOp(ctx, side, layer, rng, out, nan);
    LayerVertex v = RandomVertex(rng, nan);
    if (rng() % 3 == 0 && !layer->mMap.empty()) {
        auto it = layer->mMap.begin();
        std::advance(it, rng() % layer->mMap.size());
        v = it->first;
    }
    const uint32_t value = rng() % 4 ? rng() % 9 : uint32_t(rng());
    Heap heap;
    uint32_t r = 0x77;
    if (kind < 5) {
        using Fn = uint8_t(__thiscall *)(void *, const LayerVertex *, uint32_t);
        r = side == 0 ? ctx.Original<Fn>(kSet)(layer, &v, value) : side == 1 ? layer->Set(v, value) : A::Set(layer, nullptr, &v, value);
    } else if (kind < 9) {
        using Fn = uint8_t(__thiscall *)(void *, const LayerVertex *);
        r = side == 0 ? ctx.Original<Fn>(kRemoveFence)(layer, &v) : side == 1 ? layer->Remove(v) : A::Remove(layer, nullptr, &v);
    } else {
        using Fn = void(__thiscall *)(void *);
        if (side == 0) ctx.Original<Fn>(kClear)(layer);
        else if (side == 1) Slot<Fn>(layer, 7)(layer);
        else A::Clear(layer, nullptr);
    }
    out.insert(out.end(), {kind, r});
    heap.Stop(out);
}

// Sequences of member calls from planted layers; the layer is captured after every call. The trap copy's tested slot is
// 7 (Clear), the only virtual slot a sequence calls. Vertices' list grows through the game's allocator service.
template <class L> void SequenceCases(difftest::Context &ctx, bool nan)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        Blob blobs[3];
        for (int side = 0; side < 3; ++side) {
            gTrapHits = 0;
            L *layer = SideLayer<L>(side, 7);
            Plant(layer, seed, seed % 23, nan);
            std::mt19937 rng(seed * 7919u);
            for (int step = 0; step < 40; ++step) {
                if constexpr (std::is_same_v<L, cVertexLayer>) VertexOp(ctx, side, layer, rng, blobs[side], nan);
                else FenceOp(ctx, side, layer, rng, blobs[side], nan);
                Capture(blobs[side], layer, 7);
            }
            blobs[side].push_back(uint32_t(gTrapHits));
            FreeLayer(layer);
        }
        Compare(ctx, blobs, Layer<L>::name, nan ? "Sequence (NaN)" : "Sequence", int(seed), 0, 0);
    }
}

// ---- Write ----

template <class L> Blob RunWrite(difftest::Context &ctx, int side, unsigned slot, uint32_t seed, unsigned n, int fail, int *total)
{
    Begin({}, fail);
    gTrapHits = 0;
    L *layer = SideLayer<L>(side, slot);
    Plant(layer, seed, n, false);
    uint8_t returned = 0;
    bool threw = false;
    try {
        using Fn = uint8_t(__thiscall *)(void *, void *);
        if (side == 0) returned = ctx.Original<Fn>(Layer<L>::write)(layer, OutStream());
        else if (side == 1) returned = Slot<Fn>(layer, slot)(layer, OutStream());
        else returned = Layer<L>::Adapters::Write(layer, nullptr, OutStream());
    } catch (int) { threw = true; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, uint32_t(threw), uint32_t(gTrapHits)};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    Capture(blob, layer, slot);
    FreeLayer(layer);
    return blob;
}

template <class L> void WriteCases(difftest::Context &ctx)
{
    const unsigned slot = SlotOfRva<L>(ctx, Layer<L>::write);
    if (slot == ~0u) return;
    const uint64_t before = gThrown;
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        const unsigned n = seed < 4 ? seed - 1 : seed % 9 * 3;
        int total = 0;
        for (int fail = -1; fail < total; ++fail) {
            Blob blobs[3];
            for (int side = 0; side < 3; ++side) blobs[side] = RunWrite<L>(ctx, side, slot, seed, n, fail, side == 0 && fail < 0 ? &total : nullptr);
            Compare(ctx, blobs, Layer<L>::name, "Write", int(seed), int(n), fail);
        }
    }
    std::printf("  %s Write: exceptions injected %llu\n", Layer<L>::name, gThrown - before);
}

// ---- Read ----

template <class L> Blob RunRead(difftest::Context &ctx, int side, unsigned slot, const std::vector<Item> &script, uint32_t seed,
    unsigned n, bool keep, int fail, int *total)
{
    Begin(script, fail, keep);
    gTrapHits = 0;
    L *layer = SideLayer<L>(side, slot);
    Plant(layer, seed, n, false);
    uint8_t returned = 0;
    uint32_t threw = 0;
    Heap heap;
    try {
        using Fn = uint8_t(__thiscall *)(void *, void *);
        if (side == 0) returned = ctx.Original<Fn>(Layer<L>::read)(layer, InStream());
        else if (side == 1) returned = Slot<Fn>(layer, slot)(layer, InStream());
        else returned = Layer<L>::Adapters::Read(layer, nullptr, InStream());
    } catch (int) { threw = 1; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, threw, uint32_t(gCursor), uint32_t(gTrapHits)};
    heap.Stop(blob);
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    Capture(blob, layer, slot);
    FreeLayer(layer);
    return blob;
}

// A record: the header, then `count` entries from `seed` (vertices from the small pool: repeats test Add / Set's keep-the-
// first; for cVertexLayer ids repeat too).
template <class L> std::vector<Item> Record(uint32_t seed, int32_t count, unsigned entries, uint32_t classId = L::kClassId,
    uint32_t version = 1, bool text = true)
{
    std::vector<Item> s{U(classId), U(version), text ? S(Layer<L>::record) : U(5), U(uint32_t(count))};
    std::mt19937 rng(seed);
    for (unsigned i = 0; i < entries; ++i) {
        const LayerVertex v = RandomVertex(rng, false);
        s.insert(s.end(), {U(uint32_t(v.level)), F(v.x), F(v.y)});
        if constexpr (std::is_same_v<L, cVertexLayer>) s.push_back(U(uint32_t(int32_t(rng() % 10) - 2)));
        else s.push_back(U(rng() % 4 ? rng() % 9 : uint32_t(rng())));
    }
    return s;
}

template <class L> void ReadCases(difftest::Context &ctx)
{
    const unsigned slot = SlotOfRva<L>(ctx, Layer<L>::read);
    if (slot == ~0u) return;
    constexpr uint32_t kId = L::kClassId;
    const uint32_t otherId = std::is_same_v<L, cVertexLayer> ? cFencePostLayer::kClassId : cVertexLayer::kClassId;
    // Headers cut short, a wrong class id (also the other layer's) or version, a name that is not text, counts of none, a
    // negative one, whole records, records longer than the count, records cut inside the entries.
    std::vector<std::vector<Item>> scripts{{}, {U(kId)}, {U(kId), U(1)}, {U(kId + 1), U(1)}, {U(otherId), U(1)}, {U(0), U(1)},
        {U(kId), U(1), S("x")}};
    for (uint32_t version : {0u, 2u, 0xffffffffu}) scripts.push_back(Record<L>(5, 2, 2, kId, version));
    scripts.push_back(Record<L>(6, 2, 2, kId, 1, false));
    scripts.push_back(Record<L>(7, 0, 0));
    scripts.push_back(Record<L>(8, -1, 2));
    for (uint32_t seed = 10; seed < 30; ++seed) scripts.push_back(Record<L>(seed, int32_t(seed % 13), seed % 13 + (seed % 4 == 0)));
    {
        const auto whole = Record<L>(40, 6, 6);
        for (size_t cut : {size_t(5), size_t(6), size_t(7), size_t(9), size_t(15), whole.size() - 1}) {
            auto s = whole;
            s.resize(cut);
            scripts.push_back(s);
        }
    }
    const uint64_t before = gThrown;
    uint32_t index = 0;
    for (const auto &script : scripts) {
        // Mode B (a failed read leaves its target as it was) only once the header and the first entry are whole: the
        // original's locals are then all written.
        for (int mode = 0; mode < (script.size() >= 8 ? 2 : 1); ++mode)
            for (unsigned start : {0u, 3u, 11u}) {
                int total = 0;
                for (int fail = -1; fail < total; ++fail) {
                    Blob blobs[3];
                    for (int side = 0; side < 3; ++side)
                        blobs[side] = RunRead<L>(ctx, side, slot, script, index * 31 + start, start, mode == 1, fail,
                            side == 0 && fail < 0 ? &total : nullptr);
                    Compare(ctx, blobs, Layer<L>::name, "Read", int(index), mode * 100 + int(start), fail);
                }
            }
        ++index;
    }
    std::printf("  %s Read: %u scripts, exceptions injected %llu\n", Layer<L>::name, index, gThrown - before);
}

// ---- construction and destruction ----

// The constructor on a buffer of junk: the original's against the adapter (ours builds our vtable: both count as code 1).
template <class L> void ConstructCases(difftest::Context &ctx)
{
    for (uint32_t junk : {0u, 0xcdcdcdcdu, 0xffffffffu}) {
        Blob blobs[3];
        for (int side = 0; side < 3; side += 2) {
            void *buffer = ::operator new(sizeof(L));
            std::memset(buffer, int(junk & 0xff), sizeof(L));
            Heap heap;
            using Fn = void *(__thiscall *)(void *);
            void *r = side == 0 ? ctx.Original<Fn>(Layer<L>::ctor)(buffer) : Layer<L>::Adapters::Construct(static_cast<L *>(buffer), nullptr);
            blobs[side] = {uint32_t(r == buffer)};
            heap.Stop(blobs[side]);
            L *layer = static_cast<L *>(buffer);
            Capture(blobs[side], layer, 0);
            FreeLayer(layer);
        }
        blobs[1] = blobs[0];
        Compare(ctx, blobs, Layer<L>::name, "Construct", int(junk & 0xff), 0, 0);
    }
}

// The vtable word a destroyed layer is left with: 1 the class's own (game's or ours), 2 our base class's, else the word.
template <class L> uint32_t DestroyedCode(const L *layer)
{
    const void *word = shape::VTableAt(const_cast<L *>(layer), 0);
    return word == GameVT<L>() || word == OurVT<L>() ? 1 : word == OurDestroyedVT<L>() ? 2 : P(word);
}

// Slot 6 (flags 0 and 1, and the bits above): the frees, their sizes and whether the object went.
template <class L> void DtorCases(difftest::Context &ctx)
{
    const unsigned slot = SlotOfRva<L>(ctx, Layer<L>::vf6);
    if (slot == ~0u) return;
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (unsigned n : {0u, 1u, 5u, 17u}) {
        Blob blobs[3];
        for (int side = 0; side < 3; ++side) {
            gTrapHits = 0;
            L *layer = SideLayer<L>(side, slot);
            Plant(layer, flags * 10 + n, n, false);
            difftest::FreeRecorder rec;
            using Fn = void *(__thiscall *)(void *, uint32_t);
            void *r = side == 0 ? ctx.Original<Fn>(Layer<L>::vf6)(layer, flags)
                : side == 1   ? Slot<Fn>(layer, slot)(layer, flags)
                              : Layer<L>::Adapters::DeletingDestructor(layer, nullptr, flags);
            rec.Stop();
            size_t sizes[40]{};
            const size_t freed = rec.SortedSizes(sizes, 40);
            blobs[side] = {uint32_t(r == layer), uint32_t(rec.Freed(layer)), uint32_t(freed), uint32_t(gTrapHits)};
            for (size_t i = 0; i < freed && i < 40; ++i) blobs[side].push_back(uint32_t(sizes[i]));
            if (!rec.Freed(layer)) {
                // Destroyed in place: the vtable word as the original leaves it is not compared (ours stores our base
                // class's; nothing reads it), the count is; the storage is freed raw.
                blobs[side].push_back(layer->mRefCount);
                ::operator delete(layer, sizeof(L));
            }
        }
        Compare(ctx, blobs, Layer<L>::name, "DeletingDestructor", int(flags), int(n), 0);
    }
}

// The destructor body (cWorldDB's and cPartitionManager's destructors release their layers, the bodies run through slot
// 6; the body is hooked for the original slot 6 the game's vtable still holds).
template <class L> void DestructCases(difftest::Context &ctx)
{
    for (unsigned n : {0u, 1u, 4u, 19u}) {
        Blob blobs[3];
        for (int side = 0; side < 3; side += 2) {
            L *layer = NewLayer<L>(GameVT<L>());
            Plant(layer, n + 3, n, false);
            difftest::FreeRecorder rec;
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(Layer<L>::body)(layer);
            else Layer<L>::Adapters::Destruct(layer, nullptr);
            rec.Stop();
            size_t sizes[40]{};
            const size_t freed = rec.SortedSizes(sizes, 40);
            blobs[side] = {uint32_t(rec.Freed(layer)), uint32_t(freed), layer->mRefCount};
            for (size_t i = 0; i < freed && i < 40; ++i) blobs[side].push_back(uint32_t(sizes[i]));
            ::operator delete(layer, sizeof(L));
        }
        blobs[1] = blobs[0];
        Compare(ctx, blobs, Layer<L>::name, "Destruct", int(n), 0, 0);
    }
}

// ---- GetGZCLSID and QueryInterface ----

template <class L> void ClassIdCases(difftest::Context &ctx)
{
    const unsigned slot = SlotOfRva<L>(ctx, Layer<L>::classId);
    if (slot == ~0u) return;
    Blob blobs[3];
    for (int side = 0; side < 3; ++side) {
        gTrapHits = 0;
        L *layer = SideLayer<L>(side, slot);
        using Fn = uint32_t(__thiscall *)(void *);
        const uint32_t id = side == 0 ? ctx.Original<Fn>(Layer<L>::classId)(layer)
            : side == 1               ? Slot<Fn>(layer, slot)(layer)
                                      : Layer<L>::Adapters::GetGZCLSID(layer, nullptr);
        blobs[side] = {id, uint32_t(gTrapHits)};
        Capture(blobs[side], layer, slot);
        FreeLayer(layer);
    }
    Compare(ctx, blobs, Layer<L>::name, "GetGZCLSID", 0, 0, 0);
}

template <class L> void QueryCases(difftest::Context &ctx)
{
    const unsigned slot = SlotOfRva<L>(ctx, Layer<L>::qi);
    if (slot == ~0u) return;
    const uint32_t otherId = std::is_same_v<L, cVertexLayer> ? cFencePostLayer::kClassId : cVertexLayer::kClassId;
    for (uint32_t count : {0u, 1u, 7u}) for (uint32_t riid : {0u, 1u, 2u, L::kClassId, L::kClassId ^ 1, otherId}) for (int nullOut = 0; nullOut < 2; ++nullOut) {
        Blob blobs[3];
        for (int side = 0; side < 3; ++side) {
            gTrapHits = 0;
            L *layer = SideLayer<L>(side, slot);
            layer->mRefCount = count;
            void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
            void **outArg = nullOut ? nullptr : &out;
            using Fn = uint8_t(__thiscall *)(void *, uint32_t, void **);
            const uint8_t ok = side == 0 ? ctx.Original<Fn>(Layer<L>::qi)(layer, riid, outArg)
                : side == 1              ? Slot<Fn>(layer, slot)(layer, riid, outArg)
                                         : Layer<L>::Adapters::QueryInterface(layer, nullptr, riid, outArg);
            blobs[side] = {ok, uint32_t(out == layer), P(out) == 0x1234, uint32_t(gTrapHits)};
            Capture(blobs[side], layer, slot);
            FreeLayer(layer);
        }
        Compare(ctx, blobs, Layer<L>::name, "QueryInterface", int(count), int(riid), nullOut);
    }
}

// The folded AddRef / Release on our vtable against the shared bodies on a game-vtable layer (the last Release deletes
// through slot 6).
template <class L> void UnknownCases(difftest::Context &ctx)
{
    constexpr uint32_t kAddRef = 0x148fb0, kRelease = 0x4b1d00;
    for (uint32_t count : {1u, 2u, 5u}) for (unsigned n : {0u, 6u}) {
        Blob blobs[3];
        for (int side = 0; side < 2; ++side) {
            L *layer = side == 0 ? NewLayer<L>(GameVT<L>()) : NewLayer<L>();
            Plant(layer, count + n, n, false);
            layer->mRefCount = count;
            using Ref = uint32_t(__thiscall *)(void *);
            const uint32_t added = side == 0 ? ctx.Original<Ref>(kAddRef)(layer) : Slot<Ref>(layer, 1)(layer);
            blobs[side] = {added};
            difftest::FreeRecorder rec;
            bool freed = false;
            while (!freed && blobs[side].size() < 20) {
                const uint32_t left = side == 0 ? ctx.Original<Ref>(kRelease)(layer) : Slot<Ref>(layer, 2)(layer);
                blobs[side].push_back(left);
                freed = left == 0;
            }
            rec.Stop();
            blobs[side].push_back(uint32_t(rec.Freed(layer)));
            blobs[side].push_back(uint32_t(rec.Count()));
            if (!freed) FreeLayer(layer);
        }
        blobs[2] = blobs[1];
        Compare(ctx, blobs, Layer<L>::name, "cIGZUnknown", int(count), int(n), 0);
    }
}
} // namespace

DIFF_TEST(VertexLayer_Construct, 0xd146c0) { ConstructCases<cVertexLayer>(ctx); }
DIFF_TEST(VertexLayer_Destruct, 0xd14760) { DestructCases<cVertexLayer>(ctx); }
DIFF_TEST(VertexLayer_DeletingDestructor, 0xd14780) { DtorCases<cVertexLayer>(ctx); }
DIFF_TEST(VertexLayer_Sequence, 0xd147a0) { SequenceCases<cVertexLayer>(ctx, false); }
DIFF_TEST(VertexLayer_SequenceNaN, 0xd147a0) { SequenceCases<cVertexLayer>(ctx, true); }
DIFF_TEST(VertexLayer_GetGZCLSID, 0xd149f0) { ClassIdCases<cVertexLayer>(ctx); }
DIFF_TEST(VertexLayer_QueryInterface, 0xd14a00) { QueryCases<cVertexLayer>(ctx); }
DIFF_TEST(VertexLayer_Read, 0xd14a30) { ReadCases<cVertexLayer>(ctx); }
DIFF_TEST(VertexLayer_Write, 0xd14c50) { WriteCases<cVertexLayer>(ctx); }
DIFF_TEST(VertexLayer_Unknown, 0x148fb0) { UnknownCases<cVertexLayer>(ctx); }

DIFF_TEST(FencePostLayer_Construct, 0x820530) { ConstructCases<cFencePostLayer>(ctx); }
DIFF_TEST(FencePostLayer_Destruct, 0x820580) { DestructCases<cFencePostLayer>(ctx); }
DIFF_TEST(FencePostLayer_DeletingDestructor, 0x820590) { DtorCases<cFencePostLayer>(ctx); }
DIFF_TEST(FencePostLayer_Sequence, 0x8205b0) { SequenceCases<cFencePostLayer>(ctx, false); }
DIFF_TEST(FencePostLayer_SequenceNaN, 0x8205b0) { SequenceCases<cFencePostLayer>(ctx, true); }
DIFF_TEST(FencePostLayer_GetGZCLSID, 0x820720) { ClassIdCases<cFencePostLayer>(ctx); }
DIFF_TEST(FencePostLayer_QueryInterface, 0x820880) { QueryCases<cFencePostLayer>(ctx); }
DIFF_TEST(FencePostLayer_Read, 0x8208b0) { ReadCases<cFencePostLayer>(ctx); }
DIFF_TEST(FencePostLayer_Write, 0x820a80) { WriteCases<cFencePostLayer>(ctx); }
DIFF_TEST(FencePostLayer_Unknown, 0x148fb0) { UnknownCases<cFencePostLayer>(ctx); }
