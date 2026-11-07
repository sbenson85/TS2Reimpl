// cTSFamily members that go through the game's services: the singleton cached at 0x10cc288 (GetCachedSingleton), the persist
// system (0xb390c0) and the framework (0x1ae18d). The class layout is in cTSFamily.h.
#include "ts/cTSFamily.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/ComFramework.h"
#include "ifaces/FamilyFoundResource.h"
#include "ifaces/FamilyLotPart.h"
#include "ifaces/FamilyLotPartDescriptor.h"
#include "ifaces/FamilyResourceService.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/FamilyCurrentLot.h"
#include "ifaces/FamilyLiveHousehold.h"
#include "ifaces/FamilyLot.h"
#include "ifaces/FamilyMemberContainer.h"
#include "ifaces/FamilyObjectStore.h"
#include "ifaces/FamilyStringTable.h"
#include "ifaces/FamilyLotManager.h"
#include "ifaces/FamilyNeighbor.h"
#include "ifaces/FamilyNeighborManager.h"
#include "ifaces/FamilyRegistry.h"
#include "ifaces/FamilyResourceCache.h"
#include "ifaces/FamilyAppSingleton.h"
#include "ifaces/FamilyCatalogLot.h"
#include "ifaces/FamilyCatalogLotInfo.h"
#include "ifaces/FamilyLotCatalog.h"
#include "ifaces/FamilyLotObjects.h"
#include "ifaces/FamilyMemberObject.h"
#include "ifaces/FamilyPersistResourceFace.h"
#include "ifaces/NeighborPersistSystem.h"
#include "ifaces/cITSFamily.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "misc/HexParse.h"
#include "simantics/ServiceRefVector.h"

#include <cstring>

namespace {
constexpr uint32_t kCurrentLotClassId = 0x0bea28c2; // the current-lot object the funds follow (its GZCLSID)
constexpr uint32_t kUnplayableLotType = 9;          // a lot of this type has no worth (GetHomeLotWorth)
constexpr uint32_t kTextResourceType = 0x53545223;  // the family's string table (instance = id & 0x7fff)
constexpr uint32_t kRecordType = 0x46414d68;        // the family's record resource (instance = id)
constexpr uint32_t kRecordInterface = 0x7ab5bcbc;   // asked of the record resource (GetRecord)
constexpr uint32_t kFamilyTextType = 0x46414d73;    // the fallback text resource (LoadTexts)
constexpr uint32_t kStringTableInterface = 0xfb4cb0de;
constexpr uint32_t kMemberContainerType = 0x6c4f359d;      // the member container resource (instance = id)
constexpr uint32_t kMemberContainerInterface = 0x6c4f3599; // asked of it, and of the persist system for it
constexpr uint32_t kMemberContainerVersion = 0x80;
constexpr uint32_t kMemberContainerMode = 0x1d;

// A counted pointer held by a local of the original: released when it goes out of scope.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept(false) { if (object) As<cIGZUnknown>(object)->Release(); }
    // The original releases a stale pointer before reusing the slot.
    void Drop()
    {
        if (void *old = object) {
            object = nullptr;
            As<cIGZUnknown>(old)->Release();
        }
    }
};

// A member object's 16-byte resource key (its slot 21).
const FamilyKey *KeyOf(void *object) { return VirtualCall<const FamilyKey *(__thiscall *)(void *), 0x54 / 4>(object); }

ifaces::FamilyAppSingleton *App() { return As<ifaces::FamilyAppSingleton>(GetCachedSingleton()); }
ifaces::NeighborPersistSystem *PersistSystem()
{
    return As<ifaces::NeighborPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}
// 0x20df9a (not replaced yet): slot 3 of the singleton, cached at 0x10cc2ac.
ifaces::FamilyLiveHousehold *CachedLiveHousehold()
{
    return As<ifaces::FamilyLiveHousehold>(OriginalFunction<void *(__cdecl *)(), 0x20df9a>()());
}

// Whether `self` is the active family of the current lot object (null allowed).
bool IsActiveFamily(cITSFamily *self, ifaces::FamilyCurrentLot *lot, bool checkClass)
{
    ifaces::cITSFamily *active = lot->GetActiveFamily();
    if (!active) return false;
    if (checkClass && lot->GetGZCLSID() != kCurrentLotClassId) return false;
    const uint32_t activeId = active->GetFamilyId(); // asked before this family (operand order is unspecified)
    return activeId == self->GetFamilyId();
}

