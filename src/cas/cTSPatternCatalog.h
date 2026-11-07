#pragma once
// cTSPatternCatalog: Create-a-Sim's catalog of product patterns (RTTI name; service id 0x8c8cc5ed, priority 0xffd23940;
// cITSPatternCatalog iid 0x4c8cc5ab). 0x5c bytes, /O2. Lane JW (the whole class, standalone shape).
//
// What it holds: the pattern resources (persist resources whose slot 13 answers a kind 0..6) by instance, one map per
// kind: kinds 1 and 2 (resource type 0xcca8e925; the loader asks the resource's own slot 13 again) at +0x2c / +0x34, kind 3
// (0xaca8ea06) at +0x3c, 4 (0x2cb230b8) at +0x44, 5 (0x6d619378) at +0x4c, 6 (0x4dcadb7e) at +0x54. Each value is the
// resource's typed face (the interface the loader asks it for), counted. +0x28 is a counted segment AddPattern files into
// when neither the caller nor the resource manager names one (nothing in the image sets it; the destructor releases it).
//
// Loading is lazy: the getters (slots 7..11) first call EnsureLoaded (0xca0d40), which runs Load (slot 5) until a load has
// marked the catalog loaded: Load asks the resource manager for the keys of the five pattern types (a cPatternFilter) and
// files each resource it can get (0xca2ca0 and the per-map insert helpers 0xca0a60 / 0xca0b00 / 0xca0940 / 0xca0690 /
// 0xca0c20), counting them in mCount.
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xeb51b0, 13 slots), +4 cRZUnknownMT (0xeb51e8, 6),
// +0x1c cITSPatternCatalog (0xeb5204, 12). The original's interface slot bodies take `this` as the +0x1c face; ours are
// members (the compiler adjusts).
//
// Standalone shape (docs/standalone-shape.md): the creator 0xca27f0 (registered by address by cTSCommonCOMDirector) is the
// only caller of the constructor 0xc9f9e0; the vtables are referenced only by the constructor and the destructor 0xca0010.
// Both hooks on: every catalog carries our vtables.
#include "cas/cTSSkinCatalog.h" // SkinKey (the resource key), SkinRef, SkinPtrVector
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSCatalog.h" // cIGZPersistResourceKeyFilter

#include <cstddef>
#include <cstdint>
#include <map>

using PatternKey = nTSCASCore::SkinKey;
// A counted object (a pattern, the default segment): AddRef / Release through slots 1 / 2.
using PatternRef = nTSCASCore::SkinRef;
// instance -> the pattern's typed face (node 0x18: links, colour / nil bytes, the instance at +0x10, the counted object at
// +0x14; _Erase 0x1fc790 releases the values).
using PatternMap = std::map<uint32_t, PatternRef>;
// The getters' out-parameter: a caller's vector of raw pointers from the allocator service (growth 0x38c800).
using PatternList = nTSCASCore::SkinPtrVector;
static_assert(sizeof(PatternMap) == 8, "MSVC 14.40 map");

// cPatternFilter (type descriptor 0x10c7400, vtable 0xeb5238, 8 bytes): the key filter Load hands the resource manager: it
// takes the keys of the five pattern types. Built only by Load (operator new 8, the vtable, count 0, AddRef), so with Load
// ours it is only ever ours.
class cPatternFilter : public cIGZPersistResourceKeyFilter {
public:
    static constexpr uint32_t kInterfaceId = 0x8865cd89;

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, folded 0xb7b970
    uint32_t AddRef() override;                              // slot 1, folded 0x148fb0
    uint32_t Release() override;                             // slot 2, folded 0x72c480
    bool IsKeyIncluded(const uint32_t *key) override;        // slot 3, 0xca29f0
    ~cPatternFilter() override = default;                    // slot 4, 0xca0530 (the scalar deleting destructor)

    uint32_t mRefCount = 0; // +4
};
static_assert(sizeof(cPatternFilter) == 8, "cPatternFilter layout");

// cITSPatternCatalog (iid 0x4c8cc5ab): slots 3-11. A "pattern" is a persist resource (slot 3 its key, 4 sets it, 13 its
// kind 0..6).
class cITSPatternCatalog : public cIGZUnknown {
public:
    // 3 (0xca07b0): files a pattern: its key completed (a new instance from the COM object when it has none, group
    // 0x4c8cc5c0 when it has none, the type of its kind), then saved through the resource manager into `segment`, the
    // segment holding its key, or the default one.
    virtual bool AddPattern(void *pattern, void *segment) = 0;
    // 4 (0xca1280): deletes a pattern's resource and the file of the segment holding it.
    virtual bool RemovePattern(void *pattern) = 0;
    virtual uint32_t Load() = 0;                                  // 5 (0xca2a40): the patterns filed (mCount)
    virtual void ClearAll() = 0;                                  // 6 (0xca4630)
    virtual void GetPatterns(PatternList *out, uint32_t which) = 0; // 7 (0xca3170): 0 kind 1, 1 kind 2, else nothing
    virtual void GetKind3(PatternList *out) = 0;                  // 8 (0xca3240)
    virtual void GetKind4(PatternList *out) = 0;                  // 9 (0xca2f10)
    virtual void GetKind5(PatternList *out) = 0;                  // 10 (0xca2e70)
    virtual void GetKind6(PatternList *out) = 0;                  // 11 (0xca32e0)
};

