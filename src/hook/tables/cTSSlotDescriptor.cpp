#include "hook/hooks.h"
#include "ts/cTSSlotDescriptor.h"

namespace {
const HookSpec kHooks[] = {
    {"cTSSlotDescriptor_Read", 0x4dc020, 32, 0x36be3a51, MemberAddress(&cTSSlotDescriptor::Read)},
    {"cTSSlotDescriptor_Write", 0x4dcae0, 32, 0x0157903a, MemberAddress(&cTSSlotDescriptor::Write)},
};
REGISTER_HOOKS(kHooks);
}
