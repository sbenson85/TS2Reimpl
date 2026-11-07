// The grid's matrices of cGZLegacyControl (lane NH): src/gz/cGZLegacyGridMatrix.h. Built for size, as the game's.
#include "gz/cGZLegacyGridMatrix.h"

#include <new>

namespace legacygrid {

template <class Value> bool GridMatrix<Value>::Set(int32_t entry, int32_t line, const Value *value, Value *previous)
{
    if (line < mLineMin || entry < mEntryMin || line > mLineMax || entry > mEntryMax) return false;
    auto &entries = mLines.try_emplace(line, line).first->second.mEntries;
    const auto found = entries.find(entry);
    if (found == entries.end()) {
        entries.try_emplace(entry, entry, *value);
        ++mCount;
    } else {
        if (previous) *previous = found->second.mValue;
        found->second.mValue = *value;
    }
    return true;
}

template bool CellMatrix::Set(int32_t, int32_t, const GridCell *, GridCell *);
template bool LineMatrix::Set(int32_t, int32_t, const GridLineData *, GridLineData *);

template <class Value> void *__fastcall GridMatrix<Value>::Destroy(GridMatrix *self, void *, uint32_t flags)
{
    self->~GridMatrix();
    if (flags & 1) ::operator delete(self);
    return self;
}

template void *__fastcall CellMatrix::Destroy(CellMatrix *, void *, uint32_t);
template void *__fastcall LineMatrix::Destroy(LineMatrix *, void *, uint32_t);

GridCell *__fastcall GridCell::Construct(GridCell *memory, void *) { return ::new (memory) GridCell; }
GridCell *__fastcall GridCell::Copy(GridCell *memory, void *, const GridCell *other) { return ::new (memory) GridCell(*other); }
GridCell *__fastcall GridCell::Assign(GridCell *self, void *, const GridCell *other)
{
    *self = *other;
    return self;
}

void *__fastcall ConstructGridMatrix(void *memory, void *) { return ::new (memory) CellMatrix; }

} // namespace legacygrid
