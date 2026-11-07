// nGZSceneGraph::cMaterialParser (lane RH): see sg/cMaterialParser.h.
#include "sg/cMaterialParser.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/CommandParser.h"
#include "gz/GraphicServices.h"
#include "gz/cBaseParser.h"
#include "gz/cGZAny.h"
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cGZPropertySet.h"
#include "gz/cIGZPropertySet.h"
#include "gz/cParserThunks.h"
#include "sg/cLODLevels.h"
#include "sg/cMaterialAttributes.h"
#include "misc/UtilitiesB.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "sg/SGResourceBlockers.h"
#include "sg/cStringPool.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace nGZSceneGraph {

using nGZCommandParser::cBaseParser;

namespace {

using ReleaseFn = uint32_t(__thiscall *)(void *);
using AddRefFn = uint32_t(__thiscall *)(void *);
using SetVersionRangeFn = void(__thiscall *)(void *, int32_t, int32_t); // cIBaseParser slot 2
using RegisterCommandFn = void(__thiscall *)(void *, void *);           // cIBaseParser slot 4
using RegisterFunctionFn = void(__thiscall *)(void *, void *);          // cIBaseParser slot 7

// A counted pointer's reference dropped: the slot 2 call when it is not null.
void ReleaseObject(void *object)
{
    if (object) VirtualCall<ReleaseFn, 2>(object);
}

// The pointer cleared before the release (the reset's order).
void DropObject(void *&slot)
{
    void *const object = slot;
    if (!object) return;
    slot = nullptr;
    VirtualCall<ReleaseFn, 2>(object);
}

// A list emptied: nothing happens to a list that already is (the original compares first; the storage is kept).
template <class V> void EmptyList(V &list)
{
    if (list.begin() != list.end()) list.clear();
}

// The strings' reset: size zero and a NUL first byte, the storage kept.
void EmptyText(std::string &text) { text.clear(); }

// The commands and functions Initialise registers, in the original's order. Commands: the object's size, which base
// constructor runs (the plain command's or the block command's), the vtable. Functions (0x10 bytes, the expression
// function's constructor): the two vtables at +0 and +4.
struct CommandRow {
    uint32_t size;
    bool block;
    uint32_t vtable;
};
constexpr CommandRow kCommands[] = {
    {0x10, false, 0xe7a13c}, // cFlushMaterialsCommand
    {0x10, false, 0xe7a19c}, // cBindMaterialsCommand
    {0x10, false, 0xe7a1d8}, // cEnableSoftwareVertexProgramsCommand
    {0x10, false, 0xe7a224}, // cEnableShaderProgramDebuggingCommand
    {0x44, true, 0xe7a330},  // cMaterialDefinitionCommand
    {0x44, true, 0xe7bfac},  // cDefaultStateCommand
    {0x10, false, 0xe7bff4}, // cDefaultMaterialCommand
    {0x44, true, 0xe7b4e0},  // cShaderProgramCommand
    {0x44, true, 0xe7bf74},  // cMaterialCommand
};
struct FunctionRow {
    uint32_t vtable, faceVTable;
};
constexpr FunctionRow kFunctions[] = {
    {0xe7c030, 0xe7c040}, // cQueryIntegerDeviceCapFunction
    {0xe7c12c, 0xe7c13c}, // cQueryBooleanDeviceCapFunction
    {0xe7c0c8, 0xe7c0d8}, // cQueryAvailableVideoMemoryFunction
    {0xe7c1c0, 0xe7c1d0}, // cRSCNumLightsOfTypeFunction
    {0xe7c254, 0xe7c264}, // cRSCShapeHasBlendFunction
    {0xe7c2bc, 0xe7c2cc}, // cRSCHasVertexFormatElementFunction
    {0xe7c32c, 0xe7c33c}, // cRSCMaxBonesPerVertexFunction
    {0xe7c3d0, 0xe7c3e0}, // cRSCVertexFormatElementRangeFunction
    {0xe7c444, 0xe7c454}, // cRSCHasDeformationsFunction
    {0xe7c4c4, 0xe7c4d4}, // cRSCNumTransformsFunction
    {0xe7c52c, 0xe7c53c}, // cRSCNumPoseTransformsFunction
    {0xe7c570, 0xe7c580}, // cRSCNumMorphTargetsFunction
    {0xe7c5d8, 0xe7c5e8}, // cRSCViewerRenderTypeFunction
    {0xe7c634, 0xe7c644}, // cRSCIsEnabledFunction
};

// The cell of a name: the string pool's handle for the text (0xae87d0 with flag 0, 0xae88d0 with flag 1), the empty cell for no
// name.
struct NameCell : OwnedCell {
    NameCell(uint32_t name, uint32_t flag)
    {
        if (name) cell.Open(name, flag);
    }
};

// A list's order: by the name cell's handle, unsigned (std::equal_range over the list, as 0xacebc0).
struct PairLess {
    bool operator()(const CellPair &a, const OwnedCell &b) const { return a.first.cell.mValue < b.cell.mValue; }
    bool operator()(const OwnedCell &a, const CellPair &b) const { return a.cell.mValue < b.first.cell.mValue; }
};

// ---- what the material builders call ----

// The material definition's slots.
using DefObjectFn = void *(__thiscall *)(void *);                                // 8
using DefNameFn = const char *(__thiscall *)(void *);                            // 19
using DefTextFn = const char *(__thiscall *)(void *);                            // 20
using DefCountFn = uint32_t(__thiscall *)(void *);                               // 21
using DefPairFn = const uint32_t *(__thiscall *)(void *, uint32_t *, uint32_t);  // 22 (out, index): the out's address
// Slot 22 of slot 8's object: cSGResource::CopyKeyTo, the 16-byte resource key copied into out, out returned.
using ObjectKeyFn = cGZPersistResourceKey *(__thiscall *)(void *, cGZPersistResourceKey *);
// The cIBaseParser face's slots.
using RunLineFn = void(__thiscall *)(void *, const char *);                      // 15
using SetVariableFn = void(__thiscall *)(void *, const char *, const char *);    // 19
using PushNamespaceFn = void(__thiscall *)(void *, const char *);                // 20
using PopNamespaceFn = void(__thiscall *)(void *);                               // 21
using PurgeNamespaceFn = void(__thiscall *)(void *, const char *);               // 22
// The device's slot 35 (usable), the framework's slot 3 (class, interface, out), the shaders' slots.
using DeviceUsableFn = bool(__thiscall *)(void *);
using CreateInstanceFn = bool(__thiscall *)(void *, uint32_t, uint32_t, void **);
using ContextShaderFn = uint32_t(__thiscall *)(void *, uint32_t, const void *, uint32_t); // the stand-in's slot 21
using SetShaderFn = void(__thiscall *)(void *, void *, uint32_t);                          // the shader's slot 8

constexpr uint32_t kStandInClass = 0x7c17ff25, kStandInIid = 0x5c17ff18;
constexpr uint32_t kShaderClass = 0xebfb3834, kShaderIid = 0xfc17e651;
constexpr uint32_t kSettingsClass = 0x0ac1887e, kSettingsIid = 0x4bb0d269;

const char *DefName(void *definition) { return VirtualCall<DefNameFn, 19>(definition); }

// The cell `target` set to the cell of `name` (0xae87d0, ours: OpenNameCell).
void AssignNameCell(OwnedCell &target, const char *name)
{
    ServiceCell cell;
    nGZSceneGraph::OpenNameCell(&cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)));
    target.cell.Assign(&cell.mValue);
    cell.Close();
}

