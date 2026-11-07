// cShape's subset binding iterator (lane SH). See the header.
#include "sg/cSubsetBindingIterator.h"

#include "game/game.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"
#include "rz/StaticGetters2.h" // Dispatch8: a string-pool handle's text

namespace nGZSceneGraph {

static_assert(offsetof(cSubsetBindingIterator, mRefCount) == 4, "binding iterator count");
static_assert(offsetof(cSubsetBindingIterator, mNode) == 8, "binding iterator node");

void *cSubsetBindingIterator::operator new(size_t) noexcept { return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f62f0)->Alloc(); }
void cSubsetBindingIterator::operator delete(void *block) noexcept { reinterpret_cast<RZFastFixed0xc *>(&ts2_rva_10f62f0)->Free(block); }

bool cSubsetBindingIterator::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cSubsetBindingIterator::AddRef() { return ++mRefCount; }

uint32_t cSubsetBindingIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

// A null block (the pool failing) gives a null iterator: the class operator new is noexcept.
cISubsetBindingIterator *cSubsetBindingIterator::Clone() { return new cSubsetBindingIterator(*this); }

bool cSubsetBindingIterator::IsEqual(const cISubsetBindingIterator *other)
{
    return mNode == static_cast<const cSubsetBindingIterator *>(other)->mNode;
}

cSubsetBinding *cSubsetBindingIterator::Get(cSubsetBinding *out)
{
    out->mName = Dispatch8(const_cast<ServiceCell *>(&mNode->first.cell));
    out->mMaterial = mNode->second.mMaterial.mIndex;
    out->mMask = mNode->second.mMask;
    out->mWord = mNode->second.mWord;
    return out;
}

void cSubsetBindingIterator::Next() { ++mNode; }

void cSubsetBindingIterator::Prev() { --mNode; }

} // namespace nGZSceneGraph
