// Differential tests for nGZGraphic4::cDeviceDX as a whole class (src/gz/cDeviceDX.h, lane NC): the constructor, Init,
// the destructors and every slot, against the campaign's shared fake Direct3D (tests/d3dfx.h) and fakes of the engine
// objects the device holds.
//
// One world per case, built alike for each side from the case's seed:
// - The device object is a static 0x78-byte block at one address (a registered range): the original's side gets the
//   game's vtable word, ours ours; every other byte is the case's. The original is called at its slot RVAs, ours
//   through our vtable (devirtualised calls inside each side follow its own vtable, as in the game).
// - The state cache and the resource manager are blocks of seeded bytes whose vtable words are fakes (Embed): the device
//   only reads a few of their fields and calls their slots (state 2 / 9, manager 1 / 2 / 12). The state cache's
//   non-virtual members the device calls (Flush, MarkTargetsDirty, SetBackBufferTarget, the texture-coordinate pair,
//   ReleaseDeviceObjects) are stood in on both sides (shape::StandIn: the image's and ours), so the device is compared
//   alone; they are verified by their own tests (tests/gz_device_state_dx*.cpp).
// - Code the device calls that has its own tests (CreateDevice 0x167ec0; the enumeration's constructor, Refresh and
//   dump, 0x1774a0 / 0x178b80 / 0x1788f0, and the state's dump 0x17da90, tests/gz_device_state_dump.cpp, stood in on
//   both sides: the image's and ours) and game code it still calls (the manager's 0x17ad50 / 0x17bb80, the pixel formats
//   getter 0x18acd0, the director's framework getter) is stood in by recorders; operator new / delete are ours on both sides
//   (GameNew / GameDelete).
// - Events: our real cConnectionManager in the device's cell (0x10ca22c), with listener fakes connected to the device;
//   the attribute registry 0x16a170 (ours, tests/gz_device_event_registry.cpp) is stood in on both sides over a map placed
//   at its static (RegistryScope).
// - LogD3DError's report and the framework's services are d3dfx::ErrorLog's (the window and graphic services answered
//   by fakes); Sleep is an impfx fake.
// Compared: the log (every call on every fake, with its arguments, and the stand-ins' notes), each slot's result, every
// byte of the device object but its vtable word, of the state and manager blocks, the sync surface's memory, the sync
// clear colour (0x10ca21c), and every fake's reference count.
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"
#include "gz/GraphicServices.h"
#include "free_recorder.h"
#include "gz_registry_fx.h"
#include "impfx.h"

#include "app/WinMain.h"
#include "game/vtable.h"
#include "gz/cConnectionManager.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnumDX.h"
#include "gz/cDeviceResourceManagerDX.h"
#include "gz/cDeviceStateDX.h"
#include "rz/StaticGetters.h"

#include <bit>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace nGZGraphic4;
using nGZEvent::cConnectionManager;
using nGZEvent::cConnectionManagerAdapters;
using nGZEvent::cEventAttributeMap;

namespace {

constexpr uint32_t kVTableRva = 0xd9e478;
constexpr uint32_t kEventManagerCell = 0x10ca22c;
constexpr uint32_t kSyncClearColor = 0x10ca21c;
constexpr uint32_t kRegistryMap = 0x10ca220, kRegistryMapGuard = 0x10ca228, kRegistryOnce = 0x10ca230,
                   kRegistryHolderGuard = 0x10ca238;

constexpr uint32_t kObjectTag = 0x7e000000, kStateTag = 0x57000000, kManagerBlockTag = 0x5e000000,
                   kDataTag = 0x7c000000, kConnTag = 0x7b000000, kSyncTag = 0x7a000000;

// ---- fakes of the engine objects ----
// One interface for every engine object the device calls (each slot number means one thing across them): QI / AddRef /
// Release; 3 the COM director's GetClassObject; 4 a swap chain's (or the target provider's) back buffer; 7 the display
// monitor's SetMonitor; 9 the state's Reset; 12 the manager's ManageTextureMemory; 13 a depth-stencil format's stencil
// bits; 14 a stream's text; 20 / 21 / 22 a surface's width, height, pixel format; 25 the graphic service's
// IDirect3D9; 26 / 37 / 38 the window service's; 27 the enumeration's adapter identifier.
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
inline constexpr drawfx::Slot kEngineSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {3, drawfx::Ret::Int, true, "GetClassObject", "uui"},
    {0, drawfx::Ret::Int, true, "GetSurface", ""},
    U, U,
    {1, drawfx::Ret::Int, true, "SetMonitor", "u"},
    U,
    {1, drawfx::Ret::Int, true, "Reset", "u"},
    U, U,
    {0, drawfx::Ret::Int, true, "ManageTextureMemory", ""},
    {0, drawfx::Ret::Int, true, "GetStencilBits", ""},
    {1, drawfx::Ret::Int, true, "SetRZCharStr", "s"},
    U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetWidth", ""},
    {0, drawfx::Ret::Int, true, "GetHeight", ""},
    {0, drawfx::Ret::Int, true, "GetPixelFormat", ""},
    U, U,
    {1, drawfx::Ret::Int, true, "Slot25", "u"},
    {0, drawfx::Ret::Int, true, "Slot26", ""},
    {1, drawfx::Ret::Int, true, "Slot27", "i"},
    U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "Slot37", "u"},
    {1, drawfx::Ret::Int, true, "Slot38", "u"},
};
inline constexpr drawfx::Iface kEngine{"engine", 39, kEngineSlots};

// The graphic service (0x85522803 / 0xc55289ba): slot 25(1) the IDirect3D9; LogD3DError's QueryServiceValue asks the
// same service for its slot 30(code) (src/rz/MiscHelpersA.cpp), so the fake answers that too (0: the default text).
inline constexpr drawfx::Slot kGraphicSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "Slot25", "u"},
    U, U, U, U,
    {1, drawfx::Ret::Int, true, "Slot30", "u"},
};
inline constexpr drawfx::Iface kGraphicService{"graphic service", 31, kGraphicSlots};

inline constexpr drawfx::Slot kFormatsSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    U,
    {1, drawfx::Ret::Int, true, "GetFormat", "u"},
};
inline constexpr drawfx::Iface kFormats{"cIPixelFormatsManager", 5, kFormatsSlots};

inline constexpr drawfx::Slot kBehaviorSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {4, drawfx::Ret::Int, true, "OnEvent", "uuii"},
    {3, drawfx::Ret::Int, true, "OnQuery", "uui"},
};
inline constexpr drawfx::Iface kBehavior{"cIAbstractBehavior", 5, kBehaviorSlots};

// Fake ids.
enum : uint32_t {
    kEnumId = 0xe0001, kMonitorId, kChainId0, kChainId1, kChainId2, kBackId, kProviderId, kProviderSurfaceId,
    kPixelFormatId, kStencilId, kWindowId, kGraphicId, kStreamId, kComId, kStateFakeId, kManagerFakeId, kVBObjectId,
    kDeclObjectId, kTargetId0, kTargetId1, kTargetId2, kTargetId3, kDepthObjectId, kFormatsId, kListenerId0,
    kListenerId1, kListenerId2, kSourceId,
    kD3DId = 0xd3d00010, kQueryId, kSyncId, kDepthId, kProbeId, kVBId, kDeclId, kColorId0, kColorId1, kColorId2,
    kColorId3, kTargetDepthId
};
constexpr unsigned kListeners = 3;

drawfx::Fake gDevice, gD3D, gQuery, gSync, gDepth, gProbe, gVB, gDecl, gColor[4], gTargetDepth;
drawfx::Fake gEnum, gMonitor, gChain[3], gBack, gProvider, gProviderSurface, gPixelFormat, gStencil, gWindow, gGraphic,
    gStream, gCom, gStateFake, gManagerFake, gVBObject, gDeclObject, gTarget[4], gDepthObject, gFormatsFake,
    gListener[kListeners], gSource;

d3dfx::SurfaceData gSyncData, gDepthData;
d3dfx::QueryData gQueryData;

// ---- the blocks ----
alignas(16) uint8_t gObjectBlock[cDeviceDX::kSize];
uint8_t *gObject = gObjectBlock; // the device under test: the block, or a heap object (the last Release, delete)
constexpr size_t kStateBytes = 0xb00; // the state fields the device reads end at +0xaf8
alignas(16) uint8_t gStateBlock[kStateBytes];
alignas(16) uint8_t gManagerBlock[cDeviceResourceManagerDX::kSize];
// Arguments the cases pass by pointer (a registered range): settings, colours, rectangles, messages, render targets.
struct Data {
    cDeviceSettings settings;
    float color[4];
    int32_t rect[4];
    uint32_t message[8];
    uint32_t targets[7]; // RenderTargets: header, four colour targets, depth
    uint32_t words[4];
} gData;

