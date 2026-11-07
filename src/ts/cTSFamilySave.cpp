// cTSFamily members that save or delete the family's resources: the two member group saves, the text save and the resource
// delete. The class layout is in cTSFamily.h; the loaders these mirror are in cTSFamilyServices.cpp.
#include "ts/cTSFamily.h"
#include "game/game.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/ComFramework.h"
#include "ifaces/FamilyMemberContainer.h"
#include "ifaces/FamilyMemberObject.h"
#include "ifaces/FamilyObjectStore.h"
#include "ifaces/FamilyRecord.h"
#include "ifaces/FamilyResourceCache.h"
#include "ifaces/FamilyStringTable.h"
#include "ifaces/NeighborDbSegment.h"
#include "ifaces/NeighborPersistSystem.h"
#include "ifaces/TokenLanguageService.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cITSFamily.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"
#include "simantics/TokenExpander.h"

namespace {
constexpr uint32_t kTextResourceType = 0x53545223;         // the family's string table (instance = id & 0x7fff)
constexpr uint32_t kRecordType = 0x46414d68;               // the family's record resource (instance = id)
constexpr uint32_t kStringTableInterface = 0xfb4cb0de;
constexpr uint32_t kStringTableClass = 0xdb4cb0f9;         // the framework class of a new string table
constexpr uint32_t kMemberContainerType = 0x6c4f359d;      // the member container resource (also its framework class)
constexpr uint32_t kMemberContainerInterface = 0x6c4f3599;
constexpr uint32_t kMemberContainerMode = 0x1d;
constexpr uint32_t kFirstGroupVersion = 0x80;              // the container versions of the two groups
constexpr uint32_t kSecondGroupVersion = 0x10;
constexpr uint32_t kMemberObjectInterface = 0x0c560f33;
constexpr uint32_t kMemberObjectClass = 0x0c560f39;
constexpr uint32_t kRecordSegmentId = 0x656b8f0c;          // asked of the persist system with the record key
constexpr uint32_t kTextLanguageAny = 0xffffffff;          // SetString language: every language
constexpr uint32_t kTextLanguageDefault = 0;
constexpr uint32_t kLanguageEnglish = 1;                   // (tentative) the language id that needs no second copy
constexpr uint32_t kNameIndex = 1, kDescriptionIndex = 2;  // string table entries

// A counted pointer held by a local of the original: released when it goes out of scope.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept(false) { if (object) As<cIGZUnknown>(object)->Release(); }
    void Drop()
    {
        if (void *old = object) {
            object = nullptr;
            As<cIGZUnknown>(old)->Release();
        }
    }
};

ifaces::NeighborPersistSystem *PersistSystem()
{
    return As<ifaces::NeighborPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}
ifaces::FamilyObjectStore *ObjectStore()
{
    return As<ifaces::FamilyObjectStore>(CachedServiceGetter<0x10faf78, 0x0c4f252f, 0x2c4f2954>());
}
ifaces::ComFramework *Framework() { return As<ifaces::ComFramework>(GetDirectorFrameworkObject()); }
ifaces::cIGZPersistResource *Resource(void *object) { return As<ifaces::cIGZPersistResource>(object); }

