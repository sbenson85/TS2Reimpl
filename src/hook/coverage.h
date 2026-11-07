#pragma once
// First-call coverage of the image's original code: which original functions actually run, in which order, reached
// from where (docs/coverage.md). A diagnostic for sizing milestones, never on
// in an ordinary run: the host arms it only with TS2HOST_COVERAGE set (src/hook/coverage.cpp).
//
// Mechanism: a one-shot breakpoint (int3, 0xCC) on the first byte of every listed function start. The first call of
// a function traps once; the vectored handler records the function, the return address on the stack (the caller:
// in the image, in our code, or the caller's caller after a tail jump), the time and the thread, puts the original byte
// back and resumes at the same address. Every later call runs at full speed, so a run costs one exception per function
// that runs (some tens of thousands: well under a second in all). A new round (Rearm) puts the breakpoint back on the
// functions hit so far, so code that runs in a later phase (a steady menu frame, a lot's live mode) shows up again.
//
// Which bytes are touched: only the first byte of a listed site, and only where the image still holds the byte the
// list expects (from the unpacked exe on disk). A site that starts with our own patch (a hook's or trap's `jmp` to code
// outside the image) is left alone and counted as "ours"; a site whose byte differs otherwise is counted as mismatched
// and left alone. The image must be writable (the host maps it read-write-execute; harness/image.cpp).
//
// The mechanism is inline here so the difftests exercise it without the host (tests/hook_coverage.cpp); the host side
// (the site list, the handler registration, the writer thread, the control command) is src/hook/coverage.cpp.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <windows.h>

namespace coverage {

// One function start. Sorted by rva in the table (Recorder::Init sorts them).
struct Site {
    uint32_t rva;
    uint8_t original;       // the byte the list expects at rva (the unpacked exe's)
    uint8_t pad[3];
    LONG state;             // kSkipped, kArmed, kHit or kOurs (changed with Interlocked* once armed)
};
// kSkipped: left alone, the byte isn't the expected one; kOurs: left alone, it starts with our jump out of the image.
constexpr LONG kSkipped = 0, kArmed = 1, kHit = 2, kOurs = 3;

// One record: a function's first call in a round, or a round marker (rva == kRoundMarker, from = the round number).
// `rva` is written last (it publishes the record), so a reader treats a record with rva 0 as not yet written.
struct Hit {
    volatile LONG rva;
    uint32_t from;   // the return address on the stack at the function's first instruction (absolute)
    uint32_t ms;     // milliseconds since Arm
    uint32_t thread; // the thread id
};
constexpr uint32_t kRoundMarker = 0xFFFFFFFFu;

struct ArmCounts {
    uint32_t armed = 0;      // breakpoint written
    uint32_t ours = 0;       // starts with a `jmp` out of the image (our hook, trap or control detour): left alone
    uint32_t mismatched = 0; // the image's byte isn't the expected one (or is int3 already): left alone
};

class Recorder {
public:
    // `sites` and `hits` are caller-owned arrays (the host allocates them once; tests use locals). Sites are sorted here.
    void Init(uint8_t *base, uint32_t imageSize, Site *sites, size_t siteCount, Hit *hits, size_t hitCapacity)
    {
        mBase = base;
        mImageSize = imageSize;
        mSites = sites;
        mSiteCount = siteCount;
        mHits = hits;
        mCapacity = hitCapacity;
        mNext = 0;
        mDropped = 0;
        mRound = 0;
        std::memset(hits, 0, hitCapacity * sizeof(Hit));
        // Insertion sort: linear on the host's list, which tools/coverage.py writes sorted.
        for (size_t i = 1; i < siteCount; ++i) {
            Site s = sites[i];
            size_t j = i;
            while (j > 0 && sites[j - 1].rva > s.rva) {
                sites[j] = sites[j - 1];
                --j;
            }
            sites[j] = s;
        }
    }

    // Writes the breakpoints (round 0). Call once, before the code to be measured runs.
    ArmCounts Arm()
    {
        ArmCounts counts;
        mStart = GetTickCount();
        for (size_t i = 0; i < mSiteCount; ++i) {
            Site &s = mSites[i];
            s.state = kSkipped;
            if (s.rva < 0x1000 || s.rva >= mImageSize) {
                ++counts.mismatched;
                continue;
            }
            uint8_t *at = mBase + s.rva;
            // Our patches jump out of the image (to a hook's counting stub, a trap stub, a control detour); the image's
            // own jump stubs stay inside it.
            if (at[0] == 0xE9 && JumpsOutOfImage(at)) {
                s.state = kOurs;
                ++counts.ours;
                continue;
            }
            if (at[0] != s.original || s.original == 0xCC) {
                ++counts.mismatched;
                continue;
            }
            s.state = kArmed;
            at[0] = 0xCC;
            ++counts.armed;
        }
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        return counts;
    }

