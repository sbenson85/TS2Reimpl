// nTSSG::cLightMapManager (lanes 261108q and 261108t, scenegraph LG6 / LG6b; src/sg/cLightMapManager.h).
//
// Still the original's, called through OriginalFunction: the wall catalog's height test 0x7f2340, the placement services
// 0x20e0b2 and 0x20de97, the scene graph system's getter 0x20defb,
// the app options 0x20df9a, the wall test 0x7e2050 and the MMX blend 0xb3c6c0.
#include "sg/cLightMapManager.h"

#include "compgeom/ConvexMath.h" // X87Pick
#include "game/original.h"
#include "game/owned.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cIGZCOM.h"
#include "gz/cGZMessageServer2.h"
#include "gz/cGZPropertyManager.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "ifaces/cIGZFrameWork.h"
#include "misc/UtilitiesC.h" // KeyMapSlot5Walker
#include "misc/X87.h"
#include "rz/MiscHelpersB.h" // KeyMapWalker
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, GetCachedSingleton, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/StaticGetters2.h"   // GetCachedSlot23
#include "rz/cRZMessage2Standard.h"
#include "sg/SGUtilities.h" // the settings readers 0xaf7110 / 0xaf7190 / 0xaf7210
#include "sg/cTextureImage.h"
#include "ts/cTSGlobals.h"
#include "world/WorldCatalog.h"
#include "world/WorldHeights.h"
#include "world/WorldService.h"
#include "world/WorldTileMath.h"
#include "world/cWorldDB.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>

// lmReserveBorderTiles' value (cAnyBoolean, built by the image's initialiser with true): the default the settings
// service is given, and the value DoMessage keeps.
TS2_OWNED_STATIC(AnyBoolean, ffd8e4);

