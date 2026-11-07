#pragma once
#include "gz/Framework.h" // FwRef, FwServiceEntry, FwWalk (the hook-shaped framework's container elements)
#include "gz/GZCOM.h"     // cGZCOM (lane EN)
#include "gz/cIGZCOM.h"
#include "gz/cIGZCOMDirector.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZFrameWorkHooks.h"
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZSystemService.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZRandom.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <unordered_map>

// The framework in the standalone shape (docs/standalone-shape.md): gz::cGZFramework (lane ER, item F2,
// GZFramework.size.cpp) and gz::cCTGFramework (lane EX, item F3, Framework.size.cpp), hooked through their adapters. The
// layout views of the same names in gz/Framework.h (no member functions left) still hold the global names for the tests
// and WinMain's code, so these live in namespace gz.
//
// Members that are still hook-shaped (cRZCriticalSection, cRZCmdLine, cRZRandom, cRZString: `mVTable` members that their
// constructors point at the game's vtables) keep their layout here; taking them to the shape is a prerequisite of the
// standalone build (handoff item S0), not of these classes.
namespace gz {

// cGZCOM is the global class in gz/GZCOM.h (lane EN, item F1: in the shape, with the hook-shaped struct retired).

// cGZFramework (0x180 bytes; cIGZFrameWork +0 (game vtable 0xd9b544, 49 slots), cIGZFrameWorkW32 +4 (0xd9b60c, 7)).
// Never the live object's most-derived class (the game builds a cCTGFramework), but its own slots 40..48 are virtual and
// the base of cCTGFramework's. The +4 face's cIGZUnknown slots are the compiler's thunks 0x1431f4/0x142347/0x143273.
// Bodies (lane ER, startup F2): src/gz/GZFramework.size.cpp. Creation: the only caller of 0x141d76 is cCTGFramework's
// constructor 0xc2ccdb, run by WinMain's function-local static before the hooks are installed, so in game the live object
// keeps the game's vtables and is served by cGZFrameworkAdapters; our vtables reach only the objects our constructors build
// (the tests, and cCTGFramework's constructor through the 0xc2ccdb hook's adapter, which nothing in game runs after the
// hooks go in; WinMain's candidate in src/app/WinMainInfra.cpp builds ours once WinMain is ours, item E1).
class cGZFramework : public cIGZFrameWork, public cIGZFrameWorkW32 {
public:
    static constexpr uint32_t kVTable = 0xd9b544, kW32VTable = 0xd9b60c; // the game's (tests, adapters' trap copies)
    static constexpr uint32_t kNotificationVTable = 0xda44d4;           // cRZExceptionNotification (not in the shape)

