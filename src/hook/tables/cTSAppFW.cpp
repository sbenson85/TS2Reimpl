#include "hook/hooks.h"
#include "ts/cTSAppFW.h"
#include "ts/cTSAppFWHelpers.h"

namespace {

// ts::cTSAppFW in the standalone shape (lane FG): every entry is an adapter (docs/standalone-shape.md). Moved here from
// AppCtor (constructor, destructor body, deleting destructor), TSApp (QueryInterface, InitOptions) and AppCOMDirectors
// (AddCOMDirectorsHere); new: AddApplicationServicesHere, PreTranslateMessage, DoMessage. The game's one creation site,
// WinMain's static (0xc2b87c), calls 0xc26147 after the host installed the hooks, so the live application is built by our
// constructor and carries our vtables; the other entries serve direct calls to the RVAs. With this table off, the game's
// constructor builds the object with the game's vtables (its bases still reach the cGZApp / cTSAppCommonImpl adapters).
// Not hooked: the 5-byte AddRef / Release jumps 0xc274b6 / 0xc29892, the compiler's thunks and SetAppName 0xc299da (an
// 8-byte thunk). Lane FM added bodies for Shutdown, OnTick, PreFrameworkInit and SetupUserDataDirectories (A2b), lane FW
// AddDynamicLibrariesHere and Init (A2c), hooked like the rest: no slot is forwarded to the game's body any more.
using A = ts::cTSAppFWAdapters;
const HookSpec kHooks[] = {
    {"cTSAppFW_Construct", 0xc26147, 32, 0x526862f4, reinterpret_cast<void *>(&A::Construct)},
    {"cTSAppFW_Destruct", 0xc266b9, 32, 0x40c9f039, reinterpret_cast<void *>(&A::Destruct)},
    {"cTSAppFW_DeletingDestructor", 0xc267d9, 32, 0xb0e84ebc, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cTSAppFW_QueryInterface", 0xc297fa, 32, 0x48a4be5a, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSAppFW_vf68_13", 0xc2a7bb, 32, 0xe1ecd3f4, reinterpret_cast<void *>(&A::InitOptions)},
    {"cTSAppFW_AddCOMDirectorsHere", 0xc26bf5, 32, 0x5e38ed40, reinterpret_cast<void *>(&A::AddCOMDirectorsHere)},
    {"cTSAppFW_AddApplicationServicesHere", 0xc267fb, 32, 0xb0861ee1, reinterpret_cast<void *>(&A::AddApplicationServicesHere)},
    {"cTSAppFW_PreTranslateMessage", 0xc296b4, 32, 0x6a8c521a, reinterpret_cast<void *>(&A::PreTranslateMessage)},
    {"cTSAppFW_DoMessage", 0xc27605, 32, 0xfd50e9fe, reinterpret_cast<void *>(&A::DoMessage)},
    {"cTSAppFW_Shutdown", 0xc2a1e3, 32, 0x335acadb, reinterpret_cast<void *>(&A::Shutdown)},
    {"cTSAppFW_OnTick", 0xc28fcc, 32, 0x1c97e6c6, reinterpret_cast<void *>(&A::OnTick)},
    {"cTSAppFW_PreFrameworkInit", 0xc290d8, 32, 0x6f7726f1, reinterpret_cast<void *>(&A::PreFrameworkInit)},
    {"cTSAppFW_SetupUserDataDirectories", 0xc2a7de, 32, 0xaaf41335, reinterpret_cast<void *>(&A::SetupUserDataDirectories)},
    {"cTSAppFW_AddDynamicLibrariesHere", 0xc26c30, 32, 0xc1d52433, reinterpret_cast<void *>(&A::AddDynamicLibrariesHere)},
    {"cTSAppFW_Init", 0xc27c3e, 32, 0xd8b4f164, reinterpret_cast<void *>(&A::Init)},
    {"cTSAppFW_RegisteredProductName", 0xc27a16, 32, 0xf65c3d5f, MemberAddress<&ts::cTSAppFW::RegisteredProductName>()},
    // Lane MB: the helpers. Every caller in the image is a function of ours (hooked), whose code calls ours directly;
    // the hooks keep the originals unreachable all the same.
    {"cTSAppFW_LaunchExceptionUploader", 0xc28b7d, 32, 0x6c9878cb, MemberAddress<&ts::cTSAppFW::LaunchExceptionUploader>()},
    {"cTSAppFW_ReportFatal", 0xc28de0, 32, 0xcd254856, MemberAddress<&ts::cTSAppFW::ReportFatal>()},
    {"cTSAppFW_AddCrashData", 0xc28e32, 32, 0x57d79d5c, MemberAddress<&ts::cTSAppFW::AddCrashData>()},
    {"cTSAppFW_UnlockNeighborhoodFiles", 0xc2a3d6, 32, 0x5eaa7349, MemberAddress<&ts::cTSAppFW::UnlockNeighborhoodFiles>()},
    {"AppFW_AppendEditionSuffix", 0xc27961, 32, 0xce3ce661, reinterpret_cast<void *>(&appfw::AppendEditionSuffix)},
    {"AppFW_BodyShopProductName", 0xb417c0, 32, 0x860e1645, reinterpret_cast<void *>(&appfw::BodyShopProductName)},
    {"AppFW_ProcessPagefileUsage", 0xc27b49, 32, 0xaadaae62, reinterpret_cast<void *>(&appfw::ProcessPagefileUsage)},
    {"AppFW_SpawnAndWait", 0x1b1a3f, 19, 0xb440bf26, reinterpret_cast<void *>(&appfw::SpawnAndWait)},
    {"AppFW_IsUserAdministrator", 0xc2896d, 32, 0xecf8d0df, reinterpret_cast<void *>(&appfw::IsUserAdministrator)},
};
REGISTER_HOOKS(kHooks);

} // namespace
