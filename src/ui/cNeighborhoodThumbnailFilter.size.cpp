// cNeighborhoodThumbnailFilter (standalone shape; src/ui/cNeighborhoodThumbnailFilter.h).
#include "ui/cNeighborhoodThumbnailFilter.h"

#include <cstring>
#include <new>

namespace {

thumbfilter::Key ReadKey(const uint32_t *words)
{
    thumbfilter::Key key;
    std::memcpy(&key, words, sizeof key);
    return key;
}

} // namespace

bool cNeighborhoodThumbnailFilter::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kInterfaceId) return false;
    *out = static_cast<cIGZPersistResourceKeyFilter *>(this);
    AddRef();
    return true;
}

uint32_t cNeighborhoodThumbnailFilter::AddRef()
{
    return ++mRefCount;
}

uint32_t cNeighborhoodThumbnailFilter::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

bool cNeighborhoodThumbnailFilter::IsKeyIncluded(const uint32_t *key)
{
    return mKeys.find(ReadKey(key)) != mKeys.end();
}

void cNeighborhoodThumbnailFilter::AddKey(const uint32_t *key)
{
    mKeys.insert(ReadKey(key));
}

cNeighborhoodThumbnailFilter *__fastcall cNeighborhoodThumbnailFilterAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cNeighborhoodThumbnailFilter;
}
