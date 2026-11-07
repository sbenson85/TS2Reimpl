#include "hook/hooks.h"
#include "rz/cRZIStream.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZIStream_QueryInterface", 0x1b5f48, 32, 0x3a31394e, MemberAddress(&cRZIStream::QueryInterface)},
    {"cRZIStream_GetSint8", 0x1b5e0f, 14, 0xffb064a5, MemberAddress(&cRZIStream::GetSint8)},
    {"cRZIStream_GetSint16", 0x1b5c5b, 32, 0x57e8b379, MemberAddress(&cRZIStream::GetSint16)},
    {"cRZIStream_GetSint32", 0x1b5cca, 32, 0x02a58080, MemberAddress(&cRZIStream::GetSint32)},
    {"cRZIStream_GetFloat32", 0x1b5982, 14, 0x8ee7c8e7, MemberAddress(&cRZIStream::GetFloat32)},
    {"cRZIStream_GetFloat64", 0x1b59c0, 14, 0x84fd5a20, MemberAddress(&cRZIStream::GetFloat64)},
    {"cRZIStream_Construct", 0x1b4d82, 32, 0x350e24d2, MemberAddress(&cRZIStream::Construct)},
    {"cRZIStream_Destruct", 0x1b52af, 19, 0x173c9642, MemberAddress(&cRZIStream::Destruct)},
    {"cRZIStream_DeletingDestructor", 0x1b578e, 31, 0x130ed218, MemberAddress(&cRZIStream::DeletingDestructor)},
    {"cRZIStream_GetSint64", 0x1b5da9, 32, 0x0a4a30aa, MemberAddress(&cRZIStream::GetSint64)},
    {"cRZIStream_GetPackedUint32", 0x1b588e, 32, 0x15ba57f9, MemberAddress(&cRZIStream::GetPackedUint32)},
    {"cRZIStream_GetRZCharStr", 0x1b5b86, 32, 0x81839e10, MemberAddress(&cRZIStream::GetRZCharStr)},
    {"cRZIStream_GetGZStr", 0x1b5a4d, 32, 0xd5c19e44, MemberAddress(&cRZIStream::GetGZStr)},
    {"cRZIStream_GetGZSerializable", 0x1b59ce, 13, 0x39aecf88, MemberAddress(&cRZIStream::GetGZSerializable)},
};
REGISTER_HOOKS(kHooks);

} // namespace
