// nTSCASCore::cTSCASSystem (lanes IU, JI): the whole class in the standalone shape (src/cas/cTSCASSystem.h).
#include "cas/cTSCASSystem.h"
#include "sg/SGUtilities.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"            // gz::AnyBoolean
#include "gz/cIGZPropertySet.h"   // GZServiceString
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZSystemService.h"
#include "ifaces/cITSPersistSystem.h"
#include "misc/HexParse.h"        // hexparse::ParseGuidText
#include "rz/cRZIniFile.h"        // cRZIniStringHelpers::SplitOff
#include "rz/Singleton_20ddfb.h"  // GetServiceObject, GetCachedSingleton, ScopedRef
#include "misc/UtilitiesC.h"   // KeyMapSlot5Walker
#include "rz/MiscHelpersB.h"   // KeyMapWalker
#include "rz/RZCOMDllDirectorShape.h" // rz::cRZStringMember
#include "rz/RZSystemServiceBase.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"   // GetDirectorFrameworkObject
#include "ts/cTSSimDNA.h"      // nTSGenetics::DNAWeightedGene / DNAWeightedVector (StepChildJob's default gene)
#include "ts/cTSString.h"
#include "simantics/ServiceRefVector.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <new>

namespace nTSCASCore {

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

constexpr uint32_t kMessageIid = 0x65297976;   // the message's interface (slot 4: its id)
constexpr uint32_t kEnterCAS = 0xd9;
constexpr uint32_t kLeaveCASKeep = 0xda;       // leaving CAS with the edited sim: its outfits re-run
constexpr uint32_t kLeaveCAS = 0xdb;
constexpr uint32_t kRefreshUI = 0xb5;
constexpr uint32_t kContentChanged = 0x91a4f0d0;
constexpr uint32_t kUIRefreshKind = 0x1f;
constexpr uint32_t kOutfitResourceType = 0xac598eac; // ReloadOutfits' resource key type
constexpr uint32_t kOutfitResourceIid = 0x8ceec300;
constexpr uint32_t kOutfitIid = 0xac598ea8;          // PickRandomOutfit: an outfit's interface
constexpr uint32_t kPartIid = 0xebcf3e20;            // the part resource's interface (ApplyParts)
constexpr uint32_t kAllParts = 13;                   // ApplyParts: every body-part bit 1 << 0 .. 1 << 12
constexpr uint32_t kSkippedParts = 0xc80;            // ... but these
constexpr uint32_t kSettingsId = 0x0ac1887e;         // the settings service (the framework's slot 5)
constexpr uint32_t kSettingsIid = 0x4bb0d269;
constexpr uint32_t kBinNamesIid = 0xfb4cb0de;        // a bin's string resource (FindBinByName)
constexpr uint32_t kMeshIid = 0x0c1fe23e;            // a mesh overlay (FindMeshOverlay)
// FillNames: the sim info's slot 3 ids, the name string set's resource type and class, the name tables' resource type and
// group, the description's age id, the two name styles that format first names, and the count of names per kind.
constexpr uint32_t kInfoLanguage = 0x29, kInfoNameStyle = 0xcb, kInfoNamesFixed = 0x30;
constexpr uint32_t kNameSetType = 0x43545353, kNameSetClass = 0xdb4cb0f9;
constexpr uint32_t kNameTableType = 0x53545223, kNameTablesGroup = 0x7f96c284;
constexpr uint32_t kDescriptionAge = 0x41;
constexpr uint32_t kStyleA = 0x8ea1056c, kStyleB = 0xcf99cb2e;
constexpr uint32_t kNameCount = 44;

// A format text from the string tables (cTSString, group 0x7f96c284), destroyed at the end of its scope as cRZString's.
struct FormatText {
    union {
        cTSString value;
    };
    FormatText(uint16_t table, uint16_t entry, uint8_t flags)
    {
        value.Construct(table, entry, kNameTablesGroup, flags, cTSString::kStringTableType);
    }
    ~FormatText() { value.Destruct(); }
    FormatText(const FormatText &) = delete;
    FormatText &operator=(const FormatText &) = delete;
};

// The services CAS asks (each a cached getter of the image's shape).
void *CASUIService() { return CachedServiceGetter<0x10faf70, 0xeb7d6513, 0xcb7d64f8>(); }
void *ResourceService() { return CachedServiceGetter<0x10faf74, 0x4bcf1bb0, 0x0bcf1b58>(); }
void *PersistSystem() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }
void *BinCatalog() { return CachedServiceGetter<0x10faf78, 0x0c4f252f, 0x2c4f2954>(); }
void *SkinService() { return CachedServiceGetter<0x10faf80, 0x0dc7057a, 0xcdb467ae>(); }

// Game code nobody has replaced yet.
// 0x267350 (__cdecl): an age index's bit (0..3: 1, 2, 4, 8; others 1).
uint32_t AgeBit(int32_t age) { return OriginalFunction<uint32_t(__cdecl *)(int32_t), 0x267350>()(age); }
// 0x20df9a (__cdecl, game code): the app's options object (cell 0x10cc2ac; its slots 15 / 16 get / set an option word).
void *AppOptions() { return OriginalFunction<void *(__cdecl *)(), 0x20df9a>()(); }
// 0x54d600 (__cdecl): whether the key names custom content.
bool IsCustomKey(const SkinKey *key) { return OriginalFunction<bool(__cdecl *)(const SkinKey *), 0x54d600>()(key); }
// 0x4f8e50 (__cdecl): the static zero key.
const SkinKey *ZeroKey() { return OriginalFunction<const SkinKey *(__cdecl *)(), 0x4f8e50>()(); }

// 0x54a130 (__cdecl, game code): the sim object for an id (the app's object table, slot 26, its slot 43), uncounted.
cIGZUnknown *SimObject(uint32_t simId)
{
    return OriginalFunction<cIGZUnknown *(__cdecl *)(uint32_t), 0x54a130>()(simId);
}
// 0x54ac80 (__cdecl, game code): a sim object's outfit (resource {0xac598eac, its slot 7 / 61 / 67 / 11 group, 1, 0}, iid
// 0xac598ea8) loaded by the persist system into `out`.
bool SimOutfit(void *sim, void **out) { return OriginalFunction<bool(__cdecl *)(void *, void **), 0x54ac80>()(sim, out); }

// ShowFace's (and FindNamedPart's) pieces.
// 0x20df26 (__cdecl, game code): the cached app object whose slot 5 is the name-to-id tables (their slot 33(buffer, id)
// answers a pointer to the id).
void *NameIdTables() { return OriginalFunction<void *(__cdecl *)(), 0x20df26>()(); }
// 0xaeba70 (__cdecl, game code): a mesh resource's model (uncounted; null for none).
cIGZUnknown *ModelOf(void *mesh)
{
    return nGZSceneGraph::ShapeRefOfNodeOrSubtree(static_cast<nGZSceneGraph::cICompositionTreeNode *>(mesh));
}
constexpr uint32_t kFaceMeshIid = 0xe519c92c;        // a part's mesh resource
constexpr uint32_t kThumbnailType = 0xcccef852, kThumbnailIid = 0xaccef84c;
constexpr const char *kFaceName = "face";
// A sim's part keys (its slot 42(2, ...)): the vector's first two words.
struct SkinKeyList {
    const SkinKey *begin;
    const SkinKey *end;
};
// A model's parts (its slot 93 writes the range; not owned): the parts from `begin` to `end`.
struct PartRange {
    void *const *begin;
    void *unused;
    void *const *end;
};
// A name cursor (a name list's slot 42, a face's slot 54): three counted words; the cursor's slot 4(end) tests the end,
// slot 5 reads the entry, slot 6 steps. Released end first.
struct NameCursor {
    CASRef cursor;
    CASRef owner;
    CASRef end;
};
static_assert(sizeof(NameCursor) == 12, "NameCursor");
// A face entry (the face cursor's slot 5 writes it): its name and id.
struct NameEntry {
    const char *name;
    uint32_t id;
};

// FindFaceOverlay's (slot 38) pieces.
constexpr uint32_t kFaceOverlayCheck = 2;                     // its flags' bit 1
constexpr uint32_t kOverlayKinds[] = {1, 4, 0x10, 8, 0x20};   // the kinds its other flags walk, in order
constexpr uint32_t kTextureOverlayIid = 0x2c1fd89d;           // an overlay's texture face (its mesh face: kMeshIid)
constexpr uint32_t kOverlayAnyIndex = 0xffff;                 // the resource service's slot 20's last argument (0 before it)
constexpr const char *kOverlayCountName = "cafNumOverlays";
constexpr const char *kOverlayName = "cafOverlay";            // "%s%d" with the index
// The name tables' slot 33 entry (0x14 bytes, written by the call): ShowFace reads the id the call answers, slot 38 the
// properties at +4 (slot 24(name): a setting's text, or null).
struct NameIdEntry {
    uint32_t id;
    void *properties;
    uint32_t rest[3];
};
static_assert(sizeof(NameIdEntry) == 0x14, "NameIdEntry");
// A cRZString made from a text (as the original's inlined construction: refcount 0), destroyed at the end of its scope.
struct TextString {
    union {
        cRZString value;
    };
    explicit TextString(const char *text) { value.Construct(text); }
    ~TextString() { value.Destruct(); }
    TextString(const TextString &) = delete;
    TextString &operator=(const TextString &) = delete;
};
// A cRZString a callee constructs (SplitOff's out): raw memory until then.
struct RawString {
    union {
        cRZString value;
    };
    RawString() {}
    ~RawString() { value.Destruct(); }
    RawString(const RawString &) = delete;
    RawString &operator=(const RawString &) = delete;
};

// ApplyMeshOverlay's (slot 35) pieces.
constexpr uint32_t kOverlayKind = 0x20;      // the sim's key lists of mesh overlays (slots 42 / 72 / 82)
constexpr uint32_t kMeshOverlayType = 8;     // a mesh's slot 4 for an overlay
using OverlayKeys = std::vector<SkinKey, GZServiceAllocator<SkinKey>>;
// The sim's overlay keys for a part bit (slot 42(0x20, bit)) whose mesh loads (persist slot 5, kMeshIid) and `differs`.
template <class Differs> void KeepOverlays(void *sim, uint32_t bit, OverlayKeys &kept, Differs differs)
{
    const SkinKeyList *const list = RawSlot<42, const SkinKeyList *>(sim, kOverlayKind, bit);
    if (!list) return;
    for (const SkinKey *k = list->begin; k != list->end; ++k) {
        CASRef mesh;
        As<ifaces::cITSPersistSystem>(PersistSystem())->GetResource(reinterpret_cast<const Key16 *>(k), kMeshIid, mesh.AsPPVoid(), 0u, 0u);
        if (mesh && differs(mesh.Get())) kept.push_back(*k);
    }
}
// The sim's overlay keys for a part bit replaced: slot 82(0x20, bit), then slot 72(0x20, bit, &keys).
void StoreOverlays(void *sim, uint32_t bit, OverlayKeys *keys)
{
    RawSlot<82, void>(sim, kOverlayKind, bit);
    RawSlot<72, void>(sim, kOverlayKind, bit, keys);
}

// ApplySpecialOutfit's options object:the class 0xfa1c39f7 a sim's factory makes (iid 0x1a34f3f4).
constexpr uint32_t kOptionsClass = 0xfa1c39f7, kOptionsIid = 0x1a34f3f4;
// A counted pointer to an object whose cIGZUnknown is a virtual base: released through the base (the vbtable pointer at +4,
// the base's offset from it in the vbtable's second entry), as the original's inlined holder does.
struct VirtualBaseRef {
    void *object = nullptr;
    static void Release(void *p)
    {
        uint8_t *const vbptr = static_cast<uint8_t *>(p) + 4;
        RawSlot<2, void>(vbptr + (*reinterpret_cast<const int32_t *const *>(vbptr))[1]);
    }
    VirtualBaseRef() = default;
    VirtualBaseRef(const VirtualBaseRef &) = delete;
    VirtualBaseRef &operator=(const VirtualBaseRef &) = delete;
    ~VirtualBaseRef()
    {
        if (object) Release(object);
    }
    void **AsPPVoid()
    {
        if (object) {
            void *const old = object;
            object = nullptr;
            Release(old);
        }
        return &object;
    }
    void *Get() const { return object; }
};

bool KeysDiffer(const SkinKey *a, const SkinKey *b) { return std::memcmp(a, b, sizeof(SkinKey)) != 0; }

// A sim description's id (its slot 8, a short, widened as the original does).
uint32_t SimId(const CASRef &sim) { return static_cast<uint32_t>(static_cast<int32_t>(RawSlot<8, int16_t>(sim.Get()))); }

// RefreshContent's (slot 14) pieces.
constexpr uint32_t kContentIid = 0x3b2271bb;       // the content resource (the persist system's slot 34)
constexpr uint32_t kPartType = 0xebcf3e27;         // a skin part's resource type (loaded with kPartIid)
constexpr uint32_t kRegisteredIid = 0x8c1571d6;    // what the resource service's slot 8 takes
constexpr uint32_t kOutfitRecordType = 0x0c560f39; // the persist system's slot 3's slot 19 lists these
constexpr uint32_t kOutfitRecordIid = 0x0c560f33;  // ... loaded with this (their slot 21: the outfit's key)

// FixOutfitParts' (slot 9) and FixOutfitPart's pieces: game code nobody has replaced yet.
// 0x549f80 (__cdecl): the object a sim id names (the app singleton's slot 10's slot 17), uncounted; null for none.
cIGZUnknown *ObjectForId(uint32_t simId)
{
    return OriginalFunction<cIGZUnknown *(__cdecl *)(uint32_t), 0x549f80>()(simId);
}
// 0x54b250 (__cdecl): the object's data 0xcb (its slot 10's slot 4's slot 3) is 0xd07b9ce9 and the app's option bit 3 is set.
bool OutfitFixed(cIGZUnknown *object) { return OriginalFunction<bool(__cdecl *)(cIGZUnknown *), 0x54b250>()(object); }
// 0x54a990 (__cdecl): the object's outfit resource ({0xac598eac, its slot 10's slot 67's slot 11, 1, 0}, iid 0xac598ea8)
// loaded by the persist system into `out` (counted).
bool ObjectOutfit(cIGZUnknown *object, void **out)
{
    return OriginalFunction<bool(__cdecl *)(cIGZUnknown *, void **), 0x54a990>()(object, out);
}
// 0x20e031 (__cdecl): the cached dialog service.
void *DialogService() { return OriginalFunction<void *(__cdecl *)(), 0x20e031>()(); }
// nTSCASCore::cTSOutfitMatchSkinFilter (0x80 bytes, game code; constructor 0xb64d90(outfit, kind, bit), thiscall): a skin
// filter for an outfit's part; its cITSSkinFilter face at +0x54 (what the resource service's slots 12 / 38 take), a byte at
// +0x7c its constructor clears.
constexpr size_t kMatchFilterSize = 0x80, kMatchFilterFace = 0x54, kMatchFilterAnyByte = 0x7c;
cIGZUnknown *NewOutfitMatchFilter(void *outfit, uint32_t kind, uint32_t bit)
{
    void *const memory = ::operator new(kMatchFilterSize); // the game's operator new (0x824632) is the library's
    return OriginalFunction<cIGZUnknown *(__thiscall *)(void *, void *, uint32_t, uint32_t), 0xb64d90>()(memory, outfit, kind, bit);
}
constexpr const char *kGuidKind10 = "00000005-0000-0000-0000-000000000000"; // an outfit of kind 0x10 (its slot 22)
constexpr const char *kGuidOther = "00000001-0000-0000-0000-000000000000";
constexpr uint32_t kRandomPartBit = 0x100;              // its outfit drawn at random (slot 39) as well
constexpr uint32_t kRandomPartDefinition = 0x6dd1d04b; // the object definition slot 39 draws it from
constexpr uint32_t kOutfitDialogId = 0x20732180;       // the dialog service's slot 15 id after bit 1's part changed

} // namespace

// ---- construction ----

// 0x4ff850 (884 B): the service base, the containers in member order, the generator seeded from the clock, the five message
// ids, then the flags. +0x26, +0xc4..+0xcf and +0x13c are not written.
cTSCASSystem::cTSCASSystem() : rz::cRZSystemService(kServiceId, kPriority)
{
    mRandom.ConstructAt(0xffffffff, 0x4ff9e3); // the original's call returns to 0x4ff9e3
    for (uint32_t id : {kEnterCAS, kLeaveCASKeep, kLeaveCAS, kRefreshUI, kContentChanged}) mMessageIds.insert(id);
    mCachesFilled = false;
    mCacheTypes = true;
    mEditedSimId = 0xffffffff;
    mFreeWill = true; // one dword store in the original: +0x139..+0x13b zero
    mInCASMode = false;
    mSavedOption21 = 0;
    mObjectDetail = 3;
    mReflection = 0;
}

// 0x5006d0 (deleting, through the +4 thunk 0x50067a): the members in reverse order, then the service.
cTSCASSystem::~cTSCASSystem() noexcept = default;

// 0x502860: new 0x140 bytes, the constructor.
cTSCASSystem *__cdecl cTSCASSystem::Create() { return new cTSCASSystem; }

cTSCASSystem *__fastcall cTSCASSystemAdapters::Construct(void *memory, void *) { return ::new (memory) cTSCASSystem; }

bool __fastcall cTSCASSystemAdapters::LoadResource(cTSCASSystem *self, void *, const SkinKey *key, uint32_t iid, void **out)
{
    return self->cTSCASSystem::LoadResource(key, iid, out);
}

// ---- cIGZUnknown ----

// 0x50a910: cITSCASSystem -> +0x1c, cIGZMessageTarget2 -> +0x20 (counted through the primary face's AddRef), else the
// service's QueryInterface.
bool cTSCASSystem::QueryInterface(uint32_t riid, void **out)
{
    void *face;
    if (riid == kIid) face = static_cast<cITSCASSystem *>(this);
    else if (riid == kMessageTargetIid) face = static_cast<cIGZMessageTarget2 *>(this);
    else return rz::cRZSystemService::QueryInterface(riid, out);
    *out = face;
    AddRef();
    return true;
}

uint32_t cTSCASSystem::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSCASSystem::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService ----

