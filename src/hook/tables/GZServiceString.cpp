// GZServiceString (lane LS, src/std/GZServiceString.h): the service-allocator string's buffer allocation.
#include "hook/hooks.h"
#include "std/GZServiceString.h"

namespace {

const HookSpec kHooks[] = {
    {"GZServiceString_AllocateForCapacity", 0x177110, 32, 0x5c93ffe2,
     reinterpret_cast<void *>(&GZServiceString::AllocateForCapacity)},
};
REGISTER_HOOKS(kHooks);

} // namespace
