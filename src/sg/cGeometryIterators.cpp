// The container's own iterators (lane PA). Layouts and the game's slot bodies: src/sg/cGeometryIterators.h.
#include "sg/cGeometryIterators.h"

namespace nGZSceneGraph {

// ---- cRZNullRandomAccessIterator<unsigned short> (vtable 0xe73580) ----

bool cGeometryNullIterator::QueryInterface(uint32_t iid, void **out)
{
    if (out == nullptr || iid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGeometryNullIterator::AddRef() { return ++mRefCount; }

uint32_t cGeometryNullIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIGeometryIterator<uint16_t> *cGeometryNullIterator::Clone() { return new cGeometryNullIterator; }

bool cGeometryNullIterator::IsEqual(const cIGeometryIterator<uint16_t> *) { return true; }

uint16_t cGeometryNullIterator::Get() { return 0; }

void cGeometryNullIterator::Next() {}

void cGeometryNullIterator::Previous() {}

void cGeometryNullIterator::Advance(int32_t) {}

uint16_t cGeometryNullIterator::At(int32_t) { return Get(); }

// ---- cUnitIterator (vtable 0xe73384) ----

bool cGeometryUnitIterator::QueryInterface(uint32_t iid, void **out)
{
    if (out == nullptr || iid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGeometryUnitIterator::AddRef() { return ++mRefCount; }

uint32_t cGeometryUnitIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIGeometryIterator<uint16_t> *cGeometryUnitIterator::Clone() { return new cGeometryUnitIterator(mValue); }

bool cGeometryUnitIterator::IsEqual(const cIGeometryIterator<uint16_t> *other)
{
    return mValue == static_cast<const cGeometryUnitIterator *>(other)->mValue;
}

uint16_t cGeometryUnitIterator::Get() { return mValue; }

void cGeometryUnitIterator::Next() { ++mValue; }

void cGeometryUnitIterator::Previous() { --mValue; }

void cGeometryUnitIterator::Advance(int32_t count) { mValue = static_cast<uint16_t>(mValue + count); }

uint16_t cGeometryUnitIterator::At(int32_t index) { return static_cast<uint16_t>(mValue + index); }

} // namespace nGZSceneGraph
