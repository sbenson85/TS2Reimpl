// Differential tests for the shader programs as whole classes (lane NR, rendering R3b step 3): src/gz/cShaderProgramsDX.h
// (cShaderProgramsManagerDX, cShaderProgramInputsDX, cVertexShaderDX, cPixelShaderDX).
//
// gz_dx_Shaders: per case, a programs manager on each side (the original's: our object's layout with the game's vtable
// 0xd9f108, and the game's cached manager cell 0x10caa3c pointing at it, which the programs' release reads through
// 0x18ddf0; ours: our Instance(), which ours reads) runs the same seeded sequence through its vtable: programs
// assembled, compiled and linked from fragments (D3DX stands in at its RVAs: shader bytecode is synthetic token streams
// in fake ID3DXBuffers, constant tables and fragment linkers are fakes), with errors strings or without, on a ready
// device or not, with the device's version limits and software processing cap chosen per case; Reset, Dump into a fake
// stream, Disassemble, the programs' and inputs' slots, releases, and the destructor. The programs come from the game's
// pools 0x10ca2fc / 0x10ca308 (fresh pages per side), the inputs from the game's operator new (each side's heap).
// RVAs: the getter 0x1758c0 (its guard set initialised: the static each call, against one object of ours with our
// vtable); the manager's slots 0x176bf0 0x175120 0x175310 0x175710 0x174f60 0x1752f0 0x174d50 0x175aa0 0x1758a0
// 0x175640 0x176740 0x174be0 and what they call (CreateProgram 0x176100, the keys 0x1738a0 0x174920 0x1749b0 and their
// order 0x1747d0, the inputs' constructor 0x173950); the programs' 0x176c20 0x176b60 0x176db0 0x176d20 0x176e80
// 0x176e60 0x174c00 0x174a50 and the shared 0x175a90 0x1758b0 0x176d00 0x176d10; the inputs' 0x176bc0 0x153f50
// 0x1760c0 0x175580 0x175540 0x1760e0 0x176ca0 0x176c80 0x174ba0.
//
// Compared: the log (every fake call with its arguments, the D3DX calls with their text, defines, flags and which out
// pointers are given, LogD3DError lines, the service queries), each result, and after every step the manager's state
// (the linking flag, the six linkers, the fragment buffers, the fragment name set by text, the names to link, both
// program lists with each program's count, device, Direct3D shader, function buffer, info and fragment names), the
// held programs' counts, the fragment names' counts and every fake's count; the inputs' constants and samplers by
// value through their slots (names by text); the pools' state at the end. Left out, with the reason:
// - the objects' vtable words (each side its own);
// - the inputs' addresses (each side's heap) and the vectors' capacities;
// - a sampler's registers past those D3DX wrote: the original's record (0x173950's stack local) leaves them as its
//   stack held them; ours are zero;
// - the four statics 0x173950 sums the function into (0x10ca2ec, 0x10ca350 / 358 / 360): written once behind their own
//   guards and read by nothing in the image (their only references are in 0x173950); ours leaves them out.
// The fragment names the sequence links are interned in the manager's name set by the test (the same strings on both
// sides, in one array, counted as the manager counts its own): the programs' order compares the names by address
// (0x1747d0), so names each side allocated would order differently; one name ("nope") is left to the manager to make,
// and only reaches the "Unknown fragment" path, where the order decides nothing (the sweep requires both kinds linked).
// The manager's destructor 0x174be0 runs its body 0x1745d0 (also the static's atexit destructor's).
// The managers' constructors are not compared: both sides' objects are built by ours (the original's construction is
// the getter's first-call path, which needs the CRT's thread-safe static support the harness doesn't initialise).
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cShaderProgramsDX.h"
#include "rz/RZFixedAllocator.h"
#include "rz/cRZString.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

constexpr uint32_t kManagerVTable = 0xd9f108;
constexpr uint32_t kManagerCache = 0x10caa3c; // 0x18ddf0's cell
constexpr uint32_t kManagerStatic = 0xfed650, kManagerGuard = 0x10ca368;
constexpr uint32_t kGraphicServiceId = 0x85522803, kGraphicServiceIid = 0xc55289ba;
constexpr uint32_t kIidDeviceDX = 0x553b7cf;

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

// LinkProgram's profile buffer: the original writes seven characters at most into eight bytes and leaves the eighth as
// its stack held it (ORIGINAL-BUG; a "vs_3_sw" profile has no end in the original's buffer). The stack the original's
// frame will use is zeroed first, so its eighth byte reads as zero too, as ours is.
__declspec(noinline) __declspec(safebuffers) void ZeroStack()
{
    volatile uint8_t pad[4096];
    for (size_t i = 0; i < sizeof(pad); ++i) pad[i] = 0;
}

uint32_t W(const void *p) { return drawfx::P(p); }
uint32_t N(const void *p) { return drawfx::Norm(W(p)); }
void NoteWords(const char *label, std::vector<uint32_t> words, std::string text = {})
{
    drawfx::Call c;
    c.note = label;
    c.words = std::move(words);
    c.text = std::move(text);
    drawfx::gLog.push_back(std::move(c));
}

// ---- the fakes' interfaces ----
using drawfx::Ret;
constexpr drawfx::Slot U{0, Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, Ret::Int, true, "Release", ""};
constexpr drawfx::Slot Named(uint8_t words, const char *name, const char *args = "")
{
    return drawfx::Slot{words, Ret::Int, true, name, args};
}

inline constexpr drawfx::Slot kDeviceSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U,
    Named(0, "GetDeviceEnumConst"), // 16
    U,
    Named(0, "GetResourceManager"), // 18
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    Named(0, "IsDeviceReady"), // 35
    U, U, U, U, U, U, U, U, U, U, U, U,
    Named(0, "GetD3DDevice"), // 48
};
inline constexpr drawfx::Iface kDevice{"cIDeviceDX", 49, kDeviceSlots};

inline constexpr drawfx::Slot kEnumSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    Named(1, "HasProcessingCap", "u"), // 20
    Named(1, "GetLimit", "u"),         // 21
};
inline constexpr drawfx::Iface kEnum{"cIDeviceEnum", 22, kEnumSlots};

inline constexpr drawfx::Slot kResourcesSlots[] = {
    kQI, kAddRef, kRelease, U, U, Named(1, "Insert"), U, Named(1, "Erase"),
};
inline constexpr drawfx::Iface kResources{"cIDeviceResourceManager", 8, kResourcesSlots};

inline constexpr drawfx::Slot kGraphicsSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    Named(0, "GetVertexDeclarationsManager"), // 27
    U, U, Named(1, "ErrorText", "u"), // 30: LogD3DError's QueryServiceValue asks the same service (src/rz/MiscHelpersA.cpp)
};
inline constexpr drawfx::Iface kGraphics{"cIGraphicSystemDX", 31, kGraphicsSlots};

inline constexpr drawfx::Slot kDeclarationsSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, Named(0, "GetSemanticRenames"),
};
inline constexpr drawfx::Iface kDeclarations{"cIVertexDeclarationsManagerDX", 7, kDeclarationsSlots};

