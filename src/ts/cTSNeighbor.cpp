#include "ts/cTSNeighbor.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/NbrAppSingleton.h"
#include "ifaces/NbrFramework.h"
#include "ifaces/NbrInStream.h"
#include "ifaces/NbrKindInfo.h"
#include "ifaces/NbrList.h"
#include "ifaces/NbrListHolder.h"
#include "ifaces/NbrListItem.h"
#include "ifaces/NbrOutStream.h"
#include "ifaces/NbrOwnerInfo.h"
#include "ifaces/NbrPeople.h"
#include "ifaces/NbrPerson.h"
#include "ifaces/NbrPersistService.h"
#include "ifaces/NbrPersonData.h"
#include "ifaces/NbrPersonDataCache.h"
#include "ifaces/NbrRecord.h"
#include "ifaces/NbrRelTable.h"
#include "ifaces/NbrScoreObject.h"
#include "ifaces/NbrSelf.h"
#include "ifaces/NbrSimDefinition.h"
#include "ifaces/NbrStringReader.h"
#include "ifaces/NbrTuning.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "simantics/TokenExpander.h"
#include "ts/cTSRelationshipTable.h"
#include "ts/cTSString.h"

#include <cstring>
#include <new>

namespace {
constexpr uint32_t kUnknownInterface = 1;
constexpr uint32_t kSerializableInterface = 0xe4fda3d4;
constexpr uint32_t kSelfInterface = 0x3b43bf8a;
constexpr uint32_t kPersistResourceInterface = 0x456b8f1d; // answered with the face at +4
constexpr uint32_t kStringReaderInterface = 0x3b2271c0;    // asked of an old-format stream for the name
constexpr uint32_t kNeighborClassId = 0xaace2efb;          // also the type of the neighbour's own resource
constexpr uint32_t kRecordClassId = 0x3053cf74;            // the record object's class and the type of its resource
constexpr uint32_t kRecordInterface = 0x3053cf67;
constexpr uint32_t kScoreKeyType = 0xac598eac;
constexpr uint32_t kScoreInterface = 0xac598ea8;
constexpr uint32_t kTuningId = 0x115;
constexpr uint32_t kTuningGroup = 0x5b267c89;
constexpr uint32_t kRatioTypeA = 0xcf505ab0;
constexpr uint32_t kRatioTypeB = 0x8fcd5705;
constexpr uint32_t kRelationFlags = 1;      // GetRelationValue kinds
constexpr uint32_t kRelationValue = 8;
constexpr uint32_t kRelationOther = 3;
constexpr uint32_t kRelationKnownFlag = 0x8000;
constexpr uint32_t kRelationBit3 = 8;

// The attribute counts per layout number: the first row whose minimum the layout reaches (0x40 below 3, 0x50 for 3..5).
struct LayoutCount {
    int32_t minLayout;
    uint32_t count;
};
constexpr LayoutCount kLayoutCounts[] = {
    {0x36, 0xe7}, {0x35, 0xe6}, {0x34, 0xe5}, {0x33, 0xe4}, {0x32, 0xe2}, {0x31, 0xdb}, {0x30, 0xd8}, {0x2e, 0xcc},
    {0x2d, 0xc9}, {0x2c, 0xc8}, {0x2b, 0xc8}, {0x2a, 0xc7}, {0x29, 0xc3}, {0x28, 0xc2}, {0x27, 0xc1}, {0x26, 0xc0},
    {0x25, 0xbd}, {0x24, 0xba}, {0x23, 0xb4}, {0x22, 0xb3}, {0x21, 0xb2}, {0x20, 0xaa}, {0x1f, 0xa7}, {0x1e, 0xa6},
    {0x1d, 0xa5}, {0x1c, 0xa3}, {0x1b, 0xa2}, {0x1a, 0xa1}, {0x19, 0x9e}, {0x18, 0x9d}, {0x17, 0x90}, {0x16, 0x7c},
    {0x15, 0x7b}, {0x14, 0x7a}, {0x13, 0x79}, {0x12, 0x77}, {0x11, 0x76}, {0x10, 0x70}, {0xf, 0x6f},  {0xe, 0x6b},
    {0xd, 0x68},  {0xc, 0x66},  {0xb, 0x5c},  {0xa, 0x5b},  {9, 0x59},    {8, 0x58},    {7, 0x53},    {6, 0x51},
    {3, 0x50},
};
constexpr uint32_t kLegacyCount = 0x40;

uint32_t AttributeCountFor(int32_t layout)
{
    for (const LayoutCount &row : kLayoutCounts)
        if (layout >= row.minLayout) return row.count;
    return kLegacyCount;
}

// An object held by a local of the original: released when it goes out of scope.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept(false) { if (object) As<cIGZUnknown>(object)->Release(); }
};

