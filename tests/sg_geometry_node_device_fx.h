#pragma once
// devfx: recording fakes for the Direct3D-side graphics interfaces a scenegraph renderable draws through (lane QZ, the first
// call stream of milestone O's acceptance (a); meant for reuse by the other renderables' render tests).
//
//   devfx::World dev;                         // one per test, reset before each side
//   dev.Reset(seed, 800);                     // ids 800.. ; caps, readiness and failures from the seed's choices
//   dev.Plant(cells);                         // the declarations manager's cached global (0x10caa28)
//   graphics GetDevice answers &dev.device    // (the test's graphic-system fake)
//   cGeometryRenderContext *c = dev.Context(); // a render context over the state and the dynamic vertex arrays
//
// The fakes, each a drawfx::Fake (every call logged, strict argument kinds):
//   device (nGZGraphic4::cIDeviceDX): 16 GetDeviceEnumConst, 20 GetStateConst, 26 ProcessVertices, 32 DrawIndexedPrimitive,
//     35 IsDeviceReady (World::ready);
//   caps (cIDeviceEnum): 20 HasProcessingCap(cap): bit `cap` of World::caps;
//   state (cIDeviceState): 9 Reset, 10 CreateStateBlock(groups, out) (a new block, counted, into *out), 11 ApplyStateBlock,
//     16 SetVertexDeclaration, 17 SetStreamSource, 18 SetIndices;
//   declarations manager (cIVertexDeclarationsManagerDX): 3 GetDeclaration(device, format, out): a new declaration of the
//     format, counted, into *out, true; false (nothing stored) when the format is World::refuse or null;
//   declarations (cIVertexDeclarationDX) and state blocks (cIDeviceResource): 6 IsCreated (value[1]), and for declarations
//     11 GetStreamSize(stream) (the format's, asked through its own slot 11), 12 GetFormat (value[0]);
//   dynamic vertex arrays (cIDynamicVertexArray): 15 Lock, 16 Unlock, 20 Reserve(stride, count) (a running start index),
//     21 GetCapacity (value[0]).
// Declarations and blocks are handed out round-robin from fixed pools, so both sides see the same ids; their counts are in
// NoteRefs. Lost devices: World::lose marks every declaration and block made so far not created (IsCreated false).
#include "drawfx.h"

#include "sg/cIGeometryRender.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>

