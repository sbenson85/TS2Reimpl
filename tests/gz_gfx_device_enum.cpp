// Differential tests for nGZGraphic4::cDeviceEnumDX, cDisplayModeIterator and the counted mode vector (lane NK,
// rendering campaign R2b; src/gz/cDeviceEnumDX.h), against the campaign's fake Direct3D (tests/d3dfx.h).
//
// One world per case, built alike for each side from the case's seed:
// - The enumeration is a heap block filled with the case's bytes, then constructed by the image's constructor 0x1774a0
//   (the original's side: the game's vtable, the game's property set) or ours; every slot runs through the object's
//   own vtable, Refresh 0x178b80 and the dump 0x1788f0 are called at their RVAs or as our members.
// - The Direct3D objects are d3dfx fakes: the IDirect3D9 the graphic service hands out (slot 25(type)), the probe
//   IDirect3D9 Direct3DCreate9 makes and its device, and the device Refresh is given. Their answers come from the case:
//   the identifier, mode counts and modes, caps (per device type, and per software / hardware processing on the
//   device), creation parameters; the Check* results are a hash of the call's (normalised) arguments and the case's
//   salt, so every combination is answered the same on both sides. The out-parameters of GetDeviceCaps,
//   EnumAdapterModes, GetAdapterDisplayMode, GetDisplayMode and GetCreationParameters are written on failure too: the
//   original reads its stale stack copy then (ORIGINAL-BUG comments in the source), which no test can compare.
// - Engine objects are drawfx fakes: the owner (cIDeviceDX slots 33 GetSettings, 42 GetDisplayMonitor), the monitor
//   (slot 6, the pixel aspect), the graphic service (25, 26 whether a type is usable, 30 LogD3DError's question), the
//   pixel formats manager (0x18acd0 stood in on both sides: slot 3 the candidates of an interface as a range, 4 a
//   format by id, 5 the unknown format), the COM director (the framework getter 0x1ae18d stood in; its GetClassObject
//   hands out fresh iterator fakes), the pixel formats (3 id, 5 class, 10 Less by a seeded key, so the mode set's order
//   is compared call by call), pixel format iterators (lists of formats, cursors), the output stream (7, 9, 14).
//   cIGZString arguments are hand-made fakes (slot 4 FromChar, slot 31 the varargs Sprintf) that note their text.
// - RZText::Convert (0x1afd58, verified by tests/rz_text.cpp) is stood in on both sides by a recorder that narrows
//   the units it may read: the code-page encoding (-2) would call GetACP through the image's import table.
// - The display mode iterators come from a fake fixed pool at 0xfed8f8 (three pages, rebuilt per side, so both sides'
//   iterators sit at the same addresses); operator new / delete are the heap's on both sides.
// - The properties Refresh reads are set through slot 22 (the side's own property set) from the case.
// Compared: the log (every call on every fake with its arguments, LogD3DError's reports, the stand-ins' notes), every
// result (pointers into the object by offset, fakes by id), every byte of the object but its vtable word, the property
// set (+0x6cc: heap pointers) and the mode set's head pointer (+0x6f0), the mode set walked in order, the pool's pages,
// every fake's reference count and the heap blocks and bytes allocated over the case.
//
// A GUID text that StringFromGUID2 leaves empty is not generated: the original then writes before its narrow buffer
// on its stack (ORIGINAL-BUG in GetDescription), which would corrupt the test's own frame.
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"
#include "gz/GraphicServices.h"
#include "free_recorder.h"
#include "impfx.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnumDX.h"
#include "gz/cGZAny.h"
#include "rz/RZFixedAllocator.h"
#include "rz/RZTextConvert.h"
#include "rz/StaticGetters.h"

#include <windows.h>
#include <objbase.h>

#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

constexpr uint32_t kGameVTable = 0xd9f758;    // cDeviceEnumDX
constexpr uint32_t kModeIterVTable = 0xd9f7e8; // cDisplayModeIterator
constexpr uint32_t kCountedVTable = 0xd9f7d4; // cRZRefCountedT<vector<cDisplayMode>>
constexpr uint32_t kOriginalConstruct = 0x1774a0;
constexpr uint32_t kOriginalRefresh = 0x178b80;
constexpr uint32_t kOriginalDump = 0x1788f0;
constexpr uint32_t kOriginalClamp = 0x178560;
constexpr uint32_t kOriginalLess = 0x1778c0; // the mode set's comparator: reached through Refresh's inserts
constexpr uint32_t kPoolRva = cDisplayModeIterator::kPool;
constexpr uint32_t kModeFormatsRva = 0xd9f508;
constexpr uint32_t kSize = sizeof(cDeviceEnumDX);

constexpr uint32_t kObjectTag = 0x7e000000;
constexpr uint32_t kNotAvailable = 0x8876086a;

// ---- the fakes' interfaces ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, drawfx::Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kGraphicSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "GetDirect3D", "u"},
    {1, drawfx::Ret::Int, true, "IsDeviceTypeUsable", "u"},
    U, U, U,
    {1, drawfx::Ret::Int, true, "Slot30", "u"},
};
inline constexpr drawfx::Iface kGraphic{"graphic service", 31, kGraphicSlots};

inline constexpr drawfx::Slot kOwnerSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "GetSettings", "i"},
    U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetDisplayMonitor", ""},
};
inline constexpr drawfx::Iface kOwner{"cIDeviceDX", 43, kOwnerSlots};

inline constexpr drawfx::Slot kMonitorSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, {0, drawfx::Ret::Float, true, "GetPixelAspect", ""},
};
inline constexpr drawfx::Iface kMonitor{"display monitor", 7, kMonitorSlots};

inline constexpr drawfx::Slot kManagerSlots[] = {
    kQI, kAddRef, kRelease,
    {2, drawfx::Ret::Int, true, "GetFormats", "iu"},
    {1, drawfx::Ret::Int, true, "GetFormat", "u"},
    {0, drawfx::Ret::Int, true, "GetUnknownFormat", ""},
};
inline constexpr drawfx::Iface kManager{"cIPixelFormatsManager", 6, kManagerSlots};

inline constexpr drawfx::Slot kFormatSlots[] = {
    kQI, kAddRef, kRelease,
    {0, drawfx::Ret::Int, true, "GetFormatId", ""},
    U,
    {0, drawfx::Ret::Int, true, "GetClassId", ""},
    U, U, U, U,
    {2, drawfx::Ret::Int, true, "Less", "ib"},
};
inline constexpr drawfx::Iface kFormat{"cIPixelFormat", 11, kFormatSlots};

inline constexpr drawfx::Slot kIterSlots[] = {
    kQI, kAddRef, kRelease,
    {0, drawfx::Ret::Int, true, "Clone", ""},
    {1, drawfx::Ret::Int, true, "IsEqual", "i"},
    {0, drawfx::Ret::Int, true, "Get", ""},
    {0, drawfx::Ret::Int, true, "Next", ""},
    {0, drawfx::Ret::Int, true, "Prev", ""},
    {0, drawfx::Ret::Int, true, "CloneFormatIterator", ""},
    {0, drawfx::Ret::Int, true, "SetToBegin", ""},
    {0, drawfx::Ret::Int, true, "SetToEnd", ""},
    {1, drawfx::Ret::Int, true, "SetFilter", "u"},
    {1, drawfx::Ret::Int, true, "Insert", "i"},
    {0, drawfx::Ret::Int, true, "GetFilter", ""},
};
inline constexpr drawfx::Iface kIter{"cIPixelFormatIterator", 14, kIterSlots};

inline constexpr drawfx::Slot kComSlots[] = {
    kQI, kAddRef, kRelease, {3, drawfx::Ret::Int, true, "GetClassObject", "uui"},
};
inline constexpr drawfx::Iface kCom{"cIGZCOM", 4, kComSlots};

inline constexpr drawfx::Slot kStreamSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U,
    {1, drawfx::Ret::Int, true, "SetUint16", "u"},
    U,
    {1, drawfx::Ret::Int, true, "SetUint32", "u"},
    U, U, U, U,
    {1, drawfx::Ret::Int, true, "SetRZCharStr", "s"},
};
inline constexpr drawfx::Iface kStream{"cIGZOStream", 15, kStreamSlots};

// ---- ids ----
constexpr unsigned kFormatCount = 18;
constexpr unsigned kIterMax = 3000;
enum : uint32_t {
    kD3DId = 0xd3d00001, kProbeId, kDeviceId, kProbeDeviceId,
    kGraphicId = 0xe0000001, kOwnerId, kMonitorId, kManagerId, kComId, kStreamId, kUnknownFormatId,
    kFormatBase = 0xf0000000, kIterBase = 0xf1000000,
};
constexpr uint32_t kDesktopWindow = 0x5d000001;

