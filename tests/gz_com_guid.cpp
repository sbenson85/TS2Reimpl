// cGZCOM::CreateGuid (slot 7, 0x145325) and CreateGuids (slot 8, 0x1453ab), with the clock redirected on both sides
// (see gz_com_support.h):
// - the seconds clock: both sides' _time64 (the image's 0x82fbf6, ours nGZLog::Time64) read GetSystemTimeAsFileTime
//   through the import slot 0xd9a2a4, replaced by a fake clock;
// - the quarter-second wait: both call 0x1b9f95, which sleeps through the import slot of SleepEx (0xd9a188), replaced
//   by a fake that logs and advances the fake clock by one second every fourth call;
// - the local seconds since the GUID epoch (0x145774; ours appbase::SecondsSinceGuidEpoch): both entry points are
//   patched with a jump to a fake that derives the value from the fake clock (the real one reads the time-zone state).
// The generator state (+0x50..+0x58) is set alike on both sides and compared after the call.
#include "app/ReportText.h"
#include "free_recorder.h"
#include "gz_com_support.h"

namespace {

constexpr uint32_t kIatGetSystemTimeAsFileTime = 0xd9a2a4, kIatSleepEx = 0xd9a188, kGuidSeconds = 0x145774;

int64_t gNow = 0;      // the fake clock, seconds since 1970
int gSleeps = 0;
uint32_t gEpochShift = 855000000; // what the fake local-seconds count subtracts

void WINAPI FakeGetSystemTimeAsFileTime(FILETIME *out)
{
    uint64_t ticks = static_cast<uint64_t>(gNow) * 10000000ull + 116444736000000000ull;
    out->dwLowDateTime = static_cast<DWORD>(ticks);
    out->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    Log("time %lld", static_cast<long long>(gNow));
}

DWORD WINAPI FakeSleepEx(DWORD ms, BOOL alertable)
{
    Log("sleep %u %d", ms, alertable);
    if (++gSleeps % 4 == 0) ++gNow;
    return 0;
}

uint32_t __cdecl FakeGuidSeconds()
{
    Log("seconds");
    return static_cast<uint32_t>(gNow) - gEpochShift;
}

// A jump to `target` written over the first five bytes of `site`, restored on destruction.
struct CodeJump {
    uint8_t *site;
    uint8_t saved[5];
    CodeJump(void *where, void *target) : site(static_cast<uint8_t *>(where))
    {
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(saved, site, 5);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~CodeJump()
    {
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
};

struct ClockFixture {
    difftest::Context &ctx;
    void *savedTime, *savedSleep, *savedOwnTime;
    CodeJump original, ours;
    explicit ClockFixture(difftest::Context &c)
        : ctx(c), original(c.image.At<void>(kGuidSeconds), reinterpret_cast<void *>(&FakeGuidSeconds)),
          ours(reinterpret_cast<void *>(&appbase::SecondsSinceGuidEpoch), reinterpret_cast<void *>(&FakeGuidSeconds))
    {
        savedTime = *ctx.image.At<void *>(kIatGetSystemTimeAsFileTime);
        savedSleep = *ctx.image.At<void *>(kIatSleepEx);
        *ctx.image.At<void *>(kIatGetSystemTimeAsFileTime) = reinterpret_cast<void *>(&FakeGetSystemTimeAsFileTime);
        *ctx.image.At<void *>(kIatSleepEx) = reinterpret_cast<void *>(&FakeSleepEx);
        // Ours reads the clock through our CRT's _time64, which calls our own import.
        savedOwnTime = difftest::SwapOwnImport("GetSystemTimeAsFileTime", reinterpret_cast<void *>(&FakeGetSystemTimeAsFileTime));
    }
    ~ClockFixture()
    {
        *ctx.image.At<void *>(kIatGetSystemTimeAsFileTime) = savedTime;
        *ctx.image.At<void *>(kIatSleepEx) = savedSleep;
        difftest::SwapOwnImport("GetSystemTimeAsFileTime", savedOwnTime);
    }
};

struct GuidCase {
    int64_t now;
    uint32_t last;
    uint32_t seeds[3];
};

std::vector<GuidCase> GuidCases(std::mt19937 &rng)
{
    std::vector<GuidCase> cases;
    const int64_t nows[] = {0x6720a3c0, 0x6720a3c5, 0x7fffffff, 0x80000005, 0x100000003ll, 5};
    for (int64_t now : nows) {
        const uint32_t lasts[] = {0, static_cast<uint32_t>(now) - 5, static_cast<uint32_t>(now), static_cast<uint32_t>(now) + 1, static_cast<uint32_t>(now) + 3,
                                  0x80000000u, 0xffffffffu};
        for (uint32_t last : lasts) cases.push_back({now, last, {rng(), rng(), rng()}});
    }
    return cases;
}

void Prepare(cGZCOM *com, const GuidCase &c)
{
    com->mLastGuidTime = c.last;
    std::memcpy(reinterpret_cast<char *>(com) + 0x50, c.seeds, sizeof(c.seeds));
}

std::string Generator(const cGZCOM *com)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), " gen=%x/%x/%x", Word<uint32_t>(com, 0x50), Word<uint32_t>(com, 0x54), Word<uint32_t>(com, 0x58));
    return buf;
}

} // namespace

