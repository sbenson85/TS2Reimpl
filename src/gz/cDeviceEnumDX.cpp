// nGZGraphic4::cDeviceEnumDX and cDisplayModeIterator (lane NK, rendering campaign R2b): src/gz/cDeviceEnumDX.h.
#include "gz/cDeviceEnumDX.h"

#include <windows.h>
#include <d3d9.h>

#include <cstring>
#include <new>

#include "game/game.h"
#include "game/import_slots.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cGZAny.h"
#include "gz/cIGZString.h"
#include "gz/cSurfaceCopy.h"
#include "ifaces/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIPixelFormatsManager.h"
#include "rz/MiscHelpersA.h" // LogD3DError
#include "rz/RZAutoRef.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"
#include "rz/RZTextConvert.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cRZString.h"

namespace nGZGraphic4 {

namespace {

// The graphic service; its slot 25(type) is the IDirect3D9 for a device type, slot 26(type) whether the type is usable.
constexpr uint32_t kGraphicServiceId = 0x85522803, kGraphicServiceIid = 0xc55289ba;
// The pixel format iterator's COM class and interface (cPixelFormatIterator::CreateEmpty).
constexpr uint32_t kFormatIteratorClass = 0xbb782354, kFormatIteratorIid = 0x3b6739f5;
constexpr uint32_t kDepthStencilFormatIid = 0x5ac389b6; // cIPixelFormatDepthStencil
constexpr uint32_t kPaletteClassId = 0xbac3896f;        // the palette formats' class: never a colour format here

constexpr uint32_t kIidDeviceEnum = 0x6c02c311;   // cIDeviceEnum
constexpr uint32_t kIidDeviceEnumDX = 0xc9da7e9f; // cIDeviceEnumDX

// cDeviceDX flags Refresh reads (cDeviceSettings::mFlags).
constexpr uint32_t kFlagSoftwareProcessing = 0x02; // software vertex processing
constexpr uint32_t kFlagReferenceDevice = 0x08;    // the reference rasterizer
constexpr uint32_t kFlagSoftwareDevice = 0x10;     // a software device (as the forceSoftwareDevice property)

constexpr HRESULT kNotAvailable = static_cast<HRESULT>(0x8876086a); // D3DERR_NOTAVAILABLE: not reported

// Tables in the original's .rdata.
// ts2_rva_d9f498: 6 D3DFORMATs a usage-0 colour format must be one of
constexpr uint32_t kColorTargetCount = 6;
// ts2_rva_d9f4b0: per usage index, D3DUSAGE and D3DRESOURCETYPE (11 pairs)
// ts2_rva_d9f508: the 5 display formats Refresh enumerates modes of
constexpr uint32_t kModeFormatCount = 5;
// CanStretchRect's answers, 4x4 bytes each, by DevCaps2 CAN_STRETCHRECT_FROM_TEXTURES and stretching.
// ts2_rva_d9f964, then +0x10 stretching, +0x20 caps, +0x30 caps and stretching

constexpr uint32_t kUsageDepth = 0xb;              // the usage Refresh asks depth formats for
constexpr uint32_t kFormatA8 = 0x1c, kFormatP8 = 0x29; // D3DFMT_A8, D3DFMT_P8
constexpr uint32_t kDxt1 = 0x31545844, kDxt5 = 0x35545844;
constexpr DWORD kStretchFromTextures = 0x10;       // D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES
constexpr uint32_t kSurfaceKindTexture = 0xb;      // CanStretchRect's kind that needs the caps bit and an exact copy

// LogD3DError lines.
constexpr uint32_t kLineIdentifier = 0xa5;
constexpr uint32_t kLineEnumMode = 0xe7;
constexpr uint32_t kLineCheckModeType = 0xed;
constexpr uint32_t kLineCaps = 0x127;
constexpr uint32_t kLineCreationParameters = 0x153;
constexpr uint32_t kLineSoftwareProcessing = 0x158;
constexpr uint32_t kLineSoftwareCaps = 0x159;
constexpr uint32_t kLineMixedSoftware = 0x166;
constexpr uint32_t kLineMixedSoftwareCaps = 0x167;
constexpr uint32_t kLineMixedHardware = 0x16c;
constexpr uint32_t kLineMixedHardwareCaps = 0x16d;
constexpr uint32_t kLineDeviceDisplayMode = 0x2c9;
constexpr uint32_t kLineAdapterDisplayMode = 0x2d0;
constexpr uint32_t kLineColorFormat = 0x359;
constexpr uint32_t kLineColorType = 0x36b;
constexpr uint32_t kLineUsageFormat = 0x378;
constexpr uint32_t kLineDepthFormat = 0x3e8;
constexpr uint32_t kLineDepthMatch = 0x3f1;
constexpr uint32_t kLineMultisample = 0x433;

void Report(HRESULT hr, uint32_t line)
{
    LogD3DError(static_cast<uint32_t>(hr),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts2_rva_d9f810)),
                line);
}

// A result the original reports only when it is a failure other than D3DERR_NOTAVAILABLE.
void ReportUnexpected(HRESULT hr, uint32_t line)
{
    if (hr != 0 && hr != kNotAvailable && hr < 0) Report(hr, line);
}

// A framework service held for a scope (released through 0x1466e0's guarded release).
class HeldService {
public:
    HeldService(uint32_t id, uint32_t iid)
    {
        mRef.mObject = nullptr;
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, &mRef.mObject);
    }
    ~HeldService() { mRef.ReleaseObject(); }
    // The graphic service, through HoldGraphicService (0x1773b0).
    HeldService() { HoldGraphicService(&mRef.mObject, nullptr); }
    HeldService(const HeldService &) = delete;
    HeldService &operator=(const HeldService &) = delete;
    void *Get() const { return mRef.mObject; }

private:
    RZAutoRef mRef;
};

// The IDirect3D9 for a device type: the graphic service held only for the question (not checked for null).
IDirect3D9 *Direct3DFor(uint32_t deviceType)
{
    HeldService graphics;
    return RawSlot<25, IDirect3D9 *>(graphics.Get(), deviceType);
}

// 0x18acd0 (game code): the pixel formats manager (cached).
void *FormatsManager() { return GetPixelFormatsManagerCached(); }

// The manager's slot 5: its "unknown" format, which the "for the display" slots pass for the desktop's format.
cIPixelFormat *DesktopFormat() { return RawSlot<5, cIPixelFormat *>(FormatsManager()); }

