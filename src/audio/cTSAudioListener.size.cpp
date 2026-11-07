// cTSAudioListener (lane KH): the whole class in the standalone shape (src/audio/cTSAudioListener.h).
#include "audio/cTSAudioListener.h"

#include "audio/cTSAudioEvent.h"
#include "audio/cTSAudioSoundManager.h" // SoundDefaults::GetFloat
#include "game/game.h"
#include "game/original.h"
#include "world/WorldTileMath.h"
#include "rz/cRZMessage2Standard.h"
#include "game/vtable.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "misc/UtilitiesC.h"     // KeyMapSlot5Walker
#include "rz/MiscHelpersA.h"     // cRZStd
#include "rz/MiscHelpersB.h"     // KeyMapWalker
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, GetCachedSingleton
#include "rz/cRZRefCount.h"      // RZAtomicAdd
#include "rz/RZTextParse.h"

#include <cstdlib>
#include <new>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A log line: a cRZStd for (mode 8, this file's name, line), then its slot 2 (cdecl) with the audio channel (the word at
// 0xff5f10), level 2, the format and the arguments.
constexpr uint32_t kSourceFile = 0xda8540;
constexpr uint32_t kAudioChannel = 0xff5f10;
template <class... A> void Log(uint32_t line, uint32_t format, A... args)
{
    cRZStd log;
    cRZStd *const object = log.Construct(8, Word(GameAddress<char>(kSourceFile)), line);
    VirtualSlot<void(__cdecl *)(cRZStd *, uint32_t, uint32_t, const char *, ...)>(object, 2)(
        object, *GameAddress<uint32_t>(kAudioChannel), 2u, GameAddress<char>(format), args...);
}

// The framework service cached at 0x10f5194 (0x211051: filled from GetServiceObject the first time).
void *CachedFramework()
{
    void *&framework = *GameAddress<void *>(0x10f5194);
    if (!framework) framework = GetServiceObject();
    return framework;
}

// The service object's slot 5 into a reference released at scope end: the 37-byte getters 0x20db69 (the persist resource
// manager, class 0x056b906e, iid 0x656b8efc) and 0x20db8e (the sound renderer, 0x23cecba0 / 0x106077d2).
struct ServiceRef {
    void *p = nullptr;
    ServiceRef(uint32_t id, uint32_t iid)
    {
        if (void *const service = GetServiceObject()) As<ifaces::cIGZFrameWork>(service)->GetSystemService(id, iid, &p);
    }
    ServiceRef(const ServiceRef &) = delete;
    ServiceRef &operator=(const ServiceRef &) = delete;
    ~ServiceRef()
    {
        if (p) static_cast<cIGZUnknown *>(p)->Release();
    }
};
constexpr uint32_t kResourceManagerId = 0x056b906e, kResourceManagerIid = 0x656b8efc;
constexpr uint32_t kRendererId = 0x23cecba0, kRendererIid = 0x106077d2;

constexpr uint32_t kRolloffIid = 0x5b30e7b3;      // the renderer's face that takes the rolloff (its slot 10)
constexpr uint32_t kResourceIid = 0x4bb0d269;     // the tuning property set
constexpr uint32_t kPersistResourceIid = 0x456b8f1d;
constexpr uint32_t kResourceType = 0xebfee33f, kResourceGroup = 0x4c1940c5; // the tuning's key (instance 0x10cc428 / 0x10cc42c)

// The message types (DoMessage) and the listener event with its keys.
enum : uint32_t { kMsgLoadLot = 0xb3, kMsgLoadScene = 0xb6, kMsgDirty = 0xece20be8 };
constexpr uint32_t kListenerEvent = 0x0b8d81d5;
enum : uint32_t { kPrimaryKey = 0x8bd87d9f, kVector3cKey = 0x6bd87da2, kVector48Key = 0xabd87da5, kVector30Key = 0x6d9e30f4 };

// The screen margins (percent of the view's width and height): left, top, right, bottom.
constexpr uint32_t kMarginLeft = 0x10cc414, kMarginTop = 0x10cc418, kMarginRight = 0x10cc41c, kMarginBottom = 0x10cc420;