// The word at +0x1d4: the group of the resource key of slot 8's object (the whole key is written: 16 bytes).
uint32_t DefinitionWord(void *definition)
{
    void *const object = VirtualCall<DefObjectFn, 8>(definition);
    cGZPersistResourceKey key;
    return VirtualCall<ObjectKeyFn, 22>(object, &key)->group;
}

// "verboseShaderValidation" from the settings service's property set (false when it has none); the set is released when
// the builder ends (ScopedRef::ReleaseObject, as the original).
struct VerboseSetting {
    ScopedRef set{nullptr};
    uint8_t value;
    VerboseSetting()
    {
        AnyBoolean any{gzany::BooleanVTable(), 0, {}};
        if (void *service = GetServiceObject())
            VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(service, kSettingsClass, kSettingsIid, &set.mObject);
        {
            GZServiceString key("verboseShaderValidation");
            VirtualCall<bool(__thiscall *)(void *, const GZServiceString *, void *), 9>(set.mObject, &key, &any);
        }
        value = any.mValue;
    }
    VerboseSetting(const VerboseSetting &) = delete;
    VerboseSetting &operator=(const VerboseSetting &) = delete;
    ~VerboseSetting() { set.ReleaseObject(); }
};

// "create <name>()", the line that runs a material script.
std::string CreateLine(const char *name)
{
    std::string line("create ");
    line += name;
    line += "()";
    return line;
}

