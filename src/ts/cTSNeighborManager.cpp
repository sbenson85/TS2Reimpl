#include "rz/RZMessageServer.h"
#include "ts/cTSNeighborManager.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/GscObjectDefTable.h"
#include "ifaces/NeighborhoodImportSource.h"
#include "ifaces/GscObjectTable.h"
#include "ifaces/NbrKindInfo.h"
#include "ifaces/NbrPersonData.h"
#include "ifaces/cIGZCOM.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZMessageServer.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cITSDataImportExportContext.h"
#include "ifaces/cITSDataTables.h"
#include "ifaces/cITSFamily.h"
#include "ifaces/cITSFamilyManager.h"
#include "ifaces/cITSGameStateController.h"
#include "ifaces/cITSGlobals.h"
#include "ifaces/cITSInventory.h"
#include "ifaces/cITSInventoryToken.h"
#include "ifaces/cITSNeighbor.h"
#include "ifaces/cITSNeighborDataImportExportContext.h"
#include "ifaces/cITSNeighborManager.h"
#include "ifaces/cITSNeighborhood.h"
#include "ifaces/cITSNeighborhoodInfo.h"
#include "ifaces/cITSObjectFileRef.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cITSRelationshipTable.h"
#include "ifaces/cITSSaveGameInfo.h"
#include "ifaces/cTSObject.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"

#include <cstring>
#include <new>

namespace {
// The objects the manager holds, seen through the interfaces whose facts name their slots.
ifaces::cITSNeighbor *NeighborOf(const NeighborDataRef &ref) { return As<ifaces::cITSNeighbor>(ref.Get()); }
ifaces::cITSRelationshipTable *TableOf(ifaces::cITSNeighbor *neighbor)
{
    return As<ifaces::cITSRelationshipTable>(neighbor->GetRelationshipTable());
}
// The manager's own persist face as the persist system takes it.
__forceinline ifaces::cIGZPersistResource *PersistFace(cTSNeighborManager *self)
{
    return As<ifaces::cIGZPersistResource>(static_cast<cIGZPersistResource *>(self));
}
// Slots of this that hand out neighbours (virtual calls, as the original makes them), typed for the callers.
__forceinline ifaces::cITSNeighbor *NeighborAt(cTSNeighborManager *self, int32_t id, uint8_t includeAll)
{
    return As<ifaces::cITSNeighbor>(self->GetNeighbor(id, includeAll));
}
__forceinline ifaces::cITSNeighbor *NeighborByKey(cTSNeighborManager *self, uint32_t key)
{
    return As<ifaces::cITSNeighbor>(self->FindNeighborByKey(key));
}
__forceinline ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}
__forceinline ifaces::cITSGlobals *Globals() { return As<ifaces::cITSGlobals>(GetCachedSingleton()); }

// cTSObject::GetObjectId (slot 47). The fact says u16; the original hands its eax on as it is (to DeleteObject, or as
// QueryNeighbor's answer), which a u16 return would narrow.
__forceinline uint32_t ObjectIdOf(ifaces::cTSObject *object)
{
    return VirtualCall<uint32_t(__thiscall *)(void *), 47>(object);
}

template <typename K> const K *KeyArg(const uint32_t *key) { return reinterpret_cast<const K *>(key); }
} // namespace

// ---- construction ----

cTSNeighborManager *cTSNeighborManager::Create() { return new (std::nothrow) cTSNeighborManager; }

cTSNeighborManager::cTSNeighborManager() { mMessageIds.insert(kMessageId); }

// The members after the body: the id set, the key map, the neighbour map (as the original's 0x4bb7d0).
cTSNeighborManager::~cTSNeighborManager() {}

// ---- cIGZUnknown / cIGZSerializable ----

bool cTSNeighborManager::QueryInterface(uint32_t id, void **out)
{
    switch (id) {
    case 1:
    case 0xcc2a6a30:
    case 0xe4fda3d4:
        *out = static_cast<cITSNeighborManager *>(this);
        AddRef();
        return true;
    case 0x456b8f1d:
        *out = static_cast<cIGZPersistResource *>(this);
        AddRef();
        return true;
    case 0x8db5bf48:
        *out = static_cast<cITSDataContainer *>(this);
        AddRef();
        return true;
    }
    return false;
}

uint32_t cTSNeighborManager::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

uint32_t cTSNeighborManager::GetGZCLSID() { return kClassId; }

bool cTSNeighborManager::Write(ifaces::cIGZOStream *out)
{
    out->SetUint32(kClassId);
    out->SetUint32(kVersion);
    out->SetUint32(0);
    out->SetSint16(mNextNeighborId);
    return out->GetError() == 0;
}

bool cTSNeighborManager::Read(ifaces::cIGZIStream *in)
{
    // The class id, the version and a third word are read and not used.
    uint32_t ignored[2];
    in->GetUint32(&ignored[0]);
    in->GetUint32(&ignored[1]);
    in->GetUint32(&ignored[0]);
    mNeighbors.clear();
    in->GetSint16(reinterpret_cast<int16_t *>(&mNextNeighborId));
    return true;
}

// ---- messages ----

uint8_t cTSNeighborManager::RegisterMessages()
{
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
        ->CallSlot5ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))),
                              nullptr);
    return 1;
}

uint8_t cTSNeighborManager::UnregisterMessages()
{
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)
        ->CallSlot6ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))),
                              nullptr);
    return 1;
}

bool cTSNeighborManager::DoMessage(void *message)
{
    if (!message) return false;
    ifaces::cIGZMessage2Standard *payload = nullptr;
    bool handled = false;
    if (As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(neighbor_ids::kMessage2StandardIid,
                                                                  reinterpret_cast<void **>(&payload)) &&
        As<ifaces::cIGZMessage2Standard>(message)->GetType() == cTSNeighborManager::kMessageId && payload->HasData1()) {
        const uint32_t key = payload->GetData1();
        auto it = mByPersonDataId.find(key);
        if (it != mByPersonDataId.end()) {
            NeighborOf(it->second)->SetPersonData(nullptr);
            handled = true;
        }
    }
    if (payload) payload->Release();
    return handled;
}

void cTSNeighborManager::SetDirty() { mDirty = 1; }

uint8_t cTSNeighborManager::Save(uint8_t all)
{
    ifaces::cITSPersistSystem *persist = PersistSystem();
    ifaces::cIGZPersistDBSegment *holder = nullptr;
    const cGZPersistResourceKey ownKey = GetKey();
    uint8_t result = 0;
    if (persist->FindSegment(&ownKey, neighbor_ids::kSegmentIid, reinterpret_cast<void **>(&holder))) {
        const uint8_t stored = persist->SaveResource(PersistFace(this), holder);
        result = uint8_t(SaveNeighbors(all) & stored);
        holder->Flush();
        mDirty = 0;
    }
    if (holder) holder->Release();
    return result;
}

uint8_t cTSNeighborManager::SaveNeighbors(uint8_t all)
{
    ifaces::cITSPersistSystem *persist = PersistSystem();
    const uint32_t group = GetGroup();
    uint8_t ok = 1;
    for (auto &entry : mNeighbors) {
        ifaces::cITSNeighbor *neighbor = NeighborOf(entry.second);
        if (!neighbor) continue;
        const uint32_t instance = uint32_t(int32_t(int16_t(neighbor->GetId())));
        uint32_t recordKey[4] = {neighbor_ids::kNeighborClass, group, instance, 0};
        neighbor->AsPersistResource()->SetKey(recordKey);
        if (all || neighbor->IsModified()) {
            ok &= persist->SaveResource(neighbor->AsPersistResource(), nullptr);
            neighbor->SetModified(0);
        }
        uint32_t extraKey[4] = {neighbor_ids::kDataTablesClass, group, instance, neighbor_ids::kNeighborClass};
        ifaces::cITSDataTables *extra = neighbor->GetDataTables(0);
        if (!extra) continue;
        if (extra->IsEmpty()) {
            persist->DeleteResource(extraKey);
        } else if (extra->IsModified()) {
            extra->SetKey(extraKey);
            // The data tables are a cIGZPersistResource at +0.
            persist->SaveResource(reinterpret_cast<ifaces::cIGZPersistResource *>(extra), nullptr);
            extra->SetModified(0);
        }
    }
    return ok;
}

