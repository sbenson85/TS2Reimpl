#include "hook/hooks.h"
#include "sg/cGeometryNode.h"
#include "sg/cGeometryNodeFormats.h"

namespace {

using nGZSceneGraph::cGeometryNode;
using Adapters = nGZSceneGraph::cGeometryNodeAdapters;

// nGZSceneGraph::cGeometryNode in the standalone shape (src/sg/cGeometryNode.h, lane QU, D3 part 4): the base constructor
// 0xa3dd80 points at ours (every game-built node, from the factory, the clone paths and the record readers, is ours), so
// its vtables are ours and the slot hooks below cover what the game reaches by RVA. The geometry build 0xa3f180 is ours
// too (part 5), and so are the render slots 0xa41ee0 / 0xa429b0 / 0xa435d0 and the device reset 0xa419e0 (part 6: the
// render slots are this class's own bodies, every vtable holding them a cGeometryNode's), and the event registry 0xa42f30
// (hooked: its first-use holder 0xa3cf50 calls back into it, so neither can be cut through callers alone; the holder is
// trapped). The vertex-format helpers 0x18daa0 / 0x18d990 / 0xc627e0 the render slots and the shader pass setup share
// are hooked (src/sg/cGeometryNodeFormats.h). The subset iterators are in effect through their callers (trapped). Left original: the shared template bodies
// (folded across classes) and the 5- and 6-byte bodies.
const HookSpec kHooks[] = {
    {"cGeometryNode_Construct", 0xa3dd80, 32, 0x9ce9788b, const_cast<uint8_t *>(&QU_cGeometryNode_Constructor)},
    {"cGeometryNode_Destruct", 0xa3e320, 32, 0x8a82d287, reinterpret_cast<void *>(&Adapters::Destruct)},
    {"cGeometryNode_Create", 0xa42590, 32, 0x171b5607, reinterpret_cast<void *>(&cGeometryNode::Create)},
    {"cGeometryNode_Release", 0xa43040, 32, 0x668c8a3a, reinterpret_cast<void *>(&Adapters::Release)},
    {"cGeometryNode_Reset", 0xa3ee00, 32, 0x0eee725b, reinterpret_cast<void *>(&Adapters::Reset)},
    {"cGeometryNode_ReleaseElementResources", 0xa3ed70, 32, 0x3c642da5, reinterpret_cast<void *>(&Adapters::ReleaseElementResources)},
    {"cGeometryNode_QueryInterface", 0xa42b20, 32, 0x9d8ebcf0, reinterpret_cast<void *>(&Adapters::QueryInterface)},
    {"cGeometryNode_IsKindOf", 0xa43860, 29, 0x526da213, reinterpret_cast<void *>(&Adapters::IsKindOf)},
    {"cGeometryNode_GetRefCount", 0xa42640, 8, 0xb675b7f4, reinterpret_cast<void *>(&Adapters::GetRefCount)},
    {"cGeometryNode_OnEvent", 0xa42760, 32, 0x6f39a79c, reinterpret_cast<void *>(&Adapters::OnEvent)},
    {"cGeometryNode_NotifyListeners", 0xa43410, 32, 0x2178ef07, reinterpret_cast<void *>(&Adapters::NotifyListeners)},
    {"cGeometryNode_AskListeners", 0xa42510, 32, 0xc4271e2a, reinterpret_cast<void *>(&Adapters::AskListeners)},
    {"cGeometryNode_Connect", 0xa3f0e0, 32, 0x5f8b04f0, reinterpret_cast<void *>(&Adapters::Connect)},
    {"cGeometryNode_Disconnect", 0xa423a0, 32, 0x4429da53, reinterpret_cast<void *>(&Adapters::Disconnect)},
    {"cGeometryNode_GetEventCount", 0xa42880, 32, 0x8ff279cf, reinterpret_cast<void *>(&Adapters::GetEventCount)},
    {"cGeometryNode_GetEventIds", 0xa3e890, 32, 0x9f4eb9f1, reinterpret_cast<void *>(&Adapters::GetEventIds)},
    {"cGeometryNode_GetEventAttribute", 0xa3eb00, 32, 0x44150bbb, reinterpret_cast<void *>(&Adapters::GetEventAttribute)},
    {"cGeometryNode_GetRender", 0xa3e880, 10, 0x721ffc7d, reinterpret_cast<void *>(&Adapters::GetRender)},
    {"cGeometryNode_AddPart", 0xa3e790, 32, 0xad2ccd30, reinterpret_cast<void *>(&Adapters::AddPart)},
    {"cGeometryNode_ClearParts", 0xa430c0, 32, 0xc2f634f3, reinterpret_cast<void *>(&Adapters::ClearParts)},
    {"cGeometryNode_Rebuild", 0xa3ebe0, 32, 0x7c06a0a9, reinterpret_cast<void *>(&Adapters::Rebuild)},
    {"cGeometryNode_BuildGeometry", 0xa3f180, 32, 0xc57744ca, reinterpret_cast<void *>(&Adapters::BuildGeometry)},
    {"cGeometryNode_AddListener", 0xa3e830, 32, 0xb64befca, reinterpret_cast<void *>(&Adapters::AddListener)},
    {"cGeometryNode_RemoveListener", 0xa43160, 32, 0x4e600743, reinterpret_cast<void *>(&Adapters::RemoveListener)},
    {"cGeometryNode_GetContainer", 0xa42600, 32, 0x1482e85a, reinterpret_cast<void *>(&Adapters::GetContainer)},
    {"cGeometryNode_SetBounds", 0xa43470, 32, 0xa8e01fc9, reinterpret_cast<void *>(&Adapters::SetBounds)},
    {"cGeometryNode_GetSubsetNames", 0xa436a0, 32, 0x47e97349, reinterpret_cast<void *>(&Adapters::GetSubsetNames)},
    {"cGeometryNode_CountSubset", 0xa428d0, 32, 0xfa3356a6, reinterpret_cast<void *>(&Adapters::CountSubset)},
    {"cGeometryNode_GetSubsetParts", 0xa43770, 32, 0x365cf4ac, reinterpret_cast<void *>(&Adapters::GetSubsetParts)},
    {"cGeometryNode_Read", 0xa42bb0, 32, 0x3171cf92, reinterpret_cast<void *>(&Adapters::Read)},
    {"cGeometryNode_GetTypeInfo", 0xa43890, 32, 0xdf3e8f80, reinterpret_cast<void *>(&Adapters::GetTypeInfo)},
    {"cGeometryNode_Write", 0xa43950, 32, 0x2a87e924, reinterpret_cast<void *>(&Adapters::Write)},
    {"cGeometryNode_IsSupported", 0xa41ee0, 32, 0x6bb674cb, reinterpret_cast<void *>(&Adapters::IsSupported)},
    {"cGeometryNode_Prepare", 0xa429b0, 32, 0xa5bd45b4, reinterpret_cast<void *>(&Adapters::Prepare)},
    {"cGeometryNode_Render", 0xa435d0, 32, 0x0f7b4f08, reinterpret_cast<void *>(&Adapters::Render)},
    {"cGeometryNode_ResetDeviceState", 0xa419e0, 32, 0x280c9f78, reinterpret_cast<void *>(&Adapters::ResetDeviceState)},
    {"cGeometryNode_Registry", 0xa42f30, 32, 0x65b64681, reinterpret_cast<void *>(&nGZSceneGraph::cGeometryNodeBehaviorTraits::RegistryFor)},
    {"MakeDeviceVertexFormat", 0x18daa0, 32, 0x0f2aa248, reinterpret_cast<void *>(&nGZSceneGraph::MakeDeviceVertexFormat)},
    {"MakeStreamVertexFormat", 0x18d990, 32, 0xe6cb43b2, reinterpret_cast<void *>(&nGZSceneGraph::MakeStreamVertexFormat)},
    {"CopyVertexStream", 0xc627e0, 32, 0x2b4395b4, reinterpret_cast<void *>(&nGZSceneGraph::CopyVertexStream)},
    {"nGZSceneGraph_IsKnownVertexElement", 0x18da20, 32, 0x3dbe5c65, reinterpret_cast<void *>(&nGZSceneGraph::IsKnownVertexElement)},
};
REGISTER_HOOKS(kHooks);

} // namespace
