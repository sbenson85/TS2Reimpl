#pragma once
// nTSWorld::c3DArray<T> (lane VO, campaign world-lot WL-0): a counted, serializable 3D grid. 0x20 bytes: +0 the
// cIGZSerializable vtable (7 slots), +4 the reference count, +8 / +0xc / +0x10 the sizes x, y, z, +0x14 the elements
// (std::vector<T>). Element (x, y, z) is data[(sizeX * z + x) * sizeY + y]. Class id 0x2a51171b (GetGZCLSID, shared by
// every copy). RVAs 1.18.0.187.
//
// The image has ten copies (docs/subsystems/world-lot.md, "c3DArray"): the eight cWorldDB grids (seven element types, the
// pattern and rotation types twice), the wall tiles of cWallGraph and the room ids of cRoomManager. One template here,
// instantiated per element type; what differs per copy (the stream calls of an element, the default of a new element and
// the quirks of a short stream) is in c3DArrayElement<T>.
//
// Record: u32 class id, u32 version 1, GZStr "c3DArray", s32 x, s32 y, s32 z, then the elements in storage order. The
// element count is the vector's size, not x * y * z (Write); Read sizes the vector to x * y * z first. Write always
// returns true; Read returns false on a wrong class id or version, else true (the stream's errors are not looked at).
//
// Standalone shape (docs/standalone-shape.md): a real class, slot 6 the compiler's deleting destructor. The objects the
// database builds are ours (cWorldDB::Init); the game still builds some with its own vtables (cWorldDB slots 9 and 11,
// cWallGraph slot 5, cRoomManager slot 7, the copy embedded in cWorldOutsideSupport), so the slot bodies are hooked through
// c3DArrayAdapters. Slots 0-2 are bodies folded across unrelated classes (QueryInterface 0x178b20: riid 1 only; AddRef
// 0x148fb0; Release 0x4b1d00: deletes through slot 6 at zero): ours only in our vtable. Slot 5 (0x806aa0) is shared by the
// ten copies only.
#include "gz/cIGZSerializable.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

