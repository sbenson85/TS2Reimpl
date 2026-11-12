#include "hook/hooks.h"
#include "sg/cDisplayListDynamicRenderStrategy.h"

namespace {

using A = nTSSG::cDisplayListDynamicRenderStrategyAdapters;

// The constructor and the COM factory (held as data by the director's class table): every dynamic strategy the game
// makes is ours, so its slots, destructors, slot 8's helpers and the member constructors are in effect through callers
// (db/status/cDisplayListDynamicRenderStrategy.jsonl). The drawing paths (row TS1i, lane 261108s: 0x3743e0, 0x374670,
// 0x374bb0, 0x373a70 with the table's Record 0x372150, 0x371150, 0x371240, 0x373f00) are reached only from Draw 0x373e20
// (ours), so in effect through it. The helpers below were hooked while those paths were the original's; their only
// callers are ours now, and the hooks stay (harmless, and they keep any stray original caller on our code).
// DisableScissor is one of them (0x3743e0 and 0x374bb0 call it).
//
// The frame state (class 0x6d548978): its factory (held as data by the director's class table) and its destructor
// (called by the deleting destructor 0x36fbb0, src/misc/ClassesRound3's); the constructor and the table's constructor
// and destructor are reached only from these.
//
// The window property read (cdecl): the helper's Window 0x370cd0 (row TS1f, ours) calls it.
//
// The drawing paths' per-frame helpers (row TS1e): MarkRecords 0x3724a0 (the paths 0x3743e0, 0x374ad0 and 0x374bb0
// call it; the interval trees' Build 0x370b70 / Overlaps 0x373750 / HitsShareRectangle 0x3735b0 and cIntervalTree's
// functions are in effect through it), ComputeBounds 0x370eb0 and SetUpScissor 0x3719a0 (0x3743e0 and 0x374bb0).
//
// The frame's dirty rectangles (row TS1f): UpdateDirtyRects 0x3706a0 (the paths 0x3743e0 and 0x374670 call it); the
// rectangle merge 0x370470, the table's Update 0x372430 / Collect 0x36fc40 / Push 0x3703f0 / PushVanished 0x370980,
// DrawItemPixelRect 0x372270, and the helper's window walk (0x372480, 0x3702c0, 0x370430, 0x370cd0, 0x375a20, the
// callback 0x370e40, 0x372600, 0x3726f0, 0x3738b0) are in effect through it.
//
// The drawing paths' item drawers and clears (row TS1g, lane 261108r), each hooked itself, called by the paths:
// DrawItemRun 0x374350 (by 0x374670), DrawReflectionItems 0x374270 (0x3743e0), DrawItemsBy 0x374100
// (0x374bb0 and 0x373f00), DrawCensor 0x374ad0 (0x3743e0, 0x374670), ClearRects 0x373b70 (0x3743e0, 0x374bb0).
// DrawOneItem 0x3743a0's callers are the three drawers, so it is hooked only for symmetry with them.
const HookSpec kHooks[] = {
    {"nTSSG_WindowPropertyUint32", 0x372670, 32, 0x81d559e1, reinterpret_cast<void *>(&nTSSG::WindowPropertyUint32)},
    {"nTSSG_DynamicStrategyState_Create", 0x3721f0, 32, 0x96c0a967, reinterpret_cast<void *>(&A::CreateState)},
    {"nTSSG_DynamicStrategyState_Destruct", 0x36f5c0, 32, 0x34b5964f, reinterpret_cast<void *>(&nTSSG::DestroyDynamicStrategyState)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_Construct", 0x36e400, 32, 0x7c29faa3, reinterpret_cast<void *>(&A::Construct)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_Create", 0x372190, 32, 0x52ec6fe5, reinterpret_cast<void *>(&A::Create)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_DisableScissor", 0x3714b0, 32, 0x7660c957,
     reinterpret_cast<void *>(&A::DisableScissor)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_MarkRecords", 0x3724a0, 32, 0xbe530cf8, reinterpret_cast<void *>(&A::MarkRecords)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_ComputeBounds", 0x370eb0, 32, 0xa011aa0a,
     reinterpret_cast<void *>(&A::ComputeBounds)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_SetUpScissor", 0x3719a0, 32, 0x408b7a80,
     reinterpret_cast<void *>(&A::SetUpScissor)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_UpdateDirtyRects", 0x3706a0, 32, 0x43ad3bbb,
     reinterpret_cast<void *>(&A::UpdateDirtyRects)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_DrawOneItem", 0x3743a0, 32, 0x653ab5db,
     MemberAddress<&nTSSG::cDisplayListDynamicRenderStrategy::DrawOneItem>()},
    {"nTSSG_cDisplayListDynamicRenderStrategy_DrawItemRun", 0x374350, 32, 0x35972c6c,
     reinterpret_cast<void *>(&A::DrawItemRun)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_DrawReflectionItems", 0x374270, 32, 0x1ac5d961,
     reinterpret_cast<void *>(&A::DrawReflectionItems)},
    {"nTSSG_cDisplayListDynamicRenderStrategy_DrawItemsBy", 0x374100, 32, 0xd3a06e04,
     MemberAddress<&nTSSG::cDisplayListDynamicRenderStrategy::DrawItemsBy>()},
    {"nTSSG_cDisplayListDynamicRenderStrategy_DrawCensor", 0x374ad0, 32, 0x23339c7e,
     MemberAddress<&nTSSG::cDisplayListDynamicRenderStrategy::DrawCensor>()},
    {"nTSSG_cDisplayListDynamicRenderStrategy_ClearRects", 0x373b70, 32, 0x18f3b299,
     reinterpret_cast<void *>(&A::ClearRects)},
};
REGISTER_HOOKS(kHooks);

} // namespace
