#include "gz/cOStreamStringBehaviourAdaptor.h"
#include "hook/hooks.h"
#include "sg/cSceneGraphFactory.h"

// Lane RC. The factory's getter (0xa4d660) and creator (0xa4d600, registered by address under class id 0xe5259c1b; the
// size and CRC cover its 32 first bytes) are hooked; the factory's constructor, destructor and slots are the compiler's,
// in effect through the getter (the game's vtable 0xe6c568 is stored only by the constructor 0xa4d310 and the destructor
// 0xa4d3e0, which the getter and slot 7 reach; slot 7 only through the vtable). The class-id statics are the 6-byte
// `mov eax, imm32; ret` bodies the director and Init call.
namespace {

using nGZSceneGraph::cSceneGraphClassIds;
using nGZSceneGraph::cSceneGraphFactory;

const HookSpec kHooks[] = {
    {"nGZSceneGraph_GetSceneGraphFactory", 0xa4d660, 32, 0xda5deb43, reinterpret_cast<void *>(&cSceneGraphFactory::Get)},
    {"nGZSceneGraph_cSceneGraphFactory_Create", 0xa4d600, 32, 0x1620fd71,
     reinterpret_cast<void *>(&cSceneGraphFactory::Create)},
    {"nGZSceneGraph_cSceneGraphFactory_GetClassId", 0xa4d630, 6, 0xb81fd89c,
     reinterpret_cast<void *>(&cSceneGraphClassIds::Factory)},
    {"nGZSceneGraph_cShape_GetClassId", 0xa5d410, 6, 0xe0ac4e98, reinterpret_cast<void *>(&cSceneGraphClassIds::Shape)},
    {"nGZSceneGraph_cAmbientLight_GetClassId", 0xa47d50, 6, 0xe17cced1,
     reinterpret_cast<void *>(&cSceneGraphClassIds::AmbientLight)},
    {"nGZSceneGraph_cDirectionalLight_GetClassId", 0xa47d60, 6, 0x702d3d96,
     reinterpret_cast<void *>(&cSceneGraphClassIds::DirectionalLight)},
    {"nGZSceneGraph_cPointLight_GetClassId", 0xa47da0, 6, 0x3a9d2537,
     reinterpret_cast<void *>(&cSceneGraphClassIds::PointLight)},
    {"nGZSceneGraph_cSpotLight_GetClassId", 0xa47db0, 6, 0xcf1d83f7,
     reinterpret_cast<void *>(&cSceneGraphClassIds::SpotLight)},
    {"nGZSceneGraph_cView_GetClassId", 0xa66080, 6, 0x5438b466, reinterpret_cast<void *>(&cSceneGraphClassIds::View)},
    {"nGZSceneGraph_cGeometryNode_GetClassId", 0xa425f0, 6, 0xd939a4f3,
     reinterpret_cast<void *>(&cSceneGraphClassIds::GeometryNode)},
    {"nGZSceneGraph_cIndexedMeshBuilder_GetClassId", 0xa80020, 6, 0x5f7a895f,
     reinterpret_cast<void *>(&cSceneGraphClassIds::IndexedMeshBuilder)},
    {"nGZSceneGraph_cCompactorBuilder_GetClassId", 0xa839b0, 6, 0x69233dca,
     reinterpret_cast<void *>(&cSceneGraphClassIds::CompactorBuilder)},
    {"nGZSceneGraph_cBoundingVolumeBuilder_GetClassId", 0xa85110, 6, 0xaa3e7ad6,
     reinterpret_cast<void *>(&cSceneGraphClassIds::BoundingVolumeBuilder)},
    {"nGZSceneGraph_cTangentSpaceBuilder_GetClassId", 0xa86c70, 6, 0x74d47281,
     reinterpret_cast<void *>(&cSceneGraphClassIds::TangentSpaceBuilder)},
    {"nGZSceneGraph_cOldPart_GetClassId", 0xa8f2f0, 6, 0x676bec50, reinterpret_cast<void *>(&cSceneGraphClassIds::OldPart)},
    {"nGZSceneGraph_GetClassId_a925d0", 0xa925d0, 6, 0xb62ebaf5,
     reinterpret_cast<void *>(&cSceneGraphClassIds::Unknown925d0)},
    {"nGZSceneGraph_cMaterialDefinition_GetClassId", 0xaadf80, 6, 0x886c5145,
     reinterpret_cast<void *>(&cSceneGraphClassIds::MaterialDefinition)},
    {"nGZSceneGraph_cTexture_GetClassId", 0xab1660, 6, 0xbf6bfb31, reinterpret_cast<void *>(&cSceneGraphClassIds::Texture)},
    {"nGZSceneGraph_cLevelOfDetail_GetClassId", 0xab1670, 6, 0xcdd5cc87,
     reinterpret_cast<void *>(&cSceneGraphClassIds::LevelOfDetail)},
    {"cOStreamStringBehaviourAdaptor_Flush", 0xa4d5f0, 8, 0x156f8153,
     MemberAddress(&cOStreamStringBehaviourAdaptor::Flush)},
    {"cOStreamStringBehaviourAdaptor_vf21", 0xa4d510, 32, 0xe7236098,
     MemberAddress(&cOStreamStringBehaviourAdaptor::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
