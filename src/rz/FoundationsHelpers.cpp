// The speed-built foundations helpers (/O2 modules): 0x99e700, 0xca4ac0. Declarations: rz/FoundationsHelpers.h.
#include "rz/FoundationsHelpers.h"

#include "game/vtable.h"

#include <windows.h>

namespace {

// Release (slot 2) with every exception swallowed: the original's catch (...) is built with /EHa, so it catches SEH
// exceptions as well as C++ ones. __try needs a function free of C++ objects.
void ReleaseSwallowingExceptions(void *object)
{
    __try {
        VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

} // namespace

void ServiceRefHolder::Destroy()
{
    if (mObject) ReleaseSwallowingExceptions(mObject);
}

OptionEntryNode **__cdecl FindOptionEntry(OptionEntryNode **out, OptionEntryNode *first, OptionEntryNode *last,
                                          const ts::OptionEntry &value)
{
    for (; first != last; first = first->_Next) {
        if (first->_Myval == value) break;
    }
    *out = first;
    return out;
}
