#include "hook/hooks.h"
#include "ts/cTSAutonomyConstantsClient.h"
#include "ts/cTSNeighborhoodConstants.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSNeighborhoodConstants_vf3", 0x47a630, 32, 0x7aaf8c3f, MemberAddress(&cTSNeighborhoodConstants::Load)},
    {"cTSNeighborhoodConstants_vf4", 0x417060, 32, 0x57bbaa0b, MemberAddress(&cTSNeighborhoodConstants::DeletingDestructor)},
    {"cTSNeighborhoodConstants_vf5", 0x474100, 32, 0x9f233d9e, MemberAddress(&cTSNeighborhoodConstants::GetFloat)},
    {"cTSNeighborhoodConstants_vf6", 0x4740a0, 32, 0x294488b2, MemberAddress(&cTSNeighborhoodConstants::GetValue)},
    {"cTSConstantsClient_vf0", 0x4df3d0, 19, 0xc7a2cb02, MemberAddress(&cTSConstantsClient::GetSegment)},
    {"cTSAutonomyConstantsClient_vf4", 0x417030, 32, 0xbb6c0a4b, MemberAddress(&cTSAutonomyConstantsClient::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
