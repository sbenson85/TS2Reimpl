#include "hook/hooks.h"
#include "sg/cStandardLight.h"

namespace {

using Adapters = nGZSceneGraph::cStandardLightAdapters;

// The director's factories (src/rz/COMDirectorData.cpp registers their game addresses): every ambient and directional
// light the director makes is ours. The game's lights derived from these (the nTSSG lights, the game's point light until
// its own constructor is hooked) still call the base constructors and destructors, now ours (lane VW), and run the slot
// bodies their vtables hold, now the adapters (src/sg/cStandardLight.h). Not hooked: the shared Read 0xa48ce0 (a
// shared_vf whose other referrers are the nTSSG lights' vtables, the game's).
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cAmbientLight_Create", 0xa47ab0, 32, 0xfb337181,
     reinterpret_cast<void *>(&nGZSceneGraph::cAmbientLight::Create)},
    {"nGZSceneGraph_cDirectionalLight_Create", 0xa47b10, 32, 0xc0158682,
     reinterpret_cast<void *>(&nGZSceneGraph::cDirectionalLight::Create)},
    {"nGZSceneGraph_cStandardLight_EventRegistry", 0xa49210, 32, 0x9daf492c, reinterpret_cast<void *>(&nGZSceneGraph::cStandardLightBehaviorTraits::RegistryFor)},
    // The constructors and destructors (our compiler's, by their /alternatename names).
    {"nGZSceneGraph_cStandardLightBase_Construct", 0xa46250, 32, 0x0c0dce68, const_cast<uint8_t *>(&cStandardLightBase_Constructor)},
    {"nGZSceneGraph_cAmbientLight_Construct", 0xa45d40, 32, 0x4f2386c8, const_cast<uint8_t *>(&cAmbientLight_Constructor)},
    {"nGZSceneGraph_cDirectionalLight_Construct", 0xa45db0, 32, 0xc740babd, const_cast<uint8_t *>(&cDirectionalLight_Constructor)},
    {"nGZSceneGraph_cStandardLightBase_Destruct", 0xa467e0, 32, 0x1a0bb166, const_cast<uint8_t *>(&cStandardLightBase_Destructor)},
    {"nGZSceneGraph_cAmbientLight_Destruct", 0xa465f0, 32, 0xd3ea8e03, const_cast<uint8_t *>(&cAmbientLight_Destructor)},
    {"nGZSceneGraph_cDirectionalLight_Destruct", 0xa46630, 32, 0xce229786, const_cast<uint8_t *>(&cDirectionalLight_Destructor)},
    // The part (+0).
    {"nGZSceneGraph_cStandardLightBase_QueryInterface", 0xa48a70, 32, 0x6fe8d2a1, reinterpret_cast<void *>(&Adapters::QueryInterface)},
    {"nGZSceneGraph_cStandardLight_Release", 0x3c9030, 8, 0x12fbd6fc, reinterpret_cast<void *>(&Adapters::Release)},
    {"nGZSceneGraph_cStandardLightBase_OnLightChanged", 0xa488a0, 32, 0x184be534, reinterpret_cast<void *>(&Adapters::OnLightChanged)},
    {"nGZSceneGraph_cAmbientLight_OnLightChanged", 0xa48230, 32, 0x969c96b9, reinterpret_cast<void *>(&Adapters::AmbientOnLightChanged)},
    {"nGZSceneGraph_cDirectionalLight_OnLightChanged", 0xa482b0, 32, 0x969c96b9, reinterpret_cast<void *>(&Adapters::DirectionalOnLightChanged)},
    // The node face (+0x2c).
    {"nGZSceneGraph_cStandardLightBase_QueryInterface_node", 0x3c89e3, 8, 0x0ca779d1, reinterpret_cast<void *>(&Adapters::NodeQueryInterface)},
    {"nGZSceneGraph_cStandardLight_Release_node", 0x3c9038, 8, 0xfddceec2, reinterpret_cast<void *>(&Adapters::NodeRelease)},
    {"nGZSceneGraph_cStandardLight_NotifyListeners", 0x3c9930, 32, 0xc482d780, reinterpret_cast<void *>(&Adapters::NotifyListeners)},
    {"nGZSceneGraph_cStandardLight_AskListeners", 0x3c87b0, 32, 0xb6006c82, reinterpret_cast<void *>(&Adapters::AskListeners)},
    {"nGZSceneGraph_cStandardLight_GetEventCount", 0xa481e0, 32, 0x8ff279cf, reinterpret_cast<void *>(&Adapters::GetEventCount)},
    {"nGZSceneGraph_cStandardLight_GetEventIds", 0xa46c50, 32, 0x9f4eb9f1, reinterpret_cast<void *>(&Adapters::GetEventIds)},
    {"nGZSceneGraph_cStandardLightBase_IsKindOf", 0xa4a5d0, 29, 0x23050a16, reinterpret_cast<void *>(&Adapters::IsKindOf)},
    {"nGZSceneGraph_cStandardLightBase_GetStandardLight", 0xa46c40, 11, 0xc761f982, reinterpret_cast<void *>(&Adapters::GetStandardLight)},
    {"nGZSceneGraph_cStandardLightBase_SetLightName", 0xa4a1f0, 32, 0x83982ce2, reinterpret_cast<void *>(&Adapters::SetLightName)},
    {"nGZSceneGraph_cStandardLightBase_SetColor", 0xa49f40, 32, 0x591aa6ea, reinterpret_cast<void *>(&Adapters::SetColor)},
    {"nGZSceneGraph_cStandardLightBase_SetIntensity", 0xa4a110, 32, 0x4f9cdda2, reinterpret_cast<void *>(&Adapters::SetIntensity)},
    {"nGZSceneGraph_cStandardLightBase_SetFactor", 0xa4a4c0, 32, 0xbccb8f16, reinterpret_cast<void *>(&Adapters::SetFactor)},
    {"nGZSceneGraph_cStandardLightBase_GetLightName", 0xa480a0, 27, 0xb43612c4, reinterpret_cast<void *>(&Adapters::GetLightName)},
    {"nGZSceneGraph_cStandardLightBase_GetColor", 0xa47400, 22, 0x66befacb, reinterpret_cast<void *>(&Adapters::GetColor)},
    {"nGZSceneGraph_cStandardLightBase_GetRange", 0xa48cc0, 7, 0x2ae6151a, reinterpret_cast<void *>(&Adapters::GetRange)},
    {"nGZSceneGraph_cAmbientLight_Clone", 0xa47080, 32, 0xbdce1e6e, reinterpret_cast<void *>(&Adapters::AmbientClone)},
    {"nGZSceneGraph_cDirectionalLight_Clone", 0xa47100, 32, 0xbdce1e6e, reinterpret_cast<void *>(&Adapters::DirectionalClone)},
    {"nGZSceneGraph_cDirectionalLight_GetParams", 0xa48b40, 32, 0xc695cfb9, reinterpret_cast<void *>(&Adapters::DirectionalGetParams)},
    // The persist face (+0x34).
    {"nGZSceneGraph_cStandardLightBase_QueryInterface_persist", 0x3c89eb, 8, 0xa0f0c76f, reinterpret_cast<void *>(&Adapters::PersistQueryInterface)},
    {"nGZSceneGraph_cStandardLight_Release_persist", 0x3c9040, 8, 0x87f41144, reinterpret_cast<void *>(&Adapters::PersistRelease)},
    {"nGZSceneGraph_cStandardLightBase_Read", 0xa49070, 32, 0x7d6e26a4, reinterpret_cast<void *>(&Adapters::Read)},
    {"nGZSceneGraph_cStandardLightBase_Write", 0xa4b250, 32, 0x045a834f, reinterpret_cast<void *>(&Adapters::Write)},
    {"nGZSceneGraph_cAmbientLight_GetTypeInfo", 0xa4a6d0, 32, 0x0ac5aaa6, reinterpret_cast<void *>(&Adapters::AmbientGetTypeInfo)},
    {"nGZSceneGraph_cAmbientLight_Write", 0xa4ade0, 32, 0x045a834f, reinterpret_cast<void *>(&Adapters::AmbientWrite)},
    {"nGZSceneGraph_cDirectionalLight_GetTypeInfo", 0xa4a790, 32, 0xd1d127d2, reinterpret_cast<void *>(&Adapters::DirectionalGetTypeInfo)},
    {"nGZSceneGraph_cDirectionalLight_Write", 0xa4ae60, 32, 0x045a834f, reinterpret_cast<void *>(&Adapters::DirectionalWrite)},
    // The records and the copy.
    {"nGZSceneGraph_cAmbientLight_GetRecord19", 0xa48140, 6, 0xdb5a0def, reinterpret_cast<void *>(&Adapters::AmbientRecord19)},
    {"nGZSceneGraph_cDirectionalLight_GetRecord19", 0xa48150, 6, 0x7e4a776c, reinterpret_cast<void *>(&Adapters::DirectionalRecord19)},
    {"nGZSceneGraph_cStandardLightBase_CopyTo", 0xa47ff0, 32, 0x58d43a00, reinterpret_cast<void *>(&Adapters::CopyTo)},
};
REGISTER_HOOKS(kHooks);

} // namespace
