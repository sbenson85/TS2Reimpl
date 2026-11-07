// nTSCASCore::cTSCASBodyPartSystem (lane JY): adding morphs to the modifier stack (slots 48, 49), importing per-part
// data from another object (slot 5) and the face-0 resource cache (face-0 slot 14).
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "rz/ServiceGetters.h"
#include "rz/StringKeyParser.h"

#include <cstring>
#include <string>
#include <vector>

namespace nTSCASCore {

namespace {

cTSCASBodyPartSystem *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<cTSCASBodyPartSystem *>(reinterpret_cast<uint8_t *>(iface) - 0x1c);
}

// Appends a copy of the record to the stack (spare capacity: the copy constructor, else the growing append), then
// destroys the temporary.
void AppendAndDestroy(BodyPartEntryVector *stack, BodyPartEntry &entry)
{
    BodyPartEntry *const last = stack->mLast;
    if (last == stack->mEnd) {
        OriginalFunction<void(__thiscall *)(BodyPartEntryVector *, BodyPartEntry *, const BodyPartEntry *), 0xcda310>()(stack, last, &entry);
    } else {
        OriginalFunction<void(__thiscall *)(BodyPartEntry *, const BodyPartEntry *), 0xcddbc0>()(last, &entry);
        stack->mLast = stack->mLast + 1;
    }
    OriginalFunction<void(__thiscall *)(BodyPartEntry *), 0xcdf820>()(&entry);
}

int32_t RecordCount(const BodyPartEntryVector *stack)
{
    return static_cast<int32_t>(stack->mLast - stack->mFirst);
}

// The mesh entries 0xcf06e0 collects (ret 4: appends to the vector).
using MeshEntryVector = std::vector<PartMeshEntry *, CasAlloc<PartMeshEntry *>>;
void CollectMeshEntries(cTSCASBodyPartSystem *system, MeshEntryVector *out)
{
    OriginalFunction<void(__thiscall *)(void *, MeshEntryVector *), 0xcf06e0>()(system, out);
}

const char *NameOf(const BodyPartEntry *e)
{
    return e->Name();
}

} // namespace

// ---- Slot 48: a morph by name -----------------------------------------------------------------------------------------

void BodyPartSystemIface::AddMorph(const char *name, float weight, uint8_t merge)
{
    BodyPartEntryVector *stack = mStack;
    if (stack->mFirst != stack->mLast && merge != 0) {
        BodyPartEntry *const top = stack->mLast - 1;
        if (top->mKind == 1 && _stricmp(NameOf(top), name) == 0) {
            float w = weight + top->mWeight;
            top->mWeight = w;
            if (w > 1.0f) {
                top->mWeight = 1.0f;
                w = 1.0f;
            }
            if (0.0f > w) top->mWeight = 0.0f;
            MeshEntryVector entries;
            cTSCASBodyPartSystem *const system = SystemOf(this);
            CollectMeshEntries(system, &entries);
            for (PartMeshEntry *e : entries) {
                if (e->mStackIndex != RecordCount(mStack) || e->mEnabled == 0) continue;
                if (e->mKind == mLastKind && e->mKey == mLastKey) {
                    for (PartMeshGroup *g = e->mGroupsFirst; g != e->mGroupsLast; ++g)
                        for (PartMeshVertex *v = g->mFirst; v != g->mLast; ++v) std::memcpy(v->mPosition, v->mBase, 12);
                    OriginalFunction<void(__thiscall *)(void *, BodyPartEntry *, PartMeshEntry *), 0xce18c0>()(system, top, e);
                } else {
                    e->mStackIndex = -1;
                }
            }
            return;
        }
    }
    BodyPartEntry e;
    OriginalFunction<void(__thiscall *)(BodyPartEntry *, const char *, int32_t, int32_t, float), 0xcddc80>()(&e, name, 1, 0, weight);
    AppendAndDestroy(mStack, e);
}

// ---- Slot 49: a named weight, relative -------------------------------------------------------------------------------

