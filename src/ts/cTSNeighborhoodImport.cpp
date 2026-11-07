// cTSNeighborhood: ImportFromContainer (the cITSDataContainer face's slot 5, 0x477d70), lane EG.
#include "ts/cTSNeighborhood.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "ifaces/ComFramework.h"
#include "ifaces/PersistResourceManager.h"
#include "ifaces/cITSInventory.h"
#include "ifaces/cITSPersistSystem.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"

#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t kImportSourceInterface = 0x4e9c59c7; // what the container must answer (QueryInterface)
constexpr uint32_t kContainerEntryId = 0x656b8f0c;      // the container's slot 12 must hand out an object under this id
constexpr uint32_t kNeighborhoodInterface = 0x7aa51c46;
constexpr uint32_t kBusinessRecordType = 0x104f6a6e;
constexpr uint32_t kBusinessRecordInterface = 0x104f6a72;
constexpr uint32_t kPersistSystemCache = 0x10faf6c;
constexpr uint32_t kPersistSystemIdA = 0x056b906e;
constexpr uint32_t kPersistSystemIdB = 0x9af5e924;
constexpr uint32_t kLotInventoryOwnerKind = 1;

// A counted pointer held by a local of the original: released when it goes out of scope.
struct Ref {
    void *object = nullptr;
    Ref() = default;
    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
    ~Ref() noexcept(false) { if (object) VirtualCall<uint32_t(__thiscall *)(void *), 2>(object); }
};

struct ResourceKey {
    uint32_t type, group, instance, zero;
};
// A business record's two kinds of per-person entries (0x60 and 8 bytes), keyed by a neighbour id.
struct BusinessEntry {
    uint32_t words[0x18];
};
struct BusinessPair {
    uint32_t words[2];
};
static_assert(sizeof(BusinessEntry) == 0x60 && sizeof(BusinessPair) == 8);

// The vectors the source, the records and the persist system fill: blocks of the game's allocator service.
using LotNumberVector = std::vector<uint32_t, GZServiceAllocator<uint32_t>>;
using PersonIdVector = std::vector<uint16_t, GZServiceAllocator<uint16_t>>;
using ResourceKeyVector = std::vector<ResourceKey, GZServiceAllocator<ResourceKey>>;

void *PersistSystem() { return CachedServiceGetter<kPersistSystemCache, kPersistSystemIdA, kPersistSystemIdB>(); }
bool GetResource(void *persist, const ResourceKey *key, uint32_t iid, void **out)
{
    return VirtualCall<bool(__thiscall *)(void *, const ResourceKey *, uint32_t, void **, uint32_t, uint32_t), 5>(persist, key, iid, out, 0, 0);
}
// The id map of the import source (its slot 31): slot 32 renumbers a family or neighbour id; false when it has no new id.
bool MapId(void *idMap, int32_t id, uint32_t *out) { return VirtualCall<bool(__thiscall *)(void *, int32_t, uint32_t *), 32>(idMap, id, out); }

// Moves a business record's entries to their renumbered ids: Fill lists the ids, Get reads an entry, Remove drops it, Put
// makes the entry under the new id. The entries are gathered first (a std::unordered_map, so a renumbering that merges two ids
// keeps the later entry), then written in the map's order.
template <class Entry, unsigned Fill, unsigned Get, unsigned Remove, unsigned Put> void RenumberEntries(void *record, void *idMap)
{
    PersonIdVector ids;
    VirtualCall<void(__thiscall *)(void *, PersonIdVector *), Fill>(record, &ids);
    std::unordered_map<uint32_t, Entry> moved;
    for (uint16_t id : ids) {
        uint32_t mapped = 0;
        if (MapId(idMap, static_cast<int16_t>(id), &mapped)) {
            const Entry *entry = VirtualCall<const Entry *(__thiscall *)(void *, uint32_t), Get>(record, id);
            moved[mapped] = *entry;
        }
        VirtualCall<void(__thiscall *)(void *, uint32_t), Remove>(record, id);
    }
    for (const auto &[id, entry] : moved) {
        const Entry copy = entry;
        *VirtualCall<Entry *(__thiscall *)(void *, uint32_t), Put>(record, id) = copy;
    }
}
} // namespace

