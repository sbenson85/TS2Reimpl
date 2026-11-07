// Differential tests of nGZSceneGraph::cRenderGroupManager (src/sg/cRenderGroupManager.h) and the registry getter
// RenderGroupRegistry 0xaeb370.
//
//   SG_RenderGroupManager  a manager's life on each side: the original's factory 0xaa37f0 (constructor 0xaa3700, which
//       registers "Practical") or ours, then random calls through the interface: BitOfName 0xaa3900 (names from a pool
//       with case variants, the empty name and long names, more than 32 of them so the limit is reached), Count
//       0xaa38b0, NameOfBit 0xaa3860 (bits past the end too), QueryInterface 0xaa38d0, AddRef / Release (the shared
//       0x148fb0 / 0x4b1d00). After each step the whole state is noted: the names vector (size, capacity, each name's
//       text, size and capacity) and the map in order (key text, bit). The last Release runs the deleting destructor
//       0xaa37d0 (destructor 0xaa37a0); the heap must come back to where it was.
//   SG_RenderGroupRegistry  0xaeb370 on each side: the cache (the image's cell 0x10f905c, ours gRenderGroupRegistry)
//       empty or set, the framework (0x10c9378) a fake whose slot 5 hands out a counted scene graph service fake (its
//       slot 7 the registry); called a few times, the calls, counts and results noted.
//
// The game's operator new / sized delete (0x824632 / 0x824624) go to our heap on both sides.
#include "sgfx.h"

#include "code_patch.h"

#include "sg/cRenderGroupManager.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <string>

