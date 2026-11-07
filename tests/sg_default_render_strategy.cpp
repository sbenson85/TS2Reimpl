// Differential tests for the display list's default render strategy (src/sg/cDefaultDisplayListRenderStrategy.cpp):
// Render (slot 4, 0xaa2350), and the object's life: the constructor 0xa9d450, slots 0-3 (the folded bodies 0x3cab40,
// 0x148fb0, 0x2c1170 and 0x300a0 through the game's vtable 0xe754e0) and the deleting destructor 0xa9e560 (0xa9e080).
//
// The world: a pass (the display list's Pass) whose device state, device, handle texture and items' variants are svcfx
// fakes, and whose viewer is on a hand-made vtable (slot 104 is past the fakes' 80). Compared: every call in order with
// its arguments (the state commands by their bytes; the clear's colour as the viewer left it), the pass and the items
// after, and the object's words, results and frees.
//
// Masked: the viewport command's last two words (+0x18 / +0x1c): the original never writes them (its stack as it was),
// ours zeroes them.
#include "free_recorder.h"
#include "sg_display_list_world.h"

#include "sg/cDefaultDisplayListRenderStrategy.h"
#include "sg/cIViewerRefNode.h"

using nGZSceneGraph::cDefaultDisplayListRenderStrategy;
using nGZSceneGraph::cViewerClearInfo;

namespace {

constexpr uint32_t kStrategyVTable = 0xe754e0;
constexpr unsigned kState = 10, kDevice2 = 11, kHandle = 12, kVariant0 = 13, kVariants = 4;
constexpr uint32_t evViewer = 0x7600, evCommand = 0x5c00, evClear = 0x5d00;
constexpr uint32_t kPassTag = 0x71000000, kItemsTag = 0x72000000;
constexpr unsigned kMaxItems = 8;

alignas(16) Pass gPass;
DrawItem gItems[kMaxItems];

// The viewer (slot 104 by hand): logs the colour it was given, then writes what the seed says.
struct FakeViewer {
    void **vt;
    uint32_t flags, color[4], depth, stencil;
    bool writeColor;
};
FakeViewer gViewer;
void *gViewerVTable[105];

void __fastcall ViewerClearInfo(FakeViewer *self, void *, cViewerClearInfo *out)
{
    svcfx::Ev(evViewer + 104);
    svcfx::Ev(svcfx::Norm(svcfx::P(out)));
    uint32_t color[4];
    std::memcpy(color, out->mColor, sizeof color);
    for (uint32_t word : color) svcfx::Ev(word);
    out->mFlags = self->flags;
    if (self->writeColor) std::memcpy(out->mColor, self->color, sizeof self->color);
    std::memcpy(&out->mDepth, &self->depth, 4);
    out->mStencil = self->stencil;
}

// The device state's ApplyStateCommand (12): the command's header and words (the viewport's +0x18 / +0x1c masked).
uint32_t StateCommand(svcfx::Obj *, uint32_t *args)
{
    const auto *words = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(args[0]));
    svcfx::Ev(evCommand);
    svcfx::Ev(words[0] - svcfx::P(GameAddress<void>(0))); // the vtable as an RVA
    svcfx::Ev(words[1] & 0xffff00ffu); // id and size (the pad byte between them is the stack's in the original)
    const uint32_t size = words[1] >> 16;
    const uint32_t shown = size == 0x20 ? 0x18 : size;
    for (uint32_t off = 8; off < shown && off < 0x20; off += 4) svcfx::Ev(words[off / 4]);
    return 0;
}

// The device's Clear (27): flags, the colour's words, z, stencil.
uint32_t DeviceClear(svcfx::Obj *, uint32_t *args)
{
    svcfx::Ev(evClear);
    svcfx::Ev(args[0]);
    const auto *color = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(args[1]));
    for (unsigned i = 0; i < 4; ++i) svcfx::Ev(color[i]);
    svcfx::Ev(args[2]);
    svcfx::Ev(args[3]);
    return 0;
}

