// gz::cGZFramework in the standalone shape (lane ER, startup item F2): the bodies the hook shape verified
// (src/gz/Framework.size.cpp before this lane), now members of the class, plus the constructor and destructor
// (from fw_lifecycle), the three standard streams, AbortiveQuit and the command-line pair 0x143660 / 0x1427a7.
// Built for size (/O1), as the game's framework module.
#include "gz/FrameworkShape.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/FrameworkStartup.h"
#include "rz/cRZExceptionNotification.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <new>
#include <vector>

#pragma warning(disable : 4996) // sprintf, as the game calls it

namespace gz {
namespace {

// Calls slot `slot` of `obj`'s own vtable, __thiscall (objects the framework only knows as cIGZUnknown-shaped
// pointers: services, hooks, streams; the results are moved through as the original moves them).
template <typename R, typename... Args> R CallSlot(void *obj, int slot, Args... args)
{
    return VirtualSlot<R(__thiscall *)(void *, Args...)>(obj, slot)(obj, args...);
}

// Imports the framework calls through the game's import address table (so a test can substitute the Windows calls
// that would block, leave the process or that our binary doesn't link).
template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}
constexpr uint32_t kIatSteamRunCallbacks = 0xd9a6f8;
constexpr uint32_t kIatMessageBoxA = 0xd9a568;
constexpr uint32_t kIatSetEnvironmentVariableA = 0xd9a168;
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114;
constexpr uint32_t kIatOutputDebugStringA = 0xd9a110;
constexpr uint32_t kIatWaitMessage = 0xd9a574, kIatPeekMessageA = 0xd9a578, kIatDispatchMessageA = 0xd9a57c,
                   kIatTranslateMessage = 0xd9a580;

// Service priorities that bound the three bands (slots 41..46).
constexpr int32_t kAppBandTop = -2000000;
constexpr int32_t kAppBandBottom = -2999999;

constexpr uint32_t kIidSystemService = 0x287fb697; // cIGZSystemService
constexpr uint32_t kIidApp = 0x66;                 // cIGZApp
constexpr uint32_t kIidFramework = 0x65;   // cIGZFrameWork (1 is cIGZUnknown)
constexpr uint32_t kIidFrameworkW32 = 0x23ea70a1;
constexpr uint32_t kIidStdOutErr = 0xc51b11cb;
constexpr uint32_t kIidStdIn = 0x051b11bd;

// The game's texts (compared by the tests as the original's).
constexpr uint32_t kTextDebugLevelFormat = 0xd9b648; // "%d"
constexpr uint32_t kTextDebugLevelName = 0xd9b64c;   // GZFRAMEWORK_DEBUG_LEVEL
constexpr uint32_t kTextAbortiveQuitStart = 0xd9b664;
constexpr uint32_t kTextAbortiveQuitEnd = 0xd9b684;
constexpr uint32_t kTextNoExceptionMessage = 0xd9b6b4;
constexpr uint32_t kTextExceptionCaption = 0xd9b6d0;

// The stream objects StdOut/StdErr (0x10 bytes, 0x1b4dbe) and StdIn (0x14 bytes, 0x1b4da6) build: their classes are not
// reimplemented (startup open question 5), so their constructors stay original calls.
constexpr uint32_t kStdOutSize = 0x10, kStdInSize = 0x14;

// cIGZSystemService slots.
enum : int { kSvcQueryInterface = 0, kSvcAddRef = 1, kSvcRelease = 2, kSvcGetServiceID = 3, kSvcGetServicePriority = 5,
             kSvcSetServiceRunning = 7, kSvcInit = 8, kSvcShutdown = 9, kSvcOnTick = 10, kSvcOnIdle = 11,
             kSvcGetServiceTickPriority = 12 };
// cIGZFrameWorkHooks slots.
enum : int { kHookPreFrameWorkInit = 3, kHookPreAppInit = 4, kHookPostAppInit = 5, kHookPreAppShutdown = 6,
             kHookPostAppShutdown = 7, kHookPostSystemServiceShutdown = 8, kHookAbortiveQuit = 9 };

// The lock is the cIGZCriticalSection at +0xf4 (hook-shaped, the game's vtable): slot 1 locks, slot 2 unlocks.
void Lock(cRZCriticalSection &lock)
{
    CallSlot<int32_t>(&lock, 1);
}
void Unlock(cRZCriticalSection &lock)
{
    CallSlot<int32_t>(&lock, 2);
}

// The list node whose value equals `value` (std::find over the raw pointers), or end().
std::list<FwRef>::iterator FindRef(std::list<FwRef> &list, void *value)
{
    return std::find_if(list.begin(), list.end(), [value](const FwRef &r) { return r.mObject == value; });
}

void PushRef(std::list<FwRef> &list, void *object)
{
    list.emplace_back();
    list.back().Assign(object);
}

bool RunHooks(cGZFramework *fw, int slot)
{
    if (fw->mHooksBusy) return false;
    std::list<FwRef> hooks;
    fw->CopyHooks(hooks);
    for (FwRef &hook : hooks) CallSlot<void>(hook.mObject, slot);
    return true;
}

// StdOut / StdErr / StdIn: under the lock, an empty slot gets a new stream object (the slot holds the reference), and
// the slot's object is returned without a reference of the caller's.
template <uint32_t Size, typename Construct> void *StreamOf(cGZFramework *fw, FwRef &slot, Construct construct)
{
    Lock(fw->mLock);
    if (!slot.mObject) {
        void *memory = ::operator new(Size);
        void *stream = memory ? construct(memory) : nullptr;
        slot.Assign(stream);
    }
    void *result = slot.mObject;
    Unlock(fw->mLock);
    return result;
}

} // namespace

