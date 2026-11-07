#pragma once
#include "game/owned.h"
#include "gz/cIGZCompressor.h"
#include "gz/cIGZString.h"

#include <cstdint>

// cIGZCompressor implementation using RefPack (src/rz/RefPack.h). Compressed buffers carry a 4-byte
// little-endian prefix holding their total length, followed by the RefPack stream.
// Original vtable: RVA 0xe61b48 (1.18.0.187).
class cRZFastCompression3 {
public:
    static constexpr uint32_t kVTable = 0xe61b48; // the game's; VTable() is its owned symbol
    static const void *VTable();                  // the word our constructor and destructor store
    static const void *OwnVTable();               // rzface::cRZFastCompression3's: the image-free program's VTable()
    static constexpr uint32_t kGZIID_cIGZUnknown = 0x00000001;
    static constexpr uint32_t kGZIID_cIGZCompressor = 0x8359908e;

    bool QueryInterface(uint32_t riid, void **ppvObj);                 // slot 0, 0x9cc2d1
    bool CompressData(const void *src, uint32_t srcLength, void *dst, uint32_t *dstLength); // slot 3, 0x9cc0f9
    uint32_t AddRef();                                                 // slot 1, 0x148fb0 (folded; not hooked)
    uint32_t Release();                                                // slot 2, 0x9cc309
    bool DecompressData(const void *src, uint32_t srcLength, void *dst, uint32_t dstCapacity,
                        uint32_t *dstLength);                          // slot 4, 0x9cc174
    uint32_t GetMaxLengthRequiredForCompressedData(uint32_t length);   // slot 5, 0x9cc2bd
    uint32_t GetLengthOfCompressedData(const void *src);               // slot 6, 0x9cc229
    uint32_t GetLengthOfDecompressedData(const void *src);             // slot 7, 0x9cc26f
    void GetCompressionVersionString(cIGZString &out);                 // slot 8, 0x9cc1ed
    bool HasValidHeader(const void *src, uint32_t size);              // 0x9cc320 (thiscall, ret 8)
    void Destruct();                                                   // 0x9cc0d3
    cRZFastCompression3 *DeletingDestructor(uint32_t flags);           // slot 9, 0x9cc0da

    const void *mVTable;
    uint32_t mRefCount;
    bool mFlag; // passed to the compressor
};

static_assert(sizeof(cRZFastCompression3) == 12, "cRZFastCompression3 layout");

// The vtable (owned.h, OwnedVTable): `offset ts2_rva_e61b48`, the same one-instruction store as GameAddress(kVTable).
TS2_OWNED_STATIC(const OwnedVTable, e61b48);

namespace rzface {

// cRZFastCompression3's own vtable (the image-free program's): a cIGZCompressor face over the hook-shaped class (no data;
// `this` is the compressor). Each slot runs the member of the same name (src/rz/cRZFastCompression3.size.cpp), slot 9
// the deleting destructor. Namespaced so that its RTTI name is still cRZFastCompression3 (tools/matchcheck.py ties our
// vtable to the game's 0xe61b48).
class cRZFastCompression3 final : public cIGZCompressor {
public:
    bool QueryInterface(uint32_t riid, void **ppvObj) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    bool CompressData(const void *src, uint32_t srcLength, void *dst, uint32_t *dstLength) override;
    bool DecompressData(const void *src, uint32_t srcLength, void *dst, uint32_t dstCapacity,
                        uint32_t *dstLength) override;
    uint32_t GetMaxLengthRequiredForCompressedData(uint32_t length) override;
    uint32_t GetLengthOfCompressedData(const void *src) override;
    uint32_t GetLengthOfDecompressedData(const void *src) override;
    void GetCompressionVersionString(cIGZString &out) override;
    virtual void *DeletingDestructor(uint32_t flags); // slot 9
};

} // namespace rzface

inline const void *cRZFastCompression3::VTable() { return &ts2_rva_e61b48; }

// The address of rzface::cRZFastCompression3's vtable as a link-time constant: the compiler folds the local face's vptr
// into `offset ??_7cRZFastCompression3@rzface@@6B@` (no object is built).
inline const void *cRZFastCompression3::OwnVTable()
{
    rzface::cRZFastCompression3 face;
    return *reinterpret_cast<const void *const *>(&face);
}
