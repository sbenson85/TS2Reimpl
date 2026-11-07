#include "hook/hooks.h"
#include "simantics/EdithObjectTestSim.h"
#include "simantics/PrimPerson.h"
#include "simantics/RefCountedDeque.h"
#include "simantics/SimTraceLog.h"

namespace {

// The person's interaction queues and Person Data bookkeeping, the interaction check-tree runner (cEdithObjectTestSim) and
// the deque / trace helpers they use (lane AD).
const HookSpec kHooks[] = {
    {"RefCountedDeque_PushBack", 0x1d49e0, 32, 0x631192fb, MemberAddress(&RefCountedDeque::PushBack)},
    {"RefCountedDeque_PopFront", 0x525380, 32, 0xfa32b602, MemberAddress(&RefCountedDeque::PopFront)},
    {"RefCountedDeque_Clear", 0x1d48f0, 32, 0xd98a7e26, MemberAddress(&RefCountedDeque::Clear)},
    {"Deque_ThrowRangeError", 0x5248f0, 10, 0x8424bbc7, reinterpret_cast<void *>(&ThrowDequeRangeError)},
    {"cTSPerson_ClearActionQueue", 0x512c30, 32, 0x8ec5b082, MemberAddress(&PrimPersonObject::ClearActionQueue)},
    {"cTSPerson_RejectInteraction", 0x5120c0, 32, 0xda47445c, MemberAddress(&PrimPersonObject::RejectInteraction)},
    {"cTSPerson_PublishActionQueue", 0x520e40, 32, 0xce31e697, MemberAddress(&PrimPersonObject::PublishActionQueue)},
    {"cTSPerson_PublishPendingQueue", 0x5208a0, 32, 0xc718ac91, MemberAddress(&PrimPersonObject::PublishPendingQueue)},
    {"cTSPerson_SelectInteraction", 0x5205c0, 32, 0xe31df628, MemberAddress(&PrimPersonObject::SelectInteraction)},
    {"cTSPerson_StartInteraction", 0x520360, 32, 0x0e659e14, MemberAddress(&PrimPersonObject::StartInteraction)},
    {"cTSPerson_CountInteractionUse", 0x523da0, 32, 0xf5052b41, MemberAddress(&PrimPersonObject::CountInteractionUse)},
    {"SetUiManager", 0x4529c0, 10, 0x4c620486, reinterpret_cast<void *>(&SetUiManager)},
    {"cEdithObjectTestSim_Construct", 0x44f510, 32, 0xd5462d99, MemberAddress(&cEdithObjectTestSim::Construct)},
    {"cEdithObjectTestSim_Destruct", 0x44f540, 32, 0x1f2d21c0, MemberAddress(&cEdithObjectTestSim::Destruct)},
    {"cEdithObjectTestSim_vf3_Begin", 0x451060, 32, 0xf35b42f0, MemberAddress(&cEdithObjectTestSim::Begin)},
    {"cEdithObjectTestSim_vf9_RunCheckTree", 0x451f00, 32, 0xdefee0c9, MemberAddress(&cEdithObjectTestSim::RunCheckTree)},
    {"SimTraceLog_LogInteractionCheckStart", 0x526fc0, 32, 0x58b1f50f, MemberAddress(&SimTraceLog::LogInteractionCheckStart)},
    {"SimTraceLog_LogInteractionCheckComplete", 0x526b60, 32, 0x6a6cde56, MemberAddress(&SimTraceLog::LogInteractionCheckComplete)},
};
REGISTER_HOOKS(kHooks);

} // namespace
