// The size-built (/O1) members of src/std/StdInstances.h.
#include "std/StdInstances.h"

#include <memory>

void IntSetObject::Destroy() noexcept
{
    std::destroy_at(&mSet);
}

UintUintMapObject::Map::iterator UintUintMapObject::Find(const uint32_t &key)
{
    return mMap.find(key);
}
