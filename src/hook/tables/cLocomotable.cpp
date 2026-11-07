#include "hook/hooks.h"
#include "sg/cLocomotable.h"

namespace {

// Thunked slots of the nTSSG::cLocomotable secondary subobjects (vtables at +860 and +872). Each is 14 bytes: the
// vtordisp adjustment and a jump to a field getter, reproduced in src/sg/cLocomotable.cpp.
using nTSSG::cLocomotableSubobject;

const HookSpec kHooks[] = {
    {"nTSSG_cLocomotable_vf860_55", 0x280d35, 14, 0xd2cbd48e, MemberAddress(&cLocomotableSubobject::Vf860_55)},
    {"nTSSG_cLocomotable_vf872_25", 0x28270c, 14, 0x909c421f, MemberAddress(&cLocomotableSubobject::Vf872_25)},
    {"nTSSG_cLocomotable_vf872_24", 0x282a1c, 14, 0x965780f4, MemberAddress(&cLocomotableSubobject::Vf872_24)},
    {"nTSSG_cLocomotable_vf860_20", 0x2837ec, 14, 0xd8b554ad, MemberAddress(&cLocomotableSubobject::Vf860_20)},
    {"nTSSG_cLocomotable_vf860_126", 0x2837fa, 14, 0x1544421c, MemberAddress(&cLocomotableSubobject::Vf860_126)},
    {"nTSSG_cLocomotable_vf872_26", 0x283808, 14, 0xac91947c, MemberAddress(&cLocomotableSubobject::Vf872_26)},
    {"nTSSG_cLocomotable_vf860_39", 0x2839d3, 14, 0xd2329f16, MemberAddress(&cLocomotableSubobject::Vf860_39)},
    {"nTSSG_cLocomotable_vf860_134", 0x28651d, 14, 0x341ecd7c, MemberAddress(&cLocomotableSubobject::Vf860_134)},
    {"nTSSG_cLocomotable_vf860_49", 0x286fd9, 14, 0x5571d08a, MemberAddress(&cLocomotableSubobject::Vf860_49)},
    {"nTSSG_cLocomotable_vf860_37", 0x286fe7, 14, 0xaf614f9e, MemberAddress(&cLocomotableSubobject::Vf860_37)},
    {"nTSSG_cLocomotable_vf860_33", 0x2874e8, 14, 0x45fb236d, MemberAddress(&cLocomotableSubobject::Vf860_33)},
    {"nTSSG_cLocomotable_vf860_68", 0x2874f6, 14, 0x6799d46d, MemberAddress(&cLocomotableSubobject::Vf860_68)},
    {"nTSSG_cLocomotable_vf860_76", 0x287504, 14, 0x4bf60a41, MemberAddress(&cLocomotableSubobject::Vf860_76)},
    {"nTSSG_cLocomotable_vf860_35", 0x287683, 14, 0x94202259, MemberAddress(&cLocomotableSubobject::Vf860_35)},
    {"nTSSG_cLocomotable_vf872_27", 0x287691, 14, 0xe99eb224, MemberAddress(&cLocomotableSubobject::Vf872_27)},
};
REGISTER_HOOKS(kHooks);

} // namespace
