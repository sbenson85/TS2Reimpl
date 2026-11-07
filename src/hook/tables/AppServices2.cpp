#include "app/AppServices2.h"
#include "hook/hooks.h"

namespace {

// Group AppServices2: helpers of cTSGlobals and cGZFileSystem. The 0x1bf795 / 0x1bf84e pair has the
// same first 32 bytes (the same CRC); each site is checked on its own.
const HookSpec kHooks[] = {
    {"cTSGlobals_TestingCheatsEnabled", 0xc2cba3, 32, 0x0c10fa13, reinterpret_cast<void *>(&as2::TestingCheatsEnabled)},
    {"cTSGlobals_SetTestingCheatsEnabled", 0xc2c9f6, 32, 0xefb43099, reinterpret_cast<void *>(&as2::SetTestingCheatsEnabled)},
    {"AllocateViaAllocatorService", 0x1bf795, 32, 0xb32febb4, reinterpret_cast<void *>(&as2::AllocateViaAllocatorService)},
    {"FreeViaAllocatorService", 0x1bf84e, 32, 0xb32febb4, reinterpret_cast<void *>(&as2::FreeViaAllocatorService)},
    {"AssignServiceString", 0x1be72c, 32, 0xab90a641, reinterpret_cast<void *>(&as2::AssignServiceString)},
    {"TidyServiceString", 0x1bf753, 32, 0x5e42c7a2, reinterpret_cast<void *>(&as2::TidyString)},
    {"AcquireTestingService", 0x1cbace, 32, 0x4ad613be, reinterpret_cast<void *>(&as2::AcquireTestingService)},
    {"ConvertStringFrom", 0x1b0356, 32, 0x447551df, reinterpret_cast<void *>(&as2::ConvertStringFrom)},
    {"StringToUtf16", 0x1c53c2, 32, 0xe9971c6a, reinterpret_cast<void *>(&as2::StringToUtf16)},
    {"FindFirst", 0x1c54dd, 32, 0xb5c68aac, reinterpret_cast<void *>(&as2::FindFirst)},
    {"DestructFindNode", 0x1c49cb, 32, 0x56fc0ed0, reinterpret_cast<void *>(&as2::DestructFindNode)},
    {"CloseFind", 0x1c54b2, 32, 0x66b22143, reinterpret_cast<void *>(&as2::CloseFind)},
    {"PatternMatches", 0x1c4f79, 27, 0x1cd72091, reinterpret_cast<void *>(&as2::PatternMatches)},
    {"DirectoryExists", 0x1c4f94, 32, 0xf269315a, reinterpret_cast<void *>(&as2::DirectoryExists)},
    {"CopyModulePath", 0x1b1454, 32, 0xb264d7e6, reinterpret_cast<void *>(&as2::CopyModulePath)},
};
REGISTER_HOOKS(kHooks);

} // namespace
