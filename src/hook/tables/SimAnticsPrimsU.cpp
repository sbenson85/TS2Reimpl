#include "hook/hooks.h"
#include "simantics/OperandReader.h"

namespace {

// The SimAntics operand reader (cTSObject secondary slot 7) and its private helpers, lane U.
const HookSpec kHooks[] = {
    {"OperandReader_Read", 0x531700, 32, 0x3b27eb73, MemberAddress(&SimOperandSource::Read)},
    {"OperandReader_BindElement", 0x53d9c0, 32, 0xe34087cb, MemberAddress(&OperandRef::Bind)},
    {"OperandReader_MakeFromBase", 0x52ccc0, 32, 0x0ec00295, MemberAddress(&OperandRef::MakeFromBase)},
    {"OperandReader_RoundHalfAway", 0x53f7a0, 32, 0xc608c875, reinterpret_cast<void *>(&RoundHalfAway)},
    {"OperandReader_BindObjectData", 0x53f7d0, 32, 0x1502bc54, reinterpret_cast<void *>(&BindObjectData)},
};
REGISTER_HOOKS(kHooks);

} // namespace
