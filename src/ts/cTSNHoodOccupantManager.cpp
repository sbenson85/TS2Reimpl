// cTSNHoodOccupantManager: core, grid and persistence (lane DI, lane 3 of DD's split), removal, queries and roads (lane FY),
// in the standalone shape (lane FY). Layout and facts: DD.
#include "rz/RZMessageServer.h"
#include "ts/cTSNHoodOccupantManager.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZMessageServer.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cITSGlobals.h"
#include "ifaces/cITSNHoodFloraOccupant.h"
#include "ifaces/cITSNHoodOccupant.h"
#include "ifaces/cITSNHoodPropOccupant.h"
#include "ifaces/cITSNHoodRoadOccupant.h"
#include "ifaces/cITSNHoodRoadOccupantWithModel.h"
#include "ifaces/cITSNeighborhood.h"
#include "ifaces/cITSPersistSystem.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZMessage2Standard.h"

#include <cstring>
#include <new>
#include <stdexcept>

namespace {
constexpr uint32_t kSegmentIid = 0x656b8f0c;
constexpr uint32_t kMessageDataIid = 0x65297976;      // the message's data interface, asked by DoMessage
constexpr uint32_t kGridScaleConstant = 0xda03e4;     // float: the grid cell size is the terrain cell size times this
constexpr uint32_t kMaxPointerCount = 0x3fffffff;     // vector<pointer>::max_size
constexpr uint16_t kFloraSubVersion = 8, kRoadSubVersion = 3, kModelRoadSubVersion = 3, kPropSubVersion = 8;
constexpr uint32_t kMessageOccupantRemoved = 0x2c7812db; // sent (not posted) by RemoveOccupant, the occupant as its object
constexpr uint32_t kRoadTypeMask = 2;                     // FindOccupantsInRect's type mask for roads
constexpr uint32_t kMinGroupTiles = 0x14;                 // a traced road group shorter than this is dropped

using MessageServerGetter = void *(__cdecl *)();
using ReserveCounted = void(__thiscall *)(RefPtrVector *, const uint32_t *);
using ConstructOccupant = void *(__thiscall *)(void *);
using GrowService = void(__thiscall *)(RefPtrVector *, void **, void **);
using DestroyService = void(__thiscall *)(RefPtrVector *);
using SnapAtVertex = void(__thiscall *)(cTSNHoodOccupantManager *, uint32_t, uint32_t, void *);
using TraceRoadGroup = void(__thiscall *)(cTSNHoodOccupantManager *, void *road, cTSNHoodRoadTileVector *group);
using SnapRoad = void(__thiscall *)(cTSNHoodOccupantManager *, void *road);

// An object held by a local of the original: released when the scope ends, also when a call throws.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept(false)
    {
        if (object) As<cIGZUnknown>(object)->Release();
    }
};

void AddRefIfAny(void *object)
{
    if (object) As<cIGZUnknown>(object)->AddRef();
}

// A message made the way the game does (0x1c03b7 allocates 0x2c bytes, 0x1c033d builds a cRZMessage2Standard) and counted once.
// When the allocation fails the pointer stays null and the caller's first virtual call dereferences it, as in the original.
struct Message {
    void *object = nullptr;
    Message()
    {
        auto *raw = static_cast<uint8_t *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
        if (raw) {
            *reinterpret_cast<const void **>(raw) = GameAddress<void>(cRZMessage2Standard::kVTable);
            *reinterpret_cast<uint32_t *>(raw + 4) = 0;
            raw[8] = 0;
            for (uint32_t offset = 0x0c; offset <= 0x28; offset += 4) *reinterpret_cast<uint32_t *>(raw + offset) = 0;
        }
        object = raw;
        if (object) As<ifaces::cIGZMessage2Standard>(object)->AddRef();
    }
    Message(const Message &) = delete;
    Message &operator=(const Message &) = delete;
    ~Message() noexcept(false)
    {
        if (object) As<ifaces::cIGZMessage2Standard>(object)->Release();
    }
    ifaces::cIGZMessage2Standard *operator->() { return As<ifaces::cIGZMessage2Standard>(object); }
};

ifaces::cIGZMessageServer *MessageServer()
{
    return As<ifaces::cIGZMessageServer>(GetCachedMessageServer());
}

ifaces::cITSNeighborhood *Neighborhood()
{
    return As<ifaces::cITSNeighborhood>(As<ifaces::cITSGlobals>(GetCachedSingleton())->GetNeighborhood());
}

ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}

// The inlined push_back of a list of counted pointers: counted once for the local that holds it, stored (counted again)
// when there is room, else the reallocating insert; the local lets go at the end.
template <class Grow> void PushBackWith(RefPtrVector &list, void *object, Grow grow)
{
    AddRefIfAny(object);
    if (list.mLast != list.mEnd) {
        *list.mLast = object;
        AddRefIfAny(object);
        ++list.mLast;
    } else {
        grow(list, &object);
    }
    if (object) As<cIGZUnknown>(object)->Release();
}

// The four lists grow over operator new (RefPtrVector::EmplaceReallocate); the grid cells and the vectors handed to the Get
// functions are vectors over the game's allocator service, so they grow, and are emptied and freed, by the original's code.
void PushBackCounted(RefPtrVector &list, void *object)
{
    PushBackWith(list, object, [](RefPtrVector &l, void **value) { l.EmplaceReallocate(l.mLast, value); });
}
void PushBackToCell(RefPtrVector &cell, void *object)
{
    PushBackWith(cell, object, [](RefPtrVector &c, void **value) { OriginalFunction<GrowService, 0x4a7490>()(&c, c.mLast, value); });
}
void DestroyCell(RefPtrVector &cell) { OriginalFunction<DestroyService, 0x22589e>()(&cell); }