// 0x5049d0: the generator seeded from the clock, a listener for each message id, then the framework's slot 9 gets the
// system. The base's Init (shared 0x144910) answers true without side effects: the original's early return is dead.
bool cTSCASSystem::Init()
{
    mRandom.Seed(ClockSeedAt(0x5049ef)); // clock(), as the original (seed control keys it by that site)
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(target), nullptr);
    // The framework, cached in a global the first time (as the original does).
    void *&framework = *GameAddress<void *>(0x10f5194);
    if (!framework) framework = GetServiceObject();
    As<ifaces::cIGZFrameWork>(framework)->AddToTick(As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
    return true;
}

// 0x50bb10: the job queues emptied, the listeners dropped, the edited sim released and its outfits forgotten, the caches
// reset (slot 17).
bool cTSCASSystem::Shutdown()
{
    mNewSims.clear();
    mChildren.clear();
    mOutfitJobs.clear();
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(target), nullptr);
    mEditedSim.Assign(nullptr);
    mEditedOutfits.clear();
    ResetCaches();
    return rz::cRZSystemService::Shutdown(); // the base's (shared): true
}

// 0x506720 (744 B): one step of the first queue that has a job (children, then new sims, then outfits). A finished child
// or new sim queues its outfit job (or files false for its id when it failed); a finished outfit job files its outcome.
bool cTSCASSystem::OnTick(uint32_t counter)
{
    if (!mChildren.empty()) {
        ChildJob &job = mChildren.front();
        if (!StepChildJob(&job)) {
            if (job.done) {
                OutfitJob next(RawSlot<28, uint32_t>(job.ref18.Get()), 3); // its slot 28 (no arguments): the source
                next.sim = job.sim;
                next.ref24 = job.ref1c;
                next.ref20 = job.ref18;
                mOutfitJobs.push_back(next);
            } else {
                mResults[SimId(job.sim)] = false;
            }
            mChildren.pop_front();
        }
    } else if (!mNewSims.empty()) {
        NewSimJob &job = mNewSims.front();
        if (!StepNewSimJob(&job)) {
            if (job.done) {
                OutfitJob next(RawSlot<28, uint32_t>(job.ref20.Get()), job.outfitKind);
                next.sim = job.sim;
                next.ref24 = job.ref24;
                next.ref20 = job.ref20;
                mOutfitJobs.push_back(next);
            } else {
                mResults[SimId(job.sim)] = false;
            }
            mNewSims.pop_front();
        }
    } else if (!mOutfitJobs.empty()) {
        OutfitJob &job = mOutfitJobs.front();
        if (!StepOutfitJob(&job)) {
            mResults[SimId(job.sim)] = job.result;
            mOutfitJobs.pop_front();
        }
    }
    return rz::cRZSystemService::OnTick(counter); // the base's (shared): true
}

// ---- cIGZMessageTarget2 ----

// 0x5015c0: entering CAS fills the type caches, refreshes the UI and enters CAS mode; leaving it (keeping the edited sim:
// its outfits re-run first and it is released; or dropping its id) clears the query and name caches and leaves CAS mode.
bool cTSCASSystem::DoMessage(void *message)
{
    if (!message) return false;
    CASRef m;
    if (!As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(kMessageIid, m.AsPPVoid())) return false;
    switch (As<ifaces::cIGZMessage2Standard>(m.Get())->GetType()) {
    case kEnterCAS:
        FillTypeCaches();
        RawSlot<3, void>(CASUIService(), kUIRefreshKind);
        EnterCASMode();
        break;
    case kRefreshUI:
        RawSlot<3, void>(CASUIService(), kUIRefreshKind);
        break;
    case kContentChanged:
        ResetCaches();
        break;
    case kLeaveCASKeep:
        for (void *outfit : mEditedOutfits) RunOutfitJob(Word(outfit), 3, nullptr, nullptr, 1);
        mEditedSim.Assign(nullptr);
        mQueries.clear();
        mNamedKeysA.clear();
        LeaveCASMode();
        break;
    case kLeaveCAS:
        mEditedSimId = 0xffffffff;
        mQueries.clear();
        mNamedKeysA.clear();
        LeaveCASMode();
        break;
    default:
        return false;
    }
    return true;
}

// ---- CAS mode ----

namespace {
constexpr uint32_t kCASOption = 0x21;        // the app options' word CAS mode marks
constexpr uint32_t kCASOptionBits = 6;
constexpr int32_t kCASObjectDetail = 3;      // the display settings CAS mode wants
constexpr int32_t kCASReflection = 1;
constexpr const char *kObjectDetail = "ObjectDetail";
constexpr const char *kReflection = "Reflection";
constexpr const char *kFreeWill = "freeWill";
// The settings service (the framework's slot 5), uncounted holder; null without a framework.
void QuerySettings(ScopedRef &settings)
{
    if (void *const services = GetServiceObject()) RawSlot<5, void>(services, kSettingsId, kSettingsIid, &settings.mObject);
}
// The settings' slot 3: `name` set to a boolean (its raw byte).
void SetSetting(void *settings, const char *name, uint8_t value)
{
    GZServiceString text(name);
    gz::AnyBoolean any(value);
    RawSlot<3, void>(settings, &text, static_cast<cIGZAny *>(&any));
}
// The app's slot 21: its display settings (slot 14: get(name, &value), 13: set(name, value)), or none.
void *DisplaySettings()
{
    void *const app = GetCachedSingleton();
    return app ? RawSlot<21, void *>(app) : nullptr;
}
} // namespace

// 0x50b960 (424 B). The settings are used without a null test, as the original does.
void cTSCASSystem::EnterCASMode()
{
    ScopedRef settings{nullptr};
    mInCASMode = true;
    QuerySettings(settings);
    {
        gz::AnyBoolean freeWill(0);
        {
            GZServiceString name(kFreeWill);
            RawSlot<9, bool>(settings.mObject, &name, static_cast<cIGZAny *>(&freeWill)); // found or not: the byte as it is
        }
        mFreeWill = freeWill.mValue;
    }
    if (mFreeWill) SetSetting(settings.mObject, kFreeWill, 0);
    void *const options = AppOptions();
    mSavedOption21 = RawSlot<15, uint16_t>(options, kCASOption);
    RawSlot<16, void>(options, kCASOption, uint32_t(mSavedOption21) | kCASOptionBits);
    if (void *const display = DisplaySettings()) {
        RawSlot<14, void>(display, kObjectDetail, &mObjectDetail);
        RawSlot<14, void>(display, kReflection, &mReflection);
        if (mObjectDetail != kCASObjectDetail) RawSlot<13, void>(display, kObjectDetail, kCASObjectDetail);
        if (mReflection != kCASReflection) RawSlot<13, void>(display, kReflection, kCASReflection);
    }
    settings.ReleaseObject();
}

// 0x50b830 (293 B).
void cTSCASSystem::LeaveCASMode()
{
    ScopedRef settings{nullptr};
    mInCASMode = false;
    QuerySettings(settings);
    if (mFreeWill) SetSetting(settings.mObject, kFreeWill, mFreeWill);
    RawSlot<16, void>(AppOptions(), kCASOption, uint32_t(mSavedOption21));
    if (void *const display = DisplaySettings()) {
        if (mObjectDetail != kCASObjectDetail) RawSlot<13, void>(display, kObjectDetail, mObjectDetail);
        if (mReflection != kCASReflection) RawSlot<13, void>(display, kReflection, mReflection);
    }
    settings.ReleaseObject();
}

// ---- cITSCASSystem: implemented slots ----

// 0x50b590 (442 B): holds the sim, forgets the outfits; with a sim: its id, each of its slot 14's objects handed to its slot 13,
// then the outfits of kind 3 that its slot 29 accepts are listed.
bool cTSCASSystem::SetEditedSim(void *sim)
{
    mEditedSim.Assign(static_cast<cIGZUnknown *>(sim));
    mEditedOutfits.clear();
    if (!mEditedSim) return false;
    mEditedSimId = RawSlot<41, uint32_t>(mEditedSim.Get());
    ServiceRefVector objects{};
    RawSlot<14, void>(mEditedSim.Get(), &objects);
    for (void **it = static_cast<void **>(objects.mFirst); it != static_cast<void **>(objects.mLast); ++it)
        RawSlot<13, void>(mEditedSim.Get(), *it);
    const uint32_t count = RawSlot<24, uint32_t>(mEditedSim.Get(), 3u);
    for (uint32_t i = 0; i < count; ++i) {
        void *const outfit = RawSlot<27, void *>(mEditedSim.Get(), i);
        if (RawSlot<29, bool>(mEditedSim.Get(), outfit)) mEditedOutfits.push_back(outfit);
    }
    objects.ReleaseAndFree();
    return true;
}

// 0x50b750 (211 B): an outfit job worked to the end at once; its +0x24 object gets slot 112; the job's outcome.
bool cTSCASSystem::RunOutfitJob(uint32_t source, uint32_t kind, const SkinKey *keyA, const SkinKey *keyB, uint32_t flag)
{
    OutfitJob job(source, kind);
    job.hasKeyA = keyA != nullptr;
    if (keyA) job.keyA = *keyA;
    job.hasKeyB = keyB != nullptr;
    if (keyB) job.keyB = *keyB;
    job.reloadRecord = static_cast<uint8_t>(flag);
    while (StepOutfitJob(&job)) {
    }
    if (job.ref24) RawSlot<112, void>(job.ref24.Get());
    return job.result;
}

// 0x5008b0: no job queued.
bool cTSCASSystem::IsIdle() { return mChildren.empty() && mNewSims.empty() && mOutfitJobs.empty(); }

// 0x50b3d0 (437 B): a finished job's outcome by sim id (false and *out false when none was filed).
bool cTSCASSystem::GetResult(uint32_t simId, bool *out)
{
    *out = false;
    const auto it = mResults.find(simId);
    if (it == mResults.end()) return false;
    *out = it->second;
    return true;
}

// 0x504aa0 (522 B): cached by the key's group. The skin service answers first (its slot 13: a short, zero meaning "not the
// default"); else the resource's 0x8c1571d6 face (its slot 16 key) or 0xac598ea8 face (its slot 17 key; both without
// arguments) differs from the zero key (0x4f8e50, read before the slot is called).
bool cTSCASSystem::IsNotDefault(const SkinKey *key)
{
    const uint32_t group = key->group;
    const auto it = mNotDefault.find(group);
    if (it != mNotDefault.end()) return it->second;
    bool value = false;
    int16_t answer;
    if (RawSlot<13, bool>(SkinService(), key, &answer)) {
        value = answer == 0;
    } else {
        CASRef resource;
        if (As<ifaces::cITSPersistSystem>(PersistSystem())->GetResource(reinterpret_cast<const Key16 *>(key), 0x456b8f1du, resource.AsPPVoid(), 0u, 0u)) {
            CASRef keyed;
            CASRef mapped;
            if (RawSlot<0, bool>(resource.Get(), 0x8c1571d6u, keyed.AsPPVoid())) {
                const SkinKey *const zero = ZeroKey();
                value = KeysDiffer(RawSlot<16, const SkinKey *>(keyed.Get()), zero);
            } else if (RawSlot<0, bool>(resource.Get(), 0xac598ea8u, mapped.AsPPVoid())) {
                const SkinKey *const zero = ZeroKey();
                value = KeysDiffer(RawSlot<17, const SkinKey *>(mapped.Get()), zero);
            }
        }
    }
    mNotDefault.emplace(key->group, value);
    return value;
}

// 0x500cd0: custom content, or not the default.
bool cTSCASSystem::IsCustomOrNotDefault(const SkinKey *key) { return IsCustomKey(key) || IsNotDefault(key); }

// 0x50a980: custom content the bin catalog's slot 46 accepts; else, when not the default, slot 14.
bool cTSCASSystem::Slot16(const SkinKey *key)
{
    if (IsCustomKey(key)) {
        if (!IsCustomKey(key)) return false;
        void *const catalog = BinCatalog();
        return catalog && RawSlot<46, bool>(catalog, key);
    }
    if (!IsNotDefault(key)) return false;
    return RefreshContent(key);
}

// 0x500d00: the type caches marked unfilled; the query, type and first name caches emptied (not the second).
bool cTSCASSystem::ResetCaches()
{
    mCachesFilled = false;
    mQueries.clear();
    mTypes.clear();
    mNamedKeysA.clear();
    return true;
}

// 0x50a970 -> 0x502dc0 (128 B): the resource service's slot 37, else the persist system's slot 5 (cached in 0x10dac1c).
bool cTSCASSystem::LoadResource(const SkinKey *key, uint32_t iid, void **out)
{
    if (RawSlot<37, bool>(ResourceService(), key, iid, out)) return true;
    void *&persist = *GameAddress<void *>(0x10dac1c);
    if (!persist) persist = PersistSystem();
    return RawSlot<5, bool>(persist, key, iid, out, 0u, 0u);
}

// 0x501d20 (164 B): the CAS UI's slot 84 gets the age (first matching bit: 1, 2, 4, 0x40, 8, 0x10) and gender (1, 2) modes
// after slot 36 shows the sim, then a refresh.
bool cTSCASSystem::Slot37(uint32_t a1, void *sim, uint32_t ages, uint32_t genders)
{
    void *const ui = CASUIService();
    uint32_t age = 3;
    if (ages & 1) age = 0;
    else if (ages & 2) age = 1;
    else if (ages & 4) age = 2;
    else if (ages & 0x40) age = 7;
    else if (ages & 8) age = 3;
    else if (ages & 0x10) age = 4;
    uint32_t gender = 2;
    if (genders & 1) gender = 1;
    else if (genders & 2) gender = 0;
    ShowFace(sim, 1);
    RawSlot<84, void>(ui, a1, age, gender);
    RawSlot<3, void>(ui, kUIRefreshKind);
    return true;
}

// 0x500d30 (280 B): each of the sim's kind-3 outfits (its slot 24(3) count, slot 27(i) each: an id) looked up in the
// app's neighbour manager (the app's slot 10, its slot 17); a found neighbour's part (slot 10, slot 67, slot 11: the
// instance) names the resource {0xac598eac, instance, 1, 0}, which the persist system loads (its slot 34, iid 0x8ceec300)
// and reloads (slot 7).
void cTSCASSystem::ReloadOutfits(void *sim)
{
    void *const app = GetCachedSingleton();
    void *const persist = PersistSystem();
    void *const neighbors = // receiver: cTSNeighborManager
        RawSlot<10, void *>(app);
    if (!persist || !neighbors) return;
    const int32_t count = RawSlot<24, int32_t>(sim, 3u);
    for (int32_t i = 0; i < count; ++i) {
        void *const neighbor = RawSlot<17, void *>(neighbors, RawSlot<27, uint32_t>(sim, i));
        if (!neighbor) continue;
        void *const part = RawSlot<10, void *>(neighbor); // receiver: cTSNeighbor (0x4b8170, ret 0)
        if (!part) continue;
        const SkinKey key{kOutfitResourceType, RawSlot<11, uint32_t>(RawSlot<67, void *>(part)), 1, 0};
        CASRef resource;
        As<ifaces::cITSPersistSystem>(persist)->FindSegment(reinterpret_cast<const cGZPersistResourceKey *>(&key), kOutfitResourceIid, resource.AsPPVoid());
        if (resource) RawSlot<7, void>(resource.Get());
    }
}

// 0x504910 (190 B): slot 23 without a bin, into a vector of its own.
bool cTSCASSystem::HasFragments(uint32_t kind, int32_t a4, uint32_t a8, uint32_t a9, const SkinKey *keyA,
                                const SkinKey *keyB, uint32_t a7)
{
    CASItemVector items;
    return QueryFragments(nullptr, &items, kind, a4, keyA, keyB, a7, a8, a9, 0xfffffffe, 0) != 0;
}

// 0x5039b0 (1122 B): the fragments of a kind for these arguments (slot 31), remembered by kind when no bin is given: an
// earlier query with the same arguments (the zero key third, the custom-content setting) answers from its copy. The
// count of fragments.
int32_t cTSCASSystem::QueryFragments(void *bin, CASItemVector *out, uint32_t kind, int32_t a4, const SkinKey *keyA,
                                     const SkinKey *keyB, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t)
{
    std::list<FragmentQuery> &queries = mQueries[kind];
    FragmentQuery query{};
    query.allowCustom = AllowCustomContent();
    query.bin.Assign(static_cast<cIGZUnknown *>(bin));
    query.kind = kind;
    query.arg4 = a4;
    query.keyA = keyA ? *keyA : *ZeroKey();
    query.keyB = keyB ? *keyB : *ZeroKey();
    query.args[0] = a7;
    query.args[1] = a8;
    query.args[2] = a9;
    query.args[3] = a10;
    query.keyC = *ZeroKey();
    query.count = 0;
    if (!bin) {
        for (FragmentQuery &known : queries) {
            if (known.bin.Get() != query.bin.Get() || known.kind != query.kind || known.arg4 != query.arg4) continue;
            if (KeysDiffer(&known.keyA, &query.keyA) || KeysDiffer(&known.keyB, &query.keyB)) continue;
            if (std::memcmp(known.args, query.args, sizeof(query.args)) != 0) continue;
            if (KeysDiffer(&known.keyC, &query.keyC) || known.allowCustom != query.allowCustom) continue;
            if (out != &known.items) out->assign(known.items.begin(), known.items.end());
            return known.count;
        }
    }
    GetSkinFragments(bin, out, kind, uint32_t(a4), keyA, keyB, a7, a8, a9, a10, nullptr);
    query.count = int32_t(out->size());
    query.items.assign(out->begin(), out->end());
    if (!bin) queries.push_back(query);
    return query.count;
}

