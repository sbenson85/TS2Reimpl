// cTSEffectsParser (lane WU): see ts/cTSEffectsParser.h.
#include "ts/cTSEffectsParser.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/CommandParser.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "gz/EffectCommands.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>

using nGZCommandParser::cBaseParser;

namespace {

using ReleaseFn = uint32_t(__thiscall *)(void *);
using AddRefFn = uint32_t(__thiscall *)(void *);
using SetVersionRangeFn = void(__thiscall *)(void *, int32_t, int32_t); // cIBaseParser slot 2
using RegisterCommandFn = void(__thiscall *)(void *, void *);           // cIBaseParser slot 4
using ReleaseWithFlagFn = void(__thiscall *)(void *, uint32_t);         // a counted object's slot 4 (the reset's)
using ApplyFn = void(__thiscall *)(void *, void *);                     // a counted object's slot 3

void ReleaseObject(void *object)
{
    if (object) VirtualCall<ReleaseFn, 2>(object);
}

// A counted pointer replaced: the new one counted (slot 1) when not null, stored, the old one released (slot 2).
void ReplaceCounted(void *&slot, void *object)
{
    void *const old = slot;
    if (object == old) return;
    if (object) VirtualCall<AddRefFn, 1>(object);
    slot = object;
    if (old) VirtualCall<ReleaseFn, 2>(old);
}

// The 14 commands Initialise registers, in the original's order: the object's size, which base constructor runs (the plain
// command's, with the name a string at +0x10, or the block command's, with the name a pointer at +0x44), the vtable; the
// resource block also clears the word at +0x48. The names are the game's pointers at kCommandNames + 4 * index.
struct CommandRow {
    uint32_t size;
    bool block;
    uint32_t vtable;
    bool clearWord48;
};
constexpr CommandRow kCommands[14] = {
    {0x28, false, 0xe8dcc8, false}, // textureID
    {0x28, false, 0xe8dcf4, false}, // modelID
    {0x28, false, 0xe8dd20, false}, // soundID
    {0x28, false, 0xe8dd4c, false}, // brushID
    {0x28, false, 0xe8dd78, false}, // effectID
    {0x48, true, 0xe8ddd0, false},  // effectGroup
    {0x28, false, 0xe8dda4, false}, // instance
    {0x28, false, 0xe8e194, false}, // testEffect
    {0x28, false, 0xe8e1d4, false}, // messageTrigger
    {0x28, false, 0xe8e200, false}, // setPriority
    {0x4c, true, 0xe8de08, true},   // resource
    {0x28, false, 0xe8dee4, false}, // loadResource
    {0x4c, true, 0xe8df34, false},  // timeline
    {0x48, true, 0xe8e05c, false},  // effect
};

} // namespace

// ---- the vtables -----------------------------------------------------------------------------------------------------
// Slot order as the game's (tools/classinfo.py cTSEffectsParser). A slot is a thunk's forwarder: the face's pointer less its
// vtordisp (VtordispThunk, the bodies of this class's own faces) or less the vtordisp and the 0xf4 that separates this class's
// faces from cBaseParser's own (EffectsShiftedThunk, cBaseParser's bodies). The cIBaseParser face's slot 33 shares slot 32's
// (EvalInt); slots 2 and 3 of the primary vtable are the folded setter/getter.

#define SLOT(fn) reinterpret_cast<const void *>(fn)
#define OWN_THUNK(adapter) SLOT(&nGZCommandParser::VtordispThunk<&cTSEffectsParserAdapters::adapter>::Call)
#define BASE_THUNK(adapter) SLOT(&EffectsShiftedThunk<&nGZCommandParser::cBaseParserAdapters::adapter>::Call)