// ---- a case ----
struct Prop {
    const char *key;
    bool isUint;
};
constexpr Prop kProps[] = {
    {"forceSoftwareDevice", false},
    {"softwareDeviceLocalVideoMemorySize", true},
    {"deviceLocalVideoMemorySizeDefault", true},
    {"forceSoftwareVP", false},
    {"fixedFunctionHWVertexProcessingOK", false},
    {"queriesOK", false},
    {"skipValidateDevice", false},
    {"enableDriverMemoryManager", false},
    {"disableVSyncSupport", false},
    {"disableTexMemEstimateAdjustment", false},
    {"createNVidiaWorkaroundTexture", false},
    {"substituteFlipForDiscardSwapEffect", false},
    {"windowedModeFormatConversionOK", false},
    {"enumerateMultisampleLevels", false},
    {"disableTextureProjection", false},
    {"minTextureDimensionForBalance", true},
};
constexpr unsigned kPropCount = sizeof(kProps) / sizeof(kProps[0]);

enum class Mode { Construct, Refresh, Slots, Dump };

struct Case {
    uint32_t seed;
    uint32_t adapter;
    uint8_t pattern[kSize];
    D3DADAPTER_IDENTIFIER9 ident;
    uint32_t identHr;
    uint32_t probeCreateHr, textureMem;
    float aspect;
    uint8_t usable[4];
    uint8_t nullD3D[4];
    uint32_t formatIds[kFormatCount];
    uint32_t classIds[kFormatCount];
    uint32_t keys[kFormatCount];
    std::vector<uint32_t> colorList, depthList; // candidate format indices
    uint32_t modeCount[5];
    D3DDISPLAYMODE modes[5][6];
    uint32_t enumHr[5][6];
    uint32_t salt;
    D3DCAPS9 caps[4];          // GetDeviceCaps per device type (index type & 3)
    D3DCAPS9 deviceCaps[2];    // the device's: hardware, software processing
    uint32_t capsHr, deviceCapsHr;
    uint32_t behavior, creationHr, svpHr[2];
    D3DDISPLAYMODE desktop;
    uint32_t desktopHr, deviceModeHr;
    uint32_t qualities;
    cDisplayMode defaultMode;  // written to 0x10ca3a0
    bool settingsDefault;
    cDeviceSettings settings;  // the owner's (format pointers set per side: indices below)
    int settingsFormat;
    bool propSet[kPropCount];
    uint32_t propValue[kPropCount];
    unsigned refreshes;
    bool refreshDevice[3];
    uint32_t refreshFlags[3];
    uint32_t poolFree;
    bool release;
    uint32_t opSeed;
    unsigned ops;
};

// ---- the world's fakes (static: the same addresses on both sides) ----
drawfx::Fake gD3D, gProbe, gDevice, gProbeDevice, gGraphicFake, gOwner, gMonitor, gManager, gCom, gStreamFake, gUnknown;
drawfx::Fake gFormats[kFormatCount];
drawfx::Fake gIters[kIterMax];
unsigned gNextIter;
bool gIterOverflow;
std::vector<std::vector<uint32_t>> gLists; // the iterator fakes' lists (format pointers)
const Case *gCase;
bool gOpsPhase; // slot 25 may answer null only after Refresh

using drawfx::P;
template <class T> T *At(uint32_t word) { return reinterpret_cast<T *>(uintptr_t(word)); }

// The Check* answers: a hash of the slot, the normalised arguments and the salt.
uint32_t Hash(uint32_t slot, const uint32_t *a, unsigned n)
{
    uint32_t h = 0x811c9dc5u ^ gCase->salt ^ slot * 0x9e3779b9u;
    for (unsigned i = 0; i < n; ++i) {
        h ^= drawfx::Norm(a[i]);
        h *= 0x01000193u;
        h ^= h >> 15;
    }
    return h;
}
uint32_t CheckAnswer(uint32_t slot, const uint32_t *a, unsigned n)
{
    const uint32_t h = Hash(slot, a, n) % 20;
    if (h < 11) return 0;
    if (h < 15) return kNotAvailable;
    if (h < 17) return 0x8876086c;
    if (h < 18) return 1;
    return 0x80004005;
}

// ---- iterator fakes ----
void SetIterHandlers(drawfx::Fake &it);
drawfx::Fake *NewIter(uint32_t list, uint32_t cursor)
{
    if (gNextIter >= kIterMax) {
        gIterOverflow = true;
        gNextIter = kIterMax - 1;
    }
    drawfx::Fake &it = gIters[gNextIter];
    {
        difftest::NotCounted quiet;
        it.Reset<kIter>(kIterBase + gNextIter);
    }
    ++gNextIter;
    SetIterHandlers(it);
    it.value[0] = list;
    it.value[1] = cursor;
    return &it;
}
uint32_t NewList()
{
    difftest::NotCounted quiet;
    gLists.emplace_back();
    return uint32_t(gLists.size() - 1);
}
std::vector<uint32_t> &ListOf(const drawfx::Fake &it) { return gLists[it.value[0]]; }

uint32_t IterClone(drawfx::Fake &self, const uint32_t *) { return P(NewIter(self.value[0], self.value[1])); }
uint32_t IterIsEqual(drawfx::Fake &self, const uint32_t *a)
{
    return a[0] && At<drawfx::Fake>(a[0])->value[1] == self.value[1];
}
uint32_t IterGet(drawfx::Fake &self, const uint32_t *)
{
    const std::vector<uint32_t> &list = ListOf(self);
    return self.value[1] < list.size() ? list[self.value[1]] : 0;
}
uint32_t IterNext(drawfx::Fake &self, const uint32_t *)
{
    ++self.value[1];
    return 0;
}
uint32_t IterPrev(drawfx::Fake &self, const uint32_t *)
{
    --self.value[1];
    return 0;
}
uint32_t IterBegin(drawfx::Fake &self, const uint32_t *)
{
    self.value[1] = 0;
    return 0;
}
uint32_t IterEnd(drawfx::Fake &self, const uint32_t *)
{
    self.value[1] = uint32_t(ListOf(self).size());
    return 0;
}
uint32_t IterSetFilter(drawfx::Fake &self, const uint32_t *a)
{
    self.value[2] = a[0];
    return 0;
}
uint32_t IterInsert(drawfx::Fake &self, const uint32_t *a)
{
    difftest::NotCounted quiet;
    ListOf(self).push_back(a[0]);
    return 0;
}
uint32_t IterGetFilter(drawfx::Fake &self, const uint32_t *) { return self.value[2]; }

void SetIterHandlers(drawfx::Fake &it)
{
    it.On(3, &IterClone);
    it.On(4, &IterIsEqual);
    it.On(5, &IterGet);
    it.On(6, &IterNext);
    it.On(7, &IterPrev);
    it.On(8, &IterClone);
    it.On(9, &IterBegin);
    it.On(10, &IterEnd);
    it.On(11, &IterSetFilter);
    it.On(12, &IterInsert);
    it.On(13, &IterGetFilter);
}

// ---- engine fakes' handlers ----
uint32_t GraphicDirect3D(drawfx::Fake &, const uint32_t *a)
{
    // Null only for the reference and software types: the HAL one is also asked unchecked (GetAdapterDisplayMode).
    if (gOpsPhase && a[0] != D3DDEVTYPE_HAL && gCase->nullD3D[a[0] & 3]) return 0;
    return P(&gD3D);
}
uint32_t GraphicUsable(drawfx::Fake &, const uint32_t *a) { return gCase->usable[a[0] & 3]; }

uint32_t OwnerSettings(drawfx::Fake &, const uint32_t *a)
{
    cDeviceSettings s = gCase->settings;
    if (gCase->settingsDefault) {
        s.mMode = gCase->defaultMode;
    } else {
        s.mMode.mFormat = gCase->settingsFormat < 0 ? reinterpret_cast<cIPixelFormat *>(&gUnknown)
                                                    : reinterpret_cast<cIPixelFormat *>(&gFormats[gCase->settingsFormat]);
    }
    std::memcpy(At<void>(a[0]), &s, sizeof(s));
    return a[0];
}
uint32_t OwnerMonitor(drawfx::Fake &, const uint32_t *) { return P(&gMonitor); }

drawfx::Fake *FormatById(uint32_t id)
{
    for (drawfx::Fake &f : gFormats)
        if (f.value[0] == id) return &f;
    return &gUnknown;
}
uint32_t ManagerFormat(drawfx::Fake &, const uint32_t *a) { return P(FormatById(a[0])); }
uint32_t ManagerUnknown(drawfx::Fake &, const uint32_t *) { return P(&gUnknown); }
uint32_t ManagerFormats(drawfx::Fake &, const uint32_t *a)
{
    const uint32_t list = NewList();
    const std::vector<uint32_t> &from = a[1] == 0x5ac389b6 ? gCase->depthList : gCase->colorList;
    {
        difftest::NotCounted quiet;
        for (uint32_t i : from)
            if (a[1] == 0x5ac389b6 || ((gCase->classIds[i] ^ a[1]) & 1) == 0) gLists[list].push_back(P(&gFormats[i]));
    }
    drawfx::Fake *range[3] = {NewIter(list, 0), NewIter(list, 0), NewIter(list, uint32_t(gLists[list].size()))};
    for (drawfx::Fake *f : range) ++f->refs;
    std::memcpy(At<void>(a[0]), range, sizeof(range));
    return a[0];
}