cIPixelFormat *FormatFor(uint32_t d3dFormat)
{
    return static_cast<cIPixelFormat *>(As<ifaces::cIPixelFormatsManager>(FormatsManager())->GetFormat(d3dFormat));
}

cIPixelFormatIterator *NewFormatIterator()
{
    cIPixelFormatIterator *iterator = nullptr;
    As<ifaces::cIGZCOM>(GetDirectorFrameworkObject())->GetClassObject(kFormatIteratorClass, kFormatIteratorIid,
                                                                       &iterator);
    return iterator;
}

// The range handed out for a filled iterator (as cPixelFormatIterator::CreateRange): its end, a cursor at its start,
// itself; the caller's reference released.
void FinishRange(cPixelFormatRange *out, cIPixelFormatIterator *iterator)
{
    cIPixelFormatIterator *const end = iterator->CloneFormatIterator();
    if (end) end->AddRef();
    end->SetToEnd();
    cIPixelFormatIterator *const current = iterator->Clone();
    out->mCurrent = current;
    if (current) current->AddRef();
    out->mBegin = iterator;
    iterator->AddRef();
    out->mEnd = end;
    if (end) end->AddRef();
    if (end) end->Release();
    iterator->Release();
}

// 0x146670's order: end, begin, cursor.
template <class Iterator> void ReleaseRange(cIteratorRange<Iterator> &range)
{
    if (range.mEnd) range.mEnd->Release();
    if (range.mBegin) range.mBegin->Release();
    if (range.mCurrent) range.mCurrent->Release();
}

bool IsDefaultMode(const cDisplayMode &mode)
{
    const cDisplayMode &standard = ts2_rva_10ca3a0;
    return mode.mWidth == standard.mWidth && mode.mHeight == standard.mHeight && mode.mAspect == standard.mAspect &&
           mode.mRefresh == standard.mRefresh && mode.mFormat == standard.mFormat;
}

// Width over height times the monitor's pixel aspect (1 when the monitor answers 0).
float ModeAspect(uint32_t width, uint32_t height, float monitorAspect)
{
    if (monitorAspect == 0.0f) return 1.0f;
    const float w = static_cast<float>(static_cast<double>(width));
    const float h = static_cast<float>(static_cast<double>(height));
    return w / (h * monitorAspect);
}

// The usage table's pair for a usage index. ORIGINAL-BUG: not bounds checked (an index past 10 reads the next table).
void UsagePair(uint32_t usage, DWORD *d3dUsage, D3DRESOURCETYPE *type)
{
    const uint32_t *const pair = ts2_rva_d9f4b0[0] + usage * 2;
    *d3dUsage = pair[0];
    *type = static_cast<D3DRESOURCETYPE>(pair[1]);
}

// CanStretchRect's surface class for a kind (the byte table 0x17a948 and its jump table); -1: never; `fallback` for
// kind 11 and kinds past 13.
int32_t StretchClass(uint32_t kind, uint32_t fallback)
{
    switch (kind) {
    case 0: case 1: return 3;
    case 2: case 3: return 2;
    case 4: case 6: return 0;
    case 5: case 12: case 13: return -1;
    case 7: case 8: case 9: case 10: return 1;
    default: return static_cast<int32_t>(fallback);
    }
}

} // namespace

// 0x178560 (cdecl): the caps as the engine uses them. `noVertexShaders`: a device with vertex shaders but no pixel
// shaders reports none (the shader fields cleared); `noProjection`: no projected textures; the counts clamped; the
// power-of-two flags made consistent.
void cDeviceEnumDX::ClampCaps(D3DCAPS9 *caps, bool noVertexShaders, bool noProjection)
{
    const uint8_t *const bytes = reinterpret_cast<const uint8_t *>(caps);
    if (noVertexShaders && bytes[offsetof(D3DCAPS9, VertexShaderVersion) + 1] >= 1 &&
        bytes[offsetof(D3DCAPS9, PixelShaderVersion) + 1] == 0) {
        caps->VertexShaderVersion = 0;
        caps->MaxVertexShaderConst = 0;
        caps->VS20Caps.Caps = 0;
        caps->VS20Caps.DynamicFlowControlDepth = 0;
        caps->VS20Caps.NumTemps = 0;
        caps->VS20Caps.StaticFlowControlDepth = 0;
        caps->MaxVShaderInstructionsExecuted = 0;
        caps->MaxVertexShader30InstructionSlots = 0;
    }
    if (noProjection) caps->TextureCaps &= ~D3DPTEXTURECAPS_PROJECTED;
    if (caps->NumSimultaneousRTs > 4) caps->NumSimultaneousRTs = 4;
    if (caps->MaxTextureBlendStages > 8) caps->MaxTextureBlendStages = 8;
    if (caps->MaxSimultaneousTextures > 0x10) caps->MaxSimultaneousTextures = 0x10;
    if (caps->MaxStreams > 0x10) caps->MaxStreams = 0x10;
    if (caps->MaxVertexShaderConst > 0x400) caps->MaxVertexShaderConst = 0x400;
    if (caps->MaxActiveLights > 0x10) caps->MaxActiveLights = 0x10;
    if (!(caps->TextureCaps & D3DPTEXTURECAPS_POW2))
        caps->TextureCaps &= ~D3DPTEXTURECAPS_NONPOW2CONDITIONAL;
    else if (caps->TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL)
        caps->TextureCaps |= D3DPTEXTURECAPS_POW2;
}

// ---- cLessDisplayMode (0x1778c0) ----

bool cLessDisplayMode::operator()(const cDisplayMode &a, const cDisplayMode &b) const
{
    if (a.mFormat->Less(b.mFormat, false)) return true;
    if (b.mFormat->Less(a.mFormat, false)) return false;
    if (a.mWidth != b.mWidth) return a.mWidth < b.mWidth;
    if (a.mHeight != b.mHeight) return a.mHeight < b.mHeight;
    if (a.mRefresh != b.mRefresh) return a.mRefresh < b.mRefresh;
    return a.mAspect < b.mAspect;
}

// ---- cDisplayModeIterator ----

cDisplayModeIterator::cDisplayModeIterator(cCountedDisplayModes *modes, const cDisplayMode *cursor)
    : mRefCount(0), mModes(modes)
{
    if (mModes) mModes->AddRef();
    mCursor = cursor;
}