// 0x505de0 (449 B): the part resource (iid 0xebcf3e20) behind the key, and for the part's body-part bit (each of the
// thirteen but 0x80, 0x400 and 0x800 for part 13): slot 32 for that bit, given the target's slots 22, 23, 28 and 29 (asked
// in reverse), whose answer's key the target takes (its slot 73), or with no answer the target drops the bit (slot 82).
bool cTSCASSystem::ApplyParts(void *target, const SkinKey *key, uint32_t part)
{
    if (!target) return false;
    CASRef resource;
    const bool loaded = LoadResource(key, kPartIid, resource.AsPPVoid());
    if (!loaded) return false;
    const auto apply = [&](uint32_t bit) {
        const uint32_t a6 = RawSlot<29, uint32_t>(target);
        const uint32_t a5 = RawSlot<28, uint32_t>(target);
        const uint32_t a4 = RawSlot<23, uint32_t>(target);
        const uint32_t a3 = RawSlot<22, uint32_t>(target);
        void *const answer = FindSkinPart(bit,resource.Get(), a3, a4, reinterpret_cast<const SkinKey *>(uintptr_t(a5)),
                                    reinterpret_cast<const SkinKey *>(uintptr_t(a6)), 1);
        if (answer) {
            uint8_t scratch[16];
            void *const inner = RawSlot<3, void *>(answer);
            RawSlot<73, void>(target, 1u, bit, RawSlot<3, void *>(inner, scratch));
        } else {
            RawSlot<82, void>(target, 1u, bit);
        }
    };
    if (part == kAllParts) {
        for (uint32_t i = 0; resource && i < kAllParts; ++i) {
            const uint32_t bit = 1u << i;
            if (!(bit & kSkippedParts)) apply(bit);
        }
    } else {
        apply(1u << (part & 31));
    }
    return loaded;
}

// 0x5008e0 (197 B): the settings service's "allowCustomContent" (true when there is no service or no such setting).
uint8_t __cdecl cTSCASSystem::AllowCustomContent()
{
    uint8_t allow = 1;
    ScopedRef settings{nullptr};
    if (void *const services = GetServiceObject()) RawSlot<5, void>(services, kSettingsId, kSettingsIid, &settings.mObject);
    if (settings.mObject) {
        gz::AnyBoolean value(0);
        bool found;
        {
            GZServiceString name("allowCustomContent");
            found = RawSlot<9, bool>(settings.mObject, &name, static_cast<cIGZAny *>(&value));
        }
        if (found) allow = value.mValue; // the raw byte
    }
    settings.ReleaseObject();
    return allow;
}

// 0x506a10 (619 B): once (until ResetCaches), with the type cache on: every type CAS shows gathered (and cached) in the
// set's order.
bool cTSCASSystem::FillTypeCaches()
{
    if (mCachesFilled) return true;
    mCachesFilled = true;
    if (!mCacheTypes) return true;
    BinCatalog();
    std::set<uint32_t> types;
    for (uint32_t type : {0x80u, 0x10u, 0x40u, 1u, 8u, 0x2000u, 0x4000u, 0x8000u, 0x20000u}) types.insert(type);
    for (uint32_t type : types) {
        CASItemVector items;
        GatherByType(&items, type, nullptr);
    }
    return true;
}

// 0x50a590 (511 B): the fragments of a bin (or of a type) whose property set (the item's +0x14) has a bit of each mask,
// matches each key given (unless it has the zero key there: its slot 28 / 29), and is not custom content when that is not
// allowed; each kept one's property set gets slot 14.
bool cTSCASSystem::FilterFragments(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, uint32_t flags,
                                   const SkinKey *keyA, const SkinKey *keyB, uint32_t type)
{
    if (!out) return false;
    out->clear();
    CASItemVector items;
    const bool gathered = bin ? GatherFromBin(&items, static_cast<cIGZUnknown *>(bin), nullptr)
                              : GatherByType(&items, type, nullptr);
    if (!gathered) return false;
    const uint8_t allowCustom = AllowCustomContent();
    for (const CASItem &item : items) {
        void *const properties = item.refs[5].Get();
        if (!properties) continue;
        if (!(ages & RawSlot<22, uint32_t>(properties)) || !(genders & RawSlot<23, uint32_t>(properties)) ||
            !(flags & RawSlot<24, uint32_t>(properties)))
            continue;
        if (keyA) {
            const SkinKey *const zero = ZeroKey();
            if (KeysDiffer(RawSlot<28, const SkinKey *>(properties), zero) && KeysDiffer(keyA, RawSlot<28, const SkinKey *>(properties)))
                continue;
        }
        if (keyB) {
            const SkinKey *const zero = ZeroKey();
            if (KeysDiffer(RawSlot<29, const SkinKey *>(properties), zero) && KeysDiffer(keyB, RawSlot<29, const SkinKey *>(properties)))
                continue;
        }
        if (!allowCustom && item.flag) continue;
        RawSlot<14, void>(properties);
        out->push_back(item);
    }
    return !out->empty();
}

// ---- the gathers ----

namespace {
// The catalog's out-parameter: counted fragments in an allocator-service block (released and freed by 0x1f1f70).
struct FragmentList : ServiceRefVector {
    FragmentList() { mFirst = mLast = mEnd = nullptr; }
    FragmentList(const FragmentList &) = delete;
    FragmentList &operator=(const FragmentList &) = delete;
    ~FragmentList() { ReleaseAndFree(); }
    void **begin() const { return mFirst; }
    void **end() const { return mLast; }
};
} // namespace

bool cTSCASSystem::GatherByType(CASItemVector *out, uint32_t type, cITSFragEntryFilter *filter)
{
    if (!out) return false;
    if (!mCachesFilled) FillTypeCaches();
    out->clear();
    auto cached = mTypes.find(type);
    if (mCacheTypes) {
        if (cached == mTypes.end()) {
            FillTypeCache(type);
            cached = mTypes.find(type);
        }
        if (cached == mTypes.end()) return false;
        const CASItemVector &items = cached->second;
        if (!filter) {
            if (out != &items) out->assign(items.begin(), items.end());
            return !out->empty();
        }
        for (const CASItem &item : items)
            if (filter->AcceptItem(&item)) out->push_back(item);
        return !out->empty();
    }
    PersistSystem();
    void *const catalog = BinCatalog();
    cTSCASBinFilter bins(type);
    FragmentList fragments;
    RawSlot<30, void>(catalog, &fragments, static_cast<cITSBinFilter *>(&bins), filter->GetFilter());
    for (void *fragment : fragments) {
        CASItem item{};
        if (GatherItem(type, static_cast<cIGZUnknown *>(fragment), &item)) out->push_back(item);
    }
    return !out->empty();
}

bool cTSCASSystem::GatherFromBin(CASItemVector *out, cIGZUnknown *bin, cITSFragEntryFilter *filter)
{
    if (!out || !bin) return false;
    out->clear();
    PersistSystem();
    void *const catalog = BinCatalog();
    FragmentList fragments;
    if (!filter) {
        cTSDuplicateObjectFragmentFilter once;
        RawSlot<32, void>(catalog, bin, &fragments, static_cast<cITSSimFragmentFilter *>(&once));
    } else {
        RawSlot<32, void>(catalog, bin, &fragments, filter->GetFilter());
    }
    const uint32_t type = RawSlot<12, uint32_t>(bin);
    for (void *fragment : fragments) {
        CASItem item{};
        if (GatherItem(type, static_cast<cIGZUnknown *>(fragment), &item)) out->push_back(item);
    }
    return !out->empty();
}

void cTSCASSystem::FillTypeCache(uint32_t type)
{
    if (!mCacheTypes) return;
    PersistSystem();
    void *const catalog = BinCatalog();
    cTSCASBinFilter bins(type);
    cTSDuplicateObjectFragmentFilter once;
    FragmentList fragments;
    RawSlot<30, void>(catalog, &fragments, static_cast<cITSBinFilter *>(&bins), static_cast<cITSSimFragmentFilter *>(&once));
    CASItemVector &items = mTypes[type];
    items.clear();
    for (void *fragment : fragments) {
        CASItem item{};
        if (GatherItem(type, static_cast<cIGZUnknown *>(fragment), &item)) items.push_back(item);
    }
}

bool cTSCASSystem::GatherItem(uint32_t type, cIGZUnknown *fragment, CASItem *item)
{
    if (!fragment) return false;
    // The place is emptied first, then the fragment's key asked (its slot 21), then the resource loaded into the place.
    const auto load = [&](CASRef &place, uint32_t iid) {
        void **const out = place.AsPPVoid();
        const SkinKey *const key = RawSlot<21, const SkinKey *>(fragment);
        return cTSCASSystem::LoadResource(key, iid, out);
    };
    bool loaded;
    switch (type) {
    case 1: case 0x200: case 0x2000: case 0x4000: case 0x10000: loaded = load(item->refs[5], 0xac598ea8); break;
    case 0x10: loaded = load(item->refs[2], 0x8c1571d6); break;
    case 0x20: loaded = load(item->refs[3], 0x8c93e358); break;
    case 0x40: loaded = load(item->refs[4], 0x0c93e3da); break;
    case 0x80: loaded = load(item->refs[1], 0xebcf3e20); break;
    case 0x8000: loaded = load(item->refs[8], 0xd1954459); break;
    case 8:
        if (!load(item->refs[6], 0x4c15807c) && !load(item->refs[7], 0x8c1580ae)) return false;
        loaded = true;
        break;
    case 0x20000: loaded = LoadByKeyType(RawSlot<21, const SkinKey *>(fragment), item); break;
    default: return false;
    }
    if (!loaded) return false;
    item->refs[0].Assign(fragment);
    item->flag = RawSlot<27, int16_t>(fragment) == 0;
    return true;
}

bool cTSCASSystem::LoadByKeyType(const SkinKey *key, CASItem *item)
{
    const auto load = [&](CASRef &place, uint32_t iid) { return cTSCASSystem::LoadResource(key, iid, place.AsPPVoid()); };
    switch (key->type) {
    case 0x0c93e3de: return load(item->refs[4], 0x0c93e3da);
    case 0x2c1fd8a1: case 0x0c1fe246: return load(item->refs[2], 0x8c1571d6);
    case 0x4c158081: return load(item->refs[6], 0x4c15807c);
    case 0x8c1580b5: return load(item->refs[7], 0x8c1580ae);
    case 0x8c93e35c: return load(item->refs[3], 0x8c93e358);
    case 0xac598eac: return load(item->refs[5], 0xac598ea8);
    case 0xd1954460: return load(item->refs[8], 0xd1954459);
    case 0xebcf3e27: return load(item->refs[1], 0xebcf3e20);
    default: return false;
    }
}

// 0x501dd0 (416 B): the bins the catalog lists (its slot 14) through a name filter, in order until one answers: each
// bin's string resource (the persist system's slot 5 for the bin's key, iid 0xfb4cb0de; uncounted bins) has the bin's
// name as its entry for the bin's id (its slot 29, a short; the resource's slot 7 into one string reused for every bin);
// on a name equal to `name` without case, the bin's QueryInterface for `iid` is the answer.
bool cTSCASSystem::FindBinByName(const char *name, uint32_t iid, void **out)
{
    void *const catalog = BinCatalog();
    void *const persist = PersistSystem();
    cTSBinNameFilter filter;
    rz::cRZStringMember binName; // freed after the list, as the original does
    FragmentList bins;
    RawSlot<14, void>(catalog, &bins, static_cast<cITSBinFilter *>(&filter));
    bool found = false;
    for (void **at = bins.begin(); at != bins.end() && !found; ++at) {
        cIGZUnknown *const bin = static_cast<cIGZUnknown *>(*at);
        CASRef names;
        if (As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(RawSlot<16, const SkinKey *>(bin)), kBinNamesIid, names.AsPPVoid(), 0u, 0u)) {
            RawSlot<7, void>(names.Get(), &binName.value, uint32_t(RawSlot<29, uint16_t>(bin)), -1);
            if (_stricmp(binName.value.ToChar(), name) == 0) found = bin->QueryInterface(iid, out);
        }
    }
    return found;
}

bool __fastcall cTSCASSystemAdapters::GatherByType(cTSCASSystem *self, void *, CASItemVector *out, uint32_t type,
                                                   cITSFragEntryFilter *filter)
{
    return self->GatherByType(out, type, filter);
}
bool __fastcall cTSCASSystemAdapters::GatherFromBin(cTSCASSystem *self, void *, CASItemVector *out, cIGZUnknown *bin,
                                                    cITSFragEntryFilter *filter)
{
    return self->GatherFromBin(out, bin, filter);
}
void __fastcall cTSCASSystemAdapters::FillTypeCache(cTSCASSystem *self, void *, uint32_t type) { self->FillTypeCache(type); }
bool __fastcall cTSCASSystemAdapters::LoadByKeyType(cTSCASSystem *self, void *, const SkinKey *key, CASItem *item)
{
    return self->LoadByKeyType(key, item);
}

// ---- cITSCASSystem: creating sims (slots 5 and 6, lane JS) ----

namespace {
// The description kinds the factory builds for an age (CreateSim's 0..3) or a parent's kind bit (CreateChild's 1, 2, 4, 8).
constexpr uint32_t kDescriptionKinds[4] = {0x7fd96b54, 0x0f83c946, 0x111d8fff, 0x911d900a};
constexpr uint32_t kAnyKind = 0xffffffff;      // slots 5 / 6: the kind from the age, or from a parent drawn at random
constexpr uint32_t kChildOutfitKind = 3;       // the outfit job a child gets
constexpr uint32_t kWorldKindA = 0x0bea28c2;   // the app's world (slot 23) kinds the factory is told about (its slot 20's
constexpr uint32_t kWorldKindB = 0x2c1805b0;   // third argument)

// The app's slot 23 (a world object, or none; asked again for each test) is of kind A or B (its slot 5).
bool InMarkedWorld(void *app)
{
    if (!RawSlot<23, void *>(app)) return false;
    return RawSlot<5, uint32_t>(RawSlot<23, void *>(app)) == kWorldKindA || RawSlot<5, uint32_t>(RawSlot<23, void *>(app)) == kWorldKindB;
}

// An outfit job for a new description worked to the end at once (its source: the description's slot 10's slot 28);
// `touchSim`: each step that leaves it running calls the job's sim's slot 10 (CreateSim only). Its outcome.
bool RunOutfitNow(cTSCASSystem *self, cIGZUnknown *description, uint32_t kind, bool touchSim)
{
    OutfitJob job(RawSlot<28, uint32_t>(RawSlot<10, void *>(description)), kind);
    while (self->StepOutfitJob(&job))
        if (touchSim && job.sim) RawSlot<10, void>(job.sim.Get());
    return job.result;
}
} // namespace

// 0x5011d0 (984 B; ret 0x20). The app's factory (its slot 10) builds a description (slot 20: the kind, whether the world is
// marked, 1, 0); none: -1. The job (age -1 as 0; `extra` in its list) is then either queued (an earlier outcome for the
// sim's id dropped) or, with `now`'s low byte, stepped to the end with its outfit job: -1 when either fails. The sim's id.
int32_t cTSCASSystem::CreateSim(int32_t age, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t kind, uint32_t outfitKind,
                                void *extra, uint32_t now)
{
    void *const app = GetCachedSingleton();
    PersistSystem(); // asked for, not used
    if (!app) return -1;
    void *const factory = RawSlot<10, void *>(app);
    RawSlot<26, void>(app); // asked, the answers not used
    RawSlot<27, void>(app);
    const bool marked = InMarkedWorld(app);
    if (kind == kAnyKind && static_cast<uint32_t>(age) < 4) kind = kDescriptionKinds[age];
    CASRef description;
    RawSlot<20, void>(factory, description.AsPPVoid(), kind, marked, 1u, 0u);
    if (!description) return -1;
    NewSimJob job{};
    job.age = age == -1 ? 0 : age;
    job.arg2 = a2;
    job.arg3 = a3;
    job.arg4 = a4;
    job.outfitKind = outfitKind;
    job.sim = description;
    if (extra) job.extras.push_back(CASRef(static_cast<cIGZUnknown *>(extra)));
    if (static_cast<uint8_t>(now) == 0) {
        if (mResults.find(SimId(description)) != mResults.end()) mResults.erase(SimId(description));
        mNewSims.push_back(job);
        return static_cast<int32_t>(SimId(description));
    }
    while (StepNewSimJob(&job)) {
    }
    if (!job.done || !RunOutfitNow(this, description.Get(), outfitKind, true)) return -1;
    return static_cast<int32_t>(SimId(description));
}

