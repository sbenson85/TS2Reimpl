// The two cLightT level constructors as hook targets (lane TL). See cLightLevels.h.
#include "sg/cLightLevels.h"

// The decorated names of the template constructors (`??0<class>@QAE@XZ`): the linker fails if one is wrong.
#pragma comment(linker, "/alternatename:_cLightT_StandardLight_Constructor=??0?$cLightT@VcIStandardLight@nGZSceneGraph@@UcStandardLightBehaviorTraits@2@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_cLightT_ShadeLight_Constructor=??0?$cLightT@VcILight@nGZSceneGraph@@UcLightBehaviorTraits@2@@nGZSceneGraph@@QAE@XZ")

extern "C" const int cLightLevels_Anchor = 1;

namespace nGZSceneGraph {

// Explicit instantiations emit the constructors (and every other member) so the C names above resolve.
template class cLightT<cIStandardLight, cStandardLightBehaviorTraits>;
template class cLightT<cILight, cLightBehaviorTraits>;

} // namespace nGZSceneGraph