cDisplayModeIterator::~cDisplayModeIterator()
{
    if (mModes) mModes->Release();
}

void *cDisplayModeIterator::operator new(size_t size)
{
    if (size != kSize) return ::operator new(size);
    return reinterpret_cast<RZFixedAllocatorBase *>(&Pool())->Alloc();
}

void cDisplayModeIterator::operator delete(void *block, size_t size)
{
    if (size != kSize) {
        ::operator delete(block);
        return;
    }
    reinterpret_cast<RZFastFixed0x10 *>(&Pool())->Free(block);
}

bool cDisplayModeIterator::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (!ppvObj) return false;
    if (riid != 1) return false;
    *ppvObj = this;
    AddRef();
    return true;
}

uint32_t cDisplayModeIterator::AddRef() { return ++mRefCount; }

uint32_t cDisplayModeIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIDisplayModeIterator *cDisplayModeIterator::Clone() { return new cDisplayModeIterator(mModes, mCursor); }

bool cDisplayModeIterator::IsEqual(const cIDisplayModeIterator *other)
{
    return mCursor == static_cast<const cDisplayModeIterator *>(other)->mCursor;
}

cDisplayMode cDisplayModeIterator::Get() { return *mCursor; }

void cDisplayModeIterator::Next() { ++mCursor; }

void cDisplayModeIterator::Prev() { --mCursor; }

// ---- cDeviceEnumDX: construction ----

cDeviceEnumDX::cDeviceEnumDX(uint32_t adapter, cIDeviceDX *owner)
    : mRefCount(0), mAdapter(adapter), mOwner(owner), mVideoMemory(0), mDeviceType(D3DDEVTYPE_HAL), mDevice(nullptr),
      mHardwareCapsValid(0), mSoftwareCapsValid(0)
{
    mForceSoftwareVertexProcessing = 0;
    mStreamOffsetCap = 0;
    mWindowedFormatConversion = 0;
    mEnumerateMultisampleLevels = 0;
    mDisableTextureProjection = 0;

    IDirect3D9 *const direct3d = Direct3DFor(D3DDEVTYPE_HAL);
    const HRESULT hr = direct3d->GetAdapterIdentifier(adapter, D3DENUM_WHQL_LEVEL, &mIdentifier);
    if (hr < 0) Report(hr, kLineIdentifier);

    // The video memory: a probe device on a second IDirect3D9. ORIGINAL-BUG: the probe IDirect3D9 is never released
    // (nor checked for null).
    IDirect3D9 *const probe = ImportSlot<IDirect3D9 *(WINAPI *)(UINT), &ts2_rva_d9a6c8>()(D3D_SDK_VERSION);
    probe->GetAdapterMonitor(mAdapter);
    HWND const desktop = ImportSlot<HWND(WINAPI *)(), &ts2_rva_d9a5d0>()();
    D3DPRESENT_PARAMETERS parameters = {};
    parameters.BackBufferWidth = 800;
    parameters.BackBufferHeight = 600;
    parameters.BackBufferFormat = D3DFMT_R5G6B5;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = desktop;
    parameters.Windowed = TRUE;
    IDirect3DDevice9 *device = nullptr;
    if (probe->CreateDevice(mAdapter, D3DDEVTYPE_HAL, desktop, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters,
                            &device) >= 0) {
        const UINT memory = device->GetAvailableTextureMem();
        device->Release();
        mVideoMemory = memory;
    } else {
        mVideoMemory = 0;
    }
}

cDeviceEnumDX::~cDeviceEnumDX() = default; // the set, then the property set (member order)

cDeviceEnumDX *__fastcall cDeviceEnumDXAdapters::Construct(void *memory, void *, uint32_t adapter, cIDeviceDX *owner)
{
    return ::new (memory) cDeviceEnumDX(adapter, owner);
}

// ---- properties ----

uint8_t cDeviceEnumDX::BoolProperty(const char *key, uint8_t fallback)
{
    gz::AnyBoolean value(fallback);
    const GZServiceString name(key);
    static_cast<cIGZPropertySetString &>(mProperties).GetPropertyValue(&name, &value);
    return value.mValue;
}

uint32_t cDeviceEnumDX::Uint32Property(const char *key, uint32_t fallback)
{
    gz::AnyUint32 value(fallback);
    const GZServiceString name(key);
    static_cast<cIGZPropertySetString &>(mProperties).GetPropertyValue(&name, &value);
    return value.mValue;
}

// ---- Refresh (0x178b80) ----

