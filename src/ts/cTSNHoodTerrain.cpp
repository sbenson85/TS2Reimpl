// cTSNHoodTerrain: persistence, lifetime and the small leaves (lane DF, lane 1 of DD's split). Layout and facts: DD.
#include "rz/RZMessageServer.h"
#include "ts/cTSNHoodTerrain.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZMessageServer.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cIGZSerializable.h"
#include "ifaces/cITSGlobals.h"
#include "ifaces/cITSNHoodTerrain.h"
#include "ifaces/cITSNeighborhood.h"
#include "ifaces/cITSPersistSystem.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace {
constexpr uint32_t kTypesLoadedFlag = 0x10daad4;      // byte: LoadTerrainTypes has run
constexpr uint32_t kMaxTypeIdByte = 0x10daad5;
constexpr uint32_t kTypeListHead = 0x10dab04;         // the sentinel node of the type list
constexpr uint32_t kDefaultTypeName = 0x1013234;      // a std::string global: what Read starts the type name from
constexpr uint32_t kDefaultNormal = 0x10131e0;        // three floats: what GetVertexNormal answers outside the grid
constexpr uint32_t kSegmentIid = 0x656b8f0c;
constexpr uint32_t kCheatQueryA = 0x2a4915ea;         // the cheat service, asked of the service object (slot 5)
constexpr uint32_t kCheatQueryB = 0x4a490e00;
constexpr uint32_t kCheatVTable = 0xdcc280;           // cTerrainTypeChangeCheat
constexpr uint32_t kMessageHeightChanged = 0x6c09445c;
constexpr float kDefaultHeight = 45.0f;
constexpr uint32_t kOldestVersionWithCellSize = 1;
constexpr uint32_t kOldestVersionWithTypeName = 2;
constexpr uint32_t kOldestVersionWithHeights = 3;
constexpr uint32_t kDefaultCells = 0x80;

using TerrainTypeLoader = void(__cdecl *)();
using MessageServerGetter = void *(__cdecl *)();

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

// The type name of Read: a local cRZString built from the default-name global.
struct LocalName {
    cRZString mString;
    LocalName()
    {
        mString.mVTable = GameAddress<void>(cRZString::kVTable);
        new (&mString.mString) std::string(*GameAddress<std::string>(kDefaultTypeName));
        mString.mRefCount = 0;
    }
    ~LocalName() { mString.Destruct(); }
};

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
    void Fill(uint32_t type, uint32_t a, uint32_t b, uint32_t c)
    {
        (*this)->SetType(type);
        (*this)->SetData1(a);
        (*this)->SetData2(b);
        (*this)->SetData3(c);
    }
};

ifaces::cIGZMessageServer *MessageServer()
{
    return As<ifaces::cIGZMessageServer>(GetCachedMessageServer());
}

ifaces::cITSNeighborhood *Neighborhood()
{
    return As<ifaces::cITSNeighborhood>(As<ifaces::cITSGlobals>(GetCachedSingleton())->GetNeighborhood());
}

// Both the cell size and its inverse come from the neighbourhood (slot 50); read as 1.0 / size.
void TakeCellSize(cTSNHoodTerrain *terrain)
{
    const float cell = Neighborhood()->GetTerrainCellSize();
    terrain->mCellSize = cell;
    terrain->mInvCellSize = 1.0f / cell;
}

ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}
} // namespace

// ---- construction and destruction ----

// The constructor inline in 0x4af120. Fields 0x14-0x33, 0x38-0x4f and 0x69-0x6b stay as the allocator left them (the
// original never stores them); the rest are set as the original's stores do.
cTSNHoodTerrain::cTSNHoodTerrain() : mWaterLevel(20.0f), mNormalsValid(0), mTerrainType(nullptr), mHeights(nullptr),
                                     mVertexFlags(nullptr), mMessageIds(), mKey{}
{
    mMessageIds.insert(0xaf);
    mMessageIds.insert(0xb1);
}

