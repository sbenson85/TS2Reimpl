#include "app/AppServices.h"
#include "hook/hooks.h"

namespace {

// cGZFileSystem in the standalone shape (lane KU). The constructor (0xc75ccd; its only caller, cTSAppFW::
// AddApplicationServicesHere, is ours and builds through the adapter) builds our class, so every file system carries
// our vtables: the other slots, the destructors and the thunks are reached only through the game's vtables, which
// nothing stores any more (in effect through callers, trapped by tools/callers.py --write-traps). The five setters
// keep their adapters as hooks because their entries are byte matches (matchcheck ties the RVA through the adapter).
const HookSpec kHooks[] = {
    // cGZFileSystem
    {"cGZFileSystem_Construct", 0xc75ccd, 32, 0x02bbd9ce, reinterpret_cast<void *>(&cGZFileSystemAdapters::Construct)},
    {"cGZFileSystem_AllowRecursiveCDSearch", 0xc75eb6, 13, 0xc0f11421, reinterpret_cast<void *>(&cGZFileSystemAdapters::AllowRecursiveCDSearch)},
    {"cGZFileSystem_SetExpectedCDAppPath", 0xc76691, 11, 0xd56d35d5, reinterpret_cast<void *>(&cGZFileSystemAdapters::SetExpectedCDAppPath)},
    {"cGZFileSystem_SetExpectedCDName", 0xc7669c, 11, 0xef5bb43e, reinterpret_cast<void *>(&cGZFileSystemAdapters::SetExpectedCDName)},
    {"cGZFileSystem_SetPluginDirectoryName", 0xc766ea, 11, 0x9ab96740, reinterpret_cast<void *>(&cGZFileSystemAdapters::SetPluginDirectoryName)},
    {"cGZFileSystem_UseCDIniFile", 0xc766f5, 13, 0x66861f95, reinterpret_cast<void *>(&cGZFileSystemAdapters::UseCDIniFile)},
};
REGISTER_HOOKS(kHooks);

} // namespace