uint32_t FormatLess(drawfx::Fake &self, const uint32_t *a) { return self.value[2] < At<drawfx::Fake>(a[0])->value[2]; }
uint32_t FormatId(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t FormatClass(drawfx::Fake &self, const uint32_t *) { return self.value[1]; }

uint32_t ComClassObject(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Fake *it = NewIter(NewList(), 0);
    ++it->refs;
    if (a[2]) *At<void *>(a[2]) = it;
    return 1;
}

// ---- Direct3D handlers ----
uint32_t D3DIdentifier(drawfx::Fake &, const uint32_t *a)
{
    if (gCase->identHr == 0) std::memcpy(At<void>(a[2]), &gCase->ident, sizeof(gCase->ident));
    return gCase->identHr;
}
int ModeFormatIndex(uint32_t format)
{
    const uint32_t *formats = GameAddress<uint32_t>(kModeFormatsRva);
    for (int i = 0; i < 5; ++i)
        if (formats[i] == format) return i;
    return -1;
}
uint32_t D3DModeCount(drawfx::Fake &, const uint32_t *a)
{
    const int f = ModeFormatIndex(a[1]);
    return f < 0 ? 0 : gCase->modeCount[f];
}
uint32_t D3DEnumMode(drawfx::Fake &, const uint32_t *a)
{
    const int f = ModeFormatIndex(a[1]);
    const uint32_t i = a[2] < 6 ? a[2] : 5;
    D3DDISPLAYMODE mode = f < 0 ? D3DDISPLAYMODE{1, 2, 3, D3DFMT_UNKNOWN} : gCase->modes[f][i];
    std::memcpy(At<void>(a[3]), &mode, sizeof(mode));
    return f < 0 ? 0x8876086c : gCase->enumHr[f][i];
}
uint32_t D3DDisplayMode(drawfx::Fake &, const uint32_t *a)
{
    std::memcpy(At<void>(a[1]), &gCase->desktop, sizeof(gCase->desktop));
    return gCase->desktopHr;
}
uint32_t D3DCheck5(drawfx::Fake &, const uint32_t *a) { return CheckAnswer(9, a, 5); }
uint32_t D3DCheckFormat(drawfx::Fake &, const uint32_t *a) { return CheckAnswer(10, a, 6); }
uint32_t D3DCheckMultisample(drawfx::Fake &, const uint32_t *a)
{
    const uint32_t hr = CheckAnswer(11, a, 5);
    if (hr == 0 && a[5]) *At<DWORD>(a[5]) = gCase->qualities;
    return hr;
}
uint32_t D3DCheckDepth(drawfx::Fake &, const uint32_t *a) { return CheckAnswer(12, a, 5); }
uint32_t D3DCheckConversion(drawfx::Fake &, const uint32_t *a) { return CheckAnswer(13, a, 4); }
uint32_t D3DCaps(drawfx::Fake &, const uint32_t *a)
{
    std::memcpy(At<void>(a[2]), &gCase->caps[a[1] & 3], sizeof(D3DCAPS9));
    return gCase->capsHr;
}
uint32_t D3DMonitor(drawfx::Fake &, const uint32_t *a) { return 0x4d000000u | a[0]; }
uint32_t ProbeCreate(drawfx::Fake &, const uint32_t *a)
{
    // The present parameters, by content.
    const uint32_t *params = At<uint32_t>(a[4]);
    drawfx::Note("present parameters", {params[0], params[1], params[2], params[3], params[4], params[5], params[6],
                                        drawfx::Norm(params[7]), params[8], params[9], params[10], params[11],
                                        params[12], params[13]});
    if (int32_t(gCase->probeCreateHr) >= 0) { // a success (S_FALSE too) hands out the device
        ++gProbeDevice.refs;
        *At<void *>(a[5]) = &gProbeDevice;
    }
    return gCase->probeCreateHr;
}
uint32_t ProbeTextureMem(drawfx::Fake &, const uint32_t *) { return gCase->textureMem; }

uint32_t DeviceCreation(drawfx::Fake &, const uint32_t *a)
{
    D3DDEVICE_CREATION_PARAMETERS p{gCase->adapter, D3DDEVTYPE_HAL, reinterpret_cast<HWND>(uintptr_t(kDesktopWindow)),
                                    gCase->behavior};
    std::memcpy(At<void>(a[0]), &p, sizeof(p));
    return gCase->creationHr;
}
uint32_t DeviceSetSoftware(drawfx::Fake &self, const uint32_t *a)
{
    self.value[0] = a[0] != 0;
    return gCase->svpHr[self.value[0]];
}
uint32_t DeviceGetSoftware(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t DeviceCaps(drawfx::Fake &self, const uint32_t *a)
{
    std::memcpy(At<void>(a[0]), &gCase->deviceCaps[self.value[0] & 1], sizeof(D3DCAPS9));
    return gCase->deviceCapsHr;
}
uint32_t DeviceMode(drawfx::Fake &, const uint32_t *a)
{
    D3DDISPLAYMODE m = gCase->desktop;
    m.Width ^= 0x10;
    std::memcpy(At<void>(a[1]), &m, sizeof(m));
    return gCase->deviceModeHr;
}

// ---- imports ----
uint32_t ImpCreate9(drawfx::Fake &, const uint32_t *) { return P(&gProbe); }
uint32_t ImpGuidText(drawfx::Fake &, const uint32_t *a)
{
    const GUID &g = *At<GUID>(a[0]);
    wchar_t *out = At<wchar_t>(a[1]);
    const unsigned cch = a[2];
    for (unsigned i = 0; i < cch; ++i) out[i] = 0;
    wchar_t text[64];
    const int n = swprintf(text, 64, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", unsigned(g.Data1),
                           unsigned(g.Data2), unsigned(g.Data3), g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
                           g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    for (int i = 0; i < n && unsigned(i) + 1 < cch; ++i) out[i] = text[i];
    return uint32_t(n + 1);
}

// ---- stand-ins ----
void *__cdecl StandFramework() { return &gCom; }
void *__cdecl StandFormats() { return &gManager; }
uint32_t __cdecl StandConvert(const void *src, uint32_t srcLength, int32_t srcEncoding, void *dst, uint32_t *dstLength,
                              int32_t dstEncoding)
{
    const uint32_t n = srcLength < *dstLength ? srcLength : *dstLength;
    const uint16_t *units = static_cast<const uint16_t *>(src);
    uint8_t *out = static_cast<uint8_t *>(dst);
    std::string text;
    for (uint32_t i = 0; i < n; ++i) {
        out[i] = uint8_t(units[i]);
        if (text.size() == i && units[i]) text += char(units[i]);
    }
    drawfx::Call c;
    c.note = "Convert";
    c.words = {srcLength, uint32_t(srcEncoding), *dstLength, uint32_t(dstEncoding)};
    c.text = text + '\x1f';
    drawfx::gLog.push_back(std::move(c));
    *dstLength = n;
    return n;
}
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- cIGZString fakes (hand-made: the varargs Sprintf is cdecl) ----
struct FakeString {
    void *const *vt;
    uint32_t id;
};
void TextNote(const char *label, uint32_t id, const char *text)
{
    drawfx::Call c;
    c.note = label;
    c.words = {id};
    c.text = std::string(text) + '\x1f';
    drawfx::gLog.push_back(std::move(c));
}
void __fastcall StringFromChar(FakeString *self, void *, const char *text)
{
    TextNote("FromChar", self->id, text);
}
void __cdecl StringSprintf(FakeString *self, const char *format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    TextNote("Sprintf", self->id, text);
}
void __fastcall StringUnexpected(FakeString *self, void *) { drawfx::Note("unexpected cIGZString slot", self->id); }
void *gStringVT[33];
FakeString gStrings[6];

void BuildStrings()
{
    for (void *&s : gStringVT) s = reinterpret_cast<void *>(&StringUnexpected);
    gStringVT[4] = reinterpret_cast<void *>(&StringFromChar);
    gStringVT[31] = reinterpret_cast<void *>(&StringSprintf);
    for (unsigned i = 0; i < 6; ++i) gStrings[i] = {gStringVT, 0x57000000u + i};
}

// ---- the pool ----
constexpr uint32_t kPage = 0x800;
constexpr uint32_t kBlock = 0x10;
constexpr uint32_t kBlocks = (kPage - 16) / kBlock;
uint8_t *gPages;
RZFixedPage *Page(int i) { return reinterpret_cast<RZFixedPage *>(gPages + kPage * uint32_t(i)); }
uint8_t *Block(int p, uint32_t k) { return gPages + kPage * uint32_t(p) + 16 + k * kBlock; }
void ChainPage(int p, uint32_t freeBlocks)
{
    uint8_t *first = nullptr;
    for (uint32_t k = freeBlocks; k-- > 0;) {
        *reinterpret_cast<uint8_t **>(Block(p, k)) = first;
        first = Block(p, k);
    }
    Page(p)->freeList = first;
    Page(p)->used = kBlocks - freeBlocks;
    Page(p)->next = Page(p)->prev = Page(p);
}
int gNextPage;
RZFixedPage *__fastcall PoolNewPage(void *, void *)
{
    drawfx::Note("pool NewPage", 0);
    const int p = gNextPage < 3 ? gNextPage++ : 2;
    ChainPage(p, kBlocks);
    Page(p)->used = 0;
    return Page(p);
}
void __fastcall PoolFreePage(void *, void *, RZFixedPage *page)
{
    drawfx::Note("pool FreePage", uint32_t((reinterpret_cast<uint8_t *>(page) - gPages) / kPage));
}
void *gPoolVT[4] = {nullptr, nullptr, reinterpret_cast<void *>(&PoolNewPage), reinterpret_cast<void *>(&PoolFreePage)};
void BuildPool(uint32_t freeBlocks)
{
    if (!gPages) gPages = static_cast<uint8_t *>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::memset(gPages, 0xcd, kPage * 3);
    ChainPage(0, freeBlocks);
    gNextPage = 1;
    uint32_t *pool = GameAddress<uint32_t>(kPoolRva);
    pool[0] = P(gPoolVT);
    pool[1] = P(Page(0));
    pool[2] = 0;
}
uint32_t PoolNorm(uint32_t w)
{
    const uintptr_t base = uintptr_t(gPages);
    if (w >= base && w < base + kPage * 3) return 0x9a000000u | uint32_t(w - base);
    return w;
}
void DumpPool(std::vector<uint32_t> &out)
{
    const uint32_t *pool = GameAddress<uint32_t>(kPoolRva);
    out.push_back(PoolNorm(pool[1]));
    out.push_back(PoolNorm(pool[2]));
    for (int p = 0; p < 3; ++p) {
        out.push_back(PoolNorm(P(Page(p)->next)));
        out.push_back(PoolNorm(P(Page(p)->prev)));
        out.push_back(PoolNorm(P(Page(p)->freeList)));
        out.push_back(Page(p)->used);
    }
}

// ---- seeding ----
uint32_t Pick(std::mt19937 &r, unsigned oneIn) { return r() % oneIn == 0; }
uint32_t Hr(std::mt19937 &r, unsigned failOneIn)
{
    if (!Pick(r, failOneIn)) return 0;
    static const uint32_t kFails[] = {0x8876086c, kNotAvailable, 0x80004005, 1};
    return kFails[r() % 4];
}
void SeedText(std::mt19937 &r, char *text, size_t size)
{
    for (size_t i = 0; i < size; ++i) text[i] = char(r());
    const size_t n = r() % 40;
    for (size_t i = 0; i < n; ++i) text[i] = char(' ' + r() % 95);
    text[n] = 0;
}
void SeedCaps(std::mt19937 &r, D3DCAPS9 &caps)
{
    uint32_t *w = reinterpret_cast<uint32_t *>(&caps);
    for (size_t i = 0; i < sizeof(caps) / 4; ++i) w[i] = r();
    caps.DevCaps = (r() & ~(D3DDEVCAPS_HWTRANSFORMANDLIGHT | 0x100000u)) |
                   (Pick(r, 3) ? 0 : D3DDEVCAPS_HWTRANSFORMANDLIGHT) | (Pick(r, 2) ? 0x100000u : 0);
    caps.DevCaps2 = r() & 0x7f;
    static const uint32_t kVersions[] = {0, 0x101, 0x200, 0x201, 0x300, 0x1ff, 0x100, 0x400};
    caps.VertexShaderVersion = 0xfffe0000u | kVersions[r() % 8];
    caps.PixelShaderVersion = 0xffff0000u | kVersions[r() % 8];
    if (Pick(r, 4)) caps.PixelShaderVersion &= 0xffff00ffu;
    caps.NumSimultaneousRTs = r() % 7;
    caps.MaxTextureBlendStages = r() % 12;
    caps.MaxSimultaneousTextures = r() % 20;
    caps.MaxStreams = r() % 20;
    caps.MaxVertexShaderConst = Pick(r, 2) ? r() % 0x500 : r();
    caps.MaxActiveLights = Pick(r, 2) ? r() % 20 : r();
    caps.MaxStreamStride = Pick(r, 2) ? 0xfc + r() % 8 - 4 : r();
    caps.MaxVertexIndex = Pick(r, 2) ? 0xffff + r() % 8 - 4 : r();
    caps.VertexProcessingCaps = r() & 0x1ff;
    caps.TextureCaps = r();
    caps.DeclTypes = r() & 0x3ff;
}
void SeedMode(std::mt19937 &r, D3DDISPLAYMODE &m, uint32_t format)
{
    static const uint32_t kWidths[] = {640, 800, 1024, 1280};
    m.Width = Pick(r, 8) ? r() : kWidths[r() % 4];
    m.Height = Pick(r, 8) ? r() % 3 : kWidths[r() % 4] * 3 / 4;
    m.RefreshRate = Pick(r, 2) ? 60 : 50 + r() % 40;
    m.Format = static_cast<D3DFORMAT>(Pick(r, 6) ? 0x15 + r() % 4 : format);
}

void SeedCase(std::mt19937 &r, Case &c, Mode mode)
{
    c.seed = r();
    c.adapter = r() % 3;
    for (uint8_t &b : c.pattern) b = uint8_t(r());
    uint32_t *iw = reinterpret_cast<uint32_t *>(&c.ident);
    for (size_t i = 0; i < sizeof(c.ident) / 4; ++i) iw[i] = r();
    SeedText(r, c.ident.Driver, sizeof(c.ident.Driver));
    SeedText(r, c.ident.Description, sizeof(c.ident.Description));
    SeedText(r, c.ident.DeviceName, sizeof(c.ident.DeviceName));
    static const uint32_t kWhql[] = {0, 1, 0x7d40a1f, 0x7d00c08};
    c.ident.WHQLLevel = Pick(r, 4) ? r() : kWhql[r() % 4];
    c.identHr = Hr(r, 6);
    c.probeCreateHr = Hr(r, 4);
    c.textureMem = Pick(r, 2) ? r() : r() % 0x8000000;
    static const float kAspects[] = {0.0f, 1.0f, 1.25f, 0.8f, -0.0f};
    c.aspect = Pick(r, 10) ? std::nanf("") : Pick(r, 4) ? float(r() % 1000) / 333.0f : kAspects[r() % 5];
    for (uint8_t &u : c.usable) u = uint8_t(Pick(r, 4) ? 0 : 1 + r() % 3);
    for (uint8_t &u : c.nullD3D) u = uint8_t(Pick(r, 3));
    const uint32_t *modeFormats = GameAddress<uint32_t>(kModeFormatsRva);
    static const uint32_t kOthers[] = {0x15, 0x16, 0x17, 0x1c, 0x29, 0x31545844, 0x35545844, 0x50, 0x4b, 0x4d, 0x47, 0x14, 0x19};
    const unsigned classes = 1 + r() % 3;
    for (unsigned i = 0; i < kFormatCount; ++i) {
        c.formatIds[i] = i < 5 ? modeFormats[i] : i - 5 < 13 ? kOthers[i - 5] : r() % 0x80;
        c.classIds[i] = Pick(r, 10) ? 0xbac3896f : 0x10000u + r() % classes;
        c.keys[i] = r() % (Pick(r, 3) ? 4 : 40);
    }
    c.colorList.clear();
    c.depthList.clear();
    for (unsigned n = r() % 10; n-- > 0;) c.colorList.push_back(r() % kFormatCount);
    for (unsigned n = r() % 6; n-- > 0;) c.depthList.push_back(Pick(r, 3) ? r() % kFormatCount : 12 + r() % 4);
    for (unsigned f = 0; f < 5; ++f) {
        c.modeCount[f] = Pick(r, 3) ? 0 : r() % 7;
        for (unsigned i = 0; i < 6; ++i) {
            SeedMode(r, c.modes[f][i], modeFormats[f]);
            c.enumHr[f][i] = Hr(r, 8);
        }
    }
    c.salt = r();
    for (D3DCAPS9 &caps : c.caps) SeedCaps(r, caps);
    for (D3DCAPS9 &caps : c.deviceCaps) SeedCaps(r, caps);
    c.capsHr = Hr(r, 8);
    c.deviceCapsHr = Hr(r, 8);
    static const uint32_t kBehaviors[] = {D3DCREATE_SOFTWARE_VERTEXPROCESSING, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                          D3DCREATE_MIXED_VERTEXPROCESSING, D3DCREATE_HARDWARE_VERTEXPROCESSING | 0x10};
    c.behavior = Pick(r, 6) ? r() : kBehaviors[r() % 4];
    c.creationHr = Hr(r, 8);
    c.svpHr[0] = Hr(r, 8);
    c.svpHr[1] = Hr(r, 8);
    SeedMode(r, c.desktop, 0x16);
    c.desktopHr = Hr(r, 8);
    c.deviceModeHr = Hr(r, 8);
    c.qualities = r() % 5;
    // The default mode's format is null (windowed): an aspect that never matches (a NaN) would make the "for the
    // display" slots take that null format, which the original dereferences.
    c.defaultMode = {Pick(r, 2) ? 0u : 640u, Pick(r, 2) ? 0u : 480u, Pick(r, 4) ? 1.0f : 0.0f, 0, nullptr};
    c.settingsDefault = !Pick(r, 3);
    uint32_t *sw = reinterpret_cast<uint32_t *>(&c.settings);
    for (size_t i = 0; i < sizeof(c.settings) / 4; ++i) sw[i] = r();
    if (Pick(r, 3)) {
        c.settings.mMode.mWidth = c.defaultMode.mWidth;
        c.settings.mMode.mHeight = c.defaultMode.mHeight;
        c.settings.mMode.mAspect = c.defaultMode.mAspect;
        c.settings.mMode.mRefresh = c.defaultMode.mRefresh;
    }
    c.settingsFormat = Pick(r, 6) ? -1 : int(r() % kFormatCount);
    for (unsigned i = 0; i < kPropCount; ++i) {
        c.propSet[i] = Pick(r, 2);
        c.propValue[i] = kProps[i].isUint ? (Pick(r, 2) ? r() : r() % 0x100) : (Pick(r, 6) ? 2 + r() % 254 : r() % 2);
    }
    c.refreshes = mode == Mode::Construct || mode == Mode::Dump ? 0 : mode == Mode::Refresh ? 1 + r() % 3 : 1;
    for (unsigned i = 0; i < 3; ++i) {
        c.refreshDevice[i] = Pick(r, 2);
        static const uint32_t kFlags[] = {0, 2, 8, 0x10, 0x18, 0x12, 0x1a};
        c.refreshFlags[i] = Pick(r, 6) ? r() : kFlags[r() % 7];
    }
    c.poolFree = Pick(r, 3) ? 0 : 1 + r() % 6;
    c.release = Pick(r, 2);
    c.opSeed = r();
    c.ops = mode == Mode::Slots ? 1 + r() % 6 : 0;
}

void BuildFakes(const Case &c)
{
    gCase = &c;
    gOpsPhase = false;
    gNextIter = 0;
    gIterOverflow = false;
    {
        difftest::NotCounted quiet;
        gLists.clear();
        gLists.shrink_to_fit();
        gLists.reserve(4096);
    }
    gD3D.Reset<fx_d3d9::IDirect3D9>(kD3DId);
    gProbe.Reset<fx_d3d9::IDirect3D9>(kProbeId);
    for (drawfx::Fake *d3d : {&gD3D, &gProbe}) {
        d3d->On(5, &D3DIdentifier);
        d3d->On(6, &D3DModeCount);
        d3d->On(7, &D3DEnumMode);
        d3d->On(8, &D3DDisplayMode);
        d3d->On(9, &D3DCheck5);
        d3d->On(10, &D3DCheckFormat);
        d3d->On(11, &D3DCheckMultisample);
        d3d->On(12, &D3DCheckDepth);
        d3d->On(13, &D3DCheckConversion);
        d3d->On(14, &D3DCaps);
        d3d->On(15, &D3DMonitor);
        d3d->On(16, &ProbeCreate);
    }
    gDevice.Reset<fx_d3d9::IDirect3DDevice9>(kDeviceId);
    gDevice.On(fx_d3d9::dev::GetCreationParameters, &DeviceCreation);
    gDevice.On(fx_d3d9::dev::SetSoftwareVertexProcessing, &DeviceSetSoftware);
    gDevice.On(fx_d3d9::dev::GetSoftwareVertexProcessing, &DeviceGetSoftware);
    gDevice.On(fx_d3d9::dev::GetDeviceCaps, &DeviceCaps);
    gDevice.On(fx_d3d9::dev::GetDisplayMode, &DeviceMode);
    gDevice.value[0] = c.seed & 1;
    gProbeDevice.Reset<fx_d3d9::IDirect3DDevice9>(kProbeDeviceId);
    gProbeDevice.On(fx_d3d9::dev::GetAvailableTextureMem, &ProbeTextureMem);
    gGraphicFake.Reset<kGraphic>(kGraphicId);
    gGraphicFake.On(25, &GraphicDirect3D);
    gGraphicFake.On(26, &GraphicUsable);
    gOwner.Reset<kOwner>(kOwnerId);
    gOwner.On(33, &OwnerSettings);
    gOwner.On(42, &OwnerMonitor);
    gMonitor.Reset<kMonitor>(kMonitorId);
    gMonitor.ReturnsFloat(6, c.aspect);
    gManager.Reset<kManager>(kManagerId);
    gManager.On(3, &ManagerFormats);
    gManager.On(4, &ManagerFormat);
    gManager.On(5, &ManagerUnknown);
    gCom.Reset<kCom>(kComId);
    gCom.On(3, &ComClassObject);
    gStreamFake.Reset<kStream>(kStreamId);
    gStreamFake.Returns(7, 1);
    gStreamFake.Returns(9, 1);
    gStreamFake.Returns(14, 1);
    gUnknown.Reset<kFormat>(kUnknownFormatId);
    gUnknown.On(3, &FormatId);
    gUnknown.On(5, &FormatClass);
    gUnknown.On(10, &FormatLess);
    gUnknown.value[0] = 0;
    gUnknown.value[1] = 0x10000;
    gUnknown.value[2] = 7;
    for (unsigned i = 0; i < kFormatCount; ++i) {
        drawfx::Fake &f = gFormats[i];
        f.Reset<kFormat>(kFormatBase + i);
        f.On(3, &FormatId);
        f.On(5, &FormatClass);
        f.On(10, &FormatLess);
        f.value[0] = c.formatIds[i];
        f.value[1] = c.classIds[i];
        f.value[2] = c.keys[i];
    }
    BuildStrings();
    *GameAddress<cDisplayMode>(cDeviceEnumDX::kDefaultModeRva) = c.defaultMode;
}

// ---- slot calls ----
template <class R, class... A> R Call(void *object, unsigned slot, A... a)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot])(object, a...);
}

