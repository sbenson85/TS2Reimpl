// nGZSceneGraph's empty attribute-entry range (lane OM). See the header.
#include "sg/cNullArrayAttributeEntryIterator.h"

namespace nGZSceneGraph {

bool cNullArrayAttributeEntryIterator::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cNullArrayAttributeEntryIterator::AddRef() { return ++mRefCount; }

uint32_t cNullArrayAttributeEntryIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIArrayAttributeEntryIterator *cNullArrayAttributeEntryIterator::Clone() { return new cNullArrayAttributeEntryIterator; }

bool cNullArrayAttributeEntryIterator::IsEqual(const cIArrayAttributeEntryIterator *) { return true; }

cArrayAttributeEntryName cNullArrayAttributeEntryIterator::Get() { return {0, 0}; }

void cNullArrayAttributeEntryIterator::Next() {}

void cNullArrayAttributeEntryIterator::Previous() {}

void cNullArrayAttributeEntryIterator::Advance(int32_t) {}

cArrayAttributeEntryName cNullArrayAttributeEntryIterator::At(int32_t) { return Get(); }

cArrayAttributeEntryRange *__fastcall MakeEmptyAttributeEntryRange(void *, void *, cArrayAttributeEntryRange *out,
                                                                   uint32_t)
{
    cNullArrayAttributeEntryIterator *const end = new cNullArrayAttributeEntryIterator;
    cNullArrayAttributeEntryIterator *const begin = new cNullArrayAttributeEntryIterator;
    out->mCurrent = begin->Clone();
    if (out->mCurrent) out->mCurrent->AddRef();
    out->mBegin = begin;
    begin->AddRef();
    out->mEnd = end;
    end->AddRef();
    return out;
}

} // namespace nGZSceneGraph