DIFF_TEST(GZCOM_CreateGuid, 0x145325)
{
    ClockFixture clock(ctx);
    std::mt19937 rng(31);
    for (const GuidCase &c : GuidCases(rng)) {
        // A wait of more than a few seconds would be a test of the fake, not of the code: the cases stay below 4.
        std::string results[2];
        std::vector<unsigned char> bytes[2];
        for (int side = 0; side < 2; ++side) {
            cGZCOM *com = MakeCom(ctx, side);
            InstallCritVtable(ctx, com);
            Prepare(com, c);
            gNow = c.now;
            gSleeps = 0;
            gLog.clear();
            uint32_t guid = 0xa5a5a5a5;
            bool r = Call<bool>(ctx, side, com, 0, 7, kCreateGuid, &guid);
            char text[96];
            std::snprintf(text, sizeof(text), "ret=%d guid=%08x last=%x", r ? 1 : 0, guid, com->mLastGuidTime);
            results[side] = text + Generator(com) + " | " + Joined(gLog);
            bytes[side] = Canon(com, side);
            DestroyCom(ctx, com, side);
        }
        char what[80];
        std::snprintf(what, sizeof(what), "CreateGuid now=%llx last=%x", static_cast<long long>(c.now), c.last);
        ExpectSame(ctx, what, results[0], results[1]);
        ExpectSameBytes(ctx, what, bytes[0], bytes[1]);
    }
}

DIFF_TEST(GZCOM_CreateGuids, 0x1453ab)
{
    ClockFixture clock(ctx);
    std::mt19937 rng(37);
    std::vector<GuidCase> cases = GuidCases(rng);
    const uint32_t counts[] = {0, 1, 2, 7, 8, 9, 15, 16, 17, 40};
    for (size_t i = 0; i < cases.size(); ++i) {
        for (uint32_t count : counts) {
            if ((i + count) % 3 != 0 && count > 2) continue; // every count with a third of the clock cases
            const GuidCase &c = cases[i];
            std::string results[2];
            std::vector<unsigned char> bytes[2];
            for (int side = 0; side < 2; ++side) {
                cGZCOM *com = MakeCom(ctx, side);
                InstallCritVtable(ctx, com);
                Prepare(com, c);
                gNow = c.now;
                gSleeps = 0;
                gLog.clear();
                std::vector<uint32_t> guids(count + 2, 0xa5a5a5a5);
                bool r = Call<bool>(ctx, side, com, 0, 8, kCreateGuids, guids.data(), count);
                char text[64];
                std::snprintf(text, sizeof(text), "ret=%d last=%x", r ? 1 : 0, com->mLastGuidTime);
                std::string s = text + Generator(com) + " guids";
                for (uint32_t g : guids) {
                    std::snprintf(text, sizeof(text), " %08x", g);
                    s += text;
                }
                results[side] = s + " | " + Joined(gLog);
                bytes[side] = Canon(com, side);
                DestroyCom(ctx, com, side);
            }
            char what[96];
            std::snprintf(what, sizeof(what), "CreateGuids %u now=%llx last=%x", count, static_cast<long long>(c.now), c.last);
            ExpectSame(ctx, what, results[0], results[1]);
            ExpectSameBytes(ctx, what, bytes[0], bytes[1]);
        }
    }
}
