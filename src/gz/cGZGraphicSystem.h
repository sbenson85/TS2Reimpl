#pragma once
// nGZGraphic4::cGZGraphicSystem (RTTI 0x10950c0; vftables 0xd9bd78, 16 slots, 0xd9bdbc at +8, 69 slots, and 0xd9bed4
// at +0xc, 13 slots; 0x40 bytes) and its back buffer nGZGraphic4::cBackbufferAsGZBuffer (RTTI 0x1094fe4; vftables
// 0xd9bf0c, 41 slots, and 0xd9bfb4 at +8, 5 slots; 0x38 bytes): lane NQ, rendering campaign R2b step 3.
//
// The graphic system is the framework's service 0xc416025c (cIGZGraphicSystem, iid 0x73283c, at +8): the GZ face of the
// Direct3D graphic system (cGraphicSystemDX). It owns a cIGZBuffer that stands for the current device's back buffer
// (cBackbufferAsGZBuffer: the size and colour type of the Direct3D back buffer, its colour conversions through the
// surface's pixel format) and, once the device first reports itself ready, a draw-context buffer over it (COM class
// 0xab300b20, cGZDrawContextAsBuffer) that every 2D drawer draws through (slot 8 ActiveDrawBuffer). Init opens the
// window (the canvas service 0xfb6372ee) at the configured size and sets the display mode (0x18b440 / 0x18b250);
// Shutdown lets the buffers go. Most of the 69 slots are stubs the Direct3D build never filled.
//
// Standalone shape (docs/standalone-shape.md): both classes are ours whole. The creator 0x1475e0 (the graphics COM director's
// class 0x20732180) is hooked to Create and is the switch: it is the only code
// that stores either class's vtables (the deleting destructors 0x146970 / 0x1468d0 the others), so every slot of both
// is in effect through callers.
#include "gz/cAbstractBehaviorTypeMT.h"
#include "gz/cIGZBuffer.h"
#include "gz/cIGZSystemService.h"

#include <cstddef>
#include <cstdint>

class cIGZString;
class cIGZDrawContextAsBuffer;

#include "game/owned.h"
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10c9500); // GetEventManager's cache
TS2_OWNED_STATIC(const nGZEvent::cEventAttribute, feb1c0); // slot 14's record for an unknown event

namespace nGZGraphic4 {

class cISurfaceDX;

// The primary interface: the behaviour slots (0-14) and the virtual destructor at slot 15.
class cGZGraphicSystemBehavior : public nGZEvent::cIAbstractBehavior {
public:
    virtual ~cGZGraphicSystemBehavior() = default; // 15: the deleting destructor
};

// The screen's size and depth (slot 10).
struct cGZGraphicSystemMetrics {
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mBits;
};

// The display settings slot 53 takes (7 words; Init reads the first three and the fullscreen byte after them).
struct cGZGraphicSystemSettings {
    uint32_t mWidth;   // +0x00 (0: Init opens no window)
    uint32_t mHeight;  // +0x04
    uint32_t mBits;    // +0x08: the colour depth the formats are picked for
    uint32_t mRest[4]; // +0x0c: never read here
};

// cIGZGraphicSystem (iid 0x73283c, 69 slots). Slot names are ours where the RTTI has none; the stubs are the shared
// bodies the class's vtable points at (each with its argument count).
class cIGZGraphicSystem : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x73283c;