namespace devfx {

using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kDeclarationsManagerCell = 0x10caa28; // GetVertexDeclarationsManagerCached's global (0x18dd70)

template <unsigned N> constexpr std::array<Slot, N> Faces()
{
    std::array<Slot, N> s{};
    for (Slot &x : s) x = {0, Ret::Int, false, "", ""};
    s[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    s[1] = {0, Ret::Int, true, "AddRef", ""};
    s[2] = {0, Ret::Int, true, "Release", ""};
    return s;
}

constexpr std::array<Slot, 36> MakeDeviceSlots()
{
    auto s = Faces<36>();
    s[16] = {0, Ret::Int, true, "GetDeviceEnumConst", ""};
    s[20] = {0, Ret::Int, true, "GetStateConst", ""};
    s[26] = {5, Ret::Int, true, "ProcessVertices", "uuuaa"};
    s[32] = {6, Ret::Int, true, "DrawIndexedPrimitive", "uuuuuu"};
    s[35] = {0, Ret::Int, true, "IsDeviceReady", ""};
    return s;
}
inline constexpr auto kDeviceSlots = MakeDeviceSlots();
inline constexpr drawfx::Iface kDevice{"DeviceDX", 36, kDeviceSlots.data(), false, drawfx::kTestTable};

constexpr std::array<Slot, 21> MakeCapsSlots()
{
    auto s = Faces<21>();
    s[20] = {1, Ret::Int, true, "HasProcessingCap", "u"};
    return s;
}
inline constexpr auto kCapsSlots = MakeCapsSlots();
inline constexpr drawfx::Iface kCaps{"DeviceEnum", 21, kCapsSlots.data(), false, drawfx::kTestTable};

constexpr std::array<Slot, 19> MakeStateSlots()
{
    auto s = Faces<19>();
    s[9] = {1, Ret::Int, true, "Reset", "u"};
    s[10] = {2, Ret::Int, true, "CreateStateBlock", "ua"};
    s[11] = {1, Ret::Int, true, "ApplyStateBlock", "a"};
    s[16] = {1, Ret::Int, true, "SetVertexDeclaration", "a"};
    s[17] = {4, Ret::Int, true, "SetStreamSource", "uauu"};
    s[18] = {1, Ret::Int, true, "SetIndices", "a"};
    return s;
}
inline constexpr auto kStateSlots = MakeStateSlots();
inline constexpr drawfx::Iface kState{"DeviceState", 19, kStateSlots.data(), false, drawfx::kTestTable};

constexpr std::array<Slot, 4> MakeManagerSlots()
{
    auto s = Faces<4>();
    s[3] = {3, Ret::Int, true, "GetDeclaration", "aaa"};
    return s;
}
inline constexpr auto kManagerSlots = MakeManagerSlots();
inline constexpr drawfx::Iface kManager{"VertexDeclarationsManager", 4, kManagerSlots.data(), false, drawfx::kTestTable};

constexpr std::array<Slot, 13> MakeDeclarationSlots()
{
    auto s = Faces<13>();
    s[6] = {0, Ret::Int, true, "IsCreated", ""};
    s[11] = {1, Ret::Int, true, "GetStreamSize", "u"};
    s[12] = {0, Ret::Int, true, "GetFormat", ""};
    return s;
}
inline constexpr auto kDeclarationSlots = MakeDeclarationSlots();
inline constexpr drawfx::Iface kDeclaration{"VertexDeclaration", 13, kDeclarationSlots.data(), false, drawfx::kTestTable};

constexpr std::array<Slot, 7> MakeBlockSlots()
{
    auto s = Faces<7>();
    s[6] = {0, Ret::Int, true, "IsCreated", ""};
    return s;
}
inline constexpr auto kBlockSlots = MakeBlockSlots();
inline constexpr drawfx::Iface kBlock{"StateBlock", 7, kBlockSlots.data(), false, drawfx::kTestTable};

constexpr std::array<Slot, 22> MakeDynamicSlots()
{
    auto s = Faces<22>();
    s[15] = {3, Ret::Int, true, "Lock", "uuu"};
    s[16] = {0, Ret::Int, true, "Unlock", ""};
    s[20] = {2, Ret::Int, true, "Reserve", "uu"};
    s[21] = {0, Ret::Int, true, "GetCapacity", ""};
    return s;
}
inline constexpr auto kDynamicSlots = MakeDynamicSlots();
inline constexpr drawfx::Iface kDynamic{"DynamicVertexArray", 22, kDynamicSlots.data(), false, drawfx::kTestTable};

constexpr unsigned kPool = 24; // declarations and blocks each

struct World {
    Fake device, caps, state, manager;
    Fake declarations[kPool], blocks[kPool];
    Fake vertices[2]; // the context's mVertices, mVerticesB
    Fake target;      // a declaration for the caller's side of Prepare (value[0] its format, set by the test)
    nGZSceneGraph::cGeometryRenderContext context;
    unsigned nextDeclaration = 0, nextBlock = 0;
    uint32_t caps_ = 0;         // HasProcessingCap: bit per cap (0 hardware, 1 software, 2 / 3 the mixed forms)
    bool ready = true;          // IsDeviceReady
    const void *refuse = nullptr; // GetDeclaration's format that has no declaration

    // `firstId`.. : the device, caps, state, manager, the vertex arrays, then the pools.
    void Reset(std::mt19937 &rng, uint32_t firstId);
    void Plant(drawfx::Cells &cells) { cells.Set(kDeclarationsManagerCell, drawfx::P(&manager)); }
    nGZSceneGraph::cGeometryRenderContext *Context() { return &context; }
    // Every declaration and block made so far reports not created (a lost device).
    void Lose()
    {
        for (Fake &f : declarations) f.value[1] = 0;
        for (Fake &f : blocks) f.value[1] = 0;
    }
    void NoteRefs() const;
};

inline World *gWorld = nullptr;

inline uint32_t DeviceCaps(Fake &, const uint32_t *) { return drawfx::P(&gWorld->caps); }
inline uint32_t DeviceState(Fake &, const uint32_t *) { return drawfx::P(&gWorld->state); }
inline uint32_t DeviceReady(Fake &, const uint32_t *) { return gWorld->ready; }
inline uint32_t HasCap(Fake &, const uint32_t *a) { return a[0] < 32 && (gWorld->caps_ >> a[0] & 1); }
inline uint32_t CreateBlock(Fake &, const uint32_t *a)
{
    Fake &block = gWorld->blocks[gWorld->nextBlock++ % kPool];
    block.value[1] = 1;
    ++block.refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &block;
    return 0;
}
inline uint32_t GetDeclaration(Fake &, const uint32_t *a)
{
    if (a[1] == 0 || a[1] == drawfx::P(gWorld->refuse)) return 0; // (no declaration for a missing format)
    Fake &declaration = gWorld->declarations[gWorld->nextDeclaration++ % kPool];
    declaration.value[0] = a[1];
    declaration.value[1] = 1;
    ++declaration.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &declaration;
    return 1;
}
inline uint32_t IsCreated(Fake &self, const uint32_t *) { return self.value[1]; }
inline uint32_t DeclarationFormat(Fake &self, const uint32_t *) { return self.value[0]; }
// The format's own answer (its slot 11, logged as the format's call).
inline uint32_t DeclarationStreamSize(Fake &self, const uint32_t *a)
{
    void *const format = reinterpret_cast<void *>(uintptr_t(self.value[0]));
    using Fn = uint32_t(__thiscall *)(void *, uint32_t);
    return (*reinterpret_cast<Fn *const *>(format))[11](format, a[0]);
}
inline uint32_t Lock(Fake &self, const uint32_t *) { return drawfx::P(&self.value[4]); }
inline uint32_t Reserve(Fake &self, const uint32_t *a)
{
    const uint32_t first = self.value[1];
    self.value[1] += a[1];
    return first;
}
inline uint32_t Capacity(Fake &self, const uint32_t *) { return self.value[0]; }

inline void World::Reset(std::mt19937 &rng, uint32_t firstId)
{
    gWorld = this;
    device.Reset<kDevice>(firstId);
    device.On(16, &DeviceCaps);
    device.On(20, &DeviceState);
    device.On(35, &DeviceReady);
    caps.Reset<kCaps>(firstId + 1);
    caps.On(20, &HasCap);
    state.Reset<kState>(firstId + 2);
    state.On(10, &CreateBlock);
    manager.Reset<kManager>(firstId + 3);
    manager.On(3, &GetDeclaration);
    for (unsigned i = 0; i < 2; ++i) {
        vertices[i].Reset<kDynamic>(firstId + 4 + i);
        vertices[i].On(15, &Lock);
        vertices[i].On(20, &Reserve);
        vertices[i].On(21, &Capacity);
        vertices[i].value[0] = rng() % 4 == 0 ? 0x40 : 0x100000; // capacity in bytes
        vertices[i].value[1] = rng() % 0x100;                    // the next start index
    }
    for (unsigned i = 0; i < kPool; ++i) {
        declarations[i].Reset<kDeclaration>(firstId + 0x10 + i);
        declarations[i].On(6, &IsCreated);
        declarations[i].On(11, &DeclarationStreamSize);
        declarations[i].On(12, &DeclarationFormat);
        blocks[i].Reset<kBlock>(firstId + 0x30 + i);
        blocks[i].On(6, &IsCreated);
    }
    target.Reset<kDeclaration>(firstId + 0x50);
    target.On(6, &IsCreated);
    target.On(11, &DeclarationStreamSize);
    target.On(12, &DeclarationFormat);
    target.value[1] = 1;
    nextDeclaration = nextBlock = 0;
    caps_ = rng() % 16;
    ready = rng() % 8 != 0;
    refuse = nullptr;
    std::memset(&context, 0, sizeof context);
    context.mDevice = reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&device);
    context.mState = reinterpret_cast<nGZGraphic4::cIDeviceState *>(&state);
    context.mVertices = reinterpret_cast<nGZGraphic4::cIDynamicVertexArray *>(&vertices[0]);
    context.mVerticesB = reinterpret_cast<nGZGraphic4::cIDynamicVertexArray *>(&vertices[1]);
    drawfx::AddRange(&context, sizeof context, 0x7e000000);
}

inline void World::NoteRefs() const
{
    drawfx::Call c;
    c.note = "device refs";
    for (const Fake *f : {&device, &caps, &state, &manager, &vertices[0], &vertices[1], &target}) c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : declarations) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : blocks) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
}

} // namespace devfx
