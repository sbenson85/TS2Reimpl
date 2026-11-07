#pragma once
// nGZGraphic4::cDeviceDX (RTTI 0x10978c8; vftable 0xd9e478, 50 slots; 0x78 bytes): the game's Direct3D 9 device object
// (lane NC, rendering campaign R1; the scene bracket, slots 29 and 30, was lane LK's prototype). It owns the
// IDirect3DDevice9, the device enumeration, the resource manager and the render-state cache (gz/cDeviceStateDX.h), and
// every draw, clear and present goes through it (docs/subsystems/rendering.md).
//
// Standalone shape (docs/standalone-shape.md): the class derives from its one interface and carries our vtable. The
// game makes the device in one place, the graphic system's 0x159410 (still game code): `operator new(0x78)`, the
// constructor 0x167200, then Init 0x169cc0. Both are hooked (cDeviceDXAdapters); every other member is reached through
// our vtable or from our own members, so the slots are in effect through callers (trap
// Hooks.nGZGraphic4_cDeviceDX_Construct=1).
//
// RTTI: cAbstractBehaviorTypeT<cIDeviceDX, nGZEvent::cRefMultiThreaded> (the event behaviour template: slots 0-14, the
// count at +4 with the "active" top bit, atomic), then cIDeviceDX / cIDevice (slots 15-48), all at +0. The template's own
// vtable 0xd9e3ac is stored only by the original constructor and destructor.
//
// The device enumeration is ours (gz/cDeviceEnumDX.h, lane NK): Init builds it, RefreshDeviceEnum and DumpInfo call it.
// So is the resource manager (gz/cDeviceResourceManagerDX.h, lane NT): Init builds it, CreateDevice and ReleaseDevice call
// its members.
// The mode and format searches 0x18b490 / 0x18ab60 are ours (gz/GraphicModeSearch.h, lane RJ; the multisample clamp
// 0x18b7d0 too: ClampMultisample, lane RB), so are the state's dump 0x17da90 (cDeviceStateDX::DumpInfo, lane RJ; its
// set-up 0x1807b0: lane PO) and the event registry 0x16a170 (EventRegistryFor, cDeviceDXEvents.cpp, lane RJ). The pixel
// formats getter 0x18acd0 is GetPixelFormatsManagerCached (gz/GraphicServices.h). CreateDevice (0x167ec0) is ours (lane
// NF), reached only from our members.
#include <cstddef>
#include <cstdint>

#include "gz/cDeviceEnum.h"       // cDisplayMode, cIPixelFormat
#include "gz/cGZEventReflector.h" // nGZEvent::cIAbstractBehavior (the behaviour slots 3-14), cEventAttribute
#include "gz/cIGZUnknown.h"

struct IDirect3DDevice9;
struct IDirect3DQuery9;
struct IDirect3DSurface9;
struct IDirect3DTexture9;

#include "game/owned.h"
TS2_OWNED_STATIC(const char, d9e540)[]; // the source file of the class's reports (kSourceFileRva)
TS2_OWNED_STATIC(nGZGraphic4::cDisplayMode, 10ca208);      // the process default display mode (DefaultMode)
TS2_OWNED_STATIC(uint32_t, 10ca21c);                        // WaitForGpu's clear colour
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10ca22c); // GetEventManager's cache
TS2_OWNED_STATIC(const nGZEvent::cEventAttribute, fecaf8);  // the record slot 14 hands out for an unknown event
TS2_OWNED_STATIC(const char, d9e598)[];                     // DumpInfo's banner: a rule
TS2_OWNED_STATIC(const char, d9e5f0)[];                     // its title line
TS2_OWNED_STATIC(const char, d9e644)[];                     // its closing newline

