#pragma once
#include "gz/cGZPersistResourceManager.h"
#include "gz/cIGZPersistResourceManager.h"
#include "rz/RZSystemServiceBase.h"

#include <cstddef>
#include <cstdint>

// gz::cGZPersistResourceManager: the resource manager as a real base class (lane FX), for its only subclass cTSPersistSystem
// (the game builds the base only inside cTSPersistSystem's constructor 0x1d8b90: xrefs of 0x9b3f30 and of the vtables
// 0xe60274 / 0xe60350 / 0xe60388 show nothing else). The bodies stay the hook-shaped struct's (::cGZPersistResourceManager,
// src/gz/cGZPersistResourceManager.h, another group's): this twin has its layout (static_asserts below against the struct)
// and every slot runs the struct's body on `this` (EL's twin pattern, docs/standalone-shape.md). The calls those bodies make
// through `this`'s vtable reach our vtable. When the struct takes the shape, the twin becomes the class and the struct goes.
//
// Faces as the game's (RTTI): cIGZPersistResourceManager at +0 (54 slots), rz::cRZSystemService at +4 (its
// cIGZSystemService face, 13 slots) with cRZUnknownMT at +8 (6 slots; slot 3 the deleting destructor). 0xa8 bytes.
namespace gz {

class cGZPersistResourceManager : public cIGZPersistResourceManager, public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0xa8;
    static constexpr uint32_t kServiceId = 0x56b906e;
    static constexpr int32_t kPriority = 1000000;

    cGZPersistResourceManager();            // 0x9b3f30
    ~cGZPersistResourceManager() override;  // 0x9b4120; deleting 0x9b41d0 (its +8 thunk 0x9b41c4)

    // The hook-shaped struct over the same bytes, whose members are the bodies.
    ::cGZPersistResourceManager *Hooked() { return reinterpret_cast<::cGZPersistResourceManager *>(this); }

    // cIGZUnknown (all three faces): QueryInterface 0x9b5100; AddRef / Release the folded 0x145240 / 0x1a85b0 (the +4
    // service's, i.e. rz::cRZSystemService's).
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override;
    uint32_t Release() override;

    // cIGZPersistResourceManager, slots 3..53.
    uint32_t vf3(uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5) override;
    uint8_t vf4(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) override;
    uint8_t vf5(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) override;
    uint8_t vf6(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) override;
    uint8_t vf7(const void *key, uint32_t iid, void **out, uint32_t a4, uint32_t a5) override;
    uint8_t vf8(void *resource) override;
    uint32_t vf9(uint32_t a1, void *resource) override;
    uint8_t vf10(uint32_t key) override;
    uint8_t vf11(uint32_t key) override;
    uint8_t vf12(void *const *range, uint32_t a2) override;
    uint8_t vf13(const void *key, uint32_t a2) override;
    uint8_t vf14(void *resource, const void *key, void *segment) override;
    uint8_t vf15(uint32_t key) override;
    uint32_t vf16(uint32_t a1, uint32_t a2) override;
    uint32_t vf17(uint32_t list, uint32_t type) override;
    uint32_t vf18(uint32_t list, uint32_t filter) override;
    uint32_t vf19(uint32_t list, uint32_t type) override;
    uint8_t vf20(uint32_t factoryId, uint32_t type, void *factory) override;
    uint8_t vf21(uint32_t factoryId) override;
    uint8_t vf22(uint32_t type, void **out) override;
    uint32_t vf23(const uint32_t *key, uint32_t b) override;
    uint32_t vf24(void *resource, uint32_t a2) override;
    uint32_t vf25() override;
    uint32_t vf26(uint32_t index) override;
    uint32_t vf27(uint32_t segment) override;
    uint8_t vf28(void *segment) override;
    uint8_t vf29(void *segment) override;
    uint8_t vf30(void *segment) override;
    uint8_t vf31(uint32_t segment) override;
    uint8_t vf32() override;
    uint8_t vf33(uint32_t key, void **out) override;
    uint8_t vf34(uint32_t key, void **out) override;
    uint32_t vf35() override;
    uint32_t vf36(uint32_t index) override;
    uint8_t vf37(uint32_t predicate, uint32_t a2) override;
    uint32_t vf38(void **out, uint32_t *count) override;
    uint8_t vf39(uint32_t key, uint32_t record, uint32_t mode) override;
    uint8_t vf40(uint32_t a1, void *const *holder) override;
    uint8_t vf41(void *cache) override;
    uint8_t vf42(void *cache) override;
    uint8_t vf43() override;
    void vf44(uint32_t a1) override;
    uint8_t vf45() override;
    void vf46(void *segment) override;
    uint32_t vf47() override;
    uint32_t vf48() override;
    uint8_t vf49(uint32_t a1, uint32_t a2, uint32_t a3) override;
    uint8_t vf50(uint32_t a1, uint32_t a2) override;
    uint8_t vf51(const uint32_t *key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) override;
    uint8_t vf52(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) override;
    uint8_t vf53(uint32_t a1, void *a2, void *a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) override;

