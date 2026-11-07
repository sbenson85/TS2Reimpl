// DynamicLibraryList (a plain class cTSAppFW::AddDynamicLibrariesHere builds on its stack): every member hooked at its
// RVA; the two section-walk callbacks (0xcb3f90, 0xcb3fd0) are reached only through ReadConfiguration.
#include "hook/hooks.h"
#include "ts/DynamicLibraryList.h"

#define LIBRARY_ADAPTER(name) reinterpret_cast<void *>(&DynamicLibraryListAdapters::name)

namespace {

const HookSpec kHooks[] = {
    {"DynamicLibraryList_Construct", 0xcb3640, 32, 0xb52c9330, LIBRARY_ADAPTER(Construct)},
    {"DynamicLibraryList_Destruct", 0xcb36f0, 32, 0xc1dc52a5, LIBRARY_ADAPTER(Destruct)},
    {"DynamicLibraryList_Add", 0xcb3740, 32, 0x837168b0, LIBRARY_ADAPTER(Add)},
    {"DynamicLibraryList_ReadConfiguration", 0xcb3870, 32, 0x458a1579, LIBRARY_ADAPTER(ReadConfiguration)},
    {"DynamicLibraryList_Report", 0xcb3a80, 32, 0xd20ed9b4, LIBRARY_ADAPTER(Report)},
    {"DynamicLibraryList_LoadAll", 0xcb3b70, 32, 0x0b697d85, LIBRARY_ADAPTER(LoadAll)},
    {"DynamicLibraryList_RequireVersion", 0xcb3e10, 32, 0xd7e92997, LIBRARY_ADAPTER(RequireVersion)},
};
REGISTER_HOOKS(kHooks);

} // namespace