// The body of SaveFirstGroup / SaveSecondGroup.
uint8_t SaveGroup(cTSFamily &family, uint8_t force, uint8_t &dirty, const uint8_t &loaded, bool firstGroup,
    uint32_t version, FamilyObjectMap &first, FamilyObjectMap &second)
{
    dirty |= force;
    if (!dirty) return 1;
    if (!loaded) {
        if (firstGroup) family.LoadFirstGroup();
        else family.LoadSecondGroup();
    }
    ifaces::FamilyObjectStore *store = ObjectStore();
    Held container;
    family.QueryMemberContainer(kMemberContainerInterface, &container.object, version);
    Held segment;
    ifaces::NeighborPersistSystem *persist = PersistSystem();
    segment.Drop();
    persist->FindSegment(family.mRecordKey, kRecordSegmentId, &segment.object);
    bool haveContainer = container.object != nullptr;
    if (!haveContainer) {
        ifaces::ComFramework *framework = Framework();
        container.Drop();
        if (framework->GetClassObject(kMemberContainerType, kMemberContainerInterface, &container.object)) {
            auto *created = As<ifaces::FamilyMemberContainer>(container.object);
            created->SetVersion(version);
            created->SetMode(kMemberContainerMode);
            Resource(container.object)->SetGroup(family.mRecordKey[1]);
            Resource(container.object)->SetInstance(family.mFamilyId);
            store->AddResource(container.object, segment.object);
        }
        haveContainer = container.object != nullptr;
    }
    if (haveContainer) {
        // Every member is stored as a fresh copy in the container.
        for (auto &entry : first) {
            void *member = entry.second.Get();
            Held copy;
            ifaces::ComFramework *framework = Framework();
            copy.Drop();
            if (!framework->GetClassObject(kMemberObjectClass, kMemberObjectInterface, &copy.object)) continue;
            auto *made = As<ifaces::FamilyMemberObject>(copy.object);
            made->SetKey(As<ifaces::FamilyMemberObject>(member)->GetKey());
            uint32_t key[4], containerKey[4];
            Resource(copy.object)->SetKey(Resource(member)->GetKey(key));
            Resource(copy.object)->SetGroup(family.mRecordKey[1]);
            made->SetContainerKey(Resource(container.object)->GetKey(containerKey));
            if (store->SaveResource(copy.object, segment.object, 1) && Resource(member)->GetInstance() == 0) {
                uint32_t copyKey[4], ownerKey[4];
                Resource(member)->SetKey(Resource(copy.object)->GetKey(copyKey));
                As<ifaces::FamilyMemberObject>(member)->SetContainerKey(Resource(container.object)->GetKey(ownerKey));
            }
        }
        // The removed members of the family's group leave the store.
        for (auto &entry : second) {
            void *member = entry.second.Get();
            const uint32_t group = family.mRecordKey[1];
            if (Resource(member)->GetGroup() != group) continue;
            uint32_t containerKey[4];
            As<ifaces::FamilyMemberObject>(member)->SetContainerKey(Resource(container.object)->GetKey(containerKey));
            store->RemoveResource(member);
        }
    }
    second.clear();
    dirty = 0;
    return 1;
}
} // namespace

uint8_t cTSFamily::SaveFirstGroup(uint8_t force)
{
    return SaveGroup(*this, force, mGroup1Dirty, mGroup1Loaded, true, kFirstGroupVersion, mGroup1First, mGroup1Second);
}

uint8_t cTSFamily::SaveSecondGroup(uint8_t force)
{
    return SaveGroup(*this, force, mGroup2Dirty, mGroup2Loaded, false, kSecondGroupVersion, mGroup2First, mGroup2Second);
}

uint8_t cTSFamily::DeleteResources()
{
    ifaces::NeighborPersistSystem *persist = PersistSystem();
    ifaces::cIGZPersistResource *face = Resource(PersistFace());
    uint32_t textKey[4] = {kTextResourceType, 0, mFamilyId & 0x7fff, 0};
    textKey[1] = face->GetGroup();
    Held segment;
    ifaces::FamilyResourceCache *cache = persist->GetResourceCache();
    segment.Drop();
    if (cache->FindSegmentOf(textKey, &segment.object)) As<ifaces::NeighborDbSegment>(segment.object)->DeleteRecord(textKey);
    uint32_t recordKey[4] = {kRecordType, 0, mFamilyId, 0};
    recordKey[1] = face->GetGroup();
    cache = persist->GetResourceCache();
    segment.Drop();
    // ORIGINAL-BUG: the record's segment is asked to delete the text key, not the record key.
    if (cache->FindSegmentOf(recordKey, &segment.object)) As<ifaces::NeighborDbSegment>(segment.object)->DeleteRecord(textKey);

    if (!mGroup1Loaded) LoadFirstGroup();
    ifaces::FamilyObjectStore *store = ObjectStore();
    Held container;
    QueryMemberContainer(kMemberContainerInterface, &container.object, kFirstGroupVersion);
    if (container.object) {
        for (auto &entry : mGroup1First) {
            void *member = entry.second.Get();
            if (!Resource(member)->GetGroup()) continue;
            uint32_t containerKey[4];
            As<ifaces::FamilyMemberObject>(member)->SetContainerKey(Resource(container.object)->GetKey(containerKey));
            store->RemoveResource(member);
        }
        mGroup1First.clear();
        for (auto &entry : mGroup1Second) {
            void *member = entry.second.Get();
            if (!Resource(member)->GetGroup()) continue;
            Resource(member)->SetGroup(mRecordKey[1]);
            uint32_t containerKey[4];
            As<ifaces::FamilyMemberObject>(member)->SetContainerKey(Resource(container.object)->GetKey(containerKey));
            store->RemoveResource(member);
        }
        mGroup1Second.clear();
        store->RemoveContainer(container.object);
    }
    return 1;
}