// vector::reserve(count) as the original inlines its guard: only when the capacity is smaller; 0x158da0 does the work.
void ReserveFor(RefPtrVector &list, uint32_t count)
{
    const uint32_t capacity = static_cast<uint32_t>(list.mEnd - list.mFirst);
    if (count > capacity) {
        if (count > kMaxPointerCount) throw std::length_error("vector too long");
        OriginalFunction<ReserveCounted, 0x158da0>()(&list, &count);
    }
}

// The occupant's grid footprint: its tile bounds (x0, y0, x1, y1) turned into a cell rectangle.
void FootprintOf(cTSNHoodOccupantManager *manager, void *occupant, int32_t rect[4])
{
    int32_t bounds[4];
    As<ifaces::cITSNHoodOccupant>(occupant)->GetTileBounds(bounds);
    manager->BoundsToCellRect(bounds, rect);
}

RefPtrVector &CellAt(cTSNHoodOccupantManager *manager, int32_t x, int32_t y)
{
    return manager->mGrid.data()[manager->mGridCellsX * static_cast<uint32_t>(y) + static_cast<uint32_t>(x)];
}

// A local vector of counted pointers over the allocator service (grown by 0x4a7490 and destroyed by 0x22589e, also when a
// call throws).
struct LocalCell {
    RefPtrVector list{};
    LocalCell() = default;
    LocalCell(const LocalCell &) = delete;
    LocalCell &operator=(const LocalCell &) = delete;
    ~LocalCell() { DestroyCell(list); }
};

// The unnamed road face slots (cITSNHoodRoadOccupant): 19 a flag, 23 sets the snap mark, 25 a byte (0 = snap the road;
// below 4 = the road joins road groups).
bool RoadSlot19(void *road) { return VirtualCall<uint32_t(__thiscall *)(void *), 19>(road) & 0xff; }
void SetRoadMark(void *road, bool mark) { VirtualCall<void(__thiscall *)(void *, uint32_t), 23>(road, mark ? 1u : 0u); }
uint8_t RoadSlot25(void *road) { return static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *), 25>(road)); }

// The list holds `object` (compared as a pointer): the first entry equal to it goes, the later ones move up (each move
// counted, the overwritten one let go) and the last entry is let go.
void EraseFirst(RefPtrVector &list, void *object)
{
    void **end = list.mLast;
    void **at = list.mFirst;
    while (at != end && *at != object) ++at;
    if (at == end) return;
    for (void **next = at + 1; next != end; ++next) {
        void *moved = *next;
        void *old = *(next - 1);
        if (moved != old) {
            AddRefIfAny(moved);
            *(next - 1) = moved;
            if (old) As<cIGZUnknown>(old)->Release();
        }
    }
    void *last = list.mLast[-1];
    if (last) As<cIGZUnknown>(last)->Release();
    list.mLast -= 1;
}

// The vertex-deferral messages around the road snaps: posted, type only (data 1-3 zero).
void PostDeferral(uint32_t type)
{
    ifaces::cIGZMessageServer *server = MessageServer();
    Message message;
    message->SetType(type);
    message->SetData1(0);
    message->SetData2(0);
    message->SetData3(0);
    server->MessagePost(message.object, 0);
}

// Lets go of every entry and empties the list (its storage stays), when it holds any.
void ReleaseAll(RefPtrVector &list)
{
    void **end = list.mLast;
    void **at = list.mFirst;
    if (at == end) return;
    for (; at != end; ++at)
        if (*at) As<cIGZUnknown>(*at)->Release();
    list.mLast = list.mFirst;
}

// The four Get functions: `out` is emptied (letting go of what it held) and receives a counted copy of the list.
void CopyCounted(RefPtrVector *out, const RefPtrVector &list)
{
    out->Clear();
    void **end = list.mLast;
    for (void **at = list.mFirst; at != end; ++at) {
        if (out->mLast != out->mEnd) {
            *out->mLast = *at;
            AddRefIfAny(*at);
            ++out->mLast;
        } else {
            OriginalFunction<GrowService, 0x38c9a0>()(out, out->mLast, at);
        }
    }
}
} // namespace

// ---- construction and destruction ----

// 0x4a7ff0: the base's vtable and count, then ours; lists and containers start empty, the random generator is seeded
// 0xffffffff, then the id set gets the three messages listened to.
cTSNHoodOccupantManager::cTSNHoodOccupantManager()
    : mInitialized(0), mGrid(), mPendingVertices(), mDeferVertexUpdates(0), mMessageIds(), mKey{}, mRoadGroups()
{
    mRandom.ConstructAt(0xffffffffu, 0x4a819f);
    mMessageIds.insert(kMessageHeightChanged);
    mMessageIds.insert(kMessageBeginDefer);
    mMessageIds.insert(kMessageEndDefer);
}

// 0x4a91f0 (cdecl): the +8 face, null when the allocation fails.
cITSNHoodOccupantManager *__cdecl cTSNHoodOccupantManager::Create()
{
    return new (std::nothrow) cTSNHoodOccupantManager;
}

