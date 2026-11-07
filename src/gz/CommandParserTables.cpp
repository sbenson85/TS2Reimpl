// The command parser's own vtables (lane RS): what an image-free program's commands, functions and errors store
// (CommandParserTables.h; standalone/owned_statics.cpp aliases the owned statics to these). Each table has the image's
// slot count and order (db/classes.jsonl). A slot is the function the image's entry is hooked to; the image's unhooked
// entries (bodies folded with other classes', under 5 bytes, or the toolchain's _purecall) are the members of ours that
// do the same, each tested against the image's body in tests/command_parser.cpp, or the adapters below. RVAs are
// 1.18.0.187.
#include "gz/CommandParserTables.h"
#include "gz/CommandParser.h"
#include "gz/ParserFunctions.h"
#include "game/vtable.h"
#include "rz/TopCallersA.h"

#include <cstring>
#include <iterator>
#include <new>

extern "C" int __cdecl _purecall();

// The tables are filled by dynamic initialisers (a member function's address is no constant expression): before any
// code of the program runs, as the image's tables are there before its code.
#pragma warning(disable : 4073) // initializers put in library initialization area: intended
#pragma init_seg(lib)

namespace nGZCommandParser {

uint32_t __fastcall cExpressionFunctionFace::FaceQueryInterface(void *face, void *, uint32_t iid, void **out)
{
    if (out == nullptr) return 0;
    if (iid != 1 && iid != 0xcc1d5da0) return 0;
    // The face reached from the object (a null object gives null), as a cast through the object does.
    *out = FaceGetFunction(face, nullptr) != nullptr ? face : nullptr;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(face); // AddRef
    return 1;
}

uint32_t __fastcall cExpressionFunctionFace::FaceRelease(void *face, void *)
{
    auto *count = reinterpret_cast<int32_t *>(static_cast<char *>(face) + 4);
    const int32_t left = --*count;
    if (left != 0) return static_cast<uint32_t>(left);
    if (cExpressionFunction *self = FaceGetFunction(face, nullptr))
        VirtualCall<void *(__thiscall *)(void *, uint32_t), 1>(self, 1); // the deleting destructor
    return 0;
}

cExpressionFunction *__fastcall cExpressionFunctionFace::FaceGetFunction(void *face, void *)
{
    return reinterpret_cast<cExpressionFunction *>(static_cast<char *>(face) - 4);
}

cExpressionFunction *__fastcall cExpressionFunctionFace::BaseDeletingDestructor(cExpressionFunction *self, void *, uint32_t flags)
{
    self->Destruct();
    if (flags & 1) ::operator delete(self, sizeof(cExpressionFunction));
    return self;
}

cExpressionFunction *__fastcall cExpressionFunctionFace::DeletingDestructor(cExpressionFunction *self, void *, uint32_t flags)
{
    self->Destruct();
    if (flags & 1) ::operator delete(self, sizeof(cParserFunction));
    return self;
}

const char *__fastcall cExpressionFunctionFace::ErrorMessage(const cError *self, void *)
{
    return self->mMessage.c_str();
}

} // namespace nGZCommandParser

using namespace nGZCommandParser;

namespace {

template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
template <typename T> const void *Fn(T fn) { return reinterpret_cast<const void *>(fn); }
const void *Pure() { return reinterpret_cast<const void *>(&_purecall); }

} // namespace

