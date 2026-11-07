#include "std/NameValueTableSort.h"

#include <algorithm>

void __cdecl NameValueTableSort::SortUnchecked(Entry *first, Entry *last, ptrdiff_t ideal, Less pred)
{
    std::_Sort_unchecked(first, last, ideal, pred);
}

NameValueTable::Entry *__cdecl NameValueTableSort::InsertionSortUnchecked(Entry *first, Entry *last, Less pred)
{
    return std::_Insertion_sort_unchecked(first, last, pred);
}

std::pair<NameValueTable::Entry *, NameValueTable::Entry *> __cdecl NameValueTableSort::PartitionByMedianGuess(
    Entry *first, Entry *last, Less pred)
{
    return std::_Partition_by_median_guess_unchecked(first, last, pred);
}

void __cdecl NameValueTableSort::Med3(Entry *first, Entry *mid, Entry *last, Less pred)
{
    std::_Med3_unchecked(first, mid, last, pred);
}

void __cdecl NameValueTableSort::PopHeapHoleByIndex(Entry *first, ptrdiff_t hole, ptrdiff_t bottom, Entry &&val, Less pred)
{
    std::_Pop_heap_hole_by_index(first, hole, bottom, std::move(val), pred);
}

void __cdecl NameValueTableSort::PushHeapByIndex(Entry *first, ptrdiff_t hole, ptrdiff_t top, Entry &&val, Less pred)
{
    std::_Push_heap_by_index(first, hole, top, std::move(val), pred);
}
