// Differential tests for the sequence helpers in std/StdSeq.h: std::string append (from a buffer and
// from another string), the memory copy and fill helpers, vector<uint32_t>::assign and the destruction
// of a vector of 28-byte string-holding elements. Each case builds two identical objects in heap
// memory (std::string layout, storage from std::allocator as the game does), runs the original on one
// and the reimplementation on the other, then compares the results, the objects and their contents.
//
// Allocation is observed block by block: each heap block an object owned before the call must be freed
// on both sides or kept on both (a FreeRecorder across the call, free_scope.h), and the blocks it owns
// after the call must be live (HeapValidate) and of the same size. (Counting all busy blocks of the process heap isn't
// reliable: COM/shell worker threads left by the shortcut tests allocate and free at any time.)
// Blocks of 4 KB and more come from MSVC's aligned big-block scheme: the heap block's address is
// stored just below the user pointer, and is read before the call. Freed blocks are never read.
#include "difftest.h"
#include "free_scope.h"
#include "std/StdSeq.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

// The heap blocks an object owns (raw heap addresses), in a fixed order.
// A fixed array, so recording blocks after a call never allocates (a new allocation could reuse a
// block the call just freed and make it look live).
struct Blocks {
    struct List {
        void *items[64];
        size_t count = 0;
        size_t size() const { return count; }
        void *operator[](size_t i) const { return items[i]; }
        void *const *begin() const { return items; }
        void *const *end() const { return items + count; }
    } raw;
    void Add(const void *p, size_t bytes)
    {
        if (!p || raw.count == 64) return;
        raw.items[raw.count++] = bytes >= 4096 ? const_cast<void *>(static_cast<void *const *>(p)[-1]) : const_cast<void *>(p);
    }
};

bool Live(void *raw)
{
    return HeapValidate(GetProcessHeap(), 0, raw) != 0;
}

// A std::string object in the game's layout. It owns its heap block, if the capacity says it has
// one, and frees the block it has at the end.
class StrObj {
public:
    StdSeqString s{};

    // `content` at size; `capacity` is 15 (small buffer, content at most 15 bytes) or above 15 (heap
    // block of capacity + 1 bytes). The bytes not used by the content are random, from `junk`.
    StrObj(const std::string &content, size_t capacity, std::mt19937 &junk)
    {
        if (capacity <= 15) {
            for (auto &c : s.data.buf) c = static_cast<char>(junk());
            std::memcpy(s.data.buf, content.data(), content.size());
            s.data.buf[content.size()] = 0;
        } else {
            char *p = std::allocator<char>().allocate(capacity + 1);
            for (size_t i = 0; i <= capacity; ++i) p[i] = static_cast<char>(junk());
            std::memcpy(p, content.data(), content.size());
            p[content.size()] = 0;
            s.data.ptr = p;
        }
        s.size = content.size();
        s.capacity = capacity;
    }
    ~StrObj()
    {
        if (s.capacity > 15) std::allocator<char>().deallocate(s.data.ptr, s.capacity + 1);
    }
    StrObj(const StrObj &) = delete;
    StrObj &operator=(const StrObj &) = delete;
};

std::string RandomContent(std::mt19937 &rng, size_t maxLen)
{
    static const char kAlphabet[] = {'a', 'B', '\0', 'z', '_', '\xff'};
    size_t len;
    switch (rng() % 4) {
    case 0: len = 13 + rng() % 5; break; // around the small-string boundary
    case 1: len = rng() % 3; break;
    default: len = rng() % (maxLen + 1); break;
    }
    std::string s;
    for (size_t i = 0; i < len; ++i) s += kAlphabet[rng() % 6];
    return s;
}

// A string object for the test: small when it fits, otherwise heap with some spare capacity.
size_t PickCapacity(std::mt19937 &rng, size_t len)
{
    if (len <= 15 && rng() % 2 == 0) return 15;
    return (len > 15 ? len : 16) + rng() % 64;
}

