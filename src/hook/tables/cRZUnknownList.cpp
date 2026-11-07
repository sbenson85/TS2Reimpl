#include "hook/hooks.h"
#include "rz/cRZUnknownList.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZUnknownList_Construct", 0x1b905f, 25, 0xfcf9892d, reinterpret_cast<void *>(&cRZUnknownListAdapters::Construct)},
    {"cRZUnknownList_Destruct", 0x1b909d, 14, 0xad1734db, reinterpret_cast<void *>(&cRZUnknownListAdapters::Destruct)},
    {"cRZUnknownList_PushBack", 0x1b9333, 32, 0xa42305ff, reinterpret_cast<void *>(&cRZUnknownListAdapters::PushBack)},
};
REGISTER_HOOKS(kHooks);

} // namespace
