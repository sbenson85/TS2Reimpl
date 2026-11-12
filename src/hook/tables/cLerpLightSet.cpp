// nTSSG::cLerpLightSet (lane 261108l, src/sg/cLerpLightSet.h): the set's constructor, destructors and members, its entry's
// constructors, destructor and step, and the render-group light builder. The game's vector instances over the entries
// are library code and stay the game's (reached only from the game's own copies of them, which nothing calls once these
// are ours).
#include "hook/hooks.h"
#include "sg/cLerpLightSet.h"

#include <new>

namespace {

using nTSSG::cLerpLightSet;
using Entry = cLerpLightSet::Entry;

// 0x3c0e30 (plain ret).
Entry *__fastcall EntryConstruct(Entry *self, void *) { return ::new (self) Entry; }
// 0x3c0d60 (ret 4).
Entry *__fastcall EntryCopy(Entry *self, void *, const Entry *other) { return ::new (self) Entry(*other); }
// 0x3c0fc0 (plain ret).
void __fastcall EntryDestroy(Entry *self, void *) { self->~Entry(); }

const HookSpec kHooks[] = {
    {"nTSSG_cLerpLightSet_Construct", 0x3c0ec0, 32, 0x036947cc, MemberAddress(&cLerpLightSet::Construct)},
    {"nTSSG_cLerpLightSet_Destruct", 0x3c1030, 32, 0x42401f2a, MemberAddress(&cLerpLightSet::Destruct)},
    {"nTSSG_cLerpLightSet_vf0", 0x3c1080, 32, 0x17ad1a04, MemberAddress(&cLerpLightSet::DeletingDestructor)},
    {"nTSSG_cLerpLightSet_Entry_Entry", 0x3c0e30, 32, 0x50f0356a, reinterpret_cast<void *>(&EntryConstruct)},
    {"nTSSG_cLerpLightSet_Entry_EntryCopy", 0x3c0d60, 32, 0x320262a4, reinterpret_cast<void *>(&EntryCopy)},
    {"nTSSG_cLerpLightSet_Entry_dtor", 0x3c0fc0, 32, 0x529e062b, reinterpret_cast<void *>(&EntryDestroy)},
    {"nTSSG_cLerpLightSet_ClearActive", 0x3c2570, 32, 0x7a03c24d, MemberAddress(&cLerpLightSet::ClearActive)},
    {"nTSSG_cLerpLightSet_FadeOutUnused", 0x3c25d0, 32, 0x4d4dfd9f, MemberAddress(&cLerpLightSet::FadeOutUnused)},
    {"nTSSG_cLerpLightSet_Find", 0x3c2660, 32, 0x46a69d0f, MemberAddress(&cLerpLightSet::Find)},
    {"nTSSG_cLerpLightSet_FindPoint", 0x3c26c0, 32, 0xa965c12b, MemberAddress(&cLerpLightSet::FindPoint)},
    {"nTSSG_cLerpLightSet_Clear", 0x3c2cc0, 32, 0x06d19786, MemberAddress(&cLerpLightSet::Clear)},
    {"nTSSG_cLerpLightSet_SetKeys", 0x3c2e30, 32, 0x983895c4, MemberAddress(&cLerpLightSet::SetKeys)},
    {"nTSSG_cLerpLightSet_SetLightEvents", 0x3c2d80, 32, 0xfe86cb0b, reinterpret_cast<void *>(&cLerpLightSet::SetLightEvents)},
    {"nTSSG_cLerpLightSet_LerpLightEvents", 0x3c2b50, 32, 0xfc5d2a3c, reinterpret_cast<void *>(&cLerpLightSet::LerpLightEvents)},
    {"nTSSG_cLerpLightSet_Entry_Evaluate", 0x3c2e60, 32, 0xea2af23d, MemberAddress(&Entry::Evaluate)},
    {"nTSSG_cLerpLightSet_Evaluate", 0x3c3450, 32, 0x5e579d95, MemberAddress(&cLerpLightSet::Evaluate)},
    {"nTSSG_cLerpLightSet_AddLight", 0x3c10a0, 32, 0x8a9dd4dd, MemberAddress(&cLerpLightSet::AddLight)},
    {"nTSSG_cLerpLightSet_AddDirectional", 0x3c1600, 32, 0x3f00913b, MemberAddress(&cLerpLightSet::AddDirectional)},
    {"nTSSG_cLerpLightSet_AddExisting", 0x3c1a80, 32, 0xd9740242, MemberAddress(&cLerpLightSet::AddExisting)},
    {"nTSSG_cLerpLightSet_AddPoint", 0x3c1d60, 32, 0xeb3b4721, MemberAddress(&cLerpLightSet::AddPoint)},
    {"nTSSG_cLerpLightSet_BuildRenderGroupLights", 0x3c2120, 32, 0x47d94a0c,
     reinterpret_cast<void *>(&cLerpLightSet::BuildRenderGroupLights)},
};
REGISTER_HOOKS(kHooks);

} // namespace
