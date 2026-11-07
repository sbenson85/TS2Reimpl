// Gimex's JPEG codec through Intel's IJL (row 3 of the codec table; lane MN): the load, about, is, open, info and close.
// The table's cells are ours already (src/gz/Gimex.size.cpp); these hooks keep the original entry points from running.
#include "hook/hooks.h"
#include "gz/GimexFormats.h"

namespace {

const HookSpec kHooks[] = {
    {"GimexIjl_Unload", 0x87f790, 24, 0x42efadec, reinterpret_cast<void *>(&gimex::ijl::Unload)},
    {"GimexIjl_Load", 0x87f7a8, 32, 0x00f962e3, reinterpret_cast<void *>(&gimex::ijl::Load)},
    {"GimexIjl_About", 0x87f864, 32, 0xb27ddc6e, reinterpret_cast<void *>(&gimex::ijl::NewAbout)},
    {"GimexIjl_Is", 0x87fb72, 32, 0x1203f596, reinterpret_cast<void *>(&gimex::ijl::Probe)},
    {"GimexIjl_Open", 0x87fbcb, 32, 0xfbe68b9f, reinterpret_cast<void *>(&gimex::ijl::Open)},
    {"GimexIjl_Info", 0x87f9e0, 32, 0x9d8e525c, reinterpret_cast<void *>(&gimex::ijl::GetInfo)},
    {"GimexIjl_Close", 0x87f9b2, 32, 0x9bb845ff, reinterpret_cast<void *>(&gimex::ijl::Close)},
    {"GimexIjl_Read", 0x87fc52, 32, 0x2db29c3d, reinterpret_cast<void *>(&gimex::ijl::ReadFrame)},
    {"GimexIjl_Write", 0x87fe74, 32, 0xaecd489e, reinterpret_cast<void *>(&gimex::ijl::WriteFrame)},
};
REGISTER_HOOKS(kHooks);

} // namespace
