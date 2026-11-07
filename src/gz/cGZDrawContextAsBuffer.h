#pragma once
// cGZDrawContextAsBuffer: a cIGZBuffer over a draw context and its target buffer (docs/subsystems/rendering.md, R5). The
// buffer slots draw through the context (fills, lines, pixels, blits as textured rectangles), read pixels through the
// context's surface lock, and pass transparency and the hardware cache on to the target buffer.
//
// 0x58 bytes from the 0x58 pool (cRZFastFixedAllocator2<0x58> at 0x105f820). Faces: cIGZBuffer +0 (vtable 0xe4ef98, 40
// slots), cIGZBufferShader +4 (0xe4f03c), cRZRefCount +8 (0xe4f054: the deleting destructor 0x8c2350, Release, AddRef and
// the count's two shared bodies), cIGZDrawContextAsBuffer +0x10 (0xe4f06c, 10 slots). Made only by cGZDrawSystem slot 5
// (0x8c0130: the pool block 0x8c1460, the constructor 0x8c13a0, AddRef, then slot 7 of the +0x10 face with the new
// context and the target). Standalone shape (docs/standalone-shape.md): the constructor is hooked
// (cGZDrawContextAsBufferAdapters::Construct), so every object carries our vtables and the slots, the deleting
// destructor and the colour helper 0x8c2110 are in effect through their callers.
#include <cstddef>
#include <cstdint>

#include "gz/cGZDrawContext.h"
#include "gz/cIGZBuffer.h"
#include "gz/cIGZDrawContextEngine.h"
#include "rz/RZRefCountBase.h"

// cIGZDrawContextAsBuffer (iid 0xab300b2b, 10 slots): the buffer's view of its draw context.
class cIGZDrawContextAsBuffer : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xab300b2b;

    virtual cIGZBuffer *GetBuffer() = 0;                                  // slot 3
    // slot 4: the context, given the buffer as its target (and a batch begun) if it has none.
    virtual cIGZDrawContext *GetContext() = 0;
    virtual cIGZDrawContext *LockContext() = 0;                           // slot 5: Lock(0x8010), the context
    virtual void UnlockContext() = 0;                                     // slot 6: Unlock(0x8010)
    virtual void Init(cIGZDrawContext *context, cIGZBuffer *target) = 0;  // slot 7
    virtual void Refresh() = 0;                                           // slot 8: the target's bounds again
    // slot 9: the context's colour from `source`'s cIGZBufferShader colour (white without one).
    virtual void SetColorFromShader(cIGZUnknown *source) = 0;
};

