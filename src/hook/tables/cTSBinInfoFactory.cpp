#include "cas/cTSBinInfoFactory.h"
#include "hook/hooks.h"

namespace {

// nTSCASCore::cTSBinInfoFactory Read / Write (src/cas/cTSBinInfoFactory.cpp, lane DU) and CreateInstance2 (lane DY).
const HookSpec kHooks[] = {
    {"nTSCASCore_cTSBinInfoFactory_Read", 0xb8ddb0, 32, 0xd8e83559, MemberAddress(&nTSCASCore::cTSBinInfoFactory::Read)},
    {"nTSCASCore_cTSBinInfoFactory_Write", 0xb90e10, 32, 0x0fc051ef, MemberAddress(&nTSCASCore::cTSBinInfoFactory::Write)},
    {"nTSCASCore_cTSBinInfoFactory_CreateInstance2", 0xb89520, 32, 0x0a0c5304, MemberAddress(&nTSCASCore::cTSBinInfoFactory::CreateInstance2)},
};
REGISTER_HOOKS(kHooks);

} // namespace
