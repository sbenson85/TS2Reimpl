// nGZSceneGraph::cLODLevelsTable, its LOD command and cLODLevelsStore (lane QS): see sg/cLODLevels.h.
#include "sg/cLODLevels.h"

#include "game/game.h"
#include "game/owned.h"
#include "game/original.h"
#include "game/vtable.h"
#include "misc/SmallVirtuals.h" // cBranchNodesIIDFilter's vtable 0xe7cf54
#include "gz/CommandParser.h"
#include "gz/cBaseParser.h"
#include "gz/cFileParser.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cGZPropertySet.h"  // gzany:: the value vtables
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "sg/cSceneGraphSystem.h"
#include "sg/cSkinExtension.h"

#include <algorithm>
#include <cstring>
#include <new>

// The table's own tables (owned statics: the image's in the hooked game, cLODLevelsTable::kOwn* in an image-free build).
TS2_OWNED_STATIC(const OwnedVTable, e767b8);  // kVTable
TS2_OWNED_STATIC(const OwnedVTable, e767d0);  // kUnknownVTable
TS2_OWNED_STATIC(const OwnedVTable, e767e0);  // kParserVTable
TS2_OWNED_STATIC(const int32_t, e768ac)[3];   // kVbTable
TS2_OWNED_STATIC(const int32_t, e768b8)[2];   // kParserFaceVbTable
TS2_OWNED_STATIC(const OwnedVTable, e76914);  // cLODCommand::kVTable (ours: cLODLevelsTables.cpp)
TS2_OWNED_STATIC(const char, e6f73c)[];       // cLODCommand::kName, "LOD"