namespace {

using namespace nGZSceneGraph;

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

struct Heap {
    shape::CodePatch gameNew{GameAddress<void>(0x824632), shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{GameAddress<void>(0x824624), shape::CodeOf(&GameDelete)};
};

void NoteText(const char *label, const std::string &s)
{
    drawfx::Call c;
    c.note = label;
    c.words = {uint32_t(s.size()), uint32_t(s.capacity())};
    c.text = s;
    drawfx::gLog.push_back(std::move(c));
}

void NoteTextOrNull(const char *label, const char *text)
{
    drawfx::Call c;
    c.note = label;
    c.words = {text != nullptr};
    if (text) c.text = text;
    drawfx::gLog.push_back(std::move(c));
}

// The state, read through our layout (the STL's layouts are the original's).
void NoteState(const cIRenderGroupRegistry *r)
{
    difftest::NotCounted bookkeeping;
    const auto *const m = static_cast<const cRenderGroupManager *>(r);
    drawfx::Note("refs", m->mRefCount);
    drawfx::Note("names", {uint32_t(m->mNames.size()), uint32_t(m->mNames.capacity())});
    for (const std::string &name : m->mNames) NoteText("name", name);
    drawfx::Note("map", uint32_t(m->mBits.size()));
    for (const auto &[key, bit] : m->mBits) {
        NoteText("key", key);
        drawfx::Note("bit", bit);
    }
}

constexpr const char *kNames[] = {
    "Practical", "practical", "PRACTICAL", "", "Shadow", "shadow", "ShapeColor", "shapecolor", "Glow", "glow!",
    "Reflection", "ui", "UI", "Ui", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q",
    "r", "s", "t", "u", "v", "w", "x", "y", "z", "fifteen chars!!", "sixteen chars!!!",
    "a render group name longer than the short buffer", "Zeta_01", "zeta_01", "ZETA_02", "[bracket", "_underscore",
    "~tilde", "\xe9t\xe9"};

void Step(cIRenderGroupRegistry *r, std::mt19937 &rng)
{
    const unsigned op = rng() % 10;
    drawfx::Note("op", op);
    switch (op) {
    case 0: {
        static constexpr uint32_t kIids[] = {1, 0, cRenderGroupManager::kIid, 0xc9737f2d, 0x4bcf4f66};
        const uint32_t iid = kIids[rng() % std::size(kIids)];
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
        const bool useOut = rng() % 5 != 0;
        const bool got = r->QueryInterface(iid, useOut ? &out : nullptr);
        drawfx::Note("query", {iid, uint32_t(uint8_t(got)), uint32_t(out == r), uint32_t(uintptr_t(out) == 0x5a5a5a5a)});
        if (got) drawfx::Note("query release", r->Release());
        break;
    }
    case 1: {
        const unsigned n = 1 + rng() % 3;
        for (unsigned i = 0; i < n; ++i) drawfx::Note("addref", r->AddRef());
        for (unsigned i = 0; i < n; ++i) drawfx::Note("release", r->Release());
        break;
    }
    case 2:
        drawfx::Note("count", r->Count());
        break;
    case 3:
    case 4: {
        const uint32_t bit = rng() % 4 == 0 ? rng() : rng() % 40;
        NoteTextOrNull("name of bit", r->NameOfBit(bit));
        break;
    }
    default: { // BitOfName: one name, or a run of fresh ones
        const unsigned n = rng() % 3 == 0 ? 1 + rng() % 12 : 1;
        for (unsigned i = 0; i < n; ++i) {
            const char *const name = kNames[rng() % std::size(kNames)];
            NoteTextOrNull("bit of", name);
            drawfx::Note("bit", r->BitOfName(name));
        }
        break;
    }
    }
    NoteState(r);
}

} // namespace

// Verifies: the factory 0xaa37f0, the constructor 0xaa3700, the destructor 0xaa37a0 and deleting destructor 0xaa37d0,
// QueryInterface 0xaa38d0, BitOfName 0xaa3900, Count 0xaa38b0, NameOfBit 0xaa3860.
DIFF_TEST(SG_RenderGroupManager, 0xaa37f0)
{
    Heap heap;
    using Factory = void *(__cdecl *)();
    const Factory original = ctx.Original<Factory>(0xaa37f0);
    sgfx::CheckSides(
        ctx, "render group manager", 1200, [](uint32_t, drawfx::Cells &) {},
        [&](bool isOriginal, std::mt19937 &rng) {
            difftest::AllocationCounter counter;
            auto *const r = static_cast<cIRenderGroupRegistry *>(isOriginal ? original() : cRenderGroupManagerAdapters::Create());
            drawfx::Note("made", r != nullptr);
            NoteState(r);
            drawfx::Note("first addref", r->AddRef());
            const unsigned steps = 5 + rng() % 40;
            for (unsigned s = 0; s < steps; ++s) Step(r, rng);
            sgfx::MarkStep(ctx, "last release");
            drawfx::Note("last release", r->Release());
            counter.Stop();
            drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
            ++ctx.cases;
        });
}

namespace {

// The fakes the getter reaches: the framework (slot 5 GetSystemService) and the scene graph service (slot 7 the
// registry). Each call is noted.

class FakeService : public cIGZUnknown {
public:
    uint32_t refs = 0;
    void *registry = nullptr;
    bool QueryInterface(uint32_t, void **) override
    {
        drawfx::Note("service qi", 0);
        return false;
    }
    uint32_t AddRef() override
    {
        drawfx::Note("service addref", refs + 1);
        return ++refs;
    }
    uint32_t Release() override
    {
        drawfx::Note("service release", refs - 1);
        return --refs;
    }
    virtual void Slot3() { drawfx::Note("service slot", 3); }
    virtual void Slot4() { drawfx::Note("service slot", 4); }
    virtual void Slot5() { drawfx::Note("service slot", 5); }
    virtual void Slot6() { drawfx::Note("service slot", 6); }
    virtual void *Registry()
    {
        drawfx::Note("service registry", refs);
        return registry;
    }
};

class FakeFramework : public cIGZUnknown {
public:
    FakeService *service = nullptr;
    bool answer = true;
    bool QueryInterface(uint32_t, void **) override
    {
        drawfx::Note("framework qi", 0);
        return false;
    }
    uint32_t AddRef() override
    {
        drawfx::Note("framework addref", 0);
        return 1;
    }
    uint32_t Release() override
    {
        drawfx::Note("framework release", 0);
        return 1;
    }
    virtual void Slot3() { drawfx::Note("framework slot", 3); }
    virtual void Slot4() { drawfx::Note("framework slot", 4); }
    virtual bool GetSystemService(uint32_t id, uint32_t iid, void **out)
    {
        drawfx::Note("get system service", {id, iid, uint32_t(*out == nullptr)});
        *out = service;
        service->AddRef();
        return answer;
    }
};

FakeService gService;
FakeFramework gFramework;
uint8_t gRegistries[2][4];

} // namespace

// Verifies: 0xaeb370 (RenderGroupRegistry).
DIFF_TEST(SG_RenderGroupRegistry, 0xaeb370)
{
    using Getter = void *(__cdecl *)();
    const Getter original = ctx.Original<Getter>(0xaeb370);
    cIRenderGroupRegistry *const savedOurs = gRenderGroupRegistry;
    sgfx::CheckSides(
        ctx, "render group registry", 200,
        [&](uint32_t seed, drawfx::Cells &cells) {
            std::mt19937 rng(seed);
            gService.refs = rng() % 3;
            gService.registry = rng() % 5 == 0 ? nullptr : gRegistries[0];
            gFramework.service = &gService;
            gFramework.answer = rng() % 2 != 0;
            void *const cached = rng() % 4 == 0 ? gRegistries[1] : nullptr;
            cells.Set(0x10c9378, &gFramework);
            cells.Set(0x10f905c, cached);
            gRenderGroupRegistry = static_cast<cIRenderGroupRegistry *>(cached);
        },
        [&](bool isOriginal, std::mt19937 &rng) {
            const unsigned calls = 1 + rng() % 3;
            for (unsigned i = 0; i < calls; ++i) {
                void *const got = isOriginal ? original() : RenderGroupRegistry();
                drawfx::Note("registry", {uint32_t(got == gRegistries[0]), uint32_t(got == gRegistries[1]), uint32_t(got == nullptr)});
                drawfx::Note("service refs", gService.refs);
                // The cache as the side keeps it.
                void *const cache = isOriginal ? *GameAddress<void *>(0x10f905c) : gRenderGroupRegistry;
                drawfx::Note("cache", {uint32_t(cache == got)});
            }
            ++ctx.cases;
        });
    gRenderGroupRegistry = savedOurs;
}
