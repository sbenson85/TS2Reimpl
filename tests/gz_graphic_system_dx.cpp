// Differential tests for nGZGraphic4::cGraphicSystemDX as a whole class (src/gz/cGraphicSystemDX.h, lane NQ): the
// constructor (the creator 0x159940), every slot of both faces, the devices' build and teardown and the canvas handler
// they reach, the destructors, and the do-nothing Direct3D device slot 29 hands out (src/gz/cNullDevice.h).
//
// One world per case, built alike for each side from the case's seed:
// - The system is a static 0x458-byte block (a registered range): the original's side gets the game's two vtable words,
//   ours ours; every other byte is the case's, but for the device list (+0x418), which each side gets as a fresh heap
//   array (::operator new: the original's vector code allocates through GameNew, patched to it) holding the case's
//   devices. The original is called at its slot RVAs, ours through our vtables by slot name.
// - Devices are 0x78-byte blocks of seeded bytes (the system reads +0x71 and +0x73; RestoreDeviceSettings writes
//   +0x08..+0x33) whose vtable is an embedded fake (slots 1 / 2 / 8 / 9 / 23 / 24 / 25 / 33 / 34 / 37 / 45 / 47); each
//   device's display monitor and enumeration are fakes. Blocks 0-5 are the case's listed devices, 6-11 the ones the
//   device constructor (0x167200, stood in on both sides with Init 0x169cc0: lane NC's, verified by
//   tests/gz_device_dx_class.cpp) hands out for adapters 0-5.
// - Direct3D: Direct3DCreate9 is an impfx fake answering the case's queue (null, the "built" IDirect3D9 fake, or the
//   "fresh" one); the system's own (+0x14) is a third fake. Each answers adapter counts, caps, identifiers and monitors
//   from the case.
// - The window: the framework (d3dfx::ErrorLog) answers both of the window's interfaces with fakes; a case may run
//   without a framework (Shutdown's and TearDown's null window).
// - Events: our real cConnectionManager in the cell (0x10c9774), listener fakes connected to the system's three events;
//   the attribute registry 0x15a250 runs for real over a map placed at its static (RegistryScope).
// - Game code the slots hand on to is stood in by recorders on both sides: the getters (pixel formats 0x15eb10 and
//   shader programs 0x1758c0 and its per-device release 0x176e40 (ours on ours), vertex formats 0x16db40, declarations
//   0x16e760, image manip 0x16c0c0, the image's and ours), the D3DX / DXErr library entries 0xd5663 / 0x1c087 / 0x1c0e1
//   / 0x1546d; operator new / delete are ours on both sides (GameNew / GameDelete).
// Compared: the log (every call on every fake with its arguments, the stand-ins' notes), each slot's result, every byte
// of the system but its vtable words and the list's three pointers (compared as the list's size, capacity and devices
// instead), every device block, the allocations, an escaped cError, and every fake's count.
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "impfx.h"

#include "app/WinMain.h"
#include "game/vtable.h"
#include "gz/cConnectionManager.h"
#include "gz/cGraphicSystemDX.h"
#include "gz/cImageManipDX.h"
#include "gz/cNullDevice.h"
#include "gz/PixelFormatsManager.h"
#include "gz/cShaderProgramsDX.h"
#include "gz/cVertexFormat.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace nGZGraphic4;
using nGZEvent::cConnectionManager;
using nGZEvent::cConnectionManagerAdapters;
using nGZEvent::cEventAttributeMap;

namespace {

constexpr uint32_t kVTableRva = 0xd9ce44, kServiceVTableRva = 0xd9ced4;
constexpr uint32_t kEventManagerCell = 0x10c9774;
constexpr uint32_t kRegistryMap = 0x10c9768, kRegistryMapGuard = 0x10c9770, kRegistryOnce = 0x10c9778,
                   kRegistryHolderGuard = 0x10c9780;
constexpr uint32_t kNullDeviceRva = 0x10c9760, kNullDeviceGuard = 0x10c9764, kNullDeviceVTable = 0xd9e64c;
constexpr uint32_t kWindowService = 0xfb6372ee, kWindowCanvasIid = 0xbb6377fa, kWindowBehaviorIid = 0x5b637209;
constexpr uint32_t kErrorVTable = 0xd9cd70;
constexpr size_t kSize = cGraphicSystemDX::kSize;
constexpr size_t kDeviceSize = cDeviceDX::kSize;
constexpr unsigned kDevices = 12; // 0-5 listed, 6-11 built (adapter + 6)
constexpr unsigned kD3Ds = 3;     // 0 the system's own, 1 built by BuildDevices, 2 the canvas handler's fresh one
constexpr uint32_t kObjectTag = 0x7e000000, kDeviceTag = 0x7d000000, kDataTag = 0x7c000000, kConnTag = 0x7b000000,
                   kMarkTag = 0x7a000000;

// ---- fakes ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRefSlot{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kReleaseSlot{0, drawfx::Ret::Int, true, "Release", ""};

// A device (cIDeviceDX): the slots the system calls.
inline constexpr drawfx::Slot kDeviceSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U,
    {3, drawfx::Ret::Int, true, "dev.Connect", "uiu"},
    {3, drawfx::Ret::Int, true, "dev.Disconnect", "uiu"},
    U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "ApplySettings", "W11"},
    {0, drawfx::Ret::Int, true, "OnDeviceLost", ""},
    {0, drawfx::Ret::Int, true, "ReleaseDevice", ""},
    U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "GetSettings", "i"},
    {0, drawfx::Ret::Int, true, "HasDeviceOrLost", ""},
    U, U,
    {0, drawfx::Ret::Int, true, "IsFullscreen", ""},
    U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetDeviceEnum45", ""},
    U,
    {0, drawfx::Ret::Int, true, "GetDisplayMonitorConst", ""},
};
inline constexpr drawfx::Iface kDeviceIface{"cIDeviceDX", 48, kDeviceSlots};

inline constexpr drawfx::Slot kEnumSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "GetAdapterIdentifier", "i"},
};
inline constexpr drawfx::Iface kEnumIface{"cIDeviceEnum", 28, kEnumSlots};

inline constexpr drawfx::Slot kMonitorSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U,
    {1, drawfx::Ret::Int, true, "SetMonitor", "u"},
    {0, drawfx::Ret::Int, true, "GetMonitor", ""},
};
inline constexpr drawfx::Iface kMonitorIface{"cIDisplayMonitor", 9, kMonitorSlots};

// The window (cICanvasW32; its second interface answers slots 8 / 9 alike).
inline constexpr drawfx::Slot kWindowSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U,
    {3, drawfx::Ret::Int, true, "win.Connect", "uiu"},
    {3, drawfx::Ret::Int, true, "win.Disconnect", "uiu"},
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "SetFlag19", "b"},
    {0, drawfx::Ret::Int, true, "GetFlag18", ""},
    U, U,
    {0, drawfx::Ret::Int, true, "win.GetMonitor", ""},
};
inline constexpr drawfx::Iface kWindowIface{"cICanvasW32", 43, kWindowSlots};

inline constexpr drawfx::Slot kListenerSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot,
    {4, drawfx::Ret::Int, true, "OnEvent", "uuii"},
    {3, drawfx::Ret::Int, true, "OnQuery", "uui"},
};
inline constexpr drawfx::Iface kListenerIface{"cIAbstractBehavior", 5, kListenerSlots};

enum : uint32_t {
    kDeviceId0 = 0xe0100, kMonitorId0 = 0xe0200, kEnumId0 = 0xe0300, kWindowId = 0xe0400, kWindowBId, kSourceId,
    kListenerId0 = 0xe0410, kD3DId0 = 0xd3d00020
};
constexpr unsigned kListeners = 3;
// Listener i hears the system's event i as kHeardAs[i].
constexpr uint32_t kListenEvents[kListeners] = {cGraphicSystemDX::kCurrentDeviceStateEvent,
                                                cGraphicSystemDX::kCurrentDeviceFrameEvent,
                                                cGraphicSystemDX::kDevicesBuiltEvent};
constexpr uint32_t kHeardAs[kListeners] = {0x61, 0x62, 0x63};
// The monitor handles the fakes answer (a few, so that devices, adapters and the window match now and then).
constexpr uint32_t kMonitors[] = {0x4d000001, 0x4d000002, 0x4d000003};