void ReleaseInto(void *&slot)
{
    if (void *old = slot) {
        slot = nullptr;
        As<cIGZUnknown>(old)->Release();
    }
}
} // namespace

int32_t cTSFamily::GetFunds()
{
    cITSFamily *self = this;
    ifaces::FamilyAppSingleton *app = App();
    if (ifaces::FamilyCurrentLot *lot = app->GetCurrentLot(); lot && IsActiveFamily(self, lot, true))
        return app->GetLiveHousehold()->GetFunds();
    return mFunds;
}

void cTSFamily::SetFunds(int32_t funds)
{
    cITSFamily *self = this;
    ifaces::FamilyAppSingleton *app = App();
    if (ifaces::FamilyCurrentLot *lot = app->GetCurrentLot(); lot && IsActiveFamily(self, lot, true)) {
        if (ifaces::FamilyLiveHousehold *household = app->GetLiveHousehold()) household->SetFunds(funds);
    }
    mFunds = funds;
}

uint32_t cTSFamily::GetHomeLotWorth()
{
    cITSFamily *self = this;
    const uint32_t lot = self->GetHomeLot();
    if (!lot) return 0;
    if (IsActiveFamily(self, As<ifaces::FamilyCurrentLot>(GetCachedSlot23()), false))
        return CachedLiveHousehold()->GetLotWorth(1);
    ifaces::FamilyLotManager *manager = App()->GetLotManager();
    // The original's out-buffers: two bytes at +2 and +3 and the worth at +4 of an 8-byte local.
    struct {
        uint8_t unused[2];
        uint8_t byteB;
        uint8_t byteA;
        uint32_t worth;
    } info;
    info.worth = 0;
    if (ifaces::FamilyLot *found = manager->GetLot(lot); found && found->GetLotType() == kUnplayableLotType) return 0;
    if (!manager->GetLotInfo(lot, &info.worth, &info.byteA, &info.byteB, 1)) return 0;
    return info.worth;
}

uint32_t cTSFamily::GetRegistryEntry()
{
    ifaces::FamilyAppSingleton *app = App();
    if (!app) return 0;
    ifaces::FamilyRegistry *registry = app->GetFamilyRegistry();
    if (!registry) return 0;
    return registry->GetFamilyEntry(this);
}

void cTSFamily::FlushChanges()
{
    cITSFamily *self = this;
    if (self->GetFlag79()) {
        if (ifaces::FamilyAppSingleton *app = App()) {
            if (ifaces::FamilyNeighborManager *neighbors = app->GetNeighborManager()) neighbors->OnFamilyChanged(this);
        }
        self->SetFlag79(0);
    }
    mIdCount = uint32_t(mIdSet.size());
}

void cTSFamily::FindMemberLot(uint32_t shortId, uint32_t *out)
{
    cITSFamily *self = this;
    for (uint32_t i = 0; i < mMemberIds.size(); ++i) {
        const uint32_t *id = self->GetMemberIdSlot(int32_t(i));
        if (!id) continue;
        ifaces::FamilyNeighbor *neighbor = App()->GetNeighborManager()->GetNeighbor(*id);
        if (neighbor->GetShortId() == uint16_t(shortId)) {
            *out = mBusinessLot;
            return;
        }
    }
}

uint8_t cTSFamily::HasMemberObject(void *object)
{
    // Three slots nobody has named yet lead from the object to an id.
    void *a = VirtualCall<void *(__thiscall *)(void *), 8>(object);
    void *b = VirtualCall<void *(__thiscall *)(void *), 61>(a);
    const uint32_t id = VirtualCall<uint32_t(__thiscall *)(void *), 28>(b);
    return HasMemberId(id);
}

