#pragma once
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "rz/cRZCriticalSection.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZThread.h"
#include "rz/cRZThreadSignal.h"

#include <cstddef>
#include <cstdint>
#include <list>

// cTSPersistSpeculativeLoader: the persistence system's background resource preloader (cITSPersistSpeculativeLoader,
// interface 0x6ccf488d, class 0x8ccf489d). RVAs are 1.18.0.187; every function is /O2 ("speed": any .cpp but *.size.cpp).
//
// Who uses it. cTSPersistSystem::Init (0x1e0d60) creates it through the framework (class 0x8ccf489d, registered with the
// factory Create by the cTSPersistCOMDirector getter 0x1d3b70), keeps it at primary +0x1c0 ("object B" in
// cTSPersistSystem.h) and calls Init. The persist system then forwards: cITSPersistSystem slot 6 (0x1e3ae0) -> Preload,
// slot 7 (0x1e04e0) -> GetResource (its resource lookup goes through the loader whenever one exists), slot 8 -> IsAvailable,
// slot 9 -> IsPending; OnTick (0x1e27c0) -> Update; primary vf54 (0x1defd0) -> Clear; PreAppShutdown (0x1e2ce0) -> Shutdown
// and Release.
//
// How it works. A request (key + timing) goes to the physical layer's pending queue; a worker thread (ThreadMain ->
// PhysicalLayer::Process) takes the highest-priority one, finds the DB segment holding the key through the resource
// manager, and either reads the record's raw (possibly compressed) bytes into a cPreloadedRecord (a separate class,
// anonymous namespace, 0x58 bytes, constructor inlined here) or opens the record. With followReferences the opened record is
// read as a list of further keys, each queued as a child request; the parent waits in the waiting queue until its last
// child is done (CompleteParents). Finished requests go to the done queue. On the main thread, Update (once per persist
// system tick, with a time budget) pops done requests, decompresses preloaded raw records, creates the resource through
// the factory the resource manager names, and keeps it in the logical layer's pool (GetResource hands it out once) and/or
// gives it to the resource manager's cache. Pooled and waiting requests expire after `lifetime` ms.
//
// Layout (0xa0, factory 0x1f7ba0, deleting destructor slot 13 0x1f7580):
//   +0x00 vtable 0xda6ef0 (14 slots), +0x04 reference count (AddRef 0x148fb0 / Release 0x151900 are folded, not ours),
//   +0x08 physical layer (0x44), +0x4c logical layer (0x18), +0x64 cRZThread, +0x70 cRZStopwatch (mode 4: ms) timing the
//   frame between Updates, +0x88 average frame time (float, starts 33.333f), +0x8c..+0x98 statistics, +0x6c and +0x9c
//   never touched by any function.
// Recorded in the shared Ghidra program (lane CD): structs cTSPersistSpeculativeLoader, ...PhysicalLayer, ...LogicalLayer,
// ...Layer, ...Queue, ...Pool, SpecLoaderRequest, SpecLoaderNode, SpecLoaderList, SpecLoaderKeyVector, cRZLock (and
// sized cRZCriticalSection, cRZThreadSignal, cRZThread, cRZStopwatch); every function below is named and typed there.

class cTSPersistSpeculativeLoader;
class cTSPersistSpeculativeLoaderPhysicalLayer;

namespace SpecLoader {
// Interface and service ids the loader passes (QueryInterface / framework GetService slot 5).
constexpr uint32_t kClassId = 0x8ccf489d;                // GetClassId 0x1f7d40, COM registration
constexpr uint32_t kInterfaceId = 0x6ccf488d;            // cITSPersistSpeculativeLoader
constexpr uint32_t kServiceId = 0x056b906e;              // framework service the two below come from
constexpr uint32_t kResourceManagerIid = 0x656b8efc;     // the persist system's resource-manager face
constexpr uint32_t kPersistSystemIid = 0x9af5e924;       // cITSPersistSystem (Update's reference preloads, its slot 6)
constexpr uint32_t kReferenceServiceId = 0x8af76dc8;     // a service that lists a resource's referenced keys (slot 6)
constexpr uint32_t kReferenceServiceIid = 0x4af76d8c;
constexpr uint32_t kDBRecordIid = 0xe56b8f03;            // cIGZPersistDBRecord: slot 6 is called when a request is dropped
constexpr uint32_t kRecordStreamIid = 0xc56b8f08;        // record as an input stream: slots 13 GetUint32, 54/55 position
constexpr uint32_t kPackedSegmentIid = 0x286d4a6a;       // segment face for raw reads: slots 27 size, 28 read, 41 compressed
constexpr uint32_t kPreloadedRecordIid = 0xcdb88c9a;     // cPreloadedRecord's own face (raw bytes, +0x55 compressed flag)
constexpr uint32_t kPersistResourceIid = 0x456b8f1d;     // asked of the factory when Update creates the resource
constexpr uint32_t kLocalGroup = 0x1c0532fa;             // Process retries the segment lookup with this group
// Types Process loads raw even when the request follows references.
constexpr uint32_t kRawTypeA = 0x1c4a276c;
constexpr uint32_t kRawTypeB = 0xac4f8687;
constexpr uint32_t kRawTypeC = 0xed534136;
constexpr uint32_t kThreadNameRva = 0xda7144;            // "TSPersistSpecLoaderThread"
constexpr uint32_t kPreloadedRecordSize = 0x58;
} // namespace SpecLoader

