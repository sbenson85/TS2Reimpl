// cGZFont (lane OC, src/gz/cGZFont.h): the COM director's creator for class 0x1aa404ac. Every font is ours (the font
// system's hand-made fallback is ours too); the constructor 0x90a7db, the destructor, every slot (lane OH made the last of
// them), the helpers, the glyph caches and the font file reader are in effect through it. The Thai class lookup 0x1b1d25 is
// hooked as well: a window class outside cGZFont calls it too.
#include "gz/cGZFont.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZFont_Create", 0x909fb9, 25, 0x9662a4ca, reinterpret_cast<void *>(&cGZFont::Create)},
    {"cGZFont_ThaiClass", 0x1b1d25, 32, 0xe6fa4ccf, reinterpret_cast<void *>(&cGZFont::ThaiClass)},
};
REGISTER_HOOKS(kHooks);

} // namespace
