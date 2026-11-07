#include "hook/hooks.h"
#include "sg/cAnimResource.h"

namespace {

// Thunks of the cAnimResource secondary subobject (vtable 0xe65ec4). Each is 14 bytes: the vtordisp adjustment
// and a jump to a verified cSGResource body. Slots 10 and 13 are not done yet (see src/sg/cAnimResource.h).
using nGZSceneDBAnim::cAnimResourceSubobject;

const HookSpec kHooks[] = {
    {"nGZSceneDBAnim_cAnimResource_vf196_6", 0xa10195, 14, 0xfd0e8fad, MemberAddress(&cAnimResourceSubobject::VirtualBaseIfObject)},
    {"nGZSceneDBAnim_cAnimResource_vf196_7", 0xa0f3ae, 14, 0xd780b8b9, MemberAddress(&cAnimResourceSubobject::GetOwner)},
    {"nGZSceneDBAnim_cAnimResource_vf196_9", 0xa10187, 14, 0x78577f4f, MemberAddress(&cAnimResourceSubobject::GetVirtualBase)},
    {"nGZSceneDBAnim_cAnimResource_vf196_11", 0xa0f554, 14, 0x95a321a4, MemberAddress(&cAnimResourceSubobject::CompareIdentity)},
    {"nGZSceneDBAnim_cAnimResource_vf196_15", 0xa103ce, 14, 0x421136e1, MemberAddress(&cAnimResourceSubobject::SetResource)},
    {"nGZSceneDBAnim_cAnimResource_vf196_16", 0xa101e0, 14, 0x07198e9c, MemberAddress(&cAnimResourceSubobject::SetFlag)},
    {"nGZSceneDBAnim_cAnimResource_vf196_22", 0xa101a3, 14, 0x071e10b9, MemberAddress(&cAnimResourceSubobject::CopyKeyTo)},
    {"nGZSceneDBAnim_cAnimResource_vf196_23", 0xa0f57b, 14, 0x8256a485, MemberAddress(&cAnimResourceSubobject::GetFlag)},
    {"nGZSceneDBAnim_cAnimResource_vf196_24", 0xa0fb19, 14, 0x78f235f0, MemberAddress(&cAnimResourceSubobject::ForwardKey)},
};
REGISTER_HOOKS(kHooks);

} // namespace
