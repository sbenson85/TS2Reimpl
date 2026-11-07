#include "hook/hooks.h"
#include "simantics/PrimPerson.h"

namespace {

// cTSPerson::vf198 (src/simantics/PersonInit.cpp).
const HookSpec kHooks[] = {
    {"cTSPerson_vf198_Initialise", 0x51da50, 32, 0x27e5aa10, MemberAddress(&PrimPersonObject::Initialise198)},
};
REGISTER_HOOKS(kHooks);

} // namespace
