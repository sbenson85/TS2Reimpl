#pragma once
// The material manager's three iterator classes (lane TI, scenegraph M1a leftovers; the game's classes in 0xac95e0-0xacc4b0).
// cMaterialManager's slots 34-36 hand them out as {a clone of begin, begin, end} (MaterialRange). Each is a counted,
// forward-only iterator whose blocks come from its own fixed-size pool, as the game's:
//   cMaterialHandleIterator       vtable 0xe785b4, 0x10 bytes, pool 0x10f6818: the entries by index, skipping empty indices
//   cRenderShaderContextIterator  vtable 0xe785d8, 0xc bytes,  pool 0x10f6824: an entry's variants (its hash container's list)
//   cShaderIterator               vtable 0xe785fc, 0xc bytes,  pool 0x10f6830: the listeners of set B (the same kind of list)
// Layout of all three: +0 vtable, +4 count, then the position: +8 the index (handles) with +0xc the entry vector, or +8 the
// list node it stands on. A list node is the container's: next, previous, then the value (the variants' pair is the key's
// three words and the material; the listeners' is a counted object).
#include "rz/RZFastFixedAllocators.h"
#include "game/owned.h"
#include "gz/cGZEventReflector.h" // nGZEvent::cIAttributeIDIterator
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

// A node of the containers the range iterators walk (MSVC's list node of an unordered container: next, previous, value).
struct MaterialListNode {
    const MaterialListNode *next;
    const MaterialListNode *prev;
    uint32_t value[4];
};

// What cRenderShaderContextIterator::Get answers (by hidden pointer): the variant node's value, 16 bytes.
struct MaterialVariantValue {
    uint32_t key[3];
    cIGZUnknown *material;
};
static_assert(sizeof(MaterialVariantValue) == 0x10, "MaterialVariantValue size");

// Slots 3-6 of the range iterators (their slot 5 differs); slots 0-2 and the deleting destructor are cIGZUnknown's.
class cIRenderShaderContextIterator : public cIGZUnknown {
public:
    virtual cIRenderShaderContextIterator *Clone() = 0;                       // 3
    virtual bool IsEqual(const cIRenderShaderContextIterator *other) = 0;     // 4
    virtual MaterialVariantValue Get() = 0;                                   // 5
    virtual void Next() = 0;                                                  // 6
};

class cIShaderIterator : public cIGZUnknown {
public:
    static constexpr uint32_t kShaderIid = 0xcd2e1781; // what Get asks the listener for
    virtual cIShaderIterator *Clone() = 0;                                    // 3
    virtual bool IsEqual(const cIShaderIterator *other) = 0;                  // 4
    virtual void *Get() = 0;                                                  // 5
    virtual void Next() = 0;                                                  // 6
};

// vtable 0xe785b4: over a vector of entry pointers by index; a position is live when its entry is not null. The slots
// are the game's cIAttributeIDIterator (Get answers the index).
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, 10f6818); // cMaterialHandleIterator's pool (operator new / delete)
class cMaterialHandleIterator final : public nGZEvent::cIAttributeIDIterator {
public:
    static constexpr uint32_t kPool = 0x10f6818;
    static void *operator new(size_t size) noexcept;   // 0xaca0e0
    static void operator delete(void *block) noexcept; // 0xaca170

    cMaterialHandleIterator(const std::vector<void *> *entries, uint32_t index); // 0xac9610: at the first live index >= index
    cMaterialHandleIterator(const cMaterialHandleIterator &other);               // 0xac95e0: same place, its own count
    virtual ~cMaterialHandleIterator() = default;                                // 7 (0xaca3e0; the body 0xac9de0)

    bool QueryInterface(uint32_t riid, void **out) override;     // 0 (shared 0x178b20): only cIGZUnknown
    uint32_t AddRef() override;                                  // 1 (shared 0x148fb0)
    uint32_t Release() override;                                 // 2 (shared 0x1a84c0)
    cIAttributeIDIterator *Clone() override;                     // 3 (0xacaca0)
    bool IsEqual(const cIAttributeIDIterator *other) override;   // 4 (shared 0x17b680): same index
    uint32_t Get() override;                                     // 5 (shared 0x46390): the index
    void Next() override;                                        // 6 (shared 0xacc480): the next live one

private:
    uint32_t mCount;                       // +4
    uint32_t mIndex;                       // +8
    const std::vector<void *> *mEntries;   // +0xc
};

// vtable 0xe785d8: over the nodes of an entry's variant list.
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, 10f6824); // cRenderShaderContextIterator's pool (operator new / delete)
class cRenderShaderContextIterator final : public cIRenderShaderContextIterator {
public:
    static constexpr uint32_t kPool = 0x10f6824;
    static void *operator new(size_t size) noexcept;   // 0xaca110
    static void operator delete(void *block) noexcept; // 0xaca1f0

    // 0xac9b20: standing on `node` (the game's takes the address of the word that holds it).
    explicit cRenderShaderContextIterator(const MaterialListNode *node);
    cRenderShaderContextIterator(const cRenderShaderContextIterator &other); // 0xac9b00: same place, its own count
    virtual ~cRenderShaderContextIterator() = default;                       // 7 (0xaca450; the body 0xaca0c0)

    bool QueryInterface(uint32_t riid, void **out) override;                 // 0 (shared 0x178b20)
    uint32_t AddRef() override;                                              // 1 (shared 0x148fb0)
    uint32_t Release() override;                                             // 2 (shared 0x1a84c0)
    cIRenderShaderContextIterator *Clone() override;                         // 3 (0xacacd0)
    bool IsEqual(const cIRenderShaderContextIterator *other) override;       // 4 (shared 0x17b680): same node
    MaterialVariantValue Get() override;                                     // 5 (0xacb130): the node's value
    void Next() override;                                                    // 6 (0xacc4b0, shared with cShaderIterator)

private:
    uint32_t mCount;                 // +4
    const MaterialListNode *mNode;   // +8
};

// vtable 0xe785fc: over the nodes of the listener list of set B.
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, 10f6830); // cShaderIterator's pool (operator new / delete)
class cShaderIterator final : public cIShaderIterator {
public:
    static constexpr uint32_t kPool = 0x10f6830;
    static void *operator new(size_t size) noexcept;   // 0xaca140
    static void operator delete(void *block) noexcept; // 0xaca270

    explicit cShaderIterator(const MaterialListNode *node);                  // 0xac9b60 (see above)
    cShaderIterator(const cShaderIterator &other);                           // 0xac9b40
    virtual ~cShaderIterator() = default;                                    // 7 (0xaca490; the body 0xaca0d0)

    bool QueryInterface(uint32_t riid, void **out) override;                 // 0 (shared 0x178b20)
    uint32_t AddRef() override;                                              // 1 (shared 0x148fb0)
    uint32_t Release() override;                                             // 2 (shared 0x1a84c0)
    cIShaderIterator *Clone() override;                                      // 3 (0xacad00)
    bool IsEqual(const cIShaderIterator *other) override;                    // 4 (shared 0x17b680)
    void *Get() override;                                                    // 5 (0xacb150): the node's listener, as shader
    void Next() override;                                                    // 6 (0xacc4b0)

private:
    uint32_t mCount;
    const MaterialListNode *mNode;
};

} // namespace nGZSceneGraph
