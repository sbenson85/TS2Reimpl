#pragma once
#include "gz/cGZDBSegmentPackedFile.h"    // GZServiceAllocator
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "rz/RZUnknownBase.h"
#include "rz/cRZRandom.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector

#include <cstddef>
#include <cstdint>
#include <set>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

// cTSNHoodOccupantManager: the neighbourhood decorations (flora, roads, roads with a model, props: cTSNHoodOccupant
// subclasses) and a spatial grid over them; owns the NHOOD_DECORATIONS record 0xabd0dc63 (one per hood, 40-110 KB).
// Structure pass: lane DD (notes/feedback/DD.md: record format, per-function handoff); core and persistence: lane DI
// (notes/feedback/DI.md); removal, queries and roads (slots 7, 8, 10, 19-23) and the standalone shape: lane FY.
// RVAs 1.18.0.187, all /O2.
// RTTI bases: cRZUnknown (+0, vtable 0xdcbf28, 6 slots), cITSNHoodOccupantManager (+8, 0xdcbf44, 26 slots),
// cIGZSerializable (+0xc, 0xdcbfb0, 6), cIGZPersistResource (+0x10, 0xdcbfcc, 12: all shared key accessors on +0x98),
// cIGZMessageTarget2 (+0x14, 0xdcc000, 4). 0xd0 bytes.
// Standalone shape (docs/standalone-shape.md, lane FY): a real class on those bases, our vtables, every slot ours (the folded
// ones as overrides). Creation: Create 0x4a91f0 (registered by the cTSSimCOMDirector constructor) calls Construct 0x4a7ff0,
// the only caller; the game's vtables are referenced only by Construct and the deleting destructor 0x4a84a0. The RVA hooks on
// the old slot functions are adapters (cTSNHoodOccupantManagerAdapters).
// The four lists hold the occupants' type faces (counted): flora (QI 0x4bd0e45b), road (0xabd4c3f4), road with model
// (0x6bd4d6ce), prop (0x2c0c10c8); the grid cells hold the same objects (counted again).
//
// The occupants are opaque here: their classes (cTSNHoodFloraOccupant / Road / RoadWithModel / Prop) are other groups.
// The grid and AddOccupantInternal take the primary cITSNHoodOccupant (QueryInterface, GetTileBounds); the lists hold the
// type faces cITSNHoodFloraOccupant / RoadOccupant / RoadOccupantWithModel / PropOccupant (+0x28, +0x28, +0x98, +0x28:
// Read, Write, AsOccupant; the model road's AsRoadOccupant). GetOccupantLayerMask takes a road face (GetRoadType).

class cTSNHoodOccupantManager;

// One tile of a road group: a road's position (x, z) times 0.1, truncated (the tracer 0x4a8930 pushes them).
struct cTSNHoodRoadTile {
    int32_t x;
    int32_t y;
};
// The tiles' storage comes from the game's allocator service (0x42f340 / 0x197100), not operator new.
using cTSNHoodRoadTileVector = std::vector<cTSNHoodRoadTile, GZServiceAllocator<cTSNHoodRoadTile>>;

// One element of the road-group vector at +0xa8 (built by slot 22, read by slot 23). The declared copy operations leave it
// without a move constructor, so the group vector's reallocation copies every group (new storage, then the old freed), as the
// original's 0x4a7630 does with its copy constructor 0x4a7ea0.
struct cTSNHoodRoadGroup {
    cTSNHoodRoadTileVector mTiles;
    cTSNHoodRoadGroup() = default;
    cTSNHoodRoadGroup(const cTSNHoodRoadGroup &) = default;
    cTSNHoodRoadGroup &operator=(const cTSNHoodRoadGroup &) = default;
    ~cTSNHoodRoadGroup() = default;
};
static_assert(sizeof(cTSNHoodRoadGroup) == 12);
static_assert(!std::is_nothrow_move_constructible_v<cTSNHoodRoadGroup>, "reallocation must copy, as the original");