// 0x5083b0 (2081 B). The app (none: false), the persist system and the CAS UI asked first; the app's slots 10 and 26,
// and its genetics manager (slot 27, the "tables" below). Each call runs the job's state and moves it on:
//   0: the description's outfit source (its slot 10) held in ref20; a new outfit object (the framework's class
//      0xac598eac) in ref24, given its type, the source's object's (slot 67) slot 11 value twice, and cached;
//   1: the outfit's age bit, the UI told; ages / genders the outfit picks when -1, both set and written into the
//      description (data 0xba the age, 0x3a / 0xaf by the age kind, 0x41 whether arg3 is 1); filed with the tables;
//      the app's world told;
//   2: the outfit's flags set from the extras that match it, its key from arg4 (1..4), the matching extras added to
//      it (each released), its data 0x51 / 0x52 by its slot 40, and the UI told when every face shown succeeded;
//   3: for an adult (age 0) the outfit's parts applied (slot 33 with its first key); 4: filed in the tables again;
//   5: data 0x41 for kind bit 4, the names (slot 20), the outfit named "<first> - <last>"; 6: the app's hook;
//   7: done (false).
bool cTSCASSystem::StepNewSimJob(NewSimJob *job)
{
    void *const app = GetCachedSingleton();
    void *const persist = PersistSystem();
    void *const ui = CASUIService();
    if (!app) return false;
    RawSlot<10, void>(app);
    RawSlot<26, void>(app);
    void *const genetics = // receiver: nTSGenetics::cTSGeneticsManager
        RawSlot<27, void *>(app);
    const auto simId = [job] { return uint32_t(RawSlot<8, uint16_t>(job->sim.Get())); };
    const auto setData = [job](uint32_t id, uint32_t value) { RawSlot<17, void>(job->sim.Get(), id, value); };
    switch (job->state) {
    case 0: {
        ++job->state;
        job->ref20.Assign(RawSlot<10, cIGZUnknown *>(job->sim.Get()));
        void *const source = RawSlot<67, void *>(job->ref20.Get());
        void *const framework = GetDirectorFrameworkObject();
        RawSlot<3, void>(framework, kOutfitResourceType, kOutfitIid, job->ref24.AsPPVoid());
        RawSlot<8, void>(job->ref24.Get(), kOutfitResourceType);
        RawSlot<10, void>(job->ref24.Get(), 1u);
        void *outfit = job->ref24.Get();
        RawSlot<9, void>(outfit, RawSlot<11, uint32_t>(source));
        outfit = job->ref24.Get();
        RawSlot<109, void>(outfit, RawSlot<11, uint32_t>(source));
        void *const manager = As<ifaces::cITSPersistSystem>(persist)->GetResourceManager();
        RawSlot<8, void>(manager, job->ref24.Get());
        break;
    }
    case 1: {
        ++job->state;
        void *const outfit = job->ref24.Get();
        RawSlot<52, void>(outfit, AgeBit(job->age));
        RawSlot<71, void>(ui, RawSlot<24, uint32_t>(job->ref24.Get()));
        if (job->arg2 == UINT32_MAX) {
            RawSlot<68, void>(job->ref24.Get(), 0xffffff7fu);
            RawSlot<98, void>(job->ref24.Get(), UINT32_MAX, 9u);
            job->arg2 = RawSlot<22, uint32_t>(job->ref24.Get());
        }
        if (job->arg3 == UINT32_MAX) {
            RawSlot<68, void>(job->ref24.Get(), 0xfffffeffu);
            RawSlot<98, void>(job->ref24.Get(), UINT32_MAX, 9u);
            job->arg3 = RawSlot<23, uint32_t>(job->ref24.Get());
        }
        RawSlot<50, void>(job->ref24.Get(), job->arg2);
        RawSlot<51, void>(job->ref24.Get(), job->arg3);
        setData(0xba, uint16_t(job->age));
        uint32_t kindValue = 0x33;
        switch (job->arg2) {
        case 1: kindValue = 2; break;
        case 2: kindValue = 3; break;
        case 4: kindValue = 0x10; break;
        case 8: kindValue = 0x13; break;
        case 0x20: kindValue = 1; break;
        case 0x40: kindValue = 0x13; break;
        }
        setData(0x3a, kindValue);
        if (job->arg2 == 0x40) setData(0xaf, 1);
        setData(0x41, job->arg3 == 1 ? 1u : 0u);
        RawSlot<89, void>(job->ref24.Get());
        RawSlot<7, void>(genetics, simId());
        if (RawSlot<4, void *>(app)) RawSlot<39, void>(RawSlot<4, void *>(app), job->sim.Get(), 0u, 1u);
        break;
    }
    case 2: {
        void *const outfit = job->ref24.Get();
        RawSlot<68, void>(outfit, 0xffffffdfu);
        RawSlot<98, void>(job->ref24.Get(), 4u, 0u);
        RawSlot<68, void>(job->ref24.Get(), job->age == 0 ? 0xffffffc1u : 0xffffc7c7u);
        // An extra like the outfit (ages, genders and slot 24 the same) with flag 8 gives it flag 8.
        for (auto it = job->extras.begin(); it != job->extras.end(); ++it) {
            // ORIGINAL-BUG: every pass looks at the first extra, not the one the walk is at.
            void *const extra = job->extras.front().Get();
            if (!extra) continue;
            const uint32_t extraAges = RawSlot<22, uint32_t>(extra);
            if (RawSlot<22, uint32_t>(job->ref24.Get()) != extraAges) continue;
            const uint32_t extraGenders = RawSlot<23, uint32_t>(extra);
            if (RawSlot<23, uint32_t>(job->ref24.Get()) != extraGenders) continue;
            const uint32_t extra24 = RawSlot<24, uint32_t>(extra);
            if (RawSlot<24, uint32_t>(job->ref24.Get()) != extra24) continue;
            if (!(RawSlot<21, uint32_t>(extra) & 8)) continue;
            void *const target = job->ref24.Get();
            RawSlot<68, void>(target, RawSlot<21, uint32_t>(target) | 8);
            break;
        }
        RawSlot<98, void>(job->ref24.Get(), UINT32_MAX, 9u);
        if (job->arg4 - 1 < 4) {
            const uint32_t key[4] = {job->arg4, 0, 0, 0};
            RawSlot<55, void>(job->ref24.Get(), key);
            void *const target = job->ref24.Get();
            RawSlot<68, void>(target, RawSlot<21, uint32_t>(target) | 1);
        } else {
            RawSlot<68, void>(job->ref24.Get(), 0xfffffffeu);
            RawSlot<98, void>(job->ref24.Get(), UINT32_MAX, 9u);
        }
        // Each extra (first to last, released as it goes) like the outfit is added to it; one with flag 8 shows its face.
        bool shown = true;
        while (!job->extras.empty()) {
            if (void *const extra = job->extras.front().Get()) {
                const uint32_t ages = RawSlot<22, uint32_t>(job->ref24.Get());
                if (ages == RawSlot<22, uint32_t>(extra)) {
                    const uint32_t genders = RawSlot<23, uint32_t>(job->ref24.Get());
                    if (genders == RawSlot<23, uint32_t>(extra)) {
                        const uint32_t extra24 = RawSlot<24, uint32_t>(extra);
                        if (RawSlot<24, uint32_t>(job->ref24.Get()) == extra24) {
                            RawSlot<95, void>(job->ref24.Get(), extra);
                            if (RawSlot<21, uint32_t>(extra) & 8) shown = Face()->ShowFace(extra, 1);
                        }
                    }
                }
            }
            job->extras.pop_front();
        }
        if (RawSlot<40, uint32_t>(job->ref24.Get()) == 1) {
            setData(0x52, 0);
            setData(0x51, 0x10);
        } else if (RawSlot<40, uint32_t>(job->ref24.Get()) == 0) {
            setData(0x52, 500);
            setData(0x51, 0);
        } else if (RawSlot<40, uint32_t>(job->ref24.Get()) == 2) {
            setData(0x52, 1000);
            setData(0x51, 1);
        }
        RawSlot<96, void>(job->ref24.Get());
        if (shown) {
            void *const casUI = CASUIService();
            RawSlot<12, void>(casUI, RawSlot<28, uint32_t>(job->ref20.Get()));
        }
        ++job->state;
        break;
    }
    case 3:
        ++job->state;
        if (job->age == 0) {
            void *const outfit = job->ref24.Get();
            const auto *const keys =
                RawSlot<42, const std::pair<const SkinKey *, const SkinKey *> *>(outfit, 1u, RawSlot<41, uint32_t>(outfit));
            if (keys && keys->first != keys->second) Face()->ApplyParts(job->ref24.Get(), keys->first, 0xd);
        }
        break;
    case 4: {
        ++job->state;
        CASRef entry;
        RawSlot<3, void>(genetics, entry.AsPPVoid());
        RawSlot<6, void>(genetics, simId(), entry.Get());
        RawSlot<15, void>(entry.Get(), job->ref24.Get(), UINT32_MAX);
        if (job->age == 0) RawSlot<8, void>(genetics, simId());
        RawSlot<6, void>(genetics, simId(), entry.Get());
        break;
    }
    case 5: {
        ++job->state;
        if (job->outfitKind & 4) {
            if (job->arg3 == 1) {
                if (RawSlot<16, int16_t>(job->sim.Get(), 0x41u) != 1) setData(0x41, 1);
            } else {
                if (RawSlot<16, int16_t>(job->sim.Get(), 0x41u) != 0) setData(0x41, 0);
            }
            Face()->FillNames(job->ref20.Get(), job->sim.Get());
        }
        rz::cRZStringMember name;
        RawSlot<24, void>(job->ref20.Get(), &name.value);
        name.value.mString.append(" - ", 3);
        rz::cRZStringMember last;
        RawSlot<49, void>(job->ref20.Get(), &last.value);
        name.value.mString.append(last.value.mString);
        RawSlot<9, void>(job->ref20.Get(), name.value.mString.c_str());
        break;
    }
    case 6:
        ++job->state;
        if (RawSlot<29, void *>(app)) {
            void *const hook = RawSlot<29, void *>(app);
            RawSlot<4, void>(hook, simId());
        }
        break;
    case 7:
        job->done = true;
        ++job->state;
        return false;
    default:
        return false;
    }
    return true;
}

// 0x500e50 (857 B; ret 0x10). The two parents' descriptions (the factory's slot 16(parent, 1)); either missing: -1. With
// kind -1 the kind comes from one parent drawn at random (its slot 19: 1, 2, 4 or 8, else -1), which the job also keeps
// (1 otherwise). The description and the job as CreateSim's (the outfit job of kind 3).
int32_t cTSCASSystem::CreateChild(void *parentA, void *parentB, uint32_t kind, uint32_t now)
{
    void *const app = GetCachedSingleton();
    if (!app) return -1;
    void *const factory = RawSlot<10, void *>(app);
    void *const descriptionA = RawSlot<16, void *>(factory, parentA, 1u);
    void *const descriptionB = RawSlot<16, void *>(factory, parentB, 1u);
    if (!descriptionA || !descriptionB) return -1;
    uint32_t parentKind = 1;
    if (kind == kAnyKind) {
        parentKind = RawSlot<19, uint32_t>(mRandom.RandomUint32Uniform(100) > 50 ? descriptionA : descriptionB);
        switch (parentKind) {
        case 1: kind = kDescriptionKinds[0]; break;
        case 2: kind = kDescriptionKinds[1]; break;
        case 4: kind = kDescriptionKinds[2]; break;
        case 8: kind = kDescriptionKinds[3]; break;
        default: return -1;
        }
    }
    const bool marked = InMarkedWorld(app);
    CASRef description;
    RawSlot<20, void>(factory, description.AsPPVoid(), kind, marked, 1u, 0u);
    if (!description) return -1;
    ChildJob job{};
    job.parentA = parentA;
    job.parentB = parentB;
    job.kind = parentKind;
    job.sim = description;
    if (static_cast<uint8_t>(now) == 0) {
        if (mResults.find(SimId(description)) != mResults.end()) mResults.erase(SimId(description));
        mChildren.push_back(job);
        return static_cast<int32_t>(SimId(description));
    }
    while (StepChildJob(&job)) {
    }
    if (!job.done || !RunOutfitNow(this, description.Get(), kChildOutfitKind, false)) return -1;
    return static_cast<int32_t>(SimId(description));
}
// 0x502120 (880 B, __cdecl).
bool __cdecl FixOutfitPart(void *outfit, uint32_t kind, uint32_t bit, uint32_t pickOne)
{
    const SkinKeyList *const list = RawSlot<42, const SkinKeyList *>(outfit, kind, bit);
    if (list && list->begin != list->end && RawSlot<101, bool>(outfit, list)) {
        void *const persist = PersistSystem();
        CASRef part;
        bool misfit = false;
        for (const SkinKey *k = list->begin; k != list->end; ++k) {
            misfit = true;
            if (!As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(k), kPartIid, part.AsPPVoid(), 0u, 0u)) break;
            const uint32_t ages = RawSlot<19, uint32_t>(RawSlot<3, void *>(part.Get()));
            if (!(ages & RawSlot<22, uint32_t>(outfit))) break;
            const uint32_t genders = RawSlot<20, uint32_t>(RawSlot<3, void *>(part.Get()));
            if (!(genders & RawSlot<23, uint32_t>(outfit))) break;
            if (RawSlot<7, uint16_t>(part.Get()) != 0 && !(RawSlot<7, uint16_t>(part.Get()) & bit)) break;
            if (!(RawSlot<22, uint32_t>(RawSlot<3, void *>(part.Get())) & kind)) break;
            misfit = false;
        }
        if (!misfit) return false;
    }
    const CASRef filter(NewOutfitMatchFilter(outfit, kind, bit));
    const bool pick = (pickOne & 0xff) != 0;
    reinterpret_cast<uint8_t *>(filter.Get())[kMatchFilterAnyByte] = uint8_t(pick ^ 1);
    void *const resources = ResourceService();
    void *const face = reinterpret_cast<uint8_t *>(filter.Get()) + kMatchFilterFace;
    if (pick) {
        SkinKey chosen{};
        if (!resources || !RawSlot<38, bool>(resources, &chosen, face)) return false;
        RawSlot<73, void>(outfit, kind, bit, &chosen);
        return true;
    }
    std::vector<void *, GZServiceAllocator<void *>> found; // not counted
    RawSlot<12, void>(resources, &found, face, 0u, 0xffffffffu);
    if (found.empty()) return false;
    uint8_t scratch[16];
    RawSlot<73, void>(outfit, kind, bit, RawSlot<3, const SkinKey *>(RawSlot<3, void *>(found.front()), scratch));
    return true;
}