// Original callees nobody has replaced: the world update, the smoothing step, the stopwatch's restart, the cell's world
// position, the state service, the message server, the number reader.
void UpdateWorld(cTSAudioListener *self) { OriginalFunction<void(__thiscall *)(cTSAudioListener *), 0x211dce>()(self); }
void Step(cTSAudioListener::SmoothedPoint *point, uint32_t elapsed)
{
    OriginalFunction<void(__thiscall *)(cTSAudioListener::SmoothedPoint *, uint32_t), 0x98ba40>()(point, elapsed);
}
void Restart(cRZStopwatch *watch) { OriginalFunction<void(__thiscall *)(cRZStopwatch *), 0x1c95ec>()(watch); }
void CellToWorldPosition(cTSAudioListener::Vector3 *out, const void *cell, int32_t level)
{
    worldmath::FixedToPosition(reinterpret_cast<float *>(out), static_cast<const int32_t *>(cell), level); // 0x7dcc00
}
cIGZUnknown *StateService() { return OriginalFunction<cIGZUnknown *(__cdecl *)(), 0x211da3>()(); }
void *MessageServer() { return GetCachedMessageServer(); }
double ReadNumber(const char *text) { return rztext::ParseDouble(text); }

// Holds the listener's lock (its slots 1 / 2) for the scope.
struct Locked {
    cRZCriticalSection &lock;
    explicit Locked(cRZCriticalSection &l) : lock(l) { lock.Lock(); }
    Locked(const Locked &) = delete;
    Locked &operator=(const Locked &) = delete;
    ~Locked() { lock.Unlock(); }
};

} // namespace

// ---- construction ----

// 0x210b4d (431 B): the members in order (the never-written bytes stay), then the message ids.
cTSAudioListener::cTSAudioListener()
{
    mVector3c = *GameAddress<Vector3>(0xff394c);
    mVector48 = *GameAddress<Vector3>(0xff3958);
    for (SmoothedPoint *point : {&mPrimary, &mSecondary}) {
        point->steps = 10;
        point->minChangeSq = 0;
        point->minChange = 0;
        point->current = {};
        point->target = {};
        point->placed = 0;
    }
    mSecondaryPlaced = 0;
    mSnap = 1;
    mTickWatch.Construct(4);
    mVectorF4 = *GameAddress<Vector3>(0x10cc430);
    for (uint32_t id : {uint32_t(kMsgLoadScene), uint32_t(kMsgLoadLot), uint32_t(kMsgDirty)}) mMessageIds.insert(id);
}

// 0x210d39: the members in reverse order (the tuning set first, the renderer last).
cTSAudioListener::~cTSAudioListener() noexcept = default;

// 0x211065.
cTSAudioListener *__cdecl cTSAudioListener::Create() { return new cTSAudioListener; }

cTSAudioListener *__fastcall cTSAudioListenerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSAudioListener;
}

// ---- cIGZUnknown ----

// 0x211865: 1 and cITSAudioListener -> this, cIGZMessageTarget2 -> +8; counted through the primary face's AddRef.
bool cTSAudioListener::QueryInterface(uint32_t riid, void **out)
{
    if (riid == 1 || riid == kIid) *out = static_cast<cITSAudioListener *>(this);
    else if (riid == kMessageTargetIid) *out = MessageTarget();
    else return false;
    static_cast<cITSAudioListener *>(this)->AddRef();
    return true;
}

// 0x210e34: the interlocked count's new value.
uint32_t cTSAudioListener::AddRef() { return static_cast<uint32_t>(RZAtomicAdd(&mRefs, 1)); }

// 0x211c9d: above 1, one less (the new value); at 1 or below, deleted (the +0xc face's slot 9), 0.
uint32_t cTSAudioListener::Release()
{
    if (static_cast<uint32_t>(mRefs) > 1) return static_cast<uint32_t>(RZAtomicAdd(&mRefs, -1));
    delete this;
    return 0;
}

// ---- cITSAudioListener / cIGZSystemService ----