cDeviceDX *Device() { return reinterpret_cast<cDeviceDX *>(gObject); }
cIDeviceDX *Face() { return Device(); }
uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }
template <class T> void Put(void *block, size_t offset, T value) { std::memcpy(static_cast<uint8_t *>(block) + offset, &value, sizeof(T)); }
template <class T> T Get(const void *block, size_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t *>(block) + offset, sizeof(T));
    return value;
}

// ---- a case ----
enum Op : uint32_t {
    kQueryInterface, kAddRef, kRelease, kOnConnection, kOnCanvas, kOnResize, kOnOther, kOnQuery, kNotify, kAsk, kConnect,
    kDisconnect, kIsActive, kEventCount, kEventIds, kEventAttribute, kGetters, kSetFlags, kSetCreationBlocked,
    kApplySettings, kOnDeviceLost, kReleaseDevice, kProcessVertices, kClear, kClearTargets, kBeginScene, kEndScene,
    kDrawPrimitive, kDrawIndexed, kGetSettings, kGetBackBuffer, kGetProviderSurface, kGetTargetSize, kDumpInfo,
    kWaitForGpu, kEmptySlots, kInit, kDestruct, kReleaseLast, kDelete, kOpCount
};
// Ops that free the device (it is a heap object of the side's then).
bool Gone(uint32_t op) { return op == kReleaseLast || op == kDelete; }

struct Case {
    uint32_t op;
    uint8_t object[cDeviceDX::kSize];
    uint8_t state[kStateBytes];
    uint8_t manager[cDeviceResourceManagerDX::kSize];
    Data data;
    d3dfx::Answers device;
    uint32_t args[8];
    bool createResults[4];   // CreateDevice's answers in turn
    bool createMakesDevice;  // a successful CreateDevice sets +0x38 to the fake device
    uint32_t queryBusy;
    uint32_t depthFormat;
    uint32_t stencilBits;
    uint32_t connections;    // bit i: listener i connected to event i's id
    uint32_t listenerAnswers;
    bool vbFace, declFace, targetFaces[4], depthFace;
    uint32_t coop;           // TestCooperativeLevel's answer
    uint32_t throwAt;        // Init: 1 the enumeration's constructor throws, 2 the manager's, 3 the state's
    uint32_t sceneThrow;     // canvas: the render-scene listener throws a cD3DException at event code sceneThrow - 1
    uint32_t sceneHr;        // ... carrying this HRESULT
    uint32_t issueHr, unlockHr; // the GPU query's Issue and the sync surface's UnlockRect answers
    bool nullStream;
};

// Listener i's events: the device's events it listens to and the id it hears them as.
constexpr uint32_t kListenEvents[kListeners] = {cDeviceEvent::kEventId, cDeviceRenderSceneEvent::kEventId, 0x77};
constexpr uint32_t kHeardAs[kListeners] = {0x51, 0x52, 0x53};

// The registry's records (RegistryScope).
char gNames[3][8];
const uint32_t kRegistryIds[] = {0x10, cDeviceEvent::kEventId, 0xa0000000};

// ---- stand-ins ----
struct Script {
    const Case *c = nullptr;
    unsigned creates = 0;
} gScript;

struct D3DThrow : nRZGraphic4::cD3DException {
    int32_t hr;
};

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

bool __fastcall StandCreateDevice(cDeviceDX *self, void *)
{
    const unsigned n = gScript.creates++;
    const bool ok = n < 4 && gScript.c->createResults[n];
    drawfx::Note("CreateDevice", {drawfx::Norm(Word(self)), n, ok});
    if (ok && gScript.c->createMakesDevice) self->mDevice = reinterpret_cast<IDirect3DDevice9 *>(&gDevice);
    return ok;
}
void __fastcall StandEnumFlags(void *self, void *, uint32_t a, uint32_t flags)
{
    drawfx::Note("enum.0x178b80", {drawfx::Norm(Word(self)), a, flags});
}
// A stream the device made itself (DumpInfo without one) is a heap object: logged as a marker.
uint32_t StreamWord(void *stream) { return stream == &gStream ? kStreamId : stream ? 0x5ead : 0; }
void __fastcall StandEnumDump(void *self, void *, void *stream) { drawfx::Note("enum.dump", {drawfx::Norm(Word(self)), StreamWord(stream)}); }
void __fastcall StandStateDump(void *self, void *, void *stream) { drawfx::Note("state.dump", {drawfx::Norm(Word(self)), StreamWord(stream)}); }
void *__fastcall StandNewEnum(void *memory, void *, uint32_t adapter, void *device)
{
    drawfx::Note("new enum", {adapter, drawfx::Norm(Word(device))});
    if (gScript.c->throwAt == 1) throw D3DThrow{{nullptr}, int32_t(0x88760870)};
    ::operator delete(memory);
    return &gEnum;
}
void *__fastcall StandNewManager(void *memory, void *, void *device)
{
    drawfx::Note("new manager", drawfx::Norm(Word(device)));
    if (gScript.c->throwAt == 2) throw D3DThrow{{nullptr}, int32_t(0x8876086c)};
    ::operator delete(memory);
    return gManagerBlock;
}
void *__fastcall StandNewState(void *memory, void *, void *device, void *deviceEnum)
{
    drawfx::Note("new state", {drawfx::Norm(Word(device)), drawfx::Norm(Word(deviceEnum))});
    if (gScript.c->throwAt == 3) throw D3DThrow{{nullptr}, int32_t(0x80004005)};
    ::operator delete(memory);
    return gStateBlock;
}
void *__cdecl StandFormats() { return &gFormatsFake; }
void *__cdecl StandFramework() { return &gCom; }
void __fastcall StandReleaseAll(void *self, void *) { drawfx::Note("manager.ReleaseAll", drawfx::Norm(Word(self))); }
void __fastcall StandFlush(void *self, void *, uint32_t groups) { drawfx::Note("state.Flush", {drawfx::Norm(Word(self)), groups}); }
void __fastcall StandMarkTargets(void *self, void *) { drawfx::Note("state.MarkTargetsDirty", drawfx::Norm(Word(self))); }
void __fastcall StandSetTarget(void *self, void *, void *surface, void *depth, uint32_t width, uint32_t height)
{
    drawfx::Note("state.SetBackBufferTarget",
                 {drawfx::Norm(Word(self)), drawfx::Norm(Word(surface)), drawfx::Norm(Word(depth)), width, height});
}
void __fastcall StandResetTexCoords(void *self, void *) { drawfx::Note("state.ResetTexCoordStates", drawfx::Norm(Word(self))); }
void __fastcall StandRestoreTexCoords(void *self, void *) { drawfx::Note("state.RestoreTexCoordStates", drawfx::Norm(Word(self))); }
void __fastcall StandReleaseObjects(void *self, void *) { drawfx::Note("state.ReleaseDeviceObjects", drawfx::Norm(Word(self))); }

