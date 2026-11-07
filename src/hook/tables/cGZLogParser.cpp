#include "hook/hooks.h"
#include "gz/cGZLogParser.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZLogParser_Construct", 0xcac2b0, 32, 0xe39f31d7, reinterpret_cast<void *>(&nGZCommandParser::cGZLogParserAdapters::Construct)},
    {"cGZLogParser_Destruct", 0xcac4c0, 32, 0x249f09ba, reinterpret_cast<void *>(&nGZCommandParser::cGZLogParserAdapters::Destruct)},
    {"cGZLogParser_RegisterCommands", 0xcaf620, 32, 0xd05b332f, MemberAddress(&nGZCommandParser::cGZLogParser::RegisterCommands)},
};
REGISTER_HOOKS(kHooks);

} // namespace