struct World {
    svcfx::AllocatorScope allocator;
    d3dfx::ErrorLog errors;
    impfx::Imports imp;
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::StandIn formats{0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, &StandFormats};
    shape::StandIn framework{0x1ae18d, &::GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn convert{0x1afd58, &RZText::Convert, &StandConvert};
    uint32_t savedPool[3];
    cDisplayMode savedDefault;
    explicit World(difftest::Context &ctx) : errors(ctx), imp(ctx)
    {
        IMPFX_ADD(imp, Direct3DCreate9).On(&ImpCreate9);
        IMPFX_ADD(imp, GetDesktopWindow).Returns(kDesktopWindow);
        // Three words by hand: IMPFX_ADD would count the REFGUID (a reference) as the GUID's four.
        imp.Add("StringFromGUID2", 3, "iiu").On(&ImpGuidText);
        std::memcpy(savedPool, GameAddress<uint32_t>(kPoolRva), sizeof(savedPool));
        savedDefault = *GameAddress<cDisplayMode>(cDeviceEnumDX::kDefaultModeRva);
    }
    ~World()
    {
        std::memcpy(GameAddress<uint32_t>(kPoolRva), savedPool, sizeof(savedPool));
        *GameAddress<cDisplayMode>(cDeviceEnumDX::kDefaultModeRva) = savedDefault;
    }
    World(const World &) = delete;
    World &operator=(const World &) = delete;
};

struct Result {
    drawfx::Log log;
    std::vector<uint32_t> results;
    std::vector<uint8_t> object;
    std::vector<uint32_t> pool;
    std::vector<uint32_t> refs;
    long long blocks = 0, bytes = 0;
    bool overflow = false;
};

uint32_t N(uint32_t w) { return drawfx::Norm(w); }
uint32_t N(const void *p) { return drawfx::Norm(P(p)); }

void *FormatArg(std::mt19937 &r)
{
    const unsigned k = r() % (kFormatCount + 2);
    if (k == kFormatCount) return &gUnknown;
    if (k == kFormatCount + 1) return &gFormats[r() % 5];
    return &gFormats[k];
}

// A pixel format range: what it holds and where its cursors are, then released (end, begin, cursor).
void TakeFormatRange(void *const range[3], std::vector<uint32_t> &out)
{
    for (unsigned i = 0; i < 3; ++i) out.push_back(N(range[i]));
    for (unsigned i = 0; i < 3; ++i) {
        const drawfx::Fake *f = static_cast<const drawfx::Fake *>(range[i]);
        if (f < gIters || f >= gIters + kIterMax) continue;
        out.push_back(f->value[1]);
        out.push_back(f->value[2]);
        for (uint32_t w : gLists[f->value[0]]) out.push_back(N(w));
        out.push_back(0xe0d);
    }
    for (int i = 2; i >= 0; --i)
        if (range[i]) out.push_back(Call<uint32_t>(range[i], 2));
}

void ModeWords(const cDisplayMode &m, std::vector<uint32_t> &out)
{
    out.push_back(m.mWidth);
    out.push_back(m.mHeight);
    out.push_back(std::bit_cast<uint32_t>(m.mAspect));
    out.push_back(m.mRefresh);
    out.push_back(N(m.mFormat));
}

// A display mode range: a random script on its iterators (the real ones of each side), then everything released.
void ModeScript(void *const range[3], std::mt19937 &r, std::vector<uint32_t> &out)
{
    void *const end = range[2];
    // The count: the begin iterator cloned and walked to the end.
    void *walker = Call<void *>(range[1], 3);
    Call<uint32_t>(walker, 1);
    unsigned count = 0;
    while (!Call<bool>(walker, 4, end) && count < 100) {
        cDisplayMode m;
        Call<void *>(walker, 5, &m);
        ModeWords(m, out);
        Call<void>(walker, 6);
        ++count;
    }
    out.push_back(0xc0000000u | count);
    out.push_back(Call<uint32_t>(walker, 2));
    struct Live {
        void *it;
        int pos;
    };
    std::vector<Live> live = {{range[0], 0}};
    Call<uint32_t>(range[0], 1);
    for (unsigned op = 0, ops = 4 + r() % 20; op < ops; ++op) {
        Live &l = live[r() % live.size()];
        const unsigned kind = r() % 9;
        out.push_back(0x0b000000u | kind);
        switch (kind) {
        case 0: {
            void *c = Call<void *>(l.it, 3);
            out.push_back(Call<uint32_t>(c, 1));
            live.push_back({c, l.pos});
            break;
        }
        case 1: {
            const Live &o = live[r() % live.size()];
            out.push_back(Call<bool>(l.it, 4, o.it));
            out.push_back(Call<bool>(l.it, 4, end));
            break;
        }
        case 2:
            if (l.pos >= 0 && unsigned(l.pos) < count) {
                cDisplayMode m;
                out.push_back(N(Call<void *>(l.it, 5, &m)));
                ModeWords(m, out);
            }
            break;
        case 3:
            if (unsigned(l.pos) < count) Call<void>(l.it, 6), ++l.pos;
            break;
        case 4:
            if (l.pos > 0) Call<void>(l.it, 7), --l.pos;
            break;
        case 5: {
            static const uint32_t kIids[] = {1, 0x6c02c311, 0, 0xc9da7e9f};
            void *o = reinterpret_cast<void *>(0x5a5a5a5a);
            const bool withOut = !Pick(r, 4);
            const bool ok = Call<bool>(l.it, 0, kIids[r() % 4], withOut ? &o : static_cast<void **>(nullptr));
            out.push_back(ok);
            out.push_back(o == l.it ? 1 : o == reinterpret_cast<void *>(0x5a5a5a5a) ? 2 : 3);
            if (ok) out.push_back(Call<uint32_t>(l.it, 2));
            break;
        }
        case 6:
            out.push_back(Call<uint32_t>(l.it, 1));
            out.push_back(Call<uint32_t>(l.it, 2));
            break;
        default:
            out.push_back(static_cast<uint32_t *>(l.it)[1]);
            out.push_back(PoolNorm(P(l.it)));
            break;
        }
    }
    for (const Live &l : live) out.push_back(Call<uint32_t>(l.it, 2));
    for (int i = 2; i >= 0; --i) out.push_back(Call<uint32_t>(range[i], 2));
}

void SetProperties(void *object, const Case &c)
{
    auto *set = Call<cIGZPropertySetString *>(object, 22);
    for (unsigned i = 0; i < kPropCount; ++i) {
        if (!c.propSet[i]) continue;
        const GZServiceString key(kProps[i].key);
        if (kProps[i].isUint) {
            gz::AnyUint32 v(c.propValue[i]);
            set->SetProperty(&key, &v);
        } else {
            gz::AnyBoolean v(uint8_t(c.propValue[i]));
            set->SetProperty(&key, &v);
        }
    }
}

void Op(void *o, std::mt19937 &r, std::vector<uint32_t> &out, bool hasDevice,
        const std::function<void(void *, uint32_t)> &refresh)
{
    const unsigned kind = r() % 26;
    out.push_back(0x0a000000u | kind);
    drawfx::Note("op", kind);
    switch (kind) {
    case 0: {
        static const uint32_t kIids[] = {1, 0x6c02c311, 0xc9da7e9f, 0, 0x6c02c312};
        void *p = reinterpret_cast<void *>(0x5a5a5a5a);
        const bool withOut = !Pick(r, 4);
        const bool ok = Call<bool>(o, 0, kIids[r() % 5], withOut ? &p : static_cast<void **>(nullptr));
        out.push_back(ok);
        out.push_back(N(p));
        if (ok) out.push_back(Call<uint32_t>(o, 2));
        break;
    }
    case 1:
        out.push_back(Call<uint32_t>(o, 1));
        out.push_back(Call<uint32_t>(o, 2));
        break;
    case 2:
        out.push_back(N(Call<void *>(o, 3)));
        out.push_back(N(Call<void *>(o, 4)));
        break;
    case 3: {
        void *s[6];
        for (unsigned i = 0; i < 6; ++i) s[i] = Pick(r, 4) ? nullptr : &gStrings[i];
        Call<void>(o, 5, s[0], s[1], s[2], s[3], s[4], s[5]);
        break;
    }
    case 4: {
        cDisplayMode m;
        std::memset(&m, 0x5c, sizeof(m));
        out.push_back(N(Call<void *>(o, 6, &m)));
        ModeWords(m, out);
        break;
    }
    case 5: {
        void *range[3] = {};
        void *filter = Pick(r, 2) ? nullptr : FormatArg(r);
        out.push_back(N(Call<void *>(o, 7, range, filter)));
        ModeScript(range, r, out);
        break;
    }
    case 6:
    case 7:
    case 8:
    case 11:
    case 12: {
        void *range[3] = {};
        const uint32_t usage = r() % 14;
        const uint32_t iid = Pick(r, 3) ? 0x5ac389b6 : r() % 4;
        void *a = FormatArg(r), *b = FormatArg(r);
        void *back = nullptr;
        if (kind == 6) back = Call<void *>(o, 8, range);
        if (kind == 7) back = Call<void *>(o, 9, range, usage, iid);
        if (kind == 8) back = Call<void *>(o, 10, range, usage, a, iid);
        if (kind == 11) back = Call<void *>(o, 13, range, usage, a);
        if (kind == 12) back = Call<void *>(o, 14, range, usage, a, b);
        out.push_back(N(back));
        TakeFormatRange(range, out);
        break;
    }
    case 9: out.push_back(Call<bool>(o, 11, FormatArg(r), r() % 13)); break;
    case 10: out.push_back(Call<bool>(o, 12, FormatArg(r), r() % 13, FormatArg(r))); break;
    case 13: out.push_back(Call<bool>(o, 15, FormatArg(r), 0xa + r() % 5, FormatArg(r))); break;
    case 14: out.push_back(Call<bool>(o, 16, FormatArg(r), 0xa + r() % 5, FormatArg(r), FormatArg(r))); break;
    case 15: out.push_back(uint32_t(Call<int32_t>(o, 17, Pick(r, 4) ? 0u : r() % 17, FormatArg(r)))); break;
    case 16: out.push_back(uint32_t(Call<int32_t>(o, 18, Pick(r, 4) ? 0u : r() % 17, FormatArg(r), FormatArg(r)))); break;
    case 17: {
        if (!hasDevice) break;
        // Several questions per op: the answers that tell the stretching tables apart (a few cells of 0xd9f964..) and
        // the DXT check on either side need a stretch between two usable kinds, which one question rarely is.
        for (unsigned q = 0; q < 8; ++q) {
            cSurfaceExtent sizes[2];
            RECT rects[2];
            for (unsigned i = 0; i < 2; ++i) {
                sizes[i] = {Pick(r, 3) ? 64u : r() % 80, Pick(r, 3) ? 64u : r() % 80};
                if (Pick(r, 2)) {
                    rects[i] = {0, 0, LONG(sizes[i].mWidth), LONG(sizes[i].mHeight)};
                } else {
                    rects[i] = {LONG(r() % 90) - 8, LONG(r() % 90) - 8, LONG(r() % 90) - 8, LONG(r() % 90) - 8};
                }
            }
            if (Pick(r, 3)) rects[1] = rects[0], sizes[1] = sizes[0];
            const uint32_t kinds[2] = {Pick(r, 4) ? 0xbu : r() % 16, Pick(r, 4) ? 0xbu : r() % 16};
            out.push_back(Call<bool>(o, 19, kinds[0], &sizes[0], &rects[0], FormatArg(r), kinds[1], &sizes[1],
                                     &rects[1], FormatArg(r)));
        }
        break;
    }
    case 25:
        // A8 (gFormats[8]) for a texture usage (4-6): an unavailable A8 falls back to P8 with alpha palettes, which
        // depends on the stored caps byte +0x700 as well as the P8 answer.
        out.push_back(Call<bool>(o, 12, &gFormats[8], 4 + r() % 3, FormatArg(r)));
        break;
    case 18:
        // Slots 20 and 25 hand out the stored byte (al), so the unwritten pads (+0x703, +0x777) show the case's bytes.
        for (uint32_t i = 0; i < 0xc; ++i) out.push_back(Call<uint8_t>(o, 20, i));
        for (uint32_t i = 0; i < 0x1b; ++i) out.push_back(Call<uint32_t>(o, 21, i));
        for (uint32_t i = 0; i < 8; ++i) out.push_back(Call<uint8_t>(o, 25, i));
        for (uint32_t i = 0; i < 3; ++i) out.push_back(Call<uint32_t>(o, 26, i));
        break;
    case 19: out.push_back(N(Call<void *>(o, 22))); break;
    case 20:
        out.push_back(N(Call<void *>(o, 23, true)));
        out.push_back(N(Call<void *>(o, 23, false)));
        if (hasDevice) out.push_back(N(Call<void *>(o, 24)));
        break;
    case 21: {
        D3DADAPTER_IDENTIFIER9 id;
        std::memset(&id, 0x6b, sizeof(id));
        // Not its eax: the original's body (a rep movsd) leaves eax as it was; every caller drops the result.
        Call<void *>(o, 27, &id);
        const uint32_t *w = reinterpret_cast<const uint32_t *>(&id);
        out.insert(out.end(), w, w + sizeof(id) / 4);
        break;
    }
    case 22: out.push_back(Call<uint32_t>(o, 28, r() % 11)); break;
    case 23: refresh(Pick(r, 2) ? &gDevice : nullptr, Pick(r, 3) ? r() : r() % 0x20); break;
    default: {
        // A walk of the modes through GetDisplayModes with each format as the filter.
        for (unsigned i = 0; i < 5; ++i) {
            void *range[3] = {};
            Call<void *>(o, 7, range, &gFormats[i]);
            ModeScript(range, r, out);
        }
        break;
    }
    }
}

Result Side(difftest::Context &ctx, World &world, const Case &c, Mode mode, bool original)
{
    drawfx::ResetAll();
    world.imp.BeginSide();
    BuildFakes(c);
    world.errors.Answer(0x85522803, 0xc55289ba, gGraphicFake);
    BuildPool(c.poolFree);
    Result res;
    res.results.reserve(1u << 16);
    drawfx::gLog = drawfx::Log();
    drawfx::gLog.reserve(1u << 15);
    uint8_t *const block = static_cast<uint8_t *>(::operator new(kSize));
    std::memcpy(block, c.pattern, kSize);
    drawfx::AddRange(block, kSize, kObjectTag);
    std::vector<uint32_t> &out = res.results;
    bool released = false;
    {
        difftest::AllocationCounter counter;
        void *object;
        if (original) {
            object = ctx.Original<void *(__thiscall *)(void *, uint32_t, void *)>(kOriginalConstruct)(block, c.adapter, &gOwner);
        } else {
            object = cDeviceEnumDXAdapters::Construct(block, nullptr, c.adapter, reinterpret_cast<cIDeviceDX *>(&gOwner));
        }
        out.push_back(N(object));
        out.push_back(Call<uint32_t>(block, 1)); // the holder's reference (cDeviceDX::Init takes one)
        bool hasDevice = false;
        const std::function<void(void *, uint32_t)> refresh = [&](void *device, uint32_t flags) {
            drawfx::Note("refresh", {N(device), flags});
            const bool ops = gOpsPhase;
            gOpsPhase = false; // Refresh does not check the IDirect3D9 it is given
            hasDevice = device != nullptr;
            uint32_t type;
            if (original)
                type = ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t)>(kOriginalRefresh)(block, device, flags);
            else
                type = reinterpret_cast<cDeviceEnumDX *>(block)->Refresh(static_cast<IDirect3DDevice9 *>(device), flags);
            out.push_back(type);
            gOpsPhase = ops;
        };
        if (mode != Mode::Construct) SetProperties(block, c);
        for (unsigned i = 0; i < c.refreshes; ++i) refresh(c.refreshDevice[i] ? &gDevice : nullptr, c.refreshFlags[i]);
        gOpsPhase = true;
        std::mt19937 r(c.opSeed);
        for (unsigned i = 0; i < c.ops; ++i) Op(block, r, out, hasDevice, refresh);
        if (mode == Mode::Dump) {
            if (original)
                ctx.Original<void(__thiscall *)(void *, void *)>(kOriginalDump)(block, &gStreamFake);
            else
                reinterpret_cast<cDeviceEnumDX *>(block)->DumpInfo(reinterpret_cast<cIGZUnknown *>(&gStreamFake));
            void *s[6];
            for (unsigned i = 0; i < 6; ++i) s[i] = &gStrings[i];
            Call<void>(block, 5, s[0], s[1], s[2], s[3], s[4], s[5]);
        }
        gOpsPhase = false;
        // The object: every byte but the vtable word, the property set and the mode set's head; the modes in order.
        res.object.assign(block, block + kSize);
        std::memset(res.object.data(), 0, 4);
        std::memset(res.object.data() + 0x6cc, 0, 0x28);
        const auto &modes = *reinterpret_cast<const cDisplayModeSet *>(block + 0x6f0);
        out.push_back(0xd0000000u | uint32_t(modes.size()));
        for (const cDisplayMode &m : modes) ModeWords(m, out);
        // Destroyed: released (the deleting destructor through Release) or through slot 29 without the delete.
        if (c.release) {
            out.push_back(Call<uint32_t>(block, 2));
            released = true;
        } else {
            Call<void *>(block, 29, 0u);
        }
        counter.Stop();
        res.blocks = counter.Blocks();
        res.bytes = counter.Bytes();
    }
    if (!released) ::operator delete(block);
    for (const drawfx::Fake *f : drawfx::gFakes) {
        if (f->refs == 1000) continue;
        res.refs.push_back(f->id);
        res.refs.push_back(uint32_t(f->refs));
    }
    DumpPool(res.pool);
    res.overflow = gIterOverflow || res.results.size() > (1u << 16);
    res.log = std::move(drawfx::gLog);
    drawfx::gLog = drawfx::Log();
    return res;
}