namespace nGZGraphic4 {

class cDeviceStateDX;
class cDeviceResourceManagerDX;
struct cDeviceRequest; // CreateDevice's choices (cDeviceDX.cpp)

// What slots 23 and 33 take and give (+0x08..+0x33 of the device, 11 dwords). CreateDevice (0x167ec0) compares the
// display mode with the default mode (0x10ca208, also the constructor's): the default means windowed presentation.
struct cDeviceSettings {
    cDisplayMode mMode;           // +0x00 (device +0x08): the wanted display mode
    cIPixelFormat *mColorFormat;  // +0x14 (device +0x1c): the back buffer format (null: the deepest usable)
    cIPixelFormat *mDepthFormat;  // +0x18 (device +0x20): the depth format (null: the first usable)
    uint32_t mBackBufferCount;    // +0x1c (device +0x24; the constructor's 1, CreateDevice clamps it to 3)
    uint32_t mMultisampleType;    // +0x20
    uint32_t mMultisampleQuality; // +0x24
    uint32_t mFlags;              // +0x28 (device +0x30): cDeviceDX::kFlag*
};
static_assert(sizeof(cDeviceSettings) == 0x2c, "cDeviceSettings size");

// Slot 41's answer: the size of the render target the state cache last set.
struct cDeviceTargetSize {
    uint32_t mWidth;
    uint32_t mHeight;
};

// The device's events (nGZEvent::cIEventHeader bodies, 8 bytes: vtable, code). NotifyListeners hands listeners a pointer
// to one on the stack; listeners only read it (or copy it through slot 6). Slot 0 is the deleting destructor; slots 1
// and 2 copy the code (the original returns the copied code in eax: no caller uses it).
class cDeviceEventBody {
public:
    explicit cDeviceEventBody(uint32_t code) : mCode(code) {}
    virtual ~cDeviceEventBody() = default;                         // 0 (shared 0x154ce0)
    virtual uint32_t Assign(const cDeviceEventBody *other);        // 1 (shared 0x155080)
    virtual uint32_t AssignAlias(const cDeviceEventBody *other);   // 2 (the same body)
    virtual uint32_t GetEventType() const = 0;                     // 3
    virtual uint32_t GetSize() const;                              // 4 (shared 0x1559c0: 8)
    virtual uint32_t GetBodySize() const;                          // 5 (shared 0x155a00: 4)
    virtual void CloneInto(void *memory) const = 0;                // 6: a copy built in `memory`