void cTSFamily::ForgetResources()
{
    ifaces::NeighborPersistSystem *persist = PersistSystem();
    auto *face = As<ifaces::FamilyPersistResourceFace>(PersistFace());
    persist->GetResourceCache()->RemoveResource(mRecordKey);
    const uint32_t textKey[4] = {kTextResourceType, face->GetGroup(), mFamilyId & 0x7fff, 0};
    persist->GetResourceCache()->RemoveResource(const_cast<uint32_t *>(textKey));
    const uint32_t recordKey[4] = {kRecordType, face->GetGroup(), mFamilyId, 0};
    persist->GetResourceCache()->RemoveResource(const_cast<uint32_t *>(recordKey));
    mGroup1Dirty = 0;
    mGroup1Loaded = 0;
    mModified = 1;
    SetFamilyId(0xffffffff);
}

void *cTSFamily::GetRecord()
{
    if (mRecord) return mRecord;
    const uint32_t key[4] = {kRecordType, As<ifaces::FamilyPersistResourceFace>(PersistFace())->GetGroup(), mFamilyId, 0};
    ifaces::NeighborPersistSystem *persist = PersistSystem();
    ReleaseInto(mRecord);
    if (!persist->GetResource(const_cast<uint32_t *>(key), kRecordInterface, &mRecord, 0, 0)) {
        auto *framework = As<ifaces::ComFramework>(GetDirectorFrameworkObject());
        ReleaseInto(mRecord);
        framework->GetClassObject(kRecordType, kRecordInterface, &mRecord);
    }
    return mRecord;
}

namespace {
// Assigns string table entries 1 and 2 (any language) to the name and description through their own slot 16 (Assign).
void AssignTexts(cTSFamily &family, void *table)
{
    using AssignFn = void *(__thiscall *)(void *, void *);
    void *name = As<ifaces::FamilyStringTable>(table)->GetString(1, -1);
    VirtualCall<AssignFn, 16>(&family.mName, name);
    void *description = As<ifaces::FamilyStringTable>(table)->GetString(2, -1);
    VirtualCall<AssignFn, 16>(&family.mDescription, description);
    family.mModified = 0;
}
} // namespace

uint8_t cTSFamily::LoadTexts(int32_t id, uint32_t)
{
    if (id < 0) {
        mFamilyId = 0xffffffff;
        mName.mString.clear();
        mDescription.mString.clear();
        return 0;
    }
    mFamilyId = uint32_t(id);
    ifaces::NeighborPersistSystem *persist = PersistSystem();
    auto *face = As<ifaces::FamilyPersistResourceFace>(PersistFace());
    Held table;
    uint32_t textKey[4] = {kTextResourceType, face->GetGroup(), mFamilyId & 0x7fff, 0};
    table.Drop();
    bool found = false;
    if (persist->GetResource(textKey, kStringTableInterface, &table.object, 0, 0)) {
        found = true;
        AssignTexts(*this, table.object);
    }
    uint32_t fallbackKey[4] = {kFamilyTextType, face->GetGroup(), mFamilyId & 0x7fff, 0};
    if (!found) {
        table.Drop();
        if (persist->GetResource(fallbackKey, kStringTableInterface, &table.object, 0, 0)) {
            found = true;
            AssignTexts(*this, table.object);
        }
    }
    return found;
}

uint8_t cTSFamily::QueryMemberContainer(uint32_t iid, void **out, uint32_t version)
{
    if (!out) return 0;
    ifaces::NeighborPersistSystem *persist = PersistSystem();
    if (!persist) return 0;
    Held container;
    uint32_t key[4] = {kMemberContainerType, mRecordKey[1], mFamilyId, 0};
    persist->GetResource(key, kMemberContainerInterface, &container.object, 0, 0); // only the pointer is looked at
    if (!container.object) return 0;
    auto *found = As<ifaces::FamilyMemberContainer>(container.object);
    if (found->GetVersion() != version) return 0;
    found->SetMode(kMemberContainerMode);
    return As<cIGZUnknown>(container.object)->QueryInterface(iid, out);
}

