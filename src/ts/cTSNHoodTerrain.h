#pragma once
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "gz/cGZDBSegmentPackedFile.h"    // GZServiceAllocator
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "rz/RZUnknownBase.h"

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces
class cIGZString;

// cTSNHoodTerrain: the neighbourhood's height field and its derived maps; owns the NHOOD_TERRAIN record 0xabcb5da4
// (one per hood, ~83 KB: 129 x 129 float heights plus a 129 x 129 byte map). Structure pass: lane DD
// (notes/feedback/DD.md has the record format and the per-function handoff). RVAs 1.18.0.187, all /O2.
// RTTI bases: cRZUnknown (+0, vtable 0xdcc144, 7 slots), cITSNHoodTerrain + cIGZSerializable (+8, 0xdcc164, 44 slots),
// cIGZPersistResource (+0xc, 0xdcc218, 12 slots), cIGZMessageTarget2 (+0x10, 0xdcc24c, 4 slots). 0xc0 bytes.
// Standalone shape (docs/standalone-shape.md, lane FO): a real class on those bases, our vtables, every slot ours (the folded
// ones too). Creation: Create 0x4af120 (constructor inline; its address registered by the cTSSimCOMDirector constructor) is
// the only site, and the game's vtables are referenced only there and by the deleting destructor 0x4ad290. The RVA hooks on
// the old slot functions are adapters (cTSNHoodTerrainAdapters).
class cTSNHoodTerrain;

// The type-id lists the queries fill (slots 25, 26, 30): the caller's vectors, grown through the allocator service as the
// original's push_back (0x4ac0f0, allocating through 0x4abb30) does.
using TerrainTypeIdVector = std::vector<uint8_t, GZServiceAllocator<uint8_t>>;
// The height samples ImportHeights (slot 36) has an import source's heights object fill: freed through the allocator service.
using TerrainHeightVector = std::vector<float, GZServiceAllocator<float>>;

#include "ts/c2DArray.h"

// A node of the static terrain-type table 0x10dab00 (a hash map keyed by the one-byte type id, FNV-1a over that byte:
// cTSNHoodTerrain::FindType; its list head pointer at 0x10dab04 is a circular list whose sentinel is the head node). SetSize
// resizes the per-type weight map embedded at +0x6c; slots 29-33 read the id, the two names and the flag.
struct cTSNHoodTerrainTypeNode {
    cTSNHoodTerrainTypeNode *mNext;        // +0x00
    cTSNHoodTerrainTypeNode *mPrev;        // +0x04
    uint8_t mId;                           // +0x08 the key
    uint8_t mUnread09[7];                  // +0x09
    uint8_t mName[0x20];                   // +0x10 a cRZString (GetTypeName copies it out)
    uint8_t mFlag;                         // +0x30 GetTypeFlag
    uint8_t mUnread31[3];                  // +0x31
    uint8_t mAltName[0x20];                // +0x34 a cRZString (GetTypeAltName assigns it out)
    uint8_t mUnread54[0x18];               // +0x54
    nTSWorld::c2DArray<uint8_t> mWeightMap; // +0x6c per-vertex weight of this type (GetTypeWeightMap: its data)
};
static_assert(offsetof(cTSNHoodTerrainTypeNode, mName) == 0x10);
static_assert(offsetof(cTSNHoodTerrainTypeNode, mFlag) == 0x30);
static_assert(offsetof(cTSNHoodTerrainTypeNode, mAltName) == 0x34);
static_assert(offsetof(cTSNHoodTerrainTypeNode, mWeightMap) == 0x6c);

// One vertex's paint: up to three terrain-type layers (type 0 ends the list). Weights are normalised to sum 250 by
// RebuildDerivedMaps; GetVertexTypeWeight returns the low byte.
struct cTSNHoodTerrainPaintLayer {
    uint8_t mType;    // +0 terrain type id (key of the static type table)
    uint8_t mPad;     // +1
    uint16_t mWeight; // +2
    // New layers get type 0 and weight 0 and keep whatever the allocator left in the pad byte, as the original's paint
    // vector fill (0x4aca60, also used by its reallocation 0x4ac650) does.
    cTSNHoodTerrainPaintLayer() : mType(0), mWeight(0) {}
    cTSNHoodTerrainPaintLayer(uint8_t type, uint8_t pad, uint16_t weight) : mType(type), mPad(pad), mWeight(weight) {}
};
// User-provided so vector::resize default-initialises (runs the layer constructors) instead of zero-filling the pad bytes.
struct cTSNHoodTerrainVertexPaint {
    cTSNHoodTerrainPaintLayer mLayers[3];
    cTSNHoodTerrainVertexPaint() {}
};
static_assert(sizeof(cTSNHoodTerrainVertexPaint) == 12);

