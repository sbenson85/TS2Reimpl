#include "misc/UtilitiesC.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/MiscHelpersB.h"
#include "rz/cRZRefCount.h" // ts2_rva_da2fa8, the owned vtable
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"

namespace {

// Slot 5 of the service object: (this, first, second, output).
using QueryFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);

// Vtable slot 5 of the walked object: (this, arg, key).
using WalkSlot5Fn = void(__thiscall *)(void *, uint32_t, uint32_t);

// Clears *out, then asks the service object (GetServiceObject, game global 0x10c9378) for one
// through slot 5 with the two identifiers, the output in *out. Nothing else happens when there is no
// service object. Same as the query in ServiceHolder::QueryFromService (0x1c02bf).
void QueryService(void **out, uint32_t first, uint32_t second)
{
    *out = nullptr;
    void *service = GetServiceObject();
    if (service) VirtualCall<QueryFn, 5>(service, first, second, out);
}

// Cache globals of the two cached getters.
constexpr uint32_t kCacheGlobalC = 0x10cc648;
constexpr uint32_t kCacheGlobalD = 0x10f51ac;

// Identifiers of the queries (the first parameter is the last one pushed).
constexpr uint32_t kQueryAFirst = 0xa417445e;
constexpr uint32_t kQueryASecond = 0x5a4;
constexpr uint32_t kQueryBFirst = 0xcba95d2d;
constexpr uint32_t kQueryBSecond = 0x4ba8123a;

// The cached getter shape: the global, when null, takes the object from the query into a local
// reference, stores it and releases the local (ScopedRef::ReleaseObject, 0x1cbd79). Returns the
// global re-read after the release.
void *CachedGetter(uint32_t cacheRva, uint32_t first, uint32_t second)
{
    void **cache = GameAddress<void *>(cacheRva);
    if (*cache == nullptr) {
        ScopedRef held{nullptr};
        QueryService(&held.mObject, first, second);
        *cache = held.mObject;
        held.ReleaseObject();
    }
    return *cache;
}

} // namespace

UtilObjectA *UtilObjectA::Construct()
{
    mVTable = const_cast<void *>(static_cast<const void *>(&ts2_rva_da2fa8));
    mValue = 0;
    return this;
}

UtilObjectB *UtilObjectB::Construct(uint32_t arg)
{
    mVTable = GameAddress<void>(0xe6180c);
    mArg = arg;
    mZero8 = 0;
    mWordC = 0;
    mWordE = 6;
    mZero10 = 0;
    return this;
}

ServiceQueryHolder *ServiceQueryHolder::QueryWithIdsA()
{
    QueryService(&mObject, kQueryAFirst, kQueryASecond);
    return this;
}

ServiceQueryHolder *ServiceQueryHolder::QueryWithIdsB()
{
    QueryService(&mObject, kQueryBFirst, kQueryBSecond);
    return this;
}

void KeyMapSlot5Walker::CallSlot5ForEachKey(uint32_t arg, void *obj)
{
    if (obj == nullptr) {
        // The holder is a temporary, released right after the query through 0x1466e0.
        ServiceHolder holder{nullptr};
        holder.QueryFromService();
        obj = holder.mObject;
        ReleaseHeldObject(holder.mObject);
    }
    // ORIGINAL-BUG: there is no null check after the query. With no service object and a non-empty
    // map, the first call below goes through a null object (access violation). Reproduced as is.
    for (auto it = mMap.begin(); it != mMap.end(); ++it)
        VirtualCall<WalkSlot5Fn, 5>(obj, arg, it->first);
}

void *GetCachedServiceObjectC()
{
    return CachedGetter(kCacheGlobalC, kQueryBFirst, kQueryBSecond);
}

void *GetCachedServiceObjectD()
{
    return CachedGetter(kCacheGlobalD, kQueryAFirst, kQueryASecond);
}

void AffineTransform3::TransformPoint(float *out, const float *in)
{
    // The three inputs are read before out is written, so out may alias in.
    const float x = in[0], y = in[1], z = in[2];
    for (int i = 0; i < 3; ++i) {
        const float *row = mMatrix + 3 * i;
        float v = y * row[1];
        v = v + x * row[0];
        v = v + z * row[2];
        out[i] = v + mOffset[i];
    }
}