inline constexpr drawfx::Slot kStringSlots[] = {
    kQI, kAddRef, kRelease,
    Named(2, "FromChar", "su"), // 3
    Named(1, "FromChar", "s"),  // 4
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    Named(2, "Append", "su"), // 20
    U, U, U, U, U,
    Named(2, "Erase", "uu"), // 26
};
inline constexpr drawfx::Iface kString{"cIGZString", 27, kStringSlots};

inline constexpr drawfx::Slot kStreamSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U,
    Named(1, "SetUint32", "u"), // 9
    U, U, U, U,
    Named(1, "SetRZCharStr", "s"), // 14
};
inline constexpr drawfx::Iface kStream{"cIGZOStream", 15, kStreamSlots};

inline constexpr drawfx::Slot kBufferSlots[] = {
    kQI, kAddRef, kRelease, Named(0, "GetBufferPointer"), Named(0, "GetBufferSize"),
};
inline constexpr drawfx::Iface kBuffer{"ID3DXBuffer", 5, kBufferSlots, true};

inline constexpr drawfx::Slot kTableSlots[] = {
    kQI, kAddRef, kRelease, Named(0, "GetBufferPointer"), Named(0, "GetBufferSize"),
    Named(1, "GetDesc"), Named(3, "GetConstantDesc"), Named(1, "GetSamplerIndex"), Named(2, "GetConstant", "iu"),
};
inline constexpr drawfx::Iface kTable{"ID3DXConstantTable", 9, kTableSlots, true};

inline constexpr drawfx::Slot kLinkerSlots[] = {
    kQI, kAddRef, kRelease, Named(1, "GetDevice"), Named(0, "GetNumberOfFragments"),
    Named(1, "GetFragmentHandleByIndex", "u"), Named(1, "GetFragmentHandleByName", "s"),
    Named(2, "GetFragmentDesc"), Named(1, "AddFragments"), Named(1, "GetAllFragments"), Named(2, "GetFragment"),
    Named(6, "LinkShader", "su@3x1u"),
};
inline constexpr drawfx::Iface kLinker{"ID3DXFragmentLinker", 12, kLinkerSlots, true};

// ---- the world ----
constexpr unsigned kBuffers = 128, kLinkers = 48, kShaders = 32, kTables = 4;
constexpr uint32_t kEndToken = 0x0000ffff;

struct BufferData {
    uint32_t words[24];
    uint32_t size; // bytes
};

// The fragments the linkers know: their names (interned by the test, below) and targets. "ghost" is interned but no
// linker knows it; "nope" is neither.
struct Fragment {
    const char *name;
    uint32_t target;
};
constexpr Fragment kFragments[] = {
    {"vsA", 0xfffe0101}, {"vsB", 0xfffe0200}, {"vsX", 0xfffe0201}, {"vs3", 0xfffe0300}, {"vsSW", 0xfffe02ff},
    {"psA", 0xffff0101}, {"psB", 0xffff0200}, {"psX", 0xffff0201}, {"ps3", 0xffff0300}, {"vs3SW", 0xfffe03ff},
};
constexpr unsigned kFragmentCount = sizeof(kFragments) / sizeof(kFragments[0]);
constexpr const char *kInterned[] = {"vsA", "vsB", "vsX", "vs3", "vsSW", "psA", "psB", "psX", "ps3", "vs3SW", "ghost"};
constexpr unsigned kInternedCount = sizeof(kInterned) / sizeof(kInterned[0]);

// Programs the D3DX stand-ins hand out (the version token first; word 1 picks the constant table).
struct Program {
    uint32_t words[5];
    uint32_t count;
};
constexpr Program kPrograms[] = {
    {{0xfffe0101, 0, kEndToken}, 3},      {{0xfffe0101, 1, kEndToken}, 3},    {{0xfffe0200, 2, 7, kEndToken}, 4},
    {{0xfffe0300, 3, kEndToken}, 3},      {{0xfffe02ff, 0, kEndToken}, 3},    {{0xfffe03ff, 1, 5, kEndToken}, 4},
    {{0xffff0101, 2, kEndToken}, 3},      {{0xffff0200, 3, 9, kEndToken}, 4}, {{0xffff0300, 0, kEndToken}, 3},
    {{0xffff0104, 1, kEndToken}, 3},      {{0xfffe0101, 0, 0xfffe}, 3},       {{0xffff0201, 2, 2, kEndToken}, 4},
};
constexpr unsigned kProgramCount = sizeof(kPrograms) / sizeof(kPrograms[0]);

constexpr const char *kConstantNames[] = {"mvp", "world", "light", "color", "a", "b", "fog", "bones"};
constexpr const char *kTexts[] = {"vs.1.1 mov oPos, v0", "ps.2.0 mov oC0, c0", "float4 main() : COLOR { return 0; }",
                                  "fragment text", ""};

struct ConstantData {
    d3dx::ConstantDesc desc;
    uint32_t registers; // a sampler's register count
    uint32_t hr;
};
struct TableData {
    std::vector<ConstantData> c;
    uint32_t tableHr, descHr;
};

// Everything the code under test is handed pointers into, in one registered range.
struct Statics {
    const char *defines[6];
    uint32_t constantHandles[32];
    uint32_t fragmentHandles[16];
    BufferData buffers[kBuffers];
    char samplerNames[kTables][8][8];
    alignas(8) uint8_t names[kInternedCount][sizeof(cRZString)];
} gStatic;
constexpr uint32_t kStaticTag = 0x7e000000;

struct World {
    uint32_t seed;
    std::mt19937 rng;
    uint32_t ready, adapter, vsLimit[2], psLimit[2], softwareCap;
    TableData tables[kTables];
    unsigned nextBuffer, nextLinker, nextVertex, nextPixel;
} gWorld;

drawfx::Fake gDevice, gDevice2, gEnum, gResources, gD3D, gGraphics, gDeclarations, gErrorText, gText, gStream, gTable;
drawfx::Fake gBuffer[kBuffers], gLinker[kLinkers], gVertex[kShaders], gPixel[kShaders];

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }
template <class T> T *Out(uint32_t word) { return reinterpret_cast<T *>(uintptr_t(word)); }

cRZString *Interned(unsigned i) { return reinterpret_cast<cRZString *>(gStatic.names[i]); }

