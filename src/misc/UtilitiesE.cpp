#include "misc/UtilitiesE.h"

#include "game/game.h"
#include "game/owned.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZRefCount.h"

TS2_OWNED_STATIC(void *, 10f8fcc); // cache of 0xae8900 (zero until the first call)
TS2_OWNED_STATIC(void *, 10f8fd0); // cache of 0xae8980 (zero until the first call)

namespace {

constexpr uint32_t kTSLuaWrapperVTable = 0xe88584; // nTSLua::cTSLuaWrapper, primary
constexpr uint32_t kCachedFirstId = 0xe5074b6b; // first parameter of the query (pushed last)
constexpr uint32_t kCachedSecondId = 0x25074b4d;

using QueryFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
using Slot5Fn = void *(__thiscall *)(void *);
using Slot11Fn = void *(__thiscall *)(void *);
using Slot6Fn = void *(__thiscall *)(void *);
using Slot13Fn = void(__thiscall *)(void *, void *, const void *);

} // namespace

void TSLuaWrapperBase::Destruct()
{
    mVTable = GameAddress(kTSLuaWrapperVTable);
    reinterpret_cast<cRZUnknown *>(this)->Destruct();
}

void *GetCachedQueriedObject()
{
    void **cache = &ts2_rva_10f8fcc;
    if (*cache != nullptr) return *cache;

    ScopedRef holder{nullptr};
    void *service = GetServiceObject();
    if (service) {
        // The query takes its first identifier as the first parameter: the original pushes the
        // holder, then the second identifier, then the first.
        // ORIGINAL-BUG: when the query or slot 5 throws, the holder is not released (its reference
        // leaks). The frame has an unwind state, but it does not call ScopedRef::ReleaseObject.
        VirtualCall<QueryFn, 5>(service, kCachedFirstId, kCachedSecondId, &holder.mObject);
    }
    // With no service the holder stays null and the call below faults, as the original does.
    *cache = VirtualCall<Slot5Fn, 5>(holder.mObject);
    holder.ReleaseObject();
    return *cache;
}

void *GetMaterialParserCached()
{
    void **cache = &ts2_rva_10f8fd0;
    if (*cache != nullptr) return *cache;

    ScopedRef holder{nullptr};
    void *service = GetServiceObject();
    if (service) {
        // ORIGINAL-BUG: when the query or slot 11 throws, the holder is not released.
        VirtualCall<QueryFn, 5>(service, kCachedFirstId, kCachedSecondId, &holder.mObject);
    }
    // With no service the holder stays null and the call below faults, as the original does.
    *cache = VirtualCall<Slot11Fn, 11>(holder.mObject);
    holder.ReleaseObject();
    return *cache;
}

bool NotifySlot6ResultWithKey(void *object, const void *key)
{
    if (object == nullptr) return false;
    void *target = VirtualCall<Slot6Fn, 6>(object);
    if (target == nullptr) return false;
    VirtualCall<Slot13Fn, 13>(target, object, key);
    return true;
}

bool NotifySlot6ResultWithKeyWords(void *object, uint32_t k0, uint32_t k1, uint32_t k2, uint32_t k3)
{
    const uint32_t key[4] = {k0, k1, k2, k3};
    return NotifySlot6ResultWithKey(object, key);
}
