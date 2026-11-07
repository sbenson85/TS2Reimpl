#include "hook/hooks.h"
#include "sg/cRenderableNodeImpl.h"

namespace {

using nGZSceneGraph::cRenderableNodeImpl;

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cRenderableNodeImpl_cRenderableNodeImpl", 0xadfff0, 32, 0x861353ac, MemberAddress(&cRenderableNodeImpl::Construct)},
    {"FUN_a0ae00a0", 0xae00a0, 32, 0x5a402cb0, MemberAddress(&cRenderableNodeImpl::AddLayer)},
    {"FUN_a0ae00d0", 0xae00d0, 23, 0xa8e02149, MemberAddress(&cRenderableNodeImpl::AddLayerBits)},
    {"FUN_a0ae02a0", 0xae02a0, 32, 0x9040fa5b, MemberAddress(&cRenderableNodeImpl::RemoveLayer)},
    {"FUN_a0ae02d0", 0xae02d0, 25, 0x1c76a9fc, MemberAddress(&cRenderableNodeImpl::RemoveLayerBits)},
    {"FUN_a0ae00f0", 0xae00f0, 32, 0x539eb289, MemberAddress(&cRenderableNodeImpl::CountLayers)},
    {"nGZSceneGraph_cRenderableNodeImpl_MakeLayerNames", 0xae02f0, 32, 0xe6deea63, MemberAddress(&cRenderableNodeImpl::MakeLayerNames)},
    {"FUN_a0ae04b0", 0xae04b0, 32, 0x160f5d47, MemberAddress(&cRenderableNodeImpl::SetWorldTransform)},
    {"FUN_a0ae06b0", 0xae06b0, 23, 0xbed1fb9b, MemberAddress(&cRenderableNodeImpl::RefreshDetailLimit)},
    {"FUN_a0ae0140", 0xae0140, 32, 0x5b42eb5e, MemberAddress(&cRenderableNodeImpl::Read)},
    {"FUN_a0ae03d0", 0xae03d0, 29, 0x99b2f31a, MemberAddress(&cRenderableNodeImpl::IsKindOf)},
    {"FUN_a0ae03f0", 0xae03f0, 32, 0x975a6f68, MemberAddress(&cRenderableNodeImpl::GetTypeInfo)},
    {"FUN_a0ae04f0", 0xae04f0, 32, 0xfba7b4f4, MemberAddress(&cRenderableNodeImpl::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