// 0x502490 (973 B; ret 8).
bool cTSCASSystem::FixOutfitParts(uint32_t simId, uint32_t pickOne)
{
    cIGZUnknown *const object = ObjectForId(simId);
    if (!object) return false;
    if (RawSlot<23, bool>(object) || OutfitFixed(object)) return true;
    CASRef outfit;
    if (!ObjectOutfit(object, outfit.AsPPVoid())) return false;
    void *const resources = ResourceService();
    if (!RawSlot<19, bool>(resources, RawSlot<29, uint32_t>(outfit.Get()))) {
        const uint32_t kind = RawSlot<22, uint32_t>(outfit.Get());
        uint8_t guid[16];
        RawSlot<56, void>(outfit.Get(), hexparse::ParseGuidText(guid, kind == 0x10 ? kGuidKind10 : kGuidOther));
    }
    bool changedAny = false;
    bool changedFirst = false; // bit 1's part changed
    uint32_t bit = 1;
    for (uint32_t i = 0; i < kAllParts; ++i, bit <<= 1) {
        if (bit & kSkippedParts) continue;
        if (bit == 2 || bit == 4) {
            RawSlot<82, void>(outfit.Get(), 0x3fffu, bit);
            continue;
        }
        bool changed = false;
        if (bit == 1) {
            if (FixOutfitPart(outfit.Get(), 1, 1, pickOne)) {
                SkinKey first = *RawSlot<42, const SkinKeyList *>(outfit.Get(), 1u, 1u)->begin;
                ApplyParts(outfit.Get(), &first, kAllParts);
                changed = true;
            }
        } else if (!RawSlot<44, bool>(outfit.Get(), 1u, bit, 0u)) {
            RawSlot<76, void>(outfit.Get(), 1u, bit, 0u, 1u);
            changed = true;
        }
        if (!RawSlot<44, bool>(outfit.Get(), 2u, bit, 0u) && bit != 1) RawSlot<76, void>(outfit.Get(), 1u, bit, 0u, 2u);
        const bool four = RawSlot<44, bool>(outfit.Get(), 4u, bit, 0u);
        const bool sixteen = RawSlot<44, bool>(outfit.Get(), 0x10u, bit, 0u);
        if (four && sixteen) {
            changed |= FixOutfitPart(outfit.Get(), 4, bit, pickOne);
            changed |= FixOutfitPart(outfit.Get(), 0x10, bit, pickOne);
        } else {
            changed |= FixOutfitPart(outfit.Get(), 8, bit, pickOne);
        }
        if (bit == kRandomPartBit) {
            const uint32_t genders = RawSlot<23, uint32_t>(outfit.Get());
            const uint32_t ages = RawSlot<22, uint32_t>(outfit.Get());
            const CASRef drawn(static_cast<cIGZUnknown *>(PickRandomOutfit(kRandomPartDefinition, simId, ages, genders)));
            if (drawn) {
                RawSlot<77, void>(outfit.Get(), drawn.Get(), kRandomPartBit, kRandomPartBit, 8u, 0u);
                changed = true;
            }
        }
        changedAny |= changed;
        if (changed && bit == 1) changedFirst = true;
    }
    if (!changedAny) return false;
    RawSlot<96, void>(outfit.Get());
    As<ifaces::cITSPersistSystem>(PersistSystem())->SaveResource(As<ifaces::cIGZPersistResource>(outfit.Get()), nullptr);
    if (changedFirst) {
        void *const dialogs = RawSlot<4, void *>(DialogService());
        RawSlot<20, void>(dialogs, simId);
        CASRef answer;
        RawSlot<15, void>(dialogs, simId, kOutfitDialogId, answer.AsPPVoid());
    }
    return true;
}
// 0x5009b0 (753 B): the sim object (0x54a130(simId), counted here) and its outfit (0x54ac80); the special-outfit items for
// the key (0x503e20). When any item has a texture overlay (+0x08), the sim's outfit is shown first (slot 36(outfit, 1)) and
// the CAS UI gets each overlay's slot 17 (slot 37 with `flag`, or slot 39); an item without one hands its part (+0x04: its
// slot 3's key and slot 22, its slot 7's short) to the outfit's slot 70 (flag) or 79. Then the UI's slot 12(simId), an
// outfit job of kind 9 (slot 8), the bin catalog's slot 42(simId) and the sim's age options: its factory (slot 8) makes
// a 0xfa1c39f7 object whose slot 8 gets the sim's data 8, and 0x77 / 0x78 combined (counted through its virtual base).
bool cTSCASSystem::SpecialOutfitItems(const SkinKey *key, void *outfit, uint32_t flag, CASItemVector *out)
{
    return OriginalFunction<bool(__thiscall *)(cTSCASSystem *, const SkinKey *, void *, uint32_t, CASItemVector *), 0x503e20>()(
        this, key, outfit, flag, out);
}
bool cTSCASSystem::ApplySpecialOutfit(const SkinKey *key, uint32_t simId, uint32_t flag)
{
    const CASRef sim(SimObject(simId));
    if (!sim) return false;
    CASRef outfit;
    SimOutfit(sim.Get(), outfit.AsPPVoid());
    if (!outfit) return false;
    CASItemVector items;
    if (!SpecialOutfitItems(key, outfit.Get(), flag, &items) || items.empty()) return false;
    bool anyOverlay = false;
    for (const CASItem &item : items) {
        anyOverlay = item.refs[2].Get() != nullptr;
        if (anyOverlay) break;
    }
    void *ui = nullptr;
    if (anyOverlay) {
        ui = CASUIService();
        ShowFace(outfit.Get(), 1);
    }
    const bool byFlag = (flag & 0xff) != 0;
    for (const CASItem &item : items) {
        if (void *const overlay = item.refs[2].Get()) {
            if (byFlag) RawSlot<37, void>(ui, RawSlot<17>(overlay), flag, 0u);
            else RawSlot<39, void>(ui, RawSlot<17>(overlay));
        } else if (void *const part = item.refs[1].Get()) {
            SkinKey partKey;
            RawSlot<3, void>(RawSlot<3, void *>(part), &partKey);
            const uint32_t value = RawSlot<22>(RawSlot<3, void *>(part));
            const uint32_t parts = RawSlot<7, uint16_t>(part);
            if (byFlag) RawSlot<70, void>(outfit.Get(), value, parts, &partKey);
            else RawSlot<79, void>(outfit.Get(), value, parts, &partKey);
        }
    }
    if (anyOverlay) RawSlot<12, void>(ui, simId);
    RunOutfitJob(simId, 9, nullptr, nullptr, 0);
    if (void *const catalog = BinCatalog()) {
        CASRef answer;
        RawSlot<42, void>(catalog, simId, answer.AsPPVoid(), 0u);
    }
    VirtualBaseRef options;
    void *const factory = RawSlot<8, void *>(sim.Get());
    RawSlot<9, void>(factory, kOptionsClass, kOptionsIid, options.AsPPVoid());
    if (void *const target = options.Get()) {
        const int32_t data8 = RawSlot<10, int16_t>(sim.Get(), 8u, 1u);
        const int16_t low = RawSlot<10, int16_t>(sim.Get(), 0x77u, 1u);
        const int16_t high = RawSlot<10, int16_t>(sim.Get(), 0x78u, 1u);
        const uint32_t both = static_cast<uint32_t>(static_cast<uint16_t>(low)) | static_cast<uint32_t>(high) << 16;
        // Five arguments, as the original pushes them (0x500c3e). The target is probably nTSSG::cPerson's virtual-base
        // face at +0x3b4 (slot 8 0x28b14c ret 0x14; slots 6 / 7 one argument each), which no RTTI base name reaches.
        RawSlot<8, void>(target, data8, uint32_t(both != 0), both, 0u, 1u); // receiver: none
    }
    return true;
}
// 0x50a9f0 (2523 B; ret 4). The persist system's slot 34(key, 0x3b2271bb) is the content's resource; its slot 3's slot
// 19(&keys, 0) lists what it holds. Each listed key: the fragment types its resource type changes noted (a set: 8 for
// 0x8c1580b5 / 0x4c158081, 0x10 for 0x2c1fd8a1 / 0x0c1fe246, 0x40 and 0x20 for the four mesh types, 1 / 0x2000 / 0x4000
// for an outfit 0xac598eac, 0x8000 for 0xd1954460, and 0x80 for a part 0xebcf3e27 that loads (0xebcf3e20) with its set's
// slot 22 lacking bit 1 and its slot 7 short not 0x80); outfits and parts kept by key; the not-default flag of the key's
// group dropped; then a known type's resource (its 0x8c1571d6 face) handed to the resource service's slot 8, any other
// key to the persist system's slot 17. Then the resource service's slots 42 / 41 and the bin catalog's slots 6 / 5 with
// the content's slot 3; with outfits or parts kept, the persist system's slot 3's slot 19 lists the outfit records
// (0x0c560f39): each whose key (0x0c560f33 face, slot 21) is a kept one is handed to the catalog's slot 26 once. The skin
// service's slot 22, the query and first named-key caches emptied, and each noted type (ascending) dropped from the type
// cache and gathered again when the cache is on.
bool cTSCASSystem::RefreshContent(const SkinKey *key)
{
    void *const persist = PersistSystem();
    void *const catalog = BinCatalog();
    void *const resources = ResourceService();
    if (!IsNotDefault(key)) return false;
    CASRef content;
    if (!As<ifaces::cITSPersistSystem>(persist)->FindSegment(reinterpret_cast<const cGZPersistResourceKey *>(key), kContentIid, content.AsPPVoid())) return false;
    OverlayKeys keys;
    RawSlot<19, void>(RawSlot<3, void *>(content.Get()), &keys, 0u);
    std::set<SkinKey, SkinKeyLess> kept; // outfits and parts
    std::set<uint32_t> types;            // the fragment types to gather again
    for (const SkinKey &listed : keys) {
        bool known = false;
        switch (listed.type) {
        case 0x8c1580b5: case 0x4c158081:
            known = true;
            types.insert(8);
            break;
        case 0x2c1fd8a1: case 0x0c1fe246:
            known = true;
            types.insert(0x10);
            break;
        case 0x0c93e3de: case 0x6c93b566: case 0x8c93bf6c: case 0x8c93e35c:
            types.insert(0x40);
            types.insert(0x20);
            known = true;
            break;
        case kOutfitResourceType: // not "known": dropped by the persist system
            kept.insert(listed);
            types.insert(1);
            types.insert(0x2000);
            types.insert(0x4000);
            break;
        case 0xd1954460:
            types.insert(0x8000);
            known = true;
            break;
        case kPartType: {
            kept.insert(listed);
            CASRef part;
            if (As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&listed), kPartIid, part.AsPPVoid(), 0u, 0u)) {
                if (!(RawSlot<22, uint32_t>(RawSlot<3, void *>(part.Get())) & 2) && RawSlot<7, int16_t>(part.Get()) != 0x80) types.insert(0x80);
            }
            known = true;
            break;
        }
        default: break;
        }
        mNotDefault.erase(listed.group);
        if (known) {
            CASRef registered;
            if (As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&listed), kRegisteredIid, registered.AsPPVoid(), 0u, 0u))
                RawSlot<8, void>(resources, registered.Get());
        } else {
            As<ifaces::cITSPersistSystem>(persist)->DeleteResource(const_cast<SkinKey *>(&listed));
        }
    }
    if (RawSlot<42, bool>(resources, RawSlot<3, void *>(content.Get()))) RawSlot<41, void>(resources, RawSlot<3, void *>(content.Get()));
    if (RawSlot<6, bool>(catalog, RawSlot<3, void *>(content.Get()))) RawSlot<5, void>(catalog, RawSlot<3, void *>(content.Get()));
    if (!kept.empty()) {
        OverlayKeys records;
        RawSlot<19, void>(As<ifaces::cITSPersistSystem>(persist)->GetResourceManager(), &records, kOutfitRecordType);
        CASRef record;
        for (const SkinKey &k : records) {
            if (!As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&k), kOutfitRecordIid, record.AsPPVoid(), 0u, 0u)) continue;
            if (kept.find(*RawSlot<21, const SkinKey *>(record.Get())) == kept.end()) continue;
            kept.erase(*RawSlot<21, const SkinKey *>(record.Get()));
            RawSlot<26, void>(catalog, record.Get());
        }
    }
    void *const skins = SkinService();
    RawSlot<22, void>(skins, RawSlot<3, void *>(content.Get()));
    mQueries.clear();
    mNamedKeysA.clear();
    while (!types.empty()) {
        const uint32_t type = *types.begin();
        types.erase(types.begin());
        if (mCacheTypes) {
            mTypes.erase(type);
            CASItemVector items;
            GatherByType(&items, type, nullptr);
        }
    }
    return true;
}
// 0x506ee0 (1822 B): the sim's name string set (resource 'CTSS' {group: its slot 67's slot 11, instance: its info's language
// (slot 3(0x29), a short)}; made by the framework (class 0xdb4cb0f9) and keyed so when the persist system has none). Unless
// its info's slot 3(0x30) says otherwise, entries 0..43 of kind 1 get a first name and of kind 3 a last name, each from one
// row (drawn with the system's generator) of the string table 'STR#' 0x7f96c284 for the description's age (its slot 16(0x41)
// is 1 or not) and slot 22; for the info's name styles 0x8ea1056c / 0xcf99cb2e a first name goes through a format text
// (string tables 0xca / 0xcb). Otherwise the 44 kind-3 entries are emptied. Then the set's slot 21 and the sim's slot 55(0).
bool cTSCASSystem::FillNames(void *sim, void *description)
{
    if (!sim || !description) return false;
    void *const info = RawSlot<4, void *>(sim);
    if (!info) return false;
    void *const persist = PersistSystem();
    const uint32_t language = RawSlot<3, uint16_t>(info, kInfoLanguage);
    const uint32_t group = RawSlot<11, uint32_t>(RawSlot<67, void *>(sim));
    RawSlot<28, void>(sim);
    const uint32_t style = RawSlot<3, uint32_t>(info, kInfoNameStyle);
    const SkinKey namesKey{kNameSetType, group, language, 0};
    CASRef names;
    if (!As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&namesKey), kBinNamesIid, names.AsPPVoid(), 0u, 0u)) {
        void *const framework = GetDirectorFrameworkObject();
        RawSlot<3, void>(framework, kNameSetClass, kBinNamesIid, names.AsPPVoid());
        RawSlot<4, void>(RawSlot<3, void *>(names.Get()), &namesKey);
    }
    const bool child = RawSlot<16, int16_t>(description, kDescriptionAge) == 1;
    if (RawSlot<3, uint32_t>(info, kInfoNamesFixed) == 0) {
        CASRef table;
        const auto load = [&](uint32_t row) {
            const SkinKey key{kNameTableType, kNameTablesGroup, row, 0};
            As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&key), kBinNamesIid, table.AsPPVoid(), 0u, 0u);
        };
        load(RawSlot<22, bool>(description) ? 0x96u + child : 0xeau + child);
        if (table) {
            const uint32_t pick = mRandom.RandomUint32Uniform(RawSlot<5, uint32_t>(table.Get(), 0u)) + 1;
            for (uint32_t i = 1; i != kNameCount + 1; ++i) {
                rz::cRZStringMember text;
                RawSlot<7, void>(table.Get(), &text.value, pick, i - 1);
                if (style == kStyleA || style == kStyleB) {
                    const std::string name(text.value.ToChar(), text.value.Strlen());
                    uint16_t table2 = 0xca, entry = child ? 2 : 1;
                    if (style == kStyleB) table2 = 0xcb, entry = child ? 0x14 : 0x13;
                    FormatText format(table2, entry, uint8_t(i));
                    text.value.Sprintf(format.value.mString.ToChar(), name.c_str());
                }
                RawSlot<10, void>(names.Get(), &text.value, 1u, i - 1);
            }
        }
        load(RawSlot<22, bool>(description) ? (child ? 0xccu : 0x98u) : 0xecu + child);
        if (table) {
            const uint32_t pick = mRandom.RandomUint32Uniform(RawSlot<5, uint32_t>(table.Get(), 0u)) + 1;
            for (uint32_t i = 1; i != kNameCount + 1; ++i) {
                rz::cRZStringMember text;
                RawSlot<7, void>(table.Get(), &text.value, pick, i - 1);
                RawSlot<10, void>(names.Get(), &text.value, 3u, i - 1);
            }
        }
    } else {
        for (uint32_t i = 1; i != kNameCount + 1; ++i) {
            rz::cRZStringMember text;
            RawSlot<10, void>(names.Get(), &text.value, 3u, uint32_t(uint8_t(i - 1)));
        }
    }
    RawSlot<21, void>(names.Get());
    RawSlot<55, void>(sim, 0u);
    return true;
}
// ---- the filter slots (lane JM): a filter built on the stack, then the fragments of the bin, or of the slot's type ----

namespace {
template <class Filter> bool GatherWith(cTSCASSystem *self, void *bin, CASItemVector *out, uint32_t type, Filter &filter)
{
    cITSFragEntryFilter *const face = &filter;
    return bin ? self->GatherFromBin(out, static_cast<cIGZUnknown *>(bin), face) : self->GatherByType(out, type, face);
}
} // namespace

// 0x50a0c0 (266 B): cosmetics (type 0x10), custom content as the settings allow.
bool cTSCASSystem::GetCosmetics(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, uint32_t styles,
                                const SkinKey *keyB, const SkinKey *keyA, const SkinKey *keyC, uint32_t parts,
                                uint32_t flags)
{
    cTSCosmeticsFragFilter filter(ages, genders, styles, keyB, keyA, keyC, parts, flags);
    filter.mAllowCustom = AllowCustomContent();
    return GatherWith(this, bin, out, 0x10, filter);
}
// 0x509f60 (160 B): archetypes (type 0x20).
bool cTSCASSystem::GetArchetypes(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, uint32_t styles)
{
    cTSArchetypesFragFilter filter(ages, genders, styles, 0, 1);
    return GatherWith(this, bin, out, 0x20, filter);
}
// 0x50a4f0 (160 B): modifiers (type 0x40).
bool cTSCASSystem::GetModifiers(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, uint32_t styles)
{
    cTSModifierFragFilter filter(ages, genders, styles, 0, 1);
    return GatherWith(this, bin, out, 0x40, filter);
}
// 0x50a790 (174 B): skin tones (type 8), custom content as the settings allow.
bool cTSCASSystem::GetSkinTones(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, uint32_t styles,
                                uint32_t flags)
{
    cTSSkinTonesFragFilter filter(ages, genders, styles, flags, 1);
    filter.mAllowCustom = AllowCustomContent();
    return GatherWith(this, bin, out, 8, filter);
}
// 0x50a000 (177 B): body options (type 0x8000), custom content as the settings allow.
bool cTSCASSystem::GetBodyOptions(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, uint32_t styles,
                                  uint32_t flags)
{
    cTSBodyOptionsFragFilter filter(ages, genders, styles, flags, 1);
    filter.mAllowCustom = AllowCustomContent();
    return GatherWith(this, bin, out, 0x8000, filter);
}
// 0x50a840 (203 B): skin fragments (type 0x80), custom content as the settings allow.
bool cTSCASSystem::GetSkinFragments(void *bin, CASItemVector *out, uint32_t ages, uint32_t genders, const SkinKey *keyA,
                                    const SkinKey *keyB, uint32_t styles, uint32_t categories, uint32_t parts,
                                    uint32_t allowedFlags, const SkinKey *keyC)
{
    cTSBinSkinFragFilter filter(ages, genders, keyA, keyB, styles, categories, parts, allowedFlags, keyC, 1);
    filter.mAllowCustom = AllowCustomContent();
    return GatherWith(this, bin, out, 0x80, filter);
}
// ---- slots 32 and 34 (lane JN): a stand-in for a part or overlay that does not fit a sim ----
//
// Both first take the object itself when it fits; else, by the text of its key (0x4fdd30), the keys of the stand-ins found
// for that text before (slot 32: mNamedKeysA, 34: mNamedKeysB), loaded through the persist system, the first that fits
// and has the same key; else the skin catalog's (the resource service's) candidates, the one taken remembered under the
// text. The answer is not counted (a loaded one is released before it is handed back, as the original does).

namespace {
bool SameKey(const SkinKey *a, const SkinKey *b) { return !KeysDiffer(a, b); }
// 0x4fdd30 (__thiscall on the key, ret 4): the key as text (game code shared by the CAS UI and the genetics).
void KeyName(const SkinKey *key, GZServiceString *out)
{
    OriginalFunction<void(__thiscall *)(const SkinKey *, GZServiceString *), 0x4fdd30>()(key, out);
}
// A candidate's own key, remembered: its property set's (slot 3) key (the set's slot 3 into a buffer).
void Remember(std::set<SkinKey, SkinKeyLess> &known, void *candidate)
{
    uint8_t buffer[16];
    void *const properties = RawSlot<3, void *>(candidate);
    known.insert(*RawSlot<3, const SkinKey *>(properties, static_cast<void *>(buffer)));
}
// Whether a candidate (its property set, slot 3) is flagged 1 (the set's slot 25): taken only when nothing else is.
bool Flagged(void *candidate) { return (RawSlot<25, uint8_t>(RawSlot<3, void *>(candidate)) & 1) != 0; }
// A key that is the zero key or `want` (when given): the key asked twice, as the original does.
template <int Slot> bool KeyFits(void *object, const SkinKey *want)
{
    const SkinKey *const key = RawSlot<Slot, const SkinKey *>(object);
    const SkinKey *const zero = ZeroKey();
    return SameKey(key, zero) || SameKey(want, RawSlot<Slot, const SkinKey *>(object));
}
// A part bit mask (a short; none means every part) that has a bit of `parts`.
template <int Slot> bool PartsFit(void *object, uint32_t parts)
{
    return RawSlot<Slot, uint16_t>(object) == 0 || (parts & RawSlot<Slot, uint16_t>(object)) != 0;
}
} // namespace