// 0x4a84a0 (primary slot 3). An initialised manager stops listening and lets go of every occupant first; then the road
// groups, the id set, the pending vertices, the grid cells and the four lists are destroyed.
cTSNHoodOccupantManager::~cTSNHoodOccupantManager()
{
    if (mInitialized) {
        mInitialized = 0;
        reinterpret_cast<KeyMapWalker *>(&mMessageIds)
            ->CallSlot6ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))), nullptr);
        mFlora.Clear();
        mRoads.Clear();
        mModelRoads.Clear();
        mProps.Clear();
    }
    mRoadGroups.~vector(); // 0x4a8250: every group's tiles freed, then the group storage
    mMessageIds.~set();
    mPendingVertices.~unordered_set();
    for (RefPtrVector &cell : mGrid) DestroyCell(cell);
    mGrid.~vector();
    mProps.Destroy();
    mModelRoads.Destroy();
    mRoads.Destroy();
    mFlora.Destroy();
}

// 0x4aa090 (primary slot 0). The out pointer is written without a check; ids that are none of ours are the cIGZUnknown test.
bool cTSNHoodOccupantManager::QueryInterface(uint32_t riid, void **out)
{
    void *face;
    if (riid == kManagerIid) {
        face = static_cast<cITSNHoodOccupantManager *>(this);
    } else if (riid == kMessageTargetIid) {
        face = static_cast<cIGZMessageTarget2 *>(this);
    } else if (riid == kPersistResourceIid) {
        face = static_cast<cIGZPersistResource *>(this);
    } else if (riid == kSerializableIid) {
        face = static_cast<cIGZSerializable *>(this);
    } else {
        return rz::cRZUnknown::QueryInterface(riid, out);
    }
    *out = face;
    AddRef();
    return true;
}

uint32_t cTSNHoodOccupantManager::AddRef()
{
    return rz::cRZUnknown::AddRef();
}

uint32_t cTSNHoodOccupantManager::Release()
{
    return rz::cRZUnknown::Release();
}

// ---- the grid ----

// 0x4a9b10: the cell size and the terrain size from the neighbourhood, then the cell vector emptied (every cell lets go of
// its occupants) and sized to one cell per four terrain cells each way.
void cTSNHoodOccupantManager::InitGrid()
{
    ifaces::cITSNeighborhood *neighborhood = Neighborhood();
    mGridCellSize = neighborhood->GetTerrainCellSize() * *GameAddress<float>(kGridScaleConstant);
    mTerrainWidth = neighborhood->GetTerrainWidth();
    mTerrainHeight = neighborhood->GetTerrainHeight();
    mGridCellsX = mTerrainWidth >> 2;
    mGridCellsY = mTerrainHeight >> 2;
    for (RefPtrVector &cell : mGrid) DestroyCell(cell);
    mGrid.clear();
    mGrid.resize(mGridCellsY * mGridCellsX);
}

void cTSNHoodOccupantManager::BoundsToCellRect(const int32_t *bounds, int32_t *cellRect)
{
    int32_t x0 = bounds[0] / 4;
    cellRect[0] = x0;
    int32_t y0 = bounds[1] / 4;
    cellRect[1] = y0;
    int32_t x1 = bounds[2] / 4;
    cellRect[2] = x1;
    int32_t y1 = bounds[3] / 4;
    cellRect[3] = y1;
    if (x0 < 0) {
        cellRect[0] = 0;
        x0 = 0;
    }
    if (static_cast<uint32_t>(x0) >= mGridCellsX) cellRect[0] = static_cast<int32_t>(mGridCellsX - 1);
    if (y0 < 0) {
        cellRect[1] = 0;
        y0 = 0;
    }
    if (static_cast<uint32_t>(y0) >= mGridCellsY) cellRect[1] = static_cast<int32_t>(mGridCellsY - 1);
    if (x1 < 0) {
        cellRect[2] = 0;
        x1 = 0;
    }
    if (static_cast<uint32_t>(x1) >= mGridCellsX) cellRect[2] = static_cast<int32_t>(mGridCellsX - 1);
    if (y1 < 0) {
        cellRect[3] = 0;
        y1 = 0;
    }
    if (static_cast<uint32_t>(y1) >= mGridCellsY) cellRect[3] = static_cast<int32_t>(mGridCellsY - 1);
}

// 0x4a9c10: the occupant joins (counted) every cell its footprint touches that does not hold it yet.
void cTSNHoodOccupantManager::InsertIntoGrid(void *occupant)
{
    int32_t rect[4];
    FootprintOf(this, occupant, rect);
    for (int32_t y = rect[1]; y <= rect[3]; ++y) {
        for (int32_t x = rect[0]; x <= rect[2]; ++x) {
            RefPtrVector &cell = CellAt(this, x, y);
            bool present = false;
            for (void **at = cell.mFirst; at != cell.mLast; ++at) {
                if (*at == occupant) {
                    present = true;
                    break;
                }
            }
            if (!present) PushBackToCell(cell, occupant);
        }
    }
}

// 0x4aa550: the occupant leaves every cell its footprint touches; the later entries move up (each re-counted) and the last
// one is let go.
void cTSNHoodOccupantManager::RemoveFromGrid(void *occupant)
{
    int32_t rect[4];
    FootprintOf(this, occupant, rect);
    for (int32_t y = rect[1]; y <= rect[3]; ++y) {
        for (int32_t x = rect[0]; x <= rect[2]; ++x) {
            RefPtrVector &cell = CellAt(this, x, y);
            void **end = cell.mLast;
            void **at = cell.mFirst;
            while (at != end && *at != occupant) ++at;
            if (at == end) continue;
            for (void **next = at + 1; next != end; ++next) {
                void *moved = *next;
                void *old = *(next - 1);
                if (moved != old) {
                    AddRefIfAny(moved);
                    *(next - 1) = moved;
                    if (old) As<cIGZUnknown>(old)->Release();
                }
            }
            void *last = cell.mLast[-1];
            if (last) As<cIGZUnknown>(last)->Release();
            cell.mLast -= 1;
        }
    }
}

