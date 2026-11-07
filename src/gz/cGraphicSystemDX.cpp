// nGZGraphic4::cGraphicSystemDX (lane NQ): src/gz/cGraphicSystemDX.h.
#include "gz/cGraphicSystemDX.h"
#include "gz/cBehaviorRegistry.h"
#include <iterator>
#include <optional>
#include "gz/cShaderProgramsDX.h"

#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstring>
#include <new>

#include "app/WinMain.h" // nGZGraphic4::cError
#include "game/game.h"
#include "game/import_slots.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cDeviceEnum.h"
#include "gz/cEventFaces.h" // the null event's vtable
#include "gz/cImageManipDX.h"
#include "gz/cNullDevice.h"
#include "gz/PixelFormatsManager.h"
#include "gz/cVertexFormat.h"
#include "ifaces/cICanvasW32.h"
#include "ifaces/cIDisplayMonitor.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/MiscHelpersA.h"     // LogD3DError
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

// LogD3DError's source file for this class's report.
TS2_OWNED_STATIC(const char, d9cf08)[];

namespace nGZGraphic4 {

namespace {

constexpr uint32_t kLineAdapterIdentifier = 0x250;
constexpr IatSlot kDirect3DCreate9Iat = &ts2_rva_d9a6c8;
constexpr UINT kD3DSdkVersion = 0x20;           // D3D_SDK_VERSION as the game was built

// The window (the canvas service), asked for through two of its interfaces: the canvas one (Init, FindWindowDevice,
// HandleCanvasEvent's rebuild) and a second one whose slots 8 / 9 are the same Connect / Disconnect (BuildDevices,
// TearDown, Shutdown).
constexpr uint32_t kWindowServiceId = 0xfb6372ee;
constexpr uint32_t kWindowCanvasIid = 0xbb6377fa;
constexpr uint32_t kWindowBehaviorIid = 0x5b637209;

constexpr uint32_t kConnectionChangedEvent = 0x5b51e806; // nGZEvent::cConnectionChangedEvent
constexpr uint32_t kCanvasDisplayChanged = 8;            // HandleCanvasEvent's message kind (word 1)
constexpr uint8_t kResizeMinimized = 0x10;               // the resize event's flags byte (+0x14): no device switch
constexpr uint32_t kShaderSlots = 6;                     // the shader programs manager's per-device slots

// A framework service held for a scope: asked for when there is a framework, released through 0x1466e0's
// guarded release at the end.
class HeldService {
public:
    HeldService(uint32_t id, uint32_t iid)
    {
        mRef.mObject = nullptr;
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, &mRef.mObject);
    }
    ~HeldService() { mRef.ReleaseObject(); }
    HeldService(const HeldService &) = delete;
    HeldService &operator=(const HeldService &) = delete;
    void *Get() const { return mRef.mObject; }
    ifaces::cICanvasW32 *Window() const { return As<ifaces::cICanvasW32>(mRef.mObject); }

private:
    RZAutoRef mRef;
};

// A Direct3D object held for a scope (HandleCanvasEvent's second IDirect3D9): released at the end when there is one.
class HeldDirect3D {
public:
    explicit HeldDirect3D(IDirect3D9 *object) : mObject(object) {}
    ~HeldDirect3D()
    {
        if (mObject) mObject->Release();
    }
    HeldDirect3D(const HeldDirect3D &) = delete;
    HeldDirect3D &operator=(const HeldDirect3D &) = delete;
    IDirect3D9 *operator->() const { return mObject; }

private:
    IDirect3D9 *mObject;
};

IDirect3D9 *CreateDirect3D() { return ImportSlot<IDirect3D9 *(WINAPI *)(UINT), kDirect3DCreate9Iat>()(kD3DSdkVersion); }

[[noreturn]] void ThrowGraphicsError(uint32_t code)
{
    cError error;
    error.mVTable = &ts2_rva_d9cd70; // cError's vtable (owned static, app/WinMain.h)
    error.mCode = code;
    throw error;
}

ifaces::cIDisplayMonitor *MonitorOf(cDeviceDX *device) { return As<ifaces::cIDisplayMonitor>(device->GetDisplayMonitorConst()); }

// The shader programs manager (0x1758c0: ours, a function-local static).
void *ShaderProgramsManager() { return cShaderProgramsManagerDX::Instance(); }

cNullDevice gNullDevice; // slot 29's device (constant-initialised: no constructor runs; its destructor does nothing)

} // namespace