// The cell `cell`'s text (Dispatch8, 0xaeb800).
const char *CellText(const OwnedCell &cell)
{
    return reinterpret_cast<const char *>(static_cast<uintptr_t>(Dispatch8(const_cast<OwnedCell *>(&cell))));
}

// A list's cells set as the parser's variables (the value's text asked for first).
void SetCellVariables(void *parser, const std::vector<CellPair> &cells)
{
    for (const CellPair &pair : cells) {
        const char *const value = CellText(pair.second);
        const char *const name = CellText(pair.first);
        VirtualCall<SetVariableFn, 19>(parser, name, value);
    }
}

// The reason strings of BuildErrorShader (jump table 0xad12b0).
const char *ErrorScript(int32_t reason)
{
    switch (reason) {
    case 1: return "VisualizeValidationFailure";
    case 2: return "VisualizeUnknownMaterialNameError";
    case 3: return "VisualizeParseMaterialFailure";
    case 4: return "VisualizeInconsistentSupportedRenderShaderContexts";
    default: return nullptr;
    }
}

} // namespace

OwnedCell::OwnedCell(const OwnedCell &other)
{
    cell.mValue = other.cell.mValue;
    static_cast<cIStringPool *>(GetCachedObjectB())->AddRefHandle(cell.mValue);
}

OwnedCell &OwnedCell::operator=(const OwnedCell &other)
{
    cell.Assign(&other.cell.mValue);
    return *this;
}

// ---- the vtables -----------------------------------------------------------------------------------------------------
// Slot order as the game's (tools/classinfo.py nGZSceneGraph::cMaterialParser). A slot is a thunk's forwarder: the face's
// pointer less its vtordisp (VtordispThunk, the bodies of this class's own faces) or less the vtordisp and the 0x138 that
// separates this class's faces from cBaseParser's own (ParserShiftedThunk, cBaseParser's and cLODLevelsTable's bodies). The
// cIBaseParser face's slot 33 shares slot 32's (EvalInt); slots 2 and 3 of the primary vtable are the folded setter/getter.

#define SLOT(fn) reinterpret_cast<const void *>(fn)
#define OWN_THUNK(adapter) SLOT(&nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::adapter>::Call)
#define BASE_THUNK(adapter) SLOT(&ParserShiftedThunk<&nGZCommandParser::cBaseParserAdapters::adapter>::Call)
#define LOD_THUNK(adapter) SLOT(&ParserShiftedThunk<&cLODLevelsTableAdapters::adapter>::Call)

const void *const cMaterialParser::kOwnVTable[5] = {
    SLOT(&cMaterialParserAdapters::DeletingDestructor),
    SLOT(&nGZCommandParser::cBaseParserAdapters::SetVariable),
    SLOT(&cMaterialParserAdapters::SetField158),
    SLOT(&cMaterialParserAdapters::GetField158),
    SLOT(&nGZCommandParser::cBaseParserAdapters::DropDefinitionsOfLevel),
};
const void *const cMaterialParser::kOwnUnknownVTable[3] = {
    OWN_THUNK(QueryInterface),
    BASE_THUNK(AddRef),
    BASE_THUNK(Release),
};
const void *const cMaterialParser::kOwnParserVTable[51] = {
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
    LOD_THUNK(NoOp),                     // 11
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
    LOD_THUNK(GetVersion),               // 45
    BASE_THUNK(GetFileName),             // 46
    BASE_THUNK(GetLine),                 // 47
    BASE_THUNK(HasDefinition),           // 48
    BASE_THUNK(HasCommand),              // 49
    BASE_THUNK(GetSource),               // 50
};
const void *const cMaterialParser::kOwnMaterialVTable[9] = {
    OWN_THUNK(SetPair),         OWN_THUNK(RemovePair), OWN_THUNK(SetAlias),    OWN_THUNK(EraseCell), OWN_THUNK(ParseDefinition),
    OWN_THUNK(ResetState),      OWN_THUNK(BuildShader), OWN_THUNK(BuildErrorShader), OWN_THUNK(IsStateOne),
};
const int32_t cMaterialParser::kOwnVbTable[4] = {-8, 0x2e0, 0x2e8, 0x2f4};
const int32_t cMaterialParser::kOwnFace2VbTable[2] = {-4, -0xc};
const int32_t cMaterialParser::kOwnMaterialFaceVbTable[3] = {-4, -0x18, -0x10};