uint32_t RandomFloat(std::mt19937 &gen)
{
    const float values[] = {0.0f, 1.0f, 0.5f, -2.25f, 1e30f, 0.125f};
    float f = values[gen() % 6];
    if (gen() % 3 == 0) f = static_cast<float>(gen() % 10000) / 77.0f;
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return gen() % 11 == 0 ? 0x7fc00000u + gen() % 16 : w; // NaNs as they are
}

// A world: the fakes, the viewer, the pass and `count` items.
uint32_t MakeWorld(std::mt19937 &gen)
{
    svcfx::ResetAll();
    svcfx::F(kState).Set(9, "d");
    svcfx::F(kState).Set(12, "x", 0, &StateCommand); // logged by StateCommand
    svcfx::F(kState).Set(25, "p");
    svcfx::F(kDevice2).Set(27, "xxxx", 0, &DeviceClear); // logged by DeviceClear
    svcfx::F(kHandle).Set(12, "pd");
    for (unsigned i = 0; i < kVariants; ++i) svcfx::F(kVariant0 + i).Set(11, "pp");

    gViewerVTable[104] = reinterpret_cast<void *>(&ViewerClearInfo);
    gViewer.vt = gViewerVTable;
    gViewer.flags = gen() % 3 == 0 ? gen() : gen() % 4;
    for (uint32_t &w : gViewer.color) w = RandomFloat(gen);
    gViewer.depth = RandomFloat(gen);
    gViewer.stencil = gen() % 2 ? gen() : gen() % 256;
    gViewer.writeColor = gen() % 2 != 0;

    gPass = Pass();
    auto *bytes = reinterpret_cast<uint8_t *>(&gPass);
    // Random words everywhere the strategy may read, then the members it calls.
    for (uint32_t off = 0x84; off < 0x9c; off += 4) {
        const uint32_t w = RandomFloat(gen);
        std::memcpy(bytes + off, &w, 4);
    }
    for (float &f : gPass.mMatrices[0]) {
        const uint32_t w = RandomFloat(gen);
        std::memcpy(&f, &w, 4);
    }
    for (uint32_t &w : gPass.mDrawStats) w = gen() % 3 ? gen() % 1000 : gen();
    gPass.mViewer = reinterpret_cast<nGZSceneGraph::cIViewerRefNode *>(&gViewer);
    gPass.mHandleTexture = gen() % 3 == 0 ? reinterpret_cast<cIGZUnknown *>(&svcfx::F(kHandle)) : nullptr;
    gPass.mDevice = reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&svcfx::F(kDevice2));
    gPass.mDeviceState = reinterpret_cast<nGZGraphic4::cDeviceStateDX *>(&svcfx::F(kState));
    svcfx::AddRange(&gPass, sizeof gPass, kPassTag);
    svcfx::AddRange(gItems, sizeof gItems, kItemsTag);

    const uint32_t count = gen() % 5 == 0 ? 0 : gen() % (kMaxItems + 1);
    for (DrawItem &item : gItems) {
        auto *w = reinterpret_cast<uint32_t *>(&item);
        for (unsigned i = 0; i < sizeof(DrawItem) / 4; ++i) w[i] = gen();
        item.mMaterial = &svcfx::F(kVariant0 + gen() % kVariants);
    }
    return count;
}

void CaptureWorld(Blob &o)
{
    o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    const auto *pass = reinterpret_cast<const uint32_t *>(&gPass);
    for (uint32_t off = 0; off < sizeof(Pass); off += 4) {
        // The pass's vectors: their words are the test's (the strategy never reaches them).
        if (off >= offsetof(Pass, mEntriesA) && off < offsetof(Pass, mNodes) + 12) continue;
        o.push_back(pass[off / 4]);
    }
    const auto *items = reinterpret_cast<const uint32_t *>(gItems);
    o.insert(o.end(), items, items + sizeof gItems / 4);
}

using RenderFn = void(__thiscall *)(void *, Pass *, DrawItem *, uint32_t);

} // namespace

