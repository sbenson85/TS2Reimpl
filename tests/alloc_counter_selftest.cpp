// difftest::AllocationCounter self-checks (lane MH, harness/free_recorder.h): it counts this thread's allocations and
// frees on the process heap from both routes (our CRT's malloc / realloc / free and operator new / delete; the image's
// operator new 0x824632, whose malloc reaches HeapAlloc through the image's import slot, and free 0x6c4b50), leaves out
// other threads' and NotCounted bookkeeping, and runs alongside a FreeRecorder.
#include "difftest.h"
#include "free_recorder.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <new>

namespace {

struct OtherThread {
    HANDLE go = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    void *blocks[3] = {};
    bool release = false;
    static DWORD WINAPI Run(void *self)
    {
        auto *t = static_cast<OtherThread *>(self);
        for (int round = 0; round < 2; ++round) {
            WaitForSingleObject(t->go, INFINITE);
            for (void *&b : t->blocks) {
                if (t->release) {
                    std::free(b);
                    b = nullptr;
                } else {
                    b = std::malloc(64);
                }
            }
            SetEvent(t->done);
        }
        return 0;
    }
    void Step()
    {
        SetEvent(go);
        WaitForSingleObject(done, INFINITE);
    }
};

} // namespace

DIFF_TEST(AllocCounter_Routes, 0)
{
    using New = void *(__cdecl *)(size_t);
    using Free = void(__cdecl *)(void *);
    auto imageNew = ctx.Original<New>(0x824632);
    auto imageFree = ctx.Original<Free>(0x6c4b50);
    auto check = [&](bool ok, const char *what, long long blocks, long long bytes) {
        ++ctx.cases;
        if (!ok) ctx.Fail("%s: blocks %lld bytes %lld", what, blocks, bytes);
    };

    {
        // Ours: operator new / delete, malloc / realloc / free, calloc.
        difftest::AllocationCounter c;
        void *a = ::operator new(100);
        int *b = new int[7];
        void *m = std::malloc(33);
        m = std::realloc(m, 3000);
        void *z = std::calloc(5, 8);
        check(c.Blocks() == 4 && c.Bytes() == 100 + 28 + 3000 + 40, "ours allocated", c.Blocks(), c.Bytes());
        check(c.Allocated(a) && c.Allocated(b) && c.Allocated(m) && c.Allocated(z) && !c.Allocated(&c), "ours logged", c.Blocks(), c.Bytes());
        check(c.Allocated(a, 1) && !c.Allocated(b, 1), "upTo", c.Blocks(), c.Bytes());
        ::operator delete(a);
        delete[] b;
        std::free(z);
        check(c.Blocks() == 1 && c.Bytes() == 3000 && c.Frees() >= 3, "ours freed", c.Blocks(), c.Bytes());
        std::free(m);
        check(c.Blocks() == 0 && c.Bytes() == 0, "ours balanced", c.Blocks(), c.Bytes());
    }
    {
        // The image's operator new and free, and a block one side allocates and the other frees.
        difftest::AllocationCounter c;
        void *p = imageNew(52);
        void *q = imageNew(8);
        check(c.Blocks() == 2 && c.Bytes() == 60 && c.Allocated(p) && c.Allocated(q), "image allocated", c.Blocks(), c.Bytes());
        imageFree(p);
        std::free(q);
        check(c.Blocks() == 0 && c.Bytes() == 0 && c.Frees() == 2, "image freed", c.Blocks(), c.Bytes());
        void *leak = imageNew(12);
        check(c.Blocks() == 1 && c.Bytes() == 12, "image leak seen", c.Blocks(), c.Bytes());
        c.Stop();
        imageFree(leak);
        check(c.Blocks() == 1 && c.Bytes() == 12, "stopped", c.Blocks(), c.Bytes());
    }
    {
        // A block allocated before the counter and freed while it runs counts negative.
        void *before = std::malloc(20);
        difftest::AllocationCounter c;
        std::free(before);
        check(c.Blocks() == -1 && c.Bytes() == -20 && c.Allocations() == 0, "earlier block freed", c.Blocks(), c.Bytes());
    }
    {
        // NotCounted: the test's own bookkeeping is left out, nested too.
        difftest::AllocationCounter c;
        void *kept;
        {
            difftest::NotCounted outer;
            {
                difftest::NotCounted inner;
            }
            kept = std::malloc(77);
        }
        void *counted = std::malloc(5);
        check(c.Blocks() == 1 && c.Bytes() == 5 && !c.Allocated(kept), "not counted", c.Blocks(), c.Bytes());
        std::free(counted);
        std::free(kept);
        check(c.Blocks() == -1 && c.Bytes() == -77, "bookkeeping freed while counting", c.Blocks(), c.Bytes());
    }
    {
        // A NotCounted outliving its counter does not pause the next one.
        auto *stale = new difftest::AllocationCounter;
        auto *pause = new difftest::NotCounted;
        delete stale;
        difftest::AllocationCounter c;
        delete pause; // its own block's free is counted: -1 block, -sizeof(NotCounted) bytes
        void *p = std::malloc(9);
        const long long freed = static_cast<long long>(sizeof(difftest::NotCounted));
        check(c.Blocks() == 0 && c.Bytes() == 9 - freed && c.Frees() == 1, "stale pause", c.Blocks(), c.Bytes());
        std::free(p);
    }
    {
        // Another thread's allocations and frees are never counted.
        OtherThread t;
        HANDLE thread = CreateThread(nullptr, 0, &OtherThread::Run, &t, 0, nullptr);
        difftest::AllocationCounter c;
        void *mine = std::malloc(16);
        t.Step(); // the other thread allocates three blocks
        check(c.Blocks() == 1 && c.Bytes() == 16, "other thread allocates", c.Blocks(), c.Bytes());
        t.release = true;
        t.Step(); // and frees them
        check(c.Blocks() == 1 && c.Bytes() == 16 && c.Frees() == 0, "other thread frees", c.Blocks(), c.Bytes());
        std::free(mine);
        c.Stop();
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        CloseHandle(t.go);
        CloseHandle(t.done);
    }
    {
        // Alongside a FreeRecorder: both see the same free.
        difftest::AllocationCounter c;
        void *p = imageNew(24);
        difftest::FreeRecorder rec;
        imageFree(p);
        rec.Stop();
        check(rec.Freed(p) && rec.Count() == 1 && c.Blocks() == 0 && c.Bytes() == 0, "with FreeRecorder", c.Blocks(), c.Bytes());
    }
}
