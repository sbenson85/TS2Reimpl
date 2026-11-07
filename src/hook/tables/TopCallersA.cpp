#include "hook/hooks.h"
#include "rz/TopCallersA.h"

namespace {

const HookSpec kHooks[] = {
    {"SetWindowEnabled", 0x93526f, 32, 0x6595eb25, reinterpret_cast<void *>(&SetWindowEnabled)},
    {"nTSWorld_cWorldDBMessageData_Release", 0x5449e0, 31, 0x30ef402d, MemberAddress(&nTSWorld::cWorldDBMessageData::Release)},
    {"AV_cRZMemPtrIterator_GU_cGZElementByValue_G_vf3", 0xa70130, 32, 0x86379171, MemberAddress(&cRZMemPtrIteratorU16::Copy)},
    {"nGZCommandParser_cBlockCommand_vf3", 0xaf7bd0, 32, 0x5c1db811, MemberAddress(&nGZCommandParser::cBlockCommand::StoreAndQuery)},
};
REGISTER_HOOKS(kHooks);

} // namespace