uint32_t cDeviceEnumDX::Refresh(IDirect3DDevice9 *device, uint32_t flags)
{
    HeldService graphics(kGraphicServiceId, kGraphicServiceIid);
    mDevice = device;
    mHardwareCapsValid = 0;
    mSoftwareCapsValid = 0;

    const uint8_t forceSoftware = BoolProperty("forceSoftwareDevice", 0);
    uint32_t type = ((flags & kFlagSoftwareDevice) || forceSoftware) ? D3DDEVTYPE_SW : D3DDEVTYPE_HAL;
    if (flags & kFlagReferenceDevice) type = D3DDEVTYPE_REF;
    if (!RawSlot<26, bool>(graphics.Get(), type)) type = D3DDEVTYPE_HAL;
    if (type != mDeviceType) mModes.clear();
    mDeviceType = type;

    IDirect3D9 *const direct3d = Direct3DFor(mDeviceType);

    if (mModes.empty()) {
        const float monitorAspect = RawSlot<6, float>(mOwner->GetDisplayMonitor());
        const uint32_t *const formats = ts2_rva_d9f508;
        for (uint32_t f = 0; f < kModeFormatCount; ++f) {
            const int32_t count = static_cast<int32_t>(direct3d->GetAdapterModeCount(mAdapter, static_cast<D3DFORMAT>(formats[f])));
            for (int32_t i = 0; i < count; ++i) {
                D3DDISPLAYMODE mode = {}; // ORIGINAL-BUG: the original's is stale when EnumAdapterModes fails
                HRESULT hr = direct3d->EnumAdapterModes(mAdapter, static_cast<D3DFORMAT>(formats[f]), i, &mode);
                if (hr < 0) Report(hr, kLineEnumMode);
                hr = direct3d->CheckDeviceType(mAdapter, D3DDEVTYPE_HAL, mode.Format, mode.Format, FALSE);
                if (hr != 0 && hr != kNotAvailable && hr < 0) {
                    Report(hr, kLineCheckModeType);
                    FormatFor(mode.Format); // asked and dropped
                    continue;
                }
                cIPixelFormat *const format = FormatFor(mode.Format);
                if (hr != 0) continue;
                cPixelFormatRange depths = GetDepthFormats(kUsageDepth, format, format);
                uint32_t depthCount = 0;
                while (!depths.mCurrent->IsEqual(depths.mEnd)) {
                    depths.mCurrent->Next();
                    ++depthCount;
                }
                ReleaseRange(depths);
                if (depthCount == 0) continue;
                const cDisplayMode entry = {mode.Width, mode.Height, ModeAspect(mode.Width, mode.Height, monitorAspect),
                                            mode.RefreshRate, format};
                mModes.insert(entry);
            }
        }
    }

    std::memset(mLimits, 0, sizeof(mLimits));
    std::memset(mProcessingCaps, 0, sizeof(mProcessingCaps));
    if (mDeviceType == D3DDEVTYPE_SW) {
        mLimits[0] = Uint32Property("softwareDeviceLocalVideoMemorySize", 0x2000000);
    } else if (mDeviceType == D3DDEVTYPE_REF) {
        mLimits[0] = 0x10000000;
    } else {
        const uint32_t configured = Uint32Property("deviceLocalVideoMemorySizeDefault", 0);
        mLimits[0] = mVideoMemory < configured ? configured : mVideoMemory;
    }
    const uint8_t forceSoftwareProcessing = BoolProperty("forceSoftwareVP", 0);
    mForceSoftwareVertexProcessing = (forceSoftwareProcessing || (flags & kFlagSoftwareProcessing)) ? 1 : 0;

    // ORIGINAL-BUG: the original's caps are stale when GetDeviceCaps fails; ours start zeroed.
    D3DCAPS9 caps = {};
    uint8_t *const processing = mProcessingCaps;
    if (!mDevice) {
        HRESULT hr = direct3d->GetDeviceCaps(mAdapter, static_cast<D3DDEVTYPE>(mDeviceType), &caps);
        if (hr < 0) Report(hr, kLineCaps);
        ClampCaps(&caps, ((caps.DevCaps >> 16) & 1) != 0, mDisableTextureProjection != 0);
        mStreamOffsetCap = caps.DevCaps2 & D3DDEVCAPS2_STREAMOFFSET;
        if (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) {
            const bool bigStreams = caps.MaxStreamStride >= 0xfc && caps.MaxVertexIndex >= 0xffff;
            const bool fullProcessing = (caps.VertexProcessingCaps & D3DVTXPCAPS_DIRECTIONALLIGHTS) &&
                                        (caps.VertexProcessingCaps & D3DVTXPCAPS_POSITIONALLIGHTS);
            if (!bigStreams || !fullProcessing) mForceSoftwareVertexProcessing = 1;
        }
        processing[1] = 1;
        processing[3] = 1;
        if (!mForceSoftwareVertexProcessing && (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)) {
            processing[0] = static_cast<uint8_t>((caps.DevCaps >> 16) & 1);
            processing[2] = (caps.VertexShaderVersion & 0xff00) >= 0x100;
        } else {
            processing[0] = 0;
            processing[2] = 0;
        }
    } else {
        // ORIGINAL-BUG: the original's parameters are stale when GetCreationParameters fails; ours start zeroed.
        D3DDEVICE_CREATION_PARAMETERS creation = {};
        HRESULT hr = mDevice->GetCreationParameters(&creation);
        if (hr < 0) Report(hr, kLineCreationParameters);
        uint8_t hardwareFixed, hardwareShaders;
        if (creation.BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) {
            hr = mDevice->SetSoftwareVertexProcessing(TRUE);
            if (hr < 0) Report(hr, kLineSoftwareProcessing);
            hr = mDevice->GetDeviceCaps(&caps);
            if (hr < 0) Report(hr, kLineSoftwareCaps);
            ClampCaps(&caps, false, mDisableTextureProjection != 0);
            mSoftwareCaps = caps;
            mSoftwareCapsValid = 1;
            hardwareFixed = 0;
            hardwareShaders = 0;
        } else {
            if (creation.BehaviorFlags & D3DCREATE_MIXED_VERTEXPROCESSING) {
                hr = mDevice->SetSoftwareVertexProcessing(TRUE);
                if (hr < 0) Report(hr, kLineMixedSoftware);
                hr = mDevice->GetDeviceCaps(&mSoftwareCaps);
                if (hr < 0) Report(hr, kLineMixedSoftwareCaps);
                ClampCaps(&mSoftwareCaps, false, mDisableTextureProjection != 0);
                mSoftwareCapsValid = 1;
            }
            hr = mDevice->SetSoftwareVertexProcessing(FALSE);
            if (hr < 0) Report(hr, kLineMixedHardware);
            hr = mDevice->GetDeviceCaps(&caps);
            if (hr < 0) Report(hr, kLineMixedHardwareCaps);
            ClampCaps(&caps, true, mDisableTextureProjection != 0);
            mHardwareCaps = caps;
            hardwareShaders = (caps.VertexShaderVersion & 0xff00) >= 0x100;
            hardwareFixed = static_cast<uint8_t>((caps.DevCaps >> 16) & 1);
            mHardwareCapsValid = 1;
        }
        processing[0] = hardwareFixed;
        processing[2] = hardwareShaders;
        const uint8_t software =
            (creation.BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)) ? 1 : 0;
        processing[1] = software;
        processing[3] = software;
    }

    if (!BoolProperty("fixedFunctionHWVertexProcessingOK", 1)) processing[0] = 0;

    processing[7] = static_cast<uint8_t>((caps.DevCaps >> 20) & 1);
    processing[6] = static_cast<uint8_t>((caps.TextureCaps >> 11) & 1);
    processing[8] = static_cast<uint8_t>((caps.TextureCaps >> 7) & 1);
    processing[9] = static_cast<uint8_t>(~(caps.TextureCaps >> 1) & 1);
    processing[10] = static_cast<uint8_t>((caps.TextureCaps >> 8) & 1);
    mLimits[1] = caps.NumSimultaneousRTs;
    mLimits[2] = caps.MaxTextureBlendStages;
    mLimits[3] = caps.MaxSimultaneousTextures;
    const uint32_t pixelMajor = (caps.PixelShaderVersion >> 8) & 0xff;
    const uint32_t pixelMinor = caps.PixelShaderVersion & 0xff;
    mLimits[9] = pixelMinor;
    mLimits[8] = pixelMajor;
    const uint8_t hardwareFixed = processing[0];
    if (hardwareFixed || processing[2]) {
        processing[4] = static_cast<uint8_t>(caps.DeclTypes & D3DDTCAPS_UBYTE4);
        mLimits[0xa] = caps.MaxStreams != 0 ? caps.MaxStreams : 1;
        mLimits[0xc] = caps.MaxVertexIndex;
        mLimits[0xe] = caps.MaxStreamStride;
    }
    if (processing[2]) {
        const uint32_t vertexMajor = (caps.VertexShaderVersion >> 8) & 0xff;
        mLimits[5] = caps.VertexShaderVersion & 0xff;
        mLimits[0x10] = caps.MaxVertexShaderConst;
        mLimits[4] = vertexMajor;
        const uint32_t textures = vertexMajor < 2 ? 0 : 0x10;
        mLimits[0x12] = textures;
        mLimits[0x14] = textures;
    }
    if (hardwareFixed) mLimits[0x19] = caps.MaxActiveLights;
    if (processing[1] || processing[3]) {
        processing[5] = 1;
        mLimits[0xb] = 0x10;
        mLimits[0xd] = 0xffffffff;
        mLimits[0xf] = 0x100;
        if (processing[3]) {
            mLimits[6] = 3;
            mLimits[7] = 0;
            mLimits[0x11] = 0x400;
            mLimits[0x13] = 0x100;
            mLimits[0x15] = 0x100;
        }
    }
    if (processing[1]) mLimits[0x1a] = 0x10;
    switch (pixelMajor) {
    case 1:
        mLimits[0x16] = 8;
        break;
    case 2:
        mLimits[0x16] = 0x20;
        if (pixelMinor != 0) {
            mLimits[0x17] = 0x10;
            mLimits[0x18] = 0x10;
        }
        break;
    case 3:
        mLimits[0x16] = 0xe0;
        mLimits[0x17] = 0x10;
        mLimits[0x18] = 0x10;
        break;
    default:
        break;
    }

    std::memset(mFeatures, 0, sizeof(mFeatures));
    std::memset(&mMinTextureDimension, 0, sizeof(mMinTextureDimension));
    mFeatures[0] = BoolProperty("queriesOK", 0);
    mFeatures[1] = BoolProperty("skipValidateDevice", 0);
    mFeatures[2] = BoolProperty("enableDriverMemoryManager", 1);
    mFeatures[3] = BoolProperty("disableVSyncSupport", 0);
    mFeatures[5] = BoolProperty("disableTexMemEstimateAdjustment", 0);
    mFeatures[6] = BoolProperty("createNVidiaWorkaroundTexture", 0);
    mFeatures[4] = BoolProperty("substituteFlipForDiscardSwapEffect", 0);
    mWindowedFormatConversion = BoolProperty("windowedModeFormatConversionOK", mStreamOffsetCap);
    const uint8_t multisampleDefault = (mStreamOffsetCap && mIdentifier.WHQLLevel != 0) ? 1 : 0;
    if (BoolProperty("enumerateMultisampleLevels", multisampleDefault))
        mEnumerateMultisampleLevels = mDeviceType != D3DDEVTYPE_SW;
    else
        mEnumerateMultisampleLevels = mDeviceType == D3DDEVTYPE_REF;
    mDisableTextureProjection = BoolProperty("disableTextureProjection", 0);
    mMinTextureDimension = Uint32Property("minTextureDimensionForBalance", 0x40);
    return type;
}