void cTSNeighborManager::Reset()
{
    ifaces::cITSPersistSystem *persist = PersistSystem();
    for (auto &entry : mNeighbors) {
        ifaces::cITSNeighbor *neighbor = NeighborOf(entry.second);
        if (!neighbor) continue;
        neighbor->Shutdown();
        const int32_t instance = int16_t(neighbor->GetId());
        uint32_t key[4];
        key[1] = GetGroup();
        key[0] = neighbor_ids::kNeighborClass;
        key[2] = uint32_t(instance);
        key[3] = 0;
        auto *store = As<ifaces::cIGZPersistResourceManager>(persist->GetResourceManager());
        store->ForgetResource(KeyArg<cGZPersistResourceKey>(key));
    }
    mNeighbors.clear();
    mByPersonDataId.clear();
    mUserFileCounter = 0;
    mDirty = 0;
}

bool cTSNeighborManager::SetKey(const cGZPersistResourceKey &key)
{
    mResourceKey = key;
    return true;
}

// ---- neighbour lookups ----

int16_t cTSNeighborManager::NextNeighborId(int16_t after, uint8_t includeAll)
{
    int32_t from = int32_t(after) + 1;
    for (;;) {
        auto it = mNeighbors.lower_bound(from);
        if (it == mNeighbors.end()) return 0;
        if (includeAll || !NeighborOf(it->second)->IsRetired()) return int16_t(it->first);
        from = it->first + 1;
    }
}

void *cTSNeighborManager::GetNeighbor(int32_t id, uint8_t includeAll)
{
    if (id < 1) return nullptr;
    auto it = mNeighbors.lower_bound(id);
    if (it == mNeighbors.end() || id < it->first) return nullptr;
    ifaces::cITSNeighbor *neighbor = NeighborOf(it->second);
    if (!includeAll && neighbor->IsRetired()) return nullptr;
    // The person data and its kind info are asked again at every step, as the original does.
    if (neighbor && neighbor->GetPersonData() && neighbor->GetPersonData()->GetKindInfo()) {
        ifaces::NbrKindInfo *kindInfo = neighbor->GetPersonData()->GetKindInfo();
        kindInfo->GetKind(9);
    }
    return neighbor;
}

void *cTSNeighborManager::FindNeighborByKey(uint32_t key)
{
    if (!key) return nullptr;
    auto it = mByPersonDataId.find(key);
    return it == mByPersonDataId.end() ? nullptr : it->second.Get();
}

void cTSNeighborManager::GetNeighborIds(NeighborIdVector *out, uint32_t mask, uint8_t includeAll)
{
    out->clear();
    out->reserve(mNeighbors.size());
    const bool everyone = includeAll && mask == 0;
    for (auto &entry : mNeighbors) {
        ifaces::cITSNeighbor *neighbor = NeighborOf(entry.second);
        if (!everyone) {
            if (!includeAll && neighbor->IsRetired()) continue;
            if (mask && !(mask & (1u << (neighbor->GetAttribute(0xba) & 31)))) continue;
        }
        out->push_back(uint32_t(entry.first));
    }
}

uint32_t cTSNeighborManager::MarkFamilyChanged(void *neighborObject)
{
    auto *neighbor = As<ifaces::cITSNeighbor>(neighborObject);
    neighbor->SetFlag20(1);
    ifaces::cITSFamilyManager *families = Globals()->GetFamilyManager();
    const int32_t familyId = int16_t(neighbor->GetAttribute(0x3d));
    ifaces::cITSFamily *family = families->GetFamily(familyId);
    if (!family) return 0;
    // cITSFamily::SetFlag79(1) (slot 23). The original tail-jumps to it and returns its eax, which the fact (void) does not
    // express, so this stays a slot call returning the register.
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 23>(family, uint32_t(1));
}

void cTSNeighborManager::RemoveNeighborsWithAttribute43(int32_t value)
{
restart:
    for (auto it = mNeighbors.begin(); it != mNeighbors.end(); ++it) {
        ifaces::cITSNeighbor *neighbor = NeighborOf(it->second);
        if (!neighbor || int16_t(neighbor->GetAttribute(0x43)) != value) continue;
        if (RemoveNeighbor(neighbor) == 0) goto restart;
        return;
    }
}

void cTSNeighborManager::AddRelatedIdsToFamily(void *familyObject)
{
    auto *family = As<ifaces::cITSFamily>(familyObject);
    NeighborIdVector ids;
    CollectRelatedIds(family, &ids, 0);
    for (uint32_t id : ids) family->AddToIdSet(id);
}

void cTSNeighborManager::EnsureSimInventories()
{
    ifaces::cITSNeighborhood *neighborhood = Globals()->GetNeighborhood();
    for (auto &entry : mNeighbors) {
        ifaces::cITSNeighbor *neighbor = NeighborOf(entry.second);
        if (!neighbor) continue;
        const uint32_t simId = neighbor->GetId();
        neighborhood->GetSimInventory(simId);
    }
}

int32_t cTSNeighborManager::HighestNeighborId()
{
    if (mNeighbors.begin() == mNeighbors.end()) return 0;
    return mNeighbors.rbegin()->first;
}

int32_t cTSNeighborManager::CountFlaggedNeighbors()
{
    int32_t count = 0;
    for (auto &entry : mNeighbors) {
        ifaces::cITSNeighbor *neighbor = NeighborOf(entry.second);
        if (neighbor && neighbor->IsFlagged()) ++count;
    }
    return count;
}

namespace {
// The original loads the relationship table's vtable pointer first, then evaluates the neighbour's id, then reads the slot:
// slot 5 (id) is GetValueCount, "does the table know that neighbour", slot 9 (id, kind) is GetValue.
__forceinline uint32_t KnownOf(ifaces::cITSRelationshipTable *table, ifaces::cITSNeighbor *neighbor)
{
    void **vtable = *reinterpret_cast<void ***>(table);
    const int32_t id = int16_t(neighbor->GetId());
    return reinterpret_cast<uint32_t(__thiscall *)(void *, int32_t)>(vtable[5])(table, id);
}
__forceinline uint32_t StateOf(ifaces::cITSRelationshipTable *table, ifaces::cITSNeighbor *neighbor, int32_t kind)
{
    void **vtable = *reinterpret_cast<void ***>(table);
    const int32_t id = int16_t(neighbor->GetId());
    return reinterpret_cast<uint32_t(__thiscall *)(void *, int32_t, int32_t)>(vtable[9])(table, id, kind);
}
} // namespace

// ---- relationship checks between neighbours ----

bool cTSNeighborManager::RelationsAllowBasic(void *a, void *b)
{
    auto *na = As<ifaces::cITSNeighbor>(a);
    auto *nb = As<ifaces::cITSNeighbor>(b);
    if (!na || !nb) return false;
    ifaces::cITSRelationshipTable *tableA = TableOf(na);
    ifaces::cITSRelationshipTable *tableB = TableOf(nb);
    if (!tableA) return false;
    if (!KnownOf(tableA, nb)) return false;
    if (!tableB) return false;
    if (!KnownOf(tableB, na)) return false;
    if (StateOf(tableA, nb, 3)) return false;
    if (StateOf(tableB, na, 3)) return false;
    const uint32_t ab = StateOf(tableA, nb, 1);
    const uint32_t ba = StateOf(tableB, na, 1);
    const uint32_t abNine = StateOf(tableA, nb, 9);
    const uint32_t baNine = StateOf(tableB, na, 9);
    if (!(ab & 0x30) && !(abNine & 1)) return false;
    if (!((ba >> 4) & 1) && !((ba >> 5) & 1) && !(baNine & 1)) return false;
    return true;
}

