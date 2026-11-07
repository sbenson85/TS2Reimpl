// Differential tests of cApplyRenderTarget (lane TM, scenegraph M3a; src/sg/cApplyRenderTarget.h), in the world of
// tests/sg_shader_attr_fx.h, which this file extends with its own script (the shared one would call Init with random
// pointers and Reset with no texture):
//   - the device state's command recorder masks cViewportState's MinZ and MaxZ (+0x18, +0x1c), which the image leaves
//     as its stack had them;
//   - the context's surface count (+0xa4) is 0 to 4, its outer context (+0x284) null or a block of random words, its
//     device (+0x2a8) a fake noting Clear (slot 27);
//   - the texture manager (0xae8750's cache 0x10f8f94) is a fake: slot 17 gives an id per name ("rtA" 10, "rtB" 11,
//     "plain" 12, "rtC" 13, else -1), 18 and 19 are noted, 23 gives the id's texture: 10 and 13 answer the render target
//     iid (initialized, with 0 to 4 surfaces; 13 never initialized), 12 does not;
//   - Init's first argument is a caps fake whose slot 21 gives a render target count of 0 to 4;
//   - cRZStd's report slot (0x1bfb92) is a stand-in noting the file, line, group, level and the formatted text.
// The script skips what the image would crash on: Apply on a texture target without a render target texture (its
// SendTargets calls the null target), Init and Reset are safe for every name here.
#include "sg_shader_attr_fx.h"

#include "code_patch.h"
#include "rz/cRZStd.h"
#include "sg/cApplyRenderTarget.h"

#include <cstdarg>
#include <string>

namespace {

constexpr uint32_t kTextureManagerCache = 0x10f8f94;
constexpr uint32_t kRenderTargetIid = 0xb96cc22;

void NoteText(uint32_t tag, const std::string &text)
{
    gLog.push_back(tag);
    gLog.push_back(static_cast<uint32_t>(text.size()));
    for (size_t i = 0; i < text.size(); i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, text.data() + i, text.size() - i < 4 ? text.size() - i : 4);
        gLog.push_back(w);
    }
    gLog.push_back(0xeeeeeeee);
}

int32_t __cdecl StandReport(cRZStd *self, const char *group, int32_t level, const char *format, ...)
{
    char buf[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Note({0x77, self->mSecond, static_cast<uint32_t>(level), self->mFlags});
    NoteText(0x78, std::string(self->File()) + "|" + group + "|" + buf);
    return 0;
}

// The commands, as the shared recorder notes them, cViewportState's last two words left out.
void __fastcall RecordCommand(State *, void *, uint32_t command)
{
    const uint8_t *const p = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(command));
    uint32_t vt;
    uint16_t size;
    std::memcpy(&vt, p, 4);
    std::memcpy(&size, p + 6, 2);
    const uint32_t rva = vt - static_cast<uint32_t>(gImageBase);
    std::vector<uint32_t> w{12, rva, p[4], size};
    const uint32_t end = rva == 0xd9c5d0u ? 0x18u : size; // cViewportState
    for (uint32_t off = 8; off + 4 <= end && off < 0x100; off += 4) {
        uint32_t v;
        std::memcpy(&v, p + off, 4);
        w.push_back(v);
    }
    gLog.insert(gLog.end(), w.begin(), w.end());
    gLog.push_back(0xeeeeeeee);
}

// ---- the device, caps, texture manager, textures and render targets ----
struct Fake {
    void **vt;
};
void __fastcall FakeTrap(Fake *, void *) { Note({0xdeab}); }

void *gDeviceVTable[50];
Fake gDevice{gDeviceVTable};
uint8_t *gPart; // the part whose Apply runs (Clear's colour should be its +0x28)
void __fastcall DeviceClear(Fake *, void *, uint32_t flags, const float *color, float z, uint32_t stencil)
{
    Note({27, flags, uint32_t(reinterpret_cast<const uint8_t *>(color) == gPart + 0x28), Bits(color[0]),
          Bits(color[1]), Bits(color[2]), Bits(color[3]), Bits(z), stencil});
}