// The default constructor does nothing on purpose: the original's resize (0x3b58f0, used by SetSize) leaves new elements
// as the allocator handed them out (a user-provided empty constructor makes vector::resize default-initialise them).
struct cTSNHoodTerrainVec3 {
    float x, y, z;
    cTSNHoodTerrainVec3() {}
    cTSNHoodTerrainVec3(float ax, float ay, float az) : x(ax), y(ay), z(az) {}
};

// The +8 interface (a cIGZSerializable: slots 3-5 are its Write / Read / GetGZCLSID). Slot order and `ret N` per slot as the
// game's vtable 0xdcc164. Declared here until a second implementer appears.
class cITSNHoodTerrain : public cIGZSerializable {
public:
    virtual bool Initialize() = 0;                                  // slot 6, 0x4afc30 (ret 0): 1
    virtual bool Shutdown() = 0;                                    // slot 7, 0x4b2640 (ret 0)
    virtual void SetHeightAt(uint32_t x, uint32_t y, float height, bool notify) = 0; // slot 8, 0x4b2540 (ret 0x10)
    virtual void ResampleGrid(int32_t step) = 0;                    // slot 9, 0x4b1fb0 (ret 4)
    virtual float GetHeightAt(int32_t x, int32_t y) = 0;            // slot 10, 0x4afc10 (ret 8)
    virtual float GetInterpolatedHeight(float x, float z) = 0;      // slot 11, 0x4af9e0 (ret 8)
    virtual void GetHeightRange(const uint32_t *rect, float *minHeight, float *maxHeight) = 0; // slot 12, 0x4af650 (ret 0xc)
    virtual void WorldToVertex(float x, float z, int32_t *vx, int32_t *vz) = 0; // slot 13, 0x4ae780 (ret 0x10)
    virtual void GetVertexNormal(uint32_t x, uint32_t y, cTSNHoodTerrainVec3 *out) = 0; // slot 14, 0x4af700 (ret 0xc)
    virtual void SetWaterLevel(float level) = 0;                    // slot 15, folded 0x4b21f0 (ret 4)
    virtual float GetWaterLevel() = 0;                              // slot 16, folded 0x432e40
    virtual float GetWaterDepthAt(uint32_t x, uint32_t y) = 0;      // slot 17, 0x4b2d50 (ret 8)
    virtual void UpdateNormals(bool force) = 0;                     // slot 18, 0x4b29c0 (ret 4)
    virtual void RebuildDerivedMaps() = 0;                          // slot 19, 0x4b2a00 (ret 0)
    virtual uint8_t Save() = 0;                                     // slot 20, 0x4b1e40 (ret 0, EH): the AND of two bytes, not normalised
    virtual bool SetTerrainType(cIGZString *name) = 0;              // slot 21, 0x4b24b0 (ret 4)
    virtual bool GetTerrainTypeName(cIGZString *out) = 0;           // slot 22, 0x4afbf0 (ret 4)
    virtual void SetVertexFlag(const uint32_t *vertex, bool set, bool sendNow) = 0; // slot 23, 0x4b25a0 (ret 0xc)
    virtual bool GetVertexFlag(const uint32_t *vertex) = 0;         // slot 24, 0x4b00e0 (ret 4)
    virtual void GetVertexTerrainTypes(uint32_t x, uint32_t y, TerrainTypeIdVector *out) = 0; // slot 25, 0x4af870 (ret 0xc)
    virtual void GetRectTerrainTypes(const uint32_t *rect, TerrainTypeIdVector *out) = 0;     // slot 26, 0x4af8f0 (ret 8)
    virtual uint8_t GetVertexTypeWeight(uint32_t type, uint32_t x, uint32_t y) = 0; // slot 27, 0x4af7b0 (ret 0xc)
    virtual void GetRectTypeWeights(const uint32_t *rect, TerrainTypeIdVector *out, uint32_t count) = 0; // slot 28, 0x4af2e0 (ret 0xc, EH)
    virtual uint8_t *GetTypeWeightMap(uint8_t type) = 0;            // slot 29, 0x4af760 (ret 4), static table
    virtual void GetTerrainTypeIds(TerrainTypeIdVector *out) = 0;   // slot 30, 0x4af580 (ret 4), static table
    virtual bool GetTypeName(uint8_t type, cIGZString *out) = 0;    // slot 31, 0x4af810 (ret 8), static table
    virtual bool GetTypeAltName(uint8_t type, cIGZString *out) = 0; // slot 32, 0x4af5f0 (ret 8), static table
    virtual uint8_t GetTypeFlag(uint8_t type) = 0;                  // slot 33, 0x4b04a0 (ret 4), static table
    virtual uint8_t GetMaxTypeId() = 0;                             // slot 34, 0x4b0290 (6 bytes): byte 0x10daad5
    virtual void SlopeRect(const int32_t *rect, float angleDegrees, int32_t passes, float strength, uint32_t lockLots) = 0; // slot 35, 0x4b2700 (ret 0x14, EH)
    virtual void ImportHeights(void *source) = 0;                   // slot 36, 0x4b02a0 (ret 4, EH, magic static)
    virtual bool IsVertexUnderWater(uint32_t x, uint32_t y) = 0;    // slot 37, 0x4b0110 (ret 8)
    virtual bool IsCellUnderWater(uint32_t x, uint32_t y) = 0;      // slot 38, 0x4b0030 (ret 8)
    virtual bool IsCellAboveWater(uint32_t x, uint32_t y) = 0;      // slot 39, 0x4affc0 (ret 8)
    virtual cIGZPersistResource *AsPersistResource() = 0;           // slot 40, folded 0x1f9910
    virtual void RaiseRect(const uint32_t *rect, float delta) = 0;  // slot 41, 0x4b0620 (ret 8, EH)
    virtual void SetRectHeight(const uint32_t *rect, float height) = 0; // slot 42, 0x4b0150 (ret 8, EH)
    virtual bool IsInWorld(float x, float z) = 0;                   // slot 43, 0x4b00a0 (ret 8)
};

