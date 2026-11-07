#pragma once
// The display list's std::sort instantiations (the block 0xa9a1f0-0xa9d420 behind cDisplayList's vtable): the
// out-of-line pieces of the MSVC 14.40 introsort for its three sorts, which the toolchain's code generation does not
// reproduce byte for byte (the game's whole-program optimisation inlined the comparators, and for the float sort the
// insertion sort, the partition and the heap build too). Each forwards to the STL template it was compiled from, with
// the same comparator; all __cdecl, the empty comparator passed by value. Only our slots call them (the originals are
// in effect through the factory hook):
//   - the draw items in DrawItemOrder (slot 24, 0xaa0920),
//   - the draw items in DrawItemLayerOrder (slot 28, 0xa9e700),
//   - a float array in ascending order (slot 28).
// The vector growth these sorts' callers use (0xa9a1f0 _Emplace_reallocate and the others listed in
// docs/subsystems/scenegraph.md, DLa) matches the toolchain's code: library.
#include "sg/cDisplayList.h" // DrawItem, DrawItemOrder, DrawItemLayerOrder

#include <cstddef>
#include <functional>
#include <utility>

namespace nGZSceneGraph {

struct DisplayListSort {
    using FloatLess = std::less<>;

    // The draw items in DrawItemOrder.
    // 0xa9cf70: std::_Sort_unchecked(first, last, ideal, pred).
    static void __cdecl SortItems(DrawItem *first, DrawItem *last, ptrdiff_t ideal, DrawItemOrder pred);
    // 0xa9a4d0: std::_Insertion_sort_unchecked(first, last, pred), returns last.
    static DrawItem *__cdecl InsertionSortItems(DrawItem *first, DrawItem *last, DrawItemOrder pred);
    // 0xa9b0f0: std::_Partition_by_median_guess_unchecked(first, last, pred) (the pair through a hidden pointer).
    static std::pair<DrawItem *, DrawItem *> __cdecl PartitionItems(DrawItem *first, DrawItem *last, DrawItemOrder pred);
    // 0xa9ad70: std::_Med3_unchecked(first, mid, last, pred) (the partition inlines _Guess_median_unchecked).
    static void __cdecl Med3Items(DrawItem *first, DrawItem *mid, DrawItem *last, DrawItemOrder pred);
    // 0xa9a850: std::_Make_heap_unchecked(first, last, pred).
    static void __cdecl MakeHeapItems(DrawItem *first, DrawItem *last, DrawItemOrder pred);
    // 0xa9be10: std::_Pop_heap_hole_by_index(first, hole, bottom, std::move(val), pred).
    static void __cdecl PopHeapHoleItems(DrawItem *first, ptrdiff_t hole, ptrdiff_t bottom, DrawItem &&val,
                                         DrawItemOrder pred);

    // The draw items in DrawItemLayerOrder.
    static void __cdecl SortLayered(DrawItem *first, DrawItem *last, ptrdiff_t ideal, DrawItemLayerOrder pred); // 0xa9d100
    static DrawItem *__cdecl InsertionSortLayered(DrawItem *first, DrawItem *last, DrawItemLayerOrder pred);    // 0xa9a690
    static std::pair<DrawItem *, DrawItem *> __cdecl PartitionLayered(DrawItem *first, DrawItem *last,
                                                                      DrawItemLayerOrder pred);               // 0xa9b730
    static void __cdecl Med3Layered(DrawItem *first, DrawItem *mid, DrawItem *last,
                                           DrawItemLayerOrder pred);                                          // 0xa9af30
    static void __cdecl MakeHeapLayered(DrawItem *first, DrawItem *last, DrawItemLayerOrder pred);            // 0xa9aae0
    static void __cdecl PopHeapHoleLayered(DrawItem *first, ptrdiff_t hole, ptrdiff_t bottom, DrawItem &&val,
                                           DrawItemLayerOrder pred);                                          // 0xa9c030

    // The floats, ascending (std::sort without a comparator).
    static void __cdecl SortFloats(float *first, float *last, ptrdiff_t ideal, FloatLess pred);              // 0xa9cc30
    static void __cdecl GuessMedianFloats(float *first, float *mid, float *last, FloatLess pred);             // 0xa9a350
    static void __cdecl PopHeapHoleFloats(float *first, ptrdiff_t hole, ptrdiff_t bottom, float &&val,
                                          FloatLess pred);                                                    // 0xa9bd70
};

} // namespace nGZSceneGraph