#undef SLOT
#undef OWN_THUNK
#undef BASE_THUNK
#undef LOD_THUNK

// ---- construction and destruction ------------------------------------------------------------------------------------

cMaterialParser *cMaterialParser::Construct(uint32_t mostDerived)
{
    if (mostDerived) {
        // The virtual bases' own constructors would store their vtables here (cIMaterialParser's three, cBaseParser's two
        // empty interfaces); the stores below overwrite every one of them before anything reads, so they are not made.
        *reinterpret_cast<const void **>(mBase + 8) = kOwnVbTable;
        mFace2VbTable = kOwnFace2VbTable;
        mMaterialFaceVbTable = kOwnMaterialFaceVbTable;
    }
    Parser()->Construct(0);
    *reinterpret_cast<const void *const **>(mBase) = kOwnVTable;
    *static_cast<const void *const **>(UnknownFace()) = kOwnUnknownVTable;
    *static_cast<const void *const **>(ParserFace()) = kOwnParserVTable;
    *static_cast<const void *const **>(MaterialFace()) = kOwnMaterialVTable;
    const int32_t *const vb = VbTable();
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[1] - 4) = vb[1] - 0x2e0;
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[2] - 4) = vb[2] - 0x2e8;
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[3] - 4) = vb[3] - 0x2f4;
    mRefs.mFirst = mRefs.mLast = mRefs.mEnd = nullptr;
    UtilitiesB_SetInvalidIndex(&mCellA, nullptr);
    UtilitiesB_SetInvalidIndex(&mCellB, nullptr);
    std::memset(mZero1c0, 0, sizeof(mZero1c0));
    mState = 0;
    mZero1d4 = 0;
    mObject1d8 = mObject1dc = nullptr;
    ::new (&mList1e0) std::vector<uint32_t>();
    ::new (&mList1ec) std::vector<Record34>();
    mFlag1f8 = 0;
    mObject1fc = mObject200 = mObject204 = nullptr;
    // The strings' bytes all zero first (the constructor clears 16 bytes at a time), then the empty string's words.
    std::memset(&mText208, 0, sizeof(mText208));
    ::new (&mText208) std::string();
    ::new (&mList220) std::vector<uint32_t>();
    std::memset(&mText22c, 0, sizeof(mText22c));
    ::new (&mText22c) std::string();
    std::memset(&mText244, 0, sizeof(mText244));
    ::new (&mText244) std::string();
    ::new (&mList25c) std::vector<Blob12>();
    std::memset(mFlags268, 0, sizeof(mFlags268));
    mFlag270 = 0;
    mFlag271 = 1;
    mFlag272 = 0;
    mField274 = 4;
    mField278 = 4;
    mField27c = -1;
    mField280 = -1;
    mField284 = 0;
    ::new (&mMap2c8) std::map<OwnedCell, std::vector<CellPair>, CellLess>();
    ::new (&mCells2d0) std::vector<CellPair>();
    ::new (&mMap2dc) std::map<OwnedCell, OwnedCell, CellLess>();
    return this;
}

void cMaterialParser::Destruct()
{
    *reinterpret_cast<const void *const **>(mBase) = kOwnVTable;
    *static_cast<const void *const **>(UnknownFace()) = kOwnUnknownVTable;
    *static_cast<const void *const **>(ParserFace()) = kOwnParserVTable;
    *static_cast<const void *const **>(MaterialFace()) = kOwnMaterialVTable;
    const int32_t *const vb = VbTable();
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[1] - 4) = vb[1] - 0x2e0;
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[2] - 4) = vb[2] - 0x2e8;
    *reinterpret_cast<int32_t *>(mBase + 8 + vb[3] - 4) = vb[3] - 0x2f4;
    std::destroy_at(&mMap2dc);
    std::destroy_at(&mCells2d0);
    std::destroy_at(&mMap2c8);
    std::destroy_at(&mList25c);
    std::destroy_at(&mText244);
    std::destroy_at(&mText22c);
    std::destroy_at(&mList220);
    std::destroy_at(&mText208);
    ReleaseObject(mObject204);
    ReleaseObject(mObject200);
    ReleaseObject(mObject1fc);
    std::destroy_at(&mList1ec);
    std::destroy_at(&mList1e0);
    ReleaseObject(mObject1dc);
    ReleaseObject(mObject1d8);
    mCellB.~OwnedCell();
    mCellA.~OwnedCell();
    mRefs.Destroy();
    Parser()->Destruct();
}

