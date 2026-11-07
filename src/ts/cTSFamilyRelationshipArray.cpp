#include "ts/cTSFamilyRelationshipArray.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZCOM.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cIGZSerializable.h"
#include "ifaces/cITSDataImportExportContext.h"
#include "ifaces/cITSFamilyRelationshipArray.h"
#include "ifaces/cITSNeighborDataImportExportContext.h"
#include "ifaces/cITSPersistSystem.h"
#include "misc/UtilitiesC.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"

#include <bit>
#include <new>
#include <set>

// ---- construction and destruction ----

// 0x4f2070: the compiler installs our four vtables (the base interfaces' first, as the original's dead stores).
cTSFamilyRelationshipArray::cTSFamilyRelationshipArray() : mRefCount(0), mDirty(0), mKey{}, mRelations() {}

// The game destroys the array only inside Release (0x4f3770): the vtables go back to the class's own, then the map.
cTSFamilyRelationshipArray::~cTSFamilyRelationshipArray() = default;

// 0x4f26e0
cTSFamilyRelationshipArray *cTSFamilyRelationshipArray::Create()
{
    return new cTSFamilyRelationshipArray;
}

// slot 1 (folded 0x146a40)
uint32_t cTSFamilyRelationshipArray::AddRef()
{
    return ++mRefCount;
}

// 0x4f3770
uint32_t cTSFamilyRelationshipArray::Release()
{
    if (mRefCount <= 1) {
        delete this; // the destructor inline, then operator delete(this, 0x30)
        return 0;
    }
    return --mRefCount;
}

// 0x4f2110: the destructor of a SimIdVector (_Tidy): the block goes back through the allocator service.
void SimIdVectorTidy::Destroy()
{
    reinterpret_cast<SimIdVector *>(this)->~SimIdVector();
}

// ---- cITSFamilyRelationshipArray ----

// 0x4f2f00
bool cTSFamilyRelationshipArray::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case kInterfaceId:
    case 1: // cIGZUnknown
        *out = static_cast<cITSFamilyRelationshipArray *>(this);
        AddRef();
        return true;
    // The other faces are cast with the null test a base-class conversion carries (neg/sbb/and in the original).
    case kPersistResourceId:
        *out = static_cast<cIGZPersistResource *>(this);
        break;
    case kContainerId:
        *out = static_cast<cITSDataContainer *>(this);
        break;
    case kSerializableId:
        *out = static_cast<cIGZSerializable *>(this);
        break;
    default:
        return false;
    }
    AddRef();
    return true;
}

// 0x4f21a0: true when the sim already had a relationship (replaced).
bool cTSFamilyRelationshipArray::SetRelationship(uint16_t sim, ifaces::cITSFamilyRelationship *relation)
{
    mDirty = 1;
    auto found = mRelations.find(sim);
    if (found != mRelations.end()) {
        found->second.Assign(relation);
        return true;
    }
    const FamilyRelationMap::value_type entry(sim, relation); // one counted reference, released after the insert
    mRelations.insert(entry);
    return false;
}

// 0x4f38a0
bool cTSFamilyRelationshipArray::RemoveRelationship(uint16_t sim)
{
    auto found = mRelations.find(sim);
    if (found == mRelations.end()) return false;
    mRelations.erase(found);
    mDirty = 1;
    return true;
}

// 0x4f2e80
void cTSFamilyRelationshipArray::SetDirty()
{
    mDirty = 1;
}

// 0x4f2db0
ifaces::cITSFamilyRelationship *cTSFamilyRelationshipArray::GetRelationship(uint16_t sim)
{
    auto found = mRelations.find(sim);
    return found != mRelations.end() ? found->second.Get() : nullptr;
}

// slot 5 (folded 0x16db30): the byte as it is (not normalised to 0 / 1).
bool cTSFamilyRelationshipArray::IsDirty()
{
    return std::bit_cast<bool>(mDirty);
}

// slot 10 (folded 0x6c6b0, shared with many classes' empty slots)
uint32_t cTSFamilyRelationshipArray::Unused10()
{
    return 0;
}

// slot 11 (folded 0x1474a0, `xor al, al; ret 0xc`)
bool cTSFamilyRelationshipArray::Unused11(uint32_t, uint32_t, uint32_t)
{
    return false;
}