    virtual uint32_t CreateBuffer(void **out) = 0;                        // 3: a new cGZBuffer (COM class 0xc470d325)
    virtual uint32_t CreateBufferAs(uint32_t iid, void **out) = 0;        // 4: the same, as `iid`
    virtual bool Slot5() = 0;                                             // 5: false
    virtual void Slot6(uint32_t) = 0;                                     // 6
    virtual uint32_t Slot7() = 0;                                         // 7: 0
    virtual cIGZBuffer *ActiveDrawBuffer() = 0;                           // 8: the draw buffer, else the back buffer
    virtual cGZColorType *GetScreenColorType(cGZColorType *out) = 0;      // 9: the active buffer's native type
    virtual cGZGraphicSystemMetrics *GetScreenMetrics(cGZGraphicSystemMetrics *out) = 0; // 10
    virtual cGZGraphicSystemMetrics *GetZeroMetrics(cGZGraphicSystemMetrics *out) = 0;   // 11: all zero
    virtual bool IsFullscreen() = 0;                                      // 12: the current device's
    virtual bool Slot13() = 0;                                            // 13: false
    virtual cIGZUnknown *GetCanvas() = 0;                                 // 14: the canvas service (not counted)
    virtual bool Slot15(uint32_t, uint32_t) = 0;                          // 15: true
    virtual uint32_t Slot16(uint32_t value) = 0;                          // 16: the argument
    virtual bool Slot17() = 0;                                            // 17: false
    virtual bool BeginModal() = 0;                                        // 18: the window's slot 23; true
    virtual bool EndModal() = 0;                                          // 19: the window's slot 24; not its slot 36
    virtual bool IsWindowActive() = 0;                                    // 20: not the window's slot 36
    virtual int ShowMessageBox(cIGZString *text, cIGZString *caption) = 0;    // 21: MessageBoxA between 18 and 19
    virtual uint32_t Slot22(uint32_t) = 0;                                // 22: 0
    virtual uint32_t Slot23(uint32_t) = 0;                                // 23: 0
    virtual uint32_t Slot24(uint32_t, uint32_t) = 0;                      // 24: 0
    virtual void Slot25() = 0;                                            // 25
    virtual bool GetScale(uint32_t *x, uint32_t *y) = 0;                  // 26: 1 and 1; true
    virtual uint32_t Slot27() = 0;                                        // 27: 0
    virtual bool Slot28(uint32_t, uint32_t, uint32_t) = 0;                // 28: true
    virtual bool Slot29(uint32_t, uint32_t) = 0;                          // 29: true
    virtual uint32_t ShowCursor(uint32_t show) = 0;                       // 30: 0
    virtual void SetCursor(void *buffer, int32_t hotX, int32_t hotY) = 0; // 31
    virtual bool UseSystemCursor() = 0;                                   // 32: true
    virtual bool GetCursorPosition(int32_t *x, int32_t *y) = 0;           // 33: the cursor in the window's client area
    virtual uint32_t ConvertRGBToNative(uint8_t r, uint8_t g, uint8_t b) = 0; // 34: the active buffer's slot 30
    virtual bool ConvertRGBToNativeAt(uint32_t *out, uint8_t r, uint8_t g, uint8_t b) = 0; // 35: the same, stored
    virtual bool ConvertNativeToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b) = 0; // 36: its slot 31
    virtual void Slot37(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0; // 37-46
    virtual void Slot38(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void Slot39(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void Slot40(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void Slot41(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void Slot42(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void Slot43(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void Slot44(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void Slot45(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void Slot46(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void Slot47(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0; // 47-48
    virtual void Slot48(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void Slot49(uint32_t) = 0;                                    // 49
    virtual float *GetRate50(float *out) = 0;                             // 50-52: 60.0; return out (the original leaves it in eax: the out-getter shape that crashed OR/OX)
    virtual float *GetRate51(float *out) = 0;
    virtual float *GetRate52(float *out) = 0;
    virtual void SetSettings(const cGZGraphicSystemSettings *settings) = 0; // 53
    virtual void Slot54(uint32_t) = 0;                                    // 54
    virtual void SetFullscreen(bool fullscreen) = 0;                      // 55
    virtual void Slot56(uint32_t) = 0;                                    // 56-57
    virtual void Slot57(uint32_t) = 0;
    virtual uint32_t Slot58() = 0;                                        // 58-60: 0
    virtual uint32_t Slot59() = 0;
    virtual uint32_t Slot60() = 0;
    virtual void Slot61(uint32_t) = 0;                                    // 61-64
    virtual void Slot62(uint32_t) = 0;
    virtual void Slot63(uint32_t) = 0;
    virtual void Slot64(uint32_t) = 0;
    virtual void Slot65() = 0;                                            // 65-68
    virtual void Slot66() = 0;
    virtual void Slot67() = 0;
    virtual void Slot68() = 0;
};

// cIGZRawBuffer (iid 0xe8c95947; the back buffer's +8 face, 5 slots).
class cBackbufferAsGZBufferRaw : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xe8c95947;
    virtual bool QuerySurface(uint32_t iid, void **out) = 0; // 3: the surface's QueryInterface
    virtual bool Slot4() = 0;                                // 4: true
};

// The back buffer: a cIGZBuffer that only knows its size, colour type, transparency and hardware cache; it draws
// nothing (the drawing goes through the draw-context buffer the system makes over it). nRZGraphic4::cGZBufferStub is
// its base (constructor 0x18a290: the stub's vtable, count 0); the stub's bodies the vtable keeps are below as ours.
// nRZGraphic4::cGZBufferStub's state: the count at +4 (plain).
class cBackbufferAsGZBufferStub : public cIGZBuffer {
public:
    uint32_t mRefCount = 0; // +0x04
};

class cBackbufferAsGZBuffer : public cBackbufferAsGZBufferStub, public cBackbufferAsGZBufferRaw {
public:
    static constexpr size_t kSize = 0x38;
    static constexpr uint32_t kLockCounted = 0x8000; // Lock flags that count a change

    cBackbufferAsGZBuffer();
    virtual ~cBackbufferAsGZBuffer(); // 40: 0x1468d0 (the deleting destructor)

    // ---- cIGZUnknown (both faces) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x148370 (stub 0x18a8c0 below it); +8 0x1483af
    uint32_t AddRef() override;                              // shared 0x146a50 (jmp 0x148fb0); +8 0x146a55
    uint32_t Release() override;                             // 0x148860 (jmp the stub's 0x18a8f0); +8 0x148865

    // ---- cIGZBuffer ----
    bool Init(uint32_t width, uint32_t height, uint32_t format, uint32_t bits) override; // shared 0x147110: false
    bool Shutdown() override;                                // 0x148a10: the surface let go
    bool Uninitialize() override;                            // shared 0x147480: false
    bool Lock(uint32_t flags) override;                      // 0x1480b0
    bool Unlock(uint32_t flags) override;                    // shared 0x148260: true
    bool IsLocked() override;                                // shared 0x147480: false
    uint32_t GetWidth() override;                            // shared 0x148ba0 (+0x18)
    uint32_t GetHeight() override;                           // shared 0x147d00 (+0x1c)
    cGZRect GetBounds() override;                            // shared 0x147790
    const cGZRect *GetBoundsPtr() override;                  // shared 0x147780
    uint32_t GetMemoryUsage() override;                      // shared 0x6c6b0: 0
    cGZColorType GetColorType() override;                    // shared 0x147810
    cGZColorType GetNativeColorType() override;              // shared 0x147810
    uint32_t GetBitsPerPixel() override;                     // shared 0x147770
    uint32_t GetBytesPerPixel() override;                    // 0x1477a0
    bool Fill(uint32_t value, const cGZRect *clip) override; // shared 0x1474b0: false
    bool FillCoords(uint32_t value, int32_t left, int32_t top, int32_t right, int32_t bottom,
                    const cGZRect *clip) override;           // shared 0x147490: false
    bool FillRect(uint32_t value, const cGZRect *rect, const cGZRect *clip) override; // shared 0x1474a0: false
    uint32_t GetPixel(uint32_t x, uint32_t y) override;      // shared 0x6c6a0: 0
    void SetPixel(uint32_t x, uint32_t y, uint32_t value) override; // shared 0x377e0
    void SetTransparency(uint32_t colour) override;          // 0x148a00
    bool GetTransparency() override;                         // shared 0x147480: false
    bool IsPixelTransparent(uint32_t x, uint32_t y) override; // shared 0x1474b0: false
    bool GetTransparentColor(uint32_t *colour) override;     // 0x147a50
    void RemoveTransparency() override;                      // 0x148880
    bool DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t colour, int32_t thickness) override; // 0x147490
    bool Blt(cIGZBuffer *source, const cGZRect *sourceRect, const cGZRect *destination,
             const cGZRect *clip) override;                  // shared 0x147110: false
    uint32_t ConvertRGBValueToNative(uint8_t r, uint8_t g, uint8_t b) override;                // 0x1472f0
    bool ConvertNativeValueToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b) override;  // 0x147270
    uint32_t ConvertRGBAValueToNative(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;    // shared 0x18a5b0: 0
    bool ConvertNativeValueToRGBA(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) override; // 0x18a5a0
    void *GetColorSurfaceBits() override;                    // shared 0x6c6b0: null
    uint32_t GetColorSurfaceStride() override;               // shared 0x6c6b0: 0
    uint32_t GetChangeCounter() override;                    // shared 0x147800 (+0x34)
    cIGZUnknown *GetHardwareCache() override;                // shared 0x1457d0 (+0x30)
    bool SetHardwareCache(cIGZUnknown *cache) override;      // 0x148980
    bool IsReady() override;                                 // 0x148080

    // ---- cIGZRawBuffer (+8) ----
    bool QuerySurface(uint32_t iid, void **out) override;    // 0x1478f0
    bool Slot4() override;                                   // shared 0x144910: true

    // The device's back buffer taken (the system's OnEvent): 32-bit colour (type 9), the surface's size, the surface's
    // cISurfaceDX face (counted).
    void TakeSurface(cIGZUnknown *backBuffer);

    cISurfaceDX *mSurface;      // +0x0c: the back buffer's cISurfaceDX face (counted)
    cGZRect mBounds;            // +0x10: 0, 0, width, height
    uint32_t mTransparentColor; // +0x20
    uint32_t mFlags;            // +0x24: 2 = transparency set
    cGZColorType mColorType;    // +0x28: format 3 / 16 bits until a device's back buffer gives 9 / 32
    cIGZUnknown *mHardwareCache; // +0x30 (counted)
    uint32_t mChangeCounter;    // +0x34: Lock with kLockCounted adds one
};

// The instantiation's cells (cAbstractBehaviorTypeT<cIAbstractBehaviorType, cRefMultiThreaded> of the GZ graphics
// classes: cGZGraphicSystem and cGZCanvas share it).
struct cGZGraphicSystemBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10c9500; // 0x1471f0's cache
    static const nGZEvent::cEventAttribute &MissingAttributeRecord() { return ts2_rva_feb1c0; }
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10c9500; }
    // 0x148440 (cdecl): the registry, built on first use (id 0: nothing stored). Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute);
    static nGZEvent::cEventAttributeMap *Registry();
    static void ResetRegistryForTest();
};

class cGZGraphicSystem : public nGZEvent::cAbstractBehaviorTypeMT<cGZGraphicSystemBehavior, cGZGraphicSystemBehaviorTraits>,
                         public cIGZGraphicSystem,
                         public cIGZSystemService {
    using Behavior = nGZEvent::cAbstractBehaviorTypeMT<cGZGraphicSystemBehavior, cGZGraphicSystemBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x40;
    static constexpr uint32_t kServiceId = 0xc416025c;
    static constexpr int32_t kPriority = 0x16e35e; // service slots 5 and 12

    static constexpr uint32_t kIidBehavior2 = 0x3b253dc7;    // counted, the primary face
    static constexpr uint32_t kIidBehaviorType = 0xfb859324; // not counted

    static constexpr uint32_t kConnectionChangedEvent = 0x5b51e806;
    static constexpr uint32_t kDeviceStateEvent = 0xca7ccbbc; // cGraphicSystemDX's current device state event

    cGZGraphicSystem();
    ~cGZGraphicSystem() override; // 0x146970 (slot 15)

    // 0x1475e0 (cdecl; the COM director's creator, hooked): a new system (uncounted) as its cIGZGraphicSystem face.
    static cIGZGraphicSystem *__cdecl Create();

    // ---- cIGZUnknown (three faces) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x1483c0; +8 0x148429, +0xc 0x148431
    uint32_t AddRef() override;                              // shared 0x146a20; +8 0x146a5d, +0xc 0x146a65
    uint32_t Release() override;                             // 0x1487e0; +8 0x14886d, +0xc 0x148875

    // ---- the behaviour ----
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3) override; // 0x147ae0

    // ---- cIGZGraphicSystem (+8) ----
    uint32_t CreateBuffer(void **out) override;                 // 0x1473c0
    uint32_t CreateBufferAs(uint32_t iid, void **out) override; // 0x1473a0
    bool Slot5() override;                                      // shared 0x147480
    void Slot6(uint32_t) override;                              // shared 0x300a0
    uint32_t Slot7() override;                                  // shared 0x6c6b0
    cIGZBuffer *ActiveDrawBuffer() override;                    // 0x146a00
    cGZColorType *GetScreenColorType(cGZColorType *out) override; // 0x147910
    cGZGraphicSystemMetrics *GetScreenMetrics(cGZGraphicSystemMetrics *out) override; // 0x147930
    cGZGraphicSystemMetrics *GetZeroMetrics(cGZGraphicSystemMetrics *out) override;   // shared 0x1478d0
    bool IsFullscreen() override;                               // 0x148060
    bool Slot13() override;                                     // shared 0x147480
    cIGZUnknown *GetCanvas() override;                          // 0x1477c0
    bool Slot15(uint32_t, uint32_t) override;                   // shared 0x1477b0
    uint32_t Slot16(uint32_t value) override;                   // shared 0x147140
    bool Slot17() override;                                     // shared 0x147480
    bool BeginModal() override;                                 // 0x148270
    bool EndModal() override;                                   // 0x148b20
    bool IsWindowActive() override;                             // 0x147fe0
    int ShowMessageBox(cIGZString *text, cIGZString *caption) override; // 0x148120
    uint32_t Slot22(uint32_t) override;                         // shared 0x12e40
    uint32_t Slot23(uint32_t) override;                         // shared 0x12e40
    uint32_t Slot24(uint32_t, uint32_t) override;               // shared 0x6c6a0
    void Slot25() override;                                     // shared 0x140fe0
    bool GetScale(uint32_t *x, uint32_t *y) override;           // 0x147a80
    uint32_t Slot27() override;                                 // shared 0x6c6b0
    bool Slot28(uint32_t, uint32_t, uint32_t) override;         // shared 0x147a70
    bool Slot29(uint32_t, uint32_t) override;                   // shared 0x1477b0
    uint32_t ShowCursor(uint32_t show) override;                // shared 0x12e40
    void SetCursor(void *buffer, int32_t hotX, int32_t hotY) override; // shared 0x377e0
    bool UseSystemCursor() override;                            // shared 0x144910
    bool GetCursorPosition(int32_t *x, int32_t *y) override;    // 0x147830
    uint32_t ConvertRGBToNative(uint8_t r, uint8_t g, uint8_t b) override; // 0x147360
    bool ConvertRGBToNativeAt(uint32_t *out, uint8_t r, uint8_t g, uint8_t b) override; // 0x147370
    bool ConvertNativeToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b) override; // 0x1472e0
    void Slot37(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override; // shared 0x147130 (37-46)
    void Slot38(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override;
    void Slot39(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override;
    void Slot40(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override;
    void Slot41(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override;
    void Slot42(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override;
    void Slot43(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override;
    void Slot44(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override;
    void Slot45(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override;
    void Slot46(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override;
    void Slot47(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override; // shared 0x147120
    void Slot48(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override;
    void Slot49(uint32_t) override;                             // shared 0x300a0
    float *GetRate50(float *out) override;                      // 0x147900 (50-52)
    float *GetRate51(float *out) override;
    float *GetRate52(float *out) override;
    void SetSettings(const cGZGraphicSystemSettings *settings) override; // 0x1482e0
    void Slot54(uint32_t) override;                             // shared 0x300a0
    void SetFullscreen(bool fullscreen) override;               // shared 0x148300
    void Slot56(uint32_t) override;                             // shared 0x300a0
    void Slot57(uint32_t) override;
    uint32_t Slot58() override;                                 // shared 0x6c6b0
    uint32_t Slot59() override;
    uint32_t Slot60() override;
    void Slot61(uint32_t) override;                             // shared 0x300a0
    void Slot62(uint32_t) override;
    void Slot63(uint32_t) override;
    void Slot64(uint32_t) override;
    void Slot65() override;                                     // shared 0x140fe0
    void Slot66() override;
    void Slot67() override;
    void Slot68() override;

    // ---- cIGZSystemService (+0xc) ----
    uint32_t GetServiceID() override;               // shared 0x147a30
    void SetServiceID(uint32_t id) override;        // shared 0x1489e0
    int32_t GetServicePriority() override;          // shared 0x147a40
    bool IsServiceRunning() override;               // shared 0x1480a0
    void SetServiceRunning(bool running) override;  // shared 0x1489f0
    bool Init() override;                           // 0x147d10
    bool Shutdown() override;                       // 0x148a30
    bool OnTick(uint32_t counter) override;         // shared 0x148260
    bool OnIdle(uint32_t counter) override;         // shared 0x148260
    int32_t GetServiceTickPriority() override;      // shared 0x147a40

    // +0x00 vtable, +0x04 the count (Behavior), +0x08 / +0x0c the faces' vtables.
    uint32_t mServiceId;                    // +0x10
    bool mRunning;                          // +0x14
    uint8_t mPad15[3];                      // +0x15: never written
    cBackbufferAsGZBuffer *mBackbuffer;     // +0x18 (counted)
    cIGZDrawContextAsBuffer *mDrawBuffer;   // +0x1c (counted): made when the device first reports ready
    cGZGraphicSystemSettings mSettings;     // +0x20 (the constructor clears only the first three words)
    bool mFullscreen;                       // +0x3c
    uint8_t mPad3d[3];                      // +0x3d: never written
};

namespace gz_graphic_system_layout {
static_assert(sizeof(cBackbufferAsGZBuffer) == cBackbufferAsGZBuffer::kSize, "cBackbufferAsGZBuffer size");
static_assert(offsetof(cBackbufferAsGZBuffer, mRefCount) == 0x04 && offsetof(cBackbufferAsGZBuffer, mSurface) == 0x0c, "layout");
static_assert(offsetof(cBackbufferAsGZBuffer, mBounds) == 0x10 && offsetof(cBackbufferAsGZBuffer, mTransparentColor) == 0x20, "layout");
static_assert(offsetof(cBackbufferAsGZBuffer, mColorType) == 0x28 && offsetof(cBackbufferAsGZBuffer, mHardwareCache) == 0x30, "layout");
static_assert(offsetof(cBackbufferAsGZBuffer, mChangeCounter) == 0x34, "layout");
static_assert(sizeof(cGZGraphicSystem) == cGZGraphicSystem::kSize, "cGZGraphicSystem size");
static_assert(offsetof(cGZGraphicSystem, mRefCount) == 0x04 && offsetof(cGZGraphicSystem, mServiceId) == 0x10, "layout");
static_assert(offsetof(cGZGraphicSystem, mRunning) == 0x14 && offsetof(cGZGraphicSystem, mBackbuffer) == 0x18, "layout");
static_assert(offsetof(cGZGraphicSystem, mDrawBuffer) == 0x1c && offsetof(cGZGraphicSystem, mSettings) == 0x20, "layout");
static_assert(offsetof(cGZGraphicSystem, mFullscreen) == 0x3c, "layout");
} // namespace gz_graphic_system_layout

} // namespace nGZGraphic4
