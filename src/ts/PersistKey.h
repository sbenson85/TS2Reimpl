#pragma once
#include <cstddef>
#include <cstdint>

// The 16-byte resource key the persistence caches index by (type, group, instance, resource), and the hash and
// equality of the game's std::unordered_map instantiations over it: the hash is the sum of the four words
// (StdHashTable::Find128 0x1f5570 is their lookup), equality compares all four. The hash is not noexcept, as the
// game's: those maps' clear() is the bulk one MSVC uses for hashes that may throw.
struct PersistKey {
    uint32_t mWords[4];
};
static_assert(sizeof(PersistKey) == 0x10, "persist key");

struct PersistKeyHash {
    size_t operator()(const PersistKey &k) const { return k.mWords[0] + k.mWords[1] + k.mWords[2] + k.mWords[3]; }
};

struct PersistKeyEqual {
    bool operator()(const PersistKey &a, const PersistKey &b) const
    {
        return a.mWords[0] == b.mWords[0] && a.mWords[1] == b.mWords[1] && a.mWords[2] == b.mWords[2] &&
               a.mWords[3] == b.mWords[3];
    }
};
