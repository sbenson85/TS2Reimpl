// Differential tests for the original's std::sort pieces for NameValueTable's element type,
// pair<std::string, uint32_t> ordered by ByLengthThenReverseText (std/NameValueTableSort.h), and for
// the comparator itself (0xaf6800). Names are short (in-place) and long (heap) strings over a small
// alphabet, with repeats, and every element carries a distinct value, so any difference in the
// resulting permutation (an introsort does not keep the order of equal names) shows.
#include "difftest.h"
#include "std/NameValueTableSort.h"

#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using Entry = NameValueTableSort::Entry;
using Less = NameValueTableSort::Less;

std::string RandomName(std::mt19937 &gen, int round)
{
    // Lengths cluster so that equal lengths (compared by text) are common; some exceed the 15-byte
    // in-place buffer; signed characters (the comparison is on signed chars) appear in some rounds.
    size_t length = gen() % 4 == 0 ? 14 + gen() % 6 : gen() % 4;
    std::string name;
    for (size_t c = 0; c < length; ++c)
        name.push_back(static_cast<char>(round % 3 == 0 && gen() % 4 == 0 ? 0x80 + gen() % 0x80 : 'a' + gen() % 3));
    return name;
}

std::vector<Entry> RandomEntries(std::mt19937 &gen, int round, size_t count)
{
    std::vector<std::string> pool;
    size_t poolSize = 1 + gen() % (round % 2 ? 6 : 40);
    for (size_t i = 0; i < poolSize; ++i) pool.push_back(RandomName(gen, round));
    std::vector<Entry> entries;
    for (size_t i = 0; i < count; ++i) entries.emplace_back(pool[gen() % pool.size()], static_cast<uint32_t>(i));
    return entries;
}

bool Same(const std::vector<Entry> &a, const std::vector<Entry> &b)
{
    return a == b; // names and values, element by element
}

size_t Count(std::mt19937 &gen, int round, size_t big)
{
    return round < 40 ? static_cast<size_t>(round) : gen() % (round % 7 == 0 ? big : 70);
}

} // namespace

DIFF_TEST(NameValueTableSort_SortUnchecked, 0xaf5fe0)
{
    auto orig = ctx.Original<void(__cdecl *)(Entry *, Entry *, ptrdiff_t, Less)>(0xaf5fe0);
    std::mt19937 gen(0x5fe0);
    for (int round = 0; round < 1500; ++round) {
        size_t count = Count(gen, round, 400);
        auto a = RandomEntries(gen, round, count);
        auto b = a;
        // ideal: the element count, as std::sort passes it; small values force the heap-sort fallback.
        ptrdiff_t ideal = round % 4 == 0 ? static_cast<ptrdiff_t>(gen() % 4) : static_cast<ptrdiff_t>(count);
        orig(a.data(), a.data() + count, ideal, Less());
        NameValueTableSort::SortUnchecked(b.data(), b.data() + count, ideal, Less());
        ++ctx.cases;
        if (!Same(a, b)) ctx.Fail("%zu entries (ideal %d) sorted differently", count, static_cast<int>(ideal));
    }
}

DIFF_TEST(NameValueTableSort_InsertionSort, 0xaf51e0)
{
    auto orig = ctx.Original<Entry *(__cdecl *)(Entry *, Entry *, Less)>(0xaf51e0);
    std::mt19937 gen(0x51e0);
    for (int round = 0; round < 1500; ++round) {
        size_t count = Count(gen, round, 100);
        auto a = RandomEntries(gen, round, count);
        auto b = a;
        Entry *ra = orig(a.data(), a.data() + count, Less());
        Entry *rb = NameValueTableSort::InsertionSortUnchecked(b.data(), b.data() + count, Less());
        ++ctx.cases;
        if (ra != a.data() + count || rb != b.data() + count) ctx.Fail("%zu entries: returned not last", count);
        else if (!Same(a, b)) ctx.Fail("%zu entries sorted differently", count);
    }
}

DIFF_TEST(NameValueTableSort_PartitionByMedianGuess, 0xaf55c0)
{
    auto orig = ctx.Original<std::pair<Entry *, Entry *>(__cdecl *)(Entry *, Entry *, Less)>(0xaf55c0);
    std::mt19937 gen(0x55c0);
    for (int round = 0; round < 1500; ++round) {
        size_t count = 1 + Count(gen, round, 300);
        auto a = RandomEntries(gen, round, count);
        auto b = a;
        auto ra = orig(a.data(), a.data() + count, Less());
        auto rb = NameValueTableSort::PartitionByMedianGuess(b.data(), b.data() + count, Less());
        ++ctx.cases;
        if (ra.first - a.data() != rb.first - b.data() || ra.second - a.data() != rb.second - b.data())
            ctx.Fail("%zu entries: partition [%d, %d) vs [%d, %d)", count, static_cast<int>(ra.first - a.data()),
                     static_cast<int>(ra.second - a.data()), static_cast<int>(rb.first - b.data()), static_cast<int>(rb.second - b.data()));
        else if (!Same(a, b))
            ctx.Fail("%zu entries partitioned differently", count);
    }
}