// 0x4a9e10: into the grid, then into the list of its kind (the first interface it answers to: road with model, road, flora,
// prop). Each query hands out a counted pointer that is let go at the end.
bool cTSNHoodOccupantManager::AddOccupantInternal(void *occupant)
{
    InsertIntoGrid(occupant);
    Held modelRoad, road, flora, prop;
    auto *gridOccupant = As<ifaces::cITSNHoodOccupant>(occupant);
    if (gridOccupant->QueryInterface(kModelRoadIid, &modelRoad.object)) {
        PushBackCounted(mModelRoads, modelRoad.object);
    } else if (gridOccupant->QueryInterface(kRoadIid, &road.object)) {
        PushBackCounted(mRoads, road.object);
    } else if (gridOccupant->QueryInterface(kFloraIid, &flora.object)) {
        PushBackCounted(mFlora, flora.object);
    } else if (gridOccupant->QueryInterface(kPropIid, &prop.object)) {
        PushBackCounted(mProps, prop.object);
    }
    return true;
}

// ---- manager face (+8) ----

// 0x4a9ad0
bool cTSNHoodOccupantManager::Initialize()
{
    mInitialized = 1;
    mDeferVertexUpdates = 0;
    InitGrid();
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
        ->CallSlot5ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))), nullptr);
    return true;
}

// 0x4aad00: when initialised, stops listening and lets go of every occupant of the four lists (the cells keep theirs).
bool cTSNHoodOccupantManager::Shutdown()
{
    if (mInitialized) {
        mInitialized = 0;
        reinterpret_cast<KeyMapWalker *>(&mMessageIds)
            ->CallSlot6ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))), nullptr);
        mFlora.Clear();
        mRoads.Clear();
        mModelRoads.Clear();
        mProps.Clear();
    }
    return true;
}

// 0x4aac50: the persist service writes the resource, then the segment that holds it is flushed. True when both worked.
uint8_t cTSNHoodOccupantManager::Save()
{
    ifaces::cITSPersistSystem *persist = PersistSystem();
    const uint8_t saved = persist->SaveResource(As<ifaces::cIGZPersistResource>(AsPersistResource()), nullptr);
    Held segment;
    const cGZPersistResourceKey key = static_cast<cIGZPersistResource *>(this)->GetKey();
    uint8_t result;
    if (persist->FindSegment(&key, kSegmentIid, &segment.object)) {
        result = saved & As<ifaces::cIGZPersistDBSegment>(segment.object)->Flush();
    } else {
        result = 0;
    }
    return result;
}

// 0x4a9d20: false for null. Otherwise the occupant is added and an "occupant added" message carrying it is posted.
bool cTSNHoodOccupantManager::AddOccupant(void *occupant)
{
    if (!occupant) return false;
    AddOccupantInternal(occupant);
    ifaces::cIGZMessageServer *server = MessageServer();
    Message message;
    message->SetType(kMessageOccupantAdded);
    message->SetObject(occupant);
    message->SetData1(0);
    message->SetData2(0);
    message->SetData3(0);
    server->MessagePost(message.object, 0);
    return true;
}

// 0x4aa070
void cTSNHoodOccupantManager::UpdateOccupantPosition(void *occupant)
{
    RemoveFromGrid(occupant);
    InsertIntoGrid(occupant);
}

// 0x4a9430: the occupant is a road face; roads with a non-zero road type use both layers.
uint32_t cTSNHoodOccupantManager::GetOccupantLayerMask(void *occupant)
{
    return As<ifaces::cITSNHoodRoadOccupant>(occupant)->GetRoadType() ? 3 : 0;
}

// The four Create functions: the occupant is made by the original class constructor on fresh memory and the face the lists
// and the stream use is handed out (null when the allocation failed).
void *cTSNHoodOccupantManager::CreateFloraOccupant() // 0x4a8ef0
{
    void *raw = ::operator new(0x34);
    auto *occupant = raw ? static_cast<uint8_t *>(OriginalFunction<ConstructOccupant, 0x558890>()(raw)) : nullptr;
    return occupant ? occupant + 0x28 : nullptr;
}
void *cTSNHoodOccupantManager::CreatePropOccupant() // 0x4a8f50
{
    void *raw = ::operator new(0x34);
    auto *occupant = raw ? static_cast<uint8_t *>(OriginalFunction<ConstructOccupant, 0x558910>()(raw)) : nullptr;
    return occupant ? occupant + 0x28 : nullptr;
}
void *cTSNHoodOccupantManager::CreateRoadOccupant() // 0x4a8fb0
{
    void *raw = ::operator new(0x98);
    auto *occupant = raw ? static_cast<uint8_t *>(OriginalFunction<ConstructOccupant, 0x558990>()(raw)) : nullptr;
    return occupant ? occupant + 0x28 : nullptr;
}
void *cTSNHoodOccupantManager::CreateModelRoadOccupant() // 0x4a9010
{
    void *raw = ::operator new(0xc4);
    auto *occupant = raw ? static_cast<uint8_t *>(OriginalFunction<ConstructOccupant, 0x558a10>()(raw)) : nullptr;
    return occupant ? occupant + 0x98 : nullptr;
}

void cTSNHoodOccupantManager::GetFloraOccupants(RefPtrVector *out) { CopyCounted(out, mFlora); }         // 0x4a92c0
void cTSNHoodOccupantManager::GetRoadOccupants(RefPtrVector *out) { CopyCounted(out, mRoads); }          // 0x4a96f0
void cTSNHoodOccupantManager::GetModelRoadOccupants(RefPtrVector *out) { CopyCounted(out, mModelRoads); } // 0x4a97a0
void cTSNHoodOccupantManager::GetPropOccupants(RefPtrVector *out) { CopyCounted(out, mProps); }          // 0x4a9630

