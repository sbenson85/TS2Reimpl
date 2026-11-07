// ObjectSaveTypeTable and GUIDTranslation::Resolve (lane MB): ts/ObjectSaveTypeTable.h.
#include "ts/ObjectSaveTypeTable.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cITSCatalogResource.h"
#include "ifaces/cITSMissingObject.h"
#include "ifaces/cITSObjectDefinition.h"
#include "ifaces/cITSObjectFileRef.h"
#include "ifaces/cITSObjectSelector.h"
#include "ifaces/cITSPersistDBRecord.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cITSStringSet.h"
#include "misc/UtilitiesC.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"

#include <new>

namespace {

constexpr uint32_t kGUIDEnd = 0;                        // the entry list ends with a GUID of 0
constexpr uint32_t kFirstTreeChecksVersion = 1;         // version 0 entries have no tree checks
constexpr uint32_t kCountsVersion = 0x46;               // the attribute and array counts
constexpr uint32_t kStringCountVersion = 0x47;          // the catalog string count
constexpr uint32_t kObjectTypeAfterVersion = 1;         // the object type, after version 1
constexpr uint32_t kGZStringVersion = 0x3f;             // the name as a GZ string (older: a padded C string)
constexpr uint32_t kTileFlagVersion = 0x49;             // the tile flag and field 0xd0
constexpr int32_t kNotRead = -1;                        // a value the entry's version does not have
constexpr uint32_t kFieldObjectType = 9;                // OBJD fields (OpenTS2's names)
constexpr uint32_t kFieldNumAttributes = 0x3a;
constexpr uint32_t kFieldNumObjArrays = 0x3b;
constexpr uint32_t kFieldD0 = 0xd0;                     // unnamed in OpenTS2; written and read back as is
constexpr int32_t kSingleTile = 0;                      // the tile flag
constexpr int32_t kOtherTile = 1;
constexpr int32_t kMultiTileLead = 3;
constexpr uint32_t kTileFlagSingleBit = 1;              // ~flag & 1: the object should be a single-tile one
constexpr uint32_t kStringSetType = 0x53545223;         // 'STR#', the catalog strings
constexpr uint32_t kCatalogStringsInstance = 0x100;
constexpr uint32_t kStringSetIid = 0xfb4cb0de;          // cITSStringSet
constexpr int32_t kDefaultLanguage = 0;                 // GetStringCount's language
constexpr uint32_t kRecordIid = 0x3b2271c0;             // cITSPersistDBRecord, asked of an old record's stream
constexpr uint32_t kMissingObjectClass = 0x5b7ebdb1;    // cTSMissingObject (COM class), as cITSMissingObject
constexpr uint32_t kMissingObjectIid = 0x5b7ebdb0;
constexpr uint32_t kTileMismatchValue = 0x3c08965f;     // a missing object's value when only its tile flag disagreed

ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}

// The stream metadata object both directions hand to the stream (slot 19), which takes it over.
UtilObjectB *NewMetadata(uint32_t version)
{
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata = metadata->Construct(version);
    return metadata;
}

void ReleaseIf(void *object)
{
    if (object) As<cIGZUnknown>(object)->Release();
}

// A cRZString on the stack, built and torn down by its own (hook-shaped) members.
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
    cIGZString *Face() { return s.AsInterface(); }
};

// The number of catalog strings of the object's file (STR# of its group, instance 0x100, the default language), 0 without a
// file or without that resource. The persist service is asked for after the key is built, as Write's original does;
// Read's asks it first and passes it in.
uint32_t CatalogStringCount(ifaces::cITSObjectSelector *object, ifaces::cITSPersistSystem *persist)
{
    uint32_t count = 0;
    if (!object->GetFileRef()) return count;
    const uint32_t key[4] = {kStringSetType, object->GetFileRef()->GetGroup(), kCatalogStringsInstance, 0};
    if (!persist) persist = PersistSystem();
    void *set = nullptr;
    if (persist->GetResource(reinterpret_cast<const struct Key16 *>(key), kStringSetIid, &set, 0, 0))
        count = As<ifaces::cITSStringSet>(set)->GetStringCount(kDefaultLanguage);
    ReleaseIf(set);
    return count;
}

} // namespace

