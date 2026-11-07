// cTSAudioAmbienceSystem (lane KF): the whole class in the standalone shape (src/audio/cTSAudioAmbienceSystem.h).
#include "audio/cTSAudioAmbienceSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"          // gz::AnyUint32
#include "gz/cIGZCOM.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cIGZPropertySetString.h"
#include "ifaces/cIGZSystemService.h"
#include "misc/UtilitiesC.h"     // KeyMapSlot5Walker
#include "rz/MiscHelpersA.h"     // cRZStd
#include "rz/MiscHelpersB.h"     // KeyMapWalker
#include "rz/Singleton_20ddfb.h" // GetServiceObject, GetCachedSingleton
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/StringKeyParser.h"  // cRZStringHelpers
#include "rz/cRZString.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A log line: a cRZStd for (mode 8, this file's name, line), then its slot 2 (cdecl) with the channel (the word at
// `channelCell`), the level, the format and the arguments.
constexpr uint32_t kSourceFile = 0xda9e80;
template <class... A> void Log(uint32_t channelCell, uint32_t line, uint32_t level, uint32_t format, A... args)
{
    cRZStd log;
    cRZStd *const object = log.Construct(8, Word(GameAddress<char>(kSourceFile)), line);
    VirtualSlot<void(__cdecl *)(cRZStd *, uint32_t, uint32_t, const char *, ...)>(object, 2)(
        object, *GameAddress<uint32_t>(channelCell), level, GameAddress<char>(format), args...);
}
constexpr uint32_t kAudioChannel = 0xff5f10;
constexpr uint32_t kTimingChannel = 0xff5f14;

// The framework service cached at 0x10f5194 (0x211051: filled from GetServiceObject the first time).
void *CachedFramework()
{
    void *&framework = *GameAddress<void *>(0x10f5194);
    if (!framework) framework = GetServiceObject();
    return framework;
}

// The persist resource manager as the 37-byte getter 0x20db69 fetches it (the service object's slot 5, class 0x056b906e,
// iid 0x656b8efc), released at scope end.
struct ResourceManagerRef {
    void *p = nullptr;
    ResourceManagerRef()
    {
        if (void *const service = GetServiceObject())
            As<ifaces::cIGZFrameWork>(service)->GetSystemService(0x056b906eu, 0x656b8efcu, &p);
    }
    ResourceManagerRef(const ResourceManagerRef &) = delete;
    ResourceManagerRef &operator=(const ResourceManagerRef &) = delete;
    ~ResourceManagerRef()
    {
        if (p) static_cast<cIGZUnknown *>(p)->Release();
    }
};

// The stopwatch's restart (0x1c95ec, original).
void Restart(cRZStopwatch *watch) { OriginalFunction<void(__thiscall *)(cRZStopwatch *), 0x1c95ec>()(watch); }

// A cRZString copy of `name` (the whole string), destroyed at scope end.
struct LocalString {
    union {
        cRZString s;
    };
    explicit LocalString(const cRZString &name) { s.Construct(name, 0, 0xffffffffu); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
};

// The interfaces the code asks objects for.
constexpr uint32_t kClockClass = 0xac4da0f7, kClockIid = 0xebbaa07d;
constexpr uint32_t kPrimitiveIid = 0xcbba8c73;
constexpr uint32_t kResourceIid = 0x4bb0d269;
constexpr uint32_t kPersistResourceIid = 0x456b8f1d;
constexpr uint32_t kModeMessageIid = 0x65297976;
constexpr uint32_t kLiveMode = 0x0bea28c2; // the app's mode object's slot 5 in live mode

// The resources' keys: type 0xebfee33f, group 0x4c1940c5, the instance words from the game's globals.
constexpr uint32_t kResourceType = 0xebfee33f, kResourceGroup = 0x4c1940c5;

// The message types (DoMessage).
enum : uint32_t {
    kMsgReady = 0xb2,
    kMsgLoadLot = 0xb3,
    kMsgStopB4 = 0xb4,
    kMsgLoadScene = 0xb6,
    kMsgStopB9 = 0xb9,
    kMsgRegistryState = 0x0b8d81d5,
    kMsgModeEnter = 0x4c3220e7,
    kMsgModeChange = 0xedbeead0,
    kMsgReload = 0xb2b28373,
};

// Original callees nobody has replaced (the scene's constructor, the layers' and the scene's loaders, the curve's load and
// evaluation, the primitive registry).
cIGZUnknown *NewScene()
{
    void *const memory = ::operator new(0x5c);
    return memory ? OriginalFunction<cIGZUnknown *(__thiscall *)(void *), 0x23e694>()(memory) : nullptr;
}

} // namespace

