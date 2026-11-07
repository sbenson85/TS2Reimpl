// Harness self-test: the mapped image's implicit TLS and the CRT's thread-safe statics work, so
// original functions with function-local statics can run. _Init_thread_header/footer (library code)
// guard a static: the first header call claims the guard (-1), the footer publishes the new global
// epoch into the guard and the thread's TLS copy (_Init_thread_epoch), and later headers see it done.
#include "difftest.h"

#include <windows.h>

#include <cstdint>

DIFF_TEST(Harness_ThreadSafeStatics, 0x824d4f)
{
    auto header = ctx.Original<void(__cdecl *)(int *)>(0x824d4f);
    auto footer = ctx.Original<void(__cdecl *)(int *)>(0x824cfe);
    const int *globalEpoch = ctx.image.At<int>(0x104f508);
    for (int i = 0; i < 3; ++i) {
        int guard = 0;
        header(&guard);
        ++ctx.cases;
        if (guard != -1) ctx.Fail("header: guard %d, expected -1 (claimed)", guard);
        footer(&guard);
        ++ctx.cases;
        if (guard != *globalEpoch) ctx.Fail("footer: guard %d, global epoch %d", guard, *globalEpoch);
        // The thread's TLS copy of the epoch: TLS block of the image's index, offset 4 (as the
        // footer writes it).
        uint32_t index = *ctx.image.At<uint32_t>(0x10df814);
        auto **array = reinterpret_cast<int **>(__readfsdword(0x2c));
        ++ctx.cases;
        if (array[index][1] != *globalEpoch) ctx.Fail("thread epoch %d, global %d", array[index][1], *globalEpoch);
        int before = guard;
        header(&guard); // already initialised: returns without claiming
        ++ctx.cases;
        if (guard != before) ctx.Fail("second header changed the guard");
    }
}
