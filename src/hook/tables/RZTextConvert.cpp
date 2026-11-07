#include "hook/hooks.h"
#include "rz/RZTextConvert.h"

namespace {

// DefaultEncoding (0x1a9200) is a 4-byte function shared with unrelated classes: not hooked.
const HookSpec kHooks[] = {
    {"RZText_SystemCodePage", 0x1b0690, 22, 0x17ddc8d4, reinterpret_cast<void *>(&RZText::SystemCodePage)},
    {"RZText_Convert", 0x1afd58, 32, 0xa9965766, reinterpret_cast<void *>(&RZText::Convert)},
    {"RZText_ConvertViaUtf16", 0x1b0455, 32, 0x5be4e696, reinterpret_cast<void *>(&RZText::ConvertViaUtf16)},
    {"RZText_ConvertString", 0x1afc1f, 32, 0x90501662, reinterpret_cast<void *>(&RZText::ConvertString)},
    {"RZText_ConvertStringToString", 0x1b025d, 32, 0xd0525b34, reinterpret_cast<void *>(&RZText::ConvertStringToString)},
    {"RZText_ToUtf16", 0x1c2ad8, 32, 0xe9971c6a, reinterpret_cast<void *>(&RZText::ToUtf16)},
};
REGISTER_HOOKS(kHooks);

} // namespace
