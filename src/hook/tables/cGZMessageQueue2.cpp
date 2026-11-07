#include "hook/hooks.h"
#include "gz/cGZMessageQueue.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZMessageQueue2_Construct", 0xc66b0d, 32, 0x3f8d2d2f, reinterpret_cast<void *>(&cGZMessageQueueAdapters::Construct2)},
    {"cGZMessageQueue2_Destruct", 0xc66c91, 32, 0x597873e5, reinterpret_cast<void *>(&cGZMessageQueueAdapters::Destruct2)},
};
REGISTER_HOOKS(kHooks);

} // namespace
