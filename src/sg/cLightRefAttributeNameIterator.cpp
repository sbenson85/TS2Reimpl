// cLightRefNode's attribute-name ranges (lane TN). See the header.
#include "sg/cLightRefAttributeNameIterator.h"

#include "game/game.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"

namespace nGZSceneGraph {

static_assert(offsetof(cLightRefAttributeNameIterator, mPosition) == 8, "light name iterator position");
static_assert(offsetof(cLightRefAttributeNameIterator, mName) == 0x10, "light name iterator name");

void *cLightRefAttributeNameIterator::operator new(size_t) noexcept
{
    return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f6340)->Alloc();
}
void cLightRefAttributeNameIterator::operator delete(void *block) noexcept
{
    reinterpret_cast<RZFastFixed0x14 *>(&ts2_rva_10f6340)->Free(block);
}

bool cLightRefAttributeNameIterator::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cLightRefAttributeNameIterator::AddRef() { return ++mRefCount; }

uint32_t cLightRefAttributeNameIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIArrayAttributeEntryIterator *cLightRefAttributeNameIterator::Clone() { return new cLightRefAttributeNameIterator(*this); }

bool cLightRefAttributeNameIterator::IsEqual(const cIArrayAttributeEntryIterator *other)
{
    return mPosition == static_cast<const cLightRefAttributeNameIterator *>(other)->mPosition;
}

cArrayAttributeEntryName cLightRefAttributeNameIterator::Get()
{
    return {uint32_t(uintptr_t(mGroup)), uint32_t(uintptr_t(mName))};
}

void cLightRefAttributeNameIterator::Next() { ++mPosition; }

void cLightRefAttributeNameIterator::Previous() { --mPosition; }

void cLightRefAttributeNameIterator::Advance(int32_t count) { mPosition += count; }

cArrayAttributeEntryName cLightRefAttributeNameIterator::At(int32_t) { return Get(); }

cArrayAttributeEntryRange *MakeLightAttributeNameRange(cArrayAttributeEntryRange *out, bool hasLight, const char *group,
                                                       const char *name)
{
    // The end first (the position 1 with a light), then the beginning, as the original.
    cLightRefAttributeNameIterator *const end = new cLightRefAttributeNameIterator(hasLight ? 1 : 0, group, name);
    cLightRefAttributeNameIterator *const begin = new cLightRefAttributeNameIterator(0, group, name);
    out->mCurrent = begin->Clone();
    if (out->mCurrent) out->mCurrent->AddRef();
    out->mBegin = begin;
    begin->AddRef();
    out->mEnd = end;
    if (end) end->AddRef();
    return out;
}

} // namespace nGZSceneGraph
