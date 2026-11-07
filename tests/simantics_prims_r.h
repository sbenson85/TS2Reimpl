#pragma once
// Test helpers of the lane R handler tests: queue-driven fake slots on top of tests/simantics_prims_mid.h.
#include "simantics_prims_mid.h"
#include "simantics/PrimR.h"

#include <windows.h>

namespace {

// Queue-driven fakes: slot S pops the next scripted object (a world offset, 0xffffffff: null), whatever the
// arguments; the arguments are logged. Used where one slot answers differently per call.
std::vector<uint32_t> gQ[512];
size_t gQPos[512];

uint32_t Pop(int slot)
{
    const std::vector<uint32_t> &q = gQ[slot];
    const uint32_t e = gQPos[slot] < q.size() ? q[gQPos[slot]++] : 0xffffffffu;
    return e == 0xffffffffu ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW) + e);
}

void QObj(int slot, const void *target, World &w)
{
    gQ[slot].push_back(target ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(&w)) : 0xffffffffu);
}

template <int S> uint32_t __fastcall Q1(void *self, void *, uint32_t a1)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(S) << 8 | 1u);
    gTrace.push_back(Norm(a1));
    return Pop(S);
}

template <int S> uint32_t __fastcall Q2(void *self, void *, uint32_t a1, uint32_t a2)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(S) << 8 | 2u);
    gTrace.push_back(Norm(a1));
    gTrace.push_back(Norm(a2));
    return Pop(S);
}

// The operand reader with one answer per call (the framework's Read7 answers every call alike): the values come
// from gReadQ in call order; the destination, when given, is filled as Read7 does.
std::vector<uint32_t> gReadQ;
size_t gReadPos = 0;

uint32_t __fastcall ReadQ(void *self, void *, uint32_t scope, uint32_t id, SimDataRef *dest, float **fdest, uint32_t *a5, uint32_t a6,
                          uint32_t a7)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(7u << 8 | 7u);
    gTrace.insert(gTrace.end(), {scope, id, dest ? 1u : 0u, fdest ? 1u : 0u, a5 ? 1u : 0u, a6, a7});
    if (dest) {
        dest->mLength = 1;
        dest->mIndex = 0;
        dest->mPtr = gRead.hasTarget ? &gW->cells[gRead.cell] : nullptr;
        dest->mMin = gRead.min;
        dest->mMax = gRead.max;
    }
    return gReadPos < gReadQ.size() ? gReadQ[gReadPos++] : 0u;
}

void ResetQueues()
{
    for (size_t &p : gQPos) p = 0;
    gReadPos = 0;
}

void ClearQueues()
{
    for (std::vector<uint32_t> &q : gQ) q.clear();
    gReadQ.clear();
}

uint32_t Wide16(std::mt19937 &r)
{
    return static_cast<uint32_t>(static_cast<uint16_t>(Edge16(r))) | (r() & 0xffff0000u);
}

// Redirects a function of the mapped image to a stand-in (a jmp over its first five bytes) while it lives.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(difftest::Context &ctx, uint32_t rva, void *stand) : CodePatch(ctx.image.At<uint8_t>(rva), stand) {}
    // The same on our own code (a replacement the code under test reaches directly, not through the image), so a callee's
    // stand-in serves both sides of the caller's test.
    CodePatch(void *ours, void *stand) : site(static_cast<uint8_t *>(ours))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

} // namespace
