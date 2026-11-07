#include "ts/cTSEffectsExtension.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "sg/SGLocalString.h"

#include <new>
#include <functional>
#include <unordered_map>
#include <vector>

namespace nGZSceneGraph {
namespace {
using ReleaseFn = uint32_t(__thiscall *)(void *);
using AddRefFn = uint32_t(__thiscall *)(void *);

void ReleaseObject(void *p) { if (p) VirtualCall<ReleaseFn, 2>(p); }

using GetSystemServiceFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
using GetEffectsManagerFn = void *(__thiscall *)(void *);
using CreateVisualEffectFn = uint8_t(__thiscall *)(void *, const char *, void **);

constexpr uint32_t kCommonSystemClass = 0x8af76dc8;
constexpr uint32_t kCommonSystemIid = 0x4af76d8c;

// The effects manager of the common system service (not counted, as in the original): the framework's system service,
// asked through its slot 5, then its slot 4. A missing framework or service gives null (the original dereferences them).
void *GetEffectsManagerService()
{
    ScopedRef common{nullptr};
    if (void *const framework = GetServiceObject())
        VirtualCall<GetSystemServiceFn, 5>(framework, kCommonSystemClass, kCommonSystemIid, &common.mObject);
    void *const effects = common.mObject ? VirtualCall<GetEffectsManagerFn, 4>(common.mObject) : nullptr;
    common.ReleaseObject();
    return effects;
}

// The registry's hash of a record's key.
uint32_t RecordHash(const std::string &first, const std::string &second, uint32_t discriminator)
{
    return static_cast<uint32_t>(std::hash<std::string>{}(first)) | static_cast<uint32_t>(std::hash<std::string>{}(second)) |
           static_cast<uint32_t>(std::hash<uint32_t>{}(discriminator));
}

// The registry's insert (0xcc4e30) and destructor (0xcc5280) are library code: the toolchain's map over the same 0x20 bytes,
// on operator new as the original's, of a key (0x44 bytes: the first two fields and the discriminator of a
// TSEffectInsertValue; its copy 0x4f5a10 and destructor 0x257860 are the game's) and a value holding the instance counted.
struct RegistryKey {
    RegistryKey(const RegistryKey &other)
    {
        OriginalFunction<RegistryKey *(__thiscall *)(RegistryKey *, const RegistryKey *), 0x4f5a10>()(this, &other);
    }
    RegistryKey &operator=(const RegistryKey &) = delete;
    ~RegistryKey() { OriginalFunction<void(__thiscall *)(RegistryKey *), 0x257860>()(this); }
    const TSEffectInsertValue &Fields() const { return *reinterpret_cast<const TSEffectInsertValue *>(this); }
    alignas(4) uint8_t mBytes[0x44];
};
struct RegistryValue {
    RegistryValue(const RegistryValue &other) : mInstance(other.mInstance), mKind(other.mKind), mFlag(other.mFlag)
    {
        if (mInstance) VirtualCall<AddRefFn, 1>(mInstance);
    }
    RegistryValue &operator=(const RegistryValue &) = delete;
    ~RegistryValue() { ReleaseObject(mInstance); }
    void *mInstance;
    uint32_t mKind;
    uint8_t mFlag;
};
struct RegistryHash {
    size_t operator()(const RegistryKey &key) const
    {
        const TSEffectInsertValue &fields = key.Fields();
        return RecordHash(fields.first.mString, fields.second.mString, fields.discriminator);
    }
};
struct RegistryEqual {
    bool operator()(const RegistryKey &a, const RegistryKey &b) const
    {
        const TSEffectInsertValue &x = a.Fields(), &y = b.Fields();
        return x.first.mString == y.first.mString && x.second.mString == y.second.mString &&
               x.discriminator == y.discriminator;
    }
};
using Registry = std::unordered_map<RegistryKey, RegistryValue, RegistryHash, RegistryEqual>;
static_assert(sizeof(Registry) == sizeof(TSEffectsRegistry) && sizeof(Registry::value_type) == sizeof(TSEffectInsertValue),
              "the registry's layout");
}

TSEffectFindResult *cTSEffectsExtension::FindRecord(TSEffectFindResult *result, const TSEffectFindKey *key, uint32_t hash)
{
    auto **const range = static_cast<TSEffectRecord **>(mRegistry.buckets) + (hash & mRegistry.bucketMask) * 2;
    auto *const first = range[0];
    auto *record = range[1];
    if (record == mRegistry.sentinel) {
        result->next = static_cast<TSEffectRecord *>(mRegistry.sentinel);
        result->found = nullptr;
        return result;
    }
    for (;;) {
        if (record->firstText == key->firstText && record->secondText == key->secondText &&
            record->hashValue == key->discriminator) {
            result->next = record->next;
            result->found = record;
            return result;
        }
        if (record == first) {
            result->next = record;
            result->found = nullptr;
            return result;
        }
        record = record->previous;
    }
}

TSEffectRecord **cTSEffectsExtension::RemoveRecord(TSEffectRecord **result, TSEffectRecord *record)
{
    const uint32_t hash = (static_cast<uint32_t>(std::hash<std::string>{}(record->firstText)) |
                           static_cast<uint32_t>(std::hash<std::string>{}(record->secondText)) |
                           static_cast<uint32_t>(std::hash<uint32_t>{}(record->hashValue))) & mRegistry.bucketMask;
    auto **const range = static_cast<TSEffectRecord **>(mRegistry.buckets) + hash * 2;
    auto *const first = range[0];
    if (range[1] == record) {
        if (first == record)
            range[0] = range[1] = static_cast<TSEffectRecord *>(mRegistry.sentinel);
        else
            range[1] = record->previous;
    } else if (first == record) {
        range[0] = record->next;
    }
    auto *const next = record->next;
    --mRegistry.count;
    record->previous->next = next;
    next->previous = record->previous;
    ReleaseObject(record->instance);
    record->firstText.~basic_string();
    record->secondText.~basic_string();
    ::operator delete(record, sizeof(TSEffectRecord));
    *result = next;
    return result;
}

void cTSEffectsExtension::ApplyStateBytes(void *instance)
{
    using SetValueFn = void(__thiscall *)(void *, uint32_t);
    VirtualCall<SetValueFn, 7>(instance, *(reinterpret_cast<uint8_t *>(this) + 0x42));
    VirtualCall<SetValueFn, 8>(instance, *(reinterpret_cast<uint8_t *>(this) + 0x41));
}

void cTSEffectsExtension::ConfigureInstance(void *instance, void *node)
{
    using SetNodeFn = void(__thiscall *)(void *, uint32_t, void *);
    VirtualCall<SetNodeFn, 14>(instance, 2, node);
    ApplyStateBytes(instance);
}

// The registry's insert (library code, the toolchain's map above) of a record
// holding a counted `instance`; the temporary reference of the value is dropped again.
void cTSEffectsExtension::InsertRecord(const TSEffectFindKey &key, void *instance, uint32_t kind, uint8_t flag)
{
    TSEffectInsertValue value;
    value.first.mVTable = cRZString::VTable();
    value.first.mString = key.firstText;
    value.first.mRefCount = 0;
    value.second.mVTable = cRZString::VTable();
    value.second.mString = key.secondText;
    value.second.mRefCount = 0;
    value.discriminator = key.discriminator;
    value.instance = instance;
    if (instance) VirtualCall<AddRefFn, 1>(instance);
    value.kind = kind;
    value.flag = flag;
    reinterpret_cast<Registry *>(&mRegistry)->insert(reinterpret_cast<const Registry::value_type &>(value));
    ReleaseObject(value.instance);
}

cTSEffectsExtension::cTSEffectsExtension()
{
    Construct(1);
}

cTSEffectsExtension *cTSEffectsExtension::Construct(uint32_t)
{
    mRegistry.unknown0 = 0x3f800000;
    mRegistry.sentinel = nullptr;
    mRegistry.count = 0;
    mRegistry.buckets = nullptr;
    mRegistry.bucketEnd = nullptr;
    mRegistry.bucketCapacityEnd = nullptr;
    mRegistry.bucketMask = 7;
    mRegistry.bucketCount = 8;
    mUnknown40 = 0;
    mUnknown44 = 6;
    mText.Construct();
    mText.mString.clear();

    // The list's sentinel and the eight two-word bucket ranges are separate game-heap allocations.
    auto *sentinel = static_cast<TSEffectRecord *>(::operator new(sizeof(TSEffectRecord)));
    mRegistry.sentinel = sentinel;
    sentinel->next = sentinel;
    sentinel->previous = sentinel;
    auto **buckets = static_cast<uint32_t **>(::operator new(0x40));
    mRegistry.buckets = buckets;
    mRegistry.bucketEnd = buckets + 16;
    mRegistry.bucketCapacityEnd = buckets + 16;
    for (size_t i = 0; i < 8; ++i) {
        buckets[2 * i] = reinterpret_cast<uint32_t *>(sentinel);
        buckets[2 * i + 1] = reinterpret_cast<uint32_t *>(sentinel);
    }
    return this;
}

cTSEffectsExtension::~cTSEffectsExtension()
{
    Destruct();
}

void cTSEffectsExtension::Destruct()
{
    reinterpret_cast<Registry *>(&mRegistry)->~unordered_map();
    mText.Destruct();
}

cTSEffectsExtension *cTSEffectsExtension::DeletingDestructor(uint32_t flags)
{
    this->~cTSEffectsExtension();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

bool cTSEffectsExtension::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kEffectsIid) {
        *out = static_cast<cITSEffectsExtension *>(this);
        AddRef();
        return true;
    }
    return cExtension::QueryInterface(iid, out);
}

