// cNeighborhoodThumbnailFilter (standalone shape): the constructor builds ours, so every slot is ours through our vtable
// (no slot adapters).
#include "hook/hooks.h"
#include "ui/cNeighborhoodThumbnailFilter.h"

namespace {

const HookSpec kHooks[] = {
    {"cNeighborhoodThumbnailFilter_Construct", 0x725594, 32, 0x1e984751,
     reinterpret_cast<void *>(&cNeighborhoodThumbnailFilterAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
