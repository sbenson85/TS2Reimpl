// Gimex's FSH codec (row 1 of the codec table, lane MP): the table's cells are ours already (src/gz/Gimex.size.cpp);
// these hooks keep the original table's entry points from running.
#include "hook/hooks.h"
#include "gz/GimexFormats.h"

namespace {

const HookSpec kHooks[] = {
    {"GimexFsh_About", 0x87dbad, 32, 0xb65dd91b, reinterpret_cast<void *>(&gimex::fsh::NewAbout)},
    {"GimexFsh_Is", 0x87ea4e, 32, 0x4dc647e7, reinterpret_cast<void *>(&gimex::fsh::Probe)},
    {"GimexFsh_Open", 0x87ea9f, 32, 0x869a298b, reinterpret_cast<void *>(&gimex::fsh::Open)},
    {"GimexFsh_Info", 0x87dc98, 32, 0xd0df60f1, reinterpret_cast<void *>(&gimex::fsh::GetInfo)},
    {"GimexFsh_Read", 0x87eb11, 32, 0x1193fce2, reinterpret_cast<void *>(&gimex::fsh::ReadFrame)},
    {"GimexFsh_Close", 0x87dc79, 31, 0x93585893, reinterpret_cast<void *>(&gimex::fsh::Close)},
    {"GimexFsh_WOpen", 0x87eb94, 32, 0x64d6c97d, reinterpret_cast<void *>(&gimex::fsh::OpenWriter)},
    {"GimexFsh_Write", 0x87ebf5, 32, 0xe03a5089, reinterpret_cast<void *>(&gimex::fsh::WriteFrame)},
    {"GimexFsh_WClose", 0x87eb4b, 32, 0x7eac4cc3, reinterpret_cast<void *>(&gimex::fsh::CloseWriter)},
};
REGISTER_HOOKS(kHooks);

} // namespace
