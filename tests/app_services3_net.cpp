// Differential test for cRZNetManager's connection-state clear 0xc65a40 (src/rz/cRZNetManager.cpp). The constructor and
// the rest of the class are tested in tests/rz_net_manager.cpp.
#include "difftest.h"
#include "rz/cRZNetManager.h"

#include <algorithm>
#include <cstring>
#include <random>

namespace {

constexpr uint32_t kClear = 0xc65a40;

} // namespace

DIFF_TEST(AppServices3_NetManagerClear, kClear)
{
    using Fn = void *(__thiscall *)(void *);
    auto original = ctx.Original<Fn>(kClear);
    std::mt19937 rng(kClear);
    for (int i = 0; i < 200; ++i) {
        alignas(8) unsigned char a[sizeof(cRZNetConnectionState) + 8], b[sizeof(cRZNetConnectionState) + 8];
        for (size_t k = 0; k < sizeof(a); ++k) a[k] = b[k] = static_cast<unsigned char>(i % 4 == 0 ? 0xff : rng());
        void *ra = original(a);
        void *rb = reinterpret_cast<cRZNetConnectionState *>(b)->Clear();
        ++ctx.cases;
        if (ra != a || rb != b || std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Clear: result %p/%p or bytes differ (first difference at %d)", ra, rb, static_cast<int>(std::mismatch(a, a + sizeof(a), b).first - a));
    }
}
