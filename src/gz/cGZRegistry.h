#pragma once
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

// The registry service: an in-memory table of named values kept in a red-black tree (a
// std::multimap-shaped tree of cRZString keys, each with two cRZString values). Original vtables
// (1.18.0.187): 0xe9ec38 (cIGZRegistry, 8 slots), 0xe9ec5c (cIGZSystemService part at +4, 13 slots),
// 0xe9ec94 (cRZUnknownMT part at +8, 6 slots). Service id 0x25076b9e, priority 0x98968a. The object
// is 0x28 bytes: the primary vtable, a cRZSystemService at +4 (0x1c bytes), and the tree at +0x20
// (head node pointer, node count).
//
// Slots 3..7 (src/gz/cGZRegistryOps.cpp) work on the tree through its real std::multimap type
// (cGZRegistryMap: same MSVC STL, same layout), so the insert/find/erase instantiations
// (0xc6c9b4, 0xc6c950, 0xc6ca7e, 0xc6cb64, 0x612f59/0x612f89/0x608460, 0x143b4e, 0x143f5f...) are
// library code, not reimplemented here.

// A node of the tree (0x70 bytes): links, colour flags, the key and the two values.
// The node's value (key and two values) as the game passes it to its destructor 0xc6cb8a: `this` is
// the key, at node + 0x10, also for pairs on the stack outside any node.
struct cGZRegistryEntry {
    cRZString mKey;    // 0x00
    cRZString mFirst;  // 0x20
    cRZString mSecond; // 0x40

    // 0xc6cb8a: the second value, the first value, then the key.
    void Destruct();
};

struct cGZRegistryNode {
    cGZRegistryNode *mLeft;    // 0x00
    cGZRegistryNode *mParent;  // 0x04
    cGZRegistryNode *mRight;   // 0x08
    uint8_t mColor;            // 0x0c
    uint8_t mIsNil;            // 0x0d only the head node has this set
    uint16_t mPad;             // 0x0e
    cRZString mKey;            // 0x10
    cRZString mFirst;          // 0x30
    cRZString mSecond;         // 0x50

    cGZRegistryEntry &Entry() { return *reinterpret_cast<cGZRegistryEntry *>(&mKey); }
};

static_assert(sizeof(cGZRegistryNode) == 0x70, "cGZRegistryNode size");
static_assert(offsetof(cGZRegistryNode, mKey) == 0x10 && offsetof(cGZRegistryNode, mFirst) == 0x30 && offsetof(cGZRegistryNode, mSecond) == 0x50, "cGZRegistryNode layout");

struct cGZRegistryTree {
    cGZRegistryNode *mHead;  // 0x00 the head node (its parent is the root, its isnil flag is set)
    uint32_t mSize;          // 0x04
};

static_assert(sizeof(cGZRegistryTree) == 8, "cGZRegistryTree size");

// A cRZString held by value with the game's copy semantics (the copy constructor 0x141eb3 with
// position 0 and length -1: vtable set, text copied, reference count 0), so it can be a member of a
// std container. Raw storage: the cRZString is built and destroyed through its Construct/Destruct.
struct cGZRegistryString {
    alignas(cRZString) unsigned char mRaw[sizeof(cRZString)];

    cRZString &Str() { return *reinterpret_cast<cRZString *>(mRaw); }
    const cRZString &Str() const { return *reinterpret_cast<const cRZString *>(mRaw); }
    cGZRegistryString() { Str().Construct(); }
    explicit cGZRegistryString(const char *text) { Str().Construct(text); }
    explicit cGZRegistryString(const cIGZString &other) { Str().Construct(other); }
    cGZRegistryString(const cGZRegistryString &other) { Str().Construct(other.Str(), 0, 0xffffffffu); }
    cGZRegistryString &operator=(const cGZRegistryString &) = delete;
    ~cGZRegistryString() { Str().Destruct(); }
};

// The tree's key order: operator< on the keys' std::strings (0x144a19).
struct cGZRegistryLess {
    bool operator()(const cGZRegistryString &a, const cGZRegistryString &b) const { return a.Str().mString < b.Str().mString; }
};

using cGZRegistryValues = std::pair<cGZRegistryString, cGZRegistryString>;
// MSVC 14.40's multimap (the same STL as the game's): head pointer, then size; nodes are the 0x70
// byte cGZRegistryNode. Inserting goes to the right of equal keys, find returns the first of them.
using cGZRegistryMap = std::multimap<cGZRegistryString, cGZRegistryValues, cGZRegistryLess>;
static_assert(sizeof(cGZRegistryMap) == 8, "the map is a head pointer and a size");
static_assert(sizeof(cGZRegistryMap::value_type) == 0x60, "the map's value is the key and two values");

