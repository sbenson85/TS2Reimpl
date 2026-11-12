// nTSSG::cLightRigAndCameraParserState and its commands (lane 261109d). See cLightRigAndCameraParserState.h.
#include "sg/cLightRigAndCameraParserState.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cArguments.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "sg/cLightRigManager.h"

#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace cp = nGZCommandParser;
using nGZCommandParser::cBaseParser;

namespace nTSSG {

namespace {

using SetVersionRangeFn = void(__thiscall *)(void *, int32_t, int32_t); // cIBaseParser slot 2
using RegisterCommandFn = void(__thiscall *)(void *, void *);           // cIBaseParser slot 4
using PushOpenBlockFn = void(__thiscall *)(void *, void *);             // cIBaseParser slot 16
using PushNamespaceFn = void(__thiscall *)(void *, const char *);       // cIBaseParser slot 20
using PopNamespaceFn = void(__thiscall *)(void *);                      // cIBaseParser slot 21

const char *Text(uint32_t rva) { return GameAddress<char>(rva); }

constexpr uint32_t kWrongCount = 0xdb59cc;  // "Wrong number of arguments"
constexpr uint32_t kNameSet = 0xdb59e8;     // "Lightrig name already set"
constexpr uint32_t kAddFailed = 0xdb5a04;   // "Failed to add light rig"
constexpr uint32_t kNoName = 0xdb5a54;      // "No lightrig name"

// The plain commands Initialise registers after the block, in the original's order.
constexpr uint32_t kPlainCommands[] = {cLightRigResNameCommand::kVTable, cRootTagCommand::kVTable, cAnimNameCommand::kVTable,
                                       cAutoRigCommand::kVTable};

void Assign(std::string &to, const char *text) { to.assign(text, std::strlen(text)); }

} // namespace

// ---- the vtables -----------------------------------------------------------------------------------------------------
// Slot order as the game's (tools/classinfo.py cLightRigAndCameraParserState). A slot is a thunk's forwarder: the face's
// pointer less its vtordisp (VtordispThunk, this class's own bodies) or less the vtordisp and the 0x88 that separates this
// class's faces from cBaseParser's own (LightRigShiftedThunk, cBaseParser's bodies). The cIBaseParser face's slot 33 shares
// slot 32's (EvalInt); slots 2 and 3 of the primary vtable are the folded setter/getter.

#define SLOT(fn) reinterpret_cast<const void *>(fn)
#define OWN_THUNK(adapter) SLOT(&cp::VtordispThunk<&cLightRigAndCameraParserStateAdapters::adapter>::Call)
#define BASE_THUNK(adapter) SLOT(&LightRigShiftedThunk<&cp::cBaseParserAdapters::adapter>::Call)

const void *const cLightRigAndCameraParserState::kOwnVTable[5] = {
    SLOT(&cLightRigAndCameraParserStateAdapters::DeletingDestructor),
    SLOT(&cp::cBaseParserAdapters::SetVariable),
    SLOT(&cLightRigAndCameraParserStateAdapters::SetField158),
    SLOT(&cLightRigAndCameraParserStateAdapters::GetField158),
    SLOT(&cp::cBaseParserAdapters::DropDefinitionsOfLevel),
};
const void *const cLightRigAndCameraParserState::kOwnUnknownVTable[3] = {
    BASE_THUNK(QueryInterface),
    BASE_THUNK(AddRef),
    BASE_THUNK(Release),
};
const void *const cLightRigAndCameraParserState::kOwnParserVTable[51] = {
    OWN_THUNK(Initialise),               // 0
    BASE_THUNK(Shutdown),                // 1
    BASE_THUNK(SetVersionRange),         // 2
    BASE_THUNK(SetContextState),         // 3
    BASE_THUNK(RegisterCommand),         // 4
    BASE_THUNK(UnregisterCommand),       // 5
    BASE_THUNK(UnregisterCommandObject), // 6
    BASE_THUNK(RegisterMetaCommand),     // 7
    BASE_THUNK(RestartViaReset),         // 8
    BASE_THUNK(Reset),                   // 9
    OWN_THUNK(ResetBlocks),              // 10
    BASE_THUNK(NoOp),                    // 11
    BASE_THUNK(RunFile),                 // 12
    BASE_THUNK(RunFiles),                // 13
    BASE_THUNK(RunText),                 // 14
    BASE_THUNK(RunLine),                 // 15
    BASE_THUNK(PushOpenBlock),           // 16
    BASE_THUNK(PushMetaBlock),           // 17
    BASE_THUNK(PopMetaBlock),            // 18
    BASE_THUNK(RunExpandedText),         // 19
    BASE_THUNK(PushNamespace),           // 20
    BASE_THUNK(PopNamespace),            // 21
    BASE_THUNK(PurgeNamespace),          // 22
    BASE_THUNK(GetFunctions),            // 23
    BASE_THUNK(SetOutput),               // 24
    BASE_THUNK(SetHandler),              // 25
    BASE_THUNK(SetSource),               // 26
    BASE_THUNK(FindVariable),            // 27
    BASE_THUNK(GetNamespacePath),        // 28
    BASE_THUNK(ExpandVariables),         // 29
    BASE_THUNK(EvalBool),                // 30
    BASE_THUNK(EvalFloat),               // 31
    BASE_THUNK(EvalInt),                 // 32
    BASE_THUNK(EvalInt),                 // 33
    BASE_THUNK(EvalVector2),             // 34
    BASE_THUNK(EvalVector3),             // 35
    BASE_THUNK(EvalVector4),             // 36
    BASE_THUNK(EvalColorRGBA),           // 37
    BASE_THUNK(EvalColorARGB),           // 38
    BASE_THUNK(EvalColorRGB),            // 39
    BASE_THUNK(CommandCount),            // 40
    BASE_THUNK(CommandAt),               // 41
    BASE_THUNK(IsInitialised),           // 42
    BASE_THUNK(IsCaseSensitive),         // 43
    BASE_THUNK(GetText),                 // 44
    BASE_THUNK(GetVersion),              // 45
    BASE_THUNK(GetFileName),             // 46
    BASE_THUNK(GetLine),                 // 47
    BASE_THUNK(HasDefinition),           // 48
    BASE_THUNK(HasCommand),              // 49
    BASE_THUNK(GetSource),               // 50
};
const int32_t cLightRigAndCameraParserState::kOwnVbTable[3] = {-8, 0x230, 0x238};
const int32_t cLightRigAndCameraParserState::kOwnFace2VbTable[2] = {-4, -0xc};

#undef SLOT
#undef OWN_THUNK
#undef BASE_THUNK

// ---- construction and destruction ------------------------------------------------------------------------------------

cLightRigAndCameraParserState *cLightRigAndCameraParserState::Construct(cLightRigManager *manager, uint32_t mostDerived)
{
    if (mostDerived) {
        // The virtual bases' own constructors would store their vtables here; the stores below overwrite every one of them
        // before anything reads, so they are not made.
        *reinterpret_cast<const void **>(mBase + 8) = kOwnVbTable;
        mFace2VbTable = kOwnFace2VbTable;
    }
    Parser()->Construct(0);
    *reinterpret_cast<const void *const **>(mBase) = kOwnVTable;
    *static_cast<const void *const **>(UnknownFace()) = kOwnUnknownVTable;
    *static_cast<const void *const **>(ParserFace()) = kOwnParserVTable;
    const int32_t *const vb = VbTable();
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[1] - 4) = vb[1] - 0x230;
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[2] - 4) = vb[2] - 0x238;
    mManager = manager;
    ::new (&mRigName) std::string();
    ::new (&mResourceName) std::string();
    ::new (&mRootTag) std::string();
    ::new (&mText1f8) std::string();
    ::new (&mAutoRigName) std::string();
    ::new (&mAnimations) AnimationMap();
    return this;
}

