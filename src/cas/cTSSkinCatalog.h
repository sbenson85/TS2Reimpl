#pragma once
// nTSCASCore::cTSSkinCatalog: Create-a-Sim's catalog of skins, overlays and other body resources (RTTI name; service id
// 0x4bcf1bb0, priority 0xffd23940; class id 0x0bcf1b60, cITSSkinCatalog iid 0x0bcf1b58). 0xc8 bytes, /O2. Lanes IU (first
// slots, hook shape) and JF (every slot, the standalone shape).
//
// What it holds: the catalog's sources (+0x2c, counted package/catalog objects; +0x38 the current one's index), and the
// catalog's entries in one map per resource type, keyed by resource key (type, group, instance, resource; ordered by type,
// group, resource, instance) with a counted entry object as value. Ten types (the resource's slot 12 answers 1..10; the
// resource type each stands for is the id slot 7 stores in the key): 1 0xebcf3e27 (+0x5c), 2 0x4c158081 (+0x4c), 3
// 0x8c1580b5 (+0x54), 4 0x2c1fd8a1 (+0x3c), 5 0x0c1fe246 (+0x44), 6 0x6c93b566 (+0x6c), 7 0x8c93bf6c (+0x74), 8 0x8c93e35c
// (+0x7c), 9 0x0c93e3de (+0x84), 10 0xd1954460 (+0x8c). Type 1 is also kept grouped by the bits of a 14-bit mask (+0x64:
// bit -> map). Entries answer slot 3 (their property object) and keys / masks through further slots; the queries (slots
// 11..36) filter one map or a pair of them into a caller's vector.
//
// Loading is lazy: every query first calls EnsureLoaded (0xb74910, 5,281 B, a non-slot member still game code: it reads
// the sources and fills the maps through the per-type insert helpers, which slot 7 also calls).
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xe8a4dc, 13 slots), +4 cRZUnknownMT (0xe8a514, 6),
// +0x1c cITSSkinCatalog (0xe8a530, 48). The original's interface slot bodies take `this` as the +0x1c face; ours are
// members (the compiler adjusts).
//
// Standalone shape (docs/standalone-shape.md): the creator 0xb7a0e0 (registered by address by cTSCASCOMDirector, class
// 0x0bcf1b60) is the only caller of the constructor 0xb72310; the vtables are referenced only by the constructor and the
// destructor 0xb72b40. Both hooks on: every catalog carries our vtables.
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZRandom.h"
#include "ts/cTSCustomContentSupport.h" // ContentServiceStrings (cRZString values in an allocator-service vector)
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class cIGZString;

namespace nTSCASCore {

// A resource key as the maps order it: type, group, resource, instance (the lower-bound search 0x1d5d10, find 0x4a14b0).
struct SkinKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
    uint32_t resource;
};
static_assert(sizeof(SkinKey) == 0x10, "resource key");
struct SkinKeyLess {
    bool operator()(const SkinKey &a, const SkinKey &b) const
    {
        if (a.type != b.type) return a.type < b.type;
        if (a.group != b.group) return a.group < b.group;
        if (a.resource != b.resource) return a.resource < b.resource;
        return a.instance < b.instance;
    }
};

// A counted object (an entry, a source, a held resource): AddRef / Release through slots 1 / 2.
using SkinRef = cRZAutoRefCount<cIGZUnknown>;

// key -> counted entry: MSVC 14.40 std::map as in the game (node 0x24: links, colour / nil bytes, the key at +0x10, the entry
// at +0x20; _Erase_tree 0x49ac10 releases the entries).
using SkinEntryMap = std::map<SkinKey, SkinRef, SkinKeyLess>;
// mask bit -> the type-1 entries carrying it (node 0x1c: the bit at +0x10, the inner map at +0x14; _Erase_tree 0xb71d40).
using SkinGroupMap = std::map<uint32_t, SkinEntryMap>;
// id -> id (node 0x18; destroyed by 0xb67320).
using SkinIdMap = std::map<uint32_t, uint32_t>;
// The counted vectors (+0x2c sources, +0xa4 resources): std::allocator, destroyed by 0x14c060.
using SkinRefVector = std::vector<SkinRef>;
// The queries' out-parameter: a caller's vector of raw pointers (entries or their property objects) from the allocator
// service (growth 0xb61520 / 0xb5b2e0 / 0x6cd4da / 0x38c800, all the same code for 4-byte elements).
using SkinPtrVector = std::vector<void *, GZServiceAllocator<void *>>;
static_assert(sizeof(SkinEntryMap) == 8 && sizeof(SkinGroupMap) == 8 && sizeof(SkinIdMap) == 8, "MSVC 14.40 map");
static_assert(sizeof(SkinRefVector) == 0xc && sizeof(SkinPtrVector) == 0xc, "MSVC 14.40 vector");

