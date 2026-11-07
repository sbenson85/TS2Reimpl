#include "hook/hooks.h"
#include "rz/StaticGetters.h"

namespace {

// 0x14491b, 0x144897, 0x1adffa and 0x1ae048 moved to COMDirectors (lane EO: the directors in the standalone shape).
const HookSpec kHooks[] = {
    {"StaticGetters_GetDirectorFramework", 0x1ae18d, 12, 0x38ca2de4, reinterpret_cast<void *>(&GetDirectorFrameworkObject)},
};
REGISTER_HOOKS(kHooks);

} // namespace
