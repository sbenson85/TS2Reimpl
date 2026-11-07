#include "ts/cTSPersistCacheStrategyGenerational.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZRefCount.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <new>

namespace {

// Field access by offset from the object (as the original code does).
template <typename T> T &At(const void *base, uintptr_t offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(base) + offset);
}

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using NoArgRet8 = uint8_t(__thiscall *)(void *);
using NoArgVoid = void(__thiscall *)(void *);
using OneArgVoid = void(__thiscall *)(void *, uint32_t);
using OneArgRet8 = uint8_t(__thiscall *)(void *, uint32_t);
using ThreeArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);

// The config table at +0x5c: +4 is the third argument of slot 23, +8..+0x14 are four limits.
constexpr uintptr_t kTableField = 0x5c;
constexpr uintptr_t kCounterBase = 0x60;
constexpr uintptr_t kCounterCount = 4;

// Slot 23 with (4, 1, -1), the call made by vf4 and vf17.
void CallSlot23(void *self)
{
    VirtualCall<ThreeArgRet32, 23>(self, 4, 1, 0xffffffffu);
}

} // namespace

uint32_t cTSPersistCacheStrategyGenerational::Release()
{
    // The original jumps to cRZRefCountMT::Release for the subobject at +4 (`add ecx, 4; jmp`).
    return reinterpret_cast<cRZRefCountMT *>(reinterpret_cast<uintptr_t>(this) + 4)->Release();
}

uint32_t cTSPersistCacheStrategyGenerational::vf4()
{
    CallSlot23(this);
    // Tail jump: the result of slot 18 (its eax) is this function's result.
    return VirtualCall<NoArgRet32, 18>(this);
}

uint8_t cTSPersistCacheStrategyGenerational::vf11(uint32_t a)
{
    void *obj = At<void *>(this, 0x4c);
    if (!obj) return 1;
    // The callee's al decides the result; `test al, al` looks at the low byte only.
    return VirtualCall<OneArgRet8, 3>(obj, a) != 0 ? 1 : 0;
}

uint8_t cTSPersistCacheStrategyGenerational::vf14(uint32_t /*unused*/)
{
    // The argument is popped (ret 4) and not used.
    if (At<uint8_t>(this, 0x70)) VirtualCall<OneArgVoid, 22>(this, 0);
    return 1;
}

uint8_t cTSPersistCacheStrategyGenerational::vf17()
{
    ++At<uint32_t>(this, 0x50);
    uint32_t calls = 0;
    while (calls < 8) {
        // Re-read the vtable each time, as the original does.
        if ((VirtualCall<ThreeArgRet32, 23>(this, 4, 1, 0xffffffffu) & 0xff) == 0) break;
        ++calls;
    }
    return calls != 0 ? 1 : 0;
}

void cTSPersistCacheStrategyGenerational::vf21(uint32_t which)
{
    // Static config tables in the game image (RVAs 0xda7290, 0xda72b8, 0xda72e0).
    switch (which) {
    case 0: At<uint32_t>(this, kTableField) = reinterpret_cast<uintptr_t>(GameAddress(0xda7290)); break;
    case 1: At<uint32_t>(this, kTableField) = reinterpret_cast<uintptr_t>(GameAddress(0xda72b8)); break;
    case 2: At<uint32_t>(this, kTableField) = reinterpret_cast<uintptr_t>(GameAddress(0xda72e0)); break;
    default: break;
    }
}

uint32_t cTSPersistCacheStrategyGenerational::vf22(uint32_t force)
{
    // Only the low byte of the argument is tested (`test bl, bl`).
    const bool forceFlag = (force & 0xff) != 0;
    uint32_t i = 0;
    for (; i < kCounterCount; ++i) {
        uint32_t &counter = At<uint32_t>(this, kCounterBase + 4 * i);
        ++counter;
        // The table pointer is read again on every pass, as the original does.
        const uint32_t limit = At<uint32_t>(At<void *>(this, kTableField), 8 + 4 * i);
        bool reset;
        if (counter >= limit) {
            reset = true;
        } else if (i == 0 && forceFlag) {
            reset = true;
        } else {
            reset = false;
        }
        if (!reset) break;
        counter = 0;
    }
    // Level is the index of the counter that stopped the scan, plus one (5 when all four were reset).
    const uint32_t level = i + 1;
    void *table = At<void *>(this, kTableField);
    return VirtualCall<ThreeArgRet32, 23>(this, level, 0, At<uint32_t>(table, 4));
}

