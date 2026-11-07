#include "hook/hooks.h"
#include "app/AppDebugger.h"
#include "rz/cRZThreadRelease.h"

namespace {

using namespace appdebugger;

// The log setup of AddApplicationServicesHere (0xc29d5a) and what it calls. Not run in-game yet. 0x1b4fab
// (the cRZTextOStream constructor) is matched to libconcrt by name in db/names.jsonl, so it is not hooked:
// the code above calls its own copy. 0x1b4f60 has a byte-identical twin (db/families.jsonl) that is not hooked.
const HookSpec kHooks[] = {
    {"LogServiceHolder_Acquire", 0x1bf8fb, 32, 0x1a582073, MemberAddress(&LogServiceHolder::Acquire)},
    // 0xc9c2e2, 0xc9f372, 0xc9b998 and 0x1b9cad are hooked by the nGZLog table (its implementations
    // own those classes); the copies here are used only by SetupLogging. TODO: fold them into nGZLog.
    {"OStreamBase_ConstructOStream", 0x1b4f60, 21, 0xae5613c8, MemberAddress(&OStreamBase::ConstructOStream)},
    {"cTSWinCheatConsole_Construct", 0xca9bf0, 32, 0x05ae0beb, MemberAddress(&cTSWinCheatConsole::Construct)},
    {"AllocateListNodes", 0x1e7190, 32, 0x5c93ffe2, reinterpret_cast<void *>(&AllocateListNodes)},
    {"FreeStringBuffer", 0x152b80, 32, 0xd7d77435, reinterpret_cast<void *>(&FreeStringBuffer)},
    {"DestroyStringListNodes", 0x1d5ea0, 32, 0x5b36c1bf, reinterpret_cast<void *>(&DestroyStringListNodes)},
    {"SetCheatServiceA", 0xcb0640, 13, 0x6425ee03, reinterpret_cast<void *>(&SetCheatServiceA)},
    {"SetCheatServiceB", 0xcb0650, 13, 0x2ef351bb, reinterpret_cast<void *>(&SetCheatServiceB)},
    {"SetCheatName", 0xcb0660, 32, 0xb966f11d, reinterpret_cast<void *>(&SetCheatName)},
    {"LoadCheatServices", 0xcb0760, 32, 0xd2258f6b, reinterpret_cast<void *>(&LoadCheatServices)},
    {"PrepareCheatGlobal", 0xcb0690, 32, 0x086cefc1, reinterpret_cast<void *>(&PrepareCheatGlobal)},
    {"OpenCheatFile", 0xcaf560, 32, 0xac44abb2, reinterpret_cast<void *>(&OpenCheatFile)},
    {"LoadCheatFile", 0xcaf060, 32, 0x09ea0210, reinterpret_cast<void *>(&LoadCheatFile)},
    {"LoadLogSettingsCheats", 0xcaf090, 32, 0x7acfaafe, reinterpret_cast<void *>(&LoadLogSettingsCheats)},
    {"SetupLogging", 0xc29d5a, 32, 0x9e6e8f62, reinterpret_cast<void *>(&SetupLogging)},
    // The cRZThread destructor chain (0x1b9b71 is 14 bytes, a tail jump into 0x1b9e7b).
    {"cRZThread_Destroy", 0x1b9b71, 14, 0x6ea43238, reinterpret_cast<void *>(&DestroyThread)},
    {"cRZThreadState_Release", 0x1b9e7b, 32, 0x835ba53f, reinterpret_cast<void *>(&ReleaseThreadState)},
    {"cRZThreadState_Destroy", 0x1b9b47, 32, 0xbe711878, reinterpret_cast<void *>(&DestroyThreadState)},
};
REGISTER_HOOKS(kHooks);

} // namespace
