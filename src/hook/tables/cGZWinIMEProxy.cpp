// cGZWinIMEProxy (lane KN, src/gz/cGZWinIMEProxy.h): the factory the cGZWinCOMDirector registers (class 0x2ac4516d).
// Every proxy is ours; the constructor, destructor and slots are in effect through it.
#include "gz/cGZWinIMEProxy.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZWinIMEProxy_Create", 0x92f6aa, 22, 0x4b078512, reinterpret_cast<void *>(&cGZWinIMEProxy::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
