#include "hook/hooks.h"
#include "rz/cRZTextFile.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZTextFile_Construct", 0x1c3ec4, 32, 0xf2a82940, MemberAddress(static_cast<cRZTextFile *(cRZTextFile::*)(const char *)>(&cRZTextFile::Construct))},
    {"cRZTextFile_ConstructString", 0x1c3e4a, 32, 0x29ae3981, MemberAddress(static_cast<cRZTextFile *(cRZTextFile::*)(const cIGZString &)>(&cRZTextFile::Construct))},
    {"cRZTextFile_Destruct", 0x1bab55, 32, 0xb2163db1, MemberAddress(&cRZTextFile::Destruct)},
    {"cRZTextFile_DeletingDestructor", 0x1c3ff8, 32, 0x9afb6741, MemberAddress(&cRZTextFile::DeletingDestructor)},
    {"cRZTextFile_Open", 0x1c401a, 32, 0xd82c1990, MemberAddress(&cRZTextFile::Open)},
};
REGISTER_HOOKS(kHooks);

} // namespace
