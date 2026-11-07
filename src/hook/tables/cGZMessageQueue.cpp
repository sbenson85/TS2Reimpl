#include "hook/hooks.h"
#include "gz/cGZMessageQueue.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZMessageQueue_Construct", 0xc74c32, 32, 0x913dbbc3, reinterpret_cast<void *>(&cGZMessageQueueAdapters::Construct)},
    {"cGZMessageQueue_Destruct", 0xc74cea, 32, 0x13aa2559, reinterpret_cast<void *>(&cGZMessageQueueAdapters::Destruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
