// cGZSndSys (lane KK, src/gz/cGZSndSys.h): the factory (every sound system is ours; the rest is in effect through it).
// 0xb22378 is a manual entry in db/missed-functions.jsonl (the director only pushes its address).
#include "gz/cGZSndSys.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZSndSys_Create", 0xb22378, 25, 0xab989902, reinterpret_cast<void *>(&cGZSndSys::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