// The original's side runs these bodies through the game's vtables (checked first, so the comparison covers exactly
// these functions):
//   cDeviceEnumDX 0xd9f758: 0 QueryInterface 0x178af0, 1 AddRef 0x148fb0 (shared), 2 Release 0x178b40 (shared),
//   3 / 4 GetDevice / GetDeviceDX 0x145770 (shared), 5 GetDescription 0x178410, 6 GetAdapterDisplayMode 0x177bf0,
//   7 GetDisplayModes 0x178190, 8 GetDisplayFormats 0x178040, 9 0x1786b0, 10 0x178740, 11 0x17a540, 12 0x17a280,
//   13 0x177f20, 14 0x177d70, 15 0x17a030, 16 0x179eb0, 17 0x17a1f0, 18 0x17a0c0, 19 CanStretchRect 0x17a5d0,
//   20 0x177a90, 21 0x178680, 22 0x177fb0, 23 0x178010, 24 0x177fe0, 25 0x177aa0, 26 0x178690, 27 0x177fc0,
//   28 GetTextureFormatForUsage 0x179d20, 29 the deleting destructor 0x177960;
//   cDisplayModeIterator 0xd9f7e8: 0 0x178b20 (shared), 1 0x148fb0, 2 0x178b60 (shared), 3 Clone 0x177ae0,
//   4 IsEqual 0x178550 (shared), 5 Get 0x177bd0, 6 Next 0x1786a0, 7 Prev 0x1788e0, 8 the deleting destructor 0x1779a0;
//   the counted vector 0xd9f7d4: 0x148350, 0x146a40, 2 Release 0x148840 (shared), 3 the deleting destructor 0x177930.
// Called at their RVAs: the constructor 0x1774a0, Refresh 0x178b80, the dump 0x1788f0, ClampCaps 0x178560; the mode
// order 0x1778c0 runs inside Refresh's set inserts (its Less calls are in the log).
struct SlotRva {
    uint32_t vtable;
    unsigned slot;
    uint32_t rva;
};
constexpr SlotRva kSlotRvas[] = {
    {kGameVTable, 0, 0x178af0},  {kGameVTable, 1, 0x148fb0},  {kGameVTable, 2, 0x178b40},  {kGameVTable, 3, 0x145770},
    {kGameVTable, 4, 0x145770},  {kGameVTable, 5, 0x178410},  {kGameVTable, 6, 0x177bf0},  {kGameVTable, 7, 0x178190},
    {kGameVTable, 8, 0x178040},  {kGameVTable, 9, 0x1786b0},  {kGameVTable, 10, 0x178740}, {kGameVTable, 11, 0x17a540},
    {kGameVTable, 12, 0x17a280}, {kGameVTable, 13, 0x177f20}, {kGameVTable, 14, 0x177d70}, {kGameVTable, 15, 0x17a030},
    {kGameVTable, 16, 0x179eb0}, {kGameVTable, 17, 0x17a1f0}, {kGameVTable, 18, 0x17a0c0}, {kGameVTable, 19, 0x17a5d0},
    {kGameVTable, 20, 0x177a90}, {kGameVTable, 21, 0x178680}, {kGameVTable, 22, 0x177fb0}, {kGameVTable, 23, 0x178010},
    {kGameVTable, 24, 0x177fe0}, {kGameVTable, 25, 0x177aa0}, {kGameVTable, 26, 0x178690}, {kGameVTable, 27, 0x177fc0},
    {kGameVTable, 28, 0x179d20}, {kGameVTable, 29, 0x177960},
    {kModeIterVTable, 0, 0x178b20}, {kModeIterVTable, 1, 0x148fb0}, {kModeIterVTable, 2, 0x178b60},
    {kModeIterVTable, 3, 0x177ae0}, {kModeIterVTable, 4, 0x178550}, {kModeIterVTable, 5, 0x177bd0},
    {kModeIterVTable, 6, 0x1786a0}, {kModeIterVTable, 7, 0x1788e0}, {kModeIterVTable, 8, 0x1779a0},
    {kCountedVTable, 0, 0x148350}, {kCountedVTable, 1, 0x146a40}, {kCountedVTable, 2, 0x148840},
    {kCountedVTable, 3, 0x177930},
};
static_assert(kOriginalLess == 0x1778c0);