// 0x4aa830: the occupant (held counted for the call) leaves the grid and the list of the first kind it answers to (road
// with model, road, flora, prop), then an "occupant removed" message carrying it is sent at once.
void cTSNHoodOccupantManager::RemoveOccupant(void *occupant)
{
    if (!occupant) return;
    Held self;
    self.object = occupant;
    As<cIGZUnknown>(occupant)->AddRef();
    RemoveFromGrid(occupant);
    Held modelRoad, road, flora, prop;
    auto *gridOccupant = As<ifaces::cITSNHoodOccupant>(occupant);
    if (gridOccupant->QueryInterface(kModelRoadIid, &modelRoad.object)) {
        EraseFirst(mModelRoads, modelRoad.object);
    } else if (gridOccupant->QueryInterface(kRoadIid, &road.object)) {
        EraseFirst(mRoads, road.object);
    } else if (gridOccupant->QueryInterface(kFloraIid, &flora.object)) {
        EraseFirst(mFlora, flora.object);
    } else if (gridOccupant->QueryInterface(kPropIid, &prop.object)) {
        EraseFirst(mProps, prop.object);
    }
    ifaces::cIGZMessageServer *server = MessageServer();
    Message message;
    message->SetType(kMessageOccupantRemoved);
    message->SetObject(occupant);
    message->SetData1(0);
    message->SetData2(0);
    message->SetData3(0);
    server->MessageSend(message.object);
}

// 0x4aa690: a road of type 0 takes the flora with it, types 1-4 the props: those whose occupant's slot 12 answers this road
// face are gathered first (counted), then each is removed through slot 7.
void cTSNHoodOccupantManager::FindLinkedOccupants(void *road)
{
    if (!road) return;
    LocalCell linked;
    const uint32_t type = As<ifaces::cITSNHoodRoadOccupant>(road)->GetRoadType();
    if (type == 0) {
        void **end = mFlora.mLast;
        for (void **at = mFlora.mFirst; at != end; ++at) {
            ifaces::cITSNHoodOccupant *occupant = As<ifaces::cITSNHoodFloraOccupant>(*at)->AsOccupant();
            if (occupant->GetCatalogResource() == road) PushBackToCell(linked.list, occupant);
        }
    } else if (type <= 4) {
        void **end = mProps.mLast;
        for (void **at = mProps.mFirst; at != end; ++at) {
            ifaces::cITSNHoodOccupant *occupant = As<ifaces::cITSNHoodPropOccupant>(*at)->AsOccupant();
            if (occupant->GetCatalogResource() == road) PushBackToCell(linked.list, occupant);
        }
    }
    const int32_t count = static_cast<int32_t>(linked.list.mLast - linked.list.mFirst);
    for (int32_t i = 0; i < count; ++i) RemoveOccupant(linked.list.mFirst[i]);
}

// 0x4a9450
bool cTSNHoodOccupantManager::FindOccupantsInRect(const int32_t *rect, uint8_t typeMask, RefPtrVector *out)
{
    if (out) ReleaseAll(*out);
    bool found = false;
    int32_t cells[4];
    BoundsToCellRect(rect, cells);
    for (int32_t y = cells[1]; y <= cells[3]; ++y) {
        for (int32_t x = cells[0]; x <= cells[2]; ++x) {
            RefPtrVector &cell = CellAt(this, x, y);
            const uint32_t count = static_cast<uint32_t>(cell.mLast - cell.mFirst);
            for (uint32_t i = 0; i < count; ++i) {
                void *occupant = cell.mFirst[i];
                const uint8_t type = static_cast<uint8_t>(VirtualCall<uint32_t(__thiscall *)(void *), 3>(occupant));
                if (!(typeMask & type)) continue;
                int32_t bounds[4];
                As<ifaces::cITSNHoodOccupant>(occupant)->GetTileBounds(bounds);
                if (bounds[2] <= rect[0] || bounds[0] >= rect[2] || bounds[3] <= rect[1] || bounds[1] >= rect[3]) continue;
                found = true;
                if (!out) return true;
                bool present = false;
                for (void **at = out->mFirst; at != out->mLast; ++at) {
                    if (*at == occupant) {
                        present = true;
                        break;
                    }
                }
                if (!present) PushBackToCell(*out, occupant);
            }
        }
    }
    return found;
}

// 0x4aadb0
void cTSNHoodOccupantManager::SnapAllRoads()
{
    PostDeferral(kMessageBeginDefer);
    void **end = mRoads.mLast;
    for (void **at = mRoads.mFirst; at != end; ++at) SetRoadMark(*at, RoadSlot19(*at));
    end = mRoads.mLast;
    for (void **at = mRoads.mFirst; at != end; ++at)
        if (!RoadSlot25(*at)) OriginalFunction<SnapRoad, 0x4aaf80>()(this, *at);
    end = mRoads.mLast;
    for (void **at = mRoads.mFirst; at != end; ++at) SetRoadMark(*at, false);
    PostDeferral(kMessageEndDefer);
}

