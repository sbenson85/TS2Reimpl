#include "gz/Framework.h"
#include "gz/FrameworkShape.h"
#include "hook/hooks.h"

namespace {

// Not hooked: the folded one-liners (shared_vf_*: 0x142860, 0x142710, 0x142840, 0x142870, 0x142850,
// 0x1436cc, 0x142844, 0x1427a0, 0x142700, 0x142950, 0x140fe0), the thunks that adjust `this` and jump to
// a hooked function (0xc2d995.., 0xc2ceeb.., 0xc2d9ad..), and AbortiveQuit (0x142270): it ends in the
// CRT's exit(), and from the plugin that would be the plugin's own CRT, not the game's.
const HookSpec kHooks[] = {
    // gz::cGZFramework (lane ER): every base entry is an adapter. The live object is built before the hooks go in
    // (game vtables), so these are the route in game; our vtable serves only objects our constructor builds.
    {"cGZFramework_Construct", 0x141d76, 32, 0x06a24e6d, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::Construct)},
    {"cGZFramework_Destruct", 0x141f5c, 32, 0x7c6bfca0, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::Destruct)},
    {"cGZFramework_DeletingDestructor", 0x142207, 32, 0xf53287b0, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::DeletingDestructor)},
    {"cGZFramework_StdOut", 0x143887, 32, 0x1ada94b9, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::StdOut)},
    {"cGZFramework_StdErr", 0x1437e7, 32, 0xc5e7972b, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::StdErr)},
    {"cGZFramework_StdIn", 0x143837, 32, 0x0d9caf5d, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::StdIn)},
    {"cGZFramework_SetCommandLine", 0x143660, 32, 0x25a39247, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::SetCommandLine)},
    {"cGZFramework_CommitCommandLine", 0x1427a7, 14, 0x5d3e465c, reinterpret_cast<void *>(&gz::cGZFramework::CommitCommandLine)},
    {"cGZFramework_QueryInterface", 0x1431b6, 32, 0xd9e71f7e, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::QueryInterface)},
    {"cGZFramework_AddRef", 0x142321, 32, 0xdfe7eca2, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::AddRef)},
    {"cGZFramework_Release", 0x143244, 32, 0xe1d33cf6, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::Release)},
    {"cGZFramework_AddSystemService", 0x14234f, 32, 0xd19a89b1, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::AddSystemService)},
    {"cGZFramework_RemoveSystemService", 0x143431, 32, 0x68792fe6, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RemoveSystemService)},
    {"cGZFramework_GetSystemService", 0x1428e2, 32, 0xe07aa3ad, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::GetSystemService)},
    {"cGZFramework_EnumSystemServices", 0x14271c, 32, 0xda4d602c, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::EnumSystemServices)},
    {"cGZFramework_AddHook", 0x14229a, 32, 0xf6082f14, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::AddHook)},
    {"cGZFramework_RemoveHook", 0x1433cd, 32, 0x7b00d38a, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RemoveHook)},
    {"cGZFramework_AddToTick", 0x1424fe, 32, 0x14014f4f, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::AddToTick)},
    {"cGZFramework_RemoveFromTick", 0x143324, 32, 0x97467362, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RemoveFromTick)},
    {"cGZFramework_AddToOnIdle", 0x142474, 32, 0x1aabc77a, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::AddToOnIdle)},
    {"cGZFramework_RemoveFromOnIdle", 0x14327b, 32, 0x803d6721, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RemoveFromOnIdle)},
    {"cGZFramework_SetOnIdleInterval", 0x1436e6, 21, 0x112fff16, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::SetOnIdleInterval)},
    {"cGZFramework_OnTick", 0x142eca, 32, 0xafb46526, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::OnTick)},
    {"cCTGFramework_OnTick", 0xc2d427, 10, 0xed7e1547, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::OnTick)},
    {"cGZFramework_OnIdle", 0x142dbb, 32, 0xe0b0f0f5, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::OnIdle)},
    {"cGZFramework_IsTickEnabled", 0x142c23, 7, 0x4a6e3b6f, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::IsTickEnabled)},
    {"cGZFramework_ToggleTick", 0x1438d7, 13, 0x266c80d5, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::ToggleTick)},
    {"cGZFramework_Quit", 0x1431fc, 32, 0xccf9b763, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::Quit)},
    {"cGZFramework_IsInstall", 0x142c1c, 7, 0x2a8eaced, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::IsInstall)},
    {"cGZFramework_DefaultDebugStream", 0x142717, 5, 0x2912ebf0, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::DefaultDebugStream)},
    {"cGZFramework_SetDebugLevel", 0x143699, 32, 0x2e2efe9c, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::SetDebugLevel)},
    {"cGZFramework_GetStream", 0x142877, 32, 0xaf29e959, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::GetStream)},
    {"cGZFramework_SetStream", 0x1436fb, 32, 0x1c6f346e, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::SetStream)},
    {"cGZFramework_SetApplication", 0x14363e, 32, 0xb3c04da5, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::SetApplication)},
    {"cGZFramework_Application", 0x1426ec, 6, 0xc1fa7af6, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::Application)},
    {"cGZFramework_ReportException", 0x14358c, 32, 0xa923abc7, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::ReportException)},
    {"cGZFramework_GetMainHWND", 0x142857, 7, 0x7412146b, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::GetMainHWND)},
    {"cGZFramework_SetMainHWND", 0x1436d9, 13, 0x34479f71, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::SetMainHWND)},
    {"cGZFramework_CopyHooks", 0x142c2a, 32, 0x4fc8bfe6, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::CopyHooks)},
    {"cGZFramework_BuildServiceList", 0x142cb2, 32, 0xda68f4c6, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::BuildServiceList)},
    {"cGZFramework_RunPreFrameWorkInitHooks", 0x142b97, 32, 0x6c89282e, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RunPreFrameWorkInitHooks)},
    {"cGZFramework_RunPreAppInitHooks", 0x142ad7, 32, 0xcbbe134b, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RunPreAppInitHooks)},
    {"cGZFramework_RunPostAppInitHooks", 0x1429b7, 32, 0x0d220378, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RunPostAppInitHooks)},
    {"cGZFramework_RunPreAppShutdownHooks", 0x142b37, 32, 0xc5d122ff, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RunPreAppShutdownHooks)},
    {"cGZFramework_RunPostAppShutdownHooks", 0x142a17, 32, 0xb3011dea, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RunPostAppShutdownHooks)},
    {"cGZFramework_RunPostSystemServiceShutdownHooks", 0x142a77, 32, 0x62e6199a, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RunPostSystemServiceShutdownHooks)},
    {"cGZFramework_RunAbortiveQuitHooks", 0x142957, 32, 0x3ac00e09, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::RunAbortiveQuitHooks)},
    {"cGZFramework_ClearServiceContainers", 0x1427bb, 32, 0x435d67f0, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::ClearServiceContainers)},
    {"cGZFramework_ClearHooks", 0x142bf7, 32, 0xcc7472b8, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::ClearHooks)},
    {"cGZFramework_ShutdownCOMObject", 0x1437c5, 32, 0xf15559d6, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::ShutdownCOMObject)},
    {"cGZFramework_SetInstallFlag", 0x14368c, 13, 0xc5584f38, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::SetInstallFlag)},
    // The base's own slots 41..46 and 48 (lane EX): reached only on an object carrying the base's game vtable, which the
    // live object loses in its constructor; hooked so that every entry point of the class is ours.
    {"cGZFramework_InitHighPriorityServices", 0x14307f, 32, 0x247909cc, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::InitHighPriorityServices)},
    {"cGZFramework_InitAppBandServices", 0x1425c7, 32, 0x23a8cd9e, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::InitAppBandServices)},
    {"cGZFramework_InitLowPriorityServices", 0x142f87, 32, 0x6f29aee8, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::InitLowPriorityServices)},
    {"cGZFramework_ShutdownLowPriorityServices", 0x143133, 32, 0xc015c4ac, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::ShutdownLowPriorityServices)},
    {"cGZFramework_ShutdownAppBandServices", 0x142662, 32, 0x2de943c7, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::ShutdownAppBandServices)},
    {"cGZFramework_ShutdownHighPriorityServices", 0x142ffb, 32, 0x7897bd15, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::ShutdownHighPriorityServices)},
    {"cGZFramework_MainLoop", 0x1435c2, 32, 0x8237de53, reinterpret_cast<void *>(&gz::cGZFrameworkAdapters::MainLoop)},
    // gz::cCTGFramework (lane EX, startup F3): every entry an adapter. The live object (WinMain's static 0x10fdb50) is built
    // before the hooks go in and keeps the game's three vtables, so these are the route in game. Not hooked: the
    // cIGZUnknown forwards 0xc2d995 / 0xc2ceeb / 0xc2d9ad (5-byte jumps to the base's hooked bodies) and the six thunks
    // on the +4 and +0x180 faces; our vtables hold the compiler's own.
    // 0xc2ccdb is missing from db/functions.jsonl (its record stops after the _EH_prolog call, 10 bytes), so
    // tools/hookspec.py covers only 10 bytes of it; the CRC here is zlib's crc32 of the first 32 bytes of the unpacked exe.
    {"cCTGFramework_Construct", 0xc2ccdb, 32, 0xddfa7ae4, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::Construct)},
    {"cCTGFramework_Destruct", 0xc2ce90, 32, 0x19c9dd18, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::Destruct)},
    {"cCTGFramework_DeletingDestructor", 0xc2cec9, 32, 0xad05c7f5, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::DeletingDestructor)},
    {"cCTGFramework_InitHighPriorityServices", 0xc2d6e6, 10, 0x505b1255, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::InitHighPriorityServices)},
    {"cCTGFramework_InitAppBandServices", 0xc2cf03, 10, 0x714045de, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::InitAppBandServices)},
    {"cCTGFramework_InitLowPriorityServices", 0xc2d506, 10, 0xfc9f835a, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::InitLowPriorityServices)},
    {"cCTGFramework_ShutdownLowPriorityServices", 0xc2d8d2, 10, 0x9f030239, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::ShutdownLowPriorityServices)},
    {"cCTGFramework_ShutdownAppBandServices", 0xc2cfde, 10, 0x9ca55feb, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::ShutdownAppBandServices)},
    {"cCTGFramework_ShutdownHighPriorityServices", 0xc2d5bb, 10, 0xd4a68b30, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::ShutdownHighPriorityServices)},
    {"cCTGFramework_MainLoop", 0xc2d9c5, 32, 0x93ace149, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::MainLoop)},
    {"cCTGFramework_DoMessage", 0xc2d2ca, 10, 0xf7a9d434, reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::DoMessage)},
    // Ghidra has no function records for these three (they sit inside the span of Startup's record), so
    // the site CRCs are computed over 32 bytes the way the tool does.
    {"GZFrameWork_Run", 0x1446ba, 32, 0xadc3c05f, reinterpret_cast<void *>(&GZFrameWork_Run)},
    {"GZFrameWork_Shutdown", 0x14472b, 32, 0x149d511b, reinterpret_cast<void *>(&GZFrameWork_Shutdown)},
    {"ReleaseFrameworkGlobals", 0x143782, 32, 0x746d87f7, reinterpret_cast<void *>(&ReleaseFrameworkGlobals)},
};
REGISTER_HOOKS(kHooks);

} // namespace