// cITSSkinCatalog (iid 0x0bcf1b58): slots 3-47. A "predicate" is an object whose slot 3 answers whether it takes the
// object it is handed (al) and whose slot 4 answers a type (slot 13) or a 14-bit group mask (slot 12). In the filters,
// "props" is an entry's slot 3 object and a key argument is a pointer to 16 bytes compared whole.
class cITSSkinCatalog : public cIGZUnknown {
public:
    virtual uint32_t CountOfType(uint32_t type) = 0;                               // 3 (0xb7a620): 0 outside 1..10
    virtual uint32_t TotalCount() = 0;                                             // 4 (0xb7a6a0): slot 3 for 0..10, summed
    virtual uint32_t MappedId1(uint32_t id) = 0;                                   // 5 (0xb7a7c0): +0x94, 0 when absent
    virtual uint32_t MappedId2(uint32_t id) = 0;                                   // 6 (0xb7a5a0): +0x9c, 0 when absent
    virtual bool AddResource(void *resource, void *source) = 0;                    // 7 (0xb731d0)
    virtual bool RemoveResource(void *resource) = 0;                               // 8 (0xb76bb0)
    virtual uint32_t Reload() = 0;                                                 // 9 (0xb7aff0)
    virtual void ClearAll() = 0;                                                   // 10 (0xb81530)
    virtual void FindType1(SkinPtrVector *out, uint8_t ages, uint8_t genders, const SkinKey *guid5, const SkinKey *guid6,
                           uint8_t flags, uint32_t groups, uint16_t mask7, const SkinKey *key) = 0; // 11 (0xb7d700)
    virtual void FindType1Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 12 (0xb7da90)
    virtual void FindPropsMatching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 13 (0xb7dbf0)
    virtual void FindType2(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint32_t mask25,
                           const SkinKey *key) = 0;                                // 14 (0xb7d510)
    virtual void FindType2Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 15 (0xb7d650)
    virtual bool HasType2(const SkinKey *key) = 0;                                 // 16 (0xb7ada0)
    virtual void FindType3(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint32_t mask25,
                           const SkinKey *key) = 0;                                // 17 (0xb7b780)
    virtual void FindType3Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 18 (0xb7b8c0)
    virtual bool HasType3(const SkinKey *key) = 0;                                 // 19 (0xb7ad30)
    virtual void FindOverlayProps(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                                  const SkinKey *guidB, const SkinKey *id, uint16_t mask) = 0; // 20 (0xb7c2c0)
    virtual void FindOverlayPropsMatching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 21 (0xb7c720)
    virtual void FindType5(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                           const SkinKey *guidB, const SkinKey *id, uint16_t mask22, uint16_t mask) = 0; // 22 (0xb7bff0)
    virtual void FindType5Any(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                              const SkinKey *guidB, const SkinKey *id, uint16_t mask) = 0; // 23 (0xb7be10)
    virtual void FindType5Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 24 (0xb7c210)
    virtual void FindType4(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                           const SkinKey *guidB, const SkinKey *id, uint16_t mask22, uint16_t mask) = 0; // 25 (0xb7e090)
    virtual void FindType4Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 26 (0xb7e2b0)
    virtual void FindType6(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22) = 0; // 27
    virtual void FindType6Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 28 (0xb7cf20)
    virtual void FindType7(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                           const SkinKey *id) = 0;                                 // 29 (0xb7cfd0)
    virtual void FindType7Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 30 (0xb7d110)
    virtual void FindType8(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                           const SkinKey *id) = 0;                                 // 31 (0xb7c840)
    virtual void FindType8Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 32 (0xb7c980)
    virtual void FindType9(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                           const SkinKey *id) = 0;                                 // 33 (0xb7cc20)
    virtual void FindType9Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 34 (0xb7cd60)
    virtual void FindType10(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                            const SkinKey *id) = 0;                                // 35 (0xb7ca30)
    virtual void FindType10Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) = 0; // 36 (0xb7cb70)
    // 37 (0xb7d1c0): the entry `key` names (by its type's map) handed (target, arg) through its slot 0; when there is none or
    // it refuses, the persist resource manager's (framework service 0x056b906e / 0x9af5e924) slot 5 (key, target, arg, 0, 0).
    virtual bool ApplyEntry(const SkinKey *key, void *target, uint32_t arg) = 0;
    virtual bool PickRandomType1(SkinKey *out, void *predicate) = 0;              // 38 (0xb7a8e0)
    virtual bool PickRandomProps(SkinKey *out, void *predicate) = 0;              // 39 (0xb7a810)
    virtual bool AddSource(void *source, bool load) = 0;                           // 40 (0xb740e0)
    virtual bool RemoveSource(void *source) = 0;                                   // 41 (0xb81700)
    virtual bool HasSource(void *source) = 0;                                      // 42 (0xb7ace0)
    virtual void GetSourceNames(ContentServiceStrings *out) = 0;                   // 43 (0xb7a9b0)
    virtual bool GetCurrentSourceName(cIGZString *out) = 0;                        // 44 (0xb7a600)
    virtual bool SelectSource(const char *name) = 0;                               // 45 (0xb81920)
    virtual bool IsSourceOpen(const char *name) = 0;                               // 46 (0xb7ae60)
    virtual void ClearCurrentSource() = 0;                                         // 47 (0xb75dc0)
};

