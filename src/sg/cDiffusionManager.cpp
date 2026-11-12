// nTSSG::cDiffusionManager (lane 261109f, scenegraph LD1; src/sg/cDiffusionManager.h).
//
// Still the original's, called through OriginalFunction: the diffusion engine (row LD2: the floor colour grids' passes
// 0x405d70-0x40ac20 beyond LD2a's, LD2b's, LD2c's and LD2g's cFloorColorGrid members; the wall diffusion manager is
// all ours: its constructor and setters LD2d's, its Rebuild LD2k's, Step and the blob pass LD2f's, the blends LD2l's,
// the lights 0x40ed70 / 0x40ed90 LD2m's), the trivial setter 0x239840, the placement
// service 0x20e0b2, the scene graph
// system's getter 0x20defb, the stopwatch's Restart 0x1c95ec and
// slots 10 and 11's bodies (row LD1b; slots 8 and 9 are in cDiffusionManagerPortals.cpp).
#include "sg/cDiffusionManager.h"

#include "game/original.h"
#include "game/owned.h"
#include "game/vtable.h"
#include "gz/cGZMessageServer2.h"
#include "gz/cIGZMessage2Standard.h"
#include "misc/UtilitiesC.h" // KeyMapSlot5Walker
#include "rz/MiscHelpersB.h" // KeyMapWalker
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/StopwatchMore.h"
#include "rz/cRZMessage2Standard.h"
#include "sg/cLightMapManager.h" // cILightMapManager, LightMapStopwatch
#include "sg/SGUtilities.h" // the settings readers 0xaf7190 / 0xaf7210 / 0xaf7290
#include "sg/cWallDiffusionManager.h"
#include "world/WorldTileMath.h"
#include "world/cWorldDB.h"

#include <cstring>
#include <new>

// The passes' debug switches (bytes nothing in the image writes): .data 1, 1, 1, 1 and .bss 0, 0, 0, 0.
TS2_OWNED_STATIC(uint8_t, 100eb20); // slot 12: the blob pass (0x40edb0, 0x40fa40, the grids' 0x4065b0)
TS2_OWNED_STATIC(uint8_t, 100eb21); // slot 12: the walls' 0x40f0f0
TS2_OWNED_STATIC(uint8_t, 100eb22); // slot 13: the blob pass
TS2_OWNED_STATIC(uint8_t, 100eb23); // slot 13: 0x40f0f0
TS2_OWNED_STATIC(uint8_t, 10da3dc); // slot 12: the short pass (0x411c60, 0x40f5a0) instead of the full one
TS2_OWNED_STATIC(uint8_t, 10da3dd); // slot 12: the walls' 0x411c20 at the end
TS2_OWNED_STATIC(uint8_t, 10da3de); // slot 13: the short pass
TS2_OWNED_STATIC(uint8_t, 10da3df); // slot 13: 0x411c20

