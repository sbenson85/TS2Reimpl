// nTSWorld::cWorldDB (lane JC): the class in the standalone shape (src/world/cWorldDB.h).
#include "rz/RZMessageServer.h"
#include "world/cWorldDB.h"
#include "world/WorldTileMath.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "gz/cIGZString.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZMessageServer.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cITSPersistSystem.h"
#include "misc/UtilitiesC.h"    // KeyMapSlot5Walker
#include "rz/MiscHelpersB.h"    // KeyMapWalker
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "rz/cRZMessage2Standard.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace nTSWorld {

namespace {

// Original callees nobody has replaced.
void *MessageServer() { return GetCachedMessageServer(); }
// 0xd15080 (__cdecl): Init's set-up of the world's static tables (the world lane's, not ours).
void SetUpStatics() { OriginalFunction<void(__cdecl *)(), 0xd15080>()(); }
// 0xd17090 (__cdecl, the database): rebuilds the lot's derived floor data (after the 0xad message and in Save).
void RebuildDerived(cWorldDB *world) { OriginalFunction<void(__cdecl *)(cWorldDB *), 0xd17090>()(world); }
// 0xd0d040 (__thiscall, the database): moves the grids' contents by `delta` levels (top when `top`).
void ShiftLevels(cWorldDB *world, int32_t delta, uint32_t top)
{
    OriginalFunction<void(__thiscall *)(cWorldDB *, int32_t, uint32_t), 0xd0d040>()(world, delta, top);
}
// 0x7dd9d0 (__cdecl): the tile of a point (x, y floored, the level kept).
void TileOf(WorldCell *out, const WorldPoint *point)
{
    worldmath::PositionToTile(reinterpret_cast<int32_t *>(out), reinterpret_cast<const float *>(point)); // 0x7dd9d0
}
void *PersistSystem() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }

constexpr uint32_t kSettingsClass = 0x0ac1887e;
constexpr uint32_t kSettingsIid = 0x6bb22f38;
constexpr uint32_t kUnevenTilesSetting = 0x0c02efce;
constexpr uint32_t kMaxGradeSetting = 0x2c02eff6;
constexpr uint32_t kSimReadyMessage = 0xad;
constexpr uint32_t kSimReadyRepost = 0x6b4d074c;
constexpr uint32_t kMessageIid = 0x65297976;
constexpr uint32_t kAnyBooleanVTable = 0xd9c670;
constexpr uint32_t kAnyUint32VTable = 0xd9f534;
constexpr uint32_t kPatternChangeVTable = 0xebb6cc;
constexpr uint32_t kElevationChangeVTable = 0xebb6dc;
constexpr uint32_t kTerrainChangeVTable = 0xebb6ec;
constexpr uint32_t kVisibilityChangeVTable = 0xebb6fc;
constexpr uint32_t kStoredVTable = 0xebb730;

// The masks that clear one rotation nibble (0xebb30c).
constexpr uint16_t kRotationKeep[4] = {0xfff0, 0xff0f, 0xf0ff, 0x0fff};

// The settings service (the framework's 0x0ac1887e / 0x6bb22f38), a counted local.
ScopedRef Settings()
{
    ScopedRef settings{nullptr};
    if (void *const framework = GetServiceObject()) As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kSettingsClass, kSettingsIid, &settings.mObject);
    return settings;
}

// A value of the settings service's typed values (cAnyBoolean, cAnyUint32): vtable, value.
struct AnyValue {
    const void *mVTable;
    uint32_t mValue;
};

// A standard message (RZMemory 0x2c bytes, built as 0x1c03b7 / 0x1c033d build one), counted once when allocated.
cRZMessage2Standard *NewMessage(uint32_t id)
{
    auto *const message = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    if (message) {
        message->mVTable = GameAddress<void>(cRZMessage2Standard::kVTable);
        message->mRefCount = 0;
        message->mFlags = 0;
        message->mId = 0;
        for (uint32_t &v : message->mValue) v = 0;
        message->mPtr20 = message->mPtr24 = message->mPtr28 = nullptr;
        As<ifaces::cIGZMessage2Standard>(message)->AddRef();
    }
    As<ifaces::cIGZMessage2Standard>(message)->SetType(id); // used unchecked, as the original does
    return message;
}
void ReleaseOf(void *object)
{
    if (object) As<cIGZUnknown>(object)->Release();
}
void Post(void *message) { As<ifaces::cIGZMessageServer>(MessageServer())->MessageSend(message); }

// The change records the messages carry (game operator new, the game's vtables, a zero count, counted once).
uint32_t *NewRecord(uint32_t bytes, uint32_t vtable)
{
    auto *const record = static_cast<uint32_t *>(::operator new(bytes));
    record[1] = 0;
    record[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(vtable)));
    return record;
}
// The record handed to the message (slot 33), the message posted, the record released.
void PostWithRecord(cRZMessage2Standard *message, uint32_t *record)
{
    RawSlot<1>(record);
    As<ifaces::cIGZMessage2Standard>(message)->SetObject(record);
    Post(message);
    ReleaseOf(record);
}

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
float Float(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// AddObjectRects' tile of a coordinate: round(v - 0.5) through the 1.5 * 2^23 bias, one up when that lands on v exactly.
int32_t FloorTile(float v)
{
    const float biased = v - 0.5f + 12582912.0f;
    const int32_t n = static_cast<int32_t>(Bits(biased) + 0xb4c00000u);
    return static_cast<float>(n + 1) == v ? n + 1 : n;
}

bool SameName(const char *a, const char *b)
{
    if (!a) return !b;
    return b && _stricmp(a, b) == 0;
}

} // namespace

// ---- construction ----

// 0xd0b6c0 (506 B): the service base (id 0x4a037ee3, priority -1,000,001), the members (see the header) and the message it
// takes, 0xad.
cWorldDB::cWorldDB() : rz::cRZSystemService(kServiceId, kPriority) { mMessageIds.insert(kSimReadyMessage); }

// 0xd0b960 (614 B): the members in reverse order (the counted pointers released, the object cells, the name, the message
// ids), then the base.
cWorldDB::~cWorldDB() noexcept {}

cWorldDB *__fastcall cWorldDBAdapters::Construct(void *memory, void *) { return ::new (memory) cWorldDB; }
uint16_t __fastcall cWorldDBAdapters::PatternId(cWorldDB *self, void *, const WorldName *name) { return self->PatternId(name); }
void __fastcall cWorldDBAdapters::SetPatternByNameB(cWorldDB *self, void *, const WorldName *name, const WorldTile *tile)
{
    self->SetPatternByNameImplB(name, tile);
}
void __fastcall cWorldDBAdapters::SetLevels(cWorldDB *self, void *, int32_t minLevel, int32_t maxLevel)
{
    self->SetLevelsImpl(minLevel, maxLevel);
}
void __fastcall cWorldDBAdapters::SetByteTile(cWorldDB *self, void *, uint8_t value, const WorldTile *tile)
{
    self->SetByteTileImpl(value, tile);
}
void __fastcall cWorldDBAdapters::UsePatternName(cWorldDB *self, void *, const WorldName *oldName, const WorldName *newName,
                                                  uint8_t segments)
{
    self->UsePatternName(oldName, newName, segments);
}

// ---- cIGZUnknown ----

// 0xd11360: cIWorldDB -> +0x1c, the class id -> this (both counted), else the service's; a null `out` fails.
bool cWorldDB::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == kIid) {
        *out = static_cast<cIWorldDB *>(this);
        AddRef();
        return true;
    }
    if (riid == kClassId) {
        *out = this;
        AddRef();
        return true;
    }
    return rz::cRZSystemService::QueryInterface(riid, out);
}

