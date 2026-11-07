#include "rz/GeneralAllocator.h"

#include "game/game.h"

#include <windows.h>

#include <cstring>

namespace GeneralAllocatorOs {
VirtualAllocFn virtualAlloc = reinterpret_cast<VirtualAllocFn>(&::VirtualAlloc);
VirtualFreeFn virtualFree = reinterpret_cast<VirtualFreeFn>(&::VirtualFree);
VirtualQueryFn virtualQuery = reinterpret_cast<VirtualQueryFn>(&::VirtualQuery);
} // namespace GeneralAllocatorOs

namespace {

constexpr uint32_t kPrevInUse = 1;
constexpr uint32_t kLargeBlock = 2;
constexpr uint32_t kInFastBin = 4;
constexpr uint32_t kSizeMask = ~7u;
constexpr uint32_t kMinSize = 0x10;
constexpr uint32_t kSmallLimit = 0x200; // sizes below are small bins

uint32_t &PrevSize(uint8_t *p)
{
    return *reinterpret_cast<uint32_t *>(p);
}
uint32_t &Head(uint8_t *p)
{
    return *reinterpret_cast<uint32_t *>(p + 4);
}
uint8_t *&Fd(uint8_t *p)
{
    return *reinterpret_cast<uint8_t **>(p + 8);
}
uint8_t *&Bk(uint8_t *p)
{
    return *reinterpret_cast<uint8_t **>(p + 0xc);
}
uint32_t SizeOf(uint8_t *p)
{
    return Head(p) & kSizeMask;
}

void Unlink(uint8_t *p)
{
    uint8_t *fd = Fd(p), *bk = Bk(p);
    Bk(fd) = bk;
    bk = Bk(p);
    fd = Fd(p);
    Fd(bk) = fd;
}

uint32_t RequestToSize(uint32_t bytes)
{
    uint32_t r = bytes + 0xb;
    return r <= kMinSize ? kMinSize : (r & kSizeMask);
}

uint32_t RoundUp(uint32_t value, uint32_t page)
{
    return (value - 1 + page) & ~(page - 1);
}

using CoreFreeFn = bool(__cdecl *)(GeneralAllocator *, void *, uint32_t, void *);
using OutOfMemoryFn = bool(__cdecl *)(GeneralAllocator *, uint32_t, uint32_t, void *);

} // namespace

// Small helpers ------------------------------------------------------------------------------------

uint8_t *GeneralAllocator_BinAt(GeneralAllocator *a, uint32_t index)
{
    return reinterpret_cast<uint8_t *>(a) + 0x28 + 8 * index;
}

namespace {
uint8_t *Unsorted(GeneralAllocator *a)
{
    return reinterpret_cast<uint8_t *>(a) + 0x30;
}
uint8_t **FastBin(GeneralAllocator *a, uint32_t index)
{
    return reinterpret_cast<uint8_t **>(reinterpret_cast<uint8_t *>(a) + 4 * index);
}
// Appends p at the back (bk side) of the unsorted bin.
void InsertUnsorted(GeneralAllocator *a, uint8_t *p)
{
    uint8_t *u = Unsorted(a);
    uint8_t *last = Bk(u);
    Fd(p) = u;
    Bk(p) = last;
    Bk(u) = p;
    Fd(last) = p;
}
} // namespace

uint32_t GeneralAllocator::LargeBinIndex(uint32_t size)
{
    uint32_t index = 0x7e;
    if ((size >> 18) <= 2) index = (size >> 18) + 0x7c;
    if ((size >> 15) <= 4) index = (size >> 15) + 0x77;
    if ((size >> 12) <= 10) index = (size >> 12) + 0x6e;
    if ((size >> 9) <= 20) index = (size >> 9) + 0x5b;
    if ((size >> 6) <= 32) index = (size >> 6) + 0x38;
    return index;
}

void GeneralAllocator::SetFenceposts(uint8_t *chunk, uint32_t prevInUse)
{
    // The chunk gives up its last 16 bytes to two fencepost chunks (8 bytes each).
    uint32_t reduced = SizeOf(chunk) - 9;
    Head(chunk) = (reduced & kSizeMask) | (Head(chunk) & 7);
    uint32_t size = reduced & kSizeMask;
    PrevSize(chunk + size) = size;
    Head(chunk + size) = prevInUse | 8;
    *reinterpret_cast<uint32_t *>(chunk + size + 8) = 8;
    *reinterpret_cast<uint32_t *>(chunk + size + 0xc) = 9;
}

void GeneralAllocator::SetFencepostsAt(uint8_t *chunk, uint32_t size)
{
    uint8_t *f = chunk + size;
    PrevSize(f) = size;
    Head(f) = 8;
    *reinterpret_cast<uint32_t *>(f + 8) = 8;
    *reinterpret_cast<uint32_t *>(f + 0xc) = 9;
}

uint8_t *GeneralAllocator::InitCoreChunk(uint8_t *chunk, uint32_t size, uint32_t flags)
{
    PrevSize(chunk) = 0;
    Head(chunk) = size | flags;
    SetFenceposts(chunk, 0);
    return chunk;
}

void *GeneralAllocator::ChunkToMem(uint8_t *chunk)
{
    return chunk + 8;
}

