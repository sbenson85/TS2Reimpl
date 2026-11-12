#include "hook/hooks.h"
#include "sg/DesignMode.h"

namespace {

const HookSpec kHooks[] = {
    {"DesignMode_ObjectById", 0x3abe70, 26, 0xefb28dcb, &DesignMode::ObjectById},
    {"DesignMode_CollectChildShapes", 0x3aa260, 32, 0xc77be717, &DesignMode::CollectChildShapes},
    {"DesignMode_IsDesignModeSubset", 0x3abe90, 32, 0x82ca01ad, &DesignMode::IsDesignModeSubset},
    {"DesignMode_MaterialsMeshName", 0x3ab850, 32, 0xa04afeba, &DesignMode::MaterialsMeshName},
    {"DesignMode_GetObjectDesignModeNames", 0x3aab80, 32, 0x9db1dd86, &DesignMode::GetObjectDesignModeNames},
    {"DesignMode_GetPortalDesignModeNames", 0x3aaec0, 32, 0xda055cc5, &DesignMode::GetPortalDesignModeNames},
    {"DesignMode_FindObjectDesignMode", 0x3ab990, 32, 0xca885592, &DesignMode::FindObjectDesignMode},
    {"DesignMode_FindGeometryWithSubset", 0x3ab2b0, 32, 0x41bfbe9b, &DesignMode::FindGeometryWithSubset},
    {"DesignMode_FindListedGeometryWithSubset", 0x3ab570, 32, 0xb6eddcd0, &DesignMode::FindListedGeometryWithSubset},
    {"DesignMode_FindListedShapeWithSubset", 0x3abc40, 32, 0xb6eddcd0, &DesignMode::FindListedShapeWithSubset},
    // lane 261111p
    {"DesignMode_SlaveSubsets", 0x3aafa0, 32, 0x37db606a, &DesignMode::SlaveSubsets},
    {"DesignMode_ListedSlaveSubsets", 0x3ab1d0, 32, 0xd5601fcc, &DesignMode::ListedSlaveSubsets},
    {"DesignMode_FindMaterialOverride", 0x3aa720, 32, 0x9051c925, &DesignMode::FindMaterialOverride},
    {"DesignMode_ApplyDesignModeMaterials", 0x3abf70, 32, 0x811ea4a5, &DesignMode::ApplyDesignModeMaterials},
    {"DesignMode_FormatMaterialName", 0x3ab7a0, 32, 0x10ace54e, &DesignMode::FormatMaterialName},
    {"DesignMode_CollectSubsetNames", 0x3aa420, 32, 0x4294937d, &DesignMode::CollectSubsetNames},
};
REGISTER_HOOKS(kHooks);

} // namespace
