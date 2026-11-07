// cGZWinProcMgr (lane KM, src/gz/cGZWinProcMgr.h): the factory the COM director registers (class 0x77aa34e6). Every
// manager is ours; the constructor, destructor and slots are in effect through it.
#include "gz/cGZWinProcMgr.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZWinProcMgr_Create", 0xcc70c0, 32, 0x94c031c3, reinterpret_cast<void *>(&cGZWinProcMgr::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