drawfx::Fake gDev[kDevices], gMonitor[kDevices], gEnum[kDevices], gD3D[kD3Ds], gWindow, gWindowB,
    gListener[kListeners], gSource;

// ---- the blocks ----
alignas(16) uint8_t gObjectBlock[kSize];
uint8_t *gObject = gObjectBlock; // the system under test: the block, or a heap object (the last Release, delete)
alignas(16) uint8_t gDeviceBlocks[kDevices][kDeviceSize];
uint8_t gMarks[8]; // the stood-in getters' answers
struct Data {
    uint32_t message[8]; // event data: a device event (+4 its code), a resize (+0x14 flags), a canvas message (+4 kind)
    uint32_t words[4];
    uint32_t remap[8];
} gData;

cIGraphicSystemDX *System() { return reinterpret_cast<cIGraphicSystemDX *>(gObject); }
cIGZSystemService *Service() { return reinterpret_cast<cIGZSystemService *>(gObject + 8); }
uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }
uint32_t Norm(const void *p) { return drawfx::Norm(Word(p)); }
template <class T> void Put(void *block, size_t offset, T value) { std::memcpy(static_cast<uint8_t *>(block) + offset, &value, sizeof(T)); }
template <class T> T Get(const void *block, size_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t *>(block) + offset, sizeof(T));
    return value;
}

// ---- a case ----
enum Op : uint32_t {
    kQueryInterface, kAddRef, kRelease, kOnConnection, kOnState, kOnCanvas, kOnResize, kOnFrame, kOnOther, kOnQuery,
    kNotify, kAsk, kConnect, kDisconnect, kIsActive, kEventCount, kEventIds, kEventAttribute, kEmptySlots, kSimple,
    kManagers, kLibrary, kFindWindow, kServiceSlots, kInit, kShutdown, kDestruct, kReleaseLast, kDelete, kOpCount
};
bool Gone(uint32_t op) { return op == kReleaseLast || op == kDelete; }

struct D3DData {
    uint32_t count;      // GetAdapterCount
    uint32_t halMask;    // GetDeviceCaps(adapter, HAL) succeeds for these adapters
    uint32_t refHr;      // GetDeviceCaps(0, REF)
    uint32_t idFailMask; // GetAdapterIdentifier fails for these adapters
    uint32_t guid[8];    // each adapter's identifier (DeviceIdentifier's first dword)
    uint32_t monitor[8]; // GetAdapterMonitor
};

struct Case {
    uint32_t op;
    uint8_t object[kSize];
    uint8_t devices[kDevices][kDeviceSize];
    uint32_t listed, capacity; // the list: devices 0..listed-1, room for capacity
    bool hasD3D;               // the system's own (+0x14) is gD3D[0]
    D3DData d3d[kD3Ds];
    uint32_t creates[2];       // Direct3DCreate9's answers in turn: 0 null, else gD3D[n]
    uint32_t constructThrow;   // the device constructor throws for adapter constructThrow - 1
    bool initOk[6];
    uint32_t settings[kDevices][11];
    uint32_t hasDevice, fullscreen, apply; // bit i: device i's answer
    uint32_t monitor[kDevices];            // each device monitor's GetMonitor
    uint32_t enumGuid[kDevices];
    uint32_t flag18, windowMonitor;
    bool noFramework;
    uint32_t connections, listenerAnswers;
    Data data;
    uint32_t args[8];
};

// ---- stand-ins ----
struct Script {
    const Case *c = nullptr;
    unsigned creates = 0; // Direct3DCreate9 calls so far
    unsigned adapter = 0; // the adapter GetDeviceCaps(HAL) last asked about: the next device made is for it
} gScript;

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

cDeviceDX *__fastcall StandConstruct(void *memory, void *)
{
    const unsigned adapter = gScript.adapter;
    drawfx::Note("construct", adapter);
    if (gScript.c->constructThrow == adapter + 1) {
        cError error;
        error.mVTable = GameAddress<const char>(kErrorVTable);
        error.mCode = 7;
        throw error;
    }
    ::operator delete(memory);
    return reinterpret_cast<cDeviceDX *>(gDeviceBlocks[6 + adapter]);
}
bool __fastcall StandInit(cDeviceDX *device, void *, uint32_t adapter)
{
    drawfx::Note("Init", {Norm(device), adapter});
    return adapter < 6 && gScript.c->initOk[adapter];
}
void *__cdecl StandPixelFormats()
{
    drawfx::Note("pixel formats", 0u);
    return &gMarks[0];
}
void *__cdecl StandShaders()
{
    drawfx::Note("shaders", 0u);
    return &gMarks[1];
}
void __fastcall StandShaderRelease(void *self, void *, uint32_t slot) { drawfx::Note("shaders.release", {Norm(self), slot}); }
cVertexFormatsManager *__cdecl StandVertexFormats()
{
    drawfx::Note("vertex formats", 0u);
    return reinterpret_cast<cVertexFormatsManager *>(&gMarks[2]);
}
cVertexDeclarationsManagerDX *__cdecl StandDeclarations()
{
    drawfx::Note("declarations", 0u);
    return reinterpret_cast<cVertexDeclarationsManagerDX *>(&gMarks[3]);
}
cImageManipDX *__cdecl StandImageManip()
{
    drawfx::Note("image manip", 0u);
    return reinterpret_cast<cImageManipDX *>(&gMarks[4]);
}
const char gErrorText[] = "an error";
const char *__stdcall StandErrorString(long hr)
{
    drawfx::Note("DXGetErrorString9A", uint32_t(hr));
    return gErrorText;
}
long __stdcall StandCreateMesh(uint32_t faces, uint32_t vertices, uint32_t options, uint32_t fvf, void *device, void **mesh)
{
    drawfx::Note("D3DXCreateMeshFVF", {faces, vertices, options, fvf, Norm(device), Norm(mesh)});
    return long(0x8876086c);
}
long __stdcall StandOptimizeFaces(const void *indices, uint32_t faces, uint32_t vertices, int32_t indices32, uint32_t *remap)
{
    drawfx::Note("D3DXOptimizeFaces", {Norm(indices), faces, vertices, uint32_t(indices32), Norm(remap)});
    return 1;
}
long __stdcall StandOptimizeVertices(const void *indices, uint32_t faces, uint32_t vertices, int32_t indices32, uint32_t *remap)
{
    drawfx::Note("D3DXOptimizeVertices", {Norm(indices), faces, vertices, uint32_t(indices32), Norm(remap)});
    return 2;
}

// ---- handlers (COM fakes: the arguments after `this`) ----
const D3DData &DataOf(drawfx::Fake &self) { return *static_cast<const D3DData *>(self.owner); }
uint32_t D3DCount(drawfx::Fake &self, const uint32_t *) { return DataOf(self).count; }
uint32_t D3DCaps(drawfx::Fake &self, const uint32_t *a)
{
    const uint32_t adapter = a[0], type = a[1];
    if (type == D3DDEVTYPE_REF) return DataOf(self).refHr;
    gScript.adapter = adapter;
    return adapter < 32 && (DataOf(self).halMask >> adapter & 1) ? 0u : 0x8876086cu;
}
uint32_t D3DIdentifier(drawfx::Fake &self, const uint32_t *a)
{
    const uint32_t adapter = a[0];
    auto *id = reinterpret_cast<D3DADAPTER_IDENTIFIER9 *>(uintptr_t(a[2]));
    std::memset(id, 0x2c, sizeof(*id));
    std::memset(&id->DeviceIdentifier, 0, sizeof(GUID));
    id->DeviceIdentifier.Data1 = adapter < 8 ? DataOf(self).guid[adapter] : 0;
    return adapter < 8 && (DataOf(self).idFailMask >> adapter & 1) ? 0x8876086cu : 0u;
}
uint32_t D3DMonitor(drawfx::Fake &self, const uint32_t *a) { return a[0] < 8 ? DataOf(self).monitor[a[0]] : 0; }

