// nTSCASCore::cTSBinInfoFactory Read / Write (/O2): the three CAS record types stored as binary string property sets
// (BINX cTSSimFragment, COLL cTSBinInfo, GZPS cTSSimOutfitData). The record goes through a temporary
// cGZPropertySetResourceString; the CAS resource loads itself from (or saves itself into) its set, with the record's
// 3IDR key list and the segment it came from. Lane DU (PS3).
#include "cas/cTSBinInfoFactory.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZPropertySetResource.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPropertySetSerializer.h"
#include "ifaces/cITSPersistSystem.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"

#include <cstring>

namespace nTSCASCore {
namespace {

// A counted pointer held in a local and released on the way out.
struct Held {
    void *object = nullptr;
    ~Held()
    {
        if (object) As<cIGZUnknown>(object)->Release();
    }
    void Clear()
    {
        if (void *old = object) {
            object = nullptr;
            As<cIGZUnknown>(old)->Release();
        }
    }
};

ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}

// The record's slot 15: the segment it belongs to (iid 0x656b8f0c).
void RecordSegment(void *record, void **out)
{
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, void **), 15>(record, cTSBinInfoFactory::kSegmentIid, out);
}

// The 3IDR key list's slot 7 (what the CAS load / save takes), or 0 for no list on the read side.
uint32_t KeyListData(void *list) { return VirtualCall<uint32_t(__thiscall *)(void *), 7>(list); }

// The CAS resource's load and save slots, by record type: {face iid, load slot, save slot}.
struct CasFace {
    uint32_t iid;
    int load;
    int save;
};
bool FaceOf(uint32_t type, CasFace *face)
{
    switch (type) {
    case cTSBinInfoFactory::kTypeSimFragment: *face = {cTSBinInfoFactory::kIidSimFragment, 29, 28}; return true;
    case cTSBinInfoFactory::kTypeBinInfo: *face = {cTSBinInfoFactory::kIidBinInfo, 25, 24}; return true;
    case cTSBinInfoFactory::kTypeOutfitData: *face = {cTSBinInfoFactory::kIidOutfitData, 87, 86}; return true;
    default: return false;
    }
}
void CallLoad(void *cas, int slot, void *set, uint32_t list, void *segment)
{
    using Load = void(__thiscall *)(void *, void *, uint32_t, void *);
    if (slot == 29) VirtualCall<Load, 29>(cas, set, list, segment);
    else if (slot == 25) VirtualCall<Load, 25>(cas, set, list, segment);
    else VirtualCall<Load, 87>(cas, set, list, segment);
}
void CallSave(void *cas, int slot, void *set, uint32_t list)
{
    using Save = void(__thiscall *)(void *, void *, uint32_t);
    if (slot == 28) VirtualCall<Save, 28>(cas, set, list);
    else if (slot == 24) VirtualCall<Save, 24>(cas, set, list);
    else VirtualCall<Save, 86>(cas, set, list);
}

// A new temporary property-set resource, counted through its set face (null if the allocation failed).
void *NewSetFace()
{
    // In the standalone shape since lane EL: our resource class, our vtables.
    auto *resource = new (std::nothrow) cGZPropertySetResourceString;
    void *set = resource ? resource->SetFace() : nullptr;
    if (set) As<cIGZUnknown>(set)->AddRef();
    return set;
}

} // namespace

// 0xb89520 (slot 4): a new CAS resource of the record type (its original constructor; the CAS classes are other
// groups), counted, given its type (slot 8 SetType), then asked for riid. An unknown type, or an allocation that
// fails, gives 0. (The original's frame would free the outfit's memory if its constructor threw; none of the three
// throws but through operator new inside them, which leaves nothing to free.)
bool cTSBinInfoFactory::CreateInstance2(uint32_t type, uint32_t riid, void **out, uint32_t, uint32_t)
{
    void *created = nullptr;
    switch (type) {
    case kTypeOutfitData:
        if (void *memory = operator new(kOutfitDataSize))
            created = OriginalFunction<void *(__thiscall *)(void *), 0xb922d0>()(memory);
        break;
    case kTypeBinInfo:
        if (void *memory = operator new(kBinInfoSize))
            created = OriginalFunction<void *(__thiscall *)(void *), 0xb86e30>()(memory);
        break;
    case kTypeSimFragment:
        if (void *memory = operator new(kSimFragmentSize))
            created = OriginalFunction<void *(__thiscall *)(void *), 0xb86ea0>()(memory);
        break;
    default:
        return false;
    }
    if (!created) return false;
    As<cIGZUnknown>(created)->AddRef();
    Held resource;
    resource.object = created;
    VirtualCall<void(__thiscall *)(void *, uint32_t), 8>(created, type);
    return As<cIGZUnknown>(created)->QueryInterface(riid, out);
}