// Compares two strings after the same operation. `fullBlock` says the heap block was kept (no
// reallocation), so its spare bytes are the same too; otherwise only the content and terminator are
// compared, since the bytes of a new block are not initialised.
void CompareStrings(difftest::Context &ctx, const char *what, const StdSeqString &a, const StdSeqString &b,
                    bool fullBlock, uint32_t iter)
{
    if (a.size != b.size || a.capacity != b.capacity) {
        ctx.Fail("%s: size/capacity differ (case %u): %zu/%zu vs %zu/%zu", what, iter, a.size, a.capacity, b.size,
                 b.capacity);
        return;
    }
    if (std::memcmp(a.Data(), b.Data(), a.size + 1) != 0) {
        ctx.Fail("%s: content differs (case %u)", what, iter);
        return;
    }
    if (a.capacity == 15) {
        if (std::memcmp(a.data.buf, b.data.buf, sizeof(a.data.buf)) != 0) ctx.Fail("%s: small buffer differs (case %u)", what, iter);
    } else if (fullBlock) {
        if (std::memcmp(a.data.ptr, b.data.ptr, a.capacity + 1) != 0) ctx.Fail("%s: block differs (case %u)", what, iter);
    }
}

using AppendFn = StdSeqString *(__thiscall *)(void *, const char *, size_t);
using CopyFn = uint8_t *(__cdecl *)(const uint8_t *, const uint8_t *, uint8_t *);
using FillFn = void(__cdecl *)(uint32_t *, uint32_t *, const uint32_t *);
using AssignFn = void(__thiscall *)(void *, uint32_t, uint32_t);

// The heap effects of one call, measured right after it (before the other side runs, which could
// reuse an address this call freed): for each block owned before, whether it leaked (live but no
// longer owned; a freed address handed straight back as the new block counts as owned), and the
// sizes of the blocks owned after (0 for a block that isn't live).
struct Outcome {
    bool leaked[64] = {};
    size_t sizes[64] = {};
    size_t before = 0, after = 0;
};

Outcome Measure(const Blocks &before, const Blocks &after)
{
    Outcome o;
    o.before = before.raw.size();
    o.after = after.raw.size();
    // A block owned before is judged by the FreeRecorder that ran across the call (difftest::BeginFrees());
    // the blocks owned after should be live, which HeapValidate may check.
    for (size_t i = 0; i < before.raw.size(); ++i)
        o.leaked[i] = !difftest::FreedSinceBegin(before.raw[i]) && std::find(after.raw.begin(), after.raw.end(), before.raw[i]) == after.raw.end();
    for (size_t i = 0; i < after.raw.size(); ++i) o.sizes[i] = Live(after.raw[i]) ? HeapSize(GetProcessHeap(), 0, after.raw[i]) : 0;
    return o;
}

void CompareOutcomes(difftest::Context &ctx, const char *what, uint32_t iter, const Outcome &a, const Outcome &b)
{
    if (a.before != b.before || a.after != b.after) {
        ctx.Fail("%s: owned blocks differ in number (case %u)", what, iter);
        return;
    }
    for (size_t i = 0; i < a.before; ++i)
        if (a.leaked[i] != b.leaked[i]) ctx.Fail("%s: block %zu leaked on one side only (case %u)", what, i, iter);
    for (size_t i = 0; i < a.after; ++i)
        if (a.sizes[i] == 0 || a.sizes[i] != b.sizes[i]) ctx.Fail("%s: owned block %zu: sizes %zu vs %zu (case %u)", what, i, a.sizes[i], b.sizes[i], iter);
}

Blocks StringBlocks(const StdSeqString &s)
{
    Blocks b;
    if (s.capacity > 15) b.Add(s.data.ptr, s.capacity + 1);
    return b;
}