namespace nGZSceneGraph {

using nGZCommandParser::cBaseParser;

namespace {

// The cIGZUnknown slots of a face.
cIGZUnknown *Unknown(void *face) { return static_cast<cIGZUnknown *>(face); }

// The argument list's slots the command uses (cIArguments: 15 prepares, 11 gives `count` argument texts).
using ArgsVoidFn = void(__thiscall *)(void *);
using ArgsGetFn = const char *const *(__thiscall *)(void *, uint32_t);
using EvalIntFn = int32_t(__thiscall *)(void *, const char *); // cIBaseParser slot 32
using RegisterCommandFn = void(__thiscall *)(void *, void *);  // cIBaseParser slot 4
using ParserShutdownFn = bool(__thiscall *)(void *);           // cIBaseParser slot 1

} // namespace

// ---- cLODLevelsTable -------------------------------------------------------------------------------------------------

cLODLevelsTable *cLODLevelsTable::Construct(uint32_t mostDerived)
{
    if (mostDerived) {
        *reinterpret_cast<const void **>(mBase + 8) = ts2_rva_e768ac;
        mParserFaceVbTable = ts2_rva_e768b8;
        mUnknownFaceVTable = &ts2_rva_d9b428;
        mParserFaceVTable = &ts2_rva_db574c;
        // cIBaseParser's view of its cIGZUnknown, through the face's own vbptr.
        char *const faceVbptr = reinterpret_cast<char *>(&mParserFaceVbTable);
        *reinterpret_cast<const void **>(faceVbptr + mParserFaceVbTable[1]) = &ts2_rva_db581c;
    }
    Parser()->Construct(0);
    *reinterpret_cast<const void **>(mBase) = &ts2_rva_e767b8;
    *static_cast<const void **>(UnknownFace()) = &ts2_rva_e767d0;
    *static_cast<const void **>(ParserFace()) = &ts2_rva_e767e0;
    *reinterpret_cast<int32_t *>(static_cast<char *>(UnknownFace()) - 4) = 0;
    *reinterpret_cast<int32_t *>(static_cast<char *>(ParserFace()) - 4) = 0;
    ::new (&mLevels) std::vector<LODLevel>();
    return this;
}

void *__cdecl cLODLevelsTable::Create()
{
    void *const memory = ::operator new(kSize);
    cLODLevelsTable *const table = memory ? static_cast<cLODLevelsTable *>(memory)->Construct(1) : nullptr;
    return table ? table->UnknownFace() : nullptr;
}

cLODLevelsTable *cLODLevelsTable::DeletingDestructor(uint32_t flags)
{
    mLevels.~vector();
    Parser()->Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

bool cLODLevelsTable::Initialise(uint8_t caseSensitive)
{
    Parser()->Initialise(caseSensitive);
    void *const memory = ::operator new(sizeof(nGZCommandParser::cCommandT));
    nGZCommandParser::cCommand *command = nullptr;
    if (memory) {
        command = static_cast<nGZCommandParser::cCommand *>(memory)->Construct();
        *static_cast<const void **>(memory) = &ts2_rva_e76914;
    }
    VirtualCall<RegisterCommandFn, 4>(ParserFace(), static_cast<void *>(command));
    return true;
}

bool cLODLevelsTable::Shutdown()
{
    Parser()->Shutdown();
    mLevels.clear();
    return true;
}

void cLODLevelsTable::Reset()
{
    Parser()->Reset();
    mLevels.clear();
}

// ---- cLODCommand ---------------------------------------------------------------------------------------------------

void cLODCommand::Execute(nGZCommandParser::cIArguments *args)
{
    VirtualCall<ArgsVoidFn, 15>(args);
    const char *const *const argv = VirtualCall<ArgsGetFn, 11>(args, 2u);
    const int32_t size = VirtualCall<EvalIntFn, 32>(mTable->ParserFace(), argv[1]);
    const int32_t level = VirtualCall<EvalIntFn, 32>(mTable->ParserFace(), argv[0]);
    mTable->mLevels.push_back(LODLevel{size, std::min(level, cLODLevelsTable::kLevelLimit)});
}

const char *cLODCommand::GetName() const { return ts2_rva_e6f73c; }

// ---- the table's hook entries --------------------------------------------------------------------------------------

cLODLevelsTable *__fastcall cLODLevelsTableAdapters::Construct(cLODLevelsTable *self, void *, uint32_t mostDerived)
{
    return self->Construct(mostDerived);
}
cLODLevelsTable *__fastcall cLODLevelsTableAdapters::DeletingDestructor(cLODLevelsTable *self, void *, uint32_t flags)
{
    return self->DeletingDestructor(flags);
}
bool __fastcall cLODLevelsTableAdapters::Initialise(void *face, void *, uint8_t caseSensitive)
{
    return cLODLevelsTable::FromParserFace(face)->Initialise(caseSensitive);
}
bool __fastcall cLODLevelsTableAdapters::Shutdown(void *face, void *) { return cLODLevelsTable::FromParserFace(face)->Shutdown(); }
void __fastcall cLODLevelsTableAdapters::Reset(void *face, void *) { cLODLevelsTable::FromParserFace(face)->Reset(); }
void __fastcall cLODLevelsTableAdapters::NoOp(void *, void *) {}
int32_t __fastcall cLODLevelsTableAdapters::GetVersion(void *face, void *) { return cBaseParser::FromParserFace(face)->mVersion; }
void __fastcall cLODLevelsTableAdapters::Execute(cLODCommand *self, void *, nGZCommandParser::cIArguments *args) { self->Execute(args); }
const char *__fastcall cLODLevelsTableAdapters::GetName(cLODCommand *self, void *) { return self->GetName(); }

// ---- cLODLevelsStore -----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kSettingsClass = 0x0ac1887e;
constexpr uint32_t kSettingsIid = 0x6bb22f38;
constexpr uint32_t kMessageIid = 0x65297976;     // cIGZMessage2Standard
constexpr uint32_t kSceneGraphService = 0xe5074b6b;
constexpr uint32_t kShapeNodeIid = 0xa516cd10;   // the nodes the first walk takes
constexpr uint32_t kExtensionNodeIid = 0x7d14eb66; // the nodes the second walk takes

// The settings service (the framework's 0x0ac1887e / 0x6bb22f38), a counted local.
ScopedRef Settings()
{
    ScopedRef settings{nullptr};
    if (void *const framework = GetServiceObject())
        static_cast<cIGZFrameWork *>(framework)->GetSystemService(kSettingsClass, kSettingsIid, &settings.mObject);
    return settings;
}

// The settings service's typed default values (cAnyBoolean, cAnyUint32): the destructor stores the base's vtable.
template <class T> struct AnyValue {
    const void *mVTable;
    T mValue;
    AnyValue(const void *vtable, T value) : mVTable(vtable), mValue(value) {}
    AnyValue(const AnyValue &) = delete;
    AnyValue &operator=(const AnyValue &) = delete;
    ~AnyValue() { *static_cast<const void *volatile *>(&mVTable) = gzany::AnyVTable(); }
};

// The three counted iterators a range is made of (the cursor, the begin, the end), released end first.
struct NodeRange {
    cISceneNodeIterator *mCursor = nullptr;
    cISceneNodeIterator *mBegin = nullptr;
    cISceneNodeIterator *mEnd = nullptr;
    NodeRange() = default;
    NodeRange(const NodeRange &) = delete;
    NodeRange &operator=(const NodeRange &) = delete;
    ~NodeRange()
    {
        if (mEnd) mEnd->Release();
        if (mBegin) mBegin->Release();
        if (mCursor) mCursor->Release();
    }
    bool AtEnd() { return mCursor->IsEqual(mEnd); }
};
static_assert(sizeof(NodeRange) == sizeof(cSceneNodeRange), "NodeRange: a cSceneNodeRange");

// The filter by interface id a node's child range (slot 44) takes: an nRZSceneGraph::cBranchNodesIIDFilter (vtable
// 0xe7cf54, built inline: 0xaea7a0 / 0xaea7e0).
struct TypeFilter {
    const void *mVTable;
    uint32_t mIid;
    explicit TypeFilter(uint32_t iid) : mVTable(&ts2_rva_e7cf54), mIid(iid) {}
    TypeFilter(const TypeFilter &) = delete;
    TypeFilter &operator=(const TypeFilter &) = delete;
    ~TypeFilter() { *static_cast<const void *volatile *>(&mVTable) = &ts2_rva_e7cf54; }
};

// What a shape node's slot 93 fills: a cursor and an end three words apart (the middle word unused here; not released).
struct ChildList {
    void **mCursor;
    void *mMiddle;
    void **mEnd;
};

// 0xaea060 (ours, src/sg/cSkinExtension.cpp): a node's skin extension after `previous` (its first when null). The walk's
// node pointers are the nodes' cIObjectGraphNode faces (the search is their slot 24).
void *NextExtension(void *node, void *previous)
{
    return NextSkinExtension(static_cast<cIObjectGraphNode *>(node), static_cast<cISkinExtension *>(previous));
}

void ReleaseTable(cLODLevelsTable *table) { Unknown(table->UnknownFace())->Release(); }

} // namespace

cILODLevelsStore *__cdecl cLODLevelsStore::Create() { return new (std::nothrow) cLODLevelsStore; }

cLODLevelsStore::~cLODLevelsStore()
{
    if (mTable) ReleaseTable(mTable);
    if (mFileParser) mFileParser->Release();
}

bool cLODLevelsStore::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIID) return false;
    *out = static_cast<cILODLevelsStore *>(this);
    AddRef();
    return true;
}

