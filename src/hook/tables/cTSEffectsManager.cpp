#include "hook/hooks.h"
#include "ts/cTSEffectsManager.h"

// Standalone shape (docs/standalone-shape.md): the factory 0xbc86f0 (cTSEffectsCOMDirector registers it by address) is the
// only creation path, so with its hook every manager carries our vtables. The constructor, destructor and slot entries are
// adapters (serving a manager the game built, with the factory hook off); the non-virtual members are reached directly from
// original code (the slots still forwarded: Init, Shutdown, Update, ...).
namespace {

using A = cTSEffectsManagerAdapters;

const HookSpec kHooks[] = {
    {"cTSEffectsManager_Create", 0xbc86f0, 32, 0xaddbd5b7, reinterpret_cast<void *>(&cTSEffectsManager::Create)},
    {"cTSEffectsManager_Construct", 0xbc4870, 32, 0xb5c93b6f, reinterpret_cast<void *>(&A::Construct)},
    {"cTSEffectsManager_Destruct", 0xbc5500, 32, 0x95460353, reinterpret_cast<void *>(&A::Destruct)},
    {"cTSEffectsManager_DeletingDestructor", 0xbc5970, 32, 0x05ec47bb, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cTSEffectsManager_QueryInterface", 0xbc9ea0, 32, 0x822fd86a, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSEffectsManager_Release", 0xbca210, 32, 0xcfffa23c, reinterpret_cast<void *>(&A::Release)},
    {"cTSEffectsManager_SetPaths", 0xbca680, 32, 0x65cc5913, reinterpret_cast<void *>(&A::SetPaths)},
    {"cTSEffectsManager_AddPaths", 0xbc5c10, 32, 0xd5247896, reinterpret_cast<void *>(&A::AddPaths)},
    {"cTSEffectsManager_SetFlag", 0xbca7c0, 32, 0xb0709cdc, reinterpret_cast<void *>(&A::SetFlag)},
    {"cTSEffectsManager_GetFlag", 0xbc86c0, 32, 0x12e34a13, reinterpret_cast<void *>(&A::GetFlag)},
    {"cTSEffectsManager_ParseResources", 0xbc5b20, 32, 0x5113fd25, reinterpret_cast<void *>(&A::ParseResources)},
    {"cTSEffectsManager_HasEffectVariant", 0xbc8e20, 32, 0xb52c3f26, reinterpret_cast<void *>(&A::HasEffectVariant)},
    {"cTSEffectsManager_HasEffect", 0xbc8f00, 32, 0x3f161c1f, reinterpret_cast<void *>(&A::HasEffect)},
    {"cTSEffectsManager_SetState", 0xbca860, 32, 0x31f10481, reinterpret_cast<void *>(&A::SetState)},
    {"cTSEffectsManager_SetMaxDetail", 0xbca380, 32, 0x08c34de3, reinterpret_cast<void *>(&A::SetMaxDetail)},
    {"cTSEffectsManager_SetDetail", 0xbca470, 32, 0xf568efb0, reinterpret_cast<void *>(&A::SetDetail)},
    {"cTSEffectsManager_SetTimeMode", 0xbca580, 13, 0xcd11d76f, reinterpret_cast<void *>(&A::SetTimeMode)},
    {"cTSEffectsManager_UpdateMs", 0xbc6f80, 32, 0x675a518f, reinterpret_cast<void *>(&A::UpdateMs)},
    {"cTSEffectsManager_GetParams", 0xbc8d80, 7, 0x208c997c, reinterpret_cast<void *>(&A::GetParams)},
    {"cTSEffectsManager_GetBCC", 0xbc7340, 7, 0x3b18b7da, reinterpret_cast<void *>(&A::GetBCC)},
    {"cTSEffectsManager_SetSlotObject", 0xbca1d0, 32, 0x7507c349, reinterpret_cast<void *>(&A::SetSlotObject)},
    {"cTSEffectsManager_GetSlotObject", 0xbc9e10, 14, 0x46a497ad, reinterpret_cast<void *>(&A::GetSlotObject)},
    {"cTSEffectsManager_SetNamedA", 0xbc9f10, 32, 0xe0696432, reinterpret_cast<void *>(&A::SetNamedA)},
    {"cTSEffectsManager_GetNamedA", 0xbc5df0, 32, 0x20fc0604, reinterpret_cast<void *>(&A::GetNamedA)},
    {"cTSEffectsManager_SetNamedB", 0xbca070, 32, 0xe0696432, reinterpret_cast<void *>(&A::SetNamedB)},
    {"cTSEffectsManager_GetNamedB", 0xbc5f20, 32, 0x20fc0604, reinterpret_cast<void *>(&A::GetNamedB)},
    {"cTSEffectsManager_SetDebugBit", 0xbca340, 32, 0x15783bf3, reinterpret_cast<void *>(&A::SetDebugBit)},
    {"cTSEffectsManager_GetDebugBit", 0xbc5d20, 32, 0x087f02dc, reinterpret_cast<void *>(&A::GetDebugBit)},
    {"cTSEffectsManager_GetRendererBase", 0xbc9e80, 25, 0x4e4b0f52, reinterpret_cast<void *>(&A::GetRendererBase)},
    {"cTSEffectsManager_PrintStats", 0xbc8cb0, 32, 0xae737592, reinterpret_cast<void *>(&A::PrintStats)},
    {"cTSEffectsManager_CopyEffectNames", 0xbc8c20, 32, 0x34552045, reinterpret_cast<void *>(&A::CopyEffectNames)},
    {"cTSEffectsManager_SetScene", 0xbca810, 32, 0x7d50d8e2, reinterpret_cast<void *>(&A::SetScene)},
    {"cTSEffectsManager_SetCamera", 0xbca920, 32, 0x72f476ae, reinterpret_cast<void *>(&A::SetCamera)},
    {"cTSEffectsManager_SetCameraAndRefresh", 0xbca960, 32, 0x4f8e6b75, reinterpret_cast<void *>(&A::SetCameraAndRefresh)},
    {"cTSEffectsManager_GetScene", 0xbca330, 7, 0x2f3af03a, reinterpret_cast<void *>(&A::GetScene)},
    {"cTSEffectsManager_GetCamera", 0xbcbde0, 7, 0x4beba5bd, reinterpret_cast<void *>(&A::GetCamera)},
    {"cTSEffectsManager_GetDebugValue", 0xbca250, 32, 0xda75775e, reinterpret_cast<void *>(&A::GetDebugValue)},
    {"cTSEffectsManager_Clear", 0xbc5d50, 32, 0x8a6ffb14, reinterpret_cast<void *>(&A::Clear)},
    {"cTSEffectsManager_Link", 0xbc73d0, 32, 0x079d3ec6, reinterpret_cast<void *>(&A::Link)},
    {"cTSEffectsManager_HasEffectName", 0xbc8d90, 32, 0xef2a78f9, reinterpret_cast<void *>(&A::HasEffectName)},
    {"cTSEffectsManager_ReleaseAllEffects", 0xbcb1a0, 32, 0x86625fd4, reinterpret_cast<void *>(&A::ReleaseAllEffects)},
    {"cTSEffectsManager_RefreshDetail", 0xbcb210, 32, 0x1fbc90c1, reinterpret_cast<void *>(&A::RefreshDetail)},
    {"cTSEffectsManager_FlushPending", 0xbc59d0, 32, 0x549fce28, reinterpret_cast<void *>(&A::FlushPending)},
    {"cTSEffectsManager_Reload", 0xbc6670, 32, 0x9074c10f, reinterpret_cast<void *>(&A::Reload)},
    {"cTSEffectsManager_LoadEffects", 0xbc9840, 32, 0x833df113, reinterpret_cast<void *>(&A::LoadEffects)},
    {"cTSEffectsManager_CreateEffect", 0xbc61e0, 32, 0xddde08af, reinterpret_cast<void *>(&A::CreateEffect)},
    {"EffectsManagerCreateVisualEffect", 0xbc6360, 32, 0xe1b48879, reinterpret_cast<void *>(&A::CreateVisualEffect)},
    {"cTSEffectsManager_ListActive", 0xbc8760, 32, 0x5e530492, reinterpret_cast<void *>(&A::ListActive)},
    {"cTSEffectsManager_ListEffects", 0xbc8900, 32, 0x27ff6904, reinterpret_cast<void *>(&A::ListEffects)},
    {"cTSEffectsManager_DoMessage", 0xbc66d0, 32, 0x034bddbc, reinterpret_cast<void *>(&A::DoMessage)},
    {"cTSEffectsManager_AcquireEffect", 0xbc9ab0, 32, 0x65bd8326, reinterpret_cast<void *>(&A::AcquireEffect)},
    {"cTSEffectsManager_FindEffectIndex", 0xbc8590, 32, 0x83bbca92, reinterpret_cast<void *>(&A::FindEffectIndex)},
    {"cTSEffectsManager_FindAliasIndex", 0xbc8620, 32, 0xc6cadde3, reinterpret_cast<void *>(&A::FindAliasIndex)},
    {"cTSEffectsManager_ApplyParserState", 0xbc9e20, 32, 0x9eae70c3, reinterpret_cast<void *>(&A::ApplyParserState)},
    {"cTSEffectsManager_Update", 0xbc6fe0, 32, 0x6b824b93, reinterpret_cast<void *>(&A::Update)},
    {"cTSEffectsManager_ApplySettings", 0xbc6e10, 32, 0x6ceb0c79, reinterpret_cast<void *>(&A::ApplySettings)},
    {"cTSEffectsManager_CounterBlend", 0xbcb3f0, 32, 0x6b46ab6a, reinterpret_cast<void *>(&A::CounterBlend)},
    {"cTSEffectsManager_ActivityWalk", 0xbc6560, 32, 0x03628dd2, reinterpret_cast<void *>(&A::ActivityWalk)},
    {"cTSEffectsManager_Init", 0xbc8fa0, 32, 0xd17d4b28, reinterpret_cast<void *>(&A::Init)},
    {"cTSEffectsManager_Shutdown", 0xbcaa00, 32, 0x5887c464, reinterpret_cast<void *>(&A::Shutdown)},
    {"cTSEffectsManager_OnTimer", 0xbc7350, 32, 0xe76932c9, reinterpret_cast<void *>(&cTSEffectsManager::OnTimer)},
    {"cTSEffectsManager_SendText",0xbca590, 32, 0xad59d7c9, reinterpret_cast<void *>(&cTSEffectsManager::SendText)},
    {"FUN_a0bcb4c0", 0xbcb4c0, 32, 0xb0753eb0, MemberAddress<&cTSEffectsManager::ApplyDetail>()},
    {"FUN_a0bc6050", 0xbc6050, 32, 0xe4c753be, MemberAddress<&cTSEffectsManager::MakeChild>()},
    {"FUN_a0bcb260", 0xbcb260, 32, 0x8ac50608, MemberAddress<&cTSEffectsManager::SendChildBlend>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
