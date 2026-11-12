// nTSSG::cTSSGSystem: the start-up members (lane IR). See src/sg/cTSSGSystem.h for the layout and what each does.
#include "sg/cTSSGSystem.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cIGZPropertySet.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StringKeyParser.h"
#include "rz/StaticGetters.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "sg/SGUtilities.h"
#include "ts/cTSCatalog.h" // GZStringList
#include "world/WorldColliders.h"

#include <new>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

// The framework's slot 3 (class id, interface id, out): the object of that class, as that interface, in `out`.
constexpr int kFrameworkGetClassObject = 3;
// cIGZFrameWork slots on GetServiceObject's object.
constexpr int kFrameworkAddSystemService = 9; // the system service face
constexpr int kFrameworkGetSystemService = 5; // (class id, interface id, out)
constexpr int kFrameworkCommandLine = 21;     // the cIGZCmdLine
// Slots every sub-manager answers.
constexpr int kManagerInit = 3;
// The property notifier (class 0xac1887e as 0x6bb22f38): slot 4 watches a property (target, message id, name, default
// or null), slot 3 gives the property set (slot 3 sets a property: name, value).
constexpr uint32_t kPropertyClass = 0x0ac1887e;
constexpr uint32_t kPropertyNotifierIid = 0x6bb22f38;
constexpr int kNotifierWatch = 4;
constexpr int kNotifierPropertySet = 3;
constexpr int kPropertySetSet = 3;

// The option lists (cTSAppCommonImpl slot 5: the files of a search-path option).
constexpr uint32_t kLightRigFiles = 0x4c9f79fd;
constexpr uint32_t kListB4Files = 0x8c9f7a34;
constexpr uint32_t kListB4MoreFiles = 0x2ea751e4;
constexpr uint32_t kSceneFiles = 0xac9f7a5a;
constexpr uint32_t kParserFiles = 0xcc9f7985;

// The names of the render options (the image's text) and the command-line switch.
constexpr uint32_t kAssetServerSwitchText = 0xdaee9c; // 19 characters
constexpr uint32_t kAssetServerSwitchLength = 19;
constexpr uint32_t kLightBoundsInflateName = 0xdae784;

// The boolean (and two uint32) options CreateManagers watches, in its order, with their defaults.
constexpr uint32_t kUint32Option = 0x100; // value field: an AnyUint32 with the low byte
struct Option {
    uint32_t name;
    uint32_t message;
    uint32_t value; // the boolean's byte, or kUint32Option | the uint32
};
constexpr Option kOptions[] = {
    {0xdae524, 0x0be33684, 1},
    {0xdae52c, 0x8be6f77b, 1},                  // simShadows
    {0xdae538, 0x8be6f77c, 1},                  // objectShadows
    {0xdae548, 0x2beb0d4c, 0},                  // renderInsideObjectOnlyOnSelectedSimLevel
    {0xdae574, 0x8dc9d24a, kUint32Option | 0},  // renderInsideVisibleObjects
    {0xdae590, 0xabfc20da, 0},                  // animationLOD
    {0xdae5a0, 0x0c7bfea6, 0},                  // animationFrameSampling
    {0xdae5b8, 0x0cdc6794, kUint32Option | 6},  // animationCacheSize
    {0xdae5cc, 0x0cdc679b, 1},                  // animationPermanentCache
    {0xdae5e4, 0xccdc9639, 1},                  // animationPrediction
    {0xdae50c, 0x4ef8bd75, 1},                  // animationBackgroundLoad
    {0xdae5f8, 0x2d8812c4, 0},                  // debugCinematics
    {0xdae608, 0x8c1827f2, 0},                  // displayPaths
    {0xdae618, 0xac1827fc, 0},                  // displayLookAtBoxes
    {0xdae62c, 0x8d9a22c3, 0},                  // displayMissingAnimations
    {0xdae648, 0x2cea9fb2, 1},                  // animationTimerIsMaster
    {0xdae660, 0xcc471cc8, 1},                  // carsEnabled
    {0xdae66c, 0xec3f494d, 0},                  // carsCompact
    {0xdae678, 0xac730900, 1},                  // lotTerrainCanvas
    {0xdae68c, 0xac730901, 1},                  // lotTerrainPaints
    {0xdae6a0, 0xac730902, 1},                  // lotWater
    {0xdae6ac, 0x4c7ab209, 1},                  // lotTerrainLighting
    {0xdae6c0, 0x4c60901a, 1},                  // displayLotImposters
    {0xdae6d4, 0x2cae6756, 1},                  // displayNeighborhoodWater
    {0xdae6f0, 0x6c618b86, 1},                  // displayNeighborhoodFlora
    {0xdae70c, 0x4c618b90, 1},                  // displayNeighborhoodRoads
    {0xdae728, 0xac618b98, 1},                  // displayNeighborhoodRoadsWithModel
    {0xdae74c, 0x0c618ba0, 1},                  // displayNeighborhoodProps
};
// Watched without a default (the notifier gets a null value).
constexpr Option kPlainOptions[] = {
    {0xdae4b8, 0x2f9dc7f9, 0}, // lotSkirtSizeIncrease
    {0xdae4f4, 0x4f9dc80b, 0}, // enableLotImpostersInLot
    {0xdae4d0, 0x4f9dc81a, 0}, // enableNeighborhoodOccupantsInLot
};