const void *const cTSEffectsParser::kOwnVTable[5] = {
    SLOT(&cTSEffectsParserAdapters::DeletingDestructor),
    SLOT(&nGZCommandParser::cBaseParserAdapters::SetVariable),
    SLOT(&cTSEffectsParserAdapters::SetField158),
    SLOT(&cTSEffectsParserAdapters::GetField158),
    SLOT(&nGZCommandParser::cBaseParserAdapters::DropDefinitionsOfLevel),
};
const void *const cTSEffectsParser::kOwnUnknownVTable[3] = {
    OWN_THUNK(QueryInterface),
    BASE_THUNK(AddRef),
    BASE_THUNK(Release),
};
const void *const cTSEffectsParser::kOwnParserVTable[51] = {
    OWN_THUNK(Initialise),               // 0
    BASE_THUNK(Shutdown),                // 1
    BASE_THUNK(SetVersionRange),         // 2
    BASE_THUNK(SetContextState),         // 3
    BASE_THUNK(RegisterCommand),         // 4
    BASE_THUNK(UnregisterCommand),       // 5
    BASE_THUNK(UnregisterCommandObject), // 6
    BASE_THUNK(RegisterMetaCommand),     // 7
    BASE_THUNK(RestartViaReset),         // 8
    OWN_THUNK(Reset),                    // 9
    BASE_THUNK(ResetBlocks),             // 10
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
const void *const cTSEffectsParser::kOwnEffectsVTable[7] = {
    OWN_THUNK(SetPointer1ac), OWN_THUNK(SetPointer1b0), OWN_THUNK(SetPointer1b4), OWN_THUNK(SetPointer1bc),
    OWN_THUNK(SetFlag1d0),    OWN_THUNK(Lookup),        OWN_THUNK(Apply),
};
const int32_t cTSEffectsParser::kOwnVbTable[4] = {-8, 0x29c, 0x2a4, 0x2b0};
const int32_t cTSEffectsParser::kOwnFace2VbTable[2] = {-4, -0xc};
const int32_t cTSEffectsParser::kOwnEffectsFaceVbTable[3] = {-4, -0x18, -0x10};

#undef SLOT
#undef OWN_THUNK
#undef BASE_THUNK

// ---- construction and destruction ------------------------------------------------------------------------------------

cTSEffectsParser *cTSEffectsParser::Construct(uint32_t mostDerived)
{
    if (mostDerived) {
        // The virtual bases' own constructors would store their vtables here; the stores below overwrite every one of them
        // before anything reads, so they are not made.
        *reinterpret_cast<const void **>(mBase + 8) = kOwnVbTable;
        mFace2VbTable = kOwnFace2VbTable;
        mEffectsFaceVbTable = kOwnEffectsFaceVbTable;
    }
    Parser()->Construct(0);
    *reinterpret_cast<const void *const **>(mBase) = kOwnVTable;
    *static_cast<const void *const **>(UnknownFace()) = kOwnUnknownVTable;
    *static_cast<const void *const **>(ParserFace()) = kOwnParserVTable;
    *static_cast<const void *const **>(EffectsFace()) = kOwnEffectsVTable;
    const int32_t *const vb = VbTable();
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[1] - 4) = vb[1] - 0x29c;
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[2] - 4) = vb[2] - 0x2a4;
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[3] - 4) = vb[3] - 0x2b0;
    mObject1ac = mDescriptions = mObject1b4 = mObject1b8 = mObject1bc = nullptr;
    std::memset(mZero1c0, 0, sizeof(mZero1c0));
    ::new (&mMap1d4) std::map<std::string, uint32_t>();
    ::new (&mMap1dc) std::map<std::string, uint32_t>();
    ::new (&mMap1e4) std::map<std::string, uint32_t>();
    ::new (&mMap1ec) std::map<std::string, uint32_t>();
    mText1f4.Construct();
    mZero214 = 0;
    mWord218 = 1;
    std::memset(mZero21c, 0, sizeof(mZero21c));
    ::new (&mList230) std::vector<EffectsRecord88>();
    mText23c.Construct();
    ::new (&mSet278) std::set<EffectsBlob56, EffectsBlob56Less>();
    ::new (&mMap280) std::map<std::string, std::string>();
    mRefs288.mFirst = mRefs288.mLast = mRefs288.mEnd = nullptr;
    ::new (&mValues294) std::vector<void *>();
    return this;
}