// cIGZRegistry (interface id 0xa237613c): the registry's own face (game vtable 0xe9ec38, 8 slots). Its only implementer is
// cGZRegistry; the interface's own vtable 0xe9ec14 (purecalls) is what the original constructor stores first.
class cIGZRegistry : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xa237613c;
    // Callback of Enumerate: cdecl, called with copies of the first and second value.
    using EnumerateCallback = void(__cdecl *)(const cIGZString *first, const cIGZString *second, void *userData);

    // Entries are keyed by a scheme: key = scheme, first value = host, second = path.
    virtual bool Add(const cIGZString *url) = 0;                                                         // slot 3 (ret 4)
    virtual bool RemoveEmpty(void *unused) = 0;                                                          // slot 4 (ret 4)
    virtual bool Lookup(const char *scheme, const cIGZString *host, cIGZString *pathOut) = 0;            // slot 5 (ret 0xc)
    virtual bool Format(const char *scheme, const cIGZString *host, const cIGZString *path, cIGZString *out) = 0; // slot 6 (ret 0x10)
    virtual bool Enumerate(const char *scheme, EnumerateCallback callback, void *userData) = 0;          // slot 7 (ret 0xc)
};

// The registry service in the standalone shape (docs/standalone-shape.md, stage A; lane KD): RTTI bases cIGZRegistry at +0,
// cRZSystemService at +4 (cIGZSystemService +4, cRZUnknownMT +8). Game vtables 0xe9ec38 (8 slots), 0xe9ec5c (13), 0xe9ec94
// (6; slot 3 the deleting destructor, thunk 0xc6cbcc into 0xc6cbd4). Service id 0x25076b9e, priority 0x98968a. 0x28 bytes:
// the primary vtable, the service part at +4 (0x1c bytes), the tree at +0x20 (head node pointer, node count).
//
// Creation: the constructor 0xc6cb27 has no caller in the image (the game's AddApplicationServicesHere, replaced by ours,
// built it); ours builds this class through the adapter. The game's vtables are referenced only by the constructor and the
// destructor 0xc6cba4 (reached only from the deleting destructor 0xc6cbd4, which only the game's +8 vtable holds).
class cGZRegistry : public cIGZRegistry, public rz::cRZSystemService {
public:
    // The separators the operations below use, game globals built at startup (cRZString objects).
    static constexpr uint32_t kSchemeSeparatorRva = 0x11031a0; // "://"
    static constexpr uint32_t kPathSeparatorRva = 0x11031c0;   // "/"
    static constexpr uint32_t kVTable = 0xe9ec38;              // the game's, for tests
    static constexpr uint32_t kSystemServiceVTable = 0xe9ec5c;
    static constexpr uint32_t kUnknownVTable = 0xe9ec94;
    static constexpr uint32_t kSize = 0x28;
    static constexpr uint32_t kServiceId = 0x25076b9e;
    static constexpr int32_t kPriority = 0x98968a;
    static constexpr uint32_t kInterfaceId = cIGZRegistry::kIid;

    // 0xc6cb27: the service part (id, priority), then an empty tree (the library map constructor 0xc6caeb).
    cGZRegistry();
    // 0xc6cba4 (deleting 0xc6cbd4, +8 thunk 0xc6cbcc): the tree erased and its head freed (the library map destructor 0xc6ca2d), then the service part.
    ~cGZRegistry() override;
    cGZRegistry(const cGZRegistry &) = delete;
    cGZRegistry &operator=(const cGZRegistry &) = delete;

    // cIGZUnknown (every face)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0xc6cdfd: the registry iid -> this, else the service's
    uint32_t AddRef() override;                              // folded 0x145240 (the service part's)
    uint32_t Release() override;                             // folded 0x1a85b0

    // cIGZRegistry (src/gz/cGZRegistryOps.size.cpp)
    bool Add(const cIGZString *url) override;                // 0xc6ce2f: "scheme://host/path[/...]" -> new entry
    bool RemoveEmpty(void *unused) override;                 // 0xc6d06e: erases find(empty key); the argument is never read
    bool Lookup(const char *scheme, const cIGZString *host, cIGZString *pathOut) override;                 // 0xc6ccb0
    bool Format(const char *scheme, const cIGZString *host, const cIGZString *path, cIGZString *out) override; // 0xc6cd72
    bool Enumerate(const char *scheme, EnumerateCallback callback, void *userData) override;              // 0xc6cbf3
    cGZRegistryMap &Map() { return *reinterpret_cast<cGZRegistryMap *>(&mTree); }

    cGZRegistryTree mTree; // +0x20: the layout of Map(), the multimap built and destroyed by our constructor and destructor
};

static_assert(sizeof(cGZRegistry) == cGZRegistry::kSize, "cGZRegistry size");
static_assert(offsetof(cGZRegistry, mServiceId) == 0x10, "cGZRegistry layout: the service part at +4");
static_assert(offsetof(cGZRegistry, mTree) == 0x20, "cGZRegistry layout");

// The constructor hook's entry point (docs/standalone-shape.md, step 6). The other slots' adapters are gone: only the game's
// vtables reach their RVAs (in effect through callers).
struct cGZRegistryAdapters {
    static cGZRegistry *__fastcall Construct(void *memory, void *edx); // 0xc6cb27
};
