// gz::cCTGFramework in the standalone shape (lane EX, startup item F3): the game's framework subclass (the live object),
// its constructor and destructor (formerly fw_lifecycle), OnTick, the six band loops, MainLoop and DoMessage as members of
// the class, their adapters, and the free functions GZFrameWork_Run / GZFrameWork_Shutdown / ReleaseFrameworkGlobals on
// g_Framework. Built for size (/O1), as the game's framework module.
#include "gz/Framework.h"
#include "gz/FrameworkShape.h"
#include "gz/cIGZApp.h"

#include "game/game.h"
#include "game/vtable.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZAutoRef.h"
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/cRZMessage2Standard.h"

#include <windows.h>

#include <new>
#include <vector>

namespace {

// Calls slot `slot` of `obj`'s own vtable, __thiscall (services, messages and the message server: objects the framework
// only knows as cIGZUnknown-shaped pointers; results moved through as the original moves them).
template <typename R, typename... Args> R CallSlot(void *obj, int slot, Args... args)
{
    return VirtualSlot<R(__thiscall *)(void *, Args...)>(obj, slot)(obj, args...);
}

// The message pump's imports, read from the game's import address table (as the base's other imports in
// GZFramework.size.cpp), so the tests can script the queue on both sides with one patch.
template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}
constexpr uint32_t kIatWaitMessage = 0xd9a574, kIatPeekMessageA = 0xd9a578, kIatDispatchMessageA = 0xd9a57c,
                   kIatTranslateMessage = 0xd9a580;

// Service priorities that bound the three bands (the application is the only service in the middle one; the engine's
// high-priority services are above, the CAS/catalog systems below).
constexpr int32_t kAppBandTop = -2000000;
constexpr int32_t kAppBandBottom = -2999999;

constexpr uint32_t kIidSystemService = 0x287fb697; // cIGZSystemService
constexpr uint32_t kIidApp = 0x66;                 // cIGZApp
constexpr uint32_t kIidMessage2 = 0x65297976;      // cIGZMessage2
constexpr uint32_t kServiceIdPersistManager = 0x056b906e;
constexpr uint32_t kMessageIdPersistReady = 0xce1505fd;

// The services whose running flag the start/stop messages drive, in the order the constructor adds them (the original
// reads some from constants in .rdata: 0xe99594, 0xe995ac, 0xe995a4, 0xe995a8, 0xe995bc, 0xe99598, 0xe9959c, 0xe995a0).
constexpr uint32_t kManualServiceIds[] = {0x6baa8ba3, 0xeb7d6513, 0x2b33d319, 0x9b3798f7, 0x4a037ee3, 0xdae3783f, 0x7a3e667d, 0x9a353ce8,
                                          0x056b906e, 0x8af76dc8, 0xe92db716, 0x1835d96f, 0x0bd09b0b, 0x67780399, 0x6778039a};

// cIGZSystemService slots.
enum SystemServiceSlot {
    kSvcQueryInterface = 0,
    kSvcAddRef = 1,
    kSvcRelease = 2,
    kSvcGetServiceID = 3,
    kSvcGetServicePriority = 5,
    kSvcIsServiceRunning = 6,
    kSvcSetServiceRunning = 7,
    kSvcInit = 8,
    kSvcShutdown = 9,
    kSvcOnTick = 10,
};

// The lock is the cIGZCriticalSection at +0xf4 (hook-shaped, the game's vtable): slot 1 locks, slot 2 unlocks.
void Lock(cRZCriticalSection &lock)
{
    CallSlot<int32_t>(&lock, 1);
}
void Unlock(cRZCriticalSection &lock)
{
    CallSlot<int32_t>(&lock, 2);
}

// The cached message server (0x9c5a89, rz/RZMessageServer.h).
void *CachedMessageServer()
{
    return GetCachedMessageServer();
}

gz::cGZFramework *GlobalFramework()
{
    return static_cast<gz::cGZFramework *>(ts2_rva_10c9378);
}

} // namespace

FwRef::FwRef(void *object) : mObject(object)
{
    if (mObject) CallSlot<uint32_t>(mObject, kSvcAddRef);
}

FwRef::FwRef(const FwRef &other) : mObject(other.mObject)
{
    if (mObject) CallSlot<uint32_t>(mObject, kSvcAddRef);
}

