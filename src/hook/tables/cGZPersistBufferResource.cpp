#include "hook/hooks.h"
#include "gz/cGZPersistBufferResource.h"

namespace {

using Self = cGZPersistBufferResource;
using Adapters = cGZPersistBufferResourceAdapters;

// cGZPersistBufferResource in the standalone shape (lane NU, src/gz/cGZPersistBufferResource.cpp). The constructor hook gives
// every resource the game builds (the Gimex factory's creation site) our vtables; the destructor goes through its callers (no hook);
// the readers are our members. The class's switch (bisect): _Construct.
const HookSpec kHooks[] = {
    {"cGZPersistBufferResource_Construct", 0x9b64a0, 32, 0xa02a8403, reinterpret_cast<void *>(&Adapters::Construct)},
    {"cGZPersistBufferResource_ReadBmpRecord", 0x9b6930, 32, 0x4446379f, MemberAddress<&Self::ReadBmpRecord>()},
    {"cGZPersistBufferResource_ReadTgaRecord", 0x9b77b0, 32, 0x445452c0, MemberAddress<&Self::ReadTgaRecord>()},
    {"cGZPersistBufferResource_ReadBmpFile", 0x9b6c30, 32, 0x4b047088, MemberAddress<&Self::ReadBmpFile>()},
    {"cGZPersistBufferResource_ConvertBitmap", 0x9b8140, 32, 0x43e93f3d, MemberAddress<&Self::ConvertBitmap>()},
    {"cGZPersistBufferResource_IsSupportedBitmap", 0x9b8db0, 32, 0x3ccb41a6, MemberAddress<&Self::IsSupportedBitmap>()},
    {"cGZPersistBufferResource_DitherRgb24", 0x9b88e0, 32, 0x8debf8ef, MemberAddress<&Self::DitherRgb24>()},
    {"cGZPersistBufferResource_DitherArgb32", 0x9b8b40, 32, 0x2e8a3a0f, MemberAddress<&Self::DitherArgb32>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
