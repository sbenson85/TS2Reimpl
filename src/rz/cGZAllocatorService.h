#pragma once
#include "game/owned.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"

#include <cstddef>
#include <cstdint>

// The memory allocator service and its system-service base (1.18.0.187).
//
// cRZSystemService (0x1c bytes): +0 cIGZSystemService vtable, +4 cRZUnknownMT base (vtable +4,
// count +8), +0xc service id, +0x10 and +0x14 priority (both set from the constructor's argument),
// +0x18 a byte cleared by the constructor.
//
// cGZAllocatorService (0x20 bytes): +0 cIGZAllocatorService vtable, then a cRZSystemService at +4.
// Service id 0x3ae4bea3, interface id 0x3ae4beab, priority 0x895440. The game normally gets it from
// the framework; GetAllocatorService falls back to a function-local static instance.

class cRZSystemService {
public:
    static constexpr uint32_t kVTable = 0xda3058;     // cIGZSystemService
    static constexpr uint32_t kBaseVTable = 0xda3090; // its cRZUnknownMT base
    static constexpr uint32_t kTempVTable = 0xd9bc58; // set first, while the base is built

    cRZSystemService *Construct(uint32_t serviceId, uint32_t priority); // 0x1b9584
    void Destruct();                                                     // 0x1b95fe

    const void *mVTable;          // +0x00
    const void *mBaseVTable;      // +0x04 cRZUnknownMT
    volatile int32_t mRefCount;   // +0x08
    uint32_t mServiceId;          // +0x0c
    uint32_t mPriority;           // +0x10
    uint32_t mPriority2;          // +0x14
    uint8_t mByte18;              // +0x18
    uint8_t pad19[3];
};
static_assert(sizeof(cRZSystemService) == 0x1c, "cRZSystemService layout");

// The allocator interface (game vtable 0xda3cb8, the one the constructor stores first; RTTI cIGZAllocatorService, id
// 0x3ae4beab). The game's slot bodies are shared with cGZDefaultAllocator (0x1be4c7, 0x1be4e8) or its own (0x1be654):
// each forwards to the global allocator (src/rz/RZMemory.h); `this` is never read.
class cIGZAllocatorService : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x3ae4beab;

    virtual void *Allocate(uint32_t bytes) = 0;                // slot 3 (ret 4): RZMemory::Malloc
    virtual void Free(void *mem) = 0;                          // slot 4 (ret 4): RZMemory::Free (null passed on)
    virtual void *Reallocate(void *mem, uint32_t bytes) = 0;   // slot 5 (ret 8): RZMemory::Realloc
};

// The service in the standalone shape (docs/standalone-shape.md): the compiler emits the three vtables and the adjustor
// thunks; the game's constructor 0x1be59c is hooked to cGZAllocatorServiceAdapters::Construct, and our callers
// (GetAllocatorService, cTSAppFW::AddApplicationServicesHere) build the class directly.
class cGZAllocatorService : public cIGZAllocatorService, public rz::cRZSystemService {
public:
    static constexpr uint32_t kVTable = 0xda3cd4;          // the game's, for tests
    static constexpr uint32_t kSystemServiceVTable = 0xda3cf0;
    static constexpr uint32_t kUnknownVTable = 0xda3d28;
    static constexpr uint32_t kServiceId = 0x3ae4bea3;
    static constexpr uint32_t kInterfaceId = cIGZAllocatorService::kIid;
    static constexpr int32_t kPriority = 0x895440;
    static constexpr uint32_t kSize = 0x20;

    cGZAllocatorService() : rz::cRZSystemService(kServiceId, kPriority) {} // 0x1be59c
    // 0x1be610 (nothing of its own); deleting 0x1be635 (+8 slot 3, thunk 0x1be62d).

    // cIGZUnknown (all three faces; the +4 / +8 ones through the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0x1be661 (thunks 0x1be683, 0x1be68b)
    uint32_t AddRef() override;                              // folded 0x145240 (the service part's)
    uint32_t Release() override;                             // folded 0x1a85b0

    // cIGZAllocatorService
    void *Allocate(uint32_t bytes) override;                 // shared 0x1be4c7
    void Free(void *mem) override;                           // 0x1be654
    void *Reallocate(void *mem, uint32_t bytes) override;    // 0x1be4e8 (db: library by shape)
};
static_assert(sizeof(cGZAllocatorService) == cGZAllocatorService::kSize, "cGZAllocatorService layout");
static_assert(offsetof(cGZAllocatorService, mServiceId) == 0x10, "cGZAllocatorService layout: the service part at +4");

// The constructor hook's entry point (docs/standalone-shape.md): `this` in ecx, the dummy edx.
struct cGZAllocatorServiceAdapters {
    static cGZAllocatorService *__fastcall Construct(void *memory, void *edx); // 0x1be59c
};

// A local holder that asks the framework for the allocator service (0x1be577) and releases what it
// got (0x1be5d1).
struct AllocatorServiceHolder {
    void *mService;
    AllocatorServiceHolder *Acquire(); // 0x1be577: framework slot 5 (0x3ae4bea3, 0x3ae4beab, &mService)
    void Release();                    // 0x1be5d1: Release through slot 2 inside catch(...) built /EHa
};

// 0x1be693: the framework's allocator service if there is one, else the static instance (created on
// first use, AddRef'd once, released at exit). The holder's reference is dropped before returning.
void *__cdecl GetAllocatorService();

// 0x197100 (__stdcall, two arguments; the second is unused): frees `mem` through the allocator service
// (slot 4), which it caches in a function-local static (RVA 0x10c95d4, guard 0x10c95d8) from
// GetAllocatorService on first use.
void __stdcall FreeThroughAllocatorService(void *mem, uint32_t unused);

// The service's function-local statics, owned (docs/standalone-shape.md, "Owned statics"): the image's in the hooked
// game, standalone/owned_statics.cpp's in an image-free program. Our users name these, never the RVAs.
TS2_OWNED_STATIC(void *, 10cb5b4); // GetAllocatorService's static instance (a cIGZAllocatorService *)
TS2_OWNED_STATIC(int, 10cb5b8);    // its thread-safe-statics guard
TS2_OWNED_STATIC(void *, 10c95d4); // the service the allocation helpers cache (FreeThroughAllocatorService and others)
TS2_OWNED_STATIC(int, 10c95d8);    // its guard

// The game's allocator for STL containers whose memory comes from the allocator service (an empty
// object; `this` is never read).
struct GZServiceAllocatorBase {
    // 0x1d0903 (__thiscall, ret 8): deallocate(mem, count). The same body as 0x197100: frees `mem`
    // through slot 4 of the cached service; count is unused.
    void Deallocate(void *mem, uint32_t count);
};
