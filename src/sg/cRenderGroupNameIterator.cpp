// nGZSceneGraph::cRenderGroupNameIterator (lane PX). See the header.
#include "sg/cRenderGroupNameIterator.h"

namespace nGZSceneGraph {

cRenderGroupNameIterator::cRenderGroupNameIterator(uint32_t mask, int32_t first) : mMask(mask), mBit(first - 1) { Next(); }

bool cRenderGroupNameIterator::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cRenderGroupNameIterator::AddRef() { return ++mRefCount; }

uint32_t cRenderGroupNameIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIRenderGroupNameIterator *cRenderGroupNameIterator::Clone()
{
    auto *copy = new cRenderGroupNameIterator(*this);
    copy->mRefCount = 0;
    return copy;
}

bool cRenderGroupNameIterator::IsEqual(const cIRenderGroupNameIterator *other)
{
    return mBit == static_cast<const cRenderGroupNameIterator *>(other)->mBit;
}

const char *cRenderGroupNameIterator::Get() { return RenderGroupRegistry()->NameOfBit(uint32_t(mBit)); }

void cRenderGroupNameIterator::Next()
{
    for (;;) {
        ++mBit;
        if (mBit > 0x1f) return;
        if ((mMask >> mBit & 1) && Get()) return;
    }
}

} // namespace nGZSceneGraph
