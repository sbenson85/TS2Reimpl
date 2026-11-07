#include "app/AppServices3.h"
#include "hook/hooks.h"
#include "rz/cTSIniFile.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSIniFile_Construct", 0xcb2a20, 32, 0x75f677be, MemberAddress(&cTSIniFile::Construct)},
    {"cTSIniFile_Destruct", 0xcb2cc0, 25, 0x97ec9dc4, MemberAddress(&cTSIniFile::Destruct)},
    {"cTSIniFile_vf0", 0xcb2ce0, 32, 0x384e7073, MemberAddress(&cTSIniFile::DeletingDestructor)},
    {"cTSIniFile_vf6", 0xcb2e90, 32, 0x0e6d63f9, MemberAddress(&cTSIniFile::GetString)},
    {"cTSIniFile_vf13", 0xcb2d00, 32, 0xf7c965aa, MemberAddress(&cTSIniFile::SectionWalk)},
    {"cTSIniFile_vf20", 0xcb30f0, 32, 0xf7c965aa, MemberAddress(&cTSIniFile::HasSectionPrefixed)},
    {"cRZIniFile_vf15", 0x9c6a0e, 32, 0xaaccf18c, MemberAddress(&cTSIniFile::HasSection)},
    {"cRZString_NextToken", 0x1ad676, 32, 0x351399d8, MemberAddress(&cRZStringTokenizer::NextToken)},
    {"AppServices3_AppendIniCommandLine", 0xc2b078, 32, 0x81b845c3, reinterpret_cast<void *>(&appservices3::AppendIniCommandLine)},
};
REGISTER_HOOKS(kHooks);

} // namespace
