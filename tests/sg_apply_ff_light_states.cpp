// Differential tests of cApplyFFLightStates (lane TM, scenegraph M3a; src/sg/cApplyFFLightStates.h), in the world of
// tests/sg_shader_attr_fx.h, which this file extends:
//   - the item's +0x30 is a light key whose four low nibbles rise (so no run is negative; the image would loop for ever
//     or read past the table otherwise), +0x3c a table of 16 fake lights after three words (the ambient colour the
//     enabled-lights command copies), or null (one time in eight, with every run empty);
//   - each fake light's slot 50 notes its call and fills the eight parameters with its own drawn floats (now and then a
//     NaN with its own payload, so that the order of the operands of an addition or product shows), and its words 9,
//     12, 15 and 16-18 are drawn;
//   - Init's first argument is a caps fake: slot 20 answers a drawn byte (above it garbage), slot 21 a drawn count;
//   - cRZStd's report slot (0x1bfb92) is a stand-in noting the file, line, group, level and the text.
#include "sg_shader_attr_fx.h"

#include "code_patch.h"
#include "rz/cRZStd.h"
#include "sg/cApplyFFLightStates.h"

#include <cstdarg>
#include <string>

namespace {

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

struct Fake {
    void **vt;
};
void __fastcall FakeTrap(Fake *, void *) { Note({0xdeab}); }

// ---- the caps ----
void *gCapsVTable[24];
Fake gCaps{gCapsVTable};
uint32_t gFeature, gLimit;
uint32_t __fastcall CapsFeature(Fake *, void *, uint32_t which)
{
    Note({20, which});
    return gFeature;
}
uint32_t __fastcall CapsLimit(Fake *, void *, uint32_t which)
{
    Note({21, which});
    return gLimit;
}

// ---- the lights ----
void *gLightVTable[60];
struct FakeLight {
    void **vt;
    uint32_t params[8];
    uint32_t id;
};
// What the item's table points at: the light, then the holder's words 1..18 (and one spare).
struct FakeHolder {
    FakeLight *light;
    uint32_t w[19];
};
FakeLight gLights[16];
FakeHolder gHolders[16];
uint32_t gLightBlock[3 + 16]; // the ambient colour, then the table

void __fastcall LightGetParams(FakeLight *self, void *, uint32_t *out, uint32_t item)
{
    Note({0x50, self->id, uint32_t(item == reinterpret_cast<uintptr_t>(gItem))});
    std::memcpy(out, self->params, sizeof self->params);
}

uint32_t PickWord(std::mt19937 &rng)
{
    if (rng() % 12 == 0) return 0x7fc00000u | (rng() & 0x3fffff) | (rng() % 2 ? 0x80000000u : 0); // a NaN of its own
    return Bits(PickFloat(rng));
}

// A key whose four low nibbles rise from `first`, the rest drawn.
uint32_t RisingKey(std::mt19937 &rng, bool empty)
{
    uint32_t key = rng() & 0xffff0000u;
    uint32_t at = rng() % 4 ? rng() % 3 : rng() % 16;
    for (uint32_t n = 0; n < 4; ++n) {
        if (!empty && n != 0 && rng() % 3) at += rng() % (n == 1 ? 6 : 4);
        if (at > 15) at = 15;
        key |= at << (4 * n);
    }
    return key;
}

void LightWorld(std::mt19937 &rng)
{
    for (void *&v : gCapsVTable) v = reinterpret_cast<void *>(&FakeTrap);
    gCapsVTable[20] = reinterpret_cast<void *>(&CapsFeature);
    gCapsVTable[21] = reinterpret_cast<void *>(&CapsLimit);
    gFeature = (rng() & 0xffffff00u) | (rng() % 4 ? 1u + rng() % 255 : 0u);
    gLimit = rng() % 4 ? rng() % 17 : rng();
    for (void *&v : gLightVTable) v = reinterpret_cast<void *>(&FakeTrap);
    gLightVTable[50] = reinterpret_cast<void *>(&LightGetParams);
    for (uint32_t i = 0; i < 16; ++i) {
        FakeLight &l = gLights[i];
        l.vt = gLightVTable;
        l.id = 0x100 + i;
        for (uint32_t &w : l.params) w = PickWord(rng);
        FakeHolder &h = gHolders[i];
        h.light = &l;
        for (uint32_t &w : h.w) w = PickWord(rng);
        gLightBlock[3 + i] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&h));
    }
    for (int i = 0; i < 3; ++i) gLightBlock[i] = PickWord(rng);
    const bool none = rng() % 8 == 0;
    const uint32_t key = RisingKey(rng, none);
    std::memcpy(gItem + 0x30, &key, 4);
    const void *const table = none ? nullptr : &gLightBlock[3];
    std::memcpy(gItem + 0x3c, &table, 4);
}

