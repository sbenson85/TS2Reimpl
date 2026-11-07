#include "hook/hooks.h"
#include "simantics/EdithObjectTestSim.h"
#include "simantics/PrimPerson.h"
#include "simantics/ServiceRefVector.h"

namespace {

// The gather step of FindBestAction (opcode 0x03): the person's GatherInteractions, the interaction test sim's gather members, the
// cheat interaction, and the vector helpers they use (lane AH).
const HookSpec kHooks[] = {
    {"cTSPerson_GatherInteractions", 0x5155b0, 32, 0xd193e231, MemberAddress(&PrimPersonObject::GatherInteractions)},
    {"cEdithObjectTestSim_vf6_AppendInteractions", 0x44fb90, 32, 0x75d87691, MemberAddress(&cEdithObjectTestSim::AppendInteractions)},
    {"cEdithObjectTestSim_AppendAutonomousInteractions", 0x44fbc0, 32, 0xaf55f0b3, MemberAddress(&cEdithObjectTestSim::AppendAutonomousInteractions)},
    {"cEdithObjectTestSim_AppendInteractionsForMenu", 0x44ff80, 32, 0xe72742f3, MemberAddress(&cEdithObjectTestSim::AppendInteractionsForMenu)},
    {"cEdithObjectTestSim_AppendIfPasses", 0x4519c0, 32, 0x95b565db, MemberAddress(&cEdithObjectTestSim::AppendIfPasses)},
    {"AddCheatInteraction", 0x44f760, 32, 0x91110630, reinterpret_cast<void *>(&AddCheatInteraction)},
    {"ServiceRefVector_InsertGrown", 0x1d57e0, 32, 0x9be05d51, MemberAddress(&ServiceRefVector::InsertGrown)},
    {"ServiceRefVector_ReleaseAndFree", 0x38f140, 32, 0x8d451bbc, MemberAddress(&ServiceRefVector::ReleaseAndFree)},
    {"ServiceBlockVector_FreeBlock", 0x195720, 32, 0xe1dbaabe, MemberAddress(&ServiceBlockVector::FreeBlock)},
    {"FUN_a041d6e0", 0x41d6e0, 32, 0x980241e3, MemberAddress(&ServiceRefVector::AssignCounted)},
};
REGISTER_HOOKS(kHooks);

} // namespace
