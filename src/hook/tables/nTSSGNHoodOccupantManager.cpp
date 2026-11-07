#include "hook/hooks.h"
#include "ts/nTSSGNHoodOccupantManager.h"

namespace {

#define SGOCC_ADAPTER(name) reinterpret_cast<void *>(&nTSSG::cNHoodOccupantManagerAdapters::name)

// Construct and Create are the switch (docs/standalone-shape.md): with them on, every manager the game makes is ours, with our
// vtables; the other entries are the adapters for old entry points (callers.py rests the slot verdicts on two hooks, the
// constructor's and the destructor's, so the adapters stay) and the non-virtual members.
const HookSpec kHooks[] = {
    {"nTSSG_cNHoodOccupantManager_Construct", 0x2a4a00, 32, 0x3bcc80e7, SGOCC_ADAPTER(Construct)},
    {"nTSSG_cNHoodOccupantManager_Create", 0x2a7b70, 32, 0x1eb8bcb2,
        reinterpret_cast<void *>(&nTSSG::cNHoodOccupantManager::Create)},
    {"nTSSG_cNHoodOccupantManager_Destruct", 0x2a50f0, 32, 0x31e999bc, SGOCC_ADAPTER(Destruct)},
    {"nTSSG_cNHoodOccupantManager_vf3", 0x2a5310, 32, 0x309e8085, SGOCC_ADAPTER(DeletingDestructor)},
    {"nTSSG_cNHoodOccupantManager_QueryInterface", 0x2a8da0, 32, 0x994fb89d, SGOCC_ADAPTER(QueryInterface)},
    {"nTSSG_cNHoodOccupantManager_SceneQueryInterface", 0x2a8deb, 8, 0x58867e9d, SGOCC_ADAPTER(SceneQueryInterface)},
    {"nTSSG_cNHoodOccupantManager_MessageQueryInterface", 0x2a8df3, 8, 0x06a31464, SGOCC_ADAPTER(MessageQueryInterface)},
    {"nTSSG_cNHoodOccupantManager_vf8_3", 0x2a8100, 32, 0x7fc4e760, SGOCC_ADAPTER(Initialize)},
    {"nTSSG_cNHoodOccupantManager_vf8_4", 0x2a92c0, 32, 0xf57f1196, SGOCC_ADAPTER(Shutdown)},
    {"nTSSG_cNHoodOccupantManager_vf8_6", 0x2a7af0, 32, 0xd0be9b68, SGOCC_ADAPTER(SetLayerVisible)},
    {"nTSSG_cNHoodOccupantManager_vf8_7", 0x2a7b30, 5, 0xfa06087d, SGOCC_ADAPTER(MarkDirty)},
    {"nTSSG_cNHoodOccupantManager_vf8_8", 0x2a7660, 5, 0xe31d393c, SGOCC_ADAPTER(ClearDirty)},
    {"nTSSG_cNHoodOccupantManager_vf8_10", 0x2a86a0, 32, 0x7273b1d7, SGOCC_ADAPTER(Update)},
    {"nTSSG_cNHoodOccupantManager_DetachScene", 0x2a56d0, 32, 0x3418afc1,
        MemberAddress<&nTSSG::cNHoodOccupantManager::DetachScene>()},
    {"nTSSG_cNHoodOccupantManager_DoMessage", 0x2a7670, 32, 0xd85c4484, SGOCC_ADAPTER(DoMessage)},
    {"nTSSG_cNHoodOccupantManager_vf8_9", 0x2a93c0, 32, 0x4a43dfbe, SGOCC_ADAPTER(SetMergeFlora)},
    {"nTSSG_cNHoodOccupantManager_vf8_11", 0x2a9610, 32, 0x67d24007, SGOCC_ADAPTER(UpdateFade)},
    {"nTSSG_cNHoodOccupantManager_vf8_5", 0x2a8780, 32, 0x82e567cd, SGOCC_ADAPTER(BuildScene)},
    {"FUN_a02a9ba0", 0x2a9ba0, 32, 0xa0ce1283, MemberAddress<&nTSSG::cNHoodOccupantManager::RefreshOccupant>()},
    {"FUN_a02a8040", 0x2a8040, 32, 0xfa7d280d, MemberAddress<&nTSSG::cNHoodOccupantManager::HighlightOccupant>()},
    {"FUN_a02a8e30", 0x2a8e30, 32, 0xcfcde24f, MemberAddress<&nTSSG::cNHoodOccupantManager::ClearOccupantHighlight>()},
    {"FUN_a02a9260", 0x2a9260, 32, 0x5c2c66b4, MemberAddress<&nTSSG::cNHoodOccupantManager::ApplyTexts>()},
    {"FUN_a02a8020", 0x2a8020, 22, 0x89892460, MemberAddress<&nTSSG::cNHoodOccupantManager::ApplyRef78Setting>()},
    // lane GW: the scene-building helpers
    {"nTSSG_cNHoodOccupantManager_ChangeNodeFlags", 0x2a5560, 32, 0xaaa23ca9,
        reinterpret_cast<void *>(&nTSSG::cNHoodOccupantManager::ChangeNodeFlags)},
    {"nTSSG_cNHoodOccupantManager_SetNodeColor", 0x2a55e0, 32, 0xcff23e0d,
        reinterpret_cast<void *>(&nTSSG::cNHoodOccupantManager::SetNodeColor)},
    {"nTSSG_cNHoodOccupantManager_LayerOf", 0x2a7f20, 32, 0x0b2b47c6,
        reinterpret_cast<void *>(&nTSSG::cNHoodOccupantManager::LayerOf)},
    {"nTSSG_cNHoodOccupantManager_FootprintIn", 0x2a8570, 32, 0x088fb80b,
        reinterpret_cast<void *>(&nTSSG::cNHoodOccupantManager::FootprintIn)},
    {"nTSSG_cNHoodOccupantManager_MergeOccupant", 0x2a5340, 32, 0xcfb50abb,
        MemberAddress<&nTSSG::cNHoodOccupantManager::MergeOccupant>()},
    {"nTSSG_cNHoodOccupantManager_ClearLayers1And2", 0x2a5860, 32, 0x36271755,
        MemberAddress<&nTSSG::cNHoodOccupantManager::ClearLayers1And2>()},
    {"nTSSG_cNHoodOccupantManager_AddOccupant", 0x2a7500, 32, 0x7b0960bb,
        MemberAddress<&nTSSG::cNHoodOccupantManager::AddOccupant>()},
    {"nTSSG_cNHoodOccupantManager_RemoveOccupant", 0x2a8e70, 32, 0xe497fb75,
        MemberAddress<&nTSSG::cNHoodOccupantManager::RemoveOccupant>()},
    {"nTSSG_cNHoodOccupantManager_HandOnPending", 0x2a9100, 32, 0xf1a0dd9e,
        MemberAddress<&nTSSG::cNHoodOccupantManager::HandOnPending>()},
    {"nTSSG_cNHoodOccupantManager_RebuildMergedLayers", 0x2a5d70, 32, 0x64002033,
        MemberAddress<&nTSSG::cNHoodOccupantManager::RebuildMergedLayers>()},
    {"nTSSG_cNHoodOccupantManager_BuildFloraNode", 0x2a5a40, 32, 0xe62b294f,
        MemberAddress<&nTSSG::cNHoodOccupantManager::BuildFloraNode>()},
    {"FUN_a02a81f0", 0x2a81f0, 32, 0x611a2415, MemberAddress<&nTSSG::cNHoodOccupantManager::PrepareBuildAll>()},
    {"FUN_a02a7be0", 0x2a7be0, 32, 0x701c89de, MemberAddress<&nTSSG::cNHoodOccupantManager::MergeLayerChildren>()},
    {"FUN_a02a7130", 0x2a7130, 32, 0xf3a0b4b5, MemberAddress<&nTSSG::cNHoodOccupantManager::AddFaceCOccupant>()},
    {"FUN_a02a5ef0", 0x2a5ef0, 32, 0x0d42f7fb, MemberAddress<&nTSSG::cNHoodOccupantManager::AddFloraOccupant>()},
    {"FUN_a02a63a0", 0x2a63a0, 32, 0xa5bed6b7, MemberAddress<&nTSSG::cNHoodOccupantManager::AddOtherOccupant>()},
    {"FUN_a02a69b0", 0x2a69b0, 32, 0x1c15e683, MemberAddress<&nTSSG::cNHoodOccupantManager::AddRoadOccupant>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