    uint32_t mCode; // +4
};
static_assert(sizeof(cDeviceEventBody) == 8, "cDeviceEventBody size");

// nGZGraphic4::cDeviceEvent (game vtable 0xd9e36c): the device's state, sent as event 0x9b6c3e15. Codes: 0 about to be
// made again, then 1 created and 2 reset (CreateDevice, the two in a row), 3 about to be released (ReleaseDevice), 4 lost
// (OnDeviceLost).
class cDeviceEvent final : public cDeviceEventBody {
public:
    static constexpr uint32_t kEventId = 0x9b6c3e15;
    static constexpr uint32_t kType = 0x3b6c3e0a; // slot 3 (0x16a7e0)
    enum : uint32_t { kRecreating = 0, kCreated = 1, kReset = 2, kReleasing = 3, kLost = 4 };
    using cDeviceEventBody::cDeviceEventBody;
    uint32_t GetEventType() const override;    // 0x16a7e0
    void CloneInto(void *memory) const override; // 0x167e80
};

// nGZGraphic4::cDeviceRenderSceneEvent (game vtable 0xd9e38c): one frame of the canvas, sent as event 0xeb573db7 with
// code 0 before BeginScene, 1 inside the scene and 2 after EndScene.
class cDeviceRenderSceneEvent final : public cDeviceEventBody {
public:
    static constexpr uint32_t kEventId = 0xeb573db7;
    static constexpr uint32_t kType = 0xcb573da1; // slot 3 (0x16a7f0)
    enum : uint32_t { kBeforeScene = 0, kInScene = 1, kAfterScene = 2 };
    using cDeviceEventBody::cDeviceEventBody;
    uint32_t GetEventType() const override;    // 0x16a7f0
    void CloneInto(void *memory) const override; // 0x167ea0
};

// nGZGraphic4::cIDeviceDX: the behaviour slots (0-14, nGZEvent::cIAbstractBehavior) and the device's (15-48); the virtual
// destructor is slot 49. Slot names past the behaviour's are ours (the RTTI has none); what each does is in the class.
class cIDeviceDX : public nGZEvent::cIAbstractBehavior {
public:
    virtual cIGZUnknown *GetDeviceEnum() = 0;                                   // 15
    virtual cIGZUnknown *GetDeviceEnumConst() = 0;                              // 16
    virtual cDeviceResourceManagerDX *GetResourceManagerConst() = 0;            // 17
    virtual cDeviceResourceManagerDX *GetResourceManager() = 0;                 // 18
    virtual cDeviceStateDX *GetState() = 0;                                     // 19
    virtual cDeviceStateDX *GetStateConst() = 0;                                // 20
    virtual bool SetFlags(uint32_t flags) = 0;                                  // 21
    virtual void SetCreationBlocked(bool blocked) = 0;                          // 22
    virtual bool ApplySettings(const cDeviceSettings *settings) = 0;            // 23
    virtual void OnDeviceLost() = 0;                                            // 24
    virtual void ReleaseDevice() = 0;                                           // 25
    virtual void ProcessVertices(uint32_t srcStartIndex, uint32_t destIndex, uint32_t vertexCount,
                                 cIGZUnknown *destBuffer, cIGZUnknown *declaration) = 0; // 26
    virtual void Clear(uint32_t flags, const float *color, float z, uint32_t stencil) = 0; // 27
    virtual void ClearTargets(const void *targets, const int32_t *rect, uint32_t flags, const float *color, float z,
                              uint32_t stencil) = 0;                            // 28
    virtual void BeginScene() = 0;                                              // 29
    virtual void EndScene() = 0;                                                // 30
    virtual void DrawPrimitive(uint32_t type, uint32_t primitiveCount, uint32_t startVertex) = 0; // 31
    virtual void DrawIndexedPrimitive(uint32_t type, uint32_t primitiveCount, int32_t baseVertex, uint32_t minIndex,
                                      uint32_t vertexCount, uint32_t startIndex) = 0; // 32
    virtual cDeviceSettings GetSettings() = 0;                                  // 33
    virtual bool HasDeviceOrLost() = 0;                                         // 34
    virtual bool IsDeviceReady() = 0;                                           // 35
    virtual bool InScene() = 0;                                                 // 36
    virtual bool IsFullscreen() = 0;                                            // 37
    virtual uint32_t GetSwapChainCount() = 0;                                   // 38
    virtual cIGZUnknown *GetBackBuffer(uint32_t swapChain) = 0;                 // 39
    virtual cIGZUnknown *GetTargetProviderSurface() = 0;                        // 40
    virtual cDeviceTargetSize GetTargetSize() = 0;                              // 41
    virtual cIGZUnknown *GetDisplayMonitor() = 0;                               // 42
    virtual void DumpInfo(cIGZUnknown *stream) = 0;                             // 43
    virtual cIGZUnknown *GetDeviceEnum44() = 0;                                 // 44
    virtual cIGZUnknown *GetDeviceEnum45() = 0;                                 // 45
    virtual void WaitForGpu() = 0;                                              // 46
    virtual cIGZUnknown *GetDisplayMonitorConst() = 0;                          // 47
    virtual IDirect3DDevice9 *GetD3DDevice() = 0;                               // 48
    virtual ~cIDeviceDX() = default;                                            // 49: the deleting destructor
};

class cDeviceDX : public cIDeviceDX {
public:
    static constexpr size_t kSize = 0x78;
    static constexpr uint32_t kSourceFileRva = 0xd9e540; // LogD3DError's source file for every report in this class

    // Interface ids slot 0 answers.
    static constexpr uint32_t kIidDeviceDX = 0x553b7cf;     // counted
    static constexpr uint32_t kIidDevice = 0xe553b633;      // counted
    static constexpr uint32_t kIidBehavior2 = 0x3b253dc7;   // counted (every behaviour type answers it)
    static constexpr uint32_t kIidD3DDevice = 0x4b536705;   // the IDirect3DDevice9 itself, not counted
    // Events slot 3 handles.
    static constexpr uint32_t kCanvasEvent = 0x1b637474;    // a canvas message: kinds 3 (paint) and 7
    static constexpr uint32_t kResizeEvent = 0x3b6decd1;    // a window resize: its client rect