// ---- the dump (0x1788f0) ----

void cDeviceEnumDX::DumpInfo(cIGZUnknown *stream)
{
    ifaces::cIGZOStream *const out = As<ifaces::cIGZOStream>(stream);
    out->SetRZCharStr("Device name: '");
    out->SetRZCharStr(mIdentifier.Description);
    out->SetRZCharStr("'\n");
    out->SetRZCharStr("Driver version: ");
    out->SetUint16(HIWORD(mIdentifier.DriverVersion.HighPart));
    out->SetRZCharStr(".");
    out->SetUint16(LOWORD(mIdentifier.DriverVersion.HighPart));
    out->SetRZCharStr(".");
    out->SetUint16(HIWORD(mIdentifier.DriverVersion.LowPart));
    out->SetRZCharStr(".");
    out->SetUint16(LOWORD(mIdentifier.DriverVersion.LowPart));
    out->SetRZCharStr("\n");
    struct Text {
        cRZString s;
        Text() { s.Construct(); }
        ~Text() { s.Destruct(); }
        Text(const Text &) = delete;
        Text &operator=(const Text &) = delete;
    } ids;
    ids.s.Sprintf("VendorId: 0x%04x\nDeviceId: 0x%04x\nSubSysId: 0x%08x\nRevision: 0x%04x\n", mIdentifier.VendorId,
                  mIdentifier.DeviceId, mIdentifier.SubSysId, mIdentifier.Revision);
    out->SetRZCharStr(ids.s.Data());
    out->SetRZCharStr("WHQL cert: ");
    const DWORD whql = mIdentifier.WHQLLevel;
    if (whql == 0) {
        out->SetRZCharStr("no\n");
    } else if (whql == 1) {
        out->SetRZCharStr("yes\n");
    } else {
        // ORIGINAL-BUG: the month and day are masked with 8 rather than 0xff (the date is year << 16 | month << 8 | day).
        out->SetUint32(whql >> 16);
        out->SetRZCharStr("-");
        out->SetUint32((whql >> 8) & 8);
        out->SetRZCharStr("-");
        out->SetUint32(whql & 8);
        out->SetRZCharStr("\n");
    }
}

// ---- slots ----

bool cDeviceEnumDX::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (!ppvObj) return false;
    if (riid != 1 && riid != kIidDeviceEnum && riid != kIidDeviceEnumDX) return false;
    *ppvObj = this;
    AddRef();
    return true;
}

uint32_t cDeviceEnumDX::AddRef() { return ++mRefCount; }

uint32_t cDeviceEnumDX::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIGZUnknown *cDeviceEnumDX::GetDevice() { return reinterpret_cast<cIGZUnknown *>(mOwner); }

