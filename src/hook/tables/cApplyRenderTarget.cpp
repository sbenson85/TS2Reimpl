// Hooks for cApplyRenderTarget (src/sg/cApplyRenderTarget.h): the factory, which makes ours. The image's constructor,
// destructor, slot bodies and the helpers only they call are in effect through it (only the factory constructs the
// class, so only our objects, with our vtable, exist).
#include "hook/hooks.h"
#include "sg/cApplyRenderTarget.h"

using namespace nGZSceneGraph;

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cApplyRenderTarget_Create", 0xac31a0, 32, 0x30c66363, reinterpret_cast<void *>(&cApplyRenderTarget::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