uint32_t cLODLevelsStore::AddRef() { return ++mRefCount; }

uint32_t cLODLevelsStore::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIGZMessageTarget2 *cLODLevelsStore::GetMessageTarget() { return this; }
cIGZMessageTarget2 *cLODLevelsStore::GetMessageTarget2() { return this; }

bool cLODLevelsStore::Init()
{
    ScopedRef settings = Settings();
    cIGZMessageTarget2 *const target = this;
    {
        GZServiceString key("useLODs");
        AnyValue<bool> value(gzany::BooleanVTable(), false);
        // ORIGINAL-BUG: the settings service is used without a null check.
        RawSlot<4>(settings.mObject, target, kUseLODsSetting, &key, &value);
    }
    {
        GZServiceString key("lodOverride");
        AnyValue<uint32_t> value(gzany::Uint32VTable(), 0);
        RawSlot<4>(settings.mObject, target, kOverrideSetting, &key, &value);
    }

    void *const memory = ::operator new(cLODLevelsTable::kSize);
    cLODLevelsTable *const table = memory ? static_cast<cLODLevelsTable *>(memory)->Construct(1) : nullptr;
    if (table != mTable) {
        cLODLevelsTable *const old = mTable;
        if (table) Unknown(table->UnknownFace())->AddRef();
        mTable = table;
        if (old) ReleaseTable(old);
    }

    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (nGZCommandParser::cIFileParser *const old = mFileParser) {
        mFileParser = nullptr;
        old->Release();
    }
    com->GetClassObject(nGZCommandParser::cFileParser::kClassId, nGZCommandParser::cFileParser::kInterfaceId,
                        reinterpret_cast<void **>(&mFileParser));
    mFileParser->Init(); // no null check in the original
    mFileParser->SetParser(mTable ? static_cast<nGZCommandParser::cIBaseParser *>(mTable->ParserFace()) : nullptr, 1);
    settings.ReleaseObject();
    return true;
}

bool cLODLevelsStore::Shutdown()
{
    if (mFileParser) {
        mFileParser->Shutdown();
        if (nGZCommandParser::cIFileParser *const parser = mFileParser) {
            mFileParser = nullptr;
            parser->Release();
        }
    }
    if (mTable) {
        VirtualCall<ParserShutdownFn, 1>(mTable->ParserFace());
        if (cLODLevelsTable *const table = mTable) {
            mTable = nullptr;
            ReleaseTable(table);
        }
    }
    ScopedRef settings = Settings();
    cIGZMessageTarget2 *const target = this;
    {
        GZServiceString key("useLODs");
        RawSlot<5>(settings.mObject, target, kUseLODsSetting, &key);
    }
    {
        GZServiceString key("lodOverride");
        RawSlot<5>(settings.mObject, target, kOverrideSetting, &key);
    }
    settings.ReleaseObject();
    return true;
}

