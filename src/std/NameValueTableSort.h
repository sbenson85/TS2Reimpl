#pragma once
#include <cstddef>
#include <utility>

#include "std/StdInstances.h" // NameValueTable

// The original's std::sort instantiation for NameValueTable (0xaf6560 sorts its vector of
// pair<std::string, uint32_t> with ByLengthThenReverseText): the out-of-line pieces of the MSVC 14.40
// introsort, which the toolchain's code generation does not reproduce byte for byte (the game's
// whole-program optimisation inlined the comparator in some and not in others). Each forwards to the
// STL template it was compiled from, with the same comparator; all __cdecl, the empty comparator
// passed by value. (The vector growth, _Emplace_reallocate 0xaf4fd0 and _Uninitialized_move 0xaf6280,
// match the toolchain's code: library.) Our NameValueTable::Construct calls std::sort itself, so
// these run only where original code still calls them.
struct NameValueTableSort {
    using Entry = NameValueTable::Entry;
    using Less = NameValueTable::ByLengthThenReverseText;

    // 0xaf5fe0: std::_Sort_unchecked(first, last, ideal, pred).
    static void __cdecl SortUnchecked(Entry *first, Entry *last, ptrdiff_t ideal, Less pred);
    // 0xaf51e0: std::_Insertion_sort_unchecked(first, last, pred), returns last.
    static Entry *__cdecl InsertionSortUnchecked(Entry *first, Entry *last, Less pred);
    // 0xaf55c0: std::_Partition_by_median_guess_unchecked(first, last, pred) (pair through a hidden pointer).
    static std::pair<Entry *, Entry *> __cdecl PartitionByMedianGuess(Entry *first, Entry *last, Less pred);
    // 0xaf54f0: std::_Med3_unchecked(first, mid, last, pred): orders the three elements.
    static void __cdecl Med3(Entry *first, Entry *mid, Entry *last, Less pred);
    // 0xaf5ce0: std::_Pop_heap_hole_by_index(first, hole, bottom, std::move(val), pred).
    static void __cdecl PopHeapHoleByIndex(Entry *first, ptrdiff_t hole, ptrdiff_t bottom, Entry &&val, Less pred);
    // 0xaf5e80: std::_Push_heap_by_index(first, hole, top, std::move(val), pred).
    static void __cdecl PushHeapByIndex(Entry *first, ptrdiff_t hole, ptrdiff_t top, Entry &&val, Less pred);
};
