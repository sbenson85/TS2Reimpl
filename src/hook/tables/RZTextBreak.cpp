// The Thai mark test and the character length of the text encodings (lane OF, src/rz/RZTextBreak.h): both are called from
// outside cGZFont too (the text edit control). The rest of that file (the classifiers, the cluster and Thai syllable
// lengths) is reached only from cGZFont's line breaker, in effect through it.
#include "hook/hooks.h"
#include "rz/RZTextBreak.h"

namespace {

// The predicate answers in a whole register (0 or 1): a bool's upper bytes are not defined.
uint32_t __cdecl IsThaiMark(uint32_t ch) { return RZText::IsThaiMark(ch); }

const HookSpec kHooks[] = {
    {"RZText_IsThaiMark", 0x1b258f, 32, 0x32bacb66, reinterpret_cast<void *>(&IsThaiMark)},
    {"RZText_CharUnits", 0x1b05ed, 32, 0x980efc0a, reinterpret_cast<void *>(&RZText::CharUnits)},
};
REGISTER_HOOKS(kHooks);

} // namespace
