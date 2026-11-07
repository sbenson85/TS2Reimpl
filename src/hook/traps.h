#pragma once
// Traps on originals that are replaced through their callers (CLAUDE.md "Function status levels", *in effect*; status
// entries with "in_effect": "callers"). Nothing should reach such an original any more: tools/callers.py checks every
// reference in the image, and the host patches the original's entry with a jump to a stub, so a caller the check
// missed fails loudly ("trap: original 0x... reached from ...", then the process stops with kTrapExitCode) instead of
// running the original silently.
//
// A trap is armed only while its replacement is in use: each entry names a host switch (an ini section and key) and
// the values under which the replacement runs. The host reports the switches it acts on (SetTrapSwitch, with the
// default applied) before ArmTraps; a switch nobody reports never arms anything. InstallHooks reports every hook as
// `Hooks.<name>` (1 installed, 0 not): the switch of an adapter dropped because its class's constructor or factory hook
// gives every object our vtables ("trap": "Hooks.<that hook>=1"). When several hooks keep the game's vtable off objects
// (a constructor and a destructor body, a factory and a reader), the trap is their conjunction ("trap":
// "Hooks.A=1&Hooks.B=1", TrapSpec::also): armed only while every one is installed.
//
// The table (src/hook/trap_table.cpp) is generated from db/status by `tools/py -I tools/callers.py --write-traps`.
// The mechanism is inline here so the difftests can exercise it without the host (tests/hook_traps.cpp).
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <windows.h>

// One more switch a trap rests on (a conjunction, "Hooks.A=1&Hooks.B=1"): the trap arms only when every switch is
// reported at one of its values. Arrays of them end with an entry whose section is null.
struct TrapTerm {
    const char *section;
    const char *key;
    uint32_t values;
};

struct TrapSpec {
    const char *name;    // for the log
    uint32_t rva;        // the original function, 1.18.0.187
    uint32_t checkLen;   // bytes covered by checkCrc (from the start of the function, at most 32)
    uint32_t checkCrc;   // CRC32 of the unpacked exe's bytes [rva, rva + checkLen) (only the checksum, never the bytes)
    const char *section; // the switch: ts2reimpl.ini [section] key
    const char *key;
    uint32_t values;     // bit v set: armed when the switch's value is v (0..31)
    const TrapTerm *also = nullptr; // further switches, all required (null: the one switch above)
};

// Generated; ends with an entry whose name is null.
extern const TrapSpec kTrapTable[];

// Exit code of a process stopped by a trap ("TRAP").
constexpr unsigned kTrapExitCode = 0x54524150;

// The host's switch values (the default applied), reported before ArmTraps.
void SetTrapSwitch(const char *section, const char *key, int value);

struct TrapsArmed {
    int armed = 0;  // traps whose switch selects them, now patched
    int failed = 0; // selected, but the site doesn't match its CRC (other version, already patched): not armed
};
// Arms kTrapTable's selected traps in the image at `base` (mapped at its preferred base, as the host requires), logging
// each; the stub's handler logs and ends the process.
TrapsArmed ArmTraps(uint8_t *base);

namespace trap_detail {

// Called by a trap's stub with the trap's index and the trapped call's return address. The host's never returns; a
// test's may, and its result is then what the trapped function returns to its caller (the stub pops the index and
// returns, so this suits cdecl callees only).
using Handler = uint32_t(__cdecl *)(uint32_t index, uintptr_t returnAddress);

// The stub: push index ; call handler ; add esp, 4 ; ret
constexpr size_t kStubSize = 14;

inline void WriteStub(uint8_t *at, uint32_t index, Handler handler)
{
    at[0] = 0x68;
    std::memcpy(at + 1, &index, 4);
    at[5] = 0xE8;
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(handler) - reinterpret_cast<uintptr_t>(at + 10));
    std::memcpy(at + 6, &rel, 4);
    at[10] = 0x83;
    at[11] = 0xC4;
    at[12] = 0x04;
    at[13] = 0xC3;
}

inline uint32_t Crc32(const uint8_t *p, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// Is one switch reported at one of `values`? `lookup(section, key, &value)` says whether the switch was reported, and
// its value.
template <class Lookup> bool TermHolds(const char *section, const char *key, uint32_t values, Lookup lookup)
{
    int value = 0;
    if (!lookup(section, key, &value)) return false;
    return value >= 0 && value < 32 && (values >> value & 1u);
}

// Is a trap selected by the switches? Its own switch and every `also` term must hold.
template <class Lookup> bool Selected(const TrapSpec &t, Lookup lookup)
{
    if (!TermHolds(t.section, t.key, t.values, lookup)) return false;
    for (const TrapTerm *a = t.also; a && a->section; ++a)
        if (!TermHolds(a->section, a->key, a->values, lookup)) return false;
    return true;
}

enum class ArmResult { NotSelected, Mismatch, Armed };

// Checks the site and patches `jmp stub` over the function's first five bytes, with `stub` (kStubSize bytes,
// executable) written for `index`.
template <class Lookup>
ArmResult Arm(uint8_t *base, const TrapSpec &t, uint32_t index, Lookup lookup, uint8_t *stub, Handler handler)
{
    if (!Selected(t, lookup)) return ArmResult::NotSelected;
    uint8_t *site = base + t.rva;
    if (t.checkLen < 5 || Crc32(site, t.checkLen) != t.checkCrc) return ArmResult::Mismatch;
    DWORD old;
    VirtualProtect(stub, kStubSize, PAGE_EXECUTE_READWRITE, &old);
    WriteStub(stub, index, handler);
    FlushInstructionCache(GetCurrentProcess(), stub, kStubSize);
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - reinterpret_cast<uintptr_t>(site + 5));
    site[0] = 0xE9;
    std::memcpy(site + 1, &rel, 4);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return ArmResult::Armed;
}

} // namespace trap_detail