// ---- The cache (lane NN) ----------------------------------------------------------------------------------

namespace {

using Key = cTSPersistCacheStrategyGenerational::Key;

constexpr uintptr_t kTickField = 0x50;
constexpr uintptr_t kCountField = 0x54;
constexpr uintptr_t kLimitField = 0x58;
constexpr uint32_t kGenerations = 4;

// The resource's slots used here: 1 AddRef, 2 Release, 3 its key (into the buffer; returns the key's address),
// 4 set its key, 11 its use count.
const Key *ResourceKey(void *resource, Key *buffer)
{
    return VirtualCall<const Key *(__thiscall *)(void *, Key *), 3>(resource, buffer);
}

void AddRefObject(void *object)
{
    VirtualCall<NoArgRet32, 1>(object);
}

void ReleaseObject(void *object)
{
    VirtualCall<NoArgRet32, 2>(object);
}

// The key filter GetKeysOfType hands to slot 12: cGZPersistResourceKeyFilterByType (12 bytes, vtable 0xda5cf0; slot 3
// accepts keys of its type). Built as the game builds it: reference count 0, the caller AddRefs it.
struct TypeKeyFilter {
    static constexpr uint32_t kVTable = 0xda5cf0;

    explicit TypeKeyFilter(uint32_t type) noexcept
    {
        mVTable = GameAddress(kVTable);
        mRefCount = 0;
        mType = type;
    }

    const void *mVTable; // +0x00
    uint32_t mRefCount;  // +0x04
    uint32_t mType;      // +0x08
};
static_assert(sizeof(TypeKeyFilter) == 0xc, "type key filter layout");

} // namespace

bool cTSPersistCacheStrategyGenerational::Get(const Key *key, void **out)
{
    Index &index = Map();
    const auto found = index.find(*key);
    if (found == index.end()) return false;
    const Generation::iterator node = found->second;
    Generation &list = Gen(node->mGeneration);
    if (node->mResource != nullptr) {
        node->mStamp = At<uint32_t>(this, kTickField);
        node->mRecent = 1;
        // To the back (a no-op when it is there already). The original indexes the moved node again; splicing keeps
        // the node, so the index entry already names it.
        list.splice(list.end(), list, node);
        *out = node->mResource;
        AddRefObject(node->mResource);
        return true;
    }
    // No resource: the node is looked for in its generation and dropped; the index entry goes either way.
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it == node) {
            list.erase(it);
            break;
        }
    }
    --At<uint32_t>(this, kCountField);
    index.erase(found);
    return false;
}

bool cTSPersistCacheStrategyGenerational::Add(const Key *key, void *resource)
{
    Index &index = Map();
    if (index.find(*key) != index.end()) return false;
    AddRefObject(resource);
    VirtualCall<void(__thiscall *)(void *, const Key *), 4>(resource, key);
    Generation &young = Gen(0);
    // The original's pad byte is an uninitialised stack byte; zero here.
    young.push_back(Entry{resource, 0, 0, 1, 0, At<uint32_t>(this, kTickField)});
    ++At<uint32_t>(this, kCountField);
    index[*key] = std::prev(young.end());
    return true;
}

bool cTSPersistCacheStrategyGenerational::Remove(const Key *key)
{
    Index &index = Map();
    const auto found = index.find(*key);
    if (found == index.end()) return false;
    const Generation::iterator node = found->second;
    index.erase(found);
    if (node->mResource != nullptr) {
        ReleaseObject(node->mResource);
        node->mResource = nullptr;
    }
    Gen(node->mGeneration).erase(node);
    --At<uint32_t>(this, kCountField);
    return true;
}

bool cTSPersistCacheStrategyGenerational::Contains(const Key *key)
{
    return Map().find(*key) != Map().end();
}

uint32_t cTSPersistCacheStrategyGenerational::GetKeys(KeyList *keys, void *filter)
{
    uint32_t count = 0;
    for (const auto &item : Map()) {
        Key key;
        ResourceKey(item.second->mResource, &key);
        if (filter == nullptr || VirtualCall<uint8_t(__thiscall *)(void *, Key *), 3>(filter, &key) != 0) {
            if (keys != nullptr) keys->push_back(key);
            ++count;
        }
    }
    return count;
}