// The world for a test: the extra parts and the report stand-in.
struct World {
    shape::CodePatch report;
    explicit World(difftest::Context &ctx) : report(0x1bfb92, reinterpret_cast<const void *>(&StandReport))
    {
        gImageBase = ctx.image.Base();
        gWorldExtra = &LightWorld;
    }
    ~World() { gWorldExtra = nullptr; }
};

void NoSetters(void *, std::mt19937 &) {}

const Leaf &LightStatesLeaf()
{
    static const Leaf leaf = {
        "cApplyFFLightStates", 0xe76ea0, 0xac3140, 0xabd790, 0xabe200, 14,
        {{2, 0xac6050}, {3, 0x140fe0}, {8, 0xac3b70}, {9, 0x1b9a50}, {10, 0x15e510}, {11, 0xabee20}, {12, 0xac16a0},
         {13, 0x300a0}, {14, 0xabe8f0}, {15, 0x6c6b0}, {16, 0x148260}},
        reinterpret_cast<Factory>(&cApplyFFLightStates::Create), sizeof(cApplyFFLightStates), &NoSetters, 0,
        [](void *p) { new (p) cApplyFFLightStates; },
        [](void *p) { static_cast<cApplyFFLightStates *>(p)->~cApplyFFLightStates(); }};
    return leaf;
}

uint32_t gKey;

// Init with the caps fake, a drawn key word, and check and report words whose low bytes are drawn.
void NoteInit(void *p, std::mt19937 &rng)
{
    gKey = rng() % 3 ? RisingKey(rng, false) : rng();
    const uint32_t check = (rng() & 0xffffff00u) | (rng() % 4 ? 1u + rng() % 255 : 0u);
    const uint32_t report = (rng() & 0xffffff00u) | (rng() % 3 ? 1u + rng() % 255 : 0u);
    const bool r = Slot<bool>(p, 8, static_cast<void *>(&gCaps), static_cast<void *>(&gKey), check, report);
    Note({0x66, uint32_t(r), Slot<uint32_t>(p, 6)});
}

void NoteSlot12(void *p, std::mt19937 &rng)
{
    gKey = RisingKey(rng, rng() % 8 == 0);
    Slot<void>(p, 12, static_cast<void *>(&gState), static_cast<const void *>(&gKey));
    Note({0x64});
}

} // namespace

