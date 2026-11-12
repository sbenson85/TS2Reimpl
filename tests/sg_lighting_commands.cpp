// Differential tests for the lighting manager's light attribute commands (lane 261108h, scenegraph LG3;
// src/sg/LightAttributeCommands.cpp). The world is the material commands' (tests/gz_material_commands.h: the command, the
// argument list, the parser face), with a lighting parser state of its own: the material world's state block is 0x400
// bytes and these commands read +0x404 (the attributes object, a fake whose slots 7 and 8 record, the settings texts
// going to gExtra) and +0x40c (the settings name, a std::string rebuilt before each side's run and read after).
#include "gz_material_commands.h"

#include "sg/LightAttributeCommands.h"

using namespace mattest;

namespace {

using ExecuteFn = void(__thiscall *)(void *, void *);
using SetContextFn = void(__thiscall *)(void *, void *, void *);

constexpr size_t kLightStateSize = 0x430;

// The namespace paths the face's slot 28 answers (the last long enough that a name after it leaves the short string).
const char *const kPaths[] = {"", "outdoor:", "a:", "neighborhoodLighting:"};
// The settings names put in the state's string before a run (short texts, kept in the string itself).
const char *const kSettings[] = {"", "day", "sunset:glow", "x"};
// The words: names, the separator in its cases, a long word.
const char *const kLightWords[] = {"day", "", ":", ":", "::", "from", "nightTimeAmbientLightSettings", "Sun", "0.5"};

uint32_t __fastcall AttributesSet(void *self, void *, void *face, const char *settings, const char *attribute,
                                  const char *value)
{
    ExtraText(settings);
    return Record(self, 7, reinterpret_cast<uint32_t>(face), Arg(settings) == 0x57AC0000u, reinterpret_cast<uint32_t>(attribute),
                  reinterpret_cast<uint32_t>(value));
}
uint32_t __fastcall AttributesBase(void *self, void *, const char *base, const char *settings)
{
    ExtraText(settings);
    return Record(self, 8, reinterpret_cast<uint32_t>(base), Arg(settings) == 0x57AC0000u);
}

// The lighting parts: the state block (random words), the attributes object at +0x404, the settings name at +0x40c.
struct LightWorld {
    World &w;
    FakeVTable attributesVt{50};
    alignas(8) uint32_t attributes[4] = {};
    alignas(16) uint8_t state[kLightStateSize] = {};
    const char *settings = "";

    explicit LightWorld(World &world) : w(world)
    {
        for (auto &b : state) b = static_cast<uint8_t>(w.gen());
        attributes[0] = reinterpret_cast<uint32_t>(attributesVt.slots);
        attributesVt.slots[7] = reinterpret_cast<void *>(&AttributesSet);
        attributesVt.slots[8] = reinterpret_cast<void *>(&AttributesBase);
        *reinterpret_cast<void **>(state + 0x404) = attributes;
        settings = kSettings[w.gen() % std::size(kSettings)];
        w.faceVt.ret[28] = reinterpret_cast<uint32_t>(kPaths[w.gen() % std::size(kPaths)]);
        *reinterpret_cast<void **>(w.cmd + 4) = w.face; // the commands' parser is the face itself
        for (auto &s : w.argv) s = kLightWords[w.gen() % std::size(kLightWords)];
    }
    void Before()
    {
        std::memset(state + 0x40c, 0, sizeof(std::string));
        new (state + 0x40c) std::string(settings);
    }
    // The name read into gExtra, then destroyed (a heap block's address differs between the sides).
    void After()
    {
        auto *s = reinterpret_cast<std::string *>(state + 0x40c);
        ExtraText(s->c_str());
        ExtraWord(static_cast<uint32_t>(s->size()));
        s->~basic_string();
        std::memset(state + 0x40c, 0xcd, sizeof(std::string));
    }
};

template <class Orig, class Ours> bool CompareLight(difftest::Context &ctx, LightWorld &lw, const char *what, Orig orig, Ours ours)
{
    std::vector<Region> regions = {{lw.w.cmd, kCmdSize}, {lw.state, kLightStateSize}, {lw.attributes, sizeof(lw.attributes)}};
    return Differential(
        ctx, what, regions, [&] { return RunGuarded(orig); }, [&] { return RunGuarded(ours); }, [&] { lw.After(); },
        [&] { lw.Before(); });
}

template <class C> void NameTest(difftest::Context &ctx, uint32_t rva, const char *expected)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(rva);
    uint8_t object[0x50] = {};
    const char *a = orig(object);
    const char *b = reinterpret_cast<C *>(object)->GetName();
    ++ctx.cases;
    if (a != b || std::strcmp(b, expected) != 0) ctx.Fail("GetName %08x: %p '%s' vs %p '%s'", rva, a, a, b, b);
}

} // namespace