// slot 12 (folded 0x149cf0); also the persist face's slot 11 (adjustor thunk 0x4f2dfc)
uint32_t cTSFamilyRelationshipArray::GetRefCount()
{
    return mRefCount;
}

// slot 15 (folded 0x3a4f40): `this ? this + 8 : 0`
cIGZPersistResource *cTSFamilyRelationshipArray::GetPersistResource()
{
    return static_cast<cIGZPersistResource *>(this);
}

// 0x4f3b10: the source's stream reader, read through the serializable face.
bool cTSFamilyRelationshipArray::ReadFrom(cIGZUnknown *source)
{
    cRZAutoRefCount<ifaces::cIGZIStream> reader;
    bool result = false;
    if (source->QueryInterface(kStreamReaderId, reader.AsPPVoid())) result = Read(reader.Get());
    return result;
}

// 0x4f3c10: the target's stream writer, written through the serializable face.
bool cTSFamilyRelationshipArray::WriteTo(cIGZUnknown *target)
{
    cRZAutoRefCount<ifaces::cIGZOStream> writer;
    bool result = false;
    if (target->QueryInterface(kStreamWriterId, writer.AsPPVoid())) result = Write(writer.Get());
    return result;
}

// 0x4f3c90: the persist system saves the +8 face (no segment); the dirty flag is cleared. Callers use al only.
bool cTSFamilyRelationshipArray::Save()
{
    auto *persist = As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
    if (!persist) {
        mDirty = 0;
        return false;
    }
    // The persist system's byte goes out as it is (not normalised to 0 / 1), as the original leaves al in place.
    cIGZPersistResource *resource = this; // the select inline (neg/sbb/and), not slot 15
    const bool saved = std::bit_cast<bool>(persist->SaveResource(As<ifaces::cIGZPersistResource>(resource), nullptr));
    mDirty = 0;
    return saved;
}

// ---- cIGZPersistResource face (the key) ----

// slot 3 (folded 0x147790)
cGZPersistResourceKey cTSFamilyRelationshipArray::GetKey()
{
    return mKey;
}

// slot 4 (folded 0x47d420)
bool cTSFamilyRelationshipArray::SetKey(const cGZPersistResourceKey &key)
{
    mKey = key;
    return true;
}

// slots 5-7 (folded 0x149cf0, 0x149d00, 0x148ba0)
uint32_t cTSFamilyRelationshipArray::GetType()
{
    return mKey.type;
}

uint32_t cTSFamilyRelationshipArray::GetGroup()
{
    return mKey.group;
}

uint32_t cTSFamilyRelationshipArray::GetInstance()
{
    return mKey.instance;
}

// slots 8-10 (folded 0x1eec40, 0x47d410, 0x1f5270)
bool cTSFamilyRelationshipArray::SetType(uint32_t type)
{
    mKey.type = type;
    return true;
}

bool cTSFamilyRelationshipArray::SetGroup(uint32_t group)
{
    mKey.group = group;
    return true;
}

bool cTSFamilyRelationshipArray::SetInstance(uint32_t instance)
{
    mKey.instance = instance;
    return true;
}

// 0x4f3d90
bool cTSFamilyRelationshipArray::Clear()
{
    mRelations.clear();
    mDirty = 0;
    return true;
}

// ---- cIGZSerializable face ----

// 0x4f3e70: version, metadata, count, then (sim id, the relationship's own record) per sim; the results are ANDed.
bool cTSFamilyRelationshipArray::Write(ifaces::cIGZOStream *stream)
{
    stream->SetUint32(kWriteVersion);
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata = metadata->Construct(kWriteVersion);
    stream->SetMetadata(metadata);
    stream->SetUint32(uint32_t(mRelations.size()));
    bool result = true;
    for (const auto &entry : mRelations) {
        stream->SetUint16(entry.first);
        const FamilyRelationRef relation(entry.second);
        result &= relation->GetSerializable()->Write(stream);
    }
    return result;
}

// 0x4f30b0: version (ignored), count, then (sim id, a new relationship's record) per sim; only absent sims are inserted.
bool cTSFamilyRelationshipArray::Read(ifaces::cIGZIStream *stream)
{
    // Failed reads leave these as they were (stack words in the original): they start at zero.
    uint32_t version = 0, count = 0;
    stream->GetUint32(&version);
    stream->GetUint32(&count);
    bool result = true;
    uint16_t sim = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const FamilyRelationRef relation(As<ifaces::cITSFamilyRelationship>(cTSFamilyRelationship::Create()));
        stream->GetUint16(&sim);
        if (!relation->GetSerializable()->Read(stream)) {
            result = false;
        } else {
            const FamilyRelationRef copy(relation);
            mRelations.try_emplace(sim, copy);
        }
    }
    return result;
}