// The factory 0xac3140 and every slot on two parts: Init 0xac3b70 (both failure lines), Slot10 (the shared 0x15e510),
// Apply 0xabee20, Slot12 0xac16a0, EndState and Reset (shared no-ops), IsEquivalent with the shared HashParams 0x6c6b0
// and EqualParams 0x148260, and the last Release (the stub 0xac6050) through the deleting destructor 0xabe8f0 (the
// destructor 0xabe200).
DIFF_TEST(sg_apply_ff_light_states, 0xac3140)
{
    World world(ctx);
    const Leaf &l = LightStatesLeaf();
    CheckSides(ctx, l.name, 1500, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const a = make();
        void *const b = make();
        if (original && !CheckVTable(ctx, l, a)) return;
        Slot<uint32_t>(a, 1);
        Slot<uint32_t>(b, 1);
        NoteFields(a, l, 0x41);
        for (int k = 0; k < 10; ++k) {
            switch (rng() % 8) {
            case 0:
            case 1: NoteInit(a, rng); break;
            case 2:
            case 3:
                Slot<void>(a, 11, static_cast<void *>(gContext), static_cast<void *>(gItem));
                Note({0x61});
                break;
            case 4: NoteSlot12(a, rng); break;
            case 5:
                Note({0x60, Slot<uint32_t>(a, 10)});
                Slot<void>(a, 13, static_cast<void *>(gContext));
                Slot<void>(a, 3);
                Slot<void>(a, 9, static_cast<void *>(&gState), static_cast<void *>(gContext));
                Note({0x63});
                break;
            default:
                NoteInit(b, rng);
                Note({0x67, uint32_t(Slot<bool>(a, 7, b)), Slot<uint32_t>(a, 15), uint32_t(Slot<bool>(a, 16, b))});
                break;
            }
        }
        NoteFields(a, l, 0x42);
        NoteFields(b, l, 0x42);
        for (void *const p : {a, b}) {
            difftest::FreeRecorder freed;
            const uint32_t r = Slot<uint32_t>(p, 2);
            freed.Stop();
            Note({0x43, r, uint32_t(freed.Count()), uint32_t(freed.Freed(p))});
        }
    });
}

// Apply 0xabee20 on a fresh part: the enabled-lights command and one light command per light, every word noted.
DIFF_TEST(sg_apply_ff_light_states_apply, 0xabee20)
{
    World world(ctx);
    const Leaf &l = LightStatesLeaf();
    CheckSides(ctx, "apply", 3000, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const p = make();
        Slot<uint32_t>(p, 1);
        Slot<void>(p, 11, static_cast<void *>(gContext), static_cast<void *>(gItem));
        Note({0x61});
        if (rng() % 2) NoteSlot12(p, rng);
        Slot<uint32_t>(p, 2);
    });
}

// Init 0xac3b70 on fresh parts: each check and line.
DIFF_TEST(sg_apply_ff_light_states_init, 0xac3b70)
{
    World world(ctx);
    const Leaf &l = LightStatesLeaf();
    CheckSides(ctx, "init", 2000, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const p = make();
        Slot<uint32_t>(p, 1);
        NoteInit(p, rng);
        NoteInit(p, rng);
        NoteFields(p, l, 0x41);
        Slot<uint32_t>(p, 2);
    });
}

// Slot12 0xac16a0 on fresh parts: the default lights for each key.
DIFF_TEST(sg_apply_ff_light_states_slot12, 0xac16a0)
{
    World world(ctx);
    const Leaf &l = LightStatesLeaf();
    CheckSides(ctx, "slot12", 1000, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const p = make();
        Slot<uint32_t>(p, 1);
        NoteSlot12(p, rng);
        Slot<uint32_t>(p, 2);
    });
}

// The constructor 0xabd790 and destructor 0xabe200 alone on a block of pattern words.
DIFF_TEST(sg_apply_ff_light_states_construct_destruct, 0xabd790)
{
    World world(ctx);
    using Ctor = void *(__thiscall *)(void *);
    using Dtor = void(__thiscall *)(void *);
    const Leaf &l = LightStatesLeaf();
    CheckSides(ctx, l.name, 32, [&](bool original, std::mt19937 &rng) {
        alignas(8) uint32_t block[8];
        for (uint32_t &w : block) w = rng();
        if (original) {
            void *const r = ctx.Original<Ctor>(l.ctor)(block);
            Note({0x50, uint32_t(r == block), uint32_t(block[0] == uint32_t(ctx.image.Base() + l.vtable))});
        } else {
            l.placement(block);
            Note({0x50, 1, 1});
        }
        gLog.insert(gLog.end(), block + 1, block + 8);
        for (size_t i = 1; i < 8; ++i) block[i] = rng();
        if (original)
            ctx.Original<Dtor>(l.dtor)(block);
        else
            l.destroy(block);
        gLog.insert(gLog.end(), block + 1, block + 8);
    });
}
