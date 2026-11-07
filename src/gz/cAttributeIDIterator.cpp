// nGZEvent::cAttributeIDIterator (lane VX). See gz/cAttributeIDIterator.h.
#include "gz/cAttributeIDIterator.h"

#include "game/vtable.h"

#include <cstring>
#include <new>

namespace nGZEvent {

cAttributeIDIterator *cAttributeIDIterator::Construct(cIGZUnknown *ids, const uint32_t *const *cursor)
{
    mRefCount = 0;
    mVTable = &ts2_rva_d9bd10;
    mIds = ids;
    if (ids) ids->AddRef();
    mAt = *cursor;
    return this;
}

cAttributeIDIterator *cAttributeIDIterator::CopyConstruct(const cAttributeIDIterator &source)
{
    mRefCount = 0;
    mVTable = &ts2_rva_d9bd10;
    mIds = source.mIds;
    if (mIds) mIds->AddRef();
    mAt = source.mAt;
    return this;
}

void cAttributeIDIterator::Destruct()
{
    mVTable = &ts2_rva_d9bd10;
    if (mIds) mIds->Release();
}

void *cAttributeIDIterator::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

bool cAttributeIDIterator::QueryInterface(uint32_t riid, void **out)
{
    if (out && riid == 1) {
        *out = this;
        RawSlot<1>(this);
        return true;
    }
    return false;
}

uint32_t cAttributeIDIterator::AddRef()
{
    return ++mRefCount;
}

uint32_t cAttributeIDIterator::Release()
{
    const uint32_t count = --mRefCount;
    if (count == 0) {
        RawSlot<7, void *>(this, 1u);
        return 0;
    }
    return count;
}

cAttributeIDIterator *cAttributeIDIterator::Clone() const
{
    auto *const copy = static_cast<cAttributeIDIterator *>(::operator new(sizeof(cAttributeIDIterator)));
    if (copy) return copy->CopyConstruct(*this);
    return nullptr;
}

bool cAttributeIDIterator::IsEqual(const cAttributeIDIterator *other) const
{
    return mAt == other->mAt;
}

uint32_t cAttributeIDIterator::Get() const
{
    return *mAt;
}

void cAttributeIDIterator::Next()
{
    ++mAt;
}

} // namespace nGZEvent

// The table an image-free program stores in place of 0xd9bd10 (standalone/owned_statics.cpp aliases the owned static to
// it): our members slot for slot, each the function the hooked game runs for that image slot where it is hooked.
namespace {
template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
using nGZEvent::cAttributeIDIterator;
} // namespace

extern "C" const void *ts2_own_vt_d9bd10[8] = {
    Member(&cAttributeIDIterator::QueryInterface),     // 0x1a849d
    Member(&cAttributeIDIterator::AddRef),             // 0x148fb0
    Member(&cAttributeIDIterator::Release),            // 0x1a84c0
    Member(&cAttributeIDIterator::Clone),              // 0x1a8475
    Member(&cAttributeIDIterator::IsEqual),            // 0x178550
    Member(&cAttributeIDIterator::Get),                // 0x1a8492
    Member(&cAttributeIDIterator::Next),               // 0x1a8498
    Member(&cAttributeIDIterator::DeletingDestructor), // 0x1468b0
};
