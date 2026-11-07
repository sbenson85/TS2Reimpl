#include "app/AppBase.h"
#include "app/WinMain.h"
#include "hook/hooks.h"

namespace {

// 0x1446fc (15 bytes) and 0x14470b (33 bytes) are two functions Ghidra folded into the record of
// 0x1446fc; the records of hookspec.py don't list 0x14470b, so both entries are written by hand with
// the CRC (tools/hookcrc.py) over the function's own bytes: 15 for the first, so that the patch of
// the second (which starts 15 bytes later) does not fall inside its check window.
const HookSpec kHooks[] = {
    {"WinMain_GetModuleDirectoryW", 0xc27b04, 32, 0x9052d597, reinterpret_cast<void *>(&winmain::GetModuleDirectoryW)},
    {"WinMain_SteamInit", 0xc2a358, 22, 0x7ce20cd8, reinterpret_cast<void *>(&winmain::SteamInit)},
    {"WinMain_DisableImeTextFrameService", 0xc2a3a4, 32, 0x92920a34, reinterpret_cast<void *>(&winmain::DisableImeTextFrameService)},
    {"WinMain_SetGlobalApp", 0x1446fc, 15, 0xd547cf1f, reinterpret_cast<void *>(&winmain::SetGlobalApp)},
    {"WinMain_SetGlobalFramework", 0x14470b, 32, 0xa4f33177, reinterpret_cast<void *>(&winmain::SetGlobalFramework)},
    {"WinMain_ExceptionHandlingEnabled", 0x1bbb09, 7, 0x78e14fd1, reinterpret_cast<void *>(&winmain::ExceptionHandlingEnabled)},
    {"WinMain_AcquireWindowOwner", 0xc2ccb6, 32, 0x66c0be6a, reinterpret_cast<void *>(&winmain::AcquireWindowOwner)},
    {"WinMain_ShowFatalMessage", 0xc2da5a, 32, 0x3640ec21, reinterpret_cast<void *>(&winmain::ShowFatalMessage)},
    {"WinMain_AskYesNo", 0xc2dbf1, 32, 0xde9cad19, reinterpret_cast<void *>(&winmain::AskYesNo)},
    {"WinMain_RunFramework", 0xc2d0a8, 32, 0xe72c9a2d, reinterpret_cast<void *>(&winmain::RunFramework)},
    {"WinMain_RunFrameworkGuarded", 0xc2d121, 32, 0x2b43657c, reinterpret_cast<void *>(&winmain::RunFrameworkGuarded)},
    {"WinMain_RunApp", 0xc2d23b, 32, 0x8ff0ef5b, reinterpret_cast<void *>(&winmain::RunApp)},
    {"StringEqualsCString", 0x1c3cb0, 32, 0xa3590a78, reinterpret_cast<void *>(&appbase::StringEqualsCString)},
    {"IdSet_Construct", 0x1c02fb, 12, 0xf8f0749b, reinterpret_cast<void *>(&appbase::ConstructIdSet)},
    {"IdSet_Insert", 0x1c04a4, 25, 0x99e1dd8e, reinterpret_cast<void *>(&appbase::InsertId)},
    {"ExceptionReporter_SetEnabled", 0x1bd3d8, 32, 0xaabdb1f4, MemberAddress(&appbase::cExceptionReporter::SetEnabled)},
    {"ExceptionReporter_SetNames", 0x1bd467, 32, 0x11023e2c, MemberAddress(&appbase::cExceptionReporter::SetNames)},
    {"ExceptionReporter_SetFlag19a", 0x1bd48e, 13, 0xad358ffd, MemberAddress(&appbase::cExceptionReporter::SetFlag19a)},
    {"ExceptionReporter_SetLevel", 0x1baff8, 18, 0x517f2b82, MemberAddress(&appbase::cExceptionReporter::SetLevel)},
    {"DebuggerPresentCached", 0x1bd019, 32, 0xa766e8bb, reinterpret_cast<void *>(&appbase::DebuggerPresentCached)},
};
REGISTER_HOOKS(kHooks);

} // namespace