void SeedWorld(uint32_t seed)
{
    gWorld.seed = seed;
    std::mt19937 g(seed ^ 0x5badc0de);
    gWorld.ready = g() % 10 != 0;
    gWorld.adapter = g() % 6; // all six linker slots
    const uint32_t vs[][2] = {{1, 1}, {2, 0}, {3, 0}, {0, 0}};
    const uint32_t ps[][2] = {{1, 4}, {2, 0}, {3, 0}, {0, 0}};
    const unsigned v = g() % 4, p = g() % 4;
    gWorld.vsLimit[0] = vs[v][0];
    gWorld.vsLimit[1] = vs[v][1];
    gWorld.psLimit[0] = ps[p][0];
    gWorld.psLimit[1] = ps[p][1];
    gWorld.softwareCap = g() % 5 != 0;
    for (unsigned t = 0; t < kTables; ++t) {
        TableData &table = gWorld.tables[t];
        table.c.clear();
        table.tableHr = g() % 8 == 0 ? Pick(g, {0x8876086c, 0x80004005}) : 0;
        table.descHr = g() % 8 == 0 ? 0x80004005 : 0;
        const unsigned n = t == 0 ? 0 : g() % 12;
        unsigned samplers = 0;
        for (unsigned i = 0; i < n; ++i) {
            ConstantData c{};
            c.hr = g() % 16 == 0 ? 0x8876086c : 0;
            d3dx::ConstantDesc &d = c.desc;
            if (g() % 3 == 0 && samplers < 8) {
                std::snprintf(gStatic.samplerNames[t][samplers], 8, "s%u%u", t, samplers);
                d.Name = gStatic.samplerNames[t][samplers++];
                d.RegisterSet = g() % 6 == 0 ? 2 : 3;
                d.Class = g() % 6 == 0 ? 1 : 4;
                d.Type = g() % 6 == 0 ? 3 : 10 + g() % 5;
                c.registers = 1 + g() % 20;
            } else {
                d.Name = kConstantNames[g() % 8];
                d.RegisterSet = g() % 10 == 0 ? Pick(g, {3, 4}) : g() % 3;
                d.Class = g() % 6;
                d.Type = g() % 6 == 0 ? Pick(g, {4, 5, 10}) : g() % 4;
                c.registers = 1;
            }
            d.RegisterIndex = g() % 64;
            d.RegisterCount = 1 + g() % 4;
            d.Rows = 1 + g() % 4;
            d.Columns = 1 + g() % 4;
            d.Elements = 1 + g() % 3;
            d.StructMembers = g() % 3;
            d.Bytes = 16 * d.RegisterCount;
            table.c.push_back(c);
        }
    }
}

drawfx::Fake *NewBuffer(const uint32_t *words, unsigned count, uint32_t bytes)
{
    if (gWorld.nextBuffer >= kBuffers) {
        drawfx::gErrors.push_back("out of buffer fakes");
        return nullptr;
    }
    const unsigned i = gWorld.nextBuffer++;
    BufferData &b = gStatic.buffers[i];
    std::memset(&b, 0, sizeof(b));
    std::memcpy(b.words, words, count * 4);
    b.size = bytes;
    gBuffer[i].refs = 1;
    return &gBuffer[i];
}
drawfx::Fake *NewProgramBuffer(unsigned program)
{
    const Program &p = kPrograms[program % kProgramCount];
    return NewBuffer(p.words, p.count, p.count * 4);
}
drawfx::Fake *NewTextBuffer(const char *prefix)
{
    char text[64];
    const int n = std::snprintf(text, sizeof(text), "%s %u", prefix, unsigned(gWorld.rng() % 1000));
    uint32_t words[16] = {};
    std::memcpy(words, text, n + 1);
    // Now and then empty (no messages: the error text is left alone).
    return NewBuffer(words, 16, gWorld.rng() % 6 == 0 ? 0 : uint32_t(n + 1));
}

// ---- handlers ----
uint32_t BufferPointer(drawfx::Fake &self, const uint32_t *) { return W(static_cast<BufferData *>(self.owner)->words); }
uint32_t BufferSize(drawfx::Fake &self, const uint32_t *) { return static_cast<BufferData *>(self.owner)->size; }

uint32_t HasProcessingCap(drawfx::Fake &, const uint32_t *) { return gWorld.softwareCap; }
uint32_t GetLimit(drawfx::Fake &, const uint32_t *a)
{
    switch (a[0]) {
    case 4: return gWorld.vsLimit[0];
    case 5: return gWorld.vsLimit[1];
    case 8: return gWorld.psLimit[0];
    case 9: return gWorld.psLimit[1];
    default: return 0x77;
    }
}

uint32_t CreateShader(drawfx::Fake *pool, unsigned &next, const uint32_t *a)
{
    const uint32_t r = gWorld.rng() % 10;
    uint32_t hr = r == 0 ? 0x80004005u : r == 1 ? 0x8876086cu : 0;
    const bool object = hr == 0 || gWorld.rng() % 2 == 0;
    drawfx::Fake *f = object && next < kShaders ? &pool[next++] : nullptr;
    if (!f && hr == 0) hr = uint32_t(E_OUTOFMEMORY);
    if (f) f->refs = 1;
    if (a[1]) *Out<void *>(a[1]) = f;
    return hr;
}
uint32_t CreateVertexShader(drawfx::Fake &, const uint32_t *a) { return CreateShader(gVertex, gWorld.nextVertex, a); }
uint32_t CreatePixelShader(drawfx::Fake &, const uint32_t *a) { return CreateShader(gPixel, gWorld.nextPixel, a); }
uint32_t GetCreationParameters(drawfx::Fake &, const uint32_t *a)
{
    D3DDEVICE_CREATION_PARAMETERS p{};
    p.AdapterOrdinal = gWorld.adapter;
    p.DeviceType = D3DDEVTYPE_HAL;
    p.BehaviorFlags = 0x40;
    if (a[0]) std::memcpy(Out<void>(a[0]), &p, sizeof(p));
    return gWorld.rng() % 10 == 0 ? 0x8876086cu : 0;
}

TableData &TableOf(drawfx::Fake &self) { return *static_cast<TableData *>(self.owner); }
uint32_t TableGetDesc(drawfx::Fake &self, const uint32_t *a)
{
    TableData &t = TableOf(self);
    d3dx::ConstantTableDesc d{"creator", 0xfffe0300, UINT(t.c.size())};
    if (a[0]) std::memcpy(Out<void>(a[0]), &d, sizeof(d));
    return t.descHr;
}
uint32_t TableGetConstant(drawfx::Fake &self, const uint32_t *a)
{
    return a[1] < TableOf(self).c.size() ? W(&gStatic.constantHandles[a[1]]) : 0;
}
uint32_t TableGetConstantDesc(drawfx::Fake &self, const uint32_t *a)
{
    TableData &t = TableOf(self);
    const uint32_t i = (a[0] - W(gStatic.constantHandles)) / 4;
    UINT *count = Out<UINT>(a[2]);
    if (a[0] < W(gStatic.constantHandles) || i >= t.c.size()) {
        if (count) *count = 0;
        return 0x80004005;
    }
    const ConstantData &c = t.c[i];
    const uint32_t capacity = count ? *count : 1;
    const uint32_t n = c.desc.RegisterSet == 3 ? c.registers : 1;
    const uint32_t written = (std::min)(capacity, n);
    auto *out = Out<d3dx::ConstantDesc>(a[1]);
    for (uint32_t k = 0; k < written; ++k) {
        out[k] = c.desc;
        out[k].RegisterIndex = c.desc.RegisterIndex + k * 2;
    }
    if (count) *count = written;
    return c.hr;
}