// 0x4ab400
void cTSNHoodOccupantManager::SnapRoadsInRect(const int32_t *rect)
{
    PostDeferral(kMessageBeginDefer);
    LocalCell found;
    if (FindOccupantsInRect(rect, kRoadTypeMask, &found.list)) {
        void **end = found.list.mLast;
        for (void **at = found.list.mFirst; at != end; ++at) {
            Held road;
            if (As<ifaces::cITSNHoodOccupant>(*at)->QueryInterface(kRoadIid, &road.object)) SetRoadMark(road.object, true);
        }
        int32_t grown[4] = {rect[0] - 1, rect[1] - 1, rect[2] + 1, rect[3] + 1};
        const uint32_t limits[4] = {mTerrainWidth, mTerrainHeight, mTerrainWidth, mTerrainHeight};
        for (int k = 0; k < 4; ++k) {
            if (grown[k] < 0) grown[k] = 0;
            else if (static_cast<uint32_t>(grown[k]) > limits[k]) grown[k] = static_cast<int32_t>(limits[k]);
        }
        if (FindOccupantsInRect(grown, kRoadTypeMask, &found.list)) {
            end = found.list.mLast;
            for (void **at = found.list.mFirst; at != end; ++at) {
                Held road;
                if (As<ifaces::cITSNHoodOccupant>(*at)->QueryInterface(kRoadIid, &road.object) && !RoadSlot25(road.object)) {
                    if (RoadSlot19(road.object)) SetRoadMark(road.object, true);
                    else OriginalFunction<SnapRoad, 0x4aaf80>()(this, road.object);
                }
            }
        }
    }
    PostDeferral(kMessageEndDefer);
}

// 0x4a85f0: every occupant of every grid cell gets its slot 10 with 0.
void cTSNHoodOccupantManager::ResetGridOccupants()
{
    for (RefPtrVector &cell : mGrid)
        for (void **at = cell.mFirst; at != cell.mLast; ++at) VirtualCall<void(__thiscall *)(void *, uint32_t), 10>(*at, 0u);
}

// 0x4a8b70: the marks (road slot 23) are cleared before and after. Each road whose slot 25 answers below 4 is traced from
// (0x4a8930) into the newest group; a group of 20 tiles or more is kept and a new empty one started, a shorter one emptied.
// Two passes over the roads, stopping once there are more than `maxGroups` groups; the last (open) group is dropped.
int32_t cTSNHoodOccupantManager::BuildRoadGroups(uint32_t maxGroups)
{
    std::vector<cTSNHoodRoadGroup> &groups = mRoadGroups;
    if (!groups.empty()) groups.clear();
    void **end = mRoads.mLast;
    for (void **at = mRoads.mFirst; at != end; ++at) SetRoadMark(*at, false);
    groups.push_back(cTSNHoodRoadGroup{});
    uint32_t count = 1;
    cTSNHoodRoadTileVector *group = &groups.back().mTiles;
    for (int pass = 0; pass < 2; ++pass) {
        if (count > maxGroups) break;
        end = mRoads.mLast;
        for (void **at = mRoads.mFirst; at != end; ++at) {
            if (count > maxGroups) break;
            void *road = *at;
            if (RoadSlot25(road) >= 4) continue;
            OriginalFunction<TraceRoadGroup, 0x4a8930>()(this, road, group);
            if (group->size() < kMinGroupTiles) {
                group->clear();
            } else {
                groups.push_back(cTSNHoodRoadGroup{});
                group = &groups.back().mTiles;
                ++count;
            }
        }
    }
    groups.pop_back();
    end = mRoads.mLast;
    for (void **at = mRoads.mFirst; at != end; ++at) SetRoadMark(*at, false);
    return static_cast<int32_t>(groups.size());
}

// 0x4a9260: `out` is emptied; a valid index copies that group's tiles into it.
bool cTSNHoodOccupantManager::GetRoadGroup(uint32_t index, cTSNHoodRoadTileVector *out)
{
    out->clear();
    if (index >= mRoadGroups.size()) return false;
    *out = mRoadGroups[index].mTiles;
    return true;
}

// ---- serializable face (+0xc) ----

// 0x4a9250
uint32_t cTSNHoodOccupantManager::GetGZCLSID() { return kClassId; }

// 0x4ab6f0: version 0xcb, then four lists, each a u16 sub-version (flora 8, roads 3, roads with a model 3, props 8), a u32
// count and every occupant's own Write. Neither the stream's errors nor the occupants' results are looked at.
bool cTSNHoodOccupantManager::Write(ifaces::cIGZOStream *stream)
{
    stream->SetUint32(kRecordVersion);
    stream->SetUint16(kFloraSubVersion);
    uint32_t count = mFlora.Size();
    stream->SetUint32(count);
    for (uint32_t i = 0; i < count; ++i) As<ifaces::cITSNHoodFloraOccupant>(mFlora.mFirst[i])->Write(stream);
    stream->SetUint16(kRoadSubVersion);
    count = mRoads.Size();
    stream->SetUint32(count);
    for (uint32_t i = 0; i < count; ++i) As<ifaces::cITSNHoodRoadOccupant>(mRoads.mFirst[i])->Write(stream);
    stream->SetUint16(kModelRoadSubVersion);
    count = mModelRoads.Size();
    stream->SetUint32(count);
    for (uint32_t i = 0; i < count; ++i) As<ifaces::cITSNHoodRoadOccupantWithModel>(mModelRoads.mFirst[i])->Write(stream);
    stream->SetUint16(kPropSubVersion);
    count = mProps.Size();
    stream->SetUint32(count);
    for (uint32_t i = 0; i < count; ++i) As<ifaces::cITSNHoodPropOccupant>(mProps.mFirst[i])->Write(stream);
    return true;
}