namespace {
// The body of LoadFirstGroup / LoadSecondGroup: both read the same container, into their own group's maps.
uint8_t LoadGroup(cTSFamily &family, uint8_t &loaded, FamilyObjectMap &first, FamilyObjectMap &second)
{
    if (loaded || family.mFamilyId - 1 > cTSFamily::kMaxFamilyId - 2) return 1;
    loaded = 1;
    auto *store = As<ifaces::FamilyObjectStore>(CachedServiceGetter<0x10faf78, 0x0c4f252f, 0x2c4f2954>());
    Held container;
    if (family.QueryMemberContainer(kMemberContainerInterface, &container.object, kMemberContainerVersion)) {
        first.clear();
        second.clear();
        ServiceRefVector objects;
        objects.mFirst = objects.mLast = objects.mEnd = nullptr;
        store->CollectObjects(&objects, container.object, 0, 0);
        for (void **it = objects.mFirst, **end = objects.mLast; it != end; ++it) {
            void *object = *it;
            first[*KeyOf(object)].Assign(object);
        }
        objects.ReleaseAndFree();
    }
    return 1;
}
} // namespace

uint8_t cTSFamily::LoadFirstGroup() { return LoadGroup(*this, mGroup1Loaded, mGroup1First, mGroup1Second); }
uint8_t cTSFamily::LoadSecondGroup() { return LoadGroup(*this, mGroup2Loaded, mGroup2First, mGroup2Second); }

namespace {
constexpr uint32_t kMemberObjectInterface = 0x0c560f33; // a member object (key at slot 21)
constexpr uint32_t kMemberObjectClass = 0x0c560f39;     // the framework class new member objects are made of
constexpr uint32_t kLotObjectsInterface = 0xac598ea8;   // the lot-objects resource (member keys by mask)
constexpr uint32_t kLotObjectsType = 0xac598eac;
constexpr uint32_t kLotObjectsFormat = 1;
constexpr uint32_t kKeyListKind = 0x20;
constexpr int kKeyListMasks = 13;

bool ValidId(const cTSFamily &family) { return family.mFamilyId - 1 <= cTSFamily::kMaxFamilyId - 2; }

// Makes a member object through the framework into `created` (released first); the framework's answer.
bool CreateMember(Held &created)
{
    auto *framework = As<ifaces::ComFramework>(GetDirectorFrameworkObject());
    created.Drop();
    return framework->GetClassObject(kMemberObjectClass, kMemberObjectInterface, &created.object) != 0;
}

// Every key the lot objects list (kind 0x20, masks 1, 2, 4, ... 0x1000) becomes a new member object given to slot 18.
void ImportMembers(cTSFamily &family, ifaces::FamilyLotObjects *objects)
{
    uint32_t mask = kLotObjectsFormat;
    for (int i = 0; i < kKeyListMasks; ++i) {
        auto *list = static_cast<FamilyKey **>(objects->GetKeyList(kKeyListKind, mask));
        if (list && list[0] != list[1]) {
            for (FamilyKey *key = list[0]; key != list[1]; ++key) {
                Held created;
                if (CreateMember(created)) {
                    As<ifaces::FamilyMemberObject>(created.object)->SetKey(key);
                    family.InsertObjectIntoGroup2(created.object);
                }
            }
        }
        mask = mask << 1 | mask >> 31;
    }
}

// What the group inserts do with a lot-objects object (0x49c1a0 / 0x49cad0).
void ImportGroup1LotObjects(cTSFamily *family, void *objects) { family->ImportLotObjectsIntoGroup1(objects); }
void ImportGroup2LotObjects(cTSFamily *family, void *objects) { family->ImportLotObjectsIntoGroup2(objects); }

constexpr uint32_t kLotPartInterface = 0x0c1fe23e;     // asked of the persist system for kind-0x20 keys
constexpr uint32_t kKeyKinds[4] = {4, 0x10, 8, 0x20}; // the key list kinds group 1 imports, in order
constexpr uint32_t kLotPartKind = 0x20;                // its keys name lot parts whose source key is searched for
constexpr uint32_t kSkippedMasks = 0xc86;              // key list masks group 1 leaves out
constexpr uint32_t kSearchAny = 0xff;

ifaces::FamilyResourceService *ResourceService()
{
    return As<ifaces::FamilyResourceService>(CachedServiceGetter<0x10faf74, 0x4bcf1bb0, 0x0bcf1b58>());
}

// A plain vector of found objects (allocator service), freed when it goes (0x195720).
struct FoundResources : ServiceBlockVector {
    FoundResources() { mFirst = mLast = mEnd = nullptr; }
    FoundResources(const FoundResources &) = delete;
    FoundResources &operator=(const FoundResources &) = delete;
    ~FoundResources() { FreeBlock(); }
};

// Every found object's key goes into group 1 through the family's slot 10.
void InsertFoundKeys(cTSFamily &family, const FoundResources &found)
{
    for (void **at = static_cast<void **>(found.mFirst); at != static_cast<void **>(found.mLast); ++at) {
        if (!*at) continue;
        uint32_t key[4];
        void *resourceKey = As<ifaces::FamilyFoundResource>(*at)->GetPersistResource()->GetKey(key);
        family.InsertKeyIntoGroup1(static_cast<const FamilyKey *>(resourceKey));
    }
}

// hexparse::ParseHex32 (0x1b0e2c): the number up to eight hex digits spell (after spaces and an optional 0x).
uint32_t ParseHex(const char *digits) { return hexparse::ParseHex32(digits); }

// A GUID text ("{xxxxxxxx-xxxx-...}"): the dashes and braces dropped, every eight characters make one word of the key.
void ParseGuid(const char *text, uint32_t (&key)[4])
{
    char digits[9] = {};
    unsigned count = 0, words = 0;
    for (char c = *text; c != '\0'; c = *++text) {
        if (c == '-' || c == '{' || c == '}') continue;
        digits[count++] = c;
        if (count == 8) {
            digits[8] = '\0';
            const uint32_t word = ParseHex(digits);
            // ORIGINAL-BUG: the original stores every word, past the key's four into other locals (dead ones up to 64
            // digits); only the first four matter.
            if (words < 4) key[words] = word;
            ++words;
            count = 0;
        }
    }
}

// The body of InsertObjectIntoGroup1/2.
void InsertObject(cTSFamily &family, uint8_t loaded, bool firstGroup, FamilyObjectMap &first, FamilyObjectMap &second,
    uint8_t &dirty, void *object, void (*lotObjectsHelper)(cTSFamily *, void *))
{
    if (!object || !ValidId(family)) return;
    if (!loaded) {
        if (firstGroup) family.LoadFirstGroup();
        else family.LoadSecondGroup();
    }
    Held member, lotObjects;
    if (As<cIGZUnknown>(object)->QueryInterface(kMemberObjectInterface, &member.object)) {
        if (auto found = second.find(*KeyOf(member.object)); found != second.end()) {
            second.erase(found);
            dirty = 1;
        }
        if (first.find(*KeyOf(member.object)) == first.end()) {
            Held created;
            if (CreateMember(created)) {
                As<ifaces::FamilyMemberObject>(created.object)->SetKey(const_cast<FamilyKey *>(KeyOf(member.object)));
                first[*KeyOf(created.object)].Assign(created.object);
                dirty = 1;
            }
        }
    } else {
        lotObjects.Drop();
        if (As<cIGZUnknown>(object)->QueryInterface(kLotObjectsInterface, &lotObjects.object))
            lotObjectsHelper(&family, lotObjects.object);
    }
}
} // namespace