bool cTSNeighborManager::RelationsAllowExtended(void *a, void *b)
{
    auto *na = As<ifaces::cITSNeighbor>(a);
    auto *nb = As<ifaces::cITSNeighbor>(b);
    if (!na || !nb) return false;
    ifaces::cITSRelationshipTable *tableA = TableOf(na);
    ifaces::cITSRelationshipTable *tableB = TableOf(nb);
    if (!tableA) return false;
    if (!KnownOf(tableA, nb)) return false;
    if (!tableB) return false;
    if (!KnownOf(tableB, na)) return false;
    const uint32_t ab = StateOf(tableA, nb, 1);
    const uint32_t ba = StateOf(tableB, na, 1);
    const uint32_t abNine = StateOf(tableA, nb, 9);
    const uint32_t baNine = StateOf(tableB, na, 9);
    const uint32_t abThree = StateOf(tableA, nb, 3);
    const uint32_t baThree = StateOf(tableB, na, 3);
    if (((ab & 0x30) || (abNine & 1)) && (((ba >> 4) & 1) || ((ba >> 5) & 1) || (baNine & 1))) return true;
    if (abThree) return true;
    return baThree != 0;
}

__declspec(noinline) void cTSNeighborManager::CountRelatedNeighbors(void *targetObject)
{
    auto *target = As<ifaces::cITSNeighbor>(targetObject);
    if (!target->GetFlag20()) return;
    int32_t count = 0;
    for (auto it = mNeighbors.begin(); it != mNeighbors.end(); ++it) {
        ifaces::cITSNeighbor *neighbor = NeighborOf(it->second);
        if (!neighbor || neighbor == targetObject) continue;
        if (!neighbor->IsFlagged()) continue;
        if (!neighbor->GetAttribute(0x3d)) continue;
        if (neighbor->GetAttribute(0x44) & 1) continue;
        if (RelationsAllowBasic(neighbor, target)) ++count;
    }
    target->SetRelatedCount(count);
}

void cTSNeighborManager::CountRelatedNeighborsOf(void *target)
{
    if (target) CountRelatedNeighbors(target);
}

int32_t cTSNeighborManager::CollectRelatedIds(void *familyObject, NeighborIdVector *out, uint8_t extended)
{
    auto *family = As<ifaces::cITSFamily>(familyObject);
    family->ClearIdSet();
    for (auto it = mNeighbors.begin(); it != mNeighbors.end(); ++it) {
        ifaces::cITSNeighbor *neighbor = NeighborOf(it->second);
        if (!neighbor) continue;
        // The arguments are evaluated before the call target is read, as the original does.
        const uint32_t personDataId = neighbor->GetPersonDataId();
        if (family->HasMemberId(personDataId)) continue;
        if (!neighbor->GetAttribute(0x3d)) continue;
        for (int32_t i = 0; i < int32_t(family->CountMembers(3)); ++i) {
            const uint32_t memberKey = *family->GetMemberIdSlot(i);
            void *other = FindNeighborByKey(memberKey);
            if (other && (extended ? RelationsAllowExtended(neighbor, other) : RelationsAllowBasic(neighbor, other))) {
                out->push_back(neighbor->GetPersonDataId());
                break;
            }
        }
    }
    return int32_t(out->end() - out->begin());
}

// ---- queries about one neighbour ----

uint32_t cTSNeighborManager::QueryNeighbor(int16_t id, int16_t query, uint32_t *out)
{
    ifaces::cITSGlobals *app = Globals();
    ifaces::cITSFamilyManager *families = app->GetFamilyManager();
    ifaces::cITSNeighbor *neighbor = NeighborAt(this, int32_t(id), 1);
    ifaces::GscObjectTable *objects = app->GetObjectTable();
    uint32_t discarded;
    *(out ? out : &discarded) = 0;
    if (!neighbor) return 0;
    switch (query) {
    case 0: {
        if (!neighbor->GetPersonData()) return 0;
        struct PointerRange {
            void **begin;
            void **end;
        };
        auto *range = static_cast<PointerRange *>(objects->GetObjectsOfDefinition(neighbor->GetPersonData()));
        for (void **p = range->begin; p != range->end; ++p) {
            auto *record = As<ifaces::cTSObject>(*p);
            if (!record) continue;
            if (record->ObjectKind() != 2) continue;
            if (record->TestFlags(0x40)) continue;
            if (record->TestFlags(0x10000)) continue;
            return ObjectIdOf(record);
        }
        return 0;
    }
    case 1: {
        ifaces::cITSFamily *service = app->GetGameStateController()->GetActiveFamily();
        if (!service) return 0;
        ifaces::NbrPersonData *data = neighbor->GetPersonData();
        const uint32_t key = data->GetDataId();
        return service->HasMemberId(key);
    }
    case 2:
        // cITSNeighbor::GetAttribute(0x3a) (slot 16): the original returns the callee's eax, not the u16 the fact has.
        return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 16>(neighbor, uint32_t(0x3a));
    case 5:
        CountRelatedNeighbors(neighbor);
        return neighbor->GetRelatedCount();
    case 6: {
        const int32_t familyId = int16_t(neighbor->GetAttribute(0x3d));
        ifaces::cITSFamily *found = families->GetFamily(familyId);
        if (!found) return 0;
        return found->GetHomeLot();
    }
    case 7:
    case 8:
    case 10: {
        const int32_t familyId = int16_t(neighbor->GetAttribute(0x3d));
        if (!familyId) return 0;
        ifaces::cITSFamily *found = families->GetFamily(familyId);
        if (!found || !found->GetHomeLot()) return 0;
        // GetFlagBit0 (slot 67), GetFlagBit2 (71), GetFlagBit1 (69): only the low word is turned into 0 / 1; the upper half
        // of eax stays as the callee left it, which the u8 the facts give would drop.
        uint32_t answer;
        if (query == 7) answer = VirtualCall<uint32_t(__thiscall *)(void *), 67>(found);
        else if (query == 8) answer = VirtualCall<uint32_t(__thiscall *)(void *), 71>(found);
        else answer = VirtualCall<uint32_t(__thiscall *)(void *), 69>(found);
        return (answer & 0xffff0000u) | (uint8_t(answer) != 0);
    }
    case 9: {
        const int32_t familyId = int16_t(neighbor->GetAttribute(0x3d));
        if (!familyId) return 0;
        ifaces::cITSFamily *found = families->GetFamily(familyId);
        if (!found) return 0;
        return found->FlushChanges();
    }
    default:
        return 0;
    }
}

// ---- family assignment ----

void cTSNeighborManager::RebuildFamilyAssignments()
{
    std::set<int32_t> assigned;
    std::vector<int16_t, GZServiceAllocator<int16_t>> familyIds;
    ifaces::cITSFamilyManager *families = Globals()->GetFamilyManager();
    families->GetFamilyIds(&familyIds);
    for (auto id = familyIds.begin(); id != familyIds.end(); ++id) {
        ifaces::cITSFamily *family = families->GetFamily(int32_t(*id));
        if (!family) continue;
        for (int32_t i = 0; i < int32_t(family->CountMembers(3)); ++i) {
            const uint32_t *key = family->GetMemberIdSlot(i);
            ifaces::cITSNeighbor *member = NeighborByKey(this, *key);
            if (!member) continue;
            // cITSNeighbor::SetAttribute (slot 17): the original hands the family id on as it is, unmasked (the fact's u16
            // would narrow it).
            VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 17>(member, uint32_t(0x3d), family->GetFamilyId());
            assigned.insert(int32_t(int16_t(member->GetId())));
        }
    }
    for (auto it = mNeighbors.begin(); it != mNeighbors.end(); ++it) {
        ifaces::cITSNeighbor *neighbor = NeighborOf(it->second);
        if (neighbor && assigned.find(it->first) == assigned.end()) neighbor->SetAttribute(0x3d, 0);
    }
}

// ---- adding, removing and loading neighbours ----

