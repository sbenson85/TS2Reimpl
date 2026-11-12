#include "hook/hooks.h"
#include "sg/cBlurBrush.h"

namespace {

using A = nTSSG::cBlurBrushAdapters;

// The constructor (both sites that make the brush call it) and the destructor. The slots are ours through the vtable the
// constructor sets (db/status/cBlurBrush.jsonl).
const HookSpec kHooks[] = {
    {"cBlurBrush_Construct", 0x3c9e50, 32, 0x1b5c7f2e, reinterpret_cast<void *>(&A::Construct)},
    {"cBlurBrush_Destruct", 0x3c9f40, 32, 0xe8b937da, reinterpret_cast<void *>(&A::Destruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