void ReleaseObject(void *object) { As<cIGZUnknown>(object)->Release(); }

// A counted pointer field replaced by another object: counts the new one, stores it, releases the old one. The old
// value is read when it is stored, so it sees what a lookup in between did.
void AssignCounted(void *&field, void *object)
{
    void *old = field;
    if (object == old) return;
    if (object) As<cIGZUnknown>(object)->AddRef();
    field = object;
    if (old) ReleaseObject(old);
}

// A counted pointer field that is emptied before its object is released.
void ClearCounted(void *&field)
{
    void *old = field;
    field = nullptr;
    ReleaseObject(old);
}

// The person data object for a data id (0x54aad0, an uncounted pointer).
void *LookupPersonData(uint32_t dataId)
{
    auto *app = As<ifaces::NbrAppSingleton>(GetCachedSingleton());
    auto *cache = app->GetPersonDataCache();
    return cache->GetPersonData(dataId);
}

ifaces::NbrPersistService *PersistService()
{
    return As<ifaces::NbrPersistService>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}

// The temporary the relationship query (0x55cce0) works with: a cRZUnknown-based holder (vtable 0xdd4b60, built by
// 0x55c6d0, undone by 0x55c730) of a counted pointer at +8.
struct RelationshipHolder {
    const void *mVTable;
    uint32_t mRefCount;
    void *mTarget;
};
static_assert(sizeof(RelationshipHolder) == 12);
} // namespace

// ---- the persist face ----

cGZPersistResourceKey cTSNeighbor::GetKey()
{
    cGZPersistResourceKey key;
    std::memcpy(&key, mKey, sizeof(key));
    return key;
}

bool cTSNeighbor::SetKey(const cGZPersistResourceKey &key)
{
    const uint32_t *source = &key.type;
    uint32_t copy[4];
    std::memcpy(copy, source, sizeof(copy));
    std::memcpy(mKey, copy, sizeof(copy));
    // ORIGINAL-BUG: the comparison runs after the copy, so the modified byte is set only when `key` points into the record
    // (the copy then changes the source); a real change of the key is never noticed.
    if (mKey[0] != source[0] || mKey[1] != source[1] || mKey[2] != source[2] || mKey[3] != source[3]) mModified = 1;
    return true;
}

uint32_t cTSNeighbor::GetType() { return mKey[0]; }
uint32_t cTSNeighbor::GetGroup() { return mKey[1]; }
uint32_t cTSNeighbor::GetInstance() { return mKey[2]; }

bool cTSNeighbor::SetType(uint32_t type)
{
    mKey[0] = type;
    return true;
}

bool cTSNeighbor::SetGroup(uint32_t group)
{
    mKey[1] = group;
    return true;
}

bool cTSNeighbor::SetInstance(uint32_t instance)
{
    mKey[2] = instance;
    return true;
}

uint32_t cTSNeighbor::GetRefCount() { return mRefCount; }

// ---- construction ----

cTSNeighbor::cTSNeighbor()
    : mRefCount(0), mId(0), mPersonDataId(0), mPersonData(nullptr), mRelationships(nullptr), mRecord(nullptr), mFlag20(1),
      mModified(1), mField24(0), mLayoutVersion(kLayoutVersion), mKey{0, 0, 0, 0}, mExtra20C(0), mExtra210(0)
{
    // The table in the standalone shape (lane DZ): our constructor, our vtables.
    cTSRelationshipTable *table = new cTSRelationshipTable;
    AssignCounted(mRelationships, static_cast<cITSRelationshipTable *>(table));
    std::memset(mAttributes, 0, sizeof(mAttributes));
}

cTSNeighbor *cTSNeighbor::Create() { return new (std::nothrow) cTSNeighbor; }

cTSNeighbor::~cTSNeighbor()
{
    cTSNeighbor::Detach();
    // The members' own destructors; the fields are empty unless a release above filled one again.
    if (mRecord) ReleaseObject(mRecord);
    if (mRelationships) ReleaseObject(mRelationships);
    if (mPersonData) ReleaseObject(mPersonData);
}