// ---- construction ----

// 0x2229db (438 B): the service base (id 0x0c1e612a, priority -0x2dc6c0), the members in order (+0x31..+0x33, +0xcc and
// +0x134 not written), then the message ids.
cTSAudioAmbienceSystem::cTSAudioAmbienceSystem() : rz::cRZSystemService(kServiceId, kPriority)
{
    mActive = 0;
    mState = {};
    mTickWatch.Construct(3);
    mBudgetWatch.Construct(3);
    for (uint32_t id : {uint32_t(kMsgLoadScene), uint32_t(kMsgStopB9), uint32_t(kMsgReady), uint32_t(kMsgLoadLot),
                        uint32_t(kMsgStopB4), uint32_t(kMsgRegistryState), uint32_t(kMsgModeEnter),
                        uint32_t(kMsgModeChange), uint32_t(kMsgReload)})
        mMessageIds.insert(id);
}

// 0x222c12: the members in reverse order (the scale map first, the message ids last).
cTSAudioAmbienceSystem::~cTSAudioAmbienceSystem() noexcept = default;

// 0x22460b.
cTSAudioAmbienceSystem *__cdecl cTSAudioAmbienceSystem::Create() { return new cTSAudioAmbienceSystem; }

cTSAudioAmbienceSystem *__fastcall cTSAudioAmbienceSystemAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSAudioAmbienceSystem;
}

// ---- cIGZUnknown ----

// 0x225284: 1 -> the message target face (+4), cITSAudioAmbienceSystem -> this, counted through the primary face's AddRef;
// else the service's QueryInterface.
bool cTSAudioAmbienceSystem::QueryInterface(uint32_t riid, void **out)
{
    if (riid == 1) *out = static_cast<cIGZUnknown *>(static_cast<cIGZMessageTarget2 *>(this));
    else if (riid == kIid) *out = static_cast<cITSAudioAmbienceSystem *>(this);
    else return rz::cRZSystemService::QueryInterface(riid, out);
    static_cast<cITSAudioAmbienceSystem *>(this)->AddRef();
    return true;
}

uint32_t cTSAudioAmbienceSystem::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSAudioAmbienceSystem::Release() { return rz::cRZSystemService::Release(); }

// ---- cITSAudioAmbienceSystem / cIGZSystemService ----

// 0x224bd4 (263 B): the clock (framework class 0xac4da0f7), a new scene (its slot 3 must succeed), the two resources, the
// message ids, the tick watch started, the service and its tick registered, the layers built (0x224ce3), the service
// running. False at the first step that fails (what is held stays).
bool cTSAudioAmbienceSystem::Init()
{
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    com->GetClassObject(kClockClass, kClockIid, mClock.AsPPVoid());
    if (!mClock) return false;
    mScene.Assign(NewScene());
    if (!RawSlot<3, bool>(mScene.Get())) return false;
    if (!LoadResource64()) return false;
    if (!LoadNames()) return false;
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(MessageTarget()), nullptr);
    mTickWatch.Start();
    cIGZSystemService *const service = Service();
    As<ifaces::cIGZFrameWork>(CachedFramework())->AddSystemService(As<ifaces::cIGZSystemService>(service));
    As<ifaces::cIGZFrameWork>(CachedFramework())->AddToTick(As<ifaces::cIGZSystemService>(service));
    OriginalFunction<void(__thiscall *)(cTSAudioAmbienceSystem *), 0x224ce3>()(this); // the layers
    service->SetServiceRunning(true);
    service->SetServiceRunning(true);
    return true;
}