// A cRZString local (the game's hook-shaped string: Construct / Destruct by hand).
struct LocalString {
    cRZString s;
    LocalString() { s.Construct(); }
    explicit LocalString(const char *text) { s.Construct(text); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// A virtual base of an object built with virtual inheritance: the virtual-base table pointer at +vbptr, entry `entry`.
void *VirtualBase(void *object, uint32_t vbptr, int entry)
{
    auto *at = static_cast<uint8_t *>(object) + vbptr;
    const int32_t *table = *reinterpret_cast<int32_t *const *>(at);
    return at + table[entry];
}

void ReleaseRef(void *object) { Call<2>(object); }

// The framework (GetDirectorFrameworkObject, fetched first) gives `ref` the object of `clsid` as `iid`; the old reference is
// released and cleared before the request.
bool Acquire(nTSSG::SGRef &ref, uint32_t clsid, uint32_t iid)
{
    void *framework = GetDirectorFrameworkObject();
    if (void *old = ref.mObject) {
        ref.mObject = nullptr;
        ReleaseRef(old);
    }
    return Call<kFrameworkGetClassObject, uint8_t>(framework, clsid, iid, &ref.mObject) != 0;
}

// Acquire, then the manager's Init (no null test, as the original).
void AcquireAndInit(nTSSG::SGRef &ref, uint32_t clsid, uint32_t iid)
{
    Acquire(ref, clsid, iid);
    Call<kManagerInit>(ref.mObject);
}

// Assigns `object` to `ref` (AddRef on the new one, Release on the old, nothing when they are the same).
void Assign(nTSSG::SGRef &ref, void *object)
{
    void *old = ref.mObject;
    if (object == old) return;
    if (object) Call<1>(object);
    ref.mObject = object;
    if (old) ReleaseRef(old);
}

uint32_t ListWord(GZStringList &list) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&list)); }

} // namespace

