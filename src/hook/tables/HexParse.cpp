#include "hook/hooks.h"
#include "misc/HexParse.h"

namespace {

const HookSpec kHooks[] = {
    {"hexparse::ParseHex32", 0x1b0e2c, 32, 0xab9c86b4, reinterpret_cast<void *>(&hexparse::ParseHex32)},
    {"hexparse::ParseGuidText", 0x4f8eb0, 32, 0xa4a12540, reinterpret_cast<void *>(&hexparse::ParseGuidText)},
};
REGISTER_HOOKS(kHooks);

} // namespace
