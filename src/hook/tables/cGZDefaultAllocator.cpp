#include "gz/cGZDefaultAllocator.h"
#include "hook/hooks.h"

namespace {

// cGZDefaultAllocator in the standalone shape (lane HV): the entries are adapters (docs/standalone-shape.md). The game
// builds every instance inline (0x85a19a, 0x85bf91, the static at 0xff2464), so its objects carry the game's vtable
// and these serve them. Not hooked: Release 0x1be540, AddRef 0x148fb0 and Allocate 0x1be4c7 (folded with other
// classes), Reallocate 0x1be4e8 (listed as library code); Deallocate 0x1be4d4 is hooked by RZMemory's table.
using A = cGZDefaultAllocatorAdapters;
const HookSpec kHooks[] = {
    {"cGZDefaultAllocator_QueryInterface", 0x1be4fa, 32, 0x29d98c8c, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cGZDefaultAllocator_DeletingDestructor", 0x1be557, 32, 0xa8369644, reinterpret_cast<void *>(&A::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