uint8_t *GeneralAllocator::LargeBlockChunk(uint8_t *footer)
{
    return footer - PrevSize(footer);
}

uint32_t GeneralAllocator::PageSize()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
}

GeneralAllocatorLock *GeneralAllocator::CreateLock(GeneralAllocatorLock *lock)
{
    lock->count = 0;
    InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(lock->criticalSection));
    return lock;
}

void GeneralAllocator::DestroyLock(GeneralAllocatorLock *lock)
{
    DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(lock->criticalSection));
}

int32_t GeneralAllocator::LockCount(GeneralAllocatorLock *lock)
{
    return lock->count;
}

int32_t GeneralAllocator::Lock(GeneralAllocatorLock *lock)
{
    EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(lock->criticalSection));
    return ++lock->count;
}

int32_t GeneralAllocator::Unlock(GeneralAllocatorLock *lock)
{
    int32_t count = --lock->count;
    LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(lock->criticalSection));
    return count;
}

void GeneralAllocator::DebugString(const char *text)
{
    OutputDebugStringA(text);
}

void GeneralAllocator::DefaultAssert(const char *text, void *)
{
    DebugString(text);
    __debugbreak();
}

uint32_t GeneralAllocator::HaveFastChunks() const
{
    return mMaxFast & 1;
}

void GeneralAllocator::SetFreeChunk(uint8_t *chunk, uint32_t size)
{
    Bk(chunk) = chunk;
    Fd(chunk) = chunk;
    Head(chunk) = size | kPrevInUse;
    PrevSize(chunk + size) = size;
}

void GeneralAllocator::LinkCore(GeneralAllocatorCore *core, GeneralAllocatorCore *head)
{
    core->prev = head;
    core->next = head->next;
    head->next = core;
    core->next->prev = core;
}

void GeneralAllocator::UnlinkCore(GeneralAllocatorCore *core)
{
    core->next->prev = core->prev;
    core->prev->next = core->next;
}

void *GeneralAllocator::OsAlloc(uint32_t size, uint32_t *allocated)
{
    uint32_t type = mTopDown ? (MEM_TOP_DOWN | MEM_COMMIT | MEM_RESERVE) : (MEM_COMMIT | MEM_RESERVE);
    void *p = GeneralAllocatorOs::virtualAlloc(nullptr, size, type, PAGE_READWRITE);
    if (p && allocated) *allocated = size;
    return p;
}

void GeneralAllocator::OsFree(void *p, uint32_t /*size*/)
{
    GeneralAllocatorOs::virtualFree(p, 0, MEM_RELEASE);
}

void GeneralAllocator::SetMaxFast(uint32_t bytes)
{
    uint32_t flag = mMaxFast & 1;
    uint32_t size = bytes ? RequestToSize(bytes) : 8;
    mMaxFast = flag | size;
}

void GeneralAllocator::SetTop(uint8_t *chunk, uint32_t keepOldTop)
{
    uint8_t *old = mTop;
    mTop = chunk;
    Bk(chunk) = chunk;
    Fd(mTop) = Bk(mTop);
    if (old != Unsorted(this) && static_cast<uint8_t>(keepOldTop)) InsertUnsorted(this, old);
}

GeneralAllocatorCore *GeneralAllocator::FindCore(const void *p)
{
    for (GeneralAllocatorCore *c = mCores.next; c != &mCores; c = c->next) {
        if (p >= c && p < reinterpret_cast<uint8_t *>(c) + c->committed) return c;
    }
    return nullptr;
}

bool GeneralAllocator::IsAtCoreEnd(uint8_t *chunk)
{
    if (SizeOf(chunk) >= 0x10) return false;
    GeneralAllocatorCore *core = FindCore(chunk);
    uint8_t *end = reinterpret_cast<uint8_t *>(core) - 0x10 + core->committed;
    return !(chunk < end);
}

bool GeneralAllocator::FreeCore(GeneralAllocatorCore *core)
{
    if (!core->canFree) return false;
    if (core->freeFunction) {
        reinterpret_cast<CoreFreeFn>(core->freeFunction)(this, core, core->reserved, core->freeContext);
    } else {
        // ORIGINAL-BUG: MEM_RELEASE with a non-zero size fails, so the core is leaked.
        GeneralAllocatorOs::virtualFree(core, core->reserved, MEM_RELEASE);
    }
    return true;
}

// Cores --------------------------------------------------------------------------------------------

bool GeneralAllocator::AddCore(void *coreMemory, uint32_t size, uint32_t canFree, uint32_t canTrim, void *freeFunction, void *freeContext)
{
    GeneralAllocatorLock *lock = mLock;
    if (lock) Lock(lock);
    bool ok;
    auto *core = static_cast<GeneralAllocatorCore *>(coreMemory);
    if (core) {
        if (size < 0x40) {
            ok = false;
        } else {
            if (size % mPageSize) size &= 0u - mPageSize;
            core->reserved = size;
            core->firstChunk = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(core) + 0x27) & kSizeMask);
            core->canFree = static_cast<uint8_t>(canFree);
            core->canTrim = static_cast<uint8_t>(canTrim);
            core->freeContext = freeContext;
            core->committed = size;
            core->allocatedWhole = 0;
            core->freeFunction = freeFunction;
            LinkCore(core, &mCores);
            if (!freeFunction) {
                MEMORY_BASIC_INFORMATION info;
                GeneralAllocatorOs::virtualQuery(core, &info, sizeof(info));
                if (info.State == MEM_RESERVE) core->reserved = 0;
            }
            uint8_t *first = core->firstChunk;
            uint8_t *chunk = InitCoreChunk(first, reinterpret_cast<uint8_t *>(core) - first + size, 1);
            SetTop(chunk, mTop != Unsorted(this));
            ok = true;
        }
    } else if (size) {
        uint32_t page = mPageSize;
        ok = CreateCore((size - 1 + page) & (0u - page)) != nullptr;
    } else {
        ok = false;
    }
    if (lock) Unlock(lock);
    return ok;
}