// ---- the behaviour's instantiation ----

namespace {
std::optional<nGZEvent::cEventAttributeMap> gRegistry;
const nGZEvent::ClassEvent kClassEvents[] = {
    {0xca7ccbbc, "CurrentDeviceEvent", 0x3b6c3e0a, 2},
    {0x2b5745f8, "CurrentDeviceRenderSceneEvent", 0xcb573da1, 2},
    {0x1bf6e8a1, "DevicesChanged", 0x7b256782, 2},
    {0x1b637474, "CanvasEvent", 0x7b637514, 2},
    {0x3b6decd1, "CanvasSizeMoveEvent", 0xfb6dec65, 2},
    {0x9b6c3e15, "DeviceEvent", 0x3b6c3e0a, 2},
    {0xeb573db7, "DeviceRenderSceneEvent", 0xcb573da1, 2},
};
} // namespace

nGZEvent::cEventAttributeMap *cGraphicSystemDXBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

// 0x15a250.
nGZEvent::cEventAttributeMap *__cdecl cGraphicSystemDXBehaviorTraits::RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute)
{
    return nGZEvent::ClassRegistryFor(gRegistry, kClassEvents, std::size(kClassEvents), id, attribute);
}

void cGraphicSystemDXBehaviorTraits::ResetRegistryForTest() { gRegistry.reset(); }

// ---- construction ----

void cGraphicSystemDX::Direct3DRef::Reset()
{
    if (mObject) {
        mObject->Release();
        mObject = nullptr;
    }
}

static_assert(sizeof(cDisplayMode) == 0x14, "the constructor clears five dwords");

// 0x159940's object: only the saved settings' display mode is cleared (the rest of them and the padding are left as
// the allocator gave them); the palette is black with each entry's flags its index.
cGraphicSystemDX::cGraphicSystemDX()
{
    mServiceId = kServiceId;
    mRunning = false;
    mHasReferenceDevice = false;
    mRestorePending = false;
    std::memset(&mSavedSettings.mMode, 0, sizeof(mSavedSettings.mMode));
    mCurrentDevice = -1;
    for (int i = 0; i < 256; ++i) {
        mPalette[i].mRed = 0;
        mPalette[i].mGreen = 0;
        mPalette[i].mBlue = 0;
        mPalette[i].mFlags = static_cast<uint8_t>(i);
    }
}

// 0x158f40: the device list (each released, in order), then Direct3D.
cGraphicSystemDX::~cGraphicSystemDX() = default;

cGraphicSystemDX *__cdecl cGraphicSystemDX::Create() { return new cGraphicSystemDX; }

// ---- cIGZUnknown ----

bool cGraphicSystemDX::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == cIGZSystemService::kIID) {
        *out = static_cast<cIGZSystemService *>(this);
        AddRef();
        return true;
    }
    if (riid == kIidGraphicSystemDX || riid == kIidGraphicSystem || riid == 1 || riid == kIidBehavior2) {
        *out = static_cast<cIGraphicSystemDX *>(this);
        AddRef();
        return true;
    }
    if (riid == kIidBehaviorType) {
        *out = static_cast<cIGraphicSystemDX *>(this); // not counted (the original answers 0xfb859301)
        return true;
    }
    return false;
}

uint32_t cGraphicSystemDX::AddRef() { return Behavior::AddRef(); }
uint32_t cGraphicSystemDX::Release() { return Behavior::Release(); }

// ---- the behaviour ----

