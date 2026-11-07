#include "hook/hooks.h"
#include "sg/cExtension.h"

namespace {

using ExtensionAdapters = nGZSceneGraph::cExtensionAdapters;

// nGZSceneGraph::cExtension as a whole class (src/sg/cExtension.h, lane OY). About 17 game classes derive from it: their
// constructors call 0xa3b7d0 / 0xa3b750 as a base part (flag 0: the hooks are our compiler's own constructors), their
// destructors 0xa3b880, their QueryInterface 0xa3be60 and Write 0xa3c210 directly, and their vtables hold thunks into the
// bodies below (this = object + 0xc, + 0x10 or + 0x1c, whatever the derived class). Through callers only: the deleting
// destructor 0xa3b910, the behaviour template's constructor / destructor / deleting destructor 0xa3b710 / 0xa3b850 /
// 0xa3b8f0, its Release 0xa3bee0, the event-manager getter 0xa3bac0 and the two vtables' thunks. Left original (folded
// with other families): 0xa3bde0 (the template's QueryInterface), 0xa3bd80 (OnQuery), 0x258280 / 0x2582b0 (AddRef),
// 0x9dc310 (Read), 0x9e8850 / 0x9e7e90 (the owner / next getters), 0x144910. Bisect: the rows together (a derived object
// would otherwise mix the game's and our vtables).
const HookSpec kHooks[] = {
    {"cExtension_Construct", 0xa3b7d0, 32, 0x83f444a8, const_cast<uint8_t *>(&OY_cExtension_Constructor)},
    {"cExtension_CopyConstruct", 0xa3b750, 32, 0x83f444a8, const_cast<uint8_t *>(&OY_cExtension_CopyConstructor)},
    {"cExtension_Destruct", 0xa3b880, 32, 0x31ce6569, reinterpret_cast<void *>(&ExtensionAdapters::Destruct)},
    {"cExtension_QueryInterface", 0xa3be60, 32, 0x370029cd, reinterpret_cast<void *>(&ExtensionAdapters::QueryInterface)},
    {"cExtension_Release", 0xa3bf50, 8, 0x44fa9611, reinterpret_cast<void *>(&ExtensionAdapters::Release)},
    {"cExtension_GetTypeInfo", 0xa3c160, 32, 0x941c82f7, reinterpret_cast<void *>(&ExtensionAdapters::GetTypeInfo)},
    {"cExtension_Write", 0xa3c210, 32, 0x79c6bdcc, reinterpret_cast<void *>(&ExtensionAdapters::Write)},
    {"cExtension_GetOwnerInfo", 0xa3bf60, 32, 0xb94d1d63, reinterpret_cast<void *>(&ExtensionAdapters::GetOwnerInfo)},
    {"cExtension_OnEvent", 0xa3bd10, 32, 0x1dc4e866, reinterpret_cast<void *>(&ExtensionAdapters::OnEvent)},
    {"cExtension_NotifyListeners", 0xa3bfb0, 32, 0x4dfe35b8, reinterpret_cast<void *>(&ExtensionAdapters::NotifyListeners)},
    {"cExtension_AskListeners", 0xa3bbc0, 32, 0xf006cc8c, reinterpret_cast<void *>(&ExtensionAdapters::AskListeners)},
    {"cExtension_Connect", 0xa3ba80, 32, 0xa408ba2e, reinterpret_cast<void *>(&ExtensionAdapters::Connect)},
    {"cExtension_Disconnect", 0xa3bb50, 32, 0xae63958f, reinterpret_cast<void *>(&ExtensionAdapters::Disconnect)},
    {"cExtension_GetEventAttribute", 0xa3b990, 32, 0x44150bbb, reinterpret_cast<void *>(&ExtensionAdapters::GetEventAttribute)},
    {"cExtension_GetPersistReferent", 0xa3b950, 12, 0x900a23ef, reinterpret_cast<void *>(&ExtensionAdapters::GetPersistReferent)},
    {"cExtension_SetOwner", 0xa3c0d0, 10, 0x540db207, reinterpret_cast<void *>(&ExtensionAdapters::SetOwner)},
    {"cExtension_SetNext", 0xa3c0f0, 32, 0x33b63bc1, reinterpret_cast<void *>(&ExtensionAdapters::SetNext)},
    {"cExtension_IsKindOf", 0xa3c130, 32, 0xf274eb02, reinterpret_cast<void *>(&ExtensionAdapters::IsKindOf)},
    {"cExtension_GetDescription", 0xa3bb90, 17, 0x7a5ecfda, reinterpret_cast<void *>(&ExtensionAdapters::GetDescription)},
};
REGISTER_HOOKS(kHooks);

} // namespace
