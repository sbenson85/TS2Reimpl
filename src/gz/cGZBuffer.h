#pragma once
#include "gz/cIGZBuffer.h"

#include <cstddef>
#include <cstdint>

#include "game/owned.h"
#include "rz/RZFastFixedAllocators.h"

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x54>, 105f82c); // cGZBuffer's pool (operator new / delete)

// cGZBuffer and cGZTransientBuffer (1.18.0.187), in the standalone shape (docs/standalone-shape.md).
//
// cGZBuffer: the game's 2D pixel buffer, 0x54 bytes from the 0x54 pool (cRZFastFixedAllocator2<0x54> at 0x105f82c, built by
// XC slot 505). Faces cIGZBuffer +0 (vtable 0xe4f210, 47 slots: the interface's 40, then the deleting destructor 40 and
// slots 41..46 the class adds), cIGZBufferShader +4 (0xe4f2d0, 5 slots), cIGZAggregate +8 (0xe4f2e8, 6 slots). Class id
// 0xc470d325: the creator 0x8c75d0 (block from the pool, then the constructor 0x8c5710), registered by the
// cGZDrawCOMDirector getter 0x8b7f30, is the only creation site and the class's one hook.
//
// cGZTransientBuffer: a cGZBuffer over memory someone else owns (a surface lock), 0x68 bytes from the heap. It adds
// cIGZTransientBuffer at +0x54 (vtable 0xe4f3f8, 10 slots); its other vtables are 0xe4f304 / 0xe4f3c4 / 0xe4f3dc. Class
// id 0x6cf55d22: the creator 0x8c7620 (new 0x68, the cGZBuffer constructor, the rest inline; the +0x54 face back),
// registered by the same getter, is the only creation site (its out-of-line constructor 0x8c7190 and destructor 0x8c71d0
// are dead code) and the class's one hook.
//
// With the two creators hooked every buffer carries our vtables, so every slot body below is in effect through them
// (db/status/cGZBuffer.jsonl, "in_effect": "callers"). The pixel converter 0x8c6b10 (BltUnclipped's) is ours
// (src/gz/GZConvertPixels.cpp). Still game code, called from here: the graphics service getter 0x9c5a05 (InitPixelFormat).
class cGZBuffer : public cIGZBuffer, public cIGZBufferShader, public cIGZAggregate {
public:
    static constexpr uint32_t kSize = 0x54;
    static constexpr uint32_t kClassId = 0xc470d325;
    static constexpr uint32_t kPool = 0x105f82c; // cRZFastFixedAllocator2<0x54>

    // Flag bits (+0x2c) and lock flags.
    static constexpr uint32_t kFlagTransparent = 2;
    static constexpr uint32_t kLockCountsChange = 0x8000; // Lock: the caller will write the pixels
    static constexpr uint32_t kLockRead = 0x40;           // BltUnclipped locks its source with this

    cGZBuffer(); // 0x8c5710
    // 0x8c5790 (dead: inlined into the deleting destructors). The first virtual the class adds, so its deleting destructor
    // takes slot 40 (0x8c78b0), after the interface's 40 and before slots 41..46.
    virtual ~cGZBuffer();

    static void *operator new(size_t size);                // 0x54 bytes from the pool, any other size from the heap
    static void operator delete(void *block, size_t size); // 0x8c78b0's flags & 1 path (the pool's Free inline)

    // 0x8c75d0: the creator the COM director registers (pool block, constructor; the primary face back).
    static cIGZBuffer *__cdecl Create();

    // cIGZUnknown (thunks 0x8c7d09 / 0x8c7a13 / 0x8c7d31 at +4, 0x8c7d11 / 0x8c7a1b / 0x8c7d39 at +8)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x8c5850: the outer's, else NonDelegatingQueryInterface
    uint32_t AddRef() override;                              // 0x8c5870
    uint32_t Release() override;                             // 0x8c5890: deletes at zero (slot 40, flag 1)

