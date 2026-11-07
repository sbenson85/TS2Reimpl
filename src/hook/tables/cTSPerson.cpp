#include "hook/hooks.h"
#include "ts/cTSPerson.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSPerson_vf752_32", 0x51c160, 12, 0x2fb0a66f, MemberAddress(&cTSPersonITS::vf32)},
    {"cTSPerson_vf752_33", 0x51c0a0, 9, 0x8f95dcbb, MemberAddress(&cTSPersonITS::vf33)},
    {"cTSPerson_vf752_34", 0x51c080, 11, 0x388cbf38, MemberAddress(&cTSPersonITS::vf34)},
    {"cTSPerson_vf752_30", 0x51c210, 12, 0x7d76eeb4, MemberAddress(&cTSPersonITS::vf30)},
    {"cTSPerson_vf752_50", 0x5196e0, 8, 0x8bd009af, MemberAddress(&cTSPersonITS::vf50)},
    {"cTSPerson_vf752_126", 0x512e40, 8, 0x6178fa75, MemberAddress(&cTSPersonITS::vf126)},
    {"cTSPerson_vf752_139", 0x519790, 7, 0x21a6dce0, MemberAddress(&cTSPersonITS::vf139)},
    {"cTSPerson_vf752_148", 0x519c30, 7, 0x29460694, MemberAddress(&cTSPersonITS::vf148)},
    {"cTSPerson_vf752_10", 0x51aa40, 15, 0x8b1d6cec, MemberAddress(&cTSPersonITS::vf10)},
    {"cTSPerson_vf752_57", 0x519d80, 10, 0xab1e008a, MemberAddress(&cTSPersonITS::vf57)},
    {"cTSPerson_vf752_58", 0x520a50, 16, 0x455f289a, MemberAddress(&cTSPersonITS::vf58)},
    {"cTSPerson_vf752_169", 0x512d50, 23, 0xfb267abf, MemberAddress(&cTSPersonITS::vf169)},
    {"cTSPerson_vf752_121", 0x520a60, 15, 0xc13705a5, MemberAddress(&cTSPersonITS::vf121)},
    {"cTSPerson_vf752_124", 0x512c00, 32, 0x2a4cd400, MemberAddress(&cTSPersonITS::vf124)},
    {"cTSPerson_vf752_159", 0x51e840, 32, 0xa834bbf6, MemberAddress(&cTSPersonITS::vf159)},
};
REGISTER_HOOKS(kHooks);

} // namespace