uint32_t LinkerByName(drawfx::Fake &, const uint32_t *a)
{
    const char *name = Out<const char>(a[0]);
    for (unsigned i = 0; i < kFragmentCount; ++i)
        if (name && std::strcmp(name, kFragments[i].name) == 0) return W(&gStatic.fragmentHandles[i]);
    return 0;
}
uint32_t LinkerDesc(drawfx::Fake &, const uint32_t *a)
{
    const uint32_t i = (a[0] - W(gStatic.fragmentHandles)) / 4;
    d3dx::FragmentDesc d{};
    if (i < kFragmentCount) d = {kFragments[i].name, kFragments[i].target};
    if (a[1]) std::memcpy(Out<void>(a[1]), &d, sizeof(d));
    return gWorld.rng() % 12 == 0 ? 0x80004005u : 0;
}
uint32_t LinkerAdd(drawfx::Fake &, const uint32_t *)
{
    const uint32_t r = gWorld.rng() % 16;
    return r < 11 ? 0 : r == 11 ? 1 : r == 12 ? 0x88760b5bu : r == 13 ? 0x80004005u : r == 14 ? 0x8876086cu : 0x88760b59u;
}
uint32_t LinkerLink(drawfx::Fake &, const uint32_t *a)
{
    // The handles array is heap memory on both sides (the original's from operator new at 0x175d99 / 0x175db5, ours a
    // std::vector), so where it lies is not compared; its presence, count and contents are (words: profile present,
    // flags, then the array's presence).
    drawfx::gLog.back().words[2] = a[2] != 0;
    const uint32_t r = gWorld.rng() % 8;
    const uint32_t hr = r < 6 ? 0 : r == 6 ? 0x80004005u : 0x8876086cu;
    const auto *handles = Out<const uint32_t>(a[2]);
    if (hr == 0 && a[4]) {
        uint32_t words[12] = {};
        unsigned n = 0;
        const uint32_t first = (handles[0] - W(gStatic.fragmentHandles)) / 4;
        words[n++] = first < kFragmentCount ? kFragments[first].target : 0;
        for (uint32_t i = 0; i < a[3] && n < 10; ++i) words[n++] = 0x100 + (handles[i] - W(gStatic.fragmentHandles)) / 4;
        words[n++] = kEndToken;
        *Out<void *>(a[4]) = NewBuffer(words, n, n * 4);
    }
    if (a[5] && (hr != 0 || gWorld.rng() % 4 == 0)) *Out<void *>(a[5]) = NewTextBuffer("link error");
    return hr;
}

// ---- D3DX stand-ins (__stdcall, at their RVAs) ----
void NoteD3DX(const char *label, std::string text, std::vector<uint32_t> words) { NoteWords(label, std::move(words), std::move(text)); }
std::string Text(const char *s, uint32_t size) { return s ? std::string(s, size) + '\x1f' : std::string("(null)\x1f"); }
std::string Text(const char *s) { return s ? std::string(s) + '\x1f' : std::string("(null)\x1f"); }

HRESULT Make(void **shader, void **errors)
{
    const uint32_t r = gWorld.rng() % 10;
    const HRESULT hr = r < 7 ? 0 : r == 7 ? HRESULT(0x8876086c) : r == 8 ? E_FAIL : HRESULT(0x88760b59);
    if (hr == 0 && shader) *shader = NewProgramBuffer(gWorld.rng() % kProgramCount);
    if (errors && (hr != 0 || gWorld.rng() % 4 == 0)) *errors = NewTextBuffer("error");
    return hr;
}
HRESULT __stdcall FakeAssemble(const char *src, UINT len, const void *defines, void *include, DWORD flags, void **shader,
                               void **errors)
{
    NoteD3DX("D3DXAssembleShader", Text(src, len), {len, N(defines), W(include), flags, shader != nullptr, errors != nullptr});
    return Make(shader, errors);
}
HRESULT __stdcall FakeCompile(const char *src, UINT len, const void *defines, void *include, const char *entry,
                              const char *profile, DWORD flags, void **shader, void **errors, void **table)
{
    NoteD3DX("D3DXCompileShader", Text(src, len) + Text(entry) + Text(profile),
             {len, N(defines), W(include), flags, shader != nullptr, errors != nullptr, W(table)});
    return Make(shader, errors);
}
HRESULT __stdcall FakeGather(const char *src, UINT len, const void *defines, void *include, DWORD flags, void **fragments,
                             void **errors)
{
    NoteD3DX("D3DXGatherFragments", Text(src, len), {len, N(defines), W(include), flags, fragments != nullptr, errors != nullptr});
    const uint32_t r = gWorld.rng() % 10;
    const HRESULT hr = r < 7 ? 0 : r == 7 ? HRESULT(0x8876086c) : E_FAIL;
    if (hr == 0) {
        const uint32_t words[3] = {0xfeed0000 + gWorld.rng() % 4, gWorld.rng() % 3, kEndToken};
        *fragments = NewBuffer(words, 3, 12);
    }
    if (errors && (hr != 0 || gWorld.rng() % 4 == 0)) *errors = NewTextBuffer("gather error");
    return hr;
}
HRESULT __stdcall FakeCreateLinker(void *device, UINT flags, void **linker)
{
    NoteD3DX("D3DXCreateFragmentLinker", {}, {N(device), flags, linker != nullptr});
    // Always an object: the original adds the fragments to it whatever the creation answered.
    if (gWorld.nextLinker >= kLinkers) {
        drawfx::gErrors.push_back("out of linker fakes");
        return E_FAIL;
    }
    drawfx::Fake &l = gLinker[gWorld.nextLinker++];
    l.refs = 1;
    *linker = &l;
    return gWorld.rng() % 8 == 0 ? E_FAIL : 0;
}
HRESULT __stdcall FakeDisassemble(const DWORD *function, BOOL colour, const char *comments, void **text)
{
    NoteD3DX("D3DXDisassembleShader", {}, {N(function), uint32_t(colour), W(comments), text != nullptr});
    const uint32_t r = gWorld.rng() % 6;
    const HRESULT hr = r == 0 ? E_FAIL : r == 1 ? HRESULT(1) : 0;
    if (hr >= 0) *text = NewTextBuffer("; disassembly");
    return hr;
}
HRESULT __stdcall FakeConstantTable(const DWORD *function, void **table)
{
    NoteD3DX("D3DXGetShaderConstantTable", {}, {N(function), table != nullptr});
    TableData &t = gWorld.tables[function[1] % kTables];
    gTable.owner = &t;
    ++gTable.refs;
    *table = &gTable;
    return t.tableHr;
}