void cGraphicSystemDX::OnEvent(uint32_t type, uint32_t, void *data, uint32_t arg3)
{
    const uint32_t *const words = static_cast<const uint32_t *>(data);
    if (type == kConnectionChangedEvent) {
        SyncActive();
    } else if (type == kDeviceStateEvent) {
        // A device about to be made again, created or about to be released: the shader programs manager drops what
        // it holds for every device.
        const uint32_t code = words[1];
        if (code == 0 || code == 1 || code == 3) {
            for (uint32_t i = 0; i < kShaderSlots; ++i)
                cShaderProgramsManagerDX::Instance()->ReleaseLinker(i);
        }
        const int32_t current = mCurrentDevice;
        if (current >= 0 && static_cast<uint32_t>(current) < mDevices.size() &&
            arg3 == reinterpret_cast<uintptr_t>(mDevices[current].Get()))
            NotifyListeners(kCurrentDeviceStateEvent, 0, data);
    } else if (type == kCanvasEvent) {
        HandleCanvasEvent(words);
    } else if (type == kResizeEvent) {
        // The window moved to another adapter's monitor: the current device's settings go to that adapter's device.
        if (mCurrentDevice < 0) return;
        if (static_cast<const uint8_t *>(data)[0x14] & kResizeMinimized) return;
        if (!mDevices[mCurrentDevice]->HasDeviceOrLost()) return;
        const int32_t found = FindWindowDevice();
        if (found == -1 || found == mCurrentDevice) return;
        const cDeviceSettings settings = mDevices[mCurrentDevice]->GetSettings();
        mDevices[mCurrentDevice]->ReleaseDevice();
        mCurrentDevice = found;
        if (!mDevices[found]->ApplySettings(&settings)) mDevices[mCurrentDevice]->OnDeviceLost();
    } else if (type == kDeviceFrameEvent) {
        const int32_t current = mCurrentDevice;
        if (current >= 0 && static_cast<uint32_t>(current) < mDevices.size() &&
            arg3 == reinterpret_cast<uintptr_t>(mDevices[current].Get()))
            NotifyListeners(kCurrentDeviceFrameEvent, 0, data);
    }
}

// ---- cIGraphicSystemDX ----

void cGraphicSystemDX::SetCurrentDevice(int32_t index) { mCurrentDevice = index; }
void *cGraphicSystemDX::GetPixelFormatsManager() { return cPixelFormatsManagerDX::Instance(); }
void *cGraphicSystemDX::GetVertexFormatsManager() { return cVertexFormatsManager::Instance(); }
void *cGraphicSystemDX::GetVertexDeclarationsManager() { return cVertexDeclarationsManagerDX::Instance(); }
void *cGraphicSystemDX::GetShaderProgramsManager() { return ShaderProgramsManager(); }
void *cGraphicSystemDX::GetImageManip() { return cImageManipDX::Instance(); }
int32_t cGraphicSystemDX::GetCurrentDevice() { return mCurrentDevice; }

// The device whose adapter drives the window's monitor (none while the window is in the state slot 39 reports, or
// has no monitor).
int32_t cGraphicSystemDX::FindWindowDevice()
{
    HeldService window(kWindowServiceId, kWindowCanvasIid);
    if (window.Window()->GetFlag18()) return -1;
    void *const monitor = window.Window()->GetMonitor();
    if (!monitor) return -1;
    for (uint32_t i = 0; i < mDevices.size(); ++i)
        if (MonitorOf(mDevices[i].Get())->GetMonitor() == monitor) return static_cast<int32_t>(i);
    return -1;
}

uint32_t cGraphicSystemDX::GetDeviceCount() { return static_cast<uint32_t>(mDevices.size()); }
cDeviceDX *cGraphicSystemDX::GetDevice(uint32_t index) { return mDevices[index].Get(); }

IDirect3D9 *cGraphicSystemDX::GetDirect3D(uint32_t kind)
{
    if (HasDirect3D(kind) && (kind == 1 || kind == 2)) return mDirect3D.mObject;
    return nullptr;
}

// Kind 1: Direct3D was made; kind 2: and adapter 0 has reference-rasterizer caps.
bool cGraphicSystemDX::HasDirect3D(uint32_t kind)
{
    if (kind == 1) return mDirect3D.mObject != nullptr;
    if (kind == 2) return mDirect3D.mObject != nullptr && mHasReferenceDevice;
    return false;
}

void *cGraphicSystemDX::GetVertexDeclarationsManager27() { return cVertexDeclarationsManagerDX::Instance(); }
cPaletteEntry *cGraphicSystemDX::GetPalette() { return mPalette; }
IDirect3DDevice9 *cGraphicSystemDX::GetNullDevice() { return &gNullDevice; }

// Slots 30-33 hand straight on to the statically linked D3DX / DXErr library (its code, kept in the image).
const char *cGraphicSystemDX::GetErrorString(long hr)
{
    return OriginalFunction<const char *(__stdcall *)(long), 0xd5663>()(hr);
}

long cGraphicSystemDX::CreateMeshFVF(uint32_t faces, uint32_t vertices, uint32_t options, uint32_t fvf,
                                     IDirect3DDevice9 *device, void **mesh)
{
    return OriginalFunction<long(__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t, IDirect3DDevice9 *, void **),
                            0x1c087>()(faces, vertices, options, fvf, device, mesh);
}

