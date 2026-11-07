// nGZEvent::cEventSystemService in the standalone shape (lane KD). Built for size, as the game's (0x1a84d5..0x1a85ab and
// 0x1aad98 are packed).
#include "gz/cEventSystemService.h"

#include "gz/cConnectionManager.h"
#include "game/game.h"
#include "game/original.h"

#include <cstdlib>
#include <new>

// The thread-safe-statics helpers of our CRT (vcruntime; the compiler declares _Init_thread_header, _footer and _abort
// itself), used on the game's guard variable (as in rz/cGZAllocatorService.size.cpp).
extern "C" __declspec(thread) int _Init_thread_epoch;

namespace nGZEvent {

namespace {

// The exit handler the original registers (0xd90644): the static connection manager's destructor.
void __cdecl DestroyConnectionManager()
{
    cConnectionManagerAdapters::Destruct(
        reinterpret_cast<cConnectionManager *>(ts2_rva_10cadd8), nullptr);
}

} // namespace

cEventSystemService *__cdecl cEventSystemService::Create()
{
    void *memory = ::operator new(kSize);
    return memory ? ::new (memory) cEventSystemService : nullptr;
}

bool cEventSystemService::QueryInterface(uint32_t riid, void **ppv)
{
    if (!ppv) return false;
    if (riid != kInterfaceId) return rz::cRZSystemService::QueryInterface(riid, ppv);
    *ppv = static_cast<cIEventSystemService *>(this);
    AddRef();
    return true;
}

uint32_t cEventSystemService::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cEventSystemService::Release() { return rz::cRZSystemService::Release(); }

cConnectionManager *cEventSystemService::GetConnectionManager()
{
    void *const instance = ts2_rva_10cadd8;
    int *const guard = &ts2_rva_10cae1c;
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            cConnectionManagerAdapters::Construct(instance, nullptr); // the connection manager's constructor
            atexit(&DestroyConnectionManager);
            _Init_thread_footer(guard);
        }
    }
    return static_cast<cConnectionManager *>(instance);
}

// ---- adapters --------------------------------------------------------------------------------------------------------

cEventSystemService *__fastcall cEventSystemServiceAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cEventSystemService;
}

} // namespace nGZEvent