cIGZUnknown *cDeviceEnumDX::GetDeviceDX() { return reinterpret_cast<cIGZUnknown *>(mOwner); }

void cDeviceEnumDX::GetDescription(cIGZString *api, cIGZString *apiVersion, cIGZString *driver,
                                   cIGZString *driverVersion, cIGZString *description, cIGZString *ids)
{
    if (api) api->FromChar("Graphic4 DX9 Driver");
    if (apiVersion) apiVersion->FromChar("4");
    if (driver) driver->FromChar(mIdentifier.Driver);
    if (driverVersion) {
        const uint32_t product = HIWORD(mIdentifier.DriverVersion.HighPart);
        const uint32_t version = LOWORD(mIdentifier.DriverVersion.HighPart);
        const uint32_t subVersion = HIWORD(mIdentifier.DriverVersion.LowPart);
        const uint32_t build = LOWORD(mIdentifier.DriverVersion.LowPart);
        // ORIGINAL-BUG: the GUID text is converted as 0xa0 UTF-16 units from a 0x50-unit buffer (it reads past it);
        // ours has room for all 0xa0. The braces are dropped by cutting the last character and skipping the first
        // (the original writes before its buffer when the text is empty; ours has a spare byte there).
        wchar_t wide[0xa0] = {};
        char narrowStore[1 + 0x50] = {};
        char *const narrow = narrowStore + 1;
        ImportSlot<int(WINAPI *)(REFGUID, LPOLESTR, int), &ts2_rva_d9a6d8>()(mIdentifier.DeviceIdentifier, wide, 0x50);
        uint32_t length = 0x50;
        RZText::Convert(wide, 0xa0, 16, narrow, &length, -2);
        narrow[std::strlen(narrow) - 1] = 0;
        driverVersion->Sprintf("%d.%d.%d.%d, GUID: %s", product, version, subVersion, build, narrow + 1);
    }
    if (description) description->FromChar(mIdentifier.Description);
    if (ids)
        ids->Sprintf("Vendor: %04x, Device: %04x, Board: %08x, Chipset: %04x", mIdentifier.VendorId,
                     mIdentifier.DeviceId, mIdentifier.SubSysId, mIdentifier.Revision);
}

cDisplayMode cDeviceEnumDX::GetAdapterDisplayMode()
{
    D3DDISPLAYMODE mode = {}; // ORIGINAL-BUG: the original's is stale when the question fails
    if (mDevice) {
        const HRESULT hr = mDevice->GetDisplayMode(0, &mode);
        if (hr < 0) Report(hr, kLineDeviceDisplayMode);
    } else {
        IDirect3D9 *const direct3d = Direct3DFor(D3DDEVTYPE_HAL);
        const HRESULT hr = direct3d->GetAdapterDisplayMode(mAdapter, &mode);
        if (hr < 0) Report(hr, kLineAdapterDisplayMode);
    }
    const float aspect = ModeAspect(mode.Width, mode.Height, RawSlot<6, float>(mOwner->GetDisplayMonitor()));
    cIPixelFormat *const format = FormatFor(mode.Format);
    DesktopFormat(); // asked and dropped
    return cDisplayMode{mode.Width, mode.Height, aspect, mode.RefreshRate, format};
}

cDisplayModeRange cDeviceEnumDX::GetDisplayModes(cIPixelFormat *filter)
{
    cCountedDisplayModes *const modes = new cCountedDisplayModes;
    modes->AddRef();
    modes->mValue.reserve(mModes.size());
    for (const cDisplayMode &mode : mModes)
        if (!filter || filter == mode.mFormat) modes->mValue.push_back(mode);
    cDisplayModeVector &list = modes->mValue;
    cDisplayModeIterator *const end = new cDisplayModeIterator(modes, list.data() + list.size());
    cDisplayModeIterator *const begin = new cDisplayModeIterator(modes, list.data());
    cDisplayModeRange out;
    cIDisplayModeIterator *const current = begin->Clone();
    out.mCurrent = current;
    if (current) current->AddRef();
    out.mBegin = begin;
    begin->AddRef();
    out.mEnd = end;
    end->AddRef();
    modes->Release();
    return out;
}

cPixelFormatRange cDeviceEnumDX::GetDisplayFormats()
{
    cIPixelFormatIterator *const iterator = NewFormatIterator();
    for (const cDisplayMode &mode : mModes) iterator->Insert(mode.mFormat);
    cPixelFormatRange out;
    FinishRange(&out, iterator);
    return out;
}

cIPixelFormat *cDeviceEnumDX::DisplayFormat(cIPixelFormat *desktop)
{
    const cDeviceSettings settings = mOwner->GetSettings();
    return IsDefaultMode(settings.mMode) ? desktop : settings.mMode.mFormat;
}

cPixelFormatRange cDeviceEnumDX::GetColorFormatsForDisplay(uint32_t usage, uint32_t iid)
{
    cIPixelFormat *const desktop = DesktopFormat();
    return GetColorFormats(usage, DisplayFormat(desktop), iid);
}

cPixelFormatRange cDeviceEnumDX::GetColorFormats(uint32_t usage, cIPixelFormat *displayFormat, uint32_t iid)
{
    cIPixelFormatIterator *const iterator = NewFormatIterator();
    iterator->SetFilter(iid);
    cPixelFormatRange candidates;
    RawSlot<3, cPixelFormatRange *>(FormatsManager(), &candidates, iid);
    while (!candidates.mCurrent->IsEqual(candidates.mEnd)) {
        if (IsColorFormatSupported(candidates.mCurrent->Get(), usage, displayFormat))
            iterator->Insert(candidates.mCurrent->Get());
        candidates.mCurrent->Next();
    }
    ReleaseRange(candidates);
    cPixelFormatRange out;
    FinishRange(&out, iterator);
    return out;
}

bool cDeviceEnumDX::IsColorFormatSupportedForDisplay(cIPixelFormat *format, uint32_t usage)
{
    cIPixelFormat *const desktop = DesktopFormat();
    return IsColorFormatSupported(format, usage, DisplayFormat(desktop));
}

