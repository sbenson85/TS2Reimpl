#include "hook/hooks.h"
#include "simantics/SimTraceLog.h"
#include "simantics/TreeFrame.h"
#include "simantics/TreeReplay.h"
#include "simantics/TreeSimObject.h"
#include "simantics/TreeSim.h"
#include "simantics/TreeSimRunNode.h"
#include "simantics/TreeStack.h"

namespace {

// The SimAntics interpreter: cTSTreeSim (the sim subobject of every object: stack, steps, running trees), its call stack
// cTSTreeStack and the stack frames cTSTreeStackElem, plus the trace log functions the interpreter calls.
// Not hooked: the slots the classes share with unrelated ones (the stack's Count/AddRef/Release, the frames' plain getters
// and setters of the +0x18/+0x1a/+0x1c words, stack object id, node index, resume state, owner; the sim's accessors
// 0x450fe0 0x158f90 0x145770 0x15e850 0x5773e0), the thunks of the subobject vtables (0x577b17, 0x575c28), and the
// STL instantiations the replay table's std::vector members make (vector<TreeReplayRow> growth, assignment and the row
// destructor and copies: 0x575000 0x574df0 0x575950 0x575460 0x5754f0 0x575b00 0x4ebd10 0x4dffe0 0x4124c0; 0x575580 and
// 0x5758c0 are library matches). 0x577390 serves stack slots 8 and 9.
const HookSpec kHooks[] = {
    // cTSTreeSim
    {"cTSTreeSim_Construct", 0x5755e0, 32, 0x9217da82, MemberAddress(&cTSTreeSim::Construct)},
    {"cTSTreeSim_Destruct", 0x5759f0, 32, 0x1cd2c9fb, MemberAddress(&cTSTreeSim::Destruct)},
    {"cTSTreeSim_DeletingDestructor", 0x575c30, 32, 0xb95b248a, MemberAddress(&cTSTreeSim::DeletingDestructor)},
    {"cTSTreeSim_QueryInterface", 0x577af0, 32, 0x9eab77ab, MemberAddress(&cTSTreeSim::QueryInterface)},
    {"cTSTreeSim_vf5", 0x5761f0, 11, 0x73567cf4, MemberAddress(&cTSTreeSim::NewFrame)},
    {"cTSTreeSim_vf10", 0x577060, 32, 0xac8ac7d9, MemberAddress(&cTSTreeSim::GetPosition)},
    {"cTSTreeSim_vf11", 0x577e10, 32, 0x7898712d, MemberAddress(&cTSTreeSim::StartTree)},
    {"cTSTreeSim_vf12", 0x577460, 32, 0x20540770, MemberAddress(&cTSTreeSim::PushTree)},
    {"cTSTreeSim_vf13", 0x5762d0, 32, 0xf0d32f34, MemberAddress(&cTSTreeSim::DoNodeAction)},
    {"cTSTreeSim_vf14", 0x577810, 32, 0xf6046294, MemberAddress(&cTSTreeSim::ResumeFrame)},
    {"cTSTreeSim_vf15", 0x577e80, 32, 0xd19f0d1f, MemberAddress(&cTSTreeSim::CallTree)},
    {"cTSTreeSim_vf16", 0x5779d0, 27, 0xe011d2be, MemberAddress(&cTSTreeSim::PrepareStack)},
    {"cTSTreeSim_vf18", 0x5761d0, 7, 0x5b338253, MemberAddress(&cTSTreeSim::ClearErrorCode)},
    {"cTSTreeSim_vf19", 0x577220, 32, 0xa20bab52, MemberAddress(&cTSTreeSim::FindOwnerBaseFrame)},
    {"cTSTreeSim_vf20", 0x577040, 20, 0x418d2ca5, MemberAddress(&cTSTreeSim::GetTopFrame)},
    {"cTSTreeSim_vf21", 0x5772e0, 32, 0xc5ce8f52, MemberAddress(&cTSTreeSim::FindLastRealFrame)},
    {"cTSTreeSim_vf22", 0x577370, 18, 0x17cc95c3, MemberAddress(&cTSTreeSim::GetFrameAt)},
    {"cTSTreeSim_vf24", 0x5770e0, 32, 0xae10f63e, MemberAddress(&cTSTreeSim::GetCurrentOpcode)},
    {"cTSTreeSim_vf26", 0x5773f0, 5, 0x1021a142, MemberAddress(&cTSTreeSim::GetResult)},
    {"cTSTreeSim_vf27", 0x577e70, 7, 0x6905e0d1, MemberAddress(&cTSTreeSim::ClearResult)},
    {"cTSTreeSim_vf28", 0x5772b0, 11, 0x732bdacf, MemberAddress(&cTSTreeSim::ReturnedTrue)},
    {"cTSTreeSim_vf30", 0x577360, 6, 0x047f9eb6, MemberAddress(&cTSTreeSim::GetStepLimit)},
    {"cTSTreeSim_vf31", 0x5782d0, 12, 0xb1134390, MemberAddress(&cTSTreeSim::SetStepLimit)},
    {"cTSTreeSim_vf32", 0x5761e0, 11, 0x24be8b69, MemberAddress(&cTSTreeSim::ClearReplayCache)},
    {"cTSTreeSim_vf33", 0x5779b0, 6, 0x3aa3ebd3, MemberAddress(&cTSTreeSim::GetRunFlag)},
    {"cTSTreeSim_vf36", 0x577890, 32, 0x474f6f79, MemberAddress(&cTSTreeSim::RunToCompletion)},
    {"cTSTreeSim_vf37", 0x578450, 32, 0x531fe3b1, MemberAddress(&cTSTreeSim::Detach)},
    {"cTSTreeSim_RecordResult", 0x575ed0, 32, 0x9aaf6c1d, MemberAddress(&cTSTreeSim::RecordResult)},
    {"cTSTreeSim_ReplayResult", 0x576e70, 32, 0x15bb1574, MemberAddress(&cTSTreeSim::ReplayResult)},
    {"TreeReplayMap_Insert", 0x575280, 32, 0x327de7ba, MemberAddress(&TreeReplayMap::Insert)},
    {"TreeReplayMap_Rehash", 0x578680, 32, 0xa7ca523b, MemberAddress(&TreeReplayMap::Rehash)},
    {"TreeReplayList_FreeNodes", 0x575860, 32, 0xf4adeff5, MemberAddress(&TreeReplayList::FreeNodes)},
    {"TreeReplayMap_Clear", 0x5787b0, 32, 0xce401fa2, MemberAddress(&TreeReplayMap::Clear)},
    {"cTSObject_vf4_15", 0x53d2d0, 32, 0xc481cc22, MemberAddress(&TreeSimObject::CallTree)},
    {"cTSObject_vf4_36", 0x5315a0, 32, 0xdbd426ce, MemberAddress(&TreeSimObject::RunToCompletion)},
    {"TreeNodeParam_HasNoOperands", 0x577990, 18, 0xb713daea, MemberAddress(&TreeNodeParam::HasNoOperands)},
    {"cTSObject_vf246", 0x53e560, 32, 0x54ec477d, MemberAddress(&TreeRunNodeObject::RunNode)},
    // the trace log
    {"SimTraceLog_IsActive", 0x526fb0, 16, 0xc462d4ed, MemberAddress(&SimTraceLog::IsActive)},
    {"SimTraceLog_LogCall", 0x526e20, 32, 0x5bce43d5, MemberAddress(&SimTraceLog::LogCall)},
    {"SimTraceLog_LogErrored", 0x526be0, 32, 0x0dc93402, MemberAddress(&SimTraceLog::LogErrored)},
    {"SimTraceLog_LogNode", 0x5270a0, 32, 0xb9a59635, MemberAddress(&SimTraceLog::LogNode)},
    {"SimTraceLog_WriteIndent", 0x527540, 32, 0xfa3244b5, MemberAddress(&SimTraceLog::WriteIndent)},
    // cTSTreeStack
    {"cTSTreeStack_QueryInterface", 0x577b20, 32, 0xfb71e0e8, MemberAddress(&cTSTreeStack::QueryInterface)},
    {"cTSTreeStack_vf3", 0x577850, 32, 0x88c53a39, MemberAddress(&cTSTreeStack::Reset)},
    {"cTSTreeStack_vf4", 0x576200, 32, 0x4dea2022, MemberAddress(&cTSTreeStack::NewFrame)},
    {"cTSTreeStack_vf5", 0x577a70, 32, 0x12f579cf, MemberAddress(&cTSTreeStack::Push)},
    {"cTSTreeStack_vf6", 0x577a00, 32, 0xfa8d78ed, MemberAddress(&cTSTreeStack::Pop)},
    {"cTSTreeStack_vf7", 0x577e60, 8, 0xefc6c9b6, MemberAddress(&cTSTreeStack::Clear)},
    {"cTSTreeStack_vf8", 0x577390, 32, 0x2cb4e5e2, MemberAddress(&cTSTreeStack::GetAt)},
    {"cTSTreeStack_vf11", 0x5784c0, 32, 0x553a2336, MemberAddress(&cTSTreeStack::Write)},
    {"cTSTreeStack_vf12", 0x577b80, 32, 0xfcf520a4, MemberAddress(&cTSTreeStack::Read)},
    {"cTSTreeStack_vf16", 0x575d40, 32, 0x6e226b9d, MemberAddress(&cTSTreeStack::Destroy)},
    // cTSTreeStackElem
    {"cTSTreeStackElem_QueryInterface", 0x577b50, 32, 0x806ce07b, MemberAddress(&cTSTreeStackElem::QueryInterface)},
    {"cTSTreeStackElem_vf9", 0x577400, 22, 0x12a2e36d, MemberAddress(&cTSTreeStackElem::GetTreeId)},
    {"cTSTreeStackElem_vf10", 0x578420, 32, 0x3575f080, MemberAddress(&cTSTreeStackElem::SetTreeId)},
    {"cTSTreeStackElem_vf17", 0x578290, 10, 0x0ce5832f, MemberAddress(&cTSTreeStackElem::SetFlag16)},
    {"cTSTreeStackElem_vf22", 0x576e50, 21, 0xba0d41a2, MemberAddress(&cTSTreeStackElem::IsPaused)},
    {"cTSTreeStackElem_vf23", 0x578260, 32, 0x789c47e2, MemberAddress(&cTSTreeStackElem::SetPaused)},
    {"cTSTreeStackElem_vf25", 0x5782a0, 10, 0xc951abc0, MemberAddress(&cTSTreeStackElem::SetByte1e)},
    {"cTSTreeStackElem_vf28", 0x578350, 32, 0xb5b3bad8, MemberAddress(&cTSTreeStackElem::SetArgCount)},
    {"cTSTreeStackElem_vf29", 0x5782e0, 32, 0x0131b38d, MemberAddress(&cTSTreeStackElem::SetLocalCount)},
    {"cTSTreeStackElem_vf30", 0x5773c0, 27, 0xf3947feb, MemberAddress(&cTSTreeStackElem::GetArg)},
    {"cTSTreeStackElem_vf31", 0x5772c0, 27, 0x8a40251d, MemberAddress(&cTSTreeStackElem::GetLocal)},
    {"cTSTreeStackElem_vf32", 0x5783c0, 27, 0x3f48385d, MemberAddress(&cTSTreeStackElem::SetArg)},
    {"cTSTreeStackElem_vf33", 0x5783e0, 32, 0x8426bb87, MemberAddress(&cTSTreeStackElem::SetArgs)},
    {"cTSTreeStackElem_vf34", 0x5782b0, 19, 0x9ea72356, MemberAddress(&cTSTreeStackElem::SetLocal)},
    {"cTSTreeStackElem_vf35", 0x5779f0, 13, 0x45f15cf4, MemberAddress(&cTSTreeStackElem::GetArgAddress)},
    {"cTSTreeStackElem_vf36", 0x5779c0, 13, 0x5bdeebbd, MemberAddress(&cTSTreeStackElem::GetLocalAddress)},
    {"cTSTreeStackElem_vf37", 0x577420, 32, 0x29caada4, MemberAddress(&cTSTreeStackElem::GetTreeName)},
    {"cTSTreeStackElem_vf38", 0x578520, 32, 0xa9b38c01, MemberAddress(&cTSTreeStackElem::Write)},
    {"cTSTreeStackElem_vf39", 0x577cf0, 32, 0x0e35a3fd, MemberAddress(&cTSTreeStackElem::Read)},
    {"cTSTreeStackElem_vf42", 0x575d70, 32, 0x89c799e7, MemberAddress(&cTSTreeStackElem::Destroy)},
};
REGISTER_HOOKS(kHooks);

} // namespace
