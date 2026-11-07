#include "hook/hooks.h"
#include "gz/PixelFormatsManager.h"
#include "gz/PixelFormatStore.h"

using namespace nGZGraphic4;

namespace {

// In-game the 56 static formats live in the image's own storage (the image's static initialisers call
// our hooked constructors on them), so the manager takes its table, unknown format and CreateIterator
// from the image store (gz/PixelFormatStore.h). The default store (our own objects) is for the
// standalone check and the tests. This one line is the whole choice.
const bool kUseImageStore = (SetPixelFormatStoreProvider(&ImageStore), true);

const HookSpec kHooks[] = {
    // The getter (lane RB): the switch, the manager is ours.
    {"cPixelFormatsManagerDX_Instance", 0x15eb10, 32, 0x875822a3, reinterpret_cast<void *>(&cPixelFormatsManagerDX::Instance)},
    {"cPixelFormatsManagerDX_Construct", 0x15d4b0, 32, 0xc71ed9f2, MemberAddress(&cPixelFormatsManagerDX::Construct)},
    {"cPixelFormatsManagerDX_Destruct", 0x15db70, 32, 0x12c5cc5b, MemberAddress(&cPixelFormatsManagerDX::Destruct)},
    {"cPixelFormatsManagerDX_QueryInterface", 0x15fc70, 32, 0xfd4f9cb6, MemberAddress(&cPixelFormatsManagerDX::QueryInterface)},
    {"cPixelFormatsManagerDX_GetFormat", 0x15e980, 32, 0xbcdd6eca, MemberAddress(&cPixelFormatsManagerDX::GetFormat)},
    {"cPixelFormatsManagerDX_GetUnknownFormat", 0x15f800, 6, 0x06a02e54, MemberAddress(&cPixelFormatsManagerDX::GetUnknownFormat)},
    {"cPixelFormatsManagerDX_GetARGB", 0x15f480, 32, 0xdb9398eb, MemberAddress(&cPixelFormatsManagerDX::GetARGB)},
    {"cPixelFormatsManagerDX_GetAlphaLuminance", 0x15f560, 32, 0x70cc203f, MemberAddress(&cPixelFormatsManagerDX::GetAlphaLuminance)},
    {"cPixelFormatsManagerDX_GetABGRFloat", 0x15f3a0, 32, 0xa7b84263, MemberAddress(&cPixelFormatsManagerDX::GetABGRFloat)},
    {"cPixelFormatsManagerDX_DeletingDestructor", 0x15e3d0, 32, 0xc311baa3, MemberAddress(&cPixelFormatsManagerDX::DeletingDestructor)},
    {"cPixelFormatsManagerDX_GetYUV", 0x15f810, 32, 0x9ff44b96, MemberAddress(&cPixelFormatsManagerDX::GetYUV)},
    {"cPixelFormatsManagerDX_GetDXT", 0x15f640, 32, 0xeb1da7f3, MemberAddress(&cPixelFormatsManagerDX::GetDXT)},
    {"cPixelFormatsManagerDX_GetDepthStencil", 0x15f720, 32, 0xf57d8a64, MemberAddress(&cPixelFormatsManagerDX::GetDepthStencil)},
    {"cPixelFormatsManagerDX_EqualRange", 0x15b4a0, 32, 0x900b70dd, reinterpret_cast<void *>(&cPixelFormatsManagerDX::EqualRange)},
};
REGISTER_HOOKS(kHooks);

} // namespace
