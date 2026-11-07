#include "hook/traps.h"

#include "hook/log.h"

#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::pair<std::string, int>> gSwitches; // "section.key" -> value, as the host reported them
uintptr_t gBase = 0;
uint8_t *gStubs = nullptr;
constexpr uintptr_t kImageSize = 0x120f000; // SizeOfImage of 1.18.0.187 (IsSupportedGame)

bool LookUpSwitch(const char *section, const char *key, int *value)
{
    const std::string name = std::string(section) + "." + key;
    for (const auto &s : gSwitches) {
        if (_stricmp(s.first.c_str(), name.c_str()) == 0) {
            *value = s.second;
            return true;
        }
    }
    return false;
}

// A trapped original was reached: say where from, and stop. TerminateProcess, not exit: nothing else of the game's
// (its atexit list, our CRT's) should run after code the check said was unreachable.
uint32_t __cdecl TrapReached(uint32_t index, uintptr_t returnAddress)
{
    const TrapSpec &t = kTrapTable[index];
    const bool inImage = returnAddress >= gBase && returnAddress < gBase + kImageSize;
    if (inImage)
        Log("trap: original 0x%x (%s) reached from %p (image rva 0x%x); stopping with exit code 0x%x", t.rva, t.name,
            reinterpret_cast<void *>(returnAddress), static_cast<unsigned>(returnAddress - gBase), kTrapExitCode);
    else
        Log("trap: original 0x%x (%s) reached from %p (outside the image); stopping with exit code 0x%x", t.rva, t.name,
            reinterpret_cast<void *>(returnAddress), kTrapExitCode);
    TerminateProcess(GetCurrentProcess(), kTrapExitCode);
    for (;;) Sleep(INFINITE);
}

} // namespace

void SetTrapSwitch(const char *section, const char *key, int value)
{
    gSwitches.emplace_back(std::string(section) + "." + key, value);
}

TrapsArmed ArmTraps(uint8_t *base)
{
    TrapsArmed result;
    gBase = reinterpret_cast<uintptr_t>(base);
    size_t n = 0;
    while (kTrapTable[n].name) ++n;
    if (n && !gStubs)
        gStubs = static_cast<uint8_t *>(VirtualAlloc(nullptr, n * trap_detail::kStubSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    for (size_t i = 0; i < n && gStubs; ++i) {
        const TrapSpec &t = kTrapTable[i];
        switch (trap_detail::Arm(base, t, static_cast<uint32_t>(i), &LookUpSwitch, gStubs + i * trap_detail::kStubSize, &TrapReached)) {
        case trap_detail::ArmResult::Armed:
            ++result.armed;
        {
            std::string switches = std::string(t.section) + "." + t.key;
            for (const TrapTerm *a = t.also; a && a->section; ++a) switches += std::string(" & ") + a->section + "." + a->key;
            Log("trap %s armed at rva 0x%x (%s)", t.name, t.rva, switches.c_str());
        }
            break;
        case trap_detail::ArmResult::Mismatch:
            ++result.failed;
            Log("trap %s NOT armed: the site at rva 0x%x doesn't match (other version, or already patched)", t.name, t.rva);
            break;
        case trap_detail::ArmResult::NotSelected:
            break;
        }
    }
    return result;
}
