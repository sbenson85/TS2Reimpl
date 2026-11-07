// cGZGimexFactory (standalone shape): the director's creator and the constructor build ours; every slot is then ours
// through our vtables, and the helpers only those slots call are ours through them.
#include "hook/hooks.h"
#include "gz/cGZGimexFactory.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZGimexFactory_Create", 0x87b412, 22, 0x7f96cb39, reinterpret_cast<void *>(&cGZGimexFactoryAdapters::Create)},
    {"cGZGimexFactory_Construct", 0x87bcdc, 32, 0x9aeb4308,
     reinterpret_cast<void *>(&cGZGimexFactoryAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
