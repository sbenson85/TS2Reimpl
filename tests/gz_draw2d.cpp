// Differential tests for nGZGraphic4::cDraw2D and cDraw2DSystemService as whole classes (src/gz/cDraw2D.h; lane OD,
// rendering R5b step 1).
//
// The device objects are drawfx fakes at the interface level the class talks to (cIDeviceDX, cIDeviceState, the
// dynamic vertex and index arrays, state blocks, shaders), not Direct3D fakes: cDraw2D never reaches Direct3D itself.
// Its device objects come from CreateDeviceObjects 0x150330 (the original on one side, ours on the other; lane OU) and
// every render state it sets goes as a command to the state cache's ApplyStateCommand (0x17fd40, still game code): the commands' bytes, the state blocks,
// the vertex and index bytes it writes, the shader constants and the draw calls are what it does, and those are what
// the fakes log.
//
// Game code still called is stood in for on both sides by recorders patched over its entry (shape::CodePatch): each
// logs its `this` and arguments and answers from a script both sides share. The atlas and record code (DrawImage,
// DrawRecord, CreateRecord, the record pixels, RebuildAtlases, SetTextureSource, ReleaseRecordCells, AllocateRecord,
// PlaceRecord) and the hardware cache class run for real on both sides, against fakes of the COM objects they make and
// use (the class factory for texture and buffer objects, textures, buffers with their cache slot, the device's enum,
// the pixel formats, the image manipulator, the draw system's watch list). Still stood in: the three cached service
// getters CreateDeviceObjects asks (0x18dcf0 / 0x18ddf0 / 0x18dd70: the vertex formats, the shader compiler, the declarations; their fakes record the calls, and the shader text is judged by what it does, tests/gz_draw2d_device_fx.h), the current device 0x18ae40, the event registry
// 0x1516f0, UploadRecord 0x152410, AdjustTextureSize 0x18c990, the memory-block surface (ours, src/gz/cMemBlock.cpp, and the game's 0x18d1a0 / 0x18d2c0) and
// GetDraw2D 0x18c000. The cache pool's page (0xfeb8e0) is a seeded page of the test's.
//
// GZDraw2D_Slots: each side builds the object in a heap block of the same seeded bytes, the original by its constructor
// 0x14ba80, ours by placement new, and compares every word but the three vtable words right after (the constructor's
// work: the graphics system's Connect, the device ready or not). The case then installs the device fakes, records,
// atlas pools and a clip stack alike on both sides (through the vector types, whose layout is the game's), and makes
// 90 random calls through the object's three faces by name: the clip stack, materials and shapes, blend modes, image
// records (draw, create, add/release, info), the batch slots, Tick, events (the device going and coming, connection
// changes), the behaviour slots against a real cConnectionManager with listener fakes, QueryInterface / AddRef /
// Release on every face, CreateEngine, and the engine face as the draw context drives it (targets, clips, colours,
// gradients, textures through atlas tiles, routes that end in DrawTriangles / DrawQuads). After every call the log gets
// every word of the object but the vtables (vectors as counts, capacities and elements, nested vectors too) and the
// shader constants. At the end the object is released to zero (the destructor, ReleaseDeviceObjects, the members), and
// the log gets every fake's count and the side's heap balance.
// GZDraw2D_Intersect: 0x150e20 (hooked as itself) on random rectangles, the output aliasing an input or not.
// GZDraw2DService_Create: the creator 0x14f2b0 on a seeded block (every byte but the vtable), and a failed allocation.
// GZDraw2DService_Run: a service from its creator (each side's), then random slot calls: QueryInterface, AddRef /
// Release, the id, priorities, running flag, Init (which makes the side's cDraw2D and registers its factory with a
// draw-system fake), OnTick, OnIdle, GetDraw2D, Shutdown; released to zero at the end; counts and heap compared.
#include "code_patch.h"
#include "difftest.h"
#include "drawfx.h"
#include "gz_draw2d_device_fx.h"

#include "gz/cConnectionManager.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cDraw2D.h"
#include "gz/cDraw2DBufferHardwareCache.h"
#include "gz/GraphicServices.h"
#include "gz/cGZAny.h"
#include "gz/cGZEventReflector.h"
#include "gz/cGZGraphicSystem.h"
#include "gz/cMemBlock.h"
#include "gz/cIGZPropertySet.h"
#include "gz/cVertexArrayDX.h"
#include "rz/RZFixedAllocator.h"
#include "rz/StaticGetters.h"
#include "sg/TextureDescription.h"

#include <Windows.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

using namespace nGZGraphic4;
using nGZEvent::cConnectionManager;
using nGZEvent::cConnectionManagerAdapters;

namespace {

using D = cDraw2D;
using Hw = cGZDrawContextRenderToHardware;
using drawfx::Ret;

// ---- interfaces of the fakes ----

constexpr drawfx::Slot U{0, Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, Ret::Int, true, "Release", ""};

struct Named {
    unsigned slot;
    drawfx::Slot s;
};
template <unsigned N> struct Table {
    drawfx::Slot s[N];
};
template <unsigned N> constexpr Table<N> MakeTable(std::initializer_list<Named> named)
{
    Table<N> t{};
    for (unsigned i = 0; i < N; ++i) t.s[i] = U;
    t.s[0] = kQI;
    t.s[1] = kAddRef;
    t.s[2] = kRelease;
    for (const Named &n : named) t.s[n.slot] = n.s;
    return t;
}

inline constexpr auto kUnknownTable = MakeTable<3>({});
inline constexpr drawfx::Iface kUnknown{"cIGZUnknown", 3, kUnknownTable.s};
// The framework (cIGZFrameWork): 3 GetClassObject (the director's COM face: lane OL's image records ask it for textures
// and buffers), 5 GetSystemService, 9 AddToTick.
inline constexpr auto kFrameworkTable = MakeTable<10>({{3, {3, Ret::Int, true, "GetClassObject", "uuW0"}},
                                                       {5, {3, Ret::Int, true, "GetSystemService", "uui"}},
                                                       {9, {1, Ret::Int, true, "AddToTick", "i"}}});
inline constexpr drawfx::Iface kFramework{"cIGZFrameWork", 10, kFrameworkTable.s};
// cGraphicSystemDX's behaviour face (0xa55227bb): 8 Connect.
inline constexpr auto kGraphicsTable = MakeTable<9>({{8, {3, Ret::Int, true, "graphics.Connect", "uiu"}}});
inline constexpr drawfx::Iface kGraphics{"cGraphicSystemDX", 9, kGraphicsTable.s};
// cIDeviceDX: 32 DrawIndexedPrimitive, 35 IsDeviceReady, 41 GetTargetSize (the hidden result pointer).
inline constexpr auto kDeviceTable = MakeTable<44>({{16, {0, Ret::Int, true, "GetDeviceEnumConst", ""}},
                                                    {20, {0, Ret::Int, true, "GetStateConst", ""}},
                                                    {32, {6, Ret::Int, true, "DrawIndexedPrimitive", "uuuuuu"}},
                                                    {35, {0, Ret::Int, true, "IsDeviceReady", ""}},
                                                    {41, {1, Ret::Int, true, "GetTargetSize", "i"}}});
inline constexpr drawfx::Iface kDevice{"cIDeviceDX", 44, kDeviceTable.s};
// cIDeviceState.
inline constexpr auto kStateTable = MakeTable<32>({{9, {1, Ret::Int, true, "Reset", "u"}},
                                                   {10, {2, Ret::Int, true, "CreateStateBlock", "uW0"}},
                                                   {11, {1, Ret::Int, true, "ApplyStateBlock", "i"}},
                                                   {12, {1, Ret::Int, true, "ApplyStateCommand", "i"}},
                                                   {13, {2, Ret::Int, true, "SetTexture", "ui"}},
                                                   {14, {1, Ret::Int, true, "SetVertexShader", "i"}},
                                                   {16, {1, Ret::Int, true, "SetVertexDeclaration", "i"}},
                                                   {17, {4, Ret::Int, true, "SetStreamSource", "uiuu"}},
                                                   {18, {1, Ret::Int, true, "SetIndices", "i"}},
                                                   {19, {2, Ret::Int, true, "LockVertexFloatConstants", "uu"}},
                                                   {30, {0, Ret::Int, true, "GetChangeCount", ""}}});
inline constexpr drawfx::Iface kState{"cIDeviceState", 32, kStateTable.s};
// cIDynamicVertexArray (the vertex and the index array).
inline constexpr auto kArrayTable = MakeTable<25>({{3, {0, Ret::Int, true, "ReleaseResource", ""}},
                                                   {6, {0, Ret::Int, true, "IsCreated", ""}},
                                                   {15, {3, Ret::Int, true, "Lock", "uuu"}},
                                                   {16, {0, Ret::Int, true, "Unlock", ""}},
                                                   {17, {0, Ret::Int, true, "GetKind", ""}},
                                                   {18, {0, Ret::Int, true, "GetSize", ""}},
                                                   {20, {2, Ret::Int, true, "Reserve", "uu"}},
                                                   {22, {0, Ret::Int, true, "GetOffset", ""}},
                                                   {23, {3, Ret::Int, true, "Create", "iuu"}}});
inline constexpr drawfx::Iface kArray{"cIDynamicVertexArray", 25, kArrayTable.s};
// cIDraw2DMaterial: the points and values are logged by the handler (their count is an argument).
inline constexpr auto kMaterialTable = MakeTable<5>({{3, {2, Ret::Int, true, "GetStateBlock", "ii"}},
                                                     {4, {5, Ret::Int, true, "Draw", "iuuii"}}});
inline constexpr drawfx::Iface kMaterial{"cIDraw2DMaterial", 5, kMaterialTable.s};
// cIGZBuffer: 3 Init, 9 GetWidth, 10 GetHeight, 11 GetBounds and 15 GetNativeColorType (hidden result pointers), 26
// GetTransparentColor, 33 ConvertNativeValueToRGBA, 34 GetColorSurfaceBits, 35 GetColorSurfaceStride, 36
// GetChangeCounter, 37 GetHardwareCache, 38 SetHardwareCache (the cache itself is not logged: its address; "W0" is the
// presence word alone).
inline constexpr auto kBufferTable = MakeTable<40>({{3, {4, Ret::Int, true, "Init", "uuuu"}},
                                                    {9, {0, Ret::Int, true, "GetWidth", ""}},
                                                    {10, {0, Ret::Int, true, "GetHeight", ""}},
                                                    {11, {1, Ret::Int, true, "GetBounds", "i"}},
                                                    {15, {1, Ret::Int, true, "GetNativeColorType", "i"}},
                                                    {26, {1, Ret::Int, true, "GetTransparentColor", "i"}},
                                                    {33, {5, Ret::Int, true, "ConvertNativeValueToRGBA", "uiiii"}},
                                                    {34, {0, Ret::Int, true, "GetColorSurfaceBits", ""}},
                                                    {35, {0, Ret::Int, true, "GetColorSurfaceStride", ""}},
                                                    {36, {0, Ret::Int, true, "GetChangeCounter", ""}},
                                                    {37, {0, Ret::Int, true, "GetHardwareCache", ""}},
                                                    {38, {1, Ret::Int, true, "SetHardwareCache", "W0"}}});
inline constexpr drawfx::Iface kBuffer{"cIGZBuffer", 40, kBufferTable.s};
// The texture an atlas lives in (cITexture): 6 IsCreated, 13 GetVideoMemorySize, 22 Create(device, w, h, format, levels,
// quality, pool).
inline constexpr auto kTextureTable = MakeTable<23>({{6, {0, Ret::Int, true, "IsCreated", ""}},
                                                     {13, {0, Ret::Int, true, "GetVideoMemorySize", ""}},
                                                     {22, {7, Ret::Int, true, "Create", "iuuuufu"}}});
inline constexpr drawfx::Iface kTexture{"cITexture", 23, kTextureTable.s};
// The display's format table (cIDeviceEnum): 11 IsColorFormatSupportedForDisplay(format, usage).
inline constexpr auto kDeviceEnumTable =
    MakeTable<23>({{11, {2, Ret::Int, true, "IsColorFormatSupportedForDisplay", "uu"}},
                   {22, {0, Ret::Int, true, "GetProperties", ""}}});
inline constexpr drawfx::Iface kDeviceEnum{"cIDeviceEnum", 23, kDeviceEnumTable.s};
// Lane OU: what CreateDeviceObjects asks for and makes. The settings (cIGZPropertySetString): 9 GetPropertyValue(key,
// out); the vertex formats manager: 3 BeginFormat, 4 AddElement(stream, type, usage, index), 5 EndFormat(out) ("W0": the
// out pointer is logged as present only, the field it lands in is logged with the object); the vertex format: 15
// GetOffsetAny(usage, index); the shader compiler: 3 AssembleProgram(device, text, size, out, errors, flags) (the text
// and its size are the handler's: see AssembleProgram); a shader: 6 IsCreated, 10 GetInfo; the declarations manager: 3
// GetDeclaration(device, format, out); a made resource (state block, declaration): 6 IsCreated.
inline constexpr auto kPropsTable = MakeTable<10>({{9, {2, Ret::Int, true, "GetPropertyValue", "W0W0"}}});
inline constexpr drawfx::Iface kProps{"cIGZPropertySetString", 10, kPropsTable.s};
inline constexpr auto kVertexFormatsTable = MakeTable<6>({{3, {0, Ret::Int, true, "BeginFormat", ""}},
                                                          {4, {4, Ret::Int, true, "AddElement", "uuub"}},
                                                          {5, {1, Ret::Int, true, "EndFormat", "W0"}}});
inline constexpr drawfx::Iface kVertexFormats{"cIVertexFormatsManager", 6, kVertexFormatsTable.s};
inline constexpr auto kVertexFormatTable = MakeTable<16>({{15, {2, Ret::Int, true, "GetOffsetAny", "ub"}}});
inline constexpr drawfx::Iface kVertexFormatObject{"cIVertexFormat", 16, kVertexFormatTable.s};
inline constexpr auto kShaderManagerTable = MakeTable<4>({{3, {6, Ret::Int, true, "AssembleProgram", "iW0W0W0iu"}}});
inline constexpr drawfx::Iface kShaderManager{"cIShaderProgramsManager", 4, kShaderManagerTable.s};
inline constexpr auto kShaderTable = MakeTable<11>({{6, {0, Ret::Int, true, "IsCreated", ""}},
                                                    {10, {0, Ret::Int, true, "GetInfo", ""}}});
inline constexpr drawfx::Iface kShaderObject{"cIShaderProgramDX", 11, kShaderTable.s};
inline constexpr auto kDeclarationsTable = MakeTable<4>({{3, {3, Ret::Int, true, "GetDeclaration", "iiW0"}}});
inline constexpr drawfx::Iface kDeclarations{"cIVertexDeclarationsManagerDX", 4, kDeclarationsTable.s};
inline constexpr auto kResourceTable = MakeTable<7>({{6, {0, Ret::Int, true, "IsCreated", ""}}});
inline constexpr drawfx::Iface kResource{"cIDeviceResource", 7, kResourceTable.s};
// The pixel formats manager (cIPixelFormatsManager): 6 GetARGB(total, a, r, g, b).
inline constexpr auto kFormatsTable = MakeTable<7>({{6, {5, Ret::Int, true, "GetARGB", "uuuuu"}}});
inline constexpr drawfx::Iface kFormats{"cIPixelFormatsManager", 7, kFormatsTable.s};
// The image manipulator (cIImageManip): 3 CopySurfaceRect(source, sourceRect, target, targetRect, key). The surfaces
// are the 12-byte memory surfaces (their words are logged).
inline constexpr auto kImageManipTable = MakeTable<4>({{3, {5, Ret::Int, true, "CopySurfaceRect", "W3rW3rW1"}}});
inline constexpr drawfx::Iface kImageManip{"cIImageManip", 4, kImageManipTable.s};
// The back buffer's raw face (0xe8c95947): 4 answers whether it is the back buffer.
inline constexpr auto kRawTable = MakeTable<5>({{4, {0, Ret::Int, true, "raw.Slot4", ""}}});
inline constexpr drawfx::Iface kRaw{"cBackbufferAsGZBufferRaw", 5, kRawTable.s};
// The draw system: 3 AddFactory, 11 AddHardwareCache, 12 RemoveHardwareCache (the cache logged by the handler's note),
// 15 GetTick, 20 GetFlag.
inline constexpr auto kDrawSystemTable = MakeTable<21>({{3, {1, Ret::Int, true, "AddFactory", "i"}},
                                                        {11, {1, Ret::Int, true, "AddHardwareCache", "W0"}},
                                                        {12, {1, Ret::Int, true, "RemoveHardwareCache", "W0"}},
                                                        {15, {0, Ret::Int, true, "GetTick", ""}},
                                                        {20, {0, Ret::Int, true, "GetFlag", ""}}});
inline constexpr drawfx::Iface kDrawSystem{"cIGZDrawSystem", 21, kDrawSystemTable.s};
// A listener of the object's events.
inline constexpr auto kListenerTable = MakeTable<5>({{3, {4, Ret::Int, true, "listener.OnEvent", "uuii"}},
                                                     {4, {3, Ret::Int, true, "listener.OnQuery", "uui"}}});
inline constexpr drawfx::Iface kListener{"cIAbstractBehavior", 5, kListenerTable.s};

// ---- addresses ----

constexpr uint32_t kConstructor = 0x14ba80, kServiceCreator = 0x14f2b0;
constexpr uint32_t kPrimaryVTable = 0xd9c3c0, kEngineVTable = 0xd9c474, kFactoryVTable = 0xd9c504,
                   kServiceVTable = 0xd9c518;
constexpr uint32_t kFrameworkCell = 0x10c9378, kDesignHeight = 0x10ecc88;
constexpr uint32_t kObjectTag = 0x0d000000, kVertexTag = 0x0e000000, kIndexTag = 0x0f000000, kConstantsTag = 0x0b000000,
                   kDataTag = 0x0a000000, kManagerTag = 0x09000000, kServiceTag = 0x08000000;

// ---- the world ----

constexpr unsigned kTargets = 3, kBlocks = 8, kRecordBuffers = 6, kListeners = 3, kMaxAtlases = 3;
drawfx::Fake gFramework, gGraphics, gDevice, gState, gVertexArray, gIndexArray, gDrawSystem, gMaterials[2],
    gBlocks[kBlocks], gTargets[kTargets], gStranger, gRecordBuffers[kRecordBuffers],
    gTextures[3][kMaxAtlases], gRaw, gBackbuffer, gListeners[kListeners];
// Lane OL: the objects the image records ask the COM registry for (made as asked: one id each, in order), the device's
// format table, the pixel formats manager and the image manipulator.
constexpr unsigned kNewObjects = 160;
drawfx::Fake gNewTextures[kNewObjects], gNewBuffers[kNewObjects], gDeviceEnum, gFormats, gImageManip;
unsigned gNewTextureCount = 0, gNewBufferCount = 0, gSurfaceCount = 0;
constexpr uint32_t kFormatTag = 0x5a100000;
// Lane OU: the objects CreateDeviceObjects asks for (the settings, the vertex format with its manager, the shader
// compiler and its one shader, the declarations manager and its one declaration, the state blocks it records, one fake
// each in the order they are made) and the shader's info.
constexpr unsigned kMadeBlocks = 240;
drawfx::Fake gProps, gVertexFormats, gVertexFormat, gShaderManager, gShader, gDeclarations, gDeclaration,
    gMadeBlocks[kMadeBlocks];
unsigned gMadeBlockCount = 0;
nGZGraphic4::cShaderProgramInfo gShaderInfo{};

struct TargetSpec {
    uint32_t width, height;
    cGZRect bounds;
    bool bufferFace;
};
struct Case {
    TargetSpec targets[kTargets]; // two engine targets, the engine's texture
    uint32_t deviceWidth, deviceHeight;
    bool deviceAtStart, readyAtStart;
    uint32_t layout; // the vertex layout CreateDeviceObjects gives (two orders)
    // Lane OL: what the display takes for textures, the targets' colour formats, the texture buffer's transparent colour.
    bool supports16, supports32;
    uint32_t format[kTargets];
    bool hasKey, writesAlpha;
    uint32_t key;
    // Lane OU: the settings' "draw2dForceSWVP" (found or not, and its value), whether the shader runs with hardware
    // vertex processing, and how often a made object says it was created.
    bool forceSoftware, forceFound, hardwareShader;
    uint32_t madeOften; // a made object is created when a draw of the script below this (of 8) is hit
};
Case gCase;
std::mt19937 gScript; // the recorders' and fakes' answers (the same seed on each side)

alignas(16) uint8_t gArrayBuffer[2][0x10000];
alignas(16) uint32_t gConstants[8];
uint32_t gEventData[2];
nGZEvent::cEventAttributeMap gRegistry;
char gNames[3][8];
const uint32_t kRegistryIds[] = {0x10, 0xca7ccbbc, 0xa0000000};
const uint32_t kListenEvents[kListeners] = {0xca7ccbbc, 0x11, 0x22};
const uint32_t kHeardAs[kListeners] = {0x61, 0x62, 0x63};
cConnectionManager *gManager = nullptr;
void *gBlock = nullptr; // the object of the side running
bool gEmptyTarget = false; // lane OU: the device reports a zero-width target (case 49 sets it for one call)

uint32_t Word(const void *p, uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(p) + offset, 4);
    return v;
}
D *View(void *block) { return reinterpret_cast<D *>(block); }
uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

