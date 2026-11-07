#include "hook/hooks.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "sg/cLODLevels.h"
#include "sg/cMaterialParser.h"

// nGZSceneGraph::cMaterialParser (hook-shaped, lane RH; src/sg/cMaterialParser.h): its faces' thunks, its own bodies and its
// constructor and destructor. The faces' 14-byte thunks (`sub ecx, [ecx-4]; sub ecx, 0x138; jmp body`) forward to
// cBaseParser's adapters at cBaseParser's own face; the 8-byte ones (`sub ecx, [ecx-4]; jmp body`) to the parser's bodies.
// The bodies are hooked too (lanes RI, RL), so a call that reaches one past its thunk lands on ours.
namespace {

#define SHIFTED(adapter)     reinterpret_cast<void *>(&nGZSceneGraph::ParserShiftedThunk<&nGZCommandParser::cBaseParserAdapters::adapter>::Call)
#define SHIFTED_OWN(adapter)     reinterpret_cast<void *>(&nGZSceneGraph::ParserShiftedThunk<&nGZSceneGraph::cLODLevelsTableAdapters::adapter>::Call)
#define VTORDISP_OWN(adapter)     reinterpret_cast<void *>(&nGZCommandParser::VtordispThunk<&nGZSceneGraph::cMaterialParserAdapters::adapter>::Call)
#define OWN(adapter) reinterpret_cast<void *>(&nGZSceneGraph::cMaterialParserAdapters::adapter)

const HookSpec kHooks[] = {
    {"nGZSceneGraph::cMaterialParser_Construct", 0xacec90, 32, 0x11855479, OWN(Construct)},
    {"nGZSceneGraph::cMaterialParser_Destruct", 0xacf040, 32, 0x11b591c2, OWN(Destruct)},
    {"nGZSceneGraph::cMaterialParser_vf0", 0xacfd90, 32, 0x5de8e1c5, OWN(DeletingDestructor)},
    {"nGZSceneGraph::cMaterialParser_vf744_0", 0xadc7e9, 32, 0x1f891e8e, VTORDISP_OWN(QueryInterface)},
    {"nGZSceneGraph::cMaterialParser_vf752_0", 0xad8b8b, 8, 0xa55335d2, VTORDISP_OWN(Initialise)},
    {"nGZSceneGraph::cMaterialParser_vf752_9", 0xad02ec, 19, 0x08ec124c, SHIFTED(Reset)},
    {"nGZSceneGraph::cMaterialParser_vf752_10", 0xada9c8, 8, 0x5739ed0f, VTORDISP_OWN(ResetBlocks)},
    {"nGZSceneGraph::cMaterialParser_vf764_0", 0xadcb84, 8, 0xd85b7a58, VTORDISP_OWN(SetPair)},
    {"nGZSceneGraph::cMaterialParser_vf764_1", 0xadc919, 8, 0x0f5afd59, VTORDISP_OWN(RemovePair)},
    {"nGZSceneGraph::cMaterialParser_vf764_2", 0xadca5c, 8, 0x1def52b7, VTORDISP_OWN(SetAlias)},
    {"nGZSceneGraph::cMaterialParser_vf764_3", 0xadc88a, 8, 0xb7e69a3c, VTORDISP_OWN(EraseCell)},
    {"nGZSceneGraph::cMaterialParser_vf764_5", 0xad13b4, 8, 0xd85b7a58, VTORDISP_OWN(ResetState)},
    {"nGZSceneGraph::cMaterialParser_vf764_8", 0xad8b6c, 19, 0x08ec124c, VTORDISP_OWN(IsStateOne)},
    {"nGZSceneGraph::cMaterialParser_Initialise", 0xad8ba0, 32, 0x9a502d0e, OWN(Initialise)},
    {"nGZSceneGraph::cMaterialParser_ResetBlocks", 0xada9d0, 32, 0xd4736f7c, OWN(ResetBlocks)},
    {"nGZSceneGraph::cMaterialParser_ResetState", 0xad13c0, 32, 0xcfc039ee, OWN(ResetState)},
    {"nGZSceneGraph::cMaterialParser_EraseCell", 0xadc8a0, 32, 0xca627247, OWN(EraseCell)},
    {"nGZSceneGraph::cMaterialParser_SetPair", 0xadcb90, 32, 0x18f7f1dd, OWN(SetPair)},
    {"nGZSceneGraph::cMaterialParser_RemovePair", 0xadc930, 32, 0xc7b3c541, OWN(RemovePair)},
    {"nGZSceneGraph::cMaterialParser_SetAlias", 0xadca70, 32, 0xf613a32b, OWN(SetAlias)},
    {"nGZSceneGraph::cMaterialParser_vf764_4", 0xacfdee, 8, 0x38840d6b, VTORDISP_OWN(ParseDefinition)},
    {"nGZSceneGraph::cMaterialParser_vf764_6", 0xad03d5, 8, 0x458c42e1, VTORDISP_OWN(BuildShader)},
    {"nGZSceneGraph::cMaterialParser_vf764_7", 0xad0d79, 8, 0x0f5afd59, VTORDISP_OWN(BuildErrorShader)},
    {"nGZSceneGraph::cMaterialParser_ParseDefinition", 0xacfe00, 32, 0x0b0b615c, OWN(ParseDefinition)},
    {"nGZSceneGraph::cMaterialParser_BuildShader", 0xad03e0, 32, 0x3751800a, OWN(BuildShader)},
    {"nGZSceneGraph::cMaterialParser_BuildErrorShader", 0xad0d90, 32, 0xad9e49fa, OWN(BuildErrorShader)},
    {"nGZSceneGraph::cMaterialParser_IsStateOne", 0xad8b80, 11, 0x7411d8be, OWN(IsStateOne)},
    {"nGZSceneGraph::cMaterialParser_QueryInterface", 0xadc800, 32, 0xdd21d763, OWN(QueryInterface)},
    {"nGZSceneGraph::cMaterialParser_vf744_1", 0xacfde0, 14, 0x56c33925, SHIFTED(AddRef)},
    {"nGZSceneGraph::cMaterialParser_vf744_2", 0xadc87c, 14, 0xb722dccb, SHIFTED(Release)},
    {"nGZSceneGraph::cMaterialParser_vf752_1", 0xadcdf3, 14, 0x7afdf733, SHIFTED(Shutdown)},
    {"nGZSceneGraph::cMaterialParser_vf752_2", 0xadcd9f, 14, 0xcd9735fc, SHIFTED(SetVersionRange)},
    {"nGZSceneGraph::cMaterialParser_vf752_3", 0xadcdbb, 14, 0x47bab42e, SHIFTED(SetContextState)},
    {"nGZSceneGraph::cMaterialParser_vf752_4", 0xadc860, 14, 0x26b6d168, SHIFTED(RegisterCommand)},
    {"nGZSceneGraph::cMaterialParser_vf752_5", 0xadce47, 14, 0xaf61f8a1, SHIFTED(UnregisterCommand)},
    {"nGZSceneGraph::cMaterialParser_vf752_6", 0xadce39, 14, 0x830e268d, SHIFTED(UnregisterCommandObject)},
    {"nGZSceneGraph::cMaterialParser_vf752_7", 0xadc86e, 14, 0x4cf029e8, SHIFTED(RegisterMetaCommand)},
    {"nGZSceneGraph::cMaterialParser_vf752_8", 0xadca4e, 14, 0xf53db4fe, SHIFTED(RestartViaReset)},
    {"nGZSceneGraph::cMaterialParser_vf752_11", 0xad03c7, 14, 0xf0c265b2, SHIFTED_OWN(NoOp)},
    {"nGZSceneGraph::cMaterialParser_vf752_12", 0xadc5a6, 14, 0x29bc28c3, SHIFTED(RunFile)},
    {"nGZSceneGraph::cMaterialParser_vf752_13", 0xadc5de, 14, 0xbc4d5694, SHIFTED(RunFiles)},
    {"nGZSceneGraph::cMaterialParser_vf752_14", 0xadc5d0, 14, 0x7639012a, SHIFTED(RunText)},
    {"nGZSceneGraph::cMaterialParser_vf752_15", 0xadc598, 14, 0xa9aacea4, SHIFTED(RunLine)},
    {"nGZSceneGraph::cMaterialParser_vf752_16", 0xadce0f, 14, 0x442018d4, SHIFTED(PushOpenBlock)},
    {"nGZSceneGraph::cMaterialParser_vf752_17", 0xadce1d, 14, 0xaa4648aa, SHIFTED(PushMetaBlock)},
    {"nGZSceneGraph::cMaterialParser_vf752_18", 0xad13a6, 14, 0x8921427a, SHIFTED(PopMetaBlock)},
    {"nGZSceneGraph::cMaterialParser_vf752_19", 0xadcde5, 14, 0xd9eec6dc, SHIFTED(RunExpandedText)},
    {"nGZSceneGraph::cMaterialParser_vf752_20", 0xadc7db, 14, 0x81b6b987, SHIFTED(PushNamespace)},
    {"nGZSceneGraph::cMaterialParser_vf752_21", 0xadc7bf, 14, 0x36760dbd, SHIFTED(PopNamespace)},
    {"nGZSceneGraph::cMaterialParser_vf752_22", 0xadc7cd, 14, 0xfc025a03, SHIFTED(PurgeNamespace)},
    {"nGZSceneGraph::cMaterialParser_vf752_23", 0xad8b5e, 14, 0x4167ac3e, SHIFTED(GetFunctions)},
    {"nGZSceneGraph::cMaterialParser_vf752_24", 0xadcdc9, 14, 0xf77c9482, SHIFTED(SetOutput)},
    {"nGZSceneGraph::cMaterialParser_vf752_25", 0xadcdd7, 14, 0x97ba399c, SHIFTED(SetHandler)},
    {"nGZSceneGraph::cMaterialParser_vf752_26", 0xadcdad, 14, 0x3e832b18, SHIFTED(SetSource)},
    {"nGZSceneGraph::cMaterialParser_vf752_27", 0xadce55, 14, 0x29bcdc2d, SHIFTED(FindVariable)},
    {"nGZSceneGraph::cMaterialParser_vf752_28", 0xad12c0, 14, 0x1b3ea7fd, SHIFTED(GetNamespacePath)},
    {"nGZSceneGraph::cMaterialParser_vf752_29", 0xadce2b, 14, 0x3503c4ca, SHIFTED(ExpandVariables)},
    {"nGZSceneGraph::cMaterialParser_vf752_30", 0xadc58a, 14, 0x3885f12c, SHIFTED(EvalBool)},
    {"nGZSceneGraph::cMaterialParser_vf752_31", 0xadc5ec, 14, 0xa4d54434, SHIFTED(EvalFloat)},
    {"nGZSceneGraph::cMaterialParser_vf752_32", 0xadc5fa, 14, 0x639ce0a9, SHIFTED(EvalInt)},
    {"nGZSceneGraph::cMaterialParser_vf752_34", 0xadc795, 14, 0xf470bfa5, SHIFTED(EvalVector2)},
    {"nGZSceneGraph::cMaterialParser_vf752_35", 0xadc7a3, 14, 0xb320c115, SHIFTED(EvalVector3)},
    {"nGZSceneGraph::cMaterialParser_vf752_36", 0xadc7b1, 14, 0x80cfe19b, SHIFTED(EvalVector4)},
    {"nGZSceneGraph::cMaterialParser_vf752_37", 0xadc608, 14, 0xf2b3df21, SHIFTED(EvalColorRGBA)},
    {"nGZSceneGraph::cMaterialParser_vf752_38", 0xadc616, 14, 0x9ab399f8, SHIFTED(EvalColorARGB)},
    {"nGZSceneGraph::cMaterialParser_vf752_39", 0xadc787, 14, 0x56bf9ce9, SHIFTED(EvalColorRGB)},
    {"nGZSceneGraph::cMaterialParser_vf752_40", 0xad9a46, 14, 0xfcf4d93f, SHIFTED(CommandCount)},
    {"nGZSceneGraph::cMaterialParser_vf752_41", 0xad03ab, 14, 0x840a6ba2, SHIFTED(CommandAt)},
    {"nGZSceneGraph::cMaterialParser_vf752_42", 0xad93dc, 14, 0x90cded35, SHIFTED(IsInitialised)},
    {"nGZSceneGraph::cMaterialParser_vf752_43", 0xad93ce, 14, 0x3ac425be, SHIFTED(IsCaseSensitive)},
    {"nGZSceneGraph::cMaterialParser_vf752_44", 0xad93ea, 14, 0x70129a06, SHIFTED(GetText)},
    {"nGZSceneGraph::cMaterialParser_vf752_45", 0xadce63, 14, 0x60cef92d, SHIFTED_OWN(GetVersion)},
    {"nGZSceneGraph::cMaterialParser_vf752_46", 0xadc5c2, 14, 0xe7163ea2, SHIFTED(GetFileName)},
    {"nGZSceneGraph::cMaterialParser_vf752_47", 0xadc5b4, 14, 0x57d01e0e, SHIFTED(GetLine)},
    {"nGZSceneGraph::cMaterialParser_vf752_48", 0xad12ce, 14, 0x6e4cafbe, SHIFTED(HasDefinition)},
    {"nGZSceneGraph::cMaterialParser_vf752_49", 0xad03b9, 14, 0x34cc4b0e, SHIFTED(HasCommand)},
    {"nGZSceneGraph::cMaterialParser_vf752_50", 0xadce01, 14, 0xee29d05f, SHIFTED(GetSource)},
};
REGISTER_HOOKS(kHooks);

} // namespace
