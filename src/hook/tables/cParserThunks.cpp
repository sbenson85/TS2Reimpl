#include "hook/hooks.h"
#include "gz/cParserThunks.h"

// cBaseParser's vtordisp thunks (lane IM, src/gz/cParserThunks.h): `sub ecx, [ecx-4]; jmp body`, each hooked to the
// forwarder for its body's adapter. The faces' slots 11 and 45 hold thunks folded with other classes (not here); slot 33
// shares slot 32's.
namespace {

#define THUNK(adapter) reinterpret_cast<void *>(&nGZCommandParser::VtordispThunk<&nGZCommandParser::cBaseParserAdapters::adapter>::Call)

const HookSpec kHooks[] = {
    {"cBaseParser_vf432_0", 0xb05933, 8, 0x60e71d3d, THUNK(QueryInterface)},
    {"cBaseParser_vf432_1", 0xaff0f3, 8, 0x60e71d3d, THUNK(AddRef)},
    {"cBaseParser_vf432_2", 0xb05c43, 8, 0x60e71d3d, THUNK(Release)},
    {"cBaseParser_vf440_0", 0xb0286f, 8, 0x2a31a285, THUNK(Initialise)},
    {"cBaseParser_vf440_1", 0xb0674e, 8, 0x38840d6b, THUNK(Shutdown)},
    {"cBaseParser_vf440_2", 0xb06107, 8, 0xef858a6a, THUNK(SetVersionRange)},
    {"cBaseParser_vf440_3", 0xb0621b, 8, 0xa55335d2, THUNK(SetContextState)},
    {"cBaseParser_vf440_4", 0xb059d5, 8, 0x458c42e1, THUNK(RegisterCommand)},
    {"cBaseParser_vf440_5", 0xb0729c, 8, 0x86fb0a8a, THUNK(UnregisterCommand)},
    {"cBaseParser_vf440_6", 0xb07294, 8, 0x88422dc7, THUNK(UnregisterCommandObject)},
    {"cBaseParser_vf440_7", 0xb05b40, 8, 0x928dc5e0, THUNK(RegisterMetaCommand)},
    {"cBaseParser_vf440_8", 0xb05e98, 8, 0x5739ed0f, THUNK(RestartViaReset)},
    {"cBaseParser_vf440_9", 0xaff10e, 8, 0x38840d6b, THUNK(Reset)},
    {"cBaseParser_vf440_10", 0xb03e44, 8, 0xd85b7a58, THUNK(ResetBlocks)},
    {"cBaseParser_vf440_12", 0xb04485, 8, 0x458c42e1, THUNK(RunFile)},
    {"cBaseParser_vf440_13", 0xb04934, 8, 0xd85b7a58, THUNK(RunFiles)},
    {"cBaseParser_vf440_14", 0xb046a7, 8, 0xef858a6a, THUNK(RunText)},
    {"cBaseParser_vf440_15", 0xb03f92, 8, 0x7252b2d3, THUNK(RunLine)},
    {"cBaseParser_vf440_16", 0xb068a7, 8, 0xef858a6a, THUNK(PushOpenBlock)},
    {"cBaseParser_vf440_17", 0xb068dc, 8, 0x1def52b7, THUNK(PushMetaBlock)},
    {"cBaseParser_vf440_18", 0xaff878, 8, 0x5739ed0f, THUNK(PopMetaBlock)},
    {"cBaseParser_vf440_19", 0xb06345, 8, 0x458c42e1, THUNK(RunExpandedText)},
    {"cBaseParser_vf440_20", 0xb05764, 8, 0xd85b7a58, THUNK(PushNamespace)},
    {"cBaseParser_vf440_21", 0xb0559e, 8, 0x38840d6b, THUNK(PopNamespace)},
    {"cBaseParser_vf440_22", 0xb05631, 8, 0xcaeed5b6, THUNK(PurgeNamespace)},
    {"cBaseParser_vf440_23", 0xb02438, 8, 0x5739ed0f, THUNK(GetFunctions)},
    {"cBaseParser_vf440_24", 0xb0623d, 8, 0x80386a0e, THUNK(SetOutput)},
    {"cBaseParser_vf440_25", 0xb06304, 8, 0xd85b7a58, THUNK(SetHandler)},
    {"cBaseParser_vf440_26", 0xb0612e, 8, 0x38840d6b, THUNK(SetSource)},
    {"cBaseParser_vf440_27", 0xb07516, 8, 0xfd302584, THUNK(FindVariable)},
    {"cBaseParser_vf440_28", 0xaff4c1, 8, 0xcaeed5b6, THUNK(GetNamespacePath)},
    {"cBaseParser_vf440_29", 0xb06f9d, 8, 0x80386a0e, THUNK(ExpandVariables)},
    {"cBaseParser_vf440_30", 0xb03f23, 8, 0x60e71d3d, THUNK(EvalBool)},
    {"cBaseParser_vf440_31", 0xb050ae, 8, 0x38840d6b, THUNK(EvalFloat)},
    {"cBaseParser_vf440_32", 0xb05109, 8, 0x0f5afd59, THUNK(EvalInt)},
    {"cBaseParser_vf440_34", 0xb052d0, 8, 0x928dc5e0, THUNK(EvalVector2)},
    {"cBaseParser_vf440_35", 0xb05367, 8, 0xef858a6a, THUNK(EvalVector3)},
    {"cBaseParser_vf440_36", 0xb05402, 8, 0x7252b2d3, THUNK(EvalVector4)},
    {"cBaseParser_vf440_37", 0xb05182, 8, 0x7252b2d3, THUNK(EvalColorRGBA)},
    {"cBaseParser_vf440_38", 0xb051f2, 8, 0x7252b2d3, THUNK(EvalColorARGB)},
    {"cBaseParser_vf440_39", 0xb05256, 8, 0xfd302584, THUNK(EvalColorRGB)},
    {"cBaseParser_vf440_40", 0xb03e16, 8, 0xfd302584, THUNK(CommandCount)},
    {"cBaseParser_vf440_41", 0xaff2d3, 8, 0x60e71d3d, THUNK(CommandAt)},
    {"cBaseParser_vf440_42", 0xb035a7, 8, 0xef858a6a, THUNK(IsInitialised)},
    {"cBaseParser_vf440_43", 0xb03593, 8, 0x60e71d3d, THUNK(IsCaseSensitive)},
    {"cBaseParser_vf440_44", 0xb035b7, 8, 0xef858a6a, THUNK(GetText)},
    {"cBaseParser_vf440_46", 0xb04677, 8, 0xef858a6a, THUNK(GetFileName)},
    {"cBaseParser_vf440_47", 0xb04661, 8, 0xcaeed5b6, THUNK(GetLine)},
    {"cBaseParser_vf440_48", 0xaff4df, 8, 0x2a31a285, THUNK(HasDefinition)},
    {"cBaseParser_vf440_49", 0xaff2ed, 8, 0x80386a0e, THUNK(HasCommand)},
    {"cBaseParser_vf440_50", 0xb06892, 8, 0x7252b2d3, THUNK(GetSource)},
};
REGISTER_HOOKS(kHooks);

#undef THUNK

} // namespace