void cTSNeighbor::Detach()
{
    if (mPersonData) ClearCounted(mPersonData);
    if (mRelationships) {
        As<ifaces::NbrRelTable>(mRelationships)->Reset(0);
        if (mRelationships) ClearCounted(mRelationships);
    }
    if (mRecord) ClearCounted(mRecord);
}

// ---- cIGZUnknown / cIGZSerializable ----

bool cTSNeighbor::QueryInterface(uint32_t id, void **out)
{
    void *face;
    switch (id) {
    case kUnknownInterface:
    case kSerializableInterface:
    case kSelfInterface:
        face = static_cast<cITSNeighbor *>(this);
        break;
    case kPersistResourceInterface:
        face = static_cast<cIGZPersistResource *>(this); // (null for a null this, as the original)
        break;
    default:
        return false;
    }
    *out = face;
    AddRef();
    return true;
}

uint32_t cTSNeighbor::AddRef() { return ++mRefCount; }

uint32_t cTSNeighbor::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

uint32_t cTSNeighbor::GetGZCLSID() { return kNeighborClassId; }

bool cTSNeighbor::Write(ifaces::cIGZOStream *stream)
{
    auto *out = As<ifaces::NbrOutStream>(stream);
    out->SetUint32(kFormatVersion);
    out->SetSint32(kLayoutVersion);
    mLayoutVersion = kLayoutVersion;
    out->SetSint32(kLayoutVersion);
    for (unsigned i = 0; i < kAttributeCount; ++i) out->SetUint16(mAttributes[i]);
    out->SetUint16(uint16_t(mId));
    out->SetSint32(mPersonDataId);
    As<ifaces::NbrRelTable>(mRelationships)->WriteForPerson(stream, uint32_t(int32_t(mId)));
    out->SetUint8(0);
    out->SetUint32(mExtra20C);
    out->SetUint32(mExtra210);
    return out->GetError() == 0;
}

bool cTSNeighbor::Read(ifaces::cIGZIStream *stream)
{
    auto *in = As<ifaces::NbrInStream>(stream);
    uint32_t version = 0;
    in->GetUint32(&version);
    int32_t layout = 0;
    if (version >= 0x1e) {
        layout = 4;
        in->GetSint32(&layout);
        if (layout >= 2) {
            cRZString name;
            name.Construct();
            if (version < 0x6f) {
                if (version >= 0x3f) {
                    in->GetString(&name);
                } else {
                    Held reader;
                    if (in->QueryInterface(kStringReaderInterface, &reader.object))
                        As<ifaces::NbrStringReader>(reader.object)->ReadStringInto(&name);
                }
                // A scratch word of the original; never used again.
                uint32_t discarded = 0;
                in->GetSint32(&discarded);
            }
        }
        if (layout >= 1) {
            in->GetSint32(&mLayoutVersion);
            if (mLayoutVersion != 0) {
                const uint32_t count = AttributeCountFor(layout);
                for (uint32_t i = 0; i < count; ++i) in->GetUint16(&mAttributes[i]);
                if (count < kAttributeCount) std::memset(&mAttributes[count], 0, (kAttributeCount - count) * sizeof(uint16_t));
                if (count == kLegacyCount) {
                    uint32_t extra = count;
                    in->GetUint16(&extra);
                }
            }
        }
    }
    in->GetUint16(&mId);
    in->GetSint32(&mPersonDataId);
    AssignCounted(mPersonData, LookupPersonData(mPersonDataId));
    As<ifaces::NbrRelTable>(mRelationships)->ReadForPerson(stream, uint32_t(int32_t(mId)));
    if (version >= 0x70) {
        uint8_t discarded = 0;
        in->GetUint8(&discarded);
    }
    if (version >= 0x71) {
        in->GetUint32(&mExtra20C);
        in->GetUint32(&mExtra210);
    }
    return in->GetError() == 0;
}

// ---- identity and person data ----

uint8_t cTSNeighbor::Attach(int16_t id, void *personData)
{
    mId = id;
    AssignCounted(mPersonData, personData);
    mPersonDataId = As<ifaces::NbrPersonData>(mPersonData)->GetDataId();
    std::memset(mAttributes, 0, sizeof(mAttributes));
    return 1;
}

void *cTSNeighbor::GetPersonData()
{
    if (mPersonData == nullptr && mPersonDataId != 0) AssignCounted(mPersonData, LookupPersonData(mPersonDataId));
    return mPersonData;
}