void *gCapsVTable[24];
Fake gCaps{gCapsVTable};
uint32_t gMaxTargets;
uint32_t __fastcall CapsTargets(Fake *, void *, uint32_t which)
{
    Note({21, which});
    return gMaxTargets;
}

void *gTargetVTable[16];
struct Target {
    void **vt;
    uint32_t id;
    bool initialized;
    uint32_t surfaces;
};
Target gTargets[2] = {{gTargetVTable, 10}, {gTargetVTable, 13}};
uint32_t __fastcall TargetRelease(Target *self, void *)
{
    Note({0x82, self->id});
    return 7;
}
uint32_t __fastcall TargetInitialized(Target *self, void *)
{
    Note({0x86, self->id});
    return self->initialized ? 0xabcd01u : 0xabcd00u; // only al is the answer
}
uint32_t __fastcall TargetSurfaces(Target *self, void *)
{
    Note({0x87, self->id});
    return self->surfaces;
}
void __fastcall TargetBind(Target *self, void *, void *state, uint32_t index)
{
    Note({0x8c, self->id, uint32_t(state == &gState), index});
}

void *gTextureVTable[16];
struct Texture {
    void **vt;
    uint32_t id;
    Target *target;
};
Texture gTextures[3] = {{gTextureVTable, 10, &gTargets[0]}, {gTextureVTable, 12, nullptr}, {gTextureVTable, 13, &gTargets[1]}};
uint32_t __fastcall TextureQuery(Texture *self, void *, uint32_t iid, void **out)
{
    Note({0x90, self->id, iid});
    if (iid != kRenderTargetIid || !self->target) return 0x5500u; // al zero
    *out = self->target;
    return 1;
}
void __fastcall TextureRestore(Texture *self, void *) { Note({0x93, self->id}); }
void __fastcall TextureReleaseResource(Texture *self, void *) { Note({0x94, self->id}); }

void *gManagerFakeVTable[30];
Fake gTextureManager{gManagerFakeVTable};
uint32_t __fastcall ManagerFind(Fake *, void *, const char *name)
{
    NoteText(0x97, name);
    const std::string n = name;
    if (n == "rtA") return 10;
    if (n == "rtB") return 11;
    if (n == "plain") return 12;
    if (n == "rtC") return 13;
    return 0xffffffffu;
}
void __fastcall ManagerAddRef(Fake *, void *, uint32_t id) { Note({0x98, id}); }
void __fastcall ManagerRelease(Fake *, void *, uint32_t id) { Note({0x99, id}); }
void *__fastcall ManagerTexture(Fake *, void *, uint32_t id)
{
    Note({0x9a, id});
    switch (id) {
    case 10: return &gTextures[0];
    case 11: return &gTextures[1]; // "rtB": a texture with no render target side (as "plain")
    case 12: return &gTextures[1];
    case 13: return &gTextures[2];
    default: return nullptr;
    }
}

alignas(16) uint8_t gOuter[0x100];