// ---- handlers ----
uint32_t SurfaceOf(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t ComGetClassObject(drawfx::Fake &, const uint32_t *a)
{
    ++gMonitor.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gMonitor;
    return 1;
}
uint32_t GraphicDirect3D(drawfx::Fake &, const uint32_t *) { return Word(&gD3D); }
uint32_t EnumIdentifier(drawfx::Fake &, const uint32_t *a)
{
    auto *id = reinterpret_cast<D3DADAPTER_IDENTIFIER9 *>(uintptr_t(a[0]));
    std::memset(id, 0x3c, sizeof(*id));
    return 0;
}
uint32_t StencilBits(drawfx::Fake &, const uint32_t *) { return gScript.c->stencilBits; }
uint32_t FormatsGetFormat(drawfx::Fake &, const uint32_t *) { return Word(&gPixelFormat); }
bool OnStack(uint32_t p)
{
    const auto *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    return p >= uintptr_t(tib->StackLimit) && p < uintptr_t(tib->StackBase);
}

// The listeners' OnEvent: an event body the device built on the stack (its own events) is asked its type, sizes and
// code, and cloned, as listeners may (the original's bodies have the game's vtables, ours ours: the calls compare).
uint32_t ListenerOnEvent(drawfx::Fake &, const uint32_t *a)
{
    if ((a[0] != kHeardAs[0] && a[0] != kHeardAs[1]) || !OnStack(a[2])) return 0;
    const auto *body = reinterpret_cast<const cDeviceEventBody *>(uintptr_t(a[2]));
    alignas(4) uint8_t copy[8];
    body->CloneInto(copy);
    const auto *clone = reinterpret_cast<const cDeviceEventBody *>(copy);
    drawfx::Note("event", {body->GetEventType(), body->GetSize(), body->GetBodySize(), body->mCode, clone->GetEventType(),
                           clone->mCode});
    // A frame's render-scene listener may fail with a Direct3D exception (the frame's catch takes it).
    if (a[0] == kHeardAs[1] && gScript.c->sceneThrow && body->mCode == gScript.c->sceneThrow - 1) {
        drawfx::Note("scene thrown", gScript.c->sceneHr);
        throw D3DThrow{{nullptr}, int32_t(gScript.c->sceneHr)};
    }
    return 0;
}
uint32_t ListenerOnQuery(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
// The temporary face of the device while the listeners connect: the behaviour is the device block itself.
uint32_t SourceQuery(drawfx::Fake &self, const uint32_t *a)
{
    if (a[0] != nGZEvent::cIAbstractBehaviorType::kIid || !a[1]) return 0;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = self.owner;
    return 1;
}
uint32_t ProcessVerticesFault(drawfx::Fake &self, const uint32_t *)
{
    if (self.value[0]) *reinterpret_cast<volatile uint32_t *>(uintptr_t(self.value[0])) = 1; // a structured exception
    return self.ret[fx_d3d9::dev::ProcessVertices];
}

// ---- the registry: a map placed over the original's static, its guards marked done ----
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
            std::snprintf(gNames[i], sizeof(gNames[i]), "d%u", i);
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

// The cells a test sets, restored at its end.
class Cells {
public:
    Cells() : manager_(*GameAddress<uint32_t>(kEventManagerCell)), color_(*GameAddress<uint32_t>(kSyncClearColor)) {}
    ~Cells()
    {
        *GameAddress<uint32_t>(kEventManagerCell) = manager_;
        *GameAddress<uint32_t>(kSyncClearColor) = color_;
    }
    Cells(const Cells &) = delete;
    Cells &operator=(const Cells &) = delete;

private:
    uint32_t manager_, color_;
};

// Every stand-in of the world, for a test's scope.
struct StandIns {
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::StandIn create{0x167ec0, &cDeviceDX::CreateDevice, &StandCreateDevice};
    shape::StandIn enumFlags{0x178b80, &cDeviceEnumDX::Refresh, &StandEnumFlags};
    shape::StandIn enumDump{0x1788f0, &cDeviceEnumDX::DumpInfo, &StandEnumDump};
    shape::StandIn stateDump{0x17da90, &cDeviceStateDX::DumpInfo, &StandStateDump};
    shape::StandIn registry{0x16a170, &cDeviceDX::EventRegistryFor, &pdfx::StandRegistry<kRegistryMap>};
    shape::StandIn newEnum{0x1774a0, &cDeviceEnumDXAdapters::Construct, &StandNewEnum};
    shape::StandIn newManager{0x17ad50, &cDeviceResourceManagerAdapters::Construct, &StandNewManager};
    shape::StandIn newState{0x17c490, &cDeviceStateDXAdapters::Construct, &StandNewState};
    shape::StandIn formats{0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, &StandFormats};
    shape::StandIn releaseAll{0x17bb80, &cDeviceResourceManagerDX::ReleaseAll, &StandReleaseAll};
    shape::StandIn framework{0x1ae18d, &::GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn flush{0x17cdf0, &cDeviceStateDX::Flush, &StandFlush};
    shape::StandIn markTargets{0x17ec40, &cDeviceStateDX::MarkTargetsDirty, &StandMarkTargets};
    shape::StandIn setTarget{0x180050, &cDeviceStateDX::SetBackBufferTarget, &StandSetTarget};
    shape::StandIn resetTex{0x17ff90, &cDeviceStateDX::ResetTexCoordStates, &StandResetTexCoords};
    shape::StandIn restoreTex{0x17eb90, &cDeviceStateDX::RestoreTexCoordStates, &StandRestoreTexCoords};
    shape::StandIn releaseObjects{0x1815f0, &cDeviceStateDX::ReleaseDeviceObjects, &StandReleaseObjects};
};

// ---- seeding ----
uint32_t Pick(std::mt19937 &gen, unsigned oneIn) { return gen() % oneIn == 0; }

void SeedCase(std::mt19937 &gen, Case &c, uint32_t op)
{
    std::memset(&c, 0, sizeof(c));
    c.op = op;
    for (uint8_t &b : c.object) b = uint8_t(gen());
    for (uint8_t &b : c.state) b = uint8_t(gen());
    for (uint8_t &b : c.manager) b = uint8_t(gen());
    auto *d = reinterpret_cast<cDeviceDX *>(c.object);
    d->mRefCount = 2 + gen() % 4 | (Pick(gen, 2) ? 0x80000000u : 0);
    if (op == kRelease && Pick(gen, 3)) d->mRefCount = (d->mRefCount & 0x80000000u) | 2;
    if (op == kReleaseLast) d->mRefCount = (d->mRefCount & 0x80000000u) | 1;
    // A count of 0 (a device before its first AddRef); with the active bit clear the whole word is 0.
    if (op != kRelease && op != kReleaseLast && op != kDelete && Pick(gen, 8)) d->mRefCount &= 0x80000000u;
    d->mSettings.mFlags = gen() % 4 == 0 ? 0 : gen() & 0x3f;
    d->mDevice = Pick(gen, 5) ? nullptr : reinterpret_cast<IDirect3DDevice9 *>(&gDevice);
    // The drawing slots assume a device (the original calls through +0x38 unchecked): their callers draw only then.
    if (op >= kProcessVertices && op <= kDrawIndexed) d->mDevice = reinterpret_cast<IDirect3DDevice9 *>(&gDevice);
    d->mDeviceEnum = reinterpret_cast<cIGZUnknown *>(&gEnum);
    d->mResourceManager = reinterpret_cast<cDeviceResourceManagerDX *>(gManagerBlock);
    d->mState = reinterpret_cast<cDeviceStateDX *>(gStateBlock);
    d->mDisplayMonitor = Pick(gen, 4) ? nullptr : reinterpret_cast<cIGZUnknown *>(&gMonitor);
    d->mSwapChainCount = gen() % 4;
    for (unsigned i = 0; i < 3; ++i)
        d->mSwapChains[i] = i < d->mSwapChainCount || Pick(gen, 3) ? reinterpret_cast<cIGZUnknown *>(&gChain[i]) : nullptr;
    if (op == kOnResize || op == kGetBackBuffer) d->mSwapChains[0] = reinterpret_cast<cIGZUnknown *>(&gChain[0]);
    if (op == kGetBackBuffer)
        for (unsigned i = 0; i < 3; ++i) d->mSwapChains[i] = reinterpret_cast<cIGZUnknown *>(&gChain[i]);
    d->mTargetProvider = Pick(gen, 4) && op != kGetProviderSurface ? nullptr : reinterpret_cast<cIGZUnknown *>(&gProvider);
    d->mGpuQuery = Pick(gen, 2) ? nullptr : reinterpret_cast<IDirect3DQuery9 *>(&gQuery);
    d->mSyncSurface = Pick(gen, 2) ? nullptr : reinterpret_cast<IDirect3DSurface9 *>(&gSync);
    d->mProbeTexture = Pick(gen, 2) ? nullptr : reinterpret_cast<IDirect3DTexture9 *>(&gProbe);
    const int32_t depths[] = {0, 0, 1, 1, 2, -1};
    d->mSceneDepth = depths[gen() % 6];
    d->mFullscreen = Pick(gen, 2);
    d->mDeviceLost = Pick(gen, 3);
    d->mDeviceFailed = Pick(gen, 6);
    d->mReleased = Pick(gen, 6);
    d->mByte74 = Pick(gen, 3);
    d->mCreationBlocked = Pick(gen, 3);
    if (op == kInit) {
        // As 0x159410 makes it: just constructed (the enumeration, manager and state are what Init makes).
        d->mDeviceEnum = Pick(gen, 3) ? reinterpret_cast<cIGZUnknown *>(&gEnum) : nullptr;
        d->mResourceManager = nullptr;
        d->mState = nullptr;
    }
    // The state's fields the device reads: the pending groups (three dwords at +0x74, mostly clear), the wanted depth
    // surface and first target, the target size.
    for (size_t at = 0x74; at < 0x80; at += 4) Put<uint32_t>(c.state, at, Pick(gen, 2) ? 0u : uint32_t(gen() % 3) << (gen() % 4 * 8));
    Put<uint32_t>(c.state, 0xaec, Pick(gen, 4) ? 0u : Word(&gDepth));
    Put<uint32_t>(c.state, 0xadc, Pick(gen, 4) ? 0u : gen() | 1);
    // The manager's budget: deltas around the limit, either sign.
    const uint32_t limit = gen() % 0x400;
    Put<uint32_t>(c.manager, 0x1c, limit);
    Put<int32_t>(c.manager, 0x0c, int32_t(gen() % (2 * limit + 3)) - int32_t(limit) - 1);
    Put<int32_t>(c.manager, 0x10, int32_t(gen() % (2 * limit + 3)) - int32_t(limit) - 1);
    if (Pick(gen, 5)) Put<int32_t>(c.manager, 0x0c, INT_MIN + int32_t(gen() % 2));
    // Arguments.
    for (uint32_t &w : c.args) w = gen();
    for (unsigned i = 0; i < 11; ++i) reinterpret_cast<uint32_t *>(&c.data.settings)[i] = gen();
    c.data.settings.mFlags = Pick(gen, 2) ? d->mSettings.mFlags : gen() & 0x3f;
    std::uniform_real_distribution<float> component(-0.25f, 1.25f);
    for (float &f : c.data.color) f = component(gen);
    if (Pick(gen, 4)) c.data.color[gen() % 4] = std::bit_cast<float>(gen());
    for (int32_t &r : c.data.rect) r = int32_t(gen() % 640);
    for (uint32_t &w : c.data.message) w = gen();
    const uint32_t kinds[] = {3, 3, 3, 7, 7, 1, 4};
    c.data.message[1] = kinds[gen() % 7];
    // Resize: the client rect (+4..+0x10) the back buffer's size or not, flags (+0x14).
    if (op == kOnResize) {
        c.data.message[1] = 0;
        c.data.message[2] = 0;
        c.data.message[3] = Pick(gen, 2) ? 640 : gen() % 1024;
        c.data.message[4] = Pick(gen, 2) ? 480 : gen() % 1024;
        c.data.message[5] = Pick(gen, 3) ? 0x8 << (gen() % 2) : 0;
    }
    c.data.targets[0] = gen();
    c.data.targets[1] = gen();
    for (unsigned i = 0; i < 4; ++i) c.data.targets[2 + i] = Pick(gen, 4) ? 0 : Word(&gTarget[i]);
    c.data.targets[6] = Pick(gen, 3) ? 0 : Word(&gDepthObject);
    for (uint32_t &w : c.data.words) w = gen();
    // Answers.
    c.device.Random(gen, fx_d3d9::IDirect3DDevice9.count);
    const uint32_t coops[] = {0, 0, 0, uint32_t(D3DERR_DEVICELOST), uint32_t(D3DERR_DEVICENOTRESET), uint32_t(D3DERR_DRIVERINTERNALERROR), 0x88760870, 0x8876086c};
    c.coop = coops[gen() % 8];
    for (bool &b : c.createResults) b = !Pick(gen, 3);
    c.createMakesDevice = Pick(gen, 2);
    c.queryBusy = gen() % 4;
    const uint32_t formats[] = {D3DFMT_D24S8, D3DFMT_D16, D3DFMT_D24X8, D3DFMT_D15S1};
    c.depthFormat = formats[gen() % 4];
    c.stencilBits = Pick(gen, 2) ? 0 : 8;
    c.connections = gen() % 8;
    c.listenerAnswers = gen() % 8;
    c.vbFace = !Pick(gen, 4);
    c.declFace = !Pick(gen, 4);
    for (bool &b : c.targetFaces) b = !Pick(gen, 4);
    c.depthFace = !Pick(gen, 4);
    c.throwAt = op == kInit && Pick(gen, 3) ? 1 + gen() % 3 : 0;
    c.sceneThrow = op == kOnCanvas && Pick(gen, 4) ? 1 + gen() % 3 : 0;
    const uint32_t sceneHrs[] = {uint32_t(D3DERR_DRIVERINTERNALERROR), uint32_t(D3DERR_DEVICELOST), uint32_t(D3DERR_DEVICENOTRESET), 0x80004005};
    c.sceneHr = sceneHrs[gen() % 4];
    c.issueHr = Pick(gen, 3) ? uint32_t(D3DERR_INVALIDCALL) : 0u;
    c.unlockHr = Pick(gen, 3) ? uint32_t(D3DERR_INVALIDCALL) : 0u;
    c.nullStream = Pick(gen, 4);
}

// ---- a side's world ----
cConnectionManager *gManager = nullptr;

void ResetFakes(const Case &c)
{
    gDevice.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);
    c.device.Apply(gDevice);
    gDevice.Returns(fx_d3d9::dev::TestCooperativeLevel, c.coop);
    gDevice.On(fx_d3d9::dev::ProcessVertices, &ProcessVerticesFault);
    gDevice.value[0] = 0; // ProcessVerticesFault: no fault in the compared cases (cDeviceDX_ProcessVerticesFault)
    gD3D.Reset<fx_d3d9::IDirect3D9>(kD3DId);
    gD3D.Returns(15, 0x4d000001u); // GetAdapterMonitor
    d3dfx::MakeQuery(gQuery, kQueryId, gQueryData);
    gQueryData.busy = c.queryBusy;
    std::memset(gQueryData.result, 0x11, sizeof(gQueryData.result));
    d3dfx::MakeSurface(gSync, kSyncId, gSyncData);
    gSync.Returns(d3dfx::surf::LockRect, 0u); // WaitForGpu writes through what LockRect gives: it must give memory
    gSync.Returns(d3dfx::surf::UnlockRect, c.unlockHr);
    gQuery.Returns(d3dfx::query::Issue, c.issueHr);
    d3dfx::MakeSurface(gDepth, kDepthId, gDepthData);
    gDepthData.desc.Format = D3DFORMAT(c.depthFormat);
    gDepth.Returns(d3dfx::surf::GetDesc, c.device.ret[3] ? 0x8876086cu : 0u);
    gProbe.Reset<fx_d3d9::IDirect3DTexture9>(kProbeId);
    gVB.Reset<fx_d3d9::IDirect3DVertexBuffer9>(kVBId);
    gDecl.Reset<fx_d3d9::IDirect3DVertexDeclaration9>(kDeclId);
    for (unsigned i = 0; i < 4; ++i) gColor[i].Reset<fx_d3d9::IDirect3DSurface9>(kColorId0 + i);
    gTargetDepth.Reset<fx_d3d9::IDirect3DSurface9>(kTargetDepthId);

    gEnum.Reset<kEngine>(kEnumId);
    gEnum.On(27, &EnumIdentifier);
    gMonitor.Reset<kEngine>(kMonitorId);
    gBack.Reset<kEngine>(kBackId);
    gBack.Returns(20, 640u);
    gBack.Returns(21, 480u);
    for (unsigned i = 0; i < 3; ++i) {
        gChain[i].Reset<kEngine>(kChainId0 + i);
        gChain[i].Returns(4, &gBack);
    }
    gProvider.Reset<kEngine>(kProviderId);
    gProviderSurface.Reset<kEngine>(kProviderSurfaceId);
    gProvider.Returns(4, &gProviderSurface);
    gPixelFormat.Reset<kEngine>(kPixelFormatId);
    gStencil.Reset<kEngine>(kStencilId);
    gPixelFormat.AddFace(0x5ac389b6, gStencil);
    gStencil.On(13, &StencilBits);
    gWindow.Reset<kEngine>(kWindowId);
    gGraphic.Reset<kGraphicService>(kGraphicId);
    gGraphic.On(25, &GraphicDirect3D);
    gStream.Reset<kEngine>(kStreamId);
    gCom.Reset<kEngine>(kComId);
    gCom.On(3, &ComGetClassObject);
    gStateFake.Reset<kEngine>(kStateFakeId);
    gStateFake.Embed(gStateBlock);
    gManagerFake.Reset<kEngine>(kManagerFakeId);
    gManagerFake.Embed(gManagerBlock);
    gVBObject.Reset<kEngine>(kVBObjectId);
    if (c.vbFace) gVBObject.AddFace(0x998d7e9, gVB);
    gDeclObject.Reset<kEngine>(kDeclObjectId);
    if (c.declFace) gDeclObject.AddFace(0x8b448b4c, gDecl);
    for (unsigned i = 0; i < 4; ++i) {
        gTarget[i].Reset<kEngine>(kTargetId0 + i);
        if (c.targetFaces[i]) gTarget[i].AddFace(0x4a5a7223, gColor[i]);
        gTarget[i].Returns(20, 256u + i);
        gTarget[i].Returns(21, 128u + i);
    }
    gDepthObject.Reset<kEngine>(kDepthObjectId);
    if (c.depthFace) gDepthObject.AddFace(0x4a5a7223, gTargetDepth);
    gDepthObject.Returns(22, &gPixelFormat);
    gFormatsFake.Reset<kFormats>(kFormatsId);
    gFormatsFake.On(4, &FormatsGetFormat);
    for (unsigned i = 0; i < kListeners; ++i) {
        gListener[i].Reset<kBehavior>(kListenerId0 + i);
        gListener[i].AddFace(nGZEvent::cIAbstractBehaviorType::kIid, gListener[i]);
        gListener[i].On(3, &ListenerOnEvent);
        gListener[i].On(4, &ListenerOnQuery);
        gListener[i].value[0] = (c.listenerAnswers >> i) & 1;
    }
    gSource.Reset<kBehavior>(kSourceId);
    gSource.On(0, &SourceQuery);
}

void BuildSide(const Case &c, d3dfx::ErrorLog &errors, impfx::Imports &imp, bool original, void *vtable)
{
    drawfx::ResetAll();
    imp.BeginSide();
    gScript = {&c, 0};
    std::memcpy(gObject, c.object, cDeviceDX::kSize);
    std::memcpy(gStateBlock, c.state, sizeof(gStateBlock));
    std::memcpy(gManagerBlock, c.manager, sizeof(gManagerBlock));
    gData = c.data;
    std::memset(&gSyncData, 0, sizeof(gSyncData));
    gSyncData.bits[0] = 0x5a;
    std::memset(&gDepthData, 0, sizeof(gDepthData));
    *GameAddress<uint32_t>(kSyncClearColor) = c.args[7];
    ResetFakes(c);
    errors.Answer(0xfb6372ee, 0xbb6377fa, gWindow);
    errors.Answer(0x85522803, 0xc55289ba, gGraphic);
    drawfx::AddRange(gObject, cDeviceDX::kSize, kObjectTag);
    drawfx::AddRange(gStateBlock, sizeof(gStateBlock), kStateTag);
    drawfx::AddRange(gManagerBlock, sizeof(gManagerBlock), kManagerBlockTag);
    drawfx::AddRange(&gData, sizeof(gData), kDataTag);
    drawfx::AddRange(gSyncData.bits, sizeof(gSyncData.bits), kSyncTag);
    // The connection manager, with the listeners connected to the device (through a stand-in face while they connect).
    gManager = cConnectionManagerAdapters::Construct(::operator new(cConnectionManager::kSize), nullptr);
    drawfx::AddRange(gManager, cConnectionManager::kSize, kConnTag);
    *GameAddress<void *>(kEventManagerCell) = gManager;
    gSource.Embed(gObject);
    for (unsigned i = 0; i < kListeners; ++i)
        if ((c.connections >> i) & 1)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(gObject), kListenEvents[i], reinterpret_cast<cIGZUnknown *>(&gListener[i]),
                kHeardAs[i]);
    std::memcpy(gObject, &vtable, 4);
    (void)original;
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
    const drawfx::Fake *all[] = {&gDevice, &gD3D, &gQuery, &gSync, &gDepth, &gProbe, &gVB, &gDecl, &gColor[0], &gColor[1],
                                 &gColor[2], &gColor[3], &gTargetDepth, &gEnum, &gMonitor, &gChain[0], &gChain[1],
                                 &gChain[2], &gBack, &gProvider, &gProviderSurface, &gPixelFormat, &gStencil, &gWindow,
                                 &gGraphic, &gStream, &gCom, &gStateFake, &gManagerFake, &gVBObject, &gDeclObject,
                                 &gTarget[0], &gTarget[1], &gTarget[2], &gTarget[3], &gDepthObject, &gFormatsFake,
                                 &gListener[0], &gListener[1], &gListener[2]};
    for (const drawfx::Fake *f : all) drawfx::Note("refs", {f->id, uint32_t(f->refs)});
}

// The connections from the device after the call (the manager's view), by listener.
void NoteConnections()
{
    auto *m = static_cast<nGZEvent::cIConnectionManager *>(gManager);
    drawfx::Note("connected", m->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(gObject)));
    for (unsigned i = 0; i < kListeners; ++i)
        drawfx::Note("from", {i, m->CountFrom(reinterpret_cast<cIGZUnknown *>(gObject), kListenEvents[i])});
}

