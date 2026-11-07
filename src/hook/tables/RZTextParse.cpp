#include "hook/hooks.h"
#include "rz/RZTextParse.h"

namespace {

const HookSpec kHooks[] = {
    {"rztext::ParseHex64", 0x1b0ec2, 32, 0xc22c942d, reinterpret_cast<void *>(&rztext::ParseHex64)},
    {"rztext::ParseInt64", 0x1b101c, 32, 0x532d7a08, reinterpret_cast<void *>(&rztext::ParseInt64)},
    {"rztext::PowerOf", 0x1afaa3, 32, 0x7eb0ab77, reinterpret_cast<void *>(&rztext::PowerOf)},
    {"rztext::ReadDouble", 0x1b1ab5, 32, 0xb1769eca, reinterpret_cast<void *>(&rztext::ReadDouble)},
    {"rztext::ParseDouble", 0x1b1008, 20, 0x495a4bcb, reinterpret_cast<void *>(&rztext::ParseDouble)},
    {"rztext::SplitText", 0x1b1557, 32, 0x407cce07, reinterpret_cast<void *>(&rztext::SplitText)},
};
REGISTER_HOOKS(kHooks);

} // namespace