namespace nTSSG {

namespace {

constexpr uint32_t kSettingsId = 0x0ac1887e, kSettingsIid = 0x6bb22f38;
constexpr uint32_t kHolderIid = 0xab7cc174; // the world-loaded message's object: the command parser's properties
constexpr int32_t kWallKindDefault = 3;     // a wall region's height in wall tiles, unless the face says otherwise
constexpr uint32_t kTallKind = 0x17;        // the partition type that sizes its regions by the face's height
constexpr float kFloorTileUV = 0.015625f;   // a level-0 tile's share of the ground image (1/64)

constexpr FloorTile kNoTile{0xff, 0xff, -1, 0};      // 0xffd8c8
constexpr WallRegion kNoRegion{-1, -1, 0, 1};        // 0xffd8cc / 0xffd8d0

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The settings service (the framework's 0x0ac1887e / 0x6bb22f38), a counted local.
ScopedRef Settings()
{
    ScopedRef settings{nullptr};
    if (auto *const framework = static_cast<ifaces::cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(kSettingsId, kSettingsIid, &settings.mObject);
    return settings;
}

// Round to the nearest integer by the 1.5 * 2^23 trick, as the original writes it: the bits of (v - 0.5) + 2^23 * 1.5,
// then one up when that is v itself. Floors values short of 2^22.
int32_t RoundDown(float v)
{
    const int32_t bits = std::bit_cast<int32_t>((v - 0.5f) + 12582912.0f);
    const int32_t up = bits - 0x4b3fffff;
    return static_cast<float>(up) == v ? up : bits - 0x4b400000;
}

// The same trick upwards (the bits of (v + 0.5) + 1.5 * 2^23, one down when that is v): ceilings short of 2^22.
int32_t RoundUp(float v)
{
    const int32_t bits = std::bit_cast<int32_t>((v + 0.5f) + 12582912.0f);
    const int32_t down = bits - 0x4b400001;
    return static_cast<float>(down) == v ? down : bits - 0x4b400000;
}

// The low byte of a float's bits after adding 1.5 * 2^23 (the integer nearest it, as a byte for values in range).
uint32_t NearBits(float v) { return std::bit_cast<uint32_t>(v + 12582912.0f); }

// EqB's distance between two floats: their bits mapped to a monotonic integer (negative values mirrored), subtracted.
uint32_t Monotonic(uint32_t bits)
{
    const uint32_t sign = static_cast<uint32_t>(static_cast<int32_t>(bits) >> 31);
    return ((sign >> 1) ^ bits) - sign;
}
bool Near(uint32_t a, uint32_t b)
{
    const uint32_t d = Monotonic(a) - Monotonic(b);
    return ((static_cast<uint32_t>(static_cast<int32_t>(d) >> 26) ^ d) & 0xffffffe0u) == 0; // d in [-32, 31]
}

// ---- LG6b's helpers ----

constexpr uint32_t kImageClass = 0x1c4a276c;   // cImageData, made through the COM director as cIImageData
constexpr uint32_t kTextureClass = 0x2a956dad; // cTextureImage, made as cITextureImage
constexpr uint32_t kTransitionClock = 0x0bea28c2; // the clock id Update runs under (GetCachedSlot23's slot 5)
constexpr float kIncidenceFill[4] = {1.0f, 0.5f, 0.5f, 1.0f}; // 0xffd8d4: an incidence image's first colour
constexpr float kRoundBias = 12582912.0f;      // 1.5 * 2^23: a float's low bits are then the integer nearest it
constexpr float kPixelBias = 49152.0f;         // 1.5 * 2^15: the low bits are then 256 times the value, rounded

// An object made by the COM director (null when it fails).
void *NewObject(uint32_t classId, uint32_t iid)
{
    void *object = nullptr;
    auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    com->GetClassObject(classId, iid, &object); // receiver: cIGZCOM (0x145702, ret 12)
    return object;
}

void ReleaseObject(void *object)
{
    if (object) static_cast<cIGZUnknown *>(object)->Release(); // receiver: cIGZUnknown (slot 2)
}

// push_back of a counted pointer: counted in place, or through the growth path (which counts its copy).
void PushCounted(LightMapImages &images, void *object)
{
    if (images.mLast != images.mEnd) {
        *images.mLast = object;
        if (object) static_cast<cIGZUnknown *>(object)->AddRef(); // receiver: cIGZUnknown (slot 1)
        ++images.mLast;
    } else {
        images.EmplaceReallocate(images.mLast, &object);
    }
}

// The light's colour: the scene graph system's lighting manager's current colour.
void LightColour(float *rgb)
{
    void *const system = OriginalFunction<void *(__cdecl *)(), 0x20defb>()();
    void *const lighting = RawSlot<49, void *>(system); // receiver: nTSSG::cTSSGSystem (slot 49, the lighting manager)
    RawSlot<28, float *>(lighting, rgb); // receiver: nTSSG::cLightingManager (vf28 0x2d8310, ret 4, by value)
}

// A colour channel as a byte: 256 times the value rounded (through the bias), clamped to 0..255.
uint32_t ChannelByte(float value, float scale)
{
    const int32_t x = static_cast<int32_t>(std::bit_cast<uint32_t>(value * scale + kPixelBias) + 0xb8c00000u);
    return static_cast<uint32_t>((~(x >> 31) & x) | (static_cast<int32_t>(255u - static_cast<uint32_t>(x)) >> 31)) & 0xffu;
}

uint32_t OpaquePixel(const float *rgb, float scale)
{
    return 0xff000000u | ChannelByte(rgb[0], scale) << 16 | ChannelByte(rgb[1], scale) << 8 | ChannelByte(rgb[2], scale);
}

// The integer nearest a float (bits after the 1.5 * 2^23 bias, unbiased): exact for values short of 2^22.
int32_t NearInt(float v) { return static_cast<int32_t>(std::bit_cast<uint32_t>(v + kRoundBias) + 0xb4c00000u); }

// x87: a register plus a float in memory (fadd dword), rounded as the control word says (cSunHeightMap's).
double AddMem87(double reg, float mem)
{
    if (reg != reg && mem != mem) return x87::Rounded(nGZCompGeom::convex::X87Pick(reg, static_cast<double>(mem)));
    return x87::Rounded(reg + static_cast<double>(mem));
}

int16_t Sat16(int32_t v) { return static_cast<int16_t>(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
int16_t TableWord(const uint8_t *table, uint32_t offset)
{
    int16_t w;
    std::memcpy(&w, table + offset, sizeof w);
    return w;
}

// Each image of a group handed to its texture (the texture's image face, slot 10).
void UploadPairs(LightMapImages &images, std::vector<nGZSceneGraph::cTextureHandle> &textures)
{
    for (uint32_t i = 0; i < images.Size(); ++i) {
        cIGZUnknown *const texture = textures[i].Get();
        nGZSceneGraph::cITextureImage *face = nullptr;
        // ORIGINAL-BUG: neither the handle's texture nor the interface is checked.
        // receiver: cIGZUnknown (the texture manager's texture, slot 0)
        texture->QueryInterface(nGZSceneGraph::cITextureImage::kIid, reinterpret_cast<void **>(&face));
        nGZSceneGraph::cITextureImage *volatile used = face;
        used->SetImage(static_cast<cIGZUnknown *>(images.mFirst[i])); // receiver: nGZSceneGraph::cITextureImage
        if (face) face->Release(); // receiver: nGZSceneGraph::cITextureImage
    }
}

} // namespace

// ---- the maps' functors ---------------------------------------------------------------------------------------------

size_t FloorKeyHash::operator()(const FloorKey &key) const
{
    return (static_cast<uint32_t>(key.level) * 0x1000u + static_cast<uint32_t>(key.y)) * 0x1000u +
           static_cast<uint32_t>(key.x);
}

bool FloorKeyEqual::operator()(const FloorKey &a, const FloorKey &b) const
{
    return a.x == b.x && a.y == b.y && a.level == b.level;
}

size_t WallKeyHash::operator()(const WallKey &key) const
{
    uint32_t h = NearBits(key.y1) - 0x4b400000u;
    h = h * 0x100u + (NearBits(key.x1) & 0xffu);
    h = h * 0x100u + (NearBits(key.y0) & 0xffu);
    return h * 0x100u + (NearBits(key.x0) & 0xffu);
}

bool WallKeyEqual::operator()(const WallKey &a, const WallKey &b) const
{
    return a.word2 == b.word2 && Near(std::bit_cast<uint32_t>(a.x0), std::bit_cast<uint32_t>(b.x0)) &&
           Near(std::bit_cast<uint32_t>(a.y0), std::bit_cast<uint32_t>(b.y0)) && a.word5 == b.word5 &&
           Near(std::bit_cast<uint32_t>(a.x1), std::bit_cast<uint32_t>(b.x1)) &&
           Near(std::bit_cast<uint32_t>(a.y1), std::bit_cast<uint32_t>(b.y1));
}

// ---- life ------------------------------------------------------------------------------------------------------------

cLightMapManager::cLightMapManager() { mMessageIds.insert(kWorldLoaded); }

cLightMapManager::~cLightMapManager()
{
    if (mRef18c) mRef18c->Release(); // receiver: cIGZUnknown (slot 2; nothing in the image sets it)
}

cLightMapManager *__cdecl cLightMapManager::Create() { return new (std::nothrow) cLightMapManager; }

uint32_t __cdecl cLightMapManager::ClassId() { return kClass; }

__declspec(noinline) cLightMapManager *__fastcall cLightMapManager::Construct(void *memory, void *)
{
    return ::new (memory) cLightMapManager;
}

bool cLightMapManager::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == cILightMapManager::kIid || riid == kClass) {
        *out = static_cast<cILightMapManager *>(this);
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(riid, out);
}

uint32_t cLightMapManager::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cLightMapManager::Release() { return rz::cRZUnknown::Release(); }

// ---- the director's slots -------------------------------------------------------------------------------------------

bool cLightMapManager::Init()
{
    if (!mInitialized) {
        mInitialized = 1;
        cIGZMessageTarget2 *const target = this;
        reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(target), nullptr);
        mStarted = 0;
        ScopedRef settings = Settings();
        if (auto *const manager = static_cast<cIGZPropertyManagerString *>(settings.mObject)) {
            GZServiceString key("lmReserveBorderTiles");
            manager->AddNotification(target, kBorderSetting, &key, &ts2_rva_ffd8e4);
        }
        settings.ReleaseObject();
    }
    return true;
}

bool cLightMapManager::Shutdown()
{
    if (mInitialized) {
        mInitialized = 0;
        cIGZMessageTarget2 *const target = this;
        ScopedRef settings = Settings();
        if (auto *const manager = static_cast<cIGZPropertyManagerString *>(settings.mObject)) {
            GZServiceString key("lmReserveBorderTiles");
            manager->RemoveNotification(target, kBorderSetting, &key);
        }
        reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(target), nullptr);
        if (cIGZUnknown *const ref = mRef18c) {
            mRef18c = nullptr;
            ref->Release(); // receiver: cIGZUnknown (slot 2; nothing in the image sets it)
        }
        ClearImages(mImages);
        ClearImages(mImages2);
        for (auto &textures : mTextures) textures.clear();
        settings.ReleaseObject();
    }
    return true;
}

void cLightMapManager::Start()
{
    ReadWorldSize();
    ComputeInsets();
    if (mImages[1].Size() == 0) MakeFloorImage();
    if (mImages[0].Size() == 0) MakeWallAtlas();
    mStarted = 1;
}

void cLightMapManager::Stop()
{
    ClearImages(mImages);
    ClearImages(mImages2);
    for (auto &textures : mTextures) textures.clear();
    mFloors.clear();
    mFreeTiles.clear();
    mWalls.clear();
    mAtlases.clear();
    mSetD8.clear();
    mSetE0.clear();
    mStarted = 0;
}

// ---- slots 7-11: the transition, the colour and the incidence maps (row LG6b) ----------------------------------------

namespace {

// After a transition step or its end: GetCachedSingleton's field +0x58 (cITSGlobals slot 22), its slot 20, then slot 6
// of what that returns.
void NotifyLightsChanged()
{
    void *const field58 = static_cast<cITSGlobals *>(GetCachedSingleton())->GetField58(); // receiver: cITSGlobals (slot 22)
    // receiver: cTSConfigurationChangeManager (the field's service, ids 0x2d1a2550 / 0xed1a2513; vf20 0xcd6310, ret 0,
    // returns its +0xc's +0x2c)
    void *const target = RawSlot<20, void *>(field58);
    // receiver: not identified (cTSConfigTargets's +0x2c, set outside that class; vf20 returns it); slot 6
    // is reached by a tail jump with nothing pushed, so it takes no arguments and pops nothing
    RawSlot<6, void>(target);
}

} // namespace

void cLightMapManager::Update()
{
    // receiver: cTSGameStateController (cITSGlobals slot 23's object); vf5 0x149d00, ret 0, no arguments
    if (RawSlot<5, uint32_t>(GetCachedSlot23()) != kTransitionClock) return;
    if (mWord190 > 0) {
        const int32_t left = --mWord190;
        const float t =
            1.0f - (static_cast<float>(left) / static_cast<float>(mTransitionSteps)) * (1.0f - mTransitionStepSize);
        Blend(mImages2, mImages, t);
        Upload(mImages2);
        NotifyLightsChanged();
    } else if (mSetD8.size() != 0 || mSetE0.size() != 0) {
        Upload(mImages2);
        mSetD8.clear();
        mSetE0.clear();
    }
}

void cLightMapManager::SetColor(const float *rgb)
{
    float c[3] = {rgb[0] * mFloorScale, rgb[1] * mFloorScale, rgb[2] * mFloorScale};
    for (uint32_t i = 0; i < mImages[1].Size(); ++i) {
        const int32_t n = static_cast<int32_t>(i + 1);
        const float debug[3] = {static_cast<float>((n >> 2) & 1), static_cast<float>((n >> 1) & 1),
                                static_cast<float>(n & 1)};
        mImages[1].At(i)->FillOpaque(mDebug ? debug : c, -1); // receiver: nGZSceneGraph::cIImageData
    }
    c[0] = rgb[0] * mWallScale;
    c[1] = rgb[1] * mWallScale;
    c[2] = rgb[2] * mWallScale;
    for (uint32_t i = 0; i < mImages[0].Size(); ++i) {
        const int32_t n = static_cast<int32_t>(i + 1);
        const float debug[3] = {static_cast<float>((n >> 2) & 1), static_cast<float>((n >> 1) & 1),
                                static_cast<float>(n & 1)};
        mImages[0].At(i)->FillOpaque(mDebug ? debug : c, -1); // receiver: nGZSceneGraph::cIImageData
    }
}

void cLightMapManager::SetTransition(bool on)
{
    // receiver: cITSGlobals slot 3's object (0x20df9a, cell 0x10cc2ac; class not identified); slot 21 called with nothing
    // pushed and the stack not adjusted after (ret 0): the detail level
    const int32_t detail = RawSlot<21, int32_t>(OriginalFunction<void *(__cdecl *)(), 0x20df9a>()());
    if (on && mTransitionSteps != 0 && detail < 4) {
        mWord190 = detail >= 3 ? mTransitionSteps / 4 : mTransitionSteps;
        return;
    }
    Upload(mImages);
    NotifyLightsChanged();
}

void cLightMapManager::UpdateIncidence()
{
    const float scale = mIncidenceMapScale;
    if (0.0f >= scale) return; // a NaN scale goes on
    if (mImages[3].Size() != mImages[1].Size() || mImages[2].Size() != mImages[0].Size()) return;
    const float inverse = 2.0f / scale;
    uint8_t *table = MakeIncidenceTable(inverse, mIncidenceMapHeightClamp);
    for (uint32_t i = 0; i < mImages[1].Size(); ++i) {
        nGZSceneGraph::cIImageData *const floor = mImages[1].At(i);
        const int32_t width = floor->LevelWidth(0);           // receiver: nGZSceneGraph::cIImageData
        const int32_t halfWidth = width / 2;
        const int32_t halfHeight = floor->LevelHeight(0) / 2; // receiver: nGZSceneGraph::cIImageData
        if (floor->LevelPitch(0) != width * 4) continue;      // receiver: nGZSceneGraph::cIImageData
        if (mImages[3].At(i)->LevelPitch(0) != halfWidth * 4) continue; // receiver: nGZSceneGraph::cIImageData
        if (mImages[3].At(i)->LevelHeight(0) != halfHeight) continue;   // receiver: nGZSceneGraph::cIImageData
        const uint8_t *const src = floor->LevelData(0);       // receiver: nGZSceneGraph::cIImageData
        uint8_t *const dst = mImages[3].At(i)->LevelData(0);  // receiver: nGZSceneGraph::cIImageData
        Incidence(dst, halfWidth, src, width, halfWidth, halfHeight, table);
    }
    FreeIncidenceTable(table);
    table = MakeIncidenceTable(inverse, 0.8f);
    for (uint32_t i = 0; i < mImages[0].Size(); ++i) {
        nGZSceneGraph::cIImageData *const wall = mImages[0].At(i);
        const int32_t width = wall->LevelWidth(0);           // receiver: nGZSceneGraph::cIImageData
        const int32_t halfWidth = width / 2;
        const int32_t halfHeight = wall->LevelHeight(0) / 2; // receiver: nGZSceneGraph::cIImageData
        // ORIGINAL-BUG: a wall pair whose sizes don't agree ends the pass: the later walls keep their incidence maps and
        // the table is never freed (the floors' loop goes on to the next pair instead).
        if (wall->LevelPitch(0) != width * 4) return;                 // receiver: nGZSceneGraph::cIImageData
        if (mImages[2].At(i)->LevelPitch(0) != halfWidth * 4) return; // receiver: nGZSceneGraph::cIImageData
        if (mImages[2].At(i)->LevelHeight(0) != halfHeight) return;   // receiver: nGZSceneGraph::cIImageData
        const uint8_t *const src = wall->LevelData(0);       // receiver: nGZSceneGraph::cIImageData
        uint8_t *const dst = mImages[2].At(i)->LevelData(0); // receiver: nGZSceneGraph::cIImageData
        Incidence(dst, halfWidth, src, width, halfWidth, halfHeight, table);
    }
    FreeIncidenceTable(table);
}

void cLightMapManager::Advance(float step)
{
    Blend(mImages2, mImages, step);
    Upload(mImages2);
}

// ---- walls -------------------------------------------------------------------------------------------------------------

int32_t cLightMapManager::AcquireWall(const WallKey *key)
{
    WallRegion *held = nullptr;
    if (FindWallRegion(key, &held)) {
        ++held->count;
        if (held->atlas >= 0) return held->atlas;
    }
    int32_t kind = kWallKindDefault;
    void *const partitions = OriginalFunction<void *(__cdecl *)(), 0x20de97>()();
    // The face's partition type (vf50 0x81a670, ret 4).
    uint32_t type = RawSlot<50, uint32_t>(partitions, key); // receiver: nTSWorld::cPartitionManager
    if (worldcatalog::Slot80Is2(&type) || type == kTallKind || worldcatalog::IsKind3(&type)) {
        const float height = static_cast<float>(WallFaceHeight(key));
        kind = RoundUp(height);
        if (kind > 4) kind = 4;
        if (kind < 1) kind = 1;
    }
    const bool border = OriginalFunction<uint16_t(__cdecl *)(const WallKey *, int32_t), 0x7f2340>()(key, 0) != 0;
    const WallRegion region = AllocateRegion(kind, border);
    // ORIGINAL-BUG: when the face is held already with no atlas (an earlier allocation failed), the new region is not
    // stored (the key is there) and stays allocated in its atlas.
    mWalls.emplace(*key, region);
    DrawWallRegion(region);
    return region.atlas;
}

void cLightMapManager::ReleaseWall(const WallKey *key)
{
    WallRegion *found = nullptr;
    if (FindWallRegion(key, &found)) --found->count;
    // ORIGINAL-BUG: an unknown face is read through a null pointer.
    WallRegion *volatile region = found;
    if (region->atlas >= 0 && region->count == 0) {
        FreeRegion(region->node, region->atlas);
        mWalls.erase(*key);
    }
}

WallRegion cLightMapManager::FindWall(const WallKey *key) const
{
    const auto it = mWalls.find(*key);
    return it == mWalls.end() ? kNoRegion : it->second;
}

bool cLightMapManager::FindWallRegion(const WallKey *key, WallRegion **region)
{
    const auto it = mWalls.find(*key);
    if (it == mWalls.end()) return false; // *region left as it was
    *region = &it->second;
    return true;
}

WallRegion cLightMapManager::AllocateRegion(int32_t kind, bool border)
{
    int32_t width, height;
    uint8_t borderKind;
    if (border && kind == 1) {
        width = mBorderWidth;
        height = mBorderHeight;
        borderKind = 1;
    } else {
        width = mWallTileSize;
        height = mWallTileSize * kind;
        borderKind = 0;
    }
    for (uint32_t i = 0; i < mAtlases.size(); ++i) {
        const int32_t node = mAtlases[i].Allocate(width, height);
        if (node != -1) return WallRegion{node, static_cast<int8_t>(i), borderKind, 1};
    }
    MakeWallAtlas();
    const int32_t node = mAtlases.back().Allocate(width, height);
    if (node == -1) return kNoRegion;
    return WallRegion{node, static_cast<int8_t>(mAtlases.size() - 1), borderKind, 1};
}

void cLightMapManager::FreeRegion(int32_t node, int8_t atlas)
{
    // ORIGINAL-BUG: the bound is inclusive (an index one past the atlases is freed in memory past the vector).
    if (atlas >= 0 && static_cast<uint32_t>(atlas) <= mAtlases.size()) mAtlases.data()[atlas].Free(node);
}

// ---- floors ------------------------------------------------------------------------------------------------------------

int32_t cLightMapManager::AcquireFloor(const FloorUse *use)
{
    if (use->key.level == 0) {
        if (mImages[1].Size() == 0) MakeFloorImage();
        return 0;
    }
    const auto it = mFloors.find(use->key);
    if (it != mFloors.end()) {
        it->second.flags |= use->flags;
        return it->second.image;
    }
    FloorTile tile = TakeFreeTile();
    tile.flags = use->flags;
    mFloors.emplace(use->key, tile);
    DrawFloorTile(use);
    return tile.image;
}

void cLightMapManager::ReleaseFloor(const FloorUse *use)
{
    if (use->key.level == 0) return;
    const auto it = mFloors.find(use->key);
    if (it == mFloors.end()) return;
    it->second.flags &= static_cast<uint8_t>(~use->flags);
    if (it->second.flags == 0) {
        ReturnTile(it->second);
        mFloors.erase(use->key);
    }
}

FloorTile cLightMapManager::FindFloor(const FloorKey *key) const
{
    if (key->level == 0) return FloorTile{static_cast<uint8_t>(key->x), static_cast<uint8_t>(key->y), 0, 0};
    const auto it = mFloors.find(*key);
    return it == mFloors.end() ? kNoTile : it->second;
}

FloorTile cLightMapManager::TakeFreeTile()
{
    if (mFreeTiles.empty()) {
        MakeFloorImage();
        if (mFreeTiles.empty() && mImages[1].Size() == 1) MakeFloorImage();
        if (mFreeTiles.empty()) return kNoTile;
    }
    const FloorTile tile = mFreeTiles.back();
    mFreeTiles.pop_back();
    return tile;
}

void cLightMapManager::ReturnTile(FloorTile tile)
{
    if (tile.image >= 0) mFreeTiles.push_back(tile);
}

// ---- reading the lights back ---------------------------------------------------------------------------------------------

float *cLightMapManager::GetFloorLight(float *out, const float *position)
{
    worldmath::TileQuadrant where;
    worldmath::PositionToTileQuadrant(&where, position);
    const FloorKey key{where.x, where.y, where.level};
    const FloorTile tile = FindFloor(&key);
    if (tile.image >= 0 && static_cast<uint32_t>(tile.image) < mImages2[1].Size()) {
        const float tileSize = static_cast<float>(mFloorTileSize);
        const int32_t px = RoundDown((position[0] - static_cast<float>(RoundDown(position[0]))) * tileSize);
        const int32_t py = RoundDown((position[1] - static_cast<float>(RoundDown(position[1]))) * tileSize);
        nGZSceneGraph::cIImageData *const image = mImages2[1].At(static_cast<uint32_t>(tile.image));
        const int32_t height = image->LevelHeight(0); // receiver: nGZSceneGraph::cIImageData
        const int32_t width = image->LevelWidth(0);   // receiver: nGZSceneGraph::cIImageData
        const int32_t x = mFloorTileSize * static_cast<int8_t>(tile.x) + px;
        const int32_t y = mFloorTileSize * static_cast<int8_t>(tile.y) + py;
        if (x >= 0 && x < width && y >= 0 && y < height) {
            float rgb[3];
            const float *const texel = image->GetPixelRGB(rgb, x, y, 0); // receiver: nGZSceneGraph::cIImageData
            const float scale = mFloorLightScale;
            out[0] = texel[0], out[1] = texel[1], out[2] = texel[2];
            out[0] = scale * out[0], out[1] = scale * out[1], out[2] = scale * out[2];
            return out;
        }
    }
    out[0] = 0.0f, out[1] = 0.0f, out[2] = 0.0f; // the zero vector 0x10d9388 (.bss, never written)
    return out;
}

float *cLightMapManager::GetWallLight(float *out, const WallKey *key)
{
    const uint32_t images = mImages2[0].Size();
    if (images != 0) {
        const WallRegion region = FindWall(key);
        if (region.atlas >= 0 && static_cast<uint32_t>(region.atlas) < images &&
            static_cast<uint32_t>(region.atlas) < mAtlases.size()) {
            const cGZRect &rect = mAtlases[region.atlas].mNodes[region.node].rect;
            float rgb[3];
            nGZSceneGraph::cIImageData *const image = mImages2[0].At(static_cast<uint32_t>(region.atlas));
            // receiver: nGZSceneGraph::cIImageData (x from the rows, y from the columns, as the original)
            const float *const texel = image->GetPixelRGB(rgb, (rect.top + rect.bottom) / 2, (rect.left + rect.right) / 2, 0);
            const float scale = mWallLightScale;
            out[0] = texel[0], out[1] = texel[1], out[2] = texel[2];
            out[0] = scale * out[0], out[1] = scale * out[1], out[2] = scale * out[2];
            return out;
        }
    }
    out[0] = 0.0f, out[1] = 0.0f, out[2] = 0.0f; // the zero vector 0x10d9388
    return out;
}

bool cLightMapManager::GetFloorUV(const FloorUse *use, float *uv0, float *uv1)
{
    if (use->key.level == 0) {
        uv0[0] = static_cast<float>(use->key.x) * kFloorTileUV;
        uv0[1] = static_cast<float>(use->key.y) * kFloorTileUV;
        uv1[0] = static_cast<float>(use->key.x + 1) * kFloorTileUV;
        uv1[1] = static_cast<float>(use->key.y + 1) * kFloorTileUV;
        return true;
    }
    const FloorTile tile = FindFloor(&use->key);
    if (tile.image < 0) {
        uv0[0] = 0.0f, uv0[1] = 0.0f, uv1[0] = 0.0f, uv1[1] = 0.0f;
        return false;
    }
    const int32_t tx = static_cast<int8_t>(tile.x), ty = static_cast<int8_t>(tile.y);
    uv0[0] = static_cast<float>(tx) * mTileUV + mHalfTexelUV;
    uv0[1] = static_cast<float>(ty) * mTileUV + mHalfTexelUV;
    uv1[0] = static_cast<float>(tx + 1) * mTileUV - mHalfTexelUV;
    uv1[1] = static_cast<float>(ty + 1) * mTileUV - mHalfTexelUV;
    return true;
}

bool cLightMapManager::GetFloorRect(const FloorUse *use, int32_t *left, int32_t *top, int32_t *right, int32_t *bottom,
                                    uint32_t *image)
{
    int32_t tx, ty;
    uint32_t index;
    if (use->key.level == 0) {
        if (mImages[1].Size() == 0) return false;
        tx = use->key.x, ty = use->key.y, index = 0;
    } else {
        const FloorTile tile = FindFloor(&use->key);
        if (tile.image < 0) {
            *left = 0, *right = 0, *top = 0, *bottom = 0;
            return false;
        }
        index = static_cast<uint32_t>(static_cast<int32_t>(tile.image));
        if (index >= mImages[1].Size()) return false;
        tx = static_cast<int8_t>(tile.x), ty = static_cast<int8_t>(tile.y);
    }
    // receiver: nGZSceneGraph::cIImageData
    const uint32_t width = static_cast<uint32_t>(mImages[1].At(index)->LevelWidth(0));
    const uint32_t side = width >> 6;
    *left = static_cast<int32_t>(static_cast<uint32_t>(tx) * side);
    *right = static_cast<int32_t>(static_cast<uint32_t>(*left) + side);
    const uint32_t lower = width - static_cast<uint32_t>(ty) * side;
    *bottom = static_cast<int32_t>(lower);
    *top = static_cast<int32_t>(lower - side);
    *image = index;
    return true;
}

bool cLightMapManager::GetWallUV(const WallKey *key, float *uv0, float *uv1, uint32_t *atlas)
{
    const WallRegion region = FindWall(key);
    if (region.atlas >= 0 && static_cast<uint32_t>(region.atlas) < mAtlases.size()) {
        const cGZRect &rect = mAtlases[region.atlas].mNodes[region.node].rect;
        const float size = static_cast<float>(mWallTextureSize);
        const bool wide = region.border == 1;
        const float insetU = wide ? mBorderInsetU : mInsetU;
        const float insetV = wide ? mBorderInsetV : mInsetV;
        uv0[0] = static_cast<float>(rect.left) / size + insetU;
        uv0[1] = static_cast<float>(rect.top) / size + insetV;
        uv1[0] = static_cast<float>(rect.right) / size - insetU;
        uv1[1] = static_cast<float>(rect.bottom) / size - insetV;
        if (atlas) *atlas = static_cast<uint32_t>(static_cast<int32_t>(region.atlas));
        return true;
    }
    uv0[0] = 0.0f, uv0[1] = 0.0f, uv1[0] = 0.0f, uv1[1] = 0.0f;
    if (atlas) *atlas = 0;
    return false;
}

bool cLightMapManager::GetWallRect(const WallKey *key, int32_t *left, int32_t *top, int32_t *right, int32_t *bottom,
                                   uint32_t *atlas)
{
    const WallRegion region = FindWall(key);
    if (region.atlas >= 0 && static_cast<uint32_t>(region.atlas) < mAtlases.size()) {
        const cGZRect &rect = mAtlases[region.atlas].mNodes[region.node].rect;
        *left = rect.left;
        *right = rect.right;
        *bottom = mWallTextureSize - rect.top;
        *top = mWallTextureSize - rect.bottom;
        *atlas = static_cast<uint32_t>(static_cast<int32_t>(region.atlas));
        return true;
    }
    *left = 0, *right = 0, *top = 0, *bottom = 0;
    return false;
}

nGZSceneGraph::cIImageData *cLightMapManager::GetImage(uint32_t group, uint32_t index)
{
    if (group < 4 && static_cast<int32_t>(index) >= 0 && index < mImages[group].Size()) return mImages[group].At(index);
    return nullptr;
}

float cLightMapManager::GetScale(int32_t which)
{
    if (which == 0) return mWallScale;
    if (which != 1) return 1.0f;
    return mFloorScale;
}

// ---- sizes and settings --------------------------------------------------------------------------------------------------

void cLightMapManager::ClearImages(LightMapImages *group)
{
    group[1].Clear();
    group[3].Clear();
    group[0].Clear();
    group[2].Clear();
}

void cLightMapManager::ReadWorldSize()
{
    auto *const world = static_cast<nTSWorld::cIWorldDB *>(OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()());
    mWorldWidth = world->GetWidth();   // receiver: nTSWorld::cIWorldDB
    mWorldHeight = world->GetHeight(); // receiver: nTSWorld::cIWorldDB
    const int32_t perRow = mFloorTextureSize / mFloorTileSize;
    mTilesPerRow = perRow;
    mTileUV = static_cast<float>(1.0 / static_cast<double>(perRow));
    mHalfTexelUV = static_cast<float>(0.5 / static_cast<double>(mFloorTextureSize));
}

void cLightMapManager::ComputeInsets()
{
    const float inset = static_cast<float>(mInset) / static_cast<float>(mWallTextureSize);
    const float half = (static_cast<float>(mInset) * 0.5f) / static_cast<float>(mWallTextureSize);
    mInsetU = inset;
    mInsetV = inset;
    mBorderInsetU = half;
    mBorderInsetV = half;
}

void cLightMapManager::ReadSettings(void *properties)
{
    if (!properties) return;
    // Each reader sets `changed` when it changed its value; what an earlier reader said is carried past a later one that
    // clears it.
    uint8_t changed = 0;
    nGZSceneGraph::ReadBoolProperty(properties, "lightMapDebug", &mDebug, &changed);
    bool any = changed != 0;
    changed = any;
    nGZSceneGraph::ReadFloatProperty(properties, "incidenceMapScale", &mIncidenceMapScale, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadFloatProperty(properties, "incidenceMapHeightClamp", &mIncidenceMapHeightClamp, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadIntProperty(properties, "transitionSteps", &mTransitionSteps, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadFloatProperty(properties, "transitionStepSize", &mTransitionStepSize, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadIntProperty(properties, "floorTextureSize", &mFloorTextureSize, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadIntProperty(properties, "floorTileSize", &mFloorTileSize, &changed);
    if (changed == 0 && !any) return;
    if (!mStarted) return;
    void *const memory = RZMemory::Malloc(cRZMessage2Standard::kSize);
    auto *const message = reinterpret_cast<cIGZMessage2Standard *>(
        memory ? static_cast<cRZMessage2Standard *>(memory)->Construct() : nullptr);
    if (message) message->AddRef();
    // ORIGINAL-BUG: a failed allocation is used anyway.
    cIGZMessage2Standard *volatile posted = message;
    posted->SetType(kChanged); // receiver: cRZMessage2Standard
    posted->SetData1(0);       // receiver: cRZMessage2Standard
    // receiver: the message server (cIGZMessageServer2, slot 4)
    static_cast<cIGZMessageServer2 *>(GetCachedMessageServer())->MessagePost(message, false);
    if (message) message->Release(); // receiver: cRZMessage2Standard
}

bool cLightMapManager::DoMessage(void *data)
{
    auto *const message = static_cast<cIGZMessage2Standard *>(data);
    const uint32_t type = message->GetType(); // receiver: cIGZMessage2Standard (the server's messages)
    if (type == kWorldLoaded) {
        // ORIGINAL-BUG: a message without its object is read through a null pointer.
        cIGZUnknown *const object = message->GetIGZUnknown(); // receiver: cIGZMessage2Standard
        uint8_t *holder = nullptr;
        bool result = false;
        // receiver: the message's object (its cIGZUnknown face, slot 0)
        if (object->QueryInterface(kHolderIid, reinterpret_cast<void **>(&holder))) {
            // The holder's virtual bases through the vbtable at +8: [2] the property set the readers take.
            void *properties = nullptr;
            if (holder) properties = holder + 8 + reinterpret_cast<const int32_t *const *>(holder + 8)[0][2];
            ReadSettings(properties);
            result = true;
        }
        if (holder) {
            // receiver: the holder's cIGZUnknown virtual base (vbtable [1]), slot 2 Release
            auto *const unknown = reinterpret_cast<cIGZUnknown *>(holder + 8 +
                                                                  reinterpret_cast<const int32_t *const *>(holder + 8)[0][1]);
            unknown->Release();
        }
        return result;
    }
    if (type == kBorderSetting && message->HasData2()) { // receiver: cIGZMessage2Standard
        if (const auto *const value = static_cast<const uint8_t *>(message->GetVoid2())) // receiver: cIGZMessage2Standard
            ts2_rva_ffd8e4.mValue = value[4];
    }
    return false;
}

// ---- the images and their textures (row LG6b) -------------------------------------------------------------------------

namespace {

// A light map image (cIImageData) made by the COM director, size x size with one level, filled with a colour.
nGZSceneGraph::cIImageData *NewImage(int32_t size, const float *colour)
{
    auto *const image = static_cast<nGZSceneGraph::cIImageData *>(NewObject(kImageClass, nGZSceneGraph::cIImageData::kIid));
    // ORIGINAL-BUG: a failed creation is not checked.
    nGZSceneGraph::cIImageData *volatile made = image;
    made->Init(size, size, 1, 1, 1.0f, 1); // receiver: nGZSceneGraph::cIImageData
    made->Fill(colour, -1);                // receiver: nGZSceneGraph::cIImageData
    return image;
}

// The texture for an image: made in `texture` (one local in the original: the texture it held is released after the
// director is looked up and before the new one is made), loaded from the image, added to the texture manager under the
// name; its handle pushed onto `textures`.
void NewTexture(void *&texture, nGZSceneGraph::cIImageData *image, const char *name,
                std::vector<nGZSceneGraph::cTextureHandle> &textures)
{
    auto *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (void *const old = texture) {
        texture = nullptr;
        ReleaseObject(old);
    }
    com->GetClassObject(kTextureClass, nGZSceneGraph::cITextureImage::kIid, &texture); // receiver: cIGZCOM
    auto *const face = static_cast<nGZSceneGraph::cITextureImage *>(texture);
    face->Load(reinterpret_cast<cIGZUnknown *>(image), 1); // receiver: nGZSceneGraph::cITextureImage (slot 9)
    // receiver: nGZSceneGraph::cITextureManager (slot 20)
    const uint32_t id = nGZSceneGraph::TextureManager()->Add(static_cast<cIGZUnknown *>(texture), name, 1);
    const nGZSceneGraph::cTextureHandle handle(id);
    textures.push_back(handle);
}

} // namespace

void cLightMapManager::MakeFloorImage()
{
    float rgb[3];
    LightColour(rgb);
    const float fill[4] = {1.0f, rgb[0] * mFloorScale, rgb[1] * mFloorScale, rgb[2] * mFloorScale};
    const int32_t count = static_cast<int32_t>(mImages[1].Size());
    char name[40];
    std::snprintf(name, sizeof name, "floorLightMap_%d", count);
    nGZSceneGraph::cIImageData *const image = NewImage(mFloorTextureSize, fill);
    nGZSceneGraph::cIImageData *const shown = NewImage(mFloorTextureSize, fill);
    void *texture = nullptr;
    NewTexture(texture, shown, name, mTextures[1]);
    PushCounted(mImages[1], image);
    PushCounted(mImages2[1], shown);

    std::snprintf(name, sizeof name, "floorIncidenceMap_%d", count);
    nGZSceneGraph::cIImageData *const incidence = NewImage(mFloorTextureSize / 2, kIncidenceFill);
    nGZSceneGraph::cIImageData *const incidenceShown = NewImage(mFloorTextureSize / 2, kIncidenceFill);
    NewTexture(texture, incidenceShown, name, mTextures[3]);
    PushCounted(mImages[3], incidence);
    PushCounted(mImages2[3], incidenceShown);

    // The image's free tiles, from the top row down. The first image keeps the world's tiles (x < width and y < height)
    // for the tiles themselves (FindFloor's level 0); with lmReserveBorderTiles the border tiles and the last row and
    // column are held back too.
    FloorTile tile{0, 0, static_cast<int8_t>(count), 0};
    const bool reserve = ts2_rva_ffd8e4.mValue != 0;
    for (int32_t y = mTilesPerRow - 1; y >= 0; --y) {
        tile.y = static_cast<uint8_t>(y);
        for (int32_t x = 0; x < mTilesPerRow; ++x) {
            tile.x = static_cast<uint8_t>(x);
            if (count == 0) {
                bool free = !(x < mWorldWidth && y < mWorldHeight);
                if (reserve) {
                    const int32_t last = mTilesPerRow - 1;
                    if ((x == mWorldWidth || x == last) && (y <= mWorldHeight || y == last)) free = false;
                    if ((x <= mWorldWidth || x == last) && (y == mWorldHeight || y == last)) continue;
                }
                if (!free) continue;
            }
            mFreeTiles.push_back(tile);
        }
    }
    ReleaseObject(incidenceShown);
    ReleaseObject(incidence);
    ReleaseObject(shown);
    ReleaseObject(image);
    ReleaseObject(texture);
}

void cLightMapManager::MakeWallAtlas()
{
    float rgb[3];
    LightColour(rgb);
    const float fill[4] = {1.0f, rgb[0] * mWallScale, rgb[1] * mWallScale, rgb[2] * mWallScale};
    const int32_t count = static_cast<int32_t>(mImages[0].Size());
    char name[40];
    std::snprintf(name, sizeof name, "wallLightMap_%d", count);
    nGZSceneGraph::cIImageData *const image = NewImage(mWallTextureSize, fill);
    nGZSceneGraph::cIImageData *const shown = NewImage(mWallTextureSize, fill);
    void *texture = nullptr;
    NewTexture(texture, shown, name, mTextures[0]);
    PushCounted(mImages[0], image);
    PushCounted(mImages2[0], shown);

    std::snprintf(name, sizeof name, "wallIncidenceMap_%d", count);
    nGZSceneGraph::cIImageData *const incidence = NewImage(mWallTextureSize / 2, kIncidenceFill);
    nGZSceneGraph::cIImageData *const incidenceShown = NewImage(mWallTextureSize / 2, kIncidenceFill);
    NewTexture(texture, incidenceShown, name, mTextures[2]);
    PushCounted(mImages[2], incidence);
    PushCounted(mImages2[2], incidenceShown);

    mAtlases.push_back(nGZGraphic4::cTextureAtlas{});
    mAtlases.back().Init(mWallTextureSize, mWallTextureSize);
    ReleaseObject(incidenceShown);
    ReleaseObject(incidence);
    ReleaseObject(shown);
    ReleaseObject(image);
    ReleaseObject(texture);
}

void cLightMapManager::DrawFloorTile(const FloorUse *use)
{
    int32_t left, top, right, bottom;
    uint32_t index;
    if (!GetFloorRect(use, &left, &top, &right, &bottom, &index)) return; // receiver: cLightMapManager (slot 19)
    float rgb[3];
    LightColour(rgb);
    // ORIGINAL-BUG: the floor tiles are filled at the walls' scale (GetScale(0)), not the floors' (SetColor and
    // MakeFloorImage use mFloorScale); the two are equal unless the settings differ.
    const uint32_t pixel = OpaquePixel(rgb, mWallScale);
    for (LightMapImages *const group : {&mImages[1], &mImages2[1]}) {
        nGZSceneGraph::cIImageData *const image = group->At(index);
        uint8_t *const data = image->LevelData(0);                            // receiver: nGZSceneGraph::cIImageData
        const uint32_t pitch = static_cast<uint32_t>(image->LevelPitch(0)) / 4; // receiver: nGZSceneGraph::cIImageData
        const uint32_t height = static_cast<uint32_t>(image->LevelHeight(0));    // receiver: nGZSceneGraph::cIImageData
        // The image's rows run bottom up: the rectangle's top row is height - 1 - top.
        uint32_t *row = reinterpret_cast<uint32_t *>(data) + ((height - 1) * pitch - pitch * static_cast<uint32_t>(top));
        for (uint32_t y = static_cast<uint32_t>(top); y < static_cast<uint32_t>(bottom); ++y, row -= pitch) {
            for (uint32_t x = static_cast<uint32_t>(left); x < static_cast<uint32_t>(right); ++x) row[x] = pixel;
        }
    }
    mSetE0.insert(static_cast<int32_t>(index));
}

void cLightMapManager::DrawWallRegion(WallRegion region)
{
    if (region.atlas < 0) return;
    const cGZRect rect = mAtlases[region.atlas].mNodes[region.node].rect;
    float rgb[3];
    LightColour(rgb);
    const uint32_t pixel = OpaquePixel(rgb, mWallScale);
    // ORIGINAL-BUG: the atlas isn't checked against the images: after Shutdown (which clears the images but keeps the
    // atlases until Stop) a face met before reads past the vectors' end. DrawFloorTile has the same gap for tiles.
    for (LightMapImages *const group : {&mImages[0], &mImages2[0]}) {
        nGZSceneGraph::cIImageData *const image = group->At(static_cast<uint32_t>(static_cast<int32_t>(region.atlas)));
        uint8_t *const data = image->LevelData(0);                            // receiver: nGZSceneGraph::cIImageData
        const uint32_t pitch = static_cast<uint32_t>(image->LevelPitch(0)) / 4; // receiver: nGZSceneGraph::cIImageData
        uint32_t *row = reinterpret_cast<uint32_t *>(data) + pitch * static_cast<uint32_t>(rect.top);
        for (int32_t y = rect.top; y < rect.bottom; ++y, row += pitch) {
            for (int32_t x = rect.left; x < rect.right; ++x) row[x] = pixel;
        }
    }
    mSetD8.insert(static_cast<int32_t>(region.atlas));
}

void cLightMapManager::Upload(LightMapImages *group)
{
    UploadPairs(group[1], mTextures[1]);
    UploadPairs(group[0], mTextures[0]);
    UploadPairs(mImages[3], mTextures[3]);
    UploadPairs(mImages[2], mTextures[2]);
}

void cLightMapManager::Blend(LightMapImages *dst, LightMapImages *src, float t)
{
    for (int k = 0; k < 4; ++k) {
        while (dst[k].Size() < src[k].Size()) {
            void *const image = NewObject(kImageClass, nGZSceneGraph::cIImageData::kIid);
            // ORIGINAL-BUG: a failed creation is pushed as null and dereferenced by BlendImage below.
            PushCounted(dst[k], image);
            ReleaseObject(image);
        }
        for (uint32_t i = 0; i < src[k].Size(); ++i) BlendImage(src[k].At(i), dst[k].At(i), t);
    }
}

void __cdecl cLightMapManager::BlendImage(nGZSceneGraph::cIImageData *src, nGZSceneGraph::cIImageData *dst, float t)
{
    if (!src || !src->HasPixels()) return;     // receiver: nGZSceneGraph::cIImageData
    const int32_t width = src->LevelWidth(0);   // receiver: nGZSceneGraph::cIImageData
    const int32_t height = src->LevelHeight(0); // receiver: nGZSceneGraph::cIImageData
    if (!dst->HasPixels()) dst->Init(width, height, 1, 1, 1.0f, 1); // receiver: nGZSceneGraph::cIImageData
    if (dst->LevelPitch(0) != width * 4) return;                    // receiver: nGZSceneGraph::cIImageData
    const uint8_t *const from = src->LevelDataConst(0);             // receiver: nGZSceneGraph::cIImageData
    uint8_t *const to = dst->LevelData(0);                          // receiver: nGZSceneGraph::cIImageData
    // The image library's MMX blend (dst, dst pitch, src, src pitch, width, height, alpha 0..255): still the original's.
    OriginalFunction<void(__stdcall *)(uint8_t *, int32_t, const uint8_t *, int32_t, int32_t, int32_t, int32_t),
                     0xb3c6c0>()(to, width * 4, from, width * 4, width, height, NearInt(t * 255.0f));
}

// ---- a wall face's height (row LG6b) ------------------------------------------------------------------------------------

double __cdecl cLightMapManager::WallFaceHeight(const WallKey *key)
{
    nTSWorld::WorldPoint first{key->x0, key->y0, static_cast<int32_t>(key->word2)};
    nTSWorld::WorldPoint second{key->x1, key->y1, static_cast<int32_t>(key->word5)};
    const float ground0 = static_cast<float>(worldheights::GroundHeightAt(&first));
    const float ground1 = static_cast<float>(worldheights::GroundHeightAt(&second));
    void *const partitions = worldsvc::CachedWorldDB()->GetPartitions(); // receiver: nTSWorld::cIWorldDB (slot 39)
    // receiver: nTSWorld::cIWorldDB (0x20e0b2's object; slot 36): cWorldRoof
    void *const roofs = static_cast<nTSWorld::cIWorldDB *>(OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()())->GetRoofs();
    uint32_t type = RawSlot<50, uint32_t>(partitions, key); // receiver: nTSWorld::cPartitionManager (vf50, the key by pointer)
    const float base = OriginalFunction<bool(__cdecl *)(const WallKey *), 0x7e2050>()(key) ? 0.75f : 0.0f;
    float top0;
    // receiver: nTSWorld::cIWorldRoof (cWorldRoof's +8 face: vf37 0x80ea50 ret 4, the point under a roof; vf22
    // 0x80fc30 ret 8, the roof's height there in st0)
    if (roofs && RawSlot<37, bool>(roofs, &first)) {
        top0 = static_cast<float>(x87::Rounded(RawSlot<22, double>(roofs, &first, 1) - 0.25));
    } else if (worldcatalog::IsKind3(&type)) {
        top0 = static_cast<float>(AddMem87(AddMem87(worldheights::GroundHeightAt(&first), 1.025f), base));
    } else {
        ++first.level;
        top0 = static_cast<float>(worldheights::GroundHeightAt(&first));
    }
    float top1;
    // receiver: nTSWorld::cIWorldRoof (vf37 0x80ea50 ret 4, vf22 0x80fc30 ret 8, as above)
    if (roofs && RawSlot<37, bool>(roofs, &second)) {
        top1 = static_cast<float>(x87::Rounded(RawSlot<22, double>(roofs, &second, 1) - 0.25));
    } else if (worldcatalog::IsKind3(&type)) {
        top1 = top0;
    } else {
        ++second.level;
        top1 = static_cast<float>(worldheights::GroundHeightAt(&second));
    }
    const float high = top1 > top0 ? top1 : top0;
    const float low = ground1 < ground0 ? ground1 : ground0;
    return x87::Rounded(static_cast<double>(high) - static_cast<double>(low));
}

// ---- the incidence maps (row LG6b) --------------------------------------------------------------------------------------

// The table: a header of six dwords (the kernel's weights at +0, its mask at +8, its or-words at +0x10), the offsets at
// +0x18, then 0x4000 entries of four words (+0x20), entry i for a squared slope of i * 2^17.
uint8_t *__cdecl cLightMapManager::MakeIncidenceTable(float inverse, float clamp)
{
    auto *const memory = static_cast<uint8_t *>(std::malloc(0x2002b)); // ORIGINAL-BUG: a failed allocation is not checked
    auto *const table =
        reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(memory) + 0xb) & ~static_cast<uintptr_t>(7));
    reinterpret_cast<void **>(table)[-1] = memory;
    const uint32_t header[6] = {0x260007u, 0x13u, 0xffff0000u, 0xffffu, 0x4000u, 0u};
    std::memcpy(table, header, sizeof header);
    const int32_t cut = NearInt(inverse * 255.0f * (1.0f - clamp));
    const uint64_t offsets = static_cast<uint64_t>(static_cast<int64_t>(255 - cut)) + 0x7fff008000800080ull;
    std::memcpy(table + 0x18, &offsets, sizeof offsets);
    const float lift = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(cut) * static_cast<uint32_t>(cut))) /
                       255.0f * 1024.0f;
    const float square = inverse * inverse * 65025.0f;
    for (int32_t i = 0; i < 0x4000; ++i) {
        const float length = static_cast<float>(std::sqrt(static_cast<double>(static_cast<float>(i) * 8.0f + square)));
        const float k = 0.5f / length;
        const uint16_t slope = static_cast<uint16_t>(std::bit_cast<uint32_t>(k * 130560.0f + kRoundBias));
        const uint16_t flat = static_cast<uint16_t>(std::bit_cast<uint32_t>(lift * k + kRoundBias));
        const uint16_t entry[4] = {flat, slope, slope, 0};
        std::memcpy(table + 0x20 + 8 * i, entry, sizeof entry);
    }
    return table;
}

void __cdecl cLightMapManager::FreeIncidenceTable(uint8_t *table) { std::free(reinterpret_cast<void **>(table)[-1]); }

void __cdecl cLightMapManager::Incidence(uint8_t *dst, int32_t dstWidth, const uint8_t *src, int32_t srcWidth,
                                         int32_t width, int32_t height, const uint8_t *table)
{
    IncidenceKernel(dst, dstWidth * 4, src, srcWidth * 4, width, height, table);
}

// Each dst texel from the 2 x 2 block of src texels under it (texel bytes as words): a = p11 - p00 and b = p10 - p01 per
// channel, their sum S and difference D weighed by the table's weights into two slopes, m = (b's first two channels as a
// dword + S's first sum, the slopes, 0) saturated; the entry for m1^2 + m2^2 scales (m masked, or'ed) and the offsets
// are added, saturated to bytes.
void __stdcall cLightMapManager::IncidenceKernel(uint8_t *dst, int32_t dstPitch, const uint8_t *src, int32_t srcPitch,
                                                 int32_t width, int32_t height, const uint8_t *table)
{
    // ORIGINAL-BUG (not reproduced): a width or height of 0 runs the original's loops about 2^30 / 2^32 times; ours
    // writes nothing. The original also interleaves the texel bytes with mm7, which it never sets: the bytes' high
    // halves are mm7's low dword's, the low half of the mantissa of the x87 value that last sat in physical register 7
    // (0 after any float or 32-bit integer load, not after some doubles). Ours takes them as 0.
    if (width == 0 || height == 0) return;
    int16_t weight[4], mask[4], bits[4], offset[4];
    for (int c = 0; c < 4; ++c) {
        weight[c] = TableWord(table, 2 * c);
        mask[c] = TableWord(table, 8 + 2 * c);
        bits[c] = TableWord(table, 0x10 + 2 * c);
        offset[c] = TableWord(table, 0x18 + 2 * c);
    }
    for (int32_t y = height; y != 0; --y) {
        uint32_t *const out = reinterpret_cast<uint32_t *>(dst);
        for (int32_t x = 0; x != width; ++x) {
            const uint8_t *const p00 = src + 8 * x;
            const uint8_t *const p01 = p00 + 4;
            const uint8_t *const p10 = p00 + srcPitch;
            const uint8_t *const p11 = p10 + 4;
            int16_t b[4], s[4], d[4];
            for (int c = 0; c < 4; ++c) {
                const int16_t a = static_cast<int16_t>(p11[c] - p00[c]);
                b[c] = static_cast<int16_t>(p10[c] - p01[c]);
                s[c] = static_cast<int16_t>(a + b[c]);
                d[c] = static_cast<int16_t>(a - b[c]);
            }
            const auto madd = [&](const int16_t *v, int c) {
                return static_cast<int32_t>(static_cast<uint32_t>(v[c] * weight[c]) +
                                            static_cast<uint32_t>(v[c + 1] * weight[c + 1]));
            };
            const int32_t s0 = madd(s, 0), s1 = madd(s, 2), d0 = madd(d, 0), d1 = madd(d, 2);
            // b's first two channels as one dword (the kernel adds them to S's first sum as it packs).
            const uint32_t b01 = static_cast<uint32_t>(static_cast<uint16_t>(b[0])) |
                                 static_cast<uint32_t>(static_cast<uint16_t>(b[1])) << 16;
            int16_t m[4];
            m[0] = Sat16(static_cast<int32_t>(b01 + static_cast<uint32_t>(s0)));
            m[1] = Sat16(static_cast<int32_t>(static_cast<uint32_t>(s0) + static_cast<uint32_t>(s1)));
            m[2] = Sat16(static_cast<int32_t>(static_cast<uint32_t>(d0) + static_cast<uint32_t>(d1)));
            m[3] = 0;
            const int32_t index =
                static_cast<int32_t>(static_cast<uint32_t>(m[1] * m[1]) + static_cast<uint32_t>(m[2] * m[2])) >> 17;
            const uint8_t *const entry = table + 0x20 + 8 * index;
            uint32_t texel = 0;
            for (int c = 0; c < 4; ++c) {
                const int16_t v = static_cast<int16_t>((m[c] & mask[c]) | bits[c]);
                const int32_t scaled = (static_cast<int32_t>(v) * TableWord(entry, 2 * c)) >> 16;
                const int16_t sum = Sat16(scaled + offset[c]);
                const uint32_t byte = sum < 0 ? 0u : sum > 255 ? 255u : static_cast<uint32_t>(sum);
                texel |= byte << (8 * c);
            }
            out[x] = texel;
        }
        src += 2 * srcPitch;
        dst += dstPitch;
    }
}

} // namespace nTSSG
