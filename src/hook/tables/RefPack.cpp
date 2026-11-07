#include "hook/hooks.h"
#include "rz/RefPack.h"
#include "rz/cRZFastCompression3.h"

namespace {

const HookSpec kHooks[] = {
    {"RefPack_Decompress", 0x9cc369, 32, 0xe0fce543, reinterpret_cast<void *>(&RefPack::Decompress)},
    // Not a function start in Ghidra's analysis (merged into a neighbour), but called directly.
    {"RefPack_GetDecompressedSize", 0x9cca38, 32, 0xa2690065, reinterpret_cast<void *>(&RefPack::GetDecompressedSize)},
    {"RefPack_RecognizeHeader", 0x9cc5c6, 32, 0x0bdc4bfa, reinterpret_cast<void *>(&RefPack::RecognizeHeader)},
    {"RefPack_WriteBigEndian", 0x9cc597, 32, 0xf601d02a, reinterpret_cast<void *>(&RefPack::WriteBigEndian)},
    {"cRZFastCompression3_QueryInterface", 0x9cc2d1, 32, 0x0cb40f0b, MemberAddress(&cRZFastCompression3::QueryInterface)},
    {"cRZFastCompression3_Release", 0x9cc309, 23, 0x38a4d8e3, MemberAddress(&cRZFastCompression3::Release)},
    {"cRZFastCompression3_DecompressData", 0x9cc174, 10, 0x71025bd8, MemberAddress(&cRZFastCompression3::DecompressData)},
    {"cRZFastCompression3_GetMaxLength", 0x9cc2bd, 20, 0x0e402942, MemberAddress(&cRZFastCompression3::GetMaxLengthRequiredForCompressedData)},
    {"cRZFastCompression3_GetLengthOfCompressedData", 0x9cc229, 10, 0xc06ae85a, MemberAddress(&cRZFastCompression3::GetLengthOfCompressedData)},
    {"cRZFastCompression3_GetLengthOfDecompressedData", 0x9cc26f, 10, 0x7e37e166, MemberAddress(&cRZFastCompression3::GetLengthOfDecompressedData)},
    {"cRZFastCompression3_GetCompressionVersionString", 0x9cc1ed, 32, 0x5ce76696, MemberAddress(&cRZFastCompression3::GetCompressionVersionString)},
    {"cRZFastCompression3_Destruct", 0x9cc0d3, 7, 0xb114ed39, MemberAddress(&cRZFastCompression3::Destruct)},
    {"cRZFastCompression3_DeletingDestructor", 0x9cc0da, 31, 0x78125c77, MemberAddress(&cRZFastCompression3::DeletingDestructor)},
    {"RefPack_Encode", 0x9cc611, 32, 0x5f74f54e, reinterpret_cast<void *>(&RefPack::Encode)},
    {"RefPack_Compress", 0x9cc54d, 32, 0xc66e81e2, reinterpret_cast<void *>(&RefPack::Compress)},
    {"cRZFastCompression3_CompressData", 0x9cc0f9, 10, 0x7215593e, MemberAddress(&cRZFastCompression3::CompressData)},
    {"FUN_a09cc320", 0x9cc320, 32, 0x79c81161, MemberAddress(&cRZFastCompression3::HasValidHeader)},
};
REGISTER_HOOKS(kHooks);

} // namespace
