#include "hook/hooks.h"
#include "rz/cRZOStream.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZOStream_QueryInterface", 0x1b5f6c, 32, 0x2e366231, MemberAddress(&cRZOStream::QueryInterface)},
    {"cRZOStream_SetFloat32", 0x1b5ffc, 15, 0xfa911df8, MemberAddress(&cRZOStream::SetFloat32)},
    {"cRZOStream_SetFloat64", 0x1b608b, 15, 0x018b47bb, MemberAddress(&cRZOStream::SetFloat64)},
    {"cRZOStream_SetGZSerializable", 0x1b60fc, 13, 0x236127af, MemberAddress(&cRZOStream::SetGZSerializable)},
    {"cRZOStream_SetSint16", 0x1b621c, 32, 0x866063f8, MemberAddress(&cRZOStream::SetSint16)},
    {"cRZOStream_SetSint32", 0x1b62b0, 32, 0x655b7b3c, MemberAddress(&cRZOStream::SetSint32)},
    {"cRZOStream_SetUint16", 0x1b6459, 32, 0x82bb45ed, MemberAddress(&cRZOStream::SetUint16)},
    {"cRZOStream_Construct", 0x1b4f75, 32, 0x71c20338, MemberAddress(&cRZOStream::Construct)},
    {"cRZOStream_Destruct", 0x1b52fb, 19, 0x06311874, MemberAddress(&cRZOStream::Destruct)},
    {"cRZOStream_DeletingDestructor", 0x1b580a, 31, 0x0b5bbe7b, MemberAddress(&cRZOStream::DeletingDestructor)},
    {"cRZOStream_SetSint64", 0x1b634c, 32, 0x4f3b4073, MemberAddress(&cRZOStream::SetSint64)},
    {"cRZOStream_ByteSwap64", 0x1b6734, 32, 0x1f568907, reinterpret_cast<void *>(&cRZOStream::ByteSwap64)},
    {"cRZOStream_SetPackedUint32", 0x1b58ee, 32, 0xca80dd9a, MemberAddress(&cRZOStream::SetPackedUint32)},
    {"cRZOStream_SetRZCharStr", 0x1b61c9, 32, 0xe8424cf6, MemberAddress(&cRZOStream::SetRZCharStr)},
    // Reached only through vtables, so db/functions.jsonl has no record and hookspec refuses them:
    // CRCs computed by hand (SetSint8 is 15 bytes, ending in ret 4), checked by tools/hookcrc.py.
    {"cRZOStream_SetSint8", 0x1b63e7, 15, 0xb271139c, MemberAddress(&cRZOStream::SetSint8)},
    {"cRZOStream_SetGZStr", 0x1b6118, 32, 0x94f091c7, MemberAddress(&cRZOStream::SetGZStr)},
};
REGISTER_HOOKS(kHooks);

} // namespace
