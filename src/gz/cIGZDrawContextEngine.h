#pragma once
// The 2D drawing back ends and their service (1.18.0.187; docs/subsystems/rendering.md, R5). A cGZDrawContext
// (src/gz/cGZDrawContext.h) keeps the drawing state (origin, clip, colours, line width, texture) and hands the
// primitives to an engine: cGZDrawContextRenderToHardware (nGZGraphic4::cDraw2D derives from it: Direct3D) or
// cGZDrawContextRenderToBuffer (software, into a cIGZBuffer). The draw system service (cGZDrawSystem, id 0xeb2ea3cb)
// keeps the engine factories and makes an engine for a target buffer.
//
// Slot order is ABI. These are callers' views: named where the draw context's use shows what the slot does, numbered
// otherwise (stack words as the implementations' `ret N`).
#include <cstdint>

#include "gz/cIGZBuffer.h"
#include "gz/cIGZUnknown.h"

// A locked target's pixels (0x18 bytes, as the engine's LockSurface writes them; cGZDrawContextAsBuffer keeps one at
// +0x38). Rows are `stride` bytes apart, pixels `bytesPerPixel` (1 to 4) bytes wide.
struct cGZDrawSurface {
    uint8_t *bits;
    int32_t stride;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t bytesPerPixel;
};
static_assert(sizeof(cGZDrawSurface) == 0x18, "cGZDrawSurface: 0x18 bytes");

// cIGZDrawContextEngine (35 slots:cGZDrawContextRenderToHardware 0xe4ec84, cDraw2D +8 0xd9c474,
// cGZDrawContextRenderToBuffer).
class cIGZDrawContextEngine : public cIGZUnknown {
public:
    // slot 3: the context's target, when it takes the engine; false when the engine cannot draw on it.
    virtual bool SetTarget(cIGZBuffer *target) = 0;
    virtual void Detach() = 0;                                  // slot 4: the context lets the engine go
    virtual cGZRect *GetBounds(cGZRect *bounds) = 0;            // slot 5: the target's drawable area, written and returned
    virtual void SetClip(const cGZRect *clip) = 0;              // slot 6
    virtual void SetColors(uint32_t first, uint32_t count, const uint32_t *argb) = 0; // slot 7: the context's four
    virtual void vf8(uint32_t value) = 0;                       // slot 8: the state word (context +0x48)
    virtual void vf9(uint8_t value) = 0;                        // slot 9: the state byte +0x46
    virtual void vf10(uint8_t value) = 0;                       // slot 10: the state byte +0x45
    virtual void vf11(uint8_t value) = 0;                       // slot 11: the state byte +0x44
    virtual void SetLineWidth(float width) = 0;                 // slot 12
    virtual void SetGradient(const float *coefficients, float x, float y) = 0; // slot 13: null clears it
    // slot 14: a texture drawn at (x, y) scaled by (sx, sy); null clears it. Writes the area it covers to `area` and
    // answers whether a texture is set.
    virtual bool SetTexture(cIGZUnknown *texture, float x, float y, float sx, float sy, cGZRect *area) = 0;
    virtual void ClearTextureClip() = 0;                        // slot 15
    virtual void SetTextureClip(const cGZRect *clip) = 0;       // slot 16
    virtual void vf17() = 0;                                    // slot 17
    virtual void vf18() = 0;                                    // slot 18
    virtual void vf19() = 0;                                    // slot 19
    // slot 20: the target's pixels for direct access (cGZDrawContextAsBuffer's Lock and GetPixel ask through the
    // context's slot 47); false when the engine cannot give them.
    virtual bool LockSurface(cGZDrawSurface *surface, uint32_t readOnly) = 0;
    virtual void UnlockSurface() = 0;                           // slot 21
    // slot 22: how the engine takes primitives of `kind` (0 not at all, 1 and 2 two ways the context prepares them).
    virtual uint32_t ChooseRoute(uint32_t kind, const float *points, uint32_t count) = 0;
    virtual void DrawSpans(const int32_t *spans, uint32_t count) = 0; // slot 23: `count` (row << 16 | x) span ends pairs
};

class cIGZTransientBuffer;