// A walked and released id range.
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

uint32_t Norm(const void *p) { return drawfx::Norm(Word(p)); }
void NoteSettings(const cDeviceSettings &s) { drawfx::Note("settings", {s.mMode.mWidth, s.mMode.mHeight, std::bit_cast<uint32_t>(s.mMode.mAspect), s.mMode.mRefresh, Word(s.mMode.mFormat),Word(s.mColorFormat), Word(s.mDepthFormat), s.mBackBufferCount, s.mMultisampleType, s.mMultisampleQuality, s.mFlags}); }

// ---- one operation, on the original (at its RVAs) or ours (through our vtable) ----
template <class F> F At(difftest::Context &ctx, uint32_t rva) { return ctx.Original<F>(rva); }

// The original ProcessVertices (0x16a040) leaves its two QueryInterface answers uninitialised and passes them on when a
// query fails (the fakes, like the game's objects, write nothing then); ours passes null. The stack the original's
// frame will use is zeroed first, so its uninitialised words read as null too (the passed value is compared).
__declspec(noinline) void ZeroStackBelow()
{
    volatile uint8_t pad[4096];
    for (size_t i = 0; i < sizeof(pad); ++i) pad[i] = 0;
}

void RunOriginal(difftest::Context &ctx, const Case &c)
{
    void *const self = gObject;
    const uint32_t *a = c.args;
    using V = void(__thiscall *)(void *);
    using B = bool(__thiscall *)(void *);
    using P = void *(__thiscall *)(void *);
    switch (c.op) {
    case kQueryInterface: {
        const uint32_t iids[] = {cDeviceDX::kIidDeviceDX, cDeviceDX::kIidDevice, 1, cDeviceDX::kIidBehavior2,
                                 cDeviceDX::kIidD3DDevice, nGZEvent::cIAbstractBehaviorType::kIid, a[1]};
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        const bool ok = At<bool(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x16a110)(self, iids[a[0] % 7], a[2] % 5 ? &out : nullptr);
        drawfx::Note("ret", {ok, Norm(out)});
        break;
    }
    case kAddRef: drawfx::Note("ret", At<uint32_t(__thiscall *)(void *)>(ctx, 0x146a20)(self)); break;
    case kRelease: drawfx::Note("ret", At<uint32_t(__thiscall *)(void *)>(ctx, 0x16a3b0)(self)); break;
    case kOnConnection:
    case kOnCanvas:
    case kOnResize:
    case kOnOther: {
        const uint32_t type = c.op == kOnConnection ? nGZEvent::cConnectionChangedEvent::kType
                              : c.op == kOnCanvas   ? cDeviceDX::kCanvasEvent
                              : c.op == kOnResize   ? cDeviceDX::kResizeEvent
                                                    : a[0];
        At<void(__thiscall *)(void *, uint32_t, uint32_t, void *, uint32_t)>(ctx, 0x169bf0)(self, type, a[1], gData.message, a[2]);
        break;
    }
    case kOnQuery: drawfx::Note("ret", At<bool(__thiscall *)(void *, uint32_t, uint32_t, void *)>(ctx, 0x147ce0)(self, kListenEvents[a[0] % 3], a[1], gData.words)); break;
    case kNotify: At<void(__thiscall *)(void *, uint32_t, uint32_t, void *)>(ctx, 0x16a430)(self, kListenEvents[a[0] % 3], a[1], gData.words); break;
    case kAsk: drawfx::Note("ret", At<bool(__thiscall *)(void *, uint32_t, uint32_t, void *)>(ctx, 0x169890)(self, kListenEvents[a[0] % 3], a[1], gData.words)); break;
    case kConnect: At<void(__thiscall *)(void *, uint32_t, void *, uint32_t)>(ctx, 0x167d60)(self, kListenEvents[a[0] % 3], &gListener[a[1] % 3], a[2] % 4); break;
    case kDisconnect: At<void(__thiscall *)(void *, uint32_t, void *, uint32_t)>(ctx, 0x1694d0)(self, kListenEvents[a[0] % 3], &gListener[a[0] % 3], kHeardAs[a[0] % 3]); break;
    case kIsActive: drawfx::Note("ret", At<uint32_t(__thiscall *)(void *)>(ctx, 0x147cf0)(self)); break;
    case kEventCount: drawfx::Note("ret", At<uint32_t(__thiscall *)(void *)>(ctx, 0x169f10)(self)); break;
    case kEventIds: {
        nGZEvent::cAttributeIDRange range;
        At<void *(__thiscall *)(void *, nGZEvent::cAttributeIDRange *)>(ctx, 0x1674e0)(self, &range);
        NoteIdRange(range);
        break;
    }
    case kEventAttribute: {
        nGZEvent::cEventAttribute r;
        const uint32_t ids[] = {kRegistryIds[0], kRegistryIds[1], kRegistryIds[2], a[1]};
        At<void *(__thiscall *)(void *, nGZEvent::cEventAttribute *, uint32_t)>(ctx, 0x167750)(self, &r, ids[a[0] % 4]);
        drawfx::Note("attribute", {r.mId, Word(r.mName), r.mWord08, r.mWord0C, r.mWord10, r.mWord14});
        break;
    }
    case kGetters: {
        const uint32_t rvas[] = {0x1674b0, 0x1674c0, 0x1674d0, 0x145520, 0x1694c0};
        for (uint32_t rva : rvas) drawfx::Note("get", Norm(At<P>(ctx, rva)(self)));
        const uint32_t bools[] = {0x16a800, 0x169ef0, 0x169ed0, 0x169eb0};
        for (uint32_t rva : bools) drawfx::Note("is", At<B>(ctx, rva)(self));
        drawfx::Note("chains", At<uint32_t(__thiscall *)(void *)>(ctx, 0x169f60)(self));
        break;
    }
    case kSetFlags: drawfx::Note("ret", At<bool(__thiscall *)(void *, uint32_t)>(ctx, 0x167ce0)(self, a[0] % 3 ? gData.settings.mFlags : Device()->mSettings.mFlags)); break;
    case kSetCreationBlocked: At<void(__thiscall *)(void *, bool)>(ctx, 0x16a7d0)(self, a[0] % 2 != 0); break;
    case kApplySettings: drawfx::Note("ret", At<bool(__thiscall *)(void *, const cDeviceSettings *)>(ctx, 0x16a520)(self, &gData.settings)); break;
    case kOnDeviceLost: At<V>(ctx, 0x169690)(self); break;
    case kReleaseDevice: At<V>(ctx, 0x16a5c0)(self); break;
    case kProcessVertices: ZeroStackBelow(); At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, void *, void *)>(ctx, 0x16a040)(self, a[0] % 64, a[1] % 64, a[2] % 64, &gVBObject, &gDeclObject); break;
    case kClear: At<void(__thiscall *)(void *, uint32_t, const float *, float, uint32_t)>(ctx, 0x167890)(self, a[0] % 8, gData.color, std::bit_cast<float>(a[1] & 0x3f7fffff), a[2] % 256); break;
    case kClearTargets: At<void(__thiscall *)(void *, const void *, const int32_t *, uint32_t, const float *, float, uint32_t)>(ctx, 0x167a60)(self, gData.targets, gData.rect, a[0] % 8, gData.color, std::bit_cast<float>(a[1] & 0x3f7fffff), a[2] % 256); break;
    case kBeginScene: At<V>(ctx, 0x167850)(self); break;
    case kEndScene: At<V>(ctx, 0x169660)(self); break;
    case kDrawPrimitive: At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(ctx, 0x169570)(self, a[0] % 6, a[1] % 1000, a[2] % 1000); break;
    case kDrawIndexed: At<void(__thiscall *)(void *, uint32_t, uint32_t, int32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x1695e0)(self, a[0] % 6, a[1] % 1000, int32_t(a[2] % 64) - 32, a[3] % 100, a[4] % 1000, a[5] % 1000); break;
    case kGetSettings: {
        cDeviceSettings s;
        At<void *(__thiscall *)(void *, cDeviceSettings *)>(ctx, 0x16a590)(self, &s);
        NoteSettings(s);
        break;
    }
    case kGetBackBuffer: drawfx::Note("ret", Norm(At<void *(__thiscall *)(void *, uint32_t)>(ctx, 0x167830)(self, a[0] % 3))); break;
    case kGetProviderSurface: drawfx::Note("ret", Norm(At<P>(ctx, 0x167840)(self))); break;
    case kGetTargetSize: {
        cDeviceTargetSize s;
        At<void *(__thiscall *)(void *, cDeviceTargetSize *)>(ctx, 0x16a410)(self, &s);
        drawfx::Note("size", {s.mWidth, s.mHeight});
        break;
    }
    case kDumpInfo: At<void(__thiscall *)(void *, void *)>(ctx, 0x169f70)(self, c.nullStream ? nullptr : &gStream); break;
    case kWaitForGpu: At<V>(ctx, 0x169740)(self); break;
    case kEmptySlots: At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(ctx, 0x377e0)(self, a[0], a[1], a[2]); break;
    case kInit: drawfx::Note("ret", At<bool(__thiscall *)(void *, uint32_t)>(ctx, 0x169cc0)(self, a[0] % 4)); break;
    case kDestruct: At<V>(ctx, 0x167340)(self); break;
    case kReleaseLast: drawfx::Note("ret", At<uint32_t(__thiscall *)(void *)>(ctx, 0x16a3b0)(self)); break;
    case kDelete: At<void *(__thiscall *)(void *, uint32_t)>(ctx, 0x167490)(self, 1); break;
    }
}

