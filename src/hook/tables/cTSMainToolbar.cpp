#include "hook/hooks.h"
#include "ui/cTSMainToolbar.h"

namespace {

// The creator is the class's switch: registered by address by the cTSUICOMDirector constructor, the only place the game
// makes one; the constructor (its only caller) is hooked too. With them on every toolbar carries our vtables; the slots,
// the destructor and the deleting destructor have no entry: only the game's vtables reach their originals, trapped under
// Hooks.cTSMainToolbar_Construct=1 ("in_effect": "callers").
const HookSpec kHooks[] = {
    {"cTSMainToolbar_Create", 0x61e0e9, 32, 0x9f887161, reinterpret_cast<void *>(&cTSMainToolbar::Create)},
    {"cTSMainToolbar_Construct", 0x619068, 32, 0x8a1cc189, reinterpret_cast<void *>(&cTSMainToolbarAdapters::Construct)},
    // Slot 71: cTSWinSceneGraph's slots 162 / 163 (0x5b99f6, 0x5b9a98) call it directly.
    {"cTSMainToolbar_IsBuildMode", 0x5bb9f1, 8, 0x943ea7bd, reinterpret_cast<void *>(&cTSMainToolbarAdapters::IsBuildMode)},
};
REGISTER_HOOKS(kHooks);

} // namespace