// cCommand 0xe7d72c
extern "C" const void *ts2_own_vt_e7d72c[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommand::SetContext), // 3 0x1d0b40
    Pure(), // 4 0x8261ba
    Pure(), // 5 0x8261ba
    Member(&cCommand::GetHelpDefault), // 6 0x12e40
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommand::DeletingDestructor), // 9 0xaf7a00
};
// cBlockCommand 0xe7d758
extern "C" const void *ts2_own_vt_e7d758[13] = {
    Member(&cBlockCommandBase::QueryInterface), // 0 0xaf7c00
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cBlockCommandBase::Release), // 2 0x313300
    Member(&cBlockCommand::StoreAndQuery), // 3 0xaf7bd0
    Pure(), // 4 0x8261ba
    Pure(), // 5 0x8261ba
    Member(&cCommand::GetHelpDefault), // 6 0x12e40
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cBlockCommandBase::EndBlock), // 9 0x140fe0
    Member(&cBlockCommandBase::AddCommand), // 10 0xaf7cd0
    Member(&cBlockCommandBase::FindCommand), // 11 0xaf7a90
    Member(&cBlockCommandBase::DeletingDestructor), // 12 0xaf79c0
};
// cMetaCommand 0xe7d790
extern "C" const void *ts2_own_vt_e7d790[12] = {
    Member(&cMetaCommand::QueryInterface), // 0 0xaf7ca0
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Member(&cMetaCommand::Release), // 2 0x153f70
    Member(&cMetaCommand::SetContext), // 3 0x1b9a50
    Pure(), // 4 0x8261ba
    Pure(), // 5 0x8261ba
    Member(&cCommand::GetHelpDefault), // 6 0x12e40
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Pure(), // 9 0x8261ba
    Pure(), // 10 0x8261ba
    Member(&cMetaCommand::DeletingDestructor), // 11 0xaf7a60
};
// cExpressionFunction 0xe7d7c4
extern "C" const void *ts2_own_vt_e7d7c4[2] = {
    Pure(), // 0 0x8261ba
    Fn(&cExpressionFunctionFace::BaseDeletingDestructor), // 1 0xaf7a30
};
// cExpressionFunction 0xe7d7d0 (the cIExpressionFunction face)
extern "C" const void *ts2_own_vt_e7d7d0[7] = {
    Fn(&cExpressionFunctionFace::FaceQueryInterface), // 0 0xaf7c60
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Fn(&cExpressionFunctionFace::FaceRelease), // 2 0xaf7d80
    Member(&cMetaCommand::SetContext), // 3 0x1b9a50
    Fn(&cExpressionFunctionFace::FaceGetFunction), // 4 0x142700
    Pure(), // 5 0x8261ba
    Member(&cCommand::GetHelpDefault), // 6 0x12e40
};
// cFunctionT<cBaseParser> 0xe7eefc (the face Initialise stores before the derived one)
extern "C" const void *ts2_own_vt_e7eefc[7] = {
    Fn(&cExpressionFunctionFace::FaceQueryInterface), // 0 0xaf7c60
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Fn(&cExpressionFunctionFace::FaceRelease), // 2 0xaf7d80
    Member(&cMetaCommandT::SetContextAdjusted), // 3 0x24c860
    Fn(&cExpressionFunctionFace::FaceGetFunction), // 4 0x142700
    Pure(), // 5 0x8261ba
    Member(&cCommand::GetHelpDefault), // 6 0x12e40
};
// cIncludeCommand 0xe7dc6c
extern "C" const void *ts2_own_vt_e7dc6c[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cIncludeCommand::Execute), // 4 0xb01810
    Member(&cIncludeCommand::GetName), // 5 0xb03ca0
    Member(&cIncludeCommand::GetHelp), // 6 0xaff700
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cSincludeCommand 0xe7dcbc
extern "C" const void *ts2_own_vt_e7dcbc[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cSincludeCommand::Execute), // 4 0xb01ff0
    Member(&cSincludeCommand::GetName), // 5 0xb03dc0
    Member(&cSincludeCommand::GetHelp), // 6 0xaff820
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cWildIncludeCommand 0xe7dd38
extern "C" const void *ts2_own_vt_e7dd38[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cWildIncludeCommand::Execute), // 4 0xb02260
    Member(&cWildIncludeCommand::GetName), // 5 0xb03e10
    Member(&cWildIncludeCommand::GetHelp), // 6 0xaff870
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cEndCommand 0xe7dde8
extern "C" const void *ts2_own_vt_e7dde8[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cEndCommand::Execute), // 4 0xb01260
    Member(&cEndCommand::GetName), // 5 0xb03c40
    Member(&cEndCommand::GetHelp), // 6 0xaff6a0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cIgnoreErrorsCommand 0xe7de38