    // cIGZSystemService (+4): Shutdown 0x9b57e0 (the struct's body; never reached on cTSPersistSystem, which overrides it
    // and calls the struct's at its end) and OnIdle 0x9b5040 (the struct's body, `this` = the +4 face).
    bool Shutdown() override;
    bool OnIdle(uint32_t counter) override;

    // Fields, as the hook-shaped struct names them (its bodies use these names through Self()).
    void *mLockA[2];                 // +0x20 cRZCriticalSection (factory maps)
    cGZPersistHashMap mFactories;    // +0x28 factory id -> factory
    cGZPersistHashMap mTypes;        // +0x48 resource type -> factory id
    void *mLockB[2];                 // +0x68 cRZCriticalSection (segments)
    cGZPersistDeque mDeque;          // +0x70 the segments
    uint32_t mField84;               // +0x84 the default segment (counted)
    void *mLockC[2];                 // +0x88 cRZCriticalSection (caches)
    cGZPersistValueNode *mValueHead; // +0x90 std::list of counted cache strategies (node 0xc)
    uint32_t mValueSize;             // +0x94
    void *mLockD[2];                 // +0x98 cRZCriticalSection (serial; cTSPersistSystem's listener lock)
    uint32_t mSerial;                // +0xa0 [0xffff]
    uint32_t mField;                 // +0xa4 [0]
};

static_assert(sizeof(cGZPersistResourceManager) == cGZPersistResourceManager::kSize, "gz::cGZPersistResourceManager size");
static_assert(offsetof(cGZPersistResourceManager, mLockA) == offsetof(::cGZPersistResourceManager, mLockA), "lock A");
static_assert(offsetof(cGZPersistResourceManager, mFactories) == offsetof(::cGZPersistResourceManager, mFactories), "factories");
static_assert(offsetof(cGZPersistResourceManager, mTypes) == offsetof(::cGZPersistResourceManager, mTypes), "types");
static_assert(offsetof(cGZPersistResourceManager, mLockB) == offsetof(::cGZPersistResourceManager, mLockB), "lock B");
static_assert(offsetof(cGZPersistResourceManager, mDeque) == offsetof(::cGZPersistResourceManager, mDeque), "deque");
static_assert(offsetof(cGZPersistResourceManager, mField84) == offsetof(::cGZPersistResourceManager, mField84), "+0x84");
static_assert(offsetof(cGZPersistResourceManager, mLockC) == offsetof(::cGZPersistResourceManager, mLockC), "lock C");
static_assert(offsetof(cGZPersistResourceManager, mValueHead) == offsetof(::cGZPersistResourceManager, mValueHead), "values");
static_assert(offsetof(cGZPersistResourceManager, mLockD) == offsetof(::cGZPersistResourceManager, mLockD), "lock D");
static_assert(offsetof(cGZPersistResourceManager, mSerial) == offsetof(::cGZPersistResourceManager, mSerial), "serial");
static_assert(offsetof(cGZPersistResourceManager, mField) == offsetof(::cGZPersistResourceManager, mField), "+0xa4");

// The hook entries' entry points for the constructor and destructor (docs/standalone-shape.md, step 6). The deleting
// destructor 0x9b41d0 has none: only the game's +8 vtable reaches it (lane GT, in effect through its callers).
struct cGZPersistResourceManagerAdapters {
    static cGZPersistResourceManager *__fastcall Construct(void *memory, void *edx);                                       // 0x9b3f30
    static void __fastcall Destruct(cGZPersistResourceManager *self, void *edx);                                           // 0x9b4120
};

// An unordered_map of the game's STL as its constructors build it: max load factor 1.0, an empty list whose sentinel is a
// fresh node of `nodeSize` bytes, eight buckets (the bucket vector assigned 16 words of the sentinel), mask 7.
void ConstructPersistHashMap(cGZPersistHashMap &map, uint32_t nodeSize);
// The destructor of such a map with 0x10-byte nodes and plain values (std::unordered_map<uint32_t, uint32_t>, the game's
// 0x15d6a0, a library match: ours is the same code).
void DestroyPersistU32Map(cGZPersistHashMap &map);

} // namespace gz