    // cIGZBuffer
    bool Init(uint32_t width, uint32_t height, uint32_t format, uint32_t bits) override; // 0x8c58c0
    bool Shutdown() override;                                                            // 0x8c5930
    bool Uninitialize() override;                                                        // 0x8c5950
    bool Lock(uint32_t flags) override;                                                  // 0x8c5970
    bool Unlock(uint32_t flags) override;                                                // 0x8c59a0
    bool IsLocked() override;                                                            // 0x8c7d00
    uint32_t GetWidth() override;                                                        // shared_vf_15e550
    uint32_t GetHeight() override;                                                       // shared_vf_15e970
    cGZRect GetBounds() override;                                                        // shared_vf_1f4e50
    const cGZRect *GetBoundsPtr() override;                                              // shared_vf_158f90
    uint32_t GetMemoryUsage() override;                                                  // 0x8c59d0
    cGZColorType GetColorType() override;                                                // shared_vf_149d60
    cGZColorType GetNativeColorType() override;                                          // shared_vf_149d60
    uint32_t GetBitsPerPixel() override;                                                 // shared_vf_149d00
    uint32_t GetBytesPerPixel() override;                                                // 0x8c7c90
    bool Fill(uint32_t value, const cGZRect *clip) override;                             // 0x8c5c00
    bool FillCoords(uint32_t value, int32_t left, int32_t top, int32_t right, int32_t bottom,
                    const cGZRect *clip) override;                                       // 0x8c5bc0
    bool FillRect(uint32_t value, const cGZRect *rect, const cGZRect *clip) override;    // 0x8c59f0
    uint32_t GetPixel(uint32_t x, uint32_t y) override;                                  // 0x8c5c20
    void SetPixel(uint32_t x, uint32_t y, uint32_t value) override;                      // 0x8c5c90
    void SetTransparency(uint32_t colour) override;                                      // 0x8c5d00
    bool GetTransparency() override;                                                     // 0x8c7cf0
    bool IsPixelTransparent(uint32_t x, uint32_t y) override;                            // 0x8c5d40
    bool GetTransparentColor(uint32_t *colour) override;                                 // 0x8c5d90
    void RemoveTransparency() override;                                                  // 0x8c5d20
    bool DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t colour, int32_t thickness) override; // 0x8c5db0
    bool Blt(cIGZBuffer *source, const cGZRect *sourceRect, const cGZRect *destination,
             const cGZRect *clip) override;                                              // 0x8c6420
    uint32_t ConvertRGBValueToNative(uint8_t r, uint8_t g, uint8_t b) override;          // 0x8c65f0
    bool ConvertNativeValueToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b) override; // 0x8c66d0
    uint32_t ConvertRGBAValueToNative(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;    // 0x8c67f0
    bool ConvertNativeValueToRGBA(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) override; // 0x8c68f0
    void *GetColorSurfaceBits() override;                                                // 0x8c6a60
    uint32_t GetColorSurfaceStride() override;                                           // 0x8c6a70
    uint32_t GetChangeCounter() override;                                                // 0x8c6a80
    cIGZUnknown *GetHardwareCache() override;                                            // shared_vf_169f60
    bool SetHardwareCache(cIGZUnknown *cache) override;                                  // 0x8c7d60
    bool IsReady() override;                                                             // 0x8c59c0

    // Slots the class adds (after the destructor, slot 40).
    // 41, 0x8c64d0: clips `destination` to `bounds`' rectangle (slot 12) and then to `clip`, moving `source`'s edges by the
    // same amounts; false when nothing is left. `this` is not used.
    virtual bool ClipRects(cIGZBuffer *bounds, cGZRect *source, cGZRect *destination, const cGZRect *clip);
    virtual bool AllocatePixels();      // 42, 0x8c6ee0: slot 43, the pixel pointer cleared when it fails
    virtual bool AllocatePixelMemory(); // 43, 0x8c6f00: format (slot 45), stride, new[] of stride * height
    virtual bool FreePixelMemory();     // 44, 0x8c6f40
    virtual void InitPixelFormat();     // 45, 0x8c6f60: formats 3 and 1 become the display's
    // 46, 0x8c7060: copies `sourceRect` of `source` (locked for reading) to `destination` here through the converter.
    virtual bool BltUnclipped(cIGZBuffer *source, const cGZRect *sourceRect, const cGZRect *destination);

    // cIGZBufferShader
    bool SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;     // 0x8c6aa0
    bool GetColor(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) override; // 0x8c6ad0

    // cIGZAggregate
    bool NonDelegatingQueryInterface(uint32_t riid, void **out) override; // 0x8c57f0
    void SetOuter(cIGZUnknown *outer) override;                          // 0x8c57d0
    cIGZUnknown *GetOuter() override;                                    // 0x8c57e0

    // Helpers the slots call directly (originals in effect through them).
    static uint32_t FormatMask(uint32_t format);      // 0x8c7ca0: the colour bits of a pixel of `format`
    static uint32_t BytesPerPixelOf(uint32_t format); // 0x8c76f0: 2 for formats 4..7, 4 for 8 and 9, else 0
    // 0x8c7a50 (DrawLine's): clips the segment to the rectangle, edges included (Cohen-Sutherland, in place); false when
    // nothing of it is left.
    static bool ClipLine(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1, int32_t left, int32_t top, int32_t right,
                         int32_t bottom);

    bool mReady;                   // +0x0c
    cGZColorType mColorType;       // +0x10 (format 3, 16 bits until Init)
    cGZRect mBounds;               // +0x18: {0, 0, width, height}
    uint32_t mTransparentColour;   // +0x28
    uint32_t mFlags;               // +0x2c (bit 1: transparency on)
    uint32_t mRefCount;            // +0x30
    uint32_t mChangeCounter;       // +0x34
    uint32_t mShaderColor;         // +0x38: r | g << 8 | b << 16 | a << 24; ~0 when unset
    uint16_t mLockCount;           // +0x3c
    uint8_t mBytesPerPixel;        // +0x3e
    uint8_t *mPixels;              // +0x40
    uint32_t mStride;              // +0x44: bytes per row
    uint32_t mLockFlags;           // +0x48
    cIGZUnknown *mHardwareCache;   // +0x4c (slot 3 takes the buffer, slot 9 hears of transparency changes)
    cIGZUnknown *mOuter;           // +0x50
};

