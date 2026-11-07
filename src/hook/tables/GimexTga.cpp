// Gimex's TGA codec (row 6 of the codec table, lane MN): the table's cells are ours already (src/gz/Gimex.size.cpp); these
// hooks keep the original table's entry points from running.
#include "hook/hooks.h"
#include "gz/GimexFormats.h"

namespace {

const HookSpec kHooks[] = {
    {"GimexTga_About", 0x882862, 32, 0xe3e9fe76, reinterpret_cast<void *>(&gimex::tga::NewAbout)},
    {"GimexTga_Is", 0x882db5, 32, 0xf3f36e06, reinterpret_cast<void *>(&gimex::tga::Probe)},
    {"GimexTga_Open", 0x882e91, 32, 0x5f9c1ef2, reinterpret_cast<void *>(&gimex::tga::Open)},
    {"GimexTga_Info", 0x882958, 32, 0xc83c870b, reinterpret_cast<void *>(&gimex::tga::GetInfo)},
    {"GimexTga_Read", 0x882ec9, 32, 0xcf8f1f8d, reinterpret_cast<void *>(&gimex::tga::ReadFrame)},
    {"GimexTga_Write", 0x8833ac, 32, 0xe45101a9, reinterpret_cast<void *>(&gimex::tga::WriteFrame)},
};
REGISTER_HOOKS(kHooks);

} // namespace