DIFF_TEST(NameValueTableSort_Med3, 0xaf54f0)
{
    auto orig = ctx.Original<void(__cdecl *)(Entry *, Entry *, Entry *, Less)>(0xaf54f0);
    std::mt19937 gen(0x54f0);
    for (int round = 0; round < 3000; ++round) {
        size_t count = 1 + gen() % 6;
        auto a = RandomEntries(gen, round, count);
        auto b = a;
        // Three positions, possibly the same element (the original skips the swap of an element with itself).
        size_t i = gen() % count, j = gen() % count, k = gen() % count;
        orig(&a[i], &a[j], &a[k], Less());
        NameValueTableSort::Med3(&b[i], &b[j], &b[k], Less());
        ++ctx.cases;
        if (!Same(a, b)) ctx.Fail("med3 of %zu, %zu, %zu differs", i, j, k);
    }
}

DIFF_TEST(NameValueTableSort_PopHeapHoleByIndex, 0xaf5ce0)
{
    auto orig = ctx.Original<void(__cdecl *)(Entry *, ptrdiff_t, ptrdiff_t, Entry *, Less)>(0xaf5ce0);
    std::mt19937 gen(0x5ce0);
    for (int round = 0; round < 3000; ++round) {
        size_t bottom = 1 + Count(gen, round, 200);
        auto a = RandomEntries(gen, round, bottom);
        if (round % 2) std::make_heap(a.begin(), a.end(), Less()); // a real heap, as the sort passes
        auto b = a;
        ptrdiff_t hole = static_cast<ptrdiff_t>(gen() % bottom);
        auto value = RandomEntries(gen, round, 1);
        Entry va = value[0], vb = value[0];
        orig(a.data(), hole, static_cast<ptrdiff_t>(bottom), &va, Less());
        NameValueTableSort::PopHeapHoleByIndex(b.data(), hole, static_cast<ptrdiff_t>(bottom), std::move(vb), Less());
        ++ctx.cases;
        if (!Same(a, b)) ctx.Fail("hole %d of %zu: heaps differ", static_cast<int>(hole), bottom);
        else if (va != vb) ctx.Fail("hole %d of %zu: moved-from values differ", static_cast<int>(hole), bottom);
    }
}

DIFF_TEST(NameValueTableSort_PushHeapByIndex, 0xaf5e80)
{
    auto orig = ctx.Original<void(__cdecl *)(Entry *, ptrdiff_t, ptrdiff_t, Entry *, Less)>(0xaf5e80);
    std::mt19937 gen(0x5e80);
    for (int round = 0; round < 3000; ++round) {
        size_t count = 1 + Count(gen, round, 200);
        auto a = RandomEntries(gen, round, count);
        if (round % 2) std::make_heap(a.begin(), a.end(), Less());
        auto b = a;
        ptrdiff_t hole = static_cast<ptrdiff_t>(gen() % count);
        ptrdiff_t top = round % 3 == 0 ? 0 : static_cast<ptrdiff_t>(gen() % (hole + 1));
        auto value = RandomEntries(gen, round, 1);
        Entry va = value[0], vb = value[0];
        orig(a.data(), hole, top, &va, Less());
        NameValueTableSort::PushHeapByIndex(b.data(), hole, top, std::move(vb), Less());
        ++ctx.cases;
        if (!Same(a, b)) ctx.Fail("hole %d, top %d of %zu: heaps differ", static_cast<int>(hole), static_cast<int>(top), count);
        else if (va != vb) ctx.Fail("hole %d of %zu: moved-from values differ", static_cast<int>(hole), count);
    }
}

DIFF_TEST(NameValueTableSort_Compare, 0xaf6800)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(const Less *, const Entry &, const Entry &)>(0xaf6800);
    std::mt19937 gen(0x6800);
    const Less less;
    for (int round = 0; round < 20000; ++round) {
        auto pair = RandomEntries(gen, round, 2);
        if (round % 5 == 0) pair[1].first = pair[0].first; // equal names
        uint32_t ra = orig(&less, pair[0], pair[1]);
        uint32_t rb = less(pair[0], pair[1]) ? 1 : 0; // a bool return: al is 0 or 1
        ++ctx.cases;
        // The original returns al (setb / mov al, 1 / xor al, al): the whole byte is compared.
        if (ra != rb) ctx.Fail("'%s' < '%s': %u vs %u", pair[0].first.c_str(), pair[1].first.c_str(), ra, rb);
    }
}
