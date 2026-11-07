#include "hook/hooks.h"
#include "app/ProductInfo.h"

namespace {

// Lane MB (F4e): the product helpers, __cdecl free functions (src/app/ProductInfo.h). Every caller in the image is ours
// or one of these.
const HookSpec kHooks[] = {
    {"product_GetProductPaths", 0xcb4690, 32, 0x86a4560d, reinterpret_cast<void *>(&product::GetProductPaths)},
    {"product_ListProductExecutables", 0xcb4aa0, 32, 0x3a4a1839, reinterpret_cast<void *>(&product::ListProductExecutables)},
    {"product_InstalledProductDirectories", 0xcb4bb0, 32, 0x4dd718df, reinterpret_cast<void *>(&product::InstalledProductDirectories)},
    {"product_NewestProductExecutable", 0xcb4950, 32, 0x1ccbeae5, reinterpret_cast<void *>(&product::NewestProductExecutable)},
    {"product_FindNewerProduct", 0xcb4cc0, 32, 0x52668346, reinterpret_cast<void *>(&product::FindNewerProduct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
