// cTSFamily members that answer questions about the household through other services: the move-in limits (tuning values),
// the id set copy and the lot price modifiers. The class layout is in cTSFamily.h.
#include "ts/cTSFamily.h"
#include "app/AppDebugger.h"
#include "app/AppServices2.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/FamilyAppSingleton.h"
#include "ifaces/FamilyLotEntries.h"
#include "ifaces/FamilyLotEntry.h"
#include "ifaces/FamilyLotManager.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/FamilyMemberObject.h"
#include "ifaces/FamilyNeighborManager.h"
#include "ifaces/FamilyPart.h"
#include "ifaces/FamilyPartProperties.h"
#include "ifaces/NeighborPersistSystem.h"
#include "ifaces/cIGZPropertySetString.h"
#include "ifaces/cITSFamily.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/ServiceRefVector.h"
#include "std/StdInstances.h"

namespace {
constexpr uint32_t kPropertyServiceFirstId = 0x0ac1887e; // asked of the service object for the tuning values
constexpr uint32_t kPropertyServiceSecondId = 0x4bb0d269;
constexpr uint32_t kAnyUint32VTable = 0xd9f534; // cGZAny<uint32> value objects the property service fills
constexpr uint32_t kAnyVTable = 0xd9c654;       // cIGZAny: what their destructor leaves

// CountMembers kinds (tentative: 1 humans, 2 pets, 4 / 8 their extra members).
constexpr uint32_t kKindHumans = 1, kKindHumanExtras = 4, kKindPets = 2, kKindPetExtras = 8;

enum MoveInResult : uint32_t {
    kMoveInFits = 0,
    kTooManySims = 1,
    kTooManyHumans = 2,
    kTooManyPets = 3,
    kTooManyHumansWithExtras = 4,
    kTooManyPetsWithExtras = 5,
    kTooManySimsWithExtras = 6,
    kLotsDiffer = 7,
    kNoFamily = 8,
    kNoHumans = 9,
};

// A tuning value object: a cGZAny<uint32> on the stack (vtable and value), filled by the property service.
struct AnyUint32 {
    const void *mVTable = GameAddress(kAnyUint32VTable);
    uint32_t mValue = 0;
    AnyUint32() = default;
    AnyUint32(const AnyUint32 &) = delete;
    AnyUint32 &operator=(const AnyUint32 &) = delete;
    ~AnyUint32() { mVTable = GameAddress(kAnyVTable); }
};

// The property key: a std::string built by the game's constructor (0x1773e0), its heap buffer (above 15 characters)
// freed through FreeStringBuffer (0x152b80) as the original's inlined destructor does.
struct PropertyKey {
    as2::RawString mText;
    explicit PropertyKey(const char *text)
    {
        reinterpret_cast<ServiceStringObject *>(&mText)->ConstructFromText(text); // 0x1773e0 (src/std/StdInstances.h)
    }
    PropertyKey(const PropertyKey &) = delete;
    PropertyKey &operator=(const PropertyKey &) = delete;
    ~PropertyKey()
    {
        if (mText.mCapacity > 15) appdebugger::FreeStringBuffer(&mText, mText.mPtr, mText.mCapacity);
    }
};

// The property service (held, released through 0x1cbd79 when the function leaves).
struct PropertyService {
    ScopedRef mRef{nullptr};
    PropertyService()
    {
        if (void *services = GetServiceObject()) {
            using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Query, 5>(services, kPropertyServiceFirstId, kPropertyServiceSecondId, &mRef.mObject);
        }
    }
    PropertyService(const PropertyService &) = delete;
    PropertyService &operator=(const PropertyService &) = delete;
    ~PropertyService() noexcept(false) { mRef.ReleaseObject(); }
    ifaces::cIGZPropertySetString *operator->() const { return As<ifaces::cIGZPropertySetString>(mRef.mObject); }
};
} // namespace