unsigned IndexOf(const drawfx::Fake &self, const drawfx::Fake *array) { return unsigned(&self - array); }
uint32_t DeviceSettings(drawfx::Fake &self, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gScript.c->settings[IndexOf(self, gDev)], 44);
    return a[0];
}
uint32_t EnumIdentifier(drawfx::Fake &self, const uint32_t *a)
{
    auto *id = reinterpret_cast<D3DADAPTER_IDENTIFIER9 *>(uintptr_t(a[0]));
    std::memset(id, 0x3c, sizeof(*id));
    std::memset(&id->DeviceIdentifier, 0, sizeof(GUID));
    id->DeviceIdentifier.Data1 = gScript.c->enumGuid[IndexOf(self, gEnum)];
    return a[0];
}
uint32_t Direct3DCreate(drawfx::Fake &, const uint32_t *)
{
    const unsigned at = gScript.creates++;
    const uint32_t which = at < 2 ? gScript.c->creates[at] : 0;
    return which ? Word(&gD3D[which]) : 0u;
}
bool OnStack(uint32_t p)
{
    const auto *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    return p >= uintptr_t(tib->StackLimit) && p < uintptr_t(tib->StackBase);
}
// The listeners' OnEvent: data on the stack (the built event's cNullEvent) is logged by its vtable word.
uint32_t ListenerOnEvent(drawfx::Fake &, const uint32_t *a)
{
    if (OnStack(a[2])) drawfx::Note("event data", *reinterpret_cast<const uint32_t *>(uintptr_t(a[2])));
    return 0;
}
uint32_t ListenerOnQuery(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
// The temporary face of the system while the listeners connect: the behaviour is the system block itself.
uint32_t SourceQuery(drawfx::Fake &self, const uint32_t *a)
{
    if (a[0] != nGZEvent::cIAbstractBehaviorType::kIid || !a[1]) return 0;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = self.owner;
    return 1;
}

// ---- the registry: a map placed over the original's static, its guards marked done ----
char gNames[3][8];
const uint32_t kRegistryIds[] = {0x10, cGraphicSystemDX::kDevicesBuiltEvent, 0xa0000000};

class RegistryScope {
public:
    RegistryScope()
    {
        std::memcpy(saved_, GameAddress<void>(kRegistryMap), sizeof(saved_));
        savedMapGuard_ = *GameAddress<int32_t>(kRegistryMapGuard);
        savedOnce_ = *GameAddress<uint8_t>(kRegistryOnce);
        savedHolderGuard_ = *GameAddress<int32_t>(kRegistryHolderGuard);
        map_ = ::new (GameAddress<void>(kRegistryMap)) cEventAttributeMap;
        *GameAddress<int32_t>(kRegistryMapGuard) = INT_MIN;
        *GameAddress<uint8_t>(kRegistryOnce) = 1;
        *GameAddress<int32_t>(kRegistryHolderGuard) = INT_MIN;
        for (unsigned i = 0; i < 3; ++i) {
            std::snprintf(gNames[i], sizeof(gNames[i]), "g%u", i);
            (*map_)[kRegistryIds[i]] = {kRegistryIds[i], gNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
        }
    }
    ~RegistryScope()
    {
        std::destroy_at(map_);
        std::memcpy(GameAddress<void>(kRegistryMap), saved_, sizeof(saved_));
        *GameAddress<int32_t>(kRegistryMapGuard) = savedMapGuard_;
        *GameAddress<uint8_t>(kRegistryOnce) = savedOnce_;
        *GameAddress<int32_t>(kRegistryHolderGuard) = savedHolderGuard_;
    }
    RegistryScope(const RegistryScope &) = delete;
    RegistryScope &operator=(const RegistryScope &) = delete;

private:
    uint8_t saved_[sizeof(cEventAttributeMap)];
    int32_t savedMapGuard_;
    uint8_t savedOnce_;
    int32_t savedHolderGuard_;
    cEventAttributeMap *map_;
};

// The event manager cell, restored at the end.
class Cells {
public:
    Cells() : manager_(*GameAddress<uint32_t>(kEventManagerCell)) {}
    ~Cells() { *GameAddress<uint32_t>(kEventManagerCell) = manager_; }
    Cells(const Cells &) = delete;
    Cells &operator=(const Cells &) = delete;

private:
    uint32_t manager_;
};

struct StandIns {
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::StandIn construct{0x167200, &cDeviceDXAdapters::Construct, &StandConstruct};
    shape::StandIn init{0x169cc0, &cDeviceDXAdapters::Init, &StandInit};
    shape::StandIn pixelFormats{0x15eb10, &cPixelFormatsManagerDX::Instance, &StandPixelFormats};
    shape::StandIn shaders{0x1758c0, &cShaderProgramsManagerDX::Instance, &StandShaders};
    shape::StandIn shaderRelease{0x176e40, &cShaderProgramsManagerDX::ReleaseLinker, &StandShaderRelease};
    shape::StandIn vertexFormats{0x16db40, &cVertexFormatsManager::Instance, &StandVertexFormats};
    shape::StandIn declarations{0x16e760, &cVertexDeclarationsManagerDX::Instance, &StandDeclarations};
    shape::StandIn imageManip{0x16c0c0, &cImageManipDX::Instance, &StandImageManip};
    shape::CodePatch errorString{0xd5663, reinterpret_cast<const void *>(&StandErrorString)};
    shape::CodePatch createMesh{0x1c087, reinterpret_cast<const void *>(&StandCreateMesh)};
    shape::CodePatch optimizeFaces{0x1c0e1, reinterpret_cast<const void *>(&StandOptimizeFaces)};
    shape::CodePatch optimizeVertices{0x1546d, reinterpret_cast<const void *>(&StandOptimizeVertices)};
};

// ---- seeding ----
uint32_t Pick(std::mt19937 &gen, unsigned oneIn) { return gen() % oneIn == 0; }
uint32_t AnyMonitor(std::mt19937 &gen) { return Pick(gen, 6) ? 0u : kMonitors[gen() % 3]; }

void SeedD3D(std::mt19937 &gen, D3DData &d, uint32_t listed, bool sameCount)
{
    d.count = sameCount ? listed : gen() % 9;
    d.halMask = Pick(gen, 3) ? 0x3fu : gen() & 0xff;
    d.refHr = Pick(gen, 3) ? 0x8876086cu : 0u;
    d.idFailMask = Pick(gen, 3) ? 1u << (gen() % 6) : 0u;
    for (unsigned i = 0; i < 8; ++i) {
        d.guid[i] = 0x9000 + i;
        d.monitor[i] = AnyMonitor(gen);
    }
}

void SeedCase(std::mt19937 &gen, Case &c, uint32_t op)
{
    std::memset(&c, 0, sizeof(c));
    c.op = op;
    for (uint8_t &b : c.object) b = uint8_t(gen());
    for (auto &device : c.devices)
        for (uint8_t &b : device) b = uint8_t(gen());
    // The list: a fresh system (Init) has none; Init builds onto an empty list as the game's does.
    c.listed = op == kInit ? 0 : gen() % 7;
    if (Pick(gen, 3)) c.listed = 0;
    if (op == kOnResize || op == kOnCanvas || op == kFindWindow) c.listed = 1 + gen() % 6;
    c.capacity = c.listed + (Pick(gen, 2) ? 0 : gen() % 3);
    c.hasD3D = !Pick(gen, 5);
    // The current device: often valid; the checked paths (device events, slot 21) also get -1 and out of range.
    int32_t current = c.listed ? int32_t(gen() % c.listed) : -1;
    if (Pick(gen, 4)) current = -1;
    if (op != kOnResize && op != kOnCanvas && Pick(gen, 4)) current = int32_t(c.listed + gen() % 3);
    Put<int32_t>(c.object, 0x454, current);
    Put<uint32_t>(c.object, 4, (2 + gen() % 4) | (Pick(gen, 2) ? 0x80000000u : 0));
    if (op == kRelease && Pick(gen, 3)) Put<uint32_t>(c.object, 4, (Get<uint32_t>(c.object, 4) & 0x80000000u) | 2);
    if (op == kReleaseLast) Put<uint32_t>(c.object, 4, (Get<uint32_t>(c.object, 4) & 0x80000000u) | 1);
    c.object[0x10] = uint8_t(Pick(gen, 2));
    c.object[0x11] = uint8_t(Pick(gen, 2));
    // A pending restore only with a current device (it is set only then).
    c.object[0x424] = uint8_t(current >= 0 && Pick(gen, 2));
    for (unsigned i = 0; i < 3; ++i) SeedD3D(gen, c.d3d[i], c.listed, i == 2 && !Pick(gen, 4));
    // The fresh Direct3D's identifiers: the devices' enumerations agree unless one changed.
    for (unsigned i = 0; i < kDevices; ++i) c.enumGuid[i] = 0x9000 + i % 6;
    if (Pick(gen, 4)) c.enumGuid[gen() % 6] ^= 0x100;
    c.creates[0] = op == kOnCanvas ? 2 : (Pick(gen, 6) ? 0 : 1);
    c.creates[1] = Pick(gen, 5) ? 0 : 1;
    c.constructThrow = Pick(gen, 10) ? 1 + gen() % 6 : 0;
    for (bool &b : c.initOk) b = !Pick(gen, 4);
    for (auto &s : c.settings)
        for (uint32_t &w : s) w = gen();
    c.hasDevice = gen();
    c.fullscreen = gen() & gen();
    c.apply = gen() | gen();
    for (uint32_t &m : c.monitor) m = AnyMonitor(gen);
    for (unsigned i = 0; i < kDevices; ++i) {
        c.devices[i][0x71] = uint8_t(Pick(gen, 3));
        c.devices[i][0x73] = uint8_t(Pick(gen, 5));
    }
    c.flag18 = Pick(gen, 4);
    c.windowMonitor = AnyMonitor(gen);
    c.noFramework = (op == kShutdown) && Pick(gen, 4);
    c.connections = gen() % 64; // bits 0-2: listener i on event i; bits 3-5: listener i + 1 on event i too
    c.listenerAnswers = gen() % 8;
    for (uint32_t &w : c.data.message) w = gen();
    for (uint32_t &w : c.data.words) w = gen();
    for (uint32_t &w : c.data.remap) w = gen();
    const uint32_t codes[] = {0, 1, 2, 3, 4};
    if (op == kOnState) c.data.message[1] = codes[gen() % 5];
    if (op == kOnCanvas) c.data.message[1] = Pick(gen, 5) ? gen() % 10 : 8;
    if (op == kOnResize) c.data.message[5] = Pick(gen, 3) ? 0x10u << (gen() % 2) : (Pick(gen, 2) ? 0u : 0x08u);
    for (uint32_t &w : c.args) w = gen();
}

// ---- a side's world ----
cConnectionManager *gManager = nullptr;
std::vector<void *> gList; // the side's list array (freed at the end unless the system freed it)

void ResetFakes(const Case &c)
{
    for (unsigned i = 0; i < kD3Ds; ++i) {
        gD3D[i].Reset<fx_d3d9::IDirect3D9>(kD3DId0 + i);
        gD3D[i].owner = const_cast<D3DData *>(&c.d3d[i]);
        gD3D[i].On(4, &D3DCount);
        gD3D[i].On(5, &D3DIdentifier);
        gD3D[i].On(14, &D3DCaps);
        gD3D[i].On(15, &D3DMonitor);
    }
    for (unsigned i = 0; i < kDevices; ++i) {
        gDev[i].Reset<kDeviceIface>(kDeviceId0 + i);
        gDev[i].Embed(gDeviceBlocks[i]);
        gDev[i].On(33, &DeviceSettings);
        gDev[i].Returns(34, (c.hasDevice >> i) & 1);
        gDev[i].Returns(37, (c.fullscreen >> i) & 1);
        gDev[i].Returns(23, (c.apply >> i) & 1);
        gDev[i].Returns(45, &gEnum[i]);
        gDev[i].Returns(47, &gMonitor[i]);
        gMonitor[i].Reset<kMonitorIface>(kMonitorId0 + i);
        gMonitor[i].Returns(8, c.monitor[i]);
        gEnum[i].Reset<kEnumIface>(kEnumId0 + i);
        gEnum[i].On(27, &EnumIdentifier);
    }
    gWindow.Reset<kWindowIface>(kWindowId);
    gWindow.Returns(39, c.flag18);
    gWindow.Returns(42, c.windowMonitor);
    gWindowB.Reset<kWindowIface>(kWindowBId);
    for (unsigned i = 0; i < kListeners; ++i) {
        gListener[i].Reset<kListenerIface>(kListenerId0 + i);
        gListener[i].AddFace(nGZEvent::cIAbstractBehaviorType::kIid, gListener[i]);
        gListener[i].On(3, &ListenerOnEvent);
        gListener[i].On(4, &ListenerOnQuery);
        gListener[i].value[0] = (c.listenerAnswers >> i) & 1;
    }
    gSource.Reset<kListenerIface>(kSourceId);
    gSource.On(0, &SourceQuery);
}

struct VTables {
    void *primary, *service;
};

void BuildSide(const Case &c, d3dfx::ErrorLog &errors, impfx::Imports &imp, VTables vt)
{
    drawfx::ResetAll();
    imp.BeginSide();
    gScript = {&c, 0, 0};
    std::memcpy(gObject, c.object, kSize);
    std::memcpy(gDeviceBlocks, c.devices, sizeof(gDeviceBlocks));
    gData = c.data;
    ResetFakes(c);
    errors.Answer(kWindowService, kWindowCanvasIid, gWindow);
    errors.Answer(kWindowService, kWindowBehaviorIid, gWindowB);
    // The list.
    void **first = c.capacity ? static_cast<void **>(::operator new(c.capacity * 4)) : nullptr;
    for (uint32_t i = 0; i < c.capacity; ++i) first[i] = i < c.listed ? gDeviceBlocks[i] : nullptr;
    Put<void *>(gObject, 0x418, first);
    Put<void *>(gObject, 0x41c, first + c.listed);
    Put<void *>(gObject, 0x420, first + c.capacity);
    Put<void *>(gObject, 0x14, c.hasD3D ? &gD3D[0] : nullptr);
    drawfx::AddRange(gObject, kSize, kObjectTag);
    drawfx::AddRange(gDeviceBlocks, sizeof(gDeviceBlocks), kDeviceTag);
    drawfx::AddRange(&gData, sizeof(gData), kDataTag);
    drawfx::AddRange(gMarks, sizeof(gMarks), kMarkTag);
    // The connection manager, with the listeners connected to the system (through a stand-in face while they connect).
    gManager = cConnectionManagerAdapters::Construct(::operator new(cConnectionManager::kSize), nullptr);
    drawfx::AddRange(gManager, cConnectionManager::kSize, kConnTag);
    *GameAddress<void *>(kEventManagerCell) = gManager;
    gSource.Embed(gObject);
    for (unsigned i = 0; i < kListeners; ++i)
        if ((c.connections >> i) & 1)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(gObject), kListenEvents[i], reinterpret_cast<cIGZUnknown *>(&gListener[i]),
                kHeardAs[i]);
    // A second listener on an event (after the first), so that AskListeners sees mixed answers in both orders.
    for (unsigned i = 0; i < kListeners; ++i)
        if ((c.connections >> (3 + i)) & 1)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(gObject), kListenEvents[i],
                reinterpret_cast<cIGZUnknown *>(&gListener[(i + 1) % kListeners]), kHeardAs[(i + 1) % kListeners]);
    Put<void *>(gObject, 0, vt.primary);
    Put<void *>(gObject, 8, vt.service);
    drawfx::gLog.clear();
}

