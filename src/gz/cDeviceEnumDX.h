#pragma once
// nGZGraphic4::cDeviceEnumDX (RTTI cDeviceEnumDX; vftable 0xd9f758, 30 slots; 0x784 bytes): one adapter's Direct3D 9
// enumeration (lane NK, rendering campaign R2b; docs/subsystems/rendering.md). It holds the adapter identifier, the
// hardware and software caps, the display modes the adapter offers (a sorted set), the device type to create, and the
// capability, limit and feature tables cDeviceDX::CreateDevice and the render-state cache read; its property set
// (slot 22) holds the settings Refresh reads (forceSoftwareDevice, queriesOK, ...).
//
// Standalone shape (docs/standalone-shape.md): our class, our vtable. The game makes enumerations in one place, cDeviceDX
// Init (0x169cc0, ours): `new(0x784)` and the constructor 0x1774a0; Refresh 0x178b80 and the dump 0x1788f0 are called
// only from cDeviceDX members (ours). With those calling ours, the game's constructor, refresh and dump are in effect
// through their callers, the game's vtable 0xd9f758 is never stored, and every slot is in effect through callers.
//
// cDisplayModeIterator (vftable 0xd9f7e8, 9 slots; 0x10 bytes from the fixed pool 0xfed8f8) walks a counted vector of
// modes (cRZRefCountedT<std::vector<cDisplayMode>>, vftable 0xd9f7d4) that slot 7 fills; only slot 7 makes them.
//
// The layout is the original's (static_asserts below): the render-state cache and the tests read it by offset. The
// constructor leaves the identifier (when GetAdapterIdentifier fails), the caps and the tables unwritten, as the
// original does.
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include <d3d9.h>

#include "gz/cDeviceEnum.h"
#include "gz/cGZPropertySet.h"
#include "gz/cPixelFormatIterator.h" // cRZRefCountedT

#include "game/owned.h"
TS2_OWNED_STATIC(const char, d9f810)[]; // the source file of the class's reports (kSourceFileRva)
TS2_OWNED_STATIC(const uint32_t, d9f498)[6];      // the D3DFORMATs a usage-0 colour format must be one of
TS2_OWNED_STATIC(const uint32_t, d9f4b0)[11][2];  // per usage index: D3DUSAGE, D3DRESOURCETYPE
TS2_OWNED_STATIC(const uint32_t, d9f508)[5];      // the display formats Refresh enumerates modes of
TS2_OWNED_STATIC(const uint8_t, d9f964)[4][4][4]; // CanStretchRect's answers (see kStretchTable)
TS2_OWNED_STATIC(nGZGraphic4::cDisplayMode, 10ca3a0); // this unit's copy of the default display mode (kDefaultModeRva)

#include "game/owned.h"
#include "rz/RZFastFixedAllocators.h"

// The classes' fixed-size pools (operator new / delete).
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, fed8f8);

namespace nGZGraphic4 {

class cIDeviceDX;

// The display mode order (0x1778c0): format (cIPixelFormat slot 10, strict false, both ways), then width, height and
// refresh (unsigned), then aspect (false when either is a NaN).
struct cLessDisplayMode {
    bool operator()(const cDisplayMode &a, const cDisplayMode &b) const;
};

using cDisplayModeSet = std::set<cDisplayMode, cLessDisplayMode>;
using cDisplayModeVector = std::vector<cDisplayMode>;
// vftable 0xd9f7d4: 0x148350, 0x146a40, 0x148840, 0x177930 (the first three shared with other cRZRefCountedT instances).
using cCountedDisplayModes = cRZRefCountedT<cDisplayModeVector>;
static_assert(sizeof(cDisplayModeSet) == 8, "std::set: head and size");
static_assert(sizeof(cCountedDisplayModes) == 0x14, "cRZRefCountedT<vector<cDisplayMode>>: 0x14 bytes");

class cDisplayModeIterator : public cIDisplayModeIterator {
public:
    static constexpr uint32_t kSize = 0x10;
    static constexpr uint32_t kPool = 0xfed8f8; // the cRZFastFixedAllocator2<0x10> instance the original uses
    static rz::cRZFastFixedAllocator2<0x10> &Pool() { return ts2_rva_fed8f8; }

    // Count 0; the vector counted (when there is one); the cursor at `cursor`.
    cDisplayModeIterator(cCountedDisplayModes *modes, const cDisplayMode *cursor);

    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);

    bool QueryInterface(uint32_t riid, void **ppvObj) override; // slot 0 (shared 0x178b20): only 1; null out: false
    uint32_t AddRef() override;                                 // slot 1 (shared 0x148fb0)
    uint32_t Release() override;                                // slot 2 (shared 0x178b60): slot 8 at 0
    cIDisplayModeIterator *Clone() override;                    // slot 3 (0x177ae0)
    bool IsEqual(const cIDisplayModeIterator *other) override;  // slot 4 (shared 0x178550): the cursors
    cDisplayMode Get() override;                                // slot 5 (0x177bd0)
    void Next() override;                                       // slot 6 (0x1786a0)
    void Prev() override;                                       // slot 7 (0x1788e0)
    ~cDisplayModeIterator() override;                           // slot 8 (0x1779a0)

private:
    uint32_t mRefCount;          // +0x04
    cCountedDisplayModes *mModes; // +0x08: counted
    const cDisplayMode *mCursor; // +0x0c
};
static_assert(sizeof(cDisplayModeIterator) == 0x10, "cDisplayModeIterator: 0x10 bytes");