extern "C" const void *ts2_own_vt_e7de38[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cIgnoreErrorsCommand::Execute), // 4 0xb017d0
    Member(&cIgnoreErrorsCommand::GetName), // 5 0xb03c90
    Member(&cIgnoreErrorsCommand::GetHelp), // 6 0xaff6f0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cListCommandsCommand 0xe7dea0
extern "C" const void *ts2_own_vt_e7dea0[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cListCommandsCommand::Execute), // 4 0xb01840
    Member(&cListCommandsCommand::GetName), // 5 0xb03cb0
    Member(&cListCommandsCommand::GetHelp), // 6 0xaff710
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cEvalCommand 0xe7df1c
extern "C" const void *ts2_own_vt_e7df1c[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cEvalCommand::Execute), // 4 0xb01670
    Member(&cEvalCommand::GetName), // 5 0xb03c70
    Member(&cEvalCommand::GetHelp), // 6 0xaff6d0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cAssertCommand 0xe7df78
extern "C" const void *ts2_own_vt_e7df78[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cAssertCommand::Execute), // 4 0xaffda0
    Member(&cAssertCommand::GetName), // 5 0xb03ba0
    Member(&cAssertCommand::GetHelp), // 6 0xaff600
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cSetCommand 0xe7e004
extern "C" const void *ts2_own_vt_e7e004[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cSetCommand::Execute), // 4 0xb01b00
    Member(&cSetCommand::GetName), // 5 0xb03d40
    Member(&cSetCommand::GetHelp), // 6 0xaff7a0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cSetECommand 0xe7e054
extern "C" const void *ts2_own_vt_e7e054[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cSetECommand::Execute), // 4 0xb01b30
    Member(&cSetECommand::GetName), // 5 0xb03d50
    Member(&cSetECommand::GetHelp), // 6 0xaff7b0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cSetBCommand 0xe7e0e0
extern "C" const void *ts2_own_vt_e7e0e0[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cSetBCommand::Execute), // 4 0xb019e0
    Member(&cSetBCommand::GetName), // 5 0xb03d20
    Member(&cSetBCommand::GetHelp), // 6 0xaff780
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cSetICommand 0xe7e13c
extern "C" const void *ts2_own_vt_e7e13c[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cSetICommand::Execute), // 4 0xb01cc0
    Member(&cSetICommand::GetName), // 5 0xb03d70
    Member(&cSetICommand::GetHelp), // 6 0xaff7d0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cSetFCommand 0xe7e1b0
extern "C" const void *ts2_own_vt_e7e1b0[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cSetFCommand::Execute), // 4 0xb01c40
    Member(&cSetFCommand::GetName), // 5 0xb03d60
    Member(&cSetFCommand::GetHelp), // 6 0xaff7c0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cSetCCommand 0xe7e230
extern "C" const void *ts2_own_vt_e7e230[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cSetCCommand::Execute), // 4 0xb01a30
    Member(&cSetCCommand::GetName), // 5 0xb03d30
    Member(&cSetCCommand::GetHelp), // 6 0xaff790
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cSetV2Command 0xe7e294
extern "C" const void *ts2_own_vt_e7e294[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cSetV2Command::Execute), // 4 0xb01d40
    Member(&cSetV2Command::GetName), // 5 0xb03d80
    Member(&cSetV2Command::GetHelp), // 6 0xaff7e0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cSetV3Command 0xe7e304
extern "C" const void *ts2_own_vt_e7e304[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cSetV3Command::Execute), // 4 0xb01df0
    Member(&cSetV3Command::GetName), // 5 0xb03d90
    Member(&cSetV3Command::GetHelp), // 6 0xaff7f0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cSetV4Command 0xe7e378