// A vtable of traps (a slot called through it is logged, and the logs differ).
void __fastcall Trap(void *, void *) { drawfx::Note("trap", 0u); }
struct Traps {
    void *slots[50];
    Traps()
    {
        for (void *&s : slots) s = reinterpret_cast<void *>(&Trap);
    }
} gTrapTable;
void *const gTraps = gTrapTable.slots;

void RunOurs(const Case &c)
{
    cIDeviceDX *const d = Face();
    const uint32_t *a = c.args;
    switch (c.op) {
    case kQueryInterface: {
        const uint32_t iids[] = {cDeviceDX::kIidDeviceDX, cDeviceDX::kIidDevice, 1, cDeviceDX::kIidBehavior2,
                                 cDeviceDX::kIidD3DDevice, nGZEvent::cIAbstractBehaviorType::kIid, a[1]};
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        const bool ok = RawSlot<0, bool>(d, iids[a[0] % 7], a[2] % 5 ? &out : nullptr);
        drawfx::Note("ret", {ok, Norm(out)});
        break;
    }
    case kAddRef: drawfx::Note("ret", RawSlot<1>(d)); break;
    case kRelease: drawfx::Note("ret", RawSlot<2>(d)); break;
    case kOnConnection:
    case kOnCanvas:
    case kOnResize:
    case kOnOther: {
        const uint32_t type = c.op == kOnConnection ? nGZEvent::cConnectionChangedEvent::kType
                              : c.op == kOnCanvas   ? cDeviceDX::kCanvasEvent
                              : c.op == kOnResize   ? cDeviceDX::kResizeEvent
                                                    : a[0];
        RawSlot<3, void>(d, type, a[1], static_cast<void *>(gData.message), a[2]);
        break;
    }
    case kOnQuery: drawfx::Note("ret", RawSlot<4, bool>(d, kListenEvents[a[0] % 3], a[1], static_cast<void *>(gData.words))); break;
    case kNotify: RawSlot<5, void>(d, kListenEvents[a[0] % 3], a[1], static_cast<void *>(gData.words)); break;
    case kAsk: drawfx::Note("ret", RawSlot<6, bool>(d, kListenEvents[a[0] % 3], a[1], static_cast<void *>(gData.words))); break;
    case kConnect: RawSlot<8, void>(d, kListenEvents[a[0] % 3], reinterpret_cast<cIGZUnknown *>(&gListener[a[1] % 3]), a[2] % 4); break;
    case kDisconnect: RawSlot<9, void>(d, kListenEvents[a[0] % 3], reinterpret_cast<cIGZUnknown *>(&gListener[a[0] % 3]), kHeardAs[a[0] % 3]); break;
    case kIsActive: drawfx::Note("ret", RawSlot<11>(d)); break;
    case kEventCount: drawfx::Note("ret", RawSlot<12>(d)); break;
    case kEventIds: NoteIdRange(d->GetEventIds()); break;
    case kEventAttribute: {
        const uint32_t ids[] = {kRegistryIds[0], kRegistryIds[1], kRegistryIds[2], a[1]};
        const nGZEvent::cEventAttribute r = d->GetEventAttribute(ids[a[0] % 4]);
        drawfx::Note("attribute", {r.mId, Word(r.mName), r.mWord08, r.mWord0C, r.mWord10, r.mWord14});
        break;
    }
    case kGetters: {
        // Each body once, through one of the slots that hold it, by slot index as game code calls them.
        drawfx::Note("get", Norm(RawSlot<15, void *>(d)));
        drawfx::Note("get", Norm(RawSlot<18, void *>(d)));
        drawfx::Note("get", Norm(RawSlot<19, void *>(d)));
        drawfx::Note("get", Norm(RawSlot<42, void *>(d)));
        drawfx::Note("get", Norm(RawSlot<48, void *>(d)));
        drawfx::Note("is", RawSlot<34, bool>(d));
        drawfx::Note("is", RawSlot<35, bool>(d));
        drawfx::Note("is", RawSlot<36, bool>(d));
        drawfx::Note("is", RawSlot<37, bool>(d));
        drawfx::Note("chains", RawSlot<38>(d));
        break;
    }
    case kSetFlags: drawfx::Note("ret", RawSlot<21, bool>(d, a[0] % 3 ? gData.settings.mFlags : Device()->mSettings.mFlags)); break;
    case kSetCreationBlocked: RawSlot<22, void>(d, a[0] % 2 != 0); break;
    case kApplySettings: drawfx::Note("ret", RawSlot<23, bool>(d, &gData.settings)); break;
    case kOnDeviceLost: RawSlot<24, void>(d); break;
    case kReleaseDevice: RawSlot<25, void>(d); break;
    case kProcessVertices: RawSlot<26, void>(d, a[0] % 64, a[1] % 64, a[2] % 64, reinterpret_cast<cIGZUnknown *>(&gVBObject), reinterpret_cast<cIGZUnknown *>(&gDeclObject)); break;
    case kClear: RawSlot<27, void>(d, a[0] % 8, gData.color, std::bit_cast<float>(a[1] & 0x3f7fffff), a[2] % 256); break;
    case kClearTargets: RawSlot<28, void>(d, gData.targets, gData.rect, a[0] % 8, gData.color, std::bit_cast<float>(a[1] & 0x3f7fffff), a[2] % 256); break;
    case kBeginScene: RawSlot<29, void>(d); break;
    case kEndScene: RawSlot<30, void>(d); break;
    case kDrawPrimitive: RawSlot<31, void>(d, a[0] % 6, a[1] % 1000, a[2] % 1000); break;
    case kDrawIndexed: RawSlot<32, void>(d, a[0] % 6, a[1] % 1000, int32_t(a[2] % 64) - 32, a[3] % 100, a[4] % 1000, a[5] % 1000); break;
    case kGetSettings: NoteSettings(d->GetSettings()); break;
    case kGetBackBuffer: drawfx::Note("ret", Norm(RawSlot<39, void *>(d, a[0] % 3))); break;
    case kGetProviderSurface: drawfx::Note("ret", Norm(RawSlot<40, void *>(d))); break;
    case kGetTargetSize: {
        const cDeviceTargetSize s = d->GetTargetSize();
        drawfx::Note("size", {s.mWidth, s.mHeight});
        break;
    }
    case kDumpInfo: RawSlot<43, void>(d, c.nullStream ? nullptr : reinterpret_cast<cIGZUnknown *>(&gStream)); break;
    case kWaitForGpu: RawSlot<46, void>(d); break;
    case kEmptySlots: RawSlot<7, void>(d, a[0], a[1], a[2]); RawSlot<10, void>(d, a[0], a[1], a[2]); break;
    case kInit: {
        // Through the hook's adapter, the object's vtable all traps (Init dispatches nothing on the device).
        void *vtable;
        std::memcpy(&vtable, gObject, 4);
        void *const traps = gTraps;
        std::memcpy(gObject, &traps, 4);
        drawfx::Note("ret", cDeviceDXAdapters::Init(Device(), nullptr, a[0] % 4));
        std::memcpy(gObject, &vtable, 4);
        break;
    }
    case kDestruct: Device()->~cDeviceDX(); break;
    case kReleaseLast: drawfx::Note("ret", RawSlot<2>(d)); break;
    case kDelete: delete d; break;
    }
}

