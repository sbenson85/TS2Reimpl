#include "hook/hooks.h"
#include "gz/GimexFshDecode.h"

namespace {

const HookSpec kHooks[] = {
    {"GimexFsh_ClassifyRecord", 0x88677e, 32, 0x9f6e49ee, reinterpret_cast<void *>(&gimex::fsh::ClassifyRecord)},
    {"GimexFsh_ShapeBits", 0x886956, 32, 0x0a605d63, reinterpret_cast<void *>(&gimex::fsh::ShapeBits)},
    {"GimexFsh_HasCodec", 0x88888f, 32, 0xf96bd9b9, reinterpret_cast<void *>(&gimex::fsh::HasCodec)},
    {"GimexFsh_RecordCodec", 0x8888e7, 32, 0x740f79c7, reinterpret_cast<void *>(&gimex::fsh::RecordCodec)},
    {"GimexFsh_MipLevels", 0x88659e, 32, 0x9f675687, reinterpret_cast<void *>(&gimex::fsh::MipLevels)},
    {"GimexFsh_MipLevelsAt14", 0x887ea7, 32, 0x2a27a0a2, reinterpret_cast<void *>(&gimex::fsh::MipLevelsAt14)},
    {"GimexFsh_RecordLength", 0x88663b, 23, 0x9a42a928, reinterpret_cast<void *>(&gimex::fsh::RecordLength)},
    {"GimexFsh_IsSwizzled", 0x88824b, 26, 0x0af5f065, reinterpret_cast<void *>(&gimex::fsh::IsSwizzled)},
    {"GimexFsh_IsKnownRecord", 0x888265, 21, 0xc0623fe5, reinterpret_cast<void *>(&gimex::fsh::IsKnownRecord)},
    {"GimexFsh_IsCode", 0x88827a, 21, 0xc38882ab, reinterpret_cast<void *>(&gimex::fsh::IsCode)},
    {"GimexFsh_PaletteEntries", 0x8881f8, 32, 0xf4b78e3a, reinterpret_cast<void *>(&gimex::fsh::PaletteEntries)},
    {"GimexFsh_RecordSize", 0x886b4e, 32, 0x8dd18dd5, reinterpret_cast<void *>(&gimex::fsh::RecordSize)},
    {"GimexFsh_ReadShapeText", 0x886652, 32, 0x8137fd55, reinterpret_cast<void *>(&gimex::fsh::ReadShapeText)},
    {"GimexFsh_ReadShapeExtra", 0x886680, 32, 0x20b4b61f, reinterpret_cast<void *>(&gimex::fsh::ReadShapeExtra)},
    {"GimexFsh_ReadShapePairs", 0x8866ae, 32, 0xf6e5c6be, reinterpret_cast<void *>(&gimex::fsh::ReadShapePairs)},
    {"GimexFsh_ReadShapeMarker", 0x88675f, 31, 0x1522c5d4, reinterpret_cast<void *>(&gimex::fsh::ReadShapeMarker)},
    {"GimexFsh_FaceOffset", 0x885d54, 32, 0x843b95e1, reinterpret_cast<void *>(&gimex::fsh::FaceOffset)},
    {"GimexFsh_NextLevelOffset", 0x885e69, 31, 0x06b927c9, reinterpret_cast<void *>(&gimex::fsh::NextLevelOffset)},
    {"GimexFsh_AllocateFaces", 0x885c25, 32, 0x19af65ee, reinterpret_cast<void *>(&gimex::fsh::AllocateFaces)},
    {"GimexFsh_BlockBits", 0x885cba, 32, 0x146c802a, reinterpret_cast<void *>(&gimex::fsh::BlockBits)},
    {"GimexFsh_IsTiled", 0x885def, 32, 0x3f14afe0, reinterpret_cast<void *>(&gimex::fsh::IsTiled)},
    {"GimexFsh_SplitFaces", 0x88800e, 32, 0x927480ff, reinterpret_cast<void *>(&gimex::fsh::SplitFaces)},
    {"GimexFsh_FreeDirectory", 0x885f08, 32, 0xdf5e7513, reinterpret_cast<void *>(&gimex::fsh::FreeDirectory)},
    {"GimexFsh_RowBytes", 0x885e88, 32, 0xaa666ecc, reinterpret_cast<void *>(&gimex::fsh::RowBytes)},
    {"GimexFsh_ImageBytes", 0x885eee, 26, 0xaeaa8d1c, reinterpret_cast<void *>(&gimex::fsh::ImageBytes)},
};
REGISTER_HOOKS(kHooks);

} // namespace