// ---- handlers ----

uint32_t ServiceQuery(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Fake *f = nullptr;
    if (a[0] == D::kGraphicSystemId && a[1] == D::kGraphicSystemIid) f = &gGraphics;
    if (a[0] == cIGZDrawSystem::kServiceId && a[1] == cIGZDrawSystem::kIid) f = &gDrawSystem;
    if (!f) return 0;
    ++f->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = f;
    return 1;
}
// The constructor hands the graphics system the new object first: a cDraw2D made inside Init (the service test) is
// registered here, and the call's logged word normalised afterwards.
uint32_t GraphicsConnect(drawfx::Fake &, const uint32_t *a)
{
    if (drawfx::Norm(a[1]) == a[1]) {
        difftest::NotCounted bookkeeping;
        drawfx::AddRange(reinterpret_cast<void *>(uintptr_t(a[1])), D::kSize, kObjectTag);
        drawfx::gLog.back().words[1] = drawfx::Norm(a[1]);
    }
    return 0;
}
uint32_t TargetSize(drawfx::Fake &, const uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = gEmptyTarget ? 0 : gCase.deviceWidth;
    out[1] = gCase.deviceHeight;
    return a[0];
}
// The state commands: the header (vtable, id, size) and the bytes the original writes for the command's id. The bytes
// past those are left out because the original leaves them unwritten (stack garbage): the viewport's MinZ / MaxZ (0x151e15
// .. 0x151e8b and 0x14fc5d .. writes +4, +6 and +8 .. +0x17), the byte after the id in every command (each writes the
// id byte and the size word only), the texture filter's +0xe (0x14fdb1 .. 0x14fdcd), the alpha test's, depth test's,
// cull mode's and clipping's bytes after their fields (0x14febe, 0x151fdb, 0x152003, 0x151db1).
uint32_t StateCommand(drawfx::Fake &, const uint32_t *a)
{
    const auto *c = reinterpret_cast<const uint8_t *>(uintptr_t(a[0]));
    drawfx::Call call;
    call.note = "command";
    uint16_t size;
    std::memcpy(&size, c + 6, 2);
    call.words = {Word(c, 0), c[4], size};
    struct Span {
        uint8_t at, count;
    };
    std::vector<Span> spans;
    switch (c[4]) {
    case 1: spans = {{8, 16}}; break;              // viewport
    case 2: spans = {{8, 8}}; break;               // depth range
    case 3: case 4: spans = {{8, 1}}; break;       // clipping, cull mode
    case 0x11: case 0x1a: spans = {{8, 4}}; break; // alpha blend, colour scalar
    case 0x12: case 0x14: spans = {{8, 2}}; break; // alpha test, depth test
    case 0x19: spans = {{8, 13}}; break;           // texture blend
    case 0x1e: spans = {{8, 6}, {0x10, 4}}; break; // texture filter
    case 0x1c: spans = {{8, 4}, {0xc, 3}}; break;  // texture addressing (lane OU: the sampler and the three modes)
    case 0x1f: spans = {{8, 4}, {0xc, 1}, {0x10, 4}}; break; // texture mip filter (the sampler, the filter, the LOD bias)
    default: call.words.push_back(0xbad1d); break;
    }
    for (const Span &s : spans)
        for (unsigned i = 0; i < s.count; ++i) call.words.push_back(c[s.at + i]);
    drawfx::gLog.push_back(std::move(call));
    return 0;
}
uint32_t LockConstants(drawfx::Fake &, const uint32_t *) { return drawfx::P(gConstants); }
uint32_t ChangeCount(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
// The arrays: Reserve takes a span (its bytes in value[0], its first byte at value[1]) and fills it with a pattern;
// Lock gives the buffer; Unlock logs the span's words.
uint32_t ArrayReserve(drawfx::Fake &self, const uint32_t *a)
{
    const uint32_t bytes = a[0] * a[1];
    self.value[0] = bytes;
    self.value[1] = (gScript() % 4) * a[0];
    std::memset(gArrayBuffer[self.value[7]], 0xa5, bytes < 0x10000 ? bytes : 0x10000);
    return a[0] ? self.value[1] / a[0] : 0;
}
uint32_t ArrayLock(drawfx::Fake &self, const uint32_t *) { return drawfx::P(gArrayBuffer[self.value[7]]); }
uint32_t ArrayUnlock(drawfx::Fake &self, const uint32_t *)
{
    drawfx::Call call;
    call.note = self.value[7] ? "indices" : "vertices";
    const uint32_t words = (self.value[0] < 0x10000 ? self.value[0] : 0x10000) / 4;
    call.words.assign(reinterpret_cast<const uint32_t *>(gArrayBuffer[self.value[7]]),
                      reinterpret_cast<const uint32_t *>(gArrayBuffer[self.value[7]]) + words);
    drawfx::gLog.push_back(std::move(call));
    return 0;
}
uint32_t Value0(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t Value1(drawfx::Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t MaterialDraw(drawfx::Fake &, const uint32_t *a)
{
    const uint32_t count = a[2] < 8 ? a[2] : 8;
    drawfx::Call call;
    call.note = "shape";
    const auto *points = reinterpret_cast<const uint32_t *>(uintptr_t(a[3]));
    const auto *values = reinterpret_cast<const uint32_t *>(uintptr_t(a[4]));
    for (uint32_t i = 0; i < 2 * count; ++i) call.words.push_back(points[i]);
    for (uint32_t i = 0; i < count; ++i) call.words.push_back(values[i]);
    drawfx::gLog.push_back(std::move(call));
    return 0;
}
// Buffers: value[0] the target spec (targets) or ~0; value[1] / value[2] width and height; value[3] the colour format.
uint32_t BufferWidth(drawfx::Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t BufferHeight(drawfx::Fake &self, const uint32_t *) { return self.value[2]; }
uint32_t BufferBounds(drawfx::Fake &self, const uint32_t *a)
{
    const cGZRect r = self.value[0] < kTargets ? gCase.targets[self.value[0]].bounds
                                               : cGZRect{0, 0, int32_t(self.value[1]), int32_t(self.value[2])};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), &r, sizeof(r));
    return a[0];
}
uint32_t BufferColorType(drawfx::Fake &self, const uint32_t *a)
{
    const cGZColorType type{self.value[3], 32};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), &type, sizeof(type));
    return a[0];
}