void *cTSNeighbor::GetRecord(uint8_t create)
{
    if (mRecord == nullptr && create && mId != 0 && mKey[1] != 0) {
        const uint32_t key[4] = {kRecordClassId, mKey[1], uint32_t(int32_t(mId)), kNeighborClassId};
        auto *persist = PersistService();
        if (mRecord) ClearCounted(mRecord);
        if (!persist->GetResource(key, kRecordInterface, &mRecord, 0, 0)) {
            auto *framework = As<ifaces::NbrFramework>(GetDirectorFrameworkObject());
            if (mRecord) ClearCounted(mRecord);
            framework->GetClassObject(kRecordClassId, kRecordInterface, &mRecord);
            As<ifaces::NbrRecord>(mRecord)->Initialize(0);
        }
    }
    return mRecord;
}

uint16_t cTSNeighbor::GetId() { return uint16_t(mId); }
uint16_t cTSNeighbor::GetId2() { return uint16_t(mId); }

void cTSNeighbor::SetPersonData(void *personData) { AssignCounted(mPersonData, personData); }

void *cTSNeighbor::GetRelationshipTable() { return mRelationships; }

void cTSNeighbor::SetRecord(void *record) { AssignCounted(mRecord, record); }

uint32_t cTSNeighbor::GetPersonDataId() { return mPersonDataId; }

uint8_t cTSNeighbor::GetFlag20() { return mFlag20; }
void cTSNeighbor::SetFlag20(uint8_t value) { mFlag20 = value; }

uint32_t cTSNeighbor::GetRelatedCount() { return mField24; }
void cTSNeighbor::SetRelatedCount(uint32_t count) { mField24 = count; }

uint32_t cTSNeighbor::GetLayoutVersion() { return mLayoutVersion; }

cIGZPersistResource *cTSNeighbor::AsPersistResource() { return static_cast<cIGZPersistResource *>(this); }

uint8_t cTSNeighbor::AnswersNo(void *) { return 0; }

uint16_t cTSNeighbor::GetScore(uint16_t a, uint16_t b)
{
    auto *data = GetPersonData();
    auto *info = As<ifaces::NbrPersonData>(data)->GetOwnerInfo();
    const uint32_t value = As<ifaces::NbrOwnerInfo>(info)->GetValue();
    const uint32_t key[4] = {kScoreKeyType, value, 1, 0};
    Held holder;
    auto *persist = PersistService();
    if (holder.object) {
        void *old = holder.object;
        holder.object = nullptr;
        ReleaseObject(old);
    }
    uint16_t result = 0;
    if (persist->GetResource(key, kScoreInterface, &holder.object, 0, 0))
        result = As<ifaces::NbrScoreObject>(holder.object)->GetScore(a, b);
    return result;
}

// ---- attributes ----

uint16_t cTSNeighbor::GetAttribute(int32_t index)
{
    if (index >= int32_t(kAttributeCount)) return 0;
    return mAttributes[index]; // (no check below zero: a negative index reads the record's header)
}

void cTSNeighbor::SetAttribute(int32_t index, uint16_t value)
{
    if (index >= int32_t(kAttributeCount)) return;
    if (mAttributes[index] != value) {
        mAttributes[index] = value;
        mModified = 1;
    }
}

uint16_t *cTSNeighbor::GetAttributes(uint8_t keepClean)
{
    mModified |= uint8_t(keepClean ^ 1);
    return mAttributes;
}

uint32_t cTSNeighbor::GetSpeciesMask()
{
    switch (mAttributes[kSpeciesAttribute]) {
    case 1: return 2;
    case 2: return 4;
    case 3: return 8;
    default: return 1;
    }
}

uint32_t cTSNeighbor::GetGenderMask() { return mAttributes[kGenderAttribute] == 0 ? 2 : 1; }

uint32_t cTSNeighbor::GetAgeMask()
{
    const int16_t age = int16_t(mAttributes[kAgeAttribute]);
    if (age <= 1) return 0x20;
    if (age <= 2) return 1;
    if (age <= 0xf) return 2;
    if (age <= 0x12) return 4;
    if (age <= 0x32) return mAttributes[kFlagAttribute] == 0 ? 8 : 0x40;
    return 0x10;
}

uint8_t cTSNeighbor::IsHuman() { return mAttributes[kSpeciesAttribute] == 0; }
uint8_t cTSNeighbor::IsPet() { return mAttributes[kSpeciesAttribute] != 0; }
uint8_t cTSNeighbor::IsGender0() { return mAttributes[kGenderAttribute] == 0; }
uint8_t cTSNeighbor::IsGender1() { return mAttributes[kGenderAttribute] == 1; }