class cTSSkinCatalog : public rz::cRZSystemService, public cITSSkinCatalog {
public:
    static constexpr uint32_t kVTable = 0xe8a4dc;
    static constexpr uint32_t kVTable4 = 0xe8a514;
    static constexpr uint32_t kCatalogVTable = 0xe8a530;
    static constexpr uint32_t kSize = 0xc8;
    static constexpr uint32_t kServiceId = 0x4bcf1bb0;
    static constexpr int32_t kPriority = int32_t(0xffd23940);
    static constexpr uint32_t kIid = 0x0bcf1b58; // cITSSkinCatalog
    static constexpr uint32_t kTypeCount = 10;

    cTSSkinCatalog();                    // 0xb72310
    ~cTSSkinCatalog() override;          // 0xb72b40; deleting 0xb730f0 (+4 thunk 0xb72f37)
    cTSSkinCatalog(const cTSSkinCatalog &) = delete;
    cTSSkinCatalog &operator=(const cTSSkinCatalog &) = delete;

    // 0xb7a0e0 (__cdecl): the COM director's creator: a new catalog (its +0 face; null when new failed).
    static cTSSkinCatalog *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xb7bc70; thunks 0xb7bca1 (+4), 0xb7bca9 (+0x1c)
    uint32_t AddRef() override;                              // folded 0x1cc320 (the service's); thunks folded
    uint32_t Release() override;                             // folded 0x1cd7a0 (the service's); thunks folded

    // ---- cIGZSystemService ----
    bool Init() override;     // 0xb7aaf0
    bool Shutdown() override; // folded 0xb82190 (shared with cTSBinCatalog): ClearAll through the +0x1c face, then true

    // ---- cITSSkinCatalog (+0x1c) ----
    uint32_t CountOfType(uint32_t type) override;
    uint32_t TotalCount() override;
    uint32_t MappedId1(uint32_t id) override;
    uint32_t MappedId2(uint32_t id) override;
    bool AddResource(void *resource, void *source) override;
    bool RemoveResource(void *resource) override;
    uint32_t Reload() override;
    void ClearAll() override;
    void FindType1(SkinPtrVector *out, uint8_t ages, uint8_t genders, const SkinKey *guid5, const SkinKey *guid6,
                   uint8_t flags, uint32_t groups, uint16_t mask7, const SkinKey *key) override;
    void FindType1Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    void FindPropsMatching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    void FindType2(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint32_t mask25,
                   const SkinKey *key) override;
    void FindType2Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    bool HasType2(const SkinKey *key) override;
    void FindType3(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint32_t mask25,
                   const SkinKey *key) override;
    void FindType3Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    bool HasType3(const SkinKey *key) override;
    void FindOverlayProps(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                          const SkinKey *guidB, const SkinKey *id, uint16_t mask) override;
    void FindOverlayPropsMatching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    void FindType5(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                   const SkinKey *guidB, const SkinKey *id, uint16_t mask22, uint16_t mask) override;
    void FindType5Any(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                      const SkinKey *guidB, const SkinKey *id, uint16_t mask) override;
    void FindType5Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    void FindType4(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, const SkinKey *guidA,
                   const SkinKey *guidB, const SkinKey *id, uint16_t mask22, uint16_t mask) override;
    void FindType4Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    void FindType6(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22) override;
    void FindType6Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    void FindType7(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                   const SkinKey *id) override;
    void FindType7Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    void FindType8(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                   const SkinKey *id) override;
    void FindType8Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    void FindType9(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                   const SkinKey *id) override;
    void FindType9Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    void FindType10(SkinPtrVector *out, uint8_t ages, uint8_t genders, uint8_t flags, uint16_t mask22,
                    const SkinKey *id) override;
    void FindType10Matching(SkinPtrVector *out, void *predicate, bool append, int32_t max) override;
    bool ApplyEntry(const SkinKey *key, void *target, uint32_t arg) override;
    bool PickRandomType1(SkinKey *out, void *predicate) override;
    bool PickRandomProps(SkinKey *out, void *predicate) override;
    bool AddSource(void *source, bool load) override;
    bool RemoveSource(void *source) override;
    bool HasSource(void *source) override;
    void GetSourceNames(ContentServiceStrings *out) override;
    bool GetCurrentSourceName(cIGZString *out) override;
    bool SelectSource(const char *name) override;
    bool IsSourceOpen(const char *name) override;
    void ClearCurrentSource() override;