// 0x4af120 (cdecl): new 0xc0 (null tested), the constructor, the +8 face.
cITSNHoodTerrain *__cdecl cTSNHoodTerrain::Create()
{
    return new (std::nothrow) cTSNHoodTerrain;
}

// 0x4ad290 (primary slot 3, ret 4). The id set first, then the arrays are released (the pointers kept), then the members:
// the vectors give their blocks back and are left empty (as 0x29b310 / 0x4ad070 do), and cRZUnknown's destructor.
cTSNHoodTerrain::~cTSNHoodTerrain()
{
    mMessageIds.~set();
    if (mVertexFlags) As<cIGZUnknown>(mVertexFlags)->Release();
    if (mHeights) As<cIGZUnknown>(mHeights)->Release();
}

// 0x4b05b0 (primary slot 0). The persist-resource id answers the +0xc face, the terrain and serializable ids the +8 face;
// anything else is the cIGZUnknown test. The message-target face has no id of its own.
bool cTSNHoodTerrain::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == kPersistResourceIid) {
        *out = static_cast<cIGZPersistResource *>(this);
        AddRef();
        return true;
    }
    if (riid == kTerrainIid || riid == 0xe4fda3d4) {
        *out = static_cast<cITSNHoodTerrain *>(this);
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(riid, out);
}

uint32_t cTSNHoodTerrain::AddRef()
{
    return rz::cRZUnknown::AddRef();
}

uint32_t cTSNHoodTerrain::Release()
{
    return rz::cRZUnknown::Release();
}

// ---- persist face ----

// 0x4af5d0
cGZPersistResourceKey cTSNHoodTerrain::GetKey()
{
    cGZPersistResourceKey key;
    std::memcpy(&key, mKey, sizeof(key));
    return key;
}

// 0x4b21d0
bool cTSNHoodTerrain::SetKey(const cGZPersistResourceKey &key)
{
    std::memcpy(mKey, &key, sizeof(key));
    return true;
}

// 0x4b2630
bool cTSNHoodTerrain::SetType(uint32_t type)
{
    mKey[0] = type;
    return true;
}

// ---- helpers shared by Read and Initialize ----

// 0x4b2200 (ret 8): sizes everything that follows the grid. The grid is cells + 1 vertices each way; the world size is the
// cell count times the cell size. Row buffers, paint and normals restart zeroed; every terrain type's weight map is resized
// to the vertex grid and cleared.
void cTSNHoodTerrain::SetSize(uint32_t width, uint32_t height)
{
    mWidth = width;
    mHeight = height;
    mVertsY = height + 1;
    mVertsY2 = height + 1;
    mVertsX = width + 1;
    mVertsX2 = width + 1;
    const uint32_t vertices = mVertsY * mVertsX;
    mWorldSizeX = static_cast<float>(width) * mCellSize;
    mWorldSizeY = static_cast<float>(height) * mCellSize;
    mNormals.clear();
    mNormals.resize(vertices);
    mNormalsValid = 0;
    mFaceRowA.clear();
    mFaceRowA.resize(mWidth * 2);
    mFaceRowB.clear();
    mFaceRowB.resize(mWidth * 2);
    mPaint.clear();
    mPaint.resize(vertices);
    mShoreRows.clear();
    mShoreRows.resize(mVertsY);
    for (uint32_t row = 0; row < mVertsY; ++row) mShoreRows[row].resize(mVertsX);
    mLotRows.clear();
    mLotRows.resize(mVertsY);
    for (uint32_t row = 0; row < mVertsY; ++row) mLotRows[row].resize(mVertsX);
    cTSNHoodTerrainTypeNode *head = *GameAddress<cTSNHoodTerrainTypeNode *>(kTypeListHead);
    for (cTSNHoodTerrainTypeNode *node = head->mNext; node != head; node = node->mNext) {
        node->mWeightMap.Resize(mVertsX, mVertsY);
        std::fill(node->mWeightMap.mData.begin(), node->mWeightMap.mData.end(), uint8_t(0));
    }
}

