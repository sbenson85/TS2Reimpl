#include "rz/RZRefCountBase.h"

namespace rz {

// 0x1b9088: nothing but the vtable store (the compiler's).
cRZRefCount::~cRZRefCount() = default;

// 0x1b94af: the deleting destructor through slot 0 (push 1; call [vtable]).
uint32_t cRZRefCount::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

// 0x148fb0
uint32_t cRZRefCount::AddRef()
{
    return ++mRefCount;
}

// 0x1b9538
uint32_t cRZRefCount::RemoveRef()
{
    if (mRefCount != 0) --mRefCount;
    return mRefCount;
}

// 0x147a30
uint32_t cRZRefCount::RefCount()
{
    return mRefCount;
}

} // namespace rz
