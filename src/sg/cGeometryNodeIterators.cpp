// cGeometryNode's subset iterators (lane QZ). See the header.
#include "sg/cGeometryNodeIterators.h"

#include "game/game.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"
#include "rz/StaticGetters2.h" // Dispatch8: a string-pool handle's text

namespace nGZSceneGraph {

static_assert(offsetof(cGeometrySubsetNameIterator, mMap) == 8, "name iterator map");
static_assert(offsetof(cGeometrySubsetNameIterator, mNode) == 0xc, "name iterator node");
static_assert(offsetof(cSubsetGeomPartIndexIterator, mNode) == 8, "part iterator node");

bool cGeometrySubsetIteratorBase::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGeometrySubsetIteratorBase::AddRef() { return ++mRefCount; }

uint32_t cGeometrySubsetIteratorBase::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

// ---- the names -----------------------------------------------------------------------------------------------------

void *cGeometrySubsetNameIterator::operator new(size_t) noexcept { return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f6020)->Alloc(); }
void cGeometrySubsetNameIterator::operator delete(void *block) noexcept { reinterpret_cast<RZFastFixed0x10 *>(&ts2_rva_10f6020)->Free(block); }

// A null block (the pool failing) gives a null iterator: the class operator new is noexcept.
cIGeometrySubsetIterator *cGeometrySubsetNameIterator::Clone() { return new cGeometrySubsetNameIterator(*this); }

bool cGeometrySubsetNameIterator::IsEqual(const cIGeometrySubsetIterator *other)
{
    return mNode == static_cast<const cGeometrySubsetNameIterator *>(other)->mNode;
}

uint32_t cGeometrySubsetNameIterator::Get() { return Dispatch8(const_cast<ServiceCell *>(&mNode->first.mCell)); }

// Past every part of this name: the first node with a greater handle.
void cGeometrySubsetNameIterator::Next() { mNode = mMap->upper_bound(mNode->first.mCell.mValue); }

// ---- one name's parts ----------------------------------------------------------------------------------------------

void *cSubsetGeomPartIndexIterator::operator new(size_t) noexcept { return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f602c)->Alloc(); }
void cSubsetGeomPartIndexIterator::operator delete(void *block) noexcept { reinterpret_cast<RZFastFixed0xc *>(&ts2_rva_10f602c)->Free(block); }

cIGeometrySubsetIterator *cSubsetGeomPartIndexIterator::Clone() { return new cSubsetGeomPartIndexIterator(*this); }

bool cSubsetGeomPartIndexIterator::IsEqual(const cIGeometrySubsetIterator *other)
{
    return mNode == static_cast<const cSubsetGeomPartIndexIterator *>(other)->mNode;
}

uint32_t cSubsetGeomPartIndexIterator::Get() { return mNode->second; }

void cSubsetGeomPartIndexIterator::Next() { ++mNode; }

} // namespace nGZSceneGraph
