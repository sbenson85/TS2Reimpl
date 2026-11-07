#include "hook/hooks.h"
#include "rz/PathFileOps.h"

namespace {

// Lane QK (N-c2): two of cRZFile's path statics, __cdecl; lane QN (N-d2): the temporary file names.
const HookSpec kHooks[] = {
    {"PathFileTime", 0x1c2068, 32, 0x4a09f73b, reinterpret_cast<void *>(&PathFileTime)},
    {"PathDelete", 0x1c2e25, 32, 0x3ef72e18, reinterpret_cast<void *>(&PathDelete)},
    {"GetTempDirectory", 0x1c1f9c, 32, 0x047f6d99, reinterpret_cast<void *>(&GetTempDirectory)},
    {"TrimToByteLength", 0x1c236b, 32, 0x4a37c08b, reinterpret_cast<void *>(&TrimToByteLength)},
    {"MakeTempFileIn", 0x1c23bf, 32, 0x84d59812, reinterpret_cast<void *>(&MakeTempFileIn)},
    {"MakeTempFile", 0x1c255e, 32, 0x95020a78, reinterpret_cast<void *>(&MakeTempFile)},
};
REGISTER_HOOKS(kHooks);

} // namespace