// ---- GUIDTranslation

uint32_t GUIDTranslation::Resolve(uint32_t id)
{
    for (GUIDPair &pair : mPairs) pair.visited = 0;
    for (auto pair = mPairs.begin(); pair != mPairs.end();) {
        if (pair->from == id && !pair->visited) {
            id = pair->to;
            pair->visited = 1;
            pair = mPairs.begin();
        } else {
            ++pair;
        }
    }
    return id;
}

// ---- ObjectSaveTypeTable

ObjectSaveTypeTable::ObjectSaveTypeTable(cTSObjectFolder *folder, FolderObjects *missing, GUIDTranslation *translation)
    : mFolder(folder), mMissing(missing), mTranslation(translation), mRefCount(1)
{
    if (mTranslation) As<cIGZUnknown>(mTranslation)->AddRef();
}

ObjectSaveTypeTable::~ObjectSaveTypeTable()
{
    ReleaseIf(mTranslation);
}

void __fastcall ObjectSaveTypeTableAdapters::Destroy(ObjectSaveTypeTable *self, void *)
{
    self->ObjectSaveTypeTable::~ObjectSaveTypeTable();
}

bool ObjectSaveTypeTable::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kSerializableId) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t ObjectSaveTypeTable::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

uint32_t ObjectSaveTypeTable::GetGZCLSID()
{
    return kClassId;
}

bool ObjectSaveTypeTable::Write(ifaces::cIGZOStream *stream)
{
    stream->SetMetadata(NewMetadata(kRecordVersion));
    stream->SetUint32(GetGZCLSID());
    stream->SetUint32(kRecordVersion);
    stream->SetUint32(0);
    for (const FolderRef &entry : mFolder->mObjects) WriteEntry(stream, static_cast<ifaces::cITSObjectSelector *>(entry.Get()));
    for (const FolderRef &entry : *mFolder->mGlobals) WriteEntry(stream, static_cast<ifaces::cITSObjectSelector *>(entry.Get()));
    stream->SetUint32(kGUIDEnd);
    return !stream->GetError();
}

void ObjectSaveTypeTable::WriteEntry(ifaces::cIGZOStream *stream, ifaces::cITSObjectSelector *object)
{
    const uint16_t index = object->GetSaveIndex();
    if (!index) return;
    stream->SetSint32(static_cast<int32_t>(object->GetGUID()));
    stream->SetSint32(object->GetTreeCheckA());
    stream->SetSint32(object->GetTreeCheckB());
    ifaces::cITSObjectDefinition *definition = object->GetDefinition();
    stream->SetSint32(definition->GetField(kFieldNumAttributes));
    stream->SetSint32(definition->GetField(kFieldNumObjArrays));
    stream->SetSint32(static_cast<int32_t>(CatalogStringCount(object, nullptr)));
    stream->SetSint16(static_cast<int16_t>(index));
    stream->SetSint16(static_cast<int16_t>(definition->GetField(kFieldObjectType)));

    LocalString name;
    if (definition->IsSingleTile() || definition->IsMultiTileLead()) {
        // (no null test on the catalog resource, as the original)
        cIGZString *catalogName = nullptr;
        if (object->GetCatalogResource()->GetName(&catalogName)) name.s.Copy(*catalogName);
        ReleaseIf(catalogName);
    }
    stream->SetGZStr(name.Face());
    int32_t tile = kSingleTile;
    if (!definition->IsSingleTile()) tile = definition->IsMultiTileLead() ? kMultiTileLead : kOtherTile;
    stream->SetSint32(tile);
    stream->SetSint32(definition->GetField(kFieldD0));
}

