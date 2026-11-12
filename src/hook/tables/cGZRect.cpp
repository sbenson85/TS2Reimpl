#include "gz/cGZRect.h"
#include "hook/hooks.h"

namespace {

// cGZRect's intersection (lane 261108s): its callers outside our code (the gz draw contexts' remaining original
// functions, the window code 0x92c6b0 / 0x97d406) reach ours through the hook; ours call GZRectIntersect directly.
const HookSpec kHooks[] = {
    {"cGZRect_Intersect", 0x373520, 32, 0x42ee5cd9, reinterpret_cast<void *>(&cGZRectAdapters::Intersect)},
};
REGISTER_HOOKS(kHooks);

} // namespace
