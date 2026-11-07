// cGZSndMgr (lane KJ, src/gz/cGZSndMgr.h): the factory (every manager is ours; the rest is in effect through it). 0xb22333
// is a manual entry in db/missed-functions.jsonl (the director only pushes its address).
#include "gz/cGZSndMgr.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZSndMgr_Create", 0xb22333, 25, 0x995a0695, reinterpret_cast<void *>(&cGZSndMgr::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