    // The map of type 1..10 (null for others), as slot 8 and slot 37 pick it.
    SkinEntryMap *MapOfType(uint32_t type);

    // +0x00 .. +0x1b: cRZSystemService; +0x1c: the cITSSkinCatalog vtable
    bool mLoaded;             // 0x20 set by EnsureLoaded (0xb74910) when it runs; cleared by ClearAll
    uint32_t mLoadedCount;    // 0x24 Reload's answer (written by the loaders; not set by the constructor)
    uint32_t mNextInstance;   // 0x28 the last instance slot 7 handed out
    SkinRefVector mSources;   // 0x2c the catalog's sources
    int32_t mCurrent;         // 0x38 index of the current source, -1 for none
    SkinEntryMap mType4;      // 0x3c 0x2c1fd8a1
    SkinEntryMap mType5;      // 0x44 0x0c1fe246
    SkinEntryMap mType2;      // 0x4c 0x4c158081
    SkinEntryMap mType3;      // 0x54 0x8c1580b5
    SkinEntryMap mType1;      // 0x5c 0xebcf3e27
    SkinGroupMap mType1Groups; // 0x64 the type-1 entries by group bit
    SkinEntryMap mType6;      // 0x6c 0x6c93b566
    SkinEntryMap mType7;      // 0x74 0x8c93bf6c
    SkinEntryMap mType8;      // 0x7c 0x8c93e35c
    SkinEntryMap mType9;      // 0x84 0x0c93e3de
    SkinEntryMap mType10;     // 0x8c 0xd1954460
    SkinIdMap mIds1;          // 0x94 slot 5
    SkinIdMap mIds2;          // 0x9c slot 6
    SkinRefVector mResources; // 0xa4 counted objects held by the loaders (released by ClearAll and Reload)
    cRZRandom mRandom;        // 0xb0 seeded with clock() by the constructor (slots 38 / 39)
    uint8_t mUnusedBC[0xc];   // 0xbc never written
};

static_assert(sizeof(cTSSkinCatalog) == cTSSkinCatalog::kSize, "cTSSkinCatalog size");
static_assert(offsetof(cTSSkinCatalog, mLoaded) == 0x20, "mLoaded");
static_assert(offsetof(cTSSkinCatalog, mLoadedCount) == 0x24, "mLoadedCount");
static_assert(offsetof(cTSSkinCatalog, mNextInstance) == 0x28, "mNextInstance");
static_assert(offsetof(cTSSkinCatalog, mSources) == 0x2c, "mSources");
static_assert(offsetof(cTSSkinCatalog, mCurrent) == 0x38, "mCurrent");
static_assert(offsetof(cTSSkinCatalog, mType4) == 0x3c, "mType4");
static_assert(offsetof(cTSSkinCatalog, mType1) == 0x5c, "mType1");
static_assert(offsetof(cTSSkinCatalog, mType1Groups) == 0x64, "mType1Groups");
static_assert(offsetof(cTSSkinCatalog, mType10) == 0x8c, "mType10");
static_assert(offsetof(cTSSkinCatalog, mIds1) == 0x94, "mIds1");
static_assert(offsetof(cTSSkinCatalog, mIds2) == 0x9c, "mIds2");
static_assert(offsetof(cTSSkinCatalog, mResources) == 0xa4, "mResources");
static_assert(offsetof(cTSSkinCatalog, mRandom) == 0xb0, "mRandom");

// The hooks' entry points: the constructor (placement new of ours) for the creator's call; the creator itself is hooked
// directly (Create).
struct cTSSkinCatalogAdapters {
    static cTSSkinCatalog *__fastcall Construct(void *memory, void *edx); // 0xb72310
};

} // namespace nTSCASCore