// The counted object reference a request holds (4 bytes; AddRef slot 1, Release slot 2, null allowed). Every use is
// inlined in the original: copy = AddRef; assignment = `if (this != &other && mObject != other.mObject) { AddRef new;
// store; Release old }` (PopBack 0x1f7de0 shows both checks); destructor = Release (noexcept: its EH state calls
// terminate). The bodies belong in this header (inline) when implemented.
class SpecLoaderObjectRef {
public:
    SpecLoaderObjectRef() = default;
    SpecLoaderObjectRef(const SpecLoaderObjectRef &other);
    SpecLoaderObjectRef &operator=(const SpecLoaderObjectRef &other);
    ~SpecLoaderObjectRef();
    void *Get() const { return mObject; }

    void *mObject = nullptr; // +0x00 a cIGZUnknown (a record, a cPreloadedRecord face +4, or the created resource)
};
static_assert(sizeof(SpecLoaderObjectRef) == 4, "object reference");

inline SpecLoaderObjectRef::SpecLoaderObjectRef(const SpecLoaderObjectRef &other) : mObject(other.mObject)
{
    if (mObject) VirtualCall<uint32_t(__thiscall *)(void *), 1>(mObject);
}

inline SpecLoaderObjectRef &SpecLoaderObjectRef::operator=(const SpecLoaderObjectRef &other)
{
    if (&other.mObject != &mObject) {
        void *incoming = other.mObject;
        if (incoming != mObject) {
            if (incoming) VirtualCall<uint32_t(__thiscall *)(void *), 1>(incoming);
            void *old = mObject;
            mObject = incoming;
            if (old) VirtualCall<uint32_t(__thiscall *)(void *), 2>(old);
        }
    }
    return *this;
}

inline SpecLoaderObjectRef::~SpecLoaderObjectRef()
{
    if (mObject) VirtualCall<uint32_t(__thiscall *)(void *), 2>(mObject);
}

// One preload request (0x38): the value of every list below (node 0x40: next, prev, value).
struct SpecLoaderRequest {
    // Default construction zeroes key, parentKey and object only; the rest stays uninitialised, as in the original.
    SpecLoaderRequest() : key{}, parentKey{} {}

    // Merges of a request for the same key (the rules differ per caller, see notes/feedback/CD.md) are never a function in
    // the original: every merge is written out inline. The default construction seen in Process, QueueReferences and
    // Update zeroes key, parentKey and object only (time..followReferences stay uninitialised); Preload's construction
    // zeroes parentKey and object, stores the arguments, then assigns parentKey again from a zeroed temporary key.
    cGZPersistResourceKey key;       // +0x00
    cGZPersistResourceKey parentKey; // +0x10 the request whose reference list queued this one (only on its last child);
                                     //       zero (group 0 tested at +0x14) when none
    uint32_t time;                   // +0x20 clock() when requested; a merge copies the newer request's
    uint32_t priority;               // +0x24 queues are sorted ascending and served from the back (largest first)
    uint32_t lifetime;               // +0x28 ms after `time` a pending/waiting/pooled request is dropped; 0 = never
    uint32_t retain;                 // +0x2c done requests with 0 here are dropped by Update (unless clock went back)
    bool poolOnly;                   // +0x30 keep the resource in the pool only (else also the resource manager's cache)
    bool followReferences;           // +0x31 also preload the keys the record lists (cleared when there are none)
    SpecLoaderObjectRef object;      // +0x34
};
static_assert(sizeof(SpecLoaderRequest) == 0x38, "request");
static_assert(offsetof(SpecLoaderRequest, parentKey) == 0x10 && offsetof(SpecLoaderRequest, time) == 0x20, "request");
static_assert(offsetof(SpecLoaderRequest, poolOnly) == 0x30 && offsetof(SpecLoaderRequest, object) == 0x34, "request");

