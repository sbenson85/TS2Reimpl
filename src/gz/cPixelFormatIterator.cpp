// nGZGraphic4::cPixelFormatIterator (lane NK, rendering campaign R2): src/gz/cPixelFormatIterator.h.
#include "gz/cPixelFormatIterator.h"

#include <new>

#include "game/game.h"
#include "gz/PixelFormatStore.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"

namespace nGZGraphic4 {

namespace {

constexpr int32_t kManagerFormatCount = 0x38; // the manager's static formats CreateRange walks (kFormatCount)
static_assert(kManagerFormatCount == static_cast<int32_t>(kFormatCount), "the store's table is the one the manager's CreateIterator walks");

} // namespace

// ---- construction ----

cPixelFormatIterator::cPixelFormatIterator(const cIPixelFormat *const *formats, int32_t count, uint32_t filter)
    : mRefCount(0), mFilter(filter), mFormats(new cCountedPixelFormatSet)
{
    mFormats->AddRef();
    mCursor = mFormats->mValue.begin();
    for (int32_t i = 0; i < count; ++i) {
        const cIPixelFormat *const format = formats[i];
        if (mFilter != 0 && const_cast<cIPixelFormat *>(format)->GetClassId() != mFilter) continue;
        mFormats->mValue.insert(format);
        mCursor = mFormats->mValue.begin();
    }
}

cPixelFormatIterator::cPixelFormatIterator(const cPixelFormatIterator &other)
    : mRefCount(0), mFilter(other.mFilter), mFormats(other.mFormats)
{
    if (mFormats) mFormats->AddRef();
    mCursor = other.mCursor;
}

cPixelFormatIterator::~cPixelFormatIterator()
{
    if (mFormats) mFormats->Release();
}

void *cPixelFormatIterator::operator new(size_t size)
{
    if (size != kSize) return ::operator new(size);
    return GameAddress<RZFixedAllocatorBase>(kPool)->Alloc();
}

void cPixelFormatIterator::operator delete(void *block, size_t size)
{
    if (size != kSize) {
        ::operator delete(block);
        return;
    }
    GameAddress<RZFastFixed0x14>(kPool)->Free(block);
}

cPixelFormatIterator *cPixelFormatIterator::CreateEmpty() { return new cPixelFormatIterator(nullptr, 0, 0); }

cPixelFormatRange *cPixelFormatIterator::CreateRange(cPixelFormatsManagerDX *, cPixelFormatRange *out, uint32_t filter)
{
    const cIPixelFormat *const *const table = reinterpret_cast<const cIPixelFormat *const *>(CurrentStore().table);
    cIPixelFormatIterator *const iterator = new cPixelFormatIterator(table, kManagerFormatCount, filter);
    iterator->AddRef();
    cIPixelFormatIterator *const end = iterator->CloneFormatIterator();
    if (end) end->AddRef();
    end->SetToEnd();
    cIPixelFormatIterator *const current = iterator->Clone();
    out->mCurrent = current;
    if (current) current->AddRef();
    out->mBegin = iterator;
    iterator->AddRef();
    out->mEnd = end;
    if (end) end->AddRef();
    if (end) end->Release();
    iterator->Release();
    return out;
}

cPixelFormatRange *__fastcall cPixelFormatIteratorAdapters::CreateRange(cPixelFormatsManagerDX *manager, void *,
                                                                        cPixelFormatRange *out, uint32_t filter)
{
    return cPixelFormatIterator::CreateRange(manager, out, filter);
}

// ---- cIGZUnknown ----

bool cPixelFormatIterator::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (!ppvObj) return false;
    if (riid != 1 && riid != kIid) return false;
    *ppvObj = this;
    AddRef();
    return true;
}

uint32_t cPixelFormatIterator::AddRef() { return ++mRefCount; }

uint32_t cPixelFormatIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

// ---- the iterator ----

cIPixelFormatIterator *cPixelFormatIterator::Clone() { return CloneFormatIterator(); }

bool cPixelFormatIterator::IsEqual(const cIPixelFormatIterator *other)
{
    return mCursor == static_cast<const cPixelFormatIterator *>(other)->mCursor;
}

cIPixelFormat *cPixelFormatIterator::Get() { return const_cast<cIPixelFormat *>(*mCursor); }

void cPixelFormatIterator::Next() { ++mCursor; }

void cPixelFormatIterator::Prev() { --mCursor; }

cIPixelFormatIterator *cPixelFormatIterator::CloneFormatIterator() { return new cPixelFormatIterator(*this); }

void cPixelFormatIterator::SetToBegin() { mCursor = mFormats->mValue.begin(); }

void cPixelFormatIterator::SetToEnd() { mCursor = mFormats->mValue.end(); }

void cPixelFormatIterator::SetFilter(uint32_t classId)
{
    mFilter = classId;
    if (classId != 0) {
        cPixelFormatSet &formats = mFormats->mValue;
        for (auto it = formats.begin(); it != formats.end();) {
            const auto here = it++;
            if (const_cast<cIPixelFormat *>(*here)->GetClassId() != mFilter) formats.erase(here);
        }
    }
    mCursor = mFormats->mValue.begin();
}

void cPixelFormatIterator::Insert(const cIPixelFormat *format)
{
    if (mFilter != 0 && const_cast<cIPixelFormat *>(format)->GetClassId() != mFilter) return;
    mFormats->mValue.insert(format);
    mCursor = mFormats->mValue.begin();
}

uint32_t cPixelFormatIterator::GetFilter() { return mFilter; }

} // namespace nGZGraphic4