uint32_t cWorldDB::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cWorldDB::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService ----

// 0xd0e6b0 (2030 B): the message registered, the two settings watched (with their defaults), the static tables, then the
// grids, the framework's world objects, the object index and the pattern table made.
bool cWorldDB::Init()
{
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(MessageTarget())), nullptr);
    ScopedRef settings = Settings();
    {
        GZServiceString key("unevenTilesFloorable");
        AnyValue value{GameAddress<void>(kAnyBooleanVTable), 1};
        // ORIGINAL-BUG: the settings service is used without a null check (the original's watch calls).
        RawSlot<4>(settings.mObject, MessageTarget(), kUnevenTilesSetting, &key, &value);
    }
    {
        GZServiceString key("maxTerrainGrade");
        AnyValue value{GameAddress<void>(kAnyUint32VTable), 5};
        RawSlot<4>(settings.mObject, MessageTarget(), kMaxGradeSetting, &key, &value);
    }
    SetUpStatics();

    mPatterns.Assign(new PatternGrid());
    mRotations.Assign(new RotationGrid());
    mPatternsB.Assign(new PatternGrid());
    mRotationsB.Assign(new RotationGrid());
    mElevations.Assign(new FloatGrid());
    mVisibility.Assign(new VisibilityGrid());
    mBytes.Assign(new ByteGrid());

    // The framework's objects: the old one released, the new one made (the director's slot 3), then its slot 3 (Init).
    auto make = [](WorldRef<void> &member, uint32_t classId, uint32_t iid) {
        void *const director = GetDirectorFrameworkObject();
        void **const out = member.Out();
        RawSlot<3, bool>(director, classId, iid, out);
        if (member) RawSlot<3>(member.Get());
    };
    make(mTerrain, 0x4b58975b, 0xeb589762);
    make(mRoofs, 0xab9406aa, 0xab9406b1);
    make(mPools, 0x0c900fdb, 0x2c900fe6);
    make(mCurvedPools, 0x916da14d, 0x916da151);
    make(mPartitions, 0x6ca2a642, 0x6ca2a64e);

    mObjects.Assign(new ObjectGrid());
    mObjectIndex.Assign(new cVertexLayer());
    mPatternTable.Assign(new cStringMap()); // the table word left as the allocation held it, as the original's
    mPatternTable->mByteC = 0;
    mPatternTable->mWord8 = 1;
    mDeckFoundations.Assign(new DeckGrid());
    mAwnings.Assign(new AwningGrid());
    make(mOutsideSupport, 0xb21f9b20, 0xb21f9b25);

    mInitialized = true;
    settings.ReleaseObject();
    return true;
}

// 0xd133c0 (829 B): the message unregistered, the settings unwatched, the grids released, the framework's objects shut down
// (slot 4) and released, the object cells cleared.
bool cWorldDB::Shutdown()
{
    mInitialized = false;
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(MessageTarget())), nullptr);
    ScopedRef settings = Settings();
    if (settings.mObject) {
        {
            GZServiceString key("unevenTilesFloorable");
            RawSlot<5>(settings.mObject, MessageTarget(), kUnevenTilesSetting, &key);
        }
        GZServiceString key("maxTerrainGrade");
        RawSlot<5>(settings.mObject, MessageTarget(), kMaxGradeSetting, &key);
    }
    mPatterns.Clear();
    mRotations.Clear();
    mPatternsB.Clear();
    mRotationsB.Clear();
    mElevations.Clear();
    mVisibility.Clear();
    mBytes.Clear();
    auto shut = [](WorldRef<void> &member) {
        if (member) {
            RawSlot<4>(member.Get());
            member.Clear();
        }
    };
    shut(mTerrain);
    shut(mRoofs);
    shut(mPools);
    shut(mCurvedPools);
    shut(mPartitions);
    mObjects.Clear();
    mObjectIndex.Clear();
    mPatternTable.Clear();
    mDeckFoundations.Clear();
    mAwnings.Clear();
    shut(mOutsideSupport);
    mObjectCells.clear();
    settings.ReleaseObject();
    return true;
}

// ---- cIGZMessageTarget2 ----

// 0xd0db70 (392 B): the two settings' changes kept; 0xad (the sim is ready) re-posts the message once as 0x6b4d074c, then
// rebuilds the derived floor data. Anything that is not a standard message fails.
bool cWorldDB::DoMessage(void *message)
{
    if (!message) return false;
    void *standard = nullptr;
    if (!As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(kMessageIid, &standard)) {
        ReleaseOf(standard);
        return false;
    }
    switch (As<ifaces::cIGZMessage2Standard>(standard)->GetType()) {
    case kSimReadyMessage:
        if (!mReposted) {
            // ORIGINAL-BUG: a new message is made and counted, but the incoming one is what gets the new id and is posted;
            // the new one is released unused.
            auto *const unused = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
            if (unused) {
                unused->mVTable = GameAddress<void>(cRZMessage2Standard::kVTable);
                unused->mRefCount = 0;
                unused->mFlags = 0;
                unused->mId = 0;
                for (uint32_t &v : unused->mValue) v = 0;
                unused->mPtr20 = unused->mPtr24 = unused->mPtr28 = nullptr;
                As<ifaces::cIGZMessage2Standard>(unused)->AddRef();
            }
            As<ifaces::cIGZMessage2Standard>(message)->SetType(kSimReadyRepost);
            Post(message);
            mReposted = true;
            ReleaseOf(unused);
        }
        RebuildDerived(this);
        break;
    case kUnevenTilesSetting:
        if (RawSlot<13, bool>(standard)) {
            if (auto *const value = RawSlot<15, const uint8_t *>(standard)) mUnevenTilesFloorable = value[4];
        }
        break;
    case kMaxGradeSetting:
        if (RawSlot<13, bool>(standard)) {
            if (auto *const value = RawSlot<15, const uint32_t *>(standard)) mMaxTerrainGrade = value[1];
        }
        break;
    default:
        break;
    }
    ReleaseOf(standard);
    return true;
}

// ---- cIGZSerializable ----

// 0xd143e0 (362 B): the class id, version 7, "cWorldDB", the bounds and levels, whether it has a name, the name.
bool cWorldDB::Write(ifaces::cIGZOStream *stream)
{
    stream->SetUint32(GetGZCLSID());
    stream->SetUint32(7u);
    {
        WorldString tag("cWorldDB");
        stream->SetGZStr(&tag.s);
        stream->SetSint32(GetMinX());
        stream->SetSint32(GetMinY());
        stream->SetSint32(GetMinLevel());
        stream->SetSint32(GetMaxX());
        stream->SetSint32(GetMaxY());
        stream->SetSint32(GetMaxLevel());
        stream->SetSint32(mHasName ? 1u : 0u);
        if (mHasName) stream->SetGZStr(&mName.s);
    }
    return true;
}