uint32_t cTSFamily::CheckMoveIn(cITSFamily *other)
{
    if (!other) return kNoFamily;
    PropertyService properties;
    AnyUint32 maxSims, maxHumans, maxPets;
    {
        PropertyKey key("maxTotalSims");
        properties->GetPropertyValue(&key.mText, &maxSims);
    }
    {
        PropertyKey key("maxTotalHumans");
        properties->GetPropertyValue(&key.mText, &maxHumans);
    }
    {
        PropertyKey key("maxTotalPets");
        properties->GetPropertyValue(&key.mText, &maxPets);
    }
    cITSFamily *self = this;
    auto *joining = As<ifaces::cITSFamily>(other);
    const uint32_t humans = self->CountMembers(kKindHumans);
    const uint32_t humanExtras = self->CountMembers(kKindHumanExtras);
    const uint32_t pets = self->CountMembers(kKindPets);
    const uint32_t petExtras = self->CountMembers(kKindPetExtras);
    const uint32_t otherHumans = joining->CountMembers(kKindHumans);
    const uint32_t otherHumanExtras = joining->CountMembers(kKindHumanExtras);
    const uint32_t otherPets = joining->CountMembers(kKindPets);
    const uint32_t otherPetExtras = joining->CountMembers(kKindPetExtras);
    const uint32_t businessLot = self->GetBusinessLot();
    if (businessLot != self->GetHomeLot()) return kLotsDiffer;

    const uint32_t allHumans = otherHumans + humans;
    const uint32_t allPets = otherPets + pets;
    if (allHumans > maxHumans.mValue) return kTooManyHumans;
    if (allPets > maxPets.mValue) return kTooManyPets;
    if (allPets + allHumans > maxSims.mValue) return kTooManySims;
    const uint32_t humansWithExtras = otherHumanExtras + humanExtras + allHumans;
    if (humansWithExtras > maxHumans.mValue) return kTooManyHumansWithExtras;
    const uint32_t petsWithExtras = otherPetExtras + petExtras + allPets;
    if (petsWithExtras > maxPets.mValue) return kTooManyPetsWithExtras;
    if (humansWithExtras + petsWithExtras > maxSims.mValue) return kTooManySimsWithExtras;
    return allHumans == 0 ? kNoHumans : kMoveInFits;
}

void cTSFamily::CopyIdSet(FamilyServiceIdSet *out)
{
    out->clear();
    cITSFamily *self = this;
    if (self->GetFlag79()) {
        if (auto *app = As<ifaces::FamilyAppSingleton>(GetCachedSingleton())) {
            if (ifaces::FamilyNeighborManager *neighbors = app->GetNeighborManager()) neighbors->OnFamilyChanged(this);
        }
        self->SetFlag79(0);
    }
    for (uint32_t id : mIdSet) out->insert(id);
    mIdCount = uint32_t(mIdSet.size());
}

namespace {
constexpr uint32_t kAnyBooleanVTable = 0xd9c670;     // cGZAny<bool> value objects
constexpr uint32_t kMemberObjectInterface = 0x0c560f33;
constexpr uint32_t kPartInterface = 0xebcf3e20;      // the part resource (and the interface asked of it)
constexpr uint32_t kPartUsable = 1;                  // FamilyPartProperties::GetFlags bit a counted part needs
constexpr uint32_t kKindTop = 4, kKindBottom = 0x10, kKindFullBody = 8; // GetKindFlags (tentative names)

// A boolean tuning value object (vtable and a byte), filled by the property service.
struct AnyBoolean {
    const void *mVTable = GameAddress(kAnyBooleanVTable);
    uint8_t mValue = 0;
};

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

// The vector the original works on; its elements are released and its block freed when it goes (0x38f140).
struct LocalRefVector : ServiceRefVector {
    LocalRefVector() { mFirst = mLast = mEnd = nullptr; }
    LocalRefVector(const LocalRefVector &) = delete;
    LocalRefVector &operator=(const LocalRefVector &) = delete;
    ~LocalRefVector() { ReleaseAndFree(); }
};

// 0x41d6e0: assign(first, count) of a counted-pointer vector (ServiceRefVector::AssignCounted, lane FU).
void AssignRefs(ServiceRefVector *to, void **first, uint32_t count) { to->AssignCounted(first, count); }
} // namespace

uint32_t cTSFamily::CountOutfitCombinations(uint32_t categoryMask, uint32_t secondMask, uint32_t ageMask,
    ServiceRefVector *objects)
{
    LocalRefVector list;
    if (!objects) CollectObjectsOfGroup1(&list);
    else if (objects != &list) AssignRefs(&list, objects->mFirst, uint32_t(objects->mLast - objects->mFirst));

    uint8_t allowCustomContent = 1;
    PropertyService properties;
    if (properties.mRef.mObject) {
        AnyBoolean allowed;
        uint8_t answered;
        {
            PropertyKey key("allowCustomContent");
            answered = properties->GetPropertyValue(&key.mText, &allowed);
        }
        if (answered) allowCustomContent = allowed.mValue;
    }
    auto *persist = As<ifaces::NeighborPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());

    uint32_t tops = 0, bottoms = 0, fullBodies = 0;
    Held member, part; // released part first, as the original
    for (void **at = list.mFirst; at != list.mLast; ++at) {
        void *object = *at;
        part.Drop();
        if (object) {
            member.Drop();
            if (As<cIGZUnknown>(object)->QueryInterface(kMemberObjectInterface, &member.object)) {
                part.Drop();
                persist->GetResource(As<ifaces::FamilyMemberObject>(member.object)->GetKey(), kPartInterface, &part.object, 0, 0);
            } else {
                part.Drop();
                As<cIGZUnknown>(object)->QueryInterface(kPartInterface, &part.object);
            }
        }
        if (!part.object) continue;
        auto *found = As<ifaces::FamilyPart>(part.object);
        if (!(categoryMask & found->GetProperties()->GetCategoryMask())) continue;
        if (!(secondMask & found->GetProperties()->GetSecondMask())) continue;
        if (!(found->GetProperties()->GetFlags() & kPartUsable)) continue;
        if (found->GetAgeMask() != 0 && !(ageMask & found->GetAgeMask())) continue;
        if (!allowCustomContent && found->GetProperties()->IsCustomContent()) continue;
        if (found->GetProperties()->GetKindFlags() & kKindTop) ++tops;
        else if (found->GetProperties()->GetKindFlags() & kKindBottom) ++bottoms;
        else if (found->GetProperties()->GetKindFlags() & kKindFullBody) ++fullBodies;
    }
    return bottoms * tops + fullBodies;
}

