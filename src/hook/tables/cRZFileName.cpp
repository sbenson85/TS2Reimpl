#include "hook/hooks.h"
#include "rz/cRZFileName.h"

namespace {

// 0x1c8800 (GetName, 4 bytes, shared) cannot be hooked. 0x1c0f3b's return value is whatever the
// caller's eax held, which no caller uses.
const HookSpec kHooks[] = {
    {"cRZFileName_Construct", 0x1c7ef4, 32, 0x4215f4ce, MemberAddress(&cRZFileName::Construct)},
    {"cRZFileName_Destruct", 0x1c0eb2, 32, 0xefbf94be, MemberAddress(&cRZFileName::Destruct)},
    {"cRZFileName_Init", 0x1c8bb8, 32, 0xfb89dfe1, MemberAddress(&cRZFileName::Init)},
    {"cRZFileName_GetDrive", 0x1c87ba, 13, 0x27f959d3, MemberAddress(&cRZFileName::GetDrive)},
    {"cRZFileName_GetDirectory", 0x1c87ac, 14, 0xa8d707de, MemberAddress(&cRZFileName::GetDirectory)},
    {"cRZFileName_GetExtension", 0x1c87ed, 14, 0x055c97bc, MemberAddress(&cRZFileName::GetExtension)},
    {"cRZFileName_Assemble", 0x1c898e, 32, 0xe9ac8bfc, MemberAddress(&cRZFileName::Assemble)},
    {"cRZFileName_GetFullPath", 0x1c882a, 32, 0x65d4487f, MemberAddress(&cRZFileName::GetFullPath)},
    {"cRZFileName_GetDrivePath", 0x1c87c7, 32, 0x24f93eb0, MemberAddress(&cRZFileName::GetDrivePath)},
    {"cRZFileName_IsAbsoluteFile", 0x1c8936, 32, 0x838a395c, MemberAddress(&cRZFileName::IsAbsoluteFile)},
    {"cRZFileName_IsAbsoluteDirectory", 0x1c88cd, 32, 0x0e923a3b, MemberAddress(&cRZFileName::IsAbsoluteDirectory)},
    {"cRZFileName_NormaliseDrive", 0x1c86fe, 32, 0xcf4a04f6, reinterpret_cast<void *>(&cRZFileName::NormaliseDrive)},
    {"cRZFileName_NormaliseDirectory", 0x1c86a1, 32, 0x800bc9e1, reinterpret_cast<void *>(&cRZFileName::NormaliseDirectory)},
    {"cRZFileName_NormaliseExtension", 0x1c875f, 32, 0x6fd8ead7, reinterpret_cast<void *>(&cRZFileName::NormaliseExtension)},
    {"cRZFileName_SplitInto", 0x1c39bc, 32, 0xd168c58d, reinterpret_cast<void *>(&cRZFileName::SplitInto)},
    {"cRZFileName_NormaliseSeparators", 0x1c0f3b, 32, 0xd6e178f5, reinterpret_cast<void *>(&cRZFileName::NormaliseSeparators)},
    {"cRZFileName_ConstructEmpty", 0x1c7f30, 32, 0x078fb097, MemberAddress(&cRZFileName::ConstructEmpty)},
};
REGISTER_HOOKS(kHooks);

} // namespace
