// Differential tests for the expression evaluator's name table (gz/EvalFunctionTable.h): the
// constructor 0xb094b0 and the original's out-of-line std::sort / vector pieces for its element type
// (0xb09010 _Sort_unchecked, 0xb08a30 _Partition_by_median_guess_unchecked, 0xb08f50
// vector::_Resize_reallocate). Arrays hold names drawn from a small pool with repeats (so the order
// of equal names, which an introsort does not keep, shows whether both sides run the same algorithm)
// and distinct object pointers, so every permutation is visible.
#include "difftest.h"
#include "gz/EvalFunctionTable.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

using nGZCommandParser::cEvalFunction;
using nGZCommandParser::EvalFunctionTable;
using nGZCommandParser::EvalFunctionTableAdapters;
using nGZCommandParser::EvalFunctionTableStl;
using nGZCommandParser::EvalFunctionTableVector;
using Entry = EvalFunctionTable::Entry;
using ByName = EvalFunctionTable::ByName;

namespace {

// Names over a small alphabet (many shared prefixes and repeats), the empty name and high-bit bytes
// (strcmp compares unsigned bytes).
std::vector<std::string> NamePool(std::mt19937 &gen, size_t count)
{
    std::vector<std::string> pool;
    pool.push_back("");
    for (size_t i = 1; i < count; ++i) {
        std::string name;
        size_t length = 1 + gen() % 6;
        for (size_t c = 0; c < length; ++c)
            name.push_back(static_cast<char>(gen() % 5 == 0 ? 0x80 + gen() % 0x7f : 'a' + gen() % 3));
        pool.push_back(name);
    }
    return pool;
}

std::vector<Entry> RandomEntries(std::mt19937 &gen, const std::vector<std::string> &pool, size_t count)
{
    std::vector<Entry> entries;
    for (size_t i = 0; i < count; ++i)
        entries.push_back({pool[gen() % pool.size()].c_str(), reinterpret_cast<cEvalFunction *>(static_cast<uintptr_t>(0x1000 + i * 8))});
    return entries;
}

bool SameEntries(const Entry *a, const Entry *b, size_t count)
{
    return std::memcmp(a, b, count * sizeof(Entry)) == 0;
}

} // namespace

DIFF_TEST(EvalFunctionTable_Construct, 0xb094b0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xb094b0);
    for (int round = 0; round < 20; ++round) {
        alignas(EvalFunctionTable) uint8_t rawA[sizeof(EvalFunctionTable)], rawB[sizeof(EvalFunctionTable)];
        std::memset(rawA, 0xcd - round, sizeof(rawA));
        std::memset(rawB, 0xcd - round, sizeof(rawB));
        void *retA = orig(rawA);
        void *retB = EvalFunctionTableAdapters::Construct(rawB, nullptr);
        auto *a = reinterpret_cast<EvalFunctionTable *>(rawA);
        auto *b = reinterpret_cast<EvalFunctionTable *>(rawB);
        ++ctx.cases;
        if (retA != rawA || retB != rawB) ctx.Fail("returned %p / %p, expected this", retA, retB);
        if (a->mEntries.size() != EvalFunctionTable::kBuiltinCount || b->mEntries.size() != a->mEntries.size() ||
            a->mEntries.capacity() != b->mEntries.capacity())
            ctx.Fail("size %zu/%zu, capacity %zu/%zu", a->mEntries.size(), b->mEntries.size(), a->mEntries.capacity(),
                     b->mEntries.capacity());
        else if (!SameEntries(a->mEntries.data(), b->mEntries.data(), a->mEntries.size()))
            ctx.Fail("entries differ");
        a->mEntries.~vector();
        b->mEntries.~vector();
    }
}

