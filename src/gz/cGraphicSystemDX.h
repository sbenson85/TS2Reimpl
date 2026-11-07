#pragma once
// nGZGraphic4::cGraphicSystemDX (RTTI 0x1096298; vftables 0xd9ce44, 35 slots, and 0xd9ced4 at +8, 13 slots; 0x458
// bytes): the Direct3D 9 graphic system (lane NQ, rendering campaign R2b). It owns the IDirect3D9, makes one cDeviceDX
// per adapter (at most six) and keeps the current one, follows the window (canvas) between monitors and adapters, and
// hands out the graphics managers (pixel formats, vertex formats and declarations, shader programs, the image manip),
// a 256-entry palette and a do-nothing Direct3D device (cNullDevice, below). It is the framework's service 0x85522803
// (cIGZSystemService at +8): Init builds the devices and throws nGZGraphic4::cError(1) without Direct3D or (2) without a
// usable device; Shutdown releases them.
//
// Standalone shape (docs/standalone-shape.md): the class derives from cAbstractBehaviorTypeT<cIGraphicSystemDX,
// cRefMultiThreaded> (gz/cAbstractBehaviorTypeMT.h) and cIGZSystemService, in RTTI order. The creator 0x159940 (the
// graphics COM director's class 0x855227cd, rz/COMDirectorData.cpp) is hooked to Create and is the switch: it is the
// only code that stores the vtables (the deleting destructor 0x158f40, slot 34, the other), so every slot, the
// devices' build and teardown 0x159410 / 0x15a590 and the canvas handler 0x159a40 are in effect through callers.
//
// Events: listens to the window's resize (0x3b6decd1) and canvas (0x1b637474) events and to each device's state
// (cDeviceEvent, 0x9b6c3e15) and frame (cDeviceRenderSceneEvent, 0xeb573db7) events; tells its own listeners
// 0xca7ccbbc (the current device's state event passed on), 0x2b5745f8 (its frame event passed on) and 0x1bf6e8a1
// (the devices were built: a cNullEvent).
#include "gz/cAbstractBehaviorTypeMT.h"
#include "gz/cDeviceDX.h" // cDeviceSettings
#include "gz/cIGZSystemService.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct IDirect3D9;
struct IDirect3DDevice9;

#include "game/owned.h"
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10c9774); // GetEventManager's cache
TS2_OWNED_STATIC(const nGZEvent::cEventAttribute, febd08); // slot 14's record for an unknown event

namespace nGZGraphic4 {

// One palette entry (a PALETTEENTRY: red, green, blue, flags).
struct cPaletteEntry {
    uint8_t mRed, mGreen, mBlue, mFlags;
};

// nGZGraphic4::cIGraphicSystemDX: the behaviour slots (0-14) and the system's (15-33); the virtual destructor is slot
// 34. Slot names are ours (the RTTI has none).
class cIGraphicSystemDX : public nGZEvent::cIAbstractBehavior {
public:
    virtual void SetCurrentDevice(int32_t index) = 0;            // 15
    virtual void *GetPixelFormatsManager() = 0;                  // 16
    virtual void *GetVertexFormatsManager() = 0;                 // 17
    virtual void *GetVertexDeclarationsManager() = 0;            // 18
    virtual void *GetShaderProgramsManager() = 0;                // 19
    virtual void *GetImageManip() = 0;                           // 20
    virtual int32_t GetCurrentDevice() = 0;                      // 21
    virtual int32_t FindWindowDevice() = 0;                      // 22: the device on the window's monitor, or -1
    virtual uint32_t GetDeviceCount() = 0;                       // 23
    virtual cDeviceDX *GetDevice(uint32_t index) = 0;            // 24 (unchecked)
    virtual IDirect3D9 *GetDirect3D(uint32_t kind) = 0;          // 25
    virtual bool HasDirect3D(uint32_t kind) = 0;                 // 26
    virtual void *GetVertexDeclarationsManager27() = 0;          // 27 (the same as 18)
    virtual cPaletteEntry *GetPalette() = 0;                     // 28
    virtual IDirect3DDevice9 *GetNullDevice() = 0;               // 29
    virtual const char *GetErrorString(long hr) = 0;             // 30: DXGetErrorString9A
    virtual long CreateMeshFVF(uint32_t faces, uint32_t vertices, uint32_t options, uint32_t fvf,
                               IDirect3DDevice9 *device, void **mesh) = 0;                 // 31: D3DXCreateMeshFVF
    virtual long OptimizeFaces(const void *indices, uint32_t faces, uint32_t vertices, int32_t indices32,
                               uint32_t *remap) = 0;                                       // 32: D3DXOptimizeFaces
    virtual long OptimizeVertices(const void *indices, uint32_t faces, uint32_t vertices, int32_t indices32,
                                  uint32_t *remap) = 0;                                    // 33: D3DXOptimizeVertices
    virtual ~cIGraphicSystemDX() = default;                      // 34: the deleting destructor
};

// The instantiation's cells (cAbstractBehaviorTypeT<cIGraphicSystemDX, cRefMultiThreaded>).
struct cGraphicSystemDXBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10c9774; // 0x159390's cache
    static const nGZEvent::cEventAttribute &MissingAttributeRecord() { return ts2_rva_febd08; }
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10c9774; }
    // 0x15a250 (cdecl): the registry, built on first use (it registers the system's and the canvas's and devices'
    // events; id 0: nothing stored). Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute);
    static nGZEvent::cEventAttributeMap *Registry();
    static void ResetRegistryForTest();
};

