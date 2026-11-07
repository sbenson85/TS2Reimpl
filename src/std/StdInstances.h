#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gz/cIGZPropertySet.h" // GZServiceString

// The game's own copies of standard-library members that were not matched as library code (they are
// EA's instantiations, or wrappers the compiler kept out of line), and a trivial constructor stub.
// Each member views the object at `this` as the standard container it is: the game's containers
// have our STL's layout (MSVC 14.40, release).

// std::set<int> (the tree of _Tree_simple_types<int>, 0x14-byte nodes).
struct IntSetObject {
    std::set<int> mSet;

    // 0x1c0366 (__thiscall, no arguments; size-built): the set's destructor, out of line: the library's
    // _Erase_head (0x1c024b) with the set as its allocator frees every node and the head.
    void Destroy() noexcept;
};

// std::unordered_map<uint32_t, uint32_t> (0x20 bytes: the load factor, the node list at +4, the
// bucket vector, mask and max index).
struct UintUintMapObject {
    using Map = std::unordered_map<uint32_t, uint32_t>;
    Map mMap;

    // 0x141c15 (__thiscall, ret 8; size-built): find(key). The iterator is returned through the
    // hidden pointer: the node holding key, or the list head (end()) when there is none. The original
    // is the library's _Hash::find<void> for this map itself (matchcheck: ours calls our copy of it);
    // libmatch did not tie it, so it is replaced here as a wrapper.
    Map::iterator Find(const uint32_t &key);
};

// A GZServiceString (std::basic_string<char> with the allocator-service allocator, 0x18 bytes).
struct ServiceStringObject {
    GZServiceString mText;

    // 0x1773e0 (__thiscall, ret 4): constructs the string from a C string in place (the 16 buffer
    // bytes, size and capacity zeroed first, then the text copied; a heap buffer above 15 characters
    // comes from the allocator service through 0x177110's path). Returns this.
    ServiceStringObject *ConstructFromText(const char *text);
};

static_assert(sizeof(ServiceStringObject) == 0x18, "GZServiceString layout");

// A name -> value table built from a static array (e.g. the light type names at RVA 0xdbd4c0:
// "shapeColorLight" 7, "additiveLight" 9, ...): 61 static initialisers construct one each.
struct NameValueEntry {
    const char *name; // a null name ends the array
    uint32_t value;
};

struct NameValueTable {
    using Entry = std::pair<std::string, uint32_t>; // 0x1c bytes
    std::vector<Entry> mEntries;

    // 0xaf6560 (__thiscall, ret 4): starts empty, appends (name, value) for each entry up to the null
    // name, then sorts by ByLengthThenReverseText (std::sort; 0xaf5fe0 in the original).
    NameValueTable *Construct(const NameValueEntry *entries);

    // The order: shorter names first; names of equal length compared from their last character
    // backwards (signed chars), as std::lexicographical_compare on reverse iterators.
    struct ByLengthThenReverseText {
        bool operator()(const Entry &a, const Entry &b) const;
    };
};

static_assert(sizeof(NameValueTable::Entry) == 0x1c, "the element layout");
static_assert(sizeof(NameValueTable) == 0xc, "a std::vector");

// The objects that the 27 static initialisers around 0x13c560 construct (the first call of each
// initialiser, before it zeroes or copies the object's fields).
struct StaticObjectStub {
    // 0x57c3c0 (__thiscall, no arguments; built without optimisation: a frame and a spill of this):
    // touches nothing and returns this.
    StaticObjectStub *Construct();
};