void cLightRigAndCameraParserState::Destruct()
{
    *reinterpret_cast<const void *const **>(mBase) = kOwnVTable;
    *static_cast<const void *const **>(UnknownFace()) = kOwnUnknownVTable;
    *static_cast<const void *const **>(ParserFace()) = kOwnParserVTable;
    const int32_t *const vb = VbTable();
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[1] - 4) = vb[1] - 0x230;
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[2] - 4) = vb[2] - 0x238;
    std::destroy_at(&mAnimations);
    std::destroy_at(&mAutoRigName);
    std::destroy_at(&mText1f8);
    std::destroy_at(&mRootTag);
    std::destroy_at(&mResourceName);
    std::destroy_at(&mRigName);
    Parser()->Destruct();
}

cLightRigAndCameraParserState *cLightRigAndCameraParserState::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// ---- the bodies ------------------------------------------------------------------------------------------------------

bool cLightRigAndCameraParserState::Initialise(uint8_t caseSensitive)
{
    Parser()->Initialise(caseSensitive); // the result is not looked at
    VirtualCall<SetVersionRangeFn, 2>(ParserFace(), 1, 1); // receiver: nGZCommandParser::cIBaseParser
    {
        void *const memory = ::operator new(sizeof(cLightRigCommand));
        if (memory) {
            static_cast<cp::cBlockCommandBase *>(memory)->Construct();
            *static_cast<const void **>(memory) = GameAddress<void>(cLightRigCommand::kVTable);
        }
        VirtualCall<RegisterCommandFn, 4>(ParserFace(), memory); // receiver: nGZCommandParser::cIBaseParser
    }
    for (const uint32_t vtable : kPlainCommands) {
        void *const memory = ::operator new(sizeof(cp::cCommandT));
        if (memory) {
            static_cast<cp::cCommand *>(memory)->Construct();
            *static_cast<const void **>(memory) = GameAddress<void>(vtable);
        }
        VirtualCall<RegisterCommandFn, 4>(ParserFace(), memory); // receiver: nGZCommandParser::cIBaseParser
    }
    return true;
}

