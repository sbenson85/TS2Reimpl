
#include "hook/hooks.h"
#include "sg/cGeometryDataContainer.h"
#include "sg/cGeometryDataContainerClass.h"

namespace {

using Sub = nGZSceneGraph::cIGeometryDataContainerSub;

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cGeometryDataContainer_Create", 0xa70aa0, 32, 0x38a3a274,
     reinterpret_cast<void *>(&nGZSceneGraph::cGeometryDataContainer::Create)},
    {"nGZSceneGraph_cGeometryDataContainer_GetClassId", 0xa70b10, 6, 0xf7b61fcc,
     reinterpret_cast<void *>(&nGZSceneGraph::cGeometryDataContainer::GetClassId)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_31", 0xa70e10, 24, 0x68a91c76, MemberAddress(&Sub::CountRecordsA)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_39", 0xa70e30, 28, 0x39a1bdd9, MemberAddress(&Sub::CountRecordsB)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_50", 0xa70e50, 24, 0xe2178040, MemberAddress(&Sub::CountRecordsD)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_48", 0xa70e70, 28, 0xed25e0a3, MemberAddress(&Sub::CountElements28)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_44", 0xa70e90, 24, 0xa59ddd4d, MemberAddress(&Sub::CountRecordsC)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_52", 0xa70d80, 29, 0xe61c4381, MemberAddress(&Sub::RecordStringAt18)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_51", 0xa70da0, 24, 0x7c16f955, MemberAddress(&Sub::RecordStringAt00)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_32", 0xa708d0, 32, 0x039c747d, MemberAddress(&Sub::CopyRecordA24)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_11", 0xa72d20, 23, 0x1d91e6ee, MemberAddress(&Sub::SetRecordADword)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_15", 0xa72fb0, 32, 0x82b82fa1, MemberAddress(&Sub::SetRecordBWord)}, // eax-ignored: eax is the position argument loaded as the store's index, not a result
    {"nGZSceneGraph_cGeometryDataContainer_vf44_18", 0xa700b0, 32, 0xda72ed0d, MemberAddress(&Sub::TrimWordVector)},
    {"nGZSceneGraph_cGeometryDataContainer_vf44_22", 0xa72e70, 32, 0x4cf35f4a, MemberAddress(&Sub::SetRecordCDword)},
    {"nGZSceneGraph_cGeometryDataContainer_vf128_11", 0xa70bac, 11, 0xcdc69fbe, reinterpret_cast<void *>(&nGZSceneGraph::VbThunkCompareIdentity)},
    {"nGZSceneGraph_cGeometryDataContainer_vf128_9", 0xa72c76, 11, 0x3e3de4d7, reinterpret_cast<void *>(&nGZSceneGraph::VbThunkGetVirtualBase)},
    {"nGZSceneGraph_cGeometryDataContainer_vf128_22", 0xa72c98, 11, 0x5f3923fe, reinterpret_cast<void *>(&nGZSceneGraph::VbThunkCopyKeyTo)},
    {"nGZSceneGraph_cGeometryDataContainer_vf128_16", 0xa72d0f, 11, 0x34ba2f6c, reinterpret_cast<void *>(&nGZSceneGraph::VbThunkSetFlag)},
    {"nGZSceneGraph_cGeometryDataContainer_vf128_15", 0xa72eee, 11, 0x67943cd9, reinterpret_cast<void *>(&nGZSceneGraph::VbThunkSetResource)},
};
REGISTER_HOOKS(kHooks);

} // namespace