// 0x2112ee (220 B): the tuning, the tick watch started; with a sound renderer: held, its rolloff face given the rolloff,
// the message ids, the service and its tick registered, the service running. Always true.
bool cTSAudioListener::Init()
{
    LoadTuning();
    mTickWatch.Start();
    ServiceRef renderer(kRendererId, kRendererIid);
    if (renderer.p) {
        mRenderer.Assign(static_cast<cIGZUnknown *>(renderer.p));
        AudioRef rolloff;
        mRenderer.Get()->QueryInterface(kRolloffIid, rolloff.AsPPVoid());
        // ORIGINAL-BUG: the face is used unchecked; a renderer without it faults here.
        RawSlot<10, uint32_t>(rolloff.Get(), mRolloff);
        reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(MessageTarget()), nullptr);
        cIGZSystemService *const service = Service();
        As<ifaces::cIGZFrameWork>(CachedFramework())->AddSystemService(As<ifaces::cIGZSystemService>(service));
        As<ifaces::cIGZFrameWork>(CachedFramework())->AddToTick(As<ifaces::cIGZSystemService>(service));
        service->SetServiceRunning(true);
    }
    return true;
}

// 0x211d4d: the service and its tick unregistered, the message ids dropped, the renderer released. Always true.
bool cTSAudioListener::Shutdown()
{
    cIGZSystemService *const service = Service();
    As<ifaces::cIGZFrameWork>(CachedFramework())->RemoveFromTick(As<ifaces::cIGZSystemService>(service));
    As<ifaces::cIGZFrameWork>(CachedFramework())->RemoveSystemService(As<ifaces::cIGZSystemService>(service));
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(MessageTarget()), nullptr);
    mRenderer.Assign(nullptr);
    return true;
}

// 0x2111dc .. 0x2113e7: copies under the lock.
void cTSAudioListener::GetVector54(float *out)
{
    Locked held(mLock);
    *reinterpret_cast<Vector3 *>(out) = mVector54;
}

bool cTSAudioListener::GetPrimary(float *out)
{
    Locked held(mLock);
    *reinterpret_cast<Vector3 *>(out) = mPrimary.current;
    return true;
}

bool cTSAudioListener::GetPrimaryAnd3c(float *point, float *v3c)
{
    Locked held(mLock);
    *reinterpret_cast<Vector3 *>(point) = mPrimary.current;
    *reinterpret_cast<Vector3 *>(v3c) = mVector3c;
    return true;
}

bool cTSAudioListener::GetSecondary(float *out)
{
    Locked held(mLock);
    *reinterpret_cast<Vector3 *>(out) = mSecondary.current;
    return true;
}

bool cTSAudioListener::GetPoints(float *primary, float *secondary)
{
    Locked held(mLock);
    *reinterpret_cast<Vector3 *>(primary) = mPrimary.current;
    *reinterpret_cast<Vector3 *>(secondary) = mSecondary.current;
    return true;
}

bool cTSAudioListener::GetVector30(float *out)
{
    Locked held(mLock);
    *reinterpret_cast<Vector3 *>(out) = mVector30;
    return true;
}

bool cTSAudioListener::GetVector30And3c(float *v30, float *v3c)
{
    Locked held(mLock);
    *reinterpret_cast<Vector3 *>(v30) = mVector30;
    *reinterpret_cast<Vector3 *>(v3c) = mVector3c;
    return true;
}

int32_t cTSAudioListener::GetWordD8()
{
    Locked held(mLock);
    return mWordD8;
}

uint32_t cTSAudioListener::GetWordDC()
{
    Locked held(mLock);
    return mWordDC;
}

