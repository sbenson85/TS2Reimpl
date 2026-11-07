// The host's first-call coverage (src/hook/coverage.h): arming (expected byte, our jumps left alone, mismatches), the
// breakpoint handler (one record per site and round, the caller's return address, the original byte restored and the
// call completing normally), rounds and disarming. Not a differential test: the mechanism is ours, run here on the
// mapped image's cRZDate::IsValid 0x1b98a4, which calls cRZDate::IsLeapYear 0x1b995c (return address 0x1b98c9);
// cRZDate::ToJulianDay 0x1b98e2 stands in for a hooked site; everything is restored at the end.
#include "difftest.h"
#include "hook/coverage.h"

#include <cstring>

namespace {

constexpr uint32_t kIsValid = 0x1b98a4, kIsLeap = 0x1b995c, kIsLeapReturn = 0x1b98c9, kToJulian = 0x1b98e2;
constexpr uint32_t kMismatched = 0x1b969a; // cRZDate copy: listed with a wrong expected byte

coverage::Recorder *gRecorder = nullptr;

LONG CALLBACK TestHandler(EXCEPTION_POINTERS *info)
{
    return gRecorder && gRecorder->OnException(info) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

DIFF_TEST(HookCoverage_FirstCalls, kIsValid)
{
    uint8_t *base = static_cast<uint8_t *>(ctx.image.At(0));
    const uint32_t imageSize = 0x120f000;
    uint8_t savedValid = base[kIsValid], savedLeap = base[kIsLeap], savedMismatched = base[kMismatched];
    uint8_t savedJulian[5];
    std::memcpy(savedJulian, base + kToJulian, 5);

    // Our code: a caller of IsValid(month, day, year) (cdecl) and a target for the stand-in hook's jump.
    auto *code = static_cast<uint8_t *>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    auto makeCaller = [&](uint8_t *at, uint32_t month, uint32_t day, uint32_t year) {
        uint8_t *p = at;
        for (uint32_t v : {year, day, month}) {
            *p++ = 0x68; // push imm32
            std::memcpy(p, &v, 4);
            p += 4;
        }
        *p++ = 0xE8; // call IsValid
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(base + kIsValid) - reinterpret_cast<uintptr_t>(p + 4));
        std::memcpy(p, &rel, 4);
        p += 4;
        uint8_t *after = p;
        *p++ = 0x83; // add esp, 12
        *p++ = 0xC4;
        *p++ = 0x0C;
        *p++ = 0xC3; // ret
        return after;
    };
    uint8_t *leapCaller = code, *plainCaller = code + 64, *hookTarget = code + 128;
    uint8_t *leapReturn = makeCaller(leapCaller, 2, 29, 2024);
    makeCaller(plainCaller, 2, 29, 2023);
    hookTarget[0] = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), code, 4096);
    auto callLeap = reinterpret_cast<uint32_t(__cdecl *)()>(leapCaller);
    auto callPlain = reinterpret_cast<uint32_t(__cdecl *)()>(plainCaller);

    // The stand-in hook: `jmp hookTarget` over ToJulianDay's entry.
    base[kToJulian] = 0xE9;
    const int32_t hookRel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(hookTarget) - reinterpret_cast<uintptr_t>(base + kToJulian + 5));
    std::memcpy(base + kToJulian + 1, &hookRel, 4);

    coverage::Site sites[4] = {};
    // Unsorted on purpose: Init sorts.
    sites[0].rva = kToJulian, sites[0].original = savedJulian[0];
    sites[1].rva = kIsLeap, sites[1].original = savedLeap;
    sites[2].rva = kMismatched, sites[2].original = static_cast<uint8_t>(savedMismatched ^ 0x10);
    sites[3].rva = kIsValid, sites[3].original = savedValid;
    coverage::Hit hits[16];
    coverage::Recorder recorder;
    recorder.Init(base, imageSize, sites, 4, hits, 16);
    gRecorder = &recorder;
    PVOID handler = AddVectoredExceptionHandler(1, &TestHandler);

    auto stateOf = [&](uint32_t rva) {
        for (size_t i = 0; i < recorder.SiteCount(); ++i)
            if (recorder.Sites()[i].rva == rva) return recorder.Sites()[i].state;
        return -1L;
    };
    auto expectHit = [&](size_t i, uint32_t rva, uintptr_t from, const char *what) {
        ++ctx.cases;
        if (recorder.Count() <= i) return ctx.Fail("%s: only %zu records", what, recorder.Count());
        const coverage::Hit &h = recorder.At(i);
        if (static_cast<uint32_t>(h.rva) != rva) ctx.Fail("%s: record %zu is rva 0x%x, expected 0x%x", what, i, static_cast<uint32_t>(h.rva), rva);
        if (h.from != from) ctx.Fail("%s: record %zu has return address %08x, expected %08x", what, i, h.from, static_cast<uint32_t>(from));
        if (h.thread != GetCurrentThreadId()) ctx.Fail("%s: record %zu has thread %u", what, i, h.thread);
    };

    ctx.Mark("arm");
    for (size_t i = 1; i < recorder.SiteCount(); ++i)
        if (recorder.Sites()[i - 1].rva >= recorder.Sites()[i].rva) ctx.Fail("Init left the sites unsorted at %zu", i);
    const coverage::ArmCounts counts = recorder.Arm();
    ++ctx.cases;
    if (counts.armed != 2 || counts.ours != 1 || counts.mismatched != 1)
        ctx.Fail("Arm: %u armed, %u ours, %u mismatched; expected 2, 1, 1", counts.armed, counts.ours, counts.mismatched);
    if (base[kIsValid] != 0xCC || base[kIsLeap] != 0xCC) ctx.Fail("Arm: the armed sites hold %02x %02x, not int3", base[kIsValid], base[kIsLeap]);
    if (base[kMismatched] != savedMismatched) ctx.Fail("Arm: the mismatched site was written");
    if (base[kToJulian] != 0xE9) ctx.Fail("Arm: the hooked site was written");
    if (stateOf(kToJulian) != coverage::kOurs || stateOf(kMismatched) != coverage::kSkipped || stateOf(kIsValid) != coverage::kArmed)
        ctx.Fail("Arm: states %ld %ld %ld", stateOf(kToJulian), stateOf(kMismatched), stateOf(kIsValid));

    ctx.Mark("first call");
    ++ctx.cases;
    if (uint32_t r = callLeap(); r != 1) ctx.Fail("IsValid(2, 29, 2024) under coverage returned %u", r);
    if (recorder.Count() != 2) ctx.Fail("first call: %zu records, expected 2", recorder.Count());
    expectHit(0, kIsValid, reinterpret_cast<uintptr_t>(leapReturn), "first call, IsValid");
    expectHit(1, kIsLeap, reinterpret_cast<uintptr_t>(base + kIsLeapReturn), "first call, IsLeapYear from IsValid");
    if (base[kIsValid] != savedValid || base[kIsLeap] != savedLeap) ctx.Fail("first call: the original bytes weren't restored");
    if (stateOf(kIsValid) != coverage::kHit || stateOf(kIsLeap) != coverage::kHit) ctx.Fail("first call: sites not marked hit");

    ctx.Mark("later calls");
    ++ctx.cases;
    if (uint32_t r = callPlain(); r != 0) ctx.Fail("IsValid(2, 29, 2023) returned %u", r);
    if (uint32_t r = callLeap(); r != 1) ctx.Fail("IsValid(2, 29, 2024) again returned %u", r);
    if (recorder.Count() != 2) ctx.Fail("later calls: %zu records, expected still 2", recorder.Count());

    ctx.Mark("not ours");
    ++ctx.cases;
    {
        EXCEPTION_RECORD record{};
        CONTEXT context{};
        EXCEPTION_POINTERS info{&record, &context};
        record.ExceptionCode = EXCEPTION_BREAKPOINT;
        record.ExceptionAddress = base + kMismatched; // listed, left alone
        if (recorder.OnException(&info)) ctx.Fail("OnException claimed a breakpoint at a skipped site");
        record.ExceptionAddress = base + kToJulian; // listed, ours
        if (recorder.OnException(&info)) ctx.Fail("OnException claimed a breakpoint at our hooked site");
        record.ExceptionAddress = base + kIsValid + 1; // not listed
        if (recorder.OnException(&info)) ctx.Fail("OnException claimed a breakpoint at an unlisted address");
        record.ExceptionAddress = code; // outside the image
        if (recorder.OnException(&info)) ctx.Fail("OnException claimed a breakpoint outside the image");
        record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
        record.ExceptionAddress = base + kIsValid;
        if (recorder.OnException(&info)) ctx.Fail("OnException claimed an access violation");
        if (recorder.Count() != 2) ctx.Fail("not ours: records were added");
    }

    ctx.Mark("round 1");
    ++ctx.cases;
    const uint32_t rearmed = recorder.Rearm();
    if (rearmed != 2) ctx.Fail("Rearm: %u sites, expected 2", rearmed);
    if (recorder.Round() != 1) ctx.Fail("Rearm: round %ld", recorder.Round());
    if (base[kIsValid] != 0xCC || base[kIsLeap] != 0xCC) ctx.Fail("Rearm: sites not re-armed");
    if (base[kMismatched] != savedMismatched || base[kToJulian] != 0xE9) ctx.Fail("Rearm: a skipped site was written");
    if (recorder.Count() != 3 || static_cast<uint32_t>(recorder.At(2).rva) != coverage::kRoundMarker || recorder.At(2).from != 1)
        ctx.Fail("Rearm: no round marker for round 1 (%zu records)", recorder.Count());
    if (uint32_t r = callPlain(); r != 0) ctx.Fail("IsValid(2, 29, 2023) in round 1 returned %u", r);
    expectHit(3, kIsValid, reinterpret_cast<uintptr_t>(plainCaller) + 20, "round 1, IsValid");
    expectHit(4, kIsLeap, reinterpret_cast<uintptr_t>(base + kIsLeapReturn), "round 1, IsLeapYear");
    if (recorder.Count() != 5) ctx.Fail("round 1: %zu records, expected 5", recorder.Count());

    ctx.Mark("capacity");
    ++ctx.cases;
    for (int i = 0; i < 6; ++i) {
        recorder.Rearm();
        callLeap();
    }
    if (recorder.Count() != 16) ctx.Fail("capacity: Count %zu, expected the capacity 16", recorder.Count());
    if (recorder.Dropped() != 23 - 16) ctx.Fail("capacity: %ld dropped, expected 7", recorder.Dropped());
    if (base[kIsValid] != savedValid) ctx.Fail("capacity: a dropped record left the site armed");

    ctx.Mark("disarm");
    ++ctx.cases;
    recorder.Rearm();
    recorder.Disarm();
    if (base[kIsValid] != savedValid || base[kIsLeap] != savedLeap) ctx.Fail("Disarm: original bytes not back");

    RemoveVectoredExceptionHandler(handler);
    gRecorder = nullptr;
    std::memcpy(base + kToJulian, savedJulian, 5);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    VirtualFree(code, 0, MEM_RELEASE);
}