bool ObjectSaveTypeTable::Read(ifaces::cIGZIStream *stream)
{
    ifaces::cITSPersistSystem *persist = PersistSystem();
    uint32_t classId = 0;
    uint32_t version = 0;
    uint32_t zero = 0;
    stream->GetUint32(&classId); // (not checked)
    stream->GetUint32(&version);
    stream->GetUint32(&zero);
    stream->SetMetadata(NewMetadata(version));

    LocalString name; // one for every entry: an entry without a name keeps the last one's
    // The tile flag and field 0xd0 are only in records from 0x49 on; older entries' missing objects get 0.
    int32_t tileFlag = 0;
    int32_t fieldD0 = 0;
    int32_t treeCheckA = 0;
    int32_t treeCheckB = 0;
    int32_t guid = 0;
    stream->GetSint32(&guid);
    while (guid != static_cast<int32_t>(kGUIDEnd)) {
        if (version >= kFirstTreeChecksVersion) {
            stream->GetSint32(&treeCheckA);
            stream->GetSint32(&treeCheckB);
        }
        int32_t numAttributes = kNotRead;
        int32_t numArrays = kNotRead;
        if (version >= kCountsVersion) {
            stream->GetSint32(&numAttributes);
            stream->GetSint32(&numArrays);
        }
        int32_t stringCount = kNotRead;
        if (version >= kStringCountVersion) stream->GetSint32(&stringCount);
        int16_t saveIndex = 0;
        stream->GetSint16(&saveIndex);
        int16_t objectType = static_cast<int16_t>(kNotRead);
        if (version > kObjectTypeAfterVersion) stream->GetSint16(&objectType);
        if (version >= kGZStringVersion) {
            stream->GetGZStr(name.Face());
        } else {
            void *record = nullptr;
            if (stream->QueryInterface(kRecordIid, &record)) As<ifaces::cITSPersistDBRecord>(record)->GetPaddedCString(name.Face());
            ReleaseIf(record);
        }

        guid = static_cast<int32_t>(mTranslation->Resolve(static_cast<uint32_t>(guid)));
        auto *object = static_cast<ifaces::cITSObjectSelector *>(static_cast<cITSObjectFolder *>(mFolder)->FindObject(static_cast<uint32_t>(guid)));
        bool tileMismatch = false;
        bool found = object != nullptr;
        if (version >= kTileFlagVersion) {
            stream->GetSint32(&tileFlag);
            stream->GetSint32(&fieldD0);
            if (object) {
                ifaces::cITSObjectDefinition *definition = object->GetDefinition();
                const bool singleTile = definition->IsSingleTile();
                if (static_cast<uint32_t>(singleTile) != (~static_cast<uint32_t>(tileFlag) & kTileFlagSingleBit)) {
                    tileMismatch = true;
                    // The original reads the object's name and asks the definition again here, using neither.
                    LocalString unused;
                    object->GetName(unused.Face());
                    definition->IsSingleTile();
                    found = false;
                }
            }
        }

        if (found) {
            object->SetSaveIndex(saveIndex);
            if (version == 0) {
                object->SetSaveMismatch(object->IsPortalType());
            } else {
                bool changed = object->GetTreeCheckA() != treeCheckA;
                changed |= object->GetTreeCheckB() != treeCheckB;
                ifaces::cITSObjectDefinition *definition = object->GetDefinition();
                const uint32_t strings = CatalogStringCount(object, persist);
                LocalString unused; // the original reads the object's name here, not using it
                object->GetName(unused.Face());
                if (static_cast<uint32_t>(stringCount) != strings) changed = true;
                if (numAttributes != kNotRead) {
                    changed |= definition->GetField(kFieldNumAttributes) != numAttributes;
                    if (definition->GetField(kFieldNumObjArrays) != numArrays) changed = true;
                } else if (definition->GetField(kFieldNumAttributes) != 0 || definition->GetField(kFieldNumObjArrays) != 0) {
                    changed = true;
                }
                object->SetSaveMismatch(changed);
            }
        } else if (mMissing) {
            void *missing = nullptr;
            static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(kMissingObjectClass, kMissingObjectIid, &missing);
            // (no null test on the missing object, as the original)
            As<ifaces::cITSMissingObject>(missing)->Init(name.Face(), static_cast<uint32_t>(guid), objectType, saveIndex,
                                                         static_cast<uint32_t>(tileFlag),
                                                         tileMismatch ? kTileMismatchValue : static_cast<uint32_t>(fieldD0));
            mMissing->emplace_back(missing);
            ReleaseIf(missing);
        }
        stream->GetSint32(&guid);
    }
    return !stream->GetError();
}
