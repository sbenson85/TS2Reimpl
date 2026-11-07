// Gimex's shared helpers (lane MP): the codecs' allocator and free, stream helpers and number readers
// (src/gz/Gimex.size.cpp). Every codec of ours calls ours; these hooks send the FSH back end's calls (still the
// original's) to them.
#include "hook/hooks.h"
#include "gz/Gimex.h"

namespace {

const HookSpec kHooks[] = {
    {"GimexAllocate", 0x87da65, 13, 0xed2c6ec0, reinterpret_cast<void *>(&gimex::Allocate)},
    {"GimexFreeMemory", 0x87da72, 15, 0x7cfed9da, reinterpret_cast<void *>(&gimex::FreeMemory)},
    {"GimexStreamLength", 0x87da81, 14, 0x75a6e9f7, reinterpret_cast<void *>(&gimex::StreamLength)},
    {"GimexStreamRead", 0x87da8f, 32, 0xf1b731be, reinterpret_cast<void *>(&gimex::StreamRead)},
    {"GimexStreamSeek", 0x87dab6, 23, 0x421ee5f5, reinterpret_cast<void *>(&gimex::StreamSeek)},
    {"GimexStreamWrite", 0x87dacd, 32, 0xaf69c8b0, reinterpret_cast<void *>(&gimex::StreamWrite)},
    {"GimexReadLittleEndian", 0x87daf4, 32, 0x403998f8, reinterpret_cast<void *>(&gimex::ReadLittleEndian)},
    {"GimexReadBigEndian", 0x87db43, 32, 0x62299582, reinterpret_cast<void *>(&gimex::ReadBigEndian)},
};
REGISTER_HOOKS(kHooks);

} // namespace