void(__cdecl *cGZFramework::sExit)(int code) = &exit;

// ---------------------------------------------------------------------------------------------
// Construction

cGZFramework::cGZFramework()
    : mRefCount(0), mTickEnabled(0), mIdleInterval(4), mTickCounter(0), mIdleCounter(0), mInstall(0), mState(0),
      mServicesBusy(0), mLookupBusy(0), mIdleBusy(0), mTickBusy(0), mHooksBusy(0), mIdleNesting(0), mTickNesting(0)
{
    mLock.Construct();
    mCmdLine.Construct();
    mDebugStream = nullptr;
    mStdOut.mObject = nullptr;
    mStdErr.mObject = nullptr;
    mStdIn.mObject = nullptr;
    mDebugLevel = 2;
    mExceptionNotification.mObject = nullptr;
    mQuitPending = 0;
    mHInstance = nullptr;
    mMainHWND = nullptr;
    mHInstance = Import<HMODULE(WINAPI *)(const char *)>(kIatGetModuleHandleA)(nullptr);
    // A cRZExceptionNotification (the original calls its constructor 0x1c8c84 on 8 new bytes), referenced once by the
    // slot. Ours, in the standalone shape (src/rz/cRZExceptionNotification.h): this is its only creation site.
    cIGZExceptionNotification *notification = new cRZExceptionNotification;
    mExceptionNotification.mObject = notification;
    notification->AddRef();
}

cGZFramework::~cGZFramework()
{
    // The four references are released (the slots are not cleared), then the command line and the lock go; the
    // containers and the COM object follow as members, in the original's order.
    if (mExceptionNotification.mObject) CallSlot<uint32_t>(mExceptionNotification.mObject, kSvcRelease);
    if (mStdIn.mObject) CallSlot<uint32_t>(mStdIn.mObject, kSvcRelease);
    if (mStdErr.mObject) CallSlot<uint32_t>(mStdErr.mObject, kSvcRelease);
    if (mStdOut.mObject) CallSlot<uint32_t>(mStdOut.mObject, kSvcRelease);
    mCmdLine.Destruct();
    mLock.Destruct();
}

// ---------------------------------------------------------------------------------------------
// cIGZUnknown