// Appends from a buffer. Lengths cover empty, the small-string boundary, the spare capacity and
// growth well past the capacity.
void RunAppend(difftest::Context &ctx, uint32_t rva, uint32_t seed, uint32_t iterations)
{
    std::mt19937 rng(seed);
    auto original = ctx.Original<AppendFn>(rva);
    for (uint32_t iter = 0; iter < iterations && ctx.failures.empty(); ++iter) {
        ++ctx.cases;
        const std::string dst = RandomContent(rng, 40);
        const size_t cap = PickCapacity(rng, dst.size());
        size_t count;
        switch (rng() % 4) {
        case 0: count = 0; break;
        case 1: count = rng() % 20; break;
        case 2: count = 1 + rng() % 40; break;
        default: count = rng() % 400; break;
        }
        std::vector<char> src(count + 1);
        for (auto &c : src) c = static_cast<char>(rng());

        // Same junk for both copies: the copies differ only in the heap blocks they were given.
        const uint32_t junk = rng();
        std::mt19937 junkA(junk), junkB(junk);
        StrObj a(dst, cap, junkA);
        StrObj b(dst, cap, junkB);
        const char *const oldPtrA = a.s.capacity > 15 ? a.s.data.ptr : nullptr;
        const char *const oldPtrB = b.s.capacity > 15 ? b.s.data.ptr : nullptr;

        const Blocks beforeA = StringBlocks(a.s), beforeB = StringBlocks(b.s);
        difftest::BeginFrees();
        StdSeqString *ra = original(&a.s, src.data(), count);
        const Outcome outA = Measure(beforeA, StringBlocks(a.s));
        difftest::BeginFrees();
        StdSeqString *rb = b.s.Append(src.data(), count);
        const Outcome outB = Measure(beforeB, StringBlocks(b.s));

        if (ra != &a.s || rb != &b.s) {
            ctx.Fail("append: returned %p/%p, expected the object (case %u)", static_cast<void *>(ra), static_cast<void *>(rb),
                     iter);
            return;
        }
        const bool reallocA = a.s.capacity > 15 && a.s.data.ptr != oldPtrA;
        const bool reallocB = b.s.capacity > 15 && b.s.data.ptr != oldPtrB;
        if (reallocA != reallocB) ctx.Fail("append: reallocation differs (case %u)", iter);
        CompareStrings(ctx, "append", a.s, b.s, !reallocA && !reallocB, iter);
        CompareOutcomes(ctx, "append", iter, outA, outB);
    }
}

// Copies [first, last) into a buffer with a canary after it; the copy's result is the end pointer.
void RunCopy(difftest::Context &ctx, uint32_t seed, uint32_t iterations)
{
    std::mt19937 rng(seed);
    auto original = ctx.Original<CopyFn>(0x146090);
    for (uint32_t iter = 0; iter < iterations && ctx.failures.empty(); ++iter) {
        ++ctx.cases;
        size_t n;
        switch (rng() % 5) {
        case 0: n = 0; break;
        case 1: n = 1 + rng() % 4; break;
        case 2: n = rng() % 64; break;
        case 3: n = 4090 + rng() % 12; break; // around the 4 KB block size
        default: n = rng() % 5000; break;
        }
        std::vector<uint8_t> src(n + 8), destA(n + 16), destB(n + 16);
        for (auto &c : src) c = static_cast<uint8_t>(rng());
        for (size_t i = 0; i < destA.size(); ++i) destA[i] = destB[i] = static_cast<uint8_t>(rng());
        // Non-overlapping: the copy is memcpy, which the original and the replacement share.
        const uint8_t *first = src.data();
        const uint8_t *last = src.data() + n;

        uint8_t *ra = original(first, last, destA.data());
        uint8_t *rb = StdSeqCopyBytes(first, last, destB.data());
        if (ra != destA.data() + n || rb != destB.data() + n) {
            ctx.Fail("copy: end pointer %td/%td, expected %zu (case %u)", ra - destA.data(), rb - destB.data(), n, iter);
            return;
        }
        if (destA != destB) ctx.Fail("copy: destination differs (case %u, %zu bytes)", iter, n);
    }
}

// Fills [first, last) of uint32_t with a value (lengths include 0 and one element).
void RunFill(difftest::Context &ctx, uint32_t rva, uint32_t seed, uint32_t iterations)
{
    std::mt19937 rng(seed);
    auto original = ctx.Original<FillFn>(rva);
    for (uint32_t iter = 0; iter < iterations && ctx.failures.empty(); ++iter) {
        ++ctx.cases;
        const size_t n = rng() % 4 == 0 ? rng() % 3 : rng() % 1100;
        std::vector<uint32_t> a(n + 4), b(n + 4);
        for (size_t i = 0; i < a.size(); ++i) a[i] = b[i] = static_cast<uint32_t>(rng());
        const uint32_t value = static_cast<uint32_t>(rng());
        uint32_t *first = a.data() + 1; // starts inside the buffer, so the range is not at its start
        uint32_t *last = first + n;
        uint32_t valueA = value, valueB = value;
        original(first, last, &valueA);
        StdSeqFillU32(b.data() + 1, b.data() + 1 + n, &valueB);
        if (a != b) ctx.Fail("fill: result differs (case %u, %zu elements)", iter, n);
    }
}