void Seen(const Result &r, std::set<std::string> &seen)
{
    for (const drawfx::Call &call : r.log) {
        if (!call.iface) seen.insert(call.note);
        else if (call.slot < call.iface->count) seen.insert(call.iface->slots[call.slot].name);
    }
}

void Sweep(difftest::Context &ctx, Mode mode, uint32_t seed, unsigned cases, std::initializer_list<const char *> needs)
{
    for (const SlotRva &s : kSlotRvas)
        if (GameAddress<uint32_t>(s.vtable)[s.slot] != P(GameAddress<void>(s.rva)))
            ctx.Fail("vtable %#x slot %u is not %#x", s.vtable, s.slot, s.rva);
    World world(ctx);
    std::mt19937 gen(seed);
    std::set<std::string> seen;
    for (unsigned i = 0; i < cases; ++i) {
        ++ctx.cases;
        Case c;
        SeedCase(gen, c, mode);
        const Result a = Side(ctx, world, c, mode, true);
        const Result b = Side(ctx, world, c, mode, false);
        Seen(a, seen);
        if (!drawfx::Same(ctx, a.log, b.log, "cDeviceEnumDX", i)) return;
        if (a.overflow || b.overflow) {
            ctx.Fail("case %u: the test's own limits were reached", i);
            return;
        }
        if (a.results != b.results) {
            size_t at = 0;
            while (at < a.results.size() && at < b.results.size() && a.results[at] == b.results[at]) ++at;
            ctx.Fail("case %u: results differ at %u of %u/%u (%#x vs %#x)", i, unsigned(at), unsigned(a.results.size()),
                     unsigned(b.results.size()), at < a.results.size() ? a.results[at] : 0,
                     at < b.results.size() ? b.results[at] : 0);
            return;
        }
        if (a.object != b.object) {
            size_t at = 0;
            while (a.object[at] == b.object[at]) ++at;
            ctx.Fail("case %u: object differs at +%#x (%02x vs %02x)", i, unsigned(at), a.object[at], b.object[at]);
            return;
        }
        if (a.pool != b.pool || a.refs != b.refs || a.blocks != b.blocks || a.bytes != b.bytes) {
            ctx.Fail("case %u: pool %d refs %d (%u/%u) blocks %lld/%lld bytes %lld/%lld", i, a.pool == b.pool,
                     a.refs == b.refs, unsigned(a.refs.size()), unsigned(b.refs.size()), a.blocks, b.blocks, a.bytes,
                     b.bytes);
            return;
        }
    }
    for (const char *need : needs)
        if (!seen.count(need)) ctx.Fail("the sweep never reached '%s'", need);
}

