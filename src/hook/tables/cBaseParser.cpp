#include "hook/hooks.h"
#include "gz/cBaseParser.h"

namespace {

using nGZCommandParser::cBaseParserAdapters;
#define HU_ADAPTER(name) reinterpret_cast<void *>(&cBaseParserAdapters::name)

const HookSpec kHooks[] = {
    {"nGZCommandParser_cBaseParser_Construct", 0xafd800, 32, 0x66d61d3c, MemberAddress(&nGZCommandParser::cBaseParser::Construct)},
    {"nGZCommandParser_ParserNamespace_Construct", 0xafdc40, 32, 0x9096fcfd, MemberAddress(&nGZCommandParser::ParserNamespace::Construct)},
    // The cIGZUnknown face (lane HU).
    {"cBaseParser_QueryInterface", 0xb05940, 32, 0x0cf55d89, HU_ADAPTER(QueryInterface)},
    {"cBaseParser_AddRef", 0xaff100, 14, 0x7fb46298, HU_ADAPTER(AddRef)},
    {"cBaseParser_Release", 0xb05c50, 32, 0x2cfba537, HU_ADAPTER(Release)},
    // The primary vtable.
    {"cBaseParser_DropDefinitionsOfLevel", 0xb05c80, 32, 0x41132212, HU_ADAPTER(DropDefinitionsOfLevel)},
    // The cIBaseParser face.
    {"cBaseParser_Shutdown", 0xb06760, 32, 0x03c00ba6, HU_ADAPTER(Shutdown)},
    {"cBaseParser_SetVersionRange", 0xb06110, 30, 0x3b6c6463, HU_ADAPTER(SetVersionRange)},
    {"cBaseParser_SetContextState", 0xb06230, 13, 0xfcb04e58, HU_ADAPTER(SetContextState)},
    {"cBaseParser_RegisterCommand", 0xb059e0, 32, 0x80b71cc0, HU_ADAPTER(RegisterCommand)},
    {"cBaseParser_UnregisterCommand", 0xb072f0, 32, 0x58410728, HU_ADAPTER(UnregisterCommand)},
    {"cBaseParser_UnregisterCommandObject", 0xb072b0, 32, 0xd715442a, HU_ADAPTER(UnregisterCommandObject)},
    {"cBaseParser_RegisterMetaCommand", 0xb05b50, 32, 0xf828ee11, HU_ADAPTER(RegisterMetaCommand)},
    {"cBaseParser_RestartViaReset", 0xb05ea0, 23, 0x478e8795, HU_ADAPTER(RestartViaReset)},
    {"cBaseParser_Reset", 0xaff120, 32, 0x48143dbe, HU_ADAPTER(Reset)},
    {"cBaseParser_ResetBlocks", 0xb03e50, 32, 0xab7419fc, HU_ADAPTER(ResetBlocks)},
    {"cBaseParser_PushOpenBlock", 0xb068b0, 32, 0x85445e6e, HU_ADAPTER(PushOpenBlock)},
    {"cBaseParser_PushMetaBlock", 0xb068f0, 32, 0xbf204cad, HU_ADAPTER(PushMetaBlock)},
    {"cBaseParser_PopMetaBlock", 0xaff880, 32, 0x836b629a, HU_ADAPTER(PopMetaBlock)},
    {"cBaseParser_RunExpandedText", 0xb06350, 32, 0x8e2c7232, HU_ADAPTER(RunExpandedText)},
    {"cBaseParser_PopNamespace", 0xb055b0, 32, 0xc678175a, HU_ADAPTER(PopNamespace)},
    {"cBaseParser_GetFunctions", 0xb02440, 7, 0x416b94bc, HU_ADAPTER(GetFunctions)},
    {"cBaseParser_SetOutput", 0xb06250, 32, 0x982db12b, HU_ADAPTER(SetOutput)},
    {"cBaseParser_SetHandler", 0xb06310, 32, 0xd8d46fce, HU_ADAPTER(SetHandler)},
    {"cBaseParser_SetSource", 0xb06140, 32, 0xdda30541, HU_ADAPTER(SetSource)},
    {"cBaseParser_GetNamespacePath", 0xaff4d0, 15, 0x6ae3be6f, HU_ADAPTER(GetNamespacePath)},
    {"cBaseParser_EvalBool", 0xb03f30, 32, 0xf95d2067, HU_ADAPTER(EvalBool)},
    {"cBaseParser_EvalFloat", 0xb050c0, 32, 0x6ff7b1e4, HU_ADAPTER(EvalFloat)},
    {"cBaseParser_EvalInt", 0xb05120, 32, 0xb04263a7, HU_ADAPTER(EvalInt)},
    {"cBaseParser_EvalVector2", 0xb052e0, 32, 0xa46bd8d2, HU_ADAPTER(EvalVector2)},
    {"cBaseParser_EvalVector3", 0xb05370, 32, 0xbdb3a4cf, HU_ADAPTER(EvalVector3)},
    {"cBaseParser_EvalVector4", 0xb05410, 32, 0x83ea6436, HU_ADAPTER(EvalVector4)},
    {"cBaseParser_EvalColorRGBA", 0xb05190, 32, 0x34b70094, HU_ADAPTER(EvalColorRGBA)},
    {"cBaseParser_EvalColorARGB", 0xb05200, 32, 0x058fb64e, HU_ADAPTER(EvalColorARGB)},
    {"cBaseParser_EvalColorRGB", 0xb05260, 32, 0xaa5e5e67, HU_ADAPTER(EvalColorRGB)},
    {"cBaseParser_CommandCount", 0xb03e20, 10, 0xc5f0457c, HU_ADAPTER(CommandCount)},
    {"cBaseParser_CommandAt", 0xaff2e0, 13, 0x981ef6b7, HU_ADAPTER(CommandAt)},
    {"cBaseParser_IsInitialised", 0xb035b0, 7, 0x25ed12b4, HU_ADAPTER(IsInitialised)},
    {"cBaseParser_IsCaseSensitive", 0xb035a0, 7, 0x21dcd0b1, HU_ADAPTER(IsCaseSensitive)},
    {"cBaseParser_GetText", 0xb035c0, 24, 0xe2938601, HU_ADAPTER(GetText)},
    {"cBaseParser_GetFileName", 0xb04680, 32, 0x7b306bd8, HU_ADAPTER(GetFileName)},
    {"cBaseParser_GetLine", 0xb04670, 7, 0xc2f49eaa, HU_ADAPTER(GetLine)},
    {"cBaseParser_HasDefinition", 0xaff4f0, 32, 0xdded2704, HU_ADAPTER(HasDefinition)},
    {"cBaseParser_HasCommand", 0xaff300, 32, 0x2876d0b6, HU_ADAPTER(HasCommand)},
    {"cBaseParser_GetSource", 0xb068a0, 7, 0xe74b4c87, HU_ADAPTER(GetSource)},
    // Helpers only the parser's slots (and the undefine command) call.
    {"nGZCommandParser_MoveCountedPointers", 0xafcc10, 32, 0x23788f7a, reinterpret_cast<void *>(&nGZCommandParser::MoveCountedPointers)},
    {"EvalFunctionTable_InsertSorted", 0xb09550, 32, 0x63808f70, HU_ADAPTER(InsertEvalFunction)},
    // Namespaces (cParserNamespaces.cpp).
    {"cBaseParser_PushNamespace", 0xb05770, 32, 0x936520e0, HU_ADAPTER(PushNamespace)},
    {"cBaseParser_PurgeNamespace", 0xb05640, 32, 0xa3920f17, HU_ADAPTER(PurgeNamespace)},
    {"ParserNamespace_Clear", 0xb05d70, 32, 0x0cc609f7, MemberAddress(&nGZCommandParser::ParserNamespace::Clear)},
    {"ParserNamespace_Destruct", 0xafe800, 32, 0x19058555, MemberAddress(&nGZCommandParser::ParserNamespace::Destruct)},
    {"ParserNamespace_FindPath", 0xb036d0, 32, 0x5f4a63d8, MemberAddress(&nGZCommandParser::ParserNamespace::FindPath)},
    {"VariableTable_ClearNodes", 0xafcb90, 32, 0x4497eab1, reinterpret_cast<void *>(&nGZCommandParser::VariableTable::ClearNodes)},
    {"NamePool_Reset", 0xaf6960, 32, 0xf299f3bf, HU_ADAPTER(ResetNamePool)},
    {"ParserNamespace_EraseChildNode", 0xafd700, 32, 0x5695514b, HU_ADAPTER(EraseChildNode)},
    {"ParserNamespace_InsertChild", 0xafce70, 32, 0x778baa3f, HU_ADAPTER(InsertChild)},
    {"ParserNamespace_RehashChildren", 0xb07790, 32, 0x097fe1a1, HU_ADAPTER(RehashChildren)},
    // Variables (cParserVariables.cpp).
    {"cBaseParser_FindVariable", 0xb07520, 32, 0xc07ea239, HU_ADAPTER(FindVariable)},
    {"cBaseParser_ExpandVariables", 0xb06fb0, 32, 0xd9b0a345, HU_ADAPTER(ExpandVariables)},
    // Pending calls and the destructor.
    {"cBaseParser_DestroyPendingCalls", 0xafc470, 32, 0x59390c16, reinterpret_cast<void *>(&nGZCommandParser::DestroyPendingCalls)},
    {"cBaseParser_DestroyPendingCallVector", 0xafe000, 32, 0xf81788cc, HU_ADAPTER(DestroyPendingCallVector)},
    {"cBaseParser_Destruct", 0xafe1b0, 32, 0x6e321514, MemberAddress(&nGZCommandParser::cBaseParser::Destruct)},
    {"cBaseParser_DeletingDestructor", 0xaff070, 32, 0x4141d400, HU_ADAPTER(DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

#undef HU_ADAPTER

} // namespace
