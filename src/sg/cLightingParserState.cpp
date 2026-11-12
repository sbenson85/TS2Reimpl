// cTSLightingParserState and nTSSG::cLightingParserState (lanes 261110v, 261110y; scenegraph LG3g, LG7f). See
// sg/cLightingParserState.h.
#include "sg/cLightingParserState.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cArguments.h"
#include "gz/cParserThunks.h"
#include "sg/LightAttributeCommands.h"
#include "sg/cLightSettings.h"

#include <cstring>
#include <memory>
#include <new>

namespace cp = nGZCommandParser;

namespace {

using AddRefFn = uint32_t(__thiscall *)(void *);                        // cIGZUnknown slot 1
using SetVersionRangeFn = void(__thiscall *)(void *, int32_t, int32_t); // cIBaseParser slot 2
using RegisterCommandFn = void(__thiscall *)(void *, void *);           // cIBaseParser slot 4
using EvalFloatFn = float(__thiscall *)(void *, const char *);          // cIBaseParser slot 31

const char *Text(uint32_t rva) { return GameAddress<char>(rva); }

template <class T> void Store(uint8_t *at, const T &value) { *reinterpret_cast<T *>(at) = value; }

// A command made as the original's Initialise bodies make them: operator new, the base constructor, the game's vtable
// (the commands' own slots are hooked at their RVAs), registered through the parser face's slot 4. A constructor that
// throws frees the memory (as `new` does); a throw from the registration does not.
struct FreeUnlessKept {
    void *memory;
    ~FreeUnlessKept()
    {
        if (memory) ::operator delete(memory);
    }
};
void RegisterBlockCommand(void *face, size_t size, uint32_t vtable)
{
    void *const memory = ::operator new(size);
    if (memory) {
        FreeUnlessKept guard{memory};
        static_cast<cp::cBlockCommandBase *>(memory)->Construct();
        *static_cast<const void **>(memory) = GameAddress<void>(vtable);
        guard.memory = nullptr;
    }
    VirtualCall<RegisterCommandFn, 4>(face, memory); // receiver: nGZCommandParser::cIBaseParser
}
void RegisterPlainCommand(void *face, uint32_t vtable)
{
    void *const memory = ::operator new(sizeof(cp::cCommandT));
    if (memory) {
        FreeUnlessKept guard{memory};
        static_cast<cp::cCommand *>(memory)->Construct();
        *static_cast<const void **>(memory) = GameAddress<void>(vtable);
        guard.memory = nullptr;
    }
    VirtualCall<RegisterCommandFn, 4>(face, memory); // receiver: nGZCommandParser::cIBaseParser
}

} // namespace

// ---- the vtables -----------------------------------------------------------------------------------------------------
// Slot order as the game's (tools/classinfo.py). A face slot is a thunk's forwarder: the face less its vtordisp
// (VtordispThunk, the class's own bodies) or less the vtordisp and the distance to the body's faces
// (LightingShiftedThunk). The cIBaseParser face's slot 33 shares slot 32's (EvalInt); primary slots 2 and 3 are
// cBaseParser's folded setter and getter (the cBaseParser part is at +0).

#define SLOT(...) reinterpret_cast<const void *>(__VA_ARGS__)
#define BASE_THUNK(shift, adapter) SLOT(&LightingShiftedThunk<shift, &cp::cBaseParserAdapters::adapter>::Call)
// The cIBaseParser slots both classes take from cBaseParser (all but 0, 1, 8, 9 and 10).
#define PARSER_SLOTS_2_7(s)                                                                                            \
    BASE_THUNK(s, SetVersionRange), BASE_THUNK(s, SetContextState), BASE_THUNK(s, RegisterCommand),                   \
        BASE_THUNK(s, UnregisterCommand), BASE_THUNK(s, UnregisterCommandObject), BASE_THUNK(s, RegisterMetaCommand)
