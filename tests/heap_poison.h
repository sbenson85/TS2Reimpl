#pragma once
// HeapPoisonScope (lane DT): while one is alive, every block HeapAlloc hands out without HEAP_ZERO_MEMORY is filled
// with a byte pattern before the caller sees it, on both sides: the original image's malloc (__malloc_base 0x8461f2
// calls HeapAlloc through the image's import slot, RVA 0xd9a1d8) and our CRT's (this executable's import slot). The
// heap (low-fragmentation, randomised) does not hand back the block a test just freed, so poisoning one freed block
// leaves the words a constructor doesn't store as leftovers of earlier objects; under this scope they are the pattern
// in both runs, and a Create/factory test can compare the whole object exactly.
#include "difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace heappoison {

inline uint8_t gPattern = 0xcd;
inline decltype(&HeapAlloc) gReal = nullptr;

inline LPVOID WINAPI PoisoningHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    LPVOID p = gReal(heap, flags, bytes);
    if (p && (flags & HEAP_ZERO_MEMORY) == 0) std::memset(p, gPattern, bytes);
    return p;
}

// This executable's import slot for KERNEL32!HeapAlloc (our statically linked CRT allocates through it).
inline void **OwnHeapAllocSlot()
{
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto *d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name != 0; ++d) {
        if (_stricmp(reinterpret_cast<const char *>(base + d->Name), "KERNEL32.dll") != 0) continue;
        const auto *names = reinterpret_cast<const IMAGE_THUNK_DATA32 *>(base + d->OriginalFirstThunk);
        auto *slots = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + d->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            const auto *byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char *>(byName->Name), "HeapAlloc") == 0) return reinterpret_cast<void **>(&slots->u1.Function);
        }
    }
    return nullptr;
}

class Scope {
public:
    Scope(difftest::Context &ctx, uint8_t pattern)
    {
        slots_[0] = ctx.image.At<void *>(0xd9a1d8);
        slots_[1] = OwnHeapAllocSlot();
        if (!slots_[1]) ctx.Fail("heap poison: no HeapAlloc import slot in this executable");
        gReal = reinterpret_cast<decltype(&HeapAlloc)>(slots_[1] ? *slots_[1] : *slots_[0]);
        gPattern = pattern;
        for (int i = 0; i < 2; ++i) {
            if (!slots_[i]) continue;
            saved_[i] = *slots_[i];
            Write(slots_[i], reinterpret_cast<void *>(&PoisoningHeapAlloc));
        }
    }
    ~Scope()
    {
        for (int i = 1; i >= 0; --i)
            if (slots_[i]) Write(slots_[i], saved_[i]);
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    static void Write(void **slot, void *value)
    {
        DWORD old;
        VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
        *slot = value;
        VirtualProtect(slot, sizeof(void *), old, &old);
    }
    void **slots_[2] = {nullptr, nullptr};
    void *saved_[2] = {nullptr, nullptr};
};

} // namespace heappoison
