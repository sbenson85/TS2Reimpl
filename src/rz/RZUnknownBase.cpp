#include "rz/RZUnknownBase.h"

namespace rz {

// 0x1b945c: AddRef through the vtable (slot 1), as the original.
bool cRZUnknown::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1) return false;
    *out = static_cast<cIGZUnknown *>(this);
    AddRef();
    return true;
}

// 0x148fb0
uint32_t cRZUnknown::AddRef()
{
    return ++mRefCount;
}

// 0x1b94e9: the deleting destructor through slot 3 (push 1; call [vtable + 0xc]).
uint32_t cRZUnknown::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

// 0x1b9096: nothing but the vtable store (the compiler's).
cRZUnknown::~cRZUnknown() = default;

// 0x1b9538
uint32_t cRZUnknown::RemoveRef()
{
    if (mRefCount != 0) --mRefCount;
    return mRefCount;
}

// 0x147a30
uint32_t cRZUnknown::RefCount()
{
    return mRefCount;
}

} // namespace rz