class cDeviceEnumDX : public cIDeviceEnum {
public:
    static constexpr uint32_t kSourceFileRva = 0xd9f810; // LogD3DError's file (GZGraphic4DeviceEnumDX.cpp)
    static constexpr uint32_t kDefaultModeRva = 0x10ca3a0; // this unit's copy of the default display mode

    // 0x1774a0 (thiscall, ret 8): the adapter's identifier and its video memory (a probe device on a second
    // IDirect3D9), the type HAL, no modes yet.
    cDeviceEnumDX(uint32_t adapter, cIDeviceDX *owner);

    // 0x178b80 (thiscall, ret 8): reads the modes (once per device type), the caps and the tables for `device` (null:
    // none yet) and the cDeviceDX flags; gives the device type to create.
    uint32_t Refresh(IDirect3DDevice9 *device, uint32_t flags);
    // 0x1788f0 (thiscall, ret 4): the adapter's name, driver version, ids and WHQL level, as text.
    void DumpInfo(cIGZUnknown *stream);
    // 0x178560 (cdecl): caps as the engine uses them (Refresh's): `noVertexShaders` clears the vertex shader fields
    // of a device with vertex but no pixel shaders, `noProjection` drops projected textures; counts clamped (4 targets,
    // 8 stages, 16 textures, 16 streams, 0x400 constants, 16 lights); the power-of-two texture flags made consistent.
    static void ClampCaps(D3DCAPS9 *caps, bool noVertexShaders, bool noProjection);

    bool QueryInterface(uint32_t riid, void **ppvObj) override; // slot 0 (0x178af0): 1, 0x6c02c311, 0xc9da7e9f
    uint32_t AddRef() override;                                 // slot 1 (shared 0x148fb0)
    uint32_t Release() override;                                // slot 2 (shared 0x178b40): slot 29 at 0
    cIGZUnknown *GetDevice() override;
    cIGZUnknown *GetDeviceDX() override;
    void GetDescription(cIGZString *api, cIGZString *apiVersion, cIGZString *driver, cIGZString *driverVersion,
                        cIGZString *description, cIGZString *ids) override;
    cDisplayMode GetAdapterDisplayMode() override;
    cDisplayModeRange GetDisplayModes(cIPixelFormat *filter) override;
    cPixelFormatRange GetDisplayFormats() override;
    cPixelFormatRange GetColorFormatsForDisplay(uint32_t usage, uint32_t iid) override;
    cPixelFormatRange GetColorFormats(uint32_t usage, cIPixelFormat *displayFormat, uint32_t iid) override;
    bool IsColorFormatSupportedForDisplay(cIPixelFormat *format, uint32_t usage) override;
    bool IsColorFormatSupported(cIPixelFormat *format, uint32_t usage, cIPixelFormat *displayFormat) override;
    cPixelFormatRange GetDepthFormatsForDisplay(uint32_t usage, cIPixelFormat *color) override;
    cPixelFormatRange GetDepthFormats(uint32_t usage, cIPixelFormat *displayFormat, cIPixelFormat *color) override;
    bool IsDepthFormatSupportedForDisplay(cIPixelFormat *depth, uint32_t usage, cIPixelFormat *color) override;
    bool IsDepthFormatSupported(cIPixelFormat *depth, uint32_t usage, cIPixelFormat *displayFormat,
                                cIPixelFormat *color) override;
    int32_t GetMultisampleQualitiesForDisplay(uint32_t type, cIPixelFormat *color) override;
    int32_t GetMultisampleQualities(uint32_t type, cIPixelFormat *displayFormat, cIPixelFormat *color) override;
    bool CanStretchRect(uint32_t srcKind, const cSurfaceExtent *srcSize, const RECT *srcRect, cIPixelFormat *srcFormat,
                        uint32_t dstKind, const cSurfaceExtent *dstSize, const RECT *dstRect,
                        cIPixelFormat *dstFormat) override;
    uint8_t HasProcessingCap(uint32_t index) override;
    uint32_t GetLimit(uint32_t index) override;
    cIGZPropertySetString *GetProperties() override;
    const D3DCAPS9 *GetCaps(bool hardware) override;
    const D3DCAPS9 *GetCurrentCaps() override;
    uint8_t HasFeature(uint32_t index) override;
    uint32_t GetSetting(uint32_t index) override;
    D3DADAPTER_IDENTIFIER9 GetAdapterIdentifier() override;
    uint32_t GetTextureFormatForUsage(uint32_t usage) override;
    ~cDeviceEnumDX() override; // slot 29 (0x177960): the set, then the property set

private:
    friend struct cDeviceEnumDXLayout;