void cLightRigAndCameraParserState::ResetBlocks()
{
    Parser()->ResetBlocks();
    mRigName.clear();
    mResourceName.clear();
    mRootTag.clear();
    mAnimations.clear();
    mAutoRig = false;
    mAutoRigName.clear();
}

bool cLightRigAndCameraParserState::AddRig()
{
    LightRig &rig = mManager->mRigs[mRigName];
    rig.mResourceName.value.mString = mResourceName;
    rig.mRootTag.value.mString = mRootTag;
    rig.mAnimations = mAnimations;
    rig.mLoaded = false;
    rig.mRootMissing = false;
    rig.mAutoRig = mAutoRig;
    rig.mAutoRigName.value.mString = mAutoRigName;
    return true;
}

// ---- the adapters --------------------------------------------------------------------------------------------------------

cLightRigAndCameraParserState *__fastcall cLightRigAndCameraParserStateAdapters::Construct(cLightRigAndCameraParserState *self, void *,
                                                                                           cLightRigManager *manager, uint32_t mostDerived)
{
    return self->Construct(manager, mostDerived);
}
void __fastcall cLightRigAndCameraParserStateAdapters::Destruct(cLightRigAndCameraParserState *self, void *) { self->Destruct(); }
cLightRigAndCameraParserState *__fastcall cLightRigAndCameraParserStateAdapters::DeletingDestructor(cLightRigAndCameraParserState *self,
                                                                                                    void *, uint32_t flags)
{
    return self->DeletingDestructor(flags);
}
void __fastcall cLightRigAndCameraParserStateAdapters::SetField158(cLightRigAndCameraParserState *self, void *, uint32_t value)
{
    cp::cBaseParserAdapters::SetField158(self->Parser(), nullptr, value);
}
uint32_t __fastcall cLightRigAndCameraParserStateAdapters::GetField158(cLightRigAndCameraParserState *self, void *)
{
    return cp::cBaseParserAdapters::GetField158(self->Parser(), nullptr);
}
bool __fastcall cLightRigAndCameraParserStateAdapters::Initialise(void *face, void *, uint8_t caseSensitive)
{
    return cLightRigAndCameraParserState::FromParserFace(face)->Initialise(caseSensitive);
}
void __fastcall cLightRigAndCameraParserStateAdapters::ResetBlocks(void *face, void *)
{
    cLightRigAndCameraParserState::FromParserFace(face)->ResetBlocks();
}