FwRef::~FwRef() noexcept
{
    if (mObject) CallSlot<uint32_t>(mObject, kSvcRelease);
}

namespace gz {

// ---------------------------------------------------------------------------------------------
// Construction

cCTGFramework::cCTGFramework()
{
    mMessageIds.insert(kMessageServiceStart);
    mMessageIds.insert(kMessageServiceStop);
    for (uint32_t id : kManualServiceIds) mManualServiceIds.insert(id);
}

// The id sets go (the members, in reverse order), then the base.
cCTGFramework::~cCTGFramework() = default;

// ---------------------------------------------------------------------------------------------
// cIGZUnknown: two bases declare the slots, so the class has its own, which are the base's.

bool cCTGFramework::QueryInterface(uint32_t riid, void **out)
{
    return cGZFramework::QueryInterface(riid, out);
}
uint32_t cCTGFramework::AddRef()
{
    return cGZFramework::AddRef();
}
uint32_t cCTGFramework::Release()
{
    return cGZFramework::Release();
}

// ---------------------------------------------------------------------------------------------
// OnTick: as the base's, but only the running services tick

void(__cdecl *cCTGFramework::sTickObserver)() = nullptr;

void cCTGFramework::OnTick(uint32_t counter)
{
    if (sTickObserver) sTickObserver();
    Lock(mLock);
    ++mTickNesting;
    std::vector<void *> services;
    mTickBusy = 1;
    for (FwRef &service : mTickList) services.push_back(service.mObject);
    mTickBusy = 0;
    Unlock(mLock);
    for (void *service : services) {
        if (CallSlot<uint8_t>(service, kSvcIsServiceRunning)) CallSlot<void>(service, kSvcOnTick, counter);
    }
    if (mTickNesting == 1) mTickRemoved.clear();
    --mTickNesting;
}

// ---------------------------------------------------------------------------------------------
// cIGZMessageTarget2

bool cCTGFramework::DoMessage(void *message)
{
    if (!message) return false;
    bool result = false;
    void *typed = nullptr;
    if (CallSlot<uint8_t>(message, kSvcQueryInterface, kIidMessage2, &typed)) {
        uint32_t id = CallSlot<uint32_t>(message, 4);
        if (id == kMessageServiceStart || id == kMessageServiceStop) {
            bool start = id == kMessageServiceStart;
            std::list<FwRef> services;
            BuildServiceList(services, start ? FwWalk::Ascending : FwWalk::Descending);
            for (FwRef &service : services) {
                uint32_t serviceId = CallSlot<uint32_t>(service.mObject, kSvcGetServiceID);
                if (mManualServiceIds.find(serviceId) != mManualServiceIds.end()) {
                    CallSlot<void>(service.mObject, kSvcSetServiceRunning, start ? 1u : 0u);
                }
            }
            result = true;
        }
    }
    if (typed) CallSlot<uint32_t>(typed, kSvcRelease);
    return result;
}

// ---------------------------------------------------------------------------------------------
// The service bands. A service whose id is in mManualServiceIds is initialised but not started (the start/stop
// messages drive it); the base's bands (GZFramework.size.cpp) start every one.

bool cCTGFramework::InitAppBandServices()
{
    std::list<FwRef> services;
    BuildServiceList(services, FwWalk::Descending);
    bool result = true;
    for (FwRef &holder : services) {
        void *service = holder.mObject;
        int32_t priority = CallSlot<int32_t>(service, kSvcGetServicePriority);
        if (priority > kAppBandTop) continue;
        if (priority < kAppBandBottom) break;
        if (CallSlot<uint8_t>(service, kSvcInit)) {
            uint32_t id = CallSlot<uint32_t>(service, kSvcGetServiceID);
            if (mManualServiceIds.find(id) == mManualServiceIds.end()) {
                CallSlot<void>(service, kSvcSetServiceRunning, 1u);
            }
        } else {
            // A failed Init only aborts startup when the service is the application itself.
            void *app = nullptr;
            if (CallSlot<uint8_t>(service, kSvcQueryInterface, kIidApp, &app)) {
                CallSlot<uint32_t>(app, kSvcRelease);
                result = false;
                break;
            }
        }
    }
    return result;
}

bool cCTGFramework::ShutdownAppBandServices()
{
    std::list<FwRef> services;
    BuildServiceList(services, FwWalk::Ascending);
    bool result = true;
    for (FwRef &holder : services) {
        void *service = holder.mObject;
        int32_t priority = CallSlot<int32_t>(service, kSvcGetServicePriority);
        if (priority < kAppBandBottom) continue;
        if (priority > kAppBandTop) break;
        if (CallSlot<uint8_t>(service, kSvcShutdown)) {
            uint32_t id = CallSlot<uint32_t>(service, kSvcGetServiceID);
            if (mManualServiceIds.find(id) == mManualServiceIds.end()) {
                CallSlot<void>(service, kSvcSetServiceRunning, 0u);
            }
        } else {
            result = false;
        }
    }
    return result;
}

bool cCTGFramework::InitLowPriorityServices()
{
    std::list<FwRef> services;
    BuildServiceList(services, FwWalk::Descending);
    for (FwRef &holder : services) {
        void *service = holder.mObject;
        if (CallSlot<int32_t>(service, kSvcGetServicePriority) >= kAppBandBottom) continue;
        if (CallSlot<uint8_t>(service, kSvcInit)) {
            uint32_t id = CallSlot<uint32_t>(service, kSvcGetServiceID);
            if (mManualServiceIds.find(id) == mManualServiceIds.end()) {
                CallSlot<void>(service, kSvcSetServiceRunning, 1u);
            }
        }
    }
    return true;
}

bool cCTGFramework::ShutdownLowPriorityServices()
{
    std::list<FwRef> services;
    BuildServiceList(services, FwWalk::Ascending);
    bool result = true;
    for (FwRef &holder : services) {
        void *service = holder.mObject;
        if (CallSlot<int32_t>(service, kSvcGetServicePriority) >= kAppBandBottom) break; // ascending: the rest are higher still
        if (CallSlot<uint8_t>(service, kSvcShutdown)) {
            uint32_t id = CallSlot<uint32_t>(service, kSvcGetServiceID);
            if (mManualServiceIds.find(id) == mManualServiceIds.end()) {
                CallSlot<void>(service, kSvcSetServiceRunning, 0u);
            }
        } else {
            result = false;
        }
    }
    return result;
}

bool cCTGFramework::InitHighPriorityServices()
{
    // The application registers itself as a service first (its cIGZSystemService face).
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
            if (!CallSlot<uint8_t>(service, kSvcInit)) continue;
            uint32_t id = CallSlot<uint32_t>(service, kSvcGetServiceID);
            if (mManualServiceIds.find(id) == mManualServiceIds.end()) {
                CallSlot<void>(service, kSvcSetServiceRunning, 1u);
            }
            if (CallSlot<uint32_t>(service, kSvcGetServiceID) == kServiceIdPersistManager) {
                // Once the persist resource manager is up, tell everyone.
                void *message = RZMemory::Malloc(cRZMessage2Standard::kSize);
                if (message) {
                    // The constructor (0x1c033d): vtable, a zero count, a clear flag byte and the rest zero. Its class
                    // is not in the shape: the game's vtable.
                    auto *raw = static_cast<uint8_t *>(message);
                    *reinterpret_cast<const void **>(raw) = &ts2_rva_da4064;
                    *reinterpret_cast<uint32_t *>(raw + 4) = 0;
                    raw[8] = 0;
                    for (uint32_t offset = 0x0c; offset <= 0x28; offset += 4) *reinterpret_cast<uint32_t *>(raw + offset) = 0;
                }
                // ORIGINAL-BUG: a null allocation result would be called through (it never happens: the
                // allocator throws). The same holds here.
                CallSlot<uint32_t>(message, kSvcAddRef);
                CallSlot<void>(message, 5, kMessageIdPersistReady); // SetId
                void *server = CachedMessageServer();
                CallSlot<void>(server, 3, message);
                CallSlot<uint32_t>(message, kSvcRelease);
            }
        }