// The manager face (+8, game vtable 0xdcbf44, 26 slots).
class cITSNHoodOccupantManager : public cIGZUnknown {
public:
    virtual bool Initialize() = 0;                     // slot 3, 0x4a9ad0 (ret 0)
    virtual bool Shutdown() = 0;                       // slot 4, 0x4aad00 (ret 0, EH)
    virtual uint8_t Save() = 0;                        // slot 5, 0x4aac50 (ret 0, EH): the AND of two bytes
    virtual bool AddOccupant(void *occupant) = 0;      // slot 6, 0x4a9d20 (ret 4, EH)
    virtual void RemoveOccupant(void *occupant) = 0;   // slot 7, 0x4aa830 (ret 4, EH, 1 KB)
    // slot 8, 0x4aa690 (ret 4, EH): removes (slot 7) the flora (road type 0) or the props (types 1-4) whose occupant slot 12
    // answers this road face. The name is DD's tentative one; what it does is "remove the occupants linked to a road".
    virtual void FindLinkedOccupants(void *road) = 0;
    virtual void UpdateOccupantPosition(void *occupant) = 0; // slot 9, 0x4aa070 (ret 4): RemoveFromGrid, InsertIntoGrid
    // slot 10, 0x4a9450 (ret 0xc, EH): the grid occupants of the cells under `rect` (x0, y0, x1, y1 terrain units) whose slot 3
    // type byte meets `typeMask` and whose tile bounds overlap the rect (open bounds), each once, counted, into `out` (emptied
    // first); a null `out` answers at the first one.
    virtual bool FindOccupantsInRect(const int32_t *rect, uint8_t typeMask, RefPtrVector *out) = 0;
    virtual void GetFloraOccupants(RefPtrVector *out) = 0;     // slot 11, 0x4a92c0 (ret 4, EH)
    virtual void *CreateFloraOccupant() = 0;                   // slot 12, 0x4a8ef0 (EH): new 0x34, 0x558890, returns +0x28
    virtual void GetRoadOccupants(RefPtrVector *out) = 0;      // slot 13, 0x4a96f0
    virtual void *CreateRoadOccupant() = 0;                    // slot 14, 0x4a8fb0: new 0x98, 0x558990, +0x28
    virtual void GetModelRoadOccupants(RefPtrVector *out) = 0; // slot 15, 0x4a97a0
    virtual void *CreateModelRoadOccupant() = 0;               // slot 16, 0x4a9010: new 0xc4, 0x558a10, +0x98
    virtual void GetPropOccupants(RefPtrVector *out) = 0;      // slot 17, 0x4a9630
    virtual void *CreatePropOccupant() = 0;                    // slot 18, 0x4a8f50: new 0x34, 0x558910, +0x28
    // slot 19, 0x4aadb0 (ret 0, EH): inside a begin / end vertex-deferral message pair, every road is marked (slot 23) with
    // its slot 19, those whose slot 25 answers 0 are snapped to the terrain (0x4aaf80), then every mark is cleared.
    virtual void SnapAllRoads() = 0;
    // slot 20, 0x4ab400 (ret 4, EH): the same for the roads in `rect` grown by one and clamped to the terrain: the roads
    // in the rect itself are marked first.
    virtual void SnapRoadsInRect(const int32_t *rect) = 0;
    virtual void ResetGridOccupants() = 0; // slot 21, 0x4a85f0 (ret 0): every grid occupant's slot 10(0)
    // slot 22, 0x4a8b70 (ret 4, EH): the road groups rebuilt from the roads whose slot 25 answers below 4 (two passes, at most
    // `maxGroups`; a traced group under 20 tiles is dropped); returns the group count.
    virtual int32_t BuildRoadGroups(uint32_t maxGroups) = 0;
    virtual bool GetRoadGroup(uint32_t index, cTSNHoodRoadTileVector *out) = 0; // slot 23, 0x4a9260 (ret 8): emptied, then a copy
    virtual uint32_t GetOccupantLayerMask(void *occupant) = 0; // slot 24, 0x4a9430 (ret 4): occupant slot 7 != 0 ? 3 : 0
    virtual cIGZPersistResource *AsPersistResource() = 0;      // slot 25, folded 0x4735d0
};