    cGZFramework();           // 0x141d76 (builds mCOM, mCmdLine, mExceptionNotification; mIdleInterval 4, level 2)
    cGZFramework(const cGZFramework &) = delete;
    cGZFramework &operator=(const cGZFramework &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // 0x1431b6
    uint32_t AddRef() override;                              // 0x142321 (+0x78)
    uint32_t Release() override;                             // 0x143244 (never deletes)

    // cIGZFrameWork 3..39 (see gz/cIGZFrameWork.h for the RVAs)
    bool AddSystemService(cIGZSystemService *service) override;
    bool RemoveSystemService(cIGZSystemService *service) override;
    bool GetSystemService(uint32_t serviceId, uint32_t iid, void **ppv) override;
    bool EnumSystemServices(cIGZUnknownEnumerator *enumerator, uint32_t context, uint32_t flags) override;
    bool AddHook(cIGZFrameWorkHooks *hook) override;
    bool RemoveHook(cIGZFrameWorkHooks *hook) override;
    bool AddToTick(cIGZSystemService *service) override;
    bool RemoveFromTick(cIGZSystemService *service) override;
    bool AddToOnIdle(cIGZSystemService *service) override;
    bool RemoveFromOnIdle(cIGZSystemService *service) override;
    uint32_t GetOnIdleInterval() override;
    void SetOnIdleInterval(int32_t interval) override;
    void OnTick(uint32_t counter) override;
    void OnIdle() override;
    uint8_t IsTickEnabled() override;
    void ToggleTick(uint32_t enabled) override;
    void Quit(int32_t code) override;
    void AbortiveQuit(int32_t code) override;
    cIGZCmdLine *CommandLine() override;
    uint8_t IsInstall() override;
    cIGZCOM *GetCOMObject() override;
    uint32_t GetState() override;
    void *GetDebugStream() override;
    void *DefaultDebugStream() override;
    void *DebugStream() override;      // the same body as DefaultDebugStream (0x142717 in both slots)
    void SetDebugStream(void *stream) override;
    bool SetDebugLevel(int32_t level) override;
    int32_t GetDebugLevel() override;
    void *StdOut() override;
    void *StdErr() override;
    void *StdIn() override;
    bool GetStream(uint32_t kind, uint32_t iid, void **ppv) override;
    bool SetStream(uint32_t kind, void *stream) override;
    void SetApplication(cIGZApp *app) override;
    cIGZApp *Application() override;
    void ReportException(const char *message) override;
    void *ExceptionNotificationObj() override;

    // cIGZFrameWorkW32 3..6
    cIGZFrameWork *AsIGZFrameWork() override;   // folded 0x142700
    void *GetWindowsInstance() override;        // folded 0x142950
    void *GetMainHWND() override;               // 0x142857
    void SetMainHWND(void *hwnd) override;      // 0x1436d9

    // The class's own slots (40..48), in this order. 41..46 and 48 never run in game (the live object is a cCTGFramework,
    // which overrides them); they are the base's bands without the +0x18c set and the message server (lane EX).
    virtual ~cGZFramework();                         // slot 40: 0x141f5c, deleting 0x142207
    virtual bool InitHighPriorityServices();         // slot 41, 0x14307f: app service added; priority > -2,000,000, descending
    virtual bool InitAppBandServices();              // slot 42, 0x1425c7: [-2,999,999, -2,000,000]; a failed app aborts
    virtual bool InitLowPriorityServices();          // slot 43, 0x142f87: < -2,999,999
    virtual bool ShutdownLowPriorityServices();      // slot 44, 0x143133: ascending, stops at -2,999,999
    virtual bool ShutdownAppBandServices();          // slot 45, 0x142662
    virtual bool ShutdownHighPriorityServices();     // slot 46, 0x142ffb
    virtual void Slot47();                           // slot 47, 0x140fe0 (a bare `ret`, folded)
    virtual uint32_t MainLoop();                     // slot 48, 0x1435c2: the tick byte and the deferred quit, no app hook

    // Non-virtual members.
    void CopyHooks(std::list<FwRef> &out);                    // 0x142c2a (ret 4): AddRef'd copy, hooks-busy set
    void BuildServiceList(std::list<FwRef> &out, FwWalk walk); // 0x142cb2 (ret 8)
    bool RunPreFrameWorkInitHooks();                          // 0x142b97 (hooks slot 3)
    bool RunPreAppInitHooks();                                // 0x142ad7 (slot 4)
    bool RunPostAppInitHooks();                               // 0x1429b7 (slot 5)
    bool RunPreAppShutdownHooks();                            // 0x142b37 (slot 6)
    bool RunPostAppShutdownHooks();                           // 0x142a17 (slot 7)
    bool RunPostSystemServiceShutdownHooks();                 // 0x142a77 (slot 8)
    bool RunAbortiveQuitHooks();                              // 0x142957 (slot 9)
    bool ClearServiceContainers();                            // 0x1427bb
    bool ClearHooks();                                        // 0x142bf7
    void ShutdownCOMObject();                                 // 0x1437c5: the COM object's RealShutdown under the lock
    void SetInstallFlag(uint32_t flag);                       // 0x14368c (ret 4): the low byte into +0x150
    void SetCommandLine(const cRZCmdLine &commandLine);       // 0x143660 (ret 4): cRZCmdLine::operator= under the lock
    static void __cdecl CommitCommandLine(cIGZCmdLine *commandLine); // 0x1427a7: argument 0 = the program's path

    // AbortiveQuit's exit (exit() by default; the tests record instead). Not hooked in game: see the status entry.
    static void(__cdecl *sExit)(int code);

    // The game's globals the framework code reads and writes (g_Framework twice, g_App, g_ExitCode, g_AppForMsgHook).
    static constexpr uint32_t kGlobalFramework = 0x10c9378, kGlobalFramework2 = 0x10c937c, kGlobalApp = 0x10c9380;
    static constexpr uint32_t kGlobalExitCode = 0x10c9384, kGlobalAppForMsgHook = 0x10fdfe8;

    cGZCOM mCOM;                                      // +0x08
    uint32_t mRefCount;                               // +0x78
    std::list<FwRef> mHooks;                          // +0x7c
    std::multimap<int32_t, FwServiceEntry> mServices; // +0x84 key = priority
    std::unordered_map<uint32_t, FwRef> mServiceById; // +0x8c
    std::list<FwRef> mIdleList;                       // +0xac
    std::list<FwRef> mIdleRemoved;                    // +0xb4
    std::list<FwRef> mTickList;                       // +0xbc
    std::list<FwRef> mTickRemoved;                    // +0xc4
    uint8_t mTickEnabled;                             // +0xcc
    uint8_t padCD[3];
    uint32_t mIdleInterval;                           // +0xd0
    uint32_t mTickCounter;                            // +0xd4
    uint32_t mIdleCounter;                            // +0xd8
    uint8_t mInstall;                                 // +0xdc
    uint8_t padDD[3];
    uint32_t mState;                                  // +0xe0 0..14
    uint8_t mServicesBusy, mLookupBusy, mIdleBusy, mTickBusy, mHooksBusy; // +0xe4..+0xe8
    uint8_t padE9[3];
    int32_t mIdleNesting;                             // +0xec
    int32_t mTickNesting;                             // +0xf0
    cRZCriticalSection mLock;                         // +0xf4
    cRZCmdLine mCmdLine;                              // +0xfc
    uint8_t mDeferQuit;                               // +0x150
    uint8_t pad151[3];
    void *mDebugStream;                               // +0x154
    // The four references are released by the destructor's body, before the command line and the lock go (the
    // original's order), so they sit in unions and are built and released by hand.
    union { FwRef mStdOut; };                         // +0x158
    union { FwRef mStdErr; };                         // +0x15c
    union { FwRef mStdIn; };                          // +0x160
    int32_t mDebugLevel;                              // +0x164
    union { FwRef mExceptionNotification; };          // +0x168
    uint8_t mQuitPending;                             // +0x16c
    uint8_t pad16D[3];
    uint32_t mQuitCode;                               // +0x170
    void *mHInstance;                                 // +0x174
    void *mMainHWND;                                  // +0x178
    uint32_t pad17C;                                  // +0x17c
};
static_assert(sizeof(cGZFramework) == 0x180, "cGZFramework size");
static_assert(offsetof(cGZFramework, mCOM) == 0x08, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mRefCount) == 0x78, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mServices) == 0x84, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mServiceById) == 0x8c, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mTickEnabled) == 0xcc, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mState) == 0xe0, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mLock) == 0xf4, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mCmdLine) == 0xfc, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mDeferQuit) == 0x150, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mDebugLevel) == 0x164, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mHInstance) == 0x174, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mMainHWND) == 0x178, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mStdOut) == 0x158, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mExceptionNotification) == 0x168, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mQuitCode) == 0x170, "cGZFramework layout");

