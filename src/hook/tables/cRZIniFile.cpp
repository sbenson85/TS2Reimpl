#include "hook/hooks.h"
#include "rz/cRZIniFile.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZIniFile_SetPath", 0x9c6b06, 32, 0x5fcafb69, MemberAddress(&cRZIniFile::SetPath)},
    {"cRZIniFile_SetFile", 0x9c6a73, 32, 0xa586ff88, MemberAddress(&cRZIniFile::SetFile)},
    {"cRZIniFile_GetLong", 0x9c69ab, 32, 0xf652c219, MemberAddress(&cRZIniFile::GetLong)},
    {"cRZIniFile_SetLong", 0x9c75f5, 32, 0x8cde99c0, MemberAddress(&cRZIniFile::SetLong)},
    {"cRZIniFile_SetEnabled", 0x9c6aec, 26, 0x1ec6c902, MemberAddress(&cRZIniFile::SetEnabled)},
    // Lane LZ (F4c). The slots are shared only by the ini family (cRZIniFile, cTSIniFile, cHitIniFile).
    {"cRZIniFile_Clear", 0x9c5ecb, 32, 0x32be2fda, MemberAddress(&cRZIniFile::Clear)},
    {"cRZIniFile_GetBinary", 0x9c66b2, 32, 0xbf223e6a, MemberAddress(&cRZIniFile::GetBinary)},
    {"cRZIniFile_GetBinaryAlloc", 0x9c6722, 32, 0x62ac3cc3, MemberAddress(&cRZIniFile::GetBinaryAlloc)},
    {"cRZIniFile_SetBinary", 0x9c6b28, 32, 0x5b99b7a6, MemberAddress(&cRZIniFile::SetBinary)},
    {"cRZIniFile_ForEachKey", 0x9c5ef2, 32, 0xc54a9080, MemberAddress(&cRZIniFile::ForEachKey)},
    {"cRZIniFile_ForEachSection", 0x9c615f, 32, 0xa4883011, MemberAddress(&cRZIniFile::ForEachSection)},
    {"cRZIniFile_Load", 0x9c63da, 32, 0x60036d99, MemberAddress(&cRZIniFile::Load)},
    {"cRZIniFile_ReadLine", 0x9c623d, 32, 0x4f56c28c, MemberAddress(&cRZIniFile::ReadLine)},
    {"cRZIniFile_Open", 0x9c65f1, 32, 0xa9874f9d, MemberAddress(&cRZIniFile::Open)},
    {"IniHexDecode", 0x1afb5e, 32, 0xabf5a0b4, reinterpret_cast<void *>(&IniHexDecode)},
    {"IniHexEncode", 0x1afbd7, 32, 0xd9b43cbf, reinterpret_cast<void *>(&IniHexEncode)},
    {"cRZIniFile_vf10", 0x9c6bbc, 32, 0x9e629f82, MemberAddress(&cRZIniFile::SetString)},
};
REGISTER_HOOKS(kHooks);

} // namespace