class cTSNHoodOccupantManager : public rz::cRZUnknown,
                                public cITSNHoodOccupantManager,
                                public cIGZSerializable,
                                public cIGZPersistResource,
                                public cIGZMessageTarget2 {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them any more).
    static constexpr uint32_t kVTable = 0xdcbf28;
    static constexpr uint32_t kManagerVTable = 0xdcbf44;
    static constexpr uint32_t kSerialVTable = 0xdcbfb0;
    static constexpr uint32_t kPersistVTable = 0xdcbfcc;
    static constexpr uint32_t kMessageVTable = 0xdcc000;
    static constexpr uint32_t kClassId = 0xabd0dc63;     // GetGZCLSID, the record type NHOOD_DECORATIONS
    static constexpr uint32_t kManagerIid = 0x6bd0dc57;  // QI -> +8; 0xe4fda3d4 -> +0xc, 0x456b8f1d -> +0x10,
    static constexpr uint32_t kMessageTargetIid = 0x452294aa; // -> +0x14
    static constexpr uint32_t kSerializableIid = 0xe4fda3d4;
    static constexpr uint32_t kPersistResourceIid = 0x456b8f1d;
    static constexpr uint32_t kRecordVersion = 0xcb;     // Write; Read needs > 0x6c
    static constexpr uint32_t kOldestReadableVersion = 0x6d;
    static constexpr uint32_t kFloraIid = 0x4bd0e45b, kRoadIid = 0xabd4c3f4, kModelRoadIid = 0x6bd4d6ce,
                              kPropIid = 0x2c0c10c8;
    // Messages listened to (+0x90): 0x6c09445c terrain height changed (x = data1, y = data2), 0x0c2bd5b7 begin
    // deferring vertex updates, 0x8c2bd74c end (process the deferred vertices). Posted: 0x0d6429da occupant added.
    static constexpr uint32_t kMessageHeightChanged = 0x6c09445c;
    static constexpr uint32_t kMessageBeginDefer = 0x0c2bd5b7;
    static constexpr uint32_t kMessageEndDefer = 0x8c2bd74c;
    static constexpr uint32_t kMessageOccupantAdded = 0x0d6429da;

    // 0x4a7ff0 (ret 0, EH): the vtables, empty lists and containers, the id set {0x6c09445c, 0x0c2bd5b7, 0x8c2bd74c}, a zero
    // key, the generator seeded 0xffffffff. 0x4c-0x5f, 0xb4 and 0xc4-0xcf stay as the allocator left them.
    cTSNHoodOccupantManager();
    // Primary slot 3, 0x4a84a0 (ret 4, EH): an initialised manager stops listening and lets go of the four lists' occupants;
    // then the members (bit 0 frees the 0xd0 bytes). Out of line (FI's rule).
    ~cTSNHoodOccupantManager() override;
    // 0x4a91f0 (cdecl, EH): new 0xd0 (null tested), the constructor, the +8 face.
    static cITSNHoodOccupantManager *__cdecl Create();

    // cIGZUnknown (every face; the secondary faces' entries are the compiler's adjustor thunks)
    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0x4aa090 (ret 8); thunks 0x4aa0e7 / ff / ef / f7
    uint32_t AddRef() override;                              // slot 1, folded 0x146a50: cRZUnknown's
    uint32_t Release() override;                             // slot 2, folded 0x1f9aa0: cRZUnknown's
    // primary slots 4 / 5 stay cRZUnknown's (0x1b9538 / 0x147a30).

    // cITSNHoodOccupantManager (+8; these run with this = the face)
    bool Initialize() override;
    bool Shutdown() override;
    uint8_t Save() override;
    bool AddOccupant(void *occupant) override;
    void RemoveOccupant(void *occupant) override;
    void FindLinkedOccupants(void *road) override;
    void UpdateOccupantPosition(void *occupant) override;
    bool FindOccupantsInRect(const int32_t *rect, uint8_t typeMask, RefPtrVector *out) override;
    void GetFloraOccupants(RefPtrVector *out) override;
    void *CreateFloraOccupant() override;
    void GetRoadOccupants(RefPtrVector *out) override;
    void *CreateRoadOccupant() override;
    void GetModelRoadOccupants(RefPtrVector *out) override;
    void *CreateModelRoadOccupant() override;
    void GetPropOccupants(RefPtrVector *out) override;
    void *CreatePropOccupant() override;
    void SnapAllRoads() override;
    void SnapRoadsInRect(const int32_t *rect) override;
    void ResetGridOccupants() override;
    int32_t BuildRoadGroups(uint32_t maxGroups) override;
    bool GetRoadGroup(uint32_t index, cTSNHoodRoadTileVector *out) override;
    uint32_t GetOccupantLayerMask(void *occupant) override;
    cIGZPersistResource *AsPersistResource() override { return static_cast<cIGZPersistResource *>(this); } // folded 0x4735d0

    // cIGZSerializable (+0xc)
    bool Write(ifaces::cIGZOStream *stream) override; // slot 3, 0x4ab6f0 (ret 4)
    bool Read(ifaces::cIGZIStream *stream) override;  // slot 4, 0x4aa110 (ret 4, EH, 12 states)
    uint32_t GetGZCLSID() override;                   // slot 5, 0x4a9250: 0xabd0dc63

    // cIGZPersistResource (+0x10): every slot a folded accessor of the key at +0x98
    cGZPersistResourceKey GetKey() override;                                         // slot 3, folded 0x49e6c0
    bool SetKey(const cGZPersistResourceKey &key) override;                          // slot 4, folded 0x4a0da0: 1
    uint32_t GetType() override { return mKey[0]; }                                  // slot 5, 0x231570
    uint32_t GetGroup() override { return mKey[1]; }                                 // slot 6, 0x22fa70
    uint32_t GetInstance() override { return mKey[2]; }                              // slot 7, 0x24bda0
    bool SetType(uint32_t type) override { mKey[0] = type; return true; }             // slot 8, 0x4a0e20
    bool SetGroup(uint32_t group) override { mKey[1] = group; return true; }          // slot 9, 0x479ca0
    bool SetInstance(uint32_t instance) override { mKey[2] = instance; return true; } // slot 10, 0x4799c0
    uint32_t GetRefCount() override { return mRefCount; }                            // slot 11, the thunk 0x4a96e0 -> 0x147a30

    // cIGZMessageTarget2 (+0x14)
    bool DoMessage(void *message) override; // slot 3, 0x4a9070 (ret 4, EH): 0x0c2bd5b7 / 0x6c09445c / 0x8c2bd74c

    // Non-virtual members (__thiscall on the object).
    void InitGrid();                           // 0x4a9b10
    void InsertIntoGrid(void *occupant);       // 0x4a9c10 (ret 4, EH)
    void RemoveFromGrid(void *occupant);       // 0x4aa550 (ret 4, EH)
    bool AddOccupantInternal(void *occupant);  // 0x4a9e10 (ret 4, EH, 598 B): grid + typed list by QI
    // 0x4a9370 (ret 8): the occupant's bounds (4 ints: x0, y0, x1, y1 in world units) divided by 4 (truncating) and clamped
    // to the grid; the result is {x0, y0, x1, y1} cells.
    void BoundsToCellRect(const int32_t *bounds, int32_t *cellRect);
    // (0x4a9850, the occupants around a terrain vertex re-heighted, stays original: DoMessage calls it at its RVA.)

    // +0x00 cRZUnknown (vtable, mRefCount at +4); +0x08, +0x0c, +0x10, +0x14 the four interface vtables.
    uint8_t mInitialized;                         // +0x18 Initialize / Read set it; Shutdown and the destructor test it
    RefPtrVector mFlora{};                        // +0x1c
    RefPtrVector mRoads{};                        // +0x28
    RefPtrVector mModelRoads{};                   // +0x34
    RefPtrVector mProps{};                        // +0x40
    float mGridCellSize;                          // +0x4c neighbourhood slot 50 * 4 (40.0); never read here
    uint32_t mTerrainWidth;                       // +0x50 neighbourhood slot 52
    uint32_t mTerrainHeight;                      // +0x54 slot 53
    uint32_t mGridCellsX;                         // +0x58 width >> 2
    uint32_t mGridCellsY;                         // +0x5c height >> 2
    // The four containers sit in anonymous unions: the destructor destroys them itself, in the original's order (road groups,
    // id set, pending vertices, cells), before the four lists.
    union {
        std::vector<RefPtrVector> mGrid;          // +0x60 cells, row-major (cellsX * y + x), counted occupants
    };
    union {
        std::unordered_set<uint32_t> mPendingVertices; // +0x6c y << 16 | x, filled while deferring
    };
    uint8_t mDeferVertexUpdates;                  // +0x8c
    union {
        std::set<uint32_t> mMessageIds;           // +0x90 {0x6c09445c, 0x0c2bd5b7, 0x8c2bd74c}
    };
    uint32_t mKey[4];                             // +0x98 type, group, instance, high
    union {
        std::vector<cTSNHoodRoadGroup> mRoadGroups; // +0xa8 built by slot 22, read by slot 23
    };
    uint32_t mUnknownB4;                          // +0xb4 never touched
    cRZRandom mRandom;                            // +0xb8 seeded 0xffffffff
    uint8_t mUnknownC4[0xc];                      // +0xc4 never touched
};

