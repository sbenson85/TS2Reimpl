#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <unordered_map>

#include "rz/RZFrameworkHelpers.h"
#include "rz/cRZCriticalSection.h"

// The framework: cGZFramework (cIGZFrameWork + cIGZFrameWorkW32) and the game's subclass
// cCTGFramework (adds cIGZMessageTarget2 and the six service-band loops). RVAs are 1.18.0.187.
//
// One static instance exists (0x10fdb50, built by WinMain); g_Framework (0x10c9378) points at it.
// It owns the system services (a priority-ordered multimap plus an id hash map, both holding
// references), the hook list (cIGZFrameWorkHooks), the tick and on-idle lists, a lock, the command
// line, the debug streams and the exit/quit state.
//
// The classes themselves are gz::cGZFramework and gz::cCTGFramework (gz/FrameworkShape.h, lanes ER and EX: every
// function ours). What is left here are layout views with the same field names and no code: `::cGZFramework` /
// `::cCTGFramework` for code that reads or patches the raw object (the tests' vtable copies and field snapshots,
// WinMain's tests), the container elements both shapes use (FwRef, FwServiceEntry, FwWalk), and the free functions
// GZFrameWork_Run / GZFrameWork_Shutdown / ReleaseFrameworkGlobals.
//
// Locking: every operation that touches the lists takes the lock at +0xf4 through its vtable
// (slots 1 and 2), as the original does. The busy bytes at +0xe4..+0xe8 reject changes to a list
// while it is being copied or walked; the nesting counts at +0xec and +0xf0 defer the release of
// removed ticking services until the outermost tick/idle pass ends (the removed service is kept
// alive in the +0xc4 / +0xb4 lists meanwhile).

// A counted reference as the framework's containers hold it: copying AddRefs (vtable slot 1),
// destroying Releases (slot 2, noexcept as the original's frames are), and Assign() is the verified
// RZUnknownSlot::Assign (0x142080).
struct FwRef {
    void *mObject;

    FwRef() noexcept : mObject(nullptr) {}
    explicit FwRef(void *object);
    FwRef(const FwRef &other);
    FwRef &operator=(const FwRef &) = delete;
    ~FwRef() noexcept;
    void Assign(void *object) { reinterpret_cast<RZUnknownSlot *>(this)->Assign(object); }
};
static_assert(sizeof(FwRef) == 4, "FwRef layout");

// What the priority-ordered service container stores per service (the key is the priority).
struct FwServiceEntry {
    uint32_t mId;    // GetServiceID as the second call returned it
    FwRef mService;
};

// 0x142cb2's two walk directions.
enum class FwWalk : uint8_t { Descending = 0, Ascending = 1 };

class cGZFramework {
public:
    static constexpr uint32_t kVTable = 0xd9b544;       // cGZFramework's own (never installed on the live object)
    static constexpr uint32_t kCTGVTable = 0xe995cc;    // cCTGFramework, the live one
    static constexpr uint32_t kGlobalFramework = 0x10c9378;  // g_Framework (also 0x10c937c)
    static constexpr uint32_t kGlobalFramework2 = 0x10c937c; // second reference to the same object
    static constexpr uint32_t kGlobalApp = 0x10c9380;        // g_App, the cIGZApp face of the application
    static constexpr uint32_t kGlobalExitCode = 0x10c9384;   // g_ExitCode

