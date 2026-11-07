#include "hook/hooks.h"
#include "ts/cTSString.h"

namespace {

using Construct5 = cTSString *(cTSString::*)(uint16_t, uint16_t, uint32_t, uint8_t, uint32_t);
using Construct4 = cTSString *(cTSString::*)(uint16_t, uint16_t, uint32_t, uint32_t);

// The whole cTSString class (lane AE: the two members that need no resource system; lane AG: the loading constructors, the
// loader, the deleting destructor and the resource manager getter the loader asks). Slot 16 (0x1d1050) is a jmp into
// cRZString::operator=, which is hooked itself.
const HookSpec kHooks[] = {
    {"cTSString_ConstructEmpty", 0x1d0fb0, 32, 0x01270ade, MemberAddress(&cTSString::ConstructEmpty)},
    {"cTSString_Assign", 0x1d1000, 32, 0xd66f09f4, MemberAddress(&cTSString::Assign)},
    {"cTSString_Construct", 0x1d0df0, 32, 0xd7fa6496, MemberAddress(static_cast<Construct5>(&cTSString::Construct))},
    {"cTSString_ConstructNoFlags", 0x1d0e90, 32, 0xd7fa6496, MemberAddress(static_cast<Construct4>(&cTSString::Construct))},
    {"TSStringLoad", 0x1d10d0, 32, 0xbc3dff70, MemberAddress(&cTSString::Load)},
    {"cTSString_vf33", 0x1d1060, 32, 0x864be6c7, MemberAddress(&cTSString::DeletingDestructor)},
    {"FUN_a09c5b26", 0x9c5b26, 32, 0x85cc45c6, reinterpret_cast<void *>(&tsstring::GetResourceManager)},
};
REGISTER_HOOKS(kHooks);

} // namespace
