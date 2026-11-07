#include "hook/hooks.h"
#include "rz/cRZFile.h"

namespace {

// Overloaded members need an explicit type to take their address.
using CtorString = cRZFile *(cRZFile::*)(const cIGZString &);
using CtorChar = cRZFile *(cRZFile::*)(const char *);
using CtorDefault = cRZFile *(cRZFile::*)();
using SetPathChar = void (cRZFile::*)(const char *);
using SetPathString = void (cRZFile::*)(const cIGZString &);
using CopyFromFile = bool (cRZFile::*)(const void *, uint8_t);
using CopyFromPath = bool (cRZFile::*)(const cIGZString &, uint8_t);

const HookSpec kHooks[] = {
    {"cRZFile_Open", 0x1c285d, 32, 0xa63c5892, MemberAddress(&cRZFile::Open)},
    {"cRZFile_Length", 0x1c230b, 32, 0x295b7fdd, MemberAddress(&cRZFile::Length)},
    {"cRZFile_Remove", 0x1c2ea2, 32, 0x571b9b67, MemberAddress(&cRZFile::Remove)},
    {"cRZFile_Rename", 0x1c3015, 32, 0x24d40798, MemberAddress(&cRZFile::Rename)},
    {"cRZFile_Exists", 0x1c114e, 32, 0xcc63bae2, MemberAddress(&cRZFile::Exists)},
    {"cRZFile_Copy_File", 0x1c10d3, 32, 0x2aeac229, MemberAddress(static_cast<CopyFromFile>(&cRZFile::Copy))},
    {"cRZFile_Copy_Path", 0x1c1122, 32, 0x47135994, MemberAddress(static_cast<CopyFromPath>(&cRZFile::Copy))},
    {"cRZFile_LengthOfPath", 0x1c1b8b, 32, 0xc4a037b6, reinterpret_cast<void *>(&cRZFile::LengthOfPath)},
    {"cRZFile_PathExists", 0x1c1242, 32, 0x11cda5f0, reinterpret_cast<void *>(&cRZFile::PathExists)},
    {"cRZFile_MovePath", 0x1c2f48, 32, 0xae02df52, reinterpret_cast<void *>(&cRZFile::MovePath)},
    {"cRZFile_CopyPath", 0x1c0ff8, 32, 0xee91bffb, reinterpret_cast<void *>(&cRZFile::CopyPath)},
    {"cRZFile_ResolveShortcutW", 0x1c3235, 32, 0x7e72b01c, reinterpret_cast<void *>(&cRZFile::ResolveShortcutW)},
    {"cRZFile_ResolveShortcutA", 0x1c3059, 32, 0x7c079786, reinterpret_cast<void *>(&cRZFile::ResolveShortcutA)},
    {"cRZFile_Construct_String", 0x1c0c07, 32, 0x74275c98, MemberAddress(static_cast<CtorString>(&cRZFile::Construct))},
    {"cRZFile_Construct_Char", 0x1c0c8b, 32, 0xe4de5f83, MemberAddress(static_cast<CtorChar>(&cRZFile::Construct))},
    {"cRZFile_Construct", 0x1c0d1d, 32, 0x61c28e99, MemberAddress(static_cast<CtorDefault>(&cRZFile::Construct))},
    {"cRZFile_Destruct", 0x1c0e6d, 32, 0x01edbcd4, MemberAddress(&cRZFile::Destruct)},
    {"cRZFile_DeletingDestructor", 0x1c0ef9, 32, 0x822dede5, MemberAddress(&cRZFile::DeletingDestructor)},
    {"cRZFile_QueryInterface", 0x1c2aa0, 32, 0xb3037a82, MemberAddress(&cRZFile::QueryInterface)},
    {"cRZFile_AddRef", 0x1c0f1b, 8, 0xcd2ed416, MemberAddress(&cRZFile::AddRef)},
    {"cRZFile_Release", 0x1c2e0b, 26, 0xa2efe342, MemberAddress(&cRZFile::Release)},
    {"cRZFile_Close", 0x1c0faa, 32, 0xd5723994, MemberAddress(&cRZFile::Close)},
    {"cRZFile_Position", 0x1c2a93, 13, 0x0691fc5f, MemberAddress(&cRZFile::Position)},
    {"cRZFile_SetLength", 0x1c3717, 32, 0x4cb8527c, MemberAddress(&cRZFile::SetLength)},
    {"cRZFile_SeekToBegin", 0x1c34d5, 10, 0x09ae5630, MemberAddress(&cRZFile::SeekToBegin)},
    {"cRZFile_SeekToEnd", 0x1c34df, 10, 0x9e314719, MemberAddress(&cRZFile::SeekToEnd)},
    {"cRZFile_SeekToRelativePosition", 0x1c34f7, 14, 0x948fc439, MemberAddress(&cRZFile::SeekToRelativePosition)},
    {"cRZFile_SeekToPosition", 0x1c34e9, 14, 0x55011bf9, MemberAddress(&cRZFile::SeekToPosition)},
    {"cRZFile_Seek", 0x1c3450, 32, 0x520fd9ed, MemberAddress(&cRZFile::Seek)},
    {"cRZFile_Read", 0x1c2c6e, 32, 0x5dea557f, MemberAddress(&cRZFile::Read)},
    {"cRZFile_ReadWithCount", 0x1c2c97, 32, 0x33e9794a, MemberAddress(&cRZFile::ReadWithCount)},
    {"cRZFile_Write", 0x1c3bec, 32, 0x190be6c9, MemberAddress(&cRZFile::Write)},
    {"cRZFile_WriteWithCount", 0x1c3c15, 32, 0xa1bb1282, MemberAddress(&cRZFile::WriteWithCount)},
    {"cRZFile_Flush", 0x1c1413, 24, 0xef33cae9, MemberAddress(&cRZFile::Flush)},
    {"cRZFile_SysFlush", 0x1c3bdd, 15, 0xd6373e72, MemberAddress(&cRZFile::SysFlush)},
    {"cRZFile_SetPath_Char", 0x1c379f, 18, 0xa1bceeb8, MemberAddress(static_cast<SetPathChar>(&cRZFile::SetPath))},
    {"cRZFile_SetPath_String", 0x1c377a, 32, 0x76b60487, MemberAddress(static_cast<SetPathString>(&cRZFile::SetPath))},
    {"cRZFile_GetOpenModes", 0x1c1c37, 32, 0x2dbdfb4c, MemberAddress(&cRZFile::GetOpenModes)},
    {"cRZFile_GetBuffering", 0x1c1a4d, 24, 0x5a4ad193, MemberAddress(&cRZFile::GetBuffering)},
    {"cRZFile_SetBuffering", 0x1c3673, 32, 0xb32136c4, MemberAddress(&cRZFile::SetBuffering)},
    {"cRZFile_ClearReadBuffer", 0x1c0f8f, 12, 0x31168aa0, MemberAddress(&cRZFile::ClearReadBuffer)},
    {"cRZFile_ClearWriteBuffer", 0x1c0f9b, 15, 0x40d55bf5, MemberAddress(&cRZFile::ClearWriteBuffer)},
    {"cRZFile_FlushWriteBuffer", 0x1c142b, 32, 0x10057660, MemberAddress(&cRZFile::FlushWriteBuffer)},
    {"cRZFile_RawRead", 0x1c2bc9, 32, 0xb387ba41, MemberAddress(&cRZFile::RawRead)},
    {"cRZFile_RawSeek", 0x1c2c0a, 32, 0xadd96cd7, MemberAddress(&cRZFile::RawSeek)},
    {"cRZFile_FillReadBuffer", 0x1c12c1, 32, 0x0481fe0c, MemberAddress(&cRZFile::FillReadBuffer)},
    {"cRZFile_BufferedWrite", 0x1c1312, 32, 0x0940b5d7, MemberAddress(&cRZFile::BufferedWrite)},
    {"cRZFile_SetErrorFromLastError", 0x1c3429, 32, 0x58e8ee1a, MemberAddress(&cRZFile::SetErrorFromLastError)},
};
REGISTER_HOOKS(kHooks);

} // namespace