uint8_t cTSNeighbor::IsBaby() { return mAttributes[kSpeciesAttribute] == 0 && mAttributes[kAgeAttribute] <= 1; }
uint8_t cTSNeighbor::IsToddler() { return mAttributes[kSpeciesAttribute] == 0 && mAttributes[kAgeAttribute] == 2; }
uint8_t cTSNeighbor::IsChild()
{
    return mAttributes[kSpeciesAttribute] == 0 && uint16_t(mAttributes[kAgeAttribute] - 3) <= 0xc;
}
uint8_t cTSNeighbor::IsTeen()
{
    return mAttributes[kSpeciesAttribute] == 0 && uint16_t(mAttributes[kAgeAttribute] - 0x10) <= 2;
}
uint8_t cTSNeighbor::IsAdultWithFlag()
{
    return mAttributes[kSpeciesAttribute] == 0 && mAttributes[kFlagAttribute] != 0 &&
           uint16_t(mAttributes[kAgeAttribute] - 0x13) <= 0x1f;
}
uint8_t cTSNeighbor::IsAdult()
{
    return mAttributes[kSpeciesAttribute] == 0 && uint16_t(mAttributes[kAgeAttribute] - 0x13) <= 0x1f;
}
uint8_t cTSNeighbor::IsElder()
{
    return mAttributes[kSpeciesAttribute] == 0 && int16_t(mAttributes[kAgeAttribute]) > 0x32;
}

uint32_t cTSNeighbor::GetLifeStage()
{
    const int16_t age = int16_t(mAttributes[kAgeAttribute]);
    switch (int16_t(mAttributes[kSpeciesAttribute])) {
    case 0:
        if (age > 0x32) return 4;
        if (age > 0x12) return mAttributes[kFlagAttribute] != 0 ? 0xe : 0;
        if (age > 0xf) return 3;
        if (age > 2) return 1;
        return age > 1 ? 2 : 0xd;
    case 1:
        if (age > 0x32) return 6;
        if (age > 0x12) return 5;
        return 0xb;
    case 2:
        if (age > 0x32) return 8;
        return age <= 0x12 ? 0xb : 7;
    case 3:
        if (age > 0x32) return 0xa;
        return age > 0x12 ? 9 : 0xc;
    default:
        return 0;
    }
}

// ---- person data and relationship table queries ----

uint8_t cTSNeighbor::HasKindInfo11()
{
    if (mPersonData) {
        auto *data = As<ifaces::NbrPersonData>(mPersonData);
        if (data->GetKindInfo()) {
            auto *info = As<ifaces::NbrPersonData>(mPersonData)->GetKindInfo();
            return As<ifaces::NbrKindInfo>(info)->GetKind(9) == 0x11;
        }
    }
    return 0;
}

uint8_t cTSNeighbor::IsPersonDataFlagged()
{
    if (mPersonData && As<ifaces::NbrPersonData>(mPersonData)->IsFlagged()) return 1;
    return 0;
}

uint8_t cTSNeighbor::IsModified()
{
    if (mModified) return 1;
    return As<ifaces::NbrRelTable>(mRelationships)->IsModified() ? 1 : 0;
}

uint32_t cTSNeighbor::SetModified(uint32_t flag)
{
    mModified = uint8_t(flag);
    return As<ifaces::NbrRelTable>(mRelationships)->SetModified(flag);
}

uint32_t cTSNeighbor::GetExtraWord(int32_t which)
{
    if (which == 1) return mExtra20C;
    if (which == 2) return mExtra210;
    return 0;
}

void cTSNeighbor::SetExtraWord(int32_t which, uint32_t value)
{
    if (which == 1) mExtra20C = value;
    else if (which == 2) mExtra210 = value;
}

