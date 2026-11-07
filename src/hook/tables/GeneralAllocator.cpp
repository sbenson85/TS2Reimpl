#include "hook/hooks.h"
#include "rz/GeneralAllocator.h"

namespace {

// InitialTop (0x1c6e40, 4 bytes, shared) and the 5-byte Shutdown thunk (0x1c66c9) are not hooked.
const HookSpec kHooks[] = {
    {"GeneralAllocator_Construct", 0x1c655d, 32, 0xf98ed835, MemberAddress(&GeneralAllocator::Construct)},
    {"GeneralAllocator_Malloc", 0x1c70cc, 32, 0x20550203, MemberAddress(&GeneralAllocator::Malloc)},
    {"GeneralAllocator_MallocInternal", 0x1c7104, 32, 0x0018f262, MemberAddress(&GeneralAllocator::MallocInternal)},
    {"GeneralAllocator_Free", 0x1c6c5a, 32, 0x5a5940cb, MemberAddress(&GeneralAllocator::Free)},
    {"GeneralAllocator_FreeInternal", 0x1c6cc1, 32, 0x494f6f04, MemberAddress(&GeneralAllocator::FreeInternal)},
    {"GeneralAllocator_Realloc", 0x1c7693, 32, 0xbd957143, MemberAddress(&GeneralAllocator::Realloc)},
    {"GeneralAllocator_ReallocInternal", 0x1c76cf, 32, 0xf94a7d0a, MemberAddress(&GeneralAllocator::ReallocInternal)},
    {"GeneralAllocator_Init", 0x1c6ece, 32, 0x149f74dd, MemberAddress(&GeneralAllocator::Init)},
    {"GeneralAllocator_Shutdown", 0x1c7b90, 32, 0x679a6956, MemberAddress(&GeneralAllocator::Shutdown)},
    {"GeneralAllocator_SetOption", 0x1c79b8, 32, 0x6c491e0c, MemberAddress(&GeneralAllocator::SetOption)},
    {"GeneralAllocator_TrimCore", 0x1c7cb7, 32, 0x00258dae, MemberAddress(&GeneralAllocator::TrimCore)},
    {"GeneralAllocator_AddCore", 0x1c66ce, 32, 0x3828b98f, MemberAddress(&GeneralAllocator::AddCore)},
    {"GeneralAllocator_CreateCore", 0x1c67ee, 32, 0x31c9f3d6, MemberAddress(&GeneralAllocator::CreateCore)},
    {"GeneralAllocator_ExtendCore", 0x1c6aa0, 32, 0xf9450913, MemberAddress(&GeneralAllocator::ExtendCore)},
    {"GeneralAllocator_MallocConsolidate", 0x1c6981, 32, 0xfd3dec6b, MemberAddress(&GeneralAllocator::MallocConsolidate)},
    {"GeneralAllocator_FindCore", 0x1c6c2b, 32, 0xe40cddac, MemberAddress(&GeneralAllocator::FindCore)},
    {"GeneralAllocator_FreeCore", 0x1c6c8a, 32, 0xecd08a26, MemberAddress(&GeneralAllocator::FreeCore)},
    {"GeneralAllocator_IsAtCoreEnd", 0x1c6e05, 32, 0x4388fdb2, MemberAddress(&GeneralAllocator::IsAtCoreEnd)},
    {"GeneralAllocator_HaveFastChunks", 0x1c6e39, 7, 0x6b817541, MemberAddress(&GeneralAllocator::HaveFastChunks)},
    {"GeneralAllocator_SetFreeChunk", 0x1c6954, 28, 0x21bb51c4, MemberAddress(&GeneralAllocator::SetFreeChunk)},
    {"GeneralAllocator_LinkCore", 0x1c7043, 29, 0x6144aadb, MemberAddress(&GeneralAllocator::LinkCore)},
    {"GeneralAllocator_UnlinkCore", 0x1c7e38, 25, 0x8aa9c296, MemberAddress(&GeneralAllocator::UnlinkCore)},
    {"GeneralAllocator_OsAlloc", 0x1c7060, 32, 0x725421a4, MemberAddress(&GeneralAllocator::OsAlloc)},
    {"GeneralAllocator_OsFree", 0x1c7097, 20, 0x0cec9c94, MemberAddress(&GeneralAllocator::OsFree)},
    {"GeneralAllocator_SetMaxFast", 0x1c794d, 32, 0x395e6290, MemberAddress(&GeneralAllocator::SetMaxFast)},
    {"GeneralAllocator_SetTop", 0x1c7977, 32, 0x07922708, MemberAddress(&GeneralAllocator::SetTop)},
    {"GeneralAllocator_LargeBinIndex", 0x1c6e44, 32, 0x459e94c9, reinterpret_cast<void *>(&GeneralAllocator::LargeBinIndex)},
    {"GeneralAllocator_SetFenceposts", 0x1c6916, 32, 0xfce61bee, reinterpret_cast<void *>(&GeneralAllocator::SetFenceposts)},
    {"GeneralAllocator_SetFencepostsAt", 0x1c792f, 30, 0x1e79910a, reinterpret_cast<void *>(&GeneralAllocator::SetFencepostsAt)},
    {"GeneralAllocator_InitCoreChunk", 0x1c70ab, 32, 0x7af6428f, reinterpret_cast<void *>(&GeneralAllocator::InitCoreChunk)},
    {"GeneralAllocator_LargeBlockChunk", 0x1c6eb2, 7, 0x0ee20d94, reinterpret_cast<void *>(&GeneralAllocator::LargeBlockChunk)},
    {"GeneralAllocator_PageSize", 0x1c6eb9, 21, 0x3ca9bbf1, reinterpret_cast<void *>(&GeneralAllocator::PageSize)},
    {"GeneralAllocator_CreateLock", 0x1c763d, 23, 0x63174abd, reinterpret_cast<void *>(&GeneralAllocator::CreateLock)},
    {"GeneralAllocator_DestroyLock", 0x1c7654, 11, 0x56b32b98, reinterpret_cast<void *>(&GeneralAllocator::DestroyLock)},
    {"GeneralAllocator_LockCount", 0x1c765f, 8, 0x783e41c6, reinterpret_cast<void *>(&GeneralAllocator::LockCount)},
    {"GeneralAllocator_Lock", 0x1c7667, 21, 0x1e8f5bd0, reinterpret_cast<void *>(&GeneralAllocator::Lock)},
    {"GeneralAllocator_Unlock", 0x1c767c, 23, 0x886ab953, reinterpret_cast<void *>(&GeneralAllocator::Unlock)},
    {"GeneralAllocator_DebugString", 0x1c7cac, 11, 0xaf61cb57, reinterpret_cast<void *>(&GeneralAllocator::DebugString)},
    {"GeneralAllocator_DefaultAssert", 0x1c6970, 17, 0x93167c2f, reinterpret_cast<void *>(&GeneralAllocator::DefaultAssert)},
};
REGISTER_HOOKS(kHooks);

} // namespace
