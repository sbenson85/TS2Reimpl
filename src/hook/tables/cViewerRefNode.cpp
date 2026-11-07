#include "hook/hooks.h"
#include "sg/cViewerRefNode.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0a65fb0", 0xa65fb0, 32, 0x3d44b484, reinterpret_cast<void *>(&nGZSceneGraph::cViewerRefNode::Create)},
    {"FUN_a0a644a0", 0xa644a0, 31, 0xff80f652, MemberAddress<&nGZSceneGraph::cViewerRefNode::Construct>()},
    {"FUN_a0a64820", 0xa64820, 18, 0xc1e40330, MemberAddress<&nGZSceneGraph::cViewerRefNode::Destruct>()},
    {"FUN_a0a66440", 0xa66440, 32, 0x9c052aca, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::MakeIterator51>()},
    {"FUN_a0a67950", 0xa67950, 32, 0xabd74a0d, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::ResizeEntries>()},
    {"FUN_a0a67b60", 0xa67b60, 32, 0x8b9b2193, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetEntryPlacement>()},
    {"FUN_a0a678f0", 0xa678f0, 32, 0xe2377ae9, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetMaterialCount>()},
    {"FUN_a0a67a70", 0xa67a70, 32, 0xf6e7ea69, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetMaterial>()},
    {"FUN_a0a67b90", 0xa67b90, 32, 0x9e2153b2, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetViewport>()},
    {"FUN_a0a677f0", 0xa677f0, 32, 0xe29a6299, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetSubrect>()},
    {"FUN_a0a67870", 0xa67870, 31, 0xf2081917, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetFixedAspect>()},
    {"FUN_a0a67a40", 0xa67a40, 32, 0xb005fe70, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetPixelAspect>()},
    {"FUN_a0a67840", 0xa67840, 32, 0x3063cfa2, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetPerspective>()},
    {"FUN_a0a67890", 0xa67890, 32, 0x4c431580, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetDepthRange>()},
    {"FUN_a0a678c0", 0xa678c0, 32, 0x675731e4, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetNearFar>()},
    {"FUN_a0a677c0", 0xa677c0, 32, 0x64598aea, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetOffsets>()},
    {"FUN_a0a679e0", 0xa679e0, 32, 0xb7bc915f, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetOrtho>()},
    {"FUN_a0a67770", 0xa67770, 32, 0xaea66166, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetProjection>()},
    {"FUN_a0a699b0", 0xa699b0, 32, 0x28c84f77, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::GetEntry>()},
    {"FUN_a0a656f0", 0xa656f0, 32, 0xac64ae95, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::UpdateProjection>()},
    {"FUN_a0a69840", 0xa69840, 32, 0xc45a60a2, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::RefreshAspect>()},
    {"FUN_a0a66760", 0xa66760, 32, 0xbd7b09e2, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::GetView>()},
    {"FUN_a0a66800", 0xa66800, 32, 0xa2ca285f, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::GetViewport>()},
    {"FUN_a0a66bc0", 0xa66bc0, 32, 0xe3a7e9a7, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::WorldToScreen>()},
    {"FUN_a0a66a30", 0xa66a30, 32, 0xaaddcbd8, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::WorldToClip>()},
    {"FUN_a0a67e40", 0xa67e40, 32, 0x0ebe9df0, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::ScreenToWorld>()},
    {"FUN_a0a67cb0", 0xa67cb0, 32, 0xee61f90d, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::ClipToWorld>()},
    {"FUN_a0a674d0", 0xa674d0, 32, 0x0be31847, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::Render>()},
    {"FUN_a0a661a0", 0xa661a0, 32, 0x1b7665cf, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::Cull>()},
    {"FUN_a0a68000", 0xa68000, 32, 0x1277fb5d, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::SetAttribute>()},
    {"FUN_a0a66e50", 0xa66e50, 32, 0xfe8d322c, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::GetAttribute>()},
    {"FUN_a0a67be0", 0xa67be0, 29, 0x2d014340, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::IsKindOf>()},
    {"FUN_a0a67c00", 0xa67c00, 32, 0xab7f5289, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::GetTypeInfo>()},
    {"FUN_a0a68660", 0xa68660, 32, 0x24cafe31, MemberAddress<&nGZSceneGraph::cViewerRefNodeImpl::CloneInto>()},
    {"nGZSceneGraph_cViewerRefNodeBehavior_Registry", 0xa68db0, 32, 0x42eeae0f, reinterpret_cast<void *>(&nGZSceneGraph::cViewerRefNodeBehaviorTraits::RegistryFor)},
    {"FUN_a0ae1cb0", 0xae1cb0, 32, 0xcf051bf2, MemberAddress<&nGZSceneGraph::cViewerRefNodeBaseImpl::CloneInto>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
