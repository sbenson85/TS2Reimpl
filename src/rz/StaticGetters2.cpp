#include "rz/StaticGetters2.h"

#include "game/game.h"
#include "game/vtable.h"

namespace {

// Identifiers of the service's slot 5 for each holder. The original pushes the output pointer
// first, then the second identifier, so the last push is the first argument.
constexpr uint32_t kIdsBFirst = 0xe92db716;
constexpr uint32_t kIdsBSecond = 0x292db38c;
constexpr uint32_t kIdsCFirst = 0xdae3783f;
constexpr uint32_t kIdsCSecond = 0xdae3783e;
constexpr uint32_t kIdsObjFirst = 0xe5074b6b;
constexpr uint32_t kIdsObjSecond = 0x25074b4d;

constexpr uint32_t kCellEmpty = 0xffffffff;

using ServiceQuery = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
using ObjectCall2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using ObjectCall1 = uint32_t(__thiscall *)(void *, uint32_t);
using ObjectCall0 = void *(__thiscall *)(void *);

// The service's slot 5 is the query of GetCachedSingleton (0x20dc91, 0x20dcb6, 0x20dd00 and
// 0xaeb820 all use it): the holder receives the object through the output pointer.
void QueryService(ScopedRef *holder, uint32_t first, uint32_t second)
{
    holder->mObject = nullptr;
    void *service = GetServiceObject();
    if (service) VirtualCall<ServiceQuery, 5>(service, first, second, &holder->mObject);
}

// The cache of a ScopedRef-filled singleton (0x20de51, 0x20df6f): acquire, store, release.
void *CachedAcquired(void **cache, uint32_t first, uint32_t second)
{
    if (*cache == nullptr) {
        ScopedRef ref{nullptr};
        QueryService(&ref, first, second);
        *cache = ref.mObject;
        ref.ReleaseObject();
    }
    return *cache;
}

// Releases the reference when it goes out of scope. The original's frame for 0xaeb820 has one
// unwind action, ScopedRef::ReleaseObject on the holder (0x1cbd79), registered from the call to
// slot 8 on: the same release runs on the normal path after the store, and during unwinding.
struct ReleaseHolder {
    ScopedRef *holder;
    ~ReleaseHolder() { holder->ReleaseObject(); }
};

} // namespace

ScopedRef *__fastcall AcquireServiceRefB(ScopedRef *self)
{
    QueryService(self, kIdsBFirst, kIdsBSecond);
    return self;
}

ScopedRef *__fastcall AcquireServiceRefC(ScopedRef *self)
{
    QueryService(self, kIdsCFirst, kIdsCSecond);
    return self;
}

void *GetCachedSingletonB()
{
    return CachedAcquired(GameAddress<void *>(0x10cc28c), kIdsBFirst, kIdsBSecond);
}

void *GetCachedSingletonC()
{
    return CachedAcquired(GameAddress<void *>(0x10cc290), kIdsCFirst, kIdsCSecond);
}

void *GetCachedSlot23()
{
    void **cache = GameAddress<void *>(0x10cc2a8);
    if (*cache == nullptr) {
        void *object = GetCachedSingleton();
        *cache = VirtualCall<ObjectCall0, 23>(object);
    }
    return *cache;
}

void *GetCachedObjectB()
{
    void **cache = &ts2_rva_10f9070;
    if (*cache == nullptr) {
        ScopedRef ref{nullptr};
        // The original's slot-5 call comes before the frame's first state (no release on a throw
        // from it). A null service would crash here as in the original: it is never null in play.
        QueryService(&ref, kIdsObjFirst, kIdsObjSecond);
        ReleaseHolder release{&ref};
        void *object = ref.mObject; // receiver: nGZSceneGraph::cISceneGraphSystem (service 0xe5074b6b)
        *cache = VirtualCall<ObjectCall0, 8>(object);
    }
    return *cache;
}

uint32_t __fastcall Dispatch8(void *self)
{
    void *object = GetCachedObjectB();
    return VirtualCall<ObjectCall1, 8>(object, *static_cast<uint32_t *>(self));
}

uint32_t __fastcall Dispatch8Adjusted(void *self)
{
    return Dispatch8(static_cast<char *>(self) - 0x10);
}

ServiceCell *ServiceCell::ConstructFrom(uint32_t value)
{
    mValue = value;
    VirtualCall<ObjectCall1, 6>(GetCachedObjectB(), mValue);
    return this;
}

ServiceCell *ServiceCell::ConstructCopy(const ServiceCell *src)
{
    mValue = src->mValue;
    VirtualCall<ObjectCall1, 6>(GetCachedObjectB(), mValue);
    return this;
}

ServiceCell *ServiceCell::Open(uint32_t a, uint32_t b)
{
    void *object = GetCachedObjectB();
    mValue = VirtualCall<ObjectCall2, 5>(object, a, b);
    object = GetCachedObjectB();
    VirtualCall<ObjectCall1, 6>(object, mValue);
    return this;
}

ServiceCell *ServiceCell::Assign(const uint32_t *src)
{
    void *object = GetCachedObjectB();
    VirtualCall<ObjectCall1, 6>(object, *src);
    object = GetCachedObjectB();
    VirtualCall<ObjectCall1, 7>(object, mValue);
    mValue = *src;
    return this;
}

void ServiceCell::Reset()
{
    if (mValue != kCellEmpty) {
        void *object = GetCachedObjectB();
        VirtualCall<ObjectCall1, 7>(object, mValue);
        mValue = kCellEmpty;
    }
}

void ServiceCell::Close() noexcept
{
    Reset();
}
