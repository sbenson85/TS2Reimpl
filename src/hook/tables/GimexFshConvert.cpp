#include "hook/hooks.h"
#include "gz/GimexFshConvert.h"

namespace {

const HookSpec kHooks[] = {
    {"fsh_clamp_round", 0x885d30, 32, 0x749c9648, reinterpret_cast<void *>(&gimex::fsh::ClampRound)},
    {"fsh_yuv_to_rgb_float", 0x8885a1, 32, 0x73af419c, reinterpret_cast<void *>(&gimex::fsh::YuvToRgbFloat)},
    {"fsh_ycbcr_to_rgb", 0x888939, 32, 0xe774a250, reinterpret_cast<void *>(&gimex::fsh::YCbCrToRgb)},
    {"fsh_yuyv_pair_to_bgra", 0x888a19, 32, 0xa09f7241, reinterpret_cast<void *>(&gimex::fsh::YuyvPairToBgra)},
    {"fsh_average_cb", 0x888b13, 32, 0xb99da426, reinterpret_cast<void *>(&gimex::fsh::AverageCb)},
    {"fsh_average_cr", 0x888c40, 32, 0xb99da426, reinterpret_cast<void *>(&gimex::fsh::AverageCr)},
    {"fsh_allocate_mip_chain", 0x888d6d, 32, 0x83bbaa2a, reinterpret_cast<void *>(&gimex::fsh::AllocateMipChain)},
    {"fsh_reorder_planes", 0x888e00, 32, 0x6a8ba054, reinterpret_cast<void *>(&gimex::fsh::ReorderPlanes)},
    {"fsh_detect_pixel_format", 0x888efc, 32, 0x64d7ce35, reinterpret_cast<void *>(&gimex::fsh::DetectPixelFormat)},
    {"fsh_rgb_to_ycbcr_float", 0x888f97, 32, 0xe2d1d8da, reinterpret_cast<void *>(&gimex::fsh::RgbToYCbCrFloat)},
    {"fsh_rgb_to_yuv420", 0x88cc20, 32, 0xde12390c, reinterpret_cast<void *>(&gimex::fsh::RgbToYuv420)},
    {"fsh_rgb_to_yuyv", 0x88ce4e, 32, 0x3d141875, reinterpret_cast<void *>(&gimex::fsh::RgbToYuyv)},
    {"fsh_highest_bit", 0x88cf36, 32, 0x5be2af60, reinterpret_cast<void *>(&gimex::fsh::HighestBit)},
    {"fsh_reorder_image", 0x88cf96, 32, 0x4e2f610e, reinterpret_cast<void *>(&gimex::fsh::ReorderImage)},
    {"fsh_store_luma", 0x88d212, 32, 0x338af8c6, reinterpret_cast<void *>(&gimex::fsh::StoreLuma)},
    {"fsh_round_to_multiple", 0x88d362, 32, 0x87ec6ae5, reinterpret_cast<void *>(&gimex::fsh::RoundToMultiple)},
    {"fsh_pack_yuyv", 0x88d39e, 32, 0xea55db57, reinterpret_cast<void *>(&gimex::fsh::PackYuyv)},
    {"fsh_mip_chain_size", 0x88d52e, 32, 0x3f842752, reinterpret_cast<void *>(&gimex::fsh::MipChainSize)},
    {"fsh_aligned_offset", 0x88d5c9, 31, 0x650ac833, reinterpret_cast<void *>(&gimex::fsh::AlignedOffset)},
    {"fsh_rgb_to_ycbcr_int", 0x88d5e8, 32, 0xa72ea250, reinterpret_cast<void *>(&gimex::fsh::RgbToYCbCrInt)},
};
REGISTER_HOOKS(kHooks);

} // namespace