// 0xaa2350: Render on random passes (a handle texture or none; random viewport and depth fractions, NaNs included;
// projection words; draw stats) with 0-8 items on four variants, the viewer's clear info random (any flags, the colour
// written or left at the preset). Every call with its arguments, the pass and the items after.
DIFF_TEST(sg_default_render_strategy_render, 0xaa2350)
{
    Fixture fixture;
    auto original = ctx.Original<RenderFn>(0xaa2350);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 2654435761u + 0xaa2350u);
            const uint32_t count = MakeWorld(gen);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) {
                uint32_t object[2] = {svcfx::P(shape::GameVTable(kStrategyVTable)), 1};
                original(object, &gPass, gItems, count);
            } else {
                cDefaultDisplayListRenderStrategy strategy;
                nGZSceneGraph::cIDisplayListRenderStrategy *const face = &strategy;
                face->Render(&gPass, gItems, count);
            }
            CaptureWorld(outcome[side]);
        }
        Same(ctx, outcome[0], outcome[1], "render", seed);
    }
}

// 0xa9d450 and the object's slots 0-3 and 5: built (the original's constructor on 8 bytes of 0xcd, ours new), then
// random steps through the object's own vtable: QueryInterface (iids 1, 0xbf077fd and others; a null out), AddRef,
// Release (to zero: the object deletes itself), BeginPass (calls nothing), the deleting destructor with flags 0 or 1.
// After each step: the result (the whole eax where the slot returns a word; QueryInterface's al), the out word (the
// object or untouched), the count, every fake call, and whether the object was freed.
DIFF_TEST(sg_default_render_strategy_life, 0xa9d450)
{
    Fixture fixture;
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0xa9d450);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xa9d450u);
            svcfx::ResetAll();
            Blob &o = outcome[side];
            difftest::FreeRecorder frees;
            void *object;
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) {
                object = ::operator new(8);
                std::memset(object, 0xcd, 8);
                o.push_back(construct(object) == object);
                o.push_back(shape::VTableAt(object, 0) == shape::GameVTable(kStrategyVTable));
            } else {
                object = new cDefaultDisplayListRenderStrategy;
                o.push_back(1);
                o.push_back(1);
            }
            const auto count = [&] { return static_cast<const uint32_t *>(object)[1]; };
            o.push_back(count());
            bool alive = true;
            const unsigned steps = 1 + gen() % 10;
            for (unsigned i = 0; i < steps && alive; ++i) {
                const uint32_t op = gen() % 6;
                uint32_t result = 0x5e5e5e5e, out = 0x5e5e5e5e;
                void **const vt = shape::VTableAt(object, 0);
                switch (op) {
                case 0: {
                    const uint32_t iids[] = {1, cDefaultDisplayListRenderStrategy::kIid, 0, 0xbf077fc, 0xbf077fe, gen()};
                    const uint32_t iid = iids[gen() % 6];
                    void **where = gen() % 4 ? reinterpret_cast<void **>(&out) : nullptr;
                    result = reinterpret_cast<bool(__thiscall *)(void *, uint32_t, void **)>(vt[0])(object, iid, where) ? 1 : 0;
                    if (out == svcfx::P(object)) out = 0x0b1ec700;
                    break;
                }
                case 1: result = reinterpret_cast<uint32_t(__thiscall *)(void *)>(vt[1])(object); break;
                case 2:
                case 3: {
                    const uint32_t before = count();
                    result = reinterpret_cast<uint32_t(__thiscall *)(void *)>(vt[2])(object);
                    alive = before != 1;
                    break;
                }
                case 4: reinterpret_cast<void(__thiscall *)(void *, Pass *)>(vt[3])(object, &gPass); break;
                case 5: {
                    if (gen() % 3) continue;
                    const uint32_t flags = gen() % 2;
                    result = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(vt[5])(object, flags) == object;
                    alive = false;
                    if (!(flags & 1)) ::operator delete(object);
                    break;
                }
                }
                o.push_back(0x100 + op);
                o.push_back(result);
                o.push_back(out);
                o.push_back(alive ? count() : 0xdead);
                o.push_back(frees.Freed(object));
            }
            if (alive) {
                frees.Stop();
                reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(shape::VTableAt(object, 0)[5])(object, 1);
            }
            o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
        }
        Same(ctx, outcome[0], outcome[1], "life", seed);
    }
}
