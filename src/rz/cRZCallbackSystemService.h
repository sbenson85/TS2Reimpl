#pragma once
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"

#include <cstddef>
#include <cstdint>

class cRZCallbackThread; // src/rz/cRZThread.h

// cIGZCallbackSystemService (interface id 0x276c784b): the callback system service's own face (game vtable 0xe9e714, 4
// slots). Its only implementer is cRZCallbackSystemService; the interface's own vtable 0xe9e700 (purecalls) is what the
// original constructor stores first.
class cIGZCallbackSystemService : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x276c784b;

    virtual cRZCallbackThread *GetCallbackThread() = 0; // slot 3 (ret 0)
};

// The callback system service: owns the callback thread object that runs queued callbacks. In the standalone shape
// (docs/standalone-shape.md, stage A; lane KD): RTTI bases cIGZCallbackSystemService at +0, cRZSystemService at +4
// (cIGZSystemService +4, cRZUnknownMT +8). Game vtables 0xe9e714 (4 slots), 0xe9e728 (13), 0xe9e760 (6). Service id
// 0xa76c78e7, priority 0x2dc6bf. The object is 0x24 bytes: the primary vtable, the service part at +4 (0x1c bytes) and the
// callback thread at +0x20.
//
// Creation: the constructor 0xc68264 has one caller, cTSAppFW::AddApplicationServicesHere 0xc267fb (ours: builds this class
// through the adapter); the game's vtables are referenced only by the constructor and the destructor 0xc682c7 (reached only
// from the deleting destructor 0xc68316, which only the game's +8 vtable holds, through the thunk 0xc6830e). With the
// constructor hook on, no service carries the game's vtables.
//
// Slots: QueryInterface 0xc68335 (+4 / +8 thunks 0xc68375 / 0xc6837d); AddRef / Release the folded 0x145240 / 0x1a85b0 (the
// service part's, `add ecx, 4; jmp`); GetCallbackThread the folded 0x15e550 (`mov eax, [ecx + 0x20]`); the +4 / +8 faces'
// other slots cRZSystemService's; the +8 face's slot 3 the deleting destructor (thunk 0xc6830e into 0xc68316).
class cRZCallbackSystemService : public cIGZCallbackSystemService, public rz::cRZSystemService {
public:
    static constexpr uint32_t kVTable = 0xe9e714;             // the game's, for tests
    static constexpr uint32_t kSystemServiceVTable = 0xe9e728;
    static constexpr uint32_t kUnknownVTable = 0xe9e760;
    static constexpr uint32_t kSize = 0x24;
    static constexpr uint32_t kServiceId = 0xa76c78e7;
    static constexpr int32_t kPriority = 0x2dc6bf;
    static constexpr uint32_t kInterfaceId = cIGZCallbackSystemService::kIid;
    static constexpr uint32_t kSystemServiceId = 0x287fb697; // cIGZSystemService
    // Slots of the callback thread called through its vtable.
    static constexpr int kThreadSlotAddRef = 1;
    static constexpr int kThreadSlotRelease = 2;
    static constexpr int kThreadSlotDelete = 8;

    // 0xc68264: the service part (id, priority), then a new callback thread (0x9cfa5d), its ownership byte set (0x4ea470's
    // store) and AddRef'd through its slot 1.
    cRZCallbackSystemService();
    // 0xc682c7 (deleting 0xc68316, +8 thunk 0xc6830e): the thread released (slot 2) when its ownership byte (+9, read as
    // 0x4e9cc0 does) is set, otherwise deleted through its slot 8 with flags 1 when not null; the pointer cleared; the
    // service part.
    ~cRZCallbackSystemService() override;
    cRZCallbackSystemService(const cRZCallbackSystemService &) = delete;
    cRZCallbackSystemService &operator=(const cRZCallbackSystemService &) = delete;

    // cIGZUnknown (every face)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0xc68335: 1 and the iid -> this, cIGZSystemService -> +4
    uint32_t AddRef() override;                              // folded 0x145240 (the service part's)
    uint32_t Release() override;                             // folded 0x1a85b0 (the service part's)

    // cIGZCallbackSystemService
    cRZCallbackThread *GetCallbackThread() override; // folded 0x15e550

    cRZCallbackThread *mCallbackThread; // +0x20
};

static_assert(sizeof(cRZCallbackSystemService) == cRZCallbackSystemService::kSize, "cRZCallbackSystemService size");
static_assert(offsetof(cRZCallbackSystemService, mServiceId) == 0x10, "cRZCallbackSystemService layout: the service part at +4");
static_assert(offsetof(cRZCallbackSystemService, mRefCount) == 0xc, "cRZCallbackSystemService layout: cRZUnknownMT at +8");
static_assert(offsetof(cRZCallbackSystemService, mCallbackThread) == 0x20, "cRZCallbackSystemService layout");

// The constructor hook's entry point (docs/standalone-shape.md, step 6). The other slots' adapters are gone: only the game's
// vtables reach their RVAs (in effect through callers).
struct cRZCallbackSystemServiceAdapters {
    static cRZCallbackSystemService *__fastcall Construct(void *memory, void *edx); // 0xc68264
};
