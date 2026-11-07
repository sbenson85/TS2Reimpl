#include "ts/cTSPersistContext.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZRefCount.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/ServiceGetters.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace {

template <typename T> T &At(const void *base, intptr_t offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(base) + offset);
}

} // namespace

bool cTSPersistContext::vf9(const uint32_t *group)
{
    bool removed = false;
    Index &index = Map();
    for (auto it = index.begin(); it != index.end();) {
        if (it->first.mWords[1] == *group) {
            it = index.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

// ---- The resource index (lane NN) ---------------------------------------------------------------------------

namespace {

using Key = PersistKey;
using Entry = cTSPersistContext::Entry;

constexpr uint32_t kSelfIid = 0x105231cf;
constexpr uint32_t kStateIid = 0x456b8543;
constexpr uint32_t kLoadIid = 0x456b8f1d;

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using NoArgRet8 = uint8_t(__thiscall *)(void *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);

// The resource manager: the cached service at 0x10faf6c (0xb390c0).
void *Manager()
{
    return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
}

bool StateDirty(void *state)
{
    return VirtualCall<NoArgRet8, 14>(state) != 0;
}

} // namespace

cTSPersistContext::Counted::~Counted() noexcept(false)
{
    if (mObject != nullptr) VirtualCall<NoArgRet32, 2>(mObject);
}

void cTSPersistContext::Counted::Hold(void *object)
{
    mObject = object;
    VirtualCall<NoArgRet32, 1>(object);
}

bool cTSPersistContext::QueryInterface(uint32_t iid, void **out)
{
    if (out == nullptr) return false;
    if (iid != kSelfIid)
        return reinterpret_cast<RZUnknownQueryInterface *>(reinterpret_cast<uintptr_t>(this) + 4)->QueryInterfaceUnknown(iid, out);
    *out = this;
    ++At<uint32_t>(this, 8); // the cRZUnknown base's count (folded AddRef 0x148fb0 on +4)
    return true;
}

uint8_t cTSPersistContext::GetResource(const Key *key, uint32_t iid, void **out)
{
    if (VirtualCall<uint8_t(__thiscall *)(void *, const Key *, uint32_t, void **), 7>(this, key, iid, out) != 0) return 1;
    Counted loaded;
    void *manager = Manager();
    uint8_t found = 0;
    if (VirtualCall<bool(__thiscall *)(void *, const Key *, uint32_t, void **, uint32_t, uint32_t), 5>(
            manager, key, kLoadIid, loaded.Out(), 0, 0)) {
        found = VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, void **), 0>(loaded.Get(), iid, out);
        if (found != 0)
            VirtualCall<bool(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t), 4>(this, loaded.Get(), 0, 0, 0xffffffffu);
    }
    return found;
}

bool cTSPersistContext::AddResource(void *resource, uint8_t dirty, uint32_t priority, uint32_t mask)
{
    if (resource == nullptr) return false;
    Key key;
    VirtualCall<void *(__thiscall *)(void *, Key *), 3>(resource, &key);
    if (VirtualCall<bool(__thiscall *)(void *, Key *), 8>(this, &key)) return false;
    Entry &entry = Map().try_emplace(key).first->second;
    entry.mDirty = dirty;
    entry.mPriority = priority;
    entry.mMask = mask;
    // The entry is new (the key was not indexed), so both references are empty: the original's assignment of the
    // resource and reset of the state face release nothing.
    entry.mResource.Hold(resource);
    if (VirtualCall<QueryFn, 0>(resource, kStateIid, entry.mState.Out()))
        entry.mDirty |= VirtualCall<NoArgRet8, 14>(entry.mState.Get());
    return true;
}

bool cTSPersistContext::RemoveResource(const Key *key)
{
    Index &index = Map();
    const auto found = index.find(*key);
    if (found == index.end()) return false;
    index.erase(found);
    return true;
}

uint8_t cTSPersistContext::DeleteResource(const Key *key)
{
    const uint8_t removed = VirtualCall<uint8_t(__thiscall *)(void *, const Key *), 5>(this, key);
    const uint8_t deleted = VirtualCall<uint8_t(__thiscall *)(void *, const Key *), 17>(Manager(), key);
    return deleted | removed;
}

uint8_t cTSPersistContext::FindIndexedResource(const Key *key, uint32_t iid, void **out)
{
    Index &index = Map();
    const auto found = index.find(*key);
    if (found == index.end()) return 0;
    return VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, void **), 0>(found->second.mResource.Get(), iid, out);
}

bool cTSPersistContext::HasResource(const Key *key)
{
    return Map().find(*key) != Map().end();
}

bool cTSPersistContext::SetDirty(const Key *key, uint32_t dirty)
{
    Index &index = Map();
    const auto found = index.find(*key);
    if (found == index.end()) return false;
    Entry &entry = found->second;
    entry.mDirty = static_cast<uint8_t>(dirty);
    if (entry.mState.Get() != nullptr) VirtualCall<void(__thiscall *)(void *, uint32_t), 15>(entry.mState.Get(), dirty);
    return true;
}

bool cTSPersistContext::GetDirty(const Key *key, uint8_t *out)
{
    Index &index = Map();
    const auto found = index.find(*key);
    if (found == index.end()) return false;
    const Entry &entry = found->second;
    *out = entry.mDirty;
    if (entry.mState.Get() != nullptr) *out = VirtualCall<NoArgRet8, 14>(entry.mState.Get());
    return true;
}

bool cTSPersistContext::SetMask(const Key *key, uint32_t mask)
{
    Index &index = Map();
    const auto found = index.find(*key);
    if (found == index.end()) return false;
    found->second.mMask = mask;
    return true;
}

bool cTSPersistContext::GetMask(const Key *key, uint32_t *out)
{
    Index &index = Map();
    const auto found = index.find(*key);
    if (found == index.end()) return false;
    *out = found->second.mMask;
    return true;
}

uint8_t cTSPersistContext::Save(uint8_t force, uint32_t mask)
{
    uint8_t all = 1; // and'ed bitwise with each save's al, as the original does
    void *manager = Manager();
    VirtualCall<NoArgRet32, 3>(manager); // its result is not used
    std::vector<Entry *> chosen;
    Index &index = Map();
    if (index.size() != 0) chosen.reserve(index.size());
    for (auto &item : index) {
        Entry &entry = item.second;
        if ((entry.mMask & mask) != entry.mMask) continue;
        if (force || entry.mDirty || (entry.mState.Get() != nullptr && StateDirty(entry.mState.Get()))) chosen.push_back(&entry);
    }
    std::sort(chosen.begin(), chosen.end(), [](const Entry *a, const Entry *b) {
        return a != nullptr && b != nullptr && static_cast<int32_t>(a->mPriority) < static_cast<int32_t>(b->mPriority);
    });
    for (Entry *entry : chosen) {
        const uint8_t saved =
            VirtualCall<uint8_t(__thiscall *)(void *, void *, uint32_t), 16>(manager, entry->mResource.Get(), 0);
        all &= saved;
        entry->mDirty = saved == 0;
        if (entry->mState.Get() != nullptr)
            VirtualCall<void(__thiscall *)(void *, uint32_t), 15>(entry->mState.Get(), saved == 0 ? 1u : 0u);
    }
    return all;
}

bool cTSPersistContext::Clear()
{
    Map().clear();
    return true;
}

uint8_t cTSPersistContext::ReleaseResources(uint32_t mask)
{
    uint8_t all = 1; // and'ed bitwise with each hand-over's al
    void *target = reinterpret_cast<void *>(static_cast<uintptr_t>(VirtualCall<NoArgRet32, 3>(Manager())));
    Index &index = Map();
    for (auto it = index.begin(); it != index.end();) {
        Key copy = it->first;
        const Entry &entry = it->second;
        if ((entry.mMask & mask) != entry.mMask) {
            ++it;
            continue;
        }
        all &= VirtualCall<uint8_t(__thiscall *)(void *, Key *), 10>(target, &copy);
        it = index.erase(it);
    }
    return all;
}

cTSPersistContext *cTSPersistContext::DeletingDestructor(uint32_t flags)
{
    At<const void *>(this, 0) = GameAddress(kVTable);
    At<const void *>(this, 4) = GameAddress(kSubVTable);
    std::destroy_at(&Map());
    reinterpret_cast<cRZUnknown *>(reinterpret_cast<uintptr_t>(this) + 4)->Destruct();
    if (flags & 1) ::operator delete(this, kObjectSize);
    return this;
}