// 0x502e40 (1848 B): a skin part for the body-part bits `parts` (ApplyParts' slot 32), the ages / genders masks and the
// keys (`keyA` may be none). The part itself fits when its property set has a bit of each mask, its slot 5 key is zero or
// keyA, its parts (slot 7) are none or have a bit of `parts`, and its slot 6 key is zero or keyB (with `strict`, a slot 6
// key other than {1..5, 0, 0, 0} also lets it through). Else a stand-in, by the part's own key (slot 4).
void *cTSCASSystem::FindSkinPart(uint32_t parts, void *part, uint32_t ages, uint32_t genders, const SkinKey *keyA,
                                 const SkinKey *keyB, uint32_t strict)
{
    if (!part || !keyB) return nullptr;
    if ((ages & RawSlot<19, uint32_t>(RawSlot<3, void *>(part))) && (genders & RawSlot<20, uint32_t>(RawSlot<3, void *>(part))) &&
        (!keyA || IsZeroKey(RawSlot<5, const SkinKey *>(part)) || SameKey(keyA, RawSlot<5, const SkinKey *>(part))) &&
        PartsFit<7>(part, parts)) {
        const SkinKey *const own = RawSlot<6, const SkinKey *>(part);
        const SkinKey *const zero = ZeroKey();
        if (SameKey(own, zero) || SameKey(keyB, RawSlot<6, const SkinKey *>(part))) return part;
        if (uint8_t(strict)) {
            const SkinKey *const k = RawSlot<6, const SkinKey *>(part);
            if (k->group || k->instance || k->resource || k->type - 1 > 4) return part;
        }
    }
    const SkinKey *const key = RawSlot<4, const SkinKey *>(part);
    GZServiceString name;
    KeyName(key, &name);
    std::set<SkinKey, SkinKeyLess> &known = mNamedKeysA[std::string(name.c_str())];
    if (!known.empty()) { // a part found for this text before, loaded (iid 0xebcf3e20)
        void *const persist = PersistSystem();
        CASRef loaded;
        for (const SkinKey &k : known) {
            if (!As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&k), kPartIid, loaded.AsPPVoid(), 0u, 0u)) continue;
            void *const candidate = loaded.Get();
            if (!(ages & RawSlot<19, uint32_t>(RawSlot<3, void *>(candidate)))) continue;
            if (!(genders & RawSlot<20, uint32_t>(RawSlot<3, void *>(candidate)))) continue;
            if (keyA && !KeyFits<5>(candidate, keyA)) continue;
            if (!KeyFits<6>(candidate, keyB)) continue;
            if (!PartsFit<7>(candidate, parts)) continue;
            if (KeysDiffer(key, RawSlot<4, const SkinKey *>(candidate))) continue;
            return candidate; // released (loaded's destructor) before the name is freed
        }
    }
    // The catalog's parts for the masks (as bytes), the keys and the part bits (FindType1).
    void *const catalog = ResourceService();
    const uint32_t mask = uint16_t(parts);
    SkinPtrVector candidates;
    RawSlot<11, void>(catalog, &candidates, uint32_t(uint8_t(ages)), uint32_t(uint8_t(genders)), keyA, keyB, 1u, 1u, mask, 0u);
    if (!candidates.empty()) {
        // One with the part's own key: the first not flagged, else the last; remembered.
        void *same = nullptr;
        for (void *candidate : candidates) {
            if (KeysDiffer(RawSlot<4, const SkinKey *>(candidate), key)) continue;
            same = candidate;
            if (!Flagged(candidate)) break;
        }
        if (same) {
            Remember(known, same);
            return same;
        }
        // Else one not flagged whose slot 6 key is keyB, or the last one not flagged.
        void *loose = nullptr;
        for (void *candidate : candidates) {
            if (Flagged(candidate)) continue;
            loose = candidate;
            if (SameKey(RawSlot<6, const SkinKey *>(candidate), keyB)) return candidate;
        }
        if (loose) return loose;
    }
    // Else, for each type the catalog has for the masks (FindType3; genders as a byte), its parts by the type's key: one with
    // the part's own key (remembered), or the last one not flagged.
    SkinPtrVector types;
    RawSlot<17, void>(catalog, &types, ages, uint32_t(uint8_t(genders)), 0xffu, 0xffffffffu, 0u);
    void *loose = nullptr;
    for (void *type : types) {
        RawSlot<11, void>(catalog, &candidates, uint32_t(uint8_t(ages)), uint32_t(uint8_t(genders)), keyA,
                    RawSlot<4, const SkinKey *>(type), 1u, 1u, mask, 0u);
        for (void *candidate : candidates) {
            if (SameKey(RawSlot<4, const SkinKey *>(candidate), key)) {
                Remember(known, candidate);
                return candidate;
            }
            if (!Flagged(candidate)) loose = candidate;
        }
    }
    return loose;
}

// 0x503580 (1069 B): a mesh overlay for the part bits `parts`, the ages / genders masks and the keys (either may be none):
// the overlay itself when its property set has a bit of each mask, its slot 7 / 8 keys are zero or the keys, and its parts
// (slot 10) are none or have a bit of `parts`; else a stand-in by its own key (slot 5): one found before (iid 0x0c1fe23e),
// or the catalog's first not flagged (else its last; FindType5), remembered.
void *cTSCASSystem::FindMeshOverlay(uint32_t parts, void *overlay, uint32_t ages, uint32_t genders, const SkinKey *keyA,
                                    const SkinKey *keyB)
{
    if (!overlay) return nullptr;
    if ((ages & RawSlot<19, uint32_t>(RawSlot<3, void *>(overlay))) && (genders & RawSlot<20, uint32_t>(RawSlot<3, void *>(overlay))) &&
        (!keyA || IsZeroKey(RawSlot<7, const SkinKey *>(overlay)) || SameKey(keyA, RawSlot<7, const SkinKey *>(overlay))) &&
        (!keyB || IsZeroKey(RawSlot<8, const SkinKey *>(overlay)) || SameKey(keyB, RawSlot<8, const SkinKey *>(overlay))) &&
        PartsFit<10>(overlay, parts))
        return overlay;
    const SkinKey *const key = RawSlot<5, const SkinKey *>(overlay);
    GZServiceString name;
    KeyName(key, &name);
    std::set<SkinKey, SkinKeyLess> &known = mNamedKeysB[std::string(name.c_str())];
    if (!known.empty()) {
        void *const persist = PersistSystem();
        CASRef loaded;
        for (const SkinKey &k : known) {
            if (!As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&k), kMeshIid, loaded.AsPPVoid(), 0u, 0u)) continue;
            void *const candidate = loaded.Get();
            if (!(ages & RawSlot<19, uint32_t>(RawSlot<3, void *>(candidate)))) continue;
            if (!(genders & RawSlot<20, uint32_t>(RawSlot<3, void *>(candidate)))) continue;
            if (keyA && !KeyFits<7>(candidate, keyA)) continue;
            if (keyB && !KeyFits<8>(candidate, keyB)) continue;
            if (!PartsFit<10>(candidate, parts)) continue;
            if (KeysDiffer(key, RawSlot<5, const SkinKey *>(candidate))) continue;
            return candidate;
        }
    }
    void *const catalog = ResourceService();
    SkinPtrVector candidates;
    RawSlot<22, void>(catalog, &candidates, ages, genders, 1u, keyA, keyB, key, 0x20u, parts);
    if (candidates.empty()) return nullptr;
    void *taken = nullptr;
    for (void *candidate : candidates) {
        taken = candidate;
        if (!Flagged(candidate)) break;
    }
    Remember(known, taken);
    return taken;
}
// 0x505fb0 (1890 B). The sim's mesh-overlay keys (kind 0x20) for a body-part bit are rewritten (its slot 82(0x20, bit), then
// slot 72(0x20, bit, &keys)) with those of its current keys (slot 42(0x20, bit)) whose mesh loads and whose slot 6 differs:
// - a zero `key`: from `excludeId`; every part bit but 0xc80 when `parts` is 13, else the bit 1 << parts; false.
// - another: from the mesh `key` names (iid 0x0c1fe23e), and only when that mesh is of type 8 (its slot 4; else, or not
//   loaded, false). The overlay FindMeshOverlay (slot 34, through the vtable) finds for the bit, the mesh and the sim's
//   ages / genders / keys (slots 22, 23, 28, 29) is added (its slot 3's slot 3 key) and the keys are stored only when it
//   finds one. True.
bool cTSCASSystem::ApplyMeshOverlay(void *sim, const SkinKey *key, uint32_t parts, uint32_t excludeId)
{
    if (!sim) return false;
    if (key->type == 0 && key->group == 0 && key->instance == 0 && key->resource == 0) {
        const auto rewrite = [&](uint32_t bit) {
            OverlayKeys kept;
            KeepOverlays(sim, bit, kept, [&](void *mesh) { return RawSlot<6>(mesh) != excludeId; });
            StoreOverlays(sim, bit, &kept);
        };
        if (parts == kAllParts) {
            for (uint32_t i = 0; i < kAllParts; ++i)
                if (!((1u << i) & kSkippedParts)) rewrite(1u << i);
        } else {
            rewrite(1u << (parts & 31));
        }
        return false;
    }
    CASRef mesh;
    const uint8_t loaded = As<ifaces::cITSPersistSystem>(PersistSystem())->GetResource(reinterpret_cast<const Key16 *>(key), kMeshIid, mesh.AsPPVoid(), 0u, 0u);
    if (!loaded) return false;
    const uint8_t result = loaded & uint8_t(RawSlot<4>(mesh.Get()) == kMeshOverlayType);
    if (!result) return false;
    const auto rewrite = [&](uint32_t bit) {
        OverlayKeys kept;
        KeepOverlays(sim, bit, kept, [&](void *other) {
            const uint32_t otherId = RawSlot<6>(other);
            return otherId != RawSlot<6>(mesh.Get());
        });
        // The arguments in the original's order of evaluation (last first).
        const SkinKey *const keyB = RawSlot<29, const SkinKey *>(sim);
        const SkinKey *const keyA = RawSlot<28, const SkinKey *>(sim);
        const uint32_t genders = RawSlot<23>(sim);
        const uint32_t ages = RawSlot<22>(sim);
        if (void *const found = FindMeshOverlay(bit, mesh.Get(), ages, genders, keyA, keyB)) {
            SkinKey scratch;
            kept.push_back(*RawSlot<3, const SkinKey *>(RawSlot<3, void *>(found), &scratch));
            StoreOverlays(sim, bit, &kept);
        }
    };
    if (parts == kAllParts) {
        for (uint32_t i = 0; mesh && i < kAllParts; ++i)
            if (!((1u << i) & kSkippedParts)) rewrite(1u << i);
    } else {
        rewrite(1u << (parts & 31));
    }
    return result != 0;
}
// 0x501f80 (__cdecl, 404 B; slots 36 and 38): the first part of `model` (its slot 93's range) whose name list (the part's
// slot 50, walked by its slot 42's cursor) holds `name` (without case): the part and the list handed out counted. Both
// are nulled first; nothing is done without a model or a name.
void __cdecl FindNamedPart(void *model, const char *name, void **part, void **names)
{
    if (!model || !name) return;
    *part = nullptr;
    *names = nullptr;
    PartRange range;
    RawSlot<93, void>(model, &range);
    for (void *const *it = range.begin; it != range.end; ++it) {
        void *const candidate = *it;
        void *const list = RawSlot<50, void *>(candidate);
        if (!list) continue;
        NameCursor cursor;
        RawSlot<42, void>(list, &cursor);
        while (!RawSlot<4, bool>(cursor.cursor.Get(), cursor.end.Get())) {
            if (_stricmp(RawSlot<5, const char *>(cursor.cursor.Get()), name) == 0) {
                *part = candidate;
                RawSlot<1, void>(candidate);
                *names = list;
                RawSlot<1, void>(list);
                return;
            }
            RawSlot<6, void>(cursor.cursor.Get());
        }
    }
}

// 0x501730 (1515 B): a sim's face shown in CAS. The UI gets the sim's kind (its slot 24, to the UI's slot 71). The sim's
// part keys (its slot 42(2, slot 41)) are loaded in turn (persist slot 5, kPartIid) until one's mesh (the part's slot 10
// key, iid 0xe519c92c; again with the part's group in the key when that fails) is ready (its slot 52) and its model
// (0xaeba70) has a "face" part (FindNamedPart): the mesh's key (slot 62 / 8 / 3) as "%d:%d:%d:" text. The face's
// entries (its slot 54 cursor) give the id of the first one named "face" (the name tables' slot 33). With `show`, a
// kind-1 sim's thumbnail (key {0xcccef852, group, 2, then 1, 0}, iid 0xaccef84c) goes to the UI with the text (slot 5),
// another sim to slot 29. With a face and an id: for a kind-1 sim its two keys (slots 28 / 29, unless zero) go to the UI's
// slots 54 / 74, the id to slot 28(id, 1) and with `show` the text to slot 48; another sim to slot 29 (without `show`)
// and slot 28(id, 0). Whether the id was shown.
bool cTSCASSystem::ShowFace(void *sim, uint32_t show)
{
    if (!sim) return false;
    void *const ids = RawSlot<5, void *>(NameIdTables());
    void *const persist = PersistSystem();
    void *const ui = CASUIService();
    RawSlot<71, void>(ui, RawSlot<24>(sim));
    const uint32_t listArg = RawSlot<41>(sim);
    const SkinKeyList *const keys = RawSlot<42, const SkinKeyList *>(sim, 2u, listArg);
    if (!keys || keys->begin == keys->end) return false;
    const bool showing = (show & 0xff) != 0;
    bool found = false;
    rz::cRZStringMember text;
    CASRef face;
    uint32_t group = 0;
    for (const SkinKey *key = keys->begin; key != keys->end && !found; ++key) {
        CASRef part;
        if (!As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(key), kPartIid, part.AsPPVoid(), 0u, 0u)) continue;
        group = key->group;
        SkinKey meshKey;
        RawSlot<10, void>(part.Get(), &meshKey);
        CASRef mesh;
        if (!As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&meshKey), kFaceMeshIid, mesh.AsPPVoid(), 0u, 0u)) {
            SkinKey grouped = meshKey;
            grouped.group = group;
            As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&grouped), kFaceMeshIid, mesh.AsPPVoid(), 0u, 0u);
        }
        if (!mesh || !RawSlot<52, bool>(mesh.Get())) continue;
        const CASRef model(ModelOf(mesh.Get()));
        if (!model) continue;
        CASRef names;
        FindNamedPart(model.Get(), kFaceName, face.AsPPVoid(), names.AsPPVoid());
        if (names) {
            found = true;
            SkinKey meshId;
            RawSlot<3, void>(RawSlot<8, void *>(RawSlot<62, void *>(mesh.Get())), &meshId);
            text.value.Sprintf("%d:%d:%d:", meshId.type, meshId.group, meshId.instance);
        }
    }
    if (!face) return false;
    bool shown = false;
    NameCursor cursor;
    RawSlot<54, void>(face.Get(), &cursor, 0u);
    uint32_t id = 0;
    if (!RawSlot<4, bool>(cursor.cursor.Get(), cursor.end.Get()) && found) {
        while (!id) {
            NameEntry entry;
            RawSlot<5, void>(cursor.cursor.Get(), &entry);
            if (_stricmp(entry.name, kFaceName) == 0) {
                uint8_t scratch[0x14];
                id = *RawSlot<33, const uint32_t *>(ids, scratch, entry.id);
            }
            RawSlot<6, void>(cursor.cursor.Get());
            if (RawSlot<4, bool>(cursor.cursor.Get(), cursor.end.Get())) break;
        }
    }
    if (showing) {
        if (RawSlot<24>(sim) == 1) {
            const SkinKey small{kThumbnailType, group, 1, 0};
            const SkinKey large{kThumbnailType, group, 2, 0};
            CASRef thumbnail;
            bool loaded = As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&large), kThumbnailIid, thumbnail.AsPPVoid(), 0u, 0u);
            if (!loaded) loaded = As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&small), kThumbnailIid, thumbnail.AsPPVoid(), 0u, 0u);
            if (loaded) RawSlot<5, void>(ui, thumbnail.Get(), text.value.ToChar());
        } else {
            RawSlot<29, void>(ui, sim, 1u, 1u, 1u, 1u, 1u, 1u);
        }
    }
    if (found && id) {
        if (RawSlot<24>(sim) == 1) {
            const SkinKey first = *RawSlot<28, const SkinKey *>(sim);
            const SkinKey second = *RawSlot<29, const SkinKey *>(sim);
            if (KeysDiffer(&first, ZeroKey())) RawSlot<54, void>(ui, &first);
            if (KeysDiffer(&second, ZeroKey())) RawSlot<74, void>(ui, &second);
            RawSlot<28, void>(ui, id, 1u);
            if (showing) RawSlot<48, void>(ui, text.value.ToChar(), 1.0f, 1u);
        } else {
            if (!showing) RawSlot<29, void>(ui, sim, 1u, 1u, 0u, 0u, 1u, 1u);
            RawSlot<28, void>(ui, id, 0u);
        }
        shown = true;
    }
    return shown;
}
// 0x504230 (1756 B; `this` unused). Flags without bit 1: when non-zero, the sim's slot 42 is asked for each kind (1, 4, 0x10,
// 8, 0x20) and each of the 13 body-part bits, and the answer is false. With bit 1: the sim's part keys (slot 42(2, slot 41))
// are loaded in turn as in ShowFace (part, mesh by the part's slot 10 key, again with the part key's group) until a ready
// mesh's model has a "face" part whose slot 53("face") gives an id (none: false). The name tables' slot 33 entry for that
// id (slot 19 first) holds the face's properties; the resource service's slot 20 lists the overlays (uncounted; the block
// only is freed) for the sim's ages, genders, kind and keys (slots 22, 23, 24, 28, 29; asked last first), 0 and 0xffff. For each of the properties' "cafOverlay<i>",
// i below atoi("cafNumOverlays") (no such entry: false): its text before the first comma read as a GUID, and the first
// overlay whose texture face (iid 0x2c1fd89d, else its mesh face 0x0c1fe23e) has that GUID (slot 5) and whose slot 3
// answers true to slot 18 makes the answer true.
bool cTSCASSystem::FindFaceOverlay(void *sim, uint32_t flags)
{
    if (!sim) return false;
    void *const persist = PersistSystem();
    const bool checkFace = (flags & kFaceOverlayCheck) != 0;
    if (!checkFace && flags) {
        for (uint32_t kind : kOverlayKinds)
            for (uint32_t bit = 0; bit < kAllParts; ++bit) RawSlot<42, void>(sim, kind, 1u << bit);
    }
    if (!checkFace) return false;
    const uint32_t listArg = RawSlot<41>(sim);
    const SkinKeyList *const keys = RawSlot<42, const SkinKeyList *>(sim, 2u, listArg);
    if (!keys || keys->begin == keys->end) return false;
    uint32_t faceId = 0;
    for (const SkinKey *key = keys->begin; !faceId; ++key) {
        if (key == keys->end) return false;
        CASRef part;
        if (!As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(key), kPartIid, part.AsPPVoid(), 0u, 0u)) continue;
        const uint32_t group = key->group;
        SkinKey meshKey;
        RawSlot<10, void>(part.Get(), &meshKey);
        CASRef mesh;
        if (!As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&meshKey), kFaceMeshIid, mesh.AsPPVoid(), 0u, 0u)) {
            SkinKey grouped = meshKey;
            grouped.group = group;
            As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&grouped), kFaceMeshIid, mesh.AsPPVoid(), 0u, 0u);
        }
        if (!mesh || !RawSlot<52, bool>(mesh.Get())) continue;
        const CASRef model(ModelOf(mesh.Get()));
        if (!model) continue;
        CASRef face;
        CASRef names;
        FindNamedPart(model.Get(), kFaceName, face.AsPPVoid(), names.AsPPVoid());
        if (face) faceId = RawSlot<53>(face.Get(), kFaceName);
    }
    void *const tables = RawSlot<5, void *>(NameIdTables());
    NameIdEntry entry;
    RawSlot<33, void>(tables, &entry, RawSlot<19>(tables, faceId));
    void *const resources = ResourceService();
    std::vector<cIGZUnknown *, GZServiceAllocator<cIGZUnknown *>> overlays; // not counted
    // The arguments in the original's order of evaluation (last first).
    const SkinKey *const keyB = RawSlot<29, const SkinKey *>(sim);
    const SkinKey *const keyA = RawSlot<28, const SkinKey *>(sim);
    const uint32_t kind = RawSlot<24>(sim);
    const uint32_t genders = RawSlot<23>(sim);
    const uint32_t ages = RawSlot<22>(sim);
    RawSlot<20, void>(resources, &overlays, ages, genders, kind, keyA, keyB, 0u, kOverlayAnyIndex);
    const char *const countText = RawSlot<24, const char *>(entry.properties, kOverlayCountName);
    if (!countText) return false;
    const int32_t count = std::atoi(countText);
    for (int32_t i = 0; i < count; ++i) {
        rz::cRZStringMember name;
        name.value.Sprintf("%s%d", kOverlayName, i);
        const char *const value = RawSlot<24, const char *>(entry.properties, name.value.ToChar());
        if (!value) continue;
        TextString text(value);
        RawString guidText;
        reinterpret_cast<cRZIniStringHelpers *>(&text.value)->SplitOff(&guidText.value, ",");
        uint32_t guid[4];
        hexparse::ParseGuidText(guid, guidText.value.ToChar());
        for (cIGZUnknown *const overlay : overlays) {
            CASRef texture;
            CASRef meshFace;
            void *matched = nullptr;
            if (RawSlot<0, bool>(overlay, kTextureOverlayIid, texture.AsPPVoid())) {
                if (std::memcmp(RawSlot<5, const void *>(texture.Get()), guid, sizeof(guid)) == 0) matched = texture.Get();
            } else if (RawSlot<0, bool>(overlay, kMeshIid, meshFace.AsPPVoid())) {
                if (std::memcmp(RawSlot<5, const void *>(meshFace.Get()), guid, sizeof(guid)) == 0) matched = meshFace.Get();
            }
            if (matched && RawSlot<18, bool>(RawSlot<3, void *>(matched))) return true;
        }
    }
    return false;
}
// 0x5028d0 (813 B): the object definition (the app's slot 24, its slot 14(id)) names a group (its slot 67's slot 11); the
// persist system lists the outfit keys of that group (slot 36, type 0xac598eac) and loads each (slot 5, iid 0xac598ea8).
// Those whose ages (slot 22) and genders (slot 23) are zero or meet the masks are kept; with two or more, a generator
// seeded with `seed` picks one, else the only one (or none). The pointer comes back uncounted: the kept references are
// all dropped before the return.
void *cTSCASSystem::PickRandomOutfit(uint32_t definitionId, uint32_t seed, uint32_t ages, uint32_t genders)
{
    void *const persist = PersistSystem();
    void *const definitions = RawSlot<24, void *>(GetCachedSingleton());
    std::vector<CASRef> kept;
    CASRef outfit;
    if (void *const definition = RawSlot<14, void *>(definitions, definitionId)) {
        const uint32_t group = RawSlot<11>(RawSlot<67, void *>(definition));
        std::vector<SkinKey, GZServiceAllocator<SkinKey>> keys;
        As<ifaces::cITSPersistSystem>(persist)->ListKeys(&keys, group, kOutfitResourceType);
        for (const SkinKey &key : keys) {
            if (!As<ifaces::cITSPersistSystem>(persist)->GetResource(reinterpret_cast<const Key16 *>(&key), kOutfitIid, outfit.AsPPVoid(), 0u, 0u)) continue;
            void *const loaded = outfit.Get();
            const bool agesMatch = RawSlot<22>(loaded) == 0 || (ages & RawSlot<22>(loaded)) != 0;
            if (agesMatch && (RawSlot<23>(loaded) == 0 || (genders & RawSlot<23>(loaded)) != 0)) kept.push_back(outfit);
            else outfit.Assign(nullptr);
        }
        if (kept.size() >= 2) {
            cRZRandom random;
            random.ConstructAt(seed, 0x502a5c); // the original's constructor call returns to 0x502a5c
            outfit = kept[random.RandomUint32Uniform(static_cast<uint32_t>(kept.size()))];
        } else if (kept.empty()) {
            outfit.Assign(nullptr);
        } else {
            const CASRef only = kept.front(); // counted once more while it is taken
            outfit = only;
        }
    }
    return outfit.Get();
}