// 0xb8ddb0: reads the record into a temporary set, then fetches the record's 3IDR key list (persist system GetResource
// with the record's key retyped) and its segment (the resource manager's slot 34 for the record key, else the record's
// own segment), and has the CAS resource load itself. Returns 1 once the load is called (its result is not used); 0 if
// the stream, the read, the face or the type fails.
bool cTSBinInfoFactory::Read(void *resource, void *record)
{
    Held stream;
    if (!As<cIGZUnknown>(record)->QueryInterface(kIStreamIidForRead, &stream.object)) return false;
    void *set = NewSetFace();
    bool result = false;
    {
        Held serializer;
        if (As<cIGZUnknown>(set)->QueryInterface(gzpropser::kIid, &serializer.object) &&
            As<ifaces::cIGZPropertySetSerializer>(serializer.object)->Read(stream.object, set)) {
            const uint32_t *recordKey = static_cast<const uint32_t *>(As<ifaces::cIGZPersistDBRecord>(record)->GetKey());
            ifaces::cITSPersistSystem *persist = PersistSystem();
            uint32_t listKey[4];
            std::memcpy(listKey, recordKey, sizeof(listKey));
            listKey[0] = kKeyListType;
            Held list;
            persist->GetResource(reinterpret_cast<const struct Key16 *>(listKey), kKeyListIid, &list.object, 0, 0);
            Held segment;
            void *manager = persist->GetResourceManager();
            segment.Clear();
            if (!VirtualCall<bool(__thiscall *)(void *, const void *, void **), 34>(manager, recordKey, &segment.object)) {
                segment.Clear();
                RecordSegment(record, &segment.object);
            }
            CasFace face;
            if (FaceOf(recordKey[0], &face)) {
                Held cas;
                if (As<cIGZUnknown>(resource)->QueryInterface(face.iid, &cas.object)) {
                    CallLoad(cas.object, face.load, set, list.object ? KeyListData(list.object) : 0, segment.object);
                    result = true;
                }
            }
        }
    }
    if (set) As<cIGZUnknown>(set)->Release();
    return result;
}

// 0xb90e10: the CAS resource saves itself into a temporary set with the record's 3IDR key list (from the persist
// system, or a new one from the framework keyed like the record and given the record's segment), the list gets the
// segment (slot 4), and the set is written binary to the record's output stream; returns the write's result.
bool cTSBinInfoFactory::Write(void *resource, void *record)
{
    Held stream;
    if (!As<cIGZUnknown>(record)->QueryInterface(kOStreamIidForWrite, &stream.object)) return false;
    void *set = NewSetFace();
    bool result = false;
    {
        Held serializer;
        if (As<cIGZUnknown>(set)->QueryInterface(gzpropser::kIid, &serializer.object)) {
            uint32_t listKey[4];
            std::memcpy(listKey, As<ifaces::cIGZPersistDBRecord>(record)->GetKey(), sizeof(listKey));
            const uint32_t type = listKey[0];
            listKey[0] = kKeyListType;
            Held list;
            Held segment;
            ifaces::cITSPersistSystem *persist = PersistSystem();
            list.Clear();
            if (!persist->GetResource(reinterpret_cast<const struct Key16 *>(listKey), kKeyListIid, &list.object, 0, 0)) {
                void *framework = GetDirectorFrameworkObject();
                list.Clear();
                VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(framework, kKeyListType, kKeyListIid, &list.object);
                void *persistFace = VirtualCall<void *(__thiscall *)(void *), 5>(list.object);
                As<ifaces::cIGZPersistDBRecord>(persistFace)->SetKey(reinterpret_cast<const struct cGZPersistResourceKey *>(listKey));
                segment.Clear();
                RecordSegment(record, &segment.object);
            }
            CasFace face;
            if (FaceOf(type, &face)) {
                Held cas;
                if (As<cIGZUnknown>(resource)->QueryInterface(face.iid, &cas.object)) {
                    // (No null check on the list: the original calls it whether or not the framework made one.)
                    CallSave(cas.object, face.save, set, KeyListData(list.object));
                    VirtualCall<void(__thiscall *)(void *, void *), 4>(list.object, segment.object);
                    result = As<ifaces::cIGZPropertySetSerializer>(serializer.object)->Write(stream.object, set, true);
                }
            }
        }
    }
    if (set) As<cIGZUnknown>(set)->Release();
    return result;
}

} // namespace nTSCASCore