class cGraphicSystemDX : public nGZEvent::cAbstractBehaviorTypeMT<cIGraphicSystemDX, cGraphicSystemDXBehaviorTraits>,
                         public cIGZSystemService {
    using Behavior = nGZEvent::cAbstractBehaviorTypeMT<cIGraphicSystemDX, cGraphicSystemDXBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x458;
    static constexpr uint32_t kServiceId = 0x85522803;
    static constexpr int32_t kPriority = 0x16e360; // slots 5 and 12 of the service face
    static constexpr uint32_t kMaxDevices = 6;

    // Interface ids slot 0 answers.
    static constexpr uint32_t kIidGraphicSystemDX = 0xa55227bb; // counted
    static constexpr uint32_t kIidGraphicSystem = 0xc55289ba;   // counted
    static constexpr uint32_t kIidBehavior2 = 0x3b253dc7;       // counted
    static constexpr uint32_t kIidBehaviorType = 0xfb859324;    // not counted

    // Events.
    static constexpr uint32_t kResizeEvent = 0x3b6decd1;        // the window's (its client rect, flags at +0x14)
    static constexpr uint32_t kCanvasEvent = 0x1b637474;        // the window's canvas message
    static constexpr uint32_t kDeviceStateEvent = 0x9b6c3e15;   // cDeviceEvent::kEventId
    static constexpr uint32_t kDeviceFrameEvent = 0xeb573db7;   // cDeviceRenderSceneEvent::kEventId
    static constexpr uint32_t kCurrentDeviceStateEvent = 0xca7ccbbc;
    static constexpr uint32_t kCurrentDeviceFrameEvent = 0x2b5745f8;
    static constexpr uint32_t kDevicesBuiltEvent = 0x1bf6e8a1;

    cGraphicSystemDX();
    ~cGraphicSystemDX() override; // 0x158f40 (the deleting destructor, slot 34)

    // 0x159940 (cdecl; the COM director's creator, hooked): a new system, uncounted (null when new failed).
    static cGraphicSystemDX *__cdecl Create();

    // ---- cIGZUnknown (both faces) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x15a1d0; +8 thunk 0x15a23a
    uint32_t AddRef() override;                              // shared 0x146a20; +8 thunk shared 0x146a5d
    uint32_t Release() override;                             // 0x15a530; +8 thunk 0x15a586

    // ---- the behaviour ----
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3) override; // 0x159d90

    // ---- cIGraphicSystemDX ----
    void SetCurrentDevice(int32_t index) override;  // 0x15a7e0
    void *GetPixelFormatsManager() override;        // 0x15a1c0: jmp 0x15eb10
    void *GetVertexFormatsManager() override;       // 0x15a8f0: jmp 0x16db40
    void *GetVertexDeclarationsManager() override;  // 0x15a8e0: jmp 0x16e760
    void *GetShaderProgramsManager() override;      // 0x15a7f0: jmp 0x1758c0
    void *GetImageManip() override;                 // 0x159f80: jmp 0x16c0c0
    int32_t GetCurrentDevice() override;            // 0x159660
    int32_t FindWindowDevice() override;            // 0x159680
    uint32_t GetDeviceCount() override;             // 0x15a1b0
    cDeviceDX *GetDevice(uint32_t index) override;  // 0x159670
    IDirect3D9 *GetDirect3D(uint32_t kind) override; // 0x159750
    bool HasDirect3D(uint32_t kind) override;       // 0x15a090
    void *GetVertexDeclarationsManager27() override; // 0x15a8e0
    cPaletteEntry *GetPalette() override;           // shared 0x158f90
    IDirect3DDevice9 *GetNullDevice() override;     // 0x15a0d0
    const char *GetErrorString(long hr) override;   // 0x15a8d0
    long CreateMeshFVF(uint32_t faces, uint32_t vertices, uint32_t options, uint32_t fvf, IDirect3DDevice9 *device,
                       void **mesh) override;       // 0x15a8a0
    long OptimizeFaces(const void *indices, uint32_t faces, uint32_t vertices, int32_t indices32,
                       uint32_t *remap) override;   // 0x15a8b0
    long OptimizeVertices(const void *indices, uint32_t faces, uint32_t vertices, int32_t indices32,
                          uint32_t *remap) override; // 0x15a8c0

    // ---- cIGZSystemService (+8) ----
    uint32_t GetServiceID() override;               // shared 0x147a30
    void SetServiceID(uint32_t id) override;        // shared 0x1489e0
    int32_t GetServicePriority() override;          // 0x159a30
    bool IsServiceRunning() override;               // shared 0x1480a0
    void SetServiceRunning(bool running) override;  // shared 0x1489f0
    bool Init() override;                           // 0x159f90
    bool Shutdown() override;                       // 0x15a800
    bool OnTick(uint32_t counter) override;         // shared 0x148260
    bool OnIdle(uint32_t counter) override;         // shared 0x148260
    int32_t GetServiceTickPriority() override;      // 0x159a30

    // 0x159410: (re)makes Direct3D and one device per adapter with hardware caps (at most six); each built
    // device is connected both ways (the window's canvas and resize events to it, its state and frame events to this),
    // the current device becomes the window's (or the first), and listeners hear kDevicesBuiltEvent.
    void BuildDevices();
    // 0x15a590: every device released and disconnected, the list emptied, no current device, Direct3D released.
    void TearDown();
    // 0x159a40 (ret 4): a canvas message of kind 8 (the display changed): the adapters are checked against the
    // devices (count and identifiers; any change rebuilds everything), the monitors brought up to date, and saved
    // settings put back on the current device.
    void HandleCanvasEvent(const uint32_t *message);
    // 0x169480 (a cDeviceDX member in the game, called only from 0x159a40): the device released, given `settings`,
    // and made again through OnDeviceLost (which recreates it).
    static void RestoreDeviceSettings(cDeviceDX *device, const cDeviceSettings &settings);

    // A counted device in the list (the original's std::vector<cRZAutoRefCount<cDeviceDX>>): copies count.
    class DeviceRef {
    public:
        explicit DeviceRef(cDeviceDX *device) : mDevice(device)
        {
            if (mDevice) mDevice->AddRef();
        }
        DeviceRef(const DeviceRef &other) : DeviceRef(other.mDevice) {}
        DeviceRef &operator=(const DeviceRef &) = delete;
        ~DeviceRef()
        {
            if (mDevice) mDevice->Release();
        }
        cDeviceDX *Get() const { return mDevice; }
        cDeviceDX *operator->() const { return mDevice; }

    private:
        cDeviceDX *mDevice;
    };

    // The Direct3D object (a COM holder: released and cleared when the system goes).
    struct Direct3DRef {
        IDirect3D9 *mObject = nullptr;
        Direct3DRef() = default;
        Direct3DRef(const Direct3DRef &) = delete;
        Direct3DRef &operator=(const Direct3DRef &) = delete;
        ~Direct3DRef() { Reset(); }
        void Reset();
    };

    // +0x00 vtable, +0x04 the count (Behavior), +0x08 the service face's vtable.
    uint32_t mServiceId;                  // +0x0c
    bool mRunning;                        // +0x10
    bool mHasReferenceDevice;             // +0x11: adapter 0 has reference-rasterizer caps (HasDirect3D(2))
    uint8_t mPad12[2];                    // +0x12: never written
    Direct3DRef mDirect3D;                // +0x14
    cPaletteEntry mPalette[256];          // +0x18: all black, flags = the index
    std::vector<DeviceRef> mDevices;      // +0x418
    bool mRestorePending;                 // +0x424: mSavedSettings waits to go back on the current device
    uint8_t mPad425[3];                   // +0x425: never written
    cDeviceSettings mSavedSettings;       // +0x428 (the constructor clears only its display mode)
    int32_t mCurrentDevice;               // +0x454 (-1: none)
};

namespace graphic_system_layout {
static_assert(sizeof(cGraphicSystemDX) == cGraphicSystemDX::kSize, "cGraphicSystemDX size");
static_assert(offsetof(cGraphicSystemDX, mRefCount) == 0x04 && offsetof(cGraphicSystemDX, mServiceId) == 0x0c, "layout");
static_assert(offsetof(cGraphicSystemDX, mRunning) == 0x10 && offsetof(cGraphicSystemDX, mHasReferenceDevice) == 0x11, "layout");
static_assert(offsetof(cGraphicSystemDX, mDirect3D) == 0x14 && offsetof(cGraphicSystemDX, mPalette) == 0x18, "layout");
static_assert(offsetof(cGraphicSystemDX, mDevices) == 0x418 && offsetof(cGraphicSystemDX, mRestorePending) == 0x424, "layout");
static_assert(offsetof(cGraphicSystemDX, mSavedSettings) == 0x428 && offsetof(cGraphicSystemDX, mCurrentDevice) == 0x454, "layout");
} // namespace graphic_system_layout

} // namespace nGZGraphic4