namespace nTSSG {

namespace {

constexpr uint32_t kHolderIid = 0xab7cc174; // the world-loaded message's object: the command parser's properties

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// ---- the engine (row LD2, the original's) ----

template <class... A> using GridFn = void(__thiscall *)(DiffusionGrid *, A...);

void Restart(cRZStopwatch *watch) { OriginalFunction<void(__thiscall *)(cRZStopwatch *), 0x1c95ec>()(watch); }

// The blob records as cFloorColorGrid::TakeBlobs reads them (the same 44 bytes: DiffusionBlob44's words as FloorBlob).
const std::vector<FloorBlob> *AsFloorBlobs(const std::vector<DiffusionBlob44> *blobs)
{
    static_assert(sizeof(FloorBlob) == sizeof(DiffusionBlob44), "FloorBlob layout");
    return reinterpret_cast<const std::vector<FloorBlob> *>(blobs);
}

void *System() { return OriginalFunction<void *(__cdecl *)(), 0x20defb>()(); }

nTSWorld::cIWorldDB *World()
{
    return static_cast<nTSWorld::cIWorldDB *>(OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()());
}

// The scene graph system's light map manager (its slot 48).
cILightMapManager *LightMaps()
{
    return static_cast<cILightMapManager *>(RawSlot<48, void *>(System())); // receiver: nTSSG::cTSSGSystem
}

} // namespace

// ---- the grids (row LD2's) -------------------------------------------------------------------------------------------

DiffusionGrid::DiffusionGrid() { cFloorColorGrid::Construct(this, nullptr); }

DiffusionGrid::DiffusionGrid(const DiffusionGrid &other)
{
    cFloorColorGrid::CopyConstruct(this, nullptr, reinterpret_cast<const cFloorColorGrid *>(&other));
}

// ---- the references --------------------------------------------------------------------------------------------------

void DiffusionRef::Assign(cIGZUnknown *object)
{
    if (object == mObject) return;
    if (object) object->AddRef(); // receiver: cIGZUnknown (slot 1)
    cIGZUnknown *const old = mObject;
    mObject = object;
    if (old) old->Release(); // receiver: cIGZUnknown (slot 2)
}

void DiffusionRef::Clear()
{
    if (cIGZUnknown *const old = mObject) {
        mObject = nullptr;
        old->Release(); // receiver: cIGZUnknown (slot 2)
    }
}

void DiffusionWallRef::Assign(void *object)
{
    if (object == mObject) return;
    if (object) RawSlot<2, void>(object); // receiver: nTSSG::cWallDiffusionManager (slot 2, AddRef)
    void *const old = mObject;
    mObject = object;
    if (old) RawSlot<1, void>(old); // receiver: nTSSG::cWallDiffusionManager (slot 1, Release)
}

void DiffusionWallRef::Clear()
{
    if (void *const old = mObject) {
        mObject = nullptr;
        RawSlot<1, void>(old); // receiver: nTSSG::cWallDiffusionManager (slot 1, Release)
    }
}

// ---- life ------------------------------------------------------------------------------------------------------------

cDiffusionManager::cDiffusionManager()
{
    mMessageIds.insert(kWorldLoaded);
    mMessageIds.insert(kChanged);
    mMessageIds.insert(kLevels);
}

// The members go in reverse: the lists, the grids, the walls, the partitions, the world, the message ids.
cDiffusionManager::~cDiffusionManager() = default;

__declspec(noinline) cDiffusionManager *__fastcall cDiffusionManager::Construct(void *memory, void *)
{
    return ::new (memory) cDiffusionManager;
}

bool cDiffusionManager::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == cIDiffusionManager::kIid || riid == kClass) {
        *out = static_cast<cIDiffusionManager *>(this);
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(riid, out);
}

uint32_t cDiffusionManager::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cDiffusionManager::Release() { return rz::cRZUnknown::Release(); }

// ---- the settings ----------------------------------------------------------------------------------------------------

void cDiffusionManager::ApplySettings()
{
    if (void *const object = mWalls.mObject) {
        // The wall setters (ours, sg/cWallDiffusionManager.h; the original calls the folded setters 0x20c6f0, 0x229f60,
        // 0x4099f0 and 0x411b70 for four of them, and 0x411b60 / 0x411b30).
        cWallDiffusionManager *const walls = static_cast<cWallDiffusionManager *>(object);
        walls->SetPropagationSteps(mWallPropagationSteps);
        walls->SetInsideWallAmbient(mInsideWallAmbient);
        walls->SetOutsideWallAmbient(mOutsideWallAmbient);
        walls->SetThreshold(mWallDiffusionThreshold);
        walls->SetInsideAmbientColor(mInsideAmbientColor);
        walls->SetOutsideAmbientColor(mOutsideAmbientColor);
    }
    for (uint32_t i = 0; i < mGrids.size(); ++i) {
        DiffusionGrid *const grid = mGrids.data() + i;
        // ORIGINAL-BUG: the grids are given the wall propagation steps; floorPropagationSteps (+0x64) is read from the
        // settings but never used.
        // (0x239840 and 0x4099f0 are shared trivial setters.)
        using SetInt = GridFn<int32_t>;
        using SetFloat = GridFn<float>;
        OriginalFunction<SetInt, 0x239840>()(grid, mWallPropagationSteps);      // receiver: nTSSG::cFloorColorGrid
        OriginalFunction<SetFloat, 0x4098a0>()(grid, mInsideFloorAmbient);      // receiver: nTSSG::cFloorColorGrid
        OriginalFunction<SetFloat, 0x4099f0>()(grid, mOutsideFloorAmbient);     // receiver: nTSSG::cFloorColorGrid
        OriginalFunction<SetFloat, 0x409890>()(grid, mFloorDiffusionThreshold); // receiver: nTSSG::cFloorColorGrid
        grid->Grid()->SetInsideAmbientColor(mInsideAmbientColor);
        grid->Grid()->SetOutsideAmbientColor(mOutsideAmbientColor);
    }
}

