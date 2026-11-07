// nTSCASCore::cTSCASBodyPartSystem: the part slots of the cITSCASBodyPartSystem face (lane JA). The face's `this` is the
// subobject at +0x1c; see cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "simantics/ServiceRefVector.h"

#include <cstddef>
#include <cstring>

namespace nTSCASCore {

namespace {

void *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<uint8_t *>(iface) - 0x1c;
}

// An object reached through an interface whose cIGZUnknown is a virtual base: the vbptr at +4, the base's offset in its
// second entry, relative to the vbptr.
void *VirtualBaseOf(void *object)
{
    uint8_t *const vbptrAt = static_cast<uint8_t *>(object) + 4;
    const int32_t *const vbtable = *reinterpret_cast<const int32_t *const *>(vbptrAt);
    return vbptrAt + vbtable[1];
}

constexpr uint32_t kIidChildPart = 0x3ca0e8ab;   // asked of each child of a part (slot 81)
constexpr uint32_t kIidBodyPartSystem = 0x4512dd13; // asked of the system itself (slot 81)

// The transform a scene node hands back by value (its slot 53, hidden result pointer): a position, then 16 bytes.
struct NodeTransform {
    float mPosition[3]; // +0x00
    float mRotation[4]; // +0x0c
};
static_assert(sizeof(NodeTransform) == 0x1c, "NodeTransform layout");

using ClearPartWeightSlot = bool(__thiscall *)(void *, int32_t, int32_t);         // slot 17
using ApplyGuidTextSlot = void(__thiscall *)(void *, const char *, uint32_t, uint32_t); // slot 38
using GetPartSlot = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);   // slot 52
using ApplyPartSlot = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, void *); // slot 82

// Applies each name of the vector through slot 38 with (1, 0).
void ApplyNames(BodyPartSystemIface *self, const StringVector *names)
{
    for (cRZString *name = names->mFirst; name != names->mLast; ++name) {
        const char *const text = VirtualCall<const char *(__thiscall *)(void *), 5>(name);
        VirtualCall<ApplyGuidTextSlot, 38>(self, text, 1, 0);
    }
}

} // namespace

void BodyPartSystemIface::ApplyNamesValue0B(const StringVector *names)
{
    if (mAge == 0) return;
    VirtualCall<ClearPartWeightSlot, 17>(this, 0, -1);
    VirtualCall<ClearPartWeightSlot, 17>(this, 0xb, -1);
    ApplyNames(this, names);
}

void BodyPartSystemIface::ApplyNamesValue5(const StringVector *names)
{
    if (mAge == 0) return;
    VirtualCall<ClearPartWeightSlot, 17>(this, 5, -1);
    ApplyNames(this, names);
}

void *BodyPartSystemIface::GetPart(uint32_t a, uint32_t b, uint32_t id)
{
    const uint8_t *const entry =
        OriginalFunction<const uint8_t *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 0xcf1670>()(SystemOf(this), a, b, id);
    return *reinterpret_cast<void *const *>(entry + 0xc);
}