// 0xd11ce0 (416 B): the class id must match and the version be 1..7. Version 1 has the sizes only (the origin 0); from 4 on,
// whether there is a name, and the name.
bool cWorldDB::Read(ifaces::cIGZIStream *stream)
{
    uint32_t clsid;
    stream->GetUint32(&clsid);
    if (clsid != GetGZCLSID()) return false;
    uint32_t version;
    stream->GetUint32(&version);
    if (version - 1 > 6) return false;
    WorldString tag;
    stream->GetGZStr(&tag.s);
    int32_t minX = 0, minY = 0, minLevel = 0, maxX, maxY, maxLevel;
    if (version == 1) {
        int32_t sizeX, sizeY, levels;
        stream->GetSint32(&sizeX);
        stream->GetSint32(&sizeY);
        stream->GetSint32(&levels);
        maxX = sizeX - 1;
        maxY = sizeY - 1;
        maxLevel = levels - 1;
    } else {
        stream->GetSint32(&minX);
        stream->GetSint32(&minY);
        stream->GetSint32(&minLevel);
        stream->GetSint32(&maxX);
        stream->GetSint32(&maxY);
        stream->GetSint32(&maxLevel);
    }
    mMinX = minX;
    mMinY = minY;
    mMinLevel = minLevel;
    mMaxX = maxX;
    mMaxY = maxY;
    mMaxLevel = maxLevel;
    if (version >= 4) {
        int32_t hasName;
        stream->GetSint32(&hasName);
        mHasName = hasName == 1;
        if (mHasName) stream->GetGZStr(&mName.s);
    }
    return true;
}

uint32_t cWorldDB::GetGZCLSID() { return kClassId; } // 0xd0e2c0

// ---- cIWorldDB: bounds, levels, persistence ----

// 0xd13310: the bounds stored and handed to the partitions (slot 5), then the grids resized.
void cWorldDB::SetBounds(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY)
{
    mMinX = minX;
    mMinY = minY;
    mMaxX = maxX;
    mMaxY = maxY;
    RawSlot<5>(mPartitions.Get(), minX, minY, maxX, maxY);
    ResizeGrids();
}

// 0xd13180: the levels changed, then 0x2b4d1900 posted.
void cWorldDB::SetLevels(int32_t minLevel, int32_t maxLevel)
{
    SetLevelsImpl(minLevel, maxLevel);
    cRZMessage2Standard *const message = NewMessage(0x2b4d1900);
    Post(message);
    ReleaseOf(message);
}

// 0xd13230: the grids' contents shifted for a changed bottom (0xd0d040 with the levels gained below) and top, the levels
// stored and handed to the partitions (slot 6).
void cWorldDB::SetLevelsImpl(int32_t minLevel, int32_t maxLevel)
{
    if (minLevel != mMinLevel) ShiftLevels(this, mMinLevel - minLevel, 0);
    if (maxLevel != mMaxLevel) ShiftLevels(this, maxLevel - mMaxLevel, 1);
    mMinLevel = minLevel;
    mMaxLevel = maxLevel;
    if (mPartitions) RawSlot<6>(mPartitions.Get(), minLevel, maxLevel);
}

namespace {
// New elements take the tile type's default (the game's resizers 0xd12260, 0xd14650, 0x3c07f0, 0xd122e0, 0x81e070,
// 0xd14550, 0xd145e0, 0xd121e0 fill with it): patterns 0, rotations 0x1111, visibility 1 per segment, deck markers and
// awnings -1.
template <class T> void Resize(c3DArray<T> *grid, int32_t sizeX, int32_t sizeY, int32_t sizeZ, const T &fill = T())
{
    grid->mData.resize(static_cast<size_t>(static_cast<uint32_t>(sizeX * sizeY * sizeZ)), fill);
    grid->mSizeX = sizeX;
    grid->mSizeY = sizeY;
    grid->mSizeZ = sizeZ;
}
} // namespace

// 0xd11e80 (377 B): the terrain sized (slot 9) and every grid resized to the lot (the elevations one more each way, the
// deck markers one level fewer), then the partitions' slot 13.
void cWorldDB::ResizeGrids()
{
    const int32_t width = GetWidth();
    const int32_t height = GetHeight();
    const int32_t levels = GetLevelCount();
    RawSlot<9>(mTerrain.Get(), width, height);
    Resize(mPatterns.Get(), width, height, levels);
    constexpr FloorRotationTile kNoRotation = 0x1111;
    Resize(mRotations.Get(), width, height, levels, kNoRotation);
    Resize(mPatternsB.Get(), width, height, levels);
    Resize(mRotationsB.Get(), width, height, levels, kNoRotation);
    Resize(mElevations.Get(), width + 1, height + 1, levels);
    Resize(mVisibility.Get(), width, height, levels, FloorVisibilityTile{{1, 1, 1, 1}});
    Resize(mBytes.Get(), width, height, levels);
    Resize(mObjects.Get(), width, height, levels);
    Resize(mDeckFoundations.Get(), width, height, levels - 1, DeckFoundationTile{{~0u, ~0u, ~0u, ~0u}});
    Resize(mAwnings.Get(), width, height, levels, AwningTile{~0u, ~0u});
    RawSlot<13>(mPartitions.Get());
}

// 0xd12360 (452 B): the database and its grids saved through the persist system (slot 72: object, writer, type), the
// derived data rebuilt, the objects (unless skipped), the pattern table and the second layer, the framework's objects (slot
// 5, the partitions slot 7), the rotations, then a proxy manager made for the save (slots 3, 7, 5, 4) and released.
bool cWorldDB::Save(void *writer, bool skipObjects)
{
    void *const persist = PersistSystem();
    auto save = [&](const void *object, uint32_t type) { As<ifaces::cITSPersistSystem>(persist)->WriteResourceToFileRef(As<ifaces::cIGZSerializable>(object), As<ifaces::cITSObjectFileRef>(writer), type); };
    save(static_cast<cIWorldDB *>(this), 0); // the database by its world face
    save(mPatterns.Get(), 0);
    save(mElevations.Get(), 1);
    save(mVisibility.Get(), 0x15);
    save(mBytes.Get(), 3);
    RebuildDerived(this);
    if (!skipObjects) {
        save(mObjects.Get(), 0xc);
        save(mObjectIndex.Get(), 0x10);
    }
    save(mPatternTable.Get(), 0xe);
    save(mPatternsB.Get(), 0x5d01);
    save(mDeckFoundations.Get(), 0x14);
    save(mAwnings.Get(), 0x1b);
    RawSlot<5>(mTerrain.Get(), writer);
    RawSlot<5>(mRoofs.Get(), writer);
    RawSlot<5>(mPools.Get(), writer);
    RawSlot<5>(mCurvedPools.Get(), writer);
    RawSlot<7>(mPartitions.Get(), writer);
    save(mRotations.Get(), 0x20);
    save(mRotationsB.Get(), 0x5d02);
    void *const director = GetDirectorFrameworkObject();
    RawSlot<3, bool>(director, 0x8ce45d10u, 0x8ce45d17u, mProxies.Out());
    if (mProxies) {
        RawSlot<3>(mProxies.Get());
        RawSlot<7>(mProxies.Get(), static_cast<cIWorldDB *>(this));
        RawSlot<5>(mProxies.Get(), writer);
        RawSlot<4>(mProxies.Get());
        mProxies.Clear();
    }
    return true;
}

// 0xd0fb30 (4617 B) and 0xd0ef80 (1689 B): still game code, called on the cIWorldDB face.
uint32_t cWorldDB::Load(uint32_t a, uint32_t b)
{
    return OriginalFunction<uint32_t(__thiscall *)(cIWorldDB *, uint32_t, uint32_t), 0xd0fb30>()(World(), a, b);
}
uint32_t cWorldDB::Create(uint32_t a, uint32_t b, uint32_t c)
{
    return OriginalFunction<uint32_t(__thiscall *)(cIWorldDB *, uint32_t, uint32_t, uint32_t), 0xd0ef80>()(World(), a, b, c);
}