void cTSFamily::InsertObjectIntoGroup1(void *object)
{
    InsertObject(*this, mGroup1Loaded, true, mGroup1First, mGroup1Second, mGroup1Dirty, object, &ImportGroup1LotObjects);
}

void cTSFamily::InsertObjectIntoGroup2(void *object)
{
    InsertObject(*this, mGroup2Loaded, false, mGroup2First, mGroup2Second, mGroup2Dirty, object, &ImportGroup2LotObjects);
}

uint8_t cTSFamily::ImportLotObjectsIntoGroup1(void *objectsObject)
{
    if (!objectsObject || !ValidId(*this)) return 0;
    auto *objects = As<ifaces::FamilyLotObjects>(objectsObject);
    const uint32_t format = objects->GetFormat();
    if (format == kLotObjectsFormat) {
        uint32_t mask = format;
        for (int i = 0; i < kKeyListMasks; ++i) {
            if (!(mask & kSkippedMasks)) {
                for (uint32_t kind : kKeyKinds) {
                    auto *list = static_cast<FamilyKey **>(objects->GetKeyList(kind, mask));
                    if (!list) continue;
                    for (FamilyKey *key = list[0]; key != list[1]; ++key) {
                        if (kind == kLotPartKind) {
                            ifaces::FamilyResourceService *resources = ResourceService();
                            Held part;
                            ifaces::NeighborPersistSystem *persist = PersistSystem();
                            part.Drop();
                            persist->GetResource(key, kLotPartInterface, &part.object, 0, 0);
                            if (part.object) {
                                uint32_t source[4];
                                std::memcpy(source, As<ifaces::FamilyLotPart>(part.object)->GetDescriptor()->GetSourceKey(),
                                    sizeof(source));
                                FoundResources found;
                                resources->FindResources(&found, kSearchAny, kSearchAny, kLotObjectsFormat, 0, 0, source, 0, 0);
                                InsertFoundKeys(*this, found);
                            }
                        } else {
                            Held created;
                            if (CreateMember(created)) {
                                As<ifaces::FamilyMemberObject>(created.object)->SetKey(key);
                                InsertObjectIntoGroup1(created.object);
                            }
                        }
                    }
                }
            }
            mask = mask << 1 | mask >> 31;
        }
        return 1;
    }
    // Other formats list GUID names; each one's resources are searched with the lot objects' own search values.
    ifaces::FamilyResourceService *resources = ResourceService();
    auto *names = static_cast<char *const *>(objects->GetGuidNames()); // {first, last} of 0x20-byte cRZStrings
    for (uint32_t i = 0; i < uint32_t(names[1] - names[0]) / sizeof(cRZString); ++i) {
        const char *text = reinterpret_cast<cIGZString *>(names[0] + i * sizeof(cRZString))->ToChar();
        uint32_t guid[4] = {};
        ParseGuid(text, guid);
        uint32_t key[4] = {guid[0], guid[1], guid[2], guid[3]};
        FoundResources found;
        const uint32_t searchC = objects->GetFormat();
        const uint32_t searchB = objects->GetSearchB();
        const uint32_t searchA = objects->GetSearchA();
        resources->FindResources(&found, searchA, searchB, searchC, 0, 0, key, 0, 0);
        InsertFoundKeys(*this, found);
    }
    return 1;
}

