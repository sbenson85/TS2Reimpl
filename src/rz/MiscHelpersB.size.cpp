#include "rz/MiscHelpersB.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"

namespace {

// Identifiers for 0x1c02bf: the first parameter is the last one pushed (0x4fa845b).
constexpr uint32_t kHolderFirstId = 0x04fa845b;
constexpr uint32_t kHolderSecondId = 0x652294c7;

// Identifiers for 0x9c5baa (0x20dbb3 pushes 0x22c2eb1f first, so 0xc2c2eb0f is the first parameter).
constexpr uint32_t kCachedFirstId = 0xc2c2eb0f;
constexpr uint32_t kCachedSecondId = 0x22c2eb1f;

// The cache global of 0x9c5baa.
constexpr uint32_t kCachedGlobalRva = 0x10f51b0;

using QueryFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
using Slot6Fn = void(__thiscall *)(void *, uint32_t, uint32_t);

// Asks the service object for an object through slot 5, with the output in *out. *out is cleared
// first (the original clears its holder before asking; when there is no service it stays null).
void QueryService(void **out, uint32_t first, uint32_t second)
{
    *out = nullptr;
    void *service = GetServiceObject();
    if (service) VirtualCall<QueryFn, 5>(service, first, second, out);
}

} // namespace

ServiceHolder *ServiceHolder::QueryFromService()
{
    QueryService(&mObject, kHolderFirstId, kHolderSecondId);
    return this;
}

void KeyMapWalker::CallSlot6ForEachKey(uint32_t arg, void *obj)
{
    if (arg == 0) return;
    if (obj == nullptr) {
        // The holder is a temporary released right after the query, as the original does through
        // 0x1466e0 (try/catch(...), C++ exceptions swallowed).
        ServiceHolder holder{nullptr};
        holder.QueryFromService();
        obj = holder.mObject;
        ReleaseHeldObject(holder.mObject);
        if (obj == nullptr) return;
    }
    // The iteration is the library's own (_Tree_unchecked_const_iterator::operator++), so the walk
    // is the same as the original's: begin() is the head's left link, end() is the head.
    for (auto it = mMap.begin(); it != mMap.end(); ++it)
        VirtualCall<Slot6Fn, 6>(obj, arg, it->first);
}

void *GetCachedServiceObjectB()
{
    void **cache = GameAddress<void *>(kCachedGlobalRva);
    if (*cache == nullptr) {
        ScopedRef held{nullptr};
        QueryService(&held.mObject, kCachedFirstId, kCachedSecondId);
        *cache = held.mObject;
        held.ReleaseObject();
    }
    return *cache;
}