void RenderTargetWorld(std::mt19937 &rng)
{
    gStateVTable[12] = reinterpret_cast<void *>(&RecordCommand);
    const uint32_t count = rng() % 5;
    std::memcpy(gContext + 0xa4, &count, 4);
    for (uint8_t &b : gOuter) b = static_cast<uint8_t>(rng());
    const void *const outer = rng() % 2 ? gOuter : nullptr;
    std::memcpy(gContext + 0x284, &outer, 4);
    const Fake *const device = &gDevice;
    std::memcpy(gContext + 0x2a8, &device, 4);
    for (void *&v : gDeviceVTable) v = reinterpret_cast<void *>(&FakeTrap);
    gDeviceVTable[27] = reinterpret_cast<void *>(&DeviceClear);
    for (void *&v : gCapsVTable) v = reinterpret_cast<void *>(&FakeTrap);
    gCapsVTable[21] = reinterpret_cast<void *>(&CapsTargets);
    gMaxTargets = rng() % 5;
    for (void *&v : gTargetVTable) v = reinterpret_cast<void *>(&FakeTrap);
    gTargetVTable[2] = reinterpret_cast<void *>(&TargetRelease);
    gTargetVTable[6] = reinterpret_cast<void *>(&TargetInitialized);
    gTargetVTable[7] = reinterpret_cast<void *>(&TargetSurfaces);
    gTargetVTable[12] = reinterpret_cast<void *>(&TargetBind);
    gTargets[0].initialized = rng() % 4 != 0;
    gTargets[0].surfaces = rng() % 5;
    gTargets[1].initialized = false;
    gTargets[1].surfaces = rng() % 5;
    for (void *&v : gTextureVTable) v = reinterpret_cast<void *>(&FakeTrap);
    gTextureVTable[0] = reinterpret_cast<void *>(&TextureQuery);
    gTextureVTable[3] = reinterpret_cast<void *>(&TextureRestore);
    gTextureVTable[4] = reinterpret_cast<void *>(&TextureReleaseResource);
    for (void *&v : gManagerFakeVTable) v = reinterpret_cast<void *>(&FakeTrap);
    gManagerFakeVTable[17] = reinterpret_cast<void *>(&ManagerFind);
    gManagerFakeVTable[18] = reinterpret_cast<void *>(&ManagerAddRef);
    gManagerFakeVTable[19] = reinterpret_cast<void *>(&ManagerRelease);
    gManagerFakeVTable[23] = reinterpret_cast<void *>(&ManagerTexture);
}

// The world for a test: the extra parts, the texture manager in its cache, the report stand-in.
struct World {
    void **cache;
    void *saved;
    shape::CodePatch report;
    explicit World(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(kTextureManagerCache)), saved(*cache),
          report(0x1bfb92, reinterpret_cast<const void *>(&StandReport))
    {
        gImageBase = ctx.image.Base();
        gWorldExtra = &RenderTargetWorld;
        *cache = &gTextureManager;
    }
    ~World()
    {
        *cache = saved;
        gWorldExtra = nullptr;
    }
};

const char *const kNames[] = {"rtA", "rtB", "plain", "rtC", "missing"};

uint32_t PickSurface(std::mt19937 &rng)
{
    static const uint32_t picks[] = {0, 1, 2, 3, 4, 5, 0xffffffffu, 0xfffffffeu};
    return picks[rng() % 8];
}

// One of the setters (slots 14 to 19) with drawn values.
void SetParts(void *p, std::mt19937 &rng)
{
    switch (rng() % 7) {
    case 0: Slot<void>(p, 14, kNames[rng() % 5], PickSurface(rng)); break;
    case 1:
    case 2: Slot<void>(p, 15, static_cast<uint32_t>(rng() % 4), PickSurface(rng)); break;
    case 3: Slot<void>(p, 16, static_cast<uint32_t>(rng() % 3 ? 0 : (rng() & 0xff) | 1)); break;
    case 4: Slot<void>(p, 17, static_cast<uint32_t>(rng() % 5 | (rng() & 0xffffff00))); break;
    case 5: {
        const float color[4] = {PickFloat(rng), PickFloat(rng), PickFloat(rng), PickFloat(rng)};
        Slot<void>(p, 18, static_cast<uint32_t>(rng() % 2), color);
        break;
    }
    default: Slot<void>(p, 19, static_cast<uint32_t>(rng() % 2), PickFloat(rng), static_cast<uint32_t>(rng() % 3 ? rng() % 256 : rng()));
        break;
    }
}

const Leaf &RenderTargetLeaf()
{
    static const Leaf leaf = {
        "cApplyRenderTarget", 0xe76c00, 0xac31a0, 0xabd810, 0xabe230, 20,
        {{2, 0xac6060}, {3, 0xac6720}, {8, 0xac3f20}, {9, 0x1b9a50}, {10, 0xac2b00}, {11, 0xabf490}, {12, 0x1b9a50},
         {13, 0xac6740}, {14, 0xac6440}, {15, 0xac6420}, {16, 0x411b90}, {17, 0x1b9690}, {18, 0xac61f0},
         {19, 0xac6210}, {20, 0xabe950}, {21, 0xac3540}, {22, 0xac5350}},
        reinterpret_cast<Factory>(&cApplyRenderTarget::Create), sizeof(cApplyRenderTarget), &SetParts, 0xffffffffu,
        [](void *p) { new (p) cApplyRenderTarget; },
        [](void *p) { static_cast<cApplyRenderTarget *>(p)->~cApplyRenderTarget(); }};
    return leaf;
}