uint8_t *GeneralAllocator::CreateCore(uint32_t size)
{
    uint32_t mode = 1; // 3 when the core had to be allocated committed in one piece
    uint32_t page = mPageSize;
    uint32_t mask = ~(page - 1);
    uint32_t wanted = (size - 1 + page) & mask;
    uint32_t commit = wanted;
    uint32_t reserve = wanted;
    if (wanted < mCoreMinimum) {
        commit = (page - 1 + mCoreMinimum) & mask;
        reserve = commit;
    }
    reserve = (reserve + 0xffff) & 0xffff0000;
    uint32_t committedSize = commit;
    uint8_t *core = nullptr;
    void *base = GeneralAllocatorOs::virtualAlloc(nullptr, reserve, MEM_RESERVE, PAGE_READWRITE);
    if (base) {
        uint32_t increment = mCoreIncrement;
        if (wanted < increment) wanted = (mPageSize - 1 + increment) & (0u - mPageSize);
        commit = wanted <= reserve ? wanted : reserve;
        core = static_cast<uint8_t *>(GeneralAllocatorOs::virtualAlloc(base, commit, MEM_COMMIT, PAGE_READWRITE));
        committedSize = commit;
    }
    if (!core) {
        // The reservation (if any) is left behind, as in the original.
        uint32_t allocated = committedSize;
        core = static_cast<uint8_t *>(OsAlloc(commit, &allocated));
        if (!core) return nullptr;
        commit = allocated;
        reserve = commit;
        mode = 3;
        committedSize = reserve;
    }
    auto *header = reinterpret_cast<GeneralAllocatorCore *>(core);
    header->committed = commit;
    header->firstChunk = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(core) + 0x27) & kSizeMask);
    header->reserved = commit;
    header->allocatedWhole = static_cast<uint8_t>(mode >> 1);
    bool notFirst = mCores.prev != &mCores;
    header->freeFunction = nullptr;
    header->freeContext = nullptr;
    header->canFree = notFirst;
    header->canTrim = notFirst;
    LinkCore(header, &mCores);
    uint32_t usable = committedSize - static_cast<uint32_t>(header->firstChunk - core);
    header->reserved = reserve;
    return InitCoreChunk(header->firstChunk, usable, mode);
}

uint8_t *GeneralAllocator::ExtendCore(uint32_t nb)
{
    bool newTop = false;
    uint8_t *chunk = nullptr;
    uint32_t size = 0;
    GeneralAllocatorCore *core = mCores.next;
    if (core != &mCores) {
        uint32_t room;
        for (;;) {
            room = core->reserved - core->committed;
            if (nb <= room) break;
            core = core->next;
            if (core == &mCores) goto noCore;
        }
        {
            // ORIGINAL-BUG: a request bigger than the core increment is committed at its exact,
            // unaligned size, so `committed` stops being page-aligned; a later TrimCore decommits whole
            // pages including the one its new fencepost is written to, and faults. (Needs a request
            // above the increment, 4 MB by default.)
            uint32_t want = nb < mCoreIncrement ? mCoreIncrement : nb;
            uint32_t grow = want <= room ? want : room;
            // ORIGINAL-BUG: a failed commit only marks the core full; the code below still extends the
            // chunk into the uncommitted pages and writes fenceposts there, which faults.
            if (!GeneralAllocatorOs::virtualAlloc(reinterpret_cast<uint8_t *>(core) + core->committed, grow, MEM_COMMIT, PAGE_READWRITE))
                core->reserved = core->committed;
            if (core == &mCores) goto noCore;
            if (core->committed != 0) {
                uint8_t *end = reinterpret_cast<uint8_t *>(core) + core->committed;
                chunk = end - 0x10; // the first fencepost
                if (Head(chunk) & kPrevInUse) {
                    size = grow;
                    Head(chunk) = grow | kPrevInUse;
                } else {
                    chunk -= PrevSize(end - 0x10);
                    Unlink(chunk);
                    size = PrevSize(end - 0x10) + grow;
                    Head(chunk) = size | kPrevInUse;
                    newTop = chunk == mTop;
                }
                SetFencepostsAt(chunk, size);
            } else {
                uint8_t *first = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(core) + 0x27) & kSizeMask);
                core->firstChunk = first;
                chunk = InitCoreChunk(first, static_cast<uint32_t>(reinterpret_cast<uint8_t *>(core) - first) + grow, 1);
                size = grow;
                SetTop(chunk, 1);
                newTop = true;
            }
            core->committed += grow;
            if (chunk) goto haveChunk;
        }
    }