static_assert(sizeof(cTSNHoodOccupantManager) == 0xd0, "new 0xd0 in 0x4a91f0, delete 0xd0 in 0x4a84a0");
static_assert(offsetof(cTSNHoodOccupantManager, mInitialized) == 0x18);
static_assert(offsetof(cTSNHoodOccupantManager, mFlora) == 0x1c);
static_assert(offsetof(cTSNHoodOccupantManager, mProps) == 0x40);
static_assert(offsetof(cTSNHoodOccupantManager, mGrid) == 0x60);
static_assert(offsetof(cTSNHoodOccupantManager, mPendingVertices) == 0x6c);
static_assert(offsetof(cTSNHoodOccupantManager, mDeferVertexUpdates) == 0x8c);
static_assert(offsetof(cTSNHoodOccupantManager, mMessageIds) == 0x90);
static_assert(offsetof(cTSNHoodOccupantManager, mKey) == 0x98);
static_assert(offsetof(cTSNHoodOccupantManager, mRoadGroups) == 0xa8);
static_assert(offsetof(cTSNHoodOccupantManager, mRandom) == 0xb8);
static_assert(sizeof(std::unordered_set<uint32_t>) == 0x20);

namespace nhoodocc {
// The faces' offsets (base offsets are not constant expressions: the tests check these).
constexpr uint32_t kManagerFace = 8;
constexpr uint32_t kSerialFace = 0xc;
constexpr uint32_t kPersistFace = 0x10;
constexpr uint32_t kMessageFace = 0x14;
} // namespace nhoodocc