void DestroySide()
{
    cConnectionManagerAdapters::Destruct(gManager, nullptr);
    ::operator delete(gManager);
    gManager = nullptr;
}

void NoteRefs()
{
    std::vector<const drawfx::Fake *> all = {&gWindow, &gWindowB, &gListener[0], &gListener[1], &gListener[2]};
    for (unsigned i = 0; i < kD3Ds; ++i) all.push_back(&gD3D[i]);
    for (unsigned i = 0; i < kDevices; ++i) {
        all.push_back(&gDev[i]);
        all.push_back(&gMonitor[i]);
        all.push_back(&gEnum[i]);
    }
    for (const drawfx::Fake *f : all) drawfx::Note("refs", {f->id, uint32_t(f->refs)});
}

void NoteConnections()
{
    auto *m = static_cast<nGZEvent::cIConnectionManager *>(gManager);
    drawfx::Note("connected", m->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(gObject)));
    for (unsigned i = 0; i < kListeners; ++i)
        drawfx::Note("from", {i, m->CountFrom(reinterpret_cast<cIGZUnknown *>(gObject), kListenEvents[i])});
}

void NoteIdRange(nGZEvent::cAttributeIDRange range)
{
    unsigned steps = 0;
    while (steps < 8 && !range.mCurrent->IsEqual(range.mEnd)) {
        drawfx::Note("id", range.mCurrent->Get());
        range.mCurrent->Next();
        ++steps;
    }
    nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
}