long cGraphicSystemDX::OptimizeFaces(const void *indices, uint32_t faces, uint32_t vertices, int32_t indices32,
                                     uint32_t *remap)
{
    return OriginalFunction<long(__stdcall *)(const void *, uint32_t, uint32_t, int32_t, uint32_t *), 0x1c0e1>()(
        indices, faces, vertices, indices32, remap);
}

long cGraphicSystemDX::OptimizeVertices(const void *indices, uint32_t faces, uint32_t vertices, int32_t indices32,
                                        uint32_t *remap)
{
    return OriginalFunction<long(__stdcall *)(const void *, uint32_t, uint32_t, int32_t, uint32_t *), 0x1546d>()(
        indices, faces, vertices, indices32, remap);
}

// ---- cIGZSystemService ----

uint32_t cGraphicSystemDX::GetServiceID() { return mServiceId; }
void cGraphicSystemDX::SetServiceID(uint32_t id) { mServiceId = id; }
int32_t cGraphicSystemDX::GetServicePriority() { return kPriority; }
bool cGraphicSystemDX::IsServiceRunning() { return mRunning; }
void cGraphicSystemDX::SetServiceRunning(bool running) { mRunning = running; }
bool cGraphicSystemDX::OnTick(uint32_t) { return true; }
bool cGraphicSystemDX::OnIdle(uint32_t) { return true; }
int32_t cGraphicSystemDX::GetServiceTickPriority() { return kPriority; }

// Listens to the window (no test that there is one), builds the devices; cError(1) without Direct3D, cError(2)
// without an adapter, a device or a current device.
bool cGraphicSystemDX::Init()
{
    HeldService window(kWindowServiceId, kWindowCanvasIid);
    cIGraphicSystemDX *const self = this;
    window.Window()->Connect(kResizeEvent, self, kResizeEvent);
    window.Window()->Connect(kCanvasEvent, self, kCanvasEvent);
    BuildDevices();
    if (!mDirect3D.mObject) ThrowGraphicsError(1);
    if (mDirect3D.mObject->GetAdapterCount() == 0 || mDevices.empty() || mCurrentDevice < 0) ThrowGraphicsError(2);
    return true;
}

bool cGraphicSystemDX::Shutdown()
{
    HeldService window(kWindowServiceId, kWindowBehaviorIid);
    if (window.Get()) {
        cIGraphicSystemDX *const self = this;
        window.Window()->Disconnect(kResizeEvent, self, kResizeEvent);
        window.Window()->Disconnect(kCanvasEvent, self, kCanvasEvent);
    }
    TearDown();
    return true;
}

// ---- the devices ----

void cGraphicSystemDX::BuildDevices()
{
    IDirect3D9 *const direct3D = CreateDirect3D();
    mDirect3D.Reset();
    mDirect3D.mObject = direct3D;
    if (!direct3D || direct3D->GetAdapterCount() == 0) return;

    D3DCAPS9 caps;
    mHasReferenceDevice = SUCCEEDED(mDirect3D.mObject->GetDeviceCaps(0, D3DDEVTYPE_REF, &caps));
    HeldService window(kWindowServiceId, kWindowBehaviorIid);
    uint32_t count = mDirect3D.mObject->GetAdapterCount();
    if (count > kMaxDevices) count = kMaxDevices;
    if (mDevices.capacity() < count) mDevices.reserve(count);

    cIGraphicSystemDX *const self = this;
    for (uint32_t adapter = 0; adapter < count; ++adapter) {
        if (FAILED(mDirect3D.mObject->GetDeviceCaps(adapter, D3DDEVTYPE_HAL, &caps))) continue;
        void *const memory = ::operator new(cDeviceDX::kSize);
        cDeviceDX *device = nullptr;
        if (memory) {
            try {
                device = cDeviceDXAdapters::Construct(memory, nullptr);
            } catch (...) {
                ::operator delete(memory);
                throw;
            }
        }
        DeviceRef held(device);
        // ORIGINAL-BUG: the original counts the new device a second time here and never releases that reference,
        // so every device built outlives the system by one reference.
        device->AddRef();
        if (cDeviceDXAdapters::Init(device, nullptr, adapter)) {
            mDevices.push_back(held);
            window.Window()->Connect(kCanvasEvent, device, kCanvasEvent);
            window.Window()->Connect(kResizeEvent, device, kResizeEvent);
            device->Connect(kDeviceStateEvent, self, kDeviceStateEvent);
            device->Connect(kDeviceFrameEvent, self, kDeviceFrameEvent);
        }
    }

    if (!mDevices.empty()) {
        const int32_t found = FindWindowDevice();
        mCurrentDevice = found != -1 ? found : 0;
        const void *nullEvent = nGZEvent::NullEventVTable();
        NotifyListeners(kDevicesBuiltEvent, 0, &nullEvent);
    }
}