void cTSEffectsParser::Destruct()
{
    *reinterpret_cast<const void *const **>(mBase) = kOwnVTable;
    *static_cast<const void *const **>(UnknownFace()) = kOwnUnknownVTable;
    *static_cast<const void *const **>(ParserFace()) = kOwnParserVTable;
    *static_cast<const void *const **>(EffectsFace()) = kOwnEffectsVTable;
    const int32_t *const vb = VbTable();
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[1] - 4) = vb[1] - 0x29c;
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[2] - 4) = vb[2] - 0x2a4;
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[3] - 4) = vb[3] - 0x2b0;
    std::destroy_at(&mValues294);
    mRefs288.Destroy();
    std::destroy_at(&mMap280);
    std::destroy_at(&mSet278);
    mText23c.Destruct();
    std::destroy_at(&mList230);
    mText1f4.Destruct();
    std::destroy_at(&mMap1ec);
    std::destroy_at(&mMap1e4);
    std::destroy_at(&mMap1dc);
    std::destroy_at(&mMap1d4);
    ReleaseObject(mObject1bc);
    ReleaseObject(mObject1b8);
    ReleaseObject(mObject1b4);
    ReleaseObject(mDescriptions);
    ReleaseObject(mObject1ac);
    Parser()->Destruct();
}

cTSEffectsParser *cTSEffectsParser::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// ---- the bodies ------------------------------------------------------------------------------------------------------

bool cTSEffectsParser::Initialise(uint8_t caseSensitive)
{
    if (!Parser()->Initialise(caseSensitive)) return false;
    void *const face = ParserFace();
    VirtualCall<SetVersionRangeFn, 2>(face, 1, 0x16);
    int index = 0;
    for (const CommandRow &row : kCommands) {
        const char *const name = *GameAddress<const char *const>(kCommandNames + 4 * index++);
        void *const memory = ::operator new(row.size);
        if (memory) {
            if (row.block) {
                static_cast<nGZCommandParser::cBlockCommandBase *>(memory)->Construct();
                *static_cast<const void **>(memory) = GameAddress<void>(row.vtable);
                if (row.clearWord48) *reinterpret_cast<uint32_t *>(static_cast<char *>(memory) + 0x48) = 0;
                *reinterpret_cast<const char **>(static_cast<char *>(memory) + 0x44) = name;
            } else {
                static_cast<nGZCommandParser::cCommand *>(memory)->Construct();
                std::string *const text = ::new (static_cast<char *>(memory) + 0x10) std::string();
                *static_cast<const void **>(memory) = GameAddress<void>(row.vtable);
                text->assign(name, std::strlen(name));
            }
        }
        VirtualCall<RegisterCommandFn, 4>(face, memory);
    }
    OriginalFunction<void(__cdecl *)(void *), 0xbe7c30>()(face);
    OriginalFunction<void(__cdecl *)(void *), 0xbecba0>()(face);
    OriginalFunction<void(__cdecl *)(void *), 0xbf1b00>()(face);
    OriginalFunction<void(__cdecl *)(void *), 0xbf30b0>()(face);
    OriginalFunction<void(__cdecl *)(void *), 0xbf43f0>()(face);
    RegisterCameraEffectBlocks(face);
    RegisterWaterEffectBlock(face);
    RegisterScreenEffectBlock(face);
    OriginalFunction<void(__cdecl *)(void *), 0xbf93d0>()(face);
    return true;
}

void cTSEffectsParser::Reset()
{
    Parser()->Reset();
    mSet278.clear();
    // The counted objects of the list at +0x288 told to let go (slot 4, 1), then released; the list emptied.
    {
        void **const first = mRefs288.mFirst;
        for (void **it = first; it != mRefs288.mLast; ++it)
            if (*it) VirtualCall<ReleaseWithFlagFn, 4>(*it, 1);
        const auto last = mRefs288.mLast;
        for (void **it = first; it != last; ++it)
            if (*it) VirtualCall<ReleaseFn, 2>(*it);
        mRefs288.mLast = mRefs288.mFirst;
    }
    mValues294.clear();
    mText1f4.mString.clear();
    if (void *const object = mObject1b8) {
        mObject1b8 = nullptr;
        VirtualCall<ReleaseFn, 2>(object);
    }
    mWord276 = 0;
    mMap280.clear();
}

bool cTSEffectsParser::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kEffectsIID) {
        *out = EffectsFace();
        VirtualCall<AddRefFn, 1>(UnknownFace());
        return true;
    }
    return Parser()->QueryInterface(iid, out);
}