uint8_t cTSFamily::ImportLotObjectsIntoGroup2(void *objects)
{
    if (!objects || !ValidId(*this)) return 0;
    if (As<ifaces::FamilyLotObjects>(objects)->GetFormat() == kLotObjectsFormat)
        ImportMembers(*this, As<ifaces::FamilyLotObjects>(objects));
    return 1;
}

void cTSFamily::InsertKeyIntoGroup1(const FamilyKey *key)
{
    if (!ValidId(*this)) return;
    if (!mGroup1Loaded) LoadFirstGroup();
    if (key->mType == kLotObjectsType) {
        ifaces::NeighborPersistSystem *persist = PersistSystem();
        Held objects;
        persist->GetResource(const_cast<FamilyKey *>(key), kLotObjectsInterface, &objects.object, 0, 0);
        ImportLotObjectsIntoGroup1(objects.object); // the original inlines it
        return;
    }
    if (auto found = mGroup1Second.find(*key); found != mGroup1Second.end()) {
        mGroup1Second.erase(found);
        mGroup1Dirty = 1;
    }
    if (mGroup1First.find(*key) != mGroup1First.end()) return;
    Held created;
    CreateMember(created); // only the pointer is looked at
    if (created.object) {
        As<ifaces::FamilyMemberObject>(created.object)->SetKey(const_cast<FamilyKey *>(key));
        mGroup1First[*key].Assign(created.object);
        mGroup1Dirty = 1;
    }
}

void cTSFamily::InsertKeyIntoGroup2(const FamilyKey *key)
{
    if (!ValidId(*this)) return;
    if (!mGroup2Loaded) LoadSecondGroup();
    if (key->mType == kLotObjectsType) {
        ifaces::NeighborPersistSystem *persist = PersistSystem();
        Held objects;
        persist->GetResource(const_cast<FamilyKey *>(key), kLotObjectsInterface, &objects.object, 0, 0);
        if (objects.object && ValidId(*this) && As<ifaces::FamilyLotObjects>(objects.object)->GetFormat() == kLotObjectsFormat)
            ImportMembers(*this, As<ifaces::FamilyLotObjects>(objects.object));
        return;
    }
    if (auto found = mGroup2Second.find(*key); found != mGroup2Second.end()) {
        mGroup2Second.erase(found);
        mGroup2Dirty = 1;
    }
    if (mGroup2First.find(*key) != mGroup2First.end()) return;
    Held created;
    CreateMember(created); // only the pointer is looked at
    if (created.object) {
        As<ifaces::FamilyMemberObject>(created.object)->SetKey(const_cast<FamilyKey *>(key));
        mGroup2First[*key].Assign(created.object);
        mGroup2Dirty = 1;
    }
}