uint8_t cTSNeighbor::BuildDisplayName(void *sink)
{
    constexpr uint32_t kNameTable = 0x336;        // the string table the name format comes from
    constexpr uint32_t kNameGroup = 0x7f96c284;   // its group
    constexpr uint32_t kNameEntryAttribute = 0xe6; // the attribute that picks the entry
    void *person = GetPersonData();
    if (!person) {
        VirtualCall<void(__thiscall *)(void *, uint32_t, int32_t), 26>(sink, 0, -1);
        return 0;
    }
    cRZString first, last, name;
    first.Construct();
    last.Construct();
    name.Construct();
    VirtualCall<void(__thiscall *)(void *, void *), 49>(person, &first);
    void *details = VirtualCall<void *(__thiscall *)(void *), 54>(person);
    VirtualCall<void(__thiscall *)(void *, void *), 7>(details, &last);
    void *languages = tokens::GetSecondLanguageService();
    void *formatter = nullptr;
    if (languages) formatter = VirtualCall<void *(__thiscall *)(void *, uint32_t), 38>(languages, 0);
    if (formatter) {
        VirtualCall<void(__thiscall *)(void *, void *, void *, void *), 28>(formatter, &first, &last, &name);
    } else if (last.mString.empty() || first.mString.empty()) {
        const std::string &only = last.mString.empty() ? first.mString : last.mString;
        name.mString.assign(only.data(), only.size());
    } else {
        std::string joined(" ");
        joined.insert(0, first.mString.data(), first.mString.size());
        joined.append(last.mString.data(), last.mString.size());
        name.mString.assign(joined.data(), joined.size());
    }
    if (!name.mString.empty()) {
        void *cache = OriginalFunction<void *(__cdecl *)(void *), 0x54ac40>()(this);
        uint16_t entry;
        if (cache) entry = VirtualCall<uint16_t(__thiscall *)(void *, uint32_t, uint32_t), 10>(cache, kNameEntryAttribute, 1);
        else entry = GetAttribute(kNameEntryAttribute);
        if (entry) {
            cTSString format;
            format.Construct(uint16_t(kNameTable), entry, kNameGroup, cTSString::kStringTableType);
            cRZString text;
            text.Construct();
            text.mString.assign(format.mString.mString.data(), format.mString.mString.size());
            if (!text.IsEmpty()) {
                cRZString formatted;
                formatted.Construct();
                formatted.Sprintf(text.ToChar(), name.ToChar());
                name.Assign(*formatted.AsInterface());
            }
        }
    }
    VirtualCall<void(__thiscall *)(void *, void *), 16>(sink, &name);
    return !name.mString.empty();
}

void cTSNeighbor::RunRelationshipQuery(uint32_t a, uint32_t b, uint32_t c)
{
    using HolderFn = void *(__thiscall *)(RelationshipHolder *);
    using QueryFn = void *(__thiscall *)(RelationshipHolder *, uint32_t, uint32_t, uint32_t);
    struct Guard {
        RelationshipHolder holder;
        ~Guard() { OriginalFunction<HolderFn, 0x55c730>()(&holder); }
    } guard;
    OriginalFunction<HolderFn, 0x55c6d0>()(&guard.holder);
    AssignCounted(guard.holder.mTarget, this);
    OriginalFunction<QueryFn, 0x55cce0>()(&guard.holder, a, b, c);
}

// ---- the relationship level ----