void BuildWorld()
{
    gWorld.rng.seed(gWorld.seed * 7919 + 11);
    gWorld.nextBuffer = gWorld.nextLinker = gWorld.nextVertex = gWorld.nextPixel = 0;
    drawfx::Fake *devices[2] = {&gDevice, &gDevice2};
    for (unsigned i = 0; i < 2; ++i) {
        drawfx::Fake &d = *devices[i];
        d.Reset<kDevice>(0x5d001 + i);
        d.AddFace(kIidDeviceDX, d);
        d.Returns(16, &gEnum);
        d.Returns(18, &gResources);
        d.Returns(35, gWorld.ready);
        d.Returns(48, &gD3D);
    }
    gEnum.Reset<kEnum>(0x5d003);
    gEnum.On(20, &HasProcessingCap);
    gEnum.On(21, &GetLimit);
    gResources.Reset<kResources>(0x5d004);
    gD3D.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);
    gD3D.On(fx_d3d9::dev::CreateVertexShader, &CreateVertexShader);
    gD3D.On(fx_d3d9::dev::CreatePixelShader, &CreatePixelShader);
    gD3D.On(fx_d3d9::dev::GetCreationParameters, &GetCreationParameters);
    gGraphics.Reset<kGraphics>(0x5d005);
    gGraphics.Returns(27, &gDeclarations);
    gDeclarations.Reset<kDeclarations>(0x5d006);
    gDeclarations.Returns(6, gStatic.defines);
    gErrorText.Reset<kString>(0x5d007);
    gText.Reset<kString>(0x5d008);
    gStream.Reset<kStream>(0x5d009);
    gTable.Reset<kTable>(0x5d00a);
    gTable.On(5, &TableGetDesc);
    gTable.On(6, &TableGetConstantDesc);
    gTable.On(8, &TableGetConstant);
    gTable.owner = &gWorld.tables[0];
    for (unsigned i = 0; i < kBuffers; ++i) {
        gBuffer[i].Reset<kBuffer>(0x5b000 + i);
        gBuffer[i].owner = &gStatic.buffers[i];
        gBuffer[i].On(3, &BufferPointer);
        gBuffer[i].On(4, &BufferSize);
    }
    for (unsigned i = 0; i < kLinkers; ++i) {
        gLinker[i].Reset<kLinker>(0x5c000 + i);
        gLinker[i].On(6, &LinkerByName);
        gLinker[i].On(7, &LinkerDesc);
        gLinker[i].On(8, &LinkerAdd);
        gLinker[i].On(11, &LinkerLink);
    }
    for (unsigned i = 0; i < kShaders; ++i) {
        gVertex[i].Reset<fx_d3d9::IDirect3DVertexShader9>(0x5e000 + i);
        gPixel[i].Reset<fx_d3d9::IDirect3DPixelShader9>(0x5f000 + i);
    }
}

// ---- the pools (0x10ca2fc / 0x10ca308, RZFastFixed0x28: 0x1000-byte pages), fresh for each side ----
constexpr uint32_t kPoolRvas[2] = {cVertexShaderDX::kPoolRva, cPixelShaderDX::kPoolRva};
constexpr uint32_t kPageBytes = 0x1000, kBlockBytes = 0x28;
constexpr unsigned kPoolPages = 4;
struct Pool {
    uint8_t *region = nullptr;
    unsigned next = 0;
    uint32_t saved[3] = {};
    void *vt[4] = {};
    std::vector<uint32_t> freed;
} gPools[2];

