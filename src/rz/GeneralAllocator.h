#pragma once
#include "game/owned.h"

#include <cstddef>
#include <cstdint>

// The game's general-purpose memory allocator (1.18.0.187): Doug Lea's malloc 2.7 internals (boundary
// tags, 10 fast bins, an unsorted bin, 126 small/large bins with a binmap, a top chunk) extended by EA
// with "core blocks" (VirtualAlloc'd regions, reserved large and committed on demand, on a list),
// separately VirtualAlloc'd large blocks, an out-of-memory callback, trimming, and a recursive lock.
// The game has a global instance at RVA 0x10cb098 (malloc/free wrappers at 0x1be3b0/0x1be3a1) and the
// package code creates others. Functions are reimplemented from the binary; dlmalloc 2.7 (public
// domain) is only the map of the algorithm.
//
// Chunks: prev_size at +0, size at +4 (bit 0 PREV_INUSE, bit 1 IS_LARGE_BLOCK, bit 2 IN_FAST_BIN),
// fd at +8, bk at +0xc. Fast bins link through bk. Each core block ends with two fencepost chunks.

struct GeneralAllocatorLock {
    uint8_t criticalSection[0x18]; // CRITICAL_SECTION
    int32_t count;                 // recursion depth, maintained by Lock/Unlock
};

struct GeneralAllocatorCore {
    uint8_t *firstChunk;           // +0x00 (core + 0x27) & ~7
    uint32_t committed;            // +0x04 bytes committed from the start of the core
    uint32_t reserved;             // +0x08 bytes reserved
    uint8_t allocatedWhole;        // +0x0c 1 if obtained committed in one piece (no incremental commit)
    uint8_t canFree;               // +0x0d released at shutdown
    uint8_t canTrim;               // +0x0e may be decommitted by TrimCore
    uint8_t pad0f;
    void *freeFunction;            // +0x10 bool(__cdecl *)(GeneralAllocator *, void *core, uint32_t size, void *user)
    void *freeContext;             // +0x14
    GeneralAllocatorCore *next;    // +0x18
    GeneralAllocatorCore *prev;    // +0x1c
};
static_assert(sizeof(GeneralAllocatorCore) == 0x20, "core block header");

class GeneralAllocator {
public:
    // Constructor (0x1c655d): defaults, then Init with the same arguments.
    GeneralAllocator *Construct(void *core, uint32_t size, uint32_t canFree, uint32_t canTrim, void *freeFunction, void *freeContext);

    // Public operations (each takes and releases the lock).
    void *Malloc(uint32_t bytes, uint32_t flags);              // 0x1c70cc
    void Free(void *mem);                                      // 0x1c6c5a
    void *Realloc(void *mem, uint32_t bytes, uint32_t flags);  // 0x1c7693
    bool Init(void *core, uint32_t size, uint32_t canFree, uint32_t canTrim, void *freeFunction, void *freeContext); // 0x1c6ece
    bool Shutdown();                                           // 0x1c7b90
    void SetOption(uint32_t option, int32_t value);            // 0x1c79b8
    uint32_t TrimCore(uint32_t pad);                           // 0x1c7cb7