namespace {
// Builds a framework message (cRZMessage2Standard, as 0x1c033d does) holding one reference and sets its id.
ifaces::cIGZMessage2Standard *NewMessage(uint32_t id)
{
    auto *message = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    if (message) {
        message->mVTable = GameAddress(cRZMessage2Standard::kVTable);
        message->mRefCount = 0;
        message->mFlags = 0;
        message->mId = 0;
        for (uint32_t &value : message->mValue) value = 0;
        message->mPtr20 = message->mPtr24 = message->mPtr28 = nullptr;
        As<cIGZUnknown>(message)->AddRef();
    }
    auto *typed = As<ifaces::cIGZMessage2Standard>(message);
    typed->SetType(id);
    return typed;
}
// The cached message server (an original callee, 0x9c5a89).
ifaces::cIGZMessageServer *MessageServer()
{
    return As<ifaces::cIGZMessageServer>(GetCachedMessageServer());
}
} // namespace

void cTSNeighborManager::UpdateKeyMap(void *neighbor, uint8_t remove)
{
    if (!neighbor) {
        mByPersonDataId.clear();
        for (auto it = mNeighbors.begin(); it != mNeighbors.end(); ++it) {
            ifaces::cITSNeighbor *entry = NeighborOf(it->second);
            if (!entry) continue;
            const uint32_t key = entry->GetPersonDataId();
            mByPersonDataId[key].Assign(entry);
        }
        return;
    }
    const uint32_t key = As<ifaces::cITSNeighbor>(neighbor)->GetPersonDataId();
    if (remove) {
        auto it = mByPersonDataId.find(key);
        if (it != mByPersonDataId.end()) mByPersonDataId.erase(it);
    } else {
        mByPersonDataId[key].Assign(neighbor);
    }
}

void cTSNeighborManager::RemoveNeighborEffects(void *neighborObject)
{
    auto *neighbor = As<ifaces::cITSNeighbor>(neighborObject);
    const int32_t id = int16_t(neighbor->GetId());
    ifaces::cIGZMessageServer *server = MessageServer();
    ifaces::cIGZMessage2Standard *message = NewMessage(0x9c);
    message->SetData1(uint32_t(id));
    server->MessageSend(message);
    UpdateKeyMap(neighbor, 1);
    mNeighbors.erase(id);
    for (auto it = mNeighbors.begin(); it != mNeighbors.end(); ++it) {
        ifaces::cITSNeighbor *other = NeighborOf(it->second);
        if (other) TableOf(other)->RemovePerson(uint32_t(id));
    }
    ifaces::cITSRelationshipTable *table = TableOf(neighbor);
    if (table) table->Clear(1);
    ifaces::cITSGlobals *app = Globals();
    if (app) {
        // Two getters of cITSGlobals (slots 29 and 27) and their objects' slots 4 / 5 have no name yet; the objects are told
        // the neighbour's id.
        void *first = VirtualCall<void *(__thiscall *)(void *), 29>(app);
        if (first) VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 4>(first, uint32_t(id));
        void *second = VirtualCall<void *(__thiscall *)(void *), 27>(app);
        if (second) VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 5>(second, uint32_t(id));
    }
    neighbor->Shutdown();
    ifaces::cITSPersistSystem *persist = PersistSystem();
    ifaces::cIGZPersistResource *record = neighbor->AsPersistResource();
    uint32_t key[4];
    persist->DeleteResource(record->GetKey(key));
    if (message) message->Release();
}

uint8_t cTSNeighborManager::LoadNeighbors(uint32_t group)
{
    uint8_t ok = 1;
    ifaces::cITSPersistSystem *persist = PersistSystem();
    NeighborKeyList keys;
    const uint32_t count = persist->ListKeys(&keys, group, neighbor_ids::kNeighborClass);
    for (uint32_t i = 0; i < count; ++i) {
        ifaces::cITSNeighbor *neighbor = nullptr;
        if (persist->GetResource(reinterpret_cast<const Key16 *>(keys.data() + i), neighbor_ids::kNeighborIid,
                                 reinterpret_cast<void **>(&neighbor), 0, 0)) {
            const int32_t id = int16_t(neighbor->GetId());
            mNeighbors[id].Assign(neighbor);
            neighbor->SetModified(0);
        } else {
            ok = 0;
        }
        if (neighbor) As<cIGZUnknown>(neighbor)->Release();
    }
    UpdateKeyMap(nullptr, 0);
    return ok;
}

uint8_t cTSNeighborManager::Reload(uint32_t group, uint8_t dropFinished)
{
    mUserFileCounter = 0;
    mNeighbors.clear();
    mByPersonDataId.clear();
    LoadNeighbors(group);
    if (dropFinished) {
        std::vector<NeighborDataRef> doomed;
        for (auto it = mNeighbors.begin(); it != mNeighbors.end(); ++it) {
            ifaces::cITSNeighbor *neighbor = NeighborOf(it->second);
            if (!neighbor) continue;
            // The data objects are asked again for every test, as the original does.
            bool drop;
            if (!neighbor->GetPersonData()) {
                drop = true;
            } else if (!neighbor->GetPersonData()->GetKindInfo()) {
                drop = true;
            } else if (neighbor->GetPersonData()->GetKindInfo()->GetKind(9) == 2) {
                drop = false;
            } else {
                drop = neighbor->GetPersonData()->GetKindInfo()->GetKind(9) != 0x11;
            }
            if (drop) doomed.push_back(NeighborDataRef(neighbor));
        }
        for (const NeighborDataRef &entry : doomed) RemoveNeighborEffects(entry.Get());
    }
    return 1;
}

int32_t cTSNeighborManager::RemoveNeighbor(void *neighborObject)
{
    auto *neighbor = As<ifaces::cITSNeighbor>(neighborObject);
    if (!neighbor->IsFlagged()) return -1;
    ifaces::NbrPersonData *data = neighbor->GetPersonData();
    const uint32_t dataKey = data->GetDataId();
    const uint32_t word = neighbor->GetId();
    ifaces::cITSGlobals *app = Globals();
    ifaces::GscObjectTable *entries = app->GetObjectTable();
    while (ifaces::cTSObject *entry = entries->FindObjectWithGuid(dataKey)) entries->DeleteObject(ObjectIdOf(entry));
    NeighborShortList familyIds;
    ifaces::cITSFamilyManager *families = app->GetFamilyManager();
    families->GetFamilyIds(&familyIds);
    for (auto id = familyIds.begin(); id != familyIds.end(); ++id) {
        ifaces::cITSFamily *family = families->GetFamily(uint32_t(int32_t(*id)));
        if (family && family->HasMemberId(dataKey)) return -1;
    }
    ifaces::cIGZMessageServer *server = MessageServer();
    ifaces::cIGZMessage2Standard *message = NewMessage(0x9d);
    message->SetData1(uint32_t(int32_t(int16_t(word))));
    server->MessageSend(message);
    As<cIGZUnknown>(neighbor)->AddRef();
    RemoveNeighborEffects(neighbor);
    mDirty = 1;
    auto *held = As<ifaces::cITSObjectFileRef>(data->GetOwnerInfo());
    if (held) As<cIGZUnknown>(held)->AddRef();
    int32_t result;
    if (!app->GetObjectDefTable()->RemoveDefinition(data)) {
        result = -1;
    } else {
        const uint32_t group = held->GetGroup();
        result = PersistSystem()->ReleaseSegmentsForGroup(group, 1) ? 0 : -1;
    }
    if (held) As<cIGZUnknown>(held)->Release();
    As<cIGZUnknown>(neighbor)->Release();
    if (message) message->Release();
    return result;
}

