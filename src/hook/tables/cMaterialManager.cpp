#include "hook/hooks.h"
#include "sg/cMaterialManager.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cMaterialManager_Get", 0xacc040, 32, 0x804f61c9, reinterpret_cast<void *>(&nGZSceneGraph::cMaterialManager::Get)},
    {"nGZSceneGraph_MixHash3", 0x98bf70, 32, 0xe5478cbf, reinterpret_cast<void *>(&nGZSceneGraph::MixHash3)},
    // The behaviour registry: the game's cAbstractBehaviorTypeT instantiation for the manager (0xaca910, 0xacab80,
    // 0xacc4c0) still names it.
    {"nGZSceneGraph_cMaterialManager_Registry", 0xacc550, 32, 0x6d656ec3,
     reinterpret_cast<void *>(&nGZSceneGraph::cMaterialManagerBehaviorTraits::RegistryFor)},
    // ResetEntry: hooked as a member helper (its callers are ours, the image's copies are trapped).
    {"nGZSceneGraph_cMaterialManager_ResetEntry", 0xacb170, 32, 0x56dd4768,
     MemberAddress(&nGZSceneGraph::cMaterialManager::ResetEntry)},
    // Part 2's helpers, hooked at their RVAs.
    {"nGZSceneGraph_cMaterialManager_BuildVariant", 0xacadd0, 32, 0x2e080a52,
     MemberAddress(&nGZSceneGraph::cMaterialManager::BuildVariant)},
    {"nGZSceneGraph_cMaterialManager_ReloadEntry", 0xacce00, 32, 0xb6791587,
     MemberAddress(&nGZSceneGraph::cMaterialManager::ReloadEntry)},
    {"nGZSceneGraph_cMaterialManager_BuildEntry", 0xacbaf0, 32, 0xaf593f32,
     MemberAddress(&nGZSceneGraph::cMaterialManager::BuildEntry)},
    {"nGZSceneGraph_cMaterialManager_LoadMaterial", 0xacc640, 32, 0x5fddfe9f,
     MemberAddress(&nGZSceneGraph::cMaterialManager::LoadMaterial)},
};
REGISTER_HOOKS(kHooks);

} // namespace
