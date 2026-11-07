// Differential tests for the display list's std::sort pieces (sg/DisplayListSort.h): the draw items in DrawItemOrder
// and in DrawItemLayerOrder, and the floats. The original inlined its comparators, so the comparator's calls cannot be
// observed one by one; instead every element carries its index (mWord00, mUserData), so any difference in the resulting
// permutation shows (an introsort does not keep the order of equal elements), and the keys are drawn from small pools so
// that every tie level of the comparators is common: equal sort keys, equal materials, equal geometries, sub keys
// equal in their low 16 bits only, equal layers, equal depths, +0 and -0, NaN depths, materials and geometries above
// 0x80000000 (the comparisons are unsigned), negative sort keys and layers (signed).
#include "difftest.h"
#include "sg/DisplayListSort.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

using nGZSceneGraph::DisplayListSort;
using nGZSceneGraph::DrawItem;
using nGZSceneGraph::DrawItemLayerOrder;
using nGZSceneGraph::DrawItemOrder;

namespace {

using FloatLess = DisplayListSort::FloatLess;

float RandomDepth(std::mt19937 &gen, int round)
{
    static const float kPool[] = {0.0f, -0.0f, 1.0f, -1.0f, 2.5f, 1e30f, -1e30f, 0.5f};
    if (round % 5 == 0 && gen() % 16 == 0) return std::numeric_limits<float>::quiet_NaN();
    if (gen() % 3 == 0) return static_cast<float>(static_cast<int>(gen() % 2001) - 1000) / 7.0f;
    return kPool[gen() % (round % 2 ? 3 : 8)];
}

std::vector<DrawItem> RandomItems(std::mt19937 &gen, int round, size_t count)
{
    static const int32_t kKeys[] = {0, 1, 2, 3, -1, -2, 0x7fffffff, static_cast<int32_t>(0x80000000), 0x10, 0x11};
    static const uintptr_t kPointers[] = {0x1000, 0x2000, 0x7ffffff0, 0x80000000, 0xfffffff0, 0x1004};
    static const int32_t kLayers[] = {0, 1, -1, 5, 0x7fffffff, static_cast<int32_t>(0x80000000)};
    // Narrow pools in some rounds (ties at every level), wide in others.
    size_t keys = round % 3 == 0 ? 2 : (round % 3 == 1 ? 4 : 10);
    size_t pointers = round % 4 == 0 ? 1 : (round % 4 == 1 ? 2 : 6);
    std::vector<DrawItem> items(count);
    for (size_t i = 0; i < count; ++i) {
        DrawItem &item = items[i];
        std::memset(&item, 0, sizeof(item));
        item.mWord00 = static_cast<uint32_t>(i);
        item.mUserData = static_cast<uint32_t>(i) * 0x9e3779b9u;
        item.mSortKey = kKeys[gen() % keys];
        item.mMaterial = reinterpret_cast<void *>(kPointers[gen() % pointers]);
        item.mGeometry = reinterpret_cast<void *>(kPointers[gen() % pointers]);
        // The high half differs without changing the order (the comparators look at the low 16 bits only).
        item.mSubKey = (gen() % 3) | (gen() % 2 ? 0xffff0000u : 0) | (gen() % 4 == 0 ? 0x8000u : 0);
        item.mLayer = kLayers[gen() % (round % 2 ? 2 : 6)];
        item.mDepth = RandomDepth(gen, round);
        item.mMaterialIndex = static_cast<int32_t>(i) * 4;
    }
    return items;
}

std::vector<float> RandomFloats(std::mt19937 &gen, int round, size_t count)
{
    std::vector<float> values(count);
    for (size_t i = 0; i < count; ++i) values[i] = RandomDepth(gen, round);
    return values;
}

template <class T> bool Same(const std::vector<T> &a, const std::vector<T> &b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

// Counts: every count up to 40 (the insertion sort's threshold is 32, the ninther's 40), then random ones, some large.
size_t Count(std::mt19937 &gen, int round, size_t big)
{
    return round < 48 ? static_cast<size_t>(round) : gen() % (round % 7 == 0 ? big : 100);
}

template <class Item, class Pred, class Fn>
void TestSort(difftest::Context &ctx, uint32_t rva, Fn ours, uint32_t seed, std::vector<Item> (*make)(std::mt19937 &, int, size_t))
{
    auto orig = ctx.Original<void(__cdecl *)(Item *, Item *, ptrdiff_t, Pred)>(rva);
    std::mt19937 gen(seed);
    for (int round = 0; round < 1500; ++round) {
        size_t count = Count(gen, round, 1500);
        auto a = make(gen, round, count);
        auto b = a;
        // ideal: the element count, as std::sort passes it; small values force the heap-sort fallback.
        ptrdiff_t ideal = round % 4 == 0 ? static_cast<ptrdiff_t>(gen() % 4) : static_cast<ptrdiff_t>(count);
        orig(a.data(), a.data() + count, ideal, Pred());
        ours(b.data(), b.data() + count, ideal, Pred());
        ++ctx.cases;
        if (!Same(a, b)) ctx.Fail("%zu elements (ideal %d) sorted differently", count, static_cast<int>(ideal));
    }
}

template <class Pred, class Fn> void TestInsertionSort(difftest::Context &ctx, uint32_t rva, Fn ours, uint32_t seed)
{
    auto orig = ctx.Original<DrawItem *(__cdecl *)(DrawItem *, DrawItem *, Pred)>(rva);
    std::mt19937 gen(seed);
    for (int round = 0; round < 1500; ++round) {
        size_t count = Count(gen, round, 100);
        auto a = RandomItems(gen, round, count);
        auto b = a;
        DrawItem *ra = orig(a.data(), a.data() + count, Pred());
        DrawItem *rb = ours(b.data(), b.data() + count, Pred());
        ++ctx.cases;
        if (ra - a.data() != rb - b.data()) ctx.Fail("%zu items: returned %d vs %d", count, static_cast<int>(ra - a.data()), static_cast<int>(rb - b.data()));
        else if (!Same(a, b)) ctx.Fail("%zu items sorted differently", count);
    }
}

template <class Pred, class Fn> void TestPartition(difftest::Context &ctx, uint32_t rva, Fn ours, uint32_t seed)
{
    auto orig = ctx.Original<std::pair<DrawItem *, DrawItem *>(__cdecl *)(DrawItem *, DrawItem *, Pred)>(rva);
    std::mt19937 gen(seed);
    for (int round = 0; round < 1500; ++round) {
        size_t count = 1 + Count(gen, round, 600);
        auto a = RandomItems(gen, round, count);
        auto b = a;
        auto ra = orig(a.data(), a.data() + count, Pred());
        auto rb = ours(b.data(), b.data() + count, Pred());
        ++ctx.cases;
        if (ra.first - a.data() != rb.first - b.data() || ra.second - a.data() != rb.second - b.data())
            ctx.Fail("%zu items: partition [%d, %d) vs [%d, %d)", count, static_cast<int>(ra.first - a.data()),
                     static_cast<int>(ra.second - a.data()), static_cast<int>(rb.first - b.data()),
                     static_cast<int>(rb.second - b.data()));
        else if (!Same(a, b))
            ctx.Fail("%zu items partitioned differently", count);
    }
}

template <class Pred, class Fn> void TestMed3(difftest::Context &ctx, uint32_t rva, Fn ours, uint32_t seed)
{
    auto orig = ctx.Original<void(__cdecl *)(DrawItem *, DrawItem *, DrawItem *, Pred)>(rva);
    std::mt19937 gen(seed);
    for (int round = 0; round < 4000; ++round) {
        // Odd rounds: three positions anywhere, possibly the same element. Even rounds: first <= mid <= last over up to
        // 160 elements, often more than 40 apart (where std's median guess would take a ninther instead).
        size_t count = round % 2 ? 1 + gen() % 6 : 1 + gen() % 160;
        auto a = RandomItems(gen, round, count);
        auto b = a;
        size_t i = gen() % count, j = gen() % count, k = gen() % count;
        if (round % 2 == 0) {
            i = gen() % 3 ? 0 : i;
            k = gen() % 3 ? count - 1 : k;
            if (i > k) std::swap(i, k);
            j = i + (k - i) / 2;
        }
        orig(&a[i], &a[j], &a[k], Pred());
        ours(&b[i], &b[j], &b[k], Pred());
        ++ctx.cases;
        if (!Same(a, b)) ctx.Fail("med3 of %zu, %zu, %zu differs", i, j, k);
    }
}

template <class Pred, class Fn> void TestMakeHeap(difftest::Context &ctx, uint32_t rva, Fn ours, uint32_t seed)
{
    auto orig = ctx.Original<void(__cdecl *)(DrawItem *, DrawItem *, Pred)>(rva);
    std::mt19937 gen(seed);
    for (int round = 0; round < 1500; ++round) {
        size_t count = Count(gen, round, 600);
        auto a = RandomItems(gen, round, count);
        auto b = a;
        orig(a.data(), a.data() + count, Pred());
        ours(b.data(), b.data() + count, Pred());
        ++ctx.cases;
        if (!Same(a, b)) ctx.Fail("%zu items: heaps differ", count);
    }
}

template <class Item, class Pred, class Fn>
void TestPopHeapHole(difftest::Context &ctx, uint32_t rva, Fn ours, uint32_t seed, std::vector<Item> (*make)(std::mt19937 &, int, size_t))
{
    auto orig = ctx.Original<void(__cdecl *)(Item *, ptrdiff_t, ptrdiff_t, Item *, Pred)>(rva);
    std::mt19937 gen(seed);
    for (int round = 0; round < 3000; ++round) {
        size_t count = 1 + Count(gen, round, 300);
        auto a = make(gen, round, count + 1);
        // The value moved in is the extra last element; the heap is the first count (a heap in some rounds).
        if (round % 2 == 0) std::make_heap(a.begin(), a.begin() + count, Pred());
        auto b = a;
        ptrdiff_t bottom = 1 + static_cast<ptrdiff_t>(gen() % count);
        ptrdiff_t hole = round % 3 == 0 ? 0 : static_cast<ptrdiff_t>(gen() % bottom);
        Item va = a[count], vb = b[count];
        orig(a.data(), hole, bottom, &va, Pred());
        ours(b.data(), hole, bottom, std::move(vb), Pred());
        ++ctx.cases;
        if (!Same(a, b)) ctx.Fail("%zu elements: hole %d, bottom %d differ", count, static_cast<int>(hole), static_cast<int>(bottom));
    }
}

} // namespace

DIFF_TEST(DisplayListSort_SortItems, 0xa9cf70)
{
    TestSort<DrawItem, DrawItemOrder>(ctx, 0xa9cf70, &DisplayListSort::SortItems, 0xcf70, &RandomItems);
}

DIFF_TEST(DisplayListSort_InsertionSortItems, 0xa9a4d0)
{
    TestInsertionSort<DrawItemOrder>(ctx, 0xa9a4d0, &DisplayListSort::InsertionSortItems, 0xa4d0);
}

DIFF_TEST(DisplayListSort_PartitionItems, 0xa9b0f0)
{
    TestPartition<DrawItemOrder>(ctx, 0xa9b0f0, &DisplayListSort::PartitionItems, 0xb0f0);
}

DIFF_TEST(DisplayListSort_Med3Items, 0xa9ad70)
{
    TestMed3<DrawItemOrder>(ctx, 0xa9ad70, &DisplayListSort::Med3Items, 0xad70);
}

DIFF_TEST(DisplayListSort_MakeHeapItems, 0xa9a850)
{
    TestMakeHeap<DrawItemOrder>(ctx, 0xa9a850, &DisplayListSort::MakeHeapItems, 0xa850);
}

DIFF_TEST(DisplayListSort_PopHeapHoleItems, 0xa9be10)
{
    TestPopHeapHole<DrawItem, DrawItemOrder>(ctx, 0xa9be10, &DisplayListSort::PopHeapHoleItems, 0xbe10, &RandomItems);
}

DIFF_TEST(DisplayListSort_SortLayered, 0xa9d100)
{
    TestSort<DrawItem, DrawItemLayerOrder>(ctx, 0xa9d100, &DisplayListSort::SortLayered, 0xd100, &RandomItems);
}

DIFF_TEST(DisplayListSort_InsertionSortLayered, 0xa9a690)
{
    TestInsertionSort<DrawItemLayerOrder>(ctx, 0xa9a690, &DisplayListSort::InsertionSortLayered, 0xa690);
}

DIFF_TEST(DisplayListSort_PartitionLayered, 0xa9b730)
{
    TestPartition<DrawItemLayerOrder>(ctx, 0xa9b730, &DisplayListSort::PartitionLayered, 0xb730);
}

DIFF_TEST(DisplayListSort_Med3Layered, 0xa9af30)
{
    TestMed3<DrawItemLayerOrder>(ctx, 0xa9af30, &DisplayListSort::Med3Layered, 0xaf30);
}

DIFF_TEST(DisplayListSort_MakeHeapLayered, 0xa9aae0)
{
    TestMakeHeap<DrawItemLayerOrder>(ctx, 0xa9aae0, &DisplayListSort::MakeHeapLayered, 0xaae0);
}

DIFF_TEST(DisplayListSort_PopHeapHoleLayered, 0xa9c030)
{
    TestPopHeapHole<DrawItem, DrawItemLayerOrder>(ctx, 0xa9c030, &DisplayListSort::PopHeapHoleLayered, 0xc030, &RandomItems);
}

DIFF_TEST(DisplayListSort_SortFloats, 0xa9cc30)
{
    TestSort<float, FloatLess>(ctx, 0xa9cc30, &DisplayListSort::SortFloats, 0xcc30, &RandomFloats);
}

DIFF_TEST(DisplayListSort_GuessMedianFloats, 0xa9a350)
{
    auto orig = ctx.Original<void(__cdecl *)(float *, float *, float *, FloatLess)>(0xa9a350);
    std::mt19937 gen(0xa350);
    for (int round = 0; round < 3000; ++round) {
        // Both sides of the ninther threshold (more than 40 elements between first and last).
        size_t count = 1 + (round < 100 ? static_cast<size_t>(round) : gen() % 300);
        auto a = RandomFloats(gen, round, count);
        auto b = a;
        size_t last = count - 1, mid = last / 2;
        orig(a.data(), a.data() + mid, a.data() + last, FloatLess());
        DisplayListSort::GuessMedianFloats(b.data(), b.data() + mid, b.data() + last, FloatLess());
        ++ctx.cases;
        if (!Same(a, b)) ctx.Fail("%zu floats: median guesses differ", count);
    }
}

DIFF_TEST(DisplayListSort_PopHeapHoleFloats, 0xa9bd70)
{
    TestPopHeapHole<float, FloatLess>(ctx, 0xa9bd70, &DisplayListSort::PopHeapHoleFloats, 0xbd70, &RandomFloats);
}
