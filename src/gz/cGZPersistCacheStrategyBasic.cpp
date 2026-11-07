#include "gz/cGZPersistCacheStrategyBasic.h"
#include "game/vtable.h"
#include "gz/cGZPersistResourceKeyFilterByType.h"

#include <new>

namespace {

using Key = cGZPersistCacheStrategyBasic::Key;

// The resource's slots used here: 1 AddRef, 2 Release, 3 its key (into the buffer), 4 set its key, 11 its use count.
void AddRefObject(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
}

void ReleaseObject(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
}

uint32_t UseCount(void *resource)
{
    return VirtualCall<uint32_t(__thiscall *)(void *), 11>(resource);
}

// A key filter's slot 3: whether it accepts the key (al).
bool Accepts(void *filter, const Key *key)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, const Key *), 3>(filter, key) != 0;
}

} // namespace

cGZPersistCacheStrategyBasic::cGZPersistCacheStrategyBasic() noexcept
    : mMap(), mAccesses(0), mAutoPurge(0), mFilter(nullptr), mRefCount(0)
{
}

__declspec(noinline) cGZPersistCacheStrategyBasic *__fastcall cGZPersistCacheStrategyBasic::Construct(void *memory, void *)
{
    return new (memory) cGZPersistCacheStrategyBasic();
}

bool cGZPersistCacheStrategyBasic::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != 0x6bb923e4) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGZPersistCacheStrategyBasic::AddRef()
{
    // ORIGINAL-BUG: answers the count before the increment (the game's `mov eax, [ecx+30h]; lea edx, [eax+1]`).
    return mRefCount++;
}

uint32_t cGZPersistCacheStrategyBasic::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this; // the deleting destructor through the vtable, flags 1
    return 0;
}

void cGZPersistCacheStrategyBasic::vf3()
{
}

void cGZPersistCacheStrategyBasic::Shutdown()
{
    Clear(); // slot 18 through the vtable
    for (auto &entry : mMap)
        if (entry.second) ReleaseObject(entry.second);
    mMap.clear();
}

void cGZPersistCacheStrategyBasic::SetKeyFilter(cIGZUnknown *filter)
{
    cIGZUnknown *old = mFilter;
    if (filter == old) return;
    if (filter) filter->AddRef();
    mFilter = filter;
    if (old) old->Release();
}

cIGZUnknown *cGZPersistCacheStrategyBasic::GetKeyFilter()
{
    return mFilter;
}

bool cGZPersistCacheStrategyBasic::Get(const Key *key, void **out)
{
    const auto found = mMap.find(*key);
    if (found == mMap.end()) return false;
    *out = found->second;
    AddRefObject(found->second);
    return true;
}

bool cGZPersistCacheStrategyBasic::Add(const Key *key, cIGZUnknown *resource)
{
    if (mMap.find(*key) != mMap.end()) return false;
    resource->AddRef();
    VirtualCall<void(__thiscall *)(void *, const Key *), 4>(resource, key);
    mMap.emplace(*key, resource); // the key is read after the resource has been given it, as the game's
    return true;
}

bool cGZPersistCacheStrategyBasic::Remove(const Key *key)
{
    const auto found = mMap.find(*key);
    if (found == mMap.end()) return false;
    ReleaseObject(found->second);
    mMap.erase(found);
    return true;
}

bool cGZPersistCacheStrategyBasic::Contains(const Key *key)
{
    return mMap.find(*key) != mMap.end();
}

bool cGZPersistCacheStrategyBasic::IsKeyAccepted(const Key *key)
{
    return mFilter == nullptr || Accepts(mFilter, key);
}

uint32_t cGZPersistCacheStrategyBasic::GetKeys(KeyList *keys, cIGZUnknown *filter)
{
    uint32_t count = 0;
    for (const auto &entry : mMap) {
        Key key;
        VirtualCall<const Key *(__thiscall *)(void *, Key *), 3>(entry.second, &key);
        if (mFilter != nullptr && !Accepts(mFilter, &key)) continue;
        if (filter != nullptr && !Accepts(filter, &key)) continue;
        if (keys != nullptr) keys->push_back(key);
        ++count;
    }
    return count;
}

uint32_t cGZPersistCacheStrategyBasic::GetKeysOfType(KeyList *keys, uint32_t type)
{
    auto *filter = new cGZPersistResourceKeyFilterByType(type);
    filter->AddRef();
    const uint32_t count = GetKeys(keys, filter); // slot 12 through the vtable
    filter->Release();
    return count;
}

bool cGZPersistCacheStrategyBasic::OnAccess(uint32_t /*unused*/)
{
    if (mAutoPurge != 0 && ++mAccesses > 0x80) {
        mAccesses = 0;
        PurgeUnused(); // slot 20 through the vtable
    }
    return true;
}

uint8_t cGZPersistCacheStrategyBasic::GetAutoPurge()
{
    return mAutoPurge;
}

void cGZPersistCacheStrategyBasic::SetAutoPurge(uint8_t on)
{
    mAutoPurge = on;
}

bool cGZPersistCacheStrategyBasic::AdvanceGeneration()
{
    uint32_t passes = 0;
    while (PurgeUnused()) { // slot 20 through the vtable
        if (++passes >= 8) break;
    }
    return passes != 0;
}

void cGZPersistCacheStrategyBasic::Clear()
{
    for (auto &entry : mMap)
        if (entry.second) ReleaseObject(entry.second);
    mMap.clear();
}

cGZPersistCacheStrategyBasic::~cGZPersistCacheStrategyBasic()
{
    if (mFilter) mFilter->Release();
    // mMap's destructor frees the nodes and buckets; the resources are not released.
}

bool cGZPersistCacheStrategyBasic::PurgeUnused()
{
    bool released = false;
    for (auto it = mMap.begin(); it != mMap.end();) {
        void *resource = it->second;
        if (resource != nullptr) {
            if (UseCount(resource) >= 2) {
                ++it;
                continue;
            }
            released = true;
            ReleaseObject(resource);
            it->second = nullptr;
        }
        it = mMap.erase(it);
    }
    return released;
}
