#pragma once
// The command parser's vtables as owned statics (game/owned.h; lane RS). The hooked game stores the image's tables: the
// derived commands' and functions' tables are stored only by cBaseParser::Initialise 0xb02880 and the alias command, the
// base ones only by the base constructors and destructors, all ours, and nothing in the image compares them. An
// image-free program stores ours: standalone/owned_statics.cpp aliases each ts2_rva_<hex> below to ts2_own_vt_<hex>
// (CommandParserTables.cpp: the image's slot order and count, tools/classinfo.py). RVAs are 1.18.0.187.
#include "game/owned.h"

#include <cstddef>
#include <cstdint>

TS2_OWNED_STATIC(const OwnedVTable, e7d72c); // cCommand
TS2_OWNED_STATIC(const OwnedVTable, e7d758); // cBlockCommand
TS2_OWNED_STATIC(const OwnedVTable, e7d790); // cMetaCommand
TS2_OWNED_STATIC(const OwnedVTable, e7d7c4); // cExpressionFunction (cFunction)
TS2_OWNED_STATIC(const OwnedVTable, e7d7d0); // cExpressionFunction, its cIExpressionFunction face
TS2_OWNED_STATIC(const OwnedVTable, e7eefc); // cFunctionT<cBaseParser>, its cIExpressionFunction face
TS2_OWNED_STATIC(const OwnedVTable, e7dc6c); // cIncludeCommand
TS2_OWNED_STATIC(const OwnedVTable, e7dcbc); // cSincludeCommand
TS2_OWNED_STATIC(const OwnedVTable, e7dd38); // cWildIncludeCommand
TS2_OWNED_STATIC(const OwnedVTable, e7dde8); // cEndCommand
TS2_OWNED_STATIC(const OwnedVTable, e7de38); // cIgnoreErrorsCommand
TS2_OWNED_STATIC(const OwnedVTable, e7dea0); // cListCommandsCommand
TS2_OWNED_STATIC(const OwnedVTable, e7df1c); // cEvalCommand
TS2_OWNED_STATIC(const OwnedVTable, e7df78); // cAssertCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e004); // cSetCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e054); // cSetECommand
TS2_OWNED_STATIC(const OwnedVTable, e7e0e0); // cSetBCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e13c); // cSetICommand
TS2_OWNED_STATIC(const OwnedVTable, e7e1b0); // cSetFCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e230); // cSetCCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e294); // cSetV2Command
TS2_OWNED_STATIC(const OwnedVTable, e7e304); // cSetV3Command
TS2_OWNED_STATIC(const OwnedVTable, e7e378); // cSetV4Command
TS2_OWNED_STATIC(const OwnedVTable, e7e3e0); // cNamespaceCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e444); // cPurgeCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e4d0); // cIfCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e540); // cElseIfCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e5d4); // cElseCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e658); // cEndIfCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e6c8); // cDefineCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e748); // cEndDefCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e7bc); // cUndefineCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e850); // cCreateCommand
TS2_OWNED_STATIC(const OwnedVTable, e7e980); // cArrayCreateCommand
TS2_OWNED_STATIC(const OwnedVTable, e7ea90); // cDumpParserStateCommand
TS2_OWNED_STATIC(const OwnedVTable, e7eb20); // cDebugParserCommand
TS2_OWNED_STATIC(const OwnedVTable, e7eba8); // cShowArgumentsCommand
TS2_OWNED_STATIC(const OwnedVTable, e7ec18); // cDebuggerBreakCommand
TS2_OWNED_STATIC(const OwnedVTable, e7ec70); // cVersionCommand
TS2_OWNED_STATIC(const OwnedVTable, e7ea04); // cTraceCommand
TS2_OWNED_STATIC(const OwnedVTable, e7ea4c); // cLogCommand
TS2_OWNED_STATIC(const OwnedVTable, e7ed78); // cAliasCommand
TS2_OWNED_STATIC(const OwnedVTable, e7ed4c); // cAliasStubCommand
TS2_OWNED_STATIC(const OwnedVTable, e7ef1c); // cVarExistsFunction
TS2_OWNED_STATIC(const OwnedVTable, e7ef28); // cVarExistsFunction
TS2_OWNED_STATIC(const OwnedVTable, e7ef90); // cDefinitionExistsFunction
TS2_OWNED_STATIC(const OwnedVTable, e7ef9c); // cDefinitionExistsFunction
TS2_OWNED_STATIC(const OwnedVTable, e7f000); // cCommandExistsFunction
TS2_OWNED_STATIC(const OwnedVTable, e7f00c); // cCommandExistsFunction
TS2_OWNED_STATIC(const OwnedVTable, e7f068); // cMinVersionFunction
TS2_OWNED_STATIC(const OwnedVTable, e7f074); // cMinVersionFunction
TS2_OWNED_STATIC(const OwnedVTable, e7f0dc); // cMaxVersionFunction
TS2_OWNED_STATIC(const OwnedVTable, e7f0e8); // cMaxVersionFunction
TS2_OWNED_STATIC(const OwnedVTable, e7f150); // cMatchFunction
TS2_OWNED_STATIC(const OwnedVTable, e7f15c); // cMatchFunction
TS2_OWNED_STATIC(const OwnedVTable, da5060); // cError

namespace nGZCommandParser {

class cExpressionFunction;
class cError;

// The slots of the function tables that are no member of ours: the cIExpressionFunction face's QueryInterface, Release
// and GetFunction (this is the face, 4 bytes into the object), the deleting destructors of cExpressionFunction (0xc
// bytes) and of cFunctionT<cBaseParser> (0x10), and cError's message slot. QueryInterface, Release and cExpressionFunction's deleting
// destructor are hooked (src/hook/tables/cParserRun.cpp: every expression function in the image inherits them); the
// others are folded bodies, run by our tables only.
struct cExpressionFunctionFace {
    // 0xaf7c60 (ret 8): ids 1 and 0xcc1d5da0 give the face itself, AddRef'd through face slot 1; the result is
    // a byte (al: 1, else 0; the image leaves the upper bytes as they are, ours clears them).
    static uint32_t __fastcall FaceQueryInterface(void *face, void *, uint32_t iid, void **out);
    // 0xaf7d80: drops the count at face+4; at zero deletes the object through its slot 1 and returns 0, else the count.
    static uint32_t __fastcall FaceRelease(void *face, void *);
    // 0x142700 (shared, 4 bytes, not hooked): the object the face belongs to.
    static cExpressionFunction *__fastcall FaceGetFunction(void *face, void *);
    // 0xaf7a30 (ret 4): the cExpressionFunction destructor, then deletes 0xc bytes when flags & 1.
    static cExpressionFunction *__fastcall BaseDeletingDestructor(cExpressionFunction *self, void *, uint32_t flags);
    // 0x247a80 (ret 4, shared by the six parser functions): the same, deleting 0x10 bytes.
    static cExpressionFunction *__fastcall DeletingDestructor(cExpressionFunction *self, void *, uint32_t flags);
    // cError's slot 0, 0x1acd4f (cRZString::ToChar/Data's body, folded): the message as a C string.
    static const char *__fastcall ErrorMessage(const cError *self, void *);
};

// Our tables, for the test (tests/gz_command_parser_tables.cpp): the image's RVA, our slots and their count.
struct OwnTable {
    uint32_t rva;
    const void *const *slots;
    uint32_t count;
};
extern const OwnTable kOwnTables[];
extern const size_t kOwnTableCount;

} // namespace nGZCommandParser