noCore:
    if (mOutOfMemory) return chunk;
    // ORIGINAL-BUG: without an out-of-memory callback a failed CreateCore isn't checked: SetTop(null)
    // and SizeOf(null) fault.
    chunk = CreateCore(nb);
    SetTop(chunk, 1);
    size = SizeOf(chunk);
    newTop = true;
haveChunk:
    if (size > nb) {
        uint32_t rest = size - nb;
        uint8_t *remainder = chunk + nb;
        Head(chunk) = nb | kPrevInUse;
        PrevSize(remainder) = nb;
        Head(remainder) = rest;
        PrevSize(remainder + rest) = rest;
        if (newTop) SetTop(remainder, 0);
        else InsertUnsorted(this, remainder);
    }
    return chunk;
}

// Init / shutdown / options -------------------------------------------------------------------------

GeneralAllocator *GeneralAllocator::Construct(void *core, uint32_t size, uint32_t canFree, uint32_t canTrim, void *freeFunction, void *freeContext)
{
    auto *bytes = reinterpret_cast<uint8_t *>(this);
    mInitialized = 0;
    mMaxFast = 0;
    std::memset(mFastBins, 0, sizeof(mFastBins));
    std::memset(mBins, 0, sizeof(mBins));
    std::memset(mBinmap, 0, sizeof(mBinmap));
    mTop = nullptr;
    mLastRemainder = nullptr;
    std::memset(&mCores, 0, sizeof(mCores));
    mUnknown468 = 0;
    *reinterpret_cast<uint16_t *>(bytes + 0x46c) = 0x100; // mUnknown46c = 0, mCanGrow = 1
    *reinterpret_cast<uint32_t *>(bytes + 0x470) = 0;
    *reinterpret_cast<uint16_t *>(bytes + 0x474) = 0xa09;
    mUnknown478 = 0;
    *reinterpret_cast<uint32_t *>(bytes + 0x47c) = 0;
    *reinterpret_cast<uint32_t *>(bytes + 0x480) = 0;
    bytes[0x484] = 0;
    mLargeBlockCount = 0;
    mLargeBlockBytes = 0;
    mMaxLargeBlocks = 0;
    mLargeBlockThreshold = 0;
    mTopDown = 0;
    std::memset(mLargeBlocks, 0, sizeof(mLargeBlocks));
    *reinterpret_cast<uint32_t *>(bytes + 0x4ac) = 0;
    *reinterpret_cast<uint32_t *>(bytes + 0x4b0) = 0;
    mOutOfMemory = nullptr;
    mOutOfMemoryContext = nullptr;
    mOutOfMemoryRetries = 0x100;
    mTrimThreshold = 0;
    mTopPad = 0;
    mInitialTop = nullptr;
    mPageSize = 0x1000;
    mCoreMinimum = 0x1000000;
    mCoreIncrement = 0x400000;
    mUnknown4e8 = 0;
    mLock = nullptr;
    std::memset(&mLockStorage, 0, sizeof(mLockStorage));
    mUnknown50c = 0;
    mMagic = 0xabcddedd;
    mFillByte = 0xfe;
    // Default assert and trace callbacks (game functions) with this allocator as their context.
    *reinterpret_cast<void **>(bytes + 0x4c0) = const_cast<OwnedCode *>(&ts2_rva_1c6970);
    *reinterpret_cast<void **>(bytes + 0x4c4) = this;
    *reinterpret_cast<void **>(bytes + 0x4c8) = const_cast<OwnedCode *>(&ts2_rva_1c7cac);
    *reinterpret_cast<void **>(bytes + 0x4cc) = this;
    Init(core, size, canFree, canTrim, freeFunction, freeContext);
    return this;
}

bool GeneralAllocator::Init(void *core, uint32_t size, uint32_t canFree, uint32_t canTrim, void *freeFunction, void *freeContext)
{
    if (!mInitialized) {
        mInitialized = 1;
        SetOption(0, 1);
        GeneralAllocatorLock *lock = mLock;
        if (lock) Lock(lock);
        mMaxFast = 0x40;
        std::memset(mFastBins, 0, sizeof(mFastBins));
        std::memset(mBins, 0, sizeof(mBins));
        for (uint32_t i = 0; i < 127; ++i) {
            uint8_t *bin = Unsorted(this) + 8 * i;
            Fd(bin) = bin;
            Bk(bin) = bin;
        }
        std::memset(mBinmap, 0, sizeof(mBinmap));
        mLastRemainder = nullptr;
        mTop = Unsorted(this);
        std::memset(&mCores, 0, 0x18);
        mUnknown468 = 0;
        mUnknown46c = 0;
        mCores.next = &mCores;
        mCores.prev = &mCores;
        SetMaxFast(0x40);
        mLargeBlockCount = 0;
        mLargeBlockBytes = 0;
        mTopDown = 0;
        mMaxLargeBlocks = 0x10000;
        mLargeBlockThreshold = 0x20000;
        uint8_t *large = mLargeBlocks;
        PrevSize(large) = 0;
        Head(large) = 0;
        Fd(large) = large;
        Bk(large) = large;
        mTrimThreshold = 0x20000;
        mTopPad = 0x10000;
        mInitialTop = mTop;
        mPageSize = PageSize();
        if (lock) Unlock(lock);
    }
    if (core || size) AddCore(core, size, canFree, canTrim, freeFunction, freeContext);
    return true;
}