unsigned PoolOf(void *self) { return self == GameAddress<void>(kPoolRvas[1]) ? 1 : 0; }
RZFixedPage *InitPage(Pool &pool)
{
    auto *page = reinterpret_cast<RZFixedPage *>(pool.region + (pool.next++ % kPoolPages) * kPageBytes);
    page->next = page;
    page->prev = page;
    page->used = 0;
    uint8_t *base = reinterpret_cast<uint8_t *>(page);
    const uint32_t blocks = (kPageBytes - 0x10) / kBlockBytes;
    for (uint32_t k = 0; k < blocks; ++k)
        *reinterpret_cast<void **>(base + 0x10 + k * kBlockBytes) =
            k + 1 < blocks ? base + 0x10 + (k + 1) * kBlockBytes : nullptr;
    page->freeList = base + 0x10;
    return page;
}
RZFixedPage *__fastcall FakeNewPage(void *self, void *) { return InitPage(gPools[PoolOf(self)]); }
void __fastcall FakeFreePage(void *self, void *, RZFixedPage *page)
{
    Pool &pool = gPools[PoolOf(self)];
    pool.freed.push_back(uint32_t(reinterpret_cast<uint8_t *>(page) - pool.region));
}
void *__fastcall PoolAllocTrap(void *, void *)
{
    drawfx::gErrors.push_back("a pool's vtable slot 0 was called");
    return nullptr;
}
void __fastcall PoolFreeTrap(void *, void *, void *) { drawfx::gErrors.push_back("a pool's vtable slot 1 was called"); }
struct PoolScope {
    PoolScope()
    {
        for (unsigned i = 0; i < 2; ++i) {
            Pool &pool = gPools[i];
            pool.vt[0] = reinterpret_cast<void *>(&PoolAllocTrap);
            pool.vt[1] = reinterpret_cast<void *>(&PoolFreeTrap);
            pool.vt[2] = reinterpret_cast<void *>(&FakeNewPage);
            pool.vt[3] = reinterpret_cast<void *>(&FakeFreePage);
            pool.region = static_cast<uint8_t *>(
                VirtualAlloc(nullptr, kPoolPages * kPageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            std::memcpy(pool.saved, GameAddress<void>(kPoolRvas[i]), sizeof(pool.saved));
        }
    }
    ~PoolScope()
    {
        for (unsigned i = 0; i < 2; ++i) {
            std::memcpy(GameAddress<void>(kPoolRvas[i]), gPools[i].saved, sizeof(gPools[i].saved));
            VirtualFree(gPools[i].region, 0, MEM_RELEASE);
        }
    }
    PoolScope(const PoolScope &) = delete;
    static void Reset()
    {
        for (unsigned i = 0; i < 2; ++i) {
            Pool &pool = gPools[i];
            pool.next = 0;
            pool.freed.clear();
            std::memset(pool.region, 0xcd, kPoolPages * kPageBytes);
            uint32_t *words = GameAddress<uint32_t>(kPoolRvas[i]);
            words[0] = W(pool.vt);
            words[1] = W(InitPage(pool));
            words[2] = 0;
        }
    }
    static void Register()
    {
        drawfx::AddRange(gPools[0].region, kPoolPages * kPageBytes, 0x7a000000);
        drawfx::AddRange(gPools[1].region, kPoolPages * kPageBytes, 0x7b000000);
    }
    static void Note()
    {
        for (unsigned i = 0; i < 2; ++i) {
            const Pool &pool = gPools[i];
            const uint32_t *words = GameAddress<uint32_t>(kPoolRvas[i]);
            std::vector<uint32_t> w{drawfx::Norm(words[1]), drawfx::Norm(words[2]), pool.next};
            for (unsigned k = 0; k < pool.next && k < kPoolPages; ++k) {
                const auto *page = reinterpret_cast<const RZFixedPage *>(pool.region + k * kPageBytes);
                w.insert(w.end(), {N(page->next), N(page->prev), N(page->freeList), page->used});
            }
            w.insert(w.end(), pool.freed.begin(), pool.freed.end());
            NoteWords("pool", std::move(w));
        }
    }
};

// ---- one side ----
struct Side {
    alignas(16) uint8_t storage[sizeof(cShaderProgramsManagerDX)];
    cShaderProgramsManagerDX *m = nullptr;
    bool original = false;
    std::vector<void *> programs; // held (counted)
    std::vector<void *> inputs;   // held (counted)
};

// A fragment name: its place among the interned names (0xa0 | place), or its text's.
std::string NameText(const cRZString *s)
{
    for (unsigned i = 0; i < kInternedCount; ++i)
        if (s == Interned(i)) return "#" + std::to_string(i) + '\x1f';
    return s->mString + '\x1f';
}

template <class Shader> void ProgramWords(std::vector<uint32_t> &w, std::string &text, const Shader *p)
{
    uint32_t info[2];
    std::memcpy(info, &p->mInfo, sizeof(info));
    w.insert(w.end(), {N(p), p->mRefs, N(p->mDevice), N(p->mShader), N(p->mFunction), info[0], info[1],
                       uint32_t(p->mNames.size())});
    for (const auto &n : p->mNames) text += NameText(n.p);
    text += '|';
}

void NoteState(const Side &s)
{
    const cShaderProgramsManagerDX *m = s.m;
    std::vector<uint32_t> w{m->mLinking, uint32_t(m->mFragments.size()), uint32_t(m->mNames.size()),
                            uint32_t(m->mLink.size()), uint32_t(m->mVertex.size()), uint32_t(m->mPixel.size())};
    std::string text;
    for (const auto *l : m->mLinkers.p) w.push_back(N(l));
    for (const auto &f : m->mFragments) w.push_back(N(f.p));
    for (const cRZString *n : m->mNames) text += NameText(n);
    text += '|';
    for (const auto &n : m->mLink) text += NameText(n.p);
    text += '|';
    for (const cVertexShaderDX *p : m->mVertex) ProgramWords(w, text, p);
    for (const cPixelShaderDX *p : m->mPixel) ProgramWords(w, text, p);
    for (const void *p : s.programs) w.insert(w.end(), {N(p), static_cast<const cVertexShaderDX *>(p)->mRefs});
    for (const void *p : s.inputs) w.push_back(static_cast<const cShaderProgramInputsDX *>(p)->mRefs);
    for (unsigned i = 0; i < kInternedCount; ++i) w.push_back(Interned(i)->mRefCount);
    for (const drawfx::Fake *f : drawfx::gFakes) w.push_back(uint32_t(f->refs));
    NoteWords("state", std::move(w), std::move(text));
}

// The test's names, in the manager's set (counted as the set counts its own).
void Intern(Side &s)
{
    for (unsigned i = 0; i < kInternedCount; ++i)
        if (s.m->mNames.insert(Interned(i)).second) Interned(i)->AddRef();
}

struct Seen {
    unsigned vertex = 0, pixel = 0, shared = 0, linked = 0, linkFound = 0, unknown = 0, duplicate = 0, refused = 0,
             software = 0, version = 0, failed = 0, disassembled = 0, dumps = 0, resets = 0, samplers = 0,
             constants = 0, released = 0, notReady = 0, noFragments = 0, newName = 0, reports = 0, errorText = 0,
             interned = 0, made = 0;
};

const char *const kNames[] = {"vsA", "vsB", "vsX", "vs3", "vsSW", "psA", "psB", "psX", "ps3", "vs3SW", "ghost", "nope"};

void Hold(Side &s, void *program, void *inputs)
{
    if (program) s.programs.push_back(program);
    if (inputs) s.inputs.push_back(inputs);
}

void NoteConstant(const char *label, const cShaderConstant &c)
{
    uint32_t words[11];
    std::memcpy(words, &c, sizeof(words));
    words[0] = c.name != nullptr;
    // FindConstant's "none" (0x175580): the original zeroes the counts but copies registerSet, type and shape (+4,
    // +0x10, +0x14) from a local it never set, so those are stack contents on its side; not compared.
    if (!c.name && std::strcmp(label, "FindConstant") == 0) words[1] = words[4] = words[5] = 0;
    NoteWords(label, std::vector<uint32_t>(words, words + 11), c.name ? std::string(c.name) : std::string());
}
uint32_t SamplerWritten(const char *name)
{
    for (const TableData &t : gWorld.tables)
        for (const ConstantData &c : t.c)
            if (c.desc.RegisterSet == 3 && std::strcmp(c.desc.Name, name) == 0) return (std::min<uint32_t>)(c.registers, 16);
    return 0;
}
void NoteSampler(const char *label, const cShaderSampler &c)
{
    std::vector<uint32_t> w{c.name != nullptr, c.count, c.registers != nullptr};
    if (c.name && c.registers) {
        const uint32_t written = SamplerWritten(c.name);
        w.push_back(written);
        w.insert(w.end(), c.registers, c.registers + written);
    }
    NoteWords(label, std::move(w), c.name ? std::string(c.name) : std::string());
}

void InputsOp(std::mt19937 &g, Side &s, Seen &seen)
{
    void *in = s.inputs[g() % s.inputs.size()];
    const uint32_t constants = RawSlot<3>(in), samplers = RawSlot<6>(in);
    NoteWords("inputs", {constants, samplers});
    if (s.original) {
        seen.constants += constants != 0;
        seen.samplers += samplers != 0;
    }
    for (uint32_t i = 0; i < constants; ++i) {
        cShaderConstant c;
        RawSlot<5, void *>(in, &c, i);
        NoteConstant("constant", c);
    }
    for (uint32_t i = 0; i < samplers; ++i) {
        cShaderSampler c;
        RawSlot<8, void *>(in, &c, i);
        NoteSampler("sampler", c);
    }
    for (unsigned k = 0; k < 3; ++k) {
        const char *names[] = {"mvp", "world", "light", "color", "a", "b", "fog", "bones", "zzz", "", "s10", "s21", "s32", "s00", "s11"};
        const char *name = names[g() % 15];
        cShaderConstant c;
        std::memset(&c, 0xa5, sizeof(c));
        RawSlot<4, void *>(in, &c, name);
        NoteConstant("FindConstant", c);
        cShaderSampler sm;
        std::memset(&sm, 0xa5, sizeof(sm));
        RawSlot<7, void *>(in, &sm, name);
        NoteSampler("FindSampler", sm);
    }
    const uint32_t iid = Pick(g, {1, kIidShaderProgramInputs, kIidShaderProgram, 0});
    void *out = nullptr;
    const bool r = RawSlot<0, bool>(in, iid, g() % 5 == 0 ? nullptr : &out);
    NoteWords("inputs QueryInterface", {iid, r, out == in});
    if (r && out) NoteWords("result", {RawSlot<2>(out)});
    NoteWords("inputs AddRef", {RawSlot<1>(in), RawSlot<2>(in)});
}

void ProgramOp(std::mt19937 &g, Side &s, Seen &seen)
{
    const size_t at = g() % s.programs.size();
    void *p = s.programs[at];
    const bool created = RawSlot<6, bool>(p);
    switch (g() % 6) {
    case 0: {
        const uint32_t iid = Pick(g, {1, kIidVertexShader, kIidPixelShader, kIidShaderProgram, kIidShaderProgramDX,
                                      kIidShaderResource, kIidD3DVertexShader, kIidD3DPixelShader, 0x1234, 0});
        const bool d3d = iid == kIidD3DVertexShader || iid == kIidD3DPixelShader;
        void *out = nullptr;
        const bool r = RawSlot<0, bool>(p, iid, !d3d && g() % 6 == 0 ? nullptr : &out);
        NoteWords("QueryInterface", {iid, r, out == p ? 1u : N(out)});
        // The Direct3D ids answer the shader uncounted; the others count.
        if (r && out && out == p) NoteWords("result", {RawSlot<2>(out)});
        break;
    }
    case 1: {
        const auto *info = RawSlot<10, const uint32_t *>(p);
        NoteWords("getters", {N(RawSlot<4, void *>(p)), N(RawSlot<5, void *>(p)), created, RawSlot<7>(p),
                              RawSlot<8, bool>(p), RawSlot<9, bool>(p), info[0], info[1], N(RawSlot<11, void *>(p)),
                              N(RawSlot<14, void *>(p))});
        if (created) NoteWords("function", {N(RawSlot<12, void *>(p)), RawSlot<13>(p)});
        break;
    }
    case 2:
        RawSlot<3, void>(p); // ReleaseResource
        if (s.original && created) ++seen.released;
        break;
    case 3: NoteWords("AddRef", {RawSlot<1>(p), RawSlot<2>(p)}); break;
    case 4: {
        if (!created) break;
        void *out = &gText;
        const bool r = RawSlot<11, bool>(s.m, out, p);
        NoteWords("Disassemble", {r});
        if (s.original && r) ++seen.disassembled;
        break;
    }
    default:
        NoteWords("Release", {RawSlot<2>(p)});
        s.programs.erase(s.programs.begin() + at);
        break;
    }
}

drawfx::Fake *Device(std::mt19937 &g) { return g() % 5 == 0 ? &gDevice2 : &gDevice; }
cIGZString *Errors(std::mt19937 &g) { return g() % 3 == 0 ? nullptr : reinterpret_cast<cIGZString *>(&gErrorText); }

void Step(std::mt19937 &g, Side &s, Seen &seen)
{
    const unsigned op = g() % 22;
    const char *text = kTexts[g() % 5];
    const uint32_t size = uint32_t(std::strlen(text)) + (g() % 3 == 0 ? 1 : 0);
    const uint32_t flags = g() % 8;
    if (op <= 2) {
        void *out = reinterpret_cast<void *>(uintptr_t(0x0badf00d));
        const uint32_t r = RawSlot<3>(s.m, Device(g), text, size, &out, Errors(g), flags);
        NoteWords("AssembleProgram", {r, N(out)});
        Hold(s, out, nullptr);
        if (s.original && !r && out) {
            const auto *v = static_cast<const cVertexShaderDX *>(out);
            (v->mRefs > 1 ? seen.shared : v->mInfo.kind ? seen.pixel : seen.vertex)++;
        }
    } else if (op <= 5) {
        void *out = reinterpret_cast<void *>(uintptr_t(0x0badf00d));
        void *inputs = reinterpret_cast<void *>(uintptr_t(0x0badf00d));
        const char *entry = g() % 2 ? "main" : "vs_main";
        const char *profile = Pick(g, {0, 1, 2}) == 0 ? "vs_2_0" : g() % 2 ? "ps_2_0" : "vs_3_0";
        const uint32_t r = RawSlot<4>(s.m, Device(g), entry, profile, text, size, &out, &inputs, Errors(g), flags);
        NoteWords("CompileProgram", {r, N(out), inputs != nullptr});
        Hold(s, out, inputs);
    } else if (op == 6) {
        const uint32_t r = RawSlot<6>(s.m, text, size, Errors(g), flags);
        NoteWords("AddFragments", {r});
    } else if (op <= 10) {
        if (g() % 8 != 0) RawSlot<7, void>(s.m); // BeginLink
        const unsigned n = g() % 8 == 0 ? 0 : 1 + g() % 4;
        for (unsigned i = 0; i < n; ++i) {
            const unsigned k = g() % 16;
            const char *name = k < 12 ? kNames[k] : kNames[k % 3]; // the first ones more often
            if (std::strcmp(name, "nope") == 0 && s.original) ++seen.newName;
            RawSlot<8, void>(s.m, name);
        }
        if (s.original)
            for (const auto &linked : s.m->mLink) {
                const auto *at = reinterpret_cast<const uint8_t *>(linked.p);
                (at >= gStatic.names[0] && at < gStatic.names[kInternedCount] ? seen.interned : seen.made)++;
            }
        NoteWords("IsLinking", {RawSlot<10, bool>(s.m)});
        void *out = reinterpret_cast<void *>(uintptr_t(0x0badf00d));
        void *inputs = reinterpret_cast<void *>(uintptr_t(0x0badf00d));
        drawfx::Fake *device = Device(g);
        cIGZString *errors = Errors(g);
        ZeroStack();
        const uint32_t r = RawSlot<9>(s.m, device, &out, &inputs, errors, flags);
        NoteWords("LinkProgram", {r, N(out), inputs != nullptr});
        Hold(s, out, inputs);
        if (s.original && !r) (inputs ? seen.linked : seen.linkFound)++;
    } else if (op == 11) {
        RawSlot<5, void>(s.m); // Reset
        if (s.original && (s.m->mVertex.size() + s.m->mPixel.size()) > 1) ++seen.resets;
        Intern(s);
    } else if (op == 12) {
        gWorld.ready = !gWorld.ready || g() % 2;
        gDevice.Returns(35, gWorld.ready);
        gDevice2.Returns(35, gWorld.ready);
        if (s.original && !gWorld.ready) ++seen.notReady;
    } else if (op == 13) {
        gWorld.adapter = g() % 6; // all six linker slots
        const uint32_t iid = Pick(g, {1, kIidShaderProgramsManager, 0x1234});
        void *out = nullptr;
        const bool r = RawSlot<0, bool>(s.m, iid, g() % 5 == 0 ? nullptr : &out);
        NoteWords("manager", {iid, r, out == s.m, RawSlot<1>(s.m), RawSlot<2>(s.m), RawSlot<10, bool>(s.m)});
    } else if (op <= 16) {
        if (!s.programs.empty()) ProgramOp(g, s, seen);
    } else if (op == 17) {
        if (!s.inputs.empty()) InputsOp(g, s, seen);
    } else if (op == 18) {
        if (s.inputs.empty()) return;
        const size_t at = g() % s.inputs.size();
        NoteWords("inputs Release", {RawSlot<2>(s.inputs[at])});
        s.inputs.erase(s.inputs.begin() + at);
    } else if (op == 19) {
        RawSlot<12, void>(s.m, &gStream);
        if (s.original) ++seen.dumps;
    } else {
        // A program made again from the same function (an equal one is answered when the key matches).
        void *out = reinterpret_cast<void *>(uintptr_t(0x0badf00d));
        const uint32_t r = RawSlot<3>(s.m, &gDevice, kTexts[0], 5u, &out, Errors(g), flags & 4);
        NoteWords("AssembleProgram again", {r, N(out)});
        Hold(s, out, nullptr);
    }
}

void Case(uint32_t seed, Side &s, Seen &seen, drawfx::Cells &cells)
{
    drawfx::gLog.reserve(2048);
    drawfx::AddRange(&gStatic, sizeof(gStatic), kStaticTag);
    PoolScope::Register();
    for (unsigned i = 0; i < kInternedCount; ++i) {
        Interned(i)->Construct(kInterned[i]);
        Interned(i)->AddRef(); // the test's
    }
    if (s.original) {
        s.m = new (s.storage) cShaderProgramsManagerDX();
        *reinterpret_cast<void **>(s.m) = GameAddress<void>(kManagerVTable);
        cells.Set(kManagerCache, s.m);
    } else {
        s.m = cShaderProgramsManagerDX::Instance();
    }
    Intern(s);
    NoteState(s);
    std::mt19937 g(seed * 31 + 5);
    for (unsigned step = 0; step < 40; ++step) {
        Step(g, s, seen);
        NoteState(s);
    }
    // The release: the test's inputs and programs, the destructor (not freeing), the names' counts after it.
    for (void *in : s.inputs) NoteWords("result", {RawSlot<2>(in)});
    for (void *p : s.programs) NoteWords("result", {RawSlot<2>(p)});
    s.inputs.clear();
    s.programs.clear();
    NoteState(s);
    RawSlot<13, void>(s.m, 0u);
    if (!s.original) new (s.m) cShaderProgramsManagerDX();
    std::vector<uint32_t> w;
    for (unsigned i = 0; i < kInternedCount; ++i) w.push_back(Interned(i)->mRefCount);
    for (const drawfx::Fake *f : drawfx::gFakes) w.push_back(uint32_t(f->refs));
    NoteWords("after", std::move(w));
    PoolScope::Note();
    for (unsigned i = 0; i < kInternedCount; ++i) Interned(i)->Destruct();
}

} // namespace

DIFF_TEST(gz_dx_Shaders, 0x176100)
{
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::CodePatch assemble{0x1e89e, reinterpret_cast<const void *>(&FakeAssemble)};
    shape::CodePatch compile{0x1e981, reinterpret_cast<const void *>(&FakeCompile)};
    shape::CodePatch gather{0x22220, reinterpret_cast<const void *>(&FakeGather)};
    shape::CodePatch linker{0x28c0c, reinterpret_cast<const void *>(&FakeCreateLinker)};
    shape::CodePatch disassemble{0x2168f, reinterpret_cast<const void *>(&FakeDisassemble)};
    shape::CodePatch table{0x1eca8, reinterpret_cast<const void *>(&FakeConstantTable)};

    // The getter: the original with its static's guard set to "initialised" (INT_MIN: the thread-safe static's fast
    // path) hands out its static, every time; ours one object, every time, carrying our vtable.
    {
        using Getter = void *(__cdecl *)();
        int32_t *const guard = ctx.image.At<int32_t>(kManagerGuard);
        const int32_t savedGuard = *guard;
        *guard = INT_MIN;
        const Getter getter = ctx.Original<Getter>(cShaderProgramsManagerDX::kGetterRva);
        void *const a = getter(), *const b = getter();
        *guard = savedGuard;
        ++ctx.cases;
        if (a != b || a != static_cast<void *>(ctx.image.At<uint8_t>(kManagerStatic)))
            ctx.Fail("original getter: %p %p", a, b);
        void *const ours = cShaderProgramsManagerDX::Instance();
        ++ctx.cases;
        if (ours != cShaderProgramsManagerDX::Instance() || *static_cast<void **>(ours) == GameAddress<void>(kManagerVTable))
            ctx.Fail("Instance: not one object with our vtable");
    }

    gStatic.defines[0] = "SEMANTIC";
    gStatic.defines[1] = "TEXCOORD0";
    PoolScope pool;
    d3dfx::ErrorLog errors(ctx);
    errors.Answer(kGraphicServiceId, kGraphicServiceIid, gGraphics);
    Seen seen;
    for (uint32_t seed = 1; seed <= 1200; ++seed) {
        auto side = [&](bool original) {
            SeedWorld(seed);
            PoolScope::Reset();
            Side s;
            s.original = original;
            drawfx::Cells cells;
            auto [log, errs] = drawfx::Side([&] { BuildWorld(); }, [&] { Case(seed, s, seen, cells); });
            return std::make_pair(std::move(log), std::move(errs));
        };
        const auto a = side(true);
        const auto b = side(false);
        for (const drawfx::Call &c : a.first) {
            if (!c.iface && std::strcmp(c.note, "LogD3DError") == 0) ++seen.reports;
            if (c.iface == &kString && (c.slot == 3 || c.slot == 4)) {
                ++seen.errorText;
                if (c.text.find("Unknown fragment") != std::string::npos) ++seen.unknown;
                if (c.text.find("Duplicate") != std::string::npos || c.text.find("Unknown error") != std::string::npos)
                    ++seen.duplicate;
                if (c.text.find("Software vertex") != std::string::npos ||
                    c.text.find("Software processing") != std::string::npos)
                    ++seen.software;
                if (c.text.find("version not supported") != std::string::npos) ++seen.version;
                if (c.text.find("Failed Create") != std::string::npos) ++seen.failed;
                if (c.text.find("No fragments") != std::string::npos) ++seen.noFragments;
            }
        }
        std::vector<std::string> errs = a.second;
        errs.insert(errs.end(), b.second.begin(), b.second.end());
        drawfx::Same(ctx, a.first, b.first, "Shaders", seed, errs);
    }
    if (!seen.vertex || !seen.pixel || !seen.shared || !seen.linked || !seen.linkFound || !seen.unknown ||
        !seen.duplicate || !seen.software || !seen.version || !seen.failed || !seen.disassembled || !seen.dumps ||
        !seen.resets || !seen.samplers || !seen.constants || !seen.released || !seen.notReady || !seen.noFragments ||
        !seen.newName || !seen.reports || !seen.errorText || !seen.interned || !seen.made)
        ctx.Fail("Shaders: a path never ran (vertex %u pixel %u shared %u linked %u link found %u unknown %u duplicate "
                 "%u software %u version %u create failed %u disassembled %u dumps %u resets %u samplers %u constants "
                 "%u released %u not ready %u no fragments %u new name %u reports %u error texts %u interned names "
                 "linked %u made names linked %u)",
                 seen.vertex, seen.pixel, seen.shared, seen.linked, seen.linkFound, seen.unknown, seen.duplicate,
                 seen.software, seen.version, seen.failed, seen.disassembled, seen.dumps, seen.resets, seen.samplers,
                 seen.constants, seen.released, seen.notReady, seen.noFragments, seen.newName, seen.reports,
                 seen.errorText, seen.interned, seen.made);
}
