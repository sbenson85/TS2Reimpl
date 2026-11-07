#pragma once
// Shared pieces of the nGZLog differential tests (tests/gzlog_*.cpp): IAT fakes, random fill,
// masked comparison of two object images, and a canonical text dump of the service's object graph.
#include "difftest.h"
#include "free_recorder.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZStopwatch.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace gzlog_test {

// Replaces entries of the mapped image's import table (or any pointer slot) for the length of a
// test, restoring them on destruction. Both the original and our code call through these slots.
class Slots {
public:
    explicit Slots(difftest::Context &c) : ctx(c) {}
    ~Slots()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }
    Slots(const Slots &) = delete;
    Slots &operator=(const Slots &) = delete;
    void Set(uint32_t rva, void *fake)
    {
        void **slot = ctx.image.At<void *>(rva);
        saved.emplace_back(rva, *slot);
        *slot = fake;
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
};

// Scripted clocks for the original (its GetSystemTimeAsFileTime and QueryPerformanceCounter import
// slots) and for ours (the stopwatch's counter override; our time comes from the CRT's _time64, which calls our own
// GetSystemTimeAsFileTime import: difftest::SwapOwnImport). Call Rewind() before each side so both read the same script.
struct Clocks {
    static constexpr uint32_t kIatGetSystemTimeAsFileTime = 0xd9a2a4, kIatQueryPerformanceCounter = 0xd9a24c;
    uint64_t fileTime = 0;           // what GetSystemTimeAsFileTime returns
    std::vector<uint64_t> counter;   // successive QueryPerformanceCounter readings (the last repeats)
    size_t next = 0;
    int fileTimeCalls = 0;

    void Rewind() { next = 0; fileTimeCalls = 0; }
    uint64_t NextCounter()
    {
        uint64_t v = counter.empty() ? 0 : counter[std::min(next, counter.size() - 1)];
        ++next;
        return v;
    }
    static inline Clocks *current = nullptr;
    static void WINAPI FakeFileTime(LPFILETIME ft)
    {
        ++current->fileTimeCalls;
        ft->dwLowDateTime = static_cast<DWORD>(current->fileTime);
        ft->dwHighDateTime = static_cast<DWORD>(current->fileTime >> 32);
    }
    static BOOL WINAPI FakeQpc(LARGE_INTEGER *out)
    {
        out->QuadPart = static_cast<int64_t>(current->NextCounter());
        return TRUE;
    }
    static void FakeCounter(uint64_t *out) { *out = current->NextCounter(); }
};

// Installs the clocks for both sides and restores everything on destruction.
class ClockScope {
public:
    ClockScope(difftest::Context &ctx, Clocks &clocks) : slots(ctx)
    {
        Clocks::current = &clocks;
        slots.Set(Clocks::kIatGetSystemTimeAsFileTime, reinterpret_cast<void *>(&Clocks::FakeFileTime));
        slots.Set(Clocks::kIatQueryPerformanceCounter, reinterpret_cast<void *>(&Clocks::FakeQpc));
        cRZStopwatch::sPerformanceCounterOverride = &Clocks::FakeCounter;
        savedOwnFileTime = difftest::SwapOwnImport("GetSystemTimeAsFileTime", reinterpret_cast<void *>(&Clocks::FakeFileTime));
    }
    ~ClockScope()
    {
        difftest::SwapOwnImport("GetSystemTimeAsFileTime", savedOwnFileTime);
        cRZStopwatch::sPerformanceCounterOverride = nullptr;
        Clocks::current = nullptr;
    }

private:
    Slots slots;
    void *savedOwnFileTime;
};

// Two images of the same size filled with the same random bytes.
struct Twin {
    std::vector<uint8_t> raw;
    Twin(size_t size, std::mt19937 &rng) : raw(size + 16)
    {
        for (uint8_t &b : raw) b = static_cast<uint8_t>(rng());
    }
};

