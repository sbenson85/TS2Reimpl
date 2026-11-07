#pragma once
// nTSCASCore::cTSBinCatalog: Create-a-Sim's catalog of collections ("bins": cTSBinInfo resources, type 0x6c4f359d, iid
// 0x6c4f3599) and of the fragments that put outfits into them (cTSSimFragment, type 0x0c560f39, iid 0x0c560f33). Service
// id 0x0c4f252f, priority 0xffd23940; class id 0x2c4f295b, cITSBinCatalog iid 0x2c4f2954. 0x98 bytes, /O2. Lane JG.
//
// What it holds: the sources the bins live in (+0x28, counted packages), the bins by group then instance (+0x4c), the
// bins in display order (+0x40, sorted by their slot 22, the sort index, when mDirty), the bin types (+0x54, filled by
// the loader), a queue of pending objects (+0x60 / +0x6c, worked off by OnTick), and, when mIndexed (+0x84), an index of
// the fragments by the bin they belong to (+0x7c: bin key -> fragments, kept in fragment order). With sort mode 1 (+0x88)
// the positions of the bins in the display order are kept by key (+0x8c).
//
// Loading is lazy: most slots first call EnsureLoaded (0xb88e10, 1,282 B, a non-slot member still game code: it reads the
// custom collections folder and the fragments package, then the bin types). Sources are loaded by LoadSource (0xb8d6b0,
// 1,436 B, also game code), which files a source's bins under +0x4c.
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xe8ae0c, 13 slots), +4 cRZUnknownMT (0xe8ae44, 6),
// +0x1c cITSBinCatalog (0xe8ae60, 47). The original's interface slot bodies take `this` as the +0x1c face; ours are members
// (the compiler adjusts).
//
// Standalone shape (docs/standalone-shape.md): the creator 0xb8b270 (registered by address with the COM director, class
// 0x2c4f295b) is the only caller of the constructor 0xb86c50; the vtables are referenced only by the constructor and the
// destructor 0xb87060. Both hooks on: every catalog carries our vtables.
#include "cas/cTSSkinCatalog.h" // SkinKey / SkinKeyLess (the resource key as the maps order it), SkinRef
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSCustomContentSupport.h" // ContentServiceStrings
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace nTSCASCore {

using BinKey = SkinKey;
using BinKeyLess = SkinKeyLess;
// A counted object (a source, a bin, a fragment, a bin type): AddRef / Release through slots 1 / 2.
using BinRef = cRZAutoRefCount<cIGZUnknown>;
// The counted vectors of the object (+0x28 sources, +0x54 bin types): std::allocator, destroyed by 0x14c060.
using BinRefVector = std::vector<BinRef>;
// A counted vector from the allocator service: the display order (+0x40), the index's values, and the slots'
// out-parameters (destroyed by 0x22589e / 0x1f1f70; growth 0xb83e70).
using BinList = std::vector<BinRef, GZServiceAllocator<BinRef>>;
// instance -> bin (node 0x18: links, colour / nil bytes, the id at +0x10, the counted object at +0x14; _Erase 0x1fc790);
// also the pending objects by id (+0x6c).
using BinInstanceMap = std::map<uint32_t, BinRef>;
// group -> its bins (node 0x1c: the group at +0x10, the inner map at +0x14; _Erase 0xb84110).
using BinGroupMap = std::map<uint32_t, BinInstanceMap>;
// A pending object (+0x60): its id and its state (low byte the step OnTick's worker 0xb8cbe0 is at; -1 done).
struct PendingEntry {
    uint32_t id;
    uint32_t state;
};
static_assert(sizeof(PendingEntry) == 8, "pending entry");
using PendingVector = std::vector<PendingEntry>;
// id -> words (node 0x20: the id at +0x10, a std::allocator vector of 4-byte elements at +0x14; _Erase 0x483b90). Only the
// constructor and the destructor of the class touch it.
using IdWordsMap = std::map<uint32_t, std::vector<uint32_t>>;
// bin key -> its fragments in fragment order (node 0x2c: the key at +0x10, a BinList at +0x20; _Erase 0x71fd97).
using FragmentIndex = std::map<BinKey, BinList, BinKeyLess>;
// bin key -> its position in the display order (node 0x24: the key at +0x10, the index at +0x20).
using BinPositionMap = std::map<BinKey, int32_t, BinKeyLess>;
static_assert(sizeof(BinGroupMap) == 8 && sizeof(FragmentIndex) == 8 && sizeof(IdWordsMap) == 8, "MSVC 14.40 map");
static_assert(sizeof(BinList) == 0xc && sizeof(PendingVector) == 0xc, "MSVC 14.40 vector");

// cITSBinCatalog (iid 0x2c4f2954): slots 3-46. "bin" is a cTSBinInfo (its slot 3 writes its key into the caller's buffer
// and answers the buffer, slot 4 sets the key, slot 12 its type mask, slot 22 / 23 get / set its sort index); "fragment" a
// cTSSimFragment (slot 3 its key, 4 sets it, 19 the bin key it belongs to, 21 its outfit key, 24 / 25 set / get its sort
// index, 26 sets its layer, 27 its layer); "list" a caller's BinList, null for the display order; "filter" an object
// whose slot 3 answers whether it takes the object it is handed.
class cITSBinCatalog : public cIGZUnknown {
public:
    virtual uint32_t LoadSources() = 0;                                              // 3 (0xb8d5a0)
    virtual bool AddSource(void *source, bool load) = 0;                             // 4 (0xb88600)
    virtual bool RemoveSource(void *source) = 0;                                     // 5 (0xb903e0)
    virtual bool HasSource(void *source) = 0;                                        // 6 (0xb8c1f0)
    virtual void GetSourceNames(ContentServiceStrings *out) = 0;                     // 7 (0xb8bd00)
    virtual bool AddBin(void *bin, void *source) = 0;                                // 8 (0xb877c0)
    virtual bool SaveBins(uint32_t unused) = 0;                                      // 9 (0xb906c0)
    virtual void ClearAll() = 0;                                                     // 10 (0xb8fdc0)
    virtual bool RemoveBin(void *bin) = 0;                                           // 11 (0xb89630)
    virtual uint32_t BinCount() = 0;                                                 // 12 (0xb8d4e0)
    virtual void GetBinsOfType(BinList *out, uint32_t mask) = 0;                     // 13 (0xb8be30)
    virtual void GetBins(BinList *out, void *filter) = 0;                            // 14 (0xb8beb0)
    virtual void SortBins() = 0;                                                     // 15 (0xb90940)
    virtual bool MoveBin(void *bin, uint32_t direction) = 0;                         // 16 (0xb8c600)
    virtual uint32_t MoveBinAt(uint32_t index, uint32_t direction, BinList *list) = 0; // 17 (0xb8c240)
    virtual bool RenumberBins(BinList *list) = 0;                                    // 18 (0xb89a50)
    virtual void *FindBinType(uint32_t id) = 0;                                      // 19 (0xb8c050)
    virtual bool AddFragment(void *fragment, void *source, bool allowExisting) = 0;  // 20 (0xb87bd0)
    virtual bool SaveFragment(void *fragment) = 0;                                   // 21 (0xb90cd0)
    virtual bool MoveFragment(BinList *list, void *fragment, int32_t delta) = 0;     // 22 (0xb8c640)
    virtual bool MoveFragmentBy(void *fragment, int32_t delta) = 0;                  // 23 (0xb8c7f0)
    virtual bool MoveFragmentBefore(void *fragment) = 0;                             // 24 (0xb8c960)
    virtual bool MoveFragmentAfter(void *fragment) = 0;                              // 25 (0xb8caa0)
    virtual bool RemoveFragment(void *fragment) = 0;                                 // 26 (0xb89820)
    virtual bool RemoveFragmentFor(const BinKey *outfit, const BinKey *bin) = 0;     // 27 (0xb897e0)
    virtual uint32_t FragmentCount(void *bin) = 0;                                   // 28 (0xb8b540)
    virtual void GetFragments(BinList *out, void *bin, void *filter, bool append) = 0; // 29 (0xb8b640)
    virtual void GetAllFragments(BinList *out, void *filter, uint32_t a3) = 0;       // 30 (0xb8c010)
    virtual void GetFragmentsOfType(BinList *out, int32_t type, uint32_t a3) = 0;    // 31 (0xb8bfc0)
    virtual void GetBinFragments(void *bin, BinList *out, void *filter) = 0;         // 32 (0xb8c030)
    virtual bool HasFragmentFor(const BinKey *outfit, const BinKey *bin) = 0;        // 33 (0xb90a50)
    virtual bool BinHasFragment(void *filter, void *bin) = 0;                        // 34 (0xb90ad0)
    virtual bool AnyBinHasFragment(void *filter, uint32_t mask) = 0;                 // 35 (0xb90a70)
    virtual void SetIndexed(bool indexed) = 0;                                       // 36 (0xb89a20)
    virtual void SetSortMode(uint32_t mode) = 0;                                     // 37 (0xb907b0)
    virtual bool RenumberLayer(uint32_t layer, void *bin) = 0;                       // 38 (0xb8aee0)
    virtual bool RenumberFragments(void *bin) = 0;                                   // 39 (0xb8b0c0)
    virtual int32_t NextFragmentIndex(const BinKey *bin) = 0;                        // 40 (0xb8b8d0)
    virtual void ClearPending() = 0;                                                 // 41 (0xb89350)
    virtual bool TakePending(uint32_t id, void **out, bool wait) = 0;                // 42 (0xb89380)
    virtual bool TakeFinished(uint32_t id, void **out, bool wait) = 0;               // 43 (0xb8bbe0)
    virtual bool IsPendingDone(uint32_t id) = 0;                                     // 44 (0xb8d530)
    virtual void QueuePending(uint32_t id, void *object, uint32_t step) = 0;         // 45 (0xb88860)
    virtual bool RemoveOutfit(const BinKey *key) = 0;                                // 46 (0xb89910)
};

class cTSBinCatalog : public rz::cRZSystemService, public cITSBinCatalog {
public:
    static constexpr uint32_t kVTable = 0xe8ae0c;
    static constexpr uint32_t kVTable4 = 0xe8ae44;
    static constexpr uint32_t kCatalogVTable = 0xe8ae60;
    static constexpr uint32_t kSize = 0x98;
    static constexpr uint32_t kServiceId = 0x0c4f252f;
    static constexpr int32_t kPriority = int32_t(0xffd23940);
    static constexpr uint32_t kIid = 0x2c4f2954; // cITSBinCatalog

    cTSBinCatalog();               // 0xb86c50
    // 0xb87060; deleting 0xb87790 (+4 thunk 0xb87771). noexcept as the base's (mCustomSource's destructor is not).
    ~cTSBinCatalog() noexcept override;
    cTSBinCatalog(const cTSBinCatalog &) = delete;
    cTSBinCatalog &operator=(const cTSBinCatalog &) = delete;

    // 0xb8b270 (__cdecl): the COM director's creator: a new catalog (its +0 face; null when new failed).
    static cTSBinCatalog *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xb8dd00; thunks 0xb8dd31 (+4), 0xb8dd39 (+0x1c)
    uint32_t AddRef() override;                              // folded (the service's)
    uint32_t Release() override;                             // folded (the service's)

    // ---- cIGZSystemService ----
    bool Init() override;                   // 0xb8c090
    bool Shutdown() override;               // folded 0xb82190 (shared with cTSSkinCatalog): ClearAll, then true
    bool OnTick(uint32_t counter) override; // 0xb8d510

    // ---- cITSBinCatalog (+0x1c) ----
    uint32_t LoadSources() override;
    bool AddSource(void *source, bool load) override;
    bool RemoveSource(void *source) override;
    bool HasSource(void *source) override;
    void GetSourceNames(ContentServiceStrings *out) override;
    bool AddBin(void *bin, void *source) override;
    bool SaveBins(uint32_t unused) override;
    void ClearAll() override;
    bool RemoveBin(void *bin) override;
    uint32_t BinCount() override;
    void GetBinsOfType(BinList *out, uint32_t mask) override;
    void GetBins(BinList *out, void *filter) override;
    void SortBins() override;
    bool MoveBin(void *bin, uint32_t direction) override;
    uint32_t MoveBinAt(uint32_t index, uint32_t direction, BinList *list) override;
    bool RenumberBins(BinList *list) override;
    void *FindBinType(uint32_t id) override;
    bool AddFragment(void *fragment, void *source, bool allowExisting) override;
    bool SaveFragment(void *fragment) override;
    bool MoveFragment(BinList *list, void *fragment, int32_t delta) override;
    bool MoveFragmentBy(void *fragment, int32_t delta) override;
    bool MoveFragmentBefore(void *fragment) override;
    bool MoveFragmentAfter(void *fragment) override;
    bool RemoveFragment(void *fragment) override;
    bool RemoveFragmentFor(const BinKey *outfit, const BinKey *bin) override;
    uint32_t FragmentCount(void *bin) override;
    void GetFragments(BinList *out, void *bin, void *filter, bool append) override;
    void GetAllFragments(BinList *out, void *filter, uint32_t a3) override;
    void GetFragmentsOfType(BinList *out, int32_t type, uint32_t a3) override;
    void GetBinFragments(void *bin, BinList *out, void *filter) override;
    bool HasFragmentFor(const BinKey *outfit, const BinKey *bin) override;
    bool BinHasFragment(void *filter, void *bin) override;
    bool AnyBinHasFragment(void *filter, uint32_t mask) override;
    void SetIndexed(bool indexed) override;
    void SetSortMode(uint32_t mode) override;
    bool RenumberLayer(uint32_t layer, void *bin) override;
    bool RenumberFragments(void *bin) override;
    int32_t NextFragmentIndex(const BinKey *bin) override;
    void ClearPending() override;
    bool TakePending(uint32_t id, void **out, bool wait) override;
    bool TakeFinished(uint32_t id, void **out, bool wait) override;
    bool IsPendingDone(uint32_t id) override;
    void QueuePending(uint32_t id, void *object, uint32_t step) override;
    bool RemoveOutfit(const BinKey *key) override;

    // 0xb8a850: the bin filed under the key's group and instance (null when none).
    void *FindBin(const BinKey *key);
    // 0xb8ad20: the position in the display order where `bin` would go by its sort index (lower bound), -1 at the end.
    int32_t IndexOfBin(void *bin);
    // 0xb89320: the fragment index emptied.
    void ClearIndex();

    // +0x00 .. +0x1b: cRZSystemService; +0x1c: the cITSBinCatalog vtable
    uint32_t mLoaded;              // 0x20 set by EnsureLoaded (0xb88e10) when it runs; cleared by ClearAll
    uint32_t mBinCount;            // 0x24 LoadSources' answer: bins the loads filed (LoadSource counts them)
    BinRefVector mSources;         // 0x28 the catalog's sources
    BinRef mCustomSource;          // 0x34 the custom collections source (set by EnsureLoaded), counted
    bool mDirty;                   // 0x38 the display order needs sorting (slot 15)
    bool mSourcesLoaded;           // 0x39 every source loaded (LoadSources); cleared by AddSource without a load
    uint8_t mPad3A[2];             // 0x3a never written
    uint32_t mNextInstance;        // 0x3c the last instance AddBin / AddFragment handed out
    BinList mOrder;                // 0x40 the bins in display order
    BinGroupMap mBins;             // 0x4c group -> instance -> bin
    BinRefVector mBinTypes;        // 0x54 the bin types (slot 19 finds one by its slot 12)
    PendingVector mPending;        // 0x60 the pending objects' ids and states, in queue order
    BinInstanceMap mPendingObjects; // 0x6c id -> pending object
    IdWordsMap mIdWords;           // 0x74 only built and destroyed here
    FragmentIndex mIndex;          // 0x7c bin key -> its fragments (when mIndexed)
    bool mIndexed;                 // 0x84 the fragment index is kept (slot 36)
    uint8_t mPad85[3];             // 0x85 never written
    uint32_t mSortMode;            // 0x88 1: the positions (+0x8c) are kept (slot 37)
    BinPositionMap mPositions;     // 0x8c bin key -> its index in mOrder
    uint32_t mUnused94;            // 0x94 never written
};

static_assert(sizeof(BinPositionMap) == 8 && sizeof(BinRefVector) == 0xc, "MSVC 14.40 containers");
static_assert(alignof(cTSBinCatalog) == 4, "cTSBinCatalog alignment");
static_assert(offsetof(cTSBinCatalog, mUnused94) == 0x94, "mUnused94");
static_assert(sizeof(cTSBinCatalog) == cTSBinCatalog::kSize, "cTSBinCatalog size");
static_assert(offsetof(cTSBinCatalog, mLoaded) == 0x20, "mLoaded");
static_assert(offsetof(cTSBinCatalog, mSources) == 0x28, "mSources");
static_assert(offsetof(cTSBinCatalog, mCustomSource) == 0x34, "mCustomSource");
static_assert(offsetof(cTSBinCatalog, mDirty) == 0x38, "mDirty");
static_assert(offsetof(cTSBinCatalog, mSourcesLoaded) == 0x39, "mSourcesLoaded");
static_assert(offsetof(cTSBinCatalog, mNextInstance) == 0x3c, "mNextInstance");
static_assert(offsetof(cTSBinCatalog, mOrder) == 0x40, "mOrder");
static_assert(offsetof(cTSBinCatalog, mBins) == 0x4c, "mBins");
static_assert(offsetof(cTSBinCatalog, mBinTypes) == 0x54, "mBinTypes");
static_assert(offsetof(cTSBinCatalog, mPending) == 0x60, "mPending");
static_assert(offsetof(cTSBinCatalog, mPendingObjects) == 0x6c, "mPendingObjects");
static_assert(offsetof(cTSBinCatalog, mIdWords) == 0x74, "mIdWords");
static_assert(offsetof(cTSBinCatalog, mIndex) == 0x7c, "mIndex");
static_assert(offsetof(cTSBinCatalog, mIndexed) == 0x84, "mIndexed");
static_assert(offsetof(cTSBinCatalog, mSortMode) == 0x88, "mSortMode");
static_assert(offsetof(cTSBinCatalog, mPositions) == 0x8c, "mPositions");

// The hooks' entry points: the constructor (placement new of ours) for the creator's call; the creator itself is hooked
// directly (Create).
struct cTSBinCatalogAdapters {
    static cTSBinCatalog *__fastcall Construct(void *memory, void *edx); // 0xb86c50
};

} // namespace nTSCASCore