void GeneralAllocator::SetOption(uint32_t option, int32_t value)
{
    switch (option) {
    case 0: // locking
        if (value) {
            if (!mLock) mLock = CreateLock(&mLockStorage);
        } else if (mLock) {
            Lock(mLock);
            GeneralAllocatorLock *lock = mLock;
            mLock = nullptr;
            Unlock(lock);
            DestroyLock(lock);
        }
        break;
    case 1:
        if (value) {
            if (!mUnknown46c && !mUnknown468) mUnknown468 = 1;
        } else {
            mUnknown468 = 0;
        }
        break;
    case 2: mCanGrow = value != 0; break;
    case 3: {
        uint32_t v;
        if (!value) {
            v = 0x1000000;
        } else {
            uint32_t page = mPageSize;
            v = static_cast<uint32_t>(value) > page ? static_cast<uint32_t>(value) : page;
            mCoreMinimum = v;
            v = (v - 1 + page) & (0u - page);
        }
        mCoreMinimum = v;
        break;
    }
    case 4: {
        if (!value) {
            mCoreIncrement = 0x400000;
            break;
        }
        uint32_t page = mPageSize;
        uint32_t v = static_cast<uint32_t>(value) > page ? static_cast<uint32_t>(value) : page;
        mCoreIncrement = (v - 1 + page) & (0u - page);
        if (mCoreIncrement > mCoreMinimum) mCoreIncrement = mCoreMinimum;
        break;
    }
    case 5:
        MallocConsolidate();
        SetMaxFast(static_cast<uint32_t>(value));
        break;
    case 6: mTrimThreshold = static_cast<uint32_t>(value); break;
    case 7: mTopPad = static_cast<uint32_t>(value); break;
    case 8: mLargeBlockThreshold = static_cast<uint32_t>(value); break;
    case 9: mMaxLargeBlocks = value; break;
    case 10: mTopDown = value != 0; break;
    case 11: mUnknown4e8 = value != 0; break;
    case 12: mOutOfMemoryRetries = static_cast<uint32_t>(value); break;
    default: break;
    }
}

bool GeneralAllocator::Shutdown()
{
    if (mLock) Lock(mLock);
    if (mInitialized) {
        mInitialized = 0;
        if (HaveFastChunks()) MallocConsolidate();
        uint8_t *large = mLargeBlocks;
        while (Bk(large) != large) FreeInternal(ChunkToMem(LargeBlockChunk(Bk(large))));
        while (mCores.prev != &mCores) {
            GeneralAllocatorCore *core = mCores.prev;
            UnlinkCore(core);
            FreeCore(core);
        }
        mTop = Unsorted(this);
        mUnknown468 = 0;
        mUnknown46c = 0;
    }
    if (mLock) {
        GeneralAllocatorLock *lock = mLock;
        mLock = nullptr;
        Unlock(lock);
        DestroyLock(lock);
    }
    return true;
}

// Free ---------------------------------------------------------------------------------------------

void GeneralAllocator::Free(void *mem)
{
    GeneralAllocatorLock *lock = mLock;
    if (lock) Lock(lock);
    FreeInternal(mem);
    if (lock) Unlock(lock);
}

void GeneralAllocator::FreeInternal(void *mem)
{
    if (!mem) return;
    uint8_t *p = static_cast<uint8_t *>(mem) - 8;
    uint32_t size = SizeOf(p);
    uint32_t maxFast = mMaxFast;
    if (size <= maxFast) {
        uint32_t index = size >> 3;
        mMaxFast = maxFast | 1;
        Head(p) |= kInFastBin;
        Bk(p) = *FastBin(this, index);
        *FastBin(this, index) = p;
        return;
    }
    uint8_t *next = p + size;
    if (Head(p) & kLargeBlock) {
        uint32_t offset = PrevSize(p);
        p -= offset;
        uint32_t total = offset + 0x10 + size;
        Unlink(next); // the block's footer, on the large-block list
        mLargeBlockBytes -= total;
        --mLargeBlockCount;
        OsFree(p, total);
        return;
    }
    uint32_t nextSize = SizeOf(next);
    if (!(Head(p) & kPrevInUse)) {
        size += PrevSize(p);
        p -= PrevSize(p);
        Head(p) = size | kPrevInUse;
        PrevSize(next) = size;
        Unlink(p);
    }
    if (Head(next + nextSize) & kPrevInUse) {
        Head(next) &= ~kPrevInUse;
        PrevSize(next) = size;
    } else {
        Unlink(next);
        size += nextSize;
        Head(p) = size | kPrevInUse;
        PrevSize(p + size) = size;
    }
    if (p != mTop && next != mTop) {
        InsertUnsorted(this, p);
    } else {
        mTop = p;
        SetFreeChunk(p, size);
    }
    if (size >= 0x10000 && IsAtCoreEnd(p + size) && size >= mTrimThreshold) TrimCore(mTopPad);
}