// 0x4f2770
uint32_t cTSFamilyRelationshipArray::GetGZCLSID()
{
    return kClassId;
}

// ---- relating sims ----

namespace {
// cIGZCOM::GetClassObject into a counted pointer. The order is the original's: the framework is fetched first, then the
// pointer's old object is released (AsPPVoid), then the call.
template <class T> __forceinline void CreateInstance(uint32_t classId, uint32_t iid, cRZAutoRefCount<T> &out)
{
    auto *framework = As<ifaces::cIGZCOM>(GetDirectorFrameworkObject());
    void **slot = out.AsPPVoid();
    framework->GetClassObject(classId, iid, slot);
}

// A new relationship from the framework (null in `relation` when it fails).
__forceinline void CreateRelationship(FamilyRelationRef &relation)
{
    CreateInstance(cTSFamilyRelationship::kClassId, cTSFamilyRelationship::kInterfaceId, relation);
}

// The relationship a sim gets when it has none: made by the framework, and handed to the array. When the framework fails
// the pointer stays null and the array is told so (SetRelationship with null).
__forceinline void CreateMissing(cITSFamilyRelationshipArray *array, uint16_t sim, FamilyRelationRef &relation)
{
    CreateRelationship(relation);
    array->SetRelationship(sim, relation.Get());
}
} // namespace

// 0x4f3950: removes the first tie each of the two sims has to the other (creating a missing relationship first).
void cTSFamilyRelationshipArray::Unrelate(uint16_t sim, uint16_t other)
{
    FamilyRelationRef simRelation(GetRelationship(sim));
    FamilyRelationRef otherRelation(GetRelationship(other));
    if (!simRelation) CreateMissing(this, sim, simRelation);
    if (!otherRelation) CreateMissing(this, other, otherRelation);
    if (simRelation && otherRelation) {
        simRelation->RemoveSim(other);
        otherRelation->RemoveSim(sim);
    }
}

// 0x4f2280: makes sim the other's `role` (a FamilyTieType). False for sim == other and for a role that cannot be set;
// true when a relationship could not be created. The old tie between the two is removed first, whatever the role.
bool cTSFamilyRelationshipArray::Relate(uint16_t sim, uint16_t other, int32_t role)
{
    if (sim == other) return false;
    bool result = true;
    FamilyRelationRef simRelation(GetRelationship(sim));
    FamilyRelationRef otherRelation(GetRelationship(other));
    if (!simRelation) CreateMissing(this, sim, simRelation);
    if (!otherRelation) CreateMissing(this, other, otherRelation);
    if (simRelation && otherRelation) {
        Unrelate(sim, other);
        switch (role) {
        case kFamilyTieMother:
        case kFamilyTieFather: {
            // sim becomes a parent: the children it already has become the other's siblings.
            SimIdVector children;
            if (simRelation->GetChildren(&children)) {
                for (uint16_t child : children) {
                    if (child != other) result &= Relate(other, child, kFamilyTieSibling);
                }
            }
            simRelation->SetTie(kFamilyTieChild, other);
            otherRelation->SetTie(role, sim);
            break;
        }
        case kFamilyTieSpouse: {
            // Both sims leave their old spouses first (the same variable holds either one).
            uint16_t spouse = 0;
            if (simRelation->GetTie(kFamilyTieSpouse, &spouse)) Unrelate(sim, spouse);
            if (otherRelation->GetTie(kFamilyTieSpouse, &spouse)) Unrelate(other, spouse);
            simRelation->SetTie(kFamilyTieSpouse, other);
            otherRelation->SetTie(kFamilyTieSpouse, sim);
            break;
        }
        case kFamilyTieSibling:
            simRelation->SetTie(kFamilyTieSibling, other);
            otherRelation->SetTie(kFamilyTieSibling, sim);
            break;
        case kFamilyTieChild: {
            // sim is the other's child: the other becomes sim's mother, or its father when the mother is taken.
            uint16_t parent = 0;
            if (!simRelation->GetTie(kFamilyTieMother, &parent)) result = Relate(other, sim, kFamilyTieMother);
            else if (!simRelation->GetTie(kFamilyTieFather, &parent)) result = Relate(other, sim, kFamilyTieFather);
            else result = false;
            break;
        }
        default:
            result = false;
        }
    }
    return result;
}