int32_t cTSNeighborManager::RetireNeighbor(void *neighborObject, uint32_t unused)
{
    auto *neighbor = As<ifaces::cITSNeighbor>(neighborObject);
    if (!neighbor->IsFlagged()) return -1;
    if (neighbor->GetAttribute(0xa5) != 0) return -1;
    ifaces::cITSGlobals *app = Globals();
    ifaces::GscObjectTable *entries = app->GetObjectTable();
    ifaces::GscObjectDefTable *records = app->GetObjectDefTable();
    auto *person = static_cast<ifaces::NbrPersonData *>(records->FindRecord(neighbor->GetPersonDataId()));
    const uint32_t dataKey = person->GetDataId();
    const int32_t id = int16_t(neighbor->GetId());
    ifaces::cIGZMessageServer *server = MessageServer();
    ifaces::cIGZMessage2Standard *message = NewMessage(0x9e);
    message->SetData1(uint32_t(id));
    server->MessageSend(message);
    while (ifaces::cTSObject *entry = entries->FindObjectWithGuid(dataKey)) entries->DeleteObject(ObjectIdOf(entry));
    for (auto it = mNeighbors.begin(); it != mNeighbors.end(); ++it) {
        ifaces::cITSNeighbor *other = NeighborOf(it->second);
        if (other) TableOf(other)->RemovePerson(uint32_t(id));
    }
    ifaces::cITSRelationshipTable *table = TableOf(neighbor);
    if (table) table->Clear(1);
    neighbor->SetAttribute(0xa5, 1);
    neighbor->SetAttribute(0x3d, 0);
    auto *held = As<ifaces::cITSObjectFileRef>(person->GetOwnerInfo());
    if (held) As<cIGZUnknown>(held)->AddRef();
    if (person->GetKindInfo()) {
        person->GetKindInfo()->SetKind(9, 0x11);
        // NbrPersonData slot 3 has no name yet; it is called after the kind change.
        VirtualCall<uint32_t(__thiscall *)(void *), 3>(person);
    }
    mDirty = 1;
    const uint32_t group = held->GetGroup();
    ServiceRefVector found{};
    ifaces::cITSPersistSystem *persist = PersistSystem();
    persist->GetSegmentsForGroup(group, &found);
    int32_t result = -1;
    {
        NeighborIdVector resources;
        for (void **p = found.mFirst; p != found.mLast; ++p) resources.push_back(uint32_t(uintptr_t(*p)));
        if (DeleteNeighborResources(&resources, unused)) {
            ifaces::cITSSaveGameInfo *created = nullptr;
            As<ifaces::cIGZCOM>(GetDirectorFrameworkObject())
                ->GetClassObject(neighbor_ids::kSaveGameInfoClass, neighbor_ids::kSaveGameInfoIid, &created);
            if (created) {
                created->Fill();
                created->Save(group, 1);
                As<cIGZUnknown>(created)->Release();
            }
            result = 0;
        }
    }
    found.ReleaseAndFree();
    if (held) As<cIGZUnknown>(held)->Release();
    if (message) message->Release();
    return result;
}

uint8_t cTSNeighborManager::DeleteNeighborResources(const NeighborIdVector *resources, uint32_t)
{
    std::set<uint32_t> skipTypes;
    for (uint32_t type : {0x43545353u, 0x856ddbacu, 0x4f424a44u, 0x5b12bf90u, 0x4f424a66u}) skipTypes.insert(type);
    (void)PersistSystem();
    for (auto element = resources->begin(); element != resources->end(); ++element) {
        ifaces::cIGZPersistDBSegment *holder = nullptr;
        As<cIGZUnknown>(reinterpret_cast<void *>(uintptr_t(*element)))
            ->QueryInterface(neighbor_ids::kSegmentIid, reinterpret_cast<void **>(&holder));
        if (holder) {
            NeighborKeyList keys;
            holder->GetResourceKeyList(&keys, nullptr);
            for (const NeighborResourceKey &key : keys)
                if (skipTypes.find(key.type) == skipTypes.end())
                    holder->DeleteRecord(reinterpret_cast<const cGZPersistResourceKey *>(&key));
            holder->Flush();
        }
        if (holder) holder->Release();
    }
    return 1;
}

void *cTSNeighborManager::CreateNeighbor(void *data)
{
    if (!data) return nullptr;
    auto *person = As<ifaces::NbrPersonData>(data);
    if (person->GetKindInfo()) person->GetKindInfo()->GetKind(9);
    const int32_t want = int16_t(mNextNeighborId);
    int32_t id = want;
    while (mNeighbors.find(id) != mNeighbors.end()) ++id;
    if (uint32_t(id) <= mNeighbors.size()) {
        mNextNeighborId = uint16_t(id + 1);
    } else {
        if (id != want) id = 1;
        while (mNeighbors.find(id) != mNeighbors.end()) ++id;
        if (id >= want) mNextNeighborId = uint16_t(id + 1);
    }
    ifaces::cITSNeighbor *neighbor = nullptr;
    ifaces::cIGZCOM *framework = As<ifaces::cIGZCOM>(GetDirectorFrameworkObject());
    // Only the low byte of the answer counts (the original tests al).
    if (uint8_t(framework->GetClassObject(neighbor_ids::kNeighborClass, neighbor_ids::kNeighborIid, &neighbor))) {
        mNeighbors[id].Assign(neighbor);
        neighbor->Initialize(id, person);
        const int32_t word = int16_t(neighbor->GetId());
        const uint32_t group = GetGroup();
        uint32_t key[4] = {neighbor_ids::kNeighborClass, group, uint32_t(word), 0};
        neighbor->AsPersistResource()->SetKey(key);
        UpdateKeyMap(neighbor, 0);
        mDirty = 1;
    }
    void *result = neighbor;
    if (neighbor) As<cIGZUnknown>(neighbor)->Release();
    return result;
}

// ---- the data container face (export and import) ----

namespace {
constexpr uint32_t kInventoryRecordVersion = 0xcb; // the version word of a sim inventory record

ifaces::cIGZPersistResourceManager *ResourceManager(ifaces::cITSPersistSystem *persist)
{
    return As<ifaces::cIGZPersistResourceManager>(persist->GetResourceManager());
}
const cGZPersistResourceKey *ResourceKey(const uint32_t *key) { return KeyArg<cGZPersistResourceKey>(key); }
} // namespace

bool cTSNeighborManager::Export(ifaces::cITSDataImportExportContext *container)
{
    uint8_t result = 0;
    ifaces::cITSNeighborDataImportExportContext *context = nullptr;
    if (container && container->QueryInterface(neighbor_ids::kNeighborContextIid, &context)) {
        ifaces::cIGZPersistDBSegment *segment = nullptr;
        if (container->QueryHeld(neighbor_ids::kSegmentIid, &segment)) {
            result = 1;
            ifaces::cITSGlobals *app = Globals();
            ifaces::cITSPersistSystem *persist = PersistSystem();
            ifaces::cIGZPersistDBSegment *source = nullptr;
            ResourceManager(persist)->FindSegmentWithKey(&mResourceKey, reinterpret_cast<void **>(&source));
            ServiceBlockVector ids{};
            context->CopyAll(&ids);
            for (const uint32_t *it = static_cast<const uint32_t *>(ids.mFirst); it != ids.mLast; ++it) {
                const uint32_t id = *it;
                ifaces::cITSNeighbor *neighbor = NeighborAt(this, int32_t(id), 1);
                if (!neighbor) continue;
                ifaces::cITSRelationshipTable *table = TableOf(neighbor);
                if (table) {
                    // Touches every person's entry (loads it); the counts are not used.
                    uint32_t count = table->GetPersonCount();
                    uint32_t person = table->GetFirstPerson();
                    for (; count; --count) {
                        table->GetValueCount(person);
                        person = table->GetNextPerson(person);
                    }
                }
                // The neighbour's record goes into the container's segment under the container's group and its person
                // data id, then gets its own key and modified flag back.
                uint32_t savedKey[4];
                neighbor->AsPersistResource()->GetKey(savedKey);
                const uint32_t oldGroup = savedKey[1];
                const uint32_t group = container->GetGroup();
                uint32_t key[4] = {savedKey[0], group, 0, savedKey[3]};
                key[2] = neighbor->GetPersonDataId();
                const uint8_t modified = neighbor->IsModified();
                neighbor->AsPersistResource()->SetKey(key);
                result &= persist->SaveResource(neighbor->AsPersistResource(), segment);
                neighbor->AsPersistResource()->SetKey(savedKey);
                neighbor->SetModified(modified);
                const uint8_t resolved = context->IsResolved(int32_t(id));
                if (!app->GetNeighborhood()) {
                    if (!resolved) continue;
                } else {
                    if (!resolved) continue;
                    ifaces::cITSInventory *inventory = app->GetNeighborhood()->GetSimInventory(id);
                    if (inventory) {
                        const uint32_t recordKey[4] = {neighbor_ids::kSimInventoryRecordType, container->GetGroup(), id, 0};
                        ifaces::cIGZPersistDBRecord *record = nullptr;
                        if (segment->OpenRecord(ResourceKey(recordKey), &record, 2)) {
                            ifaces::cIGZOStream *writer = nullptr;
                            if (record->QueryInterface(neighbor_ids::kOStreamIid, &writer)) {
                                writer->SetUint32(kInventoryRecordVersion);
                                auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
                                if (metadata) metadata = metadata->Construct(kInventoryRecordVersion);
                                writer->SetMetadata(metadata);
                                result &= inventory->Write(writer);
                            }
                            segment->CloseRecord(record);
                            if (writer) writer->Release();
                        }
                        if (record) record->Release();
                    }
                }
                const uint32_t from[4] = {neighbor_ids::kDataTablesClass, oldGroup, id, neighbor_ids::kNeighborClass};
                const uint32_t to[4] = {neighbor_ids::kDataTablesClass, group, id, neighbor_ids::kNeighborClass};
                if (ResourceManager(persist)->TestForKey(ResourceKey(from))) persist->CopyRecord(from, source, to, segment);
            }
            if (ids.mFirst) ids.FreeBlock();
            if (source) source->Release();
        }
        if (segment) segment->Release();
    }
    if (context) context->Release();
    return result;
}

