#include "sg/DisplayListSort.h"

#include <algorithm>

namespace nGZSceneGraph {

void __cdecl DisplayListSort::SortItems(DrawItem *first, DrawItem *last, ptrdiff_t ideal, DrawItemOrder pred)
{
    std::_Sort_unchecked(first, last, ideal, pred);
}

DrawItem *__cdecl DisplayListSort::InsertionSortItems(DrawItem *first, DrawItem *last, DrawItemOrder pred)
{
    return std::_Insertion_sort_unchecked(first, last, pred);
}

std::pair<DrawItem *, DrawItem *> __cdecl DisplayListSort::PartitionItems(DrawItem *first, DrawItem *last,
                                                                           DrawItemOrder pred)
{
    return std::_Partition_by_median_guess_unchecked(first, last, pred);
}

void __cdecl DisplayListSort::Med3Items(DrawItem *first, DrawItem *mid, DrawItem *last, DrawItemOrder pred)
{
    std::_Med3_unchecked(first, mid, last, pred);
}

void __cdecl DisplayListSort::MakeHeapItems(DrawItem *first, DrawItem *last, DrawItemOrder pred)
{
    std::_Make_heap_unchecked(first, last, pred);
}

void __cdecl DisplayListSort::PopHeapHoleItems(DrawItem *first, ptrdiff_t hole, ptrdiff_t bottom, DrawItem &&val,
                                               DrawItemOrder pred)
{
    std::_Pop_heap_hole_by_index(first, hole, bottom, std::move(val), pred);
}

void __cdecl DisplayListSort::SortLayered(DrawItem *first, DrawItem *last, ptrdiff_t ideal, DrawItemLayerOrder pred)
{
    std::_Sort_unchecked(first, last, ideal, pred);
}

DrawItem *__cdecl DisplayListSort::InsertionSortLayered(DrawItem *first, DrawItem *last, DrawItemLayerOrder pred)
{
    return std::_Insertion_sort_unchecked(first, last, pred);
}

std::pair<DrawItem *, DrawItem *> __cdecl DisplayListSort::PartitionLayered(DrawItem *first, DrawItem *last,
                                                                             DrawItemLayerOrder pred)
{
    return std::_Partition_by_median_guess_unchecked(first, last, pred);
}

void __cdecl DisplayListSort::Med3Layered(DrawItem *first, DrawItem *mid, DrawItem *last,
                                                 DrawItemLayerOrder pred)
{
    std::_Med3_unchecked(first, mid, last, pred);
}

void __cdecl DisplayListSort::MakeHeapLayered(DrawItem *first, DrawItem *last, DrawItemLayerOrder pred)
{
    std::_Make_heap_unchecked(first, last, pred);
}

void __cdecl DisplayListSort::PopHeapHoleLayered(DrawItem *first, ptrdiff_t hole, ptrdiff_t bottom, DrawItem &&val,
                                                 DrawItemLayerOrder pred)
{
    std::_Pop_heap_hole_by_index(first, hole, bottom, std::move(val), pred);
}

void __cdecl DisplayListSort::SortFloats(float *first, float *last, ptrdiff_t ideal, FloatLess pred)
{
    std::_Sort_unchecked(first, last, ideal, pred);
}

void __cdecl DisplayListSort::GuessMedianFloats(float *first, float *mid, float *last, FloatLess pred)
{
    std::_Guess_median_unchecked(first, mid, last, pred);
}

void __cdecl DisplayListSort::PopHeapHoleFloats(float *first, ptrdiff_t hole, ptrdiff_t bottom, float &&val,
                                                FloatLess pred)
{
    std::_Pop_heap_hole_by_index(first, hole, bottom, std::move(val), pred);
}

} // namespace nGZSceneGraph