// A pair of identically filled, 8-aligned blocks (a for the original, b for ours).
struct Pair {
    alignas(16) std::vector<uint8_t> storeA, storeB;
    uint8_t *a, *b;
    size_t size;
    Pair(size_t n, std::mt19937 &rng) : storeA(n + 32), storeB(n + 32), size(n)
    {
        a = storeA.data() + 16 - reinterpret_cast<uintptr_t>(storeA.data()) % 16;
        b = storeB.data() + 16 - reinterpret_cast<uintptr_t>(storeB.data()) % 16;
        for (size_t i = 0; i < n; ++i) a[i] = b[i] = static_cast<uint8_t>(rng());
        // Canaries after the object (the constructor must not write past its size).
        for (size_t i = n; i < n + 16 && i < n + 32 - 16; ++i) a[i] = b[i] = 0xA5;
    }
    bool CanariesIntact() const
    {
        for (size_t i = size; i < size + 8; ++i)
            if (a[i] != 0xA5 || b[i] != 0xA5) return false;
        return true;
    }
};

// Compares a[0, n) with b[0, n) except the byte ranges in `skip` (offset, length). Returns the first
// differing offset, or -1.
inline int FirstDifference(const uint8_t *a, const uint8_t *b, size_t n, std::vector<std::pair<size_t, size_t>> skip = {})
{
    for (size_t i = 0; i < n; ++i) {
        bool skipped = false;
        for (auto &s : skip)
            if (i >= s.first && i < s.first + s.second) skipped = true;
        if (!skipped && a[i] != b[i]) return static_cast<int>(i);
    }
    return -1;
}

// A critical section member: same vtable, a live block with a zero lock count on each side. The
// block pointers differ, so the caller skips the 8 bytes in its masked compare.
inline bool SameLock(const cRZCriticalSection &a, const cRZCriticalSection &b, std::string &why)
{
    if (a.mVTable != b.mVTable) { why = "vtable"; return false; }
    if (!a.mBlock || !b.mBlock) { why = "no block"; return false; }
    if (a.mBlock->lockCount != b.mBlock->lockCount) { why = "lock count"; return false; }
    return true;
}

// The empty hash container the constructors build: max load factor 1.0 at +0, a list head at +4
// (its own successor and predecessor, size 0 at +8), 16 bucket entries all pointing at the head,
// mask 7 and 8 at +0x18 / +0x1c.
struct HashShape {
    uint32_t load, head, listSize, vecFirst, vecLast, vecEnd, mask, maxIdx;
    size_t headBlock, vecBlock;
    bool linksOk, bucketsOk;
};

inline HashShape Inspect(const uint8_t *obj)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(obj);
    HashShape s{};
    s.load = w[0];
    s.head = w[1];
    s.listSize = w[2];
    s.vecFirst = w[3];
    s.vecLast = w[4];
    s.vecEnd = w[5];
    s.mask = w[6];
    s.maxIdx = w[7];
    const uint32_t *head = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(s.head));
    s.headBlock = HeapSize(GetProcessHeap(), 0, const_cast<uint32_t *>(head));
    s.linksOk = head && head[0] == s.head && head[1] == s.head;
    s.vecBlock = HeapSize(GetProcessHeap(), 0, reinterpret_cast<void *>(static_cast<uintptr_t>(s.vecFirst)));
    s.bucketsOk = s.vecLast - s.vecFirst == 0x40;
    const uint32_t *buckets = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(s.vecFirst));
    for (int k = 0; s.bucketsOk && k < 16; ++k) s.bucketsOk = buckets[k] == s.head;
    return s;
}

// Frees the two blocks of an empty hash container built in raw memory.
inline void FreeHash(const uint8_t *obj)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(obj);
    ::operator delete(reinterpret_cast<void *>(static_cast<uintptr_t>(w[1])));
    ::operator delete(reinterpret_cast<void *>(static_cast<uintptr_t>(w[3])));
}

} // namespace gzlog_test