namespace {
constexpr uint32_t kTokenNeighborGuid = 0x4dd398a5;   // a family inventory token whose properties are neighbour ids to resolve
constexpr uint32_t kTokenReferencedGuid = 0xedd39930; // a family inventory token whose properties are referenced neighbour ids
} // namespace

bool cTSNeighborManager::CollectReferenced(ifaces::cITSDataImportExportContext *container)
{
    ifaces::cITSNeighborDataImportExportContext *context = nullptr;
    uint8_t changed = 0;
    if (container && container->QueryInterface(neighbor_ids::kNeighborContextIid, &context)) {
        cTSNeighborManager *manager = this;
        ifaces::cITSGlobals *app = Globals();
        std::set<int32_t> referenced;
        ServiceBlockVector families{};
        context->CopyListedIds(&families);
        for (const uint32_t *it = static_cast<const uint32_t *>(families.mFirst); it != families.mLast; ++it) {
            ifaces::cITSInventory *inventory = app->GetNeighborhood()->GetFamilyInventory(*it);
            if (!inventory) continue;
            void **token, **end;
            inventory->TokensBegin(&token);
            inventory->TokensEnd(&end);
            for (; token != end; ++token) {
                auto *object = As<ifaces::cITSInventoryToken>(*token);
                const uint32_t guid = object->GetGuid();
                if (guid == kTokenNeighborGuid) {
                    // ORIGINAL-BUG: the 16-bit count is sign-extended and compared unsigned (a negative count loops ~4e9 times).
                    const uint32_t count = uint32_t(int32_t(int16_t(object->GetPropertyCount())));
                    for (uint32_t i = 0; i < count; ++i) {
                        const int32_t id = int16_t(object->GetProperty(int32_t(i)));
                        if (manager->GetNeighbor(id, 1) && !context->IsResolved(id)) {
                            changed = 1;
                            context->AddResolved(id);
                        }
                    }
                } else if (guid == kTokenReferencedGuid) {
                    const uint32_t count = uint32_t(int32_t(int16_t(object->GetPropertyCount())));
                    for (uint32_t i = 0; i < count; ++i) {
                        const int32_t id = int16_t(object->GetProperty(int32_t(i)));
                        if (manager->GetNeighbor(id, 1)) referenced.insert(id);
                    }
                }
            }
        }
        // The resolved neighbours' sim inventories: tokens with flag 2 and at least 12 properties hold a person data id
        // (properties 6:5) of a neighbour they reference.
        ServiceBlockVector neighbors{};
        context->CopyResolved(&neighbors);
        for (const uint32_t *it = static_cast<const uint32_t *>(neighbors.mFirst); it != neighbors.mLast; ++it) {
            const uint32_t id = *it;
            if (!manager->GetNeighbor(int32_t(id), 1) || !app->GetNeighborhood()) continue;
            ifaces::cITSInventory *inventory = app->GetNeighborhood()->GetSimInventory(id);
            if (!inventory) continue;
            void **token, **end;
            inventory->TokensBegin(&token);
            inventory->TokensEnd(&end);
            for (; token != end; ++token) {
                auto *object = As<ifaces::cITSInventoryToken>(*token);
                if (!object || !(object->GetFlags() & 2) || int16_t(object->GetPropertyCount()) < 12) continue;
                const int16_t high = int16_t(object->GetProperty(6));
                const uint16_t low = object->GetProperty(5);
                ifaces::cITSNeighbor *other = NeighborByKey(manager, uint32_t(high) << 16 | low);
                if (other) referenced.insert(int16_t(other->GetId()));
            }
        }
        for (auto it = referenced.begin(); it != referenced.end(); ++it) {
            if (!context->IsKnown(*it)) {
                changed = 1;
                context->AddPending(*it);
            }
        }
        // The context lists all its neighbours into the same vector (not emptied first).
        context->CopyAll(&neighbors);
        for (const uint32_t *it = static_cast<const uint32_t *>(neighbors.mFirst); it != neighbors.mLast; ++it) {
            const uint32_t id = *it;
            ifaces::cITSNeighbor *neighbor = NeighborAt(manager, int32_t(id), 1);
            if (!neighbor) continue;
            const uint32_t key = neighbor->GetPersonDataId();
            changed |= context->AddSourceKey(int32_t(id), key);
        }
        if (neighbors.mFirst) neighbors.FreeBlock();
        if (families.mFirst) families.FreeBlock();
    }
    if (context) context->Release();
    return changed;
}

// ---- creating a user character ----

namespace {
constexpr uint32_t kDefaultTemplate = 0x7fd96b54;   // the template used for 0 / -1
constexpr uint32_t kDefaultNameString = 0x6ced4870; // the default name (app interface slot 4)
constexpr int32_t kUserCounterLimit = 32000;
using DataSet = std::set<void *, std::less<void *>, GZServiceAllocator<void *>>;
} // namespace

int32_t cTSNeighborManager::CreateUserNeighbor(void **out, uint32_t templateId, uint32_t a3, uint32_t a4, uint32_t a5)
{
    ifaces::cITSPersistSystem *persist = PersistSystem();
    ifaces::cITSGlobals *app = Globals();
    auto *characters = app->GetObjectDefTable();
    if (templateId == 0 || templateId == 0xffffffffu) templateId = kDefaultTemplate;
    void *characterTemplate = characters->FindRecord(templateId);
    if (!characterTemplate) return -1;
    cRZString name{GameAddress<void>(cRZString::kVTable), std::string(), 0};
    appinit::CommonImplSlot4(kDefaultNameString, uint32_t(uintptr_t(&name)), 1, 0, 0xffffffffu);
    ifaces::cITSGameStateController *game = app->GetGameStateController();
    int32_t counter = mUserFileCounter;
    if (counter >= kUserCounterLimit) {
        mUserFileCounter = 0;
        counter = 0;
    }
    void *character = nullptr;
    uint8_t created = 0;
    int32_t result = -1;
    for (;;) {
        // The file name: the neighbourhood's file prefix, then _UserNNNNN.
        cRZString file{GameAddress<void>(cRZString::kVTable), std::string(), 0};
        file.Sprintf("_User%05d", counter);
        auto *prefix = static_cast<cIGZString *>(game->GetNeighborhoodInfo()->GetFilePrefix());
        const uint32_t size = prefix->Strlen();
        const char *data = prefix->Data();
        file.mString = std::string(data, size) + file.mString;
        if (!persist->HasSegmentForFile(&name, &file, 0)) {
            if (character) {
                void *old = character;
                character = nullptr;
                As<cIGZUnknown>(old)->Release();
            }
            created = characters->CreateCharacter(neighbor_ids::kCharacterIid, &character, 1, characterTemplate, &name,
                                                  &file, 0, a3, a4, a5);
        }
        counter = int32_t(++mUserFileCounter);
        if (counter >= kUserCounterLimit) {
            if (!created) goto done;
            break;
        }
        if (created) break;
    }
    {
        DataSet data;
        // The character's slot 4 (fills the set with its data objects; undetermined, no fact names it).
        characters->AdoptCharacterData(VirtualCall<void *(__thiscall *)(void *, DataSet *, uint32_t), 4>(character, &data, 1));
        void *held = nullptr;
        if (data.size() && *data.begin()) {
            held = *data.begin();
            As<cIGZUnknown>(held)->AddRef();
        }
        auto *neighbor = As<ifaces::cITSNeighbor>(CreateNeighbor(held));
        if (neighbor) {
            void *person = neighbor->GetPersonData();
            if (person != held) {
                void *old = held;
                if (person) As<cIGZUnknown>(person)->AddRef();
                held = person;
                if (old) As<cIGZUnknown>(old)->Release();
            }
            if (held) {
                As<cIGZUnknown>(neighbor)->AddRef();
                *out = neighbor;
                result = 0;
            }
        }
        if (held) As<cIGZUnknown>(held)->Release();
    }
done:
    if (character) As<cIGZUnknown>(character)->Release();
    return result;
}

