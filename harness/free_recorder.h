#pragma once
// Exact, layout-independent observation of the process heap: FreeRecorder (which blocks were freed) and
// AllocationCounter (blocks and bytes allocated minus freed, for leak checks). While a FreeRecorder is alive,
// every HeapFree (and the old block of a moving HeapReAlloc) made by the creating thread on the process
// heap is recorded, from both the original image (its HeapFree import is bound to a counting wrapper)
// and our own code (the difftest exe's IAT entry is patched at startup, InstallFreeRecorderHooks()).
// Recording uses fixed arrays and never allocates, so it does not disturb the heap. Frees from other
// threads (COM/shell leftovers) are ignored. No nesting.
#include <cstddef>
#include <cstdint>

namespace difftest {

void InstallFreeRecorderHooks(); // once, at startup (harness/main.cpp)
// This exe's own import entry for `name` replaced; returns the previous one (harness/image.cpp).
void *SwapOwnImport(const char *name, void *replacement);

// A test fixture's own growing buffer (drawfx::gLog), registered once with a probe that returns its current address.
// If the address differs between a FreeRecorder's start and its end, the fixture reallocated inside the window and that
// free was counted as the code under test's: the runner fails the test, naming the buffer (TakeBufferMoves).
// Fix: reserve the capacity before the window opens.
void WatchBufferInRecorders(const char *name, const void *(*probe)());
unsigned TakeBufferMoves(const char **firstName); // moves seen since the last call (resets); the first one's name

class FreeRecorder {
public:
    FreeRecorder();  // starts recording
    ~FreeRecorder(); // stops
    FreeRecorder(const FreeRecorder &) = delete;
    FreeRecorder &operator=(const FreeRecorder &) = delete;

    void Stop(); // stop early; the results stay readable
    // Number of frees recorded (a block freed, reused by the heap and freed again counts twice).
    size_t Count() const;
    // True if p was freed at least once (a later allocation may have reused the address).
    bool Freed(const void *p) const;
    // The i-th free: pointer and the block's size just before it was freed (0 for a moved realloc).
    const void *Pointer(size_t i) const;
    size_t Size(size_t i) const;
    // Sizes of all freed blocks, sorted: equal on both sides when the same blocks were freed.
    // Writes up to `capacity` entries to out and returns the count.
    size_t SortedSizes(size_t *out, size_t capacity) const;
    // True if more frees happened than the recorder can hold (results are then incomplete).
    bool Overflowed() const;
};

// Allocated minus freed, exact and per thread: while an AllocationCounter is alive, every HeapAlloc, HeapFree and
// HeapReAlloc on the process heap made by the creating thread is counted, from the original image (its imports are
// bound to the same wrappers as FreeRecorder's) and from our code (our CRT's malloc / free / realloc, so operator
// new / delete, and every GameNew / GameDelete stand-in that calls them). Allocations and frees by other threads (the
// threads earlier tests leave running) are never counted, and the answer does not depend on the heap's layout, unlike
// a heap walk (_heapwalk / HeapWalk), which also sees other threads' blocks and freed blocks they reuse.
// Not seen: memory the game's own allocator hands out from its cores (a test that builds the real allocator rather
// than standing in for the game's operator new), private heaps, and a test's import swap that skips the previous entry.
// Like FreeRecorder: fixed arrays, never allocates, one at a time (a second one while one is alive aborts); it can run
// alongside a FreeRecorder.
class AllocationCounter {
public:
    AllocationCounter();  // starts counting on this thread
    ~AllocationCounter(); // stops
    AllocationCounter(const AllocationCounter &) = delete;
    AllocationCounter &operator=(const AllocationCounter &) = delete;

    void Stop(); // stop early; the results stay readable
    // Blocks allocated minus blocks freed (a moving realloc changes neither), and the same in bytes (each block's size
    // as requested; a realloc adds its new size minus its old). Frees of blocks allocated before the counter started
    // count too, so the result can be negative.
    long long Blocks() const;
    long long Bytes() const;
    size_t Allocations() const; // blocks allocated (a moving realloc's new block included)
    size_t Frees() const;       // blocks freed (a moving realloc's old block included)
    // True if p is one of the first `upTo` blocks handed out while counting (a later free does not clear it).
    bool Allocated(const void *p, size_t upTo = static_cast<size_t>(-1)) const;
    bool Overflowed() const; // more allocations than the log holds: Allocated() is then incomplete (counts are not)
};

// Heap watch (the normalised words' heap blocks, tests/norm_words.h): while a watcher is installed, every HeapAlloc,
// HeapReAlloc and HeapFree the installing thread makes, on any heap, from the original image and from our code, is
// reported to it (a moving realloc as a free and an allocation, one in place as a resize). The callbacks run inside the
// heap wrappers: they must not allocate. Allocations under a NotCounted are not reported (frees always are).
struct HeapWatcher {
    void (*allocated)(void *p, size_t bytes);
    void (*resized)(void *p, size_t bytes);
    void (*freed)(void *p);
};
void WatchHeap(const HeapWatcher *watcher); // on this thread; nullptr stops
// For an allocator the heap wrappers do not see (the game's general allocator behind a test's allocator service, which
// takes its cores from VirtualAlloc): reports its blocks to the watcher as the heap wrappers do (nothing when no watcher
// runs on this thread; an allocation under a NotCounted is not reported).
void WatchedAllocation(void *p, size_t bytes);
void WatchedResize(void *p, size_t bytes);
void WatchedFree(void *p);

// The test's own bookkeeping, not the code under test: nothing this thread allocates or frees while one is alive is
// counted, nor reported to a heap watcher (svcfx::Ev and AddRange use it, so a fake's logging inside a counted call is left out). Nests; costs two
// compares when no counter runs; on another thread than the counter's it does nothing.
class NotCounted {
public:
    NotCounted();
    ~NotCounted();
    NotCounted(const NotCounted &) = delete;
    NotCounted &operator=(const NotCounted &) = delete;

private:
    unsigned generation_; // the counter it paused (0: none)
    bool watchPaused_;    // it paused the heap watch
};

} // namespace difftest