uint8_t cTSFamily::SaveTexts(uint32_t)
{
    if (mFamilyId == 0xffffffff) return 0;
    ifaces::NeighborPersistSystem *persist = PersistSystem();
    if (!persist) return 0;
    if (!persist->SaveResource(GetPersistFace(), nullptr)) return 0;
    ifaces::cIGZPersistResource *face = Resource(PersistFace());
    face->GetGroup(); // the original asks twice and drops the first answer
    Held table;
    uint32_t textKey[4] = {kTextResourceType, 0, mFamilyId & 0x7fff, 0};
    textKey[1] = face->GetGroup();
    table.Drop();
    if (!persist->GetResource(textKey, kStringTableInterface, &table.object, 0, 0)) {
        ifaces::ComFramework *framework = Framework();
        table.Drop();
        framework->GetClassObject(kStringTableClass, kStringTableInterface, &table.object);
        As<ifaces::FamilyStringTable>(table.object)->GetPersistResource()->SetKey(textKey);
        mModified = 1;
    }
    uint8_t saved = 1;
    if (mModified) {
        auto *strings = As<ifaces::FamilyStringTable>(table.object);
        strings->SetString(&mName, kNameIndex, int32_t(kTextLanguageAny));
        strings->SetString(&mDescription, kDescriptionIndex, int32_t(kTextLanguageAny));
        if (As<ifaces::TokenLanguageService>(tokens::GetSecondLanguageService())->GetLanguageId() != kLanguageEnglish) {
            strings->SetString(&mName, kNameIndex, kTextLanguageDefault);
            strings->SetString(&mDescription, kDescriptionIndex, kTextLanguageDefault);
        }
        const uint32_t failed = strings->Commit();
        mModified = 0;
        saved = failed == 0;
    }
    if (mRecord) {
        uint32_t recordKey[4] = {kRecordType, 0, mFamilyId, 0};
        recordKey[1] = face->GetGroup();
        As<ifaces::FamilyRecord>(mRecord)->GetPersistResource()->SetKey(recordKey);
        saved &= persist->SaveResource(As<ifaces::FamilyRecord>(mRecord)->GetPersistResource(), nullptr);
    }
    return saved;
}

// ---- adapters (the RVA hooks; declared in cTSFamily.h) ----

using Adapters = cTSFamilyAdapters;

uint8_t __fastcall Adapters::SaveFirstGroup(Self *self, void *, uint8_t force)
{
    [[msvc::forceinline_calls]] return self->Self::SaveFirstGroup(force);
}

uint8_t __fastcall Adapters::SaveSecondGroup(Self *self, void *, uint8_t force)
{
    [[msvc::forceinline_calls]] return self->Self::SaveSecondGroup(force);
}

uint8_t __fastcall Adapters::DeleteResources(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::DeleteResources();
}

uint8_t __fastcall Adapters::SaveTexts(Self *self, void *, uint32_t unused)
{
    [[msvc::forceinline_calls]] return self->Self::SaveTexts(unused);
}
