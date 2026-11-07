#include "gz/EvalFunctionTable.h"

#include <algorithm>
#include <new>
#include <stdexcept>


namespace nGZCommandParser {

EvalFunctionTable::EvalFunctionTable()
{
    const Entry *builtins = ts2_rva_e7f590;
    mEntries.resize(kBuiltinCount);
    std::copy(builtins, builtins + kBuiltinCount, mEntries.begin());
    std::sort(mEntries.begin(), mEntries.end(), ByName());
}

EvalFunctionTable *__fastcall EvalFunctionTableAdapters::Construct(void *memory, void *)
{
    return ::new (memory) EvalFunctionTable;
}

void __cdecl EvalFunctionTableStl::SortUnchecked(Entry *first, Entry *last, ptrdiff_t ideal, ByName pred)
{
    std::_Sort_unchecked(first, last, ideal, pred);
}

std::pair<EvalFunctionTable::Entry *, EvalFunctionTable::Entry *> __cdecl EvalFunctionTableStl::PartitionByMedianGuess(
    Entry *first, Entry *last, ByName pred)
{
    return std::_Partition_by_median_guess_unchecked(first, last, pred);
}

void EvalFunctionTableVector::ResizeReallocate(size_t newSize, const void *)
{
    if (newSize > mVector.max_size()) std::_Xlength_error("vector too long");
    // vector::_Calculate_growth: half as much again, or max_size() when that overflows, at least newSize.
    const size_t capacity = mVector.capacity();
    const size_t maxSize = mVector.max_size();
    size_t newCapacity = capacity > maxSize - capacity / 2 ? maxSize : capacity + capacity / 2;
    if (newCapacity < newSize) newCapacity = newSize;
    mVector.reserve(newCapacity); // newCapacity > capacity: allocates exactly that, moves, frees the old block
    mVector.resize(newSize);      // zeroes the new elements in place
}

} // namespace nGZCommandParser