void cTSFamily::ImportLotIntoGroup1(uint32_t lot)
{
    ifaces::FamilyAppSingleton *app = App();
    ifaces::NeighborPersistSystem *persist = PersistSystem();
    if (!app || !persist || !ValidId(*this)) return;
    ifaces::FamilyLotCatalog *catalog = app->GetLotCatalog();
    if (!catalog) return;
    ifaces::FamilyCatalogLot *found = catalog->GetLot(lot);
    if (!found || !found->IsReady()) return;
    uint32_t key[4] = {kLotObjectsType, found->GetInfo()->GetGroupId(), 1, 0};
    Held objects;
    if (persist->GetResource(key, kLotObjectsInterface, &objects.object, 0, 0))
        ImportLotObjectsIntoGroup1(objects.object); // the original inlines it
}

void cTSFamily::ImportLotObjects(uint32_t lot)
{
    ifaces::FamilyAppSingleton *app = App();
    ifaces::NeighborPersistSystem *persist = PersistSystem();
    if (!app || !persist || !ValidId(*this)) return;
    ifaces::FamilyLotCatalog *catalog = app->GetLotCatalog();
    if (!catalog) return;
    ifaces::FamilyCatalogLot *found = catalog->GetLot(lot);
    if (!found || !found->IsReady()) return;
    uint32_t key[4] = {kLotObjectsType, found->GetInfo()->GetGroupId(), 1, 0};
    Held objects;
    if (persist->GetResource(key, kLotObjectsInterface, &objects.object, 0, 0) && objects.object && ValidId(*this) &&
        As<ifaces::FamilyLotObjects>(objects.object)->GetFormat() == kLotObjectsFormat)
        ImportMembers(*this, As<ifaces::FamilyLotObjects>(objects.object));
}

namespace {
constexpr uint32_t kMemberQueryKind = 0xba; // what CountMembers asks 0x54ab60 about each member
constexpr uint32_t kFilterInterface = 0x0c1fe23e; // CollectFilteredObjectsOfGroup1 keeps the objects answering it

// 0x54ab60 / 0x54aa00 (__cdecl, not replaced): per-member queries through the singleton.
uint32_t MemberHas(uint32_t id, uint32_t kind) { return OriginalFunction<uint32_t(__cdecl *)(uint32_t, uint32_t), 0x54ab60>()(id, kind); }
uint32_t MemberExtra(uint32_t id) { return OriginalFunction<uint32_t(__cdecl *)(uint32_t), 0x54aa00>()(id); }
} // namespace

uint32_t cTSFamily::CountMembers(uint32_t flags)
{
    uint32_t count = 0;
    for (uint32_t *id = mMemberIds.data(), *end = id + mMemberIds.size(); id != end; ++id) {
        const bool has = MemberHas(*id, kMemberQueryKind) != 0;
        const uint32_t counted = (flags & (has ? 2u : 1u)) ? count + 1 : count;
        count = (flags & (has ? 8u : 4u)) ? counted + MemberExtra(*id) : counted;
        end = mMemberIds.data() + mMemberIds.size(); // the original rereads the end
    }
    return count;
}

void cTSFamily::CollectFilteredObjectsOfGroup1(ServiceRefVector *out)
{
    if (!mGroup1Loaded) LoadFirstGroup();
    for (auto &entry : mGroup1First) {
        void *object = entry.second.Get();
        if (object) As<cIGZUnknown>(object)->AddRef();
        {
            Held answer;
            // A null entry is asked all the same (the original crashes on it).
            if (As<cIGZUnknown>(object)->QueryInterface(kFilterInterface, &answer.object)) {
                if (out->mLast != out->mEnd) {
                    *out->mLast = object;
                    if (object) As<cIGZUnknown>(object)->AddRef();
                    ++out->mLast;
                } else {
                    void *const copy = object;
                    out->InsertGrown(out->mLast, &copy);
                }
            }
        }
        if (object) As<cIGZUnknown>(object)->Release();
    }
}