        // Subscribe to the start/stop messages, then start the message server service.
        reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
            ->CallSlot5ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))), nullptr);
        void *server = CachedMessageServer();
        void *serverService = nullptr;
        if (CallSlot<uint8_t>(server, kSvcQueryInterface, kIidSystemService, &serverService)) {
            CallSlot<void>(serverService, kSvcSetServiceRunning, 1u);
        }
        if (serverService) CallSlot<uint32_t>(serverService, kSvcRelease);
    } // the service list is destroyed before the application's reference goes
    if (appService) CallSlot<uint32_t>(appService, kSvcRelease);
    return true;
}

bool cCTGFramework::ShutdownHighPriorityServices()
{
    // Stop the message server service first and drop the subscriptions.
    void *server = CachedMessageServer();
    void *serverService = nullptr;
    if (CallSlot<uint8_t>(server, kSvcQueryInterface, kIidSystemService, &serverService)) {
        CallSlot<void>(serverService, kSvcSetServiceRunning, 0u);
    }
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)
        ->CallSlot6ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))), nullptr);

    bool result = true;
    {
        std::list<FwRef> services;
        BuildServiceList(services, FwWalk::Ascending);
        for (FwRef &holder : services) {
            void *service = holder.mObject;
            if (CallSlot<int32_t>(service, kSvcGetServicePriority) <= kAppBandTop) continue;
            if (CallSlot<uint8_t>(service, kSvcShutdown)) {
                uint32_t id = CallSlot<uint32_t>(service, kSvcGetServiceID);
                if (mManualServiceIds.find(id) == mManualServiceIds.end()) {
                    CallSlot<void>(service, kSvcSetServiceRunning, 0u);
                }
            } else {
                result = false;
            }
        }
    } // the list is destroyed before the message server's service face is released
    if (serverService) CallSlot<uint32_t>(serverService, kSvcRelease);
    return result;
}