namespace nTSSG {

void cTSSGSystem::CreateOverlay(uint32_t index)
{
    // 0x248c80: the framework is fetched before the old overlay goes.
    void *framework = GetDirectorFrameworkObject();
    SGRef &ref = mOverlays[index];
    if (void *old = ref.mObject) {
        ref.mObject = nullptr;
        ReleaseRef(old);
    }
    Call<kFrameworkGetClassObject, uint8_t>(framework, 0xac22fa8du, 0xac22fa89u, &ref.mObject);
    if (ref.mObject) Call<kManagerInit>(ref.mObject);
}

void cTSSGSystem::ReadLotTunables(void *properties)
{
    nGZSceneGraph::ReadFloatProperty(properties, GameAddress<const char>(0xdaf038), &mLotTunables[0], nullptr);
    nGZSceneGraph::ReadFloatProperty(properties, GameAddress<const char>(0xdaf048), &mLotTunables[1], nullptr);
    nGZSceneGraph::ReadFloatProperty(properties, GameAddress<const char>(0xdaf058), &mLotTunables[2], nullptr);
}

void cTSSGSystem::AddProceduralAnimation(void *animation, const char *name)
{
    // 0x24a280: the animation's virtual base names it, its key object takes the parsed key, the animation system adds it.
    Call<12>(VirtualBase(animation, 4, 1), name, 0u);
    StringKeyParser::Result key{};
    StringKeyParser::ParseKey(&key, name, 0xfb00791du, nullptr);
    void *keyed = Call<8, void *>(VirtualBase(animation, 4, 1));
    Call<4>(keyed, &key);
    Call<13>(mAnimSystem.mObject, animation);
}

void cTSSGSystem::AddProceduralAnimations()
{
    // 0x248f90: one local reference reused for the three builders; released (through the virtual base) before each
    // rebuild, and at the end without clearing.
    struct Held {
        void *object = nullptr;
        ~Held()
        {
            if (object) ReleaseRef(VirtualBase(object, 4, 1));
        }
        void Clear()
        {
            if (void *old = object) {
                object = nullptr;
                ReleaseRef(VirtualBase(old, 4, 1));
            }
        }
    } held;
    using Build = uint8_t(__cdecl *)(void **);
    using BuildFlag = uint8_t(__cdecl *)(uint32_t, void **);
    if (OriginalFunction<Build, 0x3a27f0>()(&held.object)) AddProceduralAnimation(held.object, GameAddress<const char>(0xdae7c0));
    held.Clear();
    if (OriginalFunction<BuildFlag, 0x3a25e0>()(1, &held.object))
        AddProceduralAnimation(held.object, GameAddress<const char>(0xdae7e0));
    held.Clear();
    if (OriginalFunction<BuildFlag, 0x3a25e0>()(0, &held.object))
        AddProceduralAnimation(held.object, GameAddress<const char>(0xdae808));
}

void cTSSGSystem::CreateManagers()
{
    if (mManagersCreated) return;

    // The system service face to the framework, then the cheat and effects-constraint registrations.
    Call<kFrameworkAddSystemService>(GetServiceObject(), &mServiceVTable);
    OriginalFunction<void(__cdecl *)(), 0x3a18b0>()();
    world_colliders::RegisterWorldObjects(static_cast<cITSEffectsManager *>(OriginalFunction<void *(__cdecl *)(), 0xb39000>()()));
    mStartupWatch.Start();
    CreateOverlay(0);
    CreateOverlay(1);
    CreateOverlay(5);
    CreateOverlay(6);
    CreateOverlay(8);

    AcquireAndInit(mLightMapManager, 0x896d0177, 0x696d015c);
    AcquireAndInit(mShadowManager, 0x0bf9c04f, 0xcbf9c006);
    AcquireAndInit(mTerrain, 0x6b3a33c7, 0x6b40d998);
    AcquireAndInit(mFloorManager, 0xe9945f8d, 0xe9945f87);
    AcquireAndInit(mWallManager, 0xe989cf03, 0xe989cefc);
    AcquireAndInit(mFenceManager, 0xeab6b59e, 0xeab6b591);
    AcquireAndInit(mPoolManager, 0xac7486ae, 0x8c748697);
    AcquireAndInit(mLotSkirt, 0xec439394, 0xec439389);
    AcquireAndInit(mRugManager, 0xef5e4e99, 0xef5e4e95);
    AcquireAndInit(mCurvedPoolManager, 0xd16de942, 0xd16de93f);
    AcquireAndInit(mTerrainCutoutManager, 0x31884107, 0x31884104);
    AcquireAndInit(mAwningManager, 0x504d1938, 0x504d1930);
    AcquireAndInit(mSeasonManager, 0xf24e3352, 0x124e335c);
    Acquire(mQueryStrategyManager, 0x4988a36a, 0x6988a371);
    AcquireAndInit(mLightRigManager, 0x696d3daf, 0x696d3da9);

    GZStringList files; // one list for every option: slot 5 empties it before filling
    // The light rigs: the first file that exists.
    appinit::CommonImplSlot5(kLightRigFiles, ListWord(files), 1, 0, 0xffffffff);
    for (const GZString &file : files) {
        LocalString path(file.c_str());
        if (cRZFile::PathExists(*path.s.AsInterface())) {
            Call<5>(mLightRigManager.mObject, path.s.ToChar());
            break;
        }
    }

    AcquireAndInit(mSceneManager, 0x69512c6d, 0x69512c64);
    AcquireAndInit(mTerrainSnowManager, 0x5224dfdf, 0x5224dfdb);
    AcquireAndInit(mLightingManager, 0x0b390bf7, 0x6b390bfe);
    Call<5>(mLightingManager.mObject);
    Assign(mRefB4, OriginalFunction<void *(__cdecl *)(), 0xb38ef0>()());

    // The +0xb4 object: the first existing file of each of its two lists (slot 17, a cRZString of the name).
    for (uint32_t option : {kListB4Files, kListB4MoreFiles}) {
        appinit::CommonImplSlot5(option, ListWord(files), 1, 0, 0xffffffff);
        for (const GZString &file : files) {
            LocalString path(file.c_str());
            if (cRZFile::PathExists(*path.s.AsInterface())) {
                LocalString name(path.s.ToChar());
                Call<17>(mRefB4.mObject, &name.s);
                break;
            }
        }
    }

    AcquireAndInit(mRoofManager, 0x2b940c24, 0x0b940c0e);
    AcquireAndInit(mLotImposterManager, 0x0bc4a38d, 0x0bc4a393);
    AcquireAndInit(mVideoBroadcastManager, 0x6c4f4a1a, 0x4c4f4a15);
    AcquireAndInit(m3DUIManager, 0x2c9a6255, 0x2c9a6250);
    AddProceduralAnimations();

    // The scene's file loader (the scene's slot 12): every existing file of its list, last first.
    if (void *loader = Call<12, void *>(mScene.mObject)) {
        appinit::CommonImplSlot5(kSceneFiles, ListWord(files), 1, 0, 0xffffffff);
        for (auto it = files.rbegin(); it != files.rend(); ++it) {
            LocalString path(it->c_str());
            if (cRZFile::PathExists(*path.s.AsInterface())) {
                LocalString name(path.s.ToChar());
                Call<7>(loader, &name.s);
            }
        }
    }

    // The command-file parser, its 500-byte target and its files (every existing one, last first).
    Acquire(mCommandParser, 0x5cf9459a, 0x1cf94583);
    void *target = nullptr;
    if (void *memory = ::operator new(500)) target = OriginalFunction<void *(__thiscall *)(void *, uint32_t), 0x3a1ae0>()(memory, 1);
    if (void *old = mParserTarget; target != old) {
        if (target) Call<1>(VirtualBase(target, 8, 1));
        mParserTarget = target;
        if (old) ReleaseRef(VirtualBase(old, 8, 1));
    }
    Call<kManagerInit>(mCommandParser.mObject);
    Call<5>(mCommandParser.mObject, mParserTarget ? VirtualBase(mParserTarget, 8, 2) : nullptr, 1u);
    appinit::CommonImplSlot5(kParserFiles, ListWord(files), 1, 0, 0xffffffff);
    for (auto it = files.rbegin(); it != files.rend(); ++it) {
        LocalString path(it->c_str());
        if (cRZFile::PathExists(*path.s.AsInterface())) Call<6>(mCommandParser.mObject, path.s.ToChar(), 5u);
    }
    Call<7>(mCommandParser.mObject);
    ReadLotTunables(Call<9, void *>(mCommandParser.mObject));

    // The render options: watched through the property notifier, each with its default.
    // The notifier is held for the rest of the call and released at its end (0x1cbd79, after the command line and the
    // server string, before the list).
    struct HeldRef {
        ScopedRef ref{nullptr};
        ~HeldRef() { ref.ReleaseObject(); }
    } held;
    if (void *framework = GetServiceObject())
        Call<kFrameworkGetSystemService>(framework, kPropertyClass, kPropertyNotifierIid, &held.ref.mObject);
    void *notifier = held.ref.mObject;
    void *target8 = &mMessageVTable;
    for (const Option &o : kOptions) {
        GZServiceString name(GameAddress<const char>(o.name));
        if (o.value & kUint32Option) {
            AnyUint32 value{GameAddress<const void>(gzany::kUint32VTable), o.value & 0xff};
            Call<kNotifierWatch>(notifier, target8, o.message, &name, &value);
        } else {
            AnyBoolean value{GameAddress<const void>(gzany::kBooleanVTable), uint8_t(o.value), {}};
            Call<kNotifierWatch>(notifier, target8, o.message, &name, &value);
        }
    }
    for (const Option &o : kPlainOptions) {
        GZServiceString name(GameAddress<const char>(o.name));
        Call<kNotifierWatch>(notifier, target8, o.message, &name, static_cast<void *>(nullptr));
    }
    {
        GZServiceString name(GameAddress<const char>(kLightBoundsInflateName));
        void *set = Call<kNotifierPropertySet, void *>(notifier);
        AnyUint32 value{GameAddress<const void>(gzany::kUint32VTable), 3};
        Call<kPropertySetSet>(set, &name, &value);
    }

    // With the app in debug mode and no asset collector yet, the asset-server switch on the command line names the server
    // the asset collector (got from the framework) is given.
    bool debug = false;
    if (GetCachedSingleton()) debug = Call<31, uint8_t>(GetCachedSingleton()) != 0;
    LocalString server;
    cRZCmdLine commandLine;
    commandLine.Construct(Call<kFrameworkCommandLine, cIGZCmdLine *>(GetServiceObject()));
    bool collect = false;
    if (!mAssetCollector.mObject && debug) {
        LocalString name;
        name.s.mString.assign(GameAddress<const char>(kAssetServerSwitchText), kAssetServerSwitchLength);
        collect = commandLine.IsSwitchPresent(*name.s.AsInterface(), *server.s.AsInterface(), true);
    }
    if (collect && Acquire(mAssetCollector, 0xeda217dc, 0xeda217d1)) Call<7>(mAssetCollector.mObject, server.s.ToChar());
    mManagersCreated = 1;
    commandLine.Destruct();
}

} // namespace nTSSG
