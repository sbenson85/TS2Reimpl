// Gimex's BMP codec (row 5 of the codec table) and the instance helpers its rows share (lane MN): the table's cells are
// ours already (src/gz/Gimex.size.cpp); these hooks keep the original table's entry points from running.
#include "hook/hooks.h"
#include "gz/GimexFormats.h"

namespace {

const HookSpec kHooks[] = {
    {"GimexBmp_About", 0x881267, 32, 0xb9480fc0, reinterpret_cast<void *>(&gimex::bmp::NewAbout)},
    {"GimexBmp_Is", 0x881730, 32, 0x05cb453e, reinterpret_cast<void *>(&gimex::bmp::Probe)},
    {"GimexBmp_Open", 0x88176a, 32, 0x5fe6e3e3, reinterpret_cast<void *>(&gimex::bmp::Open)},
    {"GimexBmp_Info", 0x881316, 32, 0x218c3089, reinterpret_cast<void *>(&gimex::bmp::GetInfo)},
    {"GimexBmp_Read", 0x8817a2, 32, 0xe1562db7, reinterpret_cast<void *>(&gimex::bmp::ReadFrame)},
    {"GimexBmp_Write", 0x881d40, 32, 0xf589e68c, reinterpret_cast<void *>(&gimex::bmp::WriteFrame)},
    {"GimexOpenWriter", 0x87f3e4, 32, 0xd959f8ad, reinterpret_cast<void *>(&gimex::OpenWriter)},
    {"GimexFreeInstance", 0x87fe5d, 23, 0x48636054, reinterpret_cast<void *>(&gimex::FreeInstance)},
};
REGISTER_HOOKS(kHooks);

} // namespace
