#include "hook/hooks.h"
#include "rz/RZTextChars.h"

namespace {

// The predicates answer in a whole register (0 or 1): a bool's upper bytes are not defined.
uint32_t __cdecl IsLeadByte(uint32_t byte, int32_t codePage) { return RZText::IsLeadByte(byte, codePage); }
uint32_t __cdecl IsIdeograph(uint32_t ch) { return RZText::IsIdeograph(ch); }
uint32_t __cdecl IsHangul(uint32_t ch) { return RZText::IsHangul(ch); }
uint32_t __cdecl IsThaiBlock(uint32_t ch) { return RZText::IsThaiBlock(ch); }

const HookSpec kHooks[] = {
    {"RZText_IsLeadByte", 0x1b0de2, 32, 0xe16ccf47, reinterpret_cast<void *>(&IsLeadByte)},
    {"RZText_CharLength", 0x1b0545, 32, 0xcdd87cbf, reinterpret_cast<void *>(&RZText::CharLength)},
    {"RZText_CharsLength", 0x1b0d66, 32, 0x7e34f598, reinterpret_cast<void *>(&RZText::CharsLength)},
    {"RZText_IsIdeograph", 0x1b2518, 32, 0x8ce33f71, reinterpret_cast<void *>(&IsIdeograph)},
    {"RZText_IsHangul", 0x1b2558, 32, 0x7a3d202b, reinterpret_cast<void *>(&IsHangul)},
    {"RZText_IsThaiBlock", 0x1b257c, 19, 0x698fa2c1, reinterpret_cast<void *>(&IsThaiBlock)},
};
REGISTER_HOOKS(kHooks);

} // namespace