class cGZDrawContextAsBuffer : public cIGZBuffer,
                               public cIGZBufferShader,
                               public rz::cRZRefCount,
                               public cIGZDrawContextAsBuffer {
public:
    static constexpr uint32_t kSize = 0x58;
    static constexpr uint32_t kPool = 0x105f820; // cRZFastFixedAllocator2<0x58>
    static constexpr uint32_t kLockWrite = 0x8000;

    cGZDrawContextAsBuffer(); // 0x8c13a0
    // 0x8c22a0, the COM director's creator (class 0xab300b20; the constructor inline): a new object (count 0) as its
    // cIGZDrawContextAsBuffer face.
    static cIGZDrawContextAsBuffer *Create();
    // The deleting destructor 0x8c2350 (slot 0 of the +8 face): releases the shader, the target and the context.
    ~cGZDrawContextAsBuffer() override;

    static void *operator new(size_t size);                // 0x8c1460: a block from the pool (any other size: heap)
    static void operator delete(void *block, size_t size); // 0x8c2350's flags & 1 path (the pool's Free inline)

    // cIGZUnknown (the +4 and +0x10 faces' are thunks; +8's Release / AddRef too)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x8c14f0: its faces, else the target buffer's
    uint32_t AddRef() override;                              // 0x8c14d0: the count at +0xc
    uint32_t Release() override;                             // 0x8c14e0: deletes at 1 or below

    // cIGZBuffer
    bool Init(uint32_t width, uint32_t height, uint32_t format, uint32_t bits) override; // 0x8c17d0: false
    bool Shutdown() override;                                                            // 0x8c17e0: true
    bool Uninitialize() override;                                                        // 0x8c17f0: false
    bool Lock(uint32_t flags) override;                                                  // 0x8c1800
    bool Unlock(uint32_t flags) override;                                                // 0x8c1890
    bool IsLocked() override;                                                            // 0x8c18c0
    uint32_t GetWidth() override;                                                        // 0x8c18d0
    uint32_t GetHeight() override;                                                       // 0x8c18e0
    cGZRect GetBounds() override;                                                        // 0x8c1900
    const cGZRect *GetBoundsPtr() override;                                              // 0x8c18f0
    uint32_t GetMemoryUsage() override;                                                  // 0x8c1910: 0
    cGZColorType GetColorType() override;                                                // 0x8c1940
    cGZColorType GetNativeColorType() override;                                          // 0x8c1920
    uint32_t GetBitsPerPixel() override;                                                 // 0x8c1970
    uint32_t GetBytesPerPixel() override;                                                // 0x8c1980
    bool Fill(uint32_t value, const cGZRect *clip) override;                             // 0x8c1a30
    bool FillCoords(uint32_t value, int32_t left, int32_t top, int32_t right, int32_t bottom,
                    const cGZRect *clip) override;                                       // 0x8c19f0
    bool FillRect(uint32_t value, const cGZRect *rect, const cGZRect *clip) override;    // 0x8c1990
    uint32_t GetPixel(uint32_t x, uint32_t y) override;                                  // 0x8c1a90
    void SetPixel(uint32_t x, uint32_t y, uint32_t value) override;                      // 0x8c1b60
    void SetTransparency(uint32_t colour) override;                                      // 0x8c1ba0
    bool GetTransparency() override;                                                     // 0x8c1bb0
    bool IsPixelTransparent(uint32_t x, uint32_t y) override;                            // 0x8c1bc0
    bool GetTransparentColor(uint32_t *colour) override;                                 // 0x8c1c80
    void RemoveTransparency() override;                                                  // 0x8c1c90
    bool DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t colour, int32_t thickness) override; // 0x8c1ca0
    bool Blt(cIGZBuffer *source, const cGZRect *sourceRect, const cGZRect *destination,
             const cGZRect *clip) override;                                              // 0x8c1ea0
    uint32_t ConvertRGBValueToNative(uint8_t r, uint8_t g, uint8_t b) override;          // 0x8c1f60
    bool ConvertNativeValueToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b) override; // 0x8c2040
    uint32_t ConvertRGBAValueToNative(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;    // 0x8c2070: 0
    bool ConvertNativeValueToRGBA(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) override; // 0x8c2080
    void *GetColorSurfaceBits() override;                                                // 0x8c2090
    uint32_t GetColorSurfaceStride() override;                                           // 0x8c20a0
    uint32_t GetChangeCounter() override;                                                // 0x8c20b0
    cIGZUnknown *GetHardwareCache() override;                                            // 0x8c20c0
    bool SetHardwareCache(cIGZUnknown *cache) override;                                  // 0x8c20d0
    bool IsReady() override;                                                             // 0x8c2100: true

    // cIGZBufferShader (+4)
    bool SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;     // 0x8c1730
    bool GetColor(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) override; // 0x8c1790

    // cIGZDrawContextAsBuffer (+0x10)
    cIGZBuffer *GetBuffer() override;                                      // shared_vf_6c5d60 (`lea eax, [ecx - 0x10]`)
    cIGZDrawContext *GetContext() override;                                // 0x8c1570
    cIGZDrawContext *LockContext() override;                               // 0x8c15a0
    void UnlockContext() override;                                         // 0x8c15c0
    void Init(cIGZDrawContext *context, cIGZBuffer *target) override;      // 0x8c15d0
    void Refresh() override;                                               // 0x8c16f0
    void SetColorFromShader(cIGZUnknown *source) override;                 // 0x8c2200

    // 0x8c2110: a value in the buffer's format (+0x34) as 0xAARRGGBB, alpha full (0 for an unknown format).
    uint32_t NativeToArgb(uint32_t value);
    // 0x8c1e50: the ellipse in the rectangle (the context's slot 37) in the value's colour; true. Shaped as DrawLine
    // (the thickness is not used). Nothing in the image references it: no vtable slot, no call (a method left over
    // from an older cIGZBuffer); hooked so the colour helper's every caller is ours.
    bool DrawEllipse(int32_t left, int32_t top, int32_t right, int32_t bottom, uint32_t value, int32_t thickness);

    cIGZDrawContext *mContext;  // +0x14
    cIGZBuffer *mBuffer;        // +0x18 the target
    cIGZBufferShader *mShader;  // +0x1c the target's shader face (Init)
    cGZRect mBounds;            // +0x20 the target's (Init, Refresh)
    int32_t mLockCount;         // +0x30
    uint32_t mFormat;           // +0x34 the target's native format (Init)
    cGZDrawSurface mSurface;    // +0x38 the locked pixels (Lock); Init sets width, height and bytes per pixel
    uint32_t mColor;            // +0x50 the shader colour, r in the low byte (0xffffffff: none)
    bool mTransparent;          // +0x54
    bool mRebatch;              // +0x55 Lock ended the context's batch (or it had no target): Unlock begins one
    uint8_t mPad56[2];
};

// The hooks' adapters: the constructor 0x8c13a0 (ecx = the pool block) and DrawEllipse 0x8c1e50 (ecx = the object).
struct cGZDrawContextAsBufferAdapters {
    static cGZDrawContextAsBuffer *__fastcall Construct(void *memory, void *);
    static bool __fastcall DrawEllipse(cGZDrawContextAsBuffer *self, void *, int32_t left, int32_t top, int32_t right,
                                       int32_t bottom, uint32_t value, int32_t thickness);
};