// 0x2254bf (486 B): inactive, the service and its tick unregistered, the message ids dropped, the scene shut down and
// released, the layers cleared, the three resources forgotten by the persist resource manager (by their keys) and
// released, the primitives shut down. Always true.
bool cTSAudioAmbienceSystem::Shutdown()
{
    SetActive(0);
    cIGZSystemService *const service = Service();
    As<ifaces::cIGZFrameWork>(CachedFramework())->RemoveFromTick(As<ifaces::cIGZSystemService>(service));
    As<ifaces::cIGZFrameWork>(CachedFramework())->RemoveSystemService(As<ifaces::cIGZSystemService>(service));
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(MessageTarget()), nullptr);
    if (mScene) {
        RawSlot<4>(mScene.Get());
        mScene.Assign(nullptr);
    }
    ClearLayers();
    for (AudioRef *resource : {&mResource60, &mResource64, &mNames}) {
        if (*resource) {
            ResourceManagerRef manager;
            if (manager.p) {
                AudioRef persist;
                if (resource->Get()->QueryInterface(kPersistResourceIid, persist.AsPPVoid())) {
                    uint32_t key[4]; // the original's 16-byte frame slot
                    auto *const k = RawSlot<3, const cGZPersistResourceKey *>(persist.Get(), key);
                    As<ifaces::cIGZPersistResourceManager>(manager.p)->ForgetResource(k);
                }
            }
        }
        resource->Assign(nullptr);
    }
    ShutdownPrimitives();
    return true;
}

// 0x224777: the scene's slot 9 (its item list).
void *cTSAudioAmbienceSystem::GetItems() { return RawSlot<9, void *>(mScene.Get()); }

// 0x2248ac (246 B): the lowercased name's id in the name table, else the name read as a number (strtoul, base 0).
uint32_t cTSAudioAmbienceSystem::LookupId(const cRZString *name)
{
    LocalString text(*name);
    reinterpret_cast<cRZStringHelpers *>(&text.s)->LowerCase();
    gz::AnyUint32 value(0);
    bool known;
    {
        GZServiceString key(text.s.ToChar());
        known = As<ifaces::cIGZPropertySetString>(mNames.Get())->HasProperty(&key);
    }
    if (!known) return std::strtoul(text.s.ToChar(), nullptr, 0);
    {
        GZServiceString key(text.s.ToChar());
        As<ifaces::cIGZPropertySetString>(mNames.Get())->GetPropertyValue(&key, static_cast<cIGZAny *>(&value));
    }
    return value.mValue;
}

// 0x22477f (301 B): the cached primitive whose slot 5 is `id`, queried for `iid` (its answer returned as it is); else a new
// one from the framework (class `id`, iid 0xcbba8c73), initialised (its slot 3; a failure is logged and answers false),
// cached and queried.
bool cTSAudioAmbienceSystem::GetPrimitive(uint32_t id, uint32_t iid, void **out)
{
    for (uint32_t i = 0; i < mPrimitives.size(); ++i) {
        cIGZUnknown *const primitive = mPrimitives[i].Get();
        if (RawSlot<5>(primitive) == id) return primitive->QueryInterface(iid, out);
    }
    AudioRef primitive;
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (!uint8_t(com->GetClassObject(id, kPrimitiveIid, primitive.AsPPVoid()))) return false;
    if (!RawSlot<3, bool>(primitive.Get())) {
        Log(kAudioChannel, 0x147, 2, 0xda9ec8, id);
        return false;
    }
    mPrimitives.push_back(primitive);
    return primitive->QueryInterface(iid, out);
}

// 0x2249a2: the value under `key` over the divisor (as float); 1 for key 0 or a key not in the map.
float cTSAudioAmbienceSystem::GetScale(uint32_t key)
{
    if (!key) return 1.0f;
    const auto it = mScales.find(key);
    if (it == mScales.end()) return 1.0f;
    return static_cast<float>(static_cast<double>(it->second)) / mScaleDivisor;
}