// ---------------------------------------------------------------------------------------------
// The main loop: the tick flag through the vtable, the application's message hook, an idle pass per empty queue.

uint32_t cCTGFramework::MainLoop()
{
    MSG msg;
    for (;;) {
        if (!IsTickEnabled()) Import<BOOL(WINAPI *)()>(kIatWaitMessage)();
        for (;;) {
            if (!Import<BOOL(WINAPI *)(MSG *, HWND, UINT, UINT, UINT)>(kIatPeekMessageA)(&msg, nullptr, 0, 0, PM_REMOVE)) break;
            if (msg.message == WM_QUIT) {
                *GameAddress<uint32_t>(kGlobalExitCode) = static_cast<uint32_t>(msg.wParam);
                return static_cast<uint32_t>(msg.wParam);
            }
            // The application's message hook (the cITSAppFW face at +0x40): a true result means it consumed the
            // message.
            void *hookOwner = *GameAddress<void *>(kGlobalAppForMsgHook);
            if (hookOwner) {
                void *face = static_cast<char *>(hookOwner) + 0x40;
                if (CallSlot<uint8_t>(face, 3, &msg)) continue;
            }
            Import<BOOL(WINAPI *)(const MSG *)>(kIatTranslateMessage)(&msg);
            Import<LRESULT(WINAPI *)(const MSG *)>(kIatDispatchMessageA)(&msg);
        }
        // No message waiting: the hook gets an idle call (null message), then a frame runs.
        // The original does not check the application pointer here.
        void *face = static_cast<char *>(*GameAddress<void *>(kGlobalAppForMsgHook)) + 0x40;
        CallSlot<uint8_t>(face, 3, static_cast<MSG *>(nullptr));
        if (IsTickEnabled()) OnIdle();
    }
}

// ---------------------------------------------------------------------------------------------
// Adapters (one statement each; the constructor and the deleting destructor as standalone-shape.md step 6)

