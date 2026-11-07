#include "hook/hooks.h"
#include "gz/cGZDrawFontRenderer.h"

namespace {

// The COM director's creator 0x8c32a0 (the constructor inline) and the constructor 0x8c2440 (referenced by nothing) are
// the class's switch (docs/standalone-shape.md): every renderer carries our vtable, so its slots, the atlas helpers
// (BuildAtlas 0x8c2d10, Reset 0x8c2f10, AddGlyph 0x8c2f80), the deleting destructor and the std helpers only they call
// are in effect through their callers (status "in_effect": "callers"). The destructor 0x8c2530 is referenced by nothing
// either, so no hook cuts a path to it for a trap to rest on: it is hooked like the constructor.
const HookSpec kHooks[] = {
    {"cGZDrawFontRenderer_Create", 0x8c32a0, 32, 0x3f162324, reinterpret_cast<void *>(&cGZDrawFontRenderer::Create)},
    {"cGZDrawFontRenderer_Construct", 0x8c2440, 32, 0xe9dfe57b,
     reinterpret_cast<void *>(&cGZDrawFontRendererAdapters::Construct)},
    {"cGZDrawFontRenderer_Destruct", 0x8c2530, 32, 0xcc1f6fc0,
     reinterpret_cast<void *>(&cGZDrawFontRendererAdapters::Destruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