void BodyPartSystemIface::AddNamedWeight(const char *name, float weight)
{
    if (name == nullptr) return;
    BodyPartEntryVector *stack = mStack;
    if (stack->mFirst != stack->mLast) {
        BodyPartEntry *const top = stack->mLast - 1;
        if (top->mKind == 7 && _stricmp(NameOf(top), name) == 0) {
            const float weightBefore = top->mWeight;
            const float from = weightBefore + top->mExtra;
            const float to = from + weight;
            top->mWeight = weightBefore + weight;
            MeshEntryVector entries;
            cTSCASBodyPartSystem *const system = SystemOf(this);
            CollectMeshEntries(system, &entries);
            for (PartMeshEntry *e : entries) {
                if (e->mStackIndex != RecordCount(mStack)) continue;
                if (e->mKind == mLastKind && e->mKey == mLastKey) {
                    OriginalFunction<void(__thiscall *)(void *, BodyPartEntry *, PartMeshEntry *, float, float), 0xce1550>()(system, top, e, from, to);
                } else {
                    e->mStackIndex = -1;
                }
            }
            return;
        }
    }
    const float extra = VirtualCall<float(__thiscall *)(void *, const char *, uint32_t), 50>(this, name, 0);
    BodyPartEntry e;
    OriginalFunction<void(__thiscall *)(BodyPartEntry *, const char *, int32_t, int32_t, float, float), 0xcddd40>()(&e, name, 7, 0, weight, extra);
    AppendAndDestroy(mStack, e);
}

// ---- Slot 5: per-part data from another object ------------------------------------------------------------------------

void BodyPartSystemIface::ImportPartData(void *source, const char *name)
{
    if (source == nullptr || name == nullptr) return;
    const std::string unused(name); // the original builds (and destroys) a copy of the name it never reads
    const IdVector *ids = VirtualCall<const IdVector *(__thiscall *)(void *), 14>(source);
    for (const uint32_t *id = ids->data(); id != ids->data() + ids->size(); ++id) {
        uint32_t kind, key, c, d;
        VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t *, uint32_t *, uint32_t *, uint32_t *), 16>(source, *id, &kind, &key, &c, &d);
        const PartPoints *points = VirtualCall<const PartPoints *(__thiscall *)(void *, uint32_t), 13>(source, *id);
        if (points == nullptr) continue;
        if (kind == 7) kind = 3;
        uint8_t *const entry = OriginalFunction<uint8_t *(__thiscall *)(void *, const char *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 0xcec500>()(
            SystemOf(this), name, kind, d == 0 ? key : 0, d, c, 0, 1);
        PartPoints *const dest = reinterpret_cast<PartPoints *>(entry + 0x40);
        OriginalFunction<void(__thiscall *)(PartPoints *, int32_t), 0xcfd180>()(dest, static_cast<int32_t>(points->mLast - points->mFirst));
        PartPoint *out = dest->mFirst;
        for (const PartPoint *p = points->mFirst; p != points->mLast; ++p, ++out) *out = *p;
    }
}

// ---- Face-0 slot 14: the resource cache -------------------------------------------------------------------------------

void *cTSCASBodyPartSystem::GetPartResource(uint32_t kind, uint32_t key, uint32_t flag)
{
    if (kind == 7) kind = 3;
    const uint32_t partKey = flag == 0 ? key : 0;
    const uint32_t mapKey = ((((flag & 0xff) << 7) | (kind & 0x7f)) * 2) | (partKey & 1);
    ResourceRef &ref = (*reinterpret_cast<ResourceMap *>(static_cast<TreeHeader *>(&mResources)))[mapKey];
    if (ref.mObject == nullptr) {
        LocalRZString name;
        if (OriginalFunction<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, cRZString *), 0xcf18b0>()(this, kind, partKey, flag, &name.mText)) {
            name.mText.mString.append("_cres", 5);
            StringKeyParser::Result resourceKey = {};
            StringKeyParser::ParseKey(&resourceKey, VirtualCall<const char *(__thiscall *)(void *), 5>(&name.mText), 0xe519c933, nullptr);
            void *const persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
            if (void *const old = ref.mObject) {
                ref.mObject = nullptr;
                VirtualCall<uint32_t(__thiscall *)(void *), 2>(old);
            }
            VirtualCall<bool(__thiscall *)(void *, const StringKeyParser::Result *, uint32_t, void **, uint32_t, uint32_t), 5>(
                persist, &resourceKey, 0x4512dd13, &ref.mObject, 0, 0);
        }
    }
    return ref.mObject;
}

} // namespace nTSCASCore
