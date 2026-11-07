#include "rz/RZSystemServiceBase.h"

#include "rz/cRZRefCount.h" // RZAtomicAdd (0x1bdbad, ours)

#include <new>

// rz::cRZUnknownMT and rz::cRZSystemService in the standalone shape (lane EI). The game built these with /O1 (size); the
// cRZSystemService QueryInterface (speed) is in RZSystemServiceBase.cpp.
namespace rz {

// ---- cRZUnknownMT (vtable 0xda2fec)

// Folded 0x1b945c (shared with cRZUnknown): id 1 only, counted through slot 1.
bool cRZUnknownMT::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1) return false;
    *out = static_cast<cIGZUnknown *>(this);
    AddRef();
    return true;
}

// 0x1b914d (shared with cRZRefCountMT): the interlocked add, the new count.
uint32_t cRZUnknownMT::AddRef()
{
    return static_cast<uint32_t>(RZAtomicAdd(reinterpret_cast<volatile int32_t *>(&mRefCount), 1));
}

// 0x1b9517: the count is read without synchronisation to choose between decrementing and deleting (slot 3 with 1).
uint32_t cRZUnknownMT::Release()
{
    if (mRefCount > 1) return static_cast<uint32_t>(RZAtomicAdd(reinterpret_cast<volatile int32_t *>(&mRefCount), -1));
    delete this;
    return 0;
}

// 0x1b90ab: nothing but the vtable store (the compiler's); the scalar deleting destructor is 0x1b912e.
cRZUnknownMT::~cRZUnknownMT() = default;

// 0x1b9544: decrement unless already 0; the count read afterwards.
uint32_t cRZUnknownMT::RemoveRef()
{
    if (mRefCount > 0) RZAtomicAdd(reinterpret_cast<volatile int32_t *>(&mRefCount), -1);
    return mRefCount;
}

// Folded 0x147a30: the count.
uint32_t cRZUnknownMT::RefCount()
{
    return mRefCount;
}

// ---- cRZSystemService (vtables 0xda3058 at +0, 0xda3090 at +4)

// 0x1b9584: the tick priority starts as the priority.
cRZSystemService::cRZSystemService(uint32_t id, int32_t priority)
    : mServiceId(id), mPriority(priority), mTickPriority(priority), mRunning(false)
{
}

// 0x1b95bf
cRZSystemService::cRZSystemService(uint32_t id, int32_t priority, int32_t tickPriority)
    : mServiceId(id), mPriority(priority), mTickPriority(tickPriority), mRunning(false)
{
}

// 0x1b95fe (the vtable stores, then cRZUnknownMT's 0x1b90ab); deleting 0x1b961a, reached through the +4 thunk 0x1b9612.
cRZSystemService::~cRZSystemService() = default;

// Folded 0x1b9640 (add ecx, 4; jmp 0x1b914d); the +4 face's thunk is the folded 0x1b9648.
uint32_t cRZSystemService::AddRef()
{
    return cRZUnknownMT::AddRef();
}

// 0x1b967a (add ecx, 4; jmp 0x1b9517); the +4 face's thunk 0x1b9682.
uint32_t cRZSystemService::Release()
{
    return cRZUnknownMT::Release();
}

uint32_t cRZSystemService::GetServiceID() { return mServiceId; }              // folded 0x145770
void cRZSystemService::SetServiceID(uint32_t id) { mServiceId = id; }         // folded 0x149c60
int32_t cRZSystemService::GetServicePriority() { return mPriority; }          // folded 0x149cf0
bool cRZSystemService::IsServiceRunning() { return mRunning; }                // folded 0x156110
void cRZSystemService::SetServiceRunning(bool running) { mRunning = running; } // folded 0x1b9690
bool cRZSystemService::Init() { return true; }                                // folded 0x144910
bool cRZSystemService::Shutdown() { return true; }                            // folded 0x144910
bool cRZSystemService::OnTick(uint32_t) { return true; }                      // folded 0x148260
bool cRZSystemService::OnIdle(uint32_t) { return true; }                      // folded 0x148260
int32_t cRZSystemService::GetServiceTickPriority() { return mTickPriority; }  // folded 0x149d00

// ---- adapters

cRZUnknownMT *__fastcall cRZUnknownMTAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cRZUnknownMT;
}
void __fastcall cRZUnknownMTAdapters::Destruct(cRZUnknownMT *self, void *)
{
    [[msvc::forceinline_calls]] self->cRZUnknownMT::~cRZUnknownMT();
}
cRZUnknownMT *__fastcall cRZUnknownMTAdapters::DeletingDestructor(cRZUnknownMT *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->cRZUnknownMT::~cRZUnknownMT();
    if (flags & 1) ::operator delete(self, sizeof(cRZUnknownMT));
    return self;
}
uint32_t __fastcall cRZUnknownMTAdapters::Release(cRZUnknownMT *self, void *)
{
    [[msvc::forceinline_calls]] return self->cRZUnknownMT::Release();
}
uint32_t __fastcall cRZUnknownMTAdapters::RemoveRef(cRZUnknownMT *self, void *)
{
    [[msvc::forceinline_calls]] return self->cRZUnknownMT::RemoveRef();
}

cRZSystemService *__fastcall cRZSystemServiceAdapters::Construct(void *memory, void *, uint32_t id, int32_t priority)
{
    return ::new (memory) cRZSystemService(id, priority);
}
cRZSystemService *__fastcall cRZSystemServiceAdapters::ConstructWithTickPriority(void *memory, void *, uint32_t id,
                                                                                 int32_t priority, int32_t tickPriority)
{
    return ::new (memory) cRZSystemService(id, priority, tickPriority);
}
void __fastcall cRZSystemServiceAdapters::Destruct(cRZSystemService *self, void *)
{
    [[msvc::forceinline_calls]] self->cRZSystemService::~cRZSystemService();
}
cRZSystemService *__fastcall cRZSystemServiceAdapters::DeletingDestructor(cRZSystemService *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->cRZSystemService::~cRZSystemService();
    if (flags & 1) ::operator delete(self, sizeof(cRZSystemService));
    return self;
}

} // namespace rz