// MSVC 14.40 std::list, as in the game. Out of line in the game (STL code, do not hook): _Emplace 0x1f7160 (`insert(where,
// const value&)`, "list too long" at 0x3ffffff) and _Free_non_head 0x1f7230; everything else (push_back, erase, clear,
// pop_front/back, the constructor's head allocation) is inlined at each use.
using SpecLoaderList = std::list<SpecLoaderRequest>;
static_assert(sizeof(SpecLoaderList) == 8, "list");

// A request queue (0xc, vtable 0xda6ed0: slot 0 is the deleting destructor). Sorted by priority ascending.
class cTSPersistSpeculativeLoaderQueue {
public:
    static constexpr uint32_t kVTable = 0xda6ed0;

    // Slot 0, 0x1f7710 (ret 4, EH frame): Clear, ~list, flags & 1: operator delete(this, 0xc). Returns this.
    cTSPersistSpeculativeLoaderQueue *DeletingDestructor(uint32_t flags) noexcept;
    void Destruct();                                                  // 0x1f7450 (EH unwind only): Clear, ~list
    void Clear();                                                      // 0x1f7ae0 (ret 0, EH frame)
    bool Remove(const cGZPersistResourceKey &key);                     // 0x1f7900 (ret 4, EH frame)
    SpecLoaderRequest *Find(const cGZPersistResourceKey &key);         // 0x1f79f0 (ret 4)
    bool Contains(const cGZPersistResourceKey &key);                   // 0x1f8260 (ret 4)
    bool PopBack(SpecLoaderRequest &out);                              // 0x1f7de0 (ret 4, EH frame)
    bool Insert(const SpecLoaderRequest &request);                     // 0x1f8800 (ret 4, EH frame)

    const void *mVTable; // +0x00
    SpecLoaderList mList; // +0x04 head, +0x08 size
};
static_assert(sizeof(cTSPersistSpeculativeLoaderQueue) == 0xc, "queue");

// The pool of finished requests (0xc, vtable 0xda6ec8). No member functions of its own out of line: its find/erase/clear
// are inlined into the loader's and the logical layer's functions.
class cTSPersistSpeculativeLoaderPool {
public:
    static constexpr uint32_t kVTable = 0xda6ec8;

    // Slot 0, 0x1f76b0 (ret 4): clear() (free the nodes, relink the head, size 0), ~list, flags & 1: delete 0xc.
    cTSPersistSpeculativeLoaderPool *DeletingDestructor(uint32_t flags);

    const void *mVTable;  // +0x00
    SpecLoaderList mList; // +0x04
};
static_assert(sizeof(cTSPersistSpeculativeLoaderPool) == 0xc, "pool");

// Base of both layers (8 bytes, vtable 0xda6ed8: slot 0 deleting destructor).
class cTSPersistSpeculativeLoaderLayer {
public:
    static constexpr uint32_t kVTable = 0xda6ed8;

    // Slot 0, 0x1f7600 (ret 4): vtable, flags & 1: delete 8. 0x1f7310 (7 bytes, not hookable) is the plain destructor.
    cTSPersistSpeculativeLoaderLayer *DeletingDestructor(uint32_t flags);

    const void *mVTable;                  // +0x00
    cTSPersistSpeculativeLoader *mOwner;  // +0x04 null after construction; Init sets only the logical layer's
};
static_assert(sizeof(cTSPersistSpeculativeLoaderLayer) == 8, "layer");

// The physical layer (0x44, vtable 0xda6ee0): the worker thread's side. mLock guards the queues and mCurrent.
class cTSPersistSpeculativeLoaderPhysicalLayer : public cTSPersistSpeculativeLoaderLayer {
public:
    static constexpr uint32_t kVTable = 0xda6ee0;

