#include "hook/hooks.h"
#include "ts/cTSEffectsExtension.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSEffectsExtension_Construct", 0xcc50c0, 32, 0x6e8f6b85, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::Construct)},
    {"FUN_a0cc53d0", 0xcc53d0, 32, 0x75dc5804, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::Destruct)},
    {"cTSEffectsExtension_vf0", 0xcc5460, 32, 0x2ff5f368, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::DeletingDestructor)},
    {"cTSEffectsExtension_FindEffectNode", 0xcc5b50, 32, 0x36439b14, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::FindEffectNode)},
    {"cTSEffectsExtension_FindRecord", 0xcc4d10, 32, 0x6af1f9c4, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::FindRecord)},
    {"cTSEffectsExtension_RemoveRecord", 0xcc4fd0, 32, 0xedde91ef, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::RemoveRecord)},
    {"cTSEffectsExtension_vf24_4", 0xcc65d0, 32, 0xde88216b, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::RemoveByInstance)},
    {"cTSEffectsExtension_vf24_17", 0xcc5e40, 32, 0x355b156f, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::HasActiveEffect)},
    {"cTSEffectsExtension_vf24_10", 0xcc67e0, 32, 0xaf8a9ad8, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::SetStateByte1)},
    {"cTSEffectsExtension_vf24_11", 0xcc6820, 32, 0x2b385b56, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::SetStateByte2)},
    {"cTSEffectsExtension_vf24_12", 0xcc6870, 32, 0xcb325736, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::BroadcastThree)},
    {"cTSEffectsExtension_vf24_13", 0xcc68c0, 32, 0xd30c0c61, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::BroadcastTwo)},
    {"cTSEffectsExtension_vf24_7", 0xcc6900, 32, 0xad1241f8, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::FilterActive)},
    {"cTSEffectsExtension_vf24_8", 0xcc6760, 32, 0xe39d5642, reinterpret_cast<void *>(&nGZSceneGraph::cTSEffectsExtensionAdapters::RemoveActive)},
    {"cTSEffectsExtension_vf24_14", 0xcc6990, 32, 0xfb504ed4, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::Unbind)},
    {"cTSEffectsExtension_vf24_3", 0xcc5b90, 32, 0x6e6946ba, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::CollectNamed)},
    {"cTSEffectsExtension_vf24_0", 0xcc5e80, 32, 0x18637eb4, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::GetDisplayName)},
    {"cTSEffectsExtension_vf24_15", 0xcc6550, 32, 0x0863f7af, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::BindEffects)},
    {"cTSEffectsExtension_vf24_6", 0xcc5a00, 32, 0x2724dff8, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::FindInstance)},
    {"cTSEffectsExtension_vf24_5", 0xcc6620, 32, 0xc8146508, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::RemoveByKey)},
    {"cTSEffectsExtension_vf24_9", 0xcc5c20, 32, 0xb1d6f3cc, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::ProcessUnused)},
    {"cTSEffectsExtension_vf24_19", 0xcc69e0, 32, 0x2530719d, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::WriteRecords)},
    {"cTSEffectsExtension_vf24_1", 0xcc5ef0, 32, 0x56a1c7db, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::Dispatch)},
    {"cTSEffectsExtension_vf24_2", 0xcc5480, 32, 0xd926fb0b, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::CreateEffect)},
    {"cTSEffectsExtension_vf24_18", 0xcc61c0, 32, 0x3369b7db, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::ReadRecords)},
    {"cTSEffectsExtension_vf24_22", 0xcc5810, 32, 0xbfc3e936, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::BuildStatusText)},
    {"cTSEffectsExtension_QueryInterface", 0xcc6170, 32, 0x4d05cc35, MemberAddress(&nGZSceneGraph::cTSEffectsExtensionAdapters::QueryInterface)},
};
REGISTER_HOOKS(kHooks);

} // namespace
