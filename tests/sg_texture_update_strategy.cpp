// Differential tests of the texture update strategies (lane SJ, milestone O row M4a part 4;
// src/sg/cTextureUpdateStrategy.h).
//
//   SG_TextureUpdateStrategies  a strategy's life on each side: the plain one (the original's constructor 0xaebd30 on
//       operator new memory; ours new) or the missing-texture one of a random name (the original's 0xadd030; ours new),
//       then random calls through the interface: QueryInterface (shared_vf_178b20), AddRef / Release (shared_vf_148fb0,
//       shared_vf_4b1d00), GetDescription (shared_vf_147110, its low byte and what it leaves in the outputs), Update
//       (the plain one's 0xaed030: the surface fake locked, its rows filled; the missing one's 0xadeb90 into the
//       drawing 0xae8310), Finish (shared_vf_140fe0). After every Update the surface's pixels are noted; at the end
//       the last Release (the deleting destructors 0xaebd50 / 0xadd620), the fakes' counts and the heap.
//   SG_TextureUpdateStrategy_FillWhite  0xaed030 called directly (its hook's entry, as the video strategy's Update
//       calls it).
//
// The surface fake's Lock writes a random lock record (or none: a failed lock), counting itself in it or not, and its
// Unlock lets the record's surface go itself one time in three. The drawing 0xae8310 (ours, tested in
// tests/sg_missing_texture_name.cpp) is stood in on both sides, the image's and ours (it notes the surface and the name
// string's words and text).
#include "sgfx.h"

#include "code_patch.h"

#include "gz/cSurfaceDX.h"
#include "sg/DrawMissingTextureName.h"
#include "sg/cTextureUpdateStrategy.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <random>

