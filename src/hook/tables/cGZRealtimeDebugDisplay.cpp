// cGZRealtimeDebugDisplay (lanes LA / LB, src/gz/cGZRealtimeDebugDisplay.h): the COM class factory 0x92f5ed (in the
// cGZWinCOMDirector's class table). The constructor 0x9704bb's only other caller, cTSAppFWCOMDirector::OnStart 0xc2beec,
// is ours and builds ours (cGZRealtimeDebugDisplayAdapters::Construct), so every realtime debug display is ours (and
// through its add slots every cGZRTDEntry and cGZRTDValueEntry); the constructor, destructors, slots and the entries'
// functions are in effect through the factory.
#include "hook/hooks.h"
#include "gz/cGZRealtimeDebugDisplay.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZRealtimeDebugDisplay_Create", 0x92f5ed, 32, 0xc55f7fa0, reinterpret_cast<void *>(&cGZRealtimeDebugDisplay::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
