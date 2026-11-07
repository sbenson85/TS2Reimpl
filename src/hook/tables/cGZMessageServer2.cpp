// cGZMessageServer2 (lane KU, src/gz/cGZMessageServer2.h): the creator the cRZMessageCOMDirector registers. Every second
// message server is ours; its constructor, destructor, slots and helpers are in effect through it.
#include "gz/cGZMessageServer2.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZMessageServer2_Create", 0xc673ce, 32, 0x482ff090, reinterpret_cast<void *>(&cGZMessageServer2::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
