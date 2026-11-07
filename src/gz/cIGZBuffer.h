#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

// The 2D pixel buffer interfaces (1.18.0.187), as cGZBuffer and cGZTransientBuffer implement them (src/gz/cGZBuffer.h).
// Slot order is ABI. Names are ours where the image has none (the vtable slots' RVAs are in cGZBuffer.h).

// A rectangle in buffer coordinates, right and bottom exclusive (16 bytes, copied whole by the slots).
struct cGZRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};
static_assert(sizeof(cGZRect) == 16, "cGZRect: 16 bytes");

// A pixel format and its bits per pixel. The constructor 0x18a220 takes the format alone: format 2 has 8 bits, 3 to 7
// have 16, 8 and 9 have 32, any other 0 (the image's table at 0x18a268). Formats as the buffer's colour conversions read
// them: 4 and 6 are 1555, 5 is 565, 7 is 4444, 8 and 9 are 8888 (888 with no alpha); 3 and 1 mean "pick the display's"
// (cGZBuffer::InitPixelFormat).
struct cGZColorType {
    uint32_t format;
    uint32_t bits;

    cGZColorType() = default;
    cGZColorType(uint32_t format, uint32_t bits) : format(format), bits(bits) {}
    explicit cGZColorType(uint32_t format); // as 0x18a220 (not hooked: other classes' code calls it too)
};
static_assert(sizeof(cGZColorType) == 8, "cGZColorType: 8 bytes");

class cIGZBuffer;

// cIGZBuffer (primary face of cGZBuffer, 40 slots; the classes add 7 more, see cGZBuffer.h).
class cIGZBuffer : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x20732180;

    virtual bool Init(uint32_t width, uint32_t height, uint32_t format, uint32_t bits) = 0; // slot 3
    virtual bool Shutdown() = 0;                                                            // slot 4
    virtual bool Uninitialize() = 0;                                                        // slot 5
    virtual bool Lock(uint32_t flags) = 0;                                                  // slot 6
    virtual bool Unlock(uint32_t flags) = 0;                                                // slot 7
    virtual bool IsLocked() = 0;                                                            // slot 8
    virtual uint32_t GetWidth() = 0;                                                        // slot 9
    virtual uint32_t GetHeight() = 0;                                                       // slot 10
    virtual cGZRect GetBounds() = 0;                                                        // slot 11 (hidden result pointer)
    virtual const cGZRect *GetBoundsPtr() = 0;                                              // slot 12
    virtual uint32_t GetMemoryUsage() = 0;                                                  // slot 13
    virtual cGZColorType GetColorType() = 0;                                                // slot 14 (hidden result pointer)
    virtual cGZColorType GetNativeColorType() = 0;                                          // slot 15 (hidden result pointer)
    virtual uint32_t GetBitsPerPixel() = 0;                                                 // slot 16
    virtual uint32_t GetBytesPerPixel() = 0;                                                // slot 17
    virtual bool Fill(uint32_t value, const cGZRect *clip) = 0;                             // slot 18: the whole buffer
    virtual bool FillCoords(uint32_t value, int32_t left, int32_t top, int32_t right, int32_t bottom,
                            const cGZRect *clip) = 0;                                       // slot 19
    virtual bool FillRect(uint32_t value, const cGZRect *rect, const cGZRect *clip) = 0;    // slot 20
    virtual uint32_t GetPixel(uint32_t x, uint32_t y) = 0;                                  // slot 21
    virtual void SetPixel(uint32_t x, uint32_t y, uint32_t value) = 0;                      // slot 22
    virtual void SetTransparency(uint32_t colour) = 0;                                      // slot 23
    virtual bool GetTransparency() = 0;                                                     // slot 24
    virtual bool IsPixelTransparent(uint32_t x, uint32_t y) = 0;                            // slot 25
    virtual bool GetTransparentColor(uint32_t *colour) = 0;                                 // slot 26
    virtual void RemoveTransparency() = 0;                                                  // slot 27
    // slot 28: from (x0, y0) to (x1, y1), both ends included, `thickness` pixels wide
    virtual bool DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t colour, int32_t thickness) = 0;
    virtual bool Blt(cIGZBuffer *source, const cGZRect *sourceRect, const cGZRect *destination,
                     const cGZRect *clip) = 0;                                              // slot 29
    virtual uint32_t ConvertRGBValueToNative(uint8_t r, uint8_t g, uint8_t b) = 0;          // slot 30
    virtual bool ConvertNativeValueToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b) = 0;          // slot 31
    virtual uint32_t ConvertRGBAValueToNative(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;             // slot 32
    virtual bool ConvertNativeValueToRGBA(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) = 0; // slot 33
    virtual void *GetColorSurfaceBits() = 0;                                                // slot 34
    virtual uint32_t GetColorSurfaceStride() = 0;                                           // slot 35
    virtual uint32_t GetChangeCounter() = 0;                                                // slot 36
    virtual cIGZUnknown *GetHardwareCache() = 0;                                            // slot 37
    virtual bool SetHardwareCache(cIGZUnknown *cache) = 0;                                  // slot 38
    virtual bool IsReady() = 0;                                                             // slot 39
};

// cIGZBufferShader (+4 of cGZBuffer): a colour the buffer carries for whoever draws it.
class cIGZBufferShader : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x86d72b57;

    virtual bool SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;     // slot 3
    virtual bool GetColor(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) = 0; // slot 4
};

// cIGZAggregate (+8 of cGZBuffer): COM aggregation. While an outer unknown is set, the object's own QueryInterface /
// AddRef / Release delegate to it; slot 3 answers for the object itself.
class cIGZAggregate : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x12f;

    virtual bool NonDelegatingQueryInterface(uint32_t riid, void **out) = 0; // slot 3
    virtual void SetOuter(cIGZUnknown *outer) = 0;                          // slot 4
    virtual cIGZUnknown *GetOuter() = 0;                                    // slot 5
};

class cIGZTransientBuffer;

// The listener a transient buffer reports to (+0x58 of cGZTransientBuffer): a caller's view, slots 3..6 as the buffer
// calls them, each with the buffer's cIGZTransientBuffer face.
class cIGZTransientBufferTarget : public cIGZUnknown {
public:
    virtual void Attach(cIGZTransientBuffer *buffer) = 0;  // slot 3: the buffer took this target
    virtual void Detach(cIGZTransientBuffer *buffer) = 0;  // slot 4: the buffer let it go
    virtual bool Realize(cIGZTransientBuffer *buffer) = 0; // slot 5: give the buffer its memory (SetMemory)
    virtual void Commit(cIGZTransientBuffer *buffer) = 0;  // slot 6: the buffer's external memory is done with
};

// cIGZTransientBuffer (+0x54 of cGZTransientBuffer): a buffer over memory someone else owns.
class cIGZTransientBuffer : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x8cf55d38;

    virtual cIGZBuffer *GetBuffer() = 0;                                          // slot 3
    virtual void SetMemory(void *pixels, uint32_t stride, uint32_t width, uint32_t height, uint32_t format,
                           cIGZUnknown *owner) = 0;                               // slot 4
    virtual void Commit() = 0;                                                    // slot 5
    virtual bool Realize() = 0;                                                   // slot 6
    virtual void SetTarget(cIGZTransientBufferTarget *target) = 0;                // slot 7
    virtual cIGZTransientBufferTarget *GetTarget() = 0;                           // slot 8
    virtual uint32_t GetLockTotal() = 0;                                          // slot 9
};
