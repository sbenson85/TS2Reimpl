#include "hook/hooks.h"
#include "rz/StringKeyParser.h"

namespace {

// 0xae7460 (the bsearch comparator) is not a function start in db/functions.jsonl, so it is not hooked.
const HookSpec kHooks[] = {
    {"StringKeyParser_CrcTables_Initialize", 0x9cbb60, 32, 0x7ab7edf3, MemberAddress(&StringKeyParser::CrcTables::Initialize)},
    {"StringKeyParser_Crc24", 0x9cbbdd, 32, 0x9d10f810, reinterpret_cast<void *>(&StringKeyParser::Crc24)},
    {"StringKeyParser_Crc32", 0x9cbc3e, 32, 0xa39a9f5c, reinterpret_cast<void *>(&StringKeyParser::Crc32)},
    {"StringKeyParser_ParseNumber", 0x1b10f3, 32, 0x1ebb343b, reinterpret_cast<void *>(&StringKeyParser::ParseNumber)},
    {"StringKeyParser_EnsureServiceObject", 0xae7300, 32, 0x835d04bc, reinterpret_cast<void *>(&StringKeyParser::EnsureServiceObject)},
    {"cRZString_LowerCase_Helper", 0x1ad167, 19, 0xbd7614ec, MemberAddress(&cRZStringHelpers::LowerCase)},
    {"cRZString_EndsWithObject_Helper", 0x1acd5b, 32, 0xab1426e6, MemberAddress(&cRZStringHelpers::EndsWithObject)},
    {"cRZString_EndsWith_Helper", 0x1acd83, 32, 0x0e9c5251, MemberAddress(&cRZStringHelpers::EndsWith)},
    {"StringKeyParser_ParseKey", 0xae6e60, 32, 0xa3cb43b1, reinterpret_cast<void *>(&StringKeyParser::ParseKey)},
};
REGISTER_HOOKS(kHooks);

} // namespace