int32_t cTSNeighbor::GetRelationshipLevel(void *otherObject, int32_t kind)
{
    if (!otherObject) return 0;
    cTSNeighbor *self = this; // its own slots, through its vtable as the original
    auto *other = As<ifaces::NbrSelf>(otherObject);

    auto *table = As<ifaces::NbrRelTable>(self->GetRelationshipTable());
    int32_t otherId = int16_t(other->GetId());
    const uint32_t flags = table->GetRelationValue(otherId, kRelationFlags);
    if (!(flags & kRelationKnownFlag)) return 0;

    // The person data's sim definition, counted for the length of the call.
    ifaces::NbrSimDefinition *definition = nullptr;
    if (self->GetPersonData()) {
        auto *data = self->GetPersonData();
        definition = As<ifaces::NbrPersonData>(data)->GetSimDefinition();
        if (definition) definition->AddRef();
    }
    struct DefinitionRef {
        ifaces::NbrSimDefinition *object;
        ~DefinitionRef() noexcept(false) { if (object) object->Release(); }
    } definitionRef{definition};
    if (!definition) return 0;

    table = As<ifaces::NbrRelTable>(self->GetRelationshipTable());
    otherId = int16_t(other->GetId());
    const int32_t valueOut = int16_t(table->GetRelationValue(otherId, kRelationValue));
    auto *otherTable = other->GetRelationshipTable();
    int32_t selfId = int16_t(self->GetId());
    const int32_t valueIn = int16_t(otherTable->GetRelationValue(selfId, kRelationValue));
    int32_t level = (valueOut + valueIn) / 2;

    struct TuningRef {
        ifaces::NbrTuning *object = nullptr;
        ~TuningRef() noexcept(false) { if (object) object->Release(); }
    } tuningRef;
    ifaces::NbrTuning *&tuning = tuningRef.object;
    definition->GetTuningTable(kTuningId, kTuningGroup, reinterpret_cast<void **>(&tuning));
    if (tuning) {
        const int32_t threshold = tuning->GetValue(3);
        if (valueOut < threshold || valueIn < threshold) level -= tuning->GetValue(0x2d);
    }
    if (kind == 0) return level;
    if (kind != 1) return 0;

    if (!tuning) {
        definition->GetTuningTable(kTuningId, kTuningGroup, reinterpret_cast<void **>(&tuning));
        if (!tuning) return 0;
    }
    const int32_t t0 = tuning->GetValue(0);
    const int32_t t1 = tuning->GetValue(1);
    const int32_t t2 = tuning->GetValue(2);
    const int32_t t3 = tuning->GetValue(3);
    const int32_t t4 = tuning->GetValue(4);
    const int32_t t5 = tuning->GetValue(5);
    if (level <= t5) level = 1;
    else if (level <= t4) level = 2;
    else if (level <= t3) level = 3;
    else if (level < t0) level = 4;
    else if (level < t1) level = 5;
    else level = 6 + (level >= t2);

    const bool bit3 = (flags & kRelationBit3) != 0;
    table = As<ifaces::NbrRelTable>(self->GetRelationshipTable());
    otherId = int16_t(other->GetId());
    const uint32_t knownOther = table->GetRelationValue(otherId, kRelationOther);
    if (knownOther != 0 && !bit3) return 4;

    const uint16_t attrOther = other->GetAttribute(0xba);
    const uint16_t attrSelf = self->GetAttribute(0xba);
    if (attrSelf != attrOther) return 0;

    auto *people = As<ifaces::NbrAppSingleton>(GetCachedSingleton())->GetPeople();
    const uint32_t selfData = self->GetPersonDataId();
    auto *selfPerson = people->FindPerson(selfData);
    const uint32_t otherData = other->GetPersonDataId();
    auto *otherPerson = people->FindPerson(otherData);

    // The attributes come from the person when there is one, else from the neighbour.
    auto fromSelf = [&](uint32_t index) -> uint16_t {
        return selfPerson ? selfPerson->GetAttributeEx(index, 1) : self->GetAttribute(index);
    };
    auto fromOther = [&](uint32_t index) -> uint16_t {
        return otherPerson ? otherPerson->GetAttributeEx(index, 1) : other->GetAttribute(index);
    };
    const uint16_t a1 = fromSelf(0x3a);
    const uint16_t a2 = fromOther(0x3a);
    if (int16_t(a1) <= 0xf) return 0;
    if ((int16_t(a1) <= 0x12) != (uint16_t(a2 - 0x10) <= 2)) return 0;
    if ((int16_t(a1) > 0x12) != (int16_t(a2) > 0x12)) return 0;
    const uint16_t w1 = self->GetAttribute(0x41);
    const uint16_t w2 = other->GetAttribute(0x41);
    const uint16_t b1 = fromSelf(0x16);
    const uint16_t b2 = fromOther(0x16);
    const uint16_t c1 = fromSelf(0x17);
    const uint16_t c2 = fromOther(0x17);
    if (w1 == 0) {
        if (int16_t(b2) <= 0) return 0;
    } else if (int16_t(c2) <= 0) {
        return 0;
    }
    if (w2 == 0) {
        if (int16_t(b1) <= 0) return 0;
    } else if (int16_t(c1) <= 0) {
        return 0;
    }
    return level;
}

// The first item of the given type, with at least two values, whose first value is `key`.
template <uint32_t TypeId> static uint8_t FindValueRatio(cTSNeighbor *neighbor, uint16_t key, float *out)
{
    void *app = GetCachedSingleton();
    if (!app) return 0;
    auto *holder = As<ifaces::NbrAppSingleton>(app)->GetListHolder();
    if (!holder) return 0;
    const uint32_t id = neighbor->GetId();
    auto *list = holder->GetList(id);
    if (!list) return 0;
    // The list's iterators are pointers into an array of item pointers; End hands out a pointer to the end iterator.
    ifaces::NbrListItem **iterator = nullptr;
    list->Begin(&iterator);
    uint32_t endScratch = 0;
    auto atEnd = [&] { return iterator == *static_cast<ifaces::NbrListItem ***>(list->End(&endScratch)); };
    if (atEnd()) return 0;
    // The original's callee writes into the caller's argument slot.
    uint32_t scratch = key;
    for (;;) {
        auto *item = *iterator;
        if (item->GetTypeId() == TypeId && item->GetCount() >= 2) {
            auto *value = static_cast<uint16_t **>(item->GetValueRef(&scratch, 0));
            if (key == **value) break;
        }
        ++iterator;
        if (atEnd()) return 0;
    }
    auto *item = *iterator;
    auto *second = static_cast<uint16_t **>(item->GetValueRef(&scratch, 1));
    const uint16_t denominator = **second;
    auto *third = static_cast<uint16_t **>(item->GetValueRef(&scratch, 2));
    if (denominator == 0) return 0;
    if (out) *out = float(int16_t(**third)) / float(int16_t(denominator));
    return 1;
}