cMaterialParser *cMaterialParser::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// ---- the bodies ------------------------------------------------------------------------------------------------------

bool cMaterialParser::Initialise(uint8_t caseSensitive)
{
    Parser()->Initialise(caseSensitive);
    void *const face = ParserFace();
    VirtualCall<SetVersionRangeFn, 2>(face, 1, 4);
    for (const CommandRow &row : kCommands) {
        void *const memory = ::operator new(row.size);
        void *command = nullptr;
        if (memory) {
            if (row.block) static_cast<nGZCommandParser::cBlockCommandBase *>(memory)->Construct();
            else static_cast<nGZCommandParser::cCommand *>(memory)->Construct();
            *static_cast<const void **>(memory) = GameAddress<void>(row.vtable);
            command = memory;
        }
        VirtualCall<RegisterCommandFn, 4>(ParserFace(), command);
    }
    for (const FunctionRow &row : kFunctions) {
        void *const memory = ::operator new(0x10);
        void *function = nullptr;
        if (memory) {
            static_cast<nGZCommandParser::cExpressionFunction *>(memory)->Construct();
            static_cast<void **>(memory)[0] = GameAddress<void>(row.vtable);
            static_cast<void **>(memory)[1] = GameAddress<void>(row.faceVTable);
            function = static_cast<char *>(memory) + 4;
        }
        VirtualCall<RegisterFunctionFn, 7>(ParserFace(), function);
    }
    return true;
}

void cMaterialParser::ResetBlocks()
{
    Parser()->ResetBlocks();
    DropObject(mObject1d8);
    DropObject(mObject1dc);
    DropObject(mObject200);
    DropObject(mObject1fc);
    DropObject(mObject204);
    mFlag1f8 = 0;
    std::memset(mFlags268 + 1, 0, 8);
    mFlag271 = 1;
    mFlag272 = 0;
    mField274 = 4;
    mField278 = 4;
    mField27c = -1;
    mField280 = -1;
    mField284 = 0;
    EmptyList(mList1e0);
    EmptyList(mList1ec);
    EmptyText(mText208);
    EmptyList(mList220);
    EmptyText(mText22c);
    EmptyText(mText244);
    EmptyList(mList25c);
}

void cMaterialParser::ResetState()
{
    mState = 0;
    DropObject(mObject1dc);
    mFlag1f8 = 0;
    EmptyList(mList1e0);
    EmptyList(mList1ec);
}

void cMaterialParser::EraseCell(uint32_t handle)
{
    ServiceCell cell;
    nGZSceneGraph::OpenNameCell(&cell, handle);
    const auto found = mMap2dc.find(cell.mValue);
    if (found != mMap2dc.end()) mMap2dc.erase(found);
    cell.Close();
}

std::vector<CellPair> &cMaterialParser::GroupCells(uint32_t group)
{
    if (!group) return mCells2d0;
    NameCell key(group, 0);
    return mMap2c8[key];
}

void cMaterialParser::SetPair(uint32_t group, uint32_t name, uint32_t value)
{
    std::vector<CellPair> &cells = GroupCells(group);
    std::pair<std::vector<CellPair>::iterator, std::vector<CellPair>::iterator> range;
    {
        NameCell key(name, 1);
        range = std::equal_range(cells.begin(), cells.end(), static_cast<const OwnedCell &>(key), PairLess());
    }
    if (range.first == range.second) {
        NameCell newName(name, 1);
        NameCell newValue(value, 1);
        const CellPair pair{newName, newValue};
        cells.insert(range.first, pair);
    } else {
        NameCell newValue(value, 1);
        range.first->second = newValue;
    }
}

