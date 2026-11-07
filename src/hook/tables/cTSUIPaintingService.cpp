// Hooks for cTSUIPaintingService (src/ui/cTSUIPaintingService.size.cpp), lane KS: the standalone shape. The creator
// 0x6c1e61 (the cTSUICOMDirector's factory for the service) is the only caller of the constructor 0x6c0f3d, and the
// vtables are stored only by that constructor and the destructor 0x6c0ff9: with this hook every service carries our
// vtables, and every slot, the constructor and the destructors are in effect through callers (trapped by
// tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "ui/cTSUIPaintingService.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSUIPaintingService_Create", 0x6c1e61, 32, 0xbea92ad5, reinterpret_cast<void *>(&cTSUIPaintingService::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
