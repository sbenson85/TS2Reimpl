#include "hook/hooks.h"
#include "misc/UtilitiesE.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0b65860", 0xb65860, 11, 0x48e18994, MemberAddress(&TSLuaWrapperBase::Destruct)},
    {"FUN_a0ae8900", 0xae8900, 32, 0x08115638, reinterpret_cast<void *>(&GetCachedQueriedObject)},
    {"FUN_a0b6dfb0", 0xb6dfb0, 32, 0x16fbac8f, reinterpret_cast<void *>(&NotifySlot6ResultWithKey)},
    {"FUN_a0b6dfe0", 0xb6dfe0, 32, 0xe6670983, reinterpret_cast<void *>(&NotifySlot6ResultWithKeyWords)},
    {"GetMaterialParserCached", 0xae8980, 32, 0x7c0594d8, reinterpret_cast<void *>(&GetMaterialParserCached)},
};
REGISTER_HOOKS(kHooks);

} // namespace
