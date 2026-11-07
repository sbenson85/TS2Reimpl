#include "hook/hooks.h"
#include "world/cStringMap.h"

namespace {

using nTSWorld::cStringMapAdapters;

const HookSpec kHooks[] = {
    {"nTSWorld::cStringMap<G>::cStringMap", 0x813b30, 32, 0x35c76318, reinterpret_cast<void *>(&cStringMapAdapters::Construct)},
    {"nTSWorld::cStringMap<G>::vf6", 0x8141f0, 32, 0xb072fc55, reinterpret_cast<void *>(&cStringMapAdapters::DeletingDestructor)},
    {"nTSWorld::cStringMap<G>::GetGZCLSID", 0x815980, 6, 0x76c653bf, reinterpret_cast<void *>(&cStringMapAdapters::GetGZCLSID)},
    {"nTSWorld::cStringMap<G>::vf8", 0x816050, 32, 0x24b27425, reinterpret_cast<void *>(&cStringMapAdapters::NameOf)},
    {"nTSWorld::cStringMap<G>::vf9", 0x816680, 32, 0xdabc8e02, reinterpret_cast<void *>(&cStringMapAdapters::NextId)},
    {"nTSWorld::cStringMap<G>::Read", 0x816d90, 32, 0x56f09fe2, reinterpret_cast<void *>(&cStringMapAdapters::Read)},
    {"nTSWorld::cStringMap<G>::vf7", 0x819d20, 32, 0x5b7a8aab, reinterpret_cast<void *>(&cStringMapAdapters::IdOf)},
    {"nTSWorld::cStringMap<G>::Write", 0x81a740, 32, 0x29a7cf78, reinterpret_cast<void *>(&cStringMapAdapters::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