// ---- the objects of lane OL's image records (textures, buffers, the format table, the image manipulator) ----

// A texture of the COM registry: Create answers from the script, and a created texture says so.
uint32_t TextureCreate(drawfx::Fake &self, const uint32_t *)
{
    const bool ok = gScript() % 6 != 0;
    if (ok) self.value[0] = 1;
    return ok;
}
uint32_t TextureIsCreated(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
void SetupTexture(drawfx::Fake &f, uint32_t id, bool created)
{
    f.Reset<kTexture>(id);
    f.value[0] = created;
    f.On(6, &TextureIsCreated);
    f.On(22, &TextureCreate);
}

// The format table: what the display takes.
uint32_t FormatSupported(drawfx::Fake &, const uint32_t *a)
{
    if (a[0] == kFormatTag + 16) return gCase.supports16;
    if (a[0] == kFormatTag + 32) return gCase.supports32;
    return 1;
}
uint32_t FormatARGB(drawfx::Fake &, const uint32_t *a) { return kFormatTag + a[0]; }
uint32_t CopySurface(drawfx::Fake &, const uint32_t *) { return 1; }

// Buffers (value: 0 target spec or ~0, 1 width, 2 height, 3 colour format, 4 change counter, 5 transparent colour, 6 it
// has one, 7 conversion writes alpha; owner: the hardware cache it holds, counted).
uint32_t BufferInit(drawfx::Fake &self, const uint32_t *a)
{
    self.value[1] = a[0];
    self.value[2] = a[1];
    self.value[3] = a[2];
    return gScript() % 5 != 0;
}
uint32_t BufferTransparent(drawfx::Fake &self, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = self.value[5];
    return self.value[6] != 0;
}
uint32_t BufferConvert(drawfx::Fake &self, const uint32_t *a)
{
    *reinterpret_cast<uint8_t *>(uintptr_t(a[1])) = uint8_t(a[0] >> 16);
    *reinterpret_cast<uint8_t *>(uintptr_t(a[2])) = uint8_t(a[0] >> 8);
    *reinterpret_cast<uint8_t *>(uintptr_t(a[3])) = uint8_t(a[0]);
    if (self.value[7]) *reinterpret_cast<uint8_t *>(uintptr_t(a[4])) = uint8_t(a[0] >> 24);
    return 1;
}
uint32_t BufferBits(drawfx::Fake &self, const uint32_t *) { return 0x7e000000u + self.id * 0x1000u; }
uint32_t BufferStride(drawfx::Fake &self, const uint32_t *) { return self.value[1] * 4; }
uint32_t BufferChange(drawfx::Fake &self, const uint32_t *) { return self.value[4]; }
uint32_t BufferGetCache(drawfx::Fake &self, const uint32_t *) { return drawfx::P(self.owner); }
// The buffer keeps the cache counted and tells it whose it is (Attach).
uint32_t BufferSetCache(drawfx::Fake &self, const uint32_t *a)
{
    auto *const cache = reinterpret_cast<cDraw2DBufferHardwareCache *>(uintptr_t(a[0]));
    if (cache) {
        cache->AddRef();
        cache->Attach(reinterpret_cast<cIGZBuffer *>(&self));
    }
    if (auto *const old = static_cast<cIGZUnknown *>(self.owner)) old->Release();
    self.owner = cache;
    return 1;
}
void SetupBuffer(drawfx::Fake &f, uint32_t id)
{
    f.Reset<kBuffer>(id);
    f.value[0] = ~0u;
    f.On(3, &BufferInit);
    f.On(9, &BufferWidth);
    f.On(10, &BufferHeight);
    f.On(11, &BufferBounds);
    f.On(15, &BufferColorType);
    f.On(26, &BufferTransparent);
    f.On(33, &BufferConvert);
    f.On(34, &BufferBits);
    f.On(35, &BufferStride);
    f.On(36, &BufferChange);
    f.On(37, &BufferGetCache);
    f.On(38, &BufferSetCache);
}

// The COM registry: a texture or a buffer, a new one each time, as the caller's reference.
uint32_t GetClassObject(drawfx::Fake &, const uint32_t *a)
{
    difftest::NotCounted bookkeeping; // the fakes list grows between the sides (Reset registers the fake)
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    // Lane OU: the two dynamic arrays CreateDeviceObjects makes (the same two fakes each time, counted).
    if (a[0] == 0xbbfffce5 || a[0] == 0x9c043807) {
        drawfx::Fake &array = a[0] == 0xbbfffce5 ? gVertexArray : gIndexArray;
        ++array.refs;
        *out = &array;
        return 1;
    }
    if (a[0] == 0xbb892198 && gNewTextureCount < kNewObjects) {
        drawfx::Fake &t = gNewTextures[gNewTextureCount];
        SetupTexture(t, 3000 + gNewTextureCount++, false);
        *out = &t;
        return 1;
    }
    if (a[0] == 0xc470d325 && gNewBufferCount < kNewObjects) {
        drawfx::Fake &b = gNewBuffers[gNewBufferCount];
        SetupBuffer(b, 5000 + gNewBufferCount++);
        *out = &b;
        return 1;
    }
    *out = nullptr;
    return 0;
}
uint32_t Constant1(drawfx::Fake &, const uint32_t *) { return 1; }
uint32_t DeviceEnumOf(drawfx::Fake &, const uint32_t *) { return drawfx::P(&gDeviceEnum); }

// ---- lane OU: the objects CreateDeviceObjects asks for and makes ----

// A made object says it was created in most draws of the script (the rest lets the next call make it again).
uint32_t MadeCreated() { return gScript() % 8 < gCase.madeOften; }
uint32_t StateOf(drawfx::Fake &, const uint32_t *) { return drawfx::P(&gState); }
uint32_t PropertiesOf(drawfx::Fake &, const uint32_t *) { return drawfx::P(&gProps); }
// The settings: the key's text is logged with the call; a found value is the boolean's byte (+4).
uint32_t PropertyValue(drawfx::Fake &, const uint32_t *a)
{
    const auto *const key = reinterpret_cast<const GZServiceString *>(uintptr_t(a[0]));
    drawfx::gLog.back().text += std::string(key->c_str()) + '\x1f';
    if (!gCase.forceFound) return 0;
    reinterpret_cast<gz::AnyBoolean *>(uintptr_t(a[1]))->mValue = gCase.forceSoftware;
    return 1;
}
uint32_t EndFormat(drawfx::Fake &, const uint32_t *a)
{
    ++gVertexFormat.refs;
    *reinterpret_cast<void **>(uintptr_t(a[0])) = &gVertexFormat;
    return 0;
}
// Where each element sits: the two layouts of the case.
uint32_t FormatOffset(drawfx::Fake &, const uint32_t *a)
{
    switch (a[0]) {
    case 0x5b830781: return gCase.layout ? 0xc : 0;
    case 0xdb830795: return gCase.layout ? 0 : 8;
    case 0xbb8307ab: return gCase.layout ? 4 : 0xc;
    }
    return ~0u;
}
// The shader compiler: the text is judged by what it does (tests/gz_draw2d_device_fx.h), not by its characters; its
// size must count the terminator (the digest, whether the size is the text's length and a terminator, and the flags are
// logged).
uint32_t AssembleProgram(drawfx::Fake &, const uint32_t *a)
{
    const char *const text = reinterpret_cast<const char *>(uintptr_t(a[1]));
    const uint32_t size = a[2];
    const std::vector<uint32_t> digest = vsdigest::Digest(text, std::strlen(text) + 1);
    if (!digest[0]) drawfx::gErrors.emplace_back("AssembleProgram: the shader text is not one the test understands");
    drawfx::Call &call = drawfx::gLog.back();
    call.words.push_back(size == std::strlen(text) + 1);
    call.words.insert(call.words.end(), digest.begin(), digest.end());
    gShaderInfo = {0, 1, uint8_t(a[5] & 4 ? 0xff : 1), uint8_t((a[5] & 4) != 0), uint8_t(gCase.hardwareShader)};
    gShader.value[0] = MadeCreated();
    ++gShader.refs;
    *reinterpret_cast<void **>(uintptr_t(a[3])) = &gShader;
    return 1;
}
uint32_t ShaderInfoOf(drawfx::Fake &, const uint32_t *) { return drawfx::P(&gShaderInfo); }
uint32_t DeclarationOf(drawfx::Fake &, const uint32_t *a)
{
    gDeclaration.value[0] = MadeCreated();
    ++gDeclaration.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gDeclaration;
    return 1;
}
uint32_t Created(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
// A state block recorded: a new fake each time (the caller's one reference).
uint32_t CreateBlock(drawfx::Fake &, const uint32_t *a)
{
    difftest::NotCounted bookkeeping; // the fakes list grows between the sides (Reset registers the fake)
    drawfx::Fake &b = gMadeBlocks[gMadeBlockCount < kMadeBlocks ? gMadeBlockCount : kMadeBlocks - 1];
    b.Reset<kResource>(6000 + gMadeBlockCount++);
    b.On(6, &Created);
    b.value[0] = MadeCreated();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &b;
    return 1;
}
// The dynamic arrays: the class objects of the COM registry (counted), created on the device with a kind.
uint32_t ArrayCreated(drawfx::Fake &self, const uint32_t *) { return self.value[2]; }
uint32_t ArrayKind(drawfx::Fake &self, const uint32_t *) { return self.value[3]; }
uint32_t ArrayCreate(drawfx::Fake &self, const uint32_t *a)
{
    self.value[2] = 1;
    self.value[3] = a[2];
    return 1;
}
uint32_t ArrayRelease(drawfx::Fake &self, const uint32_t *)
{
    self.value[2] = 0;
    return 0;
}

// The caches the draw system watches (the order they came in names them).
struct Watched {
    void *cache;
    bool alive;
};
std::vector<Watched> gWatched;
uint32_t AddWatched(drawfx::Fake &, const uint32_t *a)
{
    void *const cache = reinterpret_cast<void *>(uintptr_t(a[0]));
    drawfx::Note("watch", {uint32_t(gWatched.size())});
    {
        difftest::NotCounted bookkeeping; // the test's list, kept between the sides
        gWatched.push_back({cache, true});
    }
    return 0;
}
uint32_t RemoveWatched(drawfx::Fake &, const uint32_t *a)
{
    void *const cache = reinterpret_cast<void *>(uintptr_t(a[0]));
    uint32_t which = ~0u;
    for (size_t i = gWatched.size(); i-- > 0;)
        if (gWatched[i].cache == cache && gWatched[i].alive) {
            which = uint32_t(i);
            gWatched[i].alive = false;
            break;
        }
    drawfx::Note("unwatch", {which});
    return 0;
}
uint32_t DrawSystemTick(drawfx::Fake &self, const uint32_t *) { return self.value[1]; }

// ---- the recorders standing in for game code ----

drawfx::Fake *AsFake(const void *p)
{
    for (drawfx::Fake *f : drawfx::gFakes)
        if (p == f) return f;
    return nullptr;
}
// UploadRecord 0x152410 (ours since lane OZ; its own test is gz_draw2d_upload): the record it is asked for, as the object holds it.
void __fastcall RecUpload(void *self, void *, D::Record *record)
{
    D *const v = View(self);
    drawfx::Note("0x152410", {drawfx::Norm(drawfx::P(self)), uint32_t(record - v->mRecords.data()), record->width,
                              record->height, record->c, record->d, record->word1c, record->pool, record->columns,
                              record->rows, uint32_t(record->cells.size())});
}
// 0x18c990: the device's limits for a texture of the format.
void __cdecl RecAdjust(cIDeviceDX *device, void *format, int32_t *width, int32_t *height, uint32_t *levels, uint32_t last)
{
    drawfx::Note("0x18c990", {drawfx::Norm(drawfx::P(device)), drawfx::P(format), uint32_t(*width), uint32_t(*height),
                              *levels, last});
    const int32_t sizes[] = {64, 128, 256, 256, 512};
    *width = sizes[gScript() % 5];
    *height = sizes[gScript() % 5];
    *levels = 1 + gScript() % 3;
}
// The memory surface 0x18d1a0 / 0x18d2c0: three words, the second counted.
void *__fastcall RecSurface(void *self, void *, void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch)
{
    drawfx::Note("0x18d1a0", {drawfx::P(bits), drawfx::P(format), width, height, pitch});
    auto *const raw = static_cast<uint32_t *>(self);
    raw[0] = 0x5afa0001;
    raw[1] = 0;
    raw[2] = ++gSurfaceCount;
    return self;
}
void __fastcall RecSurfaceDestroy(void *self, void *)
{
    const auto *const raw = static_cast<const uint32_t *>(self);
    drawfx::Note("0x18d2c0", {raw[0], raw[1], raw[2]});
}
cIDraw2D *__cdecl RecDraw2D() { return static_cast<cIDraw2D *>(gBlock); }
void *__cdecl RecFormatsManager() { return &gFormats; }
void *__cdecl RecImageManip() { return &gImageManip; }
void *StandFramework() { return &gFramework; }
void *__cdecl RecPixelFormat(uint32_t format)
{
    drawfx::Note("0x18a7f0", format);
    return reinterpret_cast<void *>(uintptr_t(0x5a000000u + (format & 0xff) * 0x40));
}
// The current device 0x18ae40: there at the start or not; while the test itself asks for the device objects (the
// device came back) it is there.
bool gDeviceNow = false;
cIDeviceDX *__cdecl RecCurrentDevice()
{
    const bool there = gCase.deviceAtStart || gDeviceNow;
    drawfx::Note("0x18ae40", there);
    return there ? reinterpret_cast<cIDeviceDX *>(&gDevice) : nullptr;
}
// The cached service getters CreateDeviceObjects calls (0x18dcf0 the vertex formats manager, 0x18ddf0 the shader
// compiler, 0x18dd70 the declarations manager).
void *__cdecl RecVertexFormats() { return &gVertexFormats; }
void *__cdecl RecShaderCompiler() { return &gShaderManager; }
void *__cdecl RecDeclarations() { return &gDeclarations; }
// Makes the device objects on the side's object, as the device coming back does (CreateDeviceObjects 0x150330: the
// original's, or ours).
void MakeDeviceObjects(bool ours, void *block)
{
    const bool before = gDeviceNow;
    gDeviceNow = true;
    if (ours)
        View(block)->CreateDeviceObjects();
    else
        reinterpret_cast<void(__thiscall *)(void *)>(GameAddress<void>(0x150330))(block);
    gDeviceNow = before;
}
nGZEvent::cEventAttributeMap *__cdecl RecRegistry(uint32_t a, const nGZEvent::cEventAttribute *defaults)
{
    drawfx::Note("0x1516f0", {a, defaults->mId, drawfx::P(defaults->mName), defaults->mWord08, defaults->mWord0C,
                              defaults->mWord10, defaults->mWord14});
    return &gRegistry;
}

struct Patches {
    shape::StandIn upload{0x152410, &D::UploadRecord, &RecUpload};
    // AdjustTextureSize is ours (lane VG, src/sg/TextureDescription.h; tests/sg_texture_description.cpp).
    shape::StandIn adjust{0x18c990, &nGZSceneGraph::AdjustTextureSize, &RecAdjust};
    // cMemBlockAsSurface is ours (lane RN, src/gz/cMemBlock.h; tests/gz_memblock.cpp): stood in on both sides.
    shape::StandIn surface{0x18d1a0, &nRZGraphic4::cMemBlockAsSurface::Construct, &RecSurface};
    shape::StandIn surfaceDestroy{0x18d2c0, &nRZGraphic4::cMemBlockAsSurface::Destruct, &RecSurfaceDestroy};
    shape::StandIn draw2d{0x18c000, &nGZGraphic4::GetDraw2DCached, &RecDraw2D};
    shape::StandIn formats{0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, &RecFormatsManager};
    shape::StandIn imageManip{0x18d120, &nGZGraphic4::GetImageManipCached, &RecImageManip};
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn pixelFormat{0x18a7f0, &nGZGraphic4::GetPixelFormatForColorType, &RecPixelFormat};
    shape::StandIn vertexFormats{0x18dcf0, &nGZGraphic4::GetVertexFormatsManagerCached, &RecVertexFormats};
    shape::StandIn shaderCompiler{0x18ddf0, &nGZGraphic4::GetShaderProgramsManagerCached, &RecShaderCompiler};
    shape::StandIn declarations{0x18dd70, &nGZGraphic4::GetVertexDeclarationsManagerCached, &RecDeclarations};
    shape::StandIn currentDevice{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &RecCurrentDevice};
    shape::StandIn registry{0x1516f0, &nGZGraphic4::cDraw2DBehaviorTraits::RegistryFor, &RecRegistry};
};

// ---- building the world ----

void NewCase(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 13);
    for (unsigned i = 0; i < kTargets; ++i) {
        TargetSpec &t = gCase.targets[i];
        t.width = 1 + rng() % 500;
        t.height = 1 + rng() % 400;
        const int32_t left = rng() % 3 ? 0 : int32_t(rng() % 5), top = rng() % 3 ? 0 : int32_t(rng() % 5);
        t.bounds = {left, top, left + int32_t(t.width), top + int32_t(t.height)};
        t.bufferFace = i != 1 || rng() % 5 != 0;
    }
    gCase.deviceWidth = 64 + rng() % 1600;
    gCase.deviceHeight = 48 + rng() % 1000;
    gCase.deviceAtStart = rng() % 4 != 0;
    gCase.readyAtStart = rng() % 2 != 0;
    gCase.layout = rng() % 2;
    gCase.supports16 = rng() % 5 != 0;
    gCase.supports32 = rng() % 5 != 0;
    for (uint32_t &f : gCase.format) f = rng() % 10;
    gCase.hasKey = rng() % 2;
    gCase.writesAlpha = rng() % 2;
    gCase.key = rng();
    gCase.forceSoftware = rng() % 3 == 0;
    gCase.forceFound = rng() % 4 != 0;
    gCase.hardwareShader = rng() % 3 != 0;
    gCase.madeOften = 5 + rng() % 4;
}

void BuildWorld()
{
    uint32_t id = 900;
    gFramework.Reset<kFramework>(id++);
    gFramework.On(5, &ServiceQuery);
    gFramework.Returns(9, 1u);
    gFramework.On(3, &GetClassObject);
    gNewTextureCount = gNewBufferCount = gSurfaceCount = 0;
    gWatched.clear();
    gDeviceEnum.Reset<kDeviceEnum>(id++);
    gDeviceEnum.On(11, &FormatSupported);
    gFormats.Reset<kFormats>(id++);
    gFormats.On(6, &FormatARGB);
    gImageManip.Reset<kImageManip>(id++);
    gImageManip.On(3, &CopySurface);
    gGraphics.Reset<kGraphics>(id++);
    gGraphics.On(8, &GraphicsConnect);
    gDevice.Reset<kDevice>(id++);
    gDevice.On(41, &TargetSize);
    gDevice.Returns(35, uint32_t(gCase.readyAtStart));
    gDevice.On(16, &DeviceEnumOf);
    gState.Reset<kState>(id++);
    gState.On(12, &StateCommand);
    gState.On(19, &LockConstants);
    gState.On(30, &ChangeCount);
    for (drawfx::Fake *a : {&gVertexArray, &gIndexArray}) {
        a->Reset<kArray>(id++);
        a->value[7] = a == &gIndexArray;
        a->On(15, &ArrayLock);
        a->On(16, &ArrayUnlock);
        a->On(18, &Value0);
        a->On(20, &ArrayReserve);
        a->On(22, &Value1);
    }
    gDrawSystem.Reset<kDrawSystem>(id++);
    gDrawSystem.On(20, &Value0);
    gDrawSystem.On(11, &AddWatched);
    gDrawSystem.On(12, &RemoveWatched);
    gDrawSystem.On(15, &DrawSystemTick);
    for (drawfx::Fake &b : gBlocks) b.Reset<kUnknown>(id++);
    // Lane OU: what CreateDeviceObjects asks for.
    gMadeBlockCount = 0;
    gProps.Reset<kProps>(id++);
    gProps.On(9, &PropertyValue);
    gVertexFormats.Reset<kVertexFormats>(id++);
    gVertexFormats.On(5, &EndFormat);
    gVertexFormat.Reset<kVertexFormatObject>(id++);
    gVertexFormat.On(15, &FormatOffset);
    gShaderManager.Reset<kShaderManager>(id++);
    gShaderManager.On(3, &AssembleProgram);
    gShader.Reset<kShaderObject>(id++);
    gShader.On(6, &Created);
    gShader.On(10, &ShaderInfoOf);
    gDeclarations.Reset<kDeclarations>(id++);
    gDeclarations.On(3, &DeclarationOf);
    gDeclaration.Reset<kResource>(id++);
    gDeclaration.On(6, &Created);
    gDevice.On(20, &StateOf);
    gDeviceEnum.On(22, &PropertiesOf);
    gState.On(10, &CreateBlock);
    for (drawfx::Fake *a : {&gVertexArray, &gIndexArray}) {
        a->On(3, &ArrayRelease);
        a->On(6, &ArrayCreated);
        a->On(17, &ArrayKind);
        a->On(23, &ArrayCreate);
    }
    for (unsigned i = 0; i < 2; ++i) {
        gMaterials[i].Reset<kMaterial>(id++);
        gMaterials[i].On(3, &Value0);
        gMaterials[i].On(4, &MaterialDraw);
        gMaterials[i].value[0] = i ? drawfx::P(&gBlocks[6 + (gCase.layout & 1)]) : 0;
    }
    for (unsigned i = 0; i < kTargets; ++i) {
        drawfx::Fake &f = gTargets[i];
        SetupBuffer(f, id++);
        f.value[0] = i;
        f.value[1] = gCase.targets[i].width;
        f.value[2] = gCase.targets[i].height;
        f.value[3] = gCase.format[i];
        f.value[5] = gCase.key;
        f.value[6] = gCase.hasKey;
        f.value[7] = gCase.writesAlpha;
        if (gCase.targets[i].bufferFace) f.AddFace(cIGZBuffer::kIid, f);
    }
    gStranger.Reset<kUnknown>(id++);
    for (drawfx::Fake &f : gRecordBuffers) SetupBuffer(f, id++);
    for (auto &pool : gTextures)
        for (drawfx::Fake &t : pool) SetupTexture(t, id++, false);
    gRaw.Reset<kRaw>(id++);
    gRaw.On(4, &Value0);
    gBackbuffer.Reset<kUnknown>(id++);
    gBackbuffer.AddFace(cBackbufferAsGZBufferRaw::kIid, gRaw);
    for (unsigned i = 0; i < kListeners; ++i) {
        gListeners[i].Reset<kListener>(id++);
        gListeners[i].AddFace(nGZEvent::cIAbstractBehaviorType::kIid, gListeners[i]);
        gListeners[i].On(4, &Value0);
    }
    drawfx::AddRange(gArrayBuffer[0], sizeof(gArrayBuffer[0]), kVertexTag);
    drawfx::AddRange(gArrayBuffer[1], sizeof(gArrayBuffer[1]), kIndexTag);
    drawfx::AddRange(gConstants, sizeof(gConstants), kConstantsTag);
    drawfx::AddRange(gEventData, sizeof(gEventData), kDataTag);
    for (uint32_t &w : gConstants) w = 0xcdcdcdcd;
}

void BuildRegistry()
{
    if (!gRegistry.empty()) return;
    for (unsigned i = 0; i < 3; ++i) {
        std::snprintf(gNames[i], sizeof(gNames[i]), "e%u", i);
        gRegistry[kRegistryIds[i]] = {kRegistryIds[i], gNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
    }
}

// The scope of a side's cells and event manager.
class SideWorld {
public:
    SideWorld()
    {
        cells_.Set(kFrameworkCell, &gFramework);
        cells_.Set(D::kDrawSystemCell, &gDrawSystem);
        cells_.Set(kDesignHeight, Bits(768.0f));
        gManager = cConnectionManagerAdapters::Construct(::operator new(cConnectionManager::kSize), nullptr);
        {
            difftest::NotCounted bookkeeping;
            drawfx::AddRange(gManager, cConnectionManager::kSize, kManagerTag);
        }
        cells_.Set(cDraw2DBehaviorTraits::kEventManagerCell, gManager);
    }
    ~SideWorld()
    {
        cConnectionManagerAdapters::Destruct(gManager, nullptr);
        ::operator delete(gManager);
        gManager = nullptr;
    }
    SideWorld(const SideWorld &) = delete;
    SideWorld &operator=(const SideWorld &) = delete;

private:
    drawfx::Cells cells_;
};

// ---- logging the object ----

template <class T> void PushVector(drawfx::Call &c, const std::vector<T> &v, bool norm)
{
    c.words.push_back(uint32_t(v.size()));
    c.words.push_back(uint32_t(v.capacity()));
    for (const T &e : v) {
        uint32_t w[sizeof(T) / 4];
        std::memcpy(w, &e, sizeof(T));
        for (uint32_t x : w) c.words.push_back(norm ? drawfx::Ptr(x) : x);
    }
}
template <class T> const std::vector<T> &VectorAt(const void *block, uint32_t offset)
{
    return *reinterpret_cast<const std::vector<T> *>(static_cast<const uint8_t *>(block) + offset);
}

// Whether the object's word at `off` (below 0x154) is a pointer: the engine's target, texture and texture source (the
// render-to-hardware base at +8: its +8, +0x24, +0x28) and the counted device objects and material (cDraw2D.h). The
// rest is data, logged as it is: the block starts as random bytes, and a data word guessed by value could lie inside a
// block registered on one side only (lane RK).
bool PointerWord(uint32_t off)
{
    return off == 0x10 || off == 0x2c || off == 0x30 || (off >= 0x104 && off < 0x124) || off == 0x130 ||
           (off >= 0x140 && off < 0x154);
}

// Every word but the three vtables; the vectors (the engine's four, the pools with their atlases, the records with
// their cells, the free list, the clips) as their counts, capacities and elements.
void NoteObject(void *block)
{
    drawfx::Call c;
    c.note = "object";
    for (uint32_t off = 4; off < 0x154; off += 4) {
        if (off == 8 || off == 0x100 || (off >= 0xbc && off < 0xec)) continue;
        c.words.push_back(PointerWord(off) ? drawfx::Ptr(Word(block, off)) : Word(block, off));
    }
    PushVector(c, VectorAt<Hw::Vertex>(block, 0xbc), false);
    PushVector(c, VectorAt<Hw::Tile>(block, 0xc8), false);
    PushVector(c, VectorAt<Hw::TileRect>(block, 0xd4), false);
    PushVector(c, VectorAt<Hw::TexelRect>(block, 0xe0), false);
    const D *const v = View(block);
    for (const D::Pool &pool : v->mPools) {
        c.words.push_back(uint32_t(pool.atlases.size()));
        c.words.push_back(uint32_t(pool.atlases.capacity()));
        for (const cTextureAtlas &atlas : pool.atlases) {
            c.words.push_back(atlas.mRegions);
            c.words.push_back(uint32_t(atlas.mFreeArea));
            // The nodes: the rectangle, the tree links and the used flag (the three bytes after it are not written).
            c.words.push_back(uint32_t(atlas.mNodes.size()));
            c.words.push_back(uint32_t(atlas.mNodes.capacity()));
            for (const cAtlasNode &n : atlas.mNodes)
                c.words.insert(c.words.end(), {uint32_t(n.rect.left), uint32_t(n.rect.top), uint32_t(n.rect.right),
                                               uint32_t(n.rect.bottom), uint32_t(n.parent), uint32_t(n.first),
                                               uint32_t(n.second), uint32_t(n.used)});
            PushVector(c, atlas.mFreeNodes, false);
        }
        PushVector(c, pool.textures.items, true);
    }
    c.words.push_back(uint32_t(v->mRecords.size()));
    c.words.push_back(uint32_t(v->mRecords.capacity()));
    for (const D::Record &r : v->mRecords) {
        // The flag at +0x14 is one byte: the original's writes leave the three after it as the record's memory had them.
        // The buffer (+0x18) is the record's one pointer; the key at +0x1c is data.
        for (uint32_t off = 0; off < 0x2c; off += 4)
            c.words.push_back(off == 0x14   ? Word(&r, off) & 0xff
                              : off == 0x18 ? drawfx::Ptr(Word(&r, off))
                                            : Word(&r, off));
        PushVector(c, r.cells, false);
        // The borders are set when the record is placed (a fresh original record has leftovers there).
        c.words.push_back(r.pool != D::kUnplaced ? Word(&r, 0x38) & 0xffff : 0);
    }
    PushVector(c, v->mFreeRecords, false);
    PushVector(c, v->mClips, false);
    c.words.push_back(Word(block, 0x1c0));
    drawfx::gLog.push_back(std::move(c));
    drawfx::Call k;
    k.note = "constants";
    k.words.assign(gConstants, gConstants + 8);
    drawfx::gLog.push_back(std::move(k));
    for (uint32_t &w : gConstants) w = 0xcdcdcdcd;
}

void NoteRefs()
{
    drawfx::Call c;
    c.note = "refs";
    for (const drawfx::Fake *f : drawfx::gFakes) c.words.insert(c.words.end(), {f->id, uint32_t(f->refs)});
    drawfx::gLog.push_back(std::move(c));
}

// ---- making the object ----

void *Block(uint32_t seed)
{
    uint8_t *const block = static_cast<uint8_t *>(::operator new(D::kSize));
    std::mt19937 rng(seed * 31u + 7);
    for (uint32_t i = 0; i < D::kSize; ++i) block[i] = uint8_t(rng());
    {
        difftest::NotCounted bookkeeping;
        drawfx::AddRange(block, D::kSize, kObjectTag);
    }
    return block;
}

// The records, pools and clips a case starts with, built alike on each side (the elements' vectors are ours).
void Populate(void *block, std::mt19937 &rng)
{
    D *const v = View(block);
    gDrawSystem.value[1] = rng();
    for (unsigned p = 0; p < 3; ++p) {
        const unsigned atlases = 1 + rng() % kMaxAtlases;
        std::vector<cTextureAtlas> list(atlases);
        std::vector<cIGZUnknown *> textures(atlases);
        for (unsigned a = 0; a < atlases; ++a) {
            const int32_t side = 64 << (rng() % 3);
            list[a].Init(side, side);
            // GetTextureTiles reads the whole texture's size as right - left by bottom - top, so one atlas in four is
            // seeded off the origin to tell those from right and bottom alone.
            if (rng() % 4 == 0) {
                list[a].mNodes[0].rect.left = int32_t(rng() % 5) + 1;
                list[a].mNodes[0].rect.top = int32_t(rng() % 5) + 1;
            }
            if (rng() % 6 == 0) continue;
            textures[a] = reinterpret_cast<cIGZUnknown *>(&gTextures[p][a]);
            ++gTextures[p][a].refs;
            gTextures[p][a].value[0] = rng() % 6 != 0; // created
        }
        v->mPools[p].atlases = list;
        v->mPools[p].textures.items = textures;
    }
    std::vector<D::Record> records(rng() % (kRecordBuffers + 1));
    std::vector<int32_t> free; // records nobody holds: the next ones to be made
    for (size_t i = 0; i < records.size(); ++i) {
        D::Record &r = records[i];
        if (rng() % 5 == 0) {
            free.push_back(int32_t(i));
            continue;
        }
        r.refCount = 1 + int32_t(rng() % 3);
        r.c = rng() % 4;
        r.d = rng() % 4;
        r.bufferUncounted = rng() % 4 == 0;
        r.buffer = reinterpret_cast<cIGZBuffer *>(&gRecordBuffers[i]);
        gRecordBuffers[i].value[1] = r.width = 1 + rng() % 300;
        gRecordBuffers[i].value[2] = r.height = 1 + rng() % 200;
        gRecordBuffers[i].value[3] = rng() % 10;
        if (!r.bufferUncounted) ++gRecordBuffers[i].refs;
        r.word1c = rng();
        if (rng() % 6 == 0) continue; // not placed (pool 3)
        // Cells from the pool's atlases (an atlas with a region has a texture), each a real region of its tree.
        const unsigned pool = rng() % 3;
        const uint32_t columns = 1 + rng() % 3, rows = 1 + rng() % 2;
        std::vector<D::CellRef> cells;
        D::Pool &place = v->mPools[pool];
        bool failed = false;
        for (uint32_t k = 0; k < columns * rows && !failed; ++k) {
            const int32_t w = 8 + int32_t(rng() % 40), h = 8 + int32_t(rng() % 40);
            failed = true;
            for (size_t a = 0; a < place.atlases.size() && failed; ++a) {
                if (!place.textures.items[a]) {
                    place.textures.items[a] = reinterpret_cast<cIGZUnknown *>(&gTextures[pool][a]);
                    ++gTextures[pool][a].refs;
                    gTextures[pool][a].value[0] = 1;
                }
                const int32_t region = place.atlases[a].Allocate(w, h);
                if (region == -1) continue;
                cells.push_back({uint32_t(a), uint32_t(region)});
                failed = false;
            }
        }
        if (failed) {
            for (const D::CellRef &cell : cells) place.atlases[cell.atlas].Free(int32_t(cell.region));
            continue;
        }
        r.pool = pool;
        r.columns = columns;
        r.rows = rows;
        r.borderX = uint8_t(rng() % 2);
        r.borderY = uint8_t(rng() % 2);
        r.cells = cells;
    }
    v->mRecords = records;
    v->mFreeRecords = free;
    std::vector<cDraw2DClip> clips(1 + rng() % 3);
    for (cDraw2DClip &clip : clips) {
        clip.originX = int32_t(rng() % 40) - 10;
        clip.originY = int32_t(rng() % 40) - 10;
        const int32_t l = int32_t(rng() % 200) - 20, t = int32_t(rng() % 200) - 20;
        clip.rect = {l, t, l + int32_t(rng() % 900) - 20, t + int32_t(rng() % 700) - 20};
        clip.depth = float(rng() % 100) / 100.0f;
    }
    v->mClips = clips;
    const cGZRect &top = clips.back().rect;
    v->mClipEmpty = rng() % 4 ? !(top.right > top.left && top.bottom > top.top) : rng() % 2 != 0;
    v->mNeedsRebuild = rng() % 2 != 0;
    v->mChangeCount = rng() % 3;
    v->mDrawSystemFlag = rng() % 2 != 0;
    if (rng() % 3) {
        v->mMaterial.p = reinterpret_cast<cIDraw2DMaterial *>(&gMaterials[rng() % 2]);
        ++static_cast<drawfx::Fake *>(static_cast<void *>(v->mMaterial.p))->refs;
    }
}

void *OriginalCreate(uint32_t seed)
{
    void *const block = Block(seed);
    using Ctor = void *(__thiscall *)(void *);
    reinterpret_cast<Ctor>(GameAddress<void>(kConstructor))(block);
    return block;
}
void *OurCreate(uint32_t seed)
{
    void *const block = Block(seed);
    D *const made = ::new (block) D;
    return static_cast<cIDraw2D *>(made);
}

cGZRect RandomRect(std::mt19937 &rng, int32_t w, int32_t h)
{
    const int32_t l = int32_t(rng() % uint32_t(w + 40)) - 20, t = int32_t(rng() % uint32_t(h + 40)) - 20;
    cGZRect r{l, t, l + int32_t(rng() % uint32_t(w + 1)) - 5, t + int32_t(rng() % uint32_t(h + 1)) - 5};
    if (rng() % 8 == 0) r.right = r.left;
    return r;
}
float Unit(std::mt19937 &rng) { return float(rng() % 1001) / 1000.0f; }
int32_t Coord(std::mt19937 &rng) { return int32_t(rng() % 700) - 50; }

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

// The colour classes CreateRecord is given: alpha only (0), under 24 bits (8, 0x10), 24 and 32 (0x18, 0x20) and the
// borders around them (the pools choose by < 0x18).
constexpr uint32_t kRecordClasses[8] = {0, 8, 0x10, 0x17, 0x18, 0x19, 0x20, 1};

// A record index for the slots that take one: -1 or a record (in range: the original indexes unchecked).
int32_t PickRecord(std::mt19937 &rng, const D *v)
{
    if (v->mRecords.empty() || rng() % 6 == 0) return -1;
    return int32_t(rng() % v->mRecords.size());
}

// ---- the engine face, as the draw context drives it ----

void EngineStep(std::mt19937 &rng, cIGZDrawContextEngine *e, int &target)
{
    const TargetSpec *const t = target >= 0 ? &gCase.targets[target] : nullptr;
    uint32_t pick = rng() % 20;
    if (target < 0 && rng() % 2) pick = 0;
    drawfx::Note("engine", pick);
    switch (pick) {
    case 0: {
        const unsigned which = rng() % 2;
        const uint32_t ok = e->SetTarget(reinterpret_cast<cIGZBuffer *>(&gTargets[which]));
        drawfx::Note("settarget", ok);
        target = ok ? int(which) : -1;
        break;
    }
    case 1: e->Detach(); break;
    case 2: {
        if (!t) break;
        cGZRect r{-7, -7, -7, -7};
        cGZRect *const back = e->GetBounds(&r);
        drawfx::Note("bounds", {uint32_t(back == &r), uint32_t(r.left), uint32_t(r.top), uint32_t(r.right), uint32_t(r.bottom)});
        break;
    }
    case 3: {
        if (!t) break;
        const int32_t w = int32_t(t->width), h = int32_t(t->height);
        cGZRect r{int32_t(rng() % 20) - 5, int32_t(rng() % 20) - 5, w - int32_t(rng() % 20) + 5, h - int32_t(rng() % 20) + 5};
        if (rng() % 8 == 0) r.right = r.left;
        e->SetClip(&r);
        break;
    }
    case 4: {
        uint32_t colours[4];
        for (uint32_t &c : colours) c = rng();
        const uint32_t first = rng() % 4, count = rng() % (5 - first);
        e->SetColors(first, count, colours);
        break;
    }
    case 5: {
        const uint32_t values[] = {0, 1, 2, 3, 4, 0xffffffffu};
        e->vf8(values[rng() % 6]);
        break;
    }
    case 6: e->vf9(uint8_t(rng() % 3 ? rng() % 2 : rng())); break;
    case 7: e->vf10(uint8_t(rng() % 3 ? rng() % 2 : rng())); break;
    case 8: e->vf11(uint8_t(rng() % 2)); break;
    case 9: {
        if (rng() % 4 == 0) {
            e->SetGradient(nullptr, 0.0f, 0.0f);
            break;
        }
        float c[12];
        for (int i = 0; i < 4; ++i) c[i] = Unit(rng);
        for (int i = 4; i < 12; ++i) c[i] = (Unit(rng) - 0.5f) * 0.05f;
        e->SetGradient(c, float(int(rng() % 60) - 10), float(int(rng() % 40) - 10));
        break;
    }
    case 10: case 11: {
        const uint32_t kind = rng() % 6;
        cIGZUnknown *const texture = kind < 4    ? reinterpret_cast<cIGZUnknown *>(&gTargets[2])
                                     : kind == 4 ? reinterpret_cast<cIGZUnknown *>(&gStranger)
                                                 : nullptr;
        if (texture && !t) break; // the common part asks the target's bounds
        const float scales[] = {1.0f, 1.0f, 1.0f, 2.0f, 0.5f, 1.5f};
        const float sx = scales[rng() % 6], sy = rng() % 3 ? sx : scales[rng() % 6];
        const float x = float(int(rng() % 60) - 10), y = float(int(rng() % 40) - 8) + (rng() % 3 ? 0.0f : Unit(rng));
        cGZRect r{-9, -9, -9, -9};
        const uint32_t ok = e->SetTexture(texture, x, y, sx, sy, &r);
        drawfx::Note("texture", {ok, uint32_t(r.left), uint32_t(r.top), uint32_t(r.right), uint32_t(r.bottom)});
        break;
    }
    case 12: e->ClearTextureClip(); break;
    case 13: {
        const TargetSpec &x = gCase.targets[2];
        const int32_t l = int32_t(rng() % x.width), tp = int32_t(rng() % x.height);
        cGZRect r{l, tp, l + 1 + int32_t(rng() % (x.width - l)), tp + 1 + int32_t(rng() % (x.height - tp))};
        e->SetTextureClip(&r);
        break;
    }
    case 14: e->vf17(); break;
    case 15: e->vf18(); break;
    case 16: e->vf19(); break;
    case 17: case 18: {
        if (!t) break;
        const uint32_t kinds[] = {6, 6, 3, 3, 1};
        const uint32_t kind = kinds[rng() % 5];
        const uint32_t count = kind == 3 ? 2 * (1 + rng() % 4) : 1 + rng() % 9;
        std::vector<float> points(2 * size_t(count) + 2);
        const float w = float((std::min)(t->width, 400u)), h = float((std::min)(t->height, 300u));
        for (uint32_t i = 0; i < count; ++i) {
            points[2 * i] = float(int(rng() % uint32_t(w + 20)) - 10) + Unit(rng);
            points[2 * i + 1] = float(int(rng() % uint32_t(h + 20)) - 10) + Unit(rng);
        }
        drawfx::Note("route", e->ChooseRoute(kind, points.data(), count));
        break;
    }
    default: e->DrawSpans(nullptr, 0); break;
    }
}

// ---- the 0x20-byte pool the hardware caches come from (0xfeb8e0, lane OL) ----

constexpr uint32_t kCachePool = cDraw2DBufferHardwareCache::kPoolRva;
constexpr uint32_t kPoolPage = 0x2000, kPoolBlocks = (kPoolPage - 16) / 0x20, kPoolBusy = 60;
uint8_t *gPoolPage;

// One page of seeded bytes as the pool's current page, kPoolBusy blocks in use and the rest free in a seeded order: the
// used count stays far from empty and from full, so neither side reaches the pool's page bookkeeping (TakeFreePage,
// ReleaseEmptyPage: game code not under test). The same bytes on each side.
void BuildCachePool(uint32_t seed)
{
    if (!gPoolPage)
        gPoolPage = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPoolPage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::mt19937 r(seed * 0x9e3779b1u + 17);
    for (uint32_t i = 0; i < kPoolPage; ++i) gPoolPage[i] = uint8_t(r());
    std::vector<uint32_t> order;
    for (uint32_t i = kPoolBusy; i < kPoolBlocks; ++i) order.push_back(i);
    std::shuffle(order.begin(), order.end(), r);
    uint8_t *first = nullptr;
    for (uint32_t i : order) {
        uint8_t *const block = gPoolPage + 16 + i * 0x20;
        *reinterpret_cast<uint8_t **>(block) = first;
        first = block;
    }
    auto *const page = reinterpret_cast<RZFixedPage *>(gPoolPage);
    page->freeList = first;
    page->used = kPoolBusy;
    page->next = page->prev = page;
    *GameAddress<uint32_t>(kCachePool + 4) = drawfx::P(gPoolPage);
    {
        difftest::NotCounted bookkeeping;
        drawfx::AddRange(gPoolPage, kPoolPage, 0x07000000);
    }
}

void NoteCachePool()
{
    const auto *const page = reinterpret_cast<const RZFixedPage *>(gPoolPage);
    drawfx::Note("cache pool", {drawfx::Norm(drawfx::P(page->freeList)), page->used});
}

// ---- one side ----

// The caches the draw system watches, as each holds its words (the vtable and the pad bytes left out).
void NoteCaches()
{
    for (size_t i = 0; i < gWatched.size(); ++i) {
        if (!gWatched[i].alive) continue;
        const void *const c = gWatched[i].cache;
        drawfx::Note("cache", {uint32_t(i), Word(c, 4), drawfx::Norm(Word(c, 8)), drawfx::Norm(Word(c, 0xc)), Word(c, 0x10),
                               Word(c, 0x14), Word(c, 0x18), uint32_t(static_cast<const uint8_t *>(c)[0x1c])});
    }
}

// One call on one of the caches the draw system watches, through its vtable (the original's or ours: the same layout).
void CacheStep(std::mt19937 &rng)
{
    std::vector<size_t> live;
    for (size_t i = 0; i < gWatched.size(); ++i)
        if (gWatched[i].alive) live.push_back(i);
    if (live.empty()) return;
    auto *const c = static_cast<cDraw2DBufferHardwareCache *>(gWatched[live[rng() % live.size()]].cache);
    const uint32_t op = rng() % 12;
    drawfx::Note("cache op", op);
    switch (op) {
    case 0: c->Unload(); break;
    case 1: c->Invalidate(); break;
    case 2: drawfx::Note("load", c->Load(rng())); break;
    case 3: drawfx::Note("stale", c->IsStale()); break;
    case 4: drawfx::Note("loaded", c->IsLoaded()); break;
    case 5: drawfx::Note("buffer", drawfx::Norm(drawfx::P(c->GetBuffer()))); break;
    case 6: drawfx::Note("last tick", c->GetLastUseTick()); break;
    case 7:
        drawfx::Note("addref", c->AddRef());
        drawfx::Note("release", c->Release());
        break;
    case 8: {
        const uint32_t ids[] = {1, cDraw2DBufferHardwareCache::kIidHardwareCache, cDraw2DBufferHardwareCache::kIidOther, 0x1234};
        void *out = nullptr;
        const bool ok = c->QueryInterface(ids[rng() % 4], &out);
        drawfx::Note("qi", {ok, out == c});
        if (ok) drawfx::Note("release", c->Release());
        break;
    }
    case 9: case 10: drawfx::Note("load for tick", c->LoadForTick(rng())); break;
    default: {
        // Detach: the buffer lets the cache go afterwards (nothing else may use it).
        drawfx::Fake *const buffer = AsFake(c->GetBuffer());
        c->Detach();
        if (buffer && buffer->owner == c) {
            buffer->owner = nullptr;
            c->Release();
        }
        break;
    }
    }
}

void RunSide(bool ours, uint32_t seed)
{
    SideWorld world;
    drawfx::gLog.reserve(1 << 15);
    gScript.seed(seed ^ 0x5eed);
    std::mt19937 rng(seed);
    difftest::AllocationCounter heap;

    void *const block = ours ? OurCreate(seed) : OriginalCreate(seed);
    gBlock = block;
    gDeviceNow = true; // the constructor asked at the start; from here on a device is there when the object asks
    NoteObject(block);
    auto *const d = static_cast<cIDraw2D *>(block);
    auto *const e = reinterpret_cast<cIGZDrawContextEngine *>(static_cast<uint8_t *>(block) + 8);
    auto *const factory = reinterpret_cast<cIGZDrawContextEngineFactory *>(static_cast<uint8_t *>(block) + 0x100);
    D *const v = View(block);
    drawfx::Note("addref", d->AddRef());
    if (!v->mDevice.p) MakeDeviceObjects(ours, block);
    Populate(block, rng);
    gState.value[0] = rng() % 3;
    for (unsigned i = 0; i < kListeners; ++i)
        if (rng() % 2)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(block), kListenEvents[i], reinterpret_cast<cIGZUnknown *>(&gListeners[i]),
                kHeardAs[i]);
    NoteObject(block);

    int target = -1;
    for (int step = 0; step < 90; ++step) {
        if (!v->mDevice.p) {
            // The device went (OnEvent code 3): CreateDeviceObjects gives it back, as the device's return would.
            drawfx::Note("reinstall", 0);
            MakeDeviceObjects(ours, block);
        }
        uint32_t pick = rng() % 50;
        // The shapes with no material: the original calls the null material under __try / __except(1) (0x14f04f ..
        // 0x14f076 and the others), and in the harness that fault is taken by no handler (the process dies: the game's
        // SEH handler does not run in the uninitialised image). Ours swallows it the same way (DrawGuarded); the shapes
        // run with a material.
        if (pick >= 6 && pick <= 9 && !v->mMaterial.p) pick = 0;
        drawfx::Note("pick", pick);
        const int32_t w = int32_t(gCase.deviceWidth), h = int32_t(gCase.deviceHeight);
        switch (pick) {
        case 0: {
            const uint32_t which = rng() % 4;
            d->SetMaterial(which < 2 ? reinterpret_cast<cIDraw2DMaterial *>(&gMaterials[which])
                           : which == 2 ? v->mMaterial.p
                                        : nullptr);
            break;
        }
        case 1: {
            const cGZRect r = RandomRect(rng, w, h);
            d->PushClipRect(&r, rng() % 2 != 0, Unit(rng));
            break;
        }
        case 2: {
            // An origin far up or left (one in four each) brings the unlimited rectangle's far edge (99999) inside the
            // target, where its exact value shows.
            const int32_t x = rng() % 4 == 0 ? int32_t(rng() % 400) - 99999 : Coord(rng);
            const int32_t y = rng() % 4 == 0 ? int32_t(rng() % 400) - 99999 : Coord(rng);
            const bool relative = rng() % 2 != 0;
            d->PushClipOrigin(x, y, relative, Unit(rng));
            break;
        }
        case 3: {
            const cGZRect r = RandomRect(rng, w, h);
            d->PushClip(Coord(rng), Coord(rng), &r, rng() % 2 != 0, Unit(rng));
            break;
        }
        case 4: case 5:
            // The stack is popped unchecked (0x151180 reads end[-1] after the pop): keep one clip.
            if (v->mClips.size() >= 2) d->PopClip();
            break;
        case 6: {
            const int32_t x0 = Coord(rng), y0 = Coord(rng);
            const bool same = rng() % 12 == 0;
            const float widths[] = {1.0f, 2.5f, 0.5f, 0.0f, 7.0f};
            d->DrawLine(x0, y0, same ? x0 : Coord(rng), same ? y0 : Coord(rng), widths[rng() % 5]);
            break;
        }
        case 7: d->FillRect(Coord(rng), Coord(rng), int32_t(rng() % 300) - 20, int32_t(rng() % 300) - 20); break;
        case 8: d->FillTriangle(Coord(rng), Coord(rng), Coord(rng), Coord(rng), Coord(rng), Coord(rng)); break;
        case 9:
            d->FillQuad(Coord(rng), Coord(rng), Coord(rng), Coord(rng), Coord(rng), Coord(rng), Coord(rng), Coord(rng));
            break;
        case 10: {
            const float alphas[] = {0.0f, 0.5f, 1.0f, 1.5f, -0.2f, 255.0f, 1e10f, -1e10f, 0.999f, 0.0019f};
            const float alpha = rng() % 3 ? alphas[rng() % 10] : (Unit(rng) - 0.25f) * 1.5f;
            d->SetBlendMode(rng() % 4, alpha);
            break;
        }
        case 11: {
            const cGZRect a = RandomRect(rng, w, h), b = RandomRect(rng, 200, 200);
            d->DrawImage(&a, PickRecord(rng, v), &b);
            break;
        }
        case 12: {
            const cGZRect a = RandomRect(rng, w, h);
            d->DrawRecord(&a, PickRecord(rng, v));
            break;
        }
        case 13: {
            const cGZRect b = RandomRect(rng, 200, 200);
            d->DrawImageAt(Coord(rng), Coord(rng), PickRecord(rng, v), &b);
            break;
        }
        case 14: d->DrawRecordAt(Coord(rng), Coord(rng), PickRecord(rng, v)); break;
        case 15: drawfx::Note("record", uint32_t(d->CreateRecord(rng() % 300, rng() % 300, kRecordClasses[rng() % 8], rng() % 4))); break;
        case 16: {
            const uint32_t g = rng();
            const int32_t r = PickRecord(rng, v);
            if (r >= 0 && !v->mRecords[size_t(r)].buffer) break; // the buffer is used unchecked
            void *const bits = reinterpret_cast<void *>(uintptr_t(0x6b000000u + (rng() % 16) * 0x100u));
            void *const format = reinterpret_cast<void *>(uintptr_t(kFormatTag + 0x1000 + rng() % 8));
            d->SetRecordPixels(r, bits, format, rng() % 64, rng() % 64, rng() % 64 * 4, &g);
            break;
        }
        case 17: d->RebuildAtlases(); break;
        case 18: case 19: d->Tick(); break;
        case 20: d->AddRefRecord(PickRecord(rng, v)); break;
        case 21: case 22: {
            const int32_t r = PickRecord(rng, v);
            // A record released to zero twice would release its (null) buffer again: the original does not check.
            if (r >= 0 && v->mRecords[size_t(r)].refCount == 1 && !v->mRecords[size_t(r)].buffer &&
                !v->mRecords[size_t(r)].bufferUncounted)
                break;
            // The game releases what it holds: a buffer's cache holds one count of its record (cache +0x10), which the
            // test's random releases must not take (the record would be freed under the cache).
            if (r >= 0) {
                uint32_t held = 0;
                for (const drawfx::Fake &t : gTargets)
                    if (const auto *const c = static_cast<const uint32_t *>(t.owner)) held += int32_t(c[4]) == r;
                if (v->mRecords[size_t(r)].refCount <= int32_t(held)) break;
            }
            d->ReleaseRecord(r);
            break;
        }
        case 23: d->ApplyDrawState(); break;
        case 24: {
            const uint32_t vertices = rng() % 8 ? rng() % 64 : D::kBatchVertices + rng() % 3 - 1;
            const uint32_t indices = rng() % 8 ? rng() % 96 : D::kBatchIndices + rng() % 3 - 1;
            float *positions = nullptr, *uvs = nullptr;
            uint32_t *colours = nullptr;
            uint16_t *out = nullptr;
            const uint32_t ok = d->LockBatch(vertices, indices, &positions, &colours, &uvs, &out);
            drawfx::Note("lock", {ok, drawfx::Norm(drawfx::P(positions)), drawfx::Norm(drawfx::P(colours)),
                                  drawfx::Norm(drawfx::P(uvs)), drawfx::Norm(drawfx::P(out))});
            break;
        }
        case 25: d->DrawBatch(); break;
        case 26: {
            drawfx::Note("depth", uint32_t(d->GetClipDepth()));
            const cDraw2DPoint o = d->GetClipOrigin();
            const cGZRect r = d->GetClipRect();
            drawfx::Note("clip", {uint32_t(o.x), uint32_t(o.y), uint32_t(r.left), uint32_t(r.top), uint32_t(r.right),
                                  uint32_t(r.bottom)});
            drawfx::Note("material", drawfx::Norm(drawfx::P(d->GetMaterial())));
            break;
        }
        case 27: {
            const int32_t r = PickRecord(rng, v);
            if (r >= 0 && !v->mRecords[size_t(r)].buffer) break; // the buffer is read unchecked (0x150140)
            const cDraw2DRecordInfo info = d->GetRecordInfo(r);
            drawfx::Note("info", {info.width, info.height, drawfx::P(info.pixelFormat)});
            break;
        }
        case 28: {
            // Events: a connection change, the device's state (1 back, 3 going, others), another type.
            const uint32_t kind = rng() % 6;
            const uint32_t codes[] = {1, 3, 0, 2, 4};
            gEventData[0] = 0x77;
            gEventData[1] = codes[rng() % 5];
            const uint32_t type = kind == 0 ? nGZEvent::cConnectionChangedEvent::kType
                                  : kind < 5 ? D::kDeviceStateEvent
                                             : rng();
            const uint32_t a1 = rng(), a3 = rng();
            if (rng() % 4 == 0) {
                // The template's own slot 3 (0x1500a0, only in its vtable 0xd9c30c, which the destructor stores while
                // the object goes): the connection change alone. Ours is our template's, called by name.
                using Template = nGZEvent::cAbstractBehaviorTypeST<cIDraw2D, cDraw2DBehaviorTraits>;
                if (ours)
                    static_cast<Template *>(v)->Template::OnEvent(type, a1, gEventData, a3);
                else
                    reinterpret_cast<void(__thiscall *)(void *, uint32_t, uint32_t, void *, uint32_t)>(
                        GameAddress<void>(0x1500a0))(block, type, a1, gEventData, a3);
            } else {
                d->OnEvent(type, a1, gEventData, a3);
            }
            break;
        }
        case 29: {
            const uint32_t which = rng() % 3;
            const uint32_t data[2] = {rng(), rng()};
            switch (rng() % 3) {
            case 0: d->NotifyListeners(kListenEvents[which], rng(), const_cast<uint32_t *>(data)); break;
            case 1: drawfx::Note("ask", d->AskListeners(kListenEvents[which], rng(), const_cast<uint32_t *>(data))); break;
            default: drawfx::Note("query", d->OnQuery(kListenEvents[which], rng(), const_cast<uint32_t *>(data))); break;
            }
            break;
        }
        case 30: {
            const uint32_t which = rng() % 3;
            if (rng() % 2)
                d->Connect(kListenEvents[which], reinterpret_cast<cIGZUnknown *>(&gListeners[rng() % 3]), kHeardAs[which]);
            else
                d->Disconnect(kListenEvents[which], reinterpret_cast<cIGZUnknown *>(&gListeners[which]), kHeardAs[which]);
            break;
        }
        case 31: {
            drawfx::Note("active", d->IsActive());
            drawfx::Note("events", d->GetEventCount());
            NoteIdRange(d->GetEventIds());
            const uint32_t ids[] = {kRegistryIds[0], kRegistryIds[1], kRegistryIds[2], rng()};
            const nGZEvent::cEventAttribute a = d->GetEventAttribute(ids[rng() % 4]);
            drawfx::Note("attribute", {a.mId, drawfx::P(a.mName), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
            d->SetAttribute(rng(), rng(), rng());
            d->GetAttribute(rng(), rng(), rng());
            break;
        }
        case 32: case 33: {
            const uint32_t ids[] = {1, cIDraw2D::kIid, D::kIidBehavior2, D::kIidBehaviorType, Hw::kIid,
                                    cIGZDrawContextEngineFactory::kIid, 0x12345678};
            const uint32_t iid = ids[rng() % 7];
            cIGZUnknown *const faces[] = {d, e, factory};
            cIGZUnknown *const face = faces[rng() % 3];
            void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
            const bool ok = face->QueryInterface(iid, &out);
            drawfx::Note("qi", {ok, drawfx::Norm(drawfx::P(out))});
            if (ok && iid != D::kIidBehaviorType) drawfx::Note("release", static_cast<cIGZUnknown *>(out)->Release());
            if (rng() % 4 == 0) drawfx::Note("null out", face->QueryInterface(iid, nullptr));
            break;
        }
        case 34: {
            cIGZUnknown *const faces[] = {d, e, factory};
            cIGZUnknown *const face = faces[rng() % 3];
            drawfx::Note("addref", face->AddRef());
            if (rng() % 3) drawfx::Note("release", face->Release());
            break;
        }
        case 35: {
            if ((v->mRefCount & 0x7fffffff) < 2) break;
            cIGZUnknown *const faces[] = {d, e, factory};
            drawfx::Note("release", faces[rng() % 3]->Release());
            break;
        }
        case 36: {
            gRaw.value[0] = rng() % 3 != 0;
            cIGZBuffer *const targetBuffer =
                reinterpret_cast<cIGZBuffer *>(rng() % 4 ? static_cast<void *>(&gBackbuffer) : static_cast<void *>(&gStranger));
            cIGZDrawContextEngine *made = reinterpret_cast<cIGZDrawContextEngine *>(uintptr_t(0x4321));
            const bool ok = factory->CreateEngine(targetBuffer, &made);
            drawfx::Note("engine made", {ok, drawfx::Norm(drawfx::P(made))});
            if (ok) drawfx::Note("release", made->Release());
            break;
        }
        case 37:
            // The state cache changed underneath, the device's readiness, the draw system's flag, the atlases' flag.
            gState.value[0] += rng() % 3;
            gDevice.ret[35] = rng() % 2;
            gDrawSystem.value[0] = rng() % 2;
            gDrawSystem.value[1] = rng();
            gTargets[2].value[4] += rng() % 2; // the texture buffer's pixels changed
            v->mNeedsRebuild = rng() % 2 != 0;
            break;
        case 49: {
            // Lane OU: the device objects asked for again with the old ones still there (an event repeated; the device
            // back before the old objects went): each says whether it still exists, and the ones that do not are made
            // again; the arrays say which kind they were created with.
            if (!v->mDevice.p) break;
            const auto created = [&rng](auto &ref) {
                if (ref.p) reinterpret_cast<drawfx::Fake *>(ref.p)->value[0] = rng() % 3 != 0;
            };
            created(v->mDefaultBlock);
            for (auto &block : v->mBlendBlocks) created(block);
            created(v->mEngineBlock);
            created(v->mVertexShader);
            created(v->mDeclaration);
            for (auto *array : {&v->mVertexArray, &v->mIndexArray})
                if (array->p) {
                    auto *const fake = reinterpret_cast<drawfx::Fake *>(array->p);
                    fake->value[2] = rng() % 3 != 0;
                    fake->value[3] = 1 + rng() % 2;
                }
            gEmptyTarget = rng() % 3 == 0;
            MakeDeviceObjects(ours, block);
            gEmptyTarget = false;
            break;
        }
        case 46: case 47: case 48: CacheStep(rng); break;
        default: EngineStep(rng, e, target); break;
        }
        // A record that lost its cells (a rebuild could not place it again, or its last holder let go) is not drawn
        // from: the engine face's SelectTile and GetTextureTiles index pool 3 then, the memory after the pool array, so
        // what they read is whatever the heap held. The game sets the texture again before it draws; so does the test.
        // Likewise when the record was placed again elsewhere: the engine's tile list still names the atlases it had, and
        // SelectTile reads the new pool's textures at those indices.
        // (The engine's texture source is its +0x30 in the object; it draws from the record only while that is set.)
        if (*reinterpret_cast<void *const *>(static_cast<const uint8_t *>(block) + 0x30) != nullptr) {
            bool usable = v->mCurrentRecord != -1 && size_t(v->mCurrentRecord) < v->mRecords.size() &&
                          v->mRecords[size_t(v->mCurrentRecord)].pool != D::kUnplaced &&
                          v->mRecords[size_t(v->mCurrentRecord)].buffer != nullptr; // GetTextureTiles asks the buffer its size
            if (usable)
                for (const Hw::Tile &tile : VectorAt<Hw::Tile>(block, 0xc8))
                    usable = usable && size_t(tile.index) < v->mPools[v->mRecords[size_t(v->mCurrentRecord)].pool].textures.items.size();
            if (!usable) {
                cGZRect none{};
                e->SetTexture(nullptr, 0.0f, 0.0f, 1.0f, 1.0f, &none); // the engine lets its texture go
            }
        }
        NoteObject(block);
        NoteCaches();
    }

    // The buffers let their caches go (the last reference of each: the draw system stops watching it).
    for (drawfx::Fake &t : gTargets)
        if (auto *const cache = static_cast<cIGZUnknown *>(t.owner)) {
            t.owner = nullptr;
            cache->Release();
        }
    const uint32_t count = v->mRefCount & 0x7fffffff;
    drawfx::Note("count", count);
    if (rng() % 4 == 0) {
        // Deleted through the engine face's virtual destructor (slot 24: the adjustor thunk 0x14c5a8 to the deleting
        // destructor 0x14c680), as the base's own Release would; the connections dropped first as the last Release does.
        static_cast<nGZEvent::cIConnectionManager *>(gManager)->DisconnectAll(reinterpret_cast<cIGZUnknown *>(block));
        delete reinterpret_cast<Hw *>(static_cast<uint8_t *>(block) + 8);
    } else {
        for (uint32_t i = 0; i < count; ++i) drawfx::Note("release", d->Release());
    }
    gBlock = nullptr;
    gDeviceNow = false;
    heap.Stop();
    NoteCachePool();
    NoteRefs();
    drawfx::Note("heap", {uint32_t(heap.Blocks()), uint32_t(heap.Bytes())});
}

template <size_t N> bool SlotsAre(uint32_t vtable, const uint32_t (&slots)[N][2])
{
    const auto *const table = GameAddress<void *const>(vtable);
    for (const auto &s : slots)
        if (table[s[0]] != GameAddress<void>(s[1])) return false;
    return true;
}

} // namespace

DIFF_TEST(GZDraw2D_Slots, 0x14ba80)
{
    // Every slot of the three faces and the class's own code: QueryInterface 0x151390, Release 0x1518a0, OnEvent
    // 0x1500e0, NotifyListeners 0x151b50, AskListeners 0x14f190, Connect 0x14e5d0, Disconnect 0x14ed60, GetEventCount
    // 0x151110, GetEventIds 0x14cf90, GetEventAttribute 0x14d200, SetMaterial 0x151ad0, PushClipRect 0x1511c0,
    // PushClip 0x1511e0, PopClip 0x151180, PushClipOrigin 0x151350, DrawLine 0x150f20, FillRect 0x14f0b0, FillTriangle
    // 0x14eee0, FillQuad 0x14efc0, SetBlendMode 0x151c40, DrawImageAt 0x14df20, DrawRecordAt 0x14deb0, Tick 0x14ee00,
    // AddRefRecord 0x14eec0, ReleaseRecord 0x151940, ApplyDrawState 0x151fa0, LockBatch 0x1519c0, DrawBatch 0x14ee30,
    // GetClipDepth 0x14e710, GetClipOrigin 0x14e740, GetClipRect 0x14e6f0, GetRecordInfo 0x150140, the engine's vf19
    // 0x151a60, OnTargetSet 0x14f960, ApplyState 0x14fd00, SetViewport 0x14fc20, SetProjection 0x14fb60,
    // GetTextureTiles 0x14f6f0, SelectTile 0x14fb10, DrawTriangles 0x14f580, DrawQuads 0x14f390, CreateEngine 0x14ea70,
    // the helpers ApplyTopClip 0x151d20, ReleaseDeviceObjects 0x152180, Intersect 0x150e20, the event manager's cell
    // 0x14e670, the base's batch reset 0x8b8c20 (DrawTriangles' and DrawQuads'), the constructor 0x14ba80, the destructor
    // 0x14c1a0 and the deleting destructor 0x14c680 (the last Release, or slot 24 of the engine face through its
    // adjustor thunk 0x14c5a8); the other adjustor thunks (QueryInterface 0x151402 / 0x15140a, AddRef 0x14c943 /
    // 0x14c94b, Release 0x1518ec / 0x1518f4); the template's own OnEvent 0x1500a0; the atlas and record code (DrawImage 0x14d2e0,
    // DrawRecord 0x14d8e0, CreateRecord 0x14eb10, SetRecordPixels 0x1522d0, RebuildAtlases 0x14e1e0, the engine's
    // SetTextureSource 0x14f970, ReleaseRecordCells 0x14eca0, AllocateRecord 0x14c960, PlaceRecord 0x14ca90) and the
    // hardware cache's slots (vtable 0xd9c558: QueryInterface 0x151420, Attach 0x1501b0, Detach 0x1520d0, LoadForTick
    // 0x150320, Unload 0x152160, IsStale 0x150ed0, Invalidate 0x150eb0, IsLoaded 0x150ec0, Load 0x151480, the deleting
    // destructor 0x14c6b0).
    static const uint32_t kPrimary[][2] = {{3, 0x1500e0}, {43, 0x14c680}};
    static const uint32_t kEngine[][2] = {{24, 0x14c5a8}};
    static const uint32_t kFactory[][2] = {{3, 0x14ea70}};
    if (!SlotsAre(kPrimaryVTable, kPrimary) || !SlotsAre(kEngineVTable, kEngine) || !SlotsAre(kFactoryVTable, kFactory))
        ctx.Fail("the original's vtables do not hold the slots the recorders stand in for");
    BuildRegistry();
    Patches patches;
    // The pool object: no vtable (nothing reaches the page bookkeeping), a test page as its current page.
    drawfx::Cells poolCells;
    poolCells.Set(kCachePool, 0u);
    poolCells.Set(kCachePool + 4, 0u);
    poolCells.Set(kCachePool + 8, 0u);
    char seedText[16] = {};
    const uint32_t only = GetEnvironmentVariableA("TS2_TEST_SEED", seedText, sizeof(seedText)) ? uint32_t(atoi(seedText)) : 0;
    for (uint32_t seed = only ? only : 1; seed <= (only ? only : 1500); ++seed) {
        NewCase(seed);
        ctx.Mark("orig");
        auto a = drawfx::Side(&BuildWorld, [&] {
            BuildCachePool(seed);
            RunSide(false, seed);
        });
        NewCase(seed);
        ctx.Mark("ours");
        auto b = drawfx::Side(&BuildWorld, [&] {
            BuildCachePool(seed);
            RunSide(true, seed);
        });
        if (!drawfx::Same(ctx, a.first, b.first, "draw2d", seed, a.second)) break;
    }
}

DIFF_TEST(GZDraw2D_Intersect, 0x150e20)
{
    using Fn = bool(__thiscall *)(cGZRect *, const cGZRect *, const cGZRect *);
    const auto original = ctx.Original<Fn>(0x150e20);
    std::mt19937 rng(0x150e20);
    for (unsigned round = 0; round < 20000; ++round) {
        cGZRect a, b;
        for (cGZRect *r : {&a, &b}) {
            r->left = int32_t(rng() % 60) - 20;
            r->top = int32_t(rng() % 60) - 20;
            r->right = r->left + int32_t(rng() % 50) - 8;
            r->bottom = r->top + int32_t(rng() % 50) - 8;
            if (rng() % 50 == 0) r->right = INT32_MAX - int32_t(rng() % 3);
        }
        const unsigned alias = rng() % 3; // 0 a separate out, 1 out is a, 2 out is b
        cGZRect ia = a, ib = b, oa{11, 22, 33, 44}, ob{11, 22, 33, 44};
        cGZRect ja = a, jb = b;
        cGZRect *const outA = alias == 1 ? &ia : alias == 2 ? &ib : &oa;
        cGZRect *const outB = alias == 1 ? &ja : alias == 2 ? &jb : &ob;
        const bool ra = original(outA, &ia, &ib);
        const bool rb = D::Intersect(outB, nullptr, &ja, &jb);
        ++ctx.cases;
        if (ra != rb || std::memcmp(outA, outB, sizeof(cGZRect)) != 0 || std::memcmp(&ia, &ja, sizeof(cGZRect)) != 0 ||
            std::memcmp(&ib, &jb, sizeof(cGZRect)) != 0) {
            ctx.Fail("round %u (alias %u): {%d %d %d %d} & {%d %d %d %d}: orig %d {%d %d %d %d}, ours %d {%d %d %d %d}", round,
                     alias, a.left, a.top, a.right, a.bottom, b.left, b.top, b.right, b.bottom, ra, outA->left, outA->top,
                     outA->right, outA->bottom, rb, outB->left, outB->top, outB->right, outB->bottom);
            return;
        }
    }
}

namespace {

uint8_t *gHand = nullptr;
void *__cdecl GiveService(size_t size) { return size == cDraw2DSystemService::kSize ? gHand : ::operator new(size); }

void NoteService(void *service)
{
    drawfx::Note("service", {Word(service, 4), drawfx::Norm(Word(service, 8)), Word(service, 0xc),
                             uint32_t(static_cast<uint8_t *>(service)[0x10])});
}

void RunService(bool ours, uint32_t seed)
{
    SideWorld world;
    drawfx::gLog.reserve(1 << 12);
    gScript.seed(seed ^ 0x5e41);
    std::mt19937 rng(seed);
    difftest::AllocationCounter heap;
    void *const made = ours ? static_cast<void *>(cDraw2DSystemService::Create())
                            : reinterpret_cast<void *(__cdecl *)()>(GameAddress<void>(kServiceCreator))();
    {
        difftest::NotCounted bookkeeping;
        drawfx::AddRange(made, cDraw2DSystemService::kSize, kServiceTag);
    }
    auto *const s = static_cast<cIDraw2DSystemService *>(made);
    NoteService(made);
    drawfx::Note("addref", s->AddRef());
    for (int step = 0; step < 24; ++step) {
        const uint32_t pick = rng() % 12;
        drawfx::Note("pick", pick);
        switch (pick) {
        case 0: {
            const uint32_t ids[] = {1, cIGZSystemService::kIID, cIDraw2DSystemService::kIid, cIDraw2D::kIid, 0x12345678};
            const uint32_t iid = ids[rng() % 5];
            void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
            const bool ok = s->QueryInterface(iid, rng() % 5 ? &out : nullptr);
            drawfx::Note("qi", {ok, drawfx::Norm(drawfx::P(out))});
            if (ok && out != reinterpret_cast<void *>(uintptr_t(0x1234)))
                drawfx::Note("release", static_cast<cIGZUnknown *>(out)->Release());
            break;
        }
        case 1:
            drawfx::Note("addref", s->AddRef());
            if (rng() % 2) drawfx::Note("release", s->Release());
            break;
        case 2:
            drawfx::Note("id", s->GetServiceID());
            if (rng() % 2) s->SetServiceID(rng());
            break;
        case 3: drawfx::Note("priorities", {uint32_t(s->GetServicePriority()), uint32_t(s->GetServiceTickPriority())}); break;
        case 4:
            drawfx::Note("running", s->IsServiceRunning());
            s->SetServiceRunning(rng() % 2 != 0);
            break;
        case 5: case 6: drawfx::Note("init", s->Init()); break;
        case 7: drawfx::Note("tick", s->OnTick(rng())); break;
        case 8: drawfx::Note("idle", s->OnIdle(rng())); break;
        case 9: drawfx::Note("draw2d", drawfx::Norm(drawfx::P(s->GetDraw2D()))); break;
        case 10: drawfx::Note("shutdown", s->Shutdown()); break;
        default:
            // The renderer's Tick (the service's OnTick) with the atlases due and the device ready or not.
            gDevice.ret[35] = rng() % 2;
            if (void *const r = s->GetDraw2D()) View(r)->mNeedsRebuild = rng() % 2 != 0;
            break;
        }
        NoteService(made);
    }
    const uint32_t count = Word(made, 4);
    for (uint32_t i = 0; i < count; ++i) drawfx::Note("release", s->Release());
    heap.Stop();
    NoteRefs();
    drawfx::Note("heap", {uint32_t(heap.Blocks()), uint32_t(heap.Bytes())});
}

} // namespace

DIFF_TEST(GZDraw2DService_Create, 0x14f2b0)
{
    // The creator on a block of seeded bytes (the game's operator new stood in for): every byte but the vtable, which
    // is 0xd9c518; a failed allocation gives null. Ours: a fresh service.
    std::mt19937 rng(0x14f2b0);
    alignas(8) static uint8_t seeded[cDraw2DSystemService::kSize], ours[cDraw2DSystemService::kSize];
    shape::CodePatch give{0x824632, reinterpret_cast<const void *>(&GiveService)};
    for (unsigned round = 0; round < 64; ++round) {
        for (uint8_t &x : seeded) x = uint8_t(rng());
        std::memcpy(ours, seeded, sizeof(seeded));
        gHand = seeded;
        ++ctx.cases;
        void *const made = ctx.Original<void *(__cdecl *)()>(kServiceCreator)();
        ::new (static_cast<void *>(ours)) cDraw2DSystemService;
        if (made != seeded) ctx.Fail("round %u: the creator's object is not the block it was given", round);
        if (Word(seeded, 0) != drawfx::P(GameAddress<void>(kServiceVTable))) ctx.Fail("round %u: the vtable", round);
        if (std::memcmp(seeded + 4, ours + 4, sizeof(seeded) - 4) != 0) {
            ctx.Fail("round %u: the service differs", round);
            return;
        }
    }
    gHand = nullptr;
    if (ctx.Original<void *(__cdecl *)()>(kServiceCreator)() != nullptr) ctx.Fail("no null without memory");
    cDraw2DSystemService *const s = cDraw2DSystemService::Create();
    if (!s || s->mRefCount != 0 || s->mDraw2D || s->mServiceId != cDraw2DSystemService::kServiceId || s->mRunning)
        ctx.Fail("Create: not a fresh service");
    delete s;
}

DIFF_TEST(GZDraw2DService_Run, 0x150210)
{
    // Every slot: QueryInterface 0x151450, AddRef 0x148fb0, Release 0x151920 (the deleting destructor 0x14c7e0 at
    // zero), GetServiceID 0x145770, SetServiceID 0x149c60, GetServicePriority / GetServiceTickPriority 0x14f380,
    // IsServiceRunning 0x150f10, SetServiceRunning 0x1520c0, Init 0x150210, Shutdown 0x152140, OnTick 0x151160, OnIdle
    // 0x148260, GetDraw2D 0x46390; Init makes the side's cDraw2D (the constructor 0x14ba80 on the original's side).
    BuildRegistry();
    Patches patches;
    char seedText[16] = {};
    const uint32_t only = GetEnvironmentVariableA("TS2_TEST_SEED", seedText, sizeof(seedText)) ? uint32_t(atoi(seedText)) : 0;
    for (uint32_t seed = only ? only : 1; seed <= (only ? only : 600); ++seed) {
        NewCase(seed);
        ctx.Mark("orig");
        auto a = drawfx::Side(&BuildWorld, [&] { RunService(false, seed); });
        NewCase(seed);
        ctx.Mark("ours");
        auto b = drawfx::Side(&BuildWorld, [&] { RunService(true, seed); });
        if (!drawfx::Same(ctx, a.first, b.first, "service", seed, a.second)) break;
    }
}
