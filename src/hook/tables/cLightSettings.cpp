#include "hook/hooks.h"
#include "sg/cLightSettings.h"

namespace {

// The light settings constructor (our compiler's, by its /alternatename name in src/sg/cLightSettings.cpp). Its only
// caller is 0x2da270 (the lighting parser state's), so every light settings object carries our vtables and the slots,
// the helpers and the destructor are in effect through it.
const HookSpec kHooks[] = {
    {"nTSSG_cLightSettings_Construct", 0x3c6a30, 32, 0x37cf68e0, const_cast<uint8_t *>(&cLightSettings_Constructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