    // 0x16a170 (cdecl; lane RJ): the behaviour template's event registry, built on first use, with `attribute` stored
    // under `id` when id is not 0 (replacing the record there). First use: the base events registered (the shared
    // 0x1464f0), a copy of the base registry (0x1480d0), then the device, render-scene, canvas and resize events (the
    // image's records 0xfecb10 / 0xfecb28 / 0xfecb88 / 0xfecba0; the last word 8, 8, 4, 4). Our own map: the original's
    // game global 0x10ca220 is read by nothing else, and its unused 6-byte allocation (0x10ca234) is not made.
    static nGZEvent::cEventAttributeMap *__cdecl EventRegistryFor(uint32_t id, const nGZEvent::cEventAttribute *attribute);
    // Tests only: forgets the registry, so the next use builds it again.
    static void ResetEventRegistryForTest();
    // Settings flags (+0x30).
    static constexpr uint32_t kFlagAskImmediate = 0x01;     // immediate presentation only with feature 3, else every
                                                            // vblank (without the flag: always immediate)
    static constexpr uint32_t kFlagMixedProcessing = 0x04;  // mixed vertex processing even when hardware would do
    static constexpr uint32_t kFlagWaitForGpu = 0x20;       // wait for the GPU after every Present
    static constexpr uint32_t kFlagCopySwap = 0x40;         // D3DSWAPEFFECT_COPY, one back buffer, no multisampling
    static constexpr uint32_t kFlagNoDeviceClip = 0x80;     // no D3DPRESENTFLAG_DEVICECLIP

    cDeviceDX();                    // 0x167200 (hook: cDeviceDXAdapters::Construct)
    ~cDeviceDX() override;          // 0x167340; the deleting destructor 0x167490 is slot 49

    // The process default display mode (0x10ca208): windowed presentation.
    static const cDisplayMode &DefaultMode();
    static bool IsDefaultMode(const cDisplayMode &mode);

    // 0x169cc0 (ret 4; hook: cDeviceDXAdapters::Init): binds the device to an adapter. The display monitor of the
    // adapter (a cIDisplayMonitor, COM class 0x7b64ce01), then a new device enumeration, resource manager and state
    // cache, each counted into its field. False when a cD3DException is thrown on the way.
    bool Init(uint32_t adapter);

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0x16a110
    uint32_t AddRef() override;                              // shared 0x146a20
    uint32_t Release() override;                             // 0x16a3b0