class cTSNHoodTerrain : public rz::cRZUnknown,
                        public cITSNHoodTerrain,
                        public cIGZPersistResource,
                        public cIGZMessageTarget2 {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them any more).
    static constexpr uint32_t kVTable = 0xdcc144;
    static constexpr uint32_t kTerrainVTable = 0xdcc164;
    static constexpr uint32_t kPersistVTable = 0xdcc218;
    static constexpr uint32_t kMessageVTable = 0xdcc24c;
    static constexpr uint32_t kClassId = 0xabcb5da4;       // GetGZCLSID, the record type NHOOD_TERRAIN
    static constexpr uint32_t kTerrainIid = 0xabcb5d7a;    // QI -> +8 (as is 0xe4fda3d4 cIGZSerializable)
    static constexpr uint32_t kPersistResourceIid = 0x456b8f1d; // QI -> +0xc
    static constexpr uint32_t kRecordVersion = 4;          // Write
    static constexpr uint32_t kArrayClassId = 0x6b943b43;  // c2DArray GetGZCLSID
    // Messages: listened to (+0xa8): 0xaf, 0xb1 (lot placed/changed: flatten under it). Sent: 0x6c09445c height
    // changed (x, y), 0x2bfb06fa / 0x0bfb0707 vertex flag set / cleared (x, y, layer), 0xac3a02d9 terrain changed,
    // 0x0fb96150 after a rect edit.

    // The constructor of 0x4af120 (inline there): the vtables, 20.0 water, empty vectors, null arrays and type, the
    // message-id set {0xaf, 0xb1}, a zero key. 0x14-0x33, 0x38-0x4f and 0x69-0x6b are never stored.
    cTSNHoodTerrain();
    // Primary slot 3, 0x4ad290 (ret 4, EH): the id set, the two arrays released (pointers kept), the vectors freed and left
    // empty, cRZUnknown's vtable; bit 0 frees the 0xc0 bytes. Out of line (a defaulted one can drop the vtable stores).
    ~cTSNHoodTerrain() override;

    // 0x4af120 (cdecl, EH): the factory the COM director registers; new 0xc0, the constructor, the +8 face.
    static cITSNHoodTerrain *__cdecl Create();

    // cIGZUnknown (every face; the secondary faces' entries are the compiler's adjustor thunks)
    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0x4b05b0 (ret 8); thunks 0x4b0602 / 0x4b0612 / 0x4b060a
    uint32_t AddRef() override;                              // slot 1, folded 0x146a50: cRZUnknown's
    uint32_t Release() override;                             // slot 2, folded 0x1f9aa0: cRZUnknown's
    // primary slots 4 / 5 stay cRZUnknown's (0x1b9538 / 0x147a30).
    virtual void InvalidateRect(const uint32_t *rect);       // primary slot 6, 0x4b2c00 (ret 4, EH): flag messages per vertex

    // cIGZSerializable (+8; these run with this = the face)
    bool Write(ifaces::cIGZOStream *stream) override;        // slot 3, 0x4b2fb0 (ret 4)
    bool Read(ifaces::cIGZIStream *stream) override;         // slot 4, 0x4b0980 (ret 4, EH: local cRZString)
    uint32_t GetGZCLSID() override;                          // slot 5, 0x4af2d0: 0xabcb5da4
    // cITSNHoodTerrain
    bool Initialize() override;
    bool Shutdown() override;
    void SetHeightAt(uint32_t x, uint32_t y, float height, bool notify) override;
    void ResampleGrid(int32_t step) override;
    float GetHeightAt(int32_t x, int32_t y) override;
    float GetInterpolatedHeight(float x, float z) override;
    void GetHeightRange(const uint32_t *rect, float *minHeight, float *maxHeight) override;
    void WorldToVertex(float x, float z, int32_t *vx, int32_t *vz) override;
    void GetVertexNormal(uint32_t x, uint32_t y, cTSNHoodTerrainVec3 *out) override;
    void SetWaterLevel(float level) override { mWaterLevel = level; } // folded 0x4b21f0: the face's +0x2c
    float GetWaterLevel() override { return mWaterLevel; }           // folded 0x432e40
    float GetWaterDepthAt(uint32_t x, uint32_t y) override;
    void UpdateNormals(bool force) override;
    void RebuildDerivedMaps() override;
    uint8_t Save() override;
    bool SetTerrainType(cIGZString *name) override;
    bool GetTerrainTypeName(cIGZString *out) override;
    void SetVertexFlag(const uint32_t *vertex, bool set, bool sendNow) override;
    bool GetVertexFlag(const uint32_t *vertex) override;
    void GetVertexTerrainTypes(uint32_t x, uint32_t y, TerrainTypeIdVector *out) override;
    void GetRectTerrainTypes(const uint32_t *rect, TerrainTypeIdVector *out) override;
    uint8_t GetVertexTypeWeight(uint32_t type, uint32_t x, uint32_t y) override;
    void GetRectTypeWeights(const uint32_t *rect, TerrainTypeIdVector *out, uint32_t count) override;
    uint8_t *GetTypeWeightMap(uint8_t type) override;
    void GetTerrainTypeIds(TerrainTypeIdVector *out) override;
    bool GetTypeName(uint8_t type, cIGZString *out) override;
    bool GetTypeAltName(uint8_t type, cIGZString *out) override;
    uint8_t GetTypeFlag(uint8_t type) override;
    uint8_t GetMaxTypeId() override;
    void SlopeRect(const int32_t *rect, float angleDegrees, int32_t passes, float strength, uint32_t lockLots) override;
    void ImportHeights(void *source) override;
    bool IsVertexUnderWater(uint32_t x, uint32_t y) override;
    bool IsCellUnderWater(uint32_t x, uint32_t y) override;
    bool IsCellAboveWater(uint32_t x, uint32_t y) override;
    // folded 0x1f9910 (`lea eax, [ecx + 4]`, null for a null object): the +0xc face.
    cIGZPersistResource *AsPersistResource() override { return static_cast<cIGZPersistResource *>(this); }
    void RaiseRect(const uint32_t *rect, float delta) override;
    void SetRectHeight(const uint32_t *rect, float height) override;
    bool IsInWorld(float x, float z) override;

    // cIGZPersistResource (+0xc; these run with this = the face): the key at +0xb0. Slots 5-7, 9, 10 and 11 are folded bodies.
    cGZPersistResourceKey GetKey() override;                                         // slot 3, 0x4af5d0
    bool SetKey(const cGZPersistResourceKey &key) override;                          // slot 4, 0x4b21d0: 1
    uint32_t GetType() override { return mKey[0]; }                                  // slot 5, 0x155e40
    uint32_t GetGroup() override { return mKey[1]; }                                 // slot 6, 0x254fd0
    uint32_t GetInstance() override { return mKey[2]; }                              // slot 7, 0x24ad60
    bool SetType(uint32_t type) override;                                            // slot 8, 0x4b2630: 1
    bool SetGroup(uint32_t group) override { mKey[1] = group; return true; }          // slot 9, 0x4a7210
    bool SetInstance(uint32_t instance) override { mKey[2] = instance; return true; } // slot 10, 0x4a7170
    uint32_t GetRefCount() override { return mRefCount; }                            // slot 11, the thunk 0x4af9d0 -> 0x147a30

    // cIGZMessageTarget2 (+0x10)
    bool DoMessage(void *message) override; // slot 3, 0x4aecc0 (ret 4, EH): QI 0x65297976; types 0xaf / 0xb1 -> FlattenUnderLot(data1)

    // Non-virtual members (__thiscall on the object unless noted).
    void SetSize(uint32_t width, uint32_t height); // 0x4b2200 (ret 8)
    void ResetHeights();                     // 0x4afd90: create mHeights, Resize(vertsX, vertsY), fill 45.0f
    void ResetVertexFlags();                 // 0x4aff00: create mVertexFlags, Resize, zero
    void RegisterCheatAndMessages();         // 0x4afcd0 (EH): cTerrainTypeChangeCheat to the cheat service, AddNotification
    void RecomputeNormals(const uint32_t *rect); // 0x4ade70 (ret 4, 1.4 KB; original)
    void ComputeLotDistance();               // 0x4ae410 (tentative; original)
    void ComputePaintWeights();              // 0x4ae7f0 (1.2 KB, tentative; original)
    void FlattenUnderLot(int32_t lotId);     // 0x4aee30 (ret 4, EH; original)
    static cTSNHoodTerrainTypeNode *FindType(uint8_t type); // the static table's find (inline in slots 19, 29, 31-33)
    static void LoadTerrainTypes();          // 0x4b0c00 (cdecl, 4.3 KB, EH; original): ini file I/O, once (byte 0x10daad4)
    static void __stdcall PostTerrainMessage(uint32_t type, uint32_t a, uint32_t b, uint32_t c); // 0x4b04f0 (ret 0x10)
    static void __stdcall SendTerrainMessage(uint32_t type, uint32_t a, uint32_t b, uint32_t c); // 0x4b1ef0 (ret 0x10)

    // +0x00 cRZUnknown (vtable, mRefCount at +4); +0x08, +0x0c, +0x10 the three interface vtables.
    uint32_t mWidth;                      // +0x14 cells (128); not set by the constructor
    uint32_t mHeight;                     // +0x18
    uint32_t mVertsX;                     // +0x1c width + 1
    uint32_t mVertsY;                     // +0x20 height + 1
    float mCellSize;                      // +0x24 neighbourhood slot 50 (10.0)
    float mInvCellSize;                   // +0x28 1 / cell size
    float mWorldSizeX;                    // +0x2c (float)width * cell size
    float mWorldSizeY;                    // +0x30
    float mWaterLevel;                    // +0x34 constructor 20.0; saved (312.5 in the shipped hoods)
    uint32_t mVertsX2;                    // +0x38 width + 1 again (SetSize); no reader found
    uint32_t mVertsY2;                    // +0x3c
    uint8_t mUnused40[0x10];              // +0x40 never touched
    std::vector<cTSNHoodTerrainVertexPaint> mPaint;  // +0x50 per vertex
    std::vector<cTSNHoodTerrainVec3> mNormals;       // +0x5c per vertex
    uint8_t mNormalsValid;                // +0x68
    std::vector<cTSNHoodTerrainVec3> mFaceRowA;      // +0x6c 2 * width face normals (RecomputeNormals' row buffers)
    std::vector<cTSNHoodTerrainVec3> mFaceRowB;      // +0x78
    std::vector<std::vector<uint8_t>> mShoreRows;    // +0x84 vertsY rows of vertsX bytes (distance from land)
    const void *mTerrainType;             // +0x90 entry of the static type table (0x828 bytes; GZStr name first)
    std::vector<std::vector<uint8_t>> mLotRows;      // +0x94 vertsY rows of vertsX bytes (distance from lots)
    nTSWorld::c2DArray<float> *mHeights;      // +0xa0 counted
    nTSWorld::c2DArray<uint8_t> *mVertexFlags; // +0xa4 counted; bits 1/2 per vertex (SetVertexFlag layer 0/1)
    // {0xaf, 0xb1} (inserted by the constructor). In a union: the destructor destroys it first, before the arrays and
    // vectors, as the original does.
    union {
        std::set<uint32_t> mMessageIds;   // +0xa8
    };
    uint32_t mKey[4];                     // +0xb0 type, group, instance, resource
};

