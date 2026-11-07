#pragma once
// nTSWorld::cWorldDB (lane JC): the world database service (system service id 0x4a037ee3, priority -1,000,001; class id
// 0x49ff7d76, cIWorldDB iid 0x49ff7d6f). 0xd4 bytes, /O2. It holds the lot's bounds and level range, the tile grids of the
// floors (pattern ids and rotations of two layers, elevations, visibility, a byte grid, the objects on each tile, deck
// foundation markers, awnings), the world objects the framework makes (terrain, roofs, pools, partitions, ...) and the
// pattern name table, and posts a message for every change while mPostMessages is set.
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (game vtable 0xebb4b0, 20 slots: 13-19 the class's own),
// +4 cRZUnknownMT (0xebb504), +0x1c nTSWorld::cIWorldDB / cIGZSerializable (0xebb520, 101 slots), +0x20 cIGZMessageTarget2
// (0xebb6b8). The original's +0x1c slot bodies take `this` as that face and reach the fields at face-relative offsets; ours
// are members. Calls the original makes through its own vtable (the bounds getters, the pattern getters, IsInside, ...) are
// virtual calls here too.
//
// Standalone shape (docs/standalone-shape.md): the constructor 0xd0b6c0 is called only by cTSAppFW::AddApplicationServicesHere
// (ours); its hook builds our class (placement new). The vtables are referenced only by the constructor and the destructor
// 0xd0b960, so every object carries ours. Slots still game code forward to their originals on the cIWorldDB face, every
// argument and result a whole dword; their RVAs are the constructor entry's original_calls.
//
// A tile is addressed by (x, y, level) and a mask of its four segments (WorldTile). Grid element (x, y, level), relative to
// the origin (mMinX, mMinY, mMinLevel; 0xd13780 subtracts it), is data[(sizeX * level + x) * sizeY + y].
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"
#include "game/vtable.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZString.h"
#include "world/c3DArray.h"
#include "world/cStringMap.h"
#include "world/cVertexLayer.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

class cIGZString;

namespace nTSWorld {

// A tile: x, y, level and the mask of its four segments (bit i = segment i; read as a signed byte). 0x10 bytes as passed.
struct WorldTile {
    int32_t x, y, level;
    int8_t segments;
    uint8_t pad[3];
};
static_assert(sizeof(WorldTile) == 0x10, "WorldTile");

// A tile without the mask, relative to the origin: the elements of the object-cell sets (+0xb8) and of slot 24's vector.
// Ordered by x, then y, then level.
struct WorldCell {
    int32_t x, y, level;
};
static_assert(sizeof(WorldCell) == 0xc, "WorldCell");
struct WorldCellLess {
    bool operator()(const WorldCell &a, const WorldCell &b) const
    {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.level < b.level;
    }
};
using ObjectCellSet = std::set<WorldCell, WorldCellLess>;
// The cells slot 24 copies out (its buffer from the allocator service: the game frees it there, 0x197100).
using WorldCellList = std::vector<WorldCell, GZServiceAllocator<WorldCell>>;
static_assert(sizeof(ObjectCellSet) == 8, "MSVC 14.40 set");

// A position in world units (x, y as floats, the level an integer): slots 25, 69 and Slot22.
struct WorldPoint {
    float x, y;
    int32_t level;
};

// A rectangle in world units (x0, y0, x1, y1): Slot21's footprint.
struct WorldRect {
    float x0, y0, x1, y1;
};

// A pattern name: the pattern table's interned text (null for none). Returned by value (through the hidden pointer).
struct WorldName {
    const char *text;
};
static_assert(sizeof(WorldName) == 4, "WorldName");

// Two words returned by value (slot 60: the awning tile; -1, -1 off the lot).
struct WorldPair {
    uint32_t first, second;
};

// The grids (nTSWorld::c3DArray<T>, world/c3DArray.h): Init builds ours; the game's slots 9 and 11 still build some with
// the game's vtables. The database indexes them directly.

// The pattern table (+0xc0): nTSWorld::cStringMap<unsigned short> (world/cStringMap.h). Its slot 7 maps a name to an id,
// slot 8 an id to a name, slot 9 hands out a new id. The world database keeps the use counts in its maps itself (0xd138d0).
using PatternTable = cStringMap;

// The set slot 47 fills (nodes from the allocator service, 0x14 bytes).
using PatternNameSet = std::set<const char *, PatternNameLess, GZServiceAllocator<const char *>>;

// A cRZString as a value (the hook-shaped string's own constructor and destructor bodies).
struct WorldString {
    union {
        cRZString s;
    };
    WorldString() { s.Construct(); }
    explicit WorldString(const char *text) { s.Construct(text); }
    WorldString(const WorldString &) = delete;
    WorldString &operator=(const WorldString &) = delete;
    ~WorldString() { s.Destruct(); }
};
static_assert(sizeof(WorldString) == 0x20, "cRZString");

// A counted pointer to a framework object, a grid or a table (AddRef slot 1, Release slot 2 through its own vtable).
template <class T> class WorldRef {
public:
    WorldRef() = default;
    WorldRef(const WorldRef &) = delete;
    WorldRef &operator=(const WorldRef &) = delete;
    ~WorldRef() noexcept(false)
    {
        if (mObject) ReleaseOf(mObject);
    }
    // The new one counted and stored, then the old one released; nothing for the same pointer.
    void Assign(T *object)
    {
        T *const old = mObject;
        if (object == old) return;
        if (object) AddRefOf(object);
        mObject = object;
        if (old) ReleaseOf(old);
    }
    // The held one released, the member nulled first.
    void Clear()
    {
        if (T *const old = mObject) {
            mObject = nullptr;
            ReleaseOf(old);
        }
    }
    // For an out-parameter (CreateInstance): what is held released (nulled first), the slot handed out.
    void **Out()
    {
        Clear();
        return reinterpret_cast<void **>(&mObject);
    }
    T *Get() const { return mObject; }
    T *operator->() const { return mObject; }
    explicit operator bool() const { return mObject != nullptr; }

private:
    static void AddRefOf(T *object) { VirtualCall<uint32_t(__thiscall *)(void *), 1>(object); }
    static void ReleaseOf(T *object) { VirtualCall<uint32_t(__thiscall *)(void *), 2>(object); }
    T *mObject = nullptr;
};
static_assert(sizeof(WorldRef<void>) == 4, "WorldRef");

// nTSWorld::cIWorldDB (iid 0x49ff7d6f): slots 6-100 after cIGZSerializable.
class cIWorldDB : public cIGZSerializable {
public:
    virtual void SetBounds(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY) = 0;         // 6
    virtual void SetLevels(int32_t minLevel, int32_t maxLevel) = 0;                             // 7
    virtual bool Save(void *writer, bool skipObjects) = 0;                                      // 8
    virtual uint32_t Load(uint32_t a, uint32_t b) = 0;                                          // 9 (game code)
    virtual bool Reset() = 0;                                                                   // 10
    virtual uint32_t Create(uint32_t a, uint32_t b, uint32_t c) = 0;                            // 11 (game code)
    virtual void Slot12(uint32_t a) = 0;                                                        // 12
    virtual void SetFloorPattern(const WorldName *name, const WorldTile *tile) = 0;             // 13
    virtual void SetFloorRotation(const uint16_t *rotation, const WorldTile *tile) = 0;         // 14
    virtual void SetFloorPatternB(const WorldName *name, const WorldTile *tile) = 0;            // 15
    virtual void SetFloorRotationB(const uint16_t *rotation, const WorldTile *tile) = 0;        // 16
    virtual void SetFloorPatternQuiet(const WorldName *name, const WorldTile *tile) = 0;        // 17
    virtual void SetFloorElevation(float elevation, const WorldTile *tile) = 0;                 // 18
    virtual void SetFloorElevationQuiet(float elevation, const WorldTile *tile) = 0;            // 19
    virtual void SetFloorVisibility(uint8_t visible, const WorldTile *tile) = 0;                // 20
    virtual void AddObjectRects(int32_t id, const std::vector<WorldRect> *rects, int32_t level) = 0; // 21
    virtual void AddObjectAt(int32_t id, const WorldPoint *point) = 0;                          // 22
    virtual void RemoveObject(int32_t id) = 0;                                                  // 23
    virtual bool GetObjectCells(int32_t id, WorldCellList *cells) = 0;                          // 24
    virtual void RemoveObjectAt(int32_t id, const WorldPoint *point) = 0;                       // 25
    virtual void SetObjectVertex(int32_t id, const LayerVertex *vertex) = 0;                    // 26
    virtual void RemoveObjectVertex(int32_t id, const LayerVertex *vertex) = 0;                 // 27
    virtual void SetByteTile(uint8_t value, const WorldTile *tile) = 0;                         // 28
    virtual void SetDeckFoundation(const uint32_t *value, const WorldTile *tile) = 0;           // 29
    virtual void SetAwning(const uint32_t *value, const WorldTile *tile) = 0;                   // 30
    virtual void SetAwningPair(const AwningTile *value, const WorldTile *tile) = 0;             // 31
    virtual void SetMinGrade(int32_t value) = 0;                                                // 32
    virtual void SetName(cIGZString *name) = 0;                                                 // 33
    virtual bool PostsMessages() = 0;                                                           // 34
    virtual void *GetTerrain() = 0;                                                             // 35
    virtual void *GetRoofs() = 0;                                                               // 36
    virtual void *GetPools() = 0;                                                               // 37
    virtual void *GetCurvedPools() = 0;                                                         // 38
    virtual void *GetPartitions() = 0;                                                          // 39
    virtual WorldName GetEmptyPatternName() = 0;                                                // 40
    virtual WorldName GetDefaultPatternName() = 0;                                              // 41
    virtual void *StoreVTable(void *target) = 0; // 42: returns target (the original leaves it in eax)
    virtual WorldName GetFloorPatternName(const WorldTile *tile) = 0;                           // 43
    virtual uint32_t GetFloorRotation(const WorldTile *tile) = 0;                               // 44
    virtual WorldName GetFloorPatternNameB(const WorldTile *tile) = 0;                          // 45
    virtual uint32_t GetFloorRotationB(const WorldTile *tile) = 0;                              // 46
    virtual void GetPatternNames(PatternNameSet *names) = 0;                                    // 47
    virtual bool HasPattern(const WorldName *name) = 0;                                         // 48
    virtual bool GetFloorVisibility(const WorldTile *tile, uint8_t *visible) = 0;               // 49
    virtual bool HasFloor(const WorldTile *tile, int8_t mask, bool checkVisible) = 0;           // 50
    virtual bool HasAnyFloor(const WorldTile *tile, int8_t mask, bool checkVisible) = 0;        // 51
    virtual bool HasFloorOrGround(const WorldTile *tile, int8_t mask, bool checkVisible) = 0;   // 52
    virtual bool HasAnyFloorOrGround(const WorldTile *tile, int8_t mask, bool checkVisible) = 0; // 53
    virtual bool HasFloorAbove(const WorldTile *tile, int8_t mask, bool checkVisible) = 0;      // 54
    virtual bool HasAnyFloorAbove(const WorldTile *tile, int8_t mask, bool checkVisible) = 0;   // 55
    virtual bool TerrainSlot13(const WorldTile *tile) = 0;                                      // 56
    virtual bool GetByteTile(const WorldTile *tile) = 0;                                        // 57
    virtual uint32_t GetDeckFoundation(const WorldTile *tile) = 0;                              // 58
    virtual uint32_t GetAwning(const WorldTile *tile) = 0;                                      // 59
    virtual WorldPair GetAwningPair(const WorldTile *tile) = 0;                                 // 60
    virtual bool IsFloorEmpty(const WorldTile *tile, int8_t mask) = 0;                          // 61
    virtual float GetElevation(const WorldTile *tile) = 0;                                      // 62
    virtual float GetTerrainHeight(int32_t x, int32_t y) = 0;                                   // 63
    virtual float GetGroundElevation(int32_t x, int32_t y) = 0;                                 // 64
    virtual int32_t CountObjects(const WorldTile *tile) = 0;                                    // 65
    virtual int32_t FindObject(int32_t id, const WorldTile *tile, bool footprints) = 0;         // 66
    virtual int32_t GetObjectAt(int32_t index, const WorldTile *tile, bool footprints) = 0;     // 67
    virtual int32_t ObjectAtVertex(const LayerVertex *vertex) = 0;                              // 68
    virtual bool IsInWorldPoint(const WorldPoint *point) = 0;                                   // 69
    virtual bool IsInsideCorners(const WorldTile *tile) = 0;                                    // 70
    virtual bool IsInside(const WorldTile *tile) = 0;                                           // 71
    virtual bool IsInWorld(float x, float y) = 0;                                               // 72
    virtual bool IsInterior(const WorldTile *tile) = 0;                                         // 73
    virtual bool IsInteriorPoint(float x, float y) = 0;                                         // 74
    virtual float GetMinXF() = 0;                                                               // 75
    virtual float GetMinYF() = 0;                                                               // 76
    virtual float GetMaxXF() = 0;                                                               // 77
    virtual float GetMaxYF() = 0;                                                               // 78
    virtual float GetWidthF() = 0;                                                              // 79
    virtual float GetHeightF() = 0;                                                             // 80
    virtual int32_t GetMinX() = 0;                                                              // 81
    virtual int32_t GetMinY() = 0;                                                              // 82
    virtual int32_t GetMaxX() = 0;                                                              // 83
    virtual int32_t GetMaxY() = 0;                                                              // 84
    virtual int32_t GetWidth() = 0;                                                             // 85
    virtual int32_t GetHeight() = 0;                                                            // 86
    virtual float GetTileSize() = 0;                                                            // 87
    virtual int32_t GetMinLevel() = 0;                                                          // 88
    virtual int32_t GetMaxLevel() = 0;                                                          // 89
    virtual int32_t GetLevelCount() = 0;                                                        // 90
    virtual float GetLevelTop() = 0;                                                            // 91
    virtual float GetLevelBottom() = 0;                                                         // 92
    virtual float GetMaxGradeHeight() = 0;                                                      // 93
    virtual float GetLevelHeight() = 0;                                                         // 94
    virtual float GetWallHeight() = 0;                                                          // 95
    virtual float GetFoundationHeight() = 0;                                                    // 96
    virtual uint8_t UnevenTilesFloorable() = 0; // 97: the byte as stored (DoMessage copies the setting's byte)
    virtual int32_t GetMinGrade() = 0;                                                          // 98
    virtual bool GetName(cIGZString *out) = 0;                                                  // 99
    virtual void *GetOutsideSupport() = 0;                                                      // 100
};

class cWorldDB : public rz::cRZSystemService, public cIWorldDB, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kVTable = 0xebb4b0;
    static constexpr uint32_t kVTable4 = 0xebb504;
    static constexpr uint32_t kWorldVTable = 0xebb520;
    static constexpr uint32_t kMessageVTable = 0xebb6b8;
    static constexpr uint32_t kSize = 0xd4;
    static constexpr uint32_t kServiceId = 0x4a037ee3;
    static constexpr int32_t kPriority = int32_t(0xfff0bdbf);
    static constexpr uint32_t kClassId = 0x49ff7d76;
    static constexpr uint32_t kIid = 0x49ff7d6f; // cIWorldDB

    cWorldDB();                    // 0xd0b6c0
    ~cWorldDB() noexcept override; // 0xd0b960; deleting 0xd0c030 (+4 thunk 0xd0bd57)
    cWorldDB(const cWorldDB &) = delete;
    cWorldDB &operator=(const cWorldDB &) = delete;

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xd11360; thunks 0xd113b9 (+4), 0xd113c1 (+0x1c), 0xd113c9 (+0x20)
    uint32_t AddRef() override;                              // folded 0x1cc320
    uint32_t Release() override;                             // folded 0x1cd7a0

    // ---- cIGZSystemService ----
    bool Init() override;     // 0xd0e6b0
    bool Shutdown() override; // 0xd133c0

    // ---- the primary face's own slots 13-19 ----
    virtual void SetPatternId(uint16_t id, const WorldTile *tile);              // 13 (0xd12f50)
    virtual void SetPatternByName(const WorldName *name, const WorldTile *tile); // 14 (0xd12e70: jmp 0xd12df0)
    virtual void SetElevationRaw(float elevation, const WorldTile *tile);       // 15 (0xd12c50)
    virtual void SetByteTileRaw(uint8_t value, const WorldTile *tile);          // 16 (0xd13300: jmp 0xd13290)
    virtual void SetPatternIdB(uint16_t id, const WorldTile *tile);             // 17 (0xd12910)
    virtual uint16_t GetPatternId(const WorldTile *tile);                       // 18 (0xd0e130)
    virtual uint16_t GetPatternIdB(const WorldTile *tile);                      // 19 (0xd0d9c0)

    // ---- cIGZSerializable (+0x1c) ----
    bool Write(ifaces::cIGZOStream *stream) override; // 0xd143e0
    bool Read(ifaces::cIGZIStream *stream) override;  // 0xd11ce0
    uint32_t GetGZCLSID() override;                   // 0xd0e2c0

    // ---- cIWorldDB (+0x1c) ----
    void SetBounds(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY) override;
    void SetLevels(int32_t minLevel, int32_t maxLevel) override;
    bool Save(void *writer, bool skipObjects) override;
    uint32_t Load(uint32_t a, uint32_t b) override;
    bool Reset() override;
    uint32_t Create(uint32_t a, uint32_t b, uint32_t c) override;
    void Slot12(uint32_t a) override;
    void SetFloorPattern(const WorldName *name, const WorldTile *tile) override;
    void SetFloorRotation(const uint16_t *rotation, const WorldTile *tile) override;
    void SetFloorPatternB(const WorldName *name, const WorldTile *tile) override;
    void SetFloorRotationB(const uint16_t *rotation, const WorldTile *tile) override;
    void SetFloorPatternQuiet(const WorldName *name, const WorldTile *tile) override;
    void SetFloorElevation(float elevation, const WorldTile *tile) override;
    void SetFloorElevationQuiet(float elevation, const WorldTile *tile) override;
    void SetFloorVisibility(uint8_t visible, const WorldTile *tile) override;
    void AddObjectRects(int32_t id, const std::vector<WorldRect> *rects, int32_t level) override;
    void AddObjectAt(int32_t id, const WorldPoint *point) override;
    void RemoveObject(int32_t id) override;
    bool GetObjectCells(int32_t id, WorldCellList *cells) override;
    void RemoveObjectAt(int32_t id, const WorldPoint *point) override;
    // 0xd0d3d0 / 0xd120a0: the object index's Add / Remove, the arguments swapped. The original leaves the callee's eax
    // (a tree node or head pointer, no value) as its result; ours returns nothing.
    void SetObjectVertex(int32_t id, const LayerVertex *vertex) override;
    void RemoveObjectVertex(int32_t id, const LayerVertex *vertex) override;
    void SetByteTile(uint8_t value, const WorldTile *tile) override;
    void SetDeckFoundation(const uint32_t *value, const WorldTile *tile) override;
    void SetAwning(const uint32_t *value, const WorldTile *tile) override;
    void SetAwningPair(const AwningTile *value, const WorldTile *tile) override;
    void SetMinGrade(int32_t value) override;
    void SetName(cIGZString *name) override;
    bool PostsMessages() override;
    void *GetTerrain() override;
    void *GetRoofs() override;
    void *GetPools() override;
    void *GetCurvedPools() override;
    void *GetPartitions() override;
    WorldName GetEmptyPatternName() override;
    WorldName GetDefaultPatternName() override;
    void *StoreVTable(void *target) override;
    WorldName GetFloorPatternName(const WorldTile *tile) override;
    uint32_t GetFloorRotation(const WorldTile *tile) override;
    WorldName GetFloorPatternNameB(const WorldTile *tile) override;
    uint32_t GetFloorRotationB(const WorldTile *tile) override;
    void GetPatternNames(PatternNameSet *names) override;
    bool HasPattern(const WorldName *name) override;
    bool GetFloorVisibility(const WorldTile *tile, uint8_t *visible) override;
    bool HasFloor(const WorldTile *tile, int8_t mask, bool checkVisible) override;
    bool HasAnyFloor(const WorldTile *tile, int8_t mask, bool checkVisible) override;
    bool HasFloorOrGround(const WorldTile *tile, int8_t mask, bool checkVisible) override;
    bool HasAnyFloorOrGround(const WorldTile *tile, int8_t mask, bool checkVisible) override;
    bool HasFloorAbove(const WorldTile *tile, int8_t mask, bool checkVisible) override;
    bool HasAnyFloorAbove(const WorldTile *tile, int8_t mask, bool checkVisible) override;
    bool TerrainSlot13(const WorldTile *tile) override;
    bool GetByteTile(const WorldTile *tile) override;
    uint32_t GetDeckFoundation(const WorldTile *tile) override;
    uint32_t GetAwning(const WorldTile *tile) override;
    WorldPair GetAwningPair(const WorldTile *tile) override;
    bool IsFloorEmpty(const WorldTile *tile, int8_t mask) override;
    float GetElevation(const WorldTile *tile) override;
    float GetTerrainHeight(int32_t x, int32_t y) override;
    float GetGroundElevation(int32_t x, int32_t y) override;
    int32_t CountObjects(const WorldTile *tile) override;
    int32_t FindObject(int32_t id, const WorldTile *tile, bool footprints) override;
    int32_t GetObjectAt(int32_t index, const WorldTile *tile, bool footprints) override;
    int32_t ObjectAtVertex(const LayerVertex *vertex) override; // 0xd11120: the id at the vertex, -1 for none
    bool IsInWorldPoint(const WorldPoint *point) override;
    bool IsInsideCorners(const WorldTile *tile) override;
    bool IsInside(const WorldTile *tile) override;
    bool IsInWorld(float x, float y) override;
    bool IsInterior(const WorldTile *tile) override;
    bool IsInteriorPoint(float x, float y) override;
    float GetMinXF() override;
    float GetMinYF() override;
    float GetMaxXF() override;
    float GetMaxYF() override;
    float GetWidthF() override;
    float GetHeightF() override;
    int32_t GetMinX() override;
    int32_t GetMinY() override;
    int32_t GetMaxX() override;
    int32_t GetMaxY() override;
    int32_t GetWidth() override;
    int32_t GetHeight() override;
    float GetTileSize() override;
    int32_t GetMinLevel() override;
    int32_t GetMaxLevel() override;
    int32_t GetLevelCount() override;
    float GetLevelTop() override;
    float GetLevelBottom() override;
    float GetMaxGradeHeight() override;
    float GetLevelHeight() override;
    float GetWallHeight() override;
    float GetFoundationHeight() override;
    uint8_t UnevenTilesFloorable() override;
    int32_t GetMinGrade() override;
    bool GetName(cIGZString *out) override;
    void *GetOutsideSupport() override;

    // ---- cIGZMessageTarget2 (+0x20) ----
    bool DoMessage(void *message) override; // 0xd0db70

    // ---- non-virtual members (`this` the primary face) ----
    void SetPatternByNameImpl(const WorldName *name, const WorldTile *tile);  // 0xd12df0
    void SetPatternByNameImplB(const WorldName *name, const WorldTile *tile); // 0xd127b0
    void SetElevationImpl(float elevation, const WorldTile *tile);           // 0xd12bd0
    void SetByteTileImpl(uint8_t value, const WorldTile *tile);              // 0xd13290
    void AddObjectRectsImpl(int32_t id, const std::vector<WorldRect> *rects, int32_t level); // 0xd0d400
    void AddObjectAtImpl(int32_t id, const WorldPoint *point);                // 0xd0d330
    WorldName PatternName(const uint32_t *id);                                // 0xd0df10: the pattern table's slot 8
    uint16_t PatternId(const WorldName *name);                                // 0xd0e1d0: its slot 7, 0 for 0xffff
    void UsePatternName(const WorldName *oldName, const WorldName *newName, uint8_t segments); // 0xd138d0
    bool FloorSegments(const WorldTile *tile, int8_t mask, bool want, bool checkVisible);   // 0xd0e4f0
    void SetLevelsImpl(int32_t minLevel, int32_t maxLevel);                   // 0xd13230
    void ResizeGrids();                                                       // 0xd11e80
    void PostTerrainElevation(float elevation, const WorldTile *tile);        // 0xd11150

    cIWorldDB *World() { return this; }
    cIGZMessageTarget2 *MessageTarget() { return this; }

    // +0x00 .. +0x1b: cRZSystemService; +0x1c, +0x20: the faces' vtables
    int32_t mMinX = 0, mMinY = 0, mMinLevel = 0;   // 0x24 the origin (0xd13780 subtracts it)
    int32_t mMaxX = -1, mMaxY = -1, mMaxLevel = 0; // 0x30
    int32_t mMinGrade = 4;                         // 0x3c (slot 32 keeps it at least 4)
    uint32_t mMaxTerrainGrade = 5;                 // 0x40 the setting "maxTerrainGrade" (DoMessage 0x2c02eff6)
    float mLevelHeight = 0.1875f;                  // 0x44
    uint8_t mUnevenTilesFloorable = 1;             // 0x48 the setting "unevenTilesFloorable" (DoMessage 0x0c02efce)
    std::set<uint32_t> mMessageIds;                // 0x4c the messages it takes ({0xad})
    bool mInitialized = false;                     // 0x54 (Init sets it, Shutdown clears it)
    bool mPostMessages = false;                    // 0x55 (slot 34; Reset clears it)
    WorldString mName;                             // 0x58 (slots 33 / 99; written when mHasName)
    bool mHasName = false;                         // 0x78
    WorldRef<void> mTerrain;                       // 0x7c framework class 0x4b58975b (cWorldTerrain)
    WorldRef<void> mRoofs;                         // 0x80 class 0xab9406aa (cWorldRoof)
    WorldRef<void> mPools;                         // 0x84 class 0x0c900fdb (cWorldPool)
    WorldRef<void> mCurvedPools;                   // 0x88 class 0x916da14d (cWorldCurvedPool)
    WorldRef<void> mPartitions;                    // 0x8c class 0x6ca2a642 (cPartitionManager)
    WorldRef<void> mOutsideSupport;                // 0x90 class 0xb21f9b20 (cWorldOutsideSupport)
    WorldRef<PatternGrid> mPatterns;               // 0x94
    WorldRef<RotationGrid> mRotations;             // 0x98
    WorldRef<PatternGrid> mPatternsB;              // 0x9c
    WorldRef<RotationGrid> mRotationsB;            // 0xa0
    WorldRef<FloatGrid> mElevations;               // 0xa4
    WorldRef<VisibilityGrid> mVisibility;          // 0xa8
    WorldRef<ByteGrid> mBytes;                     // 0xac
    WorldRef<ObjectGrid> mObjects;                 // 0xb0
    bool mReposted = false;                        // 0xb4 (DoMessage 0xad re-posts the message once)
    std::map<int32_t, ObjectCellSet> mObjectCells; // 0xb8 object id -> its cells
    WorldRef<PatternTable> mPatternTable;          // 0xc0
    WorldRef<cVertexLayer> mObjectIndex;           // 0xc4 object id <-> vertex (world/cVertexLayer.h)
    WorldRef<DeckGrid> mDeckFoundations;           // 0xc8
    WorldRef<AwningGrid> mAwnings;                 // 0xcc
    WorldRef<void> mProxies;                       // 0xd0 framework class 0x8ce45d10 (cProxyManager), only during Save
};
static_assert(sizeof(cWorldDB) == cWorldDB::kSize, "cWorldDB size");
static_assert(offsetof(cWorldDB, mMinX) == 0x24 && offsetof(cWorldDB, mMaxLevel) == 0x38, "bounds");
static_assert(offsetof(cWorldDB, mMinGrade) == 0x3c && offsetof(cWorldDB, mUnevenTilesFloorable) == 0x48, "settings");
static_assert(offsetof(cWorldDB, mMessageIds) == 0x4c && offsetof(cWorldDB, mPostMessages) == 0x55, "flags");
static_assert(offsetof(cWorldDB, mName) == 0x58 && offsetof(cWorldDB, mHasName) == 0x78, "name");
static_assert(offsetof(cWorldDB, mTerrain) == 0x7c && offsetof(cWorldDB, mOutsideSupport) == 0x90, "world objects");
static_assert(offsetof(cWorldDB, mPatterns) == 0x94 && offsetof(cWorldDB, mObjects) == 0xb0, "grids");
static_assert(offsetof(cWorldDB, mReposted) == 0xb4 && offsetof(cWorldDB, mObjectCells) == 0xb8, "object cells");
static_assert(offsetof(cWorldDB, mPatternTable) == 0xc0 && offsetof(cWorldDB, mProxies) == 0xd0, "tail");

// The hook's entry point: the constructor (placement new of ours).
struct cWorldDBAdapters {
    static cWorldDB *__fastcall Construct(void *memory, void *edx); // 0xd0b6c0
    // The helpers Load (0xd0fb30) and Create (0xd0ef80), still game code, call directly on our objects (`this` the
    // primary face).
    static uint16_t __fastcall PatternId(cWorldDB *self, void *edx, const WorldName *name); // 0xd0e1d0
    static void __fastcall SetPatternByNameB(cWorldDB *self, void *edx, const WorldName *name,
                                             const WorldTile *tile); // 0xd127b0
    static void __fastcall SetLevels(cWorldDB *self, void *edx, int32_t minLevel, int32_t maxLevel); // 0xd13230
    static void __fastcall SetByteTile(cWorldDB *self, void *edx, uint8_t value, const WorldTile *tile); // 0xd13290
    static void __fastcall UsePatternName(cWorldDB *self, void *edx, const WorldName *oldName, const WorldName *newName,
                                          uint8_t segments); // 0xd138d0
};

} // namespace nTSWorld