    // nGZEvent::cIAbstractBehavior
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3) override; // 0x169bf0
    bool OnQuery(uint32_t type, uint32_t arg1, void *data) override;                // shared 0x147ce0: slot 6
    void NotifyListeners(uint32_t type, uint32_t arg1, void *data) override;        // 0x16a430
    bool AskListeners(uint32_t type, uint32_t arg1, void *data) override;           // 0x169890
    void SetAttribute(uint32_t, uint32_t, uint32_t) override {}                            // shared 0x377e0
    void Connect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent) override;    // 0x167d60
    void Disconnect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent) override; // 0x1694d0
    void GetAttribute(uint32_t, uint32_t, uint32_t) override {}                           // shared 0x377e0
    uint32_t IsActive() override;                                                   // shared 0x147cf0
    uint32_t GetEventCount() override;                                              // 0x169f10
    nGZEvent::cAttributeIDRange GetEventIds() override;                             // 0x1674e0
    nGZEvent::cEventAttribute GetEventAttribute(uint32_t event) override;           // 0x167750

    // cIDeviceDX
    cIGZUnknown *GetDeviceEnum() override;                        // shared 0x1674b0
    cIGZUnknown *GetDeviceEnumConst() override;                   // shared 0x1674b0
    cDeviceResourceManagerDX *GetResourceManagerConst() override; // shared 0x1674c0
    cDeviceResourceManagerDX *GetResourceManager() override;      // shared 0x1674c0
    cDeviceStateDX *GetState() override;                          // shared 0x1674d0
    cDeviceStateDX *GetStateConst() override;                     // shared 0x1674d0
    bool SetFlags(uint32_t flags) override;                       // 0x167ce0
    void SetCreationBlocked(bool blocked) override;               // 0x16a7d0
    bool ApplySettings(const cDeviceSettings *settings) override; // 0x16a520
    void OnDeviceLost() override;                                 // 0x169690
    void ReleaseDevice() override;                                // 0x16a5c0
    void ProcessVertices(uint32_t srcStartIndex, uint32_t destIndex, uint32_t vertexCount, cIGZUnknown *destBuffer,
                         cIGZUnknown *declaration) override;      // 0x16a040
    void Clear(uint32_t flags, const float *color, float z, uint32_t stencil) override; // 0x167890
    void ClearTargets(const void *targets, const int32_t *rect, uint32_t flags, const float *color, float z,
                      uint32_t stencil) override;                 // 0x167a60
    void BeginScene() override;                                   // 0x167850
    void EndScene() override;                                     // 0x169660
    void DrawPrimitive(uint32_t type, uint32_t primitiveCount, uint32_t startVertex) override; // 0x169570
    void DrawIndexedPrimitive(uint32_t type, uint32_t primitiveCount, int32_t baseVertex, uint32_t minIndex,
                              uint32_t vertexCount, uint32_t startIndex) override;            // 0x1695e0
    cDeviceSettings GetSettings() override;                       // 0x16a590
    bool HasDeviceOrLost() override;                              // 0x16a800
    bool IsDeviceReady() override;                                // 0x169ef0
    bool InScene() override;                                      // 0x169ed0
    bool IsFullscreen() override;                                 // 0x169eb0
    uint32_t GetSwapChainCount() override;                        // shared 0x169f60
    cIGZUnknown *GetBackBuffer(uint32_t swapChain) override;      // 0x167830
    cIGZUnknown *GetTargetProviderSurface() override;             // 0x167840
    cDeviceTargetSize GetTargetSize() override;                   // 0x16a410
    cIGZUnknown *GetDisplayMonitor() override;                    // shared 0x145520
    void DumpInfo(cIGZUnknown *stream) override;                  // 0x169f70
    cIGZUnknown *GetDeviceEnum44() override;                      // shared 0x1674b0
    cIGZUnknown *GetDeviceEnum45() override;                      // shared 0x1674b0
    void WaitForGpu() override;                                   // 0x169740
    cIGZUnknown *GetDisplayMonitorConst() override;               // shared 0x145520
    IDirect3DDevice9 *GetD3DDevice() override;                    // shared 0x1694c0

    // 0x167e00: the connection manager, cached at 0x10ca22c once the event system service gave one.
    static nGZEvent::cIConnectionManager *GetEventManager();

    // 0x1699b0 (ret 4; slot 3's canvas message): one frame. Kinds 3 (paint) and 7 only, and not after a release. A lost
    // device is recreated first (else reported lost again, and the thread sleeps 100 ms). Then the cooperative level:
    // for a paint while it is fine, the scene events around BeginScene / EndScene, Present and the GPU wait (a
    // cD3DException from any of them ends the scene and gives its HRESULT). D3DERR_DRIVERINTERNALERROR recreates the
    // device or throws nGZGraphic4::cError(0); DEVICELOST reports the loss and sleeps; DEVICENOTRESET recreates.
    void HandleCanvasEvent(const uint32_t *message);

    // 0x167ec0: (re)makes the Direct3D device from the settings; true when it is usable afterwards (also while
    // creation is blocked). The display mode (the default: windowed), then the colour and depth formats are chosen
    // against the enumeration; listeners hear code 0, the device's objects are released, then the present parameters
    // and behaviour flags are built and the device Reset (an existing one) or created, dropping multisampling and then
    // extra back buffers on failure (a failed creation may fall back to the desktop mode). A device that came up gets
    // its swap chains, target provider, GPU query or sync surface, probe texture and state, and is cleared and
    // presented once per swap chain when new or fullscreen; listeners then hear codes 1 and 2. Throws
    // nGZGraphic4::cError(0) when validating or presenting gives D3DERR_DRIVERINTERNALERROR.
    bool CreateDevice();

    uint32_t mRefCount;                                  // +0x04: count (low 31 bits) and the behaviour's active bit
    cDeviceSettings mSettings;                           // +0x08
    uint32_t mAdapter;                                   // +0x34
    IDirect3DDevice9 *mDevice;                           // +0x38 (COM reference)
    cIGZUnknown *mDeviceEnum;                            // +0x3c: cDeviceEnumDX (counted)
    cDeviceResourceManagerDX *mResourceManager;          // +0x40 (counted)
    cDeviceStateDX *mState;                              // +0x44 (counted)
    cIGZUnknown *mDisplayMonitor;                        // +0x48: cIDisplayMonitor of the adapter (counted)
    uint32_t mSwapChainCount;                            // +0x4c
    cIGZUnknown *mSwapChains[3];                         // +0x50: slot 4 of each gives its back buffer (counted)
    cIGZUnknown *mTargetProvider;                        // +0x5c: COM class 0x7b75b73a object CreateDevice makes (counted)
    IDirect3DQuery9 *mGpuQuery;                          // +0x60: an event query WaitForGpu issues (COM)
    IDirect3DSurface9 *mSyncSurface;                     // +0x64: WaitForGpu's 16x16 target when there is no query (COM)
    IDirect3DTexture9 *mProbeTexture;                    // +0x68: an 8x8 DXT1 texture CreateDevice makes (COM)
    int32_t mSceneDepth;                                 // +0x6c: open BeginScene calls
    bool mFullscreen;                                    // +0x70
    bool mDeviceLost;                                    // +0x71
    bool mDeviceFailed;                                  // +0x72: CreateDevice gave up
    bool mReleased;                                      // +0x73: set while ReleaseDevice runs, cleared at its end
    bool mByte74;                                        // +0x74: only ever cleared (HandleCanvasEvent throws when set)
    bool mCreationBlocked;                               // +0x75: slot 22; CreateDevice does nothing while set
    uint8_t mPad76[2];                                   // +0x76: never written

