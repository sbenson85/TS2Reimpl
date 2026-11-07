#include "hook/hooks.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "ts/cTSEffectsParser.h"

// cTSEffectsParser (hook-shaped, lane WU; src/ts/cTSEffectsParser.h): its faces' thunks, its own bodies, its constructor and
// destructor. The faces' 14-byte thunks (`sub ecx, [ecx-4]; sub ecx, 0xf4; jmp body`) forward to cBaseParser's adapters at
// cBaseParser's own face; the 8-byte ones (`sub ecx, [ecx-4]; jmp body`) to the parser's bodies. Slots 10 and 11 of the
// cIBaseParser face have longer thunks that end in cBaseParser's ResetBlocks and in a no-op: ours behave the same.
namespace {

#define SHIFTED(adapter) reinterpret_cast<void *>(&EffectsShiftedThunk<&nGZCommandParser::cBaseParserAdapters::adapter>::Call)
#define VTORDISP_OWN(adapter) reinterpret_cast<void *>(&nGZCommandParser::VtordispThunk<&cTSEffectsParserAdapters::adapter>::Call)
#define OWN(adapter) reinterpret_cast<void *>(&cTSEffectsParserAdapters::adapter)

const HookSpec kHooks[] = {
    {"cTSEffectsParser_vf676_1", 0xbcdaec, 14, 0x946d2415, SHIFTED(AddRef)},
    {"cTSEffectsParser_vf684_9", 0xbcdafa, 8, 0xb7e69a3c, VTORDISP_OWN(Reset)},
    {"cTSEffectsParser_vf684_41", 0xbcdc17, 14, 0x7b75b252, SHIFTED(CommandAt)},
    {"cTSEffectsParser_vf684_49", 0xbcdc25, 14, 0x35214d12, SHIFTED(HasCommand)},
    {"cTSEffectsParser_vf684_11", 0xbcdc33, 24, 0x20e11ad4, SHIFTED(NoOp)},
    {"cTSEffectsParser_vf684_28", 0xbcdc4b, 14, 0x4d70cee6, SHIFTED(GetNamespacePath)},
    {"cTSEffectsParser_vf684_48", 0xbcdc59, 14, 0x2bc9af72, SHIFTED(HasDefinition)},
    {"cTSEffectsParser_vf696_5", 0xbcdc67, 8, 0xef858a6a, VTORDISP_OWN(Lookup)},
    {"cTSEffectsParser_vf684_18", 0xbcddaf, 14, 0xdce4c345, SHIFTED(PopMetaBlock)},
    {"cTSEffectsParser_vf684_23", 0xbcf792, 14, 0xd591028a, SHIFTED(GetFunctions)},
    {"cTSEffectsParser_vf684_0", 0xbcf816, 8, 0xfd302584, VTORDISP_OWN(Initialise)},
    {"cTSEffectsParser_vf684_43", 0xbd0048, 14, 0x7fa43b29, SHIFTED(IsCaseSensitive)},
    {"cTSEffectsParser_vf684_42", 0xbd0056, 14, 0x5acf64f5, SHIFTED(IsInitialised)},
    {"cTSEffectsParser_vf684_44", 0xbd0064, 14, 0xf0c6ac7e, SHIFTED(GetText)},
    {"cTSEffectsParser_vf684_40", 0xbd0072, 14, 0xc875cbcd, SHIFTED(CommandCount)},
    {"cTSEffectsParser_vf684_10", 0xbd054b, 19, 0xb117c9a4, SHIFTED(ResetBlocks)},
    {"cTSEffectsParser_vf684_30", 0xbd07f9, 14, 0x55b3b7aa, SHIFTED(EvalBool)},
    {"cTSEffectsParser_vf684_15", 0xbd0807, 14, 0xb2d16624, SHIFTED(RunLine)},
    {"cTSEffectsParser_vf684_12", 0xbd1691, 14, 0xa7e2007a, SHIFTED(RunFile)},
    {"cTSEffectsParser_vf684_47", 0xbd169f, 14, 0xfe44e43e, SHIFTED(GetLine)},
    {"cTSEffectsParser_vf684_46", 0xbd16ad, 14, 0xdb2fbbe2, SHIFTED(GetFileName)},
    {"cTSEffectsParser_vf684_14", 0xbd16bb, 14, 0xd114dc57, SHIFTED(RunText)},
    {"cTSEffectsParser_vf684_13", 0xbd16c9, 14, 0xc86c2d7c, SHIFTED(RunFiles)},
    {"cTSEffectsParser_vf684_31", 0xbd16d7, 14, 0x7342448c, SHIFTED(EvalFloat)},
    {"cTSEffectsParser_vf684_32", 0xbd16e5, 14, 0xd209b3ae, SHIFTED(EvalInt)},
    {"cTSEffectsParser_vf684_37", 0xbd16f3, 14, 0x356b6220, SHIFTED(EvalColorRGBA)},
    {"cTSEffectsParser_vf684_38", 0xbd1701, 14, 0xbe8bb58f, SHIFTED(EvalColorARGB)},
    {"cTSEffectsParser_vf684_39", 0xbd170f, 14, 0x083259a9, SHIFTED(EvalColorRGB)},
    {"cTSEffectsParser_vf684_34", 0xbd171d, 14, 0xab3f10d2, SHIFTED(EvalVector2)},
    {"cTSEffectsParser_vf684_35", 0xbd172b, 14, 0xe92078e7, SHIFTED(EvalVector3)},
    {"cTSEffectsParser_vf684_36", 0xbd1739, 14, 0x8f991973, SHIFTED(EvalVector4)},
    {"cTSEffectsParser_vf684_21", 0xbd1747, 14, 0x9b54e432, SHIFTED(PopNamespace)},
    {"cTSEffectsParser_vf684_22", 0xbd1980, 14, 0x33a73eee, SHIFTED(PurgeNamespace)},
    {"cTSEffectsParser_vf684_20", 0xbd198e, 14, 0x2c28fc06, SHIFTED(PushNamespace)},
    {"cTSEffectsParser_vf676_0", 0xbd199c, 32, 0xdd8b14dc, VTORDISP_OWN(QueryInterface)},
    {"cTSEffectsParser_vf684_4", 0xbd1a10, 14, 0x5ade202b, SHIFTED(RegisterCommand)},
    {"cTSEffectsParser_vf684_7", 0xbd1a1e, 14, 0xf7a57b95, SHIFTED(RegisterMetaCommand)},
    {"cTSEffectsParser_vf676_2", 0xbd1a2c, 14, 0xafc1f5e6, SHIFTED(Release)},
    {"cTSEffectsParser_vf684_8", 0xbd1a3a, 14, 0xd087a65a, SHIFTED(RestartViaReset)},
    {"cTSEffectsParser_vf696_1", 0xbd1d3c, 8, 0x1def52b7, VTORDISP_OWN(SetPointer1b0)},
    {"cTSEffectsParser_vf696_4", 0xbd1d85, 24, 0x3610c446, VTORDISP_OWN(SetFlag1d0)},
    {"cTSEffectsParser_vf696_0", 0xbd1d9d, 8, 0x80386a0e, VTORDISP_OWN(SetPointer1ac)},
    {"cTSEffectsParser_vf684_2", 0xbd1dec, 14, 0xae039fd1, SHIFTED(SetVersionRange)},
    {"cTSEffectsParser_vf696_2", 0xbd1dfa, 8, 0xb7e69a3c, VTORDISP_OWN(SetPointer1b4)},
    {"cTSEffectsParser_vf696_3", 0xbd1e45, 8, 0x458c42e1, VTORDISP_OWN(SetPointer1bc)},
    {"cTSEffectsParser_vf684_26", 0xbd1e85, 14, 0x65b23c7d, SHIFTED(SetSource)},
    {"cTSEffectsParser_vf684_3", 0xbd1e93, 14, 0x6e4b31ff, SHIFTED(SetContextState)},
    {"cTSEffectsParser_vf684_24", 0xbd1ea1, 14, 0x2ea6e9f2, SHIFTED(SetOutput)},
    {"cTSEffectsParser_vf684_25", 0xbd1eaf, 14, 0x0786215b, SHIFTED(SetHandler)},
    {"cTSEffectsParser_vf684_19", 0xbd1ebd, 14, 0x04b9c71e, SHIFTED(RunExpandedText)},
    {"cTSEffectsParser_vf684_1", 0xbd1ecb, 14, 0xa7aaf6f1, SHIFTED(Shutdown)},
    {"cTSEffectsParser_vf684_50", 0xbd1ed9, 14, 0xb69c1354, SHIFTED(GetSource)},
    {"cTSEffectsParser_vf684_16", 0xbd1ee7, 14, 0x1c95dbdf, SHIFTED(PushOpenBlock)},
    {"cTSEffectsParser_vf684_17", 0xbd1ef5, 14, 0xf2f38ba1, SHIFTED(PushMetaBlock)},
    {"cTSEffectsParser_vf696_6", 0xbd1f03, 8, 0x60e71d3d, VTORDISP_OWN(Apply)},
    {"cTSEffectsParser_vf684_29", 0xbd1f46, 14, 0x44ca3aa1, SHIFTED(ExpandVariables)},
    {"cTSEffectsParser_vf684_6", 0xbd1f54, 14, 0xbc9c1bec, SHIFTED(UnregisterCommandObject)},
    {"cTSEffectsParser_vf684_5", 0xbd1f62, 14, 0x2a0e82d9, SHIFTED(UnregisterCommand)},
    {"cTSEffectsParser_vf684_27", 0xbd1f70, 14, 0x24b899de, SHIFTED(FindVariable)},
    {"cTSEffectsParser_vf684_45", 0xbd1f7e, 14, 0x7e5c9ab9, SHIFTED(GetVersion)},
    {"cTSEffectsParser_Construct", 0xbcce40, 32, 0xb30931c5, OWN(Construct)},
    {"cTSEffectsParser_Destruct", 0xbcd2e0, 32, 0x6e321514, OWN(Destruct)},
    {"cTSEffectsParser_vf0", 0xbcd7e0, 32, 0xc24761f4, OWN(DeletingDestructor)},
    {"cTSEffectsParser_body_bcf820", 0xbcf820, 32, 0x0e4f8309, OWN(Initialise)},
    {"cTSEffectsParser_body_bd1db0", 0xbd1db0, 32, 0x8e00f9e1, OWN(SetPointer1ac)},
    {"cTSEffectsParser_body_bd1d50", 0xbd1d50, 32, 0x8471e9f7, OWN(SetPointer1b0)},
    {"cTSEffectsParser_body_bd1e10", 0xbd1e10, 32, 0x9688ed4c, OWN(SetPointer1b4)},
    {"cTSEffectsParser_body_bd1e50", 0xbd1e50, 32, 0xcc63381a, OWN(SetPointer1bc)},
    {"cTSEffectsParser_body_bcdc70", 0xbcdc70, 32, 0x9edbd1eb, OWN(Lookup)},
    {"cTSEffectsParser_body_bd1f10", 0xbd1f10, 32, 0xc8c09bd0, OWN(Apply)},
    {"cTSEffectsParser_body_bcdb10", 0xbcdb10, 32, 0x59ecae5c, OWN(Reset)},
};
REGISTER_HOOKS(kHooks);

} // namespace
