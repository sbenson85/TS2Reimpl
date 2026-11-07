#include "hook/hooks.h"
#include "misc/UtilitiesB.h"

namespace {

const HookSpec kHooks[] = {
    {"UtilitiesB_SetInvalidIndex", 0x18bf80, 9, 0xa97adb54, reinterpret_cast<void *>(&UtilitiesB_SetInvalidIndex)},
};
REGISTER_HOOKS(kHooks);

} // namespace
