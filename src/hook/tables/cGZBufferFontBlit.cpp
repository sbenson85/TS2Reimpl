// cGZBufferFontRenderer's code outside its slots (lane LS, src/gz/cGZBufferFontBlit.h): the constructor, the blend
// table, the glyph blitter and its two 16-bit blends.
#include "gz/cGZBufferFontBlit.h"
#include "hook/hooks.h"

namespace {

using Code = cGZBufferFontRendererCode;

const HookSpec kHooks[] = {
    {"cGZBufferFontRenderer_Construct", 0x90a7b5, 32, 0xde051623, reinterpret_cast<void *>(&Code::Construct)},
    {"cGZBufferFontRenderer_BuildBlendTable", 0x90e1aa, 32, 0xa8ae7256,
     reinterpret_cast<void *>(&Code::BuildBlendTable)},
    {"cGZBufferFontRenderer_Blit", 0x90bbba, 32, 0x88efa97a, MemberAddress(&Code::Blit)},
    {"cGZBufferFontRenderer_Blend565", 0x90c7ba, 32, 0x1ddc4f94, reinterpret_cast<void *>(&Code::Blend565)},
    {"cGZBufferFontRenderer_Blend4444", 0x90c82b, 32, 0xb9a54e3c, reinterpret_cast<void *>(&Code::Blend4444)},
};
REGISTER_HOOKS(kHooks);

} // namespace