// 0x4aa110: marks the manager initialised and listens for the terrain messages, then reads the version; versions below 0x6d
// stop there. Each list (flora, roads, roads with a model, props) is a sub-version, a count and that many occupants made by
// the manager's own Create function and read by their Read with the sub-version. A Read that fails leaves its list as it
// is (the failing occupant is let go), ends that list and makes the result false, and the next list is still read. What
// joins the manager is the occupant behind the type face: flora and props only when they found their catalog resource;
// roads and roads with a model always.
// ORIGINAL-BUG: the roads-with-a-model list is the only one not emptied first, so reading into a manager that already
// holds some adds to them.
bool cTSNHoodOccupantManager::Read(ifaces::cIGZIStream *stream)
{
    mInitialized = 1;
    mDeferVertexUpdates = 0;
    InitGrid();
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
        ->CallSlot5ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))), nullptr);
    uint32_t version; // an uninitialised stack local in the original
    stream->GetUint32(&version);
    if (version < kOldestReadableVersion) return true;

    bool result;
    uint16_t subVersion = 0;
    uint32_t count = 0;

    // flora
    mFlora.Clear();
    stream->GetUint16(&subVersion);
    stream->GetUint32(&count);
    ReserveFor(mFlora, count);
    result = true;
    for (uint32_t i = 0; i < count; ++i) {
        Held occupant;
        occupant.object = CreateFloraOccupant();
        AddRefIfAny(occupant.object);
        if (!As<ifaces::cITSNHoodFloraOccupant>(occupant.object)->Read(stream, subVersion)) {
            result = false;
            break;
        }
        ifaces::cITSNHoodOccupant *base = As<ifaces::cITSNHoodFloraOccupant>(occupant.object)->AsOccupant();
        if (base->GetCatalogResource()) AddOccupantInternal(base);
    }

    // roads
    mRoads.Clear();
    stream->GetUint16(&subVersion);
    stream->GetUint32(&count);
    ReserveFor(mRoads, count);
    bool listResult = true;
    for (uint32_t i = 0; i < count; ++i) {
        Held occupant;
        occupant.object = CreateRoadOccupant();
        AddRefIfAny(occupant.object);
        if (!As<ifaces::cITSNHoodRoadOccupant>(occupant.object)->Read(stream, subVersion)) {
            listResult = false;
            break;
        }
        AddOccupantInternal(As<ifaces::cITSNHoodRoadOccupant>(occupant.object)->AsOccupant());
    }
    result = listResult && result;

    // roads with a model (not emptied first)
    stream->GetUint16(&subVersion);
    stream->GetUint32(&count);
    ReserveFor(mModelRoads, count);
    listResult = true;
    for (uint32_t i = 0; i < count; ++i) {
        Held occupant;
        occupant.object = CreateModelRoadOccupant();
        AddRefIfAny(occupant.object);
        if (!As<ifaces::cITSNHoodRoadOccupantWithModel>(occupant.object)->Read(stream, subVersion)) {
            listResult = false;
            break;
        }
        ifaces::cITSNHoodRoadOccupant *road = As<ifaces::cITSNHoodRoadOccupantWithModel>(occupant.object)->AsRoadOccupant();
        AddOccupantInternal(road->AsOccupant());
    }
    result = listResult && result;

    // props
    mProps.Clear();
    stream->GetUint16(&subVersion);
    stream->GetUint32(&count);
    ReserveFor(mProps, count);
    listResult = true;
    for (uint32_t i = 0; i < count; ++i) {
        Held occupant;
        occupant.object = CreatePropOccupant();
        AddRefIfAny(occupant.object);
        if (!As<ifaces::cITSNHoodPropOccupant>(occupant.object)->Read(stream, subVersion)) {
            listResult = false;
            break;
        }
        ifaces::cITSNHoodOccupant *base = As<ifaces::cITSNHoodPropOccupant>(occupant.object)->AsOccupant();
        if (base->GetCatalogResource()) AddOccupantInternal(base);
    }
    return listResult && result;
}

// ---- persist face (+0x10): the folded key accessors ----

cGZPersistResourceKey cTSNHoodOccupantManager::GetKey() // folded 0x49e6c0: the 16 bytes at +0x98
{
    cGZPersistResourceKey key;
    static_assert(sizeof(key) == sizeof(mKey));
    std::memcpy(&key, mKey, sizeof(mKey));
    return key;
}

bool cTSNHoodOccupantManager::SetKey(const cGZPersistResourceKey &key) // folded 0x4a0da0
{
    std::memcpy(mKey, &key, sizeof(mKey));
    return true;
}

// ---- message face (+0x14) ----

// 0x4a9070: the message is asked for its data interface (0x65297976), whose type picks what happens: begin deferring
// (the pending vertices are emptied once), a terrain height changed (x = data 1, y = data 2: remembered while deferring,
// else the occupants around the vertex are re-heighted at once), or end deferring (every remembered vertex is processed
// against one set of already-done occupants, then the memory is emptied). Any other type answers false.
bool cTSNHoodOccupantManager::DoMessage(void *message)
{
    if (!message) return false;
    Held data;
    bool handled = false;
    if (As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(kMessageDataIid, &data.object)) {
        auto *info = As<ifaces::cIGZMessage2Standard>(data.object);
        const uint32_t type = info->GetType();
        if (type == kMessageBeginDefer) {
            if (!mDeferVertexUpdates) {
                mPendingVertices.clear();
                mDeferVertexUpdates = 1;
            }
            handled = true;
        } else if (type == kMessageHeightChanged) {
            const uint32_t x = info->GetData1();
            const uint32_t y = info->GetData2();
            if (mDeferVertexUpdates) {
                mPendingVertices.insert((y << 16) + x);
            } else {
                std::unordered_set<uint32_t> done;
                OriginalFunction<SnapAtVertex, 0x4a9850>()(this, x, y, &done);
            }
            handled = true;
        } else if (type == kMessageEndDefer) {
            std::unordered_set<uint32_t> done;
            for (uint32_t vertex : mPendingVertices) OriginalFunction<SnapAtVertex, 0x4a9850>()(this, vertex & 0xffff, vertex >> 16, &done);
            mPendingVertices.clear();
            mDeferVertexUpdates = 0;
            handled = true;
        }
    }
    return handled;
}

