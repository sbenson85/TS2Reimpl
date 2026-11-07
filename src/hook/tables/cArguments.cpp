#include "hook/hooks.h"
#include "gz/cArguments.h"

// Standalone shape (docs/standalone-shape.md): the constructor (0xb0be60) and copy constructor (0xb0bdd0) hooks build our
// class, so every object the game makes carries our vtable; the vtable 0xe7f84c is referenced only by those two, the
// destructor and the deleting destructor. The slot adapters serve objects carrying the game's vtable (none once the
// constructor hooks are in). Slot 8 is folded (0x149cf0, not hooked).
namespace {

using A = nGZCommandParser::cArgumentsAdapters;

const HookSpec kHooks[] = {
    {"cArguments_Construct", 0xb0be60, 32, 0x4fe1c917, reinterpret_cast<void *>(&A::Construct)},
    {"cArguments_ConstructCopy", 0xb0bdd0, 32, 0x41b1f066, reinterpret_cast<void *>(&A::ConstructCopy)},
    {"cArguments_Assign", 0xb0c000, 32, 0x81575955, reinterpret_cast<void *>(&A::Assign)},
    {"cArguments_Destruct", 0xb0bf40, 32, 0x949dfc9d, reinterpret_cast<void *>(&A::Destruct)},
    {"cArguments_vf16", 0xb0c320, 32, 0x949dfc9d, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cArguments_vf0", 0xb0c720, 32, 0x262fa46a, reinterpret_cast<void *>(&A::Parse)},
    {"cArguments_vf1", 0xb0cc70, 32, 0xa9bf2508, reinterpret_cast<void *>(&A::TakeSwitch)},
    {"cArguments_vf2", 0xb0cbb0, 32, 0xcb11e0b6, reinterpret_cast<void *>(&A::TakeSwitchArguments)},
    {"cArguments_vf3", 0xb0cb90, 23, 0x70723fa6, reinterpret_cast<void *>(&A::TakeSwitchArgumentsExactly)},
    {"cArguments_vf4", 0xb0c2a0, 13, 0xc7958834, reinterpret_cast<void *>(&A::Argument)},
    {"cArguments_vf5", 0xb0c410, 28, 0xcbe6424c, reinterpret_cast<void *>(&A::GetAll)},
    {"cArguments_vf6", 0xb0c3f0, 20, 0x81a59f14, reinterpret_cast<void *>(&A::ArgumentLength)},
    {"cArguments_vf7", 0xb0c6d0, 11, 0x03c023bd, reinterpret_cast<void *>(&A::Count)},
    {"cArguments_vf9", 0xb0c6e0, 10, 0x30cd3449, reinterpret_cast<void *>(&A::SwitchCount)},
    {"cArguments_vf10", 0xb0c650, 32, 0xbcee0f9e, reinterpret_cast<void *>(&A::Positional)},
    {"cArguments_vf11", 0xb0c630, 19, 0x39fb556c, reinterpret_cast<void *>(&A::PositionalExactly)},
    {"cArguments_vf12", 0xb0c550, 32, 0xedb14456, reinterpret_cast<void *>(&A::HasSwitch)},
    {"cArguments_vf13", 0xb0c6f0, 32, 0xb8d01cd5, reinterpret_cast<void *>(&A::UnusedSwitchCount)},
    {"cArguments_vf14", 0xb0cce0, 32, 0xfd7ae3ad, reinterpret_cast<void *>(&A::GetUnusedSwitches)},
    {"cArguments_vf15", 0xb0c430, 32, 0x816d00a3, reinterpret_cast<void *>(&A::CheckSwitchesUsed)},
};
REGISTER_HOOKS(kHooks);

} // namespace
