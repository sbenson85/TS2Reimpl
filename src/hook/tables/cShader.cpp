#include "hook/hooks.h"
#include "sg/cShader.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0aa3d20", 0xaa3d20, 32, 0x1bd086c8, nGZSceneGraph::ShaderPartVecDiffer},
    {"FUN_a0aa6b60", 0xaa6b60, 32, 0xa618cc45, MemberAddress(&nGZSceneGraph::ShaderPass::Reset)},
    {"FUN_a0aa6510", 0xaa6510, 32, 0x261e158e, MemberAddress(&nGZSceneGraph::ShaderPass::Equals)},
    {"FUN_a0aa6e50", 0xaa6e50, 32, 0x04d94fcb, MemberAddress(&nGZSceneGraph::ShaderPass::EndState)},
    {"nGZSceneGraph_cShader_QueryInterface", 0xaa6950, 32, 0x4a1b2f44, MemberAddress(&nGZSceneGraph::cShader::QueryInterfaceBody)},
    {"nGZSceneGraph_cShader_vf3", 0xaa6b30, 32, 0x33f8e0e4, MemberAddress(&nGZSceneGraph::cShader::ResetPassesBody)},
    {"nGZSceneGraph_cShader_vf7", 0xaa6470, 32, 0xdb74074e, MemberAddress(&nGZSceneGraph::cShader::IsEqualBody)},
    {"nGZSceneGraph_cShader_vf11", 0xaa69e0, 32, 0x84be3992, MemberAddress(&nGZSceneGraph::cShader::ApplyBody)},
    {"nGZSceneGraph_cShader_vf10", 0xaa6c80, 32, 0xb88e4889, MemberAddress(&nGZSceneGraph::cShader::ValidateBody)},
    {"FUN_a0aa6e80", 0xaa6e80, 32, 0xd0ec42f0, MemberAddress(&nGZSceneGraph::ShaderPass::Validate)},
    {"FUN_a0aa4ea0", 0xaa4ea0, 32, 0x4affde65, MemberAddress(&nGZSceneGraph::ShaderPass::Destruct)},
    {"FUN_a0aa4ce0", 0xaa4ce0, 32, 0xa08c20a1, MemberAddress(&nGZSceneGraph::cShader::PassVec::Destroy)},
    {"FUN_a0aa4d70", 0xaa4d70, 25, 0x6234692b, MemberAddress(&nGZSceneGraph::cShader::DestructBody)},
    {"nGZSceneGraph_cShader_vf13", 0xaa4e60, 32, 0x8bb6fb6c, MemberAddress(&nGZSceneGraph::cShader::DeleteBody)},
    {"FUN_a0aa4d90", 0xaa4d90, 32, 0xd6d1df59, &nGZSceneGraph::cShader::PoolNew},
    {"FUN_a0aa4dc0", 0xaa4dc0, 32, 0xe1639bd8, &nGZSceneGraph::cShader::PoolDelete},
    {"FUN_a0aa4a40", 0xaa4a40, 32, 0xfebd5779, MemberAddress(&nGZSceneGraph::cShader::Construct)},
    {"FUN_a0aa4fa0", 0xaa4fa0, 32, 0x349acb12, &nGZSceneGraph::cShader::Create},
    {"nGZSceneGraph_cShader_vf12", 0xaa65e0, 32, 0x3f7046ff, MemberAddress(&nGZSceneGraph::cShader::DumpBody)},
    {"FUN_a098bf40", 0x98bf40, 32, 0xc495ef0c, nGZSceneGraph::ShaderHashMix},
    {"FUN_a098c220", 0x98c220, 32, 0x7ed70676, nGZSceneGraph::ShaderHashCombine},
    {"FUN_a0aa4980", 0xaa4980, 32, 0x28761359, MemberAddress(&nGZSceneGraph::ShaderPartVec::CopyConstruct)},
    {"FUN_a0aa4ae0", 0xaa4ae0, 32, 0x57b71dc6, MemberAddress(&nGZSceneGraph::ShaderPass::CopyConstruct)},
    {"nGZSceneGraph_cShader_vf8", 0xaa5030, 32, 0x0a76450c, MemberAddress(&nGZSceneGraph::cShader::BuildBody)},
    {"FUN_a0aa53e0", 0xaa53e0, 32, 0xa3f30b4c, MemberAddress(&nGZSceneGraph::ShaderPass::Setup)},
    {"FUN_a0aa4fe0", 0xaa4fe0, 32, 0xfd64f9d2, &nGZSceneGraph::ShaderPartVecHash},
    {"FUN_a0aa44d0", 0xaa44d0, 32, 0xeed63110, MemberAddress(&nGZSceneGraph::ShaderPass::Apply<false, false, false>)},
    {"FUN_a0aa3d70", 0xaa3d70, 32, 0x947373b4, MemberAddress(&nGZSceneGraph::ShaderPass::Apply<true, false, false>)},
    {"FUN_a0aa3e90", 0xaa3e90, 32, 0xeed63110, MemberAddress(&nGZSceneGraph::ShaderPass::Apply<false, true, false>)},
    {"FUN_a0aa3f70", 0xaa3f70, 32, 0x947373b4, MemberAddress(&nGZSceneGraph::ShaderPass::Apply<true, true, false>)},
    {"FUN_a0aa40a0", 0xaa40a0, 32, 0x93886afb, MemberAddress(&nGZSceneGraph::ShaderPass::Apply<false, false, true>)},
    {"FUN_a0aa4180", 0xaa4180, 32, 0x93886afb, MemberAddress(&nGZSceneGraph::ShaderPass::Apply<true, false, true>)},
    {"FUN_a0aa42b0", 0xaa42b0, 32, 0x93886afb, MemberAddress(&nGZSceneGraph::ShaderPass::Apply<false, true, true>)},
    {"FUN_a0aa43a0", 0xaa43a0, 32, 0x93886afb, MemberAddress(&nGZSceneGraph::ShaderPass::Apply<true, true, true>)},
    {"FUN_a0aa47f0", 0xaa47f0, 32, 0xa502e3de, &nGZSceneGraph::cShader::PassVec::FillDefault},
    {"FUN_a0aa45e0", 0xaa45e0, 32, 0xe76be44e, MemberAddress(&nGZSceneGraph::cShader::PassVec::Reallocate)},
};
REGISTER_HOOKS(kHooks);

} // namespace