DIFF_TEST(EvalFunctionTable_SortUnchecked, 0xb09010)
{
    auto orig = ctx.Original<void(__cdecl *)(Entry *, Entry *, ptrdiff_t, ByName)>(0xb09010);
    std::mt19937 gen(9010);
    for (int round = 0; round < 3000; ++round) {
        auto pool = NamePool(gen, 2 + gen() % (round % 2 ? 8 : 60));
        size_t count = round < 40 ? round : gen() % (round % 7 == 0 ? 600 : 120);
        auto a = RandomEntries(gen, pool, count);
        auto b = a;
        // ideal: the element count, as std::sort passes it; small values force the heap-sort fallback.
        ptrdiff_t ideal = round % 4 == 0 ? static_cast<ptrdiff_t>(gen() % 4) : static_cast<ptrdiff_t>(count);
        orig(a.data(), a.data() + count, ideal, ByName());
        EvalFunctionTableStl::SortUnchecked(b.data(), b.data() + count, ideal, ByName());
        ++ctx.cases;
        if (!SameEntries(a.data(), b.data(), count)) ctx.Fail("%zu entries (ideal %d) sorted differently", count, ideal);
    }
}

DIFF_TEST(EvalFunctionTable_PartitionByMedianGuess, 0xb08a30)
{
    auto orig = ctx.Original<std::pair<Entry *, Entry *>(__cdecl *)(Entry *, Entry *, ByName)>(0xb08a30);
    std::mt19937 gen(0x8a30);
    for (int round = 0; round < 3000; ++round) {
        auto pool = NamePool(gen, 2 + gen() % (round % 2 ? 6 : 80));
        size_t count = 1 + (round < 60 ? round : gen() % (round % 5 == 0 ? 500 : 100));
        auto a = RandomEntries(gen, pool, count);
        auto b = a;
        auto ra = orig(a.data(), a.data() + count, ByName());
        auto rb = EvalFunctionTableStl::PartitionByMedianGuess(b.data(), b.data() + count, ByName());
        ++ctx.cases;
        if (ra.first - a.data() != rb.first - b.data() || ra.second - a.data() != rb.second - b.data())
            ctx.Fail("%zu entries: partition [%d, %d) vs [%d, %d)", count, static_cast<int>(ra.first - a.data()),
                     static_cast<int>(ra.second - a.data()), static_cast<int>(rb.first - b.data()), static_cast<int>(rb.second - b.data()));
        else if (!SameEntries(a.data(), b.data(), count))
            ctx.Fail("%zu entries partitioned differently", count);
    }
}

DIFF_TEST(EvalFunctionTable_ResizeReallocate, 0xb08f50)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, size_t, const void *)>(0xb08f50);
    std::mt19937 gen(0x8f50);
    const char tag = 0;
    for (int round = 0; round < 2000; ++round) {
        // Start states: empty, or some elements with spare capacity; sizes up to past the 4 KB block
        // threshold (512 entries) where the allocator switches to its aligned big-block scheme.
        size_t capacity = round < 4 ? round : gen() % (round % 5 == 0 ? 1200 : 40);
        size_t size = capacity ? gen() % (capacity + 1) : 0;
        size_t newSize = capacity + 1 + gen() % (round % 3 == 0 ? 3 : 700); // the precondition: newSize > capacity()
        EvalFunctionTableVector va, vb;
        va.mVector.reserve(capacity);
        vb.mVector.reserve(capacity);
        for (size_t i = 0; i < size; ++i) {
            Entry e{reinterpret_cast<const char *>(static_cast<uintptr_t>(gen())), reinterpret_cast<cEvalFunction *>(static_cast<uintptr_t>(gen()))};
            va.mVector.push_back(e);
            vb.mVector.push_back(e);
        }
        size_t capBefore = va.mVector.capacity();
        orig(&va, newSize, &tag);
        vb.ResizeReallocate(newSize, &tag);
        ++ctx.cases;
        if (va.mVector.size() != newSize || vb.mVector.size() != newSize || va.mVector.capacity() != vb.mVector.capacity())
            ctx.Fail("from %zu/%zu to %zu: size %zu/%zu, capacity %zu/%zu", size, capBefore, newSize, va.mVector.size(),
                     vb.mVector.size(), va.mVector.capacity(), vb.mVector.capacity());
        else if (!SameEntries(va.mVector.data(), vb.mVector.data(), newSize))
            ctx.Fail("from %zu/%zu to %zu: contents differ", size, capBefore, newSize);
    }
}
