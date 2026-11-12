#include "hook/hooks.h"
#include "ts/cTSEffectCurve.h"

// The effects' value curve (lane IW): thiscall members hooked to ours, the constructors to their adapters.
namespace {

const HookSpec kHooks[] = {
    {"cTSEffectCurve_Construct", 0xbdc8f0, 32, 0x3ddbc932, reinterpret_cast<void *>(&cTSEffectCurveAdapters::Construct)},
    {"cTSEffectCurve_ConstructValue", 0xbdc880, 32, 0x3ddbc932, reinterpret_cast<void *>(&cTSEffectCurveAdapters::ConstructValue)},
    {"cTSEffectCurve_Reset", 0xbdcb80, 32, 0x49057cf4, MemberAddress(&cTSEffectCurve::Reset)},
    {"cTSEffectCurve_SetConstant", 0xbdcd10, 32, 0xaa45b072, MemberAddress(&cTSEffectCurve::SetConstant)},
    {"cTSEffectCurve_SetKind", 0xbdcd40, 18, 0x1b0e1ffd, MemberAddress(&cTSEffectCurve::SetKind)},
    {"cTSEffectCurve_Simplify", 0xbdcd60, 32, 0x7c0d66a7, MemberAddress(&cTSEffectCurve::Simplify)},
    {"cTSEffectCurve_SetValues", 0xbdcbd0, 32, 0xd8a1a1bc, MemberAddress(&cTSEffectCurve::SetValues)},
    {"cTSEffectCurve_GetMax", 0xbdca50, 32, 0xfc0d3f4d, MemberAddress(&cTSEffectCurve::GetMax)},
    {"cTSEffectCurve_Read", 0xbdcac0, 32, 0x1b0d25b6, MemberAddress(&cTSEffectCurve::Read)},
    {"cTSEffectCurve_Write", 0xbdce40, 32, 0xcb8664ca, MemberAddress(&cTSEffectCurve::Write)},
    // lane IX
    {"cTSEffectCurve_Scale", 0xbdc950, 32, 0x0dce293d, MemberAddress(&cTSEffectCurve::Scale)},
    {"cTSEffectCurve_Divide", 0xbdc9f0, 32, 0x5d725c62, MemberAddress(&cTSEffectCurve::Divide)},
    {"cTSEffectCurve_Destroy", 0xbe1b80, 32, 0x4b9b22db, reinterpret_cast<void *>(&cTSEffectCurveAdapters::Destroy)},
    {"cTSEffectCurve_Assign", 0xbe21f0, 32, 0x25b8bf44, reinterpret_cast<void *>(&cTSEffectCurveAdapters::Assign)},
    {"cTSEffectCurve_CheckSinCoefficients", 0xbdcdb0, 32, 0x94cf18eb, MemberAddress<&cTSEffectCurve::CheckSinCoefficients>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