void cGraphicSystemDX::TearDown()
{
    HeldService window(kWindowServiceId, kWindowBehaviorIid);
    cIGraphicSystemDX *const self = this;
    for (const DeviceRef &device : mDevices) {
        device->ReleaseDevice();
        if (window.Get()) {
            window.Window()->Disconnect(kCanvasEvent, device.Get(), kCanvasEvent);
            window.Window()->Disconnect(kResizeEvent, device.Get(), kResizeEvent);
        }
        device->Disconnect(kDeviceStateEvent, self, kDeviceStateEvent);
        device->Disconnect(kDeviceFrameEvent, self, kDeviceFrameEvent);
    }
    for (const DeviceRef &device : mDevices) {
        device->AddRef(); // a counted copy of the list made and dropped again (the original's)
        device->Release();
    }
    mDevices.clear();
    mCurrentDevice = -1;
    mDirect3D.Reset();
}

void cGraphicSystemDX::HandleCanvasEvent(const uint32_t *message)
{
    if (message[1] != kCanvasDisplayChanged || !mDirect3D.mObject) return;
    if (mCurrentDevice >= 0) {
        cDeviceDX *const current = mDevices[mCurrentDevice].Get();
        if (current->mReleased) return;
        if (current->HasDeviceOrLost()) {
            mRestorePending = true;
            mSavedSettings = mDevices[mCurrentDevice]->GetSettings();
        }
    }

    // A fresh Direct3D tells whether the adapters are still the ones the devices were built for.
    HeldDirect3D fresh(CreateDirect3D());
    const UINT adapters = fresh->GetAdapterCount();
    bool same = adapters == mDevices.size();
    if (same) {
        for (uint32_t i = 0; i < mDevices.size(); ++i) {
            D3DADAPTER_IDENTIFIER9 identifier;
            const HRESULT hr = fresh->GetAdapterIdentifier(i, 0, &identifier);
            if (FAILED(hr))
                LogD3DError(static_cast<uint32_t>(hr),
                            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts2_rva_d9cf08)),
                            kLineAdapterIdentifier);
            const D3DADAPTER_IDENTIFIER9 known =
                static_cast<cIDeviceEnum *>(mDevices[i]->GetDeviceEnum45())->GetAdapterIdentifier();
            if (std::memcmp(&identifier.DeviceIdentifier, &known.DeviceIdentifier, sizeof(GUID)) != 0) {
                same = false;
                break;
            }
        }
    }

    if (same) {
        // The monitors as the (old) Direct3D now reports them.
        for (UINT i = 0; i < adapters; ++i) {
            void *const monitor = MonitorOf(mDevices[i].Get())->GetMonitor();
            void *const now = mDirect3D.mObject->GetAdapterMonitor(i);
            if (monitor != now) MonitorOf(mDevices[i].Get())->SetMonitor(now);
        }
        if (mRestorePending && !mDevices[mCurrentDevice]->IsFullscreen() && !mDevices[mCurrentDevice]->mDeviceLost) {
            mRestorePending = false;
            RestoreDeviceSettings(mDevices[mCurrentDevice].Get(), mSavedSettings);
        }
    } else {
        TearDown();
        BuildDevices();
        HeldService window(kWindowServiceId, kWindowCanvasIid);
        window.Window()->SetFlag19(true);
        if (mRestorePending && mCurrentDevice >= 0 && !mDevices.empty()) {
            mRestorePending = false;
            RestoreDeviceSettings(mDevices[mCurrentDevice].Get(), mSavedSettings);
        }
    }
}

void cGraphicSystemDX::RestoreDeviceSettings(cDeviceDX *device, const cDeviceSettings &settings)
{
    device->ReleaseDevice();
    device->mSettings = settings;
    device->OnDeviceLost();
}

} // namespace nGZGraphic4
