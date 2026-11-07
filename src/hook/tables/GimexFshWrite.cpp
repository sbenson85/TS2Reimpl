#include "hook/hooks.h"
#include "gz/GimexFshWrite.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0889917", 0x889917, 32, 0x929e1322, gimex::fsh::PadShapes},
    {"FUN_a0889a2d", 0x889a2d, 32, 0xa54fb118, gimex::fsh::MergeMipmaps},
    {"FUN_a08899a3", 0x8899a3, 32, 0x96183a89, gimex::fsh::EncodeRowBytes},
    {"FUN_a08899fc", 0x8899fc, 32, 0x6e2e586a, gimex::fsh::PlaneBytes},
    {"FUN_a088a105", 0x88a105, 32, 0xc4690b1b, gimex::fsh::FreeWrittenDirectory},
    {"FUN_a088a187", 0x88a187, 32, 0x9890f7e2, gimex::fsh::EncodeMipLevels},
    {"FUN_a088a1ed", 0x88a1ed, 32, 0x99f69719, gimex::fsh::EncodeShapeBits},
    {"FUN_a088a3c9", 0x88a3c9, 32, 0x93f3fdcb, gimex::fsh::ExpandCubeMaps},
    {"FUN_a088a5b2", 0x88a5b2, 32, 0x4d4590d5, gimex::fsh::TilePixels},
    {"FUN_a088a731", 0x88a731, 32, 0x290607f0, gimex::fsh::TileBlocks},
    {"FUN_a088a8f6", 0x88a8f6, 32, 0x858abbf6, gimex::fsh::SetMipLevels},
    {"FUN_a088a962", 0x88a962, 32, 0xc6a252e8, gimex::fsh::ChainBlock},
    {"FUN_a088a9ad", 0x88a9ad, 32, 0xf8659a62, gimex::fsh::ChainEntryBlocks},
    {"FUN_a088b405", 0x88b405, 32, 0x3bf1d784, gimex::fsh::BuildPalette},
    {"FUN_a088b9e1", 0x88b9e1, 32, 0x73c73351, gimex::fsh::BuildTextBlock},
    {"FUN_a088ba81", 0x88ba81, 32, 0xadeafcfe, gimex::fsh::BuildUserDataBlock},
    {"FUN_a088bb19", 0x88bb19, 32, 0x27f8f977, gimex::fsh::BuildPairsBlock},
    {"FUN_a088bc6b", 0x88bc6b, 32, 0xc89a45d9, gimex::fsh::BuildMetalBinBlock},
    {"FUN_a088bd45", 0x88bd45, 32, 0x5f4b2190, gimex::fsh::CopyShapeName},
    {"FUN_a088bd83", 0x88bd83, 32, 0xec007111, gimex::fsh::PackShapePixels},
    {"FUN_a088bf12", 0x88bf12, 32, 0x15fba94f, gimex::fsh::BlockCompress},
    {"FUN_a088c070", 0x88c070, 32, 0xed991f16, gimex::fsh::GmCompress},
    {"FUN_a088c0e9", 0x88c0e9, 32, 0x622b8af0, gimex::fsh::PackPixel},
    {"fsh_encode_shape", 0x88905f, 32, 0x340ff0bc, gimex::fsh::EncodeShape},
    {"fsh_compress_shape", 0x88aa6e, 32, 0xe93dc5fc, gimex::fsh::CompressShape},
    {"fsh_write_directory", 0x88c719, 32, 0x78b4ce94, gimex::fsh::WriteDirectory},
};
REGISTER_HOOKS(kHooks);

} // namespace