// 0x211469 (+4 face slot 10, 1020 B): the world update; with a renderer, under the lock: the tick watch's time (the watch
// restarted), the points' targets from the active object (the app's slot 26, its slot 39): its cell's world position, or
// the primary point when that is off the screen's margins (the view's slot 142 against its size, slot 110) or there is no
// object; a requested snap; both points stepped. Then the listener event (both points' data, +0x3c, +0x48, +0x30) to the
// renderer (its slot 12) and a message of the event's id to the message server. Always true.
bool cTSAudioListener::OnTick(uint32_t)
{
    UpdateWorld(this);
    if (!mRenderer) return true;
    mLock.Lock();
    const uint32_t elapsed = mTickWatch.Elapsed();
    Restart(&mTickWatch);
    if (void *const world = RawSlot<26, void *>(GetCachedSingleton())) {
        cIGZUnknown *const active = RawSlot<39, cIGZUnknown *>(world);
        if (active) active->AddRef();
        if (active) {
            void *const placed = RawSlot<8, void *>(active);
            void *const located = RawSlot<8, void *>(active);
            const int16_t level = RawSlot<36, int16_t>(placed);
            const void *const cell = RawSlot<33, const void *>(located);
            Vector3 position;
            CellToWorldPosition(&position, cell, level);
            if (!mSecondaryPlaced) {
                mSecondaryPlaced = 1;
                mSecondary.current = position;
            }
            cIGZUnknown *const state = StateService();
            if (state) state->AddRef();
            void *const view = RawSlot<18, void *>(RawSlot<33, void *>(state));
            int32_t size[3]; // the original's 12-byte frame slot
            const int32_t width = RawSlot<110, const int32_t *>(view, size)[0];
            const int32_t height = RawSlot<110, const int32_t *>(view, size)[1];
            const auto percent = [](uint32_t cell, int32_t extent) {
                return static_cast<int32_t>(*GameAddress<uint32_t>(cell) * static_cast<uint32_t>(extent)) / 100;
            };
            const int32_t top = percent(kMarginTop, height);
            const int32_t right = width - percent(kMarginRight, width);
            const int32_t bottom = height - percent(kMarginBottom, height);
            Vector3 screen; // the original's 12-byte frame slot
            RawSlot<142>(view, &screen, &position, 0u);
            const int32_t left = percent(kMarginLeft, width);
            if (static_cast<float>(left) > screen.x || screen.x > static_cast<float>(right) ||
                static_cast<float>(top) > screen.y || screen.y > static_cast<float>(bottom))
                position = mPrimary.current;
            mSecondary.target = position;
            if (!mSecondary.placed) {
                mSecondary.placed = 1;
                mSecondary.current = position;
            }
            if (state) state->Release();
        } else {
            const Vector3 position = mPrimary.current;
            mSecondary.target = position;
            if (!mSecondary.placed) {
                mSecondary.placed = 1;
                mSecondary.current = position;
            }
        }
        if (active) active->Release();
    }
    if (mSnap) {
        mSnap = 0;
        mSecondary.current = mSecondary.target;
        mPrimary.current = mPrimary.target;
    }
    Step(&mPrimary, elapsed);
    Step(&mSecondary, elapsed);
    const Vector3 v3c = mVector3c, v48 = mVector48, primary = mPrimary.current, v30 = mVector30;
    mLock.Unlock();

    cITSAudioEvent *const event = new cTSAudioEvent(kListenerEvent);
    if (event) event->AddRef();
    event->SetVector3(kPrimaryKey, &primary.x);
    event->SetVector3(kVector3cKey, &v3c.x);
    event->SetVector3(kVector48Key, &v48.x);
    event->SetVector3(kVector30Key, &v30.x);
    RawSlot<12>(mRenderer.Get(), event);

    cIGZUnknown *message = nullptr; // a cRZMessage2Standard (RZMemory 0x2c bytes, the constructor 0x1c033d)
    if (void *const memory = RZMemory::Malloc(0x2c))
        message = reinterpret_cast<cIGZUnknown *>(static_cast<cRZMessage2Standard *>(memory)->Construct());
    if (message) message->AddRef();
    RawSlot<5>(message, kListenerEvent);
    RawSlot<4>(MessageServer(), message, 0u);
    if (message) message->Release();
    if (event) event->Release();
    return true;
}

// ---- cIGZMessageTarget2 ----

// 0x210e60: the load / lot messages mark the listener dirty and request a snap, 0xece20be8 marks it dirty. False (al).
bool cTSAudioListener::DoMessage(void *message)
{
    const uint32_t id = RawSlot<4>(message);
    if (id == kMsgLoadLot || id == kMsgLoadScene) {
        mDirty = 1;
        RequestSnap();
    } else if (id == kMsgDirty) {
        mDirty = 1;
    }
    return false;
}

void cTSAudioListener::RequestSnap() { mSnap = 1; }

// ---- cICheatCommand ----

void cTSAudioListener::Slot3(uint32_t word, uint32_t) { mCheatWord = word; }