private:
    // LogD3DError(hr, this class's source file, line).
    static void ReportFailure(long hr, uint32_t line);
    // HandleCanvasEvent's paint (inline there in the original): the render-scene events around BeginScene and
    // EndScene, Present, and the GPU wait when kFlagWaitForGpu is set. Present's HRESULT, or the HRESULT of a
    // cD3DException thrown on the way (an open scene is then ended, its result ignored).
    int32_t PresentFrame();

    // CreateDevice's steps (inline there in the original).
    bool ChooseDisplayMode(cDeviceRequest &request);
    bool ChooseFormats(cDeviceRequest &request);
    void ReleaseForRecreate(const cDeviceRequest &request);
    void BuildPresentParameters(cDeviceRequest &request);
    long ResetD3DDevice(cDeviceRequest &request);
    long CreateD3DDevice(cDeviceRequest &request);
    void TakeNewDevice(const cDeviceRequest &request, IDirect3DDevice9 *before);
    // The query, sync surface, probe texture, swap chains and target provider released (ReleaseDevice and
    // CreateDevice, in this order).
    void ReleaseFrameObjects();
};

// 0x18b7d0 (cdecl; lane RB): fits the multisampling the settings ask for to what the enumeration offers for these formats.
// Type 1 (non-maskable) keeps its quality within that type's levels (none: no multisampling). Any other type, capped at
// 16, steps down (skipping 1) to the first the enumeration has levels for, its quality kept within them; reaching 0
// without levels leaves type 0 and the quality as it was. Hooked at its RVA; BuildPresentParameters calls it
// (not inlined there: the device tests stand in for it).
__declspec(noinline) void __cdecl ClampMultisample(uint32_t *type, uint32_t *quality, cIDeviceEnum *devices,
                                                  cIPixelFormat *displayFormat, cIPixelFormat *color);

// The constructor and Init as the game calls them (0x159410).
struct cDeviceDXAdapters {
    static cDeviceDX *__fastcall Construct(void *memory, void *);                 // 0x167200
    static bool __fastcall Init(cDeviceDX *device, void *, uint32_t adapter);      // 0x169cc0
};

namespace device_layout {
static_assert(sizeof(cDeviceDX) == cDeviceDX::kSize, "cDeviceDX size");
static_assert(offsetof(cDeviceDX, mRefCount) == 0x04 && offsetof(cDeviceDX, mSettings) == 0x08, "layout");
static_assert(offsetof(cDeviceDX, mAdapter) == 0x34 && offsetof(cDeviceDX, mDevice) == 0x38, "layout");
static_assert(offsetof(cDeviceDX, mResourceManager) == 0x40 && offsetof(cDeviceDX, mState) == 0x44, "layout");
static_assert(offsetof(cDeviceDX, mSwapChainCount) == 0x4c && offsetof(cDeviceDX, mSwapChains) == 0x50, "layout");
static_assert(offsetof(cDeviceDX, mTargetProvider) == 0x5c && offsetof(cDeviceDX, mGpuQuery) == 0x60, "layout");
static_assert(offsetof(cDeviceDX, mProbeTexture) == 0x68 && offsetof(cDeviceDX, mSceneDepth) == 0x6c, "layout");
static_assert(offsetof(cDeviceDX, mFullscreen) == 0x70 && offsetof(cDeviceDX, mReleased) == 0x73, "layout");
static_assert(offsetof(cDeviceDX, mCreationBlocked) == 0x75, "layout");
} // namespace device_layout

} // namespace nGZGraphic4