extern "C" const void *ts2_own_vt_e7e378[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cSetV4Command::Execute), // 4 0xb01eb0
    Member(&cSetV4Command::GetName), // 5 0xb03da0
    Member(&cSetV4Command::GetHelp), // 6 0xaff800
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cNamespaceCommand 0xe7e3e0
extern "C" const void *ts2_own_vt_e7e3e0[13] = {
    Member(&cBlockCommandBase::QueryInterface), // 0 0xaf7c00
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cBlockCommandBase::Release), // 2 0x313300
    Member(&cBlockCommandT::SetContextAdjusted), // 3 0x2c3c40
    Member(&cNamespaceCommand::Execute), // 4 0xb01910
    Member(&cNamespaceCommand::GetName), // 5 0xb03d00
    Member(&cNamespaceCommand::GetHelp), // 6 0xaff760
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cNamespaceCommand::EndBlock), // 9 0xb03e30
    Member(&cBlockCommandBase::AddCommand), // 10 0xaf7cd0
    Member(&cBlockCommandBase::FindCommand), // 11 0xaf7a90
    Member(&cBlockCommandT::DeletingDestructor), // 12 0x2c2160
};
// cPurgeCommand 0xe7e444
extern "C" const void *ts2_own_vt_e7e444[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cPurgeCommand::Execute), // 4 0xb01960
    Member(&cPurgeCommand::GetName), // 5 0xb03d10
    Member(&cPurgeCommand::GetHelp), // 6 0xaff770
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cIfCommand 0xe7e4d0
extern "C" const void *ts2_own_vt_e7e4d0[12] = {
    Member(&cMetaCommand::QueryInterface), // 0 0xaf7ca0
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Member(&cMetaCommand::Release), // 2 0x153f70
    Member(&cMetaCommandT::SetContextAdjusted), // 3 0x24c860
    Member(&cIfCommand::Execute), // 4 0xb016a0
    Member(&cIfCommand::GetName), // 5 0xb03c80
    Member(&cIfCommand::GetHelp), // 6 0xaff6e0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cMetaCommandT::RegisterInScope), // 9 0xb02350
    Member(&cIfCommand::HandleLine), // 10 0xb03a20
    Member(&cMetaCommandT::DeletingDestructor), // 11 0xacfdc0
};
// cElseIfCommand 0xe7e540
extern "C" const void *ts2_own_vt_e7e540[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cElseIfCommand::Execute), // 4 0xb01050
    Member(&cElseIfCommand::GetName), // 5 0xb03c30
    Member(&cElseIfCommand::GetHelp), // 6 0xaff690
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cElseCommand::ReturnsMinusOne), // 8 0x1b86e0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cElseCommand 0xe7e5d4
extern "C" const void *ts2_own_vt_e7e5d4[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cElseCommand::Execute), // 4 0xb00f20
    Member(&cElseCommand::GetName), // 5 0xb03c20
    Member(&cElseCommand::GetHelp), // 6 0xaff680
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cElseCommand::ReturnsMinusOne), // 8 0x1b86e0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cEndIfCommand 0xe7e658
extern "C" const void *ts2_own_vt_e7e658[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cEndIfCommand::Execute), // 4 0xb01590
    Member(&cEndIfCommand::GetName), // 5 0xb03c60
    Member(&cEndIfCommand::GetHelp), // 6 0xaff6c0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cEndIfCommand::NegatedBlockWord), // 8 0xb02860
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cDefineCommand 0xe7e6c8
extern "C" const void *ts2_own_vt_e7e6c8[12] = {
    Member(&cMetaCommand::QueryInterface), // 0 0xaf7ca0
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Member(&cMetaCommand::Release), // 2 0x153f70
    Member(&cMetaCommandT::SetContextAdjusted), // 3 0x24c860
    Member(&cDefineCommand::Execute), // 4 0xb005e0
    Member(&cDefineCommand::GetName), // 5 0xb03bf0
    Member(&cDefineCommand::GetHelp), // 6 0xaff650
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cMetaCommandT::RegisterInScope), // 9 0xb02350
    Member(&cDefineCommand::HandleLine), // 10 0xb038f0
    Member(&cMetaCommandT::DeletingDestructor), // 11 0xacfdc0
};
// cEndDefCommand 0xe7e748
extern "C" const void *ts2_own_vt_e7e748[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cEndDefCommand::Execute), // 4 0xb012c0
    Member(&cEndDefCommand::GetName), // 5 0xb03c50
    Member(&cEndDefCommand::GetHelp), // 6 0xaff6b0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cUndefineCommand 0xe7e7bc