// ---- adapters (the RVA hooks; declared in cTSFamily.h) ----

using Adapters = cTSFamilyAdapters;

int32_t __fastcall Adapters::GetFunds(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFunds();
}

void __fastcall Adapters::SetFunds(Self *self, void *, int32_t funds)
{
    [[msvc::forceinline_calls]] return self->Self::SetFunds(funds);
}

uint32_t __fastcall Adapters::GetHomeLotWorth(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetHomeLotWorth();
}

uint32_t __fastcall Adapters::GetRegistryEntry(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetRegistryEntry();
}

void __fastcall Adapters::FlushChanges(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::FlushChanges();
}

void __fastcall Adapters::FindMemberLot(Self *self, void *, uint32_t shortId, uint32_t *out)
{
    [[msvc::forceinline_calls]] return self->Self::FindMemberLot(shortId, out);
}

uint8_t __fastcall Adapters::HasMemberObject(Self *self, void *, void *object)
{
    [[msvc::forceinline_calls]] return self->Self::HasMemberObject(object);
}

void __fastcall Adapters::ForgetResources(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::ForgetResources();
}

void *__fastcall Adapters::GetRecord(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetRecord();
}

uint8_t __fastcall Adapters::LoadTexts(Self *self, void *, int32_t id, uint32_t unused)
{
    [[msvc::forceinline_calls]] return self->Self::LoadTexts(id, unused);
}

uint8_t __fastcall Adapters::QueryMemberContainer(Self *self, void *, uint32_t iid, void **out, uint32_t version)
{
    [[msvc::forceinline_calls]] return self->Self::QueryMemberContainer(iid, out, version);
}

uint8_t __fastcall Adapters::LoadFirstGroup(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::LoadFirstGroup();
}

uint8_t __fastcall Adapters::LoadSecondGroup(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::LoadSecondGroup();
}

void __fastcall Adapters::InsertObjectIntoGroup1(Self *self, void *, void *object)
{
    [[msvc::forceinline_calls]] return self->Self::InsertObjectIntoGroup1(object);
}

void __fastcall Adapters::InsertObjectIntoGroup2(Self *self, void *, void *object)
{
    [[msvc::forceinline_calls]] return self->Self::InsertObjectIntoGroup2(object);
}

void __fastcall Adapters::InsertKeyIntoGroup1(Self *self, void *, const FamilyKey *key)
{
    [[msvc::forceinline_calls]] return self->Self::InsertKeyIntoGroup1(key);
}

void __fastcall Adapters::InsertKeyIntoGroup2(Self *self, void *, const FamilyKey *key)
{
    [[msvc::forceinline_calls]] return self->Self::InsertKeyIntoGroup2(key);
}

uint8_t __fastcall Adapters::ImportLotObjectsIntoGroup1(Self *self, void *, void *objects)
{
    [[msvc::forceinline_calls]] return self->Self::ImportLotObjectsIntoGroup1(objects);
}

uint8_t __fastcall Adapters::ImportLotObjectsIntoGroup2(Self *self, void *, void *objects)
{
    [[msvc::forceinline_calls]] return self->Self::ImportLotObjectsIntoGroup2(objects);
}

void __fastcall Adapters::ImportLotIntoGroup1(Self *self, void *, uint32_t lot)
{
    [[msvc::forceinline_calls]] return self->Self::ImportLotIntoGroup1(lot);
}

void __fastcall Adapters::ImportLotObjects(Self *self, void *, uint32_t lot)
{
    [[msvc::forceinline_calls]] return self->Self::ImportLotObjects(lot);
}

uint32_t __fastcall Adapters::CountMembers(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] return self->Self::CountMembers(flags);
}

void __fastcall Adapters::CollectFilteredObjectsOfGroup1(Self *self, void *, ServiceRefVector *refVector)
{
    [[msvc::forceinline_calls]] return self->Self::CollectFilteredObjectsOfGroup1(refVector);
}
