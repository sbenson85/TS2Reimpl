#include "hook/hooks.h"
#include "gz/ParserFunctions.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZCommandParser_cMinVersionFunction_vf0", 0xafeeb0, 32, 0xfbe2ba9d, MemberAddress(&nGZCommandParser::cMinVersionFunction::Evaluate)},
    {"nGZCommandParser_cMinVersionFunction_vf4_5", 0xb03cf0, 6, 0x10fce8ca, MemberAddress(&nGZCommandParser::cMinVersionFunction::GetName)},
    {"nGZCommandParser_cMinVersionFunction_vf4_6", 0xaff750, 8, 0x7c1392ca, MemberAddress(&nGZCommandParser::cMinVersionFunction::GetHelp)},
    {"nGZCommandParser_cMaxVersionFunction_vf0", 0xafee50, 32, 0x4085801a, MemberAddress(&nGZCommandParser::cMaxVersionFunction::Evaluate)},
    {"nGZCommandParser_cMaxVersionFunction_vf4_5", 0xb03ce0, 6, 0x8cc60e7f, MemberAddress(&nGZCommandParser::cMaxVersionFunction::GetName)},
    {"nGZCommandParser_cMaxVersionFunction_vf4_6", 0xaff740, 8, 0xf8f9c1e9, MemberAddress(&nGZCommandParser::cMaxVersionFunction::GetHelp)},
    {"nGZCommandParser_cVarExistsFunction_vf0", 0xafef10, 32, 0x222599c4, MemberAddress(&nGZCommandParser::cVarExistsFunction::Evaluate)},
    {"nGZCommandParser_cVarExistsFunction_vf4_5", 0xb03df0, 6, 0x64f3769a, MemberAddress(&nGZCommandParser::cVarExistsFunction::GetName)},
    {"nGZCommandParser_cVarExistsFunction_vf4_6", 0xaff850, 8, 0x852ed77b, MemberAddress(&nGZCommandParser::cVarExistsFunction::GetHelp)},
    {"nGZCommandParser_cCommandExistsFunction_vf0", 0xafe850, 32, 0x222599c4, MemberAddress(&nGZCommandParser::cCommandExistsFunction::Evaluate)},
    {"nGZCommandParser_cCommandExistsFunction_vf4_5", 0xb03bb0, 6, 0x304bab1f, MemberAddress(&nGZCommandParser::cCommandExistsFunction::GetName)},
    {"nGZCommandParser_cCommandExistsFunction_vf4_6", 0xaff610, 8, 0xcff765b7, MemberAddress(&nGZCommandParser::cCommandExistsFunction::GetHelp)},
    {"nGZCommandParser_cDefinitionExistsFunction_vf0", 0xafe970, 32, 0x222599c4, MemberAddress(&nGZCommandParser::cDefinitionExistsFunction::Evaluate)},
    {"nGZCommandParser_cDefinitionExistsFunction_vf4_5", 0xb03c00, 6, 0xe937cbc6, MemberAddress(&nGZCommandParser::cDefinitionExistsFunction::GetName)},
    {"nGZCommandParser_cDefinitionExistsFunction_vf4_6", 0xaff660, 8, 0xad46cb33, MemberAddress(&nGZCommandParser::cDefinitionExistsFunction::GetHelp)},
    {"nGZCommandParser_cMatchFunction_vf0", 0xafeb90, 32, 0x3da199e1, MemberAddress(&nGZCommandParser::cMatchFunction::Evaluate)},
    {"nGZCommandParser_cMatchFunction_vf4_5", 0xb03cd0, 6, 0xb0e403b1, MemberAddress(&nGZCommandParser::cMatchFunction::GetName)},
    {"nGZCommandParser_cMatchFunction_vf4_6", 0xaff730, 8, 0x1731b7d5, MemberAddress(&nGZCommandParser::cMatchFunction::GetHelp)},
    {"cPropExistsFunction_vf0", 0xb3b1e0, 32, 0x5bfaffbf, MemberAddress(&nGZCommandParser::cPropExistsFunction::Evaluate)},
    {"cPropExistsFunction_vf4_5", 0xb3bb90, 6, 0x72c4bcfe, MemberAddress(&nGZCommandParser::cPropExistsFunction::GetName)},
    {"cPropExistsFunction_vf4_6", 0xb3b520, 8, 0x30b37ddd, MemberAddress(&nGZCommandParser::cPropExistsFunction::GetHelp)},
    {"nGZCommandParser_ExpressionEvaluator_ReadName", 0xb0a600, 32, 0x2c0f5d0f, reinterpret_cast<void *>(&nGZCommandParser::ReadName)},
};
REGISTER_HOOKS(kHooks);

} // namespace