// The RVA hooks' entry points (docs/standalone-shape.md, step 6): `this` in ecx (the face the slot is called on), the
// dummy edx, the stack arguments; each forwards to our member without dispatch. They serve the live object (a
// cCTGFramework built before the hooks, game vtables) and the game's direct calls.
struct cGZFrameworkAdapters {
    static cGZFramework *__fastcall Construct(void *memory, void *edx);                                   // 0x141d76
    static void __fastcall Destruct(cGZFramework *self, void *edx);                                       // 0x141f5c
    static cGZFramework *__fastcall DeletingDestructor(cIGZFrameWork *self, void *edx, uint32_t flags);   // 0x142207
    static bool __fastcall QueryInterface(cIGZFrameWork *self, void *edx, uint32_t riid, void **out);     // 0x1431b6
    static uint32_t __fastcall AddRef(cIGZFrameWork *self, void *edx);                                    // 0x142321
    static uint32_t __fastcall Release(cIGZFrameWork *self, void *edx);                                   // 0x143244
    static bool __fastcall AddSystemService(cIGZFrameWork *self, void *edx, cIGZSystemService *service);  // 0x14234f
    static bool __fastcall RemoveSystemService(cIGZFrameWork *self, void *edx, cIGZSystemService *service); // 0x143431
    static bool __fastcall GetSystemService(cIGZFrameWork *self, void *edx, uint32_t id, uint32_t iid, void **ppv); // 0x1428e2
    static bool __fastcall EnumSystemServices(cIGZFrameWork *self, void *edx, cIGZUnknownEnumerator *enumerator,
                                              uint32_t context, uint32_t flags);                          // 0x14271c
    static bool __fastcall AddHook(cIGZFrameWork *self, void *edx, cIGZFrameWorkHooks *hook);             // 0x14229a
    static bool __fastcall RemoveHook(cIGZFrameWork *self, void *edx, cIGZFrameWorkHooks *hook);          // 0x1433cd
    static bool __fastcall AddToTick(cIGZFrameWork *self, void *edx, cIGZSystemService *service);         // 0x1424fe
    static bool __fastcall RemoveFromTick(cIGZFrameWork *self, void *edx, cIGZSystemService *service);    // 0x143324
    static bool __fastcall AddToOnIdle(cIGZFrameWork *self, void *edx, cIGZSystemService *service);       // 0x142474
    static bool __fastcall RemoveFromOnIdle(cIGZFrameWork *self, void *edx, cIGZSystemService *service);  // 0x14327b
    static void __fastcall SetOnIdleInterval(cIGZFrameWork *self, void *edx, int32_t interval);           // 0x1436e6
    static void __fastcall OnTick(cIGZFrameWork *self, void *edx, uint32_t counter);                      // 0x142eca
    static void __fastcall OnIdle(cIGZFrameWork *self, void *edx);                                        // 0x142dbb
    static uint8_t __fastcall IsTickEnabled(cIGZFrameWork *self, void *edx);                              // 0x142c23
    static void __fastcall ToggleTick(cIGZFrameWork *self, void *edx, uint32_t enabled);                  // 0x1438d7
    static void __fastcall Quit(cIGZFrameWork *self, void *edx, int32_t code);                            // 0x1431fc
    static void __fastcall AbortiveQuit(cIGZFrameWork *self, void *edx, int32_t code);                    // 0x142270 (not hooked)
    static uint8_t __fastcall IsInstall(cIGZFrameWork *self, void *edx);                                  // 0x142c1c
    static void *__fastcall DefaultDebugStream(cIGZFrameWork *self, void *edx);                           // 0x142717
    static bool __fastcall SetDebugLevel(cIGZFrameWork *self, void *edx, int32_t level);                  // 0x143699
    static void *__fastcall StdOut(cIGZFrameWork *self, void *edx);                                       // 0x143887
    static void *__fastcall StdErr(cIGZFrameWork *self, void *edx);                                       // 0x1437e7
    static void *__fastcall StdIn(cIGZFrameWork *self, void *edx);                                        // 0x143837
    static bool __fastcall GetStream(cIGZFrameWork *self, void *edx, uint32_t kind, uint32_t iid, void **ppv); // 0x142877
    static bool __fastcall SetStream(cIGZFrameWork *self, void *edx, uint32_t kind, void *stream);        // 0x1436fb
    static void __fastcall SetApplication(cIGZFrameWork *self, void *edx, cIGZApp *app);                  // 0x14363e
    static cIGZApp *__fastcall Application(cIGZFrameWork *self, void *edx);                               // 0x1426ec
    static void __fastcall ReportException(cIGZFrameWork *self, void *edx, const char *message);          // 0x14358c
    static void *__fastcall GetMainHWND(cIGZFrameWorkW32 *self, void *edx);                               // 0x142857
    static void __fastcall SetMainHWND(cIGZFrameWorkW32 *self, void *edx, void *hwnd);                    // 0x1436d9
    static void __fastcall CopyHooks(cGZFramework *self, void *edx, std::list<FwRef> &out);               // 0x142c2a
    static void __fastcall BuildServiceList(cGZFramework *self, void *edx, std::list<FwRef> &out, FwWalk walk); // 0x142cb2
    static bool __fastcall RunPreFrameWorkInitHooks(cGZFramework *self, void *edx);                      // 0x142b97
    static bool __fastcall RunPreAppInitHooks(cGZFramework *self, void *edx);                            // 0x142ad7
    static bool __fastcall RunPostAppInitHooks(cGZFramework *self, void *edx);                           // 0x1429b7
    static bool __fastcall RunPreAppShutdownHooks(cGZFramework *self, void *edx);                        // 0x142b37
    static bool __fastcall RunPostAppShutdownHooks(cGZFramework *self, void *edx);                       // 0x142a17
    static bool __fastcall RunPostSystemServiceShutdownHooks(cGZFramework *self, void *edx);             // 0x142a77
    static bool __fastcall RunAbortiveQuitHooks(cGZFramework *self, void *edx);                          // 0x142957
    static bool __fastcall ClearServiceContainers(cGZFramework *self, void *edx);                        // 0x1427bb
    static bool __fastcall ClearHooks(cGZFramework *self, void *edx);                                    // 0x142bf7
    static void __fastcall ShutdownCOMObject(cGZFramework *self, void *edx);                             // 0x1437c5
    static void __fastcall SetInstallFlag(cGZFramework *self, void *edx, uint32_t flag);                 // 0x14368c
    static void __fastcall SetCommandLine(cGZFramework *self, void *edx, const cRZCmdLine &commandLine); // 0x143660
    // The base's own slots 41..46 and 48 (lane EX; reached only on an object with the base's game vtable 0xd9b544, which
    // the live object loses in its constructor).
    static bool __fastcall InitHighPriorityServices(cIGZFrameWork *self, void *edx);                     // 0x14307f
    static bool __fastcall InitAppBandServices(cIGZFrameWork *self, void *edx);                          // 0x1425c7
    static bool __fastcall InitLowPriorityServices(cIGZFrameWork *self, void *edx);                      // 0x142f87
    static bool __fastcall ShutdownLowPriorityServices(cIGZFrameWork *self, void *edx);                  // 0x143133
    static bool __fastcall ShutdownAppBandServices(cIGZFrameWork *self, void *edx);                      // 0x142662
    static bool __fastcall ShutdownHighPriorityServices(cIGZFrameWork *self, void *edx);                 // 0x142ffb
    static uint32_t __fastcall MainLoop(cIGZFrameWork *self, void *edx);                                 // 0x1435c2
};

