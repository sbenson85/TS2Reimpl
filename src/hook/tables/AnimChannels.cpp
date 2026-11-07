#include "hook/hooks.h"
#include "sg/AnimChannels.h"

namespace {

using nGZSceneDBAnim::cChannelUber;
using nGZSceneDBAnim::cChannelUberAttribute;
using nGZSceneDBAnim::cChannelUberQ;
using nGZSceneDBAnim::cChannelUberXYZ;
using nGZSceneDBAnim::cEventKeyConst;

const HookSpec kHooks[] = {
    {"nGZSceneDBAnim_cChannelUber_QueryInterface", 0xa266d0, 32, 0xfdff10a2, MemberAddress(&cChannelUber::QueryInterface)},
    {"nGZSceneDBAnim_cChannelUberQ_QueryInterface", 0x9efd10, 32, 0xb25bab99, MemberAddress(&cChannelUberQ::QueryInterface)},
    {"nGZSceneDBAnim_cChannelUberXYZ_QueryInterface", 0x9f0840, 32, 0x72aa9705, MemberAddress(&cChannelUberXYZ::QueryInterface)},
    {"nGZSceneDBAnim_cChannelUberAttribute_QueryInterface", 0x9f0c70, 32, 0xec17ac3c, MemberAddress(&cChannelUberAttribute::QueryInterface)},
    {"nGZSceneDBAnim_cEventKeyConst_QueryInterface", 0xa29200, 32, 0xca973f55, MemberAddress(&cEventKeyConst::QueryInterface)},

    {"nGZSceneDBAnim_cChannelUber_Destruct", 0xa264a0, 32, 0x0bd360da, MemberAddress(&cChannelUber::Destruct)},
    {"nGZSceneDBAnim_cChannelUber_vf3", 0xa266a0, 32, 0x40fb24f9, MemberAddress(&cChannelUber::DeletingDestructor)},
    {"nGZSceneDBAnim_cChannelUber_Evaluate", 0xa267b0, 32, 0x57842b27, MemberAddress(&cChannelUber::Evaluate)},

    {"nGZSceneDBAnim_cChannelUberQ_Destruct", 0x9eef80, 32, 0xbdef35de, MemberAddress(&cChannelUberQ::Destruct)},
    {"nGZSceneDBAnim_cChannelUberQ_vf3", 0x9ef0d0, 32, 0xef7ec40c, MemberAddress(&cChannelUberQ::DeletingDestructor)},
    {"nGZSceneDBAnim_cChannelUberQ_PoolFree", 0x9ef0b0, 22, 0x23281109, reinterpret_cast<void *>(&cChannelUberQ::PoolFree)},

    {"nGZSceneDBAnim_cChannelUberXYZ_Destruct", 0x9efe60, 32, 0xa480199b, MemberAddress(&cChannelUberXYZ::Destruct)},
    {"nGZSceneDBAnim_cChannelUberXYZ_vf3", 0x9eff80, 32, 0xafe1f13c, MemberAddress(&cChannelUberXYZ::DeletingDestructor)},
    {"nGZSceneDBAnim_cChannelUberXYZ_PoolFree", 0x9eff60, 22, 0x58b2a773, reinterpret_cast<void *>(&cChannelUberXYZ::PoolFree)},

    {"nGZSceneDBAnim_cChannelUberAttribute_Destruct", 0x9f08f0, 32, 0x271a7d13, MemberAddress(&cChannelUberAttribute::Destruct)},
    {"nGZSceneDBAnim_cChannelUberAttribute_vf3", 0x9f0a10, 32, 0xafe1f13c, MemberAddress(&cChannelUberAttribute::DeletingDestructor)},
    {"nGZSceneDBAnim_cChannelUberAttribute_PoolFree", 0x9f09f0, 22, 0x881cc79e, reinterpret_cast<void *>(&cChannelUberAttribute::PoolFree)},
    {"nGZSceneDBAnim_cChannelUberAttribute_Fetch", 0x9f0a50, 32, 0x9cc0a014, MemberAddress(&cChannelUberAttribute::Fetch)},
    {"nGZSceneDBAnim_cChannelUberAttribute_vf7", 0x9f0bb0, 32, 0x833861ed, MemberAddress(&cChannelUberAttribute::Slot7)},
    {"nGZSceneDBAnim_cChannelUberAttribute_vf8", 0x9f0b60, 32, 0x59ac68a0, MemberAddress(&cChannelUberAttribute::Slot8)},

    {"nGZSceneDBAnim_cEventKeyConst_vf4", 0xa29270, 32, 0x666335a9, MemberAddress(&cEventKeyConst::GetTypeName)},
    {"nGZSceneDBAnim_cEventKeyConst_vf11", 0xa29240, 19, 0x78bc6f31, MemberAddress(&cEventKeyConst::GetFloat11)},
    {"nGZSceneDBAnim_cEventKeyConst_Construct", 0xa29140, 9, 0xdb626fbb, MemberAddress(&cEventKeyConst::Construct)},
    {"nGZSceneDBAnim_cEventKeyConst_Destruct", 0xa29150, 7, 0x6dd39ee5, MemberAddress(&cEventKeyConst::Destruct)},
    {"nGZSceneDBAnim_cChannelUberQ_vf7", 0x9efc40, 32, 0x21cbfef8, MemberAddress(&nGZSceneDBAnim::cChannelUberQ::EvaluateBlended)},
    {"nGZSceneDBAnim_cChannelUberQ_vf8", 0x9efbe0, 32, 0x3f29fdfc, MemberAddress(&nGZSceneDBAnim::cChannelUberQ::EvaluateUnblended)},
    {"nGZSceneDBAnim_cChannelUberXYZ_vf7", 0x9f0770, 32, 0x8c02217b, MemberAddress(&nGZSceneDBAnim::cChannelUberXYZ::EvaluateBlended)},
    {"nGZSceneDBAnim_cChannelUberXYZ_vf8", 0x9f0710, 32, 0xefa34d17, MemberAddress(&nGZSceneDBAnim::cChannelUberXYZ::EvaluateUnblended)},
};
REGISTER_HOOKS(kHooks);

} // namespace
