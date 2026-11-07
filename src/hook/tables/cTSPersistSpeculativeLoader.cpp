#include "hook/hooks.h"
#include "ts/cTSPersistSpeculativeLoader.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a01f79f0", 0x1f79f0, 32, 0x00b40e90, MemberAddress(&cTSPersistSpeculativeLoaderQueue::Find)},
    {"FUN_a01f8260", 0x1f8260, 32, 0x42dae16e, MemberAddress(&cTSPersistSpeculativeLoaderQueue::Contains)},
    {"FUN_a01f7de0", 0x1f7de0, 32, 0x2aacc235, MemberAddress(&cTSPersistSpeculativeLoaderQueue::PopBack)},
    {"FUN_a01f7900", 0x1f7900, 32, 0xf5e896a9, MemberAddress(&cTSPersistSpeculativeLoaderQueue::Remove)},
    {"FUN_a01f7ae0", 0x1f7ae0, 32, 0x54908e0a, MemberAddress(&cTSPersistSpeculativeLoaderQueue::Clear)},
    {"FUN_a01f8800", 0x1f8800, 32, 0x50e4bcbe, MemberAddress(&cTSPersistSpeculativeLoaderQueue::Insert)},
    {"cTSPersistSpeculativeLoaderQueue_vf0", 0x1f7710, 32, 0x62c9db6c, MemberAddress(&cTSPersistSpeculativeLoaderQueue::DeletingDestructor)},
    {"cTSPersistSpeculativeLoaderPool_vf0", 0x1f76b0, 32, 0xe31582d9, MemberAddress(&cTSPersistSpeculativeLoaderPool::DeletingDestructor)},
    {"cTSPersistSpeculativeLoaderLayer_vf0", 0x1f7600, 32, 0x368d2684, MemberAddress(&cTSPersistSpeculativeLoaderLayer::DeletingDestructor)},
    {"FUN_a01f7d40", 0x1f7d40, 6, 0x3fc536f8, reinterpret_cast<void *>(&cTSPersistSpeculativeLoader::GetClassId)},
    {"cTSPersistSpeculativeLoader_vf12", 0x1f7da0, 32, 0xc898689f, MemberAddress(&cTSPersistSpeculativeLoader::GetStatistics)},
    {"cTSPersistSpeculativeLoader_QueryInterface", 0x1f8300, 32, 0x71d803f5, MemberAddress(&cTSPersistSpeculativeLoader::QueryInterface)},
    {"FUN_a01f81b0", 0x1f81b0, 32, 0x1c50d410, MemberAddress(&cTSPersistSpeculativeLoaderPhysicalLayer::IsPending)},
    {"FUN_a01f8650", 0x1f8650, 32, 0xe9fd643c, MemberAddress(&cTSPersistSpeculativeLoaderPhysicalLayer::Submit)},
    {"FUN_a01f8330", 0x1f8330, 32, 0x8c8fff3a, MemberAddress(&cTSPersistSpeculativeLoaderPhysicalLayer::CompleteParents)},
    {"FUN_a01f89d0", 0x1f89d0, 32, 0x36a770d9, MemberAddress(&cTSPersistSpeculativeLoaderPhysicalLayer::QueueReferences)},
    {"FUN_a01f9360", 0x1f9360, 32, 0xea86b66c, MemberAddress(&cTSPersistSpeculativeLoaderPhysicalLayer::Process)},
    {"FUN_a01f9860", 0x1f9860, 32, 0x9538eaf5, reinterpret_cast<void *>(&cTSPersistSpeculativeLoaderPhysicalLayer::ThreadMain)},
    {"FUN_a01f7790", 0x1f7790, 32, 0x9191b7b7, MemberAddress(&cTSPersistSpeculativeLoaderLogicalLayer::Cancel)},
    {"FUN_a01f8d90", 0x1f8d90, 32, 0x90706b5a, MemberAddress(&cTSPersistSpeculativeLoaderLogicalLayer::Update)},
    {"cTSPersistSpeculativeLoader_vf3", 0x1f8050, 32, 0xa42f7cb5, MemberAddress(&cTSPersistSpeculativeLoader::Init)},
    {"cTSPersistSpeculativeLoader_vf4", 0x1f8c70, 32, 0x42ceeccc, MemberAddress(&cTSPersistSpeculativeLoader::Shutdown)},
    {"cTSPersistSpeculativeLoader_vf5", 0x1f8450, 32, 0xe836080c, MemberAddress(&cTSPersistSpeculativeLoader::Preload)},
    {"cTSPersistSpeculativeLoader_vf6", 0x1f7780, 8, 0x4a168e39, MemberAddress(&cTSPersistSpeculativeLoader::Cancel)},
    {"cTSPersistSpeculativeLoader_vf7", 0x1f80b0, 32, 0xb47cd209, MemberAddress(&cTSPersistSpeculativeLoader::IsAvailable)},
    {"cTSPersistSpeculativeLoader_vf8", 0x1f8170, 32, 0xb12c8229, MemberAddress(&cTSPersistSpeculativeLoader::IsPending)},
    {"cTSPersistSpeculativeLoader_vf9", 0x1f7ee0, 32, 0xcf4effbf, MemberAddress(&cTSPersistSpeculativeLoader::GetResource)},
    {"cTSPersistSpeculativeLoader_vf10", 0x1f8cf0, 32, 0x19ab4631, MemberAddress(&cTSPersistSpeculativeLoader::Update)},
    {"cTSPersistSpeculativeLoader_vf11", 0x1f7a40, 32, 0x42ceeccc, MemberAddress(&cTSPersistSpeculativeLoader::Clear)},
    {"cTSPersistSpeculativeLoaderPhysicalLayer_DeletingDestructor", 0x1f7690, 32, 0xdd8df66f, MemberAddress(&cTSPersistSpeculativeLoaderPhysicalLayer::DeletingDestructor)},
    {"cTSPersistSpeculativeLoaderLogicalLayer_DeletingDestructor", 0x1f7630, 32, 0x44dcd7fa, MemberAddress(&cTSPersistSpeculativeLoaderLogicalLayer::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
