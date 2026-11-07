#include "hook/hooks.h"
#include "gz/PixelFormat.h"

using namespace nGZGraphic4;

namespace {

// Shared slots (shared_vf_*: 0x15e510, 0x147a30, 0x15e820, 0x147140, 0x15eba0, 0x15e550, 0x15e970,
// 0x15f390, 0x147770, 0x1457d0, 0x15e850, 0x15e640, 0x15e570, 0x155a00, 0x1559c0, 0x6c6b0, 0x12e40) are
// folded with unrelated classes and are not hooked; they are verified in tests/pixel_format.cpp.
const HookSpec kHooks[] = {
    // Constructors
    {"cPixelFormat_Construct", 0x15b840, 32, 0x29cf10b7, MemberAddress(&cPixelFormat::Construct)},
    {"cPixelFormatABGRFloat_Construct", 0x15ba30, 32, 0x2a59f2a1, MemberAddress(&cPixelFormatABGRFloat::Construct)},
    {"cPixelFormatARGB_Construct", 0x15bf40, 32, 0x1ce2a5dc, MemberAddress(&cPixelFormatARGB::Construct)},
    {"cPixelFormatAlphaLuminance_Construct", 0x15c5b0, 32, 0xc1f2dd6a, MemberAddress(&cPixelFormatAlphaLuminance::Construct)},
    {"cPixelFormatDXT_Construct", 0x15c9c0, 32, 0x206f52b8, MemberAddress(&cPixelFormatDXT::Construct)},
    {"cPixelFormatDepthStencil_Construct", 0x15cc10, 32, 0xb96935d2, MemberAddress(&cPixelFormatDepthStencil::Construct)},
    {"cPixelFormatYUV_Construct", 0x15d230, 32, 0x0d982ef3, MemberAddress(&cPixelFormatYUV::Construct)},

    // QueryInterface (slot 0): the base, the cPixelFormatBaseT<> versions, and the derived classes
    {"cPixelFormat_QueryInterface", 0x15f980, 32, 0xce11fc98, MemberAddress(&cPixelFormat::QueryInterface)},
    {"cPixelFormatBaseT_ABGRFloat_QueryInterface", 0x15f9b0, 32, 0x98c4e4d4, MemberAddress(&cPixelFormat::QueryInterfaceT<kIidABGRFloat>)},
    {"cPixelFormatBaseT_ARGB_QueryInterface", 0x15f9e0, 32, 0x9d711de1, MemberAddress(&cPixelFormat::QueryInterfaceT<kIidARGB>)},
    {"cPixelFormatBaseT_AlphaLuminance_QueryInterface", 0x15fa10, 32, 0x5929d502, MemberAddress(&cPixelFormat::QueryInterfaceT<kIidAlphaLuminance>)},
    {"cPixelFormatBaseT_DXT_QueryInterface", 0x15fa40, 32, 0x293320ee, MemberAddress(&cPixelFormat::QueryInterfaceT<kIidDXT>)},
    {"cPixelFormatDepthStencil_QueryInterface", 0x15fa70, 32, 0x9471e389, MemberAddress(&cPixelFormatDepthStencil::QueryInterface)},
    {"cPixelFormatUnknown_QueryInterface", 0x15faa0, 32, 0x448e3e9e, MemberAddress(&cPixelFormatUnknown::QueryInterface)},
    {"cPixelFormatYUV_QueryInterface", 0x15fad0, 32, 0xcbe5a917, MemberAddress(&cPixelFormatYUV::QueryInterface)},
    {"cPixelFormatABGRFloat_QueryInterface", 0x15fb00, 32, 0xfd59796c, MemberAddress(&cPixelFormatABGRFloat::QueryInterface)},
    {"cPixelFormatARGB_QueryInterface", 0x15fb50, 32, 0xfd59796c, MemberAddress(&cPixelFormatARGB::QueryInterface)},
    {"cPixelFormatAlphaLuminance_QueryInterface", 0x15fba0, 32, 0xfd59796c, MemberAddress(&cPixelFormatAlphaLuminance::QueryInterface)},
    {"cPixelFormatDXT_QueryInterface", 0x15fbf0, 32, 0xfd59796c, MemberAddress(&cPixelFormatDXT::QueryInterface)},

    // GetClassId (slot 5)
    {"cPixelFormat_GetClassId", 0x15fd30, 6, 0x43c6d4b7, MemberAddress(&cPixelFormat::GetClassId)},
    {"cPixelFormatABGRFloat_GetClassId", 0x15fd40, 6, 0xe90b8058, MemberAddress(&cPixelFormatABGRFloat::GetClassId)},
    {"cPixelFormatARGB_GetClassId", 0x15fd50, 6, 0x21461e29, MemberAddress(&cPixelFormatARGB::GetClassId)},
    {"cPixelFormatAlphaLuminance_GetClassId", 0x15fd60, 6, 0xd1fbd8b5, MemberAddress(&cPixelFormatAlphaLuminance::GetClassId)},
    {"cPixelFormatDXT_GetClassId", 0x15fd70, 6, 0x3e39dc2e, MemberAddress(&cPixelFormatDXT::GetClassId)},
    {"cPixelFormatDepthStencil_GetClassId", 0x15fd80, 6, 0x995d3a73, MemberAddress(&cPixelFormatDepthStencil::GetClassId)},
    {"cPixelFormatUnknown_GetClassId", 0x15fd90, 6, 0x4265202a, MemberAddress(&cPixelFormatUnknown::GetClassId)},
    {"cPixelFormatYUV_GetClassId", 0x15fda0, 6, 0xa31689f8, MemberAddress(&cPixelFormatYUV::GetClassId)},

    // Deleting destructors: the cPixelFormatBaseT<> versions (0x20 bytes), then the derived classes. Lane GT: the ABGRFloat,
    // ARGB, AlphaLuminance, DepthStencil and YUV deleting destructors and Less, and ARGB's PackColor / UnpackColor, are in effect
    // through their callers (only the game's vtable reaches them, and no format carries it while that format's constructor
    // hook is in; the host traps them under it). Our members stay: our vtables hold them.
    {"cPixelFormat_DeletingDestructor", 0x15dcb0, 32, 0x5266f76f, MemberAddress(&cPixelFormat::DeletingDestructor)},
    {"cPixelFormatBaseT_ABGRFloat_DeletingDestructor", 0x15dd20, 32, 0x472cbc00, MemberAddress(&cPixelFormat::DeletingDestructorT<FormatClass::ABGRFloat, cPixelFormat::kSize>)},
    {"cPixelFormatBaseT_ARGB_DeletingDestructor", 0x15dd90, 32, 0x57e6ac07, MemberAddress(&cPixelFormat::DeletingDestructorT<FormatClass::ARGB, cPixelFormat::kSize>)},
    {"cPixelFormatBaseT_AlphaLuminance_DeletingDestructor", 0x15de00, 32, 0x876e473e, MemberAddress(&cPixelFormat::DeletingDestructorT<FormatClass::AlphaLuminance, cPixelFormat::kSize>)},
    {"cPixelFormatBaseT_DXT_DeletingDestructor", 0x15de70, 32, 0xe00f4c7d, MemberAddress(&cPixelFormat::DeletingDestructorT<FormatClass::DXT, cPixelFormat::kSize>)},
    {"cPixelFormatBaseT_DepthStencil_DeletingDestructor", 0x15dee0, 32, 0xa43a4a84, MemberAddress(&cPixelFormat::DeletingDestructorT<FormatClass::DepthStencil, cPixelFormat::kSize>)},
    {"cPixelFormatUnknown_DeletingDestructor", 0x15df50, 32, 0xb7bb7c4c, MemberAddress(&cPixelFormatUnknown::DeletingDestructor)},
    {"cPixelFormatBaseT_YUV_DeletingDestructor", 0x15dfc0, 32, 0xd891e710, MemberAddress(&cPixelFormat::DeletingDestructorT<FormatClass::YUV, cPixelFormat::kSize>)},
    {"cPixelFormatDXT_DeletingDestructor", 0x15e1b0, 32, 0xe00f4c7d, MemberAddress(&cPixelFormatDXT::DeletingDestructor)},

    // Less (slot 10)
    {"cPixelFormatDXT_Less", 0x15f000, 32, 0x75324704, MemberAddress(&cPixelFormatDXT::Less)},
    {"cPixelFormatUnknown_Less", 0x15f210, 32, 0x2b3346df, MemberAddress(&cPixelFormatUnknown::Less)},

    // ARGB pack / unpack, and the per-class sizes
    {"cPixelFormatABGRFloat_GetBitsPerPixel", 0x15e540, 13, 0x52735a3c, MemberAddress(&cPixelFormatABGRFloat::GetBitsPerPixel)},
    {"cPixelFormatABGRFloat_GetRowBytes", 0x15e620, 19, 0xf6b46269, MemberAddress(&cPixelFormatABGRFloat::GetRowBytes)},
    {"cPixelFormatDXT_GetRowBytes", 0x15e660, 19, 0x2e07ce8e, MemberAddress(&cPixelFormatDXT::GetRowBytes)},
    {"cPixelFormatYUV_GetRowBytes", 0x15e680, 18, 0x5adc3f3e, MemberAddress(&cPixelFormatYUV::GetRowBytes)},
    {"cPixelFormatDXT_GetBlockBytes", 0x15e560, 6, 0xcbc56162, MemberAddress(&cPixelFormatDXT::GetBlockBytes)},
    {"cPixelFormatABGRFloat_GetChannelBitsOr", 0x15f900, 13, 0xb7ff7c40, MemberAddress(&cPixelFormatABGRFloat::GetChannelBitsOr)},
    {"cPixelFormatARGB_GetUnusedBits", 0x15fdb0, 16, 0x5e8ee4f0, MemberAddress(&cPixelFormatARGB::GetUnusedBits)},
};
REGISTER_HOOKS(kHooks);

} // namespace