// 0x210e9f: "lerp <steps> <minstepsize>" and then "slerp" with the same arguments both set the secondary point's steps and
// minimum change (logged); a wrong argument count is logged.
void cTSAudioListener::Slot4(uint32_t parser)
{
    constexpr uint32_t kNames[2] = {0xda8538, 0xda85e8};     // "lerp", "slerp"
    constexpr uint32_t kSetLines[2] = {0x24b, 0x25d}, kFailLines[2] = {0x243, 0x255};
    uint32_t count; // the parser's slot 2 writes the argument count (one frame slot for both calls)
    for (int i = 0; i < 2; ++i) {
        const char *const *const args = RawSlot<2, const char *const *>(reinterpret_cast<void *>(uintptr_t(parser)),
                                                                        GameAddress<char>(kNames[i]), &count, 2u, 0x7fffffffu);
        if (args && count == 2) {
            const uint32_t steps = static_cast<uint32_t>(std::strtoul(args[0], nullptr, 0));
            const float minChange = static_cast<float>(ReadNumber(args[1]));
            mSecondary.steps = static_cast<int32_t>(steps);
            mSecondary.minChange = minChange;
            mSecondary.minChangeSq = minChange * minChange;
            Log(kSetLines[i], 0xda85b4, steps, static_cast<double>(minChange));
        } else {
            Log(kFailLines[i], 0xda8580);
        }
    }
}

const char *cTSAudioListener::GetName() { return GameAddress<const char>(0xda850c); }
const char *cTSAudioListener::GetHelp(uint32_t) { return GetDescription(); }
bool cTSAudioListener::Slot7() { return false; }
uint32_t cTSAudioListener::Slot8() { return 0; }
const char *cTSAudioListener::GetDescription() { return GameAddress<const char>(0xda8518); }
uint32_t cTSAudioListener::Slot11() { return mCheatWord; }

// ---- members ----

// 0x2118bd (992 B): without a persist resource manager nothing. A held tuning set is forgotten by the manager (by its key)
// and released; the set (type 0xebfee33f, group 0x4c1940c5, the instance words 0x10cc428 / 0x10cc42c) loaded into +0x110
// (a failure is logged and ends it); then the values, each with its default when the set lacks it.
void cTSAudioListener::LoadTuning()
{
    ServiceRef manager(kResourceManagerId, kResourceManagerIid);
    if (!manager.p) return;
    if (mTuning) {
        ServiceRef forgetter(kResourceManagerId, kResourceManagerIid);
        if (forgetter.p) {
            AudioRef persist;
            if (mTuning.Get()->QueryInterface(kPersistResourceIid, persist.AsPPVoid())) {
                uint32_t key[4]; // the original's 16-byte frame slot
                auto *const k = RawSlot<3, const cGZPersistResourceKey *>(persist.Get(), key);
                As<ifaces::cIGZPersistResourceManager>(forgetter.p)->ForgetResource(k);
            }
        }
    }
    mTuning.Assign(nullptr);
    const uint32_t key[4] = {kResourceType, kResourceGroup, *GameAddress<uint32_t>(0x10cc428), *GameAddress<uint32_t>(0x10cc42c)};
    if (!As<ifaces::cIGZPersistResourceManager>(manager.p)->GetResource(key, kResourceIid, mTuning.AsPPVoid(), 0, 0)) {
        Log(0x273, 0xda85f0);
        return;
    }
    // The names (pointers at 0xff3910..0xff392c) and the defaults (.rdata floats).
    const auto value = [this](uint32_t nameCell, float fallback) {
        const GZServiceString name(*GameAddress<const char *>(nameCell));
        return SoundDefaults::GetFloat(mTuning.Get(), &name, fallback);
    };
    const auto constant = [](uint32_t rva) { return *GameAddress<float>(rva); };
    mHeightScale = value(0xff3910, constant(0xda03e0));
    mRolloff = value(0xff3914, constant(0xda8650));
    mPrimary.steps = static_cast<int32_t>(static_cast<int64_t>(value(0xff3918, constant(0xda8654))));
    mPrimary.minChange = value(0xff391c, constant(0xda74e8));
    mPrimary.minChangeSq = mPrimary.minChange * mPrimary.minChange;
    mSecondary.steps = static_cast<int32_t>(static_cast<int64_t>(value(0xff3920, constant(0xda8658))));
    mSecondary.minChange = value(0xff3924, constant(0xda74e8));
    mSecondary.minChangeSq = mSecondary.minChange * mSecondary.minChange;
    mMinHeight = value(0xff3928, constant(0xd9c69c));
    mCameraOffset = value(0xff392c, 0.0f);
}
