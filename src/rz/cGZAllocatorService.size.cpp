#include "rz/cGZAllocatorService.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZMemory.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZRefCount.h"

#include <windows.h>

#include <cstdlib>
#include <new>

// The thread-safe-statics helpers of our CRT (vcruntime; the compiler declares _Init_thread_header,
// _footer and _abort itself), used on the game's guard variable.
extern "C" __declspec(thread) int _Init_thread_epoch;

namespace {

using AddRefFn = uint32_t(__thiscall *)(void *);

// The exit handler the original registers (0xd90731): releases the static instance.
void __cdecl ReleaseStaticInstance()
{
    void *instance = ts2_rva_10cb5b4;
    if (instance) VirtualCall<AddRefFn, 2>(instance);
}

// Release through slot 2, swallowing any exception: the original's catch(...) is built /EHa, so it
// also catches SEH exceptions. (__except doesn't destroy a C++ exception object as catch(...) would;
// Release isn't expected to throw objects that own anything.)
void ReleaseCatchingAll(void *object)
{
    __try {
        VirtualCall<AddRefFn, 2>(object);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

} // namespace

cRZSystemService *cRZSystemService::Construct(uint32_t serviceId, uint32_t priority)
{
    mVTable = GameAddress(kTempVTable);
    mBaseVTable = GameAddress(cRZUnknownMT::kVTable); // cRZUnknownMT constructor (0x1b9078)
    mRefCount = 0;
    mServiceId = serviceId;
    mPriority = priority;
    mPriority2 = priority;
    mVTable = GameAddress(cRZSystemService::kVTable); // scoped: tools/callers.py resolves it to this class
    mBaseVTable = GameAddress(kBaseVTable);
    mByte18 = 0;
    return this;
}

void cRZSystemService::Destruct()
{
    mVTable = GameAddress(cRZSystemService::kVTable); // scoped: tools/callers.py resolves it to this class
    mBaseVTable = GameAddress(kBaseVTable);
    reinterpret_cast<cRZUnknownMT *>(&mBaseVTable)->Destruct();
}

bool cGZAllocatorService::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != kInterfaceId) return rz::cRZSystemService::QueryInterface(riid, ppv);
    // No null check (callers must pass ppv).
    *ppv = static_cast<cIGZAllocatorService *>(this);
    AddRef();
    return true;
}

uint32_t cGZAllocatorService::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZAllocatorService::Release() { return rz::cRZSystemService::Release(); }

void *cGZAllocatorService::Allocate(uint32_t bytes) { return RZMemory::Malloc(bytes); }
void cGZAllocatorService::Free(void *mem) { RZMemory::Free(mem); }
void *cGZAllocatorService::Reallocate(void *mem, uint32_t bytes) { return RZMemory::Realloc(mem, bytes); }

cGZAllocatorService *__fastcall cGZAllocatorServiceAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZAllocatorService;
}

AllocatorServiceHolder *AllocatorServiceHolder::Acquire()
{
    mService = nullptr;
    if (void *framework = GetServiceObject()) {
        using QueryFn = bool(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<QueryFn, 5>(framework, cGZAllocatorService::kServiceId, cGZAllocatorService::kInterfaceId, &mService);
    }
    return this;
}

void AllocatorServiceHolder::Release()
{
    if (mService) ReleaseCatchingAll(mService);
}

void *__cdecl GetAllocatorService()
{
    AllocatorServiceHolder holder;
    holder.Acquire();
    void *service = holder.mService;
    if (!service) {
        int *guard = &ts2_rva_10cb5b8;
        void **instance = &ts2_rva_10cb5b4;
        if (*guard > _Init_thread_epoch) {
            _Init_thread_header(guard);
            if (*guard == -1) {
                // The original's operator new (library, 0x824632) throws, so its null branch is dead.
                auto *created = new cGZAllocatorService;
                *instance = static_cast<cIGZAllocatorService *>(created);
                created->AddRef();
                atexit(&ReleaseStaticInstance);
                _Init_thread_footer(guard);
            }
        }
        service = *instance;
    }
    holder.Release();
    return service;
}

void __stdcall FreeThroughAllocatorService(void *mem, uint32_t /*unused*/)
{
    int *guard = &ts2_rva_10c95d8;
    void **service = &ts2_rva_10c95d4;
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            try {
                *service = GetAllocatorService();
            } catch (...) {
                _Init_thread_abort(guard);
                throw;
            }
            _Init_thread_footer(guard);
        }
    }
    using FreeFn = void(__thiscall *)(void *, void *);
    VirtualCall<FreeFn, 4>(*service, mem);
}

// The same body as 0x197100 (the original is a second copy of it, a __thiscall member).
void GZServiceAllocatorBase::Deallocate(void *mem, uint32_t count)
{
    FreeThroughAllocatorService(mem, count);
}
