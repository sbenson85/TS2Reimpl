#include "hook/hooks.h"
#include "rz/RZFileStreams.h"
#include "rz/cDebugStream.h"

namespace {

const HookSpec kHooks[] = {
    // cRZTextOStream
    {"cRZTextOStream_SetSint8", 0x1b63f6, 32, 0xd3e6aca0, MemberAddress(&cRZTextOStream::SetSint8)},
    {"cRZTextOStream_SetUint8", 0x1b65c3, 32, 0xf581746c, MemberAddress(&cRZTextOStream::SetUint8)},
    {"cRZTextOStream_SetSint16", 0x1b624d, 32, 0x40236575, MemberAddress(&cRZTextOStream::SetSint16)},
    {"cRZTextOStream_SetUint16", 0x1b648d, 32, 0x3f3adfe0, MemberAddress(&cRZTextOStream::SetUint16)},
    {"cRZTextOStream_SetSint32", 0x1b62dd, 32, 0xcd31abd7, MemberAddress(&cRZTextOStream::SetSint32)},
    {"cRZTextOStream_SetUint32", 0x1b64f0, 32, 0x3138df31, MemberAddress(&cRZTextOStream::SetUint32)},
    {"cRZTextOStream_SetSint64", 0x1b6383, 32, 0x8d2728d4, MemberAddress(&cRZTextOStream::SetSint64)},
    {"cRZTextOStream_SetUint64", 0x1b655f, 32, 0xc2cf2ee3, MemberAddress(&cRZTextOStream::SetUint64)},
    {"cRZTextOStream_SetFloat32", 0x1b600b, 32, 0x49c210a2, MemberAddress(&cRZTextOStream::SetFloat32)},
    {"cRZTextOStream_SetFloat64", 0x1b609a, 32, 0x34543d76, MemberAddress(&cRZTextOStream::SetFloat64)},
    {"cRZTextOStream_SetGZStr", 0x1b6153, 26, 0x6e8f7227, MemberAddress(&cRZTextOStream::SetGZStr)},
    {"cRZTextOStream_SetVoid", 0x1b6648, 32, 0xed94e963, MemberAddress(&cRZTextOStream::SetVoid)},
    {"cRZTextOStream_Destruct", 0x1b5319, 11, 0x183b224b, MemberAddress(&cRZTextOStream::Destruct)},
    {"cRZTextOStream_DeletingDestructor", 0x1b5848, 31, 0x91c9f320, MemberAddress(&cRZTextOStream::DeletingDestructor)},
    // cRZTextODSStream
    {"cRZTextODSStream_Construct", 0x1b4f99, 18, 0x7623a32e, MemberAddress(&cRZTextODSStream::Construct)},
    {"cRZTextODSStream_SetRZCharStr", 0x1b61fe, 15, 0x20620f69, MemberAddress(&cRZTextODSStream::SetRZCharStr)},
    {"cRZTextODSStream_Destruct", 0x1b530e, 11, 0xd3c4b5f7, MemberAddress(&cRZTextODSStream::Destruct)},
    {"cRZTextODSStream_DeletingDestructor", 0x1b5829, 31, 0x2d68a4a7, MemberAddress(&cRZTextODSStream::DeletingDestructor)},
    // cRZOFStream
    {"cRZOFStream_Construct", 0x1b4dd2, 32, 0x91fac84a, MemberAddress(static_cast<cRZOFStreamFromString>(&cRZOFStream::Construct))},
    {"cRZOFStream_ConstructChars", 0x1b4e80, 32, 0x89898169, MemberAddress(static_cast<cRZOFStreamFromChars>(&cRZOFStream::Construct))},
    {"cRZOFStream_Destruct", 0x1b52d8, 32, 0xdefbf8e6, MemberAddress(&cRZOFStream::Destruct)},
    {"cRZOFStream_DeletingDestructor", 0x1b57eb, 31, 0xe48558a4, MemberAddress(&cRZOFStream::DeletingDestructor)},
    {"cRZOFStream_Flush", 0x1b5939, 8, 0x61b7137b, MemberAddress(&cRZOFStream::Flush)},
    {"cRZOFStream_SetSint8", 0x1b63d8, 30, 0xcbecd948, MemberAddress(&cRZOFStream::SetSint8)},
    {"cRZOFStream_SetUint8", 0x1b65b4, 15, 0xad823344, MemberAddress(&cRZOFStream::SetUint8)},
    {"cRZOFStream_SetSint16", 0x1b620d, 15, 0x1dc33ab3, MemberAddress(&cRZOFStream::SetSint16)},
    {"cRZOFStream_SetUint16", 0x1b644a, 15, 0x2a1dca81, MemberAddress(&cRZOFStream::SetUint16)},
    {"cRZOFStream_SetSint32", 0x1b62a1, 15, 0x070cd294, MemberAddress(&cRZOFStream::SetSint32)},
    {"cRZOFStream_SetUint32", 0x1b64e1, 15, 0xff0f4658, MemberAddress(&cRZOFStream::SetUint32)},
    {"cRZOFStream_SetSint64", 0x1b632f, 29, 0x8b849a65, MemberAddress(&cRZOFStream::SetSint64)},
    {"cRZOFStream_SetUint64", 0x1b6542, 29, 0x19098d3a, MemberAddress(&cRZOFStream::SetUint64)},
    {"cRZOFStream_SetFloat32", 0x1b5fdb, 32, 0x057a50c1, MemberAddress(&cRZOFStream::SetFloat32)},
    {"cRZOFStream_SetFloat64", 0x1b6069, 32, 0xffcbe673, MemberAddress(&cRZOFStream::SetFloat64)},
    {"cRZOFStream_SetRZCharStr", 0x1b616d, 32, 0x91e280c7, MemberAddress(&cRZOFStream::SetRZCharStr)},
    {"cRZOFStream_SetGZStr", 0x1b6109, 32, 0x2a895866, MemberAddress(&cRZOFStream::SetGZStr)},
    {"cRZOFStream_SetVoid", 0x1b661f, 32, 0xc5d5ab6c, MemberAddress(&cRZOFStream::SetVoid)},
    // cRZIFStream
    {"cRZIFStream_Construct", 0x1b4cdc, 32, 0x09c0de75, MemberAddress(&cRZIFStream::Construct)},
    {"cRZIFStream_Destruct", 0x1b528c, 32, 0x47f563ab, MemberAddress(&cRZIFStream::Destruct)},
    {"cRZIFStream_DeletingDestructor", 0x1b576f, 31, 0xac74ecd7, MemberAddress(&cRZIFStream::DeletingDestructor)},
    {"cRZIFStream_Skip", 0x1b6704, 32, 0xd2a077f4, MemberAddress(&cRZIFStream::Skip)},
    {"cRZIFStream_GetSint8", 0x1b5ddd, 32, 0x3305aac0, MemberAddress(&cRZIFStream::GetSint8)},
    {"cRZIFStream_GetSint16", 0x1b5c2b, 32, 0x7f6b4dd1, MemberAddress(&cRZIFStream::GetSint16)},
    {"cRZIFStream_GetUint16", 0x1b5e1d, 32, 0xa39b9273, MemberAddress(&cRZIFStream::GetUint16)},
    {"cRZIFStream_GetSint32", 0x1b5c9a, 32, 0x3221e46b, MemberAddress(&cRZIFStream::GetSint32)},
    {"cRZIFStream_GetUint32", 0x1b5e4d, 32, 0x82dcd4bd, MemberAddress(&cRZIFStream::GetUint32)},
    {"cRZIFStream_GetSint64", 0x1b5d04, 32, 0xc6c42025, MemberAddress(&cRZIFStream::GetSint64)},
    {"cRZIFStream_GetUint64", 0x1b5e7d, 32, 0x7abc932c, MemberAddress(&cRZIFStream::GetUint64)},
    {"cRZIFStream_GetFloat32", 0x1b5952, 32, 0x69fc2368, MemberAddress(&cRZIFStream::GetFloat32)},
    {"cRZIFStream_GetFloat64", 0x1b5990, 32, 0x3a674e72, MemberAddress(&cRZIFStream::GetFloat64)},
    {"cRZIFStream_GetRZCharStr", 0x1b5b46, 32, 0x7a6038ef, MemberAddress(&cRZIFStream::GetRZCharStr)},
    {"cRZIFStream_GetGZStr", 0x1b59db, 32, 0x9bc0072c, MemberAddress(&cRZIFStream::GetGZStr)},
    {"cRZIFStream_GetVoid", 0x1b5f0a, 32, 0x1cb8d555, MemberAddress(&cRZIFStream::GetVoid)},
    // cDebugStream (vtable 0xdcf704; its destructor body 0x511760 is called by the sim code too)
    {"cDebugStream_SetSint8", 0x520d70, 32, 0x086cefc1, MemberAddress(&cDebugStream::SetSint8)},
    {"cDebugStream_SetUint8", 0x5211f0, 32, 0x086cefc1, MemberAddress(&cDebugStream::SetUint8)},
    {"cDebugStream_SetSint16", 0x520bd0, 32, 0x877dbf88, MemberAddress(&cDebugStream::SetSint16)},
    {"cDebugStream_SetUint16", 0x521050, 32, 0x877dbf88, MemberAddress(&cDebugStream::SetUint16)},
    {"cDebugStream_SetSint32", 0x520ca0, 32, 0x086cefc1, MemberAddress(&cDebugStream::SetSint32)},
    {"cDebugStream_SetUint32", 0x521120, 32, 0x086cefc1, MemberAddress(&cDebugStream::SetUint32)},
    {"cDebugStream_SetFloat32", 0x520720, 32, 0x086cefc1, MemberAddress(&cDebugStream::SetFloat32)},
    {"cDebugStream_SetRZCharStr", 0x520ab0, 32, 0x086cefc1, MemberAddress(&cDebugStream::SetRZCharStr)},
    {"cDebugStream_SetGZStr", 0x520800, 32, 0xa795e802, MemberAddress(&cDebugStream::SetGZStr)},
    {"cDebugStream_Destruct", 0x511760, 32, 0x8e3f89d6, MemberAddress(&cDebugStream::Destruct)},
    {"cDebugStream_DeletingDestructor", 0x512070, 32, 0x439526a4, MemberAddress(&cDebugStream::DeletingDestructor)},
    // The path helper both constructors use.
    {"RZFileStreams_ConvertPath", 0x1b5fa4, 32, 0x3bcead51, reinterpret_cast<void *>(&rzfilestreams::ConvertPath)},
};

REGISTER_HOOKS(kHooks);

} // namespace
