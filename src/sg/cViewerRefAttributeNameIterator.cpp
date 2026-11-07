// cViewerRefNode's attribute-name ranges. See the header.
#include "sg/cViewerRefAttributeNameIterator.h"

#include "game/game.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"

namespace nGZSceneGraph {

static_assert(offsetof(cViewerRefAttributeNameIterator, mPosition) == 8, "viewer name iterator position");
static_assert(offsetof(cViewerRefAttributeNameIterator, mName) == 0x10, "viewer name iterator name");

void *cViewerRefAttributeNameIterator::operator new(size_t) noexcept
{
    return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f639c)->Alloc();
}
void cViewerRefAttributeNameIterator::operator delete(void *block) noexcept
{
    reinterpret_cast<RZFastFixed0x14 *>(&ts2_rva_10f639c)->Free(block);
}

bool cViewerRefAttributeNameIterator::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cViewerRefAttributeNameIterator::AddRef() { return ++mRefCount; }

uint32_t cViewerRefAttributeNameIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIArrayAttributeEntryIterator *cViewerRefAttributeNameIterator::Clone()
{
    return new cViewerRefAttributeNameIterator(*this);
}

bool cViewerRefAttributeNameIterator::IsEqual(const cIArrayAttributeEntryIterator *other)
{
    return mPosition == static_cast<const cViewerRefAttributeNameIterator *>(other)->mPosition;
}

cArrayAttributeEntryName cViewerRefAttributeNameIterator::Get()
{
    return {uint32_t(uintptr_t(mGroup)), uint32_t(uintptr_t(mName))};
}

void cViewerRefAttributeNameIterator::Next() { ++mPosition; }

void cViewerRefAttributeNameIterator::Previous() { --mPosition; }

void cViewerRefAttributeNameIterator::Advance(int32_t count) { mPosition += count; }

cArrayAttributeEntryName cViewerRefAttributeNameIterator::At(int32_t) { return Get(); }

cArrayAttributeEntryRange *MakeViewerAttributeNameRange(cArrayAttributeEntryRange *out, uint32_t count,
                                                        const char *group, const char *name)
{
    // The end first, then the beginning, as the original.
    cViewerRefAttributeNameIterator *const end = new cViewerRefAttributeNameIterator(count, group, name);
    cViewerRefAttributeNameIterator *const begin = new cViewerRefAttributeNameIterator(0, group, name);
    out->mCurrent = begin->Clone();
    if (out->mCurrent) out->mCurrent->AddRef();
    out->mBegin = begin;
    begin->AddRef();
    out->mEnd = end;
    if (end) end->AddRef();
    return out;
}

} // namespace nGZSceneGraph
