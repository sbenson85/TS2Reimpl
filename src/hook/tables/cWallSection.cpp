#include "hook/hooks.h"
#include "world/cWallSection.h"

namespace {

using nTSWorld::cWallSectionAdapters;

// Slots 0 (0x1489e0) and 3 (0x147a30) are shared one-word bodies under 12 bytes: ours only in our vtable.
const HookSpec kHooks[] = {
    {"nTSWorld::cWallSection::cWallSection", 0x81fc70, 32, 0x06f5188b, reinterpret_cast<void *>(&cWallSectionAdapters::Construct)},
    {"nTSWorld::cWallSection::~cWallSection", 0x81fd40, 25, 0xb6a017f2, reinterpret_cast<void *>(&cWallSectionAdapters::DestructBody)},
    {"nTSWorld::cWallSection::vf4", 0x81fd60, 32, 0x6b03c8e9, reinterpret_cast<void *>(&cWallSectionAdapters::DeletingDestructor)},
    {"nTSWorld::cWallSection::vf1", 0x81fd80, 16, 0x198895b5, reinterpret_cast<void *>(&cWallSectionAdapters::EdgeAt)},
};
REGISTER_HOOKS(kHooks);

} // namespace