class cTSPatternCatalog : public rz::cRZSystemService, public cITSPatternCatalog {
public:
    static constexpr uint32_t kVTable = 0xeb51b0;
    static constexpr uint32_t kVTable4 = 0xeb51e8;
    static constexpr uint32_t kCatalogVTable = 0xeb5204;
    static constexpr uint32_t kSize = 0x5c;
    static constexpr uint32_t kServiceId = 0x8c8cc5ed;
    static constexpr int32_t kPriority = int32_t(0xffd23940);
    static constexpr uint32_t kIid = 0x4c8cc5ab; // cITSPatternCatalog

    cTSPatternCatalog();           // 0xc9f9e0
    // 0xca0010; deleting 0xca05e0 (+4 thunk 0xca051a). noexcept as the base's (mDefaultSegment's destructor is not).
    ~cTSPatternCatalog() noexcept override;
    cTSPatternCatalog(const cTSPatternCatalog &) = delete;
    cTSPatternCatalog &operator=(const cTSPatternCatalog &) = delete;

    // 0xca27f0 (__cdecl): the COM director's creator: a new catalog (its +0 face; null when new failed).
    static cTSPatternCatalog *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xca3060; thunks 0xca3091 (+4), 0xca3099 (+0x1c)
    uint32_t AddRef() override;                              // folded 0x1cc320 (the service's)
    uint32_t Release() override;                             // folded 0x1cd7a0 (the service's)

    // ---- cIGZSystemService ----
    bool Init() override;     // 0xca2880
    bool Shutdown() override; // 0xca4970

    // ---- cITSPatternCatalog (+0x1c) ----
    bool AddPattern(void *pattern, void *segment) override;
    bool RemovePattern(void *pattern) override;
    uint32_t Load() override;
    void ClearAll() override;
    void GetPatterns(PatternList *out, uint32_t which) override;
    void GetKind3(PatternList *out) override;
    void GetKind4(PatternList *out) override;
    void GetKind5(PatternList *out) override;
    void GetKind6(PatternList *out) override;

    // 0xca0d40: Load (through the vtable) when not loaded yet.
    void EnsureLoaded();
    // 0xca2ca0 (__cdecl (key, catalog)): files the pattern the resource manager has under `key`, by its kind.
    void LoadKey(const PatternKey *key);
    // 0xca0a60: files a kind 0..2 pattern (`typed` its 0xcca8e918 face) under kind 1 or 2 by its resource's slot 13.
    void InsertKind12(void *typed);
    // 0xca0b00 / 0xca0940 / 0xca0690 / 0xca0c20: files `typed` in `map` (kinds 3..6) by its resource's instance.
    void InsertInto(PatternMap &map, void *typed);

    // +0x00 .. +0x1b: cRZSystemService; +0x1c: the cITSPatternCatalog vtable
    uint32_t mCount;            // 0x20 the patterns the loads filed (zeroed by Load; not set by the constructor)
    bool mLoaded;               // 0x24 set by Load, cleared by ClearAll
    uint8_t mPad25[3];          // 0x25 never written
    PatternRef mDefaultSegment; // 0x28 AddPattern's segment of last resort (never set in the image)
    PatternMap mKind1;          // 0x2c 0xcca8e925, the resource's slot 13 answering 1
    PatternMap mKind2;          // 0x34 0xcca8e925, slot 13 answering 2
    PatternMap mKind3;          // 0x3c 0xaca8ea06
    PatternMap mKind4;          // 0x44 0x2cb230b8
    PatternMap mKind5;          // 0x4c 0x6d619378
    PatternMap mKind6;          // 0x54 0x4dcadb7e
};

static_assert(sizeof(cTSPatternCatalog) == cTSPatternCatalog::kSize, "cTSPatternCatalog size");
static_assert(offsetof(cTSPatternCatalog, mCount) == 0x20, "mCount");
static_assert(offsetof(cTSPatternCatalog, mLoaded) == 0x24, "mLoaded");
static_assert(offsetof(cTSPatternCatalog, mDefaultSegment) == 0x28, "mDefaultSegment");
static_assert(offsetof(cTSPatternCatalog, mKind1) == 0x2c, "mKind1");
static_assert(offsetof(cTSPatternCatalog, mKind2) == 0x34, "mKind2");
static_assert(offsetof(cTSPatternCatalog, mKind3) == 0x3c, "mKind3");
static_assert(offsetof(cTSPatternCatalog, mKind6) == 0x54, "mKind6");

// The hooks' entry points: the constructor (placement new of ours) for the creator's call; the creator itself is hooked
// directly (Create).
struct cTSPatternCatalogAdapters {
    static cTSPatternCatalog *__fastcall Construct(void *memory, void *edx); // 0xc9f9e0
};
