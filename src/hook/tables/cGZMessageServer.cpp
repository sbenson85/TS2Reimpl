// cGZMessageServer (lane KU, src/gz/cGZMessageServer.h): the creator the cTSAppFWCOMDirector registers. Every message
// server is ours; its constructor, destructor, slots and helpers are in effect through it.
#include "gz/cGZMessageServer.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZMessageServer_Create", 0xc751d0, 25, 0xfa3bfe19, reinterpret_cast<void *>(&cGZMessageServer::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
