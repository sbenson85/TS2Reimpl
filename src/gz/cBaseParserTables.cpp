// cBaseParser's own tables (lane RQ): the vtables and virtual base tables an image-free program's parsers store
// (standalone/owned_statics.cpp aliases the owned statics ts2_rva_e7db24 ... to these). The slot order is the image's
// (tools/classinfo.py nGZCommandParser::cBaseParser). A face slot is the forwarder its image thunk is hooked to
// (VtordispThunk: the face less its vtordisp word, then the body's adapter); slot 33 of the cIBaseParser face shares slot
// 32's body (EvalInt); slots 2 and 3 of the primary vtable and face slots 11 and 45 are bodies the image folds with other
// classes', here the class's own adapters. RVAs are 1.18.0.187.
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"

#include <cstdlib>

extern "C" int __cdecl _purecall();

namespace nGZCommandParser {

// ---- the slots with no adapter elsewhere ----

cBaseParser *__fastcall cBaseParserAdapters::DeletingDestructor(cBaseParser *self, void *, uint32_t flags)
{
    return self->DeletingDestructor(flags);
}
void __fastcall cBaseParserAdapters::SetField158(cBaseParser *self, void *, uint32_t value) { self->mField158 = value; }
uint32_t __fastcall cBaseParserAdapters::GetField158(cBaseParser *self, void *) { return self->mField158; }
void __fastcall cBaseParserAdapters::NoOp(void *, void *) {}
int32_t __fastcall cBaseParserAdapters::GetVersion(void *face, void *) { return cBaseParser::FromParserFace(face)->mVersion; }

// ---- the tables ----

#define SLOT(fn) reinterpret_cast<const void *>(fn)
#define THUNK(adapter) SLOT(&VtordispThunk<&cBaseParserAdapters::adapter>::Call)
#define PURE SLOT(&_purecall)

const void *const cBaseParser::kOwnVTable[5] = {
    SLOT(&cBaseParserAdapters::DeletingDestructor),     // 0 (0xaff070)
    SLOT(&cBaseParserAdapters::SetVariable),            // 1 (0xb06450)
    SLOT(&cBaseParserAdapters::SetField158),            // 2
    SLOT(&cBaseParserAdapters::GetField158),            // 3
    SLOT(&cBaseParserAdapters::DropDefinitionsOfLevel), // 4 (0xb05c80)
};
const void *const cBaseParser::kOwnUnknownVTable[3] = {
    THUNK(QueryInterface),
    THUNK(AddRef),
    THUNK(Release),
};
const void *const cBaseParser::kOwnParserVTable[51] = {
    THUNK(Initialise),              // 0
    THUNK(Shutdown),                // 1
    THUNK(SetVersionRange),         // 2
    THUNK(SetContextState),         // 3
    THUNK(RegisterCommand),         // 4
    THUNK(UnregisterCommand),       // 5
    THUNK(UnregisterCommandObject), // 6
    THUNK(RegisterMetaCommand),     // 7
    THUNK(RestartViaReset),         // 8
    THUNK(Reset),                   // 9
    THUNK(ResetBlocks),             // 10
    THUNK(NoOp),                    // 11
    THUNK(RunFile),                 // 12
    THUNK(RunFiles),                // 13
    THUNK(RunText),                 // 14
    THUNK(RunLine),                 // 15
    THUNK(PushOpenBlock),           // 16
    THUNK(PushMetaBlock),           // 17
    THUNK(PopMetaBlock),            // 18
    THUNK(RunExpandedText),         // 19
    THUNK(PushNamespace),           // 20
    THUNK(PopNamespace),            // 21
    THUNK(PurgeNamespace),          // 22
    THUNK(GetFunctions),            // 23
    THUNK(SetOutput),               // 24
    THUNK(SetHandler),              // 25
    THUNK(SetSource),               // 26
    THUNK(FindVariable),            // 27
    THUNK(GetNamespacePath),        // 28
    THUNK(ExpandVariables),         // 29
    THUNK(EvalBool),                // 30
    THUNK(EvalFloat),               // 31
    THUNK(EvalInt),                 // 32
    THUNK(EvalInt),                 // 33
    THUNK(EvalVector2),             // 34
    THUNK(EvalVector3),             // 35
    THUNK(EvalVector4),             // 36
    THUNK(EvalColorRGBA),           // 37
    THUNK(EvalColorARGB),           // 38
    THUNK(EvalColorRGB),            // 39
    THUNK(CommandCount),            // 40
    THUNK(CommandAt),               // 41
    THUNK(IsInitialised),           // 42
    THUNK(IsCaseSensitive),         // 43
    THUNK(GetText),                 // 44
    THUNK(GetVersion),              // 45
    THUNK(GetFileName),             // 46
    THUNK(GetLine),                 // 47
    THUNK(HasDefinition),           // 48
    THUNK(HasCommand),              // 49
    THUNK(GetSource),               // 50
};
const int32_t cBaseParser::kOwnVbTable[3] = {-8, kUnknownOffset, kParserOffset};
const int32_t cBaseParser::kOwnParserFaceVbTable[2] = {-4, -0xc};
const void *const cBaseParser::kOwnPureUnknownVTable[3] = {PURE, PURE, PURE};
const void *const cBaseParser::kOwnPureParserVTable[51] = {
    PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE,
    PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE,
    PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE, PURE,
};

#undef SLOT
#undef THUNK
#undef PURE

} // namespace nGZCommandParser
