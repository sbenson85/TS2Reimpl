#include "hook/hooks.h"
#include "simantics/PersonVectors.h"

namespace {

// The library vector workers of the person's ActionRecord vectors (src/simantics/PersonVectors.cpp).
const HookSpec kHooks[] = {
    {"ActionRecordsReallocateExactly", 0x50f560, 32, 0xdb726eda, reinterpret_cast<void *>(&ActionRecordsReallocateExactly)},
};
REGISTER_HOOKS(kHooks);

} // namespace