namespace {

using namespace nRZSceneGraph;
using nGZSceneGraph::cMissingTextureUpdateStrategy;
using nGZGraphic4::cILockableSurface;
using nGZGraphic4::cSurfaceLock;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

constexpr At kSurfaceAt[] = {{26, {2, Ret::Int, true, "Lock", "ab"}}, {27, {1, Ret::Int, true, "Unlock", "a"}}};
constexpr auto kSurfaceSlots = Table<29>(kSurfaceAt);
constexpr drawfx::Iface kSurface{"LockableSurface", 29, kSurfaceSlots.data()};

constexpr unsigned kPixels = 1024;

struct World {
    Fake surface;
    uint8_t pixels[kPixels];
    uint32_t lcg = 1;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

// Lock(record, readOnly): one time in six nothing written (0); else a record over the pixels, the surface in it
// (counted) three times in four, the pitch returned.
uint32_t Lock(Fake &self, const uint32_t *a)
{
    auto *const lock = reinterpret_cast<cSurfaceLock *>(uintptr_t(a[0]));
    const uint32_t r = gw.Next();
    if (r % 6 == 0) return 0;
    const bool held = (r >> 3) % 4 != 0;
    if (held) ++self.refs;
    lock->surface = held ? &self : nullptr;
    lock->rect = {0, 0, 0, 0};
    const uint32_t pitch = 16 + (r >> 5) % 64;
    lock->pitch = int32_t(pitch);
    lock->rowBytes = (r >> 11) % (pitch + 1);
    lock->rows = (r >> 17) % (kPixels / pitch + 1);
    lock->bits = gw.pixels + (r >> 23) % (kPixels - pitch * lock->rows + 1);
    return pitch;
}
// Unlock(record): one time in three the record's surface let go here (and cleared), as a lock that ends itself.
uint32_t Unlock(Fake &self, const uint32_t *a)
{
    auto *const lock = reinterpret_cast<cSurfaceLock *>(uintptr_t(a[0]));
    if (gw.Next() % 3 == 0 && lock->surface == &self) {
        --self.refs;
        lock->surface = nullptr;
    }
    return 0;
}

void NotePixels()
{
    difftest::NotCounted bookkeeping;
    uint32_t hash = 2166136261u, count = 0;
    for (uint8_t b : gw.pixels) {
        hash = (hash ^ b) * 16777619u;
        count += b == 0xff;
    }
    drawfx::Note("pixels", {hash, count});
}

void __cdecl StandDraw(cILockableSurface *surface, cRZString *name)
{
    drawfx::Note("draw", drawfx::Ptr(surface));
    const uint32_t *const w = reinterpret_cast<const uint32_t *>(name);
    drawfx::Note("name words", {w[0], uint32_t(name->mString.size()), uint32_t(name->mString.capacity()), name->mRefCount});
    sgfx::NoteText("name", name->mString.c_str());
}

struct Stands {
    shape::StandIn draw{0xae8310, &nGZSceneGraph::DrawMissingTextureName, &StandDraw};
};

constexpr const char *kNames[] = {"",
                                  "a",
                                  "brick_wall-01",
                                  "AlreadyUpper",
                                  "fifteen chars!!",
                                  "sixteen chars!!!",
                                  "a name longer than the short buffer, mixed Case 123",
                                  "x\xe9y"};

void Build(uint32_t seed, drawfx::Cells &)
{
    gw.lcg = seed * 2654435761u + 7;
    gw.surface.Reset<kSurface>(540);
    gw.surface.On(26, &Lock);
    gw.surface.On(27, &Unlock);
    for (unsigned i = 0; i < kPixels; ++i) gw.pixels[i] = uint8_t(i * 7 + seed);
}

cILockableSurface *Surface() { return reinterpret_cast<cILockableSurface *>(&gw.surface); }

void NoteState(const cITextureUpdateStrategy *t)
{
    difftest::NotCounted bookkeeping;
    drawfx::Note("refs", reinterpret_cast<const uint32_t *>(t)[1]);
}

void Step(cITextureUpdateStrategy *t, std::mt19937 &rng)
{
    const unsigned op = rng() % 8;
    drawfx::Note("op", op);
    switch (op) {
    case 0: {
        static constexpr uint32_t kIids[] = {1, 0, 0x2a956da8, 0xdeadbeef};
        const uint32_t iid = kIids[rng() % 4];
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
        const bool useOut = rng() % 4 != 0;
        const bool got = t->QueryInterface(iid, useOut ? &out : nullptr);
        drawfx::Note("query", {iid, uint32_t(uint8_t(got)), uint32_t(uintptr_t(out) == 0x5a5a5a5a)});
        if (got) {
            drawfx::Note("query out", drawfx::Ptr(out));
            t->Release();
        }
        break;
    }
    case 1: { // a few references taken and given back: every count AddRef and Release return
        const unsigned n = 1 + rng() % 4;
        for (unsigned i = 0; i < n; ++i) drawfx::Note("addref", t->AddRef());
        for (unsigned i = 0; i < n; ++i) drawfx::Note("release", t->Release());
        break;
    }
    case 2: {
        uint32_t width = 0x11, height = 0x22, levels = 0x33;
        float quality = 0.75f;
        const bool got = t->GetDescription(&width, &height, &levels, &quality);
        drawfx::Note("description", {uint32_t(uint8_t(got)), width, height, levels, std::bit_cast<uint32_t>(quality)});
        break;
    }
    case 3:
    case 4:
    case 5:
        t->Update(Surface(), rng() % 6, rng() % 2 != 0);
        NotePixels();
        break;
    case 6:
        t->Finish();
        break;
    default:
        for (unsigned i = 0; i < 16; ++i) gw.pixels[rng() % kPixels] = uint8_t(rng());
        NotePixels();
        break;
    }
    NoteState(t);
}

void Life(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    const bool missing = rng() % 3 != 0;
    const char *const name = kNames[rng() % std::size(kNames)];
    drawfx::Note("missing", uint32_t(missing));
    void *made;
    if (original && missing)
        made = ctx.Original<void *(__thiscall *)(void *, const char *)>(0xadd030)(::operator new(0x28), name);
    else if (original)
        made = ctx.Original<void *(__thiscall *)(void *)>(0xaebd30)(::operator new(8));
    else if (missing)
        made = static_cast<cITextureUpdateStrategy *>(new cMissingTextureUpdateStrategy(name));
    else
        made = static_cast<cITextureUpdateStrategy *>(new cTextureUpdateStrategy);
    auto *const t = static_cast<cITextureUpdateStrategy *>(made);
    drawfx::Note("made", drawfx::Ptr(made));
    NoteState(t);
    t->AddRef();
    const unsigned steps = 3 + rng() % 20;
    for (unsigned s = 0; s < steps; ++s) Step(t, rng);
    sgfx::MarkStep(ctx, "last release");
    drawfx::Note("last release", t->Release());
    drawfx::Note("surface refs", uint32_t(gw.surface.refs));
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the plain strategy's Update 0xaed030; the missing strategy's constructor 0xadd030, Update 0xadeb90 and
// deleting destructor 0xadd620; the folded slots shared_vf_178b20 (QueryInterface), shared_vf_148fb0 (AddRef),
// shared_vf_4b1d00 (Release), shared_vf_147110 (GetDescription) and shared_vf_140fe0 (Finish) as these vtables have
// them. The plain strategy's deleting destructor 0xaebd50 is compared too but not claimed: the game's derived
// strategies still run the base constructor 0xaebd30, which stores the vtable holding it.
DIFF_TEST(SG_TextureUpdateStrategies, 0xadd030)
{
    Stands stands;
    sgfx::CheckSides(
        ctx, "texture update strategy", 1500, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            Life(ctx, original, rng);
            ++ctx.cases;
        });
}

// Verifies: 0xaed030 (FillWhite, the hook's entry).
DIFF_TEST(SG_TextureUpdateStrategy_FillWhite, 0xaed030)
{
    using Fn = void(__thiscall *)(void *, cILockableSurface *, uint32_t, bool);
    const Fn original = ctx.Original<Fn>(0xaed030);
    sgfx::CheckSides(
        ctx, "fill white", 600, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool isOriginal, std::mt19937 &rng) {
            cTextureUpdateStrategy self;
            for (unsigned i = 0, n = 1 + rng() % 4; i < n; ++i) {
                const uint32_t face = rng() % 6;
                const bool first = rng() % 2 != 0;
                if (isOriginal) original(&self, Surface(), face, first);
                else self.FillWhite(Surface(), face, first);
                NotePixels();
            }
            drawfx::Note("surface refs", uint32_t(gw.surface.refs));
            ++ctx.cases;
        });
}
