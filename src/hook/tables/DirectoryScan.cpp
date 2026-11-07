// DirectoryScan (a plain class the game builds inline): every member hooked at its RVA.
#include "hook/hooks.h"
#include "rz/DirectoryScan.h"

#define SCAN_ADAPTER(name) reinterpret_cast<void *>(&DirectoryScanAdapters::name)

namespace {

const HookSpec kHooks[] = {
    {"DirectoryScan_Construct", 0x1c48da, 32, 0x4df386f5, SCAN_ADAPTER(Construct)},
    {"DirectoryScan_Destruct", 0x1c49eb, 32, 0xbb5dc0a5, SCAN_ADAPTER(Destruct)},
    {"DirectoryScan_Scan", 0x1c5896, 32, 0x06d863f5, SCAN_ADAPTER(Scan)},
    {"DirectoryScan_SetDirectory", 0x1c4a45, 32, 0xfa4ca19a, SCAN_ADAPTER(SetDirectory)},
    {"DirectoryScan_SetPattern", 0x1c63d4, 32, 0x16bfffd5, SCAN_ADAPTER(SetPattern)},
    {"DirectoryScan_EndDirectory", 0x1c539a, 32, 0x14cb1844, SCAN_ADAPTER(EndDirectory)},
    {"DirectoryScan_EntryAt", 0x1c5047, 32, 0x7843dc00, SCAN_ADAPTER(EntryAt)},
    {"DirectoryScan_HasEntry", 0x1c5000, 32, 0x4f8afe75, SCAN_ADAPTER(HasEntry)},
    {"DirectoryScan_EntryFullPath", 0x1c509f, 32, 0x6927bd45, SCAN_ADAPTER(FullPath)},
    {"DirectoryScan_ListMatching", 0x1c5db7, 32, 0x91c2e659, SCAN_ADAPTER(ListMatching)},
    {"DirectoryScan_ListFiles", 0x1c5bbd, 14, 0xc818121b, SCAN_ADAPTER(ListFiles)},
    {"DirectoryScan_ListFolders", 0x1c587f, 23, 0x68828dc0, SCAN_ADAPTER(ListFolders)},
    {"DirectoryScan_ListFilesBelow", 0x1c5bcb, 32, 0x17984fa3, SCAN_ADAPTER(ListFilesBelow)},
    {"FindNext", 0x1c5678, 32, 0x0596212e, reinterpret_cast<void *>(&dscan::FindNext)},
};
REGISTER_HOOKS(kHooks);

} // namespace
