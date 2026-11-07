// nGZGraphic4::cDraw2D and cDraw2DSystemService (lane OD, src/gz/cDraw2D.h). The COM director's creator for class
// 0x4c9282f8 (the service) is the switch: every service is ours, its Init makes our cDraw2D (the constructor 0x14ba80
// is reached only from there), so the slots of both classes, the destructors and the non-virtual helpers only they call
// are in effect through it. The rectangle intersection 0x150e20 has callers all over the image (windows, terrain, the
// atlas code still in the game) and is hooked as itself. The pixel upload (UploadRecord, UploadKeyed32) and the draw
// system getter are reached only from cDraw2D code (lane OZ), hooked as themselves like the other helpers.
#include "hook/hooks.h"
#include "gz/cDraw2D.h"
#include "gz/cIGZDrawContextEngine.h"
#include "rz/ServiceGetters.h"

namespace {

const HookSpec kHooks[] = {
    {"cDraw2DSystemService_Create", 0x14f2b0, 32, 0x554a0b33, reinterpret_cast<void *>(&nGZGraphic4::cDraw2DSystemService::Create)},
    {"cDraw2D_Intersect", 0x150e20, 32, 0x341f895d, reinterpret_cast<void *>(&nGZGraphic4::cDraw2D::Intersect)},
    {"FUN_a014f330", 0x14f330, 32, 0x76d28e5a,
     reinterpret_cast<void *>(&CachedServiceGetter<nGZGraphic4::cDraw2D::kDrawSystemCell, cIGZDrawSystem::kServiceId,
                                                   cIGZDrawSystem::kIid>)},
    {"FUN_a01516f0", 0x1516f0, 32, 0xaee4a954, reinterpret_cast<void *>(&nGZGraphic4::cDraw2DBehaviorTraits::RegistryFor)},
    {"nGZGraphic4_cDraw2D_UploadRecord", 0x152410, 32, 0x3e9b03da, MemberAddress<&nGZGraphic4::cDraw2D::UploadRecord>()},
    {"nGZGraphic4_cDraw2D_UploadKeyed32", 0x14dfa0, 32, 0x8010aafd, reinterpret_cast<void *>(&nGZGraphic4::cDraw2D::UploadKeyed32)},
};
REGISTER_HOOKS(kHooks);

} // namespace