// Whether Apply is safe for the image: a texture target needs a render target texture (ids 10 and 13).
bool ApplySafe(const void *p)
{
    uint32_t id, kind;
    std::memcpy(&id, static_cast<const uint8_t *>(p) + 0x10, 4);
    std::memcpy(&kind, static_cast<const uint8_t *>(p) + 0x14, 4);
    return kind < 2 || id == 10 || id == 13;
}

// Init with the caps fake, random key and table words, and a report word whose low byte is drawn.
void NoteInit(void *p, std::mt19937 &rng)
{
    const uint32_t report = (rng() & 0xffffff00u) | (rng() % 3 ? 1u + rng() % 255 : 0u);
    const bool r = Slot<bool>(p, 8, static_cast<void *>(&gCaps), rng(), rng(), report);
    Note({0x66, uint32_t(r), Slot<uint32_t>(p, 6)});
}

} // namespace

// The factory 0xac31a0 and every slot on two parts: the setters 0xac6440 0xac6420 (and the folded 0x411b90 0x1b9690)
// 0xac61f0 0xac6210, Init 0xac3f20 (every failure line), Slot10 0xac2b00, Apply 0xabf490 (and through it SendTargets
// 0xac1c40 and SendViewport 0xac22c0), EndState 0xac6740, Reset 0xac6720, HashParams 0xac3540 and EqualParams
// 0xac5350 (on twins one setter call apart), IsEquivalent, and the last Release through the deleting destructor 0xabe950
// (the handle released by the destructor 0xabe230).
DIFF_TEST(sg_apply_render_target, 0xac31a0)
{
    World world(ctx);
    const Leaf &l = RenderTargetLeaf();
    CheckSides(ctx, l.name, 1200, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const a = make();
        void *const b = make();
        if (original && !CheckVTable(ctx, l, a)) return;
        Slot<uint32_t>(a, 1);
        Slot<uint32_t>(b, 1);
        NoteFields(a, l, 0x41);
        gPart = static_cast<uint8_t *>(a);
        for (int k = 0; k < 16; ++k) {
            switch (rng() % 10) {
            case 0:
            case 1: SetParts(a, rng); break;
            case 2: SetParts(b, rng); break;
            case 3: NoteInit(a, rng); break;
            case 4:
                if (ApplySafe(a)) {
                    Slot<void>(a, 11, static_cast<void *>(gContext), static_cast<void *>(gItem));
                    Note({0x61});
                }
                break;
            case 5:
                Slot<void>(a, 13, static_cast<void *>(gContext));
                Note({0x63});
                break;
            case 6: Note({0x60, Slot<uint32_t>(a, 10)}); break;
            case 7:
                Slot<void>(a, 3);
                Note({0x62});
                break;
            case 8:
                NoteInit(b, rng);
                Note({0x67, uint32_t(Slot<bool>(a, 7, b)), Slot<uint32_t>(a, 21), uint32_t(Slot<bool>(a, 22, b))});
                break;
            default:
                Slot<void>(a, 9, static_cast<void *>(&gState), static_cast<void *>(gContext));
                Slot<void>(a, 12, static_cast<void *>(&gState), static_cast<void *>(gItem));
                Note({0x64});
                break;
            }
        }
        NoteFields(a, l, 0x42);
        NoteFields(b, l, 0x42);
        // Twins: the same setter calls on two fresh parts, then (two times in three) one more on the second.
        for (int t = 0; t < 4; ++t) {
            void *const x = make();
            void *const y = make();
            Slot<uint32_t>(x, 1);
            Slot<uint32_t>(y, 1);
            const unsigned calls = rng() % 4;
            std::mt19937 rx = rng;
            for (unsigned n = 0; n < calls; ++n) SetParts(x, rx);
            for (unsigned n = 0; n < calls; ++n) SetParts(y, rng);
            if (t % 3) SetParts(y, rng);
            Note({0x44, uint32_t(Slot<bool>(x, 22, y)), uint32_t(Slot<bool>(y, 22, x)), Slot<uint32_t>(x, 21),
                  Slot<uint32_t>(y, 21)});
            for (void *const p : {x, y}) Slot<uint32_t>(p, 2);
        }
        for (void *const p : {a, b}) {
            difftest::FreeRecorder freed;
            const uint32_t r = Slot<uint32_t>(p, 2);
            freed.Stop();
            Note({0x43, r, uint32_t(freed.Count()), uint32_t(freed.Freed(p))});
        }
    });
}