bool cTSNeighborhood::Import(ifaces::cITSDataImportExportContext *context)
{
    void *container = context;
    bool imported = false;
    if (!container) return imported;
    Ref source;
    if (!VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(container, kImportSourceInterface, &source.object))
        return imported;
    void *idMap = VirtualCall<void *(__thiscall *)(void *), 31>(source.object);
    Ref entry;
    if (!VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 12>(container, kContainerEntryId, &entry.object))
        return imported;
    void *persist = PersistSystem();
    const uint32_t sourceGroup = VirtualCall<uint32_t(__thiscall *)(void *), 14>(source.object);
    const uint32_t group = GetGroup(); // through the persist face's vtable (slot 6)
    Ref other; // the source neighbourhood, loaded from its group
    const ResourceKey otherKey{cTSNeighborhood::kClassId, sourceGroup, 1, 0};
    if (!GetResource(persist, &otherKey, kNeighborhoodInterface, &other.object)) return imported;
    imported = true;
    std::vector<NeighborDataRef> records; // the renumbered business records, saved at the end
    LotNumberVector lots;
    VirtualCall<void(__thiscall *)(void *, LotNumberVector *), 23>(source.object, &lots);
    for (uint32_t lot : lots) {
        int32_t newLot = -1;
        if (!VirtualCall<bool(__thiscall *)(void *, uint32_t, int32_t *), 25>(source.object, lot, &newLot)) continue;
        // The lot's inventory: the source's tokens are added to this neighbourhood's inventory of the new lot (made when missing).
        ifaces::cITSInventory *sourceInventory = static_cast<cITSNeighborhood *>(other.object)->GetLotInventory(static_cast<int32_t>(lot));
        Ref inventory;
        inventory.object = GetLotInventory(newLot);
        if (inventory.object) As<cIGZUnknown>(inventory.object)->AddRef();
        if (!inventory.object) {
            auto *framework = As<ifaces::ComFramework>(GetDirectorFrameworkObject());
            framework->GetClassObject(cTSNeighborhood::kHelperClass, cTSNeighborhood::kHelperInterface, &inventory.object);
            mMembers1[static_cast<int16_t>(newLot)].Assign(inventory.object);
            if (inventory.object) {
                As<ifaces::cITSInventory>(inventory.object)->SetOwner(kLotInventoryOwnerKind, static_cast<uint32_t>(newLot));
                As<ifaces::cITSInventory>(inventory.object)->SetOwnerGroup(group);
            }
        }
        auto *target = As<ifaces::cITSInventory>(inventory.object);
        target->ClearTokens(0);
        target->ClearEntries();
        void **token;
        void **tokensEnd;
        sourceInventory->TokensBegin(&token);
        sourceInventory->TokensEnd(&tokensEnd);
        for (; token != tokensEnd; ++token) target->AddToken(*token);
        sourceInventory->EntriesBegin(&token);
        sourceInventory->EntriesEnd(&tokensEnd);
        for (; token != tokensEnd; ++token) target->AddEntry(*token);
        // The lot's owning family gets its new id.
        if (void *lotInfo = GetLot(newLot)) {
            uint32_t family = 0;
            MapId(idMap, static_cast<int32_t>(VirtualCall<uint32_t(__thiscall *)(void *), 33>(lotInfo)), &family);
            VirtualCall<void(__thiscall *)(void *, uint32_t), 34>(lotInfo, family);
        }
        // The lot's business record: its people renumbered, its key moved to this group and the new lot number.
        const ResourceKey recordKey{kBusinessRecordType, sourceGroup, lot, 0};
        Ref record;
        if (GetResource(persist, &recordKey, kBusinessRecordInterface, &record.object)) {
            RenumberEntries<BusinessEntry, 23, 25, 28, 27>(record.object, idMap);
            RenumberEntries<BusinessPair, 29, 31, 34, 33>(record.object, idMap);
            auto *manager = VirtualCall<ifaces::PersistResourceManager *(__thiscall *)(void *), 3>(persist);
            manager->ForgetResource(const_cast<ResourceKey *>(&recordKey));
            ResourceKey newKey{kBusinessRecordType, group, static_cast<uint32_t>(newLot), 0};
            VirtualCall<void(__thiscall *)(void *, ResourceKey *), 4>(record.object, &newKey); // SetKey
            records.emplace_back(record.object);
        }
    }
    // This group's old business records go, the imported ones are saved.
    ResourceKeyVector keys;
    VirtualCall<void(__thiscall *)(void *, ResourceKeyVector *, uint32_t, uint32_t), 36>(persist, &keys, group, kBusinessRecordType);
    for (ResourceKey &key : keys) VirtualCall<void(__thiscall *)(void *, ResourceKey *), 17>(persist, &key);
    for (NeighborDataRef &record : records) VirtualCall<void(__thiscall *)(void *, void *, void *), 16>(persist, record.Get(), nullptr);
    return imported;
}

// ---- adapters (the RVA hooks; src/hook/tables/cTSNeighborhood.cpp): next to the bodies they inline ----

using Adapters = cTSNeighborhoodAdapters;

bool __fastcall Adapters::Import(cITSDataContainer *face, void *, ifaces::cITSDataImportExportContext *context)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Import(context);
}