    // Internals.
    void *MallocInternal(uint32_t bytes, uint32_t flags);      // 0x1c7104
    void FreeInternal(void *mem);                              // 0x1c6cc1
    void *ReallocInternal(void *mem, uint32_t bytes, uint32_t flags); // 0x1c76cf
    bool AddCore(void *core, uint32_t size, uint32_t canFree, uint32_t canTrim, void *freeFunction, void *freeContext); // 0x1c66ce
    uint8_t *CreateCore(uint32_t size);                        // 0x1c67ee: a new VirtualAlloc'd core; returns its first chunk
    uint8_t *ExtendCore(uint32_t nb);                          // 0x1c6aa0: commit more of a core (or a new one)
    void MallocConsolidate();                                  // 0x1c6981
    GeneralAllocatorCore *FindCore(const void *p);             // 0x1c6c2b
    bool FreeCore(GeneralAllocatorCore *core);                 // 0x1c6c8a
    bool IsAtCoreEnd(uint8_t *chunk);                          // 0x1c6e05
    uint32_t HaveFastChunks() const;                           // 0x1c6e39
    void SetFreeChunk(uint8_t *chunk, uint32_t size);          // 0x1c6954 (`this` unused)
    void LinkCore(GeneralAllocatorCore *core, GeneralAllocatorCore *head); // 0x1c7043 (`this` unused)
    void UnlinkCore(GeneralAllocatorCore *core);               // 0x1c7e38 (`this` unused)
    void *OsAlloc(uint32_t size, uint32_t *allocated);         // 0x1c7060
    void OsFree(void *p, uint32_t size);                       // 0x1c7097 (`this` unused)
    void SetMaxFast(uint32_t bytes);                           // 0x1c794d
    void SetTop(uint8_t *chunk, uint32_t keepOldTop);          // 0x1c7977

    // Free functions (cdecl).
    static uint32_t LargeBinIndex(uint32_t size);              // 0x1c6e44
    static void SetFenceposts(uint8_t *chunk, uint32_t prevInUse); // 0x1c6916
    static void SetFencepostsAt(uint8_t *chunk, uint32_t size); // 0x1c792f
    static uint8_t *InitCoreChunk(uint8_t *chunk, uint32_t size, uint32_t flags); // 0x1c70ab
    static void *ChunkToMem(uint8_t *chunk);                   // 0x1c6e31
    static uint8_t *LargeBlockChunk(uint8_t *footer);          // 0x1c6eb2
    static uint32_t PageSize();                                // 0x1c6eb9
    static GeneralAllocatorLock *CreateLock(GeneralAllocatorLock *lock); // 0x1c763d
    static void DestroyLock(GeneralAllocatorLock *lock);      // 0x1c7654
    static int32_t LockCount(GeneralAllocatorLock *lock);     // 0x1c765f
    static int32_t Lock(GeneralAllocatorLock *lock);          // 0x1c7667
    static int32_t Unlock(GeneralAllocatorLock *lock);        // 0x1c767c
    static void DebugString(const char *text);                // 0x1c7cac
    // 0x1c6970 (__cdecl, two arguments): the default assert callback: DebugString(first argument), then a breakpoint (int3).
    static void DefaultAssert(const char *text, void *context);
    // Layout (offsets from the object).
    uint8_t mInitialized;          // +0x000
    uint8_t pad001[3];
    uint32_t mMaxFast;             // +0x004 bit 0: fast chunks present
    uint8_t *mFastBins[10];        // +0x008 (index = size >> 3, so the array is addressed from `this`)
    uint8_t mBins[0x400];          // +0x030 bin_at(i) = this + 0x28 + 8 * i: fd at +8, bk at +0xc
    uint32_t mBinmap[4];           // +0x430
    uint8_t *mTop;                 // +0x440
    uint8_t *mLastRemainder;       // +0x444
    GeneralAllocatorCore mCores;   // +0x448 list head (next = +0x460, prev = +0x464)
    uint32_t mUnknown468;          // +0x468
    uint8_t mUnknown46c;           // +0x46c
    uint8_t mCanGrow;              // +0x46d new cores and large blocks may be allocated
    uint8_t pad46e[2];
    uint8_t mUnknown470[8];        // +0x470
    uint32_t mUnknown478;          // +0x478 cleared around Realloc's internal free
    uint8_t mUnknown47c[0xc];      // +0x47c
    int32_t mLargeBlockCount;      // +0x488
    uint32_t mLargeBlockBytes;     // +0x48c
    int32_t mMaxLargeBlocks;       // +0x490
    uint32_t mLargeBlockThreshold; // +0x494
    uint8_t mTopDown;              // +0x498 MEM_TOP_DOWN for large blocks
    uint8_t pad499[3];
    uint8_t mLargeBlocks[0x10];    // +0x49c list head chunk (fd +0x4a4, bk +0x4a8)
    uint8_t mUnknown4ac[8];        // +0x4ac
    void *mOutOfMemory;            // +0x4b4 bool(__cdecl *)(GeneralAllocator *, uint32_t bytes, uint32_t bytes + 0x40, void *user)
    void *mOutOfMemoryContext;     // +0x4b8
    uint32_t mOutOfMemoryRetries;  // +0x4bc
    uint8_t mUnknown4c0[0x10];     // +0x4c0 assert/trace callbacks and their contexts
    uint32_t mTrimThreshold;       // +0x4d0
    uint32_t mTopPad;              // +0x4d4
    uint8_t *mInitialTop;          // +0x4d8
    uint32_t mPageSize;            // +0x4dc
    uint32_t mCoreMinimum;         // +0x4e0
    uint32_t mCoreIncrement;       // +0x4e4
    uint8_t mUnknown4e8;           // +0x4e8
    uint8_t pad4e9[3];
    GeneralAllocatorLock *mLock;   // +0x4ec
    GeneralAllocatorLock mLockStorage; // +0x4f0
    uint32_t mUnknown50c;          // +0x50c
    uint32_t mMagic;               // +0x510 0xabcddedd (constructor)
    uint8_t mFillByte;             // +0x514 0xfe (constructor)
    uint8_t pad515[3];
};
static_assert(offsetof(GeneralAllocator, mBinmap) == 0x430, "allocator layout");
static_assert(offsetof(GeneralAllocator, mCores) == 0x448, "allocator layout");
static_assert(offsetof(GeneralAllocator, mLargeBlockCount) == 0x488, "allocator layout");
static_assert(offsetof(GeneralAllocator, mOutOfMemory) == 0x4b4, "allocator layout");
static_assert(offsetof(GeneralAllocator, mTrimThreshold) == 0x4d0, "allocator layout");
static_assert(offsetof(GeneralAllocator, mLock) == 0x4ec, "allocator layout");
static_assert(offsetof(GeneralAllocator, mLockStorage) == 0x4f0, "allocator layout");
static_assert(offsetof(GeneralAllocator, mMagic) == 0x510 && sizeof(GeneralAllocator) == 0x518, "allocator layout");

