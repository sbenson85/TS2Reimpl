#pragma once
// nTSWorld::cVertexLayer and nTSWorld::cFencePostLayer (lane VO, campaign world-lot WL-0): counted, serializable maps
// keyed by a lot vertex (x, y, level). RVAs 1.18.0.187.
//
// cVertexLayer (vtable 0xebb834, 0x18 bytes): the world database's object index (cWorldDB +0xc4). +8 vertex -> id,
// +0x10 id -> vertex; an id sits at one vertex and a vertex holds one id (the first one added). Class id 0xcb4387a1.
// cFencePostLayer (vtable 0xe053ac, 0x10 bytes): cPartitionManager's fence posts. +8 vertex -> value. Class id 0xab4ba572.
//
// Both: +0 the vtable, +4 the reference count, +8 std::map<LayerVertex, value> (MSVC's tree: head pointer, size; nodes
// of 0x20 bytes, the pair at +0x10). Contains (0x8207a0), ValueOf (0x820810) and Vertices (0x820730) work on the +8
// map of either (cWorldDB calls them on its cVertexLayer, cPartitionManager on its cFencePostLayer).
//
// Record: u32 class id (the object's own slot 5), u32 version 1, GZStr name ("cVertexLayer" / "cFencePostLayer"), s32
// entry count, then per entry in key order s32 level, f32 x, f32 y and the value (s32 id / u32 value). Read refills the
// +8 map through Add / Set.
//
// Standalone shape (docs/standalone-shape.md): real classes, slot 6 the compiler's deleting destructor, cFencePostLayer's
// slot 7 a virtual Clear. The game still builds both with its own vtables (cWorldDB::Init through 0xd146c0 until it builds
// ours, cPartitionManager slot 3), so the bodies are hooked through the adapters. Slots 1-2 are the folded AddRef 0x148fb0
// and Release 0x4b1d00 (ours only in our vtable).
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZSerializable.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

namespace nTSWorld {

// A lot vertex: x, y (floats) and the level.
struct LayerVertex {
    float x = -1.0f;
    float y = -1.0f;
    int32_t level = 0;
};
static_assert(sizeof(LayerVertex) == 12);

// The list Vertices fills: the game's vector over the allocator service (its growth frees through slot 4).
using LayerVertexList = std::vector<LayerVertex, GZServiceAllocator<LayerVertex>>;

// The maps' order: by level first (signed), then x, then y (float compares: NaN keys order as the original's do).
struct LayerVertexLess {
    bool operator()(const LayerVertex &a, const LayerVertex &b) const
    {
        if (a.level != b.level) return a.level < b.level;
        if (a.x == b.x) return a.y < b.y;
        return a.x < b.x;
    }
};

// What both layers share: the count and the +8 map (Value: int32_t for cVertexLayer, uint32_t for cFencePostLayer).
template <class Value> class cVertexMapLayer : public cIGZSerializable {
public:
    using Map = std::map<LayerVertex, Value, LayerVertexLess>;

    // cIGZUnknown slots 1-2 (folded bodies 0x148fb0, 0x4b1d00: at zero the object deletes itself through slot 6)
    uint32_t AddRef() override;
    uint32_t Release() override;
    virtual ~cVertexMapLayer() = default; // slot 6 (deleting)

    // 0x8207a0: whether the vertex has an entry.
    bool Contains(const LayerVertex &vertex) const;
    // 0x820810: the vertex's value; for a vertex without one, the value field of the map's head node (never written:
    // what its allocation held).
    Value ValueOf(const LayerVertex &vertex) const;
    // 0x820730: the vertices in key order into out (cleared first, capacity kept); true if there are any.
    bool Vertices(LayerVertexList &out) const;

    uint32_t mRefCount = 0; // +4
    Map mMap;               // +8
};

class cVertexLayer : public cVertexMapLayer<int32_t> {
public:
    static constexpr uint32_t kClassId = 0xcb4387a1;
    static constexpr uint32_t kVTable = 0xebb834;

