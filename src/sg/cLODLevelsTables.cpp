// nGZSceneGraph::cLODLevelsTable's own tables (lane VK): the vtables and virtual base tables an image-free program's LOD
// tables store (standalone/owned_statics.cpp aliases the owned statics ts2_rva_e767b8 ... to these). The slot order is the
// image's, and each slot is the forwarder its image thunk is hooked to (src/hook/tables/cLODLevels.cpp): the primary
// vtable's slots 1..4 are cBaseParser's own (the table's cBaseParser part is at +0); the faces' 11-byte thunks are
// TableShiftedThunk forwarders to cBaseParserAdapters (or the table's NoOp / GetVersion), the 8-byte ones (cIBaseParser
// slots 0, 1 and 9) VtordispThunk forwarders to the table's bodies; slot 33 shares slot 32's body (EvalInt). The virtual
// base tables hold the image's values. RVAs are 1.18.0.187.
#include "sg/cLODLevels.h"

#include "gz/CommandParser.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"

#include <cstring>

namespace nGZSceneGraph {

#define SLOT(fn) reinterpret_cast<const void *>(fn)
#define SHIFTED(adapter) SLOT(&TableShiftedThunk<&nGZCommandParser::cBaseParserAdapters::adapter>::Call)
#define SHIFTED_OWN(adapter) SLOT(&TableShiftedThunk<&cLODLevelsTableAdapters::adapter>::Call)
#define VTORDISP_OWN(adapter) SLOT(&nGZCommandParser::VtordispThunk<&cLODLevelsTableAdapters::adapter>::Call)

const void *const cLODLevelsTable::kOwnVTable[5] = {
    SLOT(&cLODLevelsTableAdapters::DeletingDestructor),                    // 0 (0xab6c50)
    SLOT(&nGZCommandParser::cBaseParserAdapters::SetVariable),            // 1
    SLOT(&nGZCommandParser::cBaseParserAdapters::SetField158),            // 2
    SLOT(&nGZCommandParser::cBaseParserAdapters::GetField158),            // 3
    SLOT(&nGZCommandParser::cBaseParserAdapters::DropDefinitionsOfLevel), // 4
};
const void *const cLODLevelsTable::kOwnUnknownVTable[3] = {
    SHIFTED(QueryInterface),
    SHIFTED(AddRef),
    SHIFTED(Release),
};
const void *const cLODLevelsTable::kOwnParserVTable[51] = {
    VTORDISP_OWN(Initialise),       // 0
    VTORDISP_OWN(Shutdown),         // 1
    SHIFTED(SetVersionRange),       // 2
    SHIFTED(SetContextState),       // 3
    SHIFTED(RegisterCommand),       // 4
    SHIFTED(UnregisterCommand),     // 5
    SHIFTED(UnregisterCommandObject),// 6
    SHIFTED(RegisterMetaCommand),   // 7
    SHIFTED(RestartViaReset),       // 8
    VTORDISP_OWN(Reset),            // 9
    SHIFTED(ResetBlocks),           // 10
    SHIFTED_OWN(NoOp),              // 11
    SHIFTED(RunFile),               // 12
    SHIFTED(RunFiles),              // 13
    SHIFTED(RunText),               // 14
    SHIFTED(RunLine),               // 15
    SHIFTED(PushOpenBlock),         // 16
    SHIFTED(PushMetaBlock),         // 17
    SHIFTED(PopMetaBlock),          // 18
    SHIFTED(RunExpandedText),       // 19
    SHIFTED(PushNamespace),         // 20
    SHIFTED(PopNamespace),          // 21
    SHIFTED(PurgeNamespace),        // 22
    SHIFTED(GetFunctions),          // 23
    SHIFTED(SetOutput),             // 24
    SHIFTED(SetHandler),            // 25
    SHIFTED(SetSource),             // 26
    SHIFTED(FindVariable),          // 27
    SHIFTED(GetNamespacePath),      // 28
    SHIFTED(ExpandVariables),       // 29
    SHIFTED(EvalBool),              // 30
    SHIFTED(EvalFloat),             // 31
    SHIFTED(EvalInt),               // 32
    SHIFTED(EvalInt),               // 33
    SHIFTED(EvalVector2),           // 34
    SHIFTED(EvalVector3),           // 35
    SHIFTED(EvalVector4),           // 36
    SHIFTED(EvalColorRGBA),         // 37
    SHIFTED(EvalColorARGB),         // 38
    SHIFTED(EvalColorRGB),          // 39
    SHIFTED(CommandCount),          // 40
    SHIFTED(CommandAt),             // 41
    SHIFTED(IsInitialised),         // 42
    SHIFTED(IsCaseSensitive),       // 43
    SHIFTED(GetText),               // 44
    SHIFTED_OWN(GetVersion),        // 45
    SHIFTED(GetFileName),           // 46
    SHIFTED(GetLine),               // 47
    SHIFTED(HasDefinition),         // 48
    SHIFTED(HasCommand),            // 49
    SHIFTED(GetSource),             // 50
};
const int32_t cLODLevelsTable::kOwnVbTable[3] = {-8, 0x1b4, 0x1bc};
const int32_t cLODLevelsTable::kOwnParserFaceVbTable[2] = {-4, -0xc};

namespace {
template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
} // namespace

// cLODCommand 0xe76914: a cCommandT (gz/CommandParserTables.cpp's command tables) with the LOD command's Execute and
// GetName.
const void *const cLODCommand::kOwnVTable[10] = {
    Member(&nGZCommandParser::cCommand::QueryInterface),      // 0 0xaf7c30
    Member(&nGZCommandParser::cCommand::AddRef),              // 1 0x145260
    Member(&nGZCommandParser::cCommand::Release),             // 2 0x1d0be0
    Member(&nGZCommandParser::cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cLODCommand::Execute),                            // 4 0xab6e90
    Member(&cLODCommand::GetName),                            // 5 0xab7320
    Member(&nGZCommandParser::cCommand::GetHelpDefault),      // 6 0x12e40
    Member(&nGZCommandParser::cCommand::ReturnsFalse),        // 7 0x147480
    Member(&nGZCommandParser::cCommand::ReturnsZero),         // 8 0x6c6b0
    Member(&nGZCommandParser::cCommandT::DeletingDestructor), // 9 0x247a60
};

#undef SLOT
#undef SHIFTED
#undef SHIFTED_OWN
#undef VTORDISP_OWN

} // namespace nGZSceneGraph