uint8_t cTSNeighbor::GetValueRatioA(uint16_t key, float *out) { return FindValueRatio<kRatioTypeA>(this, key, out); }
uint8_t cTSNeighbor::GetValueRatioB(uint16_t key, float *out) { return FindValueRatio<kRatioTypeB>(this, key, out); }

// ---- adapters (the RVA hooks) ----

using Adapters = cTSNeighborAdapters;

uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}

uint32_t __fastcall Adapters::GetGZCLSID(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGZCLSID();
}

bool __fastcall Adapters::Write(Self *self, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream);
}

void __fastcall Adapters::Detach(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Detach();
}

void __fastcall Adapters::SetAttribute(Self *self, void *, int32_t index, uint16_t value)
{
    [[msvc::forceinline_calls]] return self->Self::SetAttribute(index, value);
}

uint16_t *__fastcall Adapters::GetAttributes(Self *self, void *, uint8_t keepClean)
{
    [[msvc::forceinline_calls]] return self->Self::GetAttributes(keepClean);
}

uint32_t __fastcall Adapters::GetGenderMask(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGenderMask();
}

uint32_t __fastcall Adapters::GetAgeMask(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetAgeMask();
}

uint8_t __fastcall Adapters::IsHuman(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsHuman();
}

uint8_t __fastcall Adapters::IsPet(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsPet();
}

uint8_t __fastcall Adapters::IsGender0(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsGender0();
}

uint8_t __fastcall Adapters::IsGender1(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsGender1();
}

uint8_t __fastcall Adapters::IsBaby(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsBaby();
}

uint8_t __fastcall Adapters::IsChild(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsChild();
}

uint8_t __fastcall Adapters::IsTeen(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsTeen();
}

uint8_t __fastcall Adapters::IsAdultWithFlag(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsAdultWithFlag();
}

uint8_t __fastcall Adapters::IsAdult(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsAdult();
}

uint8_t __fastcall Adapters::IsElder(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsElder();
}

uint8_t __fastcall Adapters::IsPersonDataFlagged(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsPersonDataFlagged();
}

uint32_t __fastcall Adapters::GetExtraWord(Self *self, void *, int32_t which)
{
    [[msvc::forceinline_calls]] return self->Self::GetExtraWord(which);
}

void __fastcall Adapters::SetExtraWord(Self *self, void *, int32_t which, uint32_t value)
{
    [[msvc::forceinline_calls]] return self->Self::SetExtraWord(which, value);
}

// The persist face's slots (this = the +4 face). GetKey's hidden result pointer is the first stack argument, as the slot's.
// These two are not in the one-statement form (no vtable tie): `__assume` drops the face-to-object null test (lane EA), so
// they match by themselves.
cGZPersistResourceKey *__fastcall Adapters::GetKey(cIGZPersistResource *face, void *, cGZPersistResourceKey *out)
{
    __assume(face != nullptr); // a slot's this is never null
    [[msvc::forceinline_calls]] *out = static_cast<Self &>(*face).Self::GetKey();
    return out;
}

bool __fastcall Adapters::SetKey(cIGZPersistResource *face, void *, const cGZPersistResourceKey *key)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::SetKey(*key);
}

uint32_t __fastcall Adapters::GetType(cIGZPersistResource *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetType();
}

uint32_t __fastcall Adapters::GetGroup(cIGZPersistResource *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetGroup();
}

uint32_t __fastcall Adapters::GetInstance(cIGZPersistResource *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetInstance();
}

bool __fastcall Adapters::SetType(cIGZPersistResource *face, void *, uint32_t type)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetType(type);
}

bool __fastcall Adapters::SetGroup(cIGZPersistResource *face, void *, uint32_t group)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetGroup(group);
}

bool __fastcall Adapters::SetInstance(cIGZPersistResource *face, void *, uint32_t instance)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetInstance(instance);
}