// ---- cITSDataContainer face ----

namespace {
const ::Key16 *AsKey16(const cGZPersistResourceKey &key)
{
    return reinterpret_cast<const ::Key16 *>(&key);
}

ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}

} // namespace

// 0x4f27a0: lists the sims that the neighbour context has resolved plus everyone connected to them through ties; every sim
// of that set the context does not know yet is added to it. True when one was added.
bool cTSFamilyRelationshipArray::CollectReferenced(ifaces::cITSDataImportExportContext *context)
{
    cRZAutoRefCount<ifaces::cITSNeighborDataImportExportContext> neighbors;
    if (!context || !context->QueryInterface(kNeighborContextId, neighbors.AsPPVoid())) return false;
    std::set<int32_t> visited;
    ServiceWordVector pending;
    neighbors->CopyResolved(&pending);
    while (!pending.empty()) { // a queue: the front goes first
        const uint32_t sim = pending.front();
        pending.erase(pending.begin());
        if (visited.find(sim) != visited.end()) continue;
        visited.insert(sim);
        ifaces::cITSFamilyRelationship *relation = GetRelationship(uint16_t(sim)); // the slot reads the low word
        if (!relation) continue;
        uint16_t tie = 0;
        for (int32_t type : {kFamilyTieMother, kFamilyTieFather, kFamilyTieSpouse}) {
            if (relation->GetTie(type, &tie) && visited.find(tie) == visited.end()) pending.push_back(tie);
        }
        SimIdVector listed;
        if (relation->GetChildren(&listed)) {
            for (uint16_t id : listed) {
                if (visited.find(id) == visited.end()) pending.push_back(id);
            }
        }
        listed.clear();
        if (relation->GetSiblings(&listed)) {
            for (uint16_t id : listed) {
                if (visited.find(id) == visited.end()) pending.push_back(id);
            }
        }
    }
    bool added = false;
    for (int32_t sim : visited) {
        if (!neighbors->IsKnown(sim)) {
            added = true;
            neighbors->AddPending(sim);
        }
    }
    return added;
}

// 0x4f3ff0: writes a copy of the ties of the neighbour context's sims as a new array resource (instance 1 of the
// context's group) into its database segment.
bool cTSFamilyRelationshipArray::Export(ifaces::cITSDataImportExportContext *context)
{
    bool saved = false;
    cRZAutoRefCount<ifaces::cITSNeighborDataImportExportContext> neighbors;
    if (context && context->QueryInterface(kNeighborContextId, neighbors.AsPPVoid())) {
        cRZAutoRefCount<::cIGZUnknown> segment;
        if (context->QueryHeld(kDBSegmentId, segment.AsPPVoid())) {
            ServiceWordVector ids;
            neighbors->CopyAll(&ids);
            cRZAutoRefCount<ifaces::cITSFamilyRelationshipArray> copy;
            CreateInstance(kClassId, kInterfaceId, copy);
            // The creation is not checked: a failed one faults here.
            cGZPersistResourceKey copyKey = {kClassId, context->GetGroup(),
                                                   kExportInstance, 0};
            copy->GetPersistResource()->SetKey(&copyKey);
            for (uint32_t id : ids) {
                const uint16_t sim = uint16_t(id);
                ifaces::cITSFamilyRelationship *relation = GetRelationship(sim);
                if (!relation) continue;
                FamilyRelationRef duplicate;
                CreateRelationship(duplicate);
                // The tie variable is the stack slot that held the container face pointer: its high half is stale.
                uint16_t tie;
                if (relation->GetTie(kFamilyTieSpouse, &tie)) duplicate->SetTie(kFamilyTieSpouse, tie);
                if (relation->GetTie(kFamilyTieMother, &tie)) duplicate->SetTie(kFamilyTieMother, tie);
                if (relation->GetTie(kFamilyTieFather, &tie)) duplicate->SetTie(kFamilyTieFather, tie);
                SimIdVector listed;
                if (relation->GetSiblings(&listed)) {
                    for (uint16_t sibling : listed) duplicate->SetTie(kFamilyTieSibling, sibling);
                }
                listed.clear();
                if (relation->GetChildren(&listed)) {
                    for (uint16_t child : listed) duplicate->SetTie(kFamilyTieChild, child);
                }
                copy->SetRelationship(sim, duplicate.Get());
            }
            auto *persist = PersistSystem();
            saved = std::bit_cast<bool>(persist->SaveResource(copy->GetPersistResource(), segment.Get()));
        }
    }
    return saved;
}