void cDiffusionManager::ResizeGrids()
{
    void *const floors = RawSlot<38, void *>(System()); // receiver: nTSSG::cTSSGSystem (the floor manager)
    nTSWorld::cIWorldDB *const world = World();
    const int32_t count = world->GetLevelCount(); // receiver: nTSWorld::cWorldDB
    mBaseLevel = world->GetMinLevel();            // receiver: nTSWorld::cWorldDB
    mGrids.resize(static_cast<uint32_t>(count));
    for (uint32_t i = 0; i < mGrids.size(); ++i) {
        // Slot 19: the level's floor.
        void *const floor = RawSlot<19, void *>(floors, mBaseLevel + static_cast<int32_t>(i)); // receiver: nTSSG::cFloorManager
        if (!floor) continue;
        DiffusionGrid *const grid = mGrids.data() + i;
        const uint32_t c = RawSlot<23>(floor); // receiver: nTSSG::cFloor
        const uint32_t b = RawSlot<22>(floor); // receiver: nTSSG::cFloor
        const uint32_t a = RawSlot<20>(floor); // receiver: nTSSG::cFloor
        grid->Grid()->SetSize(static_cast<int32_t>(a), b, c);
    }
}

void cDiffusionManager::ReadSettings(void *properties)
{
    if (!properties) return;
    // Each reader sets `changed` when it changed its value; what an earlier reader said is carried past a later one that
    // clears it.
    uint8_t changed = 0;
    nGZSceneGraph::ReadFloatProperty(properties, "brightWallFloorContribution", &mBrightWallFloorContribution,
                                            &changed);
    bool any = changed != 0;
    changed = any;
    nGZSceneGraph::ReadFloatProperty(properties, "darkWallFloorContribution", &mDarkWallFloorContribution,
                                            &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadFloatProperty(properties, "insideFloorAmbient", &mInsideFloorAmbient, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadFloatProperty(properties, "outsideFloorAmbient", &mOutsideFloorAmbient, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadFloatProperty(properties, "floorDiffusionThreshold", &mFloorDiffusionThreshold, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadFloatProperty(properties, "insideWallAmbient", &mInsideWallAmbient, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadFloatProperty(properties, "outsideWallAmbient", &mOutsideWallAmbient, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadFloatProperty(properties, "wallDiffusionThreshold", &mWallDiffusionThreshold, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadVector3Property(properties, "insideDiffusionAmbientColor", mInsideAmbientColor, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadVector3Property(properties, "outsideDiffusionAmbientColor", mOutsideAmbientColor,
                                             &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadIntProperty(properties, "floorPropagationSteps", &mFloorPropagationSteps, &changed);
    any = changed != 0 || any;
    changed = any;
    nGZSceneGraph::ReadIntProperty(properties, "wallPropagationSteps", &mWallPropagationSteps, &changed);
    if (changed == 0 && !any) return;
    if (!mStarted) return;
    ApplySettings();
    void *const memory = RZMemory::Malloc(cRZMessage2Standard::kSize);
    auto *const message = reinterpret_cast<cIGZMessage2Standard *>(
        memory ? static_cast<cRZMessage2Standard *>(memory)->Construct() : nullptr);
    if (message) message->AddRef();
    // ORIGINAL-BUG: a failed allocation is used anyway.
    cIGZMessage2Standard *volatile posted = message;
    posted->SetType(kChanged); // receiver: cRZMessage2Standard
    posted->SetData1(0);       // receiver: cRZMessage2Standard
    auto *const server = static_cast<cIGZMessageServer2 *>(GetCachedMessageServer());
    server->MessagePost(message, false); // receiver: cIGZMessageServer2
    if (message) message->Release(); // receiver: cRZMessage2Standard
}

bool cDiffusionManager::DoMessage(void *data)
{
    auto *const message = static_cast<cIGZMessage2Standard *>(data);
    const uint32_t type = message->GetType(); // receiver: cIGZMessage2Standard (the server's messages)
    if (type == kLevels) {
        mDirty = 1;
        ResizeGrids();
        return true;
    }
    if (type == kWorldLoaded) {
        // ORIGINAL-BUG: a message without its object is read through a null pointer.
        cIGZUnknown *const object = message->GetIGZUnknown(); // receiver: cIGZMessage2Standard
        uint8_t *holder = nullptr;
        bool result = false;
        // The message's object (its cIGZUnknown face).
        if (object->QueryInterface(kHolderIid, reinterpret_cast<void **>(&holder))) { // receiver: cIGZUnknown
            // The holder's virtual bases through the vbtable at +8: [2] the property set the readers take.
            void *properties = nullptr;
            if (holder) properties = holder + 8 + reinterpret_cast<const int32_t *const *>(holder + 8)[0][2];
            ReadSettings(properties);
            result = true;
        }
        if (holder) {
            // The holder's cIGZUnknown virtual base (vbtable [1]).
            const int32_t *const vbtable = reinterpret_cast<const int32_t *const *>(holder + 8)[0];
            auto *const unknown = reinterpret_cast<cIGZUnknown *>(holder + 8 + vbtable[1]);
            unknown->Release(); // receiver: cIGZUnknown
        }
        return result;
    }
    if (type == kChanged) {
        const uint32_t what = message->GetData1(); // receiver: cIGZMessage2Standard
        if (what == 1 || what == 4) mDirty = 1;
        return true;
    }
    return false;
}

// ---- the director's slots -------------------------------------------------------------------------------------------

bool cDiffusionManager::Init()
{
    if (!mInitialized) {
        mInitialized = 1;
        mWorld.Assign(World());
        // Slot 39, the partitions (the world isn't checked).
        auto *const world = static_cast<nTSWorld::cIWorldDB *>(mWorld.mObject);
        mPartitions.Assign(static_cast<cIGZUnknown *>(world->GetPartitions())); // receiver: nTSWorld::cWorldDB
        cIGZMessageTarget2 *const target = this;
        reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(target), nullptr);
        mStarted = 0;
    }
    return true;
}

bool cDiffusionManager::Shutdown()
{
    if (mInitialized) {
        mInitialized = 0;
        cIGZMessageTarget2 *const target = this;
        reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(target), nullptr);
        mWorld.Clear();
        mPartitions.Clear();
        mGrids.clear();
        mBaseLevel = 0;
        mWalls.Clear();
    }
    return true;
}

void cDiffusionManager::Start()
{
    void *const memory = ::operator new(0x60, std::nothrow);
    void *const walls = memory ? cWallDiffusionManager::Construct(memory, nullptr) : nullptr; // 0x40be50 (ours)
    mWalls.Assign(walls);
    // (The original then runs 0x140fe0 on a temporary: an empty shared body.)
    mDirty = 1;
    ResizeGrids();
    ApplySettings();
    mStarted = 1;
}

void cDiffusionManager::Stop()
{
    mGrids.clear();
    mBaseLevel = 0;
    mWalls.Clear();
    mStarted = 0;
}

void cDiffusionManager::SetSettings(const float *values)
{
    std::memcpy(&mInsideFloorAmbient, &values[0], 4);
    std::memcpy(&mInsideWallAmbient, &values[1], 4);
    std::memcpy(mInsideAmbientColor, &values[2], 12);
    std::memcpy(&mWallDiffusionThreshold, &values[10], 4);
    std::memcpy(&mFloorDiffusionThreshold, &values[11], 4);
    ApplySettings();
}

// The passes. The stopwatches are timed (Restart) around each step but only A is stopped (its destructor 0x140fe0 is
// empty); the grids are counted once, and the wall manager re-read at each call.
void cDiffusionManager::Diffuse(uint32_t arg)
{
    cILightMapManager *const lights = LightMaps();
    const float floorScale = lights->GetScale(1); // receiver: nTSSG::cLightMapManager
    const float wallScale = lights->GetScale(0);  // receiver: nTSSG::cLightMapManager
    LightMapStopwatch total;
    Restart(&total.value);
    const int32_t count = static_cast<int32_t>(mGrids.size());
    LightMapStopwatch step;
    for (int32_t i = 0; i < count; ++i) {
        if (mDirty) {
            Restart(&step.value);
            mGrids[i].Grid()->Rebuild();
        }
        Restart(&step.value);
        mGrids[i].Grid()->Step(reinterpret_cast<const std::vector<FloorLight> *>(static_cast<uintptr_t>(arg)));
    }
    Restart(&total.value);
    for (int32_t i = 0; i < count; ++i)
        mGrids[i].Grid()->ShadeLights(lights, floorScale);
    if (mDirty) {
        Restart(&total.value);
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->Rebuild();
    }
    if (ts2_rva_10da3dc) {
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->GradientPass();
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->BorderPass();
    } else {
        Restart(&total.value);
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->AmbientPass();
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->Step(reinterpret_cast<const std::vector<WallLight> *>(static_cast<uintptr_t>(arg)));
        Restart(&total.value);
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->Blend(mBrightWallFloorContribution, mDarkWallFloorContribution);
        if (ts2_rva_100eb21) {
            Restart(&total.value);
            static_cast<cWallDiffusionManager *>(mWalls.mObject)->DiffuseMarked();
        }
        Restart(&total.value);
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->BorderPass();
        if (ts2_rva_100eb20) {
            Restart(&total.value);
            static_cast<cWallDiffusionManager *>(mWalls.mObject)->BlobPass();
            Restart(&total.value);
            std::vector<DiffusionBlob44> blobs;
            static_cast<cWallDiffusionManager *>(mWalls.mObject)->CollectBlobs(&blobs);
            for (int32_t i = 0; i < count; ++i)
                mGrids[i].Grid()->TakeBlobs(AsFloorBlobs(&blobs));
        }
    }
    Restart(&total.value);
    if (ts2_rva_10da3dd) static_cast<cWallDiffusionManager *>(mWalls.mObject)->TestColorPass();
    static_cast<cWallDiffusionManager *>(mWalls.mObject)->ApplyAllLights(lights, wallScale);
    stopwatch_more::Stop(&total.value, nullptr);
    mDirty = 0;
}

void cDiffusionManager::DiffuseUnlit(uint32_t arg)
{
    LightMapStopwatch total;
    Restart(&total.value);
    const int32_t count = static_cast<int32_t>(mGrids.size());
    LightMapStopwatch step;
    for (int32_t i = 0; i < count; ++i) {
        if (mDirty) {
            Restart(&step.value);
            mGrids[i].Grid()->Rebuild();
        }
        Restart(&step.value);
        mGrids[i].Grid()->Step(reinterpret_cast<const std::vector<FloorLight> *>(static_cast<uintptr_t>(arg)));
    }
    if (mDirty) {
        Restart(&total.value);
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->Rebuild();
    }
    if (ts2_rva_10da3de) {
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->GradientPass();
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->BorderPass();
    } else {
        Restart(&total.value);
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->AmbientPass();
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->Step(reinterpret_cast<const std::vector<WallLight> *>(static_cast<uintptr_t>(arg)));
        Restart(&total.value);
        // `this` as the floors: cIDiffusionManager is our first base, so the pointer is the original's own.
        static_cast<cWallDiffusionManager *>(mWalls.mObject)
            ->BlendUnlit(mBrightWallFloorContribution, mDarkWallFloorContribution, this);
        if (ts2_rva_100eb23) {
            Restart(&total.value);
            static_cast<cWallDiffusionManager *>(mWalls.mObject)->DiffuseMarked();
        }
        Restart(&total.value);
        static_cast<cWallDiffusionManager *>(mWalls.mObject)->BorderPass();
        if (ts2_rva_100eb22) {
            Restart(&total.value);
            static_cast<cWallDiffusionManager *>(mWalls.mObject)->BlobPass();
            Restart(&total.value);
            std::vector<DiffusionBlob44> blobs;
            static_cast<cWallDiffusionManager *>(mWalls.mObject)->CollectBlobs(&blobs);
            for (int32_t i = 0; i < count; ++i)
                mGrids[i].Grid()->TakeBlobs(AsFloorBlobs(&blobs));
        }
    }
    if (ts2_rva_10da3df) static_cast<cWallDiffusionManager *>(mWalls.mObject)->TestColorPass();
    mDirty = 0;
}

void cDiffusionManager::ReadLightMaps()
{
    cILightMapManager *const lights = LightMaps();
    const float floorScale = lights->GetScale(1); // receiver: nTSSG::cLightMapManager
    const float wallScale = lights->GetScale(0);  // receiver: nTSSG::cLightMapManager
    LightMapStopwatch total;
    Restart(&total.value);
    const int32_t count = static_cast<int32_t>(mGrids.size());
    for (int32_t i = 0; i < count; ++i)
        mGrids[i].Grid()->ShadeLights(lights, floorScale);
    Restart(&total.value);
    static_cast<cWallDiffusionManager *>(mWalls.mObject)->ApplyAllLights(lights, wallScale);
    stopwatch_more::Stop(&total.value, nullptr);
}

void cDiffusionManager::ReadLightMapsAndCollect()
{
    cILightMapManager *const lights = LightMaps();
    const float floorScale = lights->GetScale(1); // receiver: nTSSG::cLightMapManager
    const float wallScale = lights->GetScale(0);  // receiver: nTSSG::cLightMapManager
    LightMapStopwatch total;
    Restart(&total.value);
    const int32_t count = static_cast<int32_t>(mGrids.size());
    for (int32_t i = 0; i < count; ++i)
        mGrids[i].Grid()->ShadeChangedLights(lights, floorScale);
    Restart(&total.value);
    static_cast<cWallDiffusionManager *>(mWalls.mObject)->ApplyChangedLights(lights, wallScale);
    stopwatch_more::Stop(&total.value, nullptr);
    mTiles.clear();
    for (int32_t i = 0; i < count; ++i)
        mGrids[i].Grid()->CollectTiles(&mTiles);
    mWallList.clear();
    static_cast<cWallDiffusionManager *>(mWalls.mObject)->CollectWalls(&mWallList);
    static_cast<cWallDiffusionManager *>(mWalls.mObject)->CollectWalls2(&mWallList);
}

void cDiffusionManager::CollectTiles(std::vector<DiffusionTile> *out)
{
    out->clear();
    for (uint32_t i = 0; i < mGrids.size(); ++i)
        mGrids[i].Grid()->CollectTilesA(out);
}

void cDiffusionManager::CollectWalls(std::vector<DiffusionWall> *out)
{
    out->clear();
    static_cast<cWallDiffusionManager *>(mWalls.mObject)->CollectWalls(out);
}

void cDiffusionManager::CollectTiles2(std::vector<DiffusionTile> *out)
{
    out->clear();
    for (uint32_t i = 0; i < mGrids.size(); ++i)
        mGrids[i].Grid()->CollectTilesB(out);
}

void cDiffusionManager::CollectWalls2(std::vector<DiffusionWall> *out)
{
    out->clear();
    static_cast<cWallDiffusionManager *>(mWalls.mObject)->CollectWalls2(out);
}

void cDiffusionManager::PassChanges()
{
    void *const system = System();
    void *const floors = RawSlot<38, void *>(system); // receiver: nTSSG::cTSSGSystem (the floor manager)
    RawSlot<18, void>(floors, &mTiles);                // receiver: nTSSG::cFloorManager
    void *const walls = RawSlot<34, void *>(system);  // receiver: nTSSG::cTSSGSystem (the wall manager)
    RawSlot<25, void>(walls, &mWallList);              // receiver: nTSSG::cWallManager
}

void cDiffusionManager::Reset()
{
    for (uint32_t i = 0; i < mGrids.size(); ++i)
        mGrids[i].Grid()->Reset();
    static_cast<cWallDiffusionManager *>(mWalls.mObject)->Reset();
}

void cDiffusionManager::Mark(uint32_t a, uint32_t b)
{
    for (uint32_t i = 0; i < mGrids.size(); ++i)
        mGrids[i].Grid()->Mark(a, reinterpret_cast<const float *>(static_cast<uintptr_t>(b)));
    static_cast<cWallDiffusionManager *>(mWalls.mObject)->Mark(static_cast<int32_t>(a), reinterpret_cast<const float *>(static_cast<uintptr_t>(b)));
}

float *cDiffusionManager::GetFloorColor(float *out, const float *position)
{
    // The position's tile is worked out and dropped; the level is the position's third word taken as an integer.
    worldmath::TileQuadrant unused;
    worldmath::PositionToTileQuadrant(&unused, position);
    int32_t level;
    std::memcpy(&level, &position[2], 4);
    mGrids[level - mBaseLevel].Grid()->GetColor(out, position);
    return out;
}

float *cDiffusionManager::GetWallColor(float *out, uint32_t wall)
{
    static_cast<cWallDiffusionManager *>(mWalls.mObject)->GetColor(out, reinterpret_cast<const float *>(static_cast<uintptr_t>(wall)));
    return out;
}

} // namespace nTSSG
