#include "hook/hooks.h"
#include "simantics/cTSBehavior.h"

// Standalone shape (docs/standalone-shape.md, lane GU). The switch is the factory hook (0x469e70, `new cTSBehavior`): the
// game builds behaviour contexts nowhere else (the factory is registered by address with the COM director, class id
// 0x1b269450), so with it on every one carries our vtable. The static helpers are hooked directly (original code still calls
// them: the dump writer 0x46ca20 calls KindName and KindForId). The slot adapters kept are the byte matches (matchcheck); every other slot
// is referenced by the game's vtable only, so it has no hook and its original is trapped behind the factory hook
// (trap_table.cpp, callers.py). Not hooked either: the folded slots (shared_vf_*) and slot 46 (0x46a280, library-labelled).
namespace {

using A = cTSBehaviorAdapters;
using B = cTSBehavior;

const HookSpec kHooks[] = {
    {"cTSBehavior_Create", 0x469e70, 32, 0xb78d72e2, reinterpret_cast<void *>(&B::Create)},
    {"cTSBehavior_ApplySourceBreakpoints", 0x46da40, 32, 0xfc762366, reinterpret_cast<void *>(&B::ApplySourceBreakpoints)},
    {"cTSBehavior_KindName", 0x46f1c0, 32, 0x0c92dd0b, reinterpret_cast<void *>(&B::KindName)},
    {"cTSBehavior_KindForId", 0x46f230, 32, 0xbc84e5a3, reinterpret_cast<void *>(&B::KindForId)},
    {"cTSBehavior_QueryInterface", 0x46d020, 32, 0xdb06f843, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSBehavior_ApplyAllBreakpoints", 0x46d7c0, 32, 0x71b7e40e, reinterpret_cast<void *>(&A::ApplyAllBreakpoints)},
    {"cTSBehavior_SetGlobalSource", 0x46e0e0, 32, 0x163d7ede, reinterpret_cast<void *>(&A::SetGlobalSource)},
    {"cTSBehavior_SetSemiGlobalSource", 0x46e2b0, 32, 0xe9890a80, reinterpret_cast<void *>(&A::SetSemiGlobalSource)},
    {"cTSBehavior_SetPrivateSource", 0x46e270, 32, 0xc268524e, reinterpret_cast<void *>(&A::SetPrivateSource)},
    {"cTSBehavior_GetGroupForId", 0x46a730, 32, 0x78dc2162, reinterpret_cast<void *>(&A::GetGroupForId)},
    {"cTSBehavior_GetSourceForId", 0x46bd90, 32, 0x78dc2162, reinterpret_cast<void *>(&A::GetSourceForId)},
    {"cTSBehavior_GetSourceName", 0x46be30, 32, 0x2434e1f2, reinterpret_cast<void *>(&A::GetSourceName)},
    {"cTSBehavior_DescribePrimitive", 0x46bc50, 32, 0x41325fbb, reinterpret_cast<void *>(&A::DescribePrimitive)},
    {"cTSBehavior_LoadBreakpoints", 0x46c3e0, 13, 0xd4de6425, reinterpret_cast<void *>(&A::LoadBreakpoints)},
    {"cTSBehavior_GetKindForId", 0x46c000, 15, 0x85a64620, reinterpret_cast<void *>(&A::GetKindForId)},
};
REGISTER_HOOKS(kHooks);

} // namespace