// ---- ClampCaps (0x178560) ----
void ClampSweep(difftest::Context &ctx)
{
    std::mt19937 r(0x178560);
    const auto original = ctx.Original<void(__cdecl *)(D3DCAPS9 *, bool, bool)>(kOriginalClamp);
    for (unsigned i = 0; i < 20000; ++i) {
        ++ctx.cases;
        D3DCAPS9 a;
        SeedCaps(r, a);
        if (Pick(r, 3)) a.NumSimultaneousRTs = r();
        if (Pick(r, 3)) a.MaxTextureBlendStages = r();
        if (Pick(r, 3)) a.MaxSimultaneousTextures = r();
        if (Pick(r, 3)) a.MaxStreams = r();
        if (Pick(r, 3)) a.VertexShaderVersion = r();
        if (Pick(r, 3)) a.PixelShaderVersion = r();
        D3DCAPS9 b = a;
        const bool noVertexShaders = Pick(r, 2), noProjection = Pick(r, 2);
        // The flags as the callers pass them: a byte, the rest of the word a leftover.
        using Raw = void(__cdecl *)(D3DCAPS9 *, uint32_t, uint32_t);
        reinterpret_cast<Raw>(original)(&a, (r() & 0xffffff00u) | uint32_t(noVertexShaders),
                                        (r() & 0xffffff00u) | uint32_t(noProjection));
        cDeviceEnumDX::ClampCaps(&b, noVertexShaders, noProjection);
        if (std::memcmp(&a, &b, sizeof(a)) != 0) {
            ctx.Fail("ClampCaps case %u (%d, %d) differs", i, noVertexShaders, noProjection);
            return;
        }
    }
}

} // namespace