uint8_t BodyPartSystemIface::ApplyToAllParts(void *target)
{
    if (target == nullptr) return 0;
    VirtualCall<void(__thiscall *)(void *), 18>(target);
    using ApplyFn = uint8_t(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    uint8_t result = 1;
    for (const uint32_t *a = mListAFirst; a != mListALast; ++a) {
        for (const uint32_t *b = mListBFirst; b != mListBLast; ++b) {
            if (mAge == 0) {
                for (const uint32_t *c = mIdsFirst; c != mIdsLast; ++c) {
                    result &= OriginalFunction<ApplyFn, 0xce5830>()(SystemOf(this), target, *a, *b, *c, 1, 1);
                }
            } else {
                for (const uint32_t *c = mIdsFirst; c != mIdsLast; ++c) {
                    result &= OriginalFunction<ApplyFn, 0xce5830>()(SystemOf(this), target, *a, *b, *c, 1, 0);
                }
                OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcfa2d0>()(SystemOf(this), *a, *b, 0, 0);
                for (const uint32_t *c = mIdsFirst; c != mIdsLast; ++c) {
                    result &= OriginalFunction<ApplyFn, 0xce5830>()(SystemOf(this), target, *a, *b, *c, 0, 1);
                }
            }
        }
    }
    return result;
}

uint8_t BodyPartSystemIface::ApplyToChildren(uint32_t a, uint32_t b, void *target)
{
    if (target == nullptr) return 0;
    uint8_t result = 1;
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) {
        void *const part = VirtualCall<GetPartSlot, 52>(this, a, b, *id);
        if (part == nullptr) continue;
        void *child = VirtualCall<void *(__thiscall *)(void *), 21>(part);
        while (child != nullptr) {
            void *childPart = nullptr;
            if (VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(child, kIidChildPart, &childPart) &&
                VirtualCall<uint32_t(__thiscall *)(void *), 18>(VirtualBaseOf(childPart)) != 0) {
                void *system = nullptr;
                if (VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(SystemOf(this), kIidBodyPartSystem, &system)) {
                    result &= VirtualCall<ApplyPartSlot, 82>(this, a, b, *id, system);
                }
                if (system != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 2>(system);
            }
            child = VirtualCall<void *(__thiscall *)(void *), 20>(child);
            if (childPart != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 2>(VirtualBaseOf(childPart));
        }
    }
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) {
        result &= VirtualCall<ApplyPartSlot, 82>(this, a, b, *id, target);
    }
    return result;
}

bool BodyPartSystemIface::GetNodeTransform(uint32_t a, uint32_t b, const char *name, void *position, void *rotation)
{
    // The system's +0 face slot 14 (0xcf43f0): the object for (a, b), held for the duration.
    void *const object = VirtualCall<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 14>(SystemOf(this), a, b, 0);
    if (object != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
    bool found = false;
    if (object != nullptr) {
        // FindNodeByName (cdecl): the node of that name, of node type 0xf.
        void *const node = OriginalFunction<void *(__cdecl *)(const char *, void *, uint32_t), 0xae7490>()(name, object, 0xf);
        if (node != nullptr) {
            using TransformSlot = const NodeTransform *(__thiscall *)(void *, NodeTransform *);
            NodeTransform scratch;
            std::memcpy(position, VirtualCall<TransformSlot, 53>(node, &scratch)->mPosition, 12);
            std::memcpy(rotation, VirtualCall<TransformSlot, 53>(node, &scratch)->mRotation, 16);
            found = true;
        }
    }
    if (object != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
    return found;
}

namespace {

using IdsSlot = void(__thiscall *)(void *, IdVector *);       // a part's slot 56: its ids, appended
using EntrySlot = void *(__thiscall *)(void *, uint32_t);     // a part's slot 48: the entry of an id
using UpdateSlot = void(__thiscall *)(void *);                 // an entry's slot 34

// Whether ids other than 0 are refreshed: slot 69 of the face (the byte at +0x120).
bool ShowsAllIds(BodyPartSystemIface *self)
{
    return VirtualCall<bool(__thiscall *)(void *), 69>(self);
}

} // namespace

void BodyPartSystemIface::RefreshPart(uint32_t a, uint32_t b, uint32_t id)
{
    void *const part = VirtualCall<GetPartSlot, 52>(this, a, b, id);
    if (part != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 1>(part);
    if (part != nullptr) {
        IdVector ids;
        VirtualCall<IdsSlot, 56>(part, &ids);
        for (const uint32_t *x = ids.data(), *end = ids.data() + ids.size(); x != end; ++x) {
            // The part's own entries are not tested for null.
            if (*x == 0 || ShowsAllIds(this)) VirtualCall<UpdateSlot, 34>(VirtualCall<EntrySlot, 48>(part, *x));
        }
    }
    ServiceRefVector linked{};
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, ServiceRefVector *, IdVector *, GuidVector *), 53>(
        this, a, b, id, &linked, nullptr, nullptr);
    for (void **object = linked.mFirst; object != linked.mLast; ++object) {
        IdVector ids;
        VirtualCall<IdsSlot, 56>(*object, &ids);
        for (const uint32_t *x = ids.data(), *end = ids.data() + ids.size(); x != end; ++x) {
            if (*x != 0 && !ShowsAllIds(this)) continue;
            void *const entry = VirtualCall<EntrySlot, 48>(*object, *x);
            if (entry != nullptr) VirtualCall<UpdateSlot, 34>(entry);
        }
    }
    linked.ReleaseAndFree();
    if (part != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 2>(part);
}

namespace {

// One link of a part's entry (the vector at +0x68 of what 0xcf04b0 returns).
struct PartLink {
    uint8_t mPad0[8];
    uint8_t mEnabled; // +0x08
    uint8_t mPad9[3];
    void *mObject;    // +0x0c  counted
};
static_assert(offsetof(PartLink, mObject) == 0xc, "PartLink layout");

struct PartEntry {
    uint8_t mPad0[0x68];
    PartLink **mLinksFirst; // +0x68
    PartLink **mLinksLast;  // +0x6c
};

} // namespace

void BodyPartSystemIface::CollectLinkedParts(uint32_t a, uint32_t b, uint32_t id, ServiceRefVector *objects, IdVector *values,
                                             GuidVector *guids)
{
    void *const part = VirtualCall<GetPartSlot, 52>(this, a, b, id);
    if (part == nullptr) return;
    IdVector ids;
    VirtualCall<IdsSlot, 56>(part, &ids);
    if (ids.empty()) return;
    const PartEntry *const entry = OriginalFunction<const PartEntry *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf04b0>()(
        SystemOf(this), a, b, id, ids[0]);
    for (PartLink *const *at = entry->mLinksFirst; at != entry->mLinksLast; ++at) {
        PartLink *const link = *at;
        if (link == nullptr || !link->mEnabled || link->mObject == nullptr) continue;
        // A counted null appended, then the object assigned into it (as the original's push_back of an empty
        // reference and the assignment after it).
        objects->PushBack(nullptr);
        void **const slot = objects->mLast - 1;
        void *const previous = *slot;
        if (link->mObject != previous) {
            VirtualCall<uint32_t(__thiscall *)(void *), 1>(link->mObject);
            *slot = link->mObject;
            if (previous != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 2>(previous);
        }
        if (values != nullptr) values->push_back(OriginalFunction<uint32_t(__thiscall *)(PartLink *), 0xcfeba0>()(link));
        if (guids != nullptr) {
            const CasGuid *guid;
            if (OriginalFunction<int32_t(__thiscall *)(PartLink *), 0xd025f0>()(link) == 8) {
                guid = OriginalFunction<const CasGuid *(__thiscall *)(PartLink *), 0xcff0c0>()(link);
            } else {
                guid = OriginalFunction<const CasGuid *(__cdecl *)(), 0x4f8e50>()();
            }
            guids->push_back(*guid);
        }
    }
}

} // namespace nTSCASCore
