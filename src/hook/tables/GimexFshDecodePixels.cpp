#include "hook/hooks.h"
#include "gz/GimexFshDecode.h"

namespace {

const HookSpec kHooks[] = {
    {"GimexFsh_CopyRows", 0x88584d, 32, 0xa1eb9482, reinterpret_cast<void *>(&gimex::fsh::CopyRows)},
    {"GimexFsh_BlockIndex", 0x88828f, 32, 0x614074c3, reinterpret_cast<void *>(&gimex::fsh::BlockIndex)},
    {"GimexFsh_BlocksToRows", 0x886466, 32, 0xf567167b, reinterpret_cast<void *>(&gimex::fsh::BlocksToRows)},
    {"GimexFsh_UnblockRows", 0x887f0a, 32, 0x21a7b51e, reinterpret_cast<void *>(&gimex::fsh::UnblockRows)},
    {"GimexFsh_DecodeBlockImage", 0x88588e, 32, 0x53e277f4, reinterpret_cast<void *>(&gimex::fsh::DecodeBlockImage)},
    {"GimexFsh_Untile", 0x8882bc, 32, 0x38c152b5, reinterpret_cast<void *>(&gimex::fsh::Untile)},
    {"GimexFsh_AssemblePlanes", 0x885add, 32, 0x9f4a1701, reinterpret_cast<void *>(&gimex::fsh::AssemblePlanes)},
    {"GimexFsh_YuvPixel", 0x88855e, 32, 0xbb316a52, reinterpret_cast<void *>(&gimex::fsh::YuvPixel)},
    {"GimexFsh_YuvToRgba", 0x888623, 32, 0x1ca447d4, reinterpret_cast<void *>(&gimex::fsh::YuvToRgba)},
    {"GimexFsh_YuvPairs", 0x888826, 32, 0xdfa2952c, reinterpret_cast<void *>(&gimex::fsh::YuvPairs)},
    {"GimexFsh_UnpackPixel", 0x887507, 32, 0x040fafa6, reinterpret_cast<void *>(&gimex::fsh::UnpackPixel)},
    {"GimexFsh_DecodeShape", 0x886e83, 32, 0x8cb9f1fb, reinterpret_cast<void *>(&gimex::fsh::DecodeShape)},
};
REGISTER_HOOKS(kHooks);

} // namespace