// 0xd138b0: posting stopped, then shut down and initialised again.
bool cWorldDB::Reset()
{
    mPostMessages = false;
    Shutdown();
    Init();
    return true;
}

// 0xd0eea0 (140 B): when the lot's object (the cached singleton's slot 6, slot 57 for `a`, slot 4) answers slot 24 with
// 0x80, a 0x50-byte helper (0x81e800 for the database by its world face and `a`) runs (0x81ea20) and is destroyed (0x801530).
void cWorldDB::Slot12(uint32_t a)
{
    void *const holder = RawSlot<6, void *>(GetCachedSingleton());
    void *const lot = RawSlot<4, void *>(RawSlot<57, void *>(holder, a));
    if (!RawSlot<24, bool>(lot, 0x80u)) return;
    alignas(4) uint8_t helper[0x50];
    cIWorldDB *const face = this;
    OriginalFunction<void *(__thiscall *)(void *, cIWorldDB *, uint32_t), 0x81e800>()(helper, face, a);
    OriginalFunction<void(__thiscall *)(void *), 0x81ea20>()(helper);
    OriginalFunction<void(__thiscall *)(void *), 0x801530>()(helper);
}

// ---- floors ----

namespace {
template <class T> T &CellOf(const cWorldDB *world, c3DArray<T> *grid, const WorldTile *tile)
{
    return grid->At(tile->x - world->mMinX, tile->y - world->mMinY, tile->level - world->mMinLevel);
}
} // namespace

// 0xd12c70 (378 B): nothing for the same name; else 0x2b4cf327 (with the change) posted, the pattern set, 0xec2a657e posted.
void cWorldDB::SetFloorPattern(const WorldName *name, const WorldTile *tile)
{
    if (SameName(name->text, GetFloorPatternName(tile).text)) return;
    cRZMessage2Standard *message = nullptr;
    if (mPostMessages) {
        message = NewMessage(0x2b4cf327);
        uint32_t *const record = NewRecord(0x1c, kPatternChangeVTable);
        record[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name->text));
        record[3] = static_cast<uint32_t>(tile->x);
        record[4] = static_cast<uint32_t>(tile->y);
        record[5] = static_cast<uint32_t>(tile->level);
        reinterpret_cast<uint8_t *>(record)[0x18] = static_cast<uint8_t>(tile->segments);
        PostWithRecord(message, record);
    }
    SetPatternByNameImpl(name, tile);
    if (mPostMessages) {
        As<ifaces::cIGZMessage2Standard>(message)->SetType(0xec2a657eu);
        Post(message);
    }
    ReleaseOf(message);
}

// 0xd12630 (378 B): the same for the second layer (0xd4c00c9b, 0xf4c00cd3).
void cWorldDB::SetFloorPatternB(const WorldName *name, const WorldTile *tile)
{
    if (SameName(name->text, GetFloorPatternNameB(tile).text)) return;
    cRZMessage2Standard *message = nullptr;
    if (mPostMessages) {
        message = NewMessage(0xd4c00c9b);
        uint32_t *const record = NewRecord(0x1c, kPatternChangeVTable);
        record[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name->text));
        record[3] = static_cast<uint32_t>(tile->x);
        record[4] = static_cast<uint32_t>(tile->y);
        record[5] = static_cast<uint32_t>(tile->level);
        reinterpret_cast<uint8_t *>(record)[0x18] = static_cast<uint8_t>(tile->segments);
        PostWithRecord(message, record);
    }
    SetPatternByNameImplB(name, tile);
    if (mPostMessages) {
        As<ifaces::cIGZMessage2Standard>(message)->SetType(0xf4c00cd3u);
        Post(message);
    }
    ReleaseOf(message);
}

namespace {
// The rotation of the masked segments set (the value not masked: it may spill into the next nibbles, as in the original).
void SetRotation(uint16_t &cell, uint32_t rotation, int8_t segments)
{
    uint32_t value = cell;
    for (int i = 0; i < 4; ++i) {
        if (segments & (1 << i)) value = (value & kRotationKeep[i]) | (rotation << (4 * i));
    }
    cell = static_cast<uint16_t>(value);
}
// The masked segments' rotation: 1 when they differ, else theirs (1 for 0).
uint32_t RotationOf(uint16_t cell, int8_t segments)
{
    uint16_t found = 0;
    for (int i = 0; i < 4; ++i) {
        if (!(segments & (1 << i))) continue;
        const uint16_t value = static_cast<uint16_t>((static_cast<uint32_t>(~kRotationKeep[i] & 0xffff) & cell) >> (4 * i) & 0xf);
        if (found && found != value) return 1;
        found = value;
    }
    return found ? found : 1;
}
} // namespace

// 0xd12e80 (206 B)
void cWorldDB::SetFloorRotation(const uint16_t *rotation, const WorldTile *tile)
{
    const uint16_t value = *rotation;
    if (value == static_cast<uint16_t>(GetFloorRotation(tile))) return;
    SetRotation(CellOf(this, mRotations.Get(), tile), value, tile->segments);
}

// 0xd12830 (209 B)
void cWorldDB::SetFloorRotationB(const uint16_t *rotation, const WorldTile *tile)
{
    const uint16_t value = *rotation;
    if (value == static_cast<uint16_t>(GetFloorRotationB(tile))) return;
    SetRotation(CellOf(this, mRotationsB.Get(), tile), value, tile->segments);
}

// 0xd0ef50: the pattern set on a tile of the lot, without messages.
void cWorldDB::SetFloorPatternQuiet(const WorldName *name, const WorldTile *tile)
{
    if (IsInside(tile)) SetPatternByNameImpl(name, tile);
}

// 0xd12df0 (124 B): the name's uses counted (the old one's dropped) and its id stored in the masked segments.
void cWorldDB::SetPatternByNameImpl(const WorldName *name, const WorldTile *tile)
{
    if (SameName(name->text, GetFloorPatternName(tile).text)) return;
    const WorldName old = GetFloorPatternName(tile);
    UsePatternName(&old, name, static_cast<uint8_t>(tile->segments));
    SetPatternId(PatternId(name), tile);
}

// 0xd127b0 (124 B): the same for the second layer.
void cWorldDB::SetPatternByNameImplB(const WorldName *name, const WorldTile *tile)
{
    if (SameName(name->text, GetFloorPatternNameB(tile).text)) return;
    const WorldName old = GetFloorPatternNameB(tile);
    UsePatternName(&old, name, static_cast<uint8_t>(tile->segments));
    SetPatternIdB(PatternId(name), tile);
}

// 0xd12e70: jmp 0xd12df0.
void cWorldDB::SetPatternByName(const WorldName *name, const WorldTile *tile) { SetPatternByNameImpl(name, tile); }

// 0xd138d0 (390 B): once per masked segment, the new name counted (a new one gets the table's next id, slot 9) and the old
// one's count dropped (at zero it leaves both maps).
void cWorldDB::UsePatternName(const WorldName *oldName, const WorldName *newName, uint8_t segments)
{
    int32_t count = (segments & 1) + ((segments >> 1) & 1) + ((segments >> 2) & 1) + ((segments >> 3) & 1);
    for (; count > 0; --count) {
        if (const char *const text = newName->text) {
            PatternTable *const table = mPatternTable.Get();
            const auto found = table->mUses.find(text);
            if (found != table->mUses.end()) {
                ++found->second.count;
            } else {
                const uint16_t id = table->NextId();
                table->mUses[text].id = id;
                table->mUses[text].count = 1;
                table->mNames[id] = text;
            }
        }
        if (const char *const text = oldName->text) {
            PatternTable *const table = mPatternTable.Get();
            const auto found = table->mUses.find(text);
            if (found != table->mUses.end() && --found->second.count == 0) {
                const uint16_t id = found->second.id;
                // The original erases mNames' head when the id is missing (never: the maps are kept in step).
                const auto name = table->mNames.find(id);
                table->mUses.erase(found);
                if (name != table->mNames.end()) table->mNames.erase(name);
            }
        }
    }
}