// 0x2d4e80 lightAttribOverride: the path from the face's slot 28 and the first word make the settings name slot 7 gets.
DIFF_TEST(sg_lighting_commands_override, 0x2d4e80)
{
    auto orig = ctx.Original<ExecuteFn>(0x2d4e80);
    std::mt19937 gen(0x2d4e80);
    for (int i = 0; i < 200; ++i) {
        World w(gen);
        LightWorld lw(w);
        *reinterpret_cast<void **>(w.cmd + 0xc) = lw.state;
        char what[64];
        std::snprintf(what, sizeof(what), "lightAttribOverride case %d", i);
        CompareLight(
            ctx, lw, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<nTSSG::cLightAttribOverrideCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

// 0x2d4f90 attribute (inside a lightAttributes block): the state's settings name and two words to slot 7.
DIFF_TEST(sg_lighting_commands_attribute, 0x2d4f90)
{
    auto orig = ctx.Original<ExecuteFn>(0x2d4f90);
    std::mt19937 gen(0x2d4f90);
    for (int i = 0; i < 200; ++i) {
        World w(gen);
        LightWorld lw(w);
        *reinterpret_cast<void **>(w.cmd + 0xc) = lw.state;
        char what[64];
        std::snprintf(what, sizeof(what), "attribute case %d", i);
        CompareLight(
            ctx, lw, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<nTSSG::cLightAttributeCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

// 0x2d4fd0 lightAttributes: counts 1..4, 0 and past 2^31 (the original's tests are signed), the separator in and out
// of case (":" and others), the base word; the two throws; the block pushed (slot 16).
DIFF_TEST(sg_lighting_commands_attributes, 0x2d4fd0)
{
    auto orig = ctx.Original<ExecuteFn>(0x2d4fd0);
    Stands stands;
    std::mt19937 gen(0x2d4fd0);
    for (int i = 0; i < 400; ++i) {
        World w(gen);
        LightWorld lw(w);
        *reinterpret_cast<void **>(w.cmd + 0x40) = lw.state;
        const int32_t counts[] = {1, 1, 2, 3, 3, 3, 4, 0, static_cast<int32_t>(0x80000003u), -1};
        gPositionalCount = counts[gen() % std::size(counts)];
        char what[64];
        std::snprintf(what, sizeof(what), "lightAttributes case %d", i);
        CompareLight(
            ctx, lw, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<nTSSG::cLightAttributesCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

// 0x2df3b0 lightAttributes' slot 3: the attribute command as its child.
DIFF_TEST(sg_lighting_commands_context, 0x2df3b0)
{
    auto orig = ctx.Original<SetContextFn>(0x2df3b0);
    Stands stands;
    std::mt19937 gen(0x2df3b0);
    for (int i = 0; i < 30; ++i) {
        World w(gen);
        w.AsBlock();
        void *iface = i % 5 == 4 ? nullptr : w.state + 4;
        char what[64];
        std::snprintf(what, sizeof(what), "lightAttributes SetContext case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.parser, iface); },
            [&] { reinterpret_cast<nTSSG::cLightAttributesCommand *>(w.cmd)->SetContext(w.parser, iface); });
    }
}

DIFF_TEST(sg_lighting_commands_names, 0x2df260)
{
    NameTest<nTSSG::cLightAttribOverrideCommand>(ctx, 0x2df260, "lightAttribOverride");
    NameTest<nTSSG::cLightAttributesCommand>(ctx, 0x2df280, "lightAttributes");
}
