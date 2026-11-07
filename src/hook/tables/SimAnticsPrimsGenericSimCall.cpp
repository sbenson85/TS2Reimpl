#include "hook/hooks.h"
#include "simantics/PrimGenericSimCall.h"

namespace {

// SimAntics primitive handler 0x01, GenericSimCall (called from cTSObject::vf332's switch), lane Z/Z2.
// db/ records 367 bytes for 0x58bc50; the real function runs to 0x5986ee (about 52 KB), so the entry's size here is the
// 32-byte CRC window only.
const HookSpec kHooks[] = {
    {"Prim_01_GenericSimCall", 0x58bc50, 32, 0x87ded2ee, MemberAddress(&PrimGscObject::GenericSimCall)},
};
REGISTER_HOOKS(kHooks);

} // namespace
