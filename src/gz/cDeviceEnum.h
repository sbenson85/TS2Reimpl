#pragma once
// The device enumeration's interfaces (lane NF, rendering campaign R1b; all slots lane NK, R2b): nGZGraphic4::cIDeviceEnum
// (the enumeration itself, ours: gz/cDeviceEnumDX.h), the display mode and pixel format iterators it hands out, the
// ranges they come in, and the first slots of cIPixelFormat (our pixel formats: gz/PixelFormat.h). Every result struct
// comes back through a hidden pointer, as the original's callers pass it.
#include <cstddef>
#include <cstdint>

#include <d3d9.h>

#include "gz/cIGZPropertySet.h" // cIGZPropertySetString
#include "gz/cIGZUnknown.h"

class cIGZString;

namespace nGZGraphic4 {

// nGZGraphic4::cIPixelFormat, slots 3-10 (the rest per format class: gz/PixelFormat.h).
class cIPixelFormat : public cIGZUnknown {
public:
    virtual uint32_t GetFormatId() = 0;     // slot 3: the D3DFORMAT (or FOURCC)
    virtual const char *GetName() = 0;      // slot 4
    virtual uint32_t GetClassId() = 0;      // slot 5
    virtual int32_t GetBitsPerPixel() = 0;  // slot 6 (CreateDevice compares it signed)
    virtual uint32_t GetBlockHeight() = 0;  // slot 7 (gz/PixelFormat.h: a guess)
    virtual uint32_t GetBlockWidth() = 0;   // slot 8 (a guess)
    virtual uint32_t GetRowBytes(uint32_t width) = 0;                 // slot 9
    // slot 10: the formats' order (class id first, then the class's fields); the pixel format and display mode sets
    // sort with strict false (lane NK).
    virtual bool Less(const cIPixelFormat *other, bool strict) = 0;
};

// nGZGraphic4::cDisplayMode (20 bytes): what the mode iterators hand out, and the first five words of the device's
// settings. The process default (0x10ca208, the constructor's) means "windowed on the desktop".
struct cDisplayMode {
    uint32_t mWidth;         // +0x00
    uint32_t mHeight;        // +0x04
    float mAspect;           // +0x08: compared as a float (ucomiss: a NaN never matches)
    uint32_t mRefresh;       // +0x0c: Hz (the present parameters' FullScreen_RefreshRateInHz)
    cIPixelFormat *mFormat;  // +0x10: the display format
};
static_assert(sizeof(cDisplayMode) == 0x14, "cDisplayMode size");

// cIGZBidirectionalIterator<cDisplayMode, cGZElementByValue<cDisplayMode>> (cDisplayModeIterator, vftable 0xd9f7e8,
// 9 slots).
class cIDisplayModeIterator : public cIGZUnknown {
public:
    virtual cIDisplayModeIterator *Clone() = 0;                    // slot 3: a new iterator at the same place (count 0)
    virtual bool IsEqual(const cIDisplayModeIterator *other) = 0;  // slot 4
    virtual cDisplayMode Get() = 0;                                // slot 5: the mode (copied out, 5 words)
    virtual void Next() = 0;                                       // slot 6
    virtual void Prev() = 0;                                       // slot 7
    virtual ~cIDisplayModeIterator() = default;                    // slot 8: the deleting destructor
};

// nGZGraphic4::cIPixelFormatIterator (cPixelFormatIterator, vftable 0xd9d084, 15 slots): a bidirectional iterator over
// a sorted, counted set of formats, with a class-id filter.
class cIPixelFormatIterator : public cIGZUnknown {
public:
    virtual cIPixelFormatIterator *Clone() = 0;                    // slot 3: cIGZIterator's (the original calls slot 8)
    virtual bool IsEqual(const cIPixelFormatIterator *other) = 0;  // slot 4
    virtual cIPixelFormat *Get() = 0;                              // slot 5: the format (not counted)
    virtual void Next() = 0;                                       // slot 6
    virtual void Prev() = 0;                                       // slot 7
    virtual cIPixelFormatIterator *CloneFormatIterator() = 0;      // slot 8: a new iterator at the same place (count 0)
    virtual void SetToBegin() = 0;                                 // slot 9
    virtual void SetToEnd() = 0;                                   // slot 10
    virtual void SetFilter(uint32_t classId) = 0;                  // slot 11: drops the formats of other classes (0: none)
    virtual void Insert(const cIPixelFormat *format) = 0;          // slot 12: when it passes the filter
    virtual uint32_t GetFilter() = 0;                              // slot 13
    virtual ~cIPixelFormatIterator() = default;                    // slot 14: the deleting destructor
};

// A range as the enumeration hands it out: a cursor and the two ends, each counted. Released end, begin, cursor
// (0x146670); copied by cloning the cursor and counting the ends (0x167180).
template <class Iterator> struct cIteratorRange {
    Iterator *mCurrent;
    Iterator *mBegin;
    Iterator *mEnd;
};
using cDisplayModeRange = cIteratorRange<cIDisplayModeIterator>;
using cPixelFormatRange = cIteratorRange<cIPixelFormatIterator>;
static_assert(sizeof(cDisplayModeRange) == 0xc, "range size");

// A surface's size as CanStretchRect takes it (two words).
struct cSurfaceExtent {
    uint32_t mWidth;
    uint32_t mHeight;
};

// nGZGraphic4::cIDeviceEnum / cIDeviceEnumDX (cDeviceEnumDX, vftable 0xd9f758, 30 slots). The format questions take a
// usage index into the table 0xd9f4b0 (CreateDevice passes 0 for colour and 0xb for depth); "for the display" means
// with the display format of the owner's settings (its mode's format, or the desktop's when the mode is the default).
class cIDeviceEnum : public cIGZUnknown {
public:
    virtual cIGZUnknown *GetDevice() = 0;   // 3 (shared 0x145770): the owning device (not counted)
    virtual cIGZUnknown *GetDeviceDX() = 0; // 4 (shared 0x145770): the same
    // 5 (0x178410): the driver's description, each into its string when given: the API name, its version, the driver
    // file, the driver version and GUID, the adapter's description, the PCI ids.
    virtual void GetDescription(cIGZString *api, cIGZString *apiVersion, cIGZString *driver, cIGZString *driverVersion,
                                cIGZString *description, cIGZString *ids) = 0;
    virtual cDisplayMode GetAdapterDisplayMode() = 0;                           // 6 (0x177bf0): the desktop's mode
    virtual cDisplayModeRange GetDisplayModes(cIPixelFormat *filter) = 0;       // 7 (0x178190): every mode (null filter)
    virtual cPixelFormatRange GetDisplayFormats() = 0;                          // 8 (0x178040): the modes' formats
    virtual cPixelFormatRange GetColorFormatsForDisplay(uint32_t usage, uint32_t iid) = 0; // 9 (0x1786b0)
    // 10 (0x178740): the colour formats usable on `displayFormat` that answer interface `iid`.
    virtual cPixelFormatRange GetColorFormats(uint32_t usage, cIPixelFormat *displayFormat, uint32_t iid) = 0;
    virtual bool IsColorFormatSupportedForDisplay(cIPixelFormat *format, uint32_t usage) = 0; // 11 (0x17a540)
    virtual bool IsColorFormatSupported(cIPixelFormat *format, uint32_t usage, cIPixelFormat *displayFormat) = 0; // 12
    virtual cPixelFormatRange GetDepthFormatsForDisplay(uint32_t usage, cIPixelFormat *color) = 0; // 13 (0x177f20)
    // 14 (0x177d70): the depth formats usable with `displayFormat` and the colour format `color`.
    virtual cPixelFormatRange GetDepthFormats(uint32_t usage, cIPixelFormat *displayFormat, cIPixelFormat *color) = 0;
    virtual bool IsDepthFormatSupportedForDisplay(cIPixelFormat *depth, uint32_t usage, cIPixelFormat *color) = 0; // 15
    virtual bool IsDepthFormatSupported(cIPixelFormat *depth, uint32_t usage, cIPixelFormat *displayFormat,
                                        cIPixelFormat *color) = 0;        // 16 (0x179eb0)
    virtual int32_t GetMultisampleQualitiesForDisplay(uint32_t type, cIPixelFormat *color) = 0; // 17 (0x17a1f0)
    // 18 (0x17a0c0): the multisample quality levels of `type` (-1: none); 0x18b7d0 asks it.
    virtual int32_t GetMultisampleQualities(uint32_t type, cIPixelFormat *displayFormat, cIPixelFormat *color) = 0;
    // 19 (0x17a5d0): whether StretchRect can copy `srcRect` of a `srcKind` surface to `dstRect` of a `dstKind` one.
    virtual bool CanStretchRect(uint32_t srcKind, const cSurfaceExtent *srcSize, const RECT *srcRect,
                                cIPixelFormat *srcFormat, uint32_t dstKind, const cSurfaceExtent *dstSize,
                                const RECT *dstRect, cIPixelFormat *dstFormat) = 0;
    // 20 (0x177a90): a vertex processing capability (a byte table at +0x6f8). CreateDevice asks 0 and 2 (hardware
    // processing; neither: software) and 7 (a pure device). The byte as stored (not made 0 / 1), as are slot 25's.
    virtual uint8_t HasProcessingCap(uint32_t index) = 0;
    virtual uint32_t GetLimit(uint32_t index) = 0;                        // 21 (0x178680): a word of the table at +0x704
    virtual cIGZPropertySetString *GetProperties() = 0;                   // 22 (0x177fb0): the settings Refresh reads
    virtual const D3DCAPS9 *GetCaps(bool hardware) = 0;                   // 23 (0x178010): null until read
    virtual const D3DCAPS9 *GetCurrentCaps() = 0;                         // 24 (0x177fe0): by the device's processing
    // 25 (0x177aa0): a device feature (a byte table at +0x770, from the properties): 0 event queries, 1 skip
    // ValidateDevice, 2 driver resource management, 3 no vsync (immediate presentation), 4 flip for discard swapping,
    // 5 no texture memory estimate adjustment, 6 the NVidia workaround texture. The property's byte as stored.
    virtual uint8_t HasFeature(uint32_t index) = 0;
    virtual uint32_t GetSetting(uint32_t index) = 0;                      // 26 (0x178690): a word at +0x778
    virtual D3DADAPTER_IDENTIFIER9 GetAdapterIdentifier() = 0;            // 27 (0x177fc0): copied out (0x44c bytes)
    virtual uint32_t GetTextureFormatForUsage(uint32_t usage) = 0;        // 28 (0x179d20): A8, else P8 (alpha palettes), or 0
    virtual ~cIDeviceEnum() = default;                                    // 29 (0x177960): the deleting destructor
};

} // namespace nGZGraphic4