    // ---- fields (offsets are of the whole object) ----
    const void *mVTable;                   // +0x00 cIGZFrameWork
    const void *mW32VTable;                // +0x04 cIGZFrameWorkW32
    uint8_t mCOM[0x70];                    // +0x08 embedded cGZCOM (its own class)
    uint32_t mRefCount;                    // +0x78
    std::list<FwRef> mHooks;               // +0x7c
    std::multimap<int32_t, FwServiceEntry> mServices; // +0x84, key = service priority
    std::unordered_map<uint32_t, FwRef> mServiceById; // +0x8c
    std::list<FwRef> mIdleList;            // +0xac services called by OnIdle
    std::list<FwRef> mIdleRemoved;         // +0xb4 removed during an idle pass, released when it ends
    std::list<FwRef> mTickList;            // +0xbc services called by OnTick, ordered by tick priority
    std::list<FwRef> mTickRemoved;         // +0xc4 removed during a tick pass, released when it ends
    uint8_t mTickEnabled;                  // +0xcc
    uint8_t padCD[3];
    uint32_t mIdleInterval;                // +0xd0 ticks per idle pass, at least 1 (4 at construction)
    uint32_t mTickCounter;                 // +0xd4
    uint32_t mIdleCounter;                 // +0xd8
    uint8_t mInstall;                      // +0xdc what IsInstall returns (nothing in the code sets it)
    uint8_t padDD[3];
    uint32_t mState;                       // +0xe0 startup/shutdown stage, 0..14
    uint8_t mServicesBusy;                 // +0xe4 the priority container is being copied or edited
    uint8_t mLookupBusy;                   // +0xe5 an id lookup or removal is in progress
    uint8_t mIdleBusy;                     // +0xe6 the idle list is being copied
    uint8_t mTickBusy;                     // +0xe7 the tick list is being copied
    uint8_t mHooksBusy;                    // +0xe8 the hook list is being copied
    uint8_t padE9[3];
    int32_t mIdleNesting;                  // +0xec OnIdle passes in progress
    int32_t mTickNesting;                  // +0xf0 OnTick passes in progress
    cRZCriticalSection mLock;              // +0xf4 (8 bytes: vtable, block)
    uint8_t mCmdLine[0x54];                // +0xfc embedded cRZCmdLine (its own class)
    uint8_t mDeferQuit;                    // +0x150 set from Startup's second argument: Quit only records the request
    uint8_t pad151[3];
    void *mDebugStream;                    // +0x154 raw pointer (GetDebugStream / SetDebugStream)
    FwRef mStdOut;                         // +0x158
    FwRef mStdErr;                         // +0x15c
    FwRef mStdIn;                          // +0x160
    int32_t mDebugLevel;                   // +0x164 (2 at construction)
    FwRef mExceptionNotification;          // +0x168 a cRZUnknown-layout object made at construction
    uint8_t mQuitPending;                  // +0x16c
    uint8_t pad16D[3];
    uint32_t mQuitCode;                    // +0x170 (not initialised by the constructor)
    uint32_t mHInstance;                   // +0x174 GetModuleHandleA(NULL)
    void *mMainHWND;                       // +0x178
    uint32_t pad17C;                       // +0x17c never written
};

// The cIGZMessageTarget2 subobject of cCTGFramework (+0x180) and the two id sets that follow it (layout view).
struct cCTGMessageTarget {
    const void *mVTable;                       // +0x180
    std::set<uint32_t> mMessageIds;            // +0x184 message ids the framework listens for (0xcd, 0xce)
    std::set<uint32_t> mManualServiceIds;      // +0x18c services whose running flag the start/stop messages drive
    uint32_t pad194;                           // +0x194 never written
};

// Layout view of gz::cCTGFramework (no code: the functions are the class's).
class cCTGFramework : public cGZFramework {
public:
    cCTGMessageTarget mTarget;                // +0x180
};

static_assert(sizeof(cGZFramework) == 0x180, "cGZFramework layout");
static_assert(sizeof(cCTGMessageTarget) == 0x18, "cCTGMessageTarget layout");
static_assert(sizeof(cCTGFramework) == 0x198, "cCTGFramework layout");
static_assert(offsetof(cGZFramework, mRefCount) == 0x78, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mHooks) == 0x7c, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mServices) == 0x84, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mServiceById) == 0x8c, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mIdleList) == 0xac, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mIdleRemoved) == 0xb4, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mTickList) == 0xbc, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mTickRemoved) == 0xc4, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mTickEnabled) == 0xcc, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mIdleInterval) == 0xd0, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mInstall) == 0xdc, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mState) == 0xe0, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mServicesBusy) == 0xe4, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mHooksBusy) == 0xe8, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mIdleNesting) == 0xec, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mTickNesting) == 0xf0, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mLock) == 0xf4, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mCmdLine) == 0xfc, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mDeferQuit) == 0x150, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mDebugStream) == 0x154, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mStdOut) == 0x158, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mDebugLevel) == 0x164, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mExceptionNotification) == 0x168, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mQuitPending) == 0x16c, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mHInstance) == 0x174, "cGZFramework layout");
static_assert(offsetof(cGZFramework, mMainHWND) == 0x178, "cGZFramework layout");
static_assert(offsetof(cCTGFramework, mTarget) == 0x180, "cCTGFramework layout");

// ---- Startup, run and shutdown (free functions on g_Framework / g_App; src/gz/Framework.size.cpp) ----
// 0x144514 GZFrameWork_Startup(cmdline, installFlag) is fw_startup::GZFrameWork_Startup (gz/FrameworkStartup.h).

// 0x1446ba (__cdecl, no arguments): app = g_Framework->Application(); state = 7; unless
// app->GZRun() (slot 8) returns true, g_Framework->MainLoop() (slot 48). Returns g_ExitCode == 0.
bool __cdecl GZFrameWork_Run();

// 0x14472b (__cdecl, no arguments): states 8..14 around the shutdown sequence (see the .cpp), then
// releases g_App and g_Framework (0x143782). Returns g_ExitCode == 0.
bool __cdecl GZFrameWork_Shutdown();

// 0x143782 (__cdecl, no arguments): Release and clear g_App, then g_Framework, then g_Framework2.
void __cdecl ReleaseFrameworkGlobals();
