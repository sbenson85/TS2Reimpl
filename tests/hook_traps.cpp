// The host's traps on originals replaced through their callers (src/hook/traps.h): which switches arm a trap, the
// site check, and the stub (index and return address reach the handler; the call returns through it). Not a
// differential test: the mechanism is ours, run here on the mapped image's `entry` 0x8248a3 (10 bytes), patched and
// restored.
#include "difftest.h"
#include "hook/traps.h"

#include <cstring>
#include <string>

namespace {

constexpr uint32_t kSite = 0x8248a3;
constexpr uint32_t kSiteCrc = 0x8c45a9e8; // zlib.crc32 of the unpacked exe's [0x8248a3, +10), as tools/callers.py writes it

struct Reported {
    const char *section;
    const char *key;
    int value;
};
const Reported *gReported = nullptr;
size_t gReportedCount = 0;

bool LookUp(const char *section, const char *key, int *value)
{
    for (size_t i = 0; i < gReportedCount; ++i) {
        if (std::strcmp(gReported[i].section, section) == 0 && std::strcmp(gReported[i].key, key) == 0) {
            *value = gReported[i].value;
            return true;
        }
    }
    return false;
}

uint32_t gIndex = 0, gHits = 0;
uintptr_t gReturn = 0;

uint32_t __cdecl RecordingHandler(uint32_t index, uintptr_t returnAddress)
{
    gIndex = index;
    gReturn = returnAddress;
    ++gHits;
    return 0x5eed0000u + index;
}

const char *Name(trap_detail::ArmResult r)
{
    switch (r) {
    case trap_detail::ArmResult::NotSelected: return "not selected";
    case trap_detail::ArmResult::Mismatch: return "mismatch";
    case trap_detail::ArmResult::Armed: return "armed";
    }
    return "?";
}

} // namespace