// 0x225212 (+8 face slot 10): while active, the tick watch's whole seconds (the watch restarted) go to Update and to the
// state's seconds; past 50 seconds the count starts over and every layer gets the seconds (its slot 5). Always true.
bool cTSAudioAmbienceSystem::OnTick(uint32_t)
{
    if (mActive) {
        const uint32_t seconds = mTickWatch.Elapsed() / 1000;
        Restart(&mTickWatch);
        Update(seconds);
        mState.seconds += seconds;
        if (mState.seconds > 50) {
            mState.seconds = 0;
            for (const AudioRef &layer : mLayers) RawSlot<5>(layer.Get(), seconds);
        }
    }
    return true;
}

// ---- cIGZMessageTarget2 ----

// 0x224554: the handler for the message's type (its slot 4); false for other types (al only).
bool cTSAudioAmbienceSystem::DoMessage(void *message)
{
    auto *const m = static_cast<cIGZUnknown *>(message);
    switch (RawSlot<4>(m)) {
    case kMsgStopB9: return OnStop(m);
    case kMsgReady: return true; // 0x148260
    case kMsgLoadLot: return OnLoadLot(m);
    case kMsgStopB4: return OnStop(m);
    case kMsgLoadScene: return OnLoadScene(m);
    case kMsgRegistryState: return OnRegistryState(m);
    case kMsgModeEnter: return OnModeEnter(m);
    case kMsgReload: return OnReload(m);
    case kMsgModeChange: return OnModeChange(m);
    default: return false;
    }
}

// ---- cICheatCommand ----

void cTSAudioAmbienceSystem::Slot3(uint32_t word, uint32_t) { mCheatWord = word; }
void cTSAudioAmbienceSystem::Slot4(uint32_t) {}
const char *cTSAudioAmbienceSystem::GetName() { return GameAddress<const char>(0xdaa3c8); }
const char *cTSAudioAmbienceSystem::GetHelp(uint32_t) { return GetDescription(); }
bool cTSAudioAmbienceSystem::Slot7() { return false; }
uint32_t cTSAudioAmbienceSystem::Slot8() { return 0; }
const char *cTSAudioAmbienceSystem::GetDescription() { return GameAddress<const char>(0xdaa3cc); }
uint32_t cTSAudioAmbienceSystem::Slot11() { return mCheatWord; }

// ---- non-virtual members ----

// 0x2256ad (328 B).
void cTSAudioAmbienceSystem::Update(uint32_t seconds)
{
    if (!mActive) return;
    Restart(&mBudgetWatch);
    for (uint32_t spent = mBudgetWatch.Elapsed(); spent < 500; spent = mBudgetWatch.Elapsed()) {
        if (mLayerCursor == mLayerEnd) {
            // ORIGINAL-BUG: with no layers the cursor stays at the list's head, and an item list of two or more items
            // then hands the head node's (uninitialised) value to slot 7 below.
            mLayerCursor = mLayers.begin();
            if (mItemCursor == mItemEnd) {
                // Both lists done: a new item list from the scene for the state and the curve at the clock's time.
                const float time = RawSlot<7, float>(mClock.Get());
                const float value = static_cast<float>(
                    OriginalFunction<double(__thiscall *)(void *, float), 0xc0a6a1>()(&mCurve, time));
                mState.curveValue = value;
                auto *const items = RawSlot<10, AmbienceItemList *>(mScene.Get(), &mState, mState.weight, value);
                mItemCursor = items->begin();
                mItemEnd = items->end();
                for (const AudioRef &layer : mLayers) RawSlot<8>(layer.Get());
                for (uint32_t i = 0; i < mPrimitives.size(); ++i) RawSlot<6>(mPrimitives[i].Get(), seconds);
                for (const AudioRef &layer : mLayers) RawSlot<6>(layer.Get(), &mState);
                return;
            }
            ++mItemCursor;
        }
        if (mItemCursor != mItemEnd) RawSlot<7>(mLayerCursor->Get(), *mItemCursor);
        ++mLayerCursor;
    }
}

