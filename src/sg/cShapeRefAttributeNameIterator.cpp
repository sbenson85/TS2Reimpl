// cShapeRefNode's attribute-name ranges (lane TD). See the header.
#include "sg/cShapeRefAttributeNameIterator.h"

#include "game/game.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"

namespace nGZSceneGraph {

static_assert(offsetof(cShapeRefAttributeNameIterator, mList) == 8, "name iterator list");
static_assert(offsetof(cShapeRefAttributeNameIterator, mPosition) == 0xc, "name iterator position");

// ---- the list ----

bool cShapeRefAttributeNameList::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cShapeRefAttributeNameList::AddRef() { return ++mRefCount; }

uint32_t cShapeRefAttributeNameList::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

// ---- the iterator ----

cShapeRefAttributeNameIterator::cShapeRefAttributeNameIterator(cShapeRefAttributeNameList *list,
                                                               const cArrayAttributeEntryName *position)
    : mList(list), mPosition(position)
{
    if (mList) mList->AddRef();
}

cShapeRefAttributeNameIterator::cShapeRefAttributeNameIterator(const cShapeRefAttributeNameIterator &other)
    : cIArrayAttributeEntryIterator(), mList(other.mList), mPosition(other.mPosition)
{
    if (mList) mList->AddRef();
}

cShapeRefAttributeNameIterator::~cShapeRefAttributeNameIterator()
{
    if (mList) mList->Release();
}

void *cShapeRefAttributeNameIterator::operator new(size_t) noexcept
{
    return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f625c)->Alloc();
}
void cShapeRefAttributeNameIterator::operator delete(void *block) noexcept
{
    reinterpret_cast<RZFastFixed0x10 *>(&ts2_rva_10f625c)->Free(block);
}

bool cShapeRefAttributeNameIterator::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cShapeRefAttributeNameIterator::AddRef() { return ++mRefCount; }

uint32_t cShapeRefAttributeNameIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIArrayAttributeEntryIterator *cShapeRefAttributeNameIterator::Clone() { return new cShapeRefAttributeNameIterator(*this); }

bool cShapeRefAttributeNameIterator::IsEqual(const cIArrayAttributeEntryIterator *other)
{
    return mPosition == static_cast<const cShapeRefAttributeNameIterator *>(other)->mPosition;
}

cArrayAttributeEntryName cShapeRefAttributeNameIterator::Get() { return *mPosition; }

void cShapeRefAttributeNameIterator::Next() { ++mPosition; }

void cShapeRefAttributeNameIterator::Previous() { --mPosition; }

void cShapeRefAttributeNameIterator::Advance(int32_t count) { mPosition += count; }

cArrayAttributeEntryName cShapeRefAttributeNameIterator::At(int32_t index) { return mPosition[index]; }

// ---- the range ----

cArrayAttributeEntryRange *MakeAttributeNameRange(cArrayAttributeEntryRange *out, cShapeRefAttributeNameList *list)
{
    const cArrayAttributeEntryName *const first = list->mNames.data();
    cShapeRefAttributeNameIterator *const end = new cShapeRefAttributeNameIterator(list, first + list->mNames.size());
    cShapeRefAttributeNameIterator *const begin = new cShapeRefAttributeNameIterator(list, first);
    // (The original does not check begin: a failing pool would crash it there too.)
    out->mCurrent = begin->Clone();
    if (out->mCurrent) out->mCurrent->AddRef();
    out->mBegin = begin;
    begin->AddRef();
    out->mEnd = end;
    if (end) end->AddRef();
    return out;
}

} // namespace nGZSceneGraph
