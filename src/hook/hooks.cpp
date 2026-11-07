#include "hook/hooks.h"

#include "hook/log.h"
#include "hook/traps.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

// Each installed hook jumps through a stub that counts calls before jumping to the replacement:
//   lock inc dword ptr [counter] ; jmp replacement      (7 + 5 bytes)
// Flags are clobbered, which is fine at a function entry.
constexpr size_t kStubSize = 12;

// Installed hooks (sorted by RVA) and their call counters, which the stubs increment.
std::vector<const HookSpec *> gHooks;
volatile LONG *gCalls;

uint8_t *MakeCountingStub(uint8_t *at, volatile LONG *counter, void *target)
{
    at[0] = 0xF0;
    at[1] = 0xFF;
    at[2] = 0x05;
    uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(counter));
    std::memcpy(at + 3, &addr, 4);
    at[7] = 0xE9;
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(at + 12));
    std::memcpy(at + 8, &rel, 4);
    return at;
}

} // namespace

uint32_t Crc32(const void *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool IsSupportedGame(const uint8_t *base)
{
    auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp == 0x67f41bfe && nt->OptionalHeader.SizeOfImage == 0x120f000;
}

namespace {

constexpr uint32_t kPreferredBase = 0xA0000000; // ImageBase in the exe's header (1.18.0.187)

// RVAs of every HIGHLOW relocation, sorted, from the image's own relocation directory.
std::vector<uint32_t> RelocationRvas(const uint8_t *base)
{
    auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    std::vector<uint32_t> rvas;
    for (uint32_t off = 0; off < dir.Size;) {
        auto *block = reinterpret_cast<const IMAGE_BASE_RELOCATION *>(base + dir.VirtualAddress + off);
        if (block->SizeOfBlock == 0) break;
        auto *entries = reinterpret_cast<const uint16_t *>(block + 1);
        uint32_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
        for (uint32_t i = 0; i < count; ++i) {
            if ((entries[i] >> 12) == IMAGE_REL_BASED_HIGHLOW) rvas.push_back(block->VirtualAddress + (entries[i] & 0xFFF));
        }
        off += block->SizeOfBlock;
    }
    std::sort(rvas.begin(), rvas.end());
    return rvas;
}

// CRC of [rva, rva+len) as the bytes are on disk: relocated dwords overlapping the range are
// shifted back by the load delta first, so sites containing absolute addresses still match.
uint32_t SiteCrc(const uint8_t *base, const std::vector<uint32_t> &relocs, uint32_t rva, uint32_t len)
{
    uint8_t buf[3 + 64 + 3];
    if (len > 64) return 0;
    std::memcpy(buf, base + rva - 3, len + 6);
    uint32_t delta = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base) - kPreferredBase);
    for (auto it = std::lower_bound(relocs.begin(), relocs.end(), rva - 3); it != relocs.end() && *it < rva + len; ++it) {
        uint32_t v;
        std::memcpy(&v, buf + (*it - (rva - 3)), 4);
        v -= delta;
        std::memcpy(buf + (*it - (rva - 3)), &v, 4);
    }
    return Crc32(buf + 3, len);
}

} // namespace

int InstallHooks(uint8_t *base, const char *iniPath)
{
    std::vector<const HookSpec *> hooks = RegisteredHooks();
    std::sort(hooks.begin(), hooks.end(), [](const HookSpec *a, const HookSpec *b) { return a->rva < b->rva; });
    auto *stubs = static_cast<uint8_t *>(VirtualAlloc(nullptr, hooks.size() * kStubSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    gCalls = new LONG[hooks.size()]();
    const std::vector<uint32_t> relocs = RelocationRvas(base);
    // Safe even while game code runs (the retired in-game plugin installed that way): every stub is
    // written and executable before any site jumps to it, and each site's jump is one atomic store.
    std::vector<uint8_t *> sites;
    for (size_t i = 0; i < hooks.size(); ++i) {
        const HookSpec &h = *hooks[i];
        if (i > 0 && hooks[i - 1]->rva == h.rva) {
            Log("hook %s skipped: rva 0x%x is already hooked by %s", h.name, h.rva, hooks[i - 1]->name);
            continue;
        }
        if (!GetPrivateProfileIntA("Hooks", h.name, 1, iniPath)) {
            Log("hook %s disabled by ini", h.name);
            continue;
        }
        uint8_t *site = base + h.rva;
        if (SiteCrc(base, relocs, h.rva, h.checkLen) != h.checkCrc) {
            Log("hook %s skipped: site at rva 0x%x doesn't match (other version, or patched by another mod)", h.name, h.rva);
            continue;
        }
        MakeCountingStub(stubs + gHooks.size() * kStubSize, &gCalls[gHooks.size()], h.replacement);
        gHooks.push_back(&h);
        sites.push_back(site);
    }
    DWORD old;
    VirtualProtect(stubs, hooks.size() * kStubSize, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), stubs, hooks.size() * kStubSize);

    for (size_t i = 0; i < sites.size(); ++i) {
        uint8_t *site = sites[i];
        uint8_t *stub = stubs + i * kStubSize;
        VirtualProtect(site, 8, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - reinterpret_cast<uintptr_t>(site + 5));
        auto *word = reinterpret_cast<volatile LONGLONG *>(site);
        LONGLONG before = *word, after;
        do {
            after = before;
            auto *bytes = reinterpret_cast<uint8_t *>(&after);
            bytes[0] = 0xE9;
            std::memcpy(bytes + 1, &rel, 4);
        } while (InterlockedCompareExchange64(word, after, before) != before && ((before = *word), true));
        VirtualProtect(site, 8, old, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    for (const HookSpec *h : gHooks) Log("hook %s installed at rva 0x%x", h->name, h->rva);
    // Traps on originals whose adapters were dropped (their vtables die while the class's constructor or factory hook
    // is in) name that hook's switch: report each hook, installed or not (gHooks is in the same RVA order as hooks).
    for (size_t i = 0, k = 0; i < hooks.size(); ++i) {
        const bool installed = k < gHooks.size() && gHooks[k] == hooks[i];
        if (installed) ++k;
        SetTrapSwitch("Hooks", hooks[i]->name, installed ? 1 : 0);
    }
    return static_cast<int>(gHooks.size());
}

bool LogHookCountsIfChanged()
{
    static std::vector<LONG> last;
    last.resize(gHooks.size());
    bool changed = false;
    std::string line = "calls:";
    for (size_t i = 0; i < gHooks.size(); ++i) {
        LONG n = gCalls[i];
        changed |= n != last[i];
        last[i] = n;
        line += " " + std::string(gHooks[i]->name) + "=" + std::to_string(n);
    }
    if (changed) Log("%s", line.c_str());
    return changed;
}