// Apply 0xabf490 (SendTargets 0xac1c40, SendViewport 0xac22c0) and EndState 0xac6740 on fresh parts: up to six setter
// calls and Init (which keeps the device's render target count), then each, the commands and the clear noted.
DIFF_TEST(sg_apply_render_target_apply, 0xabf490)
{
    World world(ctx);
    const Leaf &l = RenderTargetLeaf();
    CheckSides(ctx, "apply", 2000, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const p = make();
        Slot<uint32_t>(p, 1);
        gPart = static_cast<uint8_t *>(p);
        Slot<void>(p, 15, static_cast<uint32_t>(rng() % 3), PickSurface(rng));
        const unsigned calls = rng() % 7;
        for (unsigned n = 0; n < calls; ++n) SetParts(p, rng);
        if (rng() % 4) NoteInit(p, rng);
        if (ApplySafe(p)) Slot<void>(p, 11, static_cast<void *>(gContext), static_cast<void *>(gItem));
        Note({0x61});
        Slot<void>(p, 13, static_cast<void *>(gContext));
        Note({0x63});
        NoteFields(p, l, 0x41);
        Slot<uint32_t>(p, 2);
    });
}

// Init 0xac3f20 on fresh parts of each target kind and texture: every line, the render target's calls and releases.
DIFF_TEST(sg_apply_render_target_init, 0xac3f20)
{
    World world(ctx);
    const Leaf &l = RenderTargetLeaf();
    CheckSides(ctx, "init", 2000, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const p = make();
        Slot<uint32_t>(p, 1);
        if (rng() % 3) Slot<void>(p, 15, static_cast<uint32_t>(rng() % 4), PickSurface(rng));
        if (rng() % 3) Slot<void>(p, 14, kNames[rng() % 5], PickSurface(rng));
        NoteInit(p, rng);
        NoteInit(p, rng);
        NoteFields(p, l, 0x41);
        Slot<uint32_t>(p, 2);
    });
}

// The constructor 0xabd810 and destructor 0xabe230 alone on a block of pattern words (the handle's id released through
// the texture manager fake).
DIFF_TEST(sg_apply_render_target_construct_destruct, 0xabd810)
{
    World world(ctx);
    using Ctor = void *(__thiscall *)(void *);
    using Dtor = void(__thiscall *)(void *);
    const Leaf &l = RenderTargetLeaf();
    CheckSides(ctx, l.name, 32, [&](bool original, std::mt19937 &rng) {
        alignas(8) uint32_t block[20];
        for (uint32_t &w : block) w = rng();
        if (original) {
            void *const r = ctx.Original<Ctor>(l.ctor)(block);
            Note({0x50, uint32_t(r == block), uint32_t(block[0] == uint32_t(ctx.image.Base() + l.vtable))});
        } else {
            l.placement(block);
            Note({0x50, 1, 1});
        }
        gLog.insert(gLog.end(), block + 1, block + 20);
        for (size_t i = 1; i < l.size / 4; ++i) block[i] = rng();
        if (rng() % 2) block[4] = 10 + rng() % 4;
        if (rng() % 4 == 0) block[4] = 0xffffffffu;
        if (original)
            ctx.Original<Dtor>(l.dtor)(block);
        else
            l.destroy(block);
        gLog.insert(gLog.end(), block + 1, block + 20);
    });
}
