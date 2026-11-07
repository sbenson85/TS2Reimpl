// Differential tests for the AddRef/Release thunks of cRZSystemService-based classes
// (src/rz/RZSystemServiceThunks.h). Each runs on a fabricated block holding a cRZUnknownMT whose
// vtable has a recording deleting destructor in slot 3, called with the subobject pointer the
// thunk expects; result, counts and the recorded deletion are compared with ours.
#include "difftest.h"
#include "rz/RZSystemServiceThunks.h"
#include "rz/cRZRefCount.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kService = 0x10; // where the cRZSystemService part lies in the block

std::vector<int64_t> gLog;
unsigned char *gBase = nullptr;

void *__fastcall RecordingDelete(void *self, void * /*edx*/, uint32_t flags)
{
    gLog.push_back(static_cast<unsigned char *>(self) - gBase);
    gLog.push_back(flags);
    return self;
}

struct Thunk {
    uint32_t rva;
    bool release;
    int32_t entry; // pointer given to the thunk, relative to the service part
    uint32_t (__fastcall *ours)(void *, void *);
};

const Thunk kThunks[] = {
    {0x1b9640, false, 0, svc::SystemServiceAddRef},
    {0x145240, false, -4, svc::AddRefServiceAt4},
    {0x145248, false, 0, svc::AddRefServiceAt0},
    {0x145250, false, 4, svc::AddRefServiceAtMinus4},
    {0x1cc320, false, 0, svc::AddRefServiceAt0},
    {0x1cc325, false, 4, svc::AddRefServiceAtMinus4},
    {0x1cc32d, false, 0x1c, svc::AddRefServiceAtMinus1C},
    {0x1b967a, true, 0, svc::SystemServiceRelease},
    {0x1a85b0, true, -4, svc::ReleaseServiceAt4},
    {0x1a85b8, true, 0, svc::ReleaseServiceAt0},
    {0x1a85c0, true, 4, svc::ReleaseServiceAtMinus4},
    {0x1cd7a0, true, 0, svc::ReleaseServiceAt0},
    {0x1cd7a5, true, 4, svc::ReleaseServiceAtMinus4},
    {0x1cd7ad, true, 0x1c, svc::ReleaseServiceAtMinus1C},
};

} // namespace

DIFF_TEST(RZSystemServiceThunks_AddRefRelease, 0x1cc320)
{
    void *vtable[6] = {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&RecordingDelete), nullptr, nullptr};
    std::mt19937 rng(0x7407);
    for (const Thunk &t : kThunks) {
        auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(t.rva);
        for (int run = 0; run < 60; ++run) {
            alignas(8) unsigned char a[0x40], b[0x40];
            for (int i = 0; i < 0x40; ++i) a[i] = static_cast<unsigned char>(rng());
            std::memcpy(b, a, sizeof(a));
            // The cRZUnknownMT part: vtable pointer and count, 4 bytes after the service part.
            uint32_t count = t.release ? rng() % 5 : (run % 7 == 0 ? 0xffffffffu : rng() % 6);
            for (unsigned char *block : {a, b}) {
                *reinterpret_cast<void **>(block + kService + 4) = vtable;
                *reinterpret_cast<uint32_t *>(block + kService + 8) = count;
            }
            uint32_t ra, rb;
            gLog.clear();
            gBase = a;
            ra = orig(a + kService + t.entry);
            std::vector<int64_t> logA = gLog;
            gLog.clear();
            gBase = b;
            rb = t.ours(b + kService + t.entry, nullptr);
            ++ctx.cases;
            if (ra != rb || logA != gLog || std::memcmp(a, b, sizeof(a)) != 0) {
                ctx.Fail("0x%x count %u: result %u/%u, count %u/%u, deletions %zu/%zu", t.rva, count, ra, rb,
                         *reinterpret_cast<uint32_t *>(a + kService + 8), *reinterpret_cast<uint32_t *>(b + kService + 8), logA.size(), gLog.size());
            }
            if (t.release && count <= 1 && (logA.size() != 2 || logA[1] != 1)) ctx.Fail("0x%x count %u: no deletion with flags 1", t.rva, count);
            if (t.release && count > 1 && !logA.empty()) ctx.Fail("0x%x count %u: unexpected deletion", t.rva, count);
        }
    }
}
