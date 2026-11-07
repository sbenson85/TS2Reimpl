#include "hook/hooks.h"
#include "gz/cGZDrawContextAsBuffer.h"

namespace {

// The constructor (cGZDrawSystem slot 5's) and the COM director's creator 0x8c22a0 (the constructor inline) are the
// class's switch (docs/standalone-shape.md): every object carries our vtables, so the slots, the deleting destructor
// 0x8c2350 and the colour helper 0x8c2110 are in effect through their callers (status "in_effect": "callers"). The
// pool block cGZDrawSystem slot 5 constructs in still comes from the game's 0x8c1460. DrawEllipse 0x8c1e50 is
// referenced by nothing; hooked, it is ours should anything reach it.
const HookSpec kHooks[] = {
    {"cGZDrawContextAsBuffer_Construct", 0x8c13a0, 32, 0xa1e046a2,
     reinterpret_cast<void *>(&cGZDrawContextAsBufferAdapters::Construct)},
    {"cGZDrawContextAsBuffer_Create", 0x8c22a0, 32, 0x3cff4370, reinterpret_cast<void *>(&cGZDrawContextAsBuffer::Create)},
    {"cGZDrawContextAsBuffer_DrawEllipse", 0x8c1e50, 32, 0x89ff0345,
     reinterpret_cast<void *>(&cGZDrawContextAsBufferAdapters::DrawEllipse)},
};
REGISTER_HOOKS(kHooks);

} // namespace