// The device a device event comes from: the current one, another listed one, or none of them.
uint32_t EventSource(const Case &c)
{
    const uint32_t pick = c.args[3] % 4;
    const int32_t current = Get<int32_t>(c.object, 0x454);
    if (pick < 2 && current >= 0 && uint32_t(current) < c.listed) return Word(gDeviceBlocks[current]);
    if (pick == 2 && c.listed) return Word(gDeviceBlocks[c.args[4] % c.listed]);
    return c.args[4];
}

uint32_t EventType(const Case &c)
{
    switch (c.op) {
    case kOnConnection: return nGZEvent::cConnectionChangedEvent::kType;
    case kOnState: return cGraphicSystemDX::kDeviceStateEvent;
    case kOnCanvas: return cGraphicSystemDX::kCanvasEvent;
    case kOnResize: return cGraphicSystemDX::kResizeEvent;
    case kOnFrame: return cGraphicSystemDX::kDeviceFrameEvent;
    default: return c.args[0];
    }
}

const uint32_t kIids[] = {cGraphicSystemDX::kIidGraphicSystemDX, cGraphicSystemDX::kIidGraphicSystem, 1,
                          cGraphicSystemDX::kIidBehavior2, cGraphicSystemDX::kIidBehaviorType, cIGZSystemService::kIID,
                          0};

// ---- one operation, on the original (at its RVAs) or ours (through our vtables) ----
template <class F> F At(difftest::Context &ctx, uint32_t rva) { return ctx.Original<F>(rva); }

void RunOriginal(difftest::Context &ctx, const Case &c)
{
    void *const self = gObject;
    void *const service = gObject + 8;
    const uint32_t *a = c.args;
    using V = void(__thiscall *)(void *);
    using B = bool(__thiscall *)(void *);
    using P = void *(__thiscall *)(void *);
    using W = uint32_t(__thiscall *)(void *);
    switch (c.op) {
    case kQueryInterface: {
        const uint32_t iid = a[0] % 7 == 6 ? a[1] : kIids[a[0] % 7];
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        const bool ok = At<bool(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x15a1d0)(self, iid, a[2] % 5 ? &out : nullptr);
        drawfx::Note("ret", {ok, Norm(out)});
        break;
    }
    case kAddRef: drawfx::Note("ret", At<W>(ctx, 0x146a20)(self)); break;
    case kRelease: drawfx::Note("ret", At<W>(ctx, 0x15a530)(self)); break;
    case kOnConnection:
    case kOnState:
    case kOnCanvas:
    case kOnResize:
    case kOnFrame:
    case kOnOther:
        At<void(__thiscall *)(void *, uint32_t, uint32_t, void *, uint32_t)>(ctx, 0x159d90)(self, EventType(c), a[1], gData.message, EventSource(c));
        break;
    case kOnQuery: drawfx::Note("ret", At<bool(__thiscall *)(void *, uint32_t, uint32_t, void *)>(ctx, 0x147ce0)(self, kListenEvents[a[0] % 3], a[1], gData.words)); break;
    case kNotify: At<void(__thiscall *)(void *, uint32_t, uint32_t, void *)>(ctx, 0x15a6f0)(self, kListenEvents[a[0] % 3], a[1], gData.words); break;
    case kAsk: drawfx::Note("ret", At<bool(__thiscall *)(void *, uint32_t, uint32_t, void *)>(ctx, 0x159820)(self, kListenEvents[a[0] % 3], a[1], gData.words)); break;
    case kConnect: At<void(__thiscall *)(void *, uint32_t, void *, uint32_t)>(ctx, 0x1592f0)(self, kListenEvents[a[0] % 3], &gListener[a[1] % 3], a[2] % 4); break;
    case kDisconnect: At<void(__thiscall *)(void *, uint32_t, void *, uint32_t)>(ctx, 0x159780)(self, kListenEvents[a[0] % 3], &gListener[a[0] % 3], kHeardAs[a[0] % 3]); break;
    case kIsActive: drawfx::Note("ret", At<W>(ctx, 0x147cf0)(self)); break;
    case kEventCount: drawfx::Note("ret", At<W>(ctx, 0x15a160)(self)); break;
    case kEventIds: {
        nGZEvent::cAttributeIDRange range;
        At<void *(__thiscall *)(void *, nGZEvent::cAttributeIDRange *)>(ctx, 0x158fa0)(self, &range);
        NoteIdRange(range);
        break;
    }
    case kEventAttribute: {
        nGZEvent::cEventAttribute r;
        const uint32_t ids[] = {kRegistryIds[0], kRegistryIds[1], kRegistryIds[2], a[1]};
        At<void *(__thiscall *)(void *, nGZEvent::cEventAttribute *, uint32_t)>(ctx, 0x159210)(self, &r, ids[a[0] % 4]);
        drawfx::Note("attribute", {r.mId, Word(r.mName), r.mWord08, r.mWord0C, r.mWord10, r.mWord14});
        break;
    }
    case kEmptySlots: At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(ctx, 0x377e0)(self, a[0], a[1], a[2]); break;
    case kSimple: {
        At<void(__thiscall *)(void *, int32_t)>(ctx, 0x15a7e0)(self, int32_t(a[0] % 9) - 1);
        drawfx::Note("current", At<W>(ctx, 0x159660)(self));
        drawfx::Note("count", At<W>(ctx, 0x15a1b0)(self));
        if (c.listed) drawfx::Note("device", Norm(At<void *(__thiscall *)(void *, uint32_t)>(ctx, 0x159670)(self, a[1] % c.listed)));
        for (uint32_t kind = 0; kind < 4; ++kind) {
            drawfx::Note("has", At<bool(__thiscall *)(void *, uint32_t)>(ctx, 0x15a090)(self, kind));
            drawfx::Note("d3d", Norm(At<void *(__thiscall *)(void *, uint32_t)>(ctx, 0x159750)(self, kind)));
        }
        drawfx::Note("palette", Norm(At<P>(ctx, 0x158f90)(self)));
        break;
    }
    case kManagers: {
        const uint32_t rvas[] = {0x15a1c0, 0x15a8f0, 0x15a8e0, 0x15a7f0, 0x159f80, 0x15a8e0};
        for (uint32_t rva : rvas) drawfx::Note("manager", Norm(At<P>(ctx, rva)(self)));
        break;
    }
    case kLibrary: {
        drawfx::Note("text", Word(At<const char *(__thiscall *)(void *, long)>(ctx, 0x15a8d0)(self, long(a[0]))));
        drawfx::Note("mesh", uint32_t(At<long(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, void *, void **)>(ctx, 0x15a8a0)(
                                 self, a[1], a[2], a[3], a[4], &gD3D[1], reinterpret_cast<void **>(gData.words))));
        drawfx::Note("faces", uint32_t(At<long(__thiscall *)(void *, const void *, uint32_t, uint32_t, int32_t, uint32_t *)>(ctx, 0x15a8b0)(
                                  self, gData.words, a[1], a[2], int32_t(a[3] % 2), gData.remap)));
        drawfx::Note("vertices", uint32_t(At<long(__thiscall *)(void *, const void *, uint32_t, uint32_t, int32_t, uint32_t *)>(ctx, 0x15a8c0)(
                                     self, gData.words, a[4], a[5], int32_t(a[6] % 2), gData.remap)));
        break;
    }
    case kFindWindow: drawfx::Note("ret", At<W>(ctx, 0x159680)(self)); break;
    case kServiceSlots: {
        void *out = nullptr;
        const uint32_t iid = a[0] % 7 == 6 ? a[1] : kIids[a[0] % 7];
        drawfx::Note("qi", {At<bool(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x15a23a)(service, iid, &out), Norm(out)});
        drawfx::Note("addref", At<W>(ctx, 0x146a5d)(service));
        drawfx::Note("release", At<W>(ctx, 0x15a586)(service));
        drawfx::Note("id", At<W>(ctx, 0x147a30)(service));
        At<void(__thiscall *)(void *, uint32_t)>(ctx, 0x1489e0)(service, a[2]);
        drawfx::Note("id", At<W>(ctx, 0x147a30)(service));
        drawfx::Note("priority", At<W>(ctx, 0x159a30)(service));
        drawfx::Note("running", At<B>(ctx, 0x1480a0)(service));
        At<void(__thiscall *)(void *, bool)>(ctx, 0x1489f0)(service, a[3] % 2 != 0);
        drawfx::Note("running", At<B>(ctx, 0x1480a0)(service));
        drawfx::Note("tick", At<bool(__thiscall *)(void *, uint32_t)>(ctx, 0x148260)(service, a[4]));
        break;
    }
    case kInit: drawfx::Note("ret", At<B>(ctx, 0x159f90)(service)); break;
    case kShutdown: drawfx::Note("ret", At<B>(ctx, 0x15a800)(service)); break;
    case kDestruct: At<void *(__thiscall *)(void *, uint32_t)>(ctx, 0x158f40)(self, 0); break;
    case kReleaseLast: drawfx::Note("ret", At<W>(ctx, 0x15a530)(self)); break;
    case kDelete: At<void *(__thiscall *)(void *, uint32_t)>(ctx, 0x158f40)(self, 1); break;
    }
    (void)static_cast<V>(nullptr);
}

