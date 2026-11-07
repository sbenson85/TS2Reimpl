#include "hook/hooks.h"
#include "ts/ObjectSaveTypeTable.h"

namespace {

// Lane MB (F4e): GUIDTranslation::Resolve, a non-virtual thiscall member (src/ts/ObjectSaveTypeTable.h). Its only caller,
// ObjectSaveTypeTable::Read 0x4d4c30, is ours (the save table's game slots are reached by nothing), so the hook is for
// the record: no original code calls it.
const HookSpec kHooks[] = {
    {"GUIDTranslation_Resolve", 0xc0b617, 32, 0x494c135e, MemberAddress(&GUIDTranslation::Resolve)},
};
REGISTER_HOOKS(kHooks);

} // namespace