namespace {
constexpr uint32_t kCharacterSetType = cTSNeighborManager::kClassId; // the container's own character set (a manager)
constexpr uint32_t kPersonDataSize = 0x1ce; // the attribute block copied from the source neighbour
} // namespace

bool cTSNeighborManager::Import(ifaces::cITSDataImportExportContext *container)
{
    uint8_t result = 0;
    ifaces::cITSNeighborDataImportExportContext *context = nullptr;
    if (container && container->QueryInterface(neighbor_ids::kNeighborContextIid, &context)) {
        ifaces::NeighborhoodImportSource *hoodContext = nullptr;
        container->QueryInterface(neighbor_ids::kHoodContextIid, &hoodContext);
        ifaces::cIGZPersistDBSegment *segment = nullptr;
        if (container->QueryHeld(neighbor_ids::kSegmentIid, &segment)) {
            result = 1;
            cTSNeighborManager *manager = this;
            ifaces::cITSGlobals *app = Globals();
            ifaces::cITSPersistSystem *persist = PersistSystem();
            const uint32_t group = container->GetGroup();
            const uint32_t ourGroup = GetGroup();
            ifaces::cIGZPersistDBSegment *source = nullptr;
            ifaces::cIGZPersistResourceManager *database = ResourceManager(persist);
            const cGZPersistResourceKey ourKey = GetKey();
            database->FindSegmentWithKey(&ourKey, reinterpret_cast<void **>(&source));
            ifaces::cITSNeighborManager *characterSet = nullptr;
            const uint32_t setKey[4] = {kCharacterSetType, group, 0, 0};
            persist->GetResource(KeyArg<Key16>(setKey), neighbor_ids::kManagerIid, reinterpret_cast<void **>(&characterSet),
                                 0, 0);
            if (characterSet) characterSet->Reload(group, 0);
            ifaces::cITSNeighborhood *hoodRecord = nullptr;
            const uint32_t hoodKey[4] = {neighbor_ids::kNeighborhoodType, group, 1, 0};
            persist->GetResource(KeyArg<Key16>(hoodKey), neighbor_ids::kNeighborhoodIid,
                                 reinterpret_cast<void **>(&hoodRecord), 0, 0);
            ServiceBlockVector ids{};
            context->CopyAll(&ids);
            const uint32_t *end = static_cast<const uint32_t *>(ids.mLast);
            for (const uint32_t *it = static_cast<const uint32_t *>(ids.mFirst); it != end; ++it) {
                const uint32_t id = *it;
                uint32_t ourId = 0;
                if (!context->MapId(id, &ourId)) continue;
                ifaces::cITSNeighbor *neighbor = NeighborAt(manager, int32_t(ourId), 1);
                const uint32_t sourceKey = context->GetSourceKey(id);
                ifaces::cITSNeighbor *from = nullptr;
                if (!characterSet) {
                    const uint32_t key[4] = {neighbor->GetGZCLSID(), group, sourceKey, 0};
                    persist->GetResource(KeyArg<Key16>(key), neighbor_ids::kNeighborIid, reinterpret_cast<void **>(&from),
                                         0, 0);
                } else {
                    ifaces::cITSNeighbor *found = characterSet->FindNeighborByKey(sourceKey);
                    if (found != from) {
                        ifaces::cITSNeighbor *old = from;
                        if (found) As<cIGZUnknown>(found)->AddRef();
                        from = found;
                        if (old) As<cIGZUnknown>(old)->Release();
                    }
                }
                if (!from) continue;

                // The attribute block (keeping attribute 0x3d, the family) and the mapped lot.
                uint16_t *fromBlock = from->GetAttributeBlock(1);
                uint16_t *toBlock = neighbor->GetAttributeBlock(0);
                const uint16_t family = neighbor->GetAttribute(0x3d);
                if (fromBlock && toBlock) std::memcpy(toBlock, fromBlock, kPersonDataSize);
                neighbor->SetAttribute(0x3d, family);
                const int32_t lot = int16_t(neighbor->GetAttribute(0xc3));
                uint32_t mappedLot = 0;
                if (lot && hoodContext && hoodContext->MapLotId(uint32_t(lot), &mappedLot)) {
                    neighbor->SetAttribute(0xc3, uint16_t(mappedLot));
                } else {
                    neighbor->SetAttribute(0xc3, 0);
                    neighbor->SetAttribute(0xc4, 0);
                    neighbor->SetAttribute(0xc5, 0);
                    neighbor->SetAttribute(0xc6, 0);
                }
                neighbor->SetRelatedCount(from->GetRelatedCount());
                neighbor->SetExtraWord(1, from->GetExtraWord(1));
                neighbor->SetExtraWord(2, from->GetExtraWord(2));

                // The relationship table: emptied, then filled from the source's with the ids mapped.
                ifaces::cITSRelationshipTable *fromTable = TableOf(from);
                ifaces::cITSRelationshipTable *toTable = TableOf(neighbor);
                uint32_t count = toTable->GetPersonCount();
                for (uint32_t person = toTable->GetFirstPerson(); count; --count) {
                    toTable->RemovePerson(person);
                    person = toTable->GetNextPerson(person);
                }
                count = fromTable->GetPersonCount();
                for (uint32_t person = fromTable->GetFirstPerson(); count; --count) {
                    uint32_t otherId = 0;
                    if (context->MapId(person, &otherId) && manager->GetNeighbor(int32_t(otherId), 1)) {
                        const uint32_t values = fromTable->GetValueCount(person);
                        if (toTable->GetValueCount(otherId) < values)
                            toTable->SetValueCount(uint32_t(int32_t(int16_t(neighbor->GetId()))), otherId, values);
                        for (uint32_t i = 0; i < values; ++i) toTable->SetValue(otherId, i, fromTable->GetValue(person, i));
                    }
                    person = fromTable->GetNextPerson(person);
                }

                // The sim inventory: the neighbourhood record's, else the one stored in the container's segment.
                if (app->GetNeighborhood()) {
                    ifaces::cITSInventory *inventory = app->GetNeighborhood()->GetSimInventory(ourId);
                    if (inventory) {
                        inventory->ClearTokens(0);
                        inventory->ClearEntries();
                        ifaces::cITSInventory *held = nullptr;
                        ifaces::cITSInventory *found = hoodRecord ? hoodRecord->GetSimInventory(id) : nullptr;
                        if (found) {
                            found->AddRef();
                            held = found;
                        } else {
                            const uint32_t recordKey[4] = {neighbor_ids::kSimInventoryRecordType, group, id, 0};
                            ifaces::cIGZPersistDBRecord *record = nullptr;
                            if (segment->OpenRecord(ResourceKey(recordKey), &record, 1)) {
                                ifaces::cIGZIStream *reader = nullptr;
                                if (record->QueryInterface(neighbor_ids::kIStreamIid, &reader)) {
                                    uint32_t version = kInventoryRecordVersion;
                                    reader->GetUint32(&version);
                                    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
                                    if (metadata) metadata = metadata->Construct(version);
                                    reader->SetMetadata(metadata);
                                    ifaces::cITSInventory *data = nullptr;
                                    As<ifaces::cIGZCOM>(GetDirectorFrameworkObject())
                                        ->GetClassObject(neighbor_ids::kInventoryClass, neighbor_ids::kInventoryIid, &data);
                                    result &= data->Read(reader); // not null-checked (as the original)
                                    if (data) {
                                        data->AddRef();
                                        held = data;
                                        data->Release();
                                    }
                                }
                                segment->CloseRecord(record);
                                if (reader) reader->Release();
                            }
                            if (record) record->Release();
                        }
                        if (held) {
                            void **token, **last;
                            held->TokensBegin(&token);
                            held->TokensEnd(&last);
                            for (; token != last; ++token) {
                                auto *object = As<ifaces::cITSInventoryToken>(*token);
                                uint32_t mapped = 0;
                                if (context->MapKey(object->GetGuid(), &mapped)) object->SetGuid(mapped);
                                auto *characters = app->GetObjectDefTable();
                                void *known = characters->FindRecord(object->GetGuid());
                                if (object->GetGuid() == 0 || known) {
                                    cTSNeighborManager::RemapLotEntry(context, object, ourId);
                                    if (!(object->GetFlags() & 4)) inventory->AddToken(object);
                                }
                            }
                            held->EntriesBegin(&token);
                            held->EntriesEnd(&last);
                            for (; token != last; ++token) {
                                auto *object = As<ifaces::cITSInventoryToken>(*token);
                                uint32_t mapped = 0;
                                if (context->MapKey(object->GetGuid(), &mapped)) object->SetGuid(mapped);
                                auto *characters = app->GetObjectDefTable();
                                void *known = characters->FindRecord(object->GetGuid());
                                if (object->GetGuid() == 0 || known) inventory->AddEntry(object);
                            }
                            held->Release();
                        }
                    }
                }

                // The neighbour's data tables record, copied from the container's segment into ours.
                const uint32_t fromKey[4] = {neighbor_ids::kDataTablesClass, group, id, neighbor_ids::kNeighborClass};
                const uint32_t toKey[4] = {neighbor_ids::kDataTablesClass, ourGroup, ourId, neighbor_ids::kNeighborClass};
                if (ResourceManager(persist)->TestForKey(ResourceKey(fromKey)))
                    persist->CopyRecord(fromKey, segment, toKey, source);
                As<cIGZUnknown>(from)->Release();
            }
            if (ids.mFirst) ids.FreeBlock();
            if (hoodRecord) hoodRecord->Release();
            if (characterSet) characterSet->Release();
            if (source) source->Release();
        }
        if (segment) segment->Release();
        if (hoodContext) As<cIGZUnknown>(hoodContext)->Release();
    }
    if (context) context->Release();
    return result;
}

