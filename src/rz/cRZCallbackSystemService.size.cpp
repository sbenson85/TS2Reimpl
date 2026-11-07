#include "rz/cRZCallbackSystemService.h"

#include "game/vtable.h"
#include "rz/cRZThread.h"

#include <new>

cRZCallbackSystemService::cRZCallbackSystemService() : rz::cRZSystemService(kServiceId, kPriority), mCallbackThread(nullptr)
{
    auto *memory = static_cast<cRZCallbackThread *>(::operator new(cRZCallbackThread::kSize));
    cRZCallbackThread *thread = memory ? memory->Construct() : nullptr;
    mCallbackThread = thread;
    thread->mOwnedByCaller = 1; // the service owns the thread's reference (the original's 0x4ea470, no null test)
    VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotAddRef>(thread);
}

cRZCallbackSystemService::~cRZCallbackSystemService()
{
    cRZCallbackThread *thread = mCallbackThread;
    // The ownership byte (read as 0x4e9cc0 does, before any null test) says how the thread goes: set, it is released (slot
    // 2); clear, a non-null thread is deleted through slot 8 with flags 1.
    if (thread->mOwnedByCaller) {
        VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotRelease>(thread);
    } else if (thread) {
        VirtualCall<void *(__thiscall *)(void *, uint32_t), kThreadSlotDelete>(thread, 1);
    }
    mCallbackThread = nullptr;
}

bool cRZCallbackSystemService::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid == 1 || riid == kInterfaceId) {
        *ppv = static_cast<cIGZCallbackSystemService *>(this);
    } else if (riid == kSystemServiceId) {
        *ppv = static_cast<cIGZSystemService *>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

uint32_t cRZCallbackSystemService::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cRZCallbackSystemService::Release() { return rz::cRZSystemService::Release(); }

cRZCallbackThread *cRZCallbackSystemService::GetCallbackThread() { return mCallbackThread; }

// ---- adapters --------------------------------------------------------------------------------------------------------

cRZCallbackSystemService *__fastcall cRZCallbackSystemServiceAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cRZCallbackSystemService;
}