    cVertexLayer() = default; // 0xd146c0
    cVertexLayer(const cVertexLayer &) = delete;
    cVertexLayer &operator=(const cVertexLayer &) = delete;
    ~cVertexLayer() override = default; // body 0xd14760, deleting 0xd14780

    bool QueryInterface(uint32_t riid, void **out) override; // 0xd14a00: riids 1 and kClassId
    bool Write(ifaces::cIGZOStream *stream) override;        // 0xd14c50
    bool Read(ifaces::cIGZIStream *stream) override;         // 0xd14a30
    uint32_t GetGZCLSID() override;                          // 0xd149f0

    // 0xd147a0: the id's old vertex removed (if it still holds the id); then, if the vertex is free, vertex -> id and
    // id -> vertex. A vertex that already holds an id keeps it, and the id is left without a vertex.
    void Add(const LayerVertex &vertex, int32_t id);
    // 0xd14b70: if the vertex holds this id, both entries go.
    void Remove(const LayerVertex &vertex, int32_t id);

    std::map<int32_t, LayerVertex> mIds; // +0x10
};

class cFencePostLayer : public cVertexMapLayer<uint32_t> {
public:
    static constexpr uint32_t kClassId = 0xab4ba572;
    static constexpr uint32_t kVTable = 0xe053ac;

    cFencePostLayer() = default; // 0x820530
    cFencePostLayer(const cFencePostLayer &) = delete;
    cFencePostLayer &operator=(const cFencePostLayer &) = delete;
    ~cFencePostLayer() override = default; // body 0x820580, deleting 0x820590

    bool QueryInterface(uint32_t riid, void **out) override; // 0x820880: riids 1 and kClassId
    bool Write(ifaces::cIGZOStream *stream) override;        // 0x820a80
    bool Read(ifaces::cIGZIStream *stream) override;         // 0x8208b0
    uint32_t GetGZCLSID() override;                          // 0x820720
    virtual void Clear();                                    // slot 7, 0x820710

    // 0x8205b0: a new vertex gets the value (true); a present one keeps its own (false).
    bool Set(const LayerVertex &vertex, uint32_t value);
    // 0x8209f0: true if the vertex had an entry (now gone).
    bool Remove(const LayerVertex &vertex);
};

static_assert(sizeof(cVertexLayer) == 0x18 && offsetof(cVertexLayer, mRefCount) == 4);
static_assert(offsetof(cVertexLayer, mMap) == 8 && offsetof(cVertexLayer, mIds) == 0x10);
static_assert(sizeof(cFencePostLayer) == 0x10 && offsetof(cFencePostLayer, mMap) == 8);

// The hooks' entry points: `this` is a layer with the game's vtable or ours; each calls our member without virtual
// dispatch (Construct builds ours in place over the game's: our vtable).
struct cVertexLayerAdapters {
    using Self = cVertexLayer;
    static Self *__fastcall Construct(Self *self, void *);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(Self *self, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static void __fastcall Add(Self *self, void *, const LayerVertex *vertex, int32_t id);
    static void __fastcall Remove(Self *self, void *, const LayerVertex *vertex, int32_t id);
};

struct cFencePostLayerAdapters {
    using Self = cFencePostLayer;
    static Self *__fastcall Construct(Self *self, void *);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(Self *self, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static void __fastcall Clear(Self *self, void *);
    static bool __fastcall Set(Self *self, void *, const LayerVertex *vertex, uint32_t value);
    static bool __fastcall Remove(Self *self, void *, const LayerVertex *vertex);
    // The shared bodies (either layer: only the +8 map is used; its values are 32-bit either way).
    static bool __fastcall Contains(Self *self, void *, const LayerVertex *vertex);
    static uint32_t __fastcall ValueOf(Self *self, void *, const LayerVertex *vertex);
    static bool __fastcall Vertices(Self *self, void *, LayerVertexList *out);
};

extern template class cVertexMapLayer<int32_t>;
extern template class cVertexMapLayer<uint32_t>;

} // namespace nTSWorld