// 0x225457: the layers hear the new flag (their slot 9, the whole word) only when its low byte changes it; the byte is
// stored either way.
void cTSAudioAmbienceSystem::SetActive(uint32_t active)
{
    if (uint8_t(active) ? !mActive : mActive)
        for (const AudioRef &layer : mLayers) RawSlot<9>(layer.Get(), active);
    mActive = uint8_t(active);
}

// 0x225425: the layer cursor at the layers' end, the item cursor at the end of the scene's item list (its slot 9).
void cTSAudioAmbienceSystem::ResetCursors()
{
    mLayerEnd = mLayers.end();
    mLayerCursor = mLayers.end();
    auto *const items = RawSlot<9, AmbienceItemList *>(mScene.Get());
    mItemEnd = items->end();
    mItemCursor = items->end();
}

// 0x225352 (211 B).
void cTSAudioAmbienceSystem::Reload()
{
    cRZStopwatch watch;
    watch.Construct(3);
    SetActive(0);
    Restart(&watch);
    RawSlot<6>(mScene.Get());
    if (void *const app = GetCachedSingleton()) {
        if (RawSlot<23, void *>(app) && RawSlot<5>(RawSlot<23, void *>(app)) == kLiveMode) {
            OriginalFunction<bool(__thiscall *)(cTSAudioAmbienceSystem *), 0x223cb4>()(this);
            OriginalFunction<void(__thiscall *)(cTSAudioAmbienceSystem *), 0x2232e1>()(this);
            OriginalFunction<void(__thiscall *)(cTSAudioAmbienceSystem *), 0x222d78>()(this);
        }
    }
    ResetCursors();
    SetActive(1);
    Log(kTimingChannel, 0x68c, 5, 0xdaa3e4, watch.Elapsed());
}

// 0x2244dc: while there are layers: the first one (counted for the while) unlinked, turned off (slot 9 with 0) and shut
// down (slot 4).
void cTSAudioAmbienceSystem::ClearLayers()
{
    while (mLayers.size() != 0) {
        AudioRef layer = mLayers.front();
        mLayers.erase(mLayers.begin());
        RawSlot<9>(layer.Get(), 0u);
        RawSlot<4>(layer.Get());
    }
}

// 0x22530b.
void cTSAudioAmbienceSystem::ShutdownPrimitives()
{
    for (uint32_t i = 0; i < mPrimitives.size(); ++i) {
        RawSlot<4>(mPrimitives[i].Get());
        mPrimitives[i].Assign(nullptr);
    }
    mPrimitives.clear();
}

// 0x22515a (178 B): the resource (type 0xebfee33f, group 0x4c1940c5, instance words 0x10cc540 / 0x10cc544) from the
// persist resource manager into +0x64 (released first); a failure is logged. False without a manager.
bool cTSAudioAmbienceSystem::LoadResource64()
{
    ResourceManagerRef manager;
    if (!manager.p) return false;
    const uint32_t key[4] = {kResourceType, kResourceGroup, *GameAddress<uint32_t>(0x10cc540), *GameAddress<uint32_t>(0x10cc544)};
    if (As<ifaces::cIGZPersistResourceManager>(manager.p)->GetResource(key, kResourceIid, mResource64.AsPPVoid(), 0, 0))
        return true;
    Log(kAudioChannel, 0x479, 2, 0xdaa290);
    return false;
}

// 0x2250a2 (184 B): the name table (instance words 0x10cc548 / 0x10cc54c) into +0x74; false at once when one is held.
bool cTSAudioAmbienceSystem::LoadNames()
{
    if (mNames) return false;
    const uint32_t key[4] = {kResourceType, kResourceGroup, *GameAddress<uint32_t>(0x10cc548), *GameAddress<uint32_t>(0x10cc54c)};
    ResourceManagerRef manager;
    // ORIGINAL-BUG: no check that the persist resource manager exists; without one this calls through null (Init calls
    // LoadResource64 first, which fails then, so Init never gets here without one).
    if (As<ifaces::cIGZPersistResourceManager>(manager.p)->GetResource(key, kResourceIid, mNames.AsPPVoid(), 0, 0))
        return true;
    Log(kAudioChannel, 0x48e, 2, 0xdaa2c8);
    return false;
}