static_assert(offsetof(cGZBuffer, mReady) == 0x0c, "cGZBuffer: ready flag at +0x0c");
static_assert(offsetof(cGZBuffer, mColorType) == 0x10, "cGZBuffer: colour type at +0x10");
static_assert(offsetof(cGZBuffer, mBounds) == 0x18, "cGZBuffer: bounds at +0x18");
static_assert(offsetof(cGZBuffer, mTransparentColour) == 0x28, "cGZBuffer: transparent colour at +0x28");
static_assert(offsetof(cGZBuffer, mFlags) == 0x2c, "cGZBuffer: flags at +0x2c");
static_assert(offsetof(cGZBuffer, mRefCount) == 0x30, "cGZBuffer: reference count at +0x30");
static_assert(offsetof(cGZBuffer, mChangeCounter) == 0x34, "cGZBuffer: change counter at +0x34");
static_assert(offsetof(cGZBuffer, mShaderColor) == 0x38, "cGZBuffer: shader colour at +0x38");
static_assert(offsetof(cGZBuffer, mLockCount) == 0x3c, "cGZBuffer: lock count at +0x3c");
static_assert(offsetof(cGZBuffer, mBytesPerPixel) == 0x3e, "cGZBuffer: bytes per pixel at +0x3e");
static_assert(offsetof(cGZBuffer, mPixels) == 0x40, "cGZBuffer: pixel memory at +0x40");
static_assert(offsetof(cGZBuffer, mStride) == 0x44, "cGZBuffer: stride at +0x44");
static_assert(offsetof(cGZBuffer, mLockFlags) == 0x48, "cGZBuffer: lock flags at +0x48");
static_assert(offsetof(cGZBuffer, mHardwareCache) == 0x4c, "cGZBuffer: hardware cache at +0x4c");
static_assert(offsetof(cGZBuffer, mOuter) == 0x50, "cGZBuffer: outer unknown at +0x50");
static_assert(sizeof(cGZBuffer) == cGZBuffer::kSize, "cGZBuffer: 0x54 bytes (the pool's block)");