// What a side leaves: the log so far plus the results.
struct Result {
    drawfx::Log log;
    std::vector<uint8_t> object, state, manager, sync;
    uint32_t color;
    long long blocks, bytes;
    std::string thrown;
};

Result Finish(long long blocks, long long bytes, const std::string &thrown)
{
    NoteRefs();
    // Asks the device's vtable, which a destructor leaves behind (or frees).
    if (gScript.c->op != kDestruct && !Gone(gScript.c->op)) NoteConnections();
    Result r;
    r.log = drawfx::gLog;
    if (!Gone(gScript.c->op)) r.object.assign(gObject + 4, gObject + cDeviceDX::kSize);
    r.state.assign(gStateBlock + 4, gStateBlock + sizeof(gStateBlock));
    r.manager.assign(gManagerBlock + 4, gManagerBlock + sizeof(gManagerBlock));
    r.sync.assign(gSyncData.bits, gSyncData.bits + sizeof(gSyncData.bits));
    r.color = *GameAddress<uint32_t>(kSyncClearColor);
    r.blocks = blocks;
    r.bytes = bytes;
    r.thrown = thrown;
    return r;
}

// Zeroes a stretch of the stack below the caller. Clear's original leaves its D3DSURFACE_DESC uninitialised and, when the
// depth surface's GetDesc fails (the fake writes nothing then), passes the stack's residue as the format to GetFormat; ours
// zeroes it. Scrubbed before each side, the residue is 0 on both, so the comparison no longer depends on what the harness
// last left on the stack (lane RK: seed 16 failed once the test's own code changed the residue).
__declspec(noinline) void ScrubStack()
{
    volatile uint8_t scratch[0x4000];
    for (size_t i = 0; i < sizeof(scratch); ++i) scratch[i] = 0;
}