bool cGZFramework::QueryInterface(uint32_t riid, void **out)
{
    // 1 and 0x65 give the object, 0x23ea70a1 the cIGZFrameWorkW32 face; each takes a reference through AddRef.
    if (riid == 1 || riid == kIidFramework) {
        *out = static_cast<cIGZFrameWork *>(this);
    } else if (riid == kIidFrameworkW32) {
        *out = static_cast<cIGZFrameWorkW32 *>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

uint32_t cGZFramework::AddRef()
{
    Lock(mLock);
    uint32_t count = mRefCount + 1;
    mRefCount = count;
    Unlock(mLock);
    return count;
}

uint32_t cGZFramework::Release()
{
    Lock(mLock);
    uint32_t count = mRefCount;
    // The framework is a static object: at a count of one or zero nothing changes and 0 is returned.
    if (count > 1) {
        --count;
        mRefCount = count;
    } else {
        count = 0;
    }
    Unlock(mLock);
    return count;
}

// ---------------------------------------------------------------------------------------------
// Services

bool cGZFramework::AddSystemService(cIGZSystemService *service)
{
    if (!service) return false;
    Lock(mLock);
    if (mServicesBusy || mLookupBusy) {
        Unlock(mLock);
        return false;
    }
    int32_t priority = CallSlot<int32_t>(service, kSvcGetServicePriority);
    uint32_t id = CallSlot<uint32_t>(service, kSvcGetServiceID);
    if (mServiceById.find(id) != mServiceById.end()) {
        Unlock(mLock);
        return false;
    }
    // The id is asked for a second time for the priority container's entry; the hash map is keyed by the first answer.
    uint32_t entryId = CallSlot<uint32_t>(service, kSvcGetServiceID);
    auto inserted = mServices.emplace(priority, FwServiceEntry{entryId, FwRef()});
    inserted->second.mService.Assign(service);
    mServiceById[id].Assign(service);
    Unlock(mLock);
    return true;
}

bool cGZFramework::RemoveSystemService(cIGZSystemService *service)
{
    Lock(mLock);
    if (mServicesBusy || mLookupBusy) {
        Unlock(mLock);
        return false;
    }
    // Out of the idle and tick lists first. The lookup argument is a counted copy of the service that lives only as long
    // as the search.
    {
        std::list<FwRef>::iterator found;
        {
            FwRef keep(service);
            found = FindRef(mIdleList, service);
        }
        if (found != mIdleList.end()) mIdleList.erase(found);
    }
    {
        std::list<FwRef>::iterator found;
        {
            FwRef keep(service);
            found = FindRef(mTickList, service);
        }
        if (found != mTickList.end()) mTickList.erase(found);
    }
    // The priority container (the first entry holding the service), with the services-busy byte set, then the id map
    // (its first entry holding the service), with the lookup-busy byte set.
    mServicesBusy = 1;
    for (auto it = mServices.begin(); it != mServices.end(); ++it) {
        if (it->second.mService.mObject == service) {
            mServices.erase(it);
            break;
        }
    }
    mServicesBusy = 0;
    mLookupBusy = 1;
    for (auto it = mServiceById.begin(); it != mServiceById.end(); ++it) {
        if (it->second.mObject == service) {
            mServiceById.erase(it);
            break;
        }
    }
    mLookupBusy = 0;
    Unlock(mLock);
    return true;
}

bool cGZFramework::GetSystemService(uint32_t serviceId, uint32_t iid, void **ppv)
{
    Lock(mLock);
    uint8_t result = 0;
    auto it = mServiceById.find(serviceId);
    mLookupBusy = 1;
    if (it != mServiceById.end()) result = CallSlot<uint8_t>(it->second.mObject, kSvcQueryInterface, iid, ppv);
    mLookupBusy = 0;
    Unlock(mLock);
    return result != 0;
}

void cGZFramework::BuildServiceList(std::list<FwRef> &out, FwWalk walk)
{
    Lock(mLock);
    mServicesBusy = 1;
    if (walk == FwWalk::Ascending) {
        for (auto it = mServices.begin(); it != mServices.end(); ++it) PushRef(out, it->second.mService.mObject);
    } else {
        for (auto it = mServices.end(); it != mServices.begin();) {
            --it;
            PushRef(out, it->second.mService.mObject);
        }
    }
    mServicesBusy = 0;
    Unlock(mLock);
}

bool cGZFramework::EnumSystemServices(cIGZUnknownEnumerator *enumerator, uint32_t context, uint32_t flags)
{
    std::list<FwRef> services;
    BuildServiceList(services, FwWalk::Descending);
    for (FwRef &service : services) {
        void *unknown = nullptr;
        if (CallSlot<uint8_t>(service.mObject, kSvcQueryInterface, 1u, &unknown)) {
            CallSlot<void>(enumerator, 3, unknown, context, flags);
            CallSlot<uint32_t>(unknown, kSvcRelease);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Hooks

bool cGZFramework::AddHook(cIGZFrameWorkHooks *hook)
{
    Lock(mLock);
    bool result = false;
    if (!mHooksBusy && FindRef(mHooks, hook) == mHooks.end()) {
        PushRef(mHooks, hook);
        result = true;
    }
    Unlock(mLock);
    return result;
}

bool cGZFramework::RemoveHook(cIGZFrameWorkHooks *hook)
{
    Lock(mLock);
    bool result = false;
    if (!mHooksBusy) {
        auto found = FindRef(mHooks, hook);
        if (found != mHooks.end()) {
            mHooks.erase(found);
            result = true;
        }
    }
    Unlock(mLock);
    return result;
}

void cGZFramework::CopyHooks(std::list<FwRef> &out)
{
    Lock(mLock);
    mHooksBusy = 1;
    for (FwRef &hook : mHooks) PushRef(out, hook.mObject);
    mHooksBusy = 0;
    Unlock(mLock);
}

bool cGZFramework::RunPreFrameWorkInitHooks() { return RunHooks(this, kHookPreFrameWorkInit); }
bool cGZFramework::RunPreAppInitHooks() { return RunHooks(this, kHookPreAppInit); }
bool cGZFramework::RunPostAppInitHooks() { return RunHooks(this, kHookPostAppInit); }
bool cGZFramework::RunPreAppShutdownHooks() { return RunHooks(this, kHookPreAppShutdown); }
bool cGZFramework::RunPostAppShutdownHooks() { return RunHooks(this, kHookPostAppShutdown); }
bool cGZFramework::RunPostSystemServiceShutdownHooks() { return RunHooks(this, kHookPostSystemServiceShutdown); }
bool cGZFramework::RunAbortiveQuitHooks() { return RunHooks(this, kHookAbortiveQuit); }

// ---------------------------------------------------------------------------------------------
// Tick and idle lists

bool cGZFramework::AddToTick(cIGZSystemService *service)
{
    Lock(mLock);
    bool result = false;
    if (!mTickBusy) {
        // The list is kept in descending tick-priority order; a service already in it (by id) is accepted without a
        // second entry.
        int32_t tickPriority = CallSlot<int32_t>(service, kSvcGetServiceTickPriority);
        uint32_t id = CallSlot<uint32_t>(service, kSvcGetServiceID);
        auto position = mTickList.begin();
        bool duplicate = false;
        while (position != mTickList.end()) {
            void *current = position->mObject;
            auto next = std::next(position);
            if (CallSlot<uint32_t>(current, kSvcGetServiceID) == id) {
                duplicate = true;
                break;
            }
            if (tickPriority > CallSlot<int32_t>(current, kSvcGetServiceTickPriority)) break;
            position = next;
        }
        if (!duplicate) {
            FwRef copy(service);
            mTickList.insert(position, copy);
        }
        result = true;
    }
    Unlock(mLock);
    return result;
}

bool cGZFramework::RemoveFromTick(cIGZSystemService *service)
{
    Lock(mLock);
    bool result = false;
    if (!mTickBusy) {
        // During a tick pass the service is also parked in the removed list, which keeps it alive until the outermost
        // pass ends (even if it turns out not to be in the tick list).
        if (mTickNesting > 0) PushRef(mTickRemoved, service);
        auto found = FindRef(mTickList, service);
        if (found != mTickList.end()) {
            mTickList.erase(found);
            result = true;
        }
    }
    Unlock(mLock);
    return result;
}

bool cGZFramework::AddToOnIdle(cIGZSystemService *service)
{
    Lock(mLock);
    bool result = false;
    if (!mIdleBusy && FindRef(mIdleList, service) == mIdleList.end()) {
        PushRef(mIdleList, service);
        result = true;
    }
    Unlock(mLock);
    return result;
}

bool cGZFramework::RemoveFromOnIdle(cIGZSystemService *service)
{
    Lock(mLock);
    bool result = false;
    if (!mIdleBusy) {
        if (mIdleNesting > 0) PushRef(mIdleRemoved, service);
        auto found = FindRef(mIdleList, service);
        if (found != mIdleList.end()) {
            mIdleList.erase(found);
            result = true;
        }
    }
    Unlock(mLock);
    return result;
}

uint32_t cGZFramework::GetOnIdleInterval()
{
    return mIdleInterval;
}

void cGZFramework::SetOnIdleInterval(int32_t interval)
{
    mIdleInterval = interval <= 0 ? 1 : static_cast<uint32_t>(interval);
}

void cGZFramework::OnTick(uint32_t counter)
{
    Lock(mLock);
    ++mTickNesting;
    std::vector<void *> services;
    mTickBusy = 1;
    for (FwRef &service : mTickList) services.push_back(service.mObject);
    mTickBusy = 0;
    Unlock(mLock);
    for (void *service : services) CallSlot<void>(service, kSvcOnTick, counter);
    if (mTickNesting == 1) mTickRemoved.clear();
    --mTickNesting;
}

void cGZFramework::OnIdle()
{
    Import<void(__cdecl *)()>(kIatSteamRunCallbacks)();
    uint32_t tick = mTickCounter;
    mTickCounter = tick + 1;
    OnTick(tick);
    Lock(mLock);
    uint32_t remainder = mTickCounter % mIdleInterval; // the interval is never below 1
    ++mIdleNesting;
    if (remainder == 0) {
        ++mIdleCounter;
        uint32_t idleCounter = mIdleCounter;
        std::vector<void *> services;
        mIdleBusy = 1;
        for (FwRef &service : mIdleList) services.push_back(service.mObject);
        mIdleBusy = 0;
        Unlock(mLock);
        for (void *service : services) CallSlot<void>(service, kSvcOnIdle, idleCounter);
    } else {
        Unlock(mLock);
    }
    if (mIdleNesting == 1) mIdleRemoved.clear();
    --mIdleNesting;
}

bool cGZFramework::ClearServiceContainers()
{
    Lock(mLock);
    mIdleList.clear();
    mIdleRemoved.clear();
    mTickList.clear();
    mTickRemoved.clear();
    mServiceById.clear();
    while (!mServices.empty()) mServices.erase(mServices.begin());
    Unlock(mLock);
    return true;
}

bool cGZFramework::ClearHooks()
{
    Lock(mLock);
    mHooks.clear();
    Unlock(mLock);
    return true;
}

void cGZFramework::ShutdownCOMObject()
{
    Lock(mLock);
    CallSlot<void>(&mCOM, 10); // the embedded COM object's RealShutdown, through its vtable
    Unlock(mLock);
}

// ---------------------------------------------------------------------------------------------
// Simple state

uint8_t cGZFramework::IsTickEnabled()
{
    return mTickEnabled;
}

void cGZFramework::ToggleTick(uint32_t enabled)
{
    mTickEnabled = static_cast<uint8_t>(enabled);
}

void cGZFramework::SetInstallFlag(uint32_t flag)
{
    mDeferQuit = static_cast<uint8_t>(flag);
}

uint8_t cGZFramework::IsInstall()
{
    return mInstall;
}

cIGZCmdLine *cGZFramework::CommandLine()
{
    return reinterpret_cast<cIGZCmdLine *>(&mCmdLine);
}

cIGZCOM *cGZFramework::GetCOMObject()
{
    return &mCOM;
}

uint32_t cGZFramework::GetState()
{
    return mState;
}

void *cGZFramework::GetDebugStream()
{
    return mDebugStream;
}

void *cGZFramework::DefaultDebugStream()
{
    return GetDebugStream();
}

// Slot 27 is the same function as slot 26 in the original (0x142717 in both).
void *cGZFramework::DebugStream()
{
    return GetDebugStream();
}

void cGZFramework::SetDebugStream(void *stream)
{
    mDebugStream = stream;
}

int32_t cGZFramework::GetDebugLevel()
{
    return mDebugLevel;
}

bool cGZFramework::SetDebugLevel(int32_t level)
{
    mDebugLevel = level;
    char text[0x40];
    sprintf(text, GameAddress<const char>(kTextDebugLevelFormat), level);
    return Import<BOOL(__stdcall *)(const char *, const char *)>(kIatSetEnvironmentVariableA)(GameAddress<const char>(kTextDebugLevelName), text) != 0;
}

void *cGZFramework::ExceptionNotificationObj()
{
    return mExceptionNotification.mObject;
}

void cGZFramework::SetApplication(cIGZApp *app)
{
    Lock(mLock);
    *GameAddress<cIGZApp *>(kGlobalApp) = app;
    Unlock(mLock);
}

cIGZApp *cGZFramework::Application()
{
    return *GameAddress<cIGZApp *>(kGlobalApp);
}

void cGZFramework::Quit(int32_t code)
{
    Lock(mLock);
    if (mDeferQuit) {
        mQuitPending = 1;
        mQuitCode = static_cast<uint32_t>(code);
    } else {
        PostQuitMessage(code);
    }
    Unlock(mLock);
}

void cGZFramework::AbortiveQuit(int32_t code)
{
    Import<void(WINAPI *)(const char *)>(kIatOutputDebugStringA)(GameAddress<const char>(kTextAbortiveQuitStart));
    RunAbortiveQuitHooks();
    Import<void(WINAPI *)(const char *)>(kIatOutputDebugStringA)(GameAddress<const char>(kTextAbortiveQuitEnd));
    sExit(code);
}

void cGZFramework::ReportException(const char *message)
{
    ToggleTick(0);
    const char *text = message ? message : GameAddress<const char>(kTextNoExceptionMessage);
    Import<int(__stdcall *)(HWND, const char *, const char *, UINT)>(kIatMessageBoxA)(nullptr, text, GameAddress<const char>(kTextExceptionCaption), MB_ICONEXCLAMATION);
    ToggleTick(1);
}

void *cGZFramework::StdOut()
{
    return StreamOf<kStdOutSize>(this, mStdOut, [](void *memory) { return OriginalFunction<void *(__thiscall *)(void *), 0x1b4dbe>()(memory); });
}

void *cGZFramework::StdErr()
{
    return StreamOf<kStdOutSize>(this, mStdErr, [](void *memory) { return OriginalFunction<void *(__thiscall *)(void *), 0x1b4dbe>()(memory); });
}

void *cGZFramework::StdIn()
{
    return StreamOf<kStdInSize>(this, mStdIn, [](void *memory) { return OriginalFunction<void *(__thiscall *)(void *), 0x1b4da6>()(memory); });
}

bool cGZFramework::GetStream(uint32_t kind, uint32_t iid, void **ppv)
{
    Lock(mLock);
    uint8_t result = 0;
    if (ppv) {
        void *stream = nullptr;
        bool known = true;
        switch (kind) {
        case 0: stream = StdOut(); break;
        case 1: stream = StdErr(); break;
        case 2: stream = StdIn(); break;
        default: known = false; break;
        }
        if (known) result = CallSlot<uint8_t>(stream, kSvcQueryInterface, iid, ppv);
    }
    Unlock(mLock);
    return result != 0;
}

bool cGZFramework::SetStream(uint32_t kind, void *stream)
{
    Lock(mLock);
    FwRef *slot = nullptr;
    uint32_t iid = 0;
    switch (kind) {
    case 0: slot = &mStdOut; iid = kIidStdOutErr; break;
    case 1: slot = &mStdErr; iid = kIidStdOutErr; break;
    case 2: slot = &mStdIn; iid = kIidStdIn; break;
    default: break;
    }
    uint8_t result = 0;
    if (slot) {
        slot->Assign(nullptr);
        // The stream puts its own reference into the slot through its QueryInterface.
        result = stream ? CallSlot<uint8_t>(stream, kSvcQueryInterface, iid, reinterpret_cast<void **>(slot)) : 1;
    }
    Unlock(mLock);
    return result != 0;
}

// ---------------------------------------------------------------------------------------------
// cIGZFrameWorkW32

cIGZFrameWork *cGZFramework::AsIGZFrameWork()
{
    return this;
}

void *cGZFramework::GetWindowsInstance()
{
    return mHInstance;
}

void *cGZFramework::GetMainHWND()
{
    return mMainHWND;
}

void cGZFramework::SetMainHWND(void *hwnd)
{
    mMainHWND = hwnd;
}

// ---------------------------------------------------------------------------------------------
// The command line

void cGZFramework::SetCommandLine(const cRZCmdLine &commandLine)
{
    Lock(mLock);
    fw_startup::AssignCmdLine(&mCmdLine, nullptr, &commandLine);
    Unlock(mLock);
}

void __cdecl cGZFramework::CommitCommandLine(cIGZCmdLine *commandLine)
{
    fw_startup::ParseCommandLine(commandLine, nullptr);
}

// ---------------------------------------------------------------------------------------------
// The class's own slots 41..48 (lane EX). Never run in game: the live object is a cCTGFramework, which overrides all but
// 47 (its bands also leave the +0x18c services to the start/stop messages and talk to the message server). The base's
// start every service of the band.

bool cGZFramework::InitHighPriorityServices()
{
    // The application registers itself as a service first (its cIGZSystemService face); the reference is dropped after
    // the service list.
    void *appService = nullptr;
    cIGZApp *app = Application();
    CallSlot<uint8_t>(app, kSvcQueryInterface, kIidSystemService, &appService);
    AddSystemService(static_cast<cIGZSystemService *>(appService));
    {
        std::list<FwRef> services;
        BuildServiceList(services, FwWalk::Descending);
        for (FwRef &holder : services) {
            void *service = holder.mObject;
            if (CallSlot<int32_t>(service, kSvcGetServicePriority) <= kAppBandTop) break;
            if (CallSlot<uint8_t>(service, kSvcInit)) CallSlot<void>(service, kSvcSetServiceRunning, 1u);
        }
    }
    if (appService) CallSlot<uint32_t>(appService, kSvcRelease);
    return true;
}

bool cGZFramework::InitAppBandServices()
{
    std::list<FwRef> services;
    BuildServiceList(services, FwWalk::Descending);
    for (FwRef &holder : services) {
        void *service = holder.mObject;
        int32_t priority = CallSlot<int32_t>(service, kSvcGetServicePriority);
        if (priority > kAppBandTop) continue;
        if (priority < kAppBandBottom) break;
        if (CallSlot<uint8_t>(service, kSvcInit)) {
            CallSlot<void>(service, kSvcSetServiceRunning, 1u);
        } else {
            // A failed Init only aborts startup when the service is the application itself.
            void *app = nullptr;
            if (CallSlot<uint8_t>(service, kSvcQueryInterface, kIidApp, &app)) {
                CallSlot<uint32_t>(app, kSvcRelease);
                return false;
            }
        }
    }
    return true;
}

bool cGZFramework::InitLowPriorityServices()
{
    std::list<FwRef> services;
    BuildServiceList(services, FwWalk::Descending);
    for (FwRef &holder : services) {
        void *service = holder.mObject;
        if (CallSlot<int32_t>(service, kSvcGetServicePriority) < kAppBandBottom && CallSlot<uint8_t>(service, kSvcInit)) {
            CallSlot<void>(service, kSvcSetServiceRunning, 1u);
        }
    }
    return true;
}

bool cGZFramework::ShutdownLowPriorityServices()
{
    std::list<FwRef> services;
    BuildServiceList(services, FwWalk::Ascending);
    bool result = true;
    for (FwRef &holder : services) {
        void *service = holder.mObject;
        if (CallSlot<int32_t>(service, kSvcGetServicePriority) >= kAppBandBottom) break; // ascending: the rest are higher
        if (CallSlot<uint8_t>(service, kSvcShutdown)) CallSlot<void>(service, kSvcSetServiceRunning, 0u);
        else result = false;
    }
    return result;
}

bool cGZFramework::ShutdownAppBandServices()
{
    std::list<FwRef> services;
    BuildServiceList(services, FwWalk::Ascending);
    bool result = true;
    for (FwRef &holder : services) {
        void *service = holder.mObject;
        int32_t priority = CallSlot<int32_t>(service, kSvcGetServicePriority);
        if (priority < kAppBandBottom) continue;
        if (priority > kAppBandTop) break;
        if (CallSlot<uint8_t>(service, kSvcShutdown)) CallSlot<void>(service, kSvcSetServiceRunning, 0u);
        else result = false;
    }
    return result;
}

bool cGZFramework::ShutdownHighPriorityServices()
{
    std::list<FwRef> services;
    BuildServiceList(services, FwWalk::Ascending);
    bool result = true;
    for (FwRef &holder : services) {
        void *service = holder.mObject;
        if (CallSlot<int32_t>(service, kSvcGetServicePriority) <= kAppBandTop) continue;
        if (CallSlot<uint8_t>(service, kSvcShutdown)) CallSlot<void>(service, kSvcSetServiceRunning, 0u);
        else result = false;
    }
    return result;
}

void cGZFramework::Slot47() {}

// The tick byte read directly (cCTGFramework's asks IsTickEnabled), no application message hook, and the loop also
// ends when a deferred Quit (mDeferQuit) has been requested: g_ExitCode is then the code Quit recorded.
uint32_t cGZFramework::MainLoop()
{
    MSG msg;
    do {
        if (!mTickEnabled) Import<BOOL(WINAPI *)()>(kIatWaitMessage)();
        while (Import<BOOL(WINAPI *)(MSG *, HWND, UINT, UINT, UINT)>(kIatPeekMessageA)(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                *GameAddress<uint32_t>(kGlobalExitCode) = static_cast<uint32_t>(msg.wParam);
                return static_cast<uint32_t>(msg.wParam);
            }
            Import<BOOL(WINAPI *)(const MSG *)>(kIatTranslateMessage)(&msg);
            Import<LRESULT(WINAPI *)(const MSG *)>(kIatDispatchMessageA)(&msg);
        }
        if (mTickEnabled) OnIdle();
    } while (!mDeferQuit || !mQuitPending);
    *GameAddress<uint32_t>(kGlobalExitCode) = mQuitCode;
    return mQuitCode;
}

// ---------------------------------------------------------------------------------------------
// Adapters (one statement each; the constructor and the deleting destructor as standalone-shape.md step 6)

cGZFramework *__fastcall cGZFrameworkAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZFramework;
}
void __fastcall cGZFrameworkAdapters::Destruct(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] self->cGZFramework::~cGZFramework();
}
cGZFramework *__fastcall cGZFrameworkAdapters::DeletingDestructor(cIGZFrameWork *face, void *, uint32_t flags)
{
    cGZFramework *self = static_cast<cGZFramework *>(face);
    [[msvc::forceinline_calls]] self->cGZFramework::~cGZFramework();
    if (flags & 1) ::operator delete(self, sizeof(cGZFramework));
    return self;
}

bool __fastcall cGZFrameworkAdapters::QueryInterface(cIGZFrameWork *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::QueryInterface(riid, out);
}
uint32_t __fastcall cGZFrameworkAdapters::AddRef(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::AddRef();
}
uint32_t __fastcall cGZFrameworkAdapters::Release(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::Release();
}
bool __fastcall cGZFrameworkAdapters::AddSystemService(cIGZFrameWork *self, void *, cIGZSystemService *service)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::AddSystemService(service);
}
bool __fastcall cGZFrameworkAdapters::RemoveSystemService(cIGZFrameWork *self, void *, cIGZSystemService *service)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RemoveSystemService(service);
}
bool __fastcall cGZFrameworkAdapters::GetSystemService(cIGZFrameWork *self, void *, uint32_t id, uint32_t iid, void **ppv)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::GetSystemService(id, iid, ppv);
}
bool __fastcall cGZFrameworkAdapters::EnumSystemServices(cIGZFrameWork *self, void *, cIGZUnknownEnumerator *enumerator, uint32_t context, uint32_t flags)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::EnumSystemServices(enumerator, context, flags);
}
bool __fastcall cGZFrameworkAdapters::AddHook(cIGZFrameWork *self, void *, cIGZFrameWorkHooks *hook)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::AddHook(hook);
}
bool __fastcall cGZFrameworkAdapters::RemoveHook(cIGZFrameWork *self, void *, cIGZFrameWorkHooks *hook)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RemoveHook(hook);
}
bool __fastcall cGZFrameworkAdapters::AddToTick(cIGZFrameWork *self, void *, cIGZSystemService *service)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::AddToTick(service);
}
bool __fastcall cGZFrameworkAdapters::RemoveFromTick(cIGZFrameWork *self, void *, cIGZSystemService *service)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RemoveFromTick(service);
}
bool __fastcall cGZFrameworkAdapters::AddToOnIdle(cIGZFrameWork *self, void *, cIGZSystemService *service)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::AddToOnIdle(service);
}
bool __fastcall cGZFrameworkAdapters::RemoveFromOnIdle(cIGZFrameWork *self, void *, cIGZSystemService *service)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RemoveFromOnIdle(service);
}
void __fastcall cGZFrameworkAdapters::SetOnIdleInterval(cIGZFrameWork *self, void *, int32_t interval)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::SetOnIdleInterval(interval);
}
void __fastcall cGZFrameworkAdapters::OnTick(cIGZFrameWork *self, void *, uint32_t counter)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::OnTick(counter);
}
void __fastcall cGZFrameworkAdapters::OnIdle(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::OnIdle();
}
uint8_t __fastcall cGZFrameworkAdapters::IsTickEnabled(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::IsTickEnabled();
}
void __fastcall cGZFrameworkAdapters::ToggleTick(cIGZFrameWork *self, void *, uint32_t enabled)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::ToggleTick(enabled);
}
void __fastcall cGZFrameworkAdapters::Quit(cIGZFrameWork *self, void *, int32_t code)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::Quit(code);
}
void __fastcall cGZFrameworkAdapters::AbortiveQuit(cIGZFrameWork *self, void *, int32_t code)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::AbortiveQuit(code);
}
uint8_t __fastcall cGZFrameworkAdapters::IsInstall(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::IsInstall();
}
void *__fastcall cGZFrameworkAdapters::DefaultDebugStream(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::DefaultDebugStream();
}
bool __fastcall cGZFrameworkAdapters::SetDebugLevel(cIGZFrameWork *self, void *, int32_t level)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::SetDebugLevel(level);
}
void *__fastcall cGZFrameworkAdapters::StdOut(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::StdOut();
}
void *__fastcall cGZFrameworkAdapters::StdErr(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::StdErr();
}
void *__fastcall cGZFrameworkAdapters::StdIn(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::StdIn();
}
bool __fastcall cGZFrameworkAdapters::GetStream(cIGZFrameWork *self, void *, uint32_t kind, uint32_t iid, void **ppv)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::GetStream(kind, iid, ppv);
}
bool __fastcall cGZFrameworkAdapters::SetStream(cIGZFrameWork *self, void *, uint32_t kind, void *stream)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::SetStream(kind, stream);
}
void __fastcall cGZFrameworkAdapters::SetApplication(cIGZFrameWork *self, void *, cIGZApp *app)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::SetApplication(app);
}
cIGZApp *__fastcall cGZFrameworkAdapters::Application(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::Application();
}
void __fastcall cGZFrameworkAdapters::ReportException(cIGZFrameWork *self, void *, const char *message)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::ReportException(message);
}
// The +4 face's adapters: a slot's `this` is never null, and `__assume` drops the face-to-object cast's null test (the
// one-statement form keeps it and loses these two byte matches; see docs/standalone-shape.md).
void *__fastcall cGZFrameworkAdapters::GetMainHWND(cIGZFrameWorkW32 *self, void *)
{
    __assume(self != nullptr);
    [[msvc::forceinline_calls]] return static_cast<cGZFramework &>(*self).cGZFramework::GetMainHWND();
}
void __fastcall cGZFrameworkAdapters::SetMainHWND(cIGZFrameWorkW32 *self, void *, void *hwnd)
{
    __assume(self != nullptr);
    [[msvc::forceinline_calls]] return static_cast<cGZFramework &>(*self).cGZFramework::SetMainHWND(hwnd);
}
void __fastcall cGZFrameworkAdapters::CopyHooks(cGZFramework *self, void *, std::list<FwRef> &out)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::CopyHooks(out);
}
void __fastcall cGZFrameworkAdapters::BuildServiceList(cGZFramework *self, void *, std::list<FwRef> &out, FwWalk walk)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::BuildServiceList(out, walk);
}
bool __fastcall cGZFrameworkAdapters::RunPreFrameWorkInitHooks(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RunPreFrameWorkInitHooks();
}
bool __fastcall cGZFrameworkAdapters::RunPreAppInitHooks(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RunPreAppInitHooks();
}
bool __fastcall cGZFrameworkAdapters::RunPostAppInitHooks(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RunPostAppInitHooks();
}
bool __fastcall cGZFrameworkAdapters::RunPreAppShutdownHooks(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RunPreAppShutdownHooks();
}
bool __fastcall cGZFrameworkAdapters::RunPostAppShutdownHooks(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RunPostAppShutdownHooks();
}
bool __fastcall cGZFrameworkAdapters::RunPostSystemServiceShutdownHooks(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RunPostSystemServiceShutdownHooks();
}
bool __fastcall cGZFrameworkAdapters::RunAbortiveQuitHooks(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::RunAbortiveQuitHooks();
}
bool __fastcall cGZFrameworkAdapters::ClearServiceContainers(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::ClearServiceContainers();
}
bool __fastcall cGZFrameworkAdapters::ClearHooks(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::ClearHooks();
}
void __fastcall cGZFrameworkAdapters::ShutdownCOMObject(cGZFramework *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::ShutdownCOMObject();
}
void __fastcall cGZFrameworkAdapters::SetInstallFlag(cGZFramework *self, void *, uint32_t flag)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::SetInstallFlag(flag);
}
bool __fastcall cGZFrameworkAdapters::InitHighPriorityServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::InitHighPriorityServices();
}
bool __fastcall cGZFrameworkAdapters::InitAppBandServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::InitAppBandServices();
}
bool __fastcall cGZFrameworkAdapters::InitLowPriorityServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::InitLowPriorityServices();
}
bool __fastcall cGZFrameworkAdapters::ShutdownLowPriorityServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::ShutdownLowPriorityServices();
}
bool __fastcall cGZFrameworkAdapters::ShutdownAppBandServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::ShutdownAppBandServices();
}
bool __fastcall cGZFrameworkAdapters::ShutdownHighPriorityServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::ShutdownHighPriorityServices();
}
uint32_t __fastcall cGZFrameworkAdapters::MainLoop(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::MainLoop();
}
void __fastcall cGZFrameworkAdapters::SetCommandLine(cGZFramework *self, void *, const cRZCmdLine &commandLine)
{
    [[msvc::forceinline_calls]] return static_cast<cGZFramework *>(self)->cGZFramework::SetCommandLine(commandLine);
}

} // namespace gz
