#include "hook/hooks.h"
#include "gz/GimexFshGm.h"

namespace {

const HookSpec kHooks[] = {
    {"fsh_gm_peek_bits", 0x895ee0, 32, 0xaa8f3db8, reinterpret_cast<void *>(&gimex::fsh::PeekBits)},
    {"fsh_gm_get_bits", 0x895cdb, 32, 0x171dd906, reinterpret_cast<void *>(&gimex::fsh::GetBits)},
    {"fsh_gm_read_luma_dc", 0x895d8f, 32, 0x32c5be81, reinterpret_cast<void *>(&gimex::fsh::ReadLumaDc)},
    {"fsh_gm_read_chroma_dc", 0x895d1f, 32, 0x41b12be3, reinterpret_cast<void *>(&gimex::fsh::ReadChromaDc)},
    {"fsh_gm_decode_intra_block", 0x89583b, 32, 0x97588cd3, reinterpret_cast<void *>(&gimex::fsh::DecodeIntraBlock)},
    {"fsh_gm_decode_inter_block", 0x895a44, 32, 0x730d84c7, reinterpret_cast<void *>(&gimex::fsh::DecodeInterBlock)},
    {"fsh_gm_read_address_increment", 0x895dff, 32, 0x06d7cf15, reinterpret_cast<void *>(&gimex::fsh::ReadAddressIncrement)},
    {"fsh_gm_init_clip_tables", 0x895e7c, 32, 0x77a1dccb, reinterpret_cast<void *>(&gimex::fsh::InitClipTables)},
    {"fsh_gm_idct_row", 0x896137, 32, 0xa325f830, reinterpret_cast<void *>(&gimex::fsh::IdctRow)},
    {"fsh_gm_idct_column", 0x895f18, 32, 0xd6f10a2e, reinterpret_cast<void *>(&gimex::fsh::IdctColumn)},
    {"fsh_gm_idct_8x8", 0x895c47, 32, 0xd4294dcf, reinterpret_cast<void *>(&gimex::fsh::Idct8x8)},
    {"fsh_gm_decode_macroblock", 0x8962e9, 32, 0x4380f33d, reinterpret_cast<void *>(&gimex::fsh::DecodeMacroblock)},
    {"fsh_gm_convert_block", 0x8963bd, 32, 0xb135acb6, reinterpret_cast<void *>(&gimex::fsh::ConvertBlock)},
    {"fsh_gm_decode", 0x89662e, 32, 0x295c4400, reinterpret_cast<void *>(&gimex::fsh::DecodeGm)},
    {"fsh_gm_header", 0x8968dd, 32, 0x645ce0eb, reinterpret_cast<void *>(&gimex::fsh::ParseGmHeader)},
    {"fsh_is_gm", 0x89694e, 18, 0xb14c3b28, reinterpret_cast<void *>(&gimex::fsh::IsGm)},
};
REGISTER_HOOKS(kHooks);

} // namespace
