#include "hook/hooks.h"
#include "rz/cRZThread.h"

namespace {

// The thread classes (cGZRegistry's slots 3..7 and cGZClipboardManager are in their own tables since lane KD:
// src/hook/tables/cGZRegistry.cpp, cGZClipboardManager.cpp).
const HookSpec kHooks[] = {
    {"cRZThreadState_Construct", 0x1b9aec, 32, 0xa97a6c56, MemberAddress(&cRZThreadState::Construct)},
    {"cRZThreadState_AddRef", 0x1b9b9e, 32, 0xd272af71, MemberAddress(&cRZThreadState::AddRef)},
    {"cRZThread_Construct", 0x1b9b19, 32, 0x76364c40, MemberAddress(&cRZThread::Construct)},
    {"cRZThread_SetName", 0x1b9f30, 32, 0x8f3fa617, MemberAddress(&cRZThread::SetName)},
    {"cRZThread_AnnounceThreadName", 0x1b9e26, 32, 0x2de8cb14, reinterpret_cast<void *>(&cRZThread::AnnounceThreadName)},
    {"cRZCallbackThread_Construct", 0x9cfa5d, 32, 0x8133de29, MemberAddress(&cRZCallbackThread::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