void cMaterialParser::RemovePair(uint32_t group, uint32_t name)
{
    std::vector<CellPair> &cells = GroupCells(group);
    if (!name) {
        cells.clear();
        return;
    }
    std::pair<std::vector<CellPair>::iterator, std::vector<CellPair>::iterator> range;
    {
        NameCell key(name, 1);
        range = std::equal_range(cells.begin(), cells.end(), static_cast<const OwnedCell &>(key), PairLess());
    }
    if (range.first != range.second) cells.erase(range.first);
}

void cMaterialParser::SetAlias(uint32_t from, uint32_t to)
{
    NameCell key(from, 0);
    OwnedCell target;
    target.cell.Open(to, 0);
    mMap2dc[key] = target;
}

bool cMaterialParser::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kMaterialIID) {
        *out = MaterialFace();
        VirtualCall<AddRefFn, 1>(UnknownFace());
        return true;
    }
    return Parser()->QueryInterface(iid, out);
}

// ---- the material builders ---------------------------------------------------------------------------------------------

// The text a definition's script runs: its own, or the alias the second map has for the material's name cell.
static const char *ScriptText(const std::map<OwnedCell, OwnedCell, CellLess> &aliases, const OwnedCell &name, const char *text)
{
    const auto found = aliases.find(name.cell.mValue);
    return found != aliases.end() ? CellText(found->second) : text;
}

MaterialDefinitionInfo *cMaterialParser::ParseDefinition(MaterialDefinitionInfo *out, void *definition)
{
    mState = 1;
    AssignNameCell(mCellB, DefName(definition));
    mZero1d4 = DefinitionWord(definition);
    VerboseSetting verbose;
    mFlags268[0] = verbose.value;
    const char *const text = ScriptText(mMap2dc, mCellB, VirtualCall<DefTextFn, 20>(definition));
    if (text) {
        void *const parser = ParserFace();
        try {
            VirtualCall<PushNamespaceFn, 20>(parser, DefName(definition));
            VirtualCall<SetVariableFn, 19>(parser, "currentMaterialName", DefName(definition));
            for (uint32_t i = 0; i < VirtualCall<DefCountFn, 21>(definition); ++i) {
                uint32_t valueOut[2], nameOut[2];
                const uint32_t *const value = VirtualCall<DefPairFn, 22>(definition, valueOut, i);
                const uint32_t *const name = VirtualCall<DefPairFn, 22>(definition, nameOut, i);
                VirtualCall<SetVariableFn, 19>(parser, reinterpret_cast<const char *>(static_cast<uintptr_t>(name[0])),
                                               reinterpret_cast<const char *>(static_cast<uintptr_t>(value[1])));
            }
            SetCellVariables(parser, mCells2d0);
            const auto group = mMap2c8.find(mCellB.cell.mValue);
            if (group != mMap2c8.end()) SetCellVariables(parser, group->second);
            VirtualCall<RunLineFn, 15>(parser, CreateLine(text).c_str());
            VirtualCall<PopNamespaceFn, 21>(parser);
        } catch (const nGZCommandParser::cError &) {
        }
    }
    out->definition = mObject1dc;
    out->count1e0 = static_cast<uint32_t>(mList1e0.size());
    out->list1e0 = mList1e0.empty() ? nullptr : mList1e0.data();
    out->count1ec = static_cast<uint32_t>(mList1ec.size());
    out->list1ec = mList1ec.empty() ? nullptr : mList1ec.data();
    out->flag1f8 = mFlag1f8;
    return out;
}