void RunOurs(const Case &c)
{
    cIGraphicSystemDX *const s = System();
    cIGZSystemService *const service = Service();
    const uint32_t *a = c.args;
    switch (c.op) {
    case kQueryInterface: {
        const uint32_t iid = a[0] % 7 == 6 ? a[1] : kIids[a[0] % 7];
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        const bool ok = s->QueryInterface(iid, a[2] % 5 ? &out : nullptr);
        drawfx::Note("ret", {ok, Norm(out)});
        break;
    }
    case kAddRef: drawfx::Note("ret", s->AddRef()); break;
    case kRelease: drawfx::Note("ret", s->Release()); break;
    case kOnConnection:
    case kOnState:
    case kOnCanvas:
    case kOnResize:
    case kOnFrame:
    case kOnOther: s->OnEvent(EventType(c), a[1], gData.message, EventSource(c)); break;
    case kOnQuery: drawfx::Note("ret", s->OnQuery(kListenEvents[a[0] % 3], a[1], gData.words)); break;
    case kNotify: s->NotifyListeners(kListenEvents[a[0] % 3], a[1], gData.words); break;
    case kAsk: drawfx::Note("ret", s->AskListeners(kListenEvents[a[0] % 3], a[1], gData.words)); break;
    case kConnect: s->Connect(kListenEvents[a[0] % 3], reinterpret_cast<cIGZUnknown *>(&gListener[a[1] % 3]), a[2] % 4); break;
    case kDisconnect: s->Disconnect(kListenEvents[a[0] % 3], reinterpret_cast<cIGZUnknown *>(&gListener[a[0] % 3]), kHeardAs[a[0] % 3]); break;
    case kIsActive: drawfx::Note("ret", s->IsActive()); break;
    case kEventCount: drawfx::Note("ret", s->GetEventCount()); break;
    case kEventIds: NoteIdRange(s->GetEventIds()); break;
    case kEventAttribute: {
        const uint32_t ids[] = {kRegistryIds[0], kRegistryIds[1], kRegistryIds[2], a[1]};
        const nGZEvent::cEventAttribute r = s->GetEventAttribute(ids[a[0] % 4]);
        drawfx::Note("attribute", {r.mId, Word(r.mName), r.mWord08, r.mWord0C, r.mWord10, r.mWord14});
        break;
    }
    case kEmptySlots:
        s->SetAttribute(a[0], a[1], a[2]);
        s->GetAttribute(a[0], a[1], a[2]);
        break;
    case kSimple: {
        s->SetCurrentDevice(int32_t(a[0] % 9) - 1);
        drawfx::Note("current", uint32_t(s->GetCurrentDevice()));
        drawfx::Note("count", s->GetDeviceCount());
        if (c.listed) drawfx::Note("device", Norm(s->GetDevice(a[1] % c.listed)));
        for (uint32_t kind = 0; kind < 4; ++kind) {
            drawfx::Note("has", s->HasDirect3D(kind));
            drawfx::Note("d3d", Norm(s->GetDirect3D(kind)));
        }
        drawfx::Note("palette", Norm(s->GetPalette()));
        break;
    }
    case kManagers:
        drawfx::Note("manager", Norm(s->GetPixelFormatsManager()));
        drawfx::Note("manager", Norm(s->GetVertexFormatsManager()));
        drawfx::Note("manager", Norm(s->GetVertexDeclarationsManager()));
        drawfx::Note("manager", Norm(s->GetShaderProgramsManager()));
        drawfx::Note("manager", Norm(s->GetImageManip()));
        drawfx::Note("manager", Norm(s->GetVertexDeclarationsManager27()));
        break;
    case kLibrary:
        drawfx::Note("text", Word(s->GetErrorString(long(a[0]))));
        drawfx::Note("mesh", uint32_t(s->CreateMeshFVF(a[1], a[2], a[3], a[4], reinterpret_cast<IDirect3DDevice9 *>(&gD3D[1]),
                                                       reinterpret_cast<void **>(gData.words))));
        drawfx::Note("faces", uint32_t(s->OptimizeFaces(gData.words, a[1], a[2], int32_t(a[3] % 2), gData.remap)));
        drawfx::Note("vertices", uint32_t(s->OptimizeVertices(gData.words, a[4], a[5], int32_t(a[6] % 2), gData.remap)));
        break;
    case kFindWindow: drawfx::Note("ret", uint32_t(s->FindWindowDevice())); break;
    case kServiceSlots: {
        void *out = nullptr;
        const uint32_t iid = a[0] % 7 == 6 ? a[1] : kIids[a[0] % 7];
        drawfx::Note("qi", {service->QueryInterface(iid, &out), Norm(out)});
        drawfx::Note("addref", service->AddRef());
        drawfx::Note("release", service->Release());
        drawfx::Note("id", service->GetServiceID());
        service->SetServiceID(a[2]);
        drawfx::Note("id", service->GetServiceID());
        drawfx::Note("priority", uint32_t(service->GetServicePriority()));
        drawfx::Note("running", service->IsServiceRunning());
        service->SetServiceRunning(a[3] % 2 != 0);
        drawfx::Note("running", service->IsServiceRunning());
        drawfx::Note("tick", service->OnTick(a[4]));
        break;
    }
    case kInit: drawfx::Note("ret", service->Init()); break;
    case kShutdown: drawfx::Note("ret", service->Shutdown()); break;
    case kDestruct: RawSlot<34, void *>(s, 0u); break;
    case kReleaseLast: drawfx::Note("ret", s->Release()); break;
    case kDelete: RawSlot<34, void *>(s, 1u); break;
    }
}

// What a side leaves.
struct Result {
    drawfx::Log log;
    std::vector<uint8_t> object, devices;
    std::vector<uint32_t> list;
    long long blocks, bytes;
    std::string thrown;
};

// The system's bytes but its vtable words (+0, +8) and the list's pointers (+0x418..+0x423).
std::vector<uint8_t> ObjectBytes()
{
    std::vector<uint8_t> bytes(gObject + 4, gObject + 8);
    bytes.insert(bytes.end(), gObject + 12, gObject + 0x418);
    bytes.insert(bytes.end(), gObject + 0x424, gObject + kSize);
    return bytes;
}

// The list as a side leaves it: size, capacity and the devices (normalised); after a destructor, which pointers are null.
std::vector<uint32_t> ListOf(bool destroyed)
{
    void **const first = Get<void **>(gObject, 0x418);
    void **const last = Get<void **>(gObject, 0x41c);
    void **const end = Get<void **>(gObject, 0x420);
    if (destroyed) return {first == nullptr, last == nullptr, end == nullptr};
    std::vector<uint32_t> list = {uint32_t(last - first), uint32_t(end - first)};
    for (void **p = first; p != last; ++p) list.push_back(Norm(*p));
    return list;
}