void GeneralAllocator::MallocConsolidate()
{
    if (mMaxFast == 0) {
        Init(nullptr, 0, 1, 0, nullptr, nullptr);
        return;
    }
    // ORIGINAL-BUG: after SetOption(5, 0) the limit is 8, whose bin lies before the first fast bin (2),
    // so the walk below never meets `last` and runs on through the bins, corrupting them.
    uint8_t **last = FastBin(this, mMaxFast >> 3);
    for (uint8_t **bin = FastBin(this, 2);; ++bin) {
        uint8_t *p = *bin;
        if (p) {
            *bin = nullptr;
            do {
                Head(p) &= ~kInFastBin;
                uint32_t size = SizeOf(p);
                uint8_t *nextInBin = Bk(p);
                uint8_t *next = p + size;
                uint32_t nextSize = SizeOf(next);
                if (!(Head(p) & kPrevInUse)) {
                    size += PrevSize(p);
                    p -= PrevSize(p);
                    Head(p) = size | kPrevInUse;
                    PrevSize(next) = size;
                    Unlink(p);
                }
                if (Head(next + nextSize) & kPrevInUse) {
                    Head(next) &= ~kPrevInUse;
                    PrevSize(next) = size;
                } else {
                    Unlink(next);
                    size += nextSize;
                    Head(p) = size | kPrevInUse;
                    PrevSize(p + size) = size;
                }
                if (p != mTop && next != mTop) {
                    InsertUnsorted(this, p);
                } else {
                    mTop = p;
                    SetFreeChunk(p, size);
                }
                p = nextInBin;
            } while (p);
        }
        if (bin == last) break;
    }
    mMaxFast &= ~1u;
}

uint32_t GeneralAllocator::TrimCore(uint32_t pad)
{
    GeneralAllocatorLock *lock = mLock;
    if (lock) Lock(lock);
    uint32_t released = 0;
    for (GeneralAllocatorCore *core = mCores.next; core != &mCores; core = core->next) {
        if (!core->canFree) continue;
        uint8_t *chunk = reinterpret_cast<uint8_t *>(core) + core->committed - 0x10;
        if (Head(chunk) & kPrevInUse) continue;
        chunk -= PrevSize(chunk);
        if (!core->canTrim) continue;
        uint32_t size = SizeOf(chunk);
        if (size <= pad + 0x40) continue;
        uint32_t unit = ((mCoreIncrement >> 1) + 0xffff) & 0xffff0000;
        uint32_t excess = size - (pad + 0x40);
        uint32_t amount = unit;
        uint32_t next = unit + unit;
        while (next < excess) {
            next += unit;
            amount += unit;
        }
        if (amount >= excess) continue;
        if (!GeneralAllocatorOs::virtualFree(reinterpret_cast<uint8_t *>(core) + core->committed - amount, amount, MEM_DECOMMIT)) continue;
        released += amount;
        core->committed -= amount;
        if (chunk != mTop) Unlink(chunk);
        Head(chunk) = (size - amount + 0x10) | (Head(chunk) & 7);
        SetFenceposts(chunk, 0);
        if (chunk != mTop) InsertUnsorted(this, chunk);
    }
    if (lock) Unlock(lock);
    return released;
}

// Malloc -------------------------------------------------------------------------------------------

void *GeneralAllocator::Malloc(uint32_t bytes, uint32_t flags)
{
    GeneralAllocatorLock *lock = mLock;
    if (lock) Lock(lock);
    void *result = MallocInternal(bytes, flags);
    if (lock) Unlock(lock);
    return result;
}