// 0xd0df10: the table's name for the id (slot 8).
WorldName cWorldDB::PatternName(const uint32_t *id)
{
    // Slot 8 takes the id by address and reads its low half.
    return WorldName{mPatternTable->NameOf(reinterpret_cast<const uint16_t *>(id))};
}

// 0xd0e1d0: the table's id for the name (slot 7), 0 for none.
uint16_t cWorldDB::PatternId(const WorldName *name)
{
    const uint16_t id = mPatternTable->IdOf(name->text);
    return id == 0xffff ? 0 : id;
}

// Primary slots 13 / 17 (0xd12f50, 0xd12910): the id stored in the masked segments.
void cWorldDB::SetPatternId(uint16_t id, const WorldTile *tile)
{
    FloorPatternTile &cell = CellOf(this, mPatterns.Get(), tile);
    for (int i = 0; i < 4; ++i)
        if (tile->segments & (1 << i)) cell.segment[i] = id;
}
void cWorldDB::SetPatternIdB(uint16_t id, const WorldTile *tile)
{
    FloorPatternTile &cell = CellOf(this, mPatternsB.Get(), tile);
    for (int i = 0; i < 4; ++i)
        if (tile->segments & (1 << i)) cell.segment[i] = id;
}

namespace {
uint16_t PatternIdOf(const FloorPatternTile &cell, int8_t segments)
{
    uint16_t found = 0xffff;
    for (int i = 0; i < 4; ++i) {
        if (!(segments & (1 << i))) continue;
        if (found != 0xffff && found != cell.segment[i]) return 0xffff;
        found = cell.segment[i];
    }
    return found;
}
} // namespace

// Primary slots 18 / 19 (0xd0e130, 0xd0d9c0): the masked segments' id, 0xffff when they differ (or none is masked).
uint16_t cWorldDB::GetPatternId(const WorldTile *tile) { return PatternIdOf(CellOf(this, mPatterns.Get(), tile), tile->segments); }
uint16_t cWorldDB::GetPatternIdB(const WorldTile *tile)
{
    return PatternIdOf(CellOf(this, mPatternsB.Get(), tile), tile->segments);
}

// 0xd0dee0 / 0xd0d8c0: the name of the masked segments' id.
WorldName cWorldDB::GetFloorPatternName(const WorldTile *tile)
{
    const uint32_t id = GetPatternId(tile);
    return PatternName(&id);
}
WorldName cWorldDB::GetFloorPatternNameB(const WorldTile *tile)
{
    const uint32_t id = GetPatternIdB(tile);
    return PatternName(&id);
}

// 0xd0df30 / 0xd0d8f0 (200 B)
uint32_t cWorldDB::GetFloorRotation(const WorldTile *tile) { return RotationOf(CellOf(this, mRotations.Get(), tile), tile->segments); }
uint32_t cWorldDB::GetFloorRotationB(const WorldTile *tile)
{
    return RotationOf(CellOf(this, mRotationsB.Get(), tile), tile->segments);
}

// 0xd12a50 (383 B): on the lot's corners, 0x0b4cf016 (with the change) posted, the elevation stored, 0xab4cf0b5 sent and,
// on the ground level, the terrain change posted.
void cWorldDB::SetFloorElevation(float elevation, const WorldTile *tile)
{
    if (!IsInsideCorners(tile)) return;
    void *const server = MessageServer();
    cRZMessage2Standard *message = nullptr;
    if (mPostMessages) {
        message = NewMessage(0x0b4cf016);
        uint32_t *const record = NewRecord(0x18, kElevationChangeVTable);
        record[2] = Bits(elevation);
        record[3] = static_cast<uint32_t>(tile->x);
        record[4] = static_cast<uint32_t>(tile->y);
        record[5] = static_cast<uint32_t>(tile->level);
        RawSlot<1>(record);
        As<ifaces::cIGZMessage2Standard>(message)->SetObject(record);
        As<ifaces::cIGZMessageServer>(server)->MessageSend(message);
        ReleaseOf(record);
    }
    SetElevationImpl(elevation, tile);
    if (mPostMessages) {
        As<ifaces::cIGZMessage2Standard>(message)->SetType(0xab4cf0b5u);
        As<ifaces::cIGZMessageServer>(server)->MessagePost(message, 0u);
        if (tile->level == 0) PostTerrainElevation(elevation, tile);
    }
    ReleaseOf(message);
}

// 0xd0ef30
void cWorldDB::SetFloorElevationQuiet(float elevation, const WorldTile *tile) { SetElevationImpl(elevation, tile); }

// 0xd12bd0 (125 B)
void cWorldDB::SetElevationImpl(float elevation, const WorldTile *tile)
{
    if (IsInsideCorners(tile)) CellOf(this, mElevations.Get(), tile) = elevation;
}

// Primary slot 15 (0xd12c50).
void cWorldDB::SetElevationRaw(float elevation, const WorldTile *tile) { SetElevationImpl(elevation, tile); }

// 0xd11150 (258 B): 0xcb6afc7c posted with the terrain change (x, y, elevation).
void cWorldDB::PostTerrainElevation(float elevation, const WorldTile *tile)
{
    cRZMessage2Standard *const message = NewMessage(0xcb6afc7c);
    uint32_t *const record = NewRecord(0x14, kTerrainChangeVTable);
    record[2] = static_cast<uint32_t>(tile->x);
    record[3] = static_cast<uint32_t>(tile->y);
    record[4] = Bits(elevation);
    PostWithRecord(message, record);
    ReleaseOf(message);
}

// 0xd12fe0 (379 B): on the lot, the masked segments' visibility set, then 0x2b4cf334 (with the change) posted.
void cWorldDB::SetFloorVisibility(uint8_t visible, const WorldTile *tile)
{
    if (!IsInside(tile)) return;
    FloorVisibilityTile &cell = CellOf(this, mVisibility.Get(), tile);
    FloorVisibilityTile value = cell;
    for (int i = 0; i < 4; ++i)
        if (tile->segments & (1 << i)) value.segment[i] = visible;
    cell = value;
    if (!mPostMessages) return;
    cRZMessage2Standard *const message = NewMessage(0x2b4cf334);
    uint32_t *const record = NewRecord(0x18, kVisibilityChangeVTable);
    record[2] = static_cast<uint32_t>(tile->x);
    record[3] = static_cast<uint32_t>(tile->y);
    record[4] = static_cast<uint32_t>(tile->level);
    reinterpret_cast<uint8_t *>(record)[0x14] = static_cast<uint8_t>(tile->segments);
    PostWithRecord(message, record);
    ReleaseOf(message);
}

// ---- objects on tiles ----

// 0xd0d3f0: jmp 0xd0d400.
void cWorldDB::AddObjectRects(int32_t id, const std::vector<WorldRect> *rects, int32_t level)
{
    AddObjectRectsImpl(id, rects, level);
}