// 0x4afd90: makes the height array when there is none, sizes it to the vertex grid and fills it with the default height.
void cTSNHoodTerrain::ResetHeights()
{
    if (!mHeights) {
        auto *heights = static_cast<nTSWorld::c2DArray<float> *>(::operator new(sizeof(nTSWorld::c2DArray<float>)));
        heights->mVTable = GameAddress<void>(nTSWorld::c2DArray<float>::kVTable);
        heights->mRefCount = 0;
        heights->mWidth = 0;
        heights->mHeight = 0;
        new (&heights->mData) std::vector<float>();
        nTSWorld::c2DArray<float> *old = mHeights;
        if (heights != old) {
            if (heights) As<cIGZUnknown>(heights)->AddRef();
            mHeights = heights;
            if (old) As<cIGZUnknown>(old)->Release();
        }
    }
    mHeights->Resize(mVertsX, mVertsY);
    std::fill(mHeights->mData.begin(), mHeights->mData.end(), kDefaultHeight);
}

// 0x4aff00: the same for the vertex flags, cleared.
void cTSNHoodTerrain::ResetVertexFlags()
{
    if (!mVertexFlags) {
        auto *flags = static_cast<nTSWorld::c2DArray<uint8_t> *>(::operator new(sizeof(nTSWorld::c2DArray<uint8_t>)));
        flags->mVTable = GameAddress<void>(nTSWorld::c2DArray<uint8_t>::kVTable);
        flags->mRefCount = 0;
        flags->mWidth = 0;
        flags->mHeight = 0;
        new (&flags->mData) std::vector<uint8_t>();
        nTSWorld::c2DArray<uint8_t> *old = mVertexFlags;
        if (flags != old) {
            if (flags) As<cIGZUnknown>(flags)->AddRef();
            mVertexFlags = flags;
            if (old) As<cIGZUnknown>(old)->Release();
        }
    }
    mVertexFlags->Resize(mVertsX, mVertsY);
    std::fill(mVertexFlags->mData.begin(), mVertexFlags->mData.end(), uint8_t(0));
}

// 0x4afcd0: a new terrain-type-change cheat goes to the cheat service (not freed here when there is none), and the
// terrain asks for its two messages.
void cTSNHoodTerrain::RegisterCheatAndMessages()
{
    auto *cheat = static_cast<uint32_t *>(::operator new(0xc));
    if (cheat) {
        cheat[1] = 0;
        cheat[2] = 0;
        *reinterpret_cast<const void **>(cheat) = GameAddress<void>(kCheatVTable);
    }
    void *held = nullptr;
    if (void *service = GetServiceObject()) {
        using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<Query, 5>(service, kCheatQueryA, kCheatQueryB, &held);
    }
    // The local is released when the registration throws (the original's EH state 0); on the normal path it is released
    // explicitly, outside that state.
    struct OnThrow {
        void *&object;
        bool armed = true;
        ~OnThrow() noexcept(false)
        {
            if (armed) ReleaseHeldObject(object);
        }
    } onThrow{held};
    if (held) VirtualCall<uint32_t(__thiscall *)(void *, void *), 4>(held, cheat);
    onThrow.armed = false;
    ReleaseHeldObject(held);
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))), nullptr);
}

// 0x4b04f0 (stdcall, ret 0x10): a message of this type with three data words, posted.
void __stdcall cTSNHoodTerrain::PostTerrainMessage(uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    ifaces::cIGZMessageServer *server = MessageServer();
    Message message;
    message.Fill(type, a, b, c);
    server->MessagePost(message.object, 0);
}

// 0x4b1ef0 (stdcall, ret 0x10): the same, sent at once.
void __stdcall cTSNHoodTerrain::SendTerrainMessage(uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    ifaces::cIGZMessageServer *server = MessageServer();
    Message message;
    message.Fill(type, a, b, c);
    server->MessageSend(message.object);
}