namespace {
constexpr uint32_t kDiscountEntryType = 0x74d328a8; // lot entries whose second value lowers the percent
constexpr uint32_t kMarkupEntryType = 0xf4d3289f;   // lot entries whose second value raises it
constexpr float kFullPercent = 100.0f;

// An iterator into the lot's entry vector (the lot hands them out through out-parameters).
using EntryIterator = void **;
} // namespace

int32_t cTSFamily::ScaleByLotModifiers(int32_t amount)
{
    float percent = kFullPercent;
    bool applied = false;
    auto *lots = As<ifaces::FamilyAppSingleton>(GetCachedSingleton())->GetLotManager();
    if (lots) {
        if (auto *lot = lots->GetFamilyLot(GetFamilyId())) {
            // The discounts first, then the markups; in each, the first entry with a value counts.
            for (int pass = 0; pass < 2; ++pass) {
                const uint32_t type = pass == 0 ? kDiscountEntryType : kMarkupEntryType;
                EntryIterator at, first, last;
                EntryIterator entry = *static_cast<EntryIterator *>(lot->Find(&at, type, 0, 0, -1, 0, 0));
                uint16_t next = uint16_t(entry - *static_cast<EntryIterator *>(lot->Begin(&first)));
                if (entry == *static_cast<EntryIterator *>(lot->End(&last))) continue;
                next = uint16_t(next + 1);
                for (;;) {
                    auto *modifier = As<ifaces::FamilyLotEntry>(*entry);
                    int16_t *value, *end;
                    modifier->GetValuesBegin(&value);
                    modifier->GetValuesEnd(&end);
                    bool found = false;
                    int16_t position = 1;
                    for (; value != end; ++value, ++position) {
                        if (*value != 0 && position == 2) {
                            found = applied = true;
                            if (pass == 0) percent -= float(*value);
                            else percent = float(*value) + percent;
                        }
                    }
                    if (found) break;
                    entry = *static_cast<EntryIterator *>(lot->Find(&at, type, next, 0, -1, 0, 0));
                    EntryIterator begin = *static_cast<EntryIterator *>(lot->Begin(&first));
                    EntryIterator stop = *static_cast<EntryIterator *>(lot->End(&last));
                    next = uint16_t(entry - begin + 1);
                    if (entry == stop) break;
                }
            }
        }
    }
    if (!applied) return amount;
    const float clamped = 0.0f > percent ? 0.0f : percent;
    const int32_t scaled = int32_t(clamped / kFullPercent * float(amount));
    return scaled <= 0 ? 1 : scaled;
}

// ---- adapters (the RVA hooks; declared in cTSFamily.h) ----

using Adapters = cTSFamilyAdapters;

uint32_t __fastcall Adapters::CheckMoveIn(Self *self, void *, cITSFamily *other)
{
    [[msvc::forceinline_calls]] return self->Self::CheckMoveIn(other);
}

void __fastcall Adapters::CopyIdSet(Self *self, void *, FamilyServiceIdSet *out)
{
    [[msvc::forceinline_calls]] return self->Self::CopyIdSet(out);
}

int32_t __fastcall Adapters::ScaleByLotModifiers(Self *self, void *, int32_t amount)
{
    [[msvc::forceinline_calls]] return self->Self::ScaleByLotModifiers(amount);
}

uint32_t __fastcall Adapters::CountOutfitCombinations(Self *self, void *, uint32_t categoryMask, uint32_t secondMask, uint32_t ageMask, ServiceRefVector *objects)
{
    [[msvc::forceinline_calls]] return self->Self::CountOutfitCombinations(categoryMask, secondMask, ageMask, objects);
}