// cCTGFramework (0x198 bytes; the live object, static at 0x10fdb50, built by WinMain with the constructor 0xc2ccdb;
// game vtables 0xe995cc (49), 0xe99694 (7), 0xe996b4 (4)). Adds the message-target face at +0x180 and overrides OnTick,
// the destructor, the six band loops and MainLoop. Its cIGZUnknown slots are 5-byte forwards to the base's (0xc2d995,
// 0xc2ceeb, 0xc2d9ad: an override that calls cGZFramework's, because two bases now declare them), with thunks on the
// +4 face (0xc2d99a, 0xc2cef0, 0xc2d9b2) and the +0x180 face (0xc2d9a2, 0xc2cef8, 0xc2d9ba).
// Bodies (lane EX, startup F3): src/gz/Framework.size.cpp. Creation: the only caller of 0xc2ccdb is WinMain's function-local
// static (0x10fdb50), run before the hooks go in, so in game the live object keeps the game's three vtables and every call
// reaches us through cCTGFrameworkAdapters. Our constructor builds the object for the tests and for our WinMain candidate
// (winmaininfra::Seams().constructFramework, not hooked until the CRT seam, item E1).
class cCTGFramework : public cGZFramework, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kVTable = 0xe995cc, kW32VTable = 0xe99694, kTargetVTable = 0xe996b4; // the game's
    static constexpr uint32_t kMessageServiceStart = 0xcd, kMessageServiceStop = 0xce;

    cCTGFramework();                                  // 0xc2ccdb
    cCTGFramework(const cCTGFramework &) = delete;
    cCTGFramework &operator=(const cCTGFramework &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // 0xc2d995 (the base's)
    uint32_t AddRef() override;                              // 0xc2ceeb
    uint32_t Release() override;                             // 0xc2d9ad

    void OnTick(uint32_t counter) override;          // 0xc2d427: only running services
    // Host instrumentation, not game behaviour: called first in OnTick when set (host/control.cpp's frame counter,
    // which used to detour 0xc2d427; the live framework now ticks through our vtable). Null in tests.
    static void(__cdecl *sTickObserver)();
    ~cCTGFramework() override;                       // slot 40: 0xc2ce90, deleting 0xc2cec9
    bool InitHighPriorityServices() override;        // slot 41, 0xc2d6e6: priority > -2,000,000, descending
    bool InitAppBandServices() override;             // slot 42, 0xc2cf03: [-2,999,999, -2,000,000]
    bool InitLowPriorityServices() override;         // slot 43, 0xc2d506: < -2,999,999
    bool ShutdownLowPriorityServices() override;     // slot 44, 0xc2d8d2
    bool ShutdownAppBandServices() override;         // slot 45, 0xc2cfde
    bool ShutdownHighPriorityServices() override;    // slot 46, 0xc2d5bb
    uint32_t MainLoop() override;                    // slot 48, 0xc2d9c5

    // cIGZMessageTarget2 slot 3 (0xc2d2ca, `this` = the +0x180 face): 0xcd sets every service in mManualServiceIds running
    // (ascending priority), 0xce stops them (descending); true for those two ids, false otherwise and for a null message
    // or one without the cIGZMessage2 interface.
    bool DoMessage(void *message) override;

    std::set<uint32_t> mMessageIds;                  // +0x184 0xcd, 0xce
    std::set<uint32_t> mManualServiceIds;            // +0x18c services the start/stop messages drive (not auto-started)
    uint32_t pad194;                                 // +0x194
};
static_assert(sizeof(cCTGFramework) == 0x198, "cCTGFramework size");
static_assert(offsetof(cCTGFramework, mMessageIds) == 0x184, "cCTGFramework layout: cIGZMessageTarget2 at +0x180");
static_assert(offsetof(cCTGFramework, mManualServiceIds) == 0x18c, "cCTGFramework layout");