// 0xd0d400 (927 B): the object's old cells dropped, then every tile its rectangles cover on the level (clipped to the lot,
// one past the far edges) added to its cells, and the object (negated: a footprint) listed on each of its cells. IsInside
// gets the cell relative to the origin, as in the original.
void cWorldDB::AddObjectRectsImpl(int32_t id, const std::vector<WorldRect> *rects, int32_t level)
{
    ObjectCellSet &cells = mObjectCells[id];
    if (!cells.empty()) RemoveObject(id);
    if (level < mMinLevel || level > mMaxLevel) return;
    for (const WorldRect &rect : *rects) {
        const int32_t x0 = std::max(mMinX, FloorTile(rect.x0));
        const int32_t x1 = std::min(FloorTile(rect.x1), mMaxX + 1);
        const int32_t y0 = std::max(mMinY, FloorTile(rect.y0));
        const int32_t y1 = std::min(FloorTile(rect.y1), mMaxY + 1);
        for (int32_t y = y0; y <= y1; ++y) {
            for (int32_t x = x0; x <= x1; ++x) cells.insert(WorldCell{x - mMinX, y - mMinY, level - mMinLevel});
        }
    }
    for (const WorldCell &cell : cells) {
        if (IsInside(reinterpret_cast<const WorldTile *>(&cell))) mObjects->At(cell.x, cell.y, cell.level).push_back(-id);
    }
}

// 0xd0d320: jmp 0xd0d330.
void cWorldDB::AddObjectAt(int32_t id, const WorldPoint *point) { AddObjectAtImpl(id, point); }

// 0xd0d330 (152 B): the object listed on the point's tile.
void cWorldDB::AddObjectAtImpl(int32_t id, const WorldPoint *point)
{
    WorldCell cell;
    TileOf(&cell, point);
    if (!IsInside(reinterpret_cast<const WorldTile *>(&cell))) return;
    mObjects->At(cell.x - mMinX, cell.y - mMinY, cell.level - mMinLevel).push_back(id);
}

// 0xd120c0 (278 B): the object's footprint taken off each of its cells (the first entry), its cells cleared (the entry
// stays).
void cWorldDB::RemoveObject(int32_t id)
{
    ObjectCellSet &cells = mObjectCells[id];
    if (cells.empty()) return;
    for (const WorldCell &cell : cells) {
        if (!IsInside(reinterpret_cast<const WorldTile *>(&cell))) continue;
        ObjectList &list = mObjects->At(cell.x, cell.y, cell.level);
        const auto found = std::find(list.begin(), list.end(), -id);
        if (found != list.end()) list.erase(found);
    }
    cells.clear();
}

// 0xd0e2d0 (225 B): the object's cells copied out; whether there are any.
bool cWorldDB::GetObjectCells(int32_t id, WorldCellList *cells)
{
    cells->clear();
    const auto found = mObjectCells.find(id);
    if (found != mObjectCells.end()) {
        for (const WorldCell &cell : found->second) cells->push_back(cell);
    }
    return !cells->empty();
}

// 0xd12000 (157 B): the object taken off the point's tile (the first entry).
void cWorldDB::RemoveObjectAt(int32_t id, const WorldPoint *point)
{
    WorldCell cell;
    TileOf(&cell, point);
    if (!IsInside(reinterpret_cast<const WorldTile *>(&cell))) return;
    ObjectList &list = mObjects->At(cell.x - mMinX, cell.y - mMinY, cell.level - mMinLevel);
    const auto found = std::find(list.begin(), list.end(), id);
    if (found != list.end()) list.erase(found);
}

// 0xd0d3d0 / 0xd120a0: the object index's Add / Remove, the arguments swapped.
void cWorldDB::SetObjectVertex(int32_t id, const LayerVertex *vertex) { mObjectIndex->Add(*vertex, id); }
void cWorldDB::RemoveObjectVertex(int32_t id, const LayerVertex *vertex) { mObjectIndex->Remove(*vertex, id); }

// 0xd10fa0 (110 B)
int32_t cWorldDB::CountObjects(const WorldTile *tile)
{
    if (!IsInside(tile)) return 0;
    return static_cast<int32_t>(CellOf(this, mObjects.Get(), tile).size());
}

// 0xd0e620 (143 B): the object's index on the tile (footprints only when asked for), -1 when it is not there. No bounds
// check.
int32_t cWorldDB::FindObject(int32_t id, const WorldTile *tile, bool footprints)
{
    const ObjectList &list = CellOf(this, mObjects.Get(), tile);
    const int32_t count = static_cast<int32_t>(list.size());
    for (int32_t i = 0; i < count; ++i) {
        int32_t value = list[i];
        if (value < 0) {
            if (!footprints) continue;
            value = -value;
        }
        if (value == id) return i;
    }
    return -1;
}

// 0xd110a0 (127 B): the object at `index` (a footprint's id only when asked for), 0 out of range.
int32_t cWorldDB::GetObjectAt(int32_t index, const WorldTile *tile, bool footprints)
{
    if (index < 0 || index >= CountObjects(tile)) return 0;
    const int32_t value = CellOf(this, mObjects.Get(), tile)[index];
    if (value >= 0) return value;
    return footprints ? -value : 0;
}

// 0xd11120: the object index's Contains / ValueOf, -1 when the vertex has no id.
int32_t cWorldDB::ObjectAtVertex(const LayerVertex *vertex)
{
    if (!mObjectIndex->Contains(*vertex)) return -1;
    return mObjectIndex->ValueOf(*vertex);
}

// ---- other tile grids ----

// 0xd13280 (jmp 0xd13290) / primary slot 16 (0xd13300, jmp 0xd13290).
void cWorldDB::SetByteTile(uint8_t value, const WorldTile *tile) { SetByteTileImpl(value, tile); }
void cWorldDB::SetByteTileRaw(uint8_t value, const WorldTile *tile) { SetByteTileImpl(value, tile); }

// 0xd13290 (97 B)
void cWorldDB::SetByteTileImpl(uint8_t value, const WorldTile *tile)
{
    if (IsInside(tile)) CellOf(this, mBytes.Get(), tile) = value;
}

// 0xd0f7a0 (75 B): no bounds check.
bool cWorldDB::GetByteTile(const WorldTile *tile) { return CellOf(this, mBytes.Get(), tile) != 0; }

// 0xd129a0 (172 B): the masked segments' deck marker; no bounds check.
void cWorldDB::SetDeckFoundation(const uint32_t *value, const WorldTile *tile)
{
    DeckFoundationTile &cell = CellOf(this, mDeckFoundations.Get(), tile);
    DeckFoundationTile copy = cell;
    for (int i = 0; i < 4; ++i)
        if (tile->segments & (1 << i)) copy.segment[i] = *value;
    cell = copy;
}

// 0xd0da60 (178 B): below the top level and on the lot, the masked segments' marker (-1 when they differ).
uint32_t cWorldDB::GetDeckFoundation(const WorldTile *tile)
{
    if (tile->level >= GetMaxLevel() || !IsInside(tile)) return 0xffffffff;
    const DeckFoundationTile cell = CellOf(this, mDeckFoundations.Get(), tile);
    uint32_t found = 0xffffffff;
    for (int i = 0; i < 4; ++i) {
        if (!(tile->segments & (1 << i))) continue;
        if (found != 0xffffffff && found != cell.segment[i]) return 0xffffffff;
        found = cell.segment[i];
    }
    return found;
}

// 0xd12530 (119 B) / 0xd125b0 (118 B): the awning of a tile of the lot (the second word -1 for the first form).
void cWorldDB::SetAwning(const uint32_t *value, const WorldTile *tile)
{
    if (tile->level > GetMaxLevel() || !IsInside(tile)) return;
    CellOf(this, mAwnings.Get(), tile) = AwningTile{*value, 0xffffffff};
}
void cWorldDB::SetAwningPair(const AwningTile *value, const WorldTile *tile)
{
    if (tile->level > GetMaxLevel() || !IsInside(tile)) return;
    CellOf(this, mAwnings.Get(), tile) = *value;
}

