#pragma once
// nGZSceneGraph::cStringPool (lane OX, scenegraph D1a): the scene graph's interned-string table. A function-local static
// singleton (the game's getter 0xadf8b0 builds it at 0x10f8c70); cSceneGraphSystem starts it in Init (slot 3) and empties
// it in Shutdown (slot 4), and hands it out as its slot 8. Nodes and resources keep their names as handles into it.
//
// A handle is an index into a table of entries. Interning a text answers the handle of an equal text already in the pool
// (the text lowercased first unless the caller asks for case to be kept), else a new entry in the first free index (the
// indices released last first; with none free, a new one at the table's end). A new entry starts with no references:
// interning never counts, the caller's AddRefHandle does, and ReleaseHandle drops the entry when its count reaches zero.
// Equal texts are found by their Jenkins hash (0x98bff0, seed 0), their length and their bytes.
//
// The game's object (0x70 bytes; vtable 0xe7c844, 14 slots) holds a started flag at +4, the hash table (MSVC's
// unordered_map internals, +8..+0x27: entries are 0x38-byte list nodes), the handle table (std::vector of entry pointers,
// +0x28), the free indices (std::vector, +0x34) and a scratch key (+0x40: count, hash, crc, index, a cRZString at +0x50).
// Nothing but its own slots reaches those fields, and only the getter reaches the object, so ours keeps its own containers
// (the key and the scratch string were the original's way of calling its find; they hold nothing between calls).
#include "rz/RZFastFixedAllocators.h"
#include "game/owned.h"
#include "gz/cGZEventReflector.h" // nGZEvent::cIAttributeIDIterator, cAttributeIDRange
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nGZSceneGraph {

// 0x98bff0 (__cdecl): Bob Jenkins' lookup2 hash of `length` bytes from `data`, starting from `seed` (the golden ratio
// 0x9e3779b9 in a and b). The string pool's key hash; cShader and the material attributes hash their names with it too.
uint32_t JenkinsHash(const void *data, uint32_t length, uint32_t seed);

// nGZSceneGraph::cIStringPool (interface id 0x4caa9810).
class cIStringPool : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x4caa9810;
    virtual void Init() = 0;                                              // 3: start answering
    virtual void Shutdown() = 0;                                          // 4: every entry dropped, stop answering
    virtual uint32_t Intern(const char *text, bool caseSensitive) = 0;    // 5: the text's handle, or -1
    virtual void AddRefHandle(uint32_t handle) = 0;                       // 6
    virtual void ReleaseHandle(uint32_t handle) = 0;                      // 7: the entry dropped at zero
    virtual const char *GetText(uint32_t handle) = 0;                     // 8: "" for no entry
    virtual uint32_t GetLength(uint32_t handle) = 0;                      // 9
    virtual uint32_t GetHash(uint32_t handle) = 0;                        // 10: the Jenkins hash
    virtual uint32_t GetCrc(uint32_t handle) = 0;                         // 11: CRC32 of the lowercased text, made once
    virtual nGZEvent::cAttributeIDRange GetHandles() = 0;                 // 12: the live handles (counted iterators)
};

// nGZSceneGraph::cStringHandleIterator (vtable 0xe7c880, 0x10 bytes: vtable, count, index, the table): a forward iterator
// over the pool's live handles (the indices whose entry is not empty). Its blocks come from the class's fixed-size pool
// (0x10f8c60, a cRZFastFixedAllocator2<0x10>; the game's 0xadf510 / 0xadf540), as the game's.
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, 10f8c60); // cStringHandleIterator's pool (operator new / delete)
class cStringHandleIterator final : public nGZEvent::cIAttributeIDIterator {
public:
    static constexpr uint32_t kPool = 0x10f8c60;
    static void *operator new(size_t size) noexcept;
    static void operator delete(void *block) noexcept;

    cStringHandleIterator(const std::vector<void *> *table, uint32_t index); // 0xadf0e0: at the first live index >= index
    cStringHandleIterator(const cStringHandleIterator &other);              // 0xadf0b0: same place, its own count
    virtual ~cStringHandleIterator() = default;                             // 7 (0xadf5c0)

    bool QueryInterface(uint32_t riid, void **out) override;                // 0 (shared 0x178b20): only cIGZUnknown
    uint32_t AddRef() override;                                             // 1 (shared 0x148fb0)
    uint32_t Release() override;                                            // 2 (shared 0x1a84c0)
    cIAttributeIDIterator *Clone() override;                                // 3 (0xadf6e0)
    bool IsEqual(const cIAttributeIDIterator *other) override;              // 4 (shared 0x17b680): same index
    uint32_t Get() override;                                                // 5 (shared 0x46390): the handle
    void Next() override;                                                   // 6 (shared 0xacc480): the next live one

private:
    uint32_t mCount;                       // +4
    uint32_t mIndex;                       // +8
    const std::vector<void *> *mTable;     // +0xc
};

class cStringPool final : public cIStringPool {
public:
    // 0xadf8b0 (__cdecl): the singleton, built on first use (a function-local static, destroyed at exit).
    static cStringPool *Instance();

    cStringPool();                 // 0xadf120
    virtual ~cStringPool();        // 13 (0xadf600; the destructor 0xadf460)

    bool QueryInterface(uint32_t riid, void **out) override;   // 0 (0xadf940): cIGZUnknown and cIStringPool
    uint32_t AddRef() override;                                // 1 (shared 0x15e510): 1, a static object
    uint32_t Release() override;                               // 2 (shared 0x15e510)
    void Init() override;                                      // 3 (0xadf8a0)
    void Shutdown() override;                                  // 4 (0xadfab0)
    uint32_t Intern(const char *text, bool caseSensitive) override; // 5 (0xadf740)
    void AddRefHandle(uint32_t handle) override;               // 6 (0xadf710)
    void ReleaseHandle(uint32_t handle) override;              // 7 (0xadf970)
    const char *GetText(uint32_t handle) override;             // 8 (0xadfae0)
    uint32_t GetLength(uint32_t handle) override;              // 9 (0xadfbd0)
    uint32_t GetHash(uint32_t handle) override;                // 10 (0xadfba0)
    uint32_t GetCrc(uint32_t handle) override;                 // 11 (0xadfb20)
    nGZEvent::cAttributeIDRange GetHandles() override;         // 12 (0xadf620)

private:
    // An interned text (the game's 0x30-byte value in its hash node: count, hash, crc, index, a cRZString).
    struct Entry {
        int32_t mCount;    // references the callers took (AddRefHandle); may go below zero, as in the original
        uint32_t mHash;    // JenkinsHash of the text
        uint32_t mCrc;     // CRC32 of the lowercased text, 0 until GetCrc makes it
        uint32_t mIndex;   // its handle
        const std::string *mText; // the map's key
    };

    Entry *Find(uint32_t handle) const; // the live entry of `handle`, or null (also when not started)

    bool mStarted = false;                            // +4 in the game's
    std::unordered_map<std::string, Entry> mEntries;  // by text (the game's hash table, +8)
    std::vector<void *> mTable;                       // handle -> Entry * or null (+0x28; the iterators read it)
    std::vector<uint32_t> mFree;                      // released handles, reused last first (+0x34)
};

} // namespace nGZSceneGraph