class cGZTransientBuffer : public cGZBuffer, public cIGZTransientBuffer {
public:
    static constexpr uint32_t kSize = 0x68;
    static constexpr uint32_t kClassId = 0x6cf55d22;

    cGZTransientBuffer();           // inline in the creator 0x8c7620 (0x8c7190 is the dead out-of-line copy)
    ~cGZTransientBuffer() override; // 0x8c71d0 (dead); deleting destructor slot 40, 0x8c7950

    // 0x8c7620: the creator the COM director registers (the +0x54 face back, null when new fails).
    static cIGZTransientBuffer *__cdecl Create();

    // cIGZUnknown (thunks 0x8c7d19 / 0x8c7a23 / 0x8c7d41 at +4, 0x8c7d21 / 0x8c7a2b / 0x8c7d49 at +8, 0x8c7d29 / 0x8c7a33 /
    // 0x8c7d51 at +0x54)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x8c72c0: NonDelegatingQueryInterface, outer or not
    uint32_t AddRef() override;                              // 0x8c7270
    uint32_t Release() override;                             // 0x8c7290

    // cIGZBuffer / cGZBuffer slots it overrides
    bool Lock(uint32_t flags) override;     // 0x8c74e0: realizes the memory first
    bool AllocatePixelMemory() override;    // 0x8c7530: also drops the external-memory flag
    bool FreePixelMemory() override;        // 0x8c7580: external memory is let go, not freed

    // cIGZAggregate
    bool NonDelegatingQueryInterface(uint32_t riid, void **out) override; // 0x8c72d0: also the +0x54 face

    // cIGZTransientBuffer
    cIGZBuffer *GetBuffer() override;                                       // 0x8c7a40
    void SetMemory(void *pixels, uint32_t stride, uint32_t width, uint32_t height, uint32_t format,
                   cIGZUnknown *owner) override;                            // 0x8c7360
    void Commit() override;                                                 // 0x8c7420
    bool Realize() override;                                                // 0x8c7450
    void SetTarget(cIGZTransientBufferTarget *target) override;             // 0x8c7480
    cIGZTransientBufferTarget *GetTarget() override;                        // shared_vf_147a30
    uint32_t GetLockTotal() override;                                       // shared_vf_145770

    cIGZTransientBufferTarget *mTarget; // +0x58
    cIGZUnknown *mMemoryOwner;          // +0x5c: holds the external memory (SetMemory's owner)
    uint32_t mLockTotal;                // +0x60: Locks that got past the realize step
    bool mExternalMemory;               // +0x64: the pixels are SetMemory's
};

static_assert(offsetof(cGZTransientBuffer, mTarget) == 0x58, "cGZTransientBuffer: target at +0x58");
static_assert(offsetof(cGZTransientBuffer, mMemoryOwner) == 0x5c, "cGZTransientBuffer: memory owner at +0x5c");
static_assert(offsetof(cGZTransientBuffer, mLockTotal) == 0x60, "cGZTransientBuffer: lock total at +0x60");
static_assert(offsetof(cGZTransientBuffer, mExternalMemory) == 0x64, "cGZTransientBuffer: external memory flag at +0x64");
static_assert(sizeof(cGZTransientBuffer) == cGZTransientBuffer::kSize, "cGZTransientBuffer: 0x68 bytes");
