#include "app/AppCtor.h"
#include "hook/hooks.h"

namespace {

// The teardown helpers of the application's containers and of string lists. The application's own entries (constructor,
// destructors) are adapters in src/hook/tables/cTSAppFW.cpp (lane FG).
const HookSpec kHooks[] = {
    {"AppCtor_EraseSubtree", 0xca49f0, 32, 0xabb34818, reinterpret_cast<void *>(&appctor::EraseSubtree)},
    {"AppCtor_DestroyListNodes", 0x1d5e50, 32, 0xd2687d3e, reinterpret_cast<void *>(&appctor::DestroyListNodes)},
    {"AppCtor_DestroyElement", 0x1d8450, 32, 0xaf071cb7, reinterpret_cast<void *>(&appctor::DestroyElement)},
};
REGISTER_HOOKS(kHooks);

} // namespace