namespace {
// Tokens whose property 0 is a lot id (mapped through the neighbourhood import source); B and D are marked (flag 4) when
// the lot does not map.
constexpr uint32_t kLotTokenGuidA = 0x5350ec38, kLotTokenGuidB = 0x2eaa244c, kLotTokenGuidC = 0x8e77e373,
                   kLotTokenGuidD = 0x0eaa2454;
constexpr uint32_t kNeighborTokenGuid = 0xed4cb1a4; // property 1 is a neighbour id
} // namespace

void cTSNeighborManager::RemapLotEntry(void *contextObject, void *entry, uint32_t id)
{
    if (!contextObject || !entry) return;
    auto *context = As<ifaces::cITSNeighborDataImportExportContext>(contextObject);
    auto *token = As<ifaces::cITSInventoryToken>(entry);
    if ((token->GetFlags() & 2) && int16_t(token->GetPropertyCount()) >= 12) {
        // Property 4 (and 13) a neighbour id, properties 6:5 a person data id.
        if (int16_t(token->GetPropertyCount()) < 12) return;
        uint32_t mapped = 0;
        if (context->MapId(uint32_t(int32_t(int16_t(token->GetProperty(4)))), &mapped)) token->SetProperty(4, mapped);
        const int32_t high = int16_t(token->GetProperty(6));
        const uint32_t key = uint32_t(high) << 16 | token->GetProperty(5);
        uint32_t mappedKey = 0;
        if (context->MapKey(key, &mappedKey)) {
            token->SetProperty(6, uint32_t(int32_t(mappedKey) >> 16));
            token->SetProperty(5, mappedKey & 0xffff);
        }
        if (int16_t(token->GetPropertyCount()) < 13) return;
        if (context->MapId(uint32_t(int32_t(int16_t(token->GetProperty(13)))), &mapped)) token->SetProperty(13, mapped);
        return;
    }
    const uint32_t guid = token->GetGuid();
    if (guid == kLotTokenGuidA || guid == kLotTokenGuidB || guid == kLotTokenGuidC || guid == kLotTokenGuidD) {
        if (int16_t(token->GetPropertyCount()) < 1) return;
        const int32_t lot = int16_t(token->GetProperty(0));
        uint32_t mappedLot = 0;
        ifaces::NeighborhoodImportSource *hood = nullptr;
        if (context->QueryInterface(neighbor_ids::kHoodContextIid, &hood) && hood->MapLotId(uint32_t(lot), &mappedLot)) {
            token->SetProperty(0, mappedLot);
        } else {
            const uint32_t again = token->GetGuid();
            if (again == kLotTokenGuidD || again == kLotTokenGuidB) token->SetFlags(4);
        }
        if (hood) As<cIGZUnknown>(hood)->Release();
        return;
    }
    if (token->GetGuid() != kNeighborTokenGuid || int16_t(token->GetPropertyCount()) < 4) return;
    uint32_t mapped = 0;
    if (context->MapId(uint32_t(int32_t(int16_t(token->GetProperty(1)))), &mapped))
        token->SetProperty(1, mapped);
    else
        token->SetProperty(1, id);
}

// ---- the RVA hooks' adapters (qualified calls: no virtual dispatch) ----

using Adapters = cTSNeighborManagerAdapters;

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

uint8_t __fastcall Adapters::RegisterMessages(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::RegisterMessages();
}

uint8_t __fastcall Adapters::UnregisterMessages(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::UnregisterMessages();
}

void __fastcall Adapters::SetDirty(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::SetDirty();
}

bool __fastcall Adapters::SetKey(cIGZPersistResource *face, void *, const cGZPersistResourceKey &key)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetKey(key);
}

uint8_t __fastcall Adapters::SaveNeighbors(Self *self, void *, uint8_t all)
{
    [[msvc::forceinline_calls]] return self->Self::SaveNeighbors(all);
}

void __fastcall Adapters::CountRelatedNeighbors(Self *self, void *, void *target)
{
    [[msvc::forceinline_calls]] return self->Self::CountRelatedNeighbors(target);
}

void __fastcall Adapters::CountRelatedNeighborsOf(Self *self, void *, void *target)
{
    [[msvc::forceinline_calls]] return self->Self::CountRelatedNeighborsOf(target);
}

uint8_t __fastcall Adapters::LoadNeighbors(Self *self, void *, uint32_t group)
{
    [[msvc::forceinline_calls]] return self->Self::LoadNeighbors(group);
}

void __fastcall Adapters::RemoveNeighborEffects(Self *self, void *, void *neighbor)
{
    [[msvc::forceinline_calls]] return self->Self::RemoveNeighborEffects(neighbor);
}

void __fastcall Adapters::UpdateKeyMap(Self *self, void *, void *neighbor, uint8_t remove)
{
    [[msvc::forceinline_calls]] return self->Self::UpdateKeyMap(neighbor, remove);
}