bool cDeviceEnumDX::IsColorFormatSupported(cIPixelFormat *format, uint32_t usage, cIPixelFormat *displayFormat)
{
    if (format->GetClassId() == kPaletteClassId) return false;
    IDirect3D9 *const direct3d = Direct3DFor(mDeviceType);
    if (!direct3d) return false;
    cIPixelFormat *const desktop = DesktopFormat();
    cIPixelFormat *const display = displayFormat == desktop ? GetAdapterDisplayMode().mFormat : displayFormat;
    const D3DFORMAT displayId = static_cast<D3DFORMAT>(display->GetFormatId());
    const uint32_t id = format->GetFormatId();
    if (id == kFormatP8) return false;
    if (id == kFormatA8 && (usage == 7 || usage == 8)) return false;
    const D3DDEVTYPE type = static_cast<D3DDEVTYPE>(mDeviceType);
    if (usage == 0) {
        const uint32_t *const targets = ts2_rva_d9f498;
        uint32_t i = 0;
        while (i < kColorTargetCount && targets[i] != id) ++i;
        if (i == kColorTargetCount) return false;
        HRESULT hr = direct3d->CheckDeviceFormat(mAdapter, type, displayId,
                                                 D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
                                                 D3DRTYPE_SURFACE, static_cast<D3DFORMAT>(id));
        if (hr != 0) {
            ReportUnexpected(hr, kLineColorFormat);
            return false;
        }
        if (displayFormat == desktop && !mWindowedFormatConversion) return format == GetAdapterDisplayMode().mFormat;
        hr = direct3d->CheckDeviceType(mAdapter, type, displayId, static_cast<D3DFORMAT>(id), displayFormat == desktop);
        ReportUnexpected(hr, kLineColorType);
        return hr == 0;
    }
    DWORD d3dUsage;
    D3DRESOURCETYPE resource;
    UsagePair(usage, &d3dUsage, &resource);
    const HRESULT hr = direct3d->CheckDeviceFormat(mAdapter, type, displayId, d3dUsage, resource, static_cast<D3DFORMAT>(id));
    ReportUnexpected(hr, kLineUsageFormat);
    const bool textureUsage = usage == 4 || usage == 5 || usage == 6;
    if (hr == kNotAvailable && id == kFormatA8 && textureUsage) {
        // A8 textures can be served by P8 with an alpha palette.
        const HRESULT palette = direct3d->CheckDeviceFormat(mAdapter, type, displayId, d3dUsage, resource,
                                                            static_cast<D3DFORMAT>(kFormatP8));
        return mProcessingCaps[8] && palette == 0;
    }
    return hr == 0;
}

cPixelFormatRange cDeviceEnumDX::GetDepthFormatsForDisplay(uint32_t usage, cIPixelFormat *color)
{
    cIPixelFormat *const desktop = DesktopFormat();
    return GetDepthFormats(usage, DisplayFormat(desktop), color);
}

cPixelFormatRange cDeviceEnumDX::GetDepthFormats(uint32_t usage, cIPixelFormat *displayFormat, cIPixelFormat *color)
{
    cIPixelFormatIterator *const iterator = NewFormatIterator();
    iterator->SetFilter(kDepthStencilFormatIid);
    cPixelFormatRange candidates;
    RawSlot<3, cPixelFormatRange *>(FormatsManager(), &candidates, kDepthStencilFormatIid);
    while (!candidates.mCurrent->IsEqual(candidates.mEnd)) {
        if (IsDepthFormatSupported(candidates.mCurrent->Get(), usage, displayFormat, color))
            iterator->Insert(candidates.mCurrent->Get());
        candidates.mCurrent->Next();
    }
    ReleaseRange(candidates);
    cPixelFormatRange out;
    FinishRange(&out, iterator);
    return out;
}

bool cDeviceEnumDX::IsDepthFormatSupportedForDisplay(cIPixelFormat *depth, uint32_t usage, cIPixelFormat *color)
{
    cIPixelFormat *const desktop = DesktopFormat();
    return IsDepthFormatSupported(depth, usage, DisplayFormat(desktop), color);
}

bool cDeviceEnumDX::IsDepthFormatSupported(cIPixelFormat *depth, uint32_t usage, cIPixelFormat *displayFormat,
                                           cIPixelFormat *color)
{
    cIPixelFormat *const desktop = DesktopFormat();
    cIPixelFormat *const display = displayFormat == desktop ? GetAdapterDisplayMode().mFormat : displayFormat;
    const D3DFORMAT displayId = static_cast<D3DFORMAT>(display->GetFormatId());
    const D3DFORMAT colorId = static_cast<D3DFORMAT>(color->GetFormatId());
    const D3DFORMAT depthId = static_cast<D3DFORMAT>(depth->GetFormatId());
    D3DRESOURCETYPE resource;
    switch (usage) {
    case 0xb: resource = D3DRTYPE_SURFACE; break;
    case 0xc: resource = D3DRTYPE_TEXTURE; break;
    case 0xd: resource = D3DRTYPE_CUBETEXTURE; break;
    default:
        // ORIGINAL-BUG: other usages pass the display format argument's pointer as the resource type.
        resource = static_cast<D3DRESOURCETYPE>(reinterpret_cast<uintptr_t>(displayFormat));
        break;
    }
    IDirect3D9 *const direct3d = Direct3DFor(mDeviceType);
    if (!direct3d) return false;
    const D3DDEVTYPE type = static_cast<D3DDEVTYPE>(mDeviceType);
    HRESULT hr = direct3d->CheckDeviceFormat(mAdapter, type, displayId, D3DUSAGE_DEPTHSTENCIL, resource, depthId);
    if (hr != 0) {
        ReportUnexpected(hr, kLineDepthFormat);
        return false;
    }
    hr = direct3d->CheckDepthStencilMatch(mAdapter, type, displayId, colorId, depthId);
    ReportUnexpected(hr, kLineDepthMatch);
    return hr == 0;
}

int32_t cDeviceEnumDX::GetMultisampleQualitiesForDisplay(uint32_t type, cIPixelFormat *color)
{
    cIPixelFormat *const desktop = DesktopFormat();
    return GetMultisampleQualities(type, DisplayFormat(desktop), color);
}