// cIGZDrawContextEngineFactory (iid 0x6b2ea76c): makes an engine for a target it can draw on (cGZDrawSystem keeps them).
class cIGZDrawContextEngineFactory : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x6b2ea76c;

    // slot 3: a new engine (counted) in `engine` and true when the factory takes `target`; false otherwise.
    virtual bool CreateEngine(cIGZBuffer *target, cIGZDrawContextEngine **engine) = 0;
};

// cIGZBufferHardwareCache (nGZGraphic4::cDraw2DBufferHardwareCache, vtable 0xd9c558): a 2D texture made from a buffer.
// The draw system's view: the slots its Trim, FlushAll and statistics call.
class cIGZBufferHardwareCache : public cIGZUnknown {
public:
    virtual void vf3(uint32_t) = 0;        // 0x1501b0 (ret 4)
    virtual void vf4() = 0;
    virtual void vf5() = 0;
    virtual void Unload() = 0;             // slot 6: the texture let go
    virtual cIGZBuffer *GetBuffer() = 0;   // slot 7: the buffer it was made from
    virtual void vf8() = 0;
    virtual void vf9() = 0;
    virtual bool IsLoaded() = 0;           // slot 10: a texture is held
    virtual void vf11(uint32_t) = 0;       // 0x151480 (ret 4)
    virtual uint32_t GetLastUseTick() = 0; // slot 12: the draw system tick it was last drawn at
};

// cIGZDrawSystem (cGZDrawSystem's primary face, 21 slots; src/gz/cGZDrawSystem.h).
class cIGZDrawSystem : public cIGZUnknown {
public:
    static constexpr uint32_t kServiceId = 0xeb2ea3cb;
    static constexpr uint32_t kIid = 0x4b2ea3b3;

    virtual void AddFactory(cIGZDrawContextEngineFactory *factory) = 0; // slot 3: kept (counted), asked first
    // slot 4: the first registered factory that makes an engine for `target` writes it to `engine`.
    virtual bool CreateEngine(cIGZBuffer *target, cIGZDrawContextEngine **engine) = 0;
    // slot 5: a new drawable buffer: a cGZDrawContextAsBuffer over a new graphics-system buffer of that size and colour
    // format, drawn on by a new cGZDrawContext.
    virtual bool CreateBuffer(cIGZBuffer **out, uint32_t width, uint32_t height, uint32_t format) = 0;
    // slot 6: pixels from one format to another (the converter 0x8c6b10); true without a call when the area is empty.
    virtual bool ConvertPixels(uint8_t *to, uint32_t toStride, uint32_t toFormat, const uint8_t *from, uint32_t fromStride,
                               uint32_t fromFormat, int32_t width, int32_t height, const uint32_t *key) = 0;
    virtual void SetMemoryBudget(uint32_t bytes) = 0;                                   // slot 7
    virtual uint32_t GetMemoryBudget() = 0;                                             // slot 8
    virtual void AddTransientBuffer(cIGZTransientBuffer *buffer) = 0;                   // slot 9: watched (uncounted)
    virtual void RemoveTransientBuffer(cIGZTransientBuffer *buffer) = 0;                // slot 10
    virtual void AddHardwareCache(cIGZBufferHardwareCache *cache) = 0;                  // slot 11: watched (uncounted)
    virtual void RemoveHardwareCache(cIGZBufferHardwareCache *cache) = 0;               // slot 12
    virtual void Trim(uint32_t minimumAge) = 0;                                         // slot 13
    virtual void FlushAll() = 0;                         // slot 14: every cache unloaded, every buffer committed
    virtual uint32_t GetTick() = 0;                                                     // slot 15
    virtual void GetHardwareCacheUsage(uint32_t *count, uint32_t *bytes) = 0;           // slot 16: the loaded ones
    virtual void GetTransientBufferUsage(uint32_t *count, uint32_t *ready, uint32_t *bytes) = 0; // slot 17
    virtual bool vf18(uint32_t, uint32_t) = 0;          // slot 18: false (its two stack words unread)
    virtual void SetFlag(bool value) = 0;                                               // slot 19
    virtual bool GetFlag() = 0;                                                         // slot 20
};