DIFF_TEST(HookTraps_Arm, kSite)
{
    uint8_t *base = static_cast<uint8_t *>(ctx.image.At(0));
    uint8_t *site = base + kSite;
    uint8_t saved[5];
    std::memcpy(saved, site, 5);
    if (trap_detail::Crc32(site, 10) != kSiteCrc) ctx.Fail("Crc32 of the mapped entry is %08x, zlib says %08x", trap_detail::Crc32(site, 10), kSiteCrc);

    auto *code = static_cast<uint8_t *>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    uint8_t *stub = code;
    uint8_t *caller = code + 64; // call site ; ret
    caller[0] = 0xE8;
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(site) - reinterpret_cast<uintptr_t>(caller + 5));
    std::memcpy(caller + 1, &rel, 4);
    caller[5] = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), code, 4096);

    const Reported none[1] = {{"", "", 0}};
    const Reported mode0[] = {{"Startup", "OwnStart", 0}};
    const Reported mode1[] = {{"Other", "Switch", 1}, {"Startup", "OwnStart", 1}};
    const Reported mode2[] = {{"Startup", "OwnStart", 2}};
    const Reported mode3[] = {{"Startup", "OwnStart", 3}};
    const Reported modeNeg[] = {{"Startup", "OwnStart", -1}};
    const Reported mode32[] = {{"Startup", "OwnStart", 32}};
    const Reported otherKey[] = {{"Startup", "OwnStartX", 2}};
    // Conjunctions ("Startup.OwnStart=1,2&Hooks.A=1[&Hooks.B=1]"): every term must hold.
    const TrapTerm alsoA[] = {{"Hooks", "A", 0x2}, {nullptr, nullptr, 0}};
    const TrapTerm alsoAB[] = {{"Hooks", "A", 0x2}, {"Hooks", "B", 0x2}, {nullptr, nullptr, 0}};
    const Reported both[] = {{"Hooks", "A", 1}, {"Startup", "OwnStart", 2}};
    const Reported secondOff[] = {{"Startup", "OwnStart", 2}, {"Hooks", "A", 0}};
    const Reported firstOff[] = {{"Startup", "OwnStart", 3}, {"Hooks", "A", 1}};
    const Reported threeOfThree[] = {{"Hooks", "B", 1}, {"Startup", "OwnStart", 1}, {"Hooks", "A", 1}};
    const Reported twoOfThree[] = {{"Startup", "OwnStart", 1}, {"Hooks", "A", 1}, {"Hooks", "B", 0}};
    struct Case {
        const char *label;
        const Reported *reported;
        size_t count;
        uint32_t values, len, crc, index;
        trap_detail::ArmResult expect;
        const TrapTerm *also;
    } const cases[] = {
        {"and: both hold", both, 2, 0x6, 10, kSiteCrc, 5, trap_detail::ArmResult::Armed, alsoA},
        {"and: second switch not reported", mode2, 1, 0x6, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected, alsoA},
        {"and: second switch at 0", secondOff, 2, 0x6, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected, alsoA},
        {"and: first switch off, second on", firstOff, 2, 0x6, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected, alsoA},
        {"and: three of three", threeOfThree, 3, 0x6, 10, kSiteCrc, 9, trap_detail::ArmResult::Armed, alsoAB},
        {"and: third switch at 0", twoOfThree, 3, 0x6, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected, alsoAB},
        {"and: all hold, site CRC differs", both, 2, 0x6, 10, kSiteCrc ^ 1, 0, trap_detail::ArmResult::Mismatch, alsoA},
        {"switch never reported", none, 0, 0x6, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected},
        {"switch never reported, 0 in the set", none, 0, 0x7, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected},
        {"value 0 (the default) not in the set", mode0, 1, 0x6, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected},
        {"value 3 not in the set", mode3, 1, 0x6, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected},
        {"value -1", modeNeg, 1, 0xffffffffu, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected},
        {"value 32", mode32, 1, 0xffffffffu, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected},
        {"another key", otherKey, 1, 0x6, 10, kSiteCrc, 0, trap_detail::ArmResult::NotSelected},
        {"site CRC differs", mode2, 1, 0x6, 10, kSiteCrc ^ 1, 0, trap_detail::ArmResult::Mismatch},
        {"check shorter than the jump", mode2, 1, 0x6, 4, trap_detail::Crc32(site, 4), 0, trap_detail::ArmResult::Mismatch},
        {"value 1 of 1,2 (second switch reported)", mode1, 2, 0x6, 10, kSiteCrc, 3, trap_detail::ArmResult::Armed},
        {"value 2 of 1,2", mode2, 1, 0x6, 10, kSiteCrc, 7, trap_detail::ArmResult::Armed},
        {"value 2 of 2 only", mode2, 1, 0x4, 10, kSiteCrc, 12, trap_detail::ArmResult::Armed},
    };
    for (const Case &c : cases) {
        ++ctx.cases;
        ctx.Mark(c.label);
        gReported = c.reported;
        gReportedCount = c.count;
        gHits = 0;
        const TrapSpec spec{"test", kSite, c.len, c.crc, "Startup", "OwnStart", c.values, c.also};
        std::memset(stub, 0xCC, trap_detail::kStubSize);
        const trap_detail::ArmResult got = trap_detail::Arm(base, spec, c.index, &LookUp, stub, &RecordingHandler);
        if (got != c.expect) ctx.Fail("%s: %s, expected %s", c.label, Name(got), Name(c.expect));
        if (got != trap_detail::ArmResult::Armed) {
            if (std::memcmp(site, saved, 5) != 0) ctx.Fail("%s: the site changed although nothing was armed", c.label);
            continue;
        }
        const int32_t jump = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - reinterpret_cast<uintptr_t>(site + 5));
        if (site[0] != 0xE9 || std::memcmp(site + 1, &jump, 4) != 0) ctx.Fail("%s: the site isn't `jmp stub`", c.label);
        const uint32_t result = reinterpret_cast<uint32_t(__cdecl *)()>(caller)();
        if (gHits != 1) ctx.Fail("%s: handler ran %u times", c.label, gHits);
        if (gIndex != c.index) ctx.Fail("%s: handler got index %u, expected %u", c.label, gIndex, c.index);
        if (gReturn != reinterpret_cast<uintptr_t>(caller + 5))
            ctx.Fail("%s: handler got return address %p, expected %p", c.label, reinterpret_cast<void *>(gReturn), caller + 5);
        if (result != 0x5eed0000u + c.index) ctx.Fail("%s: the trapped call returned %08x", c.label, result);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(site, saved, 5);
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    VirtualFree(code, 0, MEM_RELEASE);
}