// ---- the commands --------------------------------------------------------------------------------------------------------

void cLightRigCommand::Execute(cp::cIArguments *args)
{
    if (args->Count() != 2) cp::ThrowFormatted(Text(kWrongCount));
    cLightRigAndCameraParserState *const state = State();
    if (state->mRigName.size() != 0) cp::ThrowFormatted(Text(kNameSet));
    VirtualCall<PushOpenBlockFn, 16>(state->ParserFace(), this); // receiver: nGZCommandParser::cIBaseParser
    VirtualCall<PushNamespaceFn, 20>(State()->ParserFace(), args->Argument(1)); // receiver: nGZCommandParser::cIBaseParser
    Assign(State()->mRigName, args->Argument(1));
    State()->mResourceName.clear();
    State()->mRootTag.clear();
    State()->mAnimations.clear();
    State()->mAutoRig = false;
    State()->mAutoRigName.clear();
}

const char *cLightRigCommand::GetName() const { return Text(0xdb59c0); }

void cLightRigCommand::EndBlock()
{
    VirtualCall<PopNamespaceFn, 21>(State()->ParserFace()); // receiver: nGZCommandParser::cIBaseParser
    const bool added = State()->mRigName.size() != 0 ? State()->AddRig() : false;
    State()->mRigName.clear();
    if (!added) cp::ThrowFormatted(Text(kAddFailed));
}

void cLightRigResNameCommand::Execute(cp::cIArguments *args)
{
    if (args->Count() != 2) cp::ThrowFormatted(Text(kWrongCount));
    if (State()->mRigName.size() == 0) cp::ThrowFormatted(Text(kNoName));
    Assign(State()->mResourceName, args->Argument(1));
}

const char *cLightRigResNameCommand::GetName() const { return Text(0xdb5a48); }

void cRootTagCommand::Execute(cp::cIArguments *args)
{
    if (args->Count() != 2) cp::ThrowFormatted(Text(kWrongCount));
    if (State()->mRigName.size() == 0) cp::ThrowFormatted(Text(kNoName));
    Assign(State()->mRootTag, args->Argument(1));
}

const char *cRootTagCommand::GetName() const { return Text(0xdb5a94); }

void cAnimNameCommand::Execute(cp::cIArguments *args)
{
    if (args->Count() != 3) cp::ThrowFormatted(Text(kWrongCount));
    if (State()->mRigName.size() == 0) cp::ThrowFormatted(Text(kNoName));
    std::string key(args->Argument(1));
    std::string &name = State()->mAnimations[std::move(key)]; // the key moved into a new node (kept, and freed after, when found)
    Assign(name, args->Argument(2));
}

const char *cAnimNameCommand::GetName() const { return Text(0xdb5afc); }

void cAutoRigCommand::Execute(cp::cIArguments *args)
{
    if (State()->mRigName.size() == 0) cp::ThrowFormatted(Text(kNoName));
    uint32_t count;
    const char *const *const argv = args->Positional(&count, 0, 1);
    State()->mAutoRig = true;
    if (count == 1) Assign(State()->mAutoRigName, argv[0]);
}

const char *cAutoRigCommand::GetName() const { return Text(0xdb5ac8); }

} // namespace nTSSG