#define PARSER_SLOTS_11_50(s)                                                                                          \
    BASE_THUNK(s, NoOp), BASE_THUNK(s, RunFile), BASE_THUNK(s, RunFiles), BASE_THUNK(s, RunText),                     \
        BASE_THUNK(s, RunLine), BASE_THUNK(s, PushOpenBlock), BASE_THUNK(s, PushMetaBlock), BASE_THUNK(s, PopMetaBlock), \
        BASE_THUNK(s, RunExpandedText), BASE_THUNK(s, PushNamespace), BASE_THUNK(s, PopNamespace),                    \
        BASE_THUNK(s, PurgeNamespace), BASE_THUNK(s, GetFunctions), BASE_THUNK(s, SetOutput), BASE_THUNK(s, SetHandler), \
        BASE_THUNK(s, SetSource), BASE_THUNK(s, FindVariable), BASE_THUNK(s, GetNamespacePath),                       \
        BASE_THUNK(s, ExpandVariables), BASE_THUNK(s, EvalBool), BASE_THUNK(s, EvalFloat), BASE_THUNK(s, EvalInt),    \
        BASE_THUNK(s, EvalInt), BASE_THUNK(s, EvalVector2), BASE_THUNK(s, EvalVector3), BASE_THUNK(s, EvalVector4),   \
        BASE_THUNK(s, EvalColorRGBA), BASE_THUNK(s, EvalColorARGB), BASE_THUNK(s, EvalColorRGB),                      \
        BASE_THUNK(s, CommandCount), BASE_THUNK(s, CommandAt), BASE_THUNK(s, IsInitialised),                          \
        BASE_THUNK(s, IsCaseSensitive), BASE_THUNK(s, GetText), BASE_THUNK(s, GetVersion), BASE_THUNK(s, GetFileName), \
        BASE_THUNK(s, GetLine), BASE_THUNK(s, HasDefinition), BASE_THUNK(s, HasCommand), BASE_THUNK(s, GetSource)

using BaseAdapters = cTSLightingParserStateAdapters;
#define OWN_THUNK(adapter) SLOT(&cp::VtordispThunk<&BaseAdapters::adapter>::Call)

const void *const cTSLightingParserState::kOwnVTable[6] = {
    SLOT(&BaseAdapters::DeletingDestructor),
    SLOT(&cp::cBaseParserAdapters::SetVariable),
    SLOT(&cp::cBaseParserAdapters::SetField158),
    SLOT(&cp::cBaseParserAdapters::GetField158),
    SLOT(&cp::cBaseParserAdapters::DropDefinitionsOfLevel),
    SLOT(&BaseAdapters::FindState),
};
const void *const cTSLightingParserState::kOwnUnknownVTable[3] = {
    OWN_THUNK(QueryInterface),
    BASE_THUNK(0x258, AddRef),
    BASE_THUNK(0x258, Release),
};
const void *const cTSLightingParserState::kOwnParserVTable[51] = {
    OWN_THUNK(Initialise),               // 0
    OWN_THUNK(Shutdown),                 // 1
    PARSER_SLOTS_2_7(0x258),             // 2..7
    BASE_THUNK(0x258, RestartViaReset),  // 8
    OWN_THUNK(Reset),                    // 9
    OWN_THUNK(ResetBlocks),              // 10
    PARSER_SLOTS_11_50(0x258),           // 11..50
};
const int32_t cTSLightingParserState::kOwnVbTable[3] = {-8, 0x400, 0x408};
const int32_t cTSLightingParserState::kOwnParserFaceVbTable[2] = {-4, -0xc};

#undef OWN_THUNK