    // Slot 0, 0x1f7690 (ret 4): Destruct, flags & 1: delete 0x44.
    cTSPersistSpeculativeLoaderPhysicalLayer *DeletingDestructor(uint32_t flags);
    void Destruct();                                                   // 0x1f7370 (ret 0, EH frame)
    bool IsPending(const cGZPersistResourceKey &key);                  // 0x1f81b0 (ret 4, EH frame)
    bool Submit(const SpecLoaderRequest &request);                     // 0x1f8650 (ret 4, EH frame)
    bool Process(uint32_t now);                                        // 0x1f9360 (ret 4, EH frame): returns !mQuit
    void CompleteParents(const SpecLoaderRequest &request);            // 0x1f8330 (ret 4, EH frame)
    bool QueueReferences(SpecLoaderRequest &request, void *record);    // 0x1f89d0 (ret 8, EH frame)
    // 0x1f9860 (cdecl): the thread routine Init hands to cRZThread slot 2. Null -> -1; else
    // `do { mSignal slot 3 (wait, -1); if (mQuit) break; } while (Process(clock())); return 0;`.
    static int __cdecl ThreadMain(cTSPersistSpeculativeLoaderPhysicalLayer *layer);

    cRZThreadSignal mSignal;                 // +0x08 constructed with 1; slot 1 set (Submit, Shutdown), 6 reset (Process idle)
    cRZCriticalSection mLock;                // +0x10
    bool mQuit;                              // +0x18 set by Shutdown
    cTSPersistSpeculativeLoaderQueue mPending; // +0x1c new requests (Submit, QueueReferences' children)
    cTSPersistSpeculativeLoaderQueue mWaiting; // +0x28 loaded, waiting for their referenced children
    cTSPersistSpeculativeLoaderQueue mDone;    // +0x34 loaded, for LogicalLayer::Update
    SpecLoaderRequest *mCurrent;             // +0x40 the request Process is loading (a stack object of Process); Cancel nulls it
};
static_assert(sizeof(cTSPersistSpeculativeLoaderPhysicalLayer) == 0x44, "physical layer");
static_assert(offsetof(cTSPersistSpeculativeLoaderPhysicalLayer, mSignal) == 0x08, "physical layer");
static_assert(offsetof(cTSPersistSpeculativeLoaderPhysicalLayer, mLock) == 0x10, "physical layer");
static_assert(offsetof(cTSPersistSpeculativeLoaderPhysicalLayer, mQuit) == 0x18, "physical layer");
static_assert(offsetof(cTSPersistSpeculativeLoaderPhysicalLayer, mPending) == 0x1c, "physical layer");
static_assert(offsetof(cTSPersistSpeculativeLoaderPhysicalLayer, mDone) == 0x34, "physical layer");
static_assert(offsetof(cTSPersistSpeculativeLoaderPhysicalLayer, mCurrent) == 0x40, "physical layer");

// The logical layer (0x18, vtable 0xda6ee8): the main thread's side.
class cTSPersistSpeculativeLoaderLogicalLayer : public cTSPersistSpeculativeLoaderLayer {
public:
    static constexpr uint32_t kVTable = 0xda6ee8;

    // Slot 0, 0x1f7630 (ret 4): ~Pool inlined, Layer vtable, flags & 1: delete 0x18.
    cTSPersistSpeculativeLoaderLogicalLayer *DeletingDestructor(uint32_t flags);
    void Destruct();                                                   // 0x1f7320 (EH unwind of the factory only)
    bool Cancel(const cGZPersistResourceKey &key);                     // 0x1f7790 (ret 4, EH frame)
    bool Update(uint32_t now, uint32_t budget);                        // 0x1f8d90 (ret 8, EH frame); budget in ms

    cTSPersistSpeculativeLoaderPhysicalLayer *mPhysical; // +0x08 NOT set by the constructor; Init sets it to loader +8
    cTSPersistSpeculativeLoaderPool mPool;               // +0x0c
};
static_assert(sizeof(cTSPersistSpeculativeLoaderLogicalLayer) == 0x18, "logical layer");
static_assert(offsetof(cTSPersistSpeculativeLoaderLogicalLayer, mPhysical) == 0x08, "logical layer");
static_assert(offsetof(cTSPersistSpeculativeLoaderLogicalLayer, mPool) == 0x0c, "logical layer");

class cTSPersistSpeculativeLoader {
public:
    static constexpr uint32_t kVTable = 0xda6ef0;
    static constexpr uint32_t kSize = 0xa0;
    static constexpr float kInitialAverageFrameTime = 33.333332f; // 0x42055555
    static constexpr uint32_t kDefaultBudget = 10;              // ms, before the frame timer runs
    static constexpr uint32_t kMinBudget = 5;
    static constexpr uint32_t kMaxBudget = 100;
    static constexpr uint32_t kUpdateItemLimit = 5000;          // LogicalLayer::Update's per-call cap (+1 per re-queue)

