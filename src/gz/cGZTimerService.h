#pragma once
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZStopwatch.h"

#include <cstddef>
#include <cstdint>

// cIGZTimerService (interface id 0x48e9562e): the timer service's clock face (game vtable 0xe9ed08, 7 slots). Its slots in
// cGZTimerService are folded field accessors shared with other classes (with `this` the face at +0x1c).
class cIGZTimerService : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x48e9562e;

    virtual uint32_t GetTime() = 0;                   // slot 3 (folded 0x147a30: +0x20 of the service)
    virtual uint32_t GetDelta() = 0;                  // slot 4 (folded 0x46390: +0x24)
    virtual uint32_t GetTickCount() = 0;              // slot 5 (folded 0x149cf0: +0x2c)
    virtual void SetMaxDelta(uint32_t maxDelta) = 0;  // slot 6 (folded 0x47fc80: +0x34, ret 4)
};

// The timer service: ticks a stopwatch once per frame through the framework's tick list and keeps the total time, the last
// frame's step (clamped, so a long stall does not become a huge step), and a frame count. In the standalone shape
// (docs/standalone-shape.md, stage A; lane KD): RTTI bases cRZSystemService at +0 (cIGZSystemService +0, cRZUnknownMT +4)
// and cIGZTimerService at +0x1c. Game vtables 0xe9ecb0 (14 slots: the service's 13 and the class's SetTimeOffset), 0xe9ecec
// (6; slot 3 the deleting destructor, thunk 0xc6d14d into 0xc6d155), 0xe9ed08 (7). Service id 0x68e9565a, priority
// 0xffe17b81. 0x50 bytes.
//
// The time fields (all milliseconds, from the stopwatch in mode 4): mTime is where the clock stands (the stopwatch plus a
// compensation offset), mDelta the step since the last tick, mRaw the stopwatch's own reading, mOffset what is added to the
// stopwatch to make a clamped step add up.
//
// Creation: the constructor 0xc6d0da has no caller in the image (the game's AddApplicationServicesHere, replaced by ours,
// built it); ours builds this class through the adapter. The game's vtables are referenced only by the constructor and the
// destructor 0xc6d126 (reached only from the deleting destructor 0xc6d155, which only the game's +4 vtable holds).
class cGZTimerService : public rz::cRZSystemService, public cIGZTimerService {
public:
    static constexpr uint32_t kVTable = 0xe9ecb0; // the game's, for tests
    static constexpr uint32_t kUnknownVTable = 0xe9ecec;
    static constexpr uint32_t kTimerVTable = 0xe9ed08;
    static constexpr uint32_t kSize = 0x50;
    static constexpr uint32_t kServiceId = 0x68e9565a;
    static constexpr int32_t kPriority = static_cast<int32_t>(0xffe17b81);
    static constexpr uint32_t kTimerInterfaceId = cIGZTimerService::kIid;
    static constexpr uint32_t kDefaultMaxDelta = 0x7d0;
    static constexpr int32_t kStopwatchMode = 4;
    // Framework (the global at 0x10c9378) vtable slots Init and Shutdown call.
    static constexpr int kFrameworkAddToTick = 9;
    static constexpr int kFrameworkRemoveFromTick = 10;

    // 0xc6d0da: the service part (id, priority), the time fields zero, the step limit 0x7d0, the stopwatch in mode 4.
    cGZTimerService();
    // 0xc6d126 (deleting 0xc6d155, +4 thunk 0xc6d14d): the service part (the stopwatch's destructor 0x140fe0 is empty).
    ~cGZTimerService() override;
    cGZTimerService(const cGZTimerService &) = delete;
    cGZTimerService &operator=(const cGZTimerService &) = delete;

    // cIGZUnknown (every face)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0xc6d1d3: the timer iid -> +0x1c, else the service's
    uint32_t AddRef() override;                              // folded 0x1cc320 (jmp to the service part's)
    uint32_t Release() override;                             // folded 0x1cd7a0

    // cIGZSystemService
    bool Init() override;                  // slot 8, 0xc6d174: AddToTick on the framework, start the stopwatch
    bool Shutdown() override;              // slot 9, 0xc6d20e: RemoveFromTick on the framework
    bool OnTick(uint32_t counter) override; // slot 10, 0xc6d190 (the argument is not read)

    // The class's own slot 13 of the +0 face (folded 0x5506e0, ret 4).
    virtual void SetTimeOffset(uint32_t offset);

    // cIGZTimerService
    uint32_t GetTime() override;
    uint32_t GetDelta() override;
    uint32_t GetTickCount() override;
    void SetMaxDelta(uint32_t maxDelta) override;

    uint32_t mTime;           // 0x20
    uint32_t mDelta;          // 0x24
    uint32_t mRaw;            // 0x28
    uint32_t mTicks;          // 0x2c
    uint32_t mOffset;         // 0x30
    uint32_t mMaxDelta;       // 0x34 steps above this are clamped to it
    cRZStopwatch mStopwatch;  // 0x38 (hook-shaped: built by its Construct)
};

static_assert(sizeof(cGZTimerService) == cGZTimerService::kSize, "cGZTimerService size");
static_assert(offsetof(cGZTimerService, mRefCount) == 8, "cGZTimerService layout: cRZUnknownMT at +4");
static_assert(offsetof(cGZTimerService, mTime) == 0x20, "cGZTimerService layout");
static_assert(offsetof(cGZTimerService, mOffset) == 0x30, "cGZTimerService layout");
static_assert(offsetof(cGZTimerService, mMaxDelta) == 0x34, "cGZTimerService layout");
static_assert(offsetof(cGZTimerService, mStopwatch) == 0x38, "cGZTimerService layout");

// The constructor hook's entry point (docs/standalone-shape.md, step 6). The slots' adapters are gone: only the game's
// vtables reach their RVAs (in effect through callers).
struct cGZTimerServiceAdapters {
    static cGZTimerService *__fastcall Construct(void *memory, void *edx); // 0xc6d0da
};
