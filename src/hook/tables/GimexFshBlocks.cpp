#include "hook/hooks.h"
#include "gz/GimexFshBlocks.h"

namespace {

const HookSpec kHooks[] = {
    {"fsh_is_28fb", 0x895381, 32, 0x6653ffc5, reinterpret_cast<void *>(&gimex::fsh::HasHeader28fb)},
    {"fsh_is_1afb", 0x8953ab, 32, 0x31ba3ce6, reinterpret_cast<void *>(&gimex::fsh::HasHeader1afb)},
    {"fsh_is_1cfb", 0x8953d5, 32, 0xdd828a17, reinterpret_cast<void *>(&gimex::fsh::HasHeader1cfb)},
    {"fsh_is_block_compressed", 0x8955c7, 32, 0x2135c6e8, reinterpret_cast<void *>(&gimex::fsh::IsBlockCompressed)},
    {"fsh_is_refpack", 0x895802, 32, 0x581ef1d0, reinterpret_cast<void *>(&gimex::fsh::IsRefPack)},
    {"FUN_a0894e0e", 0x894e0e, 32, 0xe72049f1, reinterpret_cast<void *>(&gimex::fsh::DecodeAlpha4)},
    {"FUN_a0894e67", 0x894e67, 32, 0x803d2f39, reinterpret_cast<void *>(&gimex::fsh::DecodeAlpha8)},
    {"FUN_a0894ff3", 0x894ff3, 32, 0x3a738aa1, reinterpret_cast<void *>(&gimex::fsh::DecodeColourBlock)},
    {"FUN_a08953ff", 0x8953ff, 32, 0x2939345c, reinterpret_cast<void *>(&gimex::fsh::DecodeBlocks)},
    {"FUN_a0895620", 0x895620, 32, 0x04b559f0, reinterpret_cast<void *>(&gimex::fsh::DecodeRefPack)},
    {"refpack_match_length", 0x896960, 32, 0x37b5649a, reinterpret_cast<void *>(&RefPack::MatchLength)},
    {"refpack_compress", 0x89698a, 32, 0xe5e5aecd, reinterpret_cast<void *>(&gimex::fsh::RefPackEncode)},
    {"refpack_compress_with_header", 0x896db9, 32, 0xea4c9af4, reinterpret_cast<void *>(&gimex::fsh::RefPackEncodeWithHeader)},
};
REGISTER_HOOKS(kHooks);

} // namespace