uint32_t cTSPersistCacheStrategyGenerational::GetKeysOfType(KeyList *keys, uint32_t type)
{
    auto *filter = new TypeKeyFilter(type);
    AddRefObject(filter);
    const uint32_t count = VirtualCall<uint32_t(__thiscall *)(void *, KeyList *, void *), 12>(this, keys, filter);
    ReleaseObject(filter);
    return count;
}

void cTSPersistCacheStrategyGenerational::Clear()
{
    for (uint32_t g = 0; g < kGenerations; ++g) {
        Generation &list = Gen(g);
        for (Entry &entry : list) ReleaseObject(entry.mResource);
        list.clear();
    }
    Map().clear();
    At<uint32_t>(this, kCountField) = 0;
}

bool cTSPersistCacheStrategyGenerational::Evict(int32_t levels, uint8_t force, uint32_t budget)
{
    uint32_t &tick = At<uint32_t>(this, kTickField);
    uint32_t &count = At<uint32_t>(this, kCountField);
    const uint32_t &limit = At<uint32_t>(this, kLimitField);
    if (!force && count <= limit) return false;

    Index &index = Map();
    const uint32_t *table = At<const uint32_t *>(this, kTableField);
    const uint32_t minAge = table[0];
    const int32_t generations = levels < 5 ? levels : 4;
    uint32_t evicted = 0;
    for (int32_t g = 0; g < generations; ++g) {
        Generation &list = Gen(static_cast<uint32_t>(g));
        const uint8_t threshold = static_cast<uint8_t>(table[6 + g]); // table +0x18 + 4 * g, low byte
        uint32_t remaining = budget;
        auto it = list.begin();
        while (it != list.end()) {
            if (remaining == 0) {
                // Out of budget: the walk resumes here next time (the rest of the list goes to the front).
                if (it != list.begin()) list.splice(list.begin(), list, it, list.end());
                break;
            }
            --remaining;
            Entry &entry = *it;
            void *resource = entry.mResource;
            const uint32_t uses = VirtualCall<NoArgRet32, 11>(resource);
            if (uses < 2) {
                if (entry.mRecent) {
                    entry.mRecent = 0;
                    entry.mStamp = tick;
                }
                if (force || tick - entry.mStamp > minAge) {
                    ++evicted;
                    Key key;
                    ResourceKey(resource, &key);
                    const auto found = index.find(key);
                    if (found != index.end() && found->second == it) {
                        index.erase(found);
                    } else {
                        // The key does not lead here: every index entry naming this node goes.
                        for (auto m = index.begin(); m != index.end();) {
                            if (m->second == it)
                                m = index.erase(m);
                            else
                                ++m;
                        }
                    }
                    ReleaseObject(resource);
                    entry.mResource = nullptr;
                    it = list.erase(it);
                    --count;
                    if (!force && count <= limit) {
                        ++tick;
                        return true;
                    }
                    continue;
                }
            } else if (!entry.mRecent) {
                entry.mRecent = 1;
            }
            const auto next = std::next(it);
            if (g < 3 && threshold <= ++entry.mAge) {
                Generation &older = Gen(static_cast<uint32_t>(g + 1));
                older.splice(older.end(), list, it);
                const auto moved = std::prev(older.end());
                Key buffer;
                const Key *key = ResourceKey(resource, &buffer);
                index[*key] = moved;
                entry.mGeneration = static_cast<uint8_t>(g + 1);
                entry.mAge = 0;
            }
            it = next;
        }
    }
    ++tick;
    return evicted != 0;
}

cTSPersistCacheStrategyGenerational *cTSPersistCacheStrategyGenerational::DeletingDestructor(uint32_t flags)
{
    At<const void *>(this, 0) = GameAddress(kVTable);
    At<const void *>(this, 4) = GameAddress(kSubVTable);
    if (void *watcher = At<void *>(this, 0x4c)) ReleaseObject(watcher);
    std::destroy_at(&Map());
    for (uint32_t g = kGenerations; g-- > 0;) std::destroy_at(&Gen(g));
    reinterpret_cast<cRZRefCountMT *>(reinterpret_cast<uintptr_t>(this) + 4)->Destruct();
    if (flags & 1) ::operator delete(this, kObjectSize);
    return this;
}
