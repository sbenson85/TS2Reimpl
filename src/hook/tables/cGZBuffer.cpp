// cGZBuffer and cGZTransientBuffer (lane LQ, src/gz/cGZBuffer.h): the two creators the cGZDrawCOMDirector getter 0x8b7f30
// registers, the classes' only creation sites, so every buffer is ours; constructors, destructors and every slot are in
// effect through them.
#include "gz/cGZBuffer.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZBuffer_Create", 0x8c75d0, 32, 0x41edd37f, reinterpret_cast<void *>(&cGZBuffer::Create)},
    {"cGZTransientBuffer_Create", 0x8c7620, 32, 0x59bfdd48, reinterpret_cast<void *>(&cGZTransientBuffer::Create)},
    // The colour mask the pixel converter 0x8c6b10 (game code) also calls.
    {"cGZBuffer_FormatMask", 0x8c7ca0, 32, 0x2c1a6d75, reinterpret_cast<void *>(&cGZBuffer::FormatMask)},
};
REGISTER_HOOKS(kHooks);

} // namespace