    static cTSPersistSpeculativeLoader *__cdecl Create();       // 0x1f7ba0 (EH frame, 10 states): new 0xa0 + inlined ctors
    static uint32_t __cdecl GetClassId();                       // 0x1f7d40: kClassId

    // cITSPersistSpeculativeLoader (vtable 0xda6ef0)
    bool QueryInterface(uint32_t riid, void **out);                 // slot 0, 0x1f8300 (ret 8)
    //                                                                  slot 1 AddRef: folded 0x148fb0 (++mRefCount)
    //                                                                  slot 2 Release: folded 0x151900 (0 -> slot 13(1))
    bool Init();                                                    // slot 3, 0x1f8050 (ret 0)
    bool Shutdown();                                                // slot 4, 0x1f8c70 (ret 0, EH frame)
    bool Preload(const cGZPersistResourceKey &key, bool poolOnly, bool followReferences, uint32_t priority,
                 uint32_t lifetime, uint32_t retain);               // slot 5, 0x1f8450 (ret 0x18, EH frame)
    bool Cancel(const cGZPersistResourceKey &key);                  // slot 6, 0x1f7780 (8 bytes: add ecx, 0x4c; jmp)
    bool IsAvailable(const cGZPersistResourceKey &key);             // slot 7, 0x1f80b0 (ret 4, EH frame)
    bool IsPending(const cGZPersistResourceKey &key);               // slot 8, 0x1f8170 (ret 4)
    bool GetResource(const cGZPersistResourceKey &key, uint32_t riid, void **out, uint32_t a4, uint32_t a5,
                     bool *fromPool);                               // slot 9, 0x1f7ee0 (ret 0x18, EH frame)
    bool Update();                                                  // slot 10, 0x1f8cf0 (ret 0)
    void Clear();                                                   // slot 11, 0x1f7a40 (ret 0, EH frame)
    bool GetStatistics(uint32_t *hits, uint32_t *expired, uint32_t *cancelled,
                       uint32_t *misses);                           // slot 12, 0x1f7da0 (ret 0x10)
    cTSPersistSpeculativeLoader *DeletingDestructor(uint32_t flags); // slot 13, 0x1f7580 (ret 4)

    const void *mVTable;                               // +0x00
    uint32_t mRefCount;                                // +0x04
    cTSPersistSpeculativeLoaderPhysicalLayer mPhysical; // +0x08
    cTSPersistSpeculativeLoaderLogicalLayer mLogical;  // +0x4c
    cRZThread mThread;                                 // +0x64 slot 12 SetName, 2 start(fn, arg), 8 wait for exit(-1)
    uint32_t mUnused6c;                                // +0x6c
    cRZStopwatch mFrameTimer;                          // +0x70 mode 4; never Started by the ctor (Update restarts it)
    float mAverageFrameTime;                           // +0x88 ms
    uint32_t mHits;                                    // +0x8c GetResource served from the pool
    uint32_t mExpired;                                 // +0x90 only Update counts it, through the physical layer's
                                                       //       mOwner, which nothing sets: stays 0
    uint32_t mCancelled;                               // +0x94 GetResource missed, but the request was pending
    uint32_t mMisses;                                  // +0x98 GetResource missed, nothing pending
    uint32_t mUnused9c;                                // +0x9c
};
static_assert(sizeof(cTSPersistSpeculativeLoader) == cTSPersistSpeculativeLoader::kSize, "loader");
static_assert(offsetof(cTSPersistSpeculativeLoader, mPhysical) == 0x08, "loader");
static_assert(offsetof(cTSPersistSpeculativeLoader, mLogical) == 0x4c, "loader");
static_assert(offsetof(cTSPersistSpeculativeLoader, mThread) == 0x64, "loader");
static_assert(offsetof(cTSPersistSpeculativeLoader, mFrameTimer) == 0x70, "loader");
static_assert(offsetof(cTSPersistSpeculativeLoader, mAverageFrameTime) == 0x88, "loader");
static_assert(offsetof(cTSPersistSpeculativeLoader, mHits) == 0x8c, "loader");
static_assert(offsetof(cTSPersistSpeculativeLoader, mMisses) == 0x98, "loader");
