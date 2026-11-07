#include "hook/hooks.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "sg/cLODLevels.h"

// nGZSceneGraph::cLODLevelsTable (hook-shaped, lane QS; src/sg/cLODLevels.h): its faces' thunks, its own bodies, its
// constructor and factory, and its LOD command's two slots. The faces' 11-byte thunks (`sub ecx, [ecx-4]; sub ecx, 0xc;
// jmp body`) forward to cBaseParser's adapters at cBaseParser's own face; the 8-byte ones (`sub ecx, [ecx-4]; jmp body`)
// to the table's bodies. cLODLevelsStore is standalone-shaped: only its factory is hooked (db/status/cLODLevels.jsonl).
namespace {

#define SHIFTED(adapter)     reinterpret_cast<void *>(&nGZSceneGraph::TableShiftedThunk<&nGZCommandParser::cBaseParserAdapters::adapter>::Call)
#define SHIFTED_OWN(adapter)     reinterpret_cast<void *>(&nGZSceneGraph::TableShiftedThunk<&nGZSceneGraph::cLODLevelsTableAdapters::adapter>::Call)
#define VTORDISP_OWN(adapter)     reinterpret_cast<void *>(&nGZCommandParser::VtordispThunk<&nGZSceneGraph::cLODLevelsTableAdapters::adapter>::Call)
#define OWN(adapter) reinterpret_cast<void *>(&nGZSceneGraph::cLODLevelsTableAdapters::adapter)

const HookSpec kHooks[] = {
    {"nGZSceneGraph::cLODLevelsTable_vf0", 0xab6c50, 32, 0x15b3c1f6, OWN(DeletingDestructor)},
    {"nGZSceneGraph::cLODLevelsTable_vf444_0", 0xab7448, 11, 0x2b4f852e, SHIFTED(QueryInterface)},
    {"nGZSceneGraph::cLODLevelsTable_vf444_1", 0xab6cd2, 11, 0xf50ad73b, SHIFTED(AddRef)},
    {"nGZSceneGraph::cLODLevelsTable_vf444_2", 0xab7765, 11, 0xd9255df3, SHIFTED(Release)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_0", 0xab7203, 8, 0x60e71d3d, VTORDISP_OWN(Initialise)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_1", 0xab794c, 8, 0x1def52b7, VTORDISP_OWN(Shutdown)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_2", 0xab77ce, 11, 0x6a7e4f80, SHIFTED(SetVersionRange)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_3", 0xab77e4, 11, 0x266c0e99, SHIFTED(SetContextState)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_4", 0xab774f, 11, 0xc3e5899e, SHIFTED(RegisterCommand)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_5", 0xab79b1, 11, 0x64ed431e, SHIFTED(UnregisterCommand)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_6", 0xab79a6, 11, 0xc463402e, SHIFTED(UnregisterCommandObject)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_7", 0xab775a, 11, 0x0b6bcc77, SHIFTED(RegisterMetaCommand)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_8", 0xab7770, 11, 0x9af0fd9e, SHIFTED(RestartViaReset)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_9", 0xab6cfd, 8, 0x80386a0e, VTORDISP_OWN(Reset)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_10", 0xab7331, 11, 0xe330ddde, SHIFTED(ResetBlocks)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_11", 0xab6d3e, 11, 0xbf90ff82, SHIFTED_OWN(NoOp)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_12", 0xab7398, 11, 0x082dc862, SHIFTED(RunFile)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_13", 0xab73c4, 11, 0xf9705495, SHIFTED(RunFiles)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_14", 0xab73b9, 11, 0xb3157b69, SHIFTED(RunText)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_15", 0xab738d, 11, 0xfcb0237c, SHIFTED(RunLine)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_16", 0xab7985, 11, 0xa33d5c14, SHIFTED(PushOpenBlock)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_17", 0xab7990, 11, 0xffdc0cba, SHIFTED(PushMetaBlock)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_18", 0xab6e76, 11, 0x91504249, SHIFTED(PopMetaBlock)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_19", 0xab7805, 11, 0x9f1203cb, SHIFTED(RunExpandedText)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_20", 0xab743d, 11, 0x6f938d9f, SHIFTED(PushNamespace)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_21", 0xab7427, 11, 0x8271098f, SHIFTED(PopNamespace)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_22", 0xab7432, 11, 0x1962cf97, SHIFTED(PurgeNamespace)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_23", 0xab6f2f, 11, 0x474f0790, SHIFTED(GetFunctions)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_24", 0xab77ef, 11, 0xeba261bf, SHIFTED(SetOutput)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_25", 0xab77fa, 11, 0xd307dcf7, SHIFTED(SetHandler)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_26", 0xab77d9, 11, 0x86593f82, SHIFTED(SetSource)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_27", 0xab79bc, 11, 0xae25b8ae, SHIFTED(FindVariable)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_28", 0xab6d49, 11, 0x64227696, SHIFTED(GetNamespacePath)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_29", 0xab799b, 11, 0x7f458bb1, SHIFTED(ExpandVariables)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_30", 0xab7382, 11, 0x6395abfd, SHIFTED(EvalBool)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_31", 0xab73cf, 11, 0xd636b607, SHIFTED(EvalFloat)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_32", 0xab73da, 11, 0x03cabd74, SHIFTED(EvalInt)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_34", 0xab7406, 11, 0x088882dd, SHIFTED(EvalVector2)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_35", 0xab7411, 11, 0x630b8e4f, SHIFTED(EvalVector3)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_36", 0xab741c, 11, 0x8c131808, SHIFTED(EvalVector4)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_37", 0xab73e5, 11, 0x698bdbd1, SHIFTED(EvalColorRGBA)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_38", 0xab73f0, 11, 0x11788cbf, SHIFTED(EvalColorARGB)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_39", 0xab73fb, 11, 0x8216934b, SHIFTED(EvalColorRGB)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_40", 0xab7326, 11, 0xb395264f, SHIFTED(CommandCount)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_41", 0xab6d28, 11, 0xf94e7c04, SHIFTED(CommandAt)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_42", 0xab72a0, 11, 0x0586bc9c, SHIFTED(IsInitialised)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_43", 0xab7295, 11, 0x32584cae, SHIFTED(IsCaseSensitive)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_44", 0xab7314, 11, 0xc64d58f9, SHIFTED(GetText)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_45", 0xab79c7, 11, 0x6e52cf97, SHIFTED_OWN(GetVersion)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_46", 0xab73ae, 11, 0xc4265356, SHIFTED(GetFileName)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_47", 0xab73a3, 11, 0xf3f8a364, SHIFTED(GetLine)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_48", 0xab6d54, 11, 0x4037ef73, SHIFTED(HasDefinition)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_49", 0xab6d33, 11, 0x9a0b9b51, SHIFTED(HasCommand)},
    {"nGZSceneGraph::cLODLevelsTable_vf452_50", 0xab797a, 11, 0x841723b4, SHIFTED(GetSource)},
    {"nGZSceneGraph::cLODLevelsTable_Initialise", 0xab7210, 32, 0x28fb9929, OWN(Initialise)},
    {"nGZSceneGraph::cLODLevelsTable_Shutdown", 0xab7960, 26, 0xef0e26b7, OWN(Shutdown)},
    {"nGZSceneGraph::cLODLevelsTable_Reset", 0xab6d10, 24, 0x6722cf27, OWN(Reset)},
    {"nGZSceneGraph::cLODLevelsTable_Construct", 0xab6b00, 32, 0x6858d39f, OWN(Construct)},
    {"nGZSceneGraph::cLODLevelsTable_Create", 0xab6fa0, 32, 0xd8cefd95,
     reinterpret_cast<void *>(&nGZSceneGraph::cLODLevelsTable::Create)},
    {"nGZSceneGraph::cLODCommand_Execute", 0xab6e90, 32, 0xf8cec0c2, OWN(Execute)},
    {"nGZSceneGraph::cLODCommand_GetName", 0xab7320, 6, 0x393ec8d3, OWN(GetName)},
    // The COM director's factory for class 0x2c674b2c: every LOD store is ours from here.
    {"nGZSceneGraph_cLODLevelsStore_Create", 0xab6f40, 32, 0x4367d05d,
     reinterpret_cast<void *>(&nGZSceneGraph::cLODLevelsStore::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