// ---- cITSNHoodTerrain / cIGZSerializable face ----

// 0x4af2d0
uint32_t cTSNHoodTerrain::GetGZCLSID() { return cTSNHoodTerrain::kClassId; }

// 0x4b2fb0: class id, version 4, the cell counts, the water level, the type name, then both arrays when they exist.
// ORIGINAL-BUG: the flags array is written but Read never reads it back (it resets the flags instead).
bool cTSNHoodTerrain::Write(ifaces::cIGZOStream *stream)
{
    cTSNHoodTerrain *terrain = this;
    bool ok = true;
    stream->SetUint32(GetGZCLSID());
    stream->SetUint32(cTSNHoodTerrain::kRecordVersion);
    stream->SetUint32(terrain->mWidth);
    stream->SetUint32(terrain->mHeight);
    stream->SetFloat32(terrain->mWaterLevel);
    stream->SetGZStr(const_cast<void *>(terrain->mTerrainType));
    if (terrain->mHeights) ok = As<ifaces::cIGZSerializable>(terrain->mHeights)->Write(stream) & 1;
    if (terrain->mVertexFlags) ok &= As<ifaces::cIGZSerializable>(terrain->mVertexFlags)->Write(stream);
    return !stream->GetError() ? ok : false;
}

// 0x4b0980: the version gates what follows (0: sizes and water level only; 1: the cell size is taken from the
// neighbourhood and the type table is loaded; 2: the type name; 3: the height array, and then the vertex flags, the cheat
// and the messages are set up). It answers true for any stream that had the right class id, errors included.
bool cTSNHoodTerrain::Read(ifaces::cIGZIStream *stream)
{
    cTSNHoodTerrain *terrain = this;
    uint32_t classId; // an uninitialised stack local in the original
    stream->GetUint32(&classId);
    if (classId != GetGZCLSID()) return false;
    uint32_t version; // as above
    stream->GetUint32(&version);
    if (version != 0) {
        if (*GameAddress<uint8_t>(kTypesLoadedFlag) == 0) OriginalFunction<TerrainTypeLoader, 0x4b0c00>()();
        TakeCellSize(terrain);
    }
    uint32_t width = kDefaultCells, height = kDefaultCells;
    stream->GetUint32(&width);
    stream->GetUint32(&height);
    terrain->SetSize(width, height);
    stream->GetFloat32(&terrain->mWaterLevel);
    LocalName name;
    if (version >= kOldestVersionWithTypeName) stream->GetGZStr(&name.mString);
    SetTerrainType(name.mString.AsInterface());
    if (version >= kOldestVersionWithHeights) {
        TakeCellSize(terrain);
        CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
        if (!terrain->mHeights) {
            auto *heights = static_cast<nTSWorld::c2DArray<float> *>(::operator new(sizeof(nTSWorld::c2DArray<float>)));
            heights->mVTable = GameAddress<void>(nTSWorld::c2DArray<float>::kVTable);
            heights->mRefCount = 0;
            heights->mWidth = 0;
            heights->mHeight = 0;
            new (&heights->mData) std::vector<float>();
            nTSWorld::c2DArray<float> *old = terrain->mHeights;
            if (heights != old) {
                if (heights) As<cIGZUnknown>(heights)->AddRef();
                terrain->mHeights = heights;
                if (old) As<cIGZUnknown>(old)->Release();
            }
            if (!As<ifaces::cIGZSerializable>(terrain->mHeights)->Read(stream)) terrain->ResetHeights();
        }
        terrain->ResetVertexFlags();
        terrain->RegisterCheatAndMessages();
    }
    return true;
}

