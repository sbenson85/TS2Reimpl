// The size-profile members of src/misc/StartupObjects.h (lane HQ).
#include "misc/StartupObjects.h"

#include <memory>

namespace startupobj {

ZeroedVector20 *ZeroedVector20::Construct()
{
    std::construct_at(&items);
    extra = 0;
    return this;
}

uint32_t FixedPool::ChunkBytes(uint32_t elementSize)
{
    const uint32_t perChunk = elementSize < 0x200 ? 0x80 : elementSize < 0x800 ? 0x20 : 0x10;
    return elementSize * perChunk;
}

void FixedPool::Construct(uint32_t size)
{
    elementSize = size < 4 ? 4 : size;
    chunkBytes = ChunkBytes(elementSize);
}

ListAndLock *ListAndLock::Construct()
{
    std::construct_at(&items);
    lock.Construct(); // the original destroys the list if this throws (it doesn't)
    return this;
}

NamedWordHashSet *NamedWordHashSet::Construct(const void *, const void *)
{
    std::construct_at(&set);
    return this;
}

} // namespace startupobj
