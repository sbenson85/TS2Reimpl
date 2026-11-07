#include "hook/hooks.h"
#include "gz/GZCOM.h"

namespace {

// cGZCOM, cGZCOMLibrary (both in the standalone shape: every entry is an adapter forwarding to our member, for the
// game's object, which is built before the hooks with the game's vtables) and the two text helpers the library
// loader uses. Not hooked: the folded slots (cGZCOM slot 4 0x145520, the +4 face's accessors, the library's
// 0x145260 / 0x145770 / 0x1457d0 / 0x1458b0) and the adjustor thunks (0x145b36, 0x145b3e, 0x145c04, 0x145c0c,
// 0x145c24, 0x14510c): the game's thunks jump to the hooked primaries, and our vtables hold the compiler's own.
const HookSpec kHooks[] = {
    {"cGZCOM_Construct", 0x144e52, 32, 0xba58d0bf, reinterpret_cast<void *>(&cGZCOMAdapters::Construct)},
    {"cGZCOM_Destruct", 0x145075, 32, 0x5899fe2e, reinterpret_cast<void *>(&cGZCOMAdapters::Destruct)},
    {"cGZCOM_DeletingDestructor", 0x145114, 31, 0xe2440788, reinterpret_cast<void *>(&cGZCOMAdapters::DeletingDestructor)},
    {"cGZCOM_QueryInterface", 0x145b14, 32, 0xe1610499, reinterpret_cast<void *>(&cGZCOMAdapters::QueryInterface)},
    {"cGZCOM_Release", 0x145bfc, 8, 0x86f7e2ca, reinterpret_cast<void *>(&cGZCOMAdapters::Release)},
    {"cGZCOM_GetClassObject", 0x145702, 32, 0x9ac33853, reinterpret_cast<void *>(&cGZCOMAdapters::GetClassObject)},
    {"cGZCOM_AddLibrary", 0x1451a3, 32, 0xa4b0bc2f, reinterpret_cast<void *>(&cGZCOMAdapters::AddLibrary)},
    {"cGZCOM_FreeUnusedLibraries", 0x1455a6, 32, 0xc4e5a35c, reinterpret_cast<void *>(&cGZCOMAdapters::FreeUnusedLibraries)},
    {"cGZCOM_CreateGuid", 0x145325, 32, 0x4e8e304d, reinterpret_cast<void *>(&cGZCOMAdapters::CreateGuid)},
    {"cGZCOM_CreateGuids", 0x1453ab, 32, 0x6682ccaa, reinterpret_cast<void *>(&cGZCOMAdapters::CreateGuids)},
    {"cGZCOM_RealInit", 0x145b7e, 32, 0x6dc3894c, reinterpret_cast<void *>(&cGZCOMAdapters::RealInit)},
    {"cGZCOM_RealShutdown", 0x145be4, 24, 0x5bba3398, reinterpret_cast<void *>(&cGZCOMAdapters::RealShutdown)},
    {"cGZCOM_SetServiceRunning", 0x145c1c, 8, 0x7be490ed, reinterpret_cast<void *>(&cGZCOMAdapters::SetServiceRunning)},
    {"cGZCOM_EnumerateLibraries_Callback", 0x1454e7, 32, 0xf21563aa, reinterpret_cast<void *>(&cGZCOMAdapters::EnumerateLibraries)},
    {"cGZCOM_EnumerateLibraries_Array", 0x14548b, 32, 0xf089c084, reinterpret_cast<void *>(&cGZCOMAdapters::EnumerateLibrariesToArray)},
    {"cGZCOM_CanUnloadLibrary", 0x1452ff, 32, 0xbf01f2b7, reinterpret_cast<void *>(&cGZCOMAdapters::CanUnloadLibrary)},
    {"cGZCOM_UnloadLibraries", 0x145556, 32, 0xe9d6c73e, reinterpret_cast<void *>(&cGZCOMAdapters::UnloadLibraries)},
    {"cGZCOM_InitLibrary", 0x145c2c, 32, 0xe9a2210c, reinterpret_cast<void *>(&cGZCOMAdapters::InitLibrary)},
    {"cGZCOM_LoadAndGetClassObject", 0x1457d4, 32, 0xf9d49c88, reinterpret_cast<void *>(&cGZCOMAdapters::LoadAndGetClassObject)},
    {"cGZCOM_RegisterClassCallback", 0x145152, 32, 0x8a9039b7, reinterpret_cast<void *>(&cGZCOM::RegisterClassCallback)},
    {"cGZCOMLibrary_ConstructFromPath", 0x144eff, 32, 0xbd2feaea, reinterpret_cast<void *>(&cGZCOMLibraryAdapters::Construct)},
    {"cGZCOMLibrary_ConstructCopy", 0x144ec2, 32, 0x2584b485, reinterpret_cast<void *>(&cGZCOMLibraryAdapters::CopyConstruct)},
    {"cGZCOMLibrary_Destruct", 0x1450b1, 21, 0xdd1fbb73, reinterpret_cast<void *>(&cGZCOMLibraryAdapters::Destruct)},
    {"cGZCOMLibrary_DeletingDestructor", 0x145133, 31, 0x61d2d638, reinterpret_cast<void *>(&cGZCOMLibraryAdapters::DeletingDestructor)},
    {"cGZCOMLibrary_QueryInterface", 0x145b46, 32, 0x5559d624, reinterpret_cast<void *>(&cGZCOMLibraryAdapters::QueryInterface)},
    {"cGZCOMLibrary_Release", 0x145c14, 8, 0x2662ae89, reinterpret_cast<void *>(&cGZCOMLibraryAdapters::Release)},
    {"cGZCOMLibrary_GetPath", 0x14580d, 20, 0x313115dd, reinterpret_cast<void *>(&cGZCOMLibraryAdapters::GetPath)},
    {"cGZCOMLibrary_Unload", 0x145524, 32, 0xf5ec101f, reinterpret_cast<void *>(&cGZCOMLibraryAdapters::Unload)},
    {"cGZCOMLibrary_Initialize", 0x1458b4, 32, 0x9733da1a, reinterpret_cast<void *>(&cGZCOMLibraryAdapters::Initialize)},
    {"cGZCOMLibrary_Less", 0x1450c6, 32, 0x19f6cb93, reinterpret_cast<void *>(&cGZCOMLibraryAdapters::Less)},
    {"ConvertBytesToString", 0x1afc9d, 32, 0x37be0c89, reinterpret_cast<void *>(&ConvertBytesToString)},
    {"GetModulePath", 0x1b139f, 32, 0xcf8837c5, reinterpret_cast<void *>(&GetModulePath)},
    // cCTGFramework's constructor and destructors: the Framework table (gz::cCTGFrameworkAdapters, lane EX).
};
REGISTER_HOOKS(kHooks);

} // namespace