// The RVA hooks' entry points (src/hook/tables/cTSNHoodOccupantManager.cpp): every manager the game creates carries our
// vtables (the Construct and Create hooks), so these run only where the game calls an old entry point directly or holds a
// manager built with its own vtables. Original calling convention (this in ecx = the face of the slot), our functions called
// without virtual dispatch, bodies inlined.
struct cTSNHoodOccupantManagerAdapters {
    using Self = cTSNHoodOccupantManager;
    // construction and the primary face
    static Self *__fastcall Construct(Self *memory, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    // non-virtual members (this = the object): a member pointer of a multiply-inherited class is not a plain address
    static void __fastcall InitGrid(Self *self, void *);
    static void __fastcall InsertIntoGrid(Self *self, void *, void *occupant);
    static void __fastcall RemoveFromGrid(Self *self, void *, void *occupant);
    static bool __fastcall AddOccupantInternal(Self *self, void *, void *occupant);
    static void __fastcall BoundsToCellRect(Self *self, void *, const int32_t *bounds, int32_t *cellRect);
    // the face thunks (src/ts/cTSNHoodOccupantManager.size.cpp)
    static bool __fastcall ManagerQueryInterface(cITSNHoodOccupantManager *face, void *, uint32_t riid, void **out);
    static bool __fastcall SerialQueryInterface(cIGZSerializable *face, void *, uint32_t riid, void **out);
    static bool __fastcall PersistQueryInterface(cIGZPersistResource *face, void *, uint32_t riid, void **out);
    static bool __fastcall MessageQueryInterface(cIGZMessageTarget2 *face, void *, uint32_t riid, void **out);
    // +8 face
    static bool __fastcall Initialize(cITSNHoodOccupantManager *face, void *);
    static bool __fastcall Shutdown(cITSNHoodOccupantManager *face, void *);
    static uint8_t __fastcall Save(cITSNHoodOccupantManager *face, void *);
    static bool __fastcall AddOccupant(cITSNHoodOccupantManager *face, void *, void *occupant);
    static void __fastcall RemoveOccupant(cITSNHoodOccupantManager *face, void *, void *occupant);
    static void __fastcall FindLinkedOccupants(cITSNHoodOccupantManager *face, void *, void *road);
    static void __fastcall UpdateOccupantPosition(cITSNHoodOccupantManager *face, void *, void *occupant);
    static bool __fastcall FindOccupantsInRect(cITSNHoodOccupantManager *face, void *, const int32_t *rect, uint8_t typeMask, RefPtrVector *out);
    static void __fastcall GetFloraOccupants(cITSNHoodOccupantManager *face, void *, RefPtrVector *out);
    static void *__fastcall CreateFloraOccupant(cITSNHoodOccupantManager *face, void *);
    static void __fastcall GetRoadOccupants(cITSNHoodOccupantManager *face, void *, RefPtrVector *out);
    static void *__fastcall CreateRoadOccupant(cITSNHoodOccupantManager *face, void *);
    static void __fastcall GetModelRoadOccupants(cITSNHoodOccupantManager *face, void *, RefPtrVector *out);
    static void *__fastcall CreateModelRoadOccupant(cITSNHoodOccupantManager *face, void *);
    static void __fastcall GetPropOccupants(cITSNHoodOccupantManager *face, void *, RefPtrVector *out);
    static void *__fastcall CreatePropOccupant(cITSNHoodOccupantManager *face, void *);
    static void __fastcall SnapAllRoads(cITSNHoodOccupantManager *face, void *);
    static void __fastcall SnapRoadsInRect(cITSNHoodOccupantManager *face, void *, const int32_t *rect);
    static void __fastcall ResetGridOccupants(cITSNHoodOccupantManager *face, void *);
    static int32_t __fastcall BuildRoadGroups(cITSNHoodOccupantManager *face, void *, uint32_t maxGroups);
    static bool __fastcall GetRoadGroup(cITSNHoodOccupantManager *face, void *, uint32_t index, cTSNHoodRoadTileVector *out);
    static uint32_t __fastcall GetOccupantLayerMask(cITSNHoodOccupantManager *face, void *, void *occupant);
    // +0xc face
    static bool __fastcall Write(cIGZSerializable *face, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(cIGZSerializable *face, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(cIGZSerializable *face, void *);
    // +0x14 face
    static bool __fastcall DoMessage(cIGZMessageTarget2 *face, void *, void *message);
};