// 0x4f3300: reads the array resource of the context's group (instance 1, else instance 0) and copies the ties of every
// sim the neighbour context lists into this array, under the ids the context maps them to (ties with no mapping are dropped).
bool cTSFamilyRelationshipArray::Import(ifaces::cITSDataImportExportContext *context)
{
    bool result = false;
    cRZAutoRefCount<ifaces::cITSNeighborDataImportExportContext> neighbors;
    if (context && context->QueryInterface(kNeighborContextId, neighbors.AsPPVoid())) {
        cRZAutoRefCount<::cIGZUnknown> segment;
        if (context->QueryHeld(kDBSegmentId, segment.AsPPVoid())) {
            cRZAutoRefCount<ifaces::cITSFamilyRelationshipArray> loaded;
            const uint32_t group = context->GetGroup();
            const cGZPersistResourceKey keyInstance1 = {kClassId, group, 1, 0};
            const cGZPersistResourceKey keyInstance0 = {kClassId, group, 0, 0};
            auto *persist = PersistSystem();
            bool found = persist->GetResource(AsKey16(keyInstance1), kInterfaceId, loaded.AsPPVoid(), 0, 0);
            if (!found) found = persist->GetResource(AsKey16(keyInstance0), kInterfaceId, loaded.AsPPVoid(), 0, 0);
            if (found) {
                result = true;
                ServiceWordVector ids;
                neighbors->CopyAll(&ids);
                for (uint32_t id : ids) {
                    ifaces::cITSFamilyRelationship *relation = loaded->GetRelationship(uint16_t(id));
                    if (!relation) continue;
                    FamilyRelationRef duplicate;
                    CreateRelationship(duplicate);
                    uint32_t mapped = 0; // zeroed once per sim, then reused by every MapId
                    uint16_t tie;
                    if (relation->GetTie(kFamilyTieSpouse, &tie) && neighbors->MapId(tie, &mapped))
                        duplicate->SetTie(kFamilyTieSpouse, uint16_t(mapped));
                    if (relation->GetTie(kFamilyTieMother, &tie) && neighbors->MapId(tie, &mapped))
                        duplicate->SetTie(kFamilyTieMother, uint16_t(mapped));
                    if (relation->GetTie(kFamilyTieFather, &tie) && neighbors->MapId(tie, &mapped))
                        duplicate->SetTie(kFamilyTieFather, uint16_t(mapped));
                    SimIdVector listed;
                    if (relation->GetSiblings(&listed)) {
                        for (uint16_t sibling : listed) {
                            if (neighbors->MapId(sibling, &mapped)) duplicate->SetTie(kFamilyTieSibling, uint16_t(mapped));
                        }
                    }
                    listed.clear();
                    if (relation->GetChildren(&listed)) {
                        for (uint16_t child : listed) {
                            if (neighbors->MapId(child, &mapped)) duplicate->SetTie(kFamilyTieChild, uint16_t(mapped));
                        }
                    }
                    if (neighbors->MapId(id, &mapped)) SetRelationship(uint16_t(mapped), duplicate.Get());
                }
            }
        }
    }
    return result;
}

// ---- the RVA hooks' adapters ----
// Qualified calls (no virtual dispatch: an object built by the game's constructor carries the game's vtables, whose slots
// are these very hooks), with the bodies forced inline so each adapter is the function itself (byte matching compares
// the adapter, the hook's replacement). A base-to-derived static_cast finds the object from a face.

using Adapters = cTSFamilyRelationshipArrayAdapters;

cTSFamilyRelationshipArray *__fastcall Adapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSFamilyRelationshipArray;
}

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}

uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}


bool __fastcall Adapters::RemoveRelationship(Self *self, void *, uint16_t sim)
{
    [[msvc::forceinline_calls]] return self->Self::RemoveRelationship(sim);
}

void __fastcall Adapters::SetDirty(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::SetDirty();
}







bool __fastcall Adapters::Clear(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Clear();
}






uint32_t __fastcall Adapters::GetGZCLSID(cIGZSerializable *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetGZCLSID();
}
