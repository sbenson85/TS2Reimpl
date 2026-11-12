#include "hook/hooks.h"
#include "ts/cTSEffectCheats.h"
#include "ts/cTSEffectsManager.h"

#include <new>

namespace {

// The constructors as the game calls them (__thiscall, the manager on the stack).
cTSEffectCheat *__fastcall ConstructEffectCheat(void *self, void *, cITSEffectsManager *manager)
{
    return new (self) cTSEffectCheat(manager);
}
cTSEffectsManagerCheat *__fastcall ConstructEffectsManagerCheat(void *self, void *, cITSEffectsManager *manager)
{
    return new (self) cTSEffectsManagerCheat(manager);
}
cOverlayCheat *__fastcall ConstructOverlayCheat(void *self, void *, cITSEffectsManager *manager)
{
    return new (self) cOverlayCheat(manager);
}

// 0x1d0b4a (__thiscall, ret 8): the cheat family's QueryInterface; `self` is any cheat command (the game's or ours).
bool __fastcall CheatQueryInterfaceHook(nGZCommandParser::cICommand *self, void *, uint32_t iid, void **out)
{
    return CheatQueryInterface(self, iid, out);
}

const HookSpec kHooks[] = {
    {"MakeEffectCheats", 0xbc5aa0, 32, 0xee1f1813, reinterpret_cast<void *>(&MakeEffectCheats)},
    {"MakeOverlayCheat", 0xbdb700, 32, 0xf43a1f90, reinterpret_cast<void *>(&MakeOverlayCheat)},
    {"cTSEffectCheat_ctor", 0xbc46d0, 32, 0x0d0b6aa2, reinterpret_cast<void *>(&ConstructEffectCheat)},
    {"cTSEffectsManagerCheat_ctor", 0xbc4fb0, 32, 0xac0c239f, reinterpret_cast<void *>(&ConstructEffectsManagerCheat)},
    {"cOverlayCheat_ctor", 0xbd9da0, 32, 0xd2b636bc, reinterpret_cast<void *>(&ConstructOverlayCheat)},
    {"cTSEffectsManager_KillOrphans", 0xbc97f0, 32, 0x43ec00ab, MemberAddress<&cTSEffectsManager::KillOrphans>()},
    {"CheatQueryInterface", 0x1d0b4a, 32, 0xed983a7b, reinterpret_cast<void *>(&CheatQueryInterfaceHook)},
};
REGISTER_HOOKS(kHooks);

} // namespace