void *GeneralAllocator::MallocInternal(uint32_t bytes, uint32_t /*flags*/)
{
    uint32_t retries;
    void *result;
    uint32_t nb, index, binIndex, minSplit;
    uint8_t *unsorted;

restart:
    retries = 0;
retry:
    result = nullptr;
    nb = RequestToSize(bytes);
    index = nb >> 3;
    if (nb <= mMaxFast) {
        uint8_t *p = *FastBin(this, index);
        if (p) {
            *FastBin(this, index) = Bk(p);
            Head(p) &= ~kInFastBin;
            return p + 8;
        }
    }
    if (nb < kSmallLimit) {
        binIndex = index;
        uint8_t *bin = GeneralAllocator_BinAt(this, index);
        uint8_t *victim = Fd(bin);
        if (victim != bin) {
            if (!victim) {
                Init(nullptr, 0, 1, 0, nullptr, nullptr);
                goto restart;
            }
            Head(victim + nb) |= kPrevInUse;
            Unlink(victim);
            return victim + 8;
        }
    } else {
        binIndex = LargeBinIndex(nb);
        if (mMaxFast & 1) MallocConsolidate();
    }
    minSplit = nb + kMinSize;

    for (;;) {
        unsorted = Unsorted(this);
        for (uint8_t *victim = Fd(unsorted); victim != unsorted; victim = Fd(unsorted)) {
            uint32_t size = SizeOf(victim);
            uint8_t *after = Fd(victim);
            if (nb < kSmallLimit && victim == mLastRemainder && after == unsorted && size > minSplit) {
                uint8_t *rest = victim + nb;
                uint32_t restSize = size - nb;
                Bk(unsorted) = rest;
                Fd(unsorted) = rest;
                mLastRemainder = rest;
                Bk(rest) = unsorted;
                Fd(rest) = unsorted;
                Head(victim) = nb | kPrevInUse;
                Head(rest) = restSize | kPrevInUse;
                PrevSize(rest + restSize) = restSize;
                return victim + 8;
            }
            Fd(unsorted) = after;
            Bk(after) = unsorted;
            if (size == nb) {
                Head(victim + size) |= kPrevInUse;
                return victim + 8;
            }
            uint8_t *before, *beyond; // victim goes between `before` (its bk) and `beyond` (its fd)
            uint32_t victimIndex;
            if (size < kSmallLimit) {
                victimIndex = size >> 3;
                beyond = GeneralAllocator_BinAt(this, victimIndex);
                before = Bk(beyond);
            } else {
                victimIndex = LargeBinIndex(size);
                uint8_t *bin = GeneralAllocator_BinAt(this, victimIndex);
                uint8_t *walk = Bk(bin);
                beyond = bin;
                before = walk;
                if (bin != walk) {
                    uint8_t *first = Fd(bin);
                    if (size < Head(first)) { // raw size field, flags included
                        before = bin;
                        beyond = first;
                    } else {
                        if (size < SizeOf(walk)) {
                            do {
                                walk = Bk(walk);
                            } while (size < SizeOf(walk));
                        }
                        beyond = Fd(walk);
                        before = walk;
                    }
                }
            }
            mBinmap[victimIndex >> 5] |= 1u << (victimIndex & 31);
            Fd(victim) = beyond;
            Bk(victim) = before;
            Fd(before) = victim;
            Bk(beyond) = victim;
        }

        if (nb >= kSmallLimit) {
            uint8_t *bin = GeneralAllocator_BinAt(this, binIndex);
            for (uint8_t *victim = Fd(bin); victim != bin; victim = Fd(victim)) {
                uint32_t size = SizeOf(victim);
                if (size >= nb) {
                    Unlink(victim);
                    uint32_t restSize = size - nb;
                    if (restSize < kMinSize) {
                        Head(victim + size) |= kPrevInUse;
                        return victim + 8;
                    }
                    uint8_t *rest = victim + nb;
                    Bk(unsorted) = rest;
                    Fd(unsorted) = rest;
                    Bk(rest) = unsorted;
                    Fd(rest) = unsorted;
                    Head(victim) = nb | kPrevInUse;
                    Head(rest) = restSize | kPrevInUse;
                    PrevSize(rest + restSize) = restSize;
                    return victim + 8;
                }
            }
        }

        // Binmap search for the next non-empty bin.
        {
            uint32_t i = binIndex + 1;
            uint32_t block = i >> 5;
            uint32_t bit = 1u << (i & 31);
            uint8_t *bin = GeneralAllocator_BinAt(this, i);
            for (;;) {
                uint32_t map = mBinmap[block];
                if (bit > map || bit == 0) {
                    do {
                        if (++block >= 4) goto useTop;
                        map = mBinmap[block];
                    } while (map == 0);
                    bin = GeneralAllocator_BinAt(this, block << 5);
                    bit = 1;
                }
                while (!(map & bit)) {
                    bin += 8;
                    bit += bit;
                }
                uint8_t *victim = Fd(bin);
                if (victim == bin) {
                    mBinmap[block] = map & ~bit;
                    bit += bit;
                    bin += 8;
                    continue;
                }
                uint32_t size = SizeOf(victim);
                uint32_t restSize = size - nb;
                Unlink(victim);
                if (restSize < kMinSize) {
                    Head(victim + size) |= kPrevInUse;
                    return victim + 8;
                }
                uint8_t *rest = victim + nb;
                Bk(unsorted) = rest;
                Fd(unsorted) = rest;
                Bk(rest) = unsorted;
                Fd(rest) = unsorted;
                if (nb < kSmallLimit) mLastRemainder = rest;
                Head(victim) = nb | kPrevInUse;
                Head(rest) = restSize | kPrevInUse;
                PrevSize(rest + restSize) = restSize;
                return victim + 8;
            }
        }

    useTop:
        {
            uint8_t *victim = mTop;
            minSplit = nb + kMinSize;
            uint32_t size = SizeOf(victim);
            if (size >= minSplit) {
                uint8_t *rest = victim + nb;
                Head(victim) = nb | kPrevInUse;
                Head(rest) = (size - nb) | kPrevInUse;
                mTop = rest;
                SetFreeChunk(rest, size - nb);
                return victim + 8;
            }
            if (!(mMaxFast & 1)) break;
            MallocConsolidate();
            binIndex = index;
        }
    }

    // Out of free memory: a large block, a grown core, or the out-of-memory callback.
    if (mCanGrow && nb >= mLargeBlockThreshold && mLargeBlockCount < mMaxLargeBlocks) {
        uint32_t blockSize = (bytes + 0x101e) & 0xfffff000;
        if (blockSize >= bytes) {
            uint8_t *block = static_cast<uint8_t *>(OsAlloc(blockSize, &blockSize));
            if (block) {
                mLargeBlockBytes += blockSize;
                ++mLargeBlockCount;
                uint32_t footer = blockSize - 0x10;
                uint32_t size;
                uint32_t misalign = reinterpret_cast<uintptr_t>(block) & 7;
                if (misalign) {
                    uint32_t adjust = 8 - misalign;
                    block += adjust;
                    footer -= adjust;
                    size = footer - adjust;
                    PrevSize(block) = adjust;
                } else {
                    PrevSize(block) = 0;
                    size = footer;
                }
                Head(block) = size | kLargeBlock;
                uint8_t *node = block + footer;
                PrevSize(node) = footer;
                Head(node) = 0x13;
                uint8_t *list = mLargeBlocks;
                uint8_t *last = Bk(list);
                Fd(node) = list;
                Bk(node) = last;
                Bk(list) = node;
                Fd(last) = node;
                return block + 8;
            }
        }
    }
    if (mCanGrow) {
        uint8_t *chunk = ExtendCore(nb);
        if (chunk) {
            Head(chunk + SizeOf(chunk)) |= kPrevInUse;
            result = chunk + 8;
            if (result) return result;
        }
    }
    if (!mOutOfMemory) return result;
    if (++retries >= mOutOfMemoryRetries) return result;
    {
        int32_t held = LockCount(mLock);
        for (int32_t i = held; i > 0; --i) Unlock(mLock);
        bool again = reinterpret_cast<OutOfMemoryFn>(mOutOfMemory)(this, bytes, bytes + 0x40, mOutOfMemoryContext);
        for (int32_t i = held; i > 0; --i) Lock(mLock);
        if (!again) return result;
    }
    goto retry;
}

