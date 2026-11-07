// Hooks for cInputEditFilter (src/gz/cInputEditFilter.cpp), lane MC. Its builder 0xcaaed0 is not ours, so the
// objects carry the game's vtables 0xeb5e4c / 0xeb5e60: the class's own bodies are hooked at their RVAs (the deleting
// destructor's thunk 0xca9fd9 jumps to 0xcaa030).
#include "gz/cInputEditFilter.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cInputEditFilter_QueryInterface", 0xcabbc0, 32, 0xf2882c05, MemberAddress(&cInputEditFilter::QueryInterface)},
    {"cInputEditFilter_Filter", 0xcaa120, 32, 0x17756c83, MemberAddress(&cInputEditFilter::Filter)},
    {"cInputEditFilter_DeletingDestructor", 0xcaa030, 32, 0x6c650096, MemberAddress(&cInputEditFilter::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
