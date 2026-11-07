#pragma once
// nRZGraphic4::cSurfaceLockAsGZBuffer (lane VV, milestone O): a lockable surface's lock seen as a 2D pixel buffer, so the
// fonts can draw into a texture level (0x2c bytes, made on the stack by its users: the missing-texture drawing 0xae8310,
// the video texture strategy's Update 0x405130 and the text texture strategy's Update 0xaf44b0).
//
// Layout: the buffer stub (nRZGraphic4::cGZBufferStub: the vtable and a reference count, its constructor 0x18a290 and
// destructor-time part 0x18a320) and the lock record at +8 (nGZGraphic4::cSurfaceLock: the counted surface, the locked
// rectangle, the bits, the pitch, the row bytes and the rows).
//
// Standalone shape (docs/standalone-shape.md): the constructor 0x18a2a0 and destructor 0x18a330 are hooked (adapters in
// src/hook/tables/cSurfaceLockAsGZBuffer.cpp), so every buffer the image builds is ours and carries our compiler vtable;
// the image's vtable 0xda0ca8 (stored by those two alone) and its own slot bodies are reached by nothing. The buffer
// answers what a lock can: its size, its colour type from the surface's pixel format, single pixels, the colour
// conversions through the format's ARGB face; everything else is the stub's "not supported".
#include "gz/cDeviceEnum.h"
#include "gz/cIGZBuffer.h"
#include "gz/cSurfaceDX.h"

#include <cstdint>

namespace nGZGraphic4 {

// The ARGB face of a pixel format (interface 0x9ac38991; src/gz/PixelFormat.h cPixelFormatARGB), as the buffer calls it.
class cIPixelFormatARGBFace : public cIGZUnknown {
public:
    virtual uint32_t GetFormatId() = 0;                         // 3
    virtual const char *GetName() = 0;                          // 4
    virtual uint32_t GetClassId() = 0;                          // 5
    virtual int32_t GetBitsPerPixel() = 0;                      // 6
    virtual uint32_t GetBlockHeight() = 0;                      // 7
    virtual uint32_t GetBlockWidth() = 0;                       // 8
    virtual uint32_t GetRowBytes(uint32_t width) = 0;           // 9
    virtual bool Less(const void *other, bool strict) = 0;      // 10
    virtual uint32_t GetAlphaBits() = 0;                        // 11
    virtual uint32_t GetRedBits() = 0;                          // 12
    virtual uint32_t GetGreenBits() = 0;                        // 13
    virtual uint32_t GetBlueBits() = 0;                         // 14
    virtual uint32_t GetUnusedBits() = 0;                       // 15
    virtual uint32_t PackColor(const uint8_t *bgra) = 0;        // 16: blue, green, red, alpha bytes to a pixel
    virtual uint8_t *UnpackColor(uint8_t *bgra, uint32_t pixel) = 0; // 17: a pixel to those bytes; returns bgra
};

} // namespace nGZGraphic4

namespace nRZGraphic4 {

// The buffer stub (vtable 0xda0c00): a counted cIGZBuffer whose QueryInterface (0x18a8c0) answers 1 and cIGZBuffer,
// whose Release (0x18a8f0) destroys the object at zero. cBackbufferAsGZBuffer has its own copy (src/gz/cGZGraphicSystem.h).
class cGZBufferStub : public cIGZBuffer {
public:
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    virtual ~cGZBufferStub() = default; // slot 40: the deleting destructor (0x18a400 for the stub alone)

protected:
    uint32_t mRefCount = 0; // +4
};

class cSurfaceLockAsGZBuffer : public cGZBufferStub {
public:
    explicit cSurfaceLockAsGZBuffer(nGZGraphic4::cILockableSurface *surface); // 0x18a2a0: locks the whole surface
    ~cSurfaceLockAsGZBuffer() override;                                        // 0x18a330; slot 40 deleting 0x18a420

    bool Init(uint32_t, uint32_t, uint32_t, uint32_t) override;               // 3: false
    bool Shutdown() override;                                                  // 4: false
    bool Uninitialize() override;                                              // 5: false
    bool Lock(uint32_t) override;                                              // 6: true
    bool Unlock(uint32_t) override;                                            // 7: true
    bool IsLocked() override;                                                  // 8: the lock holds a surface
    uint32_t GetWidth() override;                                              // 9: right - left
    uint32_t GetHeight() override;                                             // 10: bottom - top
    cGZRect GetBounds() override;                                              // 11: the locked rectangle
    const cGZRect *GetBoundsPtr() override;                                    // 12: the same, in one static
    uint32_t GetMemoryUsage() override;                                        // 13: 0
    cGZColorType GetColorType() override;                                      // 14: slot 15's
    cGZColorType GetNativeColorType() override;                                // 15: from the pixel format
    uint32_t GetBitsPerPixel() override;                                       // 16: the pixel format's
    uint32_t GetBytesPerPixel() override;                                      // 17: slot 16's / 8, signed
    bool Fill(uint32_t, const cGZRect *) override;                             // 18: false
    bool FillCoords(uint32_t, int32_t, int32_t, int32_t, int32_t, const cGZRect *) override; // 19: false
    bool FillRect(uint32_t, const cGZRect *, const cGZRect *) override;        // 20: false
    uint32_t GetPixel(uint32_t x, uint32_t y) override;                        // 21: 1-4 bytes per pixel, else 0
    void SetPixel(uint32_t x, uint32_t y, uint32_t value) override;            // 22: 1-4 bytes per pixel
    void SetTransparency(uint32_t) override;                                   // 23: nothing
    bool GetTransparency() override;                                           // 24: false
    bool IsPixelTransparent(uint32_t, uint32_t) override;                      // 25: false
    bool GetTransparentColor(uint32_t *) override;                             // 26: false
    void RemoveTransparency() override;                                        // 27: nothing
    bool DrawLine(int32_t, int32_t, int32_t, int32_t, uint32_t, int32_t) override; // 28: false
    bool Blt(cIGZBuffer *, const cGZRect *, const cGZRect *, const cGZRect *) override; // 29: false
    uint32_t ConvertRGBValueToNative(uint8_t r, uint8_t g, uint8_t b) override;          // 30: the format packs it
    bool ConvertNativeValueToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b) override; // 31: unpacks it
    uint32_t ConvertRGBAValueToNative(uint8_t, uint8_t, uint8_t, uint8_t) override;      // 32: 0
    bool ConvertNativeValueToRGBA(uint32_t, uint8_t *, uint8_t *, uint8_t *, uint8_t *) override; // 33: false
    void *GetColorSurfaceBits() override;                                      // 34: the lock's bits
    uint32_t GetColorSurfaceStride() override;                                 // 35: the lock's pitch
    uint32_t GetChangeCounter() override;                                      // 36: 0
    cIGZUnknown *GetHardwareCache() override;                                  // 37: null
    bool SetHardwareCache(cIGZUnknown *) override;                             // 38: false
    bool IsReady() override;                                                   // 39: the lock holds a surface

    // 0x18a440: the colour type code of a pixel format, from its ARGB face (2: 8 bits; 3-7: 16 bits by alpha and green;
    // 8 and 9: 32 bits without and with alpha; 0 otherwise or without the face).
    static uint32_t ColorTypeOf(nGZGraphic4::cIPixelFormat *format);

    nGZGraphic4::cSurfaceLock mLock; // +8
};
static_assert(sizeof(cSurfaceLockAsGZBuffer) == 0x2c, "cSurfaceLockAsGZBuffer: 0x2c bytes");

} // namespace nRZGraphic4