// ---- StepChildJob 0x507600 (lane JU) ----

namespace {
constexpr uint32_t kParentLookType = 0xcccef852; // state 0: the type of the parents' resources queued for loading
constexpr uint32_t kQueueWaitMs = 60000;
constexpr uint32_t kBabyKind = 1;                // the job's age kind bit that gets a baby's hair, face and name
constexpr uint32_t kAdultAges = 0x20;            // state 2: the ages the baby's hair colour and faces are looked up for
constexpr uint32_t kAnyGenders = 0xff;
constexpr uint32_t kDataSex = 0x41, kDataOther = 0xa5; // a description's data ids (its slot 16 reads, 17 writes)
constexpr uint16_t kBabyNameTable = 0x95, kChildNameTable = 0xe6;
constexpr uint32_t kFemale = 2;                  // the outfit's genders (slot 23) for a girl

// 0x54b6e0 (38 B, __cdecl; called only by 0x507600, so in effect through it): an age kind bit's index (1, 2, 4, 8 ->
// 0..3; any other 0).
uint32_t AgeIndexOfBit(uint32_t bit)
{
    switch (bit) {
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    default: return 0;
    }
}
// 0xb5cce0 (__cdecl, game code shared with the CAS UI): the hair colour key for `key` (the resource service's slot 18
// through a hair colour filter; `key` itself when none; a fixed GUID for ages 0x10). `genders` is not read.
SkinKey *HairColorKey(SkinKey *out, uint32_t ages, uint32_t genders, const SkinKey *key)
{
    return OriginalFunction<SkinKey *(__cdecl *)(SkinKey *, uint32_t, uint32_t, const SkinKey *), 0xb5cce0>()(out, ages, genders, key);
}
// 0xb5e990 (__cdecl, game code shared with the genetics): a fixed GUID by age kind (2 / 4, 8, others) and index (0, 1, other).
SkinKey *DefaultGeneGuid(SkinKey *out, uint32_t ageKind, uint32_t index)
{
    return OriginalFunction<SkinKey *(__cdecl *)(SkinKey *, uint32_t, uint32_t), 0xb5e990>()(out, ageKind, index);
}
// A weighted gene on the stack (the genetics' record: two empty strings, weight 0xff), destroyed by 0x257860.
struct LocalGene {
    alignas(nTSGenetics::DNAWeightedGene) uint8_t storage[sizeof(nTSGenetics::DNAWeightedGene)];
    nTSGenetics::DNAWeightedGene *Get() { return reinterpret_cast<nTSGenetics::DNAWeightedGene *>(storage); }
    LocalGene()
    {
        std::memset(storage, 0, sizeof(storage));
        Get()->name.Construct();
        Get()->value.Construct();
        Get()->weight = 0xff;
    }
    ~LocalGene() { OriginalFunction<void(__thiscall *)(nTSGenetics::DNAWeightedGene *), 0x257860>()(Get()); }
    LocalGene(const LocalGene &) = delete;
    LocalGene &operator=(const LocalGene &) = delete;
};
// A vector of weighted genes (allocator service), destroyed by 0x4f5cc0.
struct LocalGenes {
    nTSGenetics::DNAWeightedVector values{};
    LocalGenes() = default;
    ~LocalGenes() { OriginalFunction<void(__thiscall *)(nTSGenetics::DNAWeightedVector *), 0x4f5cc0>()(&values); }
    LocalGenes(const LocalGenes &) = delete;
    LocalGenes &operator=(const LocalGenes &) = delete;
};
// A text of the name tables (cTSString's flagless loader 0x1d0e90, group 0x7f96c284) copied into a cRZString; the loader is
// destroyed once copied, the copy at the end of the scope.
struct TableText {
    union {
        cRZString value;
    };
    TableText(uint16_t table, uint16_t entry)
    {
        alignas(cTSString) uint8_t storage[sizeof(cTSString)];
        cTSString *const text = reinterpret_cast<cTSString *>(storage);
        value.Construct(text->Construct(table, entry, kNameTablesGroup, cTSString::kStringTableType)->mString, 0, UINT32_MAX);
        text->Destruct();
    }
    ~TableText() { value.Destruct(); }
    TableText(const TableText &) = delete;
    TableText &operator=(const TableText &) = delete;
};
int16_t Data(void *description, uint32_t id) { return RawSlot<16, int16_t>(description, id); }
// The parent a child follows: the only one with a description, else the one whose data 0xa5 is 0 while the other's is
// not (each asked again as the original does); 0 for neither, 1 for A, 2 for B.
int PickParent(void *descriptionA, void *descriptionB)
{
    if (!descriptionA) return descriptionB ? 2 : 0;
    if (!descriptionB) return 1;
    if (Data(descriptionA, kDataOther) == 0 && Data(descriptionB, kDataOther) != 0) return 1;
    if (Data(descriptionB, kDataOther) == 0 && Data(descriptionA, kDataOther) != 0) return 2;
    return 0;
}
} // namespace

// 0x507600 (3440 B). The persist system and the app (used unchecked) asked first. Each call runs the job's state and moves it
// on (the parents' descriptions are the app's factory's slot 16(parent, 1)):
//   0: the description's slot 10 held in ref18; for a baby with both parents, each parent's look resource (type 0xcccef852,
//      group its slot 10's slot 67's slot 11) queued for loading;
//   1: the child's sex: the opposite of the parent PickParent names (data 0x41), else a coin (but the one parent's when both
//      are the same); the description's data (0x3a, 0xba the age index, 0x41, 0x52 / 0x51), the outfit object made (the
//      framework's class 0xac598eac) and set up from the job (ages 0x20, genders, the kind), cached;
//   2: the UI told; filed with the app's genetics manager (its slot 27) under "<ref18's slot 28>" (the parents' ids, the one PickParent names in
//      both places); with the entry found: a baby's hair colour, face and a random face of the adult faces the resource
//      service has (by key text), else, when the outfit has no genes, a default gene set on it and the entry;
//   3: the outfit's tidy-up; 4: the child's name "<first> - <age / sex title>" (string tables 0x95 / 0xe6), the parents'
//      last name, the app's hook; 5: a baby handed to the app's world; 6: done (false).
bool cTSCASSystem::StepChildJob(ChildJob *job)
{
    void *const persist = PersistSystem();
    void *const app = GetCachedSingleton();
    const auto simId = [job] { return uint32_t(RawSlot<8, uint16_t>(job->sim.Get())); };
    const auto setData = [job](uint32_t id, uint32_t value) { RawSlot<17, void>(job->sim.Get(), id, value); };
    switch (job->state) {
    case 0: {
        ++job->state;
        job->ref18.Assign(RawSlot<10, cIGZUnknown *>(job->sim.Get()));
        void *const factory = RawSlot<10, void *>(app);
        void *const parentA = RawSlot<16, void *>(factory, job->parentA, 1u);
        void *const parentB = RawSlot<16, void *>(factory, job->parentB, 1u);
        if (parentA && parentB && job->kind == kBabyKind) {
            const SkinKey lookA = {kParentLookType, RawSlot<11, uint32_t>(RawSlot<67, void *>(RawSlot<10, void *>(parentA))), 1, 0};
            const SkinKey lookB = {kParentLookType, RawSlot<11, uint32_t>(RawSlot<67, void *>(RawSlot<10, void *>(parentB))), 1, 0};
            As<ifaces::cITSPersistSystem>(persist)->QueueResourceLoad(reinterpret_cast<const Key16 *>(&lookA), 0u, kQueueWaitMs, 0u, 0u);
            As<ifaces::cITSPersistSystem>(persist)->QueueResourceLoad(reinterpret_cast<const Key16 *>(&lookB), 0u, kQueueWaitMs, 0u, 0u);
        }
        return true;
    }
    case 1: {
        ++job->state;
        void *const factory = RawSlot<10, void *>(app);
        void *const parentA = RawSlot<16, void *>(factory, job->parentA, 1u);
        void *const parentB = RawSlot<16, void *>(factory, job->parentB, 1u);
        void *follow = nullptr;
        uint32_t genders = 0;
        switch (PickParent(parentA, parentB)) {
        case 1:
            follow = parentA;
            mRandom.RandomDoubleUniform(); // a draw the original throws away
            break;
        case 2:
            follow = parentB;
            mRandom.RandomDoubleUniform();
            break;
        default:
            genders = mRandom.RandomDoubleUniform() > 0.5 ? 2 : 1;
            if (job->parentA == job->parentB && parentA) follow = parentA;
            break;
        }
        if (follow) genders = Data(follow, kDataSex) == 1 ? 2 : 1;
        setData(0x3a, 1);
        setData(0xba, AgeIndexOfBit(job->kind));
        setData(kDataSex, genders == 1 ? 1u : 0u);
        setData(0x52, 500);
        setData(0x51, 0);
        void *const framework = GetDirectorFrameworkObject();
        RawSlot<3, void>(framework, kOutfitResourceType, kOutfitIid, job->ref1c.AsPPVoid());
        RawSlot<50, void>(job->ref1c.Get(), kAdultAges);
        RawSlot<51, void>(job->ref1c.Get(), genders);
        RawSlot<52, void>(job->ref1c.Get(), job->kind);
        RawSlot<54, void>(job->ref1c.Get(), 1.0f);
        RawSlot<67, void>(job->ref1c.Get(), 0u);
        RawSlot<8, void>(job->ref1c.Get(), kOutfitResourceType);
        RawSlot<10, void>(job->ref1c.Get(), 1u);
        void *const source = RawSlot<67, void *>(job->ref18.Get());
        void *const outfit = job->ref1c.Get();
        RawSlot<9, void>(outfit, RawSlot<11, uint32_t>(source));
        RawSlot<89, void>(job->ref1c.Get());
        void *const manager = As<ifaces::cITSPersistSystem>(persist)->GetResourceManager();
        RawSlot<8, void>(manager, job->ref1c.Get());
        return true;
    }
    case 2: {
        ++job->state;
        void *const genetics = // receiver: nTSGenetics::cTSGeneticsManager
            RawSlot<27, void *>(app);
        void *const ui = CASUIService();
        RawSlot<71, void>(ui, RawSlot<24, uint32_t>(job->ref1c.Get()));
        RawSlot<23, uint32_t>(job->ref1c.Get()); // asked, not used
        void *idA = job->parentA;
        void *idB = job->parentB;
        void *const factory = RawSlot<10, void *>(app);
        void *const parentA = RawSlot<16, void *>(factory, idA, 1u);
        void *const parentB = RawSlot<16, void *>(factory, idB, 1u);
        switch (PickParent(parentA, parentB)) {
        case 1: idB = idA; break;
        case 2: idA = idB; break;
        }
        rz::cRZStringMember name;
        name.value.Sprintf("%d", RawSlot<28, uint32_t>(job->ref18.Get()));
        RawSlot<10, void>(genetics, job->kind, idA, idB, simId(), name.value.mString.c_str(), 1u);
        CASRef entry;
        if (RawSlot<4, bool>(genetics, simId(), entry.AsPPVoid())) {
            RawSlot<16, void>(entry.Get(), job->ref1c.Get());
            RawSlot<16, void>(ui);
            void *const outfit = job->ref1c.Get();
            if (job->kind == kBabyKind) {
                // The baby's hair colour (of the outfit's hair key) and face, then a random face of the adult ones of its
                // age kind, by the text of their keys.
                SkinKey hair;
                const SkinKey *const hairKey = RawSlot<29, const SkinKey *>(outfit);
                HairColorKey(&hair, kAdultAges, RawSlot<23, uint32_t>(outfit), hairKey);
                RawSlot<54, void>(ui, RawSlot<28, const SkinKey *>(job->ref1c.Get()));
                RawSlot<74, void>(ui, &hair);
                GZServiceString text;
                KeyName(RawSlot<30, const SkinKey *>(job->ref1c.Get()), &text);
                RawSlot<38, void>(ui, text.c_str(), 1u, 0u);
                SkinPtrVector faces; // not counted
                void *const resources = ResourceService();
                const SkinKey skin = *RawSlot<28, const SkinKey *>(job->ref1c.Get());
                const uint32_t ageKind = RawSlot<24, uint32_t>(job->ref1c.Get());
                RawSlot<25, void>(resources, &faces, kAdultAges, kAnyGenders, ageKind, &skin, &hair, 0u, 0xffffu, 0xffffu);
                std::vector<std::string> names; // the original's vector of cRZString
                for (void *const face : faces) {
                    if (RawSlot<4, uint32_t>(face) != 1) continue;
                    if (RawSlot<25, uint8_t>(RawSlot<3, void *>(face)) & 1) continue;
                    KeyName(RawSlot<5, const SkinKey *>(face), &text);
                    names.emplace_back(text.c_str());
                }
                if (!names.empty())
                    RawSlot<38, void>(ui, names[mRandom.RandomUint32Uniform(uint32_t(names.size()))].c_str(), 1u, 0u);
            } else {
                // An outfit without genes gets the default gene of its age kind, set on it and on the entry.
                // (slot 34's vector: only its first two words, begin and end, are read)
                const auto *const genes = RawSlot<34, const nTSGenetics::DNAWeightedVector *>(outfit);
                if (genes->begin == genes->end) {
                    LocalGene gene;
                    SkinKey guid;
                    DefaultGeneGuid(&guid, RawSlot<24, uint32_t>(job->ref1c.Get()), 0);
                    GZServiceString text;
                    KeyName(&guid, &text);
                    gene.Get()->name.mString.assign(text.c_str(), std::strlen(text.c_str()));
                    LocalGenes set;
                    set.values.InsertReallocate(set.values.begin, gene.Get());
                    RawSlot<61, void>(job->ref1c.Get(), &set.values);
                    RawSlot<10, void>(entry.Get(), &set.values, 1u);
                }
                RawSlot<29, void>(ui, job->ref1c.Get(), 1u, 1u, 1u, 1u, 1u, 0u);
            }
        }
        if (job->kind == kBabyKind) RawSlot<48, void>(ui, name.value.mString.c_str(), 1.0f, 1u);
        RawSlot<12, void>(ui, RawSlot<28, uint32_t>(job->ref18.Get()));
        return true;
    }
    case 3:
        ++job->state;
        RawSlot<84, void>(job->ref1c.Get());
        RawSlot<96, void>(job->ref1c.Get());
        RawSlot<68, void>(job->ref1c.Get(), 0x3dbu);
        RawSlot<98, void>(job->ref1c.Get(), UINT32_MAX, 8u);
        return true;
    case 4: {
        ++job->state;
        void *const names = RawSlot<54, void *>(job->ref18.Get());
        // The title: by the outfit's age kind (slot 24) and whether it is a girl (slot 23).
        const uint32_t genders = RawSlot<23, uint32_t>(job->ref1c.Get());
        uint16_t table = kChildNameTable, entry = 0;
        switch (RawSlot<24, uint32_t>(job->ref1c.Get())) {
        case 1: table = kBabyNameTable, entry = genders != kFemale ? 2 : 1; break;
        case 2:
        case 4: entry = genders == kFemale ? 0x38 : 0x37; break;
        case 8: entry = genders != kFemale ? 0x44 : 0x43; break;
        }
        TableText title(table, entry);
        rz::cRZStringMember last;
        void *const factory = RawSlot<10, void *>(app);
        void *const parentA = RawSlot<16, void *>(factory, job->parentA, 1u);
        void *const parentB = RawSlot<16, void *>(factory, job->parentB, 1u);
        if (parentA && parentB) RawSlot<7, void>(RawSlot<54, void *>(RawSlot<10, void *>(parentA)), &last.value);
        RawSlot<11, void>(names, &title.value);
        RawSlot<12, void>(names, &last.value);
        rz::cRZStringMember full;
        RawSlot<24, void>(job->ref18.Get(), &full.value);
        full.value.mString.append(" - ", 3);
        full.value.mString.append(title.value.mString.data(), title.value.mString.size());
        RawSlot<9, void>(job->ref18.Get(), full.value.mString.c_str());
        RawSlot<29, void *>(app); // asked, not used
        void *const hook = RawSlot<29, void *>(app);
        RawSlot<4, void>(hook, simId());
        return true;
    }
    case 5:
        ++job->state;
        if (RawSlot<4, void *>(app) && job->kind == kBabyKind) RawSlot<39, void>(RawSlot<4, void *>(app), job->sim.Get(), 0u, 1u);
        return true;
    case 6:
        job->done = true;
        ++job->state;
        return false;
    default:
        return false;
    }
}