namespace nTSSG {

using DerivedAdapters = cLightingParserStateAdapters;
#define OWN_THUNK(adapter) SLOT(&cp::VtordispThunk<&DerivedAdapters::adapter>::Call)

const void *const cLightingParserState::kOwnVTable[6] = {
    SLOT(&DerivedAdapters::DeletingDestructor),
    SLOT(&cp::cBaseParserAdapters::SetVariable),
    SLOT(&cp::cBaseParserAdapters::SetField158),
    SLOT(&cp::cBaseParserAdapters::GetField158),
    SLOT(&cp::cBaseParserAdapters::DropDefinitionsOfLevel),
    SLOT(&BaseAdapters::FindState),
};
const void *const cLightingParserState::kOwnUnknownVTable[3] = {
    SLOT(&LightingShiftedThunk<0x24, &BaseAdapters::QueryInterface>::Call),
    BASE_THUNK(0x27c, AddRef),
    BASE_THUNK(0x27c, Release),
};
const void *const cLightingParserState::kOwnParserVTable[51] = {
    OWN_THUNK(Initialise),                                         // 0
    OWN_THUNK(Shutdown),                                           // 1
    PARSER_SLOTS_2_7(0x27c),                                       // 2..7
    OWN_THUNK(RestartViaReset),                                    // 8
    OWN_THUNK(Reset),                                              // 9
    SLOT(&LightingShiftedThunk<0x24, &BaseAdapters::ResetBlocks>::Call), // 10
    PARSER_SLOTS_11_50(0x27c),                                     // 11..50
};
const int32_t cLightingParserState::kOwnVbTable[3] = {-8, 0x424, 0x42c};
const int32_t cLightingParserState::kOwnParserFaceVbTable[2] = {-4, -0xc};

#undef OWN_THUNK

} // namespace nTSSG

#undef SLOT
#undef BASE_THUNK
#undef PARSER_SLOTS_2_7
#undef PARSER_SLOTS_11_50

// ---- the room settings entries ---------------------------------------------------------------------------------------

LightingSettingsEntry *LightingSettingsEntry::CopyConstruct(const LightingSettingsEntry &other)
{
    mBlock.mConfig = other.mBlock.mConfig;
    const std::string &text = other.mBlock.mText.mString;
    ::new (&mBlock.mText.mString) std::string(text.data(), text.size());
    mBlock.mText.mVTable = cRZString::VTable();
    mBlock.mText.mRefCount = 0;
    return this;
}

// ---- cTSLightingParserState ------------------------------------------------------------------------------------------

cTSLightingParserState *cTSLightingParserState::Construct(uint32_t mostDerived)
{
    uint8_t *const self = reinterpret_cast<uint8_t *>(this);
    if (mostDerived != 0) {
        Store<const int32_t *>(self + 8, kOwnVbTable);
        Store<const int32_t *>(self + 0x414, kOwnParserFaceVbTable);
    }
    Base()->Construct(0);
    Store<const void *>(self, kOwnVTable);
    Store<const void *>(static_cast<uint8_t *>(UnknownFace()), kOwnUnknownVTable);
    Store<const void *>(static_cast<uint8_t *>(ParserFace()), kOwnParserVTable);
    const int32_t unknownOffset = VbTable()[1];
    Store<int32_t>(self + 4 + unknownOffset, unknownOffset - kUnknownOffset);
    const int32_t parserOffset = VbTable()[2];
    Store<int32_t>(self + 4 + parserOffset, parserOffset - kParserOffset);

    std::construct_at(&mStates);
    mCurrent.Construct();
    std::construct_at(&mRoomSettings);
    m3D8.Construct();
    std::construct_at(&mTimesOfDay);
    return this;
}

// Destruct, Initialise and Reset are out of line (noinline): the derived class's bodies call them as the original's call
// 0xb3d3e0, 0xb3eea0 and 0xb3d8a0, and the tests stand in for them there.
__declspec(noinline) void cTSLightingParserState::Destruct()
{
    uint8_t *const self = reinterpret_cast<uint8_t *>(this);
    Store<const void *>(self, kOwnVTable);
    Store<const void *>(static_cast<uint8_t *>(UnknownFace()), kOwnUnknownVTable);
    Store<const void *>(static_cast<uint8_t *>(ParserFace()), kOwnParserVTable);
    const int32_t unknownOffset = VbTable()[1];
    Store<int32_t>(self + 4 + unknownOffset, unknownOffset - kUnknownOffset);
    const int32_t parserOffset = VbTable()[2];
    Store<int32_t>(self + 4 + parserOffset, parserOffset - kParserOffset);
    std::destroy_at(&mTimesOfDay);
    m3D8.Destruct();
    std::destroy_at(&mRoomSettings);
    std::destroy_at(&mCurrent);
    std::destroy_at(&mStates);
    Base()->Destruct();
}

cTSLightingParserState *cTSLightingParserState::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