// 0x4afc30: the type table, the cell size, the grid from the neighbourhood (slots 52 and 53), both arrays and the messages.
bool cTSNHoodTerrain::Initialize()
{
    cTSNHoodTerrain *terrain = this;
    if (*GameAddress<uint8_t>(kTypesLoadedFlag) == 0) OriginalFunction<TerrainTypeLoader, 0x4b0c00>()();
    ifaces::cITSNeighborhood *neighborhood = Neighborhood();
    TakeCellSize(terrain);
    const uint32_t width = VirtualCall<uint32_t(__thiscall *)(void *), 52>(neighborhood);
    const uint32_t height = VirtualCall<uint32_t(__thiscall *)(void *), 53>(neighborhood);
    terrain->SetSize(width, height);
    terrain->ResetHeights();
    terrain->ResetVertexFlags();
    terrain->RegisterCheatAndMessages();
    return true;
}

// 0x4b2640: stops the messages, empties the derived rows and drops both arrays.
bool cTSNHoodTerrain::Shutdown()
{
    cTSNHoodTerrain *terrain = this;
    reinterpret_cast<KeyMapWalker *>(&terrain->mMessageIds)->CallSlot6ForEachKey(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(terrain))), nullptr);
    terrain->mShoreRows.clear();
    terrain->mLotRows.clear();
    terrain->mPaint.clear();
    terrain->mFaceRowA.clear();
    terrain->mFaceRowB.clear();
    if (nTSWorld::c2DArray<uint8_t> *flags = terrain->mVertexFlags) {
        terrain->mVertexFlags = nullptr;
        As<cIGZUnknown>(flags)->Release();
    }
    if (nTSWorld::c2DArray<float> *heights = terrain->mHeights) {
        terrain->mHeights = nullptr;
        As<cIGZUnknown>(heights)->Release();
    }
    return true;
}

// 0x4b2540: stores the height when the vertex is inside the cell counts and the value changed; announces it when asked.
void cTSNHoodTerrain::SetHeightAt(uint32_t x, uint32_t y, float height, bool notify)
{
    cTSNHoodTerrain *terrain = this;
    if (x >= terrain->mVertsX || y >= terrain->mVertsY) return;
    nTSWorld::c2DArray<float> *heights = terrain->mHeights;
    float &slot = heights->mData.data()[heights->mWidth * y + x];
    if (slot != height) {
        slot = height;
        if (notify) cTSNHoodTerrain::SendTerrainMessage(kMessageHeightChanged, x, y, 0);
    }
}

// 0x4b1e40: the persist service writes the resource, then the segment that holds it is flushed. True when both worked.
uint8_t cTSNHoodTerrain::Save()
{
    ifaces::cITSPersistSystem *persist = PersistSystem();
    const uint8_t saved = persist->SaveResource(
        As<ifaces::cIGZPersistResource>(AsPersistResource()), nullptr);
    Held segment;
    cGZPersistResourceKey key = static_cast<cIGZPersistResource *>(this)->GetKey();
    const cGZPersistResourceKey *found = &key;
    uint8_t result;
    if (persist->FindSegment(static_cast<const ::cGZPersistResourceKey *>(found), kSegmentIid, &segment.object)) {
        result = saved & As<ifaces::cIGZPersistDBSegment>(segment.object)->Flush();
    } else {
        result = 0;
    }
    return result;
}

// ---- small leaves of the +8 face (this = face; the fields are read through this) ----

// 0x4afc10: no bounds check.
float cTSNHoodTerrain::GetHeightAt(int32_t x, int32_t y)
{
    nTSWorld::c2DArray<float> *heights = this->mHeights;
    return heights->mData.data()[heights->mWidth * y + x];
}

// 0x4b2d50: through the vtable slot of GetHeightAt, as the original calls it.
float cTSNHoodTerrain::GetWaterDepthAt(uint32_t x, uint32_t y)
{
    return this->mWaterLevel - GetHeightAt(x, y);
}

// 0x4b00e0: layer 0 is bit 1, layer 1 is bit 2; no bounds check.
bool cTSNHoodTerrain::GetVertexFlag(const uint32_t *vertex)
{
    nTSWorld::c2DArray<uint8_t> *flags = this->mVertexFlags;
    const uint8_t bit = (vertex[2] != 0) + 1;
    return (flags->mData.data()[flags->mWidth * vertex[1] + vertex[0]] & bit) != 0;
}