template <class Run> Result Side(const Case &c, d3dfx::ErrorLog &errors, impfx::Imports &imp, VTables vt, Run run)
{
    gObject = Gone(c.op) ? static_cast<uint8_t *>(::operator new(kSize)) : gObjectBlock;
    BuildSide(c, errors, imp, vt);
    std::string thrown;
    long long blocks = 0, bytes = 0;
    drawfx::gLog = drawfx::Log();
    drawfx::gLog.reserve(4096);
    void *const framework = *GameAddress<void *>(d3dfx::ErrorLog::kServiceRva);
    if (c.noFramework) *GameAddress<void *>(d3dfx::ErrorLog::kServiceRva) = nullptr;
    {
        difftest::AllocationCounter counter;
        try {
            run();
        } catch (const cError &e) {
            char text[48];
            std::snprintf(text, sizeof(text), "cError vt %d code %d", e.mVTable == GameAddress<void>(kErrorVTable), e.mCode);
            thrown = text;
        }
        counter.Stop();
        blocks = counter.Blocks();
        bytes = counter.Bytes();
    }
    *GameAddress<void *>(d3dfx::ErrorLog::kServiceRva) = framework;
    NoteRefs();
    const bool destroyed = c.op == kDestruct;
    if (!destroyed && !Gone(c.op)) NoteConnections();
    Result r;
    r.log = drawfx::gLog;
    if (!Gone(c.op)) {
        r.object = ObjectBytes();
        r.list = ListOf(destroyed);
        // The side's list array (unless the destructor freed it).
        if (!destroyed) ::operator delete(Get<void *>(gObject, 0x418));
    }
    r.devices.assign(&gDeviceBlocks[0][0], &gDeviceBlocks[0][0] + sizeof(gDeviceBlocks));
    r.blocks = blocks;
    r.bytes = bytes;
    r.thrown = thrown;
    DestroySide();
    gObject = gObjectBlock;
    return r;
}

const char *kOpNames[kOpCount] = {
    "QueryInterface", "AddRef", "Release", "OnEvent(connection)", "OnEvent(device state)", "OnEvent(canvas)",
    "OnEvent(resize)", "OnEvent(device frame)", "OnEvent(other)", "OnQuery", "NotifyListeners", "AskListeners",
    "Connect", "Disconnect", "IsActive", "GetEventCount", "GetEventIds", "GetEventAttribute", "Slot7/Slot10",
    "simple slots", "managers", "D3DX slots", "FindWindowDevice", "service slots", "Init", "Shutdown", "destructor",
    "Release (last)", "delete"};

// Our vtable words (a constructed object's; it holds nothing to release).
VTables OurVTables()
{
    alignas(16) static uint8_t scratch[kSize];
    ::new (scratch) cGraphicSystemDX;
    return {Get<void *>(scratch, 0), Get<void *>(scratch, 8)};
}

struct World {
    RegistryScope registry;
    shape::StandIn registryStand{0x15a250, &cGraphicSystemDXBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kRegistryMap>};
    Cells cells;
    StandIns stands;
    d3dfx::ErrorLog errors;
    impfx::Imports imp;
    explicit World(difftest::Context &ctx) : errors(ctx), imp(ctx) { IMPFX_ADD(imp, Direct3DCreate9).On(&Direct3DCreate); }
};

void Seen(const Result &r, std::set<std::string> &seen)
{
    for (const drawfx::Call &call : r.log) {
        if (!call.iface) seen.insert(call.note);
        else if (call.slot < call.iface->count) seen.insert(call.iface->slots[call.slot].name);
    }
    if (!r.thrown.empty()) seen.insert(r.thrown);
}

// Runs `rounds` cases of each op in `ops`. `needs`: what the original's logs must show over the sweep (note labels,
// slot names, thrown errors), so a sweep that never reaches a path fails.
void Sweep(difftest::Context &ctx, uint32_t seed, std::initializer_list<uint32_t> ops, unsigned rounds,
           std::initializer_list<const char *> needs = {})
{
    World world(ctx);
    const VTables ours = OurVTables();
    const VTables game = {GameAddress<void>(kVTableRva), GameAddress<void>(kServiceVTableRva)};
    std::mt19937 gen(seed);
    std::set<std::string> seen;
    for (uint32_t op : ops) {
        for (unsigned round = 0; round < rounds; ++round) {
            auto c = std::make_unique<Case>();
            SeedCase(gen, *c, op);
            const Result a = Side(*c, world.errors, world.imp, game, [&] { RunOriginal(ctx, *c); });
            const Result b = Side(*c, world.errors, world.imp, ours, [&] { RunOurs(*c); });
            Seen(a, seen);
            const char *label = kOpNames[op];
            if (!drawfx::Same(ctx, a.log, b.log, label, round)) return;
            if (a.object != b.object || a.devices != b.devices || a.list != b.list || a.blocks != b.blocks ||
                a.bytes != b.bytes || a.thrown != b.thrown) {
                size_t at = 0;
                while (at < a.object.size() && at < b.object.size() && a.object[at] == b.object[at]) ++at;
                size_t dev = 0;
                while (dev < a.devices.size() && a.devices[dev] == b.devices[dev]) ++dev;
                ctx.Fail("%s round %u: object %d (first difference at byte %zu of the compared ones) devices %d (byte %zu) "
                         "list %d blocks %lld/%lld bytes %lld/%lld thrown '%s'/'%s'",
                         label, round, a.object == b.object, at, a.devices == b.devices, dev, a.list == b.list, a.blocks,
                         b.blocks, a.bytes, b.bytes, a.thrown.c_str(), b.thrown.c_str());
                return;
            }
        }
    }
    for (const char *need : needs)
        if (!seen.count(need)) ctx.Fail("the sweep never reached '%s' (a vacuous path)", need);
}

// ---- the null device: each slot called with every argument pointing at a zeroed scratch ----
uint32_t gScratch[64];
template <size_t... I> uint32_t CallStd(void *fn, void *self, std::index_sequence<I...>)
{
    using F = uint32_t(__stdcall *)(void *, decltype((void)I, uint32_t{})...);
    return reinterpret_cast<F>(fn)(self, ((void)I, Word(gScratch))...);
}
uint32_t CallSlot(void *object, unsigned slot, unsigned words)
{
    void *const fn = (*static_cast<void ***>(object))[slot];
    switch (words) {
    case 0: return CallStd(fn, object, std::make_index_sequence<0>());
    case 1: return CallStd(fn, object, std::make_index_sequence<1>());
    case 2: return CallStd(fn, object, std::make_index_sequence<2>());
    case 3: return CallStd(fn, object, std::make_index_sequence<3>());
    case 4: return CallStd(fn, object, std::make_index_sequence<4>());
    case 5: return CallStd(fn, object, std::make_index_sequence<5>());
    case 6: return CallStd(fn, object, std::make_index_sequence<6>());
    case 7: return CallStd(fn, object, std::make_index_sequence<7>());
    case 8: return CallStd(fn, object, std::make_index_sequence<8>());
    case 9: return CallStd(fn, object, std::make_index_sequence<9>());
    case 10: return CallStd(fn, object, std::make_index_sequence<10>());
    default: return 0xdead;
    }
}

} // namespace

// Construction (0x159940 with GameNew handing out a block of seeded bytes; ours constructed over the same bytes): every
// byte the creator writes, and what it leaves.
DIFF_TEST(cGraphicSystemDX_Create, 0x159940)
{
    StandIns stands;
    std::mt19937 gen(0x159940);
    static uint8_t seeded[kSize], ours[kSize];
    static uint8_t *handOut = nullptr;
    struct New {
        static void *__cdecl Give(size_t size) { return size == kSize ? handOut : ::operator new(size); }
    };
    shape::CodePatch give{0x824632, reinterpret_cast<const void *>(&New::Give)};
    for (unsigned round = 0; round < 64; ++round) {
        for (uint8_t &x : seeded) x = uint8_t(gen());
        std::memcpy(ours, seeded, kSize);
        handOut = seeded;
        void *const made = ctx.Original<void *(__cdecl *)()>(0x159940)();
        if (made != seeded) ctx.Fail("round %u: the creator's object is not the block it was given", round);
        ::new (ours) cGraphicSystemDX;
        if (std::memcmp(seeded + 4, ours + 4, 4) != 0 || std::memcmp(seeded + 12, ours + 12, kSize - 12) != 0) {
            size_t at = 4;
            while (at < kSize && (at < 12 && at >= 8 || seeded[at] == ours[at])) ++at;
            ctx.Fail("round %u: the object differs at +0x%zx", round, at);
            return;
        }
        if (Get<void *>(seeded, 0) != GameAddress<void>(kVTableRva) || Get<void *>(seeded, 8) != GameAddress<void>(kServiceVTableRva))
            ctx.Fail("round %u: the original's vtable words are not 0xd9ce44 / 0xd9ced4", round);
    }
    // A failed allocation gives null.
    handOut = nullptr;
    if (ctx.Original<void *(__cdecl *)()>(0x159940)() != nullptr) ctx.Fail("the creator did not give null without memory");
    // Ours: an object with our vtables, counted by nothing, released by its owner.
    cGraphicSystemDX *const made = cGraphicSystemDX::Create();
    if (!made || made->mRefCount != 0 || made->mCurrentDevice != -1) ctx.Fail("Create: not a fresh system");
    delete made;
}

