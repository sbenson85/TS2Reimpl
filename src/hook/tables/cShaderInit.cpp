#include "hook/hooks.h"
#include "sg/cShaderInit.h"

namespace {

using nGZSceneGraph::cShaderInit;

const HookSpec kHooks[] = {
    {"FUN_a0aa7b90", 0xaa7b90, 32, 0x8a4eacb5, MemberAddress(&cShaderInit::Construct)},
    {"FUN_a0aa7bc0", 0xaa7bc0, 32, 0x0a3f9345, MemberAddress(&nGZSceneGraph::ShaderInitData::Construct)},
    {"FUN_a0aa7d70", 0xaa7d70, 32, 0x3a8970ce, MemberAddress(&cShaderInit::DestructBody)},
    {"FUN_a0aa7cf0", 0xaa7cf0, 32, 0x88909144, MemberAddress(&nGZSceneGraph::DescriptorVec::Destroy)},
    {"FUN_a0c631d0", 0xc631d0, 15, 0xabbd0744, MemberAddress(&nGZSceneGraph::NamePool::At)},
    {"nGZSceneGraph_cShaderInit_vf26", 0xaa7f60, 32, 0xfb76de33, MemberAddress(&cShaderInit::DeleteBody)},
    {"FUN_a0aa82a0", 0xaa82a0, 32, 0xf1be488a, &cShaderInit::Create},
    {"FUN_a0aa8460", 0xaa8460, 32, 0x0e419947, MemberAddress(&cShaderInit::Element)},
    {"nGZSceneGraph_cShaderInit_QueryInterface", 0xaa8430, 32, 0x1e0ea5ad, MemberAddress(&cShaderInit::QueryInterfaceBody)},
    {"nGZSceneGraph_cShaderInit_vf3", 0xaa8180, 32, 0xf8cbf0fa, MemberAddress(&cShaderInit::CloneBody)},
    {"nGZSceneGraph_cShaderInit_vf5", 0xaa85b0, 32, 0xcc31e89a, MemberAddress(&cShaderInit::SetObject00Body)},
    {"nGZSceneGraph_cShaderInit_vf6", 0xaa8630, 32, 0xf7f006f3, MemberAddress(&cShaderInit::SetObject1cBody)},
    {"nGZSceneGraph_cShaderInit_vf7", 0xaa85f0, 32, 0xd53b25fd, MemberAddress(&cShaderInit::SetObject20Body)},
    {"nGZSceneGraph_cShaderInit_vf14", 0xaa8720, 32, 0xb33c7dba, MemberAddress(&cShaderInit::SetObject24Body)},
    {"nGZSceneGraph_cShaderInit_vf15", 0xaa8570, 32, 0x7cc5b3ca, MemberAddress(&cShaderInit::SetObject28Body)},
    {"nGZSceneGraph_cShaderInit_vf8", 0xaa80b0, 32, 0x63a0640d, MemberAddress(&cShaderInit::AddPartABody)},
    {"nGZSceneGraph_cShaderInit_vf9", 0xaa7f90, 32, 0x63a0640d, MemberAddress(&cShaderInit::AddPartBBody)},
    {"nGZSceneGraph_cShaderInit_vf10", 0xaa8030, 32, 0x12f579cf, MemberAddress(&cShaderInit::AddObjectBody)},
    {"nGZSceneGraph_cShaderInit_vf11", 0xaa8160, 29, 0x83984d6f, MemberAddress(&cShaderInit::AddPassNameBody)},
    {"nGZSceneGraph_cShaderInit_vf12", 0xaa8150, 8, 0xd056fb19, MemberAddress(&cShaderInit::AddNameBody)},
    {"nGZSceneGraph_cShaderInit_vf13", 0xaa8690, 32, 0x94f02e64, MemberAddress(&cShaderInit::SetTextureBody)},
    {"nGZSceneGraph_cShaderInit_vf16", 0xaa8700, 27, 0x43c73d14, MemberAddress(&cShaderInit::SetByteC4Body)},
    {"nGZSceneGraph_cShaderInit_vf17", 0xaa8550, 27, 0x2807ef4a, MemberAddress(&cShaderInit::SetByteC5Body)},
    {"nGZSceneGraph_cShaderInit_vf18", 0xaa8670, 27, 0x57264c52, MemberAddress(&cShaderInit::SetByteC6Body)},
    {"nGZSceneGraph_cShaderInit_vf19", 0xaa8530, 27, 0x913bcc99, MemberAddress(&cShaderInit::SetByteC7Body)},
    {"nGZSceneGraph_cShaderInit_vf20", 0xaa8510, 23, 0x82192af7, MemberAddress(&cShaderInit::SetRangeBody)},
    {"nGZSceneGraph_cShaderInit_vf22", 0xaa83c0, 32, 0x819ab805, MemberAddress(&cShaderInit::PartAIsBody)},
    {"nGZSceneGraph_cShaderInit_vf23", 0xaa8310, 32, 0x819ab805, MemberAddress(&cShaderInit::PartBIsBody)},
    {"nGZSceneGraph_cShaderInit_vf24", 0xaa8380, 32, 0xce6db594, MemberAddress(&cShaderInit::ObjectIsBody)},
    {"nGZSceneGraph_cShaderInit_vf25", 0xaa8760, 32, 0x2b79bc3a, MemberAddress(&cShaderInit::TextureOfBody)},
    {"FUN_a0c62f50", 0xc62f50, 32, 0x26c7389f, MemberAddress(&nGZSceneGraph::NamePool::Construct)},
    {"FUN_a0c62ee0", 0xc62ee0, 32, 0xc2513d35, MemberAddress(&nGZSceneGraph::NamePool::CopyConstruct)},
    {"FUN_a0c630e0", 0xc630e0, 32, 0x23f9449e, MemberAddress(&nGZSceneGraph::NamePool::Clear)},
    {"FUN_a0c62f90", 0xc62f90, 32, 0xf2aa07eb, MemberAddress(&nGZSceneGraph::NamePool::Destroy)},
    {"FUN_a0c63030", 0xc63030, 32, 0x2f8a9af9, MemberAddress(&nGZSceneGraph::NamePool::Assign)},
    {"FUN_a0c63120", 0xc63120, 32, 0x9dd2f645, MemberAddress(&nGZSceneGraph::NamePool::Add)},
    {"FUN_a0aa7500", 0xaa7500, 32, 0x1188496d, &nGZSceneGraph::DescriptorVec::DestroyRange},
    {"FUN_a0aa7a90", 0xaa7a90, 32, 0x2f7a5145, &nGZSceneGraph::DescriptorVec::FillDefault},
    {"FUN_a0aa7c80", 0xaa7c80, 32, 0x5e260bad, MemberAddress(&nGZSceneGraph::ShaderTextureSlot::CopyConstruct)},
    {"FUN_a0aa7db0", 0xaa7db0, 32, 0xdc04140c, MemberAddress(&nGZSceneGraph::ShaderDescriptor::Assign)},
    {"FUN_a0aa77c0", 0xaa77c0, 32, 0x452fda8b, &nGZSceneGraph::DescriptorVec::CopyRange},
    {"FUN_a0aa7930", 0xaa7930, 32, 0x3228ea1d, &nGZSceneGraph::DescriptorVec::CopyCount},
    {"FUN_a0aa72f0", 0xaa72f0, 32, 0xe40d03a4, &nGZSceneGraph::DescriptorVec::AssignCount},
    {"FUN_a0aa75e0", 0xaa75e0, 32, 0xa2379f7d, MemberAddress(&nGZSceneGraph::DescriptorVec::Reallocate)},
    {"FUN_a0aa7130", 0xaa7130, 32, 0x7347fa06, MemberAddress(&nGZSceneGraph::DescriptorVec::Assign)},
};

REGISTER_HOOKS(kHooks);

} // namespace