DIFF_TEST(cDeviceEnumDX_Construct, 0x1774a0)
{
    Sweep(ctx, Mode::Construct, 0x1774a0, 300,
          {"GetAdapterIdentifier", "CreateDevice", "GetAvailableTextureMem", "present parameters", "LogD3DError"});
}

DIFF_TEST(cDeviceEnumDX_Refresh, 0x178b80)
{
    Sweep(ctx, Mode::Refresh, 0x178b80, 600,
          {"EnumAdapterModes", "CheckDeviceType", "CheckDeviceFormat", "CheckDepthStencilMatch", "Less",
           "GetCreationParameters", "SetSoftwareVertexProcessing", "GetDeviceCaps", "LogD3DError", "GetPixelAspect",
           "IsDeviceTypeUsable", "GetFormats", "Insert"});
}

DIFF_TEST(cDeviceEnumDX_Slots, 0x178af0)
{
    Sweep(ctx, Mode::Slots, 0x178af0, 1500,
          {"CheckDeviceFormat", "CheckDeviceMultiSampleType", "CheckDeviceFormatConversion", "CheckDepthStencilMatch",
           "FromChar", "Sprintf", "StringFromGUID2", "Convert", "GetSettings", "Insert", "GetClassObject",
           "GetAdapterDisplayMode", "GetDisplayMode", "GetSoftwareVertexProcessing", "pool NewPage", "LogD3DError"});
}

DIFF_TEST(cDeviceEnumDX_DumpInfo, 0x1788f0)
{
    Sweep(ctx, Mode::Dump, 0x1788f0, 300, {"SetUint16", "SetUint32", "SetRZCharStr", "Sprintf", "FromChar"});
}

DIFF_TEST(cDeviceEnumDX_ClampCaps, 0x178560) { ClampSweep(ctx); }
