#include "hook/hooks.h"
#include "gz/DxtBlocks.h"
#include "gz/GraphicServices.h"
#include "gz/MipKernels.h"
#include "gz/cImageManipDXMemory.h"

using namespace nGZGraphic4;

namespace {

// The memory-texture mip chain (lane OW): the row kernels, the DXT block decoders, the level routines, the two entry points
// of cImageManipDX's copy and normal-map slots, and the graphic system's cached manager getters.
#define KERNEL(Name, Rva, Crc) {"mip_" #Name, Rva, 32, Crc, reinterpret_cast<void *>(&mip::Name)}
#define PLAIN(Group, Name, Rva, Crc) {#Group "_" #Name, Rva, 32, Crc, reinterpret_cast<void *>(&Group::Name)}

const HookSpec kHooks[] = {
    KERNEL(HalveRow8888, 0x185280, 0x501aa7e6),
    KERNEL(FilterRow8888, 0x1852c0, 0x0c9e7238),
    KERNEL(HalveRow2101010, 0x185100, 0x501aa7e6),
    KERNEL(FilterRow2101010, 0x185140, 0x0c9e7238),
    KERNEL(HalveRow8, 0x185400, 0x6d83e46f),
    KERNEL(FilterRow8, 0x185430, 0x171aef52),
    KERNEL(CombineTwoRows8888, 0x1862a0, 0x83f01b3b),
    KERNEL(CombineFourRows8888, 0x1862e0, 0xef4d2424),
    KERNEL(CombineTwoRows2101010, 0x1861c0, 0x83f01b3b),
    KERNEL(CombineFourRows2101010, 0x186200, 0xef4d2424),
    KERNEL(CombineTwoRows8, 0x186380, 0xf081d9c0),
    KERNEL(CombineFourRows8, 0x1863c0, 0xb293322a),
    PLAIN(dxt, DecodeDxt1, 0x1837f0, 0x93fedfe4),
    PLAIN(dxt, DecodeDxt3, 0x1839e0, 0xaec4d179),
    PLAIN(dxt, DecodeDxt5, 0x183dd0, 0x346e4c1b),
    PLAIN(dxt, PaintBlock, 0x186420, 0x8f080988),
    {"nGZGraphic4_GetPixelFormatsManagerCached", 0x18acd0, 32, 0x1ee87990, reinterpret_cast<void *>(&GetPixelFormatsManagerCached)},
    {"nGZGraphic4_GetImageManipCached", 0x18d120, 32, 0x3a9441ce, reinterpret_cast<void *>(&GetImageManipCached)},
    {"nGZGraphic4_GetVertexFormatsManager", 0x18dcf0, 32, 0xfc6f6a7d, reinterpret_cast<void *>(&GetVertexFormatsManagerCached)},
    {"nGZGraphic4_GetVertexDeclarationsManager", 0x18dd70, 32, 0x8badaa06, reinterpret_cast<void *>(&GetVertexDeclarationsManagerCached)},
    {"nGZGraphic4_GetShaderProgramsManager", 0x18ddf0, 32, 0x13eaea8b, reinterpret_cast<void *>(&GetShaderProgramsManagerCached)},
    PLAIN(memchain, CopyLevel, 0x184290, 0xd497ac32),
    PLAIN(memchain, ConvertRgbToArgb, 0x1844b0, 0x9cce9a8e),
    PLAIN(memchain, ChooseConverter, 0x185a30, 0x0edd6c28),
    PLAIN(memchain, DecodeDxt1Level, 0x185b90, 0xee12b3cd),
    PLAIN(memchain, DecodeDxt3Level, 0x185da0, 0xee12b3cd),
    PLAIN(memchain, DecodeDxt5Level, 0x185fb0, 0xee12b3cd),
    PLAIN(memchain, ReduceLevelTwoTap, 0x1854e0, 0x91c09409),
    PLAIN(memchain, ReduceLevelFourTap, 0x185770, 0xe3f97700),
    PLAIN(memchain, ComputeNormalMapLevel, 0x184ec0, 0x38722730),
    PLAIN(memchain, CopyMemoryTextures, 0x184710, 0x81b5c596),
    PLAIN(memchain, ComputeMemoryNormalMap, 0x183500, 0xeaa61e63),
};
REGISTER_HOOKS(kHooks);

#undef KERNEL
#undef PLAIN

} // namespace
