#pragma once
#include <cstddef>
#include <cstdint>

// Sound channel implementing the cIGZSnd family (one vtable pointer per base subobject). Only the
// reference-counting slots are modelled: the primary vtable (RVA 0xe82164, 1.18.0.187) and the count
// at +0x20, which Release compares as signed.
class cHitChannel {
public:
    static constexpr uint32_t kVTable = 0xe82164;

    bool QueryInterface(uint32_t riid, void **ppvObj); // slot 0, 0xb32945
    uint32_t Release();                                // slot 2, 0xb32a01: deletes via slot 37

    const void *mVTable;
    uint8_t mBases[0x20 - sizeof(void *)]; // subobject vtables and base data (not modelled)
    int32_t mRefCount;
};

static_assert(offsetof(cHitChannel, mRefCount) == 0x20, "cHitChannel reference count offset");