// ---- adapters (the RVA hooks) ----

using Adapters = cTSNHoodOccupantManagerAdapters;
using Self = cTSNHoodOccupantManager;

// 0x4a7ff0
Self *__fastcall Adapters::Construct(Self *memory, void *)
{
    return ::new (memory) Self;
}

// 0x4a84a0: the destructor and, with bit 0, the sized delete (the compiler's scalar deleting destructor, by hand).
Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}
void __fastcall Adapters::InitGrid(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::InitGrid();
}
void __fastcall Adapters::InsertIntoGrid(Self *self, void *, void *occupant)
{
    [[msvc::forceinline_calls]] return self->Self::InsertIntoGrid(occupant);
}
void __fastcall Adapters::RemoveFromGrid(Self *self, void *, void *occupant)
{
    [[msvc::forceinline_calls]] return self->Self::RemoveFromGrid(occupant);
}
bool __fastcall Adapters::AddOccupantInternal(Self *self, void *, void *occupant)
{
    [[msvc::forceinline_calls]] return self->Self::AddOccupantInternal(occupant);
}
void __fastcall Adapters::BoundsToCellRect(Self *self, void *, const int32_t *bounds, int32_t *cellRect)
{
    [[msvc::forceinline_calls]] return self->Self::BoundsToCellRect(bounds, cellRect);
}

bool __fastcall Adapters::Initialize(cITSNHoodOccupantManager *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Initialize();
}
bool __fastcall Adapters::Shutdown(cITSNHoodOccupantManager *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Shutdown();
}
uint8_t __fastcall Adapters::Save(cITSNHoodOccupantManager *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Save();
}
bool __fastcall Adapters::AddOccupant(cITSNHoodOccupantManager *face, void *, void *occupant)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::AddOccupant(occupant);
}
void __fastcall Adapters::RemoveOccupant(cITSNHoodOccupantManager *face, void *, void *occupant)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::RemoveOccupant(occupant);
}
void __fastcall Adapters::FindLinkedOccupants(cITSNHoodOccupantManager *face, void *, void *road)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::FindLinkedOccupants(road);
}
void __fastcall Adapters::UpdateOccupantPosition(cITSNHoodOccupantManager *face, void *, void *occupant)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::UpdateOccupantPosition(occupant);
}
bool __fastcall Adapters::FindOccupantsInRect(cITSNHoodOccupantManager *face, void *, const int32_t *rect, uint8_t typeMask, RefPtrVector *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::FindOccupantsInRect(rect, typeMask, out);
}
void __fastcall Adapters::GetFloraOccupants(cITSNHoodOccupantManager *face, void *, RefPtrVector *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetFloraOccupants(out);
}
void *__fastcall Adapters::CreateFloraOccupant(cITSNHoodOccupantManager *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::CreateFloraOccupant();
}
void __fastcall Adapters::GetRoadOccupants(cITSNHoodOccupantManager *face, void *, RefPtrVector *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetRoadOccupants(out);
}
void *__fastcall Adapters::CreateRoadOccupant(cITSNHoodOccupantManager *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::CreateRoadOccupant();
}
void __fastcall Adapters::GetModelRoadOccupants(cITSNHoodOccupantManager *face, void *, RefPtrVector *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetModelRoadOccupants(out);
}
void *__fastcall Adapters::CreateModelRoadOccupant(cITSNHoodOccupantManager *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::CreateModelRoadOccupant();
}
void __fastcall Adapters::GetPropOccupants(cITSNHoodOccupantManager *face, void *, RefPtrVector *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetPropOccupants(out);
}
void *__fastcall Adapters::CreatePropOccupant(cITSNHoodOccupantManager *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::CreatePropOccupant();
}
void __fastcall Adapters::SnapAllRoads(cITSNHoodOccupantManager *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SnapAllRoads();
}
void __fastcall Adapters::SnapRoadsInRect(cITSNHoodOccupantManager *face, void *, const int32_t *rect)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SnapRoadsInRect(rect);
}
void __fastcall Adapters::ResetGridOccupants(cITSNHoodOccupantManager *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::ResetGridOccupants();
}
int32_t __fastcall Adapters::BuildRoadGroups(cITSNHoodOccupantManager *face, void *, uint32_t maxGroups)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::BuildRoadGroups(maxGroups);
}
bool __fastcall Adapters::GetRoadGroup(cITSNHoodOccupantManager *face, void *, uint32_t index, cTSNHoodRoadTileVector *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetRoadGroup(index, out);
}
uint32_t __fastcall Adapters::GetOccupantLayerMask(cITSNHoodOccupantManager *face, void *, void *occupant)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetOccupantLayerMask(occupant);
}
bool __fastcall Adapters::Write(cIGZSerializable *face, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Write(stream);
}
bool __fastcall Adapters::Read(cIGZSerializable *face, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Read(stream);
}
uint32_t __fastcall Adapters::GetGZCLSID(cIGZSerializable *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetGZCLSID();
}
bool __fastcall Adapters::DoMessage(cIGZMessageTarget2 *face, void *, void *message)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::DoMessage(message);
}