cCTGFramework *__fastcall cCTGFrameworkAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cCTGFramework;
}
void __fastcall cCTGFrameworkAdapters::Destruct(cCTGFramework *self, void *)
{
    [[msvc::forceinline_calls]] self->cCTGFramework::~cCTGFramework();
}
cCTGFramework *__fastcall cCTGFrameworkAdapters::DeletingDestructor(cIGZFrameWork *face, void *, uint32_t flags)
{
    cCTGFramework *self = static_cast<cCTGFramework *>(face);
    [[msvc::forceinline_calls]] self->cCTGFramework::~cCTGFramework();
    if (flags & 1) ::operator delete(self, sizeof(cCTGFramework));
    return self;
}
void __fastcall cCTGFrameworkAdapters::OnTick(cIGZFrameWork *self, void *, uint32_t counter)
{
    [[msvc::forceinline_calls]] return static_cast<cCTGFramework *>(self)->cCTGFramework::OnTick(counter);
}
bool __fastcall cCTGFrameworkAdapters::InitHighPriorityServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cCTGFramework *>(self)->cCTGFramework::InitHighPriorityServices();
}
bool __fastcall cCTGFrameworkAdapters::InitAppBandServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cCTGFramework *>(self)->cCTGFramework::InitAppBandServices();
}
bool __fastcall cCTGFrameworkAdapters::InitLowPriorityServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cCTGFramework *>(self)->cCTGFramework::InitLowPriorityServices();
}
bool __fastcall cCTGFrameworkAdapters::ShutdownLowPriorityServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cCTGFramework *>(self)->cCTGFramework::ShutdownLowPriorityServices();
}
bool __fastcall cCTGFrameworkAdapters::ShutdownAppBandServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cCTGFramework *>(self)->cCTGFramework::ShutdownAppBandServices();
}
bool __fastcall cCTGFrameworkAdapters::ShutdownHighPriorityServices(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cCTGFramework *>(self)->cCTGFramework::ShutdownHighPriorityServices();
}
uint32_t __fastcall cCTGFrameworkAdapters::MainLoop(cIGZFrameWork *self, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cCTGFramework *>(self)->cCTGFramework::MainLoop();
}
bool __fastcall cCTGFrameworkAdapters::DoMessage(cIGZMessageTarget2 *self, void *, void *message)
{
    [[msvc::forceinline_calls]] return static_cast<cCTGFramework *>(self)->cCTGFramework::DoMessage(message);
}

} // namespace gz

// ---------------------------------------------------------------------------------------------
// Run and shutdown (free functions; g_Framework is read again at every step, as the original does)

bool __cdecl GZFrameWork_Run()
{
    cIGZApp *app = GlobalFramework()->Application();
    GlobalFramework()->mState = 7;
    if (!app->GZRun()) GlobalFramework()->MainLoop();
    return *GameAddress<int32_t>(gz::cGZFramework::kGlobalExitCode) == 0;
}

void __cdecl ReleaseFrameworkGlobals()
{
    void **app = GameAddress<void *>(gz::cGZFramework::kGlobalApp);
    if (*app) {
        CallSlot<uint32_t>(*app, kSvcRelease);
        *app = nullptr;
    }
    void **framework = &ts2_rva_10c9378;
    if (*framework) {
        CallSlot<uint32_t>(*framework, kSvcRelease);
        *framework = nullptr;
    }
    void **framework2 = &ts2_rva_10c937c;
    if (*framework2) {
        CallSlot<uint32_t>(*framework2, kSvcRelease);
        *framework2 = nullptr;
    }
}

bool __cdecl GZFrameWork_Shutdown()
{
    cIGZApp *app = GlobalFramework()->Application();
    GlobalFramework()->mState = 8;
    GlobalFramework()->mState = 9;
    GlobalFramework()->mState = 10;
    GlobalFramework()->RunPreAppShutdownHooks();
    app->PreFrameworkShutdown();
    GlobalFramework()->ShutdownLowPriorityServices();
    GlobalFramework()->mState = 11;
    GlobalFramework()->ShutdownAppBandServices();
    GlobalFramework()->RunPostAppShutdownHooks();
    GlobalFramework()->mState = 12;
    GlobalFramework()->ShutdownHighPriorityServices();
    GlobalFramework()->ToggleTick(0);
    GlobalFramework()->RunPostSystemServiceShutdownHooks();
    GlobalFramework()->ClearServiceContainers();
    GlobalFramework()->ClearHooks();
    GlobalFramework()->ShutdownCOMObject();
    GlobalFramework()->mState = 13;
    GlobalFramework()->mState = 14;
    ReleaseFrameworkGlobals();
    bool ok = *GameAddress<int32_t>(gz::cGZFramework::kGlobalExitCode) == 0;
    ts2_rva_10c9378 = nullptr;
    ts2_rva_10c937c = nullptr;
    return ok;
}