extern "C" const void *ts2_own_vt_e7e7bc[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cUndefineCommand::Execute), // 4 0xb02060
    Member(&cUndefineCommand::GetName), // 5 0xb03de0
    Member(&cUndefineCommand::GetHelp), // 6 0xaff840
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cCreateCommand 0xe7e850
extern "C" const void *ts2_own_vt_e7e850[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cCreateCommand::Execute), // 4 0xaffe10
    Member(&cCreateCommand::GetName), // 5 0xb03bc0
    Member(&cCreateCommand::GetHelp), // 6 0xaff620
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cArrayCreateCommand 0xe7e980
extern "C" const void *ts2_own_vt_e7e980[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cArrayCreateCommand::Execute), // 4 0xaffc70
    Member(&cArrayCreateCommand::GetName), // 5 0xb03b90
    Member(&cArrayCreateCommand::GetHelp), // 6 0xaff5f0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cDumpParserStateCommand 0xe7ea90
extern "C" const void *ts2_own_vt_e7ea90[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cDumpParserStateCommand::Execute), // 4 0xb008a0
    Member(&cDumpParserStateCommand::GetName), // 5 0xb03c10
    Member(&cDumpParserStateCommand::GetHelp), // 6 0xaff670
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cDebugParserCommand 0xe7eb20
extern "C" const void *ts2_own_vt_e7eb20[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cDebugParserCommand::Execute), // 4 0xb00480
    Member(&cDebugParserCommand::GetName), // 5 0xb03bd0
    Member(&cDebugParserCommand::GetHelp), // 6 0xaff630
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cShowArgumentsCommand 0xe7eba8
extern "C" const void *ts2_own_vt_e7eba8[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cShowArgumentsCommand::Execute), // 4 0xb01f80
    Member(&cShowArgumentsCommand::GetName), // 5 0xb03db0
    Member(&cShowArgumentsCommand::GetHelp), // 6 0xaff810
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cDebuggerBreakCommand 0xe7ec18
extern "C" const void *ts2_own_vt_e7ec18[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cDebuggerBreakCommand::Execute), // 4 0xb005b0
    Member(&cDebuggerBreakCommand::GetName), // 5 0xb03be0
    Member(&cDebuggerBreakCommand::GetHelp), // 6 0xaff640
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cVersionCommand 0xe7ec70
extern "C" const void *ts2_own_vt_e7ec70[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cVersionCommand::Execute), // 4 0xb021c0
    Member(&cVersionCommand::GetName), // 5 0xb03e00
    Member(&cVersionCommand::GetHelp), // 6 0xaff860
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cTraceCommand 0xe7ea04
extern "C" const void *ts2_own_vt_e7ea04[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cTraceCommand::Execute), // 4 0xb02020
    Member(&cTraceCommand::GetName), // 5 0xb03dd0
    Member(&cTraceCommand::GetHelp), // 6 0xaff830
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cLogCommand 0xe7ea4c
extern "C" const void *ts2_own_vt_e7ea4c[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cLogCommand::Execute), // 4 0xb018d0
    Member(&cLogCommand::GetName), // 5 0xb03cc0
    Member(&cLogCommand::GetHelp), // 6 0xaff720
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cAliasCommand 0xe7ed78
extern "C" const void *ts2_own_vt_e7ed78[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextAdjusted), // 3 0x24c810
    Member(&cAliasCommand::Execute), // 4 0xaff8b0
    Member(&cAliasCommand::GetName), // 5 0xb03b70
    Member(&cAliasCommand::GetHelp), // 6 0xaff590
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cCommandT::DeletingDestructor), // 9 0x247a60
};
// cAliasStubCommand 0xe7ed4c
extern "C" const void *ts2_own_vt_e7ed4c[10] = {
    Member(&cCommand::QueryInterface), // 0 0xaf7c30
    Member(&cCommand::AddRef), // 1 0x145260
    Member(&cCommand::Release), // 2 0x1d0be0
    Member(&cCommandT::SetContextExact), // 3 0x369510
    Member(&cAliasStubCommand::Execute), // 4 0xaffb80
    Member(&cAliasStubCommand::GetName), // 5 0xb03b80
    Member(&cAliasStubCommand::GetHelp), // 6 0xaff5c0
    Member(&cCommand::ReturnsFalse), // 7 0x147480
    Member(&cCommand::ReturnsZero), // 8 0x6c6b0
    Member(&cAliasStubCommand::DeletingDestructor), // 9 0xaff050
};
// cVarExistsFunction 0xe7ef1c
extern "C" const void *ts2_own_vt_e7ef1c[2] = {
    Member(&cVarExistsFunction::Evaluate), // 0 0xafef10
    Fn(&cExpressionFunctionFace::DeletingDestructor), // 1 0x247a80
};
// cVarExistsFunction 0xe7ef28 (the cIExpressionFunction face)
extern "C" const void *ts2_own_vt_e7ef28[7] = {
    Fn(&cExpressionFunctionFace::FaceQueryInterface), // 0 0xaf7c60
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Fn(&cExpressionFunctionFace::FaceRelease), // 2 0xaf7d80
    Member(&cMetaCommandT::SetContextAdjusted), // 3 0x24c860
    Fn(&cExpressionFunctionFace::FaceGetFunction), // 4 0x142700
    Member(&cVarExistsFunction::GetName), // 5 0xb03df0
    Member(&cVarExistsFunction::GetHelp), // 6 0xaff850
};
// cDefinitionExistsFunction 0xe7ef90
extern "C" const void *ts2_own_vt_e7ef90[2] = {
    Member(&cDefinitionExistsFunction::Evaluate), // 0 0xafe970
    Fn(&cExpressionFunctionFace::DeletingDestructor), // 1 0x247a80
};
// cDefinitionExistsFunction 0xe7ef9c (the cIExpressionFunction face)
extern "C" const void *ts2_own_vt_e7ef9c[7] = {
    Fn(&cExpressionFunctionFace::FaceQueryInterface), // 0 0xaf7c60
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Fn(&cExpressionFunctionFace::FaceRelease), // 2 0xaf7d80
    Member(&cMetaCommandT::SetContextAdjusted), // 3 0x24c860
    Fn(&cExpressionFunctionFace::FaceGetFunction), // 4 0x142700
    Member(&cDefinitionExistsFunction::GetName), // 5 0xb03c00
    Member(&cDefinitionExistsFunction::GetHelp), // 6 0xaff660
};
// cCommandExistsFunction 0xe7f000
extern "C" const void *ts2_own_vt_e7f000[2] = {
    Member(&cCommandExistsFunction::Evaluate), // 0 0xafe850
    Fn(&cExpressionFunctionFace::DeletingDestructor), // 1 0x247a80
};
// cCommandExistsFunction 0xe7f00c (the cIExpressionFunction face)
extern "C" const void *ts2_own_vt_e7f00c[7] = {
    Fn(&cExpressionFunctionFace::FaceQueryInterface), // 0 0xaf7c60
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Fn(&cExpressionFunctionFace::FaceRelease), // 2 0xaf7d80
    Member(&cMetaCommandT::SetContextAdjusted), // 3 0x24c860
    Fn(&cExpressionFunctionFace::FaceGetFunction), // 4 0x142700
    Member(&cCommandExistsFunction::GetName), // 5 0xb03bb0
    Member(&cCommandExistsFunction::GetHelp), // 6 0xaff610
};
// cMinVersionFunction 0xe7f068
extern "C" const void *ts2_own_vt_e7f068[2] = {
    Member(&cMinVersionFunction::Evaluate), // 0 0xafeeb0
    Fn(&cExpressionFunctionFace::DeletingDestructor), // 1 0x247a80
};
// cMinVersionFunction 0xe7f074 (the cIExpressionFunction face)
extern "C" const void *ts2_own_vt_e7f074[7] = {
    Fn(&cExpressionFunctionFace::FaceQueryInterface), // 0 0xaf7c60
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Fn(&cExpressionFunctionFace::FaceRelease), // 2 0xaf7d80
    Member(&cMetaCommandT::SetContextAdjusted), // 3 0x24c860
    Fn(&cExpressionFunctionFace::FaceGetFunction), // 4 0x142700
    Member(&cMinVersionFunction::GetName), // 5 0xb03cf0
    Member(&cMinVersionFunction::GetHelp), // 6 0xaff750
};
// cMaxVersionFunction 0xe7f0dc
extern "C" const void *ts2_own_vt_e7f0dc[2] = {
    Member(&cMaxVersionFunction::Evaluate), // 0 0xafee50
    Fn(&cExpressionFunctionFace::DeletingDestructor), // 1 0x247a80
};
// cMaxVersionFunction 0xe7f0e8 (the cIExpressionFunction face)
extern "C" const void *ts2_own_vt_e7f0e8[7] = {
    Fn(&cExpressionFunctionFace::FaceQueryInterface), // 0 0xaf7c60
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Fn(&cExpressionFunctionFace::FaceRelease), // 2 0xaf7d80
    Member(&cMetaCommandT::SetContextAdjusted), // 3 0x24c860
    Fn(&cExpressionFunctionFace::FaceGetFunction), // 4 0x142700
    Member(&cMaxVersionFunction::GetName), // 5 0xb03ce0
    Member(&cMaxVersionFunction::GetHelp), // 6 0xaff740
};
// cMatchFunction 0xe7f150
extern "C" const void *ts2_own_vt_e7f150[2] = {
    Member(&cMatchFunction::Evaluate), // 0 0xafeb90
    Fn(&cExpressionFunctionFace::DeletingDestructor), // 1 0x247a80
};
// cMatchFunction 0xe7f15c (the cIExpressionFunction face)
extern "C" const void *ts2_own_vt_e7f15c[7] = {
    Fn(&cExpressionFunctionFace::FaceQueryInterface), // 0 0xaf7c60
    Member(&cMetaCommand::AddRef), // 1 0x148fb0
    Fn(&cExpressionFunctionFace::FaceRelease), // 2 0xaf7d80
    Member(&cMetaCommandT::SetContextAdjusted), // 3 0x24c860
    Fn(&cExpressionFunctionFace::FaceGetFunction), // 4 0x142700
    Member(&cMatchFunction::GetName), // 5 0xb03cd0
    Member(&cMatchFunction::GetHelp), // 6 0xaff730
};
// cError 0xda5060
extern "C" const void *ts2_own_vt_da5060[1] = {
    Fn(&cExpressionFunctionFace::ErrorMessage), // 0 0x1acd4f (folded, not hooked)
};