// 0xd0d7a0 (122 B) / 0xd0d820 (149 B): -1 off the lot or above the grid.
uint32_t cWorldDB::GetAwning(const WorldTile *tile)
{
    if (tile->level > GetMaxLevel() || !IsInside(tile)) return 0xffffffff;
    const int32_t level = tile->level - mMinLevel;
    AwningGrid *const grid = mAwnings.Get();
    if (level >= grid->mSizeZ) return 0xffffffff;
    return grid->At(tile->x - mMinX, tile->y - mMinY, level).first;
}
WorldPair cWorldDB::GetAwningPair(const WorldTile *tile)
{
    if (tile->level > GetMaxLevel() || !IsInside(tile)) return WorldPair{0xffffffff, 0xffffffff};
    const int32_t level = tile->level - mMinLevel;
    AwningGrid *const grid = mAwnings.Get();
    if (level >= grid->mSizeZ) return WorldPair{0xffffffff, 0xffffffff};
    const AwningTile &cell = grid->At(tile->x - mMinX, tile->y - mMinY, level);
    return WorldPair{cell.first, cell.second};
}

// 0xd13160: at least 4.
void cWorldDB::SetMinGrade(int32_t value) { mMinGrade = std::max(value, 4); }

// 0xd13350 (99 B): a non-empty name; "clear" (slot 10) drops it.
void cWorldDB::SetName(cIGZString *name)
{
    if (name->Strlen() == 0) return;
    if (name->IsEqual(GameAddress<const char>(0xdcb2ec), false)) {
        mName.s.mString.clear();
        mHasName = false;
        return;
    }
    As<cIGZString>(&mName.s)->FromChar(name->ToChar());
    mHasName = true;
}

bool cWorldDB::PostsMessages() { return mPostMessages; }
void *cWorldDB::GetTerrain() { return mTerrain.Get(); }
void *cWorldDB::GetRoofs() { return mRoofs.Get(); }
void *cWorldDB::GetPools() { return mPools.Get(); }
void *cWorldDB::GetCurvedPools() { return mCurvedPools.Get(); }
void *cWorldDB::GetPartitions() { return mPartitions.Get(); }
void *cWorldDB::GetOutsideSupport() { return mOutsideSupport.Get(); }

// 0xd10f50: the name of id 0.
WorldName cWorldDB::GetEmptyPatternName()
{
    const uint32_t id = 0;
    return PatternName(&id);
}

// 0xd0db30 (59 B): the default floor's name (the cached singleton's slot 16, slot 43 for 1, slot 7), null without one.
WorldName cWorldDB::GetDefaultPatternName()
{
    void *const catalog = RawSlot<16, void *>(GetCachedSingleton());
    void *const entry = RawSlot<43, void *>(catalog, 1u);
    return WorldName{entry ? RawSlot<7, const char *>(entry) : nullptr};
}

// 0xd0db20: the game's 0xebb730 written to *target.
void *cWorldDB::StoreVTable(void *target)
{
    *static_cast<const void **>(target) = GameAddress<void>(kStoredVTable);
    return target; // eax at the original's ret: the pointer it stored through
}

// 0xd0e000 (289 B): every name the pattern table counts, added to `names`.
void cWorldDB::GetPatternNames(PatternNameSet *names)
{
    if (!names) return;
    for (const auto &use : mPatternTable->mUses) names->insert(use.first);
}

// 0xd0f630 (85 B)
bool cWorldDB::HasPattern(const WorldName *name)
{
    const PatternTable *const table = mPatternTable.Get();
    return table->mUses.find(name->text) != table->mUses.end();
}

// 0xd0e200 (188 B): bit i for each masked segment that is visible.
bool cWorldDB::GetFloorVisibility(const WorldTile *tile, uint8_t *visible)
{
    if (!IsInside(tile)) return false;
    const FloorVisibilityTile cell = CellOf(this, mVisibility.Get(), tile);
    uint8_t found = 0;
    for (int i = 0; i < 4; ++i)
        if ((tile->segments & (1 << i)) && cell.segment[i]) found |= static_cast<uint8_t>(1 << i);
    if (visible) *visible = found;
    return found != 0;
}

// 0xd0e4f0 (256 B): `want` = 1: every masked segment has a floor (and, when asked, is visible); 0: whether none has one
// (false as soon as one does).
bool cWorldDB::FloorSegments(const WorldTile *tile, int8_t mask, bool want, bool checkVisible)
{
    if (!IsInside(tile)) return false;
    const FloorPatternTile cell = CellOf(this, mPatterns.Get(), tile);
    for (int i = 0; i < 4; ++i) {
        if (!(mask & (1 << i))) continue;
        if (want) {
            if (cell.segment[i] == 0) return false;
        } else if (cell.segment[i] != 0) {
            return true;
        }
    }
    if (checkVisible) {
        const FloorVisibilityTile visible = CellOf(this, mVisibility.Get(), tile);
        for (int i = 0; i < 4; ++i)
            if ((mask & (1 << i)) && want && visible.segment[i] == 0) return false;
    }
    return want;
}

bool cWorldDB::HasFloor(const WorldTile *tile, int8_t mask, bool checkVisible) { return FloorSegments(tile, mask, true, checkVisible); }
bool cWorldDB::HasAnyFloor(const WorldTile *tile, int8_t mask, bool checkVisible)
{
    return FloorSegments(tile, mask, false, checkVisible);
}
bool cWorldDB::HasFloorOrGround(const WorldTile *tile, int8_t mask, bool checkVisible)
{
    return tile->level == 0 || FloorSegments(tile, mask, true, checkVisible);
}
bool cWorldDB::HasAnyFloorOrGround(const WorldTile *tile, int8_t mask, bool checkVisible)
{
    return tile->level == 0 || FloorSegments(tile, mask, false, checkVisible);
}

// 0xd0e470 / 0xd0e3c0 (96 B): the same for the tile above.
bool cWorldDB::HasFloorAbove(const WorldTile *tile, int8_t mask, bool checkVisible)
{
    WorldTile above{};
    above.x = tile->x;
    above.y = tile->y;
    above.level = tile->level + 1;
    if (!IsInside(&above)) return false;
    return FloorSegments(&above, mask, true, checkVisible);
}
bool cWorldDB::HasAnyFloorAbove(const WorldTile *tile, int8_t mask, bool checkVisible)
{
    WorldTile above{};
    above.x = tile->x;
    above.y = tile->y;
    above.level = tile->level + 1;
    if (!IsInside(&above)) return false;
    return FloorSegments(&above, mask, false, checkVisible);
}

// 0xd0fae0 (79 B): the terrain's slot 13 for a ground corner of the lot.
bool cWorldDB::TerrainSlot13(const WorldTile *tile)
{
    if (!IsInsideCorners(tile) || tile->level != 0 || !mTerrain || !IsInsideCorners(tile)) return false;
    return RawSlot<13, bool>(mTerrain.Get(), tile->x, tile->y);
}

// 0xd0fa60 (126 B): whether the masked segments' pattern is the empty one.
bool cWorldDB::IsFloorEmpty(const WorldTile *tile, int8_t mask)
{
    WorldTile local{};
    local.x = tile->x;
    local.y = tile->y;
    local.level = tile->level;
    local.segments = mask;
    const char *const name = GetFloorPatternName(&local).text;
    return SameName(name, GetEmptyPatternName().text);
}