// ---- StepOutfitJob 0x508c60 (lane JV) ----

namespace {
constexpr uint32_t kSegmentIid = 0x656b8f0c;     // the stored outfit (the persist system's FindSegment): a segment whose
                                                 // slot 19(&keys, 0) lists its keys (a count), slot 10 the job's result
constexpr uint32_t kOutfitHookClass = 0xcdcbe0e3, kOutfitHookIid = 0xcdcbe0dd; // state 6: the framework's object told
constexpr uint32_t kUntidyParts = 0x1fff;        // state 0: the outfit's slot 115 for a kind-1 outfit
constexpr uint32_t kHeadKind = 2;                // the part kind a kind-1 outfit gets every listed part as
constexpr uint32_t kPartKinds[4] = {2, 8, 0x3d40, 0x280}; // the part kinds other outfits walk (0xdce880)
constexpr uint32_t kInfoNameId = 0x29;           // state 0: the source's info word the names object gets
// The resource types whose keys a stored outfit drops in state 0 (each deleted through the persist system).
constexpr uint32_t kStaleTypes[11] = {0xebcf3e27, 0xac598eac, 0xe519c933, 0xfc700d40, 0x7ba3838c, 0xfc6eb1f7,
                                      0x65245517, 0x49596978, 0x1c4a276c, 0xac506764, 0x856ddbac};

ifaces::cITSPersistSystem *Persist(void *persist) { return As<ifaces::cITSPersistSystem>(persist); }
const cGZPersistResourceKey *PersistKey(const SkinKey *key) { return reinterpret_cast<const cGZPersistResourceKey *>(key); }

// The outfit resources' key for a description's source: {0xac598eac, its slot 67's slot 11, 1, 0}.
SkinKey OutfitKey(void *source)
{
    return {kOutfitResourceType, RawSlot<11, uint32_t>(RawSlot<67, void *>(source)), 1, 0};
}

// The UI's index of an outfit's ages (slot 22: 1, 2, 4, 8, 0x10, 0x20, 0x40 -> 0, 1, 2, 3, 4, 5, 7; others 3).
uint32_t AgeIndex(uint32_t ages)
{
    switch (ages) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 0x10: return 4;
    case 0x20: return 5;
    case 0x40: return 7;
    default: return 3;
    }
}

// The face of the outfit's slot 30 key (its text) handed to the UI when the key is not the zero key: shown by text (slot
// 38) when the UI has none for the ages / genders (slot 40).
void ShowFaceKey(void *ui, void *outfit, uint32_t ageIndex, uint32_t genderIndex)
{
    GZServiceString text;
    KeyName(RawSlot<30, const SkinKey *>(outfit), &text);
    const SkinKey *const zero = ZeroKey();
    if (!KeysDiffer(RawSlot<30, const SkinKey *>(outfit), zero)) return;
    if (RawSlot<40, int32_t>(ui, ageIndex, genderIndex, text.c_str()) == 0) RawSlot<38, void>(ui, text.c_str(), 1u, 0u);
}

// State 0 after the description: the outfit object (loaded when the job has none), the UI told its age kind, hair and
// face keys; when the UI shows the sim (slot 13): its face key, thumbnail and name (kind-1 outfits) or the outfit's slot 34
// (others), then slot 22. The source's name "<first> - <last>" set on it, its names told; the stored outfit's stale keys
// deleted and, for kind bit 0, the outfit set from it (slot 99) and the segment registered.
void SetUpOutfit(OutfitJob *job, void *ui, void *persist)
{
    const uint32_t group = RawSlot<11, uint32_t>(RawSlot<67, void *>(job->ref20.Get()));
    const SkinKey outfitKey{kOutfitResourceType, group, 1, 0};
    if (!job->ref24) Persist(persist)->GetResource(reinterpret_cast<const Key16 *>(&outfitKey), kOutfitIid, job->ref24.AsPPVoid(), 0u, 0u);
    const uint32_t ageIndex = AgeIndex(RawSlot<22, uint32_t>(job->ref24.Get()));
    const uint32_t genderIndex = RawSlot<23, uint32_t>(job->ref24.Get()) == 2 ? 0u : 1u;
    job->genderIndex = genderIndex;
    job->ageIndex = ageIndex;
    const SkinKey outfitHair = *RawSlot<29, const SkinKey *>(job->ref24.Get());
    const SkinKey outfitFace = *RawSlot<28, const SkinKey *>(job->ref24.Get());
    SkinKey hair = job->hasKeyA ? job->keyA : outfitHair;
    if (RawSlot<24, uint32_t>(job->ref24.Get()) == 1) {
        void *const outfit = job->ref24.Get();
        const uint32_t genders = RawSlot<23, uint32_t>(outfit);
        const uint32_t ages = RawSlot<22, uint32_t>(outfit);
        SkinKey colour;
        hair = *HairColorKey(&colour, ages, genders, &hair);
    }
    RawSlot<71, void>(ui, RawSlot<24, uint32_t>(job->ref24.Get()));
    RawSlot<74, void>(ui, &hair);
    SkinKey face = job->hasKeyB ? job->keyB : outfitFace;
    RawSlot<54, void>(ui, &face);
    const bool shown = RawSlot<13, bool>(ui, job->source);
    if (shown) {
        if (RawSlot<24, uint32_t>(job->ref24.Get()) == 1) {
            ShowFaceKey(ui, job->ref24.Get(), ageIndex, genderIndex);
            rz::cRZStringMember name;
            name.value.Sprintf("%d", job->source);
            if (RawSlot<8, bool>(ui, name.value.mString.c_str()) && !RawSlot<7, bool>(ui, name.value.mString.c_str())) {
                const SkinKey thumbKey{kThumbnailType, group, 1, 0};
                CASRef thumbnail;
                Persist(persist)->GetResource(reinterpret_cast<const Key16 *>(&thumbKey), kThumbnailIid, thumbnail.AsPPVoid(), 0u, 0u);
                if (thumbnail) RawSlot<5, void>(ui, thumbnail.Get(), name.value.mString.c_str());
            }
            RawSlot<115, void>(job->ref24.Get(), kUntidyParts);
        } else {
            void *const genes = RawSlot<34, void *>(job->ref24.Get());
            if (!RawSlot<34, bool>(ui, genes)) RawSlot<33, void>(ui, genes);
            ShowFaceKey(ui, job->ref24.Get(), ageIndex, genderIndex);
        }
        RawSlot<22, void>(ui, ageIndex, genderIndex, 1u, 0u, 0u, 0u);
    }
    rz::cRZStringMember name;
    RawSlot<24, void>(job->ref20.Get(), &name.value);
    name.value.mString.append(" - ", 3);
    rz::cRZStringMember last;
    RawSlot<49, void>(job->ref20.Get(), &last.value);
    name.value.mString.append(last.value.mString);
    RawSlot<9, void>(job->ref20.Get(), name.value.mString.c_str());
    void *const names = RawSlot<54, void *>(job->ref20.Get());
    void *const info = RawSlot<4, void *>(job->ref20.Get());
    const uint32_t nameId = RawSlot<3, uint32_t>(info, kInfoNameId);
    void *const source = RawSlot<67, void *>(job->ref20.Get());
    RawSlot<4, void>(names, source, nameId);
    RawSlot<3, void>(job->ref20.Get());
    CASRef segment;
    Persist(persist)->FindSegment(PersistKey(&outfitKey), kSegmentIid, segment.AsPPVoid());
    OverlayKeys keys;
    const uint32_t count = RawSlot<19, uint32_t>(segment.Get(), &keys, 0u);
    for (uint32_t i = 0; i < count; ++i) {
        const SkinKey *const key = keys.data() + i;
        for (const uint32_t type : kStaleTypes) {
            if (key->type != type) continue;
            Persist(persist)->DeleteResource(const_cast<SkinKey *>(key));
            break;
        }
    }
    if (shown && (job->kind & 1)) {
        const uint32_t kind = job->kind;
        const bool fresh = !(kind & 2);
        const uint32_t how = (kind & 8) ? 0u : (kind & 0x10) ? 2u : 1u;
        RawSlot<99, void>(job->ref24.Get(), segment.Get(), group, 0u, fresh, &hair, &face, how);
        Persist(persist)->RegisterSegment(As<ifaces::cIGZPersistDBSegment>(segment.Get()));
    }
}

// State 1: the parts the stored outfit lists (type 0xebcf3e27) that load and fit the outfit's ages and genders, by body
// part bit (kind-1 outfits: all as kind 2); then for each of the 13 body-part bits (0xc80 skipped but still cleared) and each
// part kind, the outfit's parts of that bit and kind cleared (slot 82) and the listed ones set (slot 70).
void SetListedParts(OutfitJob *job, void *persist)
{
    const SkinKey outfitKey = OutfitKey(job->ref20.Get());
    CASRef segment;
    Persist(persist)->FindSegment(PersistKey(&outfitKey), kSegmentIid, segment.AsPPVoid());
    std::map<uint32_t, OverlayKeys> byBit;
    OverlayKeys keys;
    const uint32_t count = RawSlot<19, uint32_t>(segment.Get(), &keys, 0u);
    for (uint32_t i = 0; i < count; ++i) {
        const SkinKey *const key = keys.data() + i;
        if (key->type != kPartType) continue;
        CASRef part;
        if (!Persist(persist)->GetResource(reinterpret_cast<const Key16 *>(key), kPartIid, part.AsPPVoid(), 0u, 0u)) continue;
        void *properties = RawSlot<3, void *>(part.Get());
        const uint32_t ages = RawSlot<22, uint32_t>(job->ref24.Get());
        if (RawSlot<19, uint32_t>(properties) != ages) continue;
        properties = RawSlot<3, void *>(part.Get());
        const uint32_t genders = RawSlot<23, uint32_t>(job->ref24.Get());
        if (RawSlot<20, uint32_t>(properties) != genders) continue;
        if (RawSlot<24, uint32_t>(job->ref24.Get()) == 1) {
            byBit[kHeadKind].push_back(*key);
        } else {
            uint32_t bits = RawSlot<22, uint32_t>(RawSlot<3, void *>(part.Get()));
            for (uint32_t bit = bits & (0 - bits); bit; bit = bits & (0 - bits)) {
                byBit[bit].push_back(*key);
                bits &= ~bit;
            }
        }
    }
    uint32_t bit = 1;
    if (RawSlot<24, uint32_t>(job->ref24.Get()) == 1) {
        for (uint32_t n = 0; n < kAllParts; ++n, bit <<= 1) {
            RawSlot<82, void>(job->ref24.Get(), kHeadKind, bit);
            if (bit & kSkippedParts) continue;
            const OverlayKeys &listed = byBit[kHeadKind];
            for (const SkinKey *k = listed.data(); k != listed.data() + listed.size(); ++k)
                RawSlot<70, void>(job->ref24.Get(), kHeadKind, bit, k);
        }
    } else {
        for (uint32_t n = 0; n < kAllParts; ++n, bit <<= 1) {
            for (const uint32_t kind : kPartKinds) {
                RawSlot<82, void>(job->ref24.Get(), kind, bit);
                if (bit & kSkippedParts) continue;
                const uint32_t kinds = RawSlot<25, uint32_t>(job->ref24.Get()) & kind;
                if (!kinds) continue;
                const OverlayKeys &listed = byBit[kinds];
                for (const SkinKey *k = listed.data(); k != listed.data() + listed.size(); ++k)
                    RawSlot<70, void>(job->ref24.Get(), kinds, bit, k);
            }
        }
    }
}
} // namespace

// 0x508c60 (3752 B). The app (used unchecked: its slot 10, the factory, and slots 26 / 27 asked), the CAS UI and the persist
// system asked first. Each call runs the job's state and moves it on:
//   0: without a description, the factory's slot 17(source) and its slot 10 (ref20; none, or its slot 65 false: the result
//      false and done); then SetUpOutfit;
//   1: SetListedParts; 2: the outfit's slot 82(0x3fff, 6);
//   3: the outfit's slot 68(-1), saved into the stored outfit's segment, which is registered;
//   4: the dialog service's slot 4 asked; for kind bits 0 / 1 the outfit handed to the resource manager and the dialogs'
//      slots 20 / 15(source, 0x20732180) told;
//   5: the outfit's record reloaded (when asked), the result from the stored outfit's slot 10; the edited sim's slot 9(source);
//   6: the framework's object 0xcdcbe0e3 told the outfit's slot 6(1); 7: done (false).
bool cTSCASSystem::StepOutfitJob(OutfitJob *job)
{
    void *const app = GetCachedSingleton();
    void *const ui = CASUIService();
    void *const persist = PersistSystem();
    void *const factory = RawSlot<10, void *>(app);
    RawSlot<26, void>(app);
    RawSlot<27, void>(app);
    switch (job->state) {
    case 0:
        ++job->state;
        if (!job->sim) {
            job->sim.Assign(RawSlot<17, cIGZUnknown *>(factory, job->source));
            job->ref20.Assign(RawSlot<10, cIGZUnknown *>(job->sim.Get()));
            if (!job->ref20 || !RawSlot<65, bool>(job->ref20.Get())) {
                job->result = false;
                return false;
            }
        }
        SetUpOutfit(job, ui, persist);
        return true;
    case 1:
        ++job->state;
        SetListedParts(job, persist);
        return true;
    case 2:
        ++job->state;
        RawSlot<82, void>(job->ref24.Get(), 0x3fffu, 6u);
        return true;
    case 3: {
        ++job->state;
        const SkinKey outfitKey = OutfitKey(job->ref20.Get());
        CASRef segment;
        Persist(persist)->FindSegment(PersistKey(&outfitKey), kSegmentIid, segment.AsPPVoid());
        RawSlot<68, void>(job->ref24.Get(), UINT32_MAX);
        Persist(persist)->SaveResource(As<ifaces::cIGZPersistResource>(job->ref24.Get()), segment.Get());
        Persist(persist)->RegisterSegment(As<ifaces::cIGZPersistDBSegment>(segment.Get()));
        return true;
    }
    case 4: {
        ++job->state;
        void *const dialogs = RawSlot<4, void *>(DialogService());
        if (!(job->kind & 3)) return true;
        RawSlot<8, void>(Persist(persist)->GetResourceManager(), job->ref24.Get());
        RawSlot<20, void>(dialogs, job->source);
        CASRef answer;
        RawSlot<15, void>(dialogs, job->source, kOutfitDialogId, answer.AsPPVoid());
        return true;
    }
    case 5: {
        ++job->state;
        SkinKey key;
        if (job->reloadRecord) {
            CASRef record;
            void **const out = record.AsPPVoid();
            Persist(persist)->FindSegment(PersistKey(RawSlot<3, const SkinKey *>(job->ref24.Get(), &key)), kOutfitResourceIid, out);
            RawSlot<7, void>(record.Get());
        }
        CASRef segment;
        Persist(persist)->FindSegment(PersistKey(RawSlot<3, const SkinKey *>(job->ref24.Get(), &key)), kSegmentIid, segment.AsPPVoid());
        const bool result = RawSlot<10, bool>(segment.Get());
        if (mEditedSim) RawSlot<9, void>(mEditedSim.Get(), job->source);
        job->result = result;
        return true;
    }
    case 6: {
        ++job->state;
        CASRef hook;
        void *const framework = GetDirectorFrameworkObject();
        RawSlot<3, void>(framework, kOutfitHookClass, kOutfitHookIid, hook.AsPPVoid());
        if (hook) {
            RawSlot<3, void>(hook.Get());
            void *const target = hook.Get();
            RawSlot<10, void>(target, RawSlot<6, uint32_t>(job->ref24.Get(), 1u));
        }
        return true;
    }
    case 7:
        ++job->state;
        return false;
    default:
        return false;
    }
}

} // namespace nTSCASCore