    // The display format for the slots "for the display": the settings' mode format, or the desktop's (slot 6) when
    // the owner's mode is the default. `desktop` is the formats manager's unknown format, asked first.
    cIPixelFormat *DisplayFormat(cIPixelFormat *desktop);
    uint8_t BoolProperty(const char *key, uint8_t fallback);
    uint32_t Uint32Property(const char *key, uint32_t fallback);

    uint32_t mRefCount;                       // +0x004
    uint32_t mAdapter;                        // +0x008
    cIDeviceDX *mOwner;                       // +0x00c: not counted
    uint32_t mVideoMemory;                    // +0x010: the probe device's GetAvailableTextureMem (0: none)
    D3DADAPTER_IDENTIFIER9 mIdentifier;       // +0x014
    uint32_t mDeviceType;                     // +0x460: D3DDEVTYPE (1 HAL, 2 REF, 3 SW)
    IDirect3DDevice9 *mDevice;                // +0x464: the device of the last Refresh (not counted)
    D3DCAPS9 mHardwareCaps;                   // +0x468
    D3DCAPS9 mSoftwareCaps;                   // +0x598
    uint8_t mHardwareCapsValid;               // +0x6c8
    uint8_t mSoftwareCapsValid;               // +0x6c9
    uint8_t mPad6ca[2];                       // +0x6ca: never written
    cGZPropertySetString mProperties;         // +0x6cc
    cDisplayModeSet mModes;                   // +0x6f0
    // +0x6f8: HasProcessingCap's: 0 / 2 hardware fixed-function / shader vertex processing, 1 / 3 software ones, 4 UBYTE4
    // declarations, 5 either software one, 6-10 caps bits (DevCaps bit 20, TextureCaps bits 11, 7, !1, 8).
    uint8_t mProcessingCaps[0xb];
    uint8_t mPad703;                          // +0x703: never written
    uint32_t mLimits[0x1b];                   // +0x704: video memory first, then the caps-derived limits
    uint8_t mFeatures[7];                     // +0x770: HasFeature's
    uint8_t mPad777;                          // +0x777: never written
    uint32_t mMinTextureDimension;            // +0x778: GetSetting(0)
    uint8_t mForceSoftwareVertexProcessing;   // +0x77c
    uint8_t mStreamOffsetCap;                 // +0x77d: DevCaps2 bit 0 (no device only), the next two's default
    uint8_t mWindowedFormatConversion;        // +0x77e
    uint8_t mEnumerateMultisampleLevels;      // +0x77f
    uint8_t mDisableTextureProjection;        // +0x780
    uint8_t mPad781[3];                       // +0x781
};

struct cDeviceEnumDXLayout {
    static_assert(offsetof(cDeviceEnumDX, mIdentifier) == 0x14, "identifier");
    static_assert(offsetof(cDeviceEnumDX, mDeviceType) == 0x460, "device type");
    static_assert(offsetof(cDeviceEnumDX, mHardwareCaps) == 0x468, "hardware caps");
    static_assert(offsetof(cDeviceEnumDX, mSoftwareCaps) == 0x598, "software caps");
    static_assert(offsetof(cDeviceEnumDX, mProperties) == 0x6cc, "property set");
    static_assert(offsetof(cDeviceEnumDX, mModes) == 0x6f0, "modes");
    static_assert(offsetof(cDeviceEnumDX, mProcessingCaps) == 0x6f8, "processing caps");
    static_assert(offsetof(cDeviceEnumDX, mLimits) == 0x704, "limits");
    static_assert(offsetof(cDeviceEnumDX, mFeatures) == 0x770, "features");
    static_assert(offsetof(cDeviceEnumDX, mMinTextureDimension) == 0x778, "settings");
    static_assert(offsetof(cDeviceEnumDX, mDisableTextureProjection) == 0x780, "texture projection");
};
static_assert(sizeof(cDeviceEnumDX) == 0x784, "cDeviceEnumDX: 0x784 bytes");

// The entry point cDeviceDX::Init makes enumerations through (a function of its own, so the tests can stand in for it).
struct cDeviceEnumDXAdapters {
    // 0x1774a0's work on `memory` (0x784 bytes): placement construction.
    static cDeviceEnumDX *__fastcall Construct(void *memory, void *, uint32_t adapter, cIDeviceDX *owner);
};

// 0x1773b0 (thiscall, no arguments; `holder` is this; lane WI): the graphic service's holder. Clears *holder, then, when
// there is a framework (GetServiceObject), asks its slot 5 for the service 0x85522803 with the interface 0xc55289ba
// into *holder. Returns holder. The game's one caller is CanStretchRect 0x17a5d0 (its IDirect3D9 for a format
// conversion); ours (Direct3DFor in cDeviceEnumDX.cpp) holds the service through it.
void **__fastcall HoldGraphicService(void **holder, void *);

} // namespace nGZGraphic4
