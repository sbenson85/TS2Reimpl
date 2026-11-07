#include "hook/hooks.h"
#include "gz/cBaseParser.h"
#include "gz/CommandParserTables.h"

// cBaseParser's run path (lane IH; src/gz/cParserRun.cpp). The face slots are reached through the vtordisp thunks of
// every derived parser's vtable, which jump to these RVAs.
namespace {

using nGZCommandParser::cBaseParserAdapters;
#define IH_ADAPTER(name) reinterpret_cast<void *>(&cBaseParserAdapters::name)

const HookSpec kHooks[] = {
    {"cBaseParser_RunLine", 0xb03fa0, 32, 0x62f2750f, IH_ADAPTER(RunLine)},
    {"cBaseParser_AppendUncommented", 0xb02450, 32, 0x74a239bc, IH_ADAPTER(AppendUncommented)},
    {"cBaseParser_RunText", 0xb046b0, 32, 0x6ffbd6e9, IH_ADAPTER(RunText)},
    {"cBaseParser_RunFile", 0xb04490, 32, 0x8db59b41, IH_ADAPTER(RunFile)},
    {"cBaseParser_RunFiles", 0xb04940, 32, 0xa359b482, IH_ADAPTER(RunFiles)},
    {"cBaseParser_Initialise", 0xb02880, 32, 0x14bd6ba6, IH_ADAPTER(Initialise)},
    {"cBaseParser_SetVariable", 0xb06450, 32, 0x39fb14b3, IH_ADAPTER(SetVariable)},
    {"nGZCommandParser_VariableTable_Insert", 0xafd050, 32, 0x90211f52, IH_ADAPTER(InsertVariable)},
    {"nGZCommandParser_cExpressionFunction_Construct", 0xaf7840, 23, 0x393d8d9d,
     MemberAddress(&nGZCommandParser::cExpressionFunction::Construct)},
    // The cIExpressionFunction face's QueryInterface and Release and cExpressionFunction's deleting destructor (lane RS;
    // src/gz/CommandParserTables.cpp): every expression function's table in the image inherits them, ours too.
    {"nGZCommandParser_cExpressionFunction_QueryInterface", 0xaf7c60, 32, 0x7ca7d0b6,
     reinterpret_cast<void *>(&nGZCommandParser::cExpressionFunctionFace::FaceQueryInterface)},
    {"nGZCommandParser_cExpressionFunction_Release", 0xaf7d80, 26, 0x15152e43,
     reinterpret_cast<void *>(&nGZCommandParser::cExpressionFunctionFace::FaceRelease)},
    {"nGZCommandParser_cExpressionFunction_vf1", 0xaf7a30, 32, 0x8d518f0b,
     reinterpret_cast<void *>(&nGZCommandParser::cExpressionFunctionFace::BaseDeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

#undef IH_ADAPTER

} // namespace