// ---- heights ----

namespace {
int32_t Clamp(int32_t value, int32_t low, int32_t high) { return std::min(std::max(value, low), high); }
} // namespace

// 0xd0de50 (132 B): the elevation at the tile's corner; out of range, the corner is clamped against the absolute bounds
// (the original clamps the origin-relative values that way).
float cWorldDB::GetElevation(const WorldTile *tile)
{
    int32_t x = tile->x - mMinX;
    int32_t y = tile->y - mMinY;
    const int32_t level = tile->level - mMinLevel;
    if (x < mMinX || y < mMinY || x > mMaxX + 1 || y > mMaxY + 1) {
        x = Clamp(x, mMinX, mMaxX + 1);
        y = Clamp(y, mMinY, mMaxY + 1);
    }
    if (!mElevations) return 0.0f;
    return mElevations->At(x, y, level);
}

// 0xd13ac0 (106 B): the terrain's height (slot 12) at the clamped corner.
float cWorldDB::GetTerrainHeight(int32_t x, int32_t y)
{
    if (x < mMinX || y < mMinY || x > mMaxX + 1 || y > mMaxY + 1) {
        x = Clamp(x, mMinX, mMaxX + 1);
        y = Clamp(y, mMinY, mMaxY + 1);
    }
    if (!mTerrain) return 0.0f;
    return RawSlot<12, float>(mTerrain.Get(), x, y);
}

// 0xd13700 (121 B): the bottom level's elevation at the clamped corner (indexed as data[(x - sizeX * mMinLevel) * sizeY
// + y], the absolute corner, as in the original).
float cWorldDB::GetGroundElevation(int32_t x, int32_t y)
{
    if (x < mMinX || y < mMinY || x > mMaxX + 1 || y > mMaxY + 1) {
        x = Clamp(x, mMinX, mMaxX + 1);
        y = Clamp(y, mMinY, mMaxY + 1);
    }
    FloatGrid *const grid = mElevations.Get();
    if (!grid) return 0.0f;
    return grid->mData.data()[(x - grid->mSizeX * mMinLevel) * grid->mSizeY + y];
}

// ---- bounds tests ----

// 0xd0f7f0 (88 B)
bool cWorldDB::IsInWorldPoint(const WorldPoint *point)
{
    if (!IsInWorld(point->x, point->y)) return false;
    if (point->level < GetMinLevel()) return false;
    return point->level <= GetMaxLevel();
}

// 0xd0f8f0 (162 B): a lot with area, the corner within it (one past the far edges) and the level in range.
bool cWorldDB::IsInsideCorners(const WorldTile *tile)
{
    if (GetMinX() >= GetMaxX()) return false;
    if (GetMinY() >= GetMaxY()) return false;
    if (tile->x < GetMinX() || tile->x > GetMaxX() + 1) return false;
    if (tile->y < GetMinY() || tile->y > GetMaxY() + 1) return false;
    if (tile->level < GetMinLevel()) return false;
    return tile->level <= GetMaxLevel();
}

// 0xd0f850 (160 B): the same for a tile.
bool cWorldDB::IsInside(const WorldTile *tile)
{
    if (GetMinX() >= GetMaxX()) return false;
    if (GetMinY() >= GetMaxY()) return false;
    if (tile->x < GetMinX() || tile->x > GetMaxX()) return false;
    if (tile->y < GetMinY() || tile->y > GetMaxY()) return false;
    if (tile->level < GetMinLevel()) return false;
    return tile->level <= GetMaxLevel();
}

// 0xd0f9a0 (191 B): a lot with area and the point within it (a NaN passes the comparisons).
bool cWorldDB::IsInWorld(float x, float y)
{
    if (GetMinX() >= GetMaxX()) return false;
    if (GetMinY() >= GetMaxY()) return false;
    if (GetMinXF() > x) return false;
    if (x > GetMaxXF()) return false;
    if (GetMinYF() > y) return false;
    return !(y > GetMaxYF());
}

// 0xd0f690 (107 B): strictly inside the edges, the level in range.
bool cWorldDB::IsInterior(const WorldTile *tile)
{
    if (tile->x <= GetMinX() || tile->x >= GetMaxX()) return false;
    if (tile->y <= GetMinY() || tile->y >= GetMaxY()) return false;
    if (tile->level < GetMinLevel()) return false;
    return tile->level <= GetMaxLevel();
}

// 0xd0f700 (147 B): at least a tile in from every edge (a NaN passes).
bool cWorldDB::IsInteriorPoint(float x, float y)
{
    if (GetMinXF() + 1.0f > x) return false;
    if (x > GetMaxXF() - 1.0f) return false;
    if (GetMinYF() + 1.0f > y) return false;
    return !(y > GetMaxYF() - 1.0f);
}

// ---- getters ----

float cWorldDB::GetMinXF() { return static_cast<float>(GetMinX()) * GetTileSize(); }
float cWorldDB::GetMinYF() { return static_cast<float>(GetMinY()) * GetTileSize(); }
float cWorldDB::GetMaxXF() { return (static_cast<float>(GetMaxX()) + 1.0f) * GetTileSize(); }
float cWorldDB::GetMaxYF() { return (static_cast<float>(GetMaxY()) + 1.0f) * GetTileSize(); }
float cWorldDB::GetWidthF()
{
    const float high = GetMaxXF();
    return high - GetMinXF();
}
float cWorldDB::GetHeightF()
{
    const float high = GetMaxYF();
    return high - GetMinYF();
}
int32_t cWorldDB::GetMinX() { return mMinX; }
int32_t cWorldDB::GetMinY() { return mMinY; }
int32_t cWorldDB::GetMaxX() { return mMaxX; }
int32_t cWorldDB::GetMaxY() { return mMaxY; }
int32_t cWorldDB::GetWidth()
{
    const int32_t low = 1 - GetMinX();
    return GetMaxX() + low;
}
int32_t cWorldDB::GetHeight()
{
    const int32_t low = 1 - GetMinY();
    return GetMaxY() + low;
}
float cWorldDB::GetTileSize() { return 1.0f; }
int32_t cWorldDB::GetMinLevel() { return mMinLevel; }
int32_t cWorldDB::GetMaxLevel() { return mMaxLevel; }
int32_t cWorldDB::GetLevelCount()
{
    const int32_t low = 1 - GetMinLevel();
    return GetMaxLevel() + low;
}
float cWorldDB::GetLevelTop() { return GetLevelHeight() * 150.0f; }
float cWorldDB::GetLevelBottom() { return GetLevelHeight() * -150.0f; }
float cWorldDB::GetMaxGradeHeight()
{
    const float height = GetLevelHeight();
    return static_cast<float>(static_cast<double>(mMaxTerrainGrade)) * height;
}
float cWorldDB::GetLevelHeight() { return mLevelHeight; }
float cWorldDB::GetWallHeight() { return 3.0f; }
float cWorldDB::GetFoundationHeight() { return 0.75f; }
uint8_t cWorldDB::UnevenTilesFloorable() { return mUnevenTilesFloorable; }
int32_t cWorldDB::GetMinGrade() { return mMinGrade; }

// 0xd10d40 (57 B): the name (the string's slots 7 and 5) into `out` (slot 4), when there is a non-empty one.
bool cWorldDB::GetName(cIGZString *out)
{
    if (!mHasName || As<cIGZString>(&mName.s)->Strlen() == 0) return false;
    out->FromChar(As<cIGZString>(&mName.s)->ToChar());
    return true;
}

} // namespace nTSWorld
