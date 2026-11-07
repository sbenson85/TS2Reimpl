#include "hook/hooks.h"
#include "gz/GimexFshGmEncode.h"

namespace {

const HookSpec kHooks[] = {
    {"fsh_gm_bits_written", 0x89d938, 15, 0x98cf9b41, reinterpret_cast<void *>(&gimex::fsh::BitsWritten)},
    {"fsh_gm_write_block", 0x89d947, 32, 0x827cff32, reinterpret_cast<void *>(&gimex::fsh::WriteBlock)},
    {"fsh_gm_dct_block", 0x89d9c9, 32, 0x54addc84, reinterpret_cast<void *>(&gimex::fsh::DctBlock)},
    {"fsh_gm_dct_column", 0x89db37, 32, 0x1b1030a7, reinterpret_cast<void *>(&gimex::fsh::DctColumn)},
    {"fsh_gm_dct_row", 0x89dc7b, 32, 0x179ad69c, reinterpret_cast<void *>(&gimex::fsh::DctRow)},
    {"fsh_gm_write_macroblock", 0x89dd9b, 32, 0xa1bf8025, reinterpret_cast<void *>(&gimex::fsh::WriteMacroblock)},
    {"fsh_gm_init_tables", 0x89de18, 32, 0xf11fb81d, reinterpret_cast<void *>(&gimex::fsh::InitEncodeTables)},
    {"fsh_gm_write_run_level", 0x89dfd7, 32, 0x95a92665, reinterpret_cast<void *>(&gimex::fsh::WriteRunLevel)},
    {"fsh_gm_write_dc", 0x89e098, 32, 0x5ad25a37, reinterpret_cast<void *>(&gimex::fsh::WriteDc)},
    {"fsh_gm_write_address_increment", 0x89e102, 32, 0x4bceacdd, reinterpret_cast<void *>(&gimex::fsh::WriteAddressIncrement)},
    {"fsh_gm_write_header_code", 0x89e14a, 32, 0x59bea2f4, reinterpret_cast<void *>(&gimex::fsh::WriteHeaderCode)},
    {"fsh_gm_quantise_block", 0x89e16f, 32, 0xaef3b730, reinterpret_cast<void *>(&gimex::fsh::QuantiseBlock)},
    {"fsh_gm_convert_to_blocks", 0x89e20d, 32, 0x10103ccb, reinterpret_cast<void *>(&gimex::fsh::ConvertToBlocks)},
    {"fsh_gm_put_bits", 0x89e3d7, 32, 0xb9168a59, reinterpret_cast<void *>(&gimex::fsh::PutBits)},
    {"fsh_gm_flush_bits", 0x89e445, 26, 0x55dad112, reinterpret_cast<void *>(&gimex::fsh::FlushBits)},
    {"fsh_gm_reset_bit_writer", 0x89e45f, 27, 0x5962d07a, reinterpret_cast<void *>(&gimex::fsh::ResetBitWriter)},
    {"fsh_gm_encode", 0x89e47a, 32, 0x35779310, reinterpret_cast<void *>(&gimex::fsh::EncodeGm)},
};
REGISTER_HOOKS(kHooks);

} // namespace
