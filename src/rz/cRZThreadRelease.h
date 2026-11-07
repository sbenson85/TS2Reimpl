#pragma once
#include "rz/cRZThread.h"

#include <cstdint>

// The cRZThread destructor chain (RVAs 1.18.0.187): the thread object drops its reference on the shared
// state block; the last reference destroys the state. cRZThread.h has the construction side.
namespace appdebugger {

// 0x1b9b47 (__thiscall): the state block's destructor body. Closes the thread handle at +4 when it is not
// null, destroys the lock (+0x34), resets the name's vtable (+0x10) and releases its text (the cRZString's
// destructor body). Does not free the block.
void __fastcall DestroyThreadState(cRZThreadState *self, void *edx);

// 0x1b9e7b (__thiscall): the state's Release. Under its own lock: when the count (+0x30) is above 1 it is
// decremented, the lock released and the new count returned; otherwise (1, or 0) the lock is released, the
// state destroyed (0x1b9b47) and freed (sized delete, 0x3c bytes), and 0 returned. The count is compared
// unsigned.
uint32_t __fastcall ReleaseThreadState(cRZThreadState *self, void *edx);

// 0x1b9b71 (__thiscall): the thread's destructor body. Sets the vtable (0xda3168) and releases the state
// (0x1b9e7b), returning that function's result.
uint32_t __fastcall DestroyThread(cRZThread *self, void *edx);

} // namespace appdebugger
