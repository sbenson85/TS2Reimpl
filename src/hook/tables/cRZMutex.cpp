#include "hook/hooks.h"
#include "rz/cRZMutex.h"

namespace {

// Not hooked: slot 0 (0x9ccb85) and slot 6 (0x9ccb69) are folded with another class's vtable.
// The constructor (0xc6c85e, 99 bytes) is missing from db/functions.jsonl, so tools/hookspec.py
// refuses it; its CRC is zlib's crc32 of the first 32 bytes of the unpacked exe.
const HookSpec kHooks[] = {
    {"cRZMutex_Construct", 0xc6c85e, 32, 0x6619be7c, MemberAddress(&cRZMutex::Construct)},
    {"cRZMutex_Destruct", 0xc6c8c3, 29, 0x31c47537, MemberAddress(&cRZMutex::Destruct)},
    {"cRZMutex_DeletingDestructor", 0xc6c8e0, 31, 0x20e98186, MemberAddress(&cRZMutex::DeletingDestructor)},
    {"cRZMutex_Lock", 0xc6c906, 24, 0x042e33ea, MemberAddress(&cRZMutex::Lock)},
    {"cRZMutex_Unlock", 0xc6c93e, 18, 0x382dae7e, MemberAddress(&cRZMutex::Unlock)},
    {"cRZMutex_TryLock", 0xc6c91e, 32, 0x912f7ac9, MemberAddress(&cRZMutex::TryLock)},
    {"cRZMutex_IsValid", 0xc6c8ff, 7, 0x0a3af48b, MemberAddress(&cRZMutex::IsValid)},
    {"cRZMutexData_DeletingDestructor", 0x218f06, 32, 0xdb0663bc, MemberAddress(&cRZMutexData::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
