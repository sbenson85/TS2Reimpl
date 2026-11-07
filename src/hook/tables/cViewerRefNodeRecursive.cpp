#include "hook/hooks.h"
#include "sg/cViewerRefNodeRecursive.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0a6ac70", 0xa6ac70, 32, 0xd433587e, reinterpret_cast<void *>(&nGZSceneGraph::cViewerRefNodeRecursive::Create)},
    {"FUN_a0a69f50", 0xa69f50, 31, 0x4145afeb, MemberAddress<&nGZSceneGraph::cViewerRefNodeRecursive::Construct>()},
    {"FUN_a0a6a130", 0xa6a130, 18, 0x733b08fb, MemberAddress<&nGZSceneGraph::cViewerRefNodeRecursive::Destruct>()},
    {"AV_cViewerRefNodeBaseT_VcIViewerRefNodeRecursive_nGZSceneGraph_VcViewerRefNodeRecursiveImpl_2_nGZSceneGraph_vf97", 0xa6ca60, 13, 0xb8b09fba, MemberAddress<&nGZSceneGraph::cViewerRefNodeRecursive::SetWordC4>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