// Assigns (count, value) to a vector with storage of random capacity. Counts below, equal to and
// above the size, and counts that need a block of 4 KB and over (the MSVC alignment header).
void RunAssign(difftest::Context &ctx, uint32_t seed, uint32_t iterations)
{
    std::mt19937 rng(seed);
    auto original = ctx.Original<AssignFn>(0x15fdc0);
    for (uint32_t iter = 0; iter < iterations && ctx.failures.empty(); ++iter) {
        ++ctx.cases;
        size_t size = rng() % 4 == 0 ? rng() % 3 : rng() % 300;
        size_t cap = size + (rng() % 3 == 0 ? 0 : rng() % 40);
        uint32_t count;
        switch (rng() % 4) {
        case 0: count = static_cast<uint32_t>(size); break;
        case 1: count = static_cast<uint32_t>(size + 1 + rng() % 8); break;
        case 2: count = static_cast<uint32_t>(rng() % 300); break;
        default: count = 1020 + static_cast<uint32_t>(rng() % 20); break; // 4 KB around
        }
        const uint32_t value = static_cast<uint32_t>(rng());

        std::allocator<uint32_t> alloc;
        StdSeqVectorU32 a{nullptr, nullptr, nullptr}, b{nullptr, nullptr, nullptr};
        if (cap != 0) {
            a.first = alloc.allocate(cap);
            b.first = alloc.allocate(cap);
            for (size_t i = 0; i < cap; ++i) a.first[i] = b.first[i] = static_cast<uint32_t>(rng());
            a.last = a.first + size;
            b.last = b.first + size;
            a.capEnd = a.first + cap;
            b.capEnd = b.first + cap;
        }

        Blocks beforeA, beforeB;
        beforeA.Add(a.first, cap * 4);
        beforeB.Add(b.first, cap * 4);
        difftest::BeginFrees();
        original(&a, count, value);
        Blocks afterA;
        afterA.Add(a.first, static_cast<size_t>(a.capEnd - a.first) * 4);
        const Outcome outA = Measure(beforeA, afterA);
        difftest::BeginFrees();
        b.Assign(count, value);
        Blocks afterB;
        afterB.Add(b.first, static_cast<size_t>(b.capEnd - b.first) * 4);
        const Outcome outB = Measure(beforeB, afterB);

        const size_t sizeA = static_cast<size_t>(a.last - a.first), sizeB = static_cast<size_t>(b.last - b.first);
        const size_t capA = static_cast<size_t>(a.capEnd - a.first), capB = static_cast<size_t>(b.capEnd - b.first);
        if (sizeA != sizeB || capA != capB) {
            ctx.Fail("assign: size/capacity differ (case %u, count %u, size %zu): %zu/%zu vs %zu/%zu", iter, count, size,
                     sizeA, capA, sizeB, capB);
        } else if (!std::equal(a.first, a.last, b.first)) {
            ctx.Fail("assign: contents differ (case %u, count %u)", iter, count);
        }
        CompareOutcomes(ctx, "assign", iter, outA, outB);

        // Both copies end with their elements in fresh or original storage; release what we own.
        if (a.first) alloc.deallocate(a.first, capA);
        if (b.first) alloc.deallocate(b.first, capB);
        if (!ctx.failures.empty()) return;
    }
}

} // namespace

DIFF_TEST(StdSeq_StringAppend_1603f0, 0x1603f0) { RunAppend(ctx, 0x1603f0, 2001, 3000); }
DIFF_TEST(StdSeq_CopyBytes_146090, 0x146090) { RunCopy(ctx, 2003, 3000); }
DIFF_TEST(StdSeq_FillU32_15b5e0, 0x15b5e0) { RunFill(ctx, 0x15b5e0, 2004, 3000); }
DIFF_TEST(StdSeq_FillU32_1d88e0, 0x1d88e0) { RunFill(ctx, 0x1d88e0, 2007, 3000); }
DIFF_TEST(StdSeq_VectorU32Assign_15fdc0, 0x15fdc0) { RunAssign(ctx, 2005, 2000); }