uint32_t cMaterialParser::BuildShader(void **out, void *definition, uint32_t context, const Blob12 *data)
{
    *out = nullptr;
    const char *const text = ScriptText(mMap2dc, mCellB, VirtualCall<DefTextFn, 20>(definition));
    if (!text) return 2;
    void *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (!device || !VirtualCall<DeviceUsableFn, 35>(device)) {
        // No usable device: the framework's stand-in shader, set up for the context.
        void *standIn = nullptr;
        VirtualCall<CreateInstanceFn, 3>(GetDirectorFrameworkObject(), kStandInClass, kStandInIid, &standIn);
        VirtualCall<CreateInstanceFn, 3>(GetDirectorFrameworkObject(), kShaderClass, kShaderIid, out);
        const uint32_t shaderContext = VirtualCall<ContextShaderFn, 21>(standIn, context, data, 0);
        VirtualCall<SetShaderFn, 8>(*out, device, shaderContext);
        ReleaseObject(standIn);
        return 0;
    }
    AssignNameCell(mCellB, DefName(definition));
    mZero1d4 = DefinitionWord(definition);
    mZero1c0[0] = context;
    mZero1c0[1] = data->words[0];
    mZero1c0[2] = data->words[1];
    mZero1c0[3] = data->words[2];
    VerboseSetting verbose;
    mFlags268[0] = verbose.value;
    std::string space(DefName(definition));
    space += ':';
    void *const parser = ParserFace();
    VirtualCall<PurgeNamespaceFn, 22>(parser, space.c_str());
    uint32_t status;
    try {
        VirtualCall<PushNamespaceFn, 20>(parser, DefName(definition));
        VirtualCall<SetVariableFn, 19>(parser, "currentMaterialName", DefName(definition));
        const uint32_t count = VirtualCall<DefCountFn, 21>(definition);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t pair[2];
            VirtualCall<DefPairFn, 22>(definition, pair, i); // read from the out (slot 4 reads the answer)
            VirtualCall<SetVariableFn, 19>(parser, reinterpret_cast<const char *>(static_cast<uintptr_t>(pair[0])),
                                           reinterpret_cast<const char *>(static_cast<uintptr_t>(pair[1])));
        }
        SetCellVariables(parser, mCells2d0);
        const auto group = mMap2c8.find(mCellB.cell.mValue);
        if (group != mMap2c8.end()) SetCellVariables(parser, group->second);
        mState = 2;
        DropObject(mObject200);
        DropObject(mObject1fc);
        VirtualCall<RunLineFn, 15>(parser, CreateLine(text).c_str());
        // Nothing validated: the fallback material's script (the cell at +0x1b8) when there is one.
        if (!mObject200 && !reinterpret_cast<const SGResourceBlockers::cInvalidDwordTag *>(&mCellA)->IsInvalid()) {
            mState = 2;
            DropObject(mObject200);
            DropObject(mObject1fc);
            VirtualCall<RunLineFn, 15>(parser, CreateLine(CellText(mCellA)).c_str());
        }
        if (mState == 6) {
            status = 1;
            DropObject(mObject200);
            DropObject(mObject1fc);
        } else {
            status = mObject200 ? 0 : 3;
        }
        VirtualCall<PopNamespaceFn, 21>(parser);
    } catch (const nGZCommandParser::cError &) {
        status = 3;
    }
    VirtualCall<PurgeNamespaceFn, 22>(parser, space.c_str());
    if (mObject200) {
        *out = mObject200;
        VirtualCall<AddRefFn, 1>(mObject200);
    }
    mState = 0;
    DropObject(mObject1dc);
    DropObject(mObject200);
    DropObject(mObject1fc);
    std::memset(mZero1c0, 0, sizeof(mZero1c0));
    return status;
}

void cMaterialParser::BuildErrorShader(void **out, void *definition, uint32_t context, const Blob12 *data, int32_t reason)
{
    *out = nullptr;
    AssignNameCell(mCellB, DefName(definition));
    mZero1d4 = DefinitionWord(definition);
    mZero1c0[0] = context;
    mZero1c0[1] = data->words[0];
    mZero1c0[2] = data->words[1];
    mZero1c0[3] = data->words[2];
    mFlags268[0] = 0;
    std::string space(DefName(definition));
    space += ':';
    void *const parser = ParserFace();
    VirtualCall<PurgeNamespaceFn, 22>(parser, space.c_str());
    const char *const script = ErrorScript(reason);
    void *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (device && VirtualCall<DeviceUsableFn, 35>(device) && script) {
        try {
            VirtualCall<PushNamespaceFn, 20>(parser, DefName(definition));
            VirtualCall<SetVariableFn, 19>(parser, "currentMaterialName", DefName(definition));
            mState = 2;
            DropObject(mObject200);
            DropObject(mObject1fc);
            VirtualCall<RunLineFn, 15>(parser, CreateLine(script).c_str());
            VirtualCall<PopNamespaceFn, 21>(parser);
        } catch (const nGZCommandParser::cError &) {
        }
    }
    VirtualCall<PurgeNamespaceFn, 22>(parser, space.c_str());
    if (!mObject200) {
        // Nothing built: the framework's stand-in shader for the context.
        void *const framework = GetDirectorFrameworkObject();
        DropObject(mObject1fc);
        VirtualCall<CreateInstanceFn, 3>(framework, kStandInClass, kStandInIid, &mObject1fc);
        void *const framework2 = GetDirectorFrameworkObject();
        DropObject(mObject200);
        VirtualCall<CreateInstanceFn, 3>(framework2, kShaderClass, kShaderIid, &mObject200);
        const uint32_t shaderContext = VirtualCall<ContextShaderFn, 21>(mObject1fc, mZero1c0[0], &mZero1c0[1], 0);
        VirtualCall<SetShaderFn, 8>(mObject200, device, shaderContext);
    }
    // ORIGINAL-BUG: no test for a framework that made no shader (the AddRef goes through a null pointer).
    *out = mObject200;
    VirtualCall<AddRefFn, 1>(mObject200);
    mState = 0;
    DropObject(mObject1dc);
    DropObject(mObject200);
    DropObject(mObject1fc);
    std::memset(mZero1c0, 0, sizeof(mZero1c0));
}