// Slot 15 is the folded return-zero body (0x2a58eb -> 0x6c6b0): the class does not copy itself.
cIExtension *cTSEffectsExtension::Clone() { return nullptr; }

// Slot 24 is the folded return-false body (0x25d659 -> 0x147480), not cExtension's true.
bool cTSEffectsExtension::IsPersistent() { return false; }

const char *cTSEffectsExtension::GetTypeName() { return "effectActions"; }

bool cTSEffectsExtension::IsKindOf(uint32_t iid)
{
    return iid == kEffectsIid || cExtension::IsKindOf(iid);
}

uint8_t cTSEffectsExtension::vf0()
{
    cIObjectGraphNode *const owner = GetOwner();
    using DisplayNameFn = const char *(__cdecl *)(void *);
    const char *const displayName = OriginalFunction<DisplayNameFn, 0xae7900>()(owner);
    if (displayName) mText.mString.assign(displayName);
    else mText.Sprintf("node 0x%08x", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)));
    return 1;
}
uint8_t cTSEffectsExtension::Dispatch(uint32_t operation, const char *name, const char *nodeName, uint32_t kind,
                                      uint32_t discriminator, uint32_t flag)
{
    void *const face = reinterpret_cast<uint8_t *>(this) + 0x18;
    using FindFn = void *(__thiscall *)(void *, const char *, const char *, uint32_t);
    using CreateFn = void *(__thiscall *)(void *, uint32_t, const char *, const char *, uint32_t, uint32_t);
    using RemoveFn = uint32_t(__thiscall *)(void *, const char *, const char *, uint32_t);
    using SetStateFn = void(__thiscall *)(void *, uint32_t);
    switch (operation) {
    case 0: {
        void *instance = VirtualCall<FindFn, 6>(face, name, nodeName, discriminator);
        if (!instance) instance = VirtualCall<CreateFn, 2>(face, kind, name, nodeName, discriminator, flag);
        if (!instance) return 0;
        VirtualCall<SetStateFn, 3>(instance, 0);
        return 1;
    }
    case 1: {
        void *const instance = VirtualCall<FindFn, 6>(face, name, nodeName, discriminator);
        if (!instance) return 0;
        VirtualCall<SetStateFn, 4>(instance, 0);
        return 1;
    }
    case 2: {
        void *instance = VirtualCall<FindFn, 6>(face, name, nodeName, discriminator);
        if (!instance) instance = VirtualCall<CreateFn, 2>(face, kind, name, nodeName, discriminator, 0);
        if (!instance) return 0;
        VirtualCall<SetStateFn, 3>(instance, 1);
        return 1;
    }
    case 3: {
        void *const instance = VirtualCall<FindFn, 6>(face, name, nodeName, discriminator);
        if (!instance) return 0;
        VirtualCall<SetStateFn, 4>(instance, 1);
        return static_cast<uint8_t>(VirtualCall<RemoveFn, 5>(face, name, nodeName, discriminator));
    }
    case 4: {
        // A standalone effect: made and started, not registered; the instance is released again on the way out.
        void *const effects = GetEffectsManagerService();
        void *instance = nullptr;
        void *const node = FindEffectNode(nodeName, GetOwner());
        if (node && effects) {
            if (VirtualCall<CreateVisualEffectFn, 14>(effects, name, &instance)) {
                ConfigureInstance(instance, node);
                VirtualCall<SetStateFn, 3>(instance, 0);
                ReleaseObject(instance);
                return 1;
            }
        }
        ReleaseObject(instance);
        return 0;
    }
    default:
        return 0;
    }
}
void *cTSEffectsExtension::CreateEffect(uint32_t kind, const char *name, const char *nodeName, uint32_t discriminator,
                                        uint8_t flag)
{
    void *const node = FindEffectNode(nodeName, GetOwner());
    if (!node) return nullptr;
    TSEffectFindKey key{};
    key.firstText.assign(name);
    key.secondText.assign(nodeName ? nodeName : "");
    key.discriminator = discriminator;
    TSEffectFindResult found{};
    FindRecord(&found, &key, RecordHash(key.firstText, key.secondText, key.discriminator));
    if (found.found) return nullptr;

    void *const effects = GetEffectsManagerService();
    void *instance = nullptr;
    void *result = nullptr;
    if (effects) {
        // The manager's factory name: the debug stand-in while the debug byte (face +0x2b) is set.
        const char *const factoryName = *(reinterpret_cast<uint8_t *>(this) + 0x43) ? "debugEffect" : name;
        if (VirtualCall<CreateVisualEffectFn, 14>(effects, factoryName, &instance)) {
            ConfigureInstance(instance, node);
            // The first record switches the extension to bound (face +0x10 is the registry's count).
            if (mRegistry.count == 0 && *(reinterpret_cast<uint8_t *>(this) + 0x40) == 0)
                *(reinterpret_cast<uint8_t *>(this) + 0x40) = 1;
            InsertRecord(key, instance, kind, flag);
            result = instance;
        }
    }
    ReleaseObject(instance);
    return result;
}
void *cTSEffectsExtension::FindEffectNode(const char *name, void *node)
{
    if (!name || !*name || _stricmp(name, "locator") == 0)
        return node;
    using FindNodeByNameFn = void *(__cdecl *)(const char *, void *, int);
    return OriginalFunction<FindNodeByNameFn, 0xae7490>()(name, node, 0xf);
}
bool cTSEffectsExtension::vf3(const uint16_t *name, std::vector<uint16_t> *out)
{
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        using GetNameFn = const uint16_t *(__thiscall *)(void *);
        const uint16_t *candidate = VirtualCall<GetNameFn, 5>(&record->nameProviderVTable);
        const uint16_t *left = candidate;
        const uint16_t *right = name;
        while (*left && *left == *right) { ++left; ++right; }
        if (*left == *right) out->push_back(static_cast<uint16_t>(record->hashValue));
    }
    return !out->empty();
}
bool cTSEffectsExtension::RemoveByInstance(void *instance)
{
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        if (record->instance == instance) {
            using SetActiveFn = void(__thiscall *)(void *, uint32_t);
            VirtualCall<SetActiveFn, 4>(record->instance, 1);
            TSEffectRecord *next;
            RemoveRecord(&next, record);
            return 1;
        }
    }
    return 0;
}
uint32_t cTSEffectsExtension::vf5(const char *first, const char *second, uint32_t discriminator)
{
    TSEffectFindKey key{};
    key.firstText.assign(first);
    key.secondText.assign(second ? second : "");
    key.discriminator = discriminator;
    const uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(key.firstText)) |
                          static_cast<uint32_t>(std::hash<std::string>{}(key.secondText)) |
                          static_cast<uint32_t>(std::hash<uint32_t>{}(key.discriminator));
    TSEffectFindResult result{};
    FindRecord(&result, &key, hash);
    if (auto *record = result.found) {
        if (record->instance) {
            using IsActiveFn = uint8_t(__thiscall *)(void *);
            if (VirtualCall<IsActiveFn, 5>(record->instance)) {
                using SetActiveFn = void(__thiscall *)(void *, uint32_t);
                VirtualCall<SetActiveFn, 4>(record->instance, 1);
            }
        }
        TSEffectRecord *next;
        RemoveRecord(&next, record);
    }
    // 0xcc6620 explicitly clears AL on every exit; callers observe this byte result.
    return 0;
}
void *cTSEffectsExtension::vf6(const char *first, const char *second, uint32_t discriminator)
{
    TSEffectFindKey key{};
    key.firstText.assign(first);
    key.secondText.assign(second ? second : "");
    key.discriminator = discriminator;
    const uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(key.firstText)) |
                          static_cast<uint32_t>(std::hash<std::string>{}(key.secondText)) |
                          static_cast<uint32_t>(std::hash<uint32_t>{}(key.discriminator));
    TSEffectFindResult result{};
    FindRecord(&result, &key, hash);
    return result.found ? result.found->instance : nullptr;
}
void cTSEffectsExtension::vf7(uint8_t flag, int32_t kind)
{
    if (flag != 0) {
        vf8(static_cast<uint32_t>(kind));
        return;
    }
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        if (kind != 0 && static_cast<int32_t>(record->kind) != kind) continue;
        if (!record->instance) continue;
        using IsActiveFn = uint8_t(__thiscall *)(void *);
        using SetValueFn = void(__thiscall *)(void *, uint32_t);
        if (VirtualCall<IsActiveFn, 5>(record->instance) && record->flag == 0)
            VirtualCall<SetValueFn, 4>(record->instance, 0);
    }
}
void cTSEffectsExtension::vf8(uint32_t kind)
{
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel;) {
        auto *const next = record->next;
        if ((kind == 0 || record->kind == kind) && record->instance) {
            using IsActiveFn = uint8_t(__thiscall *)(void *);
            if (VirtualCall<IsActiveFn, 5>(record->instance) && record->flag == 0) {
                using SetValueFn = void(__thiscall *)(void *, uint32_t);
                VirtualCall<SetValueFn, 4>(record->instance, 1);
                void *const instance = record->instance;
                record->instance = nullptr;
                ReleaseObject(instance);
                TSEffectRecord *removedNext;
                RemoveRecord(&removedNext, record);
            }
        }
        record = next;
    }
}
void cTSEffectsExtension::vf9(uint8_t flag)
{
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel;) {
        auto *const next = record->next;
        if (record->kind != 1) {
            if (!record->instance) {
                TSEffectRecord *removedNext;
                RemoveRecord(&removedNext, record);
            } else {
                using IsActiveFn = uint8_t(__thiscall *)(void *);
                const bool active = VirtualCall<IsActiveFn, 5>(record->instance) != 0;
                using IsReadyFn = uint8_t(__thiscall *)(void *);
                if (!active || (flag != 0 && VirtualCall<IsReadyFn, 6>(record->instance) != 0)) {
                    using AddRefFn = uint32_t(__thiscall *)(void *);
                    using ReleaseFn = uint32_t(__thiscall *)(void *);
                    VirtualCall<AddRefFn, 1>(record->instance);
                    const int32_t references = static_cast<int32_t>(VirtualCall<ReleaseFn, 2>(record->instance));
                    if (references < 3) {
                        void *const instance = record->instance;
                        record->instance = nullptr;
                        VirtualCall<ReleaseFn, 2>(instance);
                        TSEffectRecord *removedNext;
                        RemoveRecord(&removedNext, record);
                    }
                }
            }
        }
        record = next;
    }
}
void cTSEffectsExtension::vf10(uint32_t value)
{
    auto *const byte = reinterpret_cast<uint8_t *>(this) + 0x41;
    if (*byte == static_cast<uint8_t>(value)) return;
    *byte = static_cast<uint8_t>(value);
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        if (record->instance) {
            using SetValueFn = void(__thiscall *)(void *, uint32_t);
            VirtualCall<SetValueFn, 8>(record->instance, value);
        }
    }
}
void cTSEffectsExtension::vf11(uint32_t value)
{
    auto *const byte = reinterpret_cast<uint8_t *>(this) + 0x42;
    if (*byte == static_cast<uint8_t>(value)) return;
    *byte = static_cast<uint8_t>(value);
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        if (record->instance) {
            using SetValueFn = void(__thiscall *)(void *, uint32_t);
            VirtualCall<SetValueFn, 7>(record->instance, value);
        }
    }
}
void cTSEffectsExtension::vf12(uint32_t a, uint32_t b, uint32_t c)
{
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        if (record->instance) {
            using CallFn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
            VirtualCall<CallFn, 13>(record->instance, a, b, c);
        }
    }
}
void cTSEffectsExtension::vf13(uint32_t a, uint32_t b)
{
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        if (record->instance) {
            using CallFn = void(__thiscall *)(void *, uint32_t, uint32_t);
            VirtualCall<CallFn, 14>(record->instance, a, b);
        }
    }
}
void cTSEffectsExtension::vf14()
{
    // The original slot is entered through the +0x18 effects face and tests face+0x28.
    auto *const bound = reinterpret_cast<uint8_t *>(this) + 0x40;
    if (*bound == 0) return;
    *bound = 0;
    vf9(0);
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        if (!record->instance) continue;
        using SetValueFn = void(__thiscall *)(void *, uint32_t);
        using SetPairFn = void(__thiscall *)(void *, uint32_t, uint32_t);
        VirtualCall<SetValueFn, 4>(record->instance, 1);
        VirtualCall<SetPairFn, 14>(record->instance, 2, 0);
    }
}
void cTSEffectsExtension::vf15()
{
    auto *const bound = reinterpret_cast<uint8_t *>(this) + 0x40;
    if (*bound != 0) return;
    *bound = 1;
    void *const owner = GetOwner();
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        if (!record->instance) continue;
        void *const node = FindEffectNode(record->secondText.c_str(), owner);
        if (!node) continue;
        using SetNodeFn = void(__thiscall *)(void *, uint32_t, void *);
        using SetStateFn = void(__thiscall *)(void *, uint32_t);
        VirtualCall<SetNodeFn, 14>(record->instance, 2, node);
        VirtualCall<SetStateFn, 3>(record->instance, 1);
    }
}
uint8_t cTSEffectsExtension::vf16() { return static_cast<uint8_t>(mUnknown40); }
uint8_t cTSEffectsExtension::vf17()
{
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        if (!record->instance) continue;
        using IsActiveFn = uint8_t(__thiscall *)(void *);
        if (VirtualCall<IsActiveFn, 5>(record->instance)) return 1;
    }
    return 0;
}
void cTSEffectsExtension::ReadRecords(void *stream)
{
    using GetByteFn = uint8_t(__thiscall *)(void *, uint8_t *);
    using GetWordFn = uint8_t(__thiscall *)(void *, uint16_t *);
    using GetDwordFn = uint8_t(__thiscall *)(void *, uint32_t *);
    using GetStringFn = uint8_t(__thiscall *)(void *, cRZString *);
    // The original dereferences a null framework or common system unchecked; here the manager is then null.
    void *const effects = GetEffectsManagerService();
    nTSSG::SGLocalString first, second;
    void *instance = nullptr;
    uint16_t version = 0;
    VirtualCall<GetWordFn, 7>(stream, &version);
    uint32_t count = 0;
    if (static_cast<uint16_t>(version - 1) < 2) {
        VirtualCall<GetDwordFn, 9>(stream, &count);
        if (count - 1 < 999) {
            for (uint32_t index = 0; index < count; ++index) {
                uint32_t discriminator = 0, kind = 0;
                // A version 1 stream has no flag byte; the original then stores an uninitialised stack byte, 0 here.
                uint8_t flag = 0;
                VirtualCall<GetStringFn, 15>(stream, &first.value);
                VirtualCall<GetStringFn, 15>(stream, &second.value);
                VirtualCall<GetDwordFn, 9>(stream, &discriminator);
                VirtualCall<GetDwordFn, 9>(stream, &kind);
                if (version > 1) VirtualCall<GetByteFn, 5>(stream, &flag);
                TSEffectFindKey key{};
                key.firstText = first.value.mString;
                key.secondText = second.value.mString;
                key.discriminator = discriminator;
                TSEffectFindResult found{};
                FindRecord(&found, &key, RecordHash(key.firstText, key.secondText, discriminator));
                if (!found.found && effects) {
                    void *const previous = instance;
                    instance = nullptr;
                    ReleaseObject(previous);
                    if (VirtualCall<CreateVisualEffectFn, 14>(effects, first.Text(), &instance)) {
                        ApplyStateBytes(instance);
                        InsertRecord(key, instance, kind, flag);
                    }
                }
            }
        }
    }
    ReleaseObject(instance);
}
void cTSEffectsExtension::vf19(void *writer)
{
    void *const face = reinterpret_cast<uint8_t *>(this) + 0x18;
    using ProcessUnusedFn = void(__thiscall *)(void *, uint32_t);
    VirtualCall<ProcessUnusedFn, 9>(face, 1);
    using WriteUintFn = uint32_t(__thiscall *)(void *, uint32_t);
    using WriteStringFn = uint32_t(__thiscall *)(void *, const void *);
    using WriteByteFn = uint32_t(__thiscall *)(void *, int8_t);
    VirtualCall<WriteUintFn, 7>(writer, 2);
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    uint32_t count = 0;
    for (auto *record = sentinel->next; record != sentinel; record = record->next)
        if (record->kind != 1) ++count;
    VirtualCall<WriteUintFn, 9>(writer, count);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        if (record->kind == 1) continue;
        VirtualCall<WriteStringFn, 15>(writer, &record->firstTextVTable);
        VirtualCall<WriteStringFn, 15>(writer, &record->nameProviderVTable);
        VirtualCall<WriteUintFn, 9>(writer, record->hashValue);
        VirtualCall<WriteUintFn, 9>(writer, record->kind);
        VirtualCall<WriteByteFn, 5>(writer, static_cast<int8_t>(record->flag));
    }
}
void cTSEffectsExtension::SetByte2b(uint8_t value) { *reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(this) + 0x43) = value; }
void cTSEffectsExtension::SetWord2c(uint32_t value) { *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(this) + 0x44) = value; }
std::string cTSEffectsExtension::StatusText()
{
    using IsActiveFn = uint8_t(__thiscall *)(void *);
    nTSSG::SGLocalString status, line;
    status.value.Sprintf("Effects status for '%s':\n", mText.mString.c_str());
    if (*(reinterpret_cast<uint8_t *>(this) + 0x40) != 0) status.value.mString.append("  Effects are bound.\n", 21);
    else status.value.mString.append("  Effects are unbound.\n", 23);
    auto *const sentinel = static_cast<TSEffectRecord *>(mRegistry.sentinel);
    for (auto *record = sentinel->next; record != sentinel; record = record->next) {
        line.value.Sprintf("  %s/%s/0x%08x/0x%08x", record->firstText.c_str(), record->secondText.c_str(), record->kind,
                           record->hashValue);
        status.value.mString.append(line.value.mString.c_str(), line.value.mString.size());
        if (!record->instance) status.value.mString.append(" is not instantiated\n", 21);
        else if (VirtualCall<IsActiveFn, 5>(record->instance)) status.value.mString.append(" is started\n", 12);
        else status.value.mString.append(" is stopped\n", 12);
    }
    return status.value.mString;
}
// The text is built and dropped, as in the original; only the instances' IsActive calls are seen from outside.
void cTSEffectsExtension::BuildStatusText() { (void)StatusText(); }

