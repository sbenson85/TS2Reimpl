#include "hook/hooks.h"
#include "gz/LightingCommands.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "sg/cLightingParserState.h"

// The lighting file's parser states (lane 261110y, scenegraph LG3g and LG7f; src/sg/cLightingParserState.h).
// cTSLightingParserState's face thunks: 14-byte `sub ecx, [ecx-4]; sub ecx, 0x258; jmp body` to cBaseParser's adapters,
// 8-byte `sub ecx, [ecx-4]; jmp body` to its own. nTSSG::cLightingParserState's: shift 0x27c to cBaseParser's, 8 bytes to
// its own, and 11-byte `sub ecx, [ecx-4]; sub ecx, 0x24; jmp body` into the base class's bodies (QueryInterface,
// ResetBlocks). The base class's Shutdown thunk 0xb3f710 jumps to an 11-byte body at 0xb3f720 (cBaseParser::Shutdown at the
// cBaseParser face) that nothing else reaches; the hook covers both. The constructor 0xb3ce70 is hooked in
// cLightingManager's table (the derived constructor is inlined in the manager's Init, src/sg/cLightingManagerInit.cpp).
// The commands Initialise registers are made with the game's vtables, so their slots are hooked at their RVAs.
namespace {

using nTSSG::cLightingParserStateAdapters;

#define BASE_SHIFTED(adapter) \
    reinterpret_cast<void *>(&LightingShiftedThunk<0x258, &nGZCommandParser::cBaseParserAdapters::adapter>::Call)
#define DERIVED_SHIFTED(adapter) \
    reinterpret_cast<void *>(&LightingShiftedThunk<0x27c, &nGZCommandParser::cBaseParserAdapters::adapter>::Call)
#define BASE_VTORDISP(adapter) reinterpret_cast<void *>(&nGZCommandParser::VtordispThunk<&cTSLightingParserStateAdapters::adapter>::Call)
#define DERIVED_VTORDISP(adapter) reinterpret_cast<void *>(&nGZCommandParser::VtordispThunk<&cLightingParserStateAdapters::adapter>::Call)
#define INTO_BASE(adapter) reinterpret_cast<void *>(&LightingShiftedThunk<0x24, &cTSLightingParserStateAdapters::adapter>::Call)
#define BASE_OWN(adapter) reinterpret_cast<void *>(&cTSLightingParserStateAdapters::adapter)
#define DERIVED_OWN(adapter) reinterpret_cast<void *>(&cLightingParserStateAdapters::adapter)

const HookSpec kHooks[] = {
    // ---- cTSLightingParserState: bodies
    {"cTSLightingParserState_Destruct", 0xb3d3e0, 32, 0xba6088dc, BASE_OWN(Destruct)},
    {"cTSLightingParserState_vf0", 0xb3d860, 32, 0x319ab38a, BASE_OWN(DeletingDestructor)},
    {"cTSLightingParserState_vf5", 0xb3eda0, 32, 0x993e11d4, BASE_OWN(FindState)},
    {"cTSLightingParserState_Initialise", 0xb3eea0, 32, 0x8ad4a616, BASE_OWN(Initialise)},
    {"cTSLightingParserState_Reset", 0xb3d8a0, 32, 0xead24fff, BASE_OWN(Reset)},
    {"cTSLightingParserState_ResetBlocks", 0xb3f140, 11, 0xf826f48c, BASE_OWN(ResetBlocks)},
    {"cTSLightingParserState_QueryInterface", 0xb3f640, 32, 0xeb8d0289, BASE_OWN(QueryInterface)},
    {"LightingSettingsEntry_CopyConstruct", 0xb3d190, 32, 0x8f323ce3, BASE_OWN(CopySettings)},
    {"LightingState_operator_assign", 0xb3d510, 32, 0xad183d1d, BASE_OWN(AssignState)},
    // ---- cTSLightingParserState: the cIGZUnknown face
    {"cTSLightingParserState_vf1032_0", 0xb3f632, 8, 0x7252b2d3, BASE_VTORDISP(QueryInterface)},
    {"cTSLightingParserState_vf1032_1", 0xb3d883, 14, 0x1ff0dbdc, BASE_SHIFTED(AddRef)},
    {"cTSLightingParserState_vf1032_2", 0xb3f6a0, 14, 0xc7fe1604, BASE_SHIFTED(Release)},
    // ---- cTSLightingParserState: the cIBaseParser face
    {"cTSLightingParserState_vf1040_0", 0xb3ee93, 8, 0x60e71d3d, BASE_VTORDISP(Initialise)},
    {"cTSLightingParserState_vf1040_1", 0xb3f710, 8, 0x928dc5e0, BASE_VTORDISP(Shutdown)},
    {"cTSLightingParserState_vf1040_2", 0xb3f6bc, 14, 0x95bf8756, BASE_SHIFTED(SetVersionRange)},
    {"cTSLightingParserState_vf1040_3", 0xb3f6d8, 14, 0xdeab52d9, BASE_SHIFTED(SetContextState)},
    {"cTSLightingParserState_vf1040_4", 0xb3f684, 14, 0x5fda9436, BASE_SHIFTED(RegisterCommand)},
    {"cTSLightingParserState_vf1040_5", 0xb400ee, 14, 0x32b74dd6, BASE_SHIFTED(UnregisterCommand)},
    {"cTSLightingParserState_vf1040_6", 0xb400e0, 14, 0xe7f7eaab, BASE_SHIFTED(UnregisterCommandObject)},
    {"cTSLightingParserState_vf1040_7", 0xb3f692, 14, 0x3c2ce327, BASE_SHIFTED(RegisterMetaCommand)},
    {"cTSLightingParserState_vf1040_8", 0xb3f6ae, 14, 0x7a05c58b, BASE_SHIFTED(RestartViaReset)},
    {"cTSLightingParserState_vf1040_9", 0xb3d891, 8, 0xcaeed5b6, BASE_VTORDISP(Reset)},
    {"cTSLightingParserState_vf1040_10", 0xb3f130, 8, 0x928dc5e0, BASE_VTORDISP(ResetBlocks)},
    {"cTSLightingParserState_vf1040_11", 0xb3d9cb, 14, 0x0e45aacc, BASE_SHIFTED(NoOp)},
    {"cTSLightingParserState_vf1040_12", 0xb3f552, 14, 0xe56dfc1d, BASE_SHIFTED(RunFile)},
    {"cTSLightingParserState_vf1040_13", 0xb3f58a, 14, 0x88a56f42, BASE_SHIFTED(RunFiles)},
    {"cTSLightingParserState_vf1040_14", 0xb3f57c, 14, 0x86a77a24, BASE_SHIFTED(RunText)},
    {"cTSLightingParserState_vf1040_15", 0xb3f544, 14, 0x6846e959, BASE_SHIFTED(RunLine)},
    {"cTSLightingParserState_vf1040_16", 0xb3f739, 14, 0x7d9d74af, BASE_SHIFTED(PushOpenBlock)},
    {"cTSLightingParserState_vf1040_17", 0xb3f747, 14, 0x51f2aa83, BASE_SHIFTED(PushMetaBlock)},
    {"cTSLightingParserState_vf1040_18", 0xb3da93, 14, 0x044957ad, BASE_SHIFTED(PopMetaBlock)},
    {"cTSLightingParserState_vf1040_19", 0xb3f702, 14, 0xb459a438, BASE_SHIFTED(RunExpandedText)},
    {"cTSLightingParserState_vf1040_20", 0xb3f624, 14, 0x3761e0c4, BASE_SHIFTED(PushNamespace)},
    {"cTSLightingParserState_vf1040_21", 0xb3f608, 14, 0xdb9da60c, BASE_SHIFTED(PopNamespace)},
    {"cTSLightingParserState_vf1040_22", 0xb3f616, 14, 0x3c98ed46, BASE_SHIFTED(PurgeNamespace)},
    {"cTSLightingParserState_vf1040_23", 0xb3ed8e, 14, 0x8c6fd931, BASE_SHIFTED(GetFunctions)},
    {"cTSLightingParserState_vf1040_24", 0xb3f6e6, 14, 0xabd95a9a, BASE_SHIFTED(SetOutput)},
    {"cTSLightingParserState_vf1040_25", 0xb3f6f4, 14, 0xcf928b36, BASE_SHIFTED(SetHandler)},
    {"cTSLightingParserState_vf1040_26", 0xb3f6ca, 14, 0xd5525f5b, BASE_SHIFTED(SetSource)},
    {"cTSLightingParserState_vf1040_27", 0xb400fc, 14, 0x480a068e, BASE_SHIFTED(FindVariable)},
    {"cTSLightingParserState_vf1040_28", 0xb3d9d9, 14, 0xd446e655, BASE_SHIFTED(GetNamespacePath)},
    {"cTSLightingParserState_vf1040_29", 0xb3f755, 14, 0x5d1c7749, BASE_SHIFTED(ExpandVariables)},
    {"cTSLightingParserState_vf1040_30", 0xb3f536, 14, 0x8d62868e, BASE_SHIFTED(EvalBool)},
    {"cTSLightingParserState_vf1040_31", 0xb3f598, 14, 0x31cdb8eb, BASE_SHIFTED(EvalFloat)},
    {"cTSLightingParserState_vf1040_32", 0xb3f5a6, 14, 0x7f9947ab, BASE_SHIFTED(EvalInt)},
    {"cTSLightingParserState_vf1040_34", 0xb3f5de, 14, 0x02229865, BASE_SHIFTED(EvalVector2)},
    {"cTSLightingParserState_vf1040_35", 0xb3f5ec, 14, 0x44b08ce2, BASE_SHIFTED(EvalVector3)},
    {"cTSLightingParserState_vf1040_36", 0xb3f5fa, 14, 0xcd16e514, BASE_SHIFTED(EvalVector4)},
    {"cTSLightingParserState_vf1040_37", 0xb3f5b4, 14, 0x98fb9625, BASE_SHIFTED(EvalColorRGBA)},
    {"cTSLightingParserState_vf1040_38", 0xb3f5c2, 14, 0xfb0de134, BASE_SHIFTED(EvalColorARGB)},
    {"cTSLightingParserState_vf1040_39", 0xb3f5d0, 14, 0x4c766725, BASE_SHIFTED(EvalColorRGB)},
    {"cTSLightingParserState_vf1040_40", 0xb3f0b6, 14, 0x971960c8, BASE_SHIFTED(CommandCount)},
    {"cTSLightingParserState_vf1040_41", 0xb3d9af, 14, 0xcff0566e, BASE_SHIFTED(CommandAt)},
    {"cTSLightingParserState_vf1040_42", 0xb3ef97, 14, 0xcd96b832, BASE_SHIFTED(IsInitialised)},
    {"cTSLightingParserState_vf1040_43", 0xb3ef89, 14, 0x679f70b9, BASE_SHIFTED(IsCaseSensitive)},
    {"cTSLightingParserState_vf1040_44", 0xb3efa5, 14, 0xe8fde7ee, BASE_SHIFTED(GetText)},
    {"cTSLightingParserState_vf1040_45", 0xb4010a, 14, 0x82845eee, BASE_SHIFTED(GetVersion)},
    {"cTSLightingParserState_vf1040_46", 0xb3f56e, 14, 0x8c9c1d91, BASE_SHIFTED(GetFileName)},
    {"cTSLightingParserState_vf1040_47", 0xb3f560, 14, 0xa9f7424d, BASE_SHIFTED(GetLine)},
    {"cTSLightingParserState_vf1040_48", 0xb3d9e7, 14, 0x2e567941, BASE_SHIFTED(HasDefinition)},
    {"cTSLightingParserState_vf1040_49", 0xb3d9bd, 14, 0xba825e2d, BASE_SHIFTED(HasCommand)},
    {"cTSLightingParserState_vf1040_50", 0xb3f72b, 14, 0x9d42039c, BASE_SHIFTED(GetSource)},
    // ---- the timeOfDay command and the help lines of the state's commands
    {"AUcTimeOfDayCommand_A0x036c3978_vf4", 0xb3ea80, 32, 0x0ff10e67, MemberAddress(&nTSSG::cTimeOfDayCommand::Execute)},
    {"AUcTimeOfDayCommand_A0x036c3978_vf5", 0xb3f080, 6, 0x271834ab, MemberAddress(&nTSSG::cTimeOfDayCommand::GetName)},
    {"AUcTimeOfDayCommand_A0x036c3978_vf6", 0xb3da50, 8, 0x5e956727, MemberAddress(&nTSSG::cTimeOfDayCommand::GetHelp)},
    {"AUcControlCommand_A0x036c3978_vf6", 0xb3da00, 32, 0xe5171775, MemberAddress(&cControlCommand::GetHelp)},
    {"AUcFogCommand_A0x036c3978_vf6", 0xb3da30, 8, 0x80cc7ef1, MemberAddress(&cFogCommand::GetHelp)},
    {"AUcLightMapCommand_A0x036c3978_vf6", 0xb3da40, 8, 0x0b7a3214, MemberAddress(&cLightMapCommand::GetHelp)},
    {"AUcTransitionTimesCommand_A0x036c3978_vf6", 0xb3da60, 8, 0xebee5bee, MemberAddress(&cTransitionTimesCommand::GetHelp)},
    {"AUcUnlitRoomColorCommand_A0x036c3978_vf6", 0xb3da70, 32, 0x8d3e2aee, MemberAddress(&cUnlitRoomColorCommand::GetHelp)},
    // ---- nTSSG::cLightingParserState: bodies
    {"nTSSG_cLightingParserState_vf0", 0x2cbc70, 32, 0x3d8dd063, DERIVED_OWN(DeletingDestructor)},
    {"nTSSG_cLightingParserState_Initialise", 0x2da270, 32, 0xf89c4a50, DERIVED_OWN(Initialise)},
    {"nTSSG_cLightingParserState_Shutdown", 0x2e30e0, 32, 0x89c231e5, DERIVED_OWN(Shutdown)},
    {"nTSSG_cLightingParserState_RestartViaReset", 0x2e02e0, 32, 0xc4b882c3, DERIVED_OWN(RestartViaReset)},
    {"nTSSG_cLightingParserState_Reset", 0x2d2810, 25, 0x9949673c, DERIVED_OWN(Reset)},
    // ---- nTSSG::cLightingParserState: the cIGZUnknown face
    {"nTSSG_cLightingParserState_vf1068_0", 0x2dfdf5, 11, 0x481530b3, INTO_BASE(QueryInterface)},
    {"nTSSG_cLightingParserState_vf1068_1", 0x2d023b, 14, 0x0efa2d92, DERIVED_SHIFTED(AddRef)},
    {"nTSSG_cLightingParserState_vf1068_2", 0x2dfed0, 14, 0x887173ef, DERIVED_SHIFTED(Release)},
    // ---- nTSSG::cLightingParserState: the cIBaseParser face
    {"nTSSG_cLightingParserState_vf1076_0", 0x2da25d, 8, 0x80386a0e, DERIVED_VTORDISP(Initialise)},
    {"nTSSG_cLightingParserState_vf1076_1", 0x2e30d5, 8, 0x458c42e1, DERIVED_VTORDISP(Shutdown)},
    {"nTSSG_cLightingParserState_vf1076_2", 0x2e154f, 14, 0x5e2b254e, DERIVED_SHIFTED(SetVersionRange)},
    {"nTSSG_cLightingParserState_vf1076_3", 0x2e29d4, 14, 0x7c903d3d, DERIVED_SHIFTED(SetContextState)},
    {"nTSSG_cLightingParserState_vf1076_4", 0x2dfe00, 14, 0xc1971658, DERIVED_SHIFTED(RegisterCommand)},
    {"nTSSG_cLightingParserState_vf1076_5", 0x2e36fb, 14, 0x4eb5d5eb, DERIVED_SHIFTED(UnregisterCommand)},
    {"nTSSG_cLightingParserState_vf1076_6", 0x2e36ed, 14, 0x2fb112c2, DERIVED_SHIFTED(UnregisterCommandObject)},
    {"nTSSG_cLightingParserState_vf1076_7", 0x2dfec2, 14, 0xa5dcc7f4, DERIVED_SHIFTED(RegisterMetaCommand)},
    {"nTSSG_cLightingParserState_vf1076_8", 0x2e02cd, 8, 0x80386a0e, DERIVED_VTORDISP(RestartViaReset)},
    {"nTSSG_cLightingParserState_vf1076_9", 0x2d2805, 8, 0x458c42e1, DERIVED_VTORDISP(Reset)},
    {"nTSSG_cLightingParserState_vf1076_10", 0x2df399, 11, 0x225cf479, INTO_BASE(ResetBlocks)},
    {"nTSSG_cLightingParserState_vf1076_11", 0x2d2a34, 14, 0xf30663e9, DERIVED_SHIFTED(NoOp)},
    {"nTSSG_cLightingParserState_vf1076_12", 0x2dfbc6, 14, 0x040e8b41, DERIVED_SHIFTED(RunFile)},
    {"nTSSG_cLightingParserState_vf1076_13", 0x2dfbfe, 14, 0x2a28d77e, DERIVED_SHIFTED(RunFiles)},
    {"nTSSG_cLightingParserState_vf1076_14", 0x2dfbf0, 14, 0xe755281c, DERIVED_SHIFTED(RunText)},
    {"nTSSG_cLightingParserState_vf1076_15", 0x2df88a, 14, 0x9d4215b1, DERIVED_SHIFTED(RunLine)},
    {"nTSSG_cLightingParserState_vf1076_16", 0x2e31a1, 14, 0xb4bb326b, DERIVED_SHIFTED(PushOpenBlock)},
    {"nTSSG_cLightingParserState_vf1076_17", 0x2e31af, 14, 0xee990241, DERIVED_SHIFTED(PushMetaBlock)},
    {"nTSSG_cLightingParserState_vf1076_18", 0x2d4e10, 14, 0x68e34125, DERIVED_SHIFTED(PopMetaBlock)},
    {"nTSSG_cLightingParserState_vf1076_19", 0x2e2a86, 14, 0x50ffe311, DERIVED_SHIFTED(RunExpandedText)},
    {"nTSSG_cLightingParserState_vf1076_20", 0x2dfd8a, 14, 0xadae2252, DERIVED_SHIFTED(PushNamespace)},
    {"nTSSG_cLightingParserState_vf1076_21", 0x2dfd6e, 14, 0x30c5b6e5, DERIVED_SHIFTED(PopNamespace)},
    {"nTSSG_cLightingParserState_vf1076_22", 0x2dfd7c, 14, 0x7795c855, DERIVED_SHIFTED(PurgeNamespace)},
    {"nTSSG_cLightingParserState_vf1076_23", 0x2d50d3, 14, 0x31823286, DERIVED_SHIFTED(GetFunctions)},
    {"nTSSG_cLightingParserState_vf1076_24", 0x2e29e2, 14, 0x3c7de530, DERIVED_SHIFTED(SetOutput)},
    {"nTSSG_cLightingParserState_vf1076_25", 0x2e2a78, 14, 0x85bf446c, DERIVED_SHIFTED(SetHandler)},
    {"nTSSG_cLightingParserState_vf1076_26", 0x2e29c6, 14, 0x7df71db5, DERIVED_SHIFTED(SetSource)},
    {"nTSSG_cLightingParserState_vf1076_27", 0x2e5689, 14, 0xabcad1d0, DERIVED_SHIFTED(FindVariable)},
    {"nTSSG_cLightingParserState_vf1076_28", 0x2d3804, 14, 0x1cca98c6, DERIVED_SHIFTED(GetNamespacePath)},
    {"nTSSG_cLightingParserState_vf1076_29", 0x2e31bd, 14, 0xe03161d2, DERIVED_SHIFTED(ExpandVariables)},
    {"nTSSG_cLightingParserState_vf1076_30", 0x2df87c, 14, 0x313411b0, DERIVED_SHIFTED(EvalBool)},
    {"nTSSG_cLightingParserState_vf1076_31", 0x2dfc0c, 14, 0x2e9666ae, DERIVED_SHIFTED(EvalFloat)},
    {"nTSSG_cLightingParserState_vf1076_32", 0x2dfc1a, 14, 0x05441e3f, DERIVED_SHIFTED(EvalInt)},
    {"nTSSG_cLightingParserState_vf1076_34", 0x2dfc52, 14, 0xa84b4222, DERIVED_SHIFTED(EvalVector2)},
    {"nTSSG_cLightingParserState_vf1076_35", 0x2dfc60, 14, 0xeed956a5, DERIVED_SHIFTED(EvalVector3)},
    {"nTSSG_cLightingParserState_vf1076_36", 0x2dfc6e, 14, 0x76f2e8dd, DERIVED_SHIFTED(EvalVector4)},
    {"nTSSG_cLightingParserState_vf1076_37", 0x2dfc28, 14, 0xe3e4a586, DERIVED_SHIFTED(EvalColorRGBA)},
    {"nTSSG_cLightingParserState_vf1076_38", 0x2dfc36, 14, 0x10f0bb62, DERIVED_SHIFTED(EvalColorARGB)},
    {"nTSSG_cLightingParserState_vf1076_39", 0x2dfc44, 14, 0xa5cd832a, DERIVED_SHIFTED(EvalColorRGB)},
    {"nTSSG_cLightingParserState_vf1076_40", 0x2df38b, 14, 0x20a0208f, DERIVED_SHIFTED(CommandCount)},
    {"nTSSG_cLightingParserState_vf1076_41", 0x2d2a18, 14, 0xc050cf1d, DERIVED_SHIFTED(CommandAt)},
    {"nTSSG_cLightingParserState_vf1076_42", 0x2db041, 14, 0x4947ef16, DERIVED_SHIFTED(IsInitialised)},
    {"nTSSG_cLightingParserState_vf1076_43", 0x2db033, 14, 0x59b36084, DERIVED_SHIFTED(IsCaseSensitive)},
    {"nTSSG_cLightingParserState_vf1076_44", 0x2db04f, 14, 0xe34e279d, DERIVED_SHIFTED(GetText)},
    {"nTSSG_cLightingParserState_vf1076_45", 0x2e5697, 14, 0xf7f75dbd, DERIVED_SHIFTED(GetVersion)},
    {"nTSSG_cLightingParserState_vf1076_46", 0x2dfbe2, 14, 0xed6e4fa9, DERIVED_SHIFTED(GetFileName)},
    {"nTSSG_cLightingParserState_vf1076_47", 0x2dfbd4, 14, 0x5da86f05, DERIVED_SHIFTED(GetLine)},
    {"nTSSG_cLightingParserState_vf1076_48", 0x2d3812, 14, 0xc73318f6, DERIVED_SHIFTED(HasDefinition)},
    {"nTSSG_cLightingParserState_vf1076_49", 0x2d2a26, 14, 0x8e04305d, DERIVED_SHIFTED(HasCommand)},
    {"nTSSG_cLightingParserState_vf1076_50", 0x2e310e, 14, 0x6e3c7462, DERIVED_SHIFTED(GetSource)},
};
REGISTER_HOOKS(kHooks);

} // namespace