// ---- the hook entries ------------------------------------------------------------------------------------------------

cMaterialParser *__fastcall cMaterialParserAdapters::Construct(cMaterialParser *self, void *, uint32_t mostDerived)
{
    return self->Construct(mostDerived);
}
void __fastcall cMaterialParserAdapters::Destruct(cMaterialParser *self, void *) { self->Destruct(); }
cMaterialParser *__fastcall cMaterialParserAdapters::DeletingDestructor(cMaterialParser *self, void *, uint32_t flags)
{
    return self->DeletingDestructor(flags);
}
void __fastcall cMaterialParserAdapters::SetField158(cMaterialParser *self, void *, uint32_t value) { self->Parser()->mField158 = value; }
uint32_t __fastcall cMaterialParserAdapters::GetField158(cMaterialParser *self, void *) { return self->Parser()->mField158; }
bool __fastcall cMaterialParserAdapters::Initialise(void *face, void *, uint8_t caseSensitive)
{
    return cMaterialParser::FromParserFace(face)->Initialise(caseSensitive);
}
void __fastcall cMaterialParserAdapters::ResetBlocks(void *face, void *) { cMaterialParser::FromParserFace(face)->ResetBlocks(); }
void __fastcall cMaterialParserAdapters::ResetState(void *face, void *) { cMaterialParser::FromMaterialFace(face)->ResetState(); }
bool __fastcall cMaterialParserAdapters::IsStateOne(void *face, void *) { return cMaterialParser::FromMaterialFace(face)->IsStateOne(); }
void __fastcall cMaterialParserAdapters::EraseCell(void *face, void *, uint32_t handle)
{
    cMaterialParser::FromMaterialFace(face)->EraseCell(handle);
}
void __fastcall cMaterialParserAdapters::SetPair(void *face, void *, uint32_t group, uint32_t name, uint32_t value)
{
    cMaterialParser::FromMaterialFace(face)->SetPair(group, name, value);
}
void __fastcall cMaterialParserAdapters::RemovePair(void *face, void *, uint32_t group, uint32_t name)
{
    cMaterialParser::FromMaterialFace(face)->RemovePair(group, name);
}
void __fastcall cMaterialParserAdapters::SetAlias(void *face, void *, uint32_t from, uint32_t to)
{
    cMaterialParser::FromMaterialFace(face)->SetAlias(from, to);
}
bool __fastcall cMaterialParserAdapters::QueryInterface(void *face, void *, uint32_t iid, void **out)
{
    return cMaterialParser::FromUnknownFace(face)->QueryInterface(iid, out);
}
MaterialDefinitionInfo *__fastcall cMaterialParserAdapters::ParseDefinition(void *face, void *, MaterialDefinitionInfo *out, void *definition)
{
    return cMaterialParser::FromMaterialFace(face)->ParseDefinition(out, definition);
}
uint32_t __fastcall cMaterialParserAdapters::BuildShader(void *face, void *, void **out, void *definition, uint32_t context, const Blob12 *data)
{
    return cMaterialParser::FromMaterialFace(face)->BuildShader(out, definition, context, data);
}
void __fastcall cMaterialParserAdapters::BuildErrorShader(void *face, void *, void **out, void *definition, uint32_t context, const Blob12 *data,
                                                          int32_t reason)
{
    cMaterialParser::FromMaterialFace(face)->BuildErrorShader(out, definition, context, data, reason);
}

} // namespace nGZSceneGraph