// 0x4b0110: inside the cell counts, and the vertex below the water level.
bool cTSNHoodTerrain::IsVertexUnderWater(uint32_t x, uint32_t y)
{
    cTSNHoodTerrain *terrain = this;
    if (x >= terrain->mVertsX || y >= terrain->mVertsY) return false;
    nTSWorld::c2DArray<float> *heights = terrain->mHeights;
    return terrain->mWaterLevel > heights->mData.data()[heights->mWidth * y + x];
}

// 0x4b0030: the cell's four corners are all below the water level.
bool cTSNHoodTerrain::IsCellUnderWater(uint32_t x, uint32_t y)
{
    cTSNHoodTerrain *terrain = this;
    if (x >= terrain->mWidth || y >= terrain->mHeight) return false;
    nTSWorld::c2DArray<float> *heights = terrain->mHeights;
    const float level = terrain->mWaterLevel;
    const float *data = heights->mData.data();
    const uint32_t w = heights->mWidth;
    const uint32_t lower = w * y + x, upper = w * (y + 1) + x;
    if (!(level > data[lower])) return false;
    if (!(level > data[upper])) return false;
    if (!(level > data[upper + 1])) return false;
    return level > data[lower + 1];
}

// 0x4affc0: none of the cell's four corners is below the water level.
bool cTSNHoodTerrain::IsCellAboveWater(uint32_t x, uint32_t y)
{
    cTSNHoodTerrain *terrain = this;
    if (x >= terrain->mWidth || y >= terrain->mHeight) return false;
    nTSWorld::c2DArray<float> *heights = terrain->mHeights;
    const float level = terrain->mWaterLevel;
    const float *data = heights->mData.data();
    const uint32_t w = heights->mWidth;
    const uint32_t lower = w * y + x, upper = w * (y + 1) + x;
    if (level > data[lower]) return false;
    if (level > data[upper]) return false;
    if (level > data[upper + 1]) return false;
    return !(level > data[lower + 1]);
}

// 0x4b00a0: inside the world rectangle (edges included); NaN is outside.
bool cTSNHoodTerrain::IsInWorld(float x, float z)
{
    cTSNHoodTerrain *terrain = this;
    if (x >= 0.0f && z >= 0.0f && terrain->mWorldSizeX >= x && terrain->mWorldSizeY >= z) return true;
    return false;
}

// 0x4ae780: the vertex index of a world position (truncated), not clamped.
void cTSNHoodTerrain::WorldToVertex(float x, float z, int32_t *vx, int32_t *vz)
{
    cTSNHoodTerrain *terrain = this;
    *vx = static_cast<int32_t>(x / terrain->mWorldSizeX * static_cast<float>(terrain->mVertsX));
    *vz = static_cast<int32_t>(z / terrain->mWorldSizeY * static_cast<float>(terrain->mVertsY));
}

// 0x4af700: the vertex's normal, or the default normal outside the vertex grid.
void cTSNHoodTerrain::GetVertexNormal(uint32_t x, uint32_t y, cTSNHoodTerrainVec3 *out)
{
    cTSNHoodTerrain *terrain = this;
    if (x < terrain->mVertsX && y < terrain->mVertsY) {
        *out = terrain->mNormals.data()[terrain->mVertsX * y + x];
    } else {
        *out = *GameAddress<cTSNHoodTerrainVec3>(kDefaultNormal);
    }
}

// 0x4afbf0: false without a type; otherwise the type entry's name is copied out.
bool cTSNHoodTerrain::GetTerrainTypeName(cIGZString *out)
{
    const void *type = this->mTerrainType;
    if (!type) return false;
    out->Copy(*static_cast<const cIGZString *>(type));
    return true;
}

// 0x4b0290
uint8_t cTSNHoodTerrain::GetMaxTypeId() { return *GameAddress<uint8_t>(kMaxTypeIdByte); }