namespace nTSWorld {

// The element types (the original's names in brackets).
// cWorldDB::cFloorPatternStorageTile: the pattern of each of the tile's four segments (0: none).
struct FloorPatternTile {
    uint16_t segment[4];
};
// cWorldDB::cFloorPatternRotationTile: four nibbles, one per segment; a new tile is 0x1111.
using FloorRotationTile = uint16_t;
// cWorldDB::cFloorVisibilityTile: one flag per segment; a new tile is 1, 1, 1, 1.
struct FloorVisibilityTile {
    uint8_t segment[4];
};
// cWorldDB::cDeckFoundationMarkerTile: a new tile is all -1.
struct DeckFoundationTile {
    uint32_t segment[4];
};
// cAwningTile: a new tile is -1, -1.
struct AwningTile {
    uint32_t first, second;
};
// std::vector<long>: the object ids on a tile (negated: a footprint).
using ObjectList = std::vector<int32_t>;
// cWallGraph::cWallTile: one byte of flags (a distinct type: its Read ORs into the byte, unlike the plain byte grid).
struct WallTile {
    uint8_t bits;
};
// cRoomManager::cRoomIDTile: the room id of each segment.
struct RoomIDTile {
    int16_t segment[4];
};
static_assert(sizeof(FloorPatternTile) == 8 && sizeof(FloorVisibilityTile) == 4 && sizeof(DeckFoundationTile) == 16);
static_assert(sizeof(AwningTile) == 8 && sizeof(ObjectList) == 12 && sizeof(WallTile) == 1 && sizeof(RoomIDTile) == 8);

// Per copy: the game's vtable, the default of a new element, and how the elements go to and come from a stream.
// WriteAll / ReadAll run over the vector's current size.
template <class T> struct c3DArrayElement;

template <class T> class c3DArray : public cIGZSerializable {
public:
    using Element = c3DArrayElement<T>;
    static constexpr uint32_t kClassId = 0x2a51171b;
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kVTable = Element::kVTable; // the game's vtable of this copy

    c3DArray() = default; // count 0, sizes 0, no elements (what the game's builders store)
    c3DArray(const c3DArray &) = delete;
    c3DArray &operator=(const c3DArray &) = delete;

    // cIGZUnknown (folded bodies, see above)
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override;
    uint32_t Release() override;

    // cIGZSerializable
    bool Write(ifaces::cIGZOStream *stream) override; // slot 3
    bool Read(ifaces::cIGZIStream *stream) override;  // slot 4
    uint32_t GetGZCLSID() override;                   // slot 5, 0x806aa0
    virtual ~c3DArray();                              // slot 6 (deleting); the body alone is 0x81d2f0 for <uint8_t>

    // x * y * z elements (new ones the element's default, from the vector's resize), then the sizes. The game's copies
    // inline it in Read (the float, object, deck, rotation and room copies) or call it (the others; RVAs in the hook table).
    void Resize(int32_t sizeX, int32_t sizeY, int32_t sizeZ);
    T &At(int32_t x, int32_t y, int32_t z) { return mData.data()[(mSizeX * z + x) * mSizeY + y]; }

    uint32_t mRefCount = 0; // +4
    int32_t mSizeX = 0;     // +8
    int32_t mSizeY = 0;     // +0xc
    int32_t mSizeZ = 0;     // +0x10
    std::vector<T> mData;   // +0x14
};

// The hooks' entry points (docs/standalone-shape.md step 6): `this` is a grid with the game's vtable or ours; each calls
// our member without virtual dispatch.
template <class T> struct c3DArrayAdapters {
    using Self = c3DArray<T>;
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(Self *self, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static void __fastcall Destruct(Self *self, void *);
    static void __fastcall Resize(Self *self, void *, int32_t sizeX, int32_t sizeY, int32_t sizeZ);
};

using PatternGrid = c3DArray<FloorPatternTile>;
using RotationGrid = c3DArray<FloorRotationTile>;
using FloatGrid = c3DArray<float>;
using VisibilityGrid = c3DArray<FloorVisibilityTile>;
using ByteGrid = c3DArray<uint8_t>;
using ObjectGrid = c3DArray<ObjectList>;
using DeckGrid = c3DArray<DeckFoundationTile>;
using AwningGrid = c3DArray<AwningTile>;
using WallTileGrid = c3DArray<WallTile>;
using RoomIDGrid = c3DArray<RoomIDTile>;

// The game's vtables of the copies.
constexpr uint32_t kPatternGridVTable = 0xebb740;
constexpr uint32_t kRotationGridVTable = 0xebb760;
constexpr uint32_t kFloatGridVTable = 0xebb780;
constexpr uint32_t kVisibilityGridVTable = 0xebb7a0;
constexpr uint32_t kObjectGridVTable = 0xebb7c0;
constexpr uint32_t kDeckGridVTable = 0xebb7e0;
constexpr uint32_t kAwningGridVTable = 0xebb800;
constexpr uint32_t kByteGridVTable = 0xe05250;
constexpr uint32_t kWallTileGridVTable = 0xe048b0;
constexpr uint32_t kRoomIDGridVTable = 0xe0495c;

template <> struct c3DArrayElement<FloorPatternTile> {
    static constexpr uint32_t kVTable = kPatternGridVTable;
    static FloorPatternTile Default() { return {}; }
    static void WriteAll(ifaces::cIGZOStream *stream, const std::vector<FloorPatternTile> &data);
    static void ReadAll(ifaces::cIGZIStream *stream, std::vector<FloorPatternTile> &data);
};
template <> struct c3DArrayElement<FloorRotationTile> {
    static constexpr uint32_t kVTable = kRotationGridVTable;
    static FloorRotationTile Default() { return 0x1111; }
    static void WriteAll(ifaces::cIGZOStream *stream, const std::vector<FloorRotationTile> &data);
    static void ReadAll(ifaces::cIGZIStream *stream, std::vector<FloorRotationTile> &data);
};
template <> struct c3DArrayElement<float> {
    static constexpr uint32_t kVTable = kFloatGridVTable;
    static float Default() { return 0.0f; }
    static void WriteAll(ifaces::cIGZOStream *stream, const std::vector<float> &data);
    static void ReadAll(ifaces::cIGZIStream *stream, std::vector<float> &data);
};
template <> struct c3DArrayElement<FloorVisibilityTile> {
    static constexpr uint32_t kVTable = kVisibilityGridVTable;
    static FloorVisibilityTile Default() { return {{1, 1, 1, 1}}; }
    static void WriteAll(ifaces::cIGZOStream *stream, const std::vector<FloorVisibilityTile> &data);
    static void ReadAll(ifaces::cIGZIStream *stream, std::vector<FloorVisibilityTile> &data);
};
template <> struct c3DArrayElement<uint8_t> {
    static constexpr uint32_t kVTable = kByteGridVTable;
    static uint8_t Default() { return 0; }
    static void WriteAll(ifaces::cIGZOStream *stream, const std::vector<uint8_t> &data);
    static void ReadAll(ifaces::cIGZIStream *stream, std::vector<uint8_t> &data);
};
template <> struct c3DArrayElement<ObjectList> {
    static constexpr uint32_t kVTable = kObjectGridVTable;
    static ObjectList Default() { return {}; }
    static void WriteAll(ifaces::cIGZOStream *stream, const std::vector<ObjectList> &data);
    static void ReadAll(ifaces::cIGZIStream *stream, std::vector<ObjectList> &data);
};
template <> struct c3DArrayElement<DeckFoundationTile> {
    static constexpr uint32_t kVTable = kDeckGridVTable;
    static DeckFoundationTile Default() { return {{~0u, ~0u, ~0u, ~0u}}; }
    static void WriteAll(ifaces::cIGZOStream *stream, const std::vector<DeckFoundationTile> &data);
    static void ReadAll(ifaces::cIGZIStream *stream, std::vector<DeckFoundationTile> &data);
};
template <> struct c3DArrayElement<AwningTile> {
    static constexpr uint32_t kVTable = kAwningGridVTable;
    static AwningTile Default() { return {~0u, ~0u}; }
    static void WriteAll(ifaces::cIGZOStream *stream, const std::vector<AwningTile> &data);
    static void ReadAll(ifaces::cIGZIStream *stream, std::vector<AwningTile> &data);
};
template <> struct c3DArrayElement<WallTile> {
    static constexpr uint32_t kVTable = kWallTileGridVTable;
    static WallTile Default() { return {0}; }
    static void WriteAll(ifaces::cIGZOStream *stream, const std::vector<WallTile> &data);
    static void ReadAll(ifaces::cIGZIStream *stream, std::vector<WallTile> &data);
};
template <> struct c3DArrayElement<RoomIDTile> {
    static constexpr uint32_t kVTable = kRoomIDGridVTable;
    static RoomIDTile Default() { return {}; }
    static void WriteAll(ifaces::cIGZOStream *stream, const std::vector<RoomIDTile> &data);
    static void ReadAll(ifaces::cIGZIStream *stream, std::vector<RoomIDTile> &data);
};

static_assert(sizeof(ByteGrid) == 0x20, "c3DArray: new 0x20 in cWorldDB::Init");
static_assert(offsetof(ByteGrid, mRefCount) == 4 && offsetof(ByteGrid, mSizeX) == 8 && offsetof(ByteGrid, mData) == 0x14);
static_assert(sizeof(ObjectGrid) == 0x20 && offsetof(ObjectGrid, mData) == 0x14);

extern template class c3DArray<FloorPatternTile>;
extern template class c3DArray<FloorRotationTile>;
extern template class c3DArray<float>;
extern template class c3DArray<FloorVisibilityTile>;
extern template class c3DArray<uint8_t>;
extern template class c3DArray<ObjectList>;
extern template class c3DArray<DeckFoundationTile>;
extern template class c3DArray<AwningTile>;
extern template class c3DArray<WallTile>;
extern template class c3DArray<RoomIDTile>;
extern template struct c3DArrayAdapters<FloorPatternTile>;
extern template struct c3DArrayAdapters<FloorRotationTile>;
extern template struct c3DArrayAdapters<float>;
extern template struct c3DArrayAdapters<FloorVisibilityTile>;
extern template struct c3DArrayAdapters<uint8_t>;
extern template struct c3DArrayAdapters<ObjectList>;
extern template struct c3DArrayAdapters<DeckFoundationTile>;
extern template struct c3DArrayAdapters<AwningTile>;
extern template struct c3DArrayAdapters<WallTile>;
extern template struct c3DArrayAdapters<RoomIDTile>;

} // namespace nTSWorld