// The RVA hooks' entry points for cCTGFramework (as cGZFrameworkAdapters): the live object carries the game's vtables.
struct cCTGFrameworkAdapters {
    static cCTGFramework *__fastcall Construct(void *memory, void *edx);                                   // 0xc2ccdb
    static void __fastcall Destruct(cCTGFramework *self, void *edx);                                       // 0xc2ce90
    static cCTGFramework *__fastcall DeletingDestructor(cIGZFrameWork *self, void *edx, uint32_t flags);   // 0xc2cec9
    static void __fastcall OnTick(cIGZFrameWork *self, void *edx, uint32_t counter);                      // 0xc2d427
    static bool __fastcall InitHighPriorityServices(cIGZFrameWork *self, void *edx);                     // 0xc2d6e6
    static bool __fastcall InitAppBandServices(cIGZFrameWork *self, void *edx);                          // 0xc2cf03
    static bool __fastcall InitLowPriorityServices(cIGZFrameWork *self, void *edx);                      // 0xc2d506
    static bool __fastcall ShutdownLowPriorityServices(cIGZFrameWork *self, void *edx);                  // 0xc2d8d2
    static bool __fastcall ShutdownAppBandServices(cIGZFrameWork *self, void *edx);                      // 0xc2cfde
    static bool __fastcall ShutdownHighPriorityServices(cIGZFrameWork *self, void *edx);                 // 0xc2d5bb
    static uint32_t __fastcall MainLoop(cIGZFrameWork *self, void *edx);                                 // 0xc2d9c5
    static bool __fastcall DoMessage(cIGZMessageTarget2 *self, void *edx, void *message);                // 0xc2d2ca
};

} // namespace gz
