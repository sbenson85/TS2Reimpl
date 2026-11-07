#include "hook/hooks.h"
#include "sg/cTextureManager.h"

namespace {

using namespace nGZSceneGraph;

const HookSpec kHooks[] = {
    {"nGZSceneGraph_TextureManager", 0xae8750, 32, 0xe8ccdca2, reinterpret_cast<void *>(&TextureManager)},
    {"cTextureHandle_CopyFrom", 0xae8240, 31, 0xb54d11ff, MemberAddress(&cTextureHandle::CopyFrom)},
    {"cTextureHandle_InitFromId", 0xae8260, 29, 0x23ae22db, MemberAddress(&cTextureHandle::InitFromId)},
    {"cTextureHandle_InitFromName", 0xae8280, 32, 0x91d482fd, MemberAddress(&cTextureHandle::InitFromName)},
    {"cTextureHandle_Assign", 0xae82e0, 32, 0xf3174d6e, MemberAddress(&cTextureHandle::Assign)},
    {"cTextureHandle_Destroy", 0xae82b0, 32, 0x18221ad4, MemberAddress(&cTextureHandle::Destroy)},
    {"cTextureHandle_Reset", 0xae8710, 30, 0x4a758ed3, MemberAddress(&cTextureHandle::Reset)},
    {"cTextureHandle_Get", 0xae8730, 28, 0x50a3e2b7, MemberAddress(&cTextureHandle::Get)},
    {"nGZSceneGraph_ResolveResourceName", 0xae7340, 32, 0xc418e0b2, reinterpret_cast<void *>(&ResolveResourceName)},
    // The manager: its getter and its behaviour registry (the slots are ours through our vtable since lane SE).
    {"nGZSceneGraph_cTextureManager_Get", 0xade2e0, 32, 0xd933d188, reinterpret_cast<void *>(&cTextureManager::Get)},
    {"nGZSceneGraph_cTextureManager_Registry", 0xade400, 32, 0x31f31d6a,
     reinterpret_cast<void *>(&cTextureManagerBehaviorTraits::RegistryFor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
