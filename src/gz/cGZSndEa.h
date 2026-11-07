#pragma once
#include <cstddef>
#include <cstdint>

// EA sound object implementing the cIGZSnd family. Only the reference-counting slots are modelled:
// the primary vtable (RVA 0xe800e4, 1.18.0.187) and the count at +0x10c, which is updated with
// RZAtomicAdd.
class cGZSndEa {
public:
    static constexpr uint32_t kVTable = 0xe800e4;

    bool QueryInterface(uint32_t riid, void **ppvObj); // slot 0, 0xb146c1
    uint32_t AddRef();                                 // slot 1, 0xb14128
    uint32_t Release();                                // slot 2, 0xb14787: deletes via slot 37

    const void *mVTable;
    uint8_t mBases[0x10c - sizeof(void *)]; // subobject vtables and members (not modelled)
    volatile int32_t mRefCount;
};

static_assert(offsetof(cGZSndEa, mRefCount) == 0x10c, "cGZSndEa reference count offset");