// The behaviour slots (0-14).
DIFF_TEST(cGraphicSystemDX_Behaviour, 0x15a6f0)
{
    Sweep(ctx, 0x15a6f0,
          {kQueryInterface, kAddRef, kRelease, kOnConnection, kOnOther, kOnQuery, kNotify, kAsk, kConnect, kDisconnect,
           kIsActive, kEventCount, kEventIds, kEventAttribute, kEmptySlots},
          96, {"OnEvent", "OnQuery", "id", "attribute"});
}

// The device events passed on (state and frame) and the window's resize.
DIFF_TEST(cGraphicSystemDX_DeviceEvents, 0x159d90)
{
    Sweep(ctx, 0x159d90, {kOnState, kOnFrame, kOnResize}, 200,
          {"shaders", "shaders.release", "OnEvent", "HasDeviceOrLost", "GetFlag18", "win.GetMonitor", "GetMonitor",
           "GetSettings", "ReleaseDevice", "ApplySettings", "OnDeviceLost"});
}

// The canvas handler: the adapters checked, the monitors brought up to date, saved settings restored (cDeviceDX's
// RestoreSettings 0x169480, ours RestoreDeviceSettings), or a rebuild.
DIFF_TEST(cGraphicSystemDX_Canvas, 0x159a40)
{
    Sweep(ctx, 0x159a40, {kOnCanvas}, 600,
          {"GetSettings", "GetAdapterIdentifier", "LogD3DError", "GetDeviceEnum45", "GetAdapterMonitor", "SetMonitor",
           "IsFullscreen", "OnDeviceLost", "dev.Disconnect", "win.Disconnect", "construct", "Init", "dev.Connect",
           "SetFlag19", "event data"});
}

// Slots 15-33 but 29 (cGraphicSystemDX_NullDevice) and the service face's.
DIFF_TEST(cGraphicSystemDX_Slots, 0x159750)
{
    Sweep(ctx, 0x159750, {kSimple, kManagers, kLibrary, kFindWindow, kServiceSlots}, 64,
          {"pixel formats", "vertex formats", "declarations", "shaders", "image manip", "DXGetErrorString9A",
           "D3DXCreateMeshFVF", "D3DXOptimizeFaces", "D3DXOptimizeVertices", "GetFlag18", "GetMonitor"});
}

// Init (BuildDevices 0x159410 and its errors), Shutdown (TearDown 0x15a590), the destructors.
DIFF_TEST(cGraphicSystemDX_Lifetime, 0x159f90)
{
    Sweep(ctx, 0x159f90, {kInit, kShutdown, kDestruct, kReleaseLast, kDelete}, 160,
          {"win.Connect", "construct", "Init", "dev.Connect", "event data", "GetDeviceCaps", "cError vt 1 code 1",
           "cError vt 1 code 2", "cError vt 1 code 7", "ReleaseDevice", "win.Disconnect", "dev.Disconnect", "Release"});
}

// Slot 29: the original's static device (its guard marked done, its vtable word as its constructor 0x16a820 sets it)
// and ours answer every Direct3D device slot alike (its slot bodies 0x16a880, 0x16a890, 0x16a8b0, 0x16a8c0, 0x16a8d0,
// 0x16a8e0, 0x16a940 among them); the original's deleting destructor 0x16a840 without the delete flag
// leaves the object as ours does.
DIFF_TEST(cGraphicSystemDX_NullDevice, 0x15a0d0)
{
    alignas(16) static uint8_t block[kSize] = {};
    void *const stored = *GameAddress<void *>(kNullDeviceRva);
    const int32_t guard = *GameAddress<int32_t>(kNullDeviceGuard);
    *GameAddress<int32_t>(kNullDeviceGuard) = INT_MIN;
    *GameAddress<void *>(kNullDeviceRva) = GameAddress<void>(kNullDeviceVTable);
    void *const original = ctx.Original<void *(__thiscall *)(void *)>(0x15a0d0)(block);
    ::new (block) cGraphicSystemDX;
    IDirect3DDevice9 *const ours = reinterpret_cast<cIGraphicSystemDX *>(block)->GetNullDevice();
    if (original != GameAddress<void>(kNullDeviceRva)) ctx.Fail("the original's slot 29 is not its static device");
    if (!ours || ours != reinterpret_cast<cIGraphicSystemDX *>(block)->GetNullDevice()) ctx.Fail("ours is not one device");
    for (unsigned slot = 0; slot < fx_d3d9::IDirect3DDevice9.count; ++slot) {
        const drawfx::Slot &s = fx_d3d9::IDirect3DDevice9.slots[slot];
        uint32_t results[2];
        uint32_t scratch[2][64];
        void *const objects[2] = {original, ours};
        for (unsigned side = 0; side < 2; ++side) {
            std::memset(gScratch, 0, sizeof(gScratch));
            if (s.ret == drawfx::Ret::Float) {
                using F = float(__stdcall *)(void *);
                results[side] = std::bit_cast<uint32_t>(reinterpret_cast<F>((*static_cast<void ***>(objects[side]))[slot])(objects[side]));
            } else {
                results[side] = CallSlot(objects[side], slot, s.words);
            }
            std::memcpy(scratch[side], gScratch, sizeof(gScratch));
        }
        // Slots 11, 21 and 22 return nothing (eax is whatever the body left).
        const bool returnsNothing = slot == 11 || slot == 21 || slot == 22;
        if ((!returnsNothing && results[0] != results[1]) || std::memcmp(scratch[0], scratch[1], sizeof(scratch[0])) != 0)
            ctx.Fail("slot %u (%s): %08x / %08x, out-arguments %s", slot, s.name, results[0], results[1],
                     std::memcmp(scratch[0], scratch[1], sizeof(scratch[0])) ? "differ" : "same");
    }
    // The deleting destructor without the delete flag: the object's word stays the vtable.
    alignas(4) uint8_t a[4], b[4];
    std::memcpy(a, GameAddress<void>(kNullDeviceRva), 4);
    ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x16a840)(a, 0);
    if (Get<void *>(a, 0) != GameAddress<void>(kNullDeviceVTable)) ctx.Fail("the original's destructor changed the vtable word");
    ::new (b) cNullDevice;
    std::destroy_at(reinterpret_cast<cNullDevice *>(b));
    // The constructor 0x16a820 and destructor body 0x16a830 store the vtable and nothing else (ours: the compiler's).
    alignas(4) uint8_t c[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    if (ctx.Original<void *(__thiscall *)(void *)>(0x16a820)(c) != c || Get<void *>(c, 0) != GameAddress<void>(kNullDeviceVTable) ||
        Get<uint32_t>(c, 4) != 0x08070605u)
        ctx.Fail("the original's constructor wrote more than its vtable word");
    std::memset(c, 0x11, sizeof(c));
    ctx.Original<void(__thiscall *)(void *)>(0x16a830)(c);
    if (Get<void *>(c, 0) != GameAddress<void>(kNullDeviceVTable) || Get<uint32_t>(c, 4) != 0x11111111u)
        ctx.Fail("the original's destructor body wrote more than its vtable word");
    if (sizeof(cNullDevice) != 4) ctx.Fail("our null device is not one word");
    *GameAddress<void *>(kNullDeviceRva) = stored;
    *GameAddress<int32_t>(kNullDeviceGuard) = guard;
}
