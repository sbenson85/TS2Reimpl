#include "rz/StaticGetters.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/Singleton_20ddfb.h"

#include <intrin.h>

#include <cstdlib>
#include <new>
#include <vector>

// Library functions of the thread-safe statics protocol (our toolchain's copies, as the original
// code uses its own). The guard is a plain int.
extern "C" void __cdecl _Init_thread_header(int *target);
extern "C" void __cdecl _Init_thread_footer(int *target);

int (*gRegisterExitHandler)(void (*)()) = &atexit;

namespace {
constexpr size_t kListNodeSize = 0x14;
}

HashNodeList *HashNodeList::Construct(uint32_t /*unused*/)
{
    // 0x1aa5b3: clear, allocate the 0x14-byte sentinel and link it to itself.
    mHead = nullptr;
    mSize = 0;
    auto **node = static_cast<void **>(::operator new(0x14));
    node[0] = node;
    node[1] = node;
    mHead = node;
    return this;
}

cRZCOMDllDirector *cRZCOMDllDirector::Construct()
{
    // 0x1adffa: the cRZCOMDllDirector vtables, the two zeroed fields, the string at +0x10, the
    // framework pointer, the empty vector and the id set: the class map's own constructor (library, 0x1aa569).
    mVTable = GameAddress<void>(0xda265c);
    mVTableAt4 = GameAddress<void>(0xda26a4);
    mField08 = 0;
    mField0C = 0;
    mString.Construct();
    mFramework = nullptr;
    mField34 = 0;
    mVec38First = nullptr;
    mVec38Last = nullptr;
    mVec38End = nullptr;
    ::new (static_cast<void *>(&mIds)) comdir::ClassMap();
    return this;
}

void *GetDirectorFrameworkObject()
{
    // 0x1ae18d: the original calls 0x14491b, then tail-jumps to slot 12 with the object in ecx.
    cGZCOMDirector *object = GetCOMDirector();
    return VirtualCall<void *(__thiscall *)(void *), 12>(object);
}

void FreeListNodes(void * /*list*/, void *head)
{
    // 0x1adf40. The sentinel's predecessor link is cleared first (its next pointer, so the walk
    // below stops at the sentinel); the nodes after the sentinel are then freed in order.
    auto **node = static_cast<void **>(head);
    auto **prev = static_cast<void **>(node[1]);
    prev[0] = nullptr;
    void *next = node[0];
    while (next) {
        void *after = *static_cast<void **>(next);
        ::operator delete(next, kListNodeSize);
        next = after;
    }
}

void HashNodeList::Destruct()
{
    // 0x1ae301: the chain, then the sentinel itself (the head pointer is left as it was).
    FreeListNodes(this, mHead);
    ::operator delete(mHead, kListNodeSize);
}

void cRZCOMDllDirector::Destruct()
{
    // 0x1ae048. The base vtables first, then the members in the original's order. The vectors are
    // destroyed through the toolchain's std::vector (the original's _Tidy bodies, 0x14416d and
    // 0x1441c5), and the string through std::string's destructor (0x141455).
    mVTable = GameAddress<void>(0xda265c);
    mVTableAt4 = GameAddress<void>(0xda26a4);
    reinterpret_cast<std::vector<void *> *>(&mIds.mFirst)->~vector();
    mIds.mList.Destruct();
    reinterpret_cast<std::vector<void *> *>(&mVec38First)->~vector();
    mString.mVTable = GameAddress<void>(cRZString::kVTable);
    mString.mString.~basic_string();
}

// GetCOMDirector (0x14491b) and DestroyCOMDirector are in RZCOMDllDirector.size.cpp (lane EO: the root director is
// rz::cGZCOMDirector, built in place).