// 0x2254a3: the state's three words and weight.
void cTSAudioAmbienceSystem::SetState(const uint32_t *words, float weight)
{
    mState.words[0] = words[0];
    mState.words[1] = words[1];
    mState.words[2] = words[2];
    mState.weight = weight;
}

// 0x22544f: the curve loaded from `name` (the original 0xc0a86a).
void cTSAudioAmbienceSystem::LoadCurve(cRZString *name)
{
    OriginalFunction<void(__thiscall *)(void *, void *), 0xc0a86a>()(&mCurve, name);
}

// 0x224af6 (the stop messages): inactive, the scene's slot 6, the cursors reset, an update of 0 seconds. True.
bool cTSAudioAmbienceSystem::OnStop(cIGZUnknown *)
{
    SetActive(0);
    RawSlot<6>(mScene.Get());
    ResetCursors();
    Update(0);
    return true;
}

// 0x224aae: the cursors reset, the scene loaders 0x22418d and 0x22334d (false when either fails), the curve from +0x88,
// active when there are layers. True.
bool cTSAudioAmbienceSystem::OnLoadScene(cIGZUnknown *)
{
    ResetCursors();
    if (!OriginalFunction<bool(__thiscall *)(cTSAudioAmbienceSystem *), 0x22418d>()(this)) return false;
    if (!OriginalFunction<bool(__thiscall *)(cTSAudioAmbienceSystem *), 0x22334d>()(this)) return false;
    LoadCurve(&mCurveName88.s);
    SetActive(mLayers.size() != 0);
    return true;
}

// 0x224b1e: the cursors reset, the lot loader 0x223cb4 (its answer returned), then 0x2232e1, 0x222d78, the curve from
// +0xa8, active when there are layers.
bool cTSAudioAmbienceSystem::OnLoadLot(cIGZUnknown *)
{
    ResetCursors();
    if (!OriginalFunction<bool(__thiscall *)(cTSAudioAmbienceSystem *), 0x223cb4>()(this)) return false;
    OriginalFunction<void(__thiscall *)(cTSAudioAmbienceSystem *), 0x2232e1>()(this);
    OriginalFunction<void(__thiscall *)(cTSAudioAmbienceSystem *), 0x222d78>()(this);
    LoadCurve(&mCurveNameA8.s);
    SetActive(mLayers.size() != 0);
    return true;
}

// 0x224a01: the message queried for 0x65297976; when it has that face: a reload when its slot 9 answers 2, and true.
bool cTSAudioAmbienceSystem::OnModeEnter(cIGZUnknown *message)
{
    AudioRef mode;
    if (!message->QueryInterface(kModeMessageIid, mode.AsPPVoid())) return false;
    if (RawSlot<9>(mode.Get()) == 2) Reload();
    return true;
}

// 0x224b65: as OnModeEnter, reloading when slot 9 answers 0.
bool cTSAudioAmbienceSystem::OnModeChange(cIGZUnknown *message)
{
    AudioRef mode;
    if (!message->QueryInterface(kModeMessageIid, mode.AsPPVoid())) return false;
    if (RawSlot<9>(mode.Get()) == 0) Reload();
    return true;
}

// 0x224bca.
bool cTSAudioAmbienceSystem::OnReload(cIGZUnknown *)
{
    Reload();
    return true;
}

// 0x224a68: the primitive registry (0x236660) fills a 12-byte block through its slot 6 (unused) and the state's three words
// through its slot 5; the third of those is also the weight. True.
bool cTSAudioAmbienceSystem::OnRegistryState(cIGZUnknown *)
{
    uint32_t unused[3];
    uint32_t state[3];
    RawSlot<6>(OriginalFunction<void *(__cdecl *)(), 0x236660>()(), unused);
    RawSlot<5>(OriginalFunction<void *(__cdecl *)(), 0x236660>()(), state);
    float weight;
    std::memcpy(&weight, &state[2], sizeof(weight));
    SetState(state, weight);
    return true;
}