void cLODLevelsStore::SortLevels()
{
    std::vector<LODLevel> &levels = mTable->mLevels;
    std::sort(levels.begin(), levels.end(), [](const LODLevel &a, const LODLevel &b) { return a.size < b.size; });
}

void cLODLevelsStore::LoadFile(cIGZString *path)
{
    mFileParser->AddFile(path->ToChar(), 5);
    mFileParser->RunFiles();
    SortLevels();
}

void cLODLevelsStore::Update()
{
    if (!mFileParser->IsDirty()) return;
    mFileParser->RunFiles();
    SortLevels();
}

int32_t cLODLevelsStore::GetLevel(float width, float height)
{
    const int32_t a = static_cast<int32_t>(width);
    const int32_t b = static_cast<int32_t>(height);
    const int32_t key = a < b ? b : a;
    const LODLevel *const first = mTable->mLevels.data();
    const LODLevel *const last = first + mTable->mLevels.size();
    const LODLevel *const found =
        std::lower_bound(first, last, key, [](const LODLevel &level, int32_t k) { return level.size < k; });
    // ORIGINAL-BUG: past the largest size the last row answers; with no rows that reads the word before the storage.
    if (found == last) return last[-1].level;
    return found->level;
}

bool cLODLevelsStore::IsEnabled() { return mUseLODs != 0 && !mTable->mLevels.empty(); }

int32_t cLODLevelsStore::GetOverride() { return mOverride; }

void cLODLevelsStore::Refresh()
{
    ScopedRef service{nullptr};
    if (void *const framework = GetServiceObject())
        static_cast<cIGZFrameWork *>(framework)->GetSystemService(kSceneGraphService, cISceneGraphSystem::kIid, &service.mObject);
    void *const system = service.mObject;
    service.ReleaseObject(); // the framework keeps the system alive
    if (!system) return;

    NodeRange nodes;
    *reinterpret_cast<cSceneNodeRange *>(&nodes) = static_cast<cISceneGraphSystem *>(system)->GetSceneNodes();
    while (!nodes.AtEnd()) {
        {
            TypeFilter filter(kShapeNodeIid);
            NodeRange children;
            RawSlot<44>(nodes.mCursor->Get(), &children, 0u, &filter); // the node's children of that interface
            while (!children.AtEnd()) {
                ChildList list;
                RawSlot<93>(children.mCursor->Get(), &list);
                for (; list.mCursor != list.mEnd; ++list.mCursor) RawSlot<44>(*list.mCursor);
                children.mCursor->Next();
            }
        }
        {
            TypeFilter filter(kExtensionNodeIid);
            NodeRange children;
            RawSlot<44>(nodes.mCursor->Get(), &children, 0u, &filter);
            while (!children.AtEnd()) {
                for (void *extension = NextExtension(children.mCursor->Get(), nullptr); extension;
                     extension = NextExtension(children.mCursor->Get(), extension)) {
                    const uint32_t count = RawSlot<15>(extension);
                    for (uint32_t i = 0; i < count; ++i)
                        if (void *const item = reinterpret_cast<void *>(RawSlot<5>(extension, i))) RawSlot<44>(item);
                }
                children.mCursor->Next();
            }
        }
        nodes.mCursor->Next();
    }
}

bool cLODLevelsStore::DoMessage(void *message)
{
    if (!message) return false;
    cIGZMessage2Standard *standard = nullptr;
    bool handled = false;
    if (static_cast<cIGZUnknown *>(message)->QueryInterface(kMessageIid, reinterpret_cast<void **>(&standard))) {
        const uint32_t type = standard->GetType();
        if (type == kOverrideSetting) {
            handled = true;
            if (standard && standard->HasData2()) {
                if (const uint32_t *const value = static_cast<const uint32_t *>(standard->GetVoid2())) {
                    mOverride = static_cast<int32_t>(value[1]);
                    if (mOverride == 0) {
                        mOverride = -1;
                        mUseLODs = 0;
                    } else {
                        mUseLODs = 1;
                    }
                    Refresh();
                }
            }
        } else if (type == kUseLODsSetting) {
            handled = true;
            if (standard && standard->HasData2()) {
                if (const uint8_t *const value = static_cast<const uint8_t *>(standard->GetVoid2())) {
                    const uint8_t use = value[4]; // stored as it came, not as a bool
                    if (use != mUseLODs) {
                        mUseLODs = use;
                        if (use) mOverride = cLODLevelsTable::kLevelLimit; // turned on: the finest level
                        Refresh();
                    }
                }
            }
        }
    }
    if (standard) standard->Release();
    return handled;
}

} // namespace nGZSceneGraph
