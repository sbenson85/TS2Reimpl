// rz::cRZCOMDllDirector and rz::cGZCOMDirector: constructors, destructor, the root director's getter
// (src/rz/RZCOMDllDirectorShape.h). Built for size, as the game's.
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"

#include "game/game.h"

#include <intrin.h>

#include <new>

// Library functions of the thread-safe statics protocol (our toolchain's copies, as the original code uses its own).
extern "C" void __cdecl _Init_thread_header(int *target);
extern "C" void __cdecl _Init_thread_footer(int *target);
extern "C" __declspec(thread) int _Init_thread_epoch; // our CRT's statics epoch, not the image's TLS block (docs/standalone-shape.md, "Owned statics")

namespace rz {

cRZCOMDllDirector::cRZCOMDllDirector() : mRefCount(0), mField0C(0), mCOM(nullptr), mFramework(nullptr)
{
    // 0x1adffa: the vtables, the two zeroed fields, the string at +0x10 (cRZString::Construct 0x144f5b), the two
    // pointers, the empty vector at +0x38 and the class map (0x1aa569: 8 buckets, max load 1.0), in member order.
}

cRZCOMDllDirector::~cRZCOMDllDirector()
{
    // 0x1ae048: the vtables, then the members in reverse order: the class map (its buckets, then its list), the
    // vector at +0x38, the string (its vtable stored, then freed).
}

cGZCOMDirector::cGZCOMDirector()
{
    // 0x144897. The original reads the field through the folded getter 0x1457d0, inline; the branch is always taken
    // (the base constructor clears the field).
    if (mCOM == nullptr) mCOM = static_cast<cIGZFrameWork *>(GetServiceObject())->GetCOMObject();
}

cRZCOMDllDirector *__fastcall cRZCOMDllDirectorAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cRZCOMDllDirector;
}
void __fastcall cRZCOMDllDirectorAdapters::Destruct(cIGZCOMDirector *face, void *)
{
    [[msvc::forceinline_calls]] static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::~cRZCOMDllDirector();
}
cRZCOMDllDirector *__fastcall cRZCOMDllDirectorAdapters::DeletingDestructor(cIGZCOMDirector *face, void *, uint32_t flags)
{
    auto *self = static_cast<cRZCOMDllDirector *>(face);
    [[msvc::forceinline_calls]] self->cRZCOMDllDirector::~cRZCOMDllDirector();
    if (flags & 1) ::operator delete(self, sizeof(cRZCOMDllDirector));
    return self;
}
cGZCOMDirector *__fastcall cGZCOMDirectorAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZCOMDirector;
}

} // namespace rz

// 0x14491b. The guard at 0x10c93f4 and the thread's epoch: the fast path is taken when the guard is not
// above the thread's epoch (our CRT's _Init_thread_epoch). The director is built in place: our class.
cGZCOMDirector *GetCOMDirector()
{
    void *object = &ts2_rva_10c9390;
    int *guard = &ts2_rva_10c93f4;
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            ::new (object) rz::cGZCOMDirector;
            // The original registers its destructor stub (0xd8e6e0); ours registers DestroyCOMDirector.
            gRegisterExitHandler(&DestroyCOMDirector);
            _Init_thread_footer(guard);
        }
    }
    return static_cast<cGZCOMDirector *>(object);
}

void DestroyCOMDirector()
{
    reinterpret_cast<rz::cGZCOMDirector *>(&ts2_rva_10c9390)->rz::cGZCOMDirector::~cGZCOMDirector();
}
