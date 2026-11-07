// nTSSG::cTSSGSystem: the event behaviour type's slots 12-14 (lane JD). See src/sg/cTSSGSystem.h.
//
// cTSSGSystem's primary base is nGZEvent::cAbstractBehaviorTypeT<cITSSGSystem, cRefMultiThreaded>, whose slots 12-14
// read the type's registry of attribute ids: a function-local static std::map from id to a 24-byte entry (0x253de0,
// which with a non-zero id first stores the entry). The base's own vtable (0xdae0fc, constructor and destructor only)
// shares these three bodies.
#include "sg/SGEventRecords.h"
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cAbstractBehaviorTypeMT.h"
#include "gz/cAttributeIDIterator.h"

#include <cstring>
#include <map>
#include <new>
#include <optional>
#include <vector>

namespace {

// A registry entry (24 bytes; slot 14 copies it out whole).
using AttributeEntry = nGZEvent::cEventAttribute;
static_assert(sizeof(AttributeEntry) == 24);
using AttributeMap = nGZEvent::cEventAttributeMap;

// The entry slot 14 gives for an id the registry does not have.
constexpr uint32_t kNoEntry = 0xff7848;

std::optional<AttributeMap> gTSSGRegistry;

// The registry (0x253de0): with id 0 the entry is not stored. Every reader passes an empty entry.
AttributeMap &Registry()
{
    const AttributeEntry probe = nGZEvent::behavior_mt::DefaultAttribute();
    return *nTSSG::cTSSGSystemBehaviorTraits::RegistryFor(0, &probe);
}

// The game's reference-counted id vector (cRZRefCountedT<std::vector<unsigned long>>, vtable 0xd9bcfc): slot 1 AddRef,
// slot 2 Release (deletes at zero), the vector at +4, the count at +0x10.
constexpr uint32_t kIdVectorVTable = 0xd9bcfc;
struct IdVector {
    const void *vtable;
    std::vector<uint32_t> ids;
    uint32_t refs;
};
static_assert(sizeof(IdVector) == 0x14);

// nGZEvent::cAttributeIDIterator (vtable 0xd9bd10): the count at +4, the id vector it holds a reference on, the position.
// (the vtable word: the owned static 0xd9bd10, gz/cAttributeIDIterator.h)
struct IdIterator {
    const void *vtable;
    uint32_t refs;
    IdVector *ids;
    const uint32_t *at;
};
static_assert(sizeof(IdIterator) == 0x10);

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

IdIterator *NewIterator(IdVector *ids, const uint32_t *at)
{
    auto *it = static_cast<IdIterator *>(::operator new(sizeof(IdIterator)));
    it->vtable = &ts2_rva_d9bd10;
    it->refs = 0;
    it->ids = ids;
    if (ids) Call<1>(ids);
    it->at = at;
    return it;
}

} // namespace

namespace nTSSG {

// Built on first use. The original runs its holder 0x2455e0 first (on every call, a once byte skipping it after the first):
// the base events, then 0xca7ccbbc (record 0xff7890) and 0x2b5745f8 (record 0xff78a8), each with its last word 4, through
// this function, whose map is made by the first of those calls as a copy of the base registry (0x1485f0). (The holder's
// 4-byte object, kept in the static 0x10cc8b0, is read by nothing.)
nGZEvent::cEventAttributeMap *__cdecl cTSSGSystemBehaviorTraits::RegistryFor(uint32_t id,
                                                                           const nGZEvent::cEventAttribute *attribute)
{
    if (!gTSSGRegistry) {
        nGZEvent::cAbstractBehaviorType::RegisterBaseEvents();
        const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
        gTSSGRegistry.emplace(*nGZEvent::cAbstractBehaviorType::RegistryFor(0, &defaults));
        nGZEvent::cEventAttribute record = sgevents::kCurrentDeviceEvent;
        record.mWord14 = 4;
        (*gTSSGRegistry)[0xca7ccbbc] = record;
        record = sgevents::kCurrentDeviceRenderSceneEvent;
        record.mWord14 = 4;
        (*gTSSGRegistry)[0x2b5745f8] = record;
    }
    if (id != 0) (*gTSSGRegistry)[id] = *attribute;
    return &*gTSSGRegistry;
}

void cTSSGSystemBehaviorTraits::ResetRegistryForTest() { gTSSGRegistry.reset(); }

uint32_t cTSSGSystem::AttributeCount() { return uint32_t(Registry().size()); }

void *cTSSGSystem::AttributeIds(void *out)
{
    auto *ids = static_cast<IdVector *>(::operator new(sizeof(IdVector)));
    ids->vtable = GameAddress<void>(kIdVectorVTable);
    ::new (&ids->ids) std::vector<uint32_t>();
    ids->refs = 0;
    Call<1>(ids);
    for (auto it = Registry().begin(); it != Registry().end(); ++it) ids->ids.push_back(it->first);
    IdIterator *last = NewIterator(ids, ids->ids.data() + ids->ids.size());
    IdIterator *first = NewIterator(ids, ids->ids.data());
    // The result: the first iterator's copy (its slot 3), the first iterator and the last, each with a reference.
    auto *result = static_cast<void **>(out);
    result[0] = Call<3, void *>(first);
    if (result[0]) Call<1>(result[0]);
    result[1] = first;
    Call<1>(first);
    result[2] = last;
    Call<1>(last);
    Call<2>(ids);
    return out;
}

void *cTSSGSystem::AttributeInfo(void *out, uint32_t id)
{
    AttributeMap &map = Registry();
    const auto found = map.find(id);
    const void *entry = found == Registry().end() ? GameAddress<const void>(kNoEntry) : &found->second;
    std::memcpy(out, entry, sizeof(AttributeEntry));
    return out;
}

} // namespace nTSSG
