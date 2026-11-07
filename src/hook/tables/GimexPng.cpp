// Gimex's PNG codec through the libpng in the image (row 4 of the codec table; lane MO): about, is, open, info, read,
// write and close, the read and write rows, the library's four callbacks and the gray palette test. The table's cells are
// ours already (src/gz/Gimex.size.cpp); these hooks keep the original entry points from running.
#include "hook/hooks.h"
#include "gz/GimexFormats.h"

namespace {

const HookSpec kHooks[] = {
    {"GimexPng_FreeCallback", 0x8800aa, 10, 0xfd5ca5dc, reinterpret_cast<void *>(&gimex::png::FreeCallback)},
    {"GimexPng_AllocateCallback", 0x8800b4, 10, 0x2f9cee4e, reinterpret_cast<void *>(&gimex::png::AllocateCallback)},
    {"GimexPng_About", 0x8800be, 32, 0xfac55b4b, reinterpret_cast<void *>(&gimex::png::NewAbout)},
    {"GimexPng_Close", 0x880168, 32, 0x63c28b78, reinterpret_cast<void *>(&gimex::png::Close)},
    {"GimexPng_Info", 0x88019a, 32, 0x8c39dc2a, reinterpret_cast<void *>(&gimex::png::GetInfo)},
    {"GimexPng_Is", 0x8803e9, 32, 0x989ca38b, reinterpret_cast<void *>(&gimex::png::Probe)},
    {"GimexPng_Open", 0x880435, 32, 0x6f969cad, reinterpret_cast<void *>(&gimex::png::Open)},
    {"GimexPng_Read", 0x880536, 32, 0xd8050780, reinterpret_cast<void *>(&gimex::png::ReadFrame)},
    {"GimexPng_ReadCallback", 0x88062e, 32, 0x783924ac, reinterpret_cast<void *>(&gimex::png::ReadCallback)},
    {"GimexPng_ReadRows", 0x88065c, 32, 0x0dbba179, reinterpret_cast<void *>(&gimex::png::ReadRows)},
    {"GimexPng_Write", 0x8807bb, 32, 0x819bd290, reinterpret_cast<void *>(&gimex::png::WriteFrame)},
    {"GimexPng_WriteCallback", 0x880a3a, 32, 0x30f0573b, reinterpret_cast<void *>(&gimex::png::WriteCallback)},
    {"GimexPng_WriteRows", 0x880a68, 32, 0xcabf1d83, reinterpret_cast<void *>(&gimex::png::WriteRows)},
    {"GimexPng_IsGrayPalette", 0x880c5d, 32, 0xf83143aa, reinterpret_cast<void *>(&gimex::png::IsGrayPalette)},
};
REGISTER_HOOKS(kHooks);

} // namespace