template <class Run> Result Side(const Case &c, d3dfx::ErrorLog &errors, impfx::Imports &imp, bool original, void *vtable, Run run)
{
    gObject = Gone(c.op) ? static_cast<uint8_t *>(::operator new(cDeviceDX::kSize)) : gObjectBlock;
    BuildSide(c, errors, imp, original, vtable);
    std::string thrown;
    long long blocks = 0, bytes = 0;
    // The log's own buffer made big enough first, so it does not grow (and count) on one side only.
    drawfx::gLog = drawfx::Log();
    drawfx::gLog.reserve(4096);
    {
        difftest::AllocationCounter counter;
        ScrubStack();
        try {
            run();
        } catch (const cError &e) {
            char text[48];
            std::snprintf(text, sizeof(text), "cError vt %d code %d", e.mVTable == GameAddress<void>(0xd9cd70), e.mCode);
            thrown = text;
        }
        counter.Stop();
        blocks = counter.Blocks();
        bytes = counter.Bytes();
    }
    Result r = Finish(blocks, bytes, thrown);
    DestroySide();
    gObject = gObjectBlock;
    return r;
}

const char *kOpNames[kOpCount] = {"QueryInterface", "AddRef", "Release", "OnEvent(connection)", "OnEvent(canvas)",
                                  "OnEvent(resize)", "OnEvent(other)", "OnQuery", "NotifyListeners", "AskListeners",
                                  "Connect", "Disconnect", "IsActive", "GetEventCount", "GetEventIds",
                                  "GetEventAttribute", "getters", "SetFlags", "SetCreationBlocked", "ApplySettings",
                                  "OnDeviceLost", "ReleaseDevice", "ProcessVertices", "Clear", "ClearTargets",
                                  "BeginScene", "EndScene", "DrawPrimitive", "DrawIndexedPrimitive", "GetSettings",
                                  "GetBackBuffer", "GetTargetProviderSurface", "GetTargetSize", "DumpInfo",
                                  "WaitForGpu", "Slot7/Slot10", "Init", "destructor", "Release (last)", "delete"};

// Our vtable word (a constructed object's).
void *OurVTable()
{
    alignas(16) uint8_t scratch[cDeviceDX::kSize];
    cDeviceDXAdapters::Construct(scratch, nullptr);
    void *vtable;
    std::memcpy(&vtable, scratch, 4);
    return vtable;
}

struct World {
    RegistryScope registry;
    Cells cells;
    StandIns stands;
    d3dfx::ErrorLog errors;
    impfx::Imports imp;
    explicit World(difftest::Context &ctx) : errors(ctx), imp(ctx)
    {
        IMPFX_ADD(imp, Sleep);
        IMPFX_ADD(imp, OutputDebugStringA, "s");
    }
};

// What a side's log shows happened: each note's label, each fake call's slot name, "thrown" for an escaped cError.
void Seen(const Result &r, std::set<std::string> &seen)
{
    for (const drawfx::Call &call : r.log) {
        if (!call.iface) seen.insert(call.note);
        else if (call.slot < call.iface->count) seen.insert(call.iface->slots[call.slot].name);
    }
    if (!r.thrown.empty()) seen.insert("thrown");
}