static_assert(sizeof(cTSNHoodTerrain) == 0xc0, "new 0xc0 in 0x4af120, delete 0xc0 in 0x4ad290");
static_assert(offsetof(cTSNHoodTerrain, mWidth) == 0x14);
static_assert(offsetof(cTSNHoodTerrain, mWaterLevel) == 0x34);
static_assert(offsetof(cTSNHoodTerrain, mPaint) == 0x50);
static_assert(offsetof(cTSNHoodTerrain, mNormalsValid) == 0x68);
static_assert(offsetof(cTSNHoodTerrain, mShoreRows) == 0x84);
static_assert(offsetof(cTSNHoodTerrain, mTerrainType) == 0x90);
static_assert(offsetof(cTSNHoodTerrain, mHeights) == 0xa0);
static_assert(offsetof(cTSNHoodTerrain, mMessageIds) == 0xa8);
static_assert(offsetof(cTSNHoodTerrain, mKey) == 0xb0);

namespace nhoodterrain {
// The faces' offsets (base offsets are not constant expressions: the tests check these).
constexpr uint32_t kTerrainFace = 8;
constexpr uint32_t kPersistFace = 0xc;
constexpr uint32_t kMessageFace = 0x10;
} // namespace nhoodterrain

// The RVA hooks' entry points (src/hook/tables/cTSNHoodTerrain.cpp): every terrain the game creates carries our vtables (the
// Create hook), so these run only where the game calls an old entry point directly or holds a terrain built with its own
// vtables. Original calling convention (this in ecx = the face of the slot), our functions called without virtual dispatch,
// bodies inlined.
struct cTSNHoodTerrainAdapters {
    using Self = cTSNHoodTerrain;
    using Terrain = cITSNHoodTerrain;
    // primary face
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static void __fastcall InvalidateRect(Self *self, void *, const uint32_t *rect);
    // non-virtual members (this = the object): a member pointer of a multiply-inherited class is not a plain address
    static void __fastcall SetSize(Self *self, void *, uint32_t width, uint32_t height);
    static void __fastcall ResetHeights(Self *self, void *);
    static void __fastcall ResetVertexFlags(Self *self, void *);
    static void __fastcall RegisterCheatAndMessages(Self *self, void *);
    // the face thunks (src/ts/cTSNHoodTerrain.size.cpp)
    static bool __fastcall TerrainQueryInterface(Terrain *face, void *, uint32_t riid, void **out);
    static bool __fastcall PersistQueryInterface(cIGZPersistResource *face, void *, uint32_t riid, void **out);
    static bool __fastcall MessageQueryInterface(cIGZMessageTarget2 *face, void *, uint32_t riid, void **out);
    // +8 face
    static bool __fastcall Write(Terrain *face, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(Terrain *face, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(Terrain *face, void *);
    static bool __fastcall Initialize(Terrain *face, void *);
    static bool __fastcall Shutdown(Terrain *face, void *);
    static void __fastcall SetHeightAt(Terrain *face, void *, uint32_t x, uint32_t y, float height, bool notify);
    static void __fastcall ResampleGrid(Terrain *face, void *, int32_t step);
    static float __fastcall GetHeightAt(Terrain *face, void *, int32_t x, int32_t y);
    static float __fastcall GetInterpolatedHeight(Terrain *face, void *, float x, float z);
    static void __fastcall GetHeightRange(Terrain *face, void *, const uint32_t *rect, float *minHeight, float *maxHeight);
    static void __fastcall WorldToVertex(Terrain *face, void *, float x, float z, int32_t *vx, int32_t *vz);
    static void __fastcall GetVertexNormal(Terrain *face, void *, uint32_t x, uint32_t y, cTSNHoodTerrainVec3 *out);
    static float __fastcall GetWaterDepthAt(Terrain *face, void *, uint32_t x, uint32_t y);
    static void __fastcall UpdateNormals(Terrain *face, void *, bool force);
    static void __fastcall RebuildDerivedMaps(Terrain *face, void *);
    static uint8_t __fastcall Save(Terrain *face, void *);
    static bool __fastcall SetTerrainType(Terrain *face, void *, cIGZString *name);
    static bool __fastcall GetTerrainTypeName(Terrain *face, void *, cIGZString *out);
    static void __fastcall SetVertexFlag(Terrain *face, void *, const uint32_t *vertex, bool set, bool sendNow);
    static bool __fastcall GetVertexFlag(Terrain *face, void *, const uint32_t *vertex);
    static void __fastcall GetVertexTerrainTypes(Terrain *face, void *, uint32_t x, uint32_t y, TerrainTypeIdVector *out);
    static void __fastcall GetRectTerrainTypes(Terrain *face, void *, const uint32_t *rect, TerrainTypeIdVector *out);
    static uint8_t __fastcall GetVertexTypeWeight(Terrain *face, void *, uint32_t type, uint32_t x, uint32_t y);
    static void __fastcall GetRectTypeWeights(Terrain *face, void *, const uint32_t *rect, TerrainTypeIdVector *out, uint32_t count);
    static uint8_t *__fastcall GetTypeWeightMap(Terrain *face, void *, uint8_t type);
    static void __fastcall GetTerrainTypeIds(Terrain *face, void *, TerrainTypeIdVector *out);
    static bool __fastcall GetTypeName(Terrain *face, void *, uint8_t type, cIGZString *out);
    static bool __fastcall GetTypeAltName(Terrain *face, void *, uint8_t type, cIGZString *out);
    static uint8_t __fastcall GetTypeFlag(Terrain *face, void *, uint8_t type);
    static uint8_t __fastcall GetMaxTypeId(Terrain *face, void *);
    static void __fastcall SlopeRect(Terrain *face, void *, const int32_t *rect, float angleDegrees, int32_t passes, float strength, uint32_t lockLots);
    static void __fastcall ImportHeights(Terrain *face, void *, void *source);
    static bool __fastcall IsVertexUnderWater(Terrain *face, void *, uint32_t x, uint32_t y);
    static bool __fastcall IsCellUnderWater(Terrain *face, void *, uint32_t x, uint32_t y);
    static bool __fastcall IsCellAboveWater(Terrain *face, void *, uint32_t x, uint32_t y);
    static void __fastcall RaiseRect(Terrain *face, void *, const uint32_t *rect, float delta);
    static void __fastcall SetRectHeight(Terrain *face, void *, const uint32_t *rect, float height);
    static bool __fastcall IsInWorld(Terrain *face, void *, float x, float z);
    // +0xc face
    static cGZPersistResourceKey *__fastcall GetKey(cIGZPersistResource *face, void *, cGZPersistResourceKey *out);
    static bool __fastcall SetKey(cIGZPersistResource *face, void *, const cGZPersistResourceKey &key);
    static bool __fastcall SetType(cIGZPersistResource *face, void *, uint32_t type);
    // +0x10 face
    static bool __fastcall DoMessage(cIGZMessageTarget2 *face, void *, void *message);
};
