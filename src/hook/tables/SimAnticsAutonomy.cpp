#include "hook/hooks.h"
#include "simantics/AutonomyHelpers.h"
#include "simantics/BconTable.h"
#include "simantics/EdithObjectTestSim.h"
#include "simantics/FindBestAction.h"
#include "simantics/PrimPerson.h"
#include "simantics/SimTraceLog.h"

namespace {

// The person's FindBestAction (opcode 0x03) machinery and the helpers it reaches (lane AF).
const HookSpec kHooks[] = {
    {"GetBconTable", 0x54a770, 32, 0xa987e95d, reinterpret_cast<void *>(&GetBconTable)},
    {"GetBconValue", 0x54a6b0, 32, 0x98ed8718, reinterpret_cast<void *>(&GetBconValue)},
    {"GetStackObjectScoreFactor", 0x512860, 32, 0x4670e1d2, reinterpret_cast<void *>(&GetStackObjectScoreFactor)},
    {"InteractionTreeHasFlags", 0x516e00, 32, 0x95c6957f, reinterpret_cast<void *>(&InteractionTreeHasFlags)},
    {"SearchSnapshot_Assign", 0x4365d0, 32, 0xfbfc86c2, MemberAddress(&SearchSnapshot::Assign)},
    {"CompareActionRecords", 0x512d70, 32, 0x1720c707, reinterpret_cast<void *>(&CompareActionRecords)},
    {"CompareScoreRecords", 0x512db0, 32, 0x8ace4730, reinterpret_cast<void *>(&CompareScoreRecords)},
    {"cTSPerson_ChooseAction", 0x5150d0, 32, 0x299690b9, MemberAddress(&PrimPersonObject::ChooseAction)},
    {"cTSPerson_CheckInteraction", 0x515c80, 32, 0x0df2cbd2, MemberAddress(&PrimPersonObject::CheckInteraction)},
    {"cEdithObjectTestSim_vf4_SetPerson", 0x4510b0, 32, 0xccae4e21, MemberAddress(&cEdithObjectTestSim::SetPerson)},
    {"SetActionStringShown", 0x4529b0, 10, 0x9d7c1add, reinterpret_cast<void *>(&SetActionStringShown)},
    {"WasActionStringShown", 0x452690, 6, 0x79d6f17f, reinterpret_cast<void *>(&WasActionStringShown)},
    {"cTSPerson_ScoreInteraction", 0x516160, 32, 0xfb55776b, MemberAddress(&PrimPersonObject::ScoreInteraction)},
    {"cTSPerson_ScorePending", 0x516c40, 32, 0xf9c7c3b7, MemberAddress(&PrimPersonObject::ScorePending)},
    {"cTSPerson_BeginSearch", 0x515940, 32, 0xf9e22608, MemberAddress(&PrimPersonObject::BeginSearch)},
    {"SimTraceLog_LogSearchStart", 0x5273b0, 32, 0xa9d2e24b, MemberAddress(&SimTraceLog::LogSearchStart)},
    {"cTSPerson_StartChosenAction", 0x515270, 32, 0x349d91f1, MemberAddress(&PrimPersonObject::StartChosenAction)},
    {"SimTraceLog_CloseSink", 0x526b40, 30, 0x12bb0538, MemberAddress(&SimTraceLog::CloseSink)},
};
REGISTER_HOOKS(kHooks);

} // namespace