namespace nGZCommandParser {

const OwnTable kOwnTables[] = {
    {0xe7d72c, ts2_own_vt_e7d72c, 10},
    {0xe7d758, ts2_own_vt_e7d758, 13},
    {0xe7d790, ts2_own_vt_e7d790, 12},
    {0xe7d7c4, ts2_own_vt_e7d7c4, 2},
    {0xe7d7d0, ts2_own_vt_e7d7d0, 7},
    {0xe7eefc, ts2_own_vt_e7eefc, 7},
    {0xe7dc6c, ts2_own_vt_e7dc6c, 10},
    {0xe7dcbc, ts2_own_vt_e7dcbc, 10},
    {0xe7dd38, ts2_own_vt_e7dd38, 10},
    {0xe7dde8, ts2_own_vt_e7dde8, 10},
    {0xe7de38, ts2_own_vt_e7de38, 10},
    {0xe7dea0, ts2_own_vt_e7dea0, 10},
    {0xe7df1c, ts2_own_vt_e7df1c, 10},
    {0xe7df78, ts2_own_vt_e7df78, 10},
    {0xe7e004, ts2_own_vt_e7e004, 10},
    {0xe7e054, ts2_own_vt_e7e054, 10},
    {0xe7e0e0, ts2_own_vt_e7e0e0, 10},
    {0xe7e13c, ts2_own_vt_e7e13c, 10},
    {0xe7e1b0, ts2_own_vt_e7e1b0, 10},
    {0xe7e230, ts2_own_vt_e7e230, 10},
    {0xe7e294, ts2_own_vt_e7e294, 10},
    {0xe7e304, ts2_own_vt_e7e304, 10},
    {0xe7e378, ts2_own_vt_e7e378, 10},
    {0xe7e3e0, ts2_own_vt_e7e3e0, 13},
    {0xe7e444, ts2_own_vt_e7e444, 10},
    {0xe7e4d0, ts2_own_vt_e7e4d0, 12},
    {0xe7e540, ts2_own_vt_e7e540, 10},
    {0xe7e5d4, ts2_own_vt_e7e5d4, 10},
    {0xe7e658, ts2_own_vt_e7e658, 10},
    {0xe7e6c8, ts2_own_vt_e7e6c8, 12},
    {0xe7e748, ts2_own_vt_e7e748, 10},
    {0xe7e7bc, ts2_own_vt_e7e7bc, 10},
    {0xe7e850, ts2_own_vt_e7e850, 10},
    {0xe7e980, ts2_own_vt_e7e980, 10},
    {0xe7ea90, ts2_own_vt_e7ea90, 10},
    {0xe7eb20, ts2_own_vt_e7eb20, 10},
    {0xe7eba8, ts2_own_vt_e7eba8, 10},
    {0xe7ec18, ts2_own_vt_e7ec18, 10},
    {0xe7ec70, ts2_own_vt_e7ec70, 10},
    {0xe7ea04, ts2_own_vt_e7ea04, 10},
    {0xe7ea4c, ts2_own_vt_e7ea4c, 10},
    {0xe7ed78, ts2_own_vt_e7ed78, 10},
    {0xe7ed4c, ts2_own_vt_e7ed4c, 10},
    {0xe7ef1c, ts2_own_vt_e7ef1c, 2},
    {0xe7ef28, ts2_own_vt_e7ef28, 7},
    {0xe7ef90, ts2_own_vt_e7ef90, 2},
    {0xe7ef9c, ts2_own_vt_e7ef9c, 7},
    {0xe7f000, ts2_own_vt_e7f000, 2},
    {0xe7f00c, ts2_own_vt_e7f00c, 7},
    {0xe7f068, ts2_own_vt_e7f068, 2},
    {0xe7f074, ts2_own_vt_e7f074, 7},
    {0xe7f0dc, ts2_own_vt_e7f0dc, 2},
    {0xe7f0e8, ts2_own_vt_e7f0e8, 7},
    {0xe7f150, ts2_own_vt_e7f150, 2},
    {0xe7f15c, ts2_own_vt_e7f15c, 7},
    {0xda5060, ts2_own_vt_da5060, 1},
};
const size_t kOwnTableCount = std::size(kOwnTables);

} // namespace nGZCommandParser