int32_t cDeviceEnumDX::GetMultisampleQualities(uint32_t type, cIPixelFormat *displayFormat, cIPixelFormat *color)
{
    if (type == 0) return 0;
    if (!mEnumerateMultisampleLevels) return -1;
    IDirect3D9 *const direct3d = Direct3DFor(mDeviceType);
    if (!direct3d) return 0;
    cIPixelFormat *const desktop = DesktopFormat();
    const D3DFORMAT colorId = static_cast<D3DFORMAT>(color->GetFormatId());
    DWORD qualities = 0; // written by the runtime on success
    const HRESULT hr = direct3d->CheckDeviceMultiSampleType(mAdapter, static_cast<D3DDEVTYPE>(mDeviceType), colorId,
                                                            displayFormat == desktop,
                                                            static_cast<D3DMULTISAMPLE_TYPE>(type), &qualities);
    if (hr == 0) return static_cast<int32_t>(qualities - 1);
    ReportUnexpected(hr, kLineMultisample);
    return -1;
}

void **__fastcall HoldGraphicService(void **holder, void *)
{
    *holder = nullptr;
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kGraphicServiceId, kGraphicServiceIid, holder);
    return holder;
}

bool cDeviceEnumDX::CanStretchRect(uint32_t srcKind, const cSurfaceExtent *srcSize, const RECT *srcRect,
                                   cIPixelFormat *srcFormat, uint32_t dstKind, const cSurfaceExtent *dstSize,
                                   const RECT *dstRect, cIPixelFormat *dstFormat)
{
    RECT src = *srcRect;
    RECT dst = *dstRect;
    const RECT srcBounds = {0, 0, static_cast<LONG>(srcSize->mWidth), static_cast<LONG>(srcSize->mHeight)};
    const RECT dstBounds = {0, 0, static_cast<LONG>(dstSize->mWidth), static_cast<LONG>(dstSize->mHeight)};
    // Clips both rectangles to their surfaces; nothing left to copy is fine.
    if (!ClipCopyRects(&src, &srcBounds, &dst, &dstBounds)) return true;
    const uint32_t srcId = srcFormat->GetFormatId();
    const uint32_t dstId = dstFormat->GetFormatId();
    const D3DCAPS9 *const caps = GetCurrentCaps();
    const bool fromTextures = (caps->DevCaps2 & kStretchFromTextures) != 0;
    if (srcKind == kSurfaceKindTexture || dstKind == kSurfaceKindTexture) {
        return fromTextures && srcId == dstId && src.left == 0 && src.top == 0 &&
               static_cast<uint32_t>(src.right) == srcSize->mWidth &&
               static_cast<uint32_t>(src.bottom) == srcSize->mHeight && dst.left == 0 && dst.top == 0 &&
               static_cast<uint32_t>(dst.right) == dstSize->mWidth &&
               static_cast<uint32_t>(dst.bottom) == dstSize->mHeight && srcSize->mWidth == dstSize->mWidth &&
               srcSize->mHeight == dstSize->mHeight;
    }
    const bool stretching = (src.right - src.left) != (dst.right - dst.left) || (src.bottom - src.top) != (dst.bottom - dst.top);
    if (stretching && ((srcId >= kDxt1 && srcId <= kDxt5) || (dstId >= kDxt1 && dstId <= kDxt5))) return false;
    if (srcId != dstId) {
        IDirect3D9 *const direct3d = Direct3DFor(mDeviceType);
        if (!direct3d) return false;
        if (direct3d->CheckDeviceFormatConversion(mAdapter, static_cast<D3DDEVTYPE>(mDeviceType),
                                                  static_cast<D3DFORMAT>(srcId), static_cast<D3DFORMAT>(dstId)) < 0)
            return false;
    }
    // ORIGINAL-BUG: a source kind without a class (11, or past 13) takes the destination kind as its class, and a
    // destination's likewise; either can index past the 4x4 table.
    const int32_t srcClass = StretchClass(srcKind, dstKind);
    if (srcClass < 0) return false;
    const int32_t dstClass = StretchClass(dstKind, dstKind);
    if (dstClass < 0) return false;
    const uint8_t *const table = ts2_rva_d9f964[0][0] + (fromTextures ? 0x20 : 0) + (stretching ? 0x10 : 0);
    return table[srcClass * 4 + dstClass] != 0;
}

uint8_t cDeviceEnumDX::HasProcessingCap(uint32_t index) { return mProcessingCaps[index]; }

uint32_t cDeviceEnumDX::GetLimit(uint32_t index) { return mLimits[index]; }

cIGZPropertySetString *cDeviceEnumDX::GetProperties() { return &mProperties; }

const D3DCAPS9 *cDeviceEnumDX::GetCaps(bool hardware)
{
    if (hardware) return mHardwareCapsValid ? &mHardwareCaps : nullptr;
    return mSoftwareCapsValid ? &mSoftwareCaps : nullptr;
}

const D3DCAPS9 *cDeviceEnumDX::GetCurrentCaps()
{
    return mDevice->GetSoftwareVertexProcessing() ? &mSoftwareCaps : &mHardwareCaps;
}

uint8_t cDeviceEnumDX::HasFeature(uint32_t index) { return mFeatures[index]; }

uint32_t cDeviceEnumDX::GetSetting(uint32_t index) { return (&mMinTextureDimension)[index]; }

D3DADAPTER_IDENTIFIER9 cDeviceEnumDX::GetAdapterIdentifier() { return mIdentifier; }

uint32_t cDeviceEnumDX::GetTextureFormatForUsage(uint32_t usage)
{
    IDirect3D9 *const direct3d = Direct3DFor(mDeviceType);
    if (!direct3d) return 0;
    const cDeviceSettings settings = mOwner->GetSettings();
    const D3DFORMAT displayId = static_cast<D3DFORMAT>(
        IsDefaultMode(settings.mMode) ? GetAdapterDisplayMode().mFormat->GetFormatId() : settings.mMode.mFormat->GetFormatId());
    DWORD d3dUsage;
    D3DRESOURCETYPE resource;
    UsagePair(usage, &d3dUsage, &resource);
    const D3DDEVTYPE type = static_cast<D3DDEVTYPE>(mDeviceType);
    if (direct3d->CheckDeviceFormat(mAdapter, type, displayId, d3dUsage, resource, static_cast<D3DFORMAT>(kFormatA8)) == 0)
        return kFormatA8;
    if (!mProcessingCaps[8]) return 0;
    return direct3d->CheckDeviceFormat(mAdapter, type, displayId, d3dUsage, resource, static_cast<D3DFORMAT>(kFormatP8)) == 0
               ? kFormatP8
               : 0;
}

} // namespace nGZGraphic4