// Realloc ------------------------------------------------------------------------------------------

void *GeneralAllocator::Realloc(void *mem, uint32_t bytes, uint32_t flags)
{
    GeneralAllocatorLock *lock = mLock;
    if (lock) Lock(lock);
    void *result = ReallocInternal(mem, bytes, flags);
    if (lock) Unlock(lock);
    return result;
}

void *GeneralAllocator::ReallocInternal(void *mem, uint32_t bytes, uint32_t flags)
{
    if (!mem) return MallocInternal(bytes, flags);
    uint8_t *m = static_cast<uint8_t *>(mem);
    uint32_t nb = RequestToSize(bytes);
    uint32_t head = *reinterpret_cast<uint32_t *>(m - 4);
    uint32_t oldSize = head & kSizeMask;
    uint32_t newSize;

    if (head & kLargeBlock) {
        if (nb + 4 <= oldSize) return mem;
        void *fresh = MallocInternal(nb - 7, flags);
        if (fresh) {
            std::memcpy(fresh, mem, oldSize - 8);
            FreeInternal(mem);
        }
        return fresh;
    }

    if (nb <= oldSize) {
        newSize = oldSize;
    } else {
        uint8_t *next = m - 8 + oldSize;
        if (next == mTop) {
            uint32_t total = SizeOf(next) + oldSize;
            if (total >= nb + kMinSize) {
                *reinterpret_cast<uint32_t *>(m - 4) = (head & 7) | nb;
                mTop = m - 8 + nb;
                SetFreeChunk(mTop, total - nb);
                return mem;
            }
        } else {
            uint32_t nextHead = Head(next);
            if (!(Head(next + (nextHead & kSizeMask)) & kPrevInUse) && (nextHead & kSizeMask) + oldSize >= nb) {
                newSize = (nextHead & kSizeMask) + oldSize;
                Unlink(next);
                goto split;
            }
        }
        // Allocate, copy, free.
        void *freshMem = MallocInternal(nb - 7, flags);
        if (!freshMem) return nullptr;
        uint8_t *fresh = static_cast<uint8_t *>(freshMem) - 8;
        if (fresh == m - 8 + oldSize) {
            newSize = SizeOf(fresh) + oldSize;
            goto split;
        }
        uint32_t copy = oldSize - 4;
        if (copy <= 0x20) {
            uint32_t words = copy >> 2;
            if (words - 1 <= 7) {
                auto *d = static_cast<uint32_t *>(freshMem);
                auto *s = reinterpret_cast<const uint32_t *>(m);
                for (uint32_t i = 0; i < words; ++i) d[i] = s[i];
            }
        } else {
            std::memcpy(freshMem, mem, copy);
        }
        FreeInternal(mem);
        return freshMem;
    }

split:
    {
        uint32_t flagsBits = *reinterpret_cast<uint32_t *>(m - 4) & 7;
        uint32_t rest = newSize - nb;
        if (rest < kMinSize) {
            *reinterpret_cast<uint32_t *>(m - 4) = flagsBits | newSize;
            *reinterpret_cast<uint32_t *>(m - 4 + newSize) |= kPrevInUse;
            return mem;
        }
        *reinterpret_cast<uint32_t *>(m - 4) = flagsBits | nb;
        uint8_t *restMem = m + nb;
        *reinterpret_cast<uint32_t *>(restMem - 4) = rest | kPrevInUse;
        *reinterpret_cast<uint32_t *>(restMem + rest - 4) |= kPrevInUse;
        *reinterpret_cast<uint32_t *>(restMem + rest - 8) = rest;
        uint32_t saved = mUnknown478;
        mUnknown478 = 0;
        FreeInternal(restMem);
        mUnknown478 = saved;
        return mem;
    }
}
