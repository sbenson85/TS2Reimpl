#pragma once
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"

#include <cstdint>

// The game's compression interface (RTTI cIGZCompressor; db/interface-slots.jsonl, checked against cRZFastCompression3's
// vtable 0xe61b48 and the member list in rz/cRZFastCompression3.h). Implemented by rzface::cRZFastCompression3
// (cRZFastCompression3's own vtable), which adds the deleting destructor at slot 9.
class cIGZCompressor : public cIGZUnknown {
public:
    virtual bool CompressData(const void *src, uint32_t srcLength, void *dst, uint32_t *dstLength) = 0; // slot 3
    virtual bool DecompressData(const void *src, uint32_t srcLength, void *dst, uint32_t dstCapacity,
                                uint32_t *dstLength) = 0;                                               // slot 4
    virtual uint32_t GetMaxLengthRequiredForCompressedData(uint32_t length) = 0;                        // slot 5
    virtual uint32_t GetLengthOfCompressedData(const void *src) = 0;                                    // slot 6
    virtual uint32_t GetLengthOfDecompressedData(const void *src) = 0;                                  // slot 7
    virtual void GetCompressionVersionString(cIGZString &out) = 0;                                      // slot 8
};
