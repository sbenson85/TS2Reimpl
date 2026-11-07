#include "gz/cGZApp.h"
#include "hook/hooks.h"

namespace {

// gz::cGZApp in the standalone shape (lane EV): every entry is an adapter (docs/standalone-shape.md). Moved here from
// WinMain (the constructor), AppCtor (the destructor body) and TSApp (QueryInterface, AddApplicationService); new: the
// deleting destructor. The game's one creation site is cTSAppFW's constructor 0xc26147 (ours, hook-shaped: it builds
// the base with appbase::cGZApp::Construct and stores the game's vtables), so in game these entries serve the
// application object's game vtables. Framework 0xc59ff2 (a 5-byte jmp) and AsIGZSystemService 0xa28af0 (4 bytes) are
// not hooked; the thunks 0xc59fbf, 0xc5a01e, 0xc5a026, 0xc5a02e, 0xc5a036 are the compiler's in our vtables.
using A = gz::cGZAppAdapters;
const HookSpec kHooks[] = {
    {"cGZApp_Construct", 0xc59f5a, 32, 0x96ce97c0, reinterpret_cast<void *>(&A::Construct)},
    {"cGZApp_DestructBody", 0xc59f91, 32, 0x1d5603ff, reinterpret_cast<void *>(&A::Destruct)},
    {"cGZApp_DeletingDestructor", 0xc59fc7, 31, 0x4083ec62, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cGZApp_QueryInterface", 0xc59ff7, 32, 0xda8a7da1, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cGZApp_AddApplicationService", 0xc59fe6, 12, 0x13e8b8ee, reinterpret_cast<void *>(&A::AddApplicationService)},
};
REGISTER_HOOKS(kHooks);

} // namespace
