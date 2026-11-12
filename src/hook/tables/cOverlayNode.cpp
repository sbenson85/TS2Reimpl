#include "hook/hooks.h"
#include "sg/cOverlayNode.h"

namespace {

using A = nTSSG::cOverlayNodeAdapters;

// The constructor (cFloor and cLotSkirt make the node through it) and the destructor. The slots are ours through the
// vtables the constructor sets (db/status/cOverlayNode.jsonl).
const HookSpec kHooks[] = {
    {"cOverlayNode_Construct", 0x3ae470, 32, 0x0d17a565, reinterpret_cast<void *>(&A::Construct)},
    {"cOverlayNode_Destruct", 0x3ae890, 32, 0x2a2a32bc, reinterpret_cast<void *>(&A::Destruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
