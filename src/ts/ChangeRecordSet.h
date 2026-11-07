#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

// ChangeRecordSet: one group's cached resources in the persist system's change records (cTSPersistSystem::mChangeRecords,
// group -> set). Each entry maps a resource key to the object read for it (a counted pointer) and the change id it was
// read at; the set is saved in the store as the record {0xad1a6720, 0, group, 0} (Write) and read back from it (Read).
// No RTTI: the name is ours. 0x24 bytes; RVAs are 1.18.0.187.
//
// The table is the game's std::unordered_map instantiation for this key: the hash of a key is the sum of its first three
// words (type + group + instance), keys compare as 16 bytes, nodes are 0x20 bytes (link, key at +8, object at +0x18,
// change id at +0x1c). The node of an erased or cleared entry releases its object (slot 2). The set's destructor
// (0x1f6080, a jump to the map's 0x1f5f90) is that library destructor.
struct ChangeRecordKey {
    uint32_t words[4];
};
struct ChangeRecordKeyHash {
    size_t operator()(const ChangeRecordKey &key) const { return key.words[0] + key.words[1] + key.words[2]; }
};
struct ChangeRecordKeyEqual {
    bool operator()(const ChangeRecordKey &a, const ChangeRecordKey &b) const { return std::memcmp(&a, &b, sizeof(a)) == 0; }
};

// The cached object: a counted pointer (cIGZUnknown: 1 AddRef, 2 Release) released when the entry goes.
struct ChangeRecordObject {
    void *object = nullptr;
    ChangeRecordObject() = default;
    ChangeRecordObject(const ChangeRecordObject &) = delete;
    ChangeRecordObject &operator=(const ChangeRecordObject &) = delete;
    ~ChangeRecordObject();
};
struct ChangeRecordEntry {
    ChangeRecordObject record; // node +0x18
    uint32_t changeId = 0;     // node +0x1c
};

class ChangeRecordSet {
public:
    using Map = std::unordered_map<ChangeRecordKey, ChangeRecordEntry, ChangeRecordKeyHash, ChangeRecordKeyEqual>;

    // 0x1f5850 (thiscall): an empty table (8 buckets), not changed.
    ChangeRecordSet();
    ChangeRecordSet(const ChangeRecordSet &) = delete;
    ChangeRecordSet &operator=(const ChangeRecordSet &) = delete;

    // 0x1f6410 (thiscall, ret 4): the entry of `key` erased, and the set marked changed, when there is one.
    void Erase(const ChangeRecordKey *key);
    // 0x1f6780 (thiscall, ret 0xc): the entry of `key`: when its change id is `changeId`, its object into *out (AddRef'd)
    // and 1; otherwise the stale entry is erased (the set not marked) and 0. 0 when there is none.
    uint8_t Take(const ChangeRecordKey *key, uint32_t changeId, void **out);
    // 0x1f6de0 (thiscall, ret 0xc): the entry of `key` (made when absent) holds a new copy of `variant` (a cRZVariant,
    // copied by the game's constructor 0x9c7a40) and `changeId`; the set is marked changed.
    void Put(const ChangeRecordKey *key, uint32_t changeId, const void *variant);
    // 0x1f6bd0 (thiscall, ret 4): the set emptied, then read from a cIGZIStream: version (slot 13, must be 1), count
    // (slot 13), then per entry the key (slot 25), the entry (made when absent), its change id (slot 13), its object
    // released and read again (slot 26). 0 at the first failed read (what was read stays), else 1.
    uint8_t Read(void *stream);
    // 0x1f6e90 (thiscall, ret 4): written to a cIGZOStream: version 1 and the count (slot 37), then per entry in table order
    // the key (slot 49), change id (slot 37) and object (slot 50). 0 at the first failed write; else 1, not changed.
    uint8_t Write(void *stream);

    Map mRecords;      // +0
    uint8_t mChanged;  // +0x20
    uint8_t mPad[3];
};

// Placement construction for the hook (the constructor's RVA).
struct ChangeRecordSetAdapters {
    static ChangeRecordSet *__fastcall Construct(ChangeRecordSet *self, void *);
};