void cTSEffectsParser::SetPointer1ac(void *object)
{
    ReplaceCounted(mObject1ac, object);
    mFlag1d0 = 0;
}
void cTSEffectsParser::SetPointer1b0(void *object) { ReplaceCounted(mDescriptions, object); }
void cTSEffectsParser::SetPointer1b4(void *object) { ReplaceCounted(mObject1b4, object); }
void cTSEffectsParser::SetPointer1bc(void *object) { ReplaceCounted(mObject1bc, object); }
void cTSEffectsParser::SetFlag1d0(uint8_t value) { mFlag1d0 = value; }

const char *cTSEffectsParser::Lookup(const char *name, int32_t number)
{
    cRZString key;
    key.Construct();
    key.Sprintf(GameAddress<char>(kLookupFormat), name, number);
    for (char &c : key.mString) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const char *result = nullptr;
    const auto found = mMap280.find(key.mString);
    if (found != mMap280.end()) result = found->second.c_str();
    key.Destruct();
    return result;
}

void cTSEffectsParser::Apply()
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(mRefs288.mLast - mRefs288.mFirst); ++i)
        VirtualCall<ApplyFn, 3>(mRefs288.mFirst[i], mValues294.data()[i]);
}

// ---- the adapters --------------------------------------------------------------------------------------------------------

cTSEffectsParser *__fastcall cTSEffectsParserAdapters::Construct(cTSEffectsParser *self, void *, uint32_t mostDerived) { return self->Construct(mostDerived); }
void __fastcall cTSEffectsParserAdapters::Destruct(cTSEffectsParser *self, void *) { self->Destruct(); }
cTSEffectsParser *__fastcall cTSEffectsParserAdapters::DeletingDestructor(cTSEffectsParser *self, void *, uint32_t flags) { return self->DeletingDestructor(flags); }
void __fastcall cTSEffectsParserAdapters::SetField158(cTSEffectsParser *self, void *, uint32_t value)
{
    nGZCommandParser::cBaseParserAdapters::SetField158(self->Parser(), nullptr, value);
}
uint32_t __fastcall cTSEffectsParserAdapters::GetField158(cTSEffectsParser *self, void *)
{
    return nGZCommandParser::cBaseParserAdapters::GetField158(self->Parser(), nullptr);
}
bool __fastcall cTSEffectsParserAdapters::Initialise(void *face, void *, uint8_t caseSensitive)
{
    return cTSEffectsParser::FromParserFace(face)->Initialise(caseSensitive);
}
void __fastcall cTSEffectsParserAdapters::Reset(void *face, void *) { cTSEffectsParser::FromParserFace(face)->Reset(); }
bool __fastcall cTSEffectsParserAdapters::QueryInterface(void *face, void *, uint32_t iid, void **out)
{
    return cTSEffectsParser::FromUnknownFace(face)->QueryInterface(iid, out);
}
void __fastcall cTSEffectsParserAdapters::SetPointer1ac(void *face, void *, void *object) { cTSEffectsParser::FromEffectsFace(face)->SetPointer1ac(object); }
void __fastcall cTSEffectsParserAdapters::SetPointer1b0(void *face, void *, void *object) { cTSEffectsParser::FromEffectsFace(face)->SetPointer1b0(object); }
void __fastcall cTSEffectsParserAdapters::SetPointer1b4(void *face, void *, void *object) { cTSEffectsParser::FromEffectsFace(face)->SetPointer1b4(object); }
void __fastcall cTSEffectsParserAdapters::SetPointer1bc(void *face, void *, void *object) { cTSEffectsParser::FromEffectsFace(face)->SetPointer1bc(object); }
void __fastcall cTSEffectsParserAdapters::SetFlag1d0(void *face, void *, uint8_t value) { cTSEffectsParser::FromEffectsFace(face)->SetFlag1d0(value); }
const char *__fastcall cTSEffectsParserAdapters::Lookup(void *face, void *, const char *name, int32_t number)
{
    return cTSEffectsParser::FromEffectsFace(face)->Lookup(name, number);
}
void __fastcall cTSEffectsParserAdapters::Apply(void *face, void *) { cTSEffectsParser::FromEffectsFace(face)->Apply(); }
