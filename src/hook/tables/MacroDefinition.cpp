#include "hook/hooks.h"
#include "gz/MacroDefinition.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZCommandParser_cBaseParser_cMacroDefinition_vf0", 0xaff0d0, 32, 0xc1101fd9, MemberAddress(&nGZCommandParser::cMacroDefinition::DeletingDestructor)},
    {"nGZCommandParser_cBaseParser_cMacroDefinition_Destruct", 0xafe6c0, 32, 0x4a3bbc60, MemberAddress(&nGZCommandParser::cMacroDefinition::Destruct)},
    {"nGZCommandParser_DefinitionVector_Insert", 0xafd3c0, 32, 0xf03e0439, MemberAddress(&nGZCommandParser::DefinitionVector::Insert)},
    {"nGZCommandParser_DefinitionVector_EmplaceReallocate", 0xafc950, 32, 0x1f0ad98b, MemberAddress(&nGZCommandParser::DefinitionVector::EmplaceReallocate)},
    {"nGZCommandParser_CopyHeldDefinitions", 0x29a2e0, 32, 0x685faaca, reinterpret_cast<void *>(&nGZCommandParser::CopyDefinitions)},
    {"nGZCommandParser_KeyRange_DefinitionBefore", 0xafea90, 32, 0xba7de55f, MemberAddress(&nGZCommandParser::DefinitionKey::DefinitionBefore)},
    {"nGZCommandParser_KeyRange_BeforeDefinition", 0xafeb10, 32, 0x707f0aff, MemberAddress(&nGZCommandParser::DefinitionKey::BeforeDefinition)},
    {"nGZCommandParser_LowerBoundDefinition", 0xafd790, 32, 0x6b6a98cd, reinterpret_cast<void *>(&nGZCommandParser::LowerBoundDefinition)},
    {"nGZCommandParser_EqualRangeDefinitions", 0xafd520, 32, 0xebdbecb6, reinterpret_cast<void *>(&nGZCommandParser::EqualRangeDefinitions)},
    {"nGZCommandParser_cDefineCommand_vf4", 0xb005e0, 32, 0xc4ac8cac, MemberAddress(&nGZCommandParser::cDefineCommand::Execute)},
    {"nGZCommandParser_cDefineCommand_vf10", 0xb038f0, 32, 0x6dd084d7, MemberAddress(&nGZCommandParser::cDefineCommand::HandleLine)},
    {"nGZCommandParser_cBaseParser_ExpandParameters", 0xb06920, 32, 0x9072a204, MemberAddress(&nGZCommandParser::cMacroParser::ExpandParameters)},
};
REGISTER_HOOKS(kHooks);

} // namespace