bool cTSLightingParserState::FindState(const char *name, LightingState *out, uint32_t *index)
{
    const uint32_t count = static_cast<uint32_t>(mStates.size());
    for (uint32_t i = 0; i < count; ++i) {
        const std::string &stateName = mStates[i].mName.mString;
        const size_t length = std::strlen(name);
        if (length == stateName.size() && std::memcmp(name, stateName.data(), length) == 0) {
            *out = mStates[i];
            *index = i;
            return true;
        }
    }
    return false;
}

bool cTSLightingParserState::QueryInterface(uint32_t iid, void **out)
{
    if (iid != kIID) return Base()->QueryInterface(iid, out);
    *out = this;
    VirtualCall<AddRefFn, 1>(UnknownFace()); // receiver: cIGZUnknown
    return true;
}

__declspec(noinline) bool cTSLightingParserState::Initialise(uint8_t caseSensitive)
{
    Base()->Initialise(caseSensitive); // the result is not looked at
    VirtualCall<SetVersionRangeFn, 2>(ParserFace(), 4, 10); // receiver: nGZCommandParser::cIBaseParser
    RegisterBlockCommand(ParserFace(), sizeof(cLightingStateCommand), cLightingStateCommand::kVTable);
    RegisterBlockCommand(ParserFace(), sizeof(nTSSG::cTimeOfDayCommand), nTSSG::cTimeOfDayCommand::kVTable);
    return true;
}

bool cTSLightingParserState::Shutdown() { return Base()->Shutdown(); }

__declspec(noinline) void cTSLightingParserState::Reset()
{
    Base()->Reset();
    mStates.clear();
    mRoomSettings.clear();
    mTimesOfDay.clear();
    const LightingSettingsEntry defaults;
    mRoomSettings.push_back(defaults);
}

void cTSLightingParserState::ResetBlocks() { Base()->ResetBlocks(); }

// ---- its adapters ----------------------------------------------------------------------------------------------------

cTSLightingParserState *__fastcall cTSLightingParserStateAdapters::Construct(cTSLightingParserState *self, void *, uint32_t mostDerived)
{
    return self->Construct(mostDerived);
}
void __fastcall cTSLightingParserStateAdapters::Destruct(cTSLightingParserState *self, void *) { self->Destruct(); }
cTSLightingParserState *__fastcall cTSLightingParserStateAdapters::DeletingDestructor(cTSLightingParserState *self, void *, uint32_t flags)
{
    return self->DeletingDestructor(flags);
}
bool __fastcall cTSLightingParserStateAdapters::FindState(cTSLightingParserState *self, void *, const char *name, LightingState *out,
                                                          uint32_t *index)
{
    return self->FindState(name, out, index);
}
bool __fastcall cTSLightingParserStateAdapters::QueryInterface(void *unknownFace, void *, uint32_t iid, void **out)
{
    return cTSLightingParserState::FromUnknownFace(unknownFace)->QueryInterface(iid, out);
}
bool __fastcall cTSLightingParserStateAdapters::Initialise(void *face, void *, uint8_t caseSensitive)
{
    return cTSLightingParserState::FromParserFace(face)->Initialise(caseSensitive);
}
bool __fastcall cTSLightingParserStateAdapters::Shutdown(void *face, void *) { return cTSLightingParserState::FromParserFace(face)->Shutdown(); }
void __fastcall cTSLightingParserStateAdapters::Reset(void *face, void *) { cTSLightingParserState::FromParserFace(face)->Reset(); }
void __fastcall cTSLightingParserStateAdapters::ResetBlocks(void *face, void *) { cTSLightingParserState::FromParserFace(face)->ResetBlocks(); }
LightingSettingsEntry *__fastcall cTSLightingParserStateAdapters::CopySettings(LightingSettingsEntry *self, void *,
                                                                              const LightingSettingsEntry *other)
{
    return self->CopyConstruct(*other);
}
LightingState *__fastcall cTSLightingParserStateAdapters::AssignState(LightingState *self, void *, const LightingState *other)
{
    return &(*self = *other);
}

