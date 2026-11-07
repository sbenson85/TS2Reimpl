#include "hook/hooks.h"
#include "sg/cAnimatable.h"

namespace {

// Thunks of nTSSG::cAnimatable's secondary subobjects (vtables at +424, +432 and +444). Each is an 8, 11 or 14 byte
// vtordisp adjustment and a jump to a two-instruction getter (see src/sg/cAnimatable.h).
using nTSSG::cAnimatableSubobject;

const HookSpec kHooks[] = {
    {"nTSSG_cAnimatable_vf432_55", 0x26e018, 14, 0xb957ac34, MemberAddress(&cAnimatableSubobject::Vf432_55)},
    {"nTSSG_cAnimatable_vf444_25", 0x26fbdf, 8, 0x2a31a285, MemberAddress(&cAnimatableSubobject::Vf444_25)},
    {"nTSSG_cAnimatable_vf432_20", 0x26fc04, 14, 0x5cc8f1c8, MemberAddress(&cAnimatableSubobject::Vf432_20)},
    {"nTSSG_cAnimatable_vf432_126", 0x26fda5, 11, 0x52f60e30, MemberAddress(&cAnimatableSubobject::Vf432_126)},
    {"nTSSG_cAnimatable_vf444_26", 0x26fdb0, 8, 0x928dc5e0, MemberAddress(&cAnimatableSubobject::Vf444_26)},
    {"nTSSG_cAnimatable_vf432_39", 0x270558, 14, 0x7c49f22e, MemberAddress(&cAnimatableSubobject::Vf432_39)},
    {"nTSSG_cAnimatable_vf432_134", 0x271dac, 11, 0x947562f5, MemberAddress(&cAnimatableSubobject::Vf432_134)},
    {"nTSSG_cAnimatable_vf432_49", 0x272197, 14, 0xf221a4e3, MemberAddress(&cAnimatableSubobject::Vf432_49)},
    {"nTSSG_cAnimatable_vf432_37", 0x2721a5, 14, 0x8753aca0, MemberAddress(&cAnimatableSubobject::Vf432_37)},
    {"nTSSG_cAnimatable_vf424_10", 0x27253f, 11, 0x2ebe168a, MemberAddress(&cAnimatableSubobject::Vf424_10)},
    {"nTSSG_cAnimatable_vf432_33", 0x273339, 14, 0x03149e9a, MemberAddress(&cAnimatableSubobject::Vf432_33)},
    {"nTSSG_cAnimatable_vf432_68", 0x2737dc, 14, 0x44437e0f, MemberAddress(&cAnimatableSubobject::Vf432_68)},
    {"nTSSG_cAnimatable_vf432_76", 0x2737ea, 14, 0x54b7f19d, MemberAddress(&cAnimatableSubobject::Vf432_76)},
    {"nTSSG_cAnimatable_vf432_35", 0x274062, 14, 0x791579cc, MemberAddress(&cAnimatableSubobject::Vf432_35)},
    {"nTSSG_cAnimatable_vf444_27", 0x274070, 8, 0x928dc5e0, MemberAddress(&cAnimatableSubobject::Vf444_27)},
};
REGISTER_HOOKS(kHooks);

} // namespace