// Runs `rounds` cases of each op in `ops`. `needs`: what the original's logs must show over the sweep (note labels,
// slot names, "thrown"), so a sweep that never reaches a path fails.
void Sweep(difftest::Context &ctx, uint32_t seed, const std::vector<uint32_t> &ops, unsigned rounds,
           std::initializer_list<const char *> needs = {})
{
    World world(ctx);
    void *const ours = OurVTable();
    void *const game = GameAddress<void>(kVTableRva);
    std::mt19937 gen(seed);
    std::set<std::string> seen;
    for (uint32_t op : ops) {
        for (unsigned round = 0; round < rounds; ++round) {
            Case c;
            SeedCase(gen, c, op);
            const Result a = Side(c, world.errors, world.imp, true, game, [&] { RunOriginal(ctx, c); });
            const Result b = Side(c, world.errors, world.imp, false, ours, [&] { RunOurs(c); });
            Seen(a, seen);
            const char *label = kOpNames[op];
            if (!drawfx::Same(ctx, a.log, b.log, label, round)) return;
            if (a.object != b.object || a.state != b.state || a.manager != b.manager || a.sync != b.sync ||
                a.color != b.color || a.blocks != b.blocks || a.bytes != b.bytes || a.thrown != b.thrown) {
                size_t at = 0;
                while (at < a.object.size() && a.object[at] == b.object[at]) ++at;
                ctx.Fail("%s round %u: object %d (first difference +0x%zx) state %d manager %d sync %d color %08x/%08x "
                         "blocks %lld/%lld bytes %lld/%lld thrown '%s'/'%s'",
                         label, round, a.object == b.object, at + 4, a.state == b.state, a.manager == b.manager,
                         a.sync == b.sync, a.color, b.color, a.blocks, b.blocks, a.bytes, b.bytes, a.thrown.c_str(),
                         b.thrown.c_str());
                return;
            }
        }
    }
    for (const char *need : needs)
        if (!seen.count(need)) ctx.Fail("the sweep never reached '%s' (a vacuous path)", need);
}

std::vector<uint32_t> Ops(std::initializer_list<uint32_t> ops) { return ops; }

} // namespace

// Construction: every byte the constructor writes, over random bytes (the two at +0x76 stay as they were).
DIFF_TEST(cDeviceDX_Construct, 0x167200)
{
    std::mt19937 gen(0x167200);
    for (unsigned round = 0; round < 64; ++round) {
        alignas(16) uint8_t a[cDeviceDX::kSize], b[cDeviceDX::kSize];
        for (uint8_t &x : a) x = uint8_t(gen());
        std::memcpy(b, a, sizeof(a));
        void *const ra = ctx.Original<void *(__thiscall *)(void *)>(0x167200)(a);
        void *const rb = cDeviceDXAdapters::Construct(b, nullptr);
        if (ra != a || rb != b) ctx.Fail("round %u: the result is not the object", round);
        if (std::memcmp(a + 4, b + 4, sizeof(a) - 4) != 0) {
            size_t at = 4;
            while (a[at] == b[at]) ++at;
            ctx.Fail("round %u: object differs at +0x%zx", round, at);
            return;
        }
        if (Get<uint32_t>(a, 0) != Word(GameAddress<void>(kVTableRva))) ctx.Fail("the original's vtable word is not 0xd9e478");
    }
}

// The behaviour slots (0-14).
DIFF_TEST(cDeviceDX_Behaviour, 0x169bf0)
{
    Sweep(ctx, 0x169bf0,
          Ops({kQueryInterface, kAddRef, kRelease, kOnConnection, kOnResize, kOnOther, kOnQuery, kNotify, kAsk, kConnect,
               kDisconnect, kIsActive, kEventCount, kEventIds, kEventAttribute, kEmptySlots}),
          96, {"OnEvent", "OnQuery", "id", "attribute", "GetWidth", "GetHeight", "CreateDevice", "Slot26"});
}

// The getters and settings slots.
DIFF_TEST(cDeviceDX_Getters, 0x16a590)
{
    Sweep(ctx, 0x16a590, Ops({kGetters, kGetSettings, kGetBackBuffer, kGetProviderSurface, kGetTargetSize, kSetCreationBlocked}),
          48, {"get", "is", "chains", "settings", "size", "GetSurface"});
}

// The device's life: settings applied, the device lost, released, the GPU wait, and the frame (canvas paint) with its
// recovery paths.
DIFF_TEST(cDeviceDX_Settings, 0x16a520)
{
    Sweep(ctx, 0x16a520, Ops({kApplySettings, kSetFlags}), 160,
          {"CreateDevice", "Slot38", "Slot37", "Reset", "enum.0x178b80", "manager.ReleaseAll"});
}
DIFF_TEST(cDeviceDX_Lost, 0x169690) { Sweep(ctx, 0x169690, Ops({kOnDeviceLost}), 160, {"event", "Slot26", "Release"}); }
DIFF_TEST(cDeviceDX_Release, 0x16a5c0)
{
    Sweep(ctx, 0x16a5c0, Ops({kReleaseDevice}), 160,
          {"EndScene", "LogD3DError", "Reset", "state.Flush", "event", "Issue", "GetData", "manager.ReleaseAll",
           "state.ReleaseDeviceObjects", "enum.0x178b80", "Slot37", "Slot38", "Slot26"});
}
DIFF_TEST(cDeviceDX_WaitForGpu, 0x169740)
{
    Sweep(ctx, 0x169740, Ops({kWaitForGpu}), 160,
          {"state.Flush", "Issue", "GetData", "state.SetBackBufferTarget", "Clear", "LockRect", "UnlockRect",
           "state.MarkTargetsDirty", "LogD3DError"});
}
DIFF_TEST(cDeviceDX_Canvas, 0x1699b0)
{
    Sweep(ctx, 0x1699b0, Ops({kOnCanvas}), 400,
          {"TestCooperativeLevel", "BeginScene", "EndScene", "Present", "event", "CreateDevice", "Sleep", "Slot26",
           "Issue", "thrown", "scene thrown"});
}

// Drawing: clears, the scene bracket, draws, vertex processing, the info dump.
DIFF_TEST(cDeviceDX_Clear, 0x167890)
{
    Sweep(ctx, 0x167890, Ops({kClear}), 160, {"state.Flush", "GetDesc", "GetFormat", "GetStencilBits", "Clear", "LogD3DError"});
}
DIFF_TEST(cDeviceDX_ClearTargets, 0x167a60)
{
    Sweep(ctx, 0x167a60, Ops({kClearTargets}), 160,
          {"GetPixelFormat", "GetStencilBits", "GetWidth", "GetHeight", "state.SetBackBufferTarget", "Clear",
           "LogD3DError", "state.MarkTargetsDirty"});
}
DIFF_TEST(cDeviceDX_Draw, 0x169570)
{
    Sweep(ctx, 0x169570, Ops({kBeginScene, kEndScene, kDrawPrimitive, kDrawIndexed}), 96,
          {"BeginScene", "EndScene", "DrawPrimitive", "DrawIndexedPrimitive", "ManageTextureMemory", "state.Flush",
           "LogD3DError"});
}
DIFF_TEST(cDeviceDX_ProcessVertices, 0x16a040)
{
    Sweep(ctx, 0x16a040, Ops({kProcessVertices}), 96,
          {"state.ResetTexCoordStates", "ProcessVertices", "LogD3DError", "state.RestoreTexCoordStates"});
}
DIFF_TEST(cDeviceDX_DumpInfo, 0x169f70)
{
    Sweep(ctx, 0x169f70, Ops({kDumpInfo}), 48, {"SetRZCharStr", "enum.dump", "state.dump", "OutputDebugStringA"});
}

// Init (ours through its hook adapter on an object whose vtable is all traps) and the destructors.
DIFF_TEST(cDeviceDX_InitDestroy, 0x169cc0)
{
    Sweep(ctx, 0x169cc0, Ops({kInit, kDestruct, kReleaseLast, kDelete}), 96,
          {"GetAdapterMonitor", "GetClassObject", "SetMonitor", "new enum", "new manager", "new state", "Slot25",
           "Slot27", "enum.0x178b80", "Release", "OnEvent"});
}

// ProcessVertices' __try: a structured exception in the driver call ends the call quietly. Ours only: the harness maps
// the original's image without registering its exception handlers, so the original's __except never runs here (the
// fault ends the process). Checked: the texture-coordinate states are restored and nothing is reported.
DIFF_TEST(cDeviceDX_ProcessVerticesFault, 0x16a040)
{
    World world(ctx);
    void *const ours = OurVTable();
    std::mt19937 gen(0x16a041);
    for (unsigned round = 0; round < 8; ++round) {
        Case c;
        SeedCase(gen, c, kProcessVertices);
        const Result r = Side(c, world.errors, world.imp, false, ours, [&] {
            gDevice.value[0] = 8;
            RunOurs(c);
        });
        std::set<std::string> seen;
        Seen(r, seen);
        if (!seen.count("ProcessVertices") || !seen.count("state.RestoreTexCoordStates") || seen.count("LogD3DError"))
            ctx.Fail("round %u: the fault was not contained (restored %d, reported %d)", round,
                     int(seen.count("state.RestoreTexCoordStates")), int(seen.count("LogD3DError")));
    }
}