    // Starts round `Round() + 1`: puts the breakpoint back on every site hit so far and records a round marker. Safe
    // while other threads run image code (each site's state goes to kArmed before its byte, a one-byte store). Returns
    // the number of sites re-armed.
    uint32_t Rearm()
    {
        const LONG round = InterlockedIncrement(&mRound);
        Record(kRoundMarker, static_cast<uint32_t>(round));
        uint32_t n = 0;
        for (size_t i = 0; i < mSiteCount; ++i) {
            Site &s = mSites[i];
            if (InterlockedCompareExchange(&s.state, kArmed, kHit) != kHit) continue;
            mBase[s.rva] = 0xCC;
            ++n;
        }
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        return n;
    }

    // For a vectored exception handler: true when the exception was one of our breakpoints (recorded if it was the
    // site's first hit this round, the byte restored, execution resumed at the site); false for anything else.
    bool OnException(EXCEPTION_POINTERS *info)
    {
        const EXCEPTION_RECORD *r = info->ExceptionRecord;
        if (r->ExceptionCode != EXCEPTION_BREAKPOINT) return false;
        const auto at = reinterpret_cast<uintptr_t>(r->ExceptionAddress);
        const auto base = reinterpret_cast<uintptr_t>(mBase);
        if (at < base || at >= base + mImageSize) return false;
        Site *s = Find(static_cast<uint32_t>(at - base));
        if (!s || (s->state != kArmed && s->state != kHit)) return false;
        // First thread here records it; a thread that hit the same breakpoint before the byte came back only restores it
        // (the same store) and resumes.
        if (InterlockedCompareExchange(&s->state, kHit, kArmed) == kArmed)
            Record(s->rva, *reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(info->ContextRecord->Esp)));
        mBase[s->rva] = s->original;
        FlushInstructionCache(GetCurrentProcess(), mBase + s->rva, 1);
        // Resume at the int3's own address. Windows already reports Eip there for a breakpoint (removing this line
        // passes tests/hook_coverage.cpp: an equivalent mutant), but the resume address shouldn't rest on that.
        info->ContextRecord->Eip = static_cast<DWORD>(at);
        return true;
    }

    // Records written so far (some of the last may still be unpublished: rva 0).
    size_t Count() const { return mNext < static_cast<LONG>(mCapacity) ? static_cast<size_t>(mNext) : mCapacity; }
    const Hit &At(size_t i) const { return mHits[i]; }
    LONG Dropped() const { return mDropped; }
    LONG Round() const { return mRound; }
    const Site *Sites() const { return mSites; }
    size_t SiteCount() const { return mSiteCount; }

    // Puts every original byte back (tests; the host never disarms).
    void Disarm()
    {
        for (size_t i = 0; i < mSiteCount; ++i) {
            Site &s = mSites[i];
            if (InterlockedExchange(&s.state, kSkipped) == kArmed) mBase[s.rva] = s.original;
        }
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    }

private:
    bool JumpsOutOfImage(const uint8_t *at) const
    {
        int32_t rel;
        std::memcpy(&rel, at + 1, 4);
        const uintptr_t target = reinterpret_cast<uintptr_t>(at) + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(rel));
        const auto base = reinterpret_cast<uintptr_t>(mBase);
        return target < base || target >= base + mImageSize;
    }

    Site *Find(uint32_t rva) const
    {
        size_t lo = 0, hi = mSiteCount;
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (mSites[mid].rva < rva) lo = mid + 1;
            else hi = mid;
        }
        return lo < mSiteCount && mSites[lo].rva == rva ? &mSites[lo] : nullptr;
    }

    void Record(uint32_t rva, uint32_t from)
    {
        const LONG i = InterlockedIncrement(&mNext) - 1;
        if (i >= static_cast<LONG>(mCapacity)) {
            InterlockedIncrement(&mDropped);
            return;
        }
        Hit &h = mHits[i];
        h.from = from;
        h.ms = GetTickCount() - mStart;
        h.thread = GetCurrentThreadId();
        InterlockedExchange(&h.rva, static_cast<LONG>(rva));
    }

    uint8_t *mBase = nullptr;
    uint32_t mImageSize = 0;
    Site *mSites = nullptr;
    size_t mSiteCount = 0;
    Hit *mHits = nullptr;
    size_t mCapacity = 0;
    volatile LONG mNext = 0;
    volatile LONG mDropped = 0;
    volatile LONG mRound = 0;
    DWORD mStart = 0;
};

} // namespace coverage

// Host side (src/hook/coverage.cpp). With TS2HOST_COVERAGE set (to the site list's path, or to 1 for
// <dir>ts2coverage-sites.txt; tools/coverage.py sites writes it), arms the breakpoints in the image at `base` and writes
// ts2coverage.txt into `dir` (with its trailing backslash) as the run goes. Without it, does nothing and returns false.
// Call after every hook, trap and control patch is in and before any image code runs.
bool StartCoverage(uint8_t *base, uint32_t imageSize, const wchar_t *dir);
// The host control command "coverage <label>": starts a new round. False when coverage is off.
bool CoverageRound(const char *label);