cTSEffectsExtension *__fastcall cTSEffectsExtensionAdapters::Construct(cTSEffectsExtension *self, void *, uint32_t mostDerived)
{
    return ::new (self) cTSEffectsExtension();
}
void __fastcall cTSEffectsExtensionAdapters::Destruct(cTSEffectsExtension *self, void *) { self->Destruct(); }
cTSEffectsExtension *__fastcall cTSEffectsExtensionAdapters::DeletingDestructor(cTSEffectsExtension *self, void *, uint32_t flags)
{
    return self->DeletingDestructor(flags);
}
bool __fastcall cTSEffectsExtensionAdapters::QueryInterface(void *face, void *, uint32_t iid, void **out)
{
    return reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0xc)->QueryInterface(iid, out);
}
uint32_t __fastcall cTSEffectsExtensionAdapters::ForwardNoArg(void *face, void *)
{
    return reinterpret_cast<cTSEffectsExtension *>(face)->AddRef();
}
void *__cdecl cTSEffectsExtensionAdapters::FindEffectNode(const char *name, void *node)
{
    if (!name || !*name || _stricmp(name, "locator") == 0)
        return node;
    using FindNodeByNameFn = void *(__cdecl *)(const char *, void *, int);
    return OriginalFunction<FindNodeByNameFn, 0xae7490>()(name, node, 0xf);
}
TSEffectFindResult *__fastcall cTSEffectsExtensionAdapters::FindRecord(void *registry, void *, TSEffectFindResult *result,
                                                                        const TSEffectFindKey *key, uint32_t hash)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(registry) - 0x20);
    return self->FindRecord(result, key, hash);
}
TSEffectRecord **__fastcall cTSEffectsExtensionAdapters::RemoveRecord(void *registry, void *, TSEffectRecord **result,
                                                                      TSEffectRecord *record)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(registry) - 0x20);
    return self->RemoveRecord(result, record);
}
bool __fastcall cTSEffectsExtensionAdapters::RemoveByInstance(void *face, void *, void *instance)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    return self->RemoveByInstance(instance);
}
uint8_t __fastcall cTSEffectsExtensionAdapters::HasActiveEffect(void *face, void *)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    return self->cTSEffectsExtension::vf17();
}
void __fastcall cTSEffectsExtensionAdapters::SetStateByte1(void *face, void *, uint32_t value)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::vf10(value);
}
void __fastcall cTSEffectsExtensionAdapters::SetStateByte2(void *face, void *, uint32_t value)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::vf11(value);
}
void __fastcall cTSEffectsExtensionAdapters::BroadcastThree(void *face, void *, uint32_t a, uint32_t b, uint32_t c)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::vf12(a, b, c);
}
void __fastcall cTSEffectsExtensionAdapters::BroadcastTwo(void *face, void *, uint32_t a, uint32_t b)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::vf13(a, b);
}
void __fastcall cTSEffectsExtensionAdapters::FilterActive(void *face, void *, uint8_t flag, int32_t kind)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::vf7(flag, kind);
}
void __fastcall cTSEffectsExtensionAdapters::RemoveActive(void *face, void *, uint32_t kind)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::vf8(kind);
}
void __fastcall cTSEffectsExtensionAdapters::Unbind(void *face, void *)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::vf14();
}
bool __fastcall cTSEffectsExtensionAdapters::CollectNamed(void *face, void *, const uint16_t *name,
                                                          std::vector<uint16_t> *out)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    return self->cTSEffectsExtension::vf3(name, out);
}
uint8_t __fastcall cTSEffectsExtensionAdapters::GetDisplayName(void *face, void *)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    return self->cTSEffectsExtension::vf0();
}
void __fastcall cTSEffectsExtensionAdapters::BindEffects(void *face, void *)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::vf15();
}
void *__fastcall cTSEffectsExtensionAdapters::FindInstance(void *face, void *, const char *first,
                                                            const char *second, uint32_t discriminator)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    return self->cTSEffectsExtension::vf6(first, second, discriminator);
}
uint32_t __fastcall cTSEffectsExtensionAdapters::RemoveByKey(void *face, void *, const char *first,
                                                              const char *second, uint32_t discriminator)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    return self->cTSEffectsExtension::vf5(first, second, discriminator);
}
void __fastcall cTSEffectsExtensionAdapters::ProcessUnused(void *face, void *, uint8_t flag)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::vf9(flag);
}
uint8_t __fastcall cTSEffectsExtensionAdapters::Dispatch(void *face, void *, uint32_t operation, const char *name,
                                                          const char *nodeName, uint32_t kind, uint32_t discriminator,
                                                          uint32_t flag)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    return self->cTSEffectsExtension::Dispatch(operation, name, nodeName, kind, discriminator, flag);
}
void *__fastcall cTSEffectsExtensionAdapters::CreateEffect(void *face, void *, uint32_t kind, const char *name,
                                                            const char *nodeName, uint32_t discriminator, uint8_t flag)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    return self->cTSEffectsExtension::CreateEffect(kind, name, nodeName, discriminator, flag);
}
void __fastcall cTSEffectsExtensionAdapters::ReadRecords(void *face, void *, void *stream)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::ReadRecords(stream);
}
void __fastcall cTSEffectsExtensionAdapters::BuildStatusText(void *face, void *)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::BuildStatusText();
}
void __fastcall cTSEffectsExtensionAdapters::WriteRecords(void *face, void *, void *writer)
{
    auto *self = reinterpret_cast<cTSEffectsExtension *>(static_cast<uint8_t *>(face) - 0x18);
    self->cTSEffectsExtension::vf19(writer);
}

} // namespace nGZSceneGraph
