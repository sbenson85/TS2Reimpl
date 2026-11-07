// cGZWinCtrlMgr (lane KO, src/gz/cGZWinCtrlMgr.h): the factory the COM director registers (class 0xc2c2eb0f). Every
// manager is ours; the constructor, destructors and slots are in effect through it.
#include "gz/cGZWinCtrlMgr.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZWinCtrlMgr_Create", 0x92f732, 25, 0x0fc7c1f8, reinterpret_cast<void *>(&cGZWinCtrlMgr::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