namespace nTSSG {

// ---- the timeOfDay command -------------------------------------------------------------------------------------------

void cTimeOfDayCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *const argv = args->Positional(&count, 2, 3);
    LightingTimeOfDay entry;
    entry.mName.assign(argv[0], std::strlen(argv[0]));
    entry.mTime = VirtualCall<EvalFloatFn, 31>(mParser, argv[1]); // receiver: nGZCommandParser::cIBaseParser
    entry.mValue = count == 3 ? VirtualCall<EvalFloatFn, 31>(mParser, argv[2]) : 0.0f; // receiver: nGZCommandParser::cIBaseParser
    State()->mTimesOfDay.push_back(entry);
}

const char *cTimeOfDayCommand::GetName() const { return Text(0xe849fc); }
const char *cTimeOfDayCommand::GetHelp(uint32_t) const { return Text(0xe84a08); }

// ---- nTSSG::cLightingParserState -------------------------------------------------------------------------------------

cLightingParserState *cLightingParserState::Construct()
{
    uint8_t *const self = reinterpret_cast<uint8_t *>(this);
    Store<const int32_t *>(self + 8, kOwnVbTable);
    mParserFaceVbTable = kOwnParserFaceVbTable;
    mBase.Construct(0);
    Store<const void *>(self, kOwnVTable);
    Store<const void *>(static_cast<uint8_t *>(UnknownFace()), kOwnUnknownVTable);
    Store<const void *>(static_cast<uint8_t *>(ParserFace()), kOwnParserVTable);
    Store<int32_t>(self + 4 + mBase.VbTable()[1], 0); // the vtordisps: this is the whole object
    Store<int32_t>(self + 4 + mBase.VbTable()[2], 0);
    mBase.m404 = 0;
    mText.Construct();
    return this;
}

cLightingParserState *cLightingParserState::DeletingDestructor(uint32_t flags)
{
    mText.Destruct();
    if (cILightSettings *const settings = Settings()) settings->Release();
    mBase.Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

bool cLightingParserState::Initialise(uint8_t caseSensitive)
{
    if (!mBase.Initialise(caseSensitive)) return false;
    cILightSettings *const made = new cLightSettings();
    cILightSettings *const old = Settings();
    if (made != old) {
        if (made) made->AddRef();
        Settings() = made;
        if (old) old->Release();
    }
    Settings()->Init();
    RegisterPlainCommand(ParserFace(), cLightAttribOverrideCommand::kVTable);
    RegisterBlockCommand(ParserFace(), sizeof(cLightAttributesCommand), cLightAttributesCommand::kVTable);
    return true;
}

bool cLightingParserState::Shutdown()
{
    if (Settings()) Settings()->Shutdown();
    if (cILightSettings *const old = Settings()) {
        Settings() = nullptr;
        old->Release();
    }
    return mBase.Base()->Shutdown();
}

void cLightingParserState::RestartViaReset()
{
    mBase.Base()->RestartViaReset();
    using RunAppLightingFn = void(__cdecl *)(void *);
    OriginalFunction<RunAppLightingFn, 0xb41a80>()(ParserFace());
}

void cLightingParserState::Reset()
{
    mBase.Reset();
    if (Settings()) Settings()->Clear();
}

// ---- its adapters ----------------------------------------------------------------------------------------------------

cLightingParserState *__fastcall cLightingParserStateAdapters::DeletingDestructor(cLightingParserState *self, void *, uint32_t flags)
{
    return self->DeletingDestructor(flags);
}
bool __fastcall cLightingParserStateAdapters::Initialise(void *face, void *, uint8_t caseSensitive)
{
    return cLightingParserState::FromParserFace(face)->Initialise(caseSensitive);
}
bool __fastcall cLightingParserStateAdapters::Shutdown(void *face, void *) { return cLightingParserState::FromParserFace(face)->Shutdown(); }
void __fastcall cLightingParserStateAdapters::RestartViaReset(void *face, void *)
{
    cLightingParserState::FromParserFace(face)->RestartViaReset();
}
void __fastcall cLightingParserStateAdapters::Reset(void *face, void *) { cLightingParserState::FromParserFace(face)->Reset(); }

} // namespace nTSSG