// The global instance, owned (docs/standalone-shape.md, "Owned statics"): the image's in the hooked game (built by the
// XC static initialiser of src/app/StaticInitHT.cpp, slot 3), standalone/owned_statics.cpp's in an image-free program
// (built there the same way: Construct(nullptr, 0, 1, 0, nullptr, nullptr), Shutdown at exit).
TS2_OWNED_STATIC(GeneralAllocator, 10cb098);

// 0x10cb5b0: the start-up slot (XC slot 4) that points at the general allocator, nothing else reads it. Image-free: the address.
TS2_OWNED_STATIC(void *, 10cb5b0);

// The OS memory functions the allocator uses, replaceable by tests (defaults: the Windows ones).
namespace GeneralAllocatorOs {
using VirtualAllocFn = void *(__stdcall *)(void *, size_t, uint32_t, uint32_t);
using VirtualFreeFn = int(__stdcall *)(void *, size_t, uint32_t);
using VirtualQueryFn = size_t(__stdcall *)(const void *, void *, size_t);
extern VirtualAllocFn virtualAlloc;
extern VirtualFreeFn virtualFree;
extern VirtualQueryFn virtualQuery;
} // namespace GeneralAllocatorOs

// The two default callbacks the constructor stores (the words at +0x4c0 and +0x4c8) as addresses of code: the game's
// functions in the hooked game (hooked to ours), our own in an image-free program (standalone/owned_statics.cpp aliases
// them to GeneralAllocator::DefaultAssert and DebugString). Only their addresses are used.
struct OwnedCode;
TS2_OWNED_STATIC(const OwnedCode, 1c6970);
TS2_OWNED_STATIC(const OwnedCode, 1c7cac);