// ---- adapters (the RVA hooks): bodies of this file ----

using Adapters = cTSNHoodTerrainAdapters;

// 0x4ad290: the destructor and, with bit 0, the sized delete (the compiler's scalar deleting destructor, by hand).
Adapters::Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}

cGZPersistResourceKey *__fastcall Adapters::GetKey(cIGZPersistResource *face, void *, cGZPersistResourceKey *out)
{
    [[msvc::forceinline_calls]] *out = static_cast<Self *>(face)->Self::GetKey();
    return out;
}

bool __fastcall Adapters::Write(cITSNHoodTerrain *face, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Write(stream);
}

bool __fastcall Adapters::Read(cITSNHoodTerrain *face, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Read(stream);
}

uint32_t __fastcall Adapters::GetGZCLSID(cITSNHoodTerrain *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetGZCLSID();
}

bool __fastcall Adapters::Initialize(cITSNHoodTerrain *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Initialize();
}

bool __fastcall Adapters::Shutdown(cITSNHoodTerrain *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Shutdown();
}

void __fastcall Adapters::SetHeightAt(cITSNHoodTerrain *face, void *, uint32_t x, uint32_t y, float height, bool notify)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetHeightAt(x, y, height, notify);
}

float __fastcall Adapters::GetHeightAt(cITSNHoodTerrain *face, void *, int32_t x, int32_t y)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetHeightAt(x, y);
}

void __fastcall Adapters::WorldToVertex(cITSNHoodTerrain *face, void *, float x, float z, int32_t *vx, int32_t *vz)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::WorldToVertex(x, z, vx, vz);
}

void __fastcall Adapters::GetVertexNormal(cITSNHoodTerrain *face, void *, uint32_t x, uint32_t y, cTSNHoodTerrainVec3 *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetVertexNormal(x, y, out);
}

float __fastcall Adapters::GetWaterDepthAt(cITSNHoodTerrain *face, void *, uint32_t x, uint32_t y)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetWaterDepthAt(x, y);
}

uint8_t __fastcall Adapters::Save(cITSNHoodTerrain *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Save();
}

bool __fastcall Adapters::GetTerrainTypeName(cITSNHoodTerrain *face, void *, cIGZString *out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetTerrainTypeName(out);
}

bool __fastcall Adapters::GetVertexFlag(cITSNHoodTerrain *face, void *, const uint32_t *vertex)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetVertexFlag(vertex);
}

uint8_t __fastcall Adapters::GetMaxTypeId(cITSNHoodTerrain *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetMaxTypeId();
}

bool __fastcall Adapters::IsVertexUnderWater(cITSNHoodTerrain *face, void *, uint32_t x, uint32_t y)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::IsVertexUnderWater(x, y);
}

bool __fastcall Adapters::IsCellUnderWater(cITSNHoodTerrain *face, void *, uint32_t x, uint32_t y)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::IsCellUnderWater(x, y);
}

bool __fastcall Adapters::IsCellAboveWater(cITSNHoodTerrain *face, void *, uint32_t x, uint32_t y)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::IsCellAboveWater(x, y);
}

bool __fastcall Adapters::IsInWorld(cITSNHoodTerrain *face, void *, float x, float z)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::IsInWorld(x, z);
}

bool __fastcall Adapters::SetKey(cIGZPersistResource *face, void *, const cGZPersistResourceKey &key)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetKey(key);
}

bool __fastcall Adapters::SetType(cIGZPersistResource *face, void *, uint32_t type)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetType(type);
}

void __fastcall Adapters::SetSize(Self *self, void *, uint32_t width, uint32_t height)
{
    [[msvc::forceinline_calls]] return self->Self::SetSize(width, height);
}

void __fastcall Adapters::ResetHeights(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::ResetHeights();
}

void __fastcall Adapters::ResetVertexFlags(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::ResetVertexFlags();
}

void __fastcall Adapters::RegisterCheatAndMessages(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::RegisterCheatAndMessages();
}
