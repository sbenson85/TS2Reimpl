// cGZWinScrollbar2 (src/ui/cGZWinScrollbar2.*) against the original, through drawfx recording fakes (tests/drawfx.h,
// docs/ui-testing.md). The bar is a block of random bytes whose faces answer through embedded fakes
// (drawfx::Fake::Embed): the cGZWin face at +0 (cIGZWin), the cIGZWinScrollbar2 face at +0xf8 and the repeat timer at
// +0x14c (cRZCallbackTimer). So every call the bar makes on itself and its timer is logged and answered by a fake, like
// the calls on its images, its draw context and the window manager. Each case builds the same world twice, runs the
// original on one and ours on the other, and compares the logs (calls, arguments, the rects and points drawn, notes of
// the result and the images' reference counts) and the object's bytes afterwards.
#include "difftest.h"
#include "code_patch.h"
#include "drawfx.h"
#include "game/game.h"
#include "rz/cRZCallbackTimer.h"
#include "ui/cGZWinScrollbar2.h"

#include <array>
#include <bit>
#include <cstring>
#include <random>
#include <string>

namespace {

using drawfx::Fake;
using drawfx::Iface;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

constexpr Slot kUnknown{0, Ret::Int, false, "", ""};

// cRZCallbackTimer (vtable 0xe61bb8): the slots the bar calls.
constexpr Slot kTimerSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ii"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {1, Ret::Int, true, "SetInterval", "i"}, kUnknown, {2, Ret::Int, true, "Start", "ii"}, kUnknown,
    {0, Ret::Int, true, "Stop", ""}, {0, Ret::Int, true, "IsRunning", ""}, {2, Ret::Int, true, "SetCallback", "ii"},
};
constexpr Iface kTimer{"cRZCallbackTimer", 10, kTimerSlots};

// The images: width 9, height 10.
constexpr Slot kImageSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ii"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown,
    {0, Ret::Int, true, "GetWidth", ""}, {0, Ret::Int, true, "GetHeight", ""},
};
constexpr Iface kImage{"Image", 11, kImageSlots};

constexpr size_t kBlock = 0x180;
constexpr uint32_t kBlockTag = 0x7b000000;
alignas(16) uint8_t gBlock[kBlock];

Fake gWin, gBar, gTimer; // embedded faces
Fake gImages[3], gDc, gWm, gOwner;
uint32_t gTickOrig = 0;
// Empty std::map / std::list heads for cGZWin's containers (Teardown clears them; nothing is freed).
uint32_t gMapHead[6];
uint32_t gListHead0[3];
uint32_t gListHead1[3];
// cGZWin's shared child list block (+0x54: list head, size, reference count), empty.
uint32_t gChildHead[3];
uint32_t gChildren[3];

cGZWinScrollbar2 *Obj() { return reinterpret_cast<cGZWinScrollbar2 *>(gBlock); }
cGZWinScrollbar2Face *Face() { return reinterpret_cast<cGZWinScrollbar2Face *>(gBlock + 0xf8); }
void Set32(uint32_t offset, uint32_t value) { std::memcpy(gBlock + offset, &value, 4); }
uint32_t Get32(uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, gBlock + offset, 4);
    return v;
}

void NoteRect(const char *label, uint32_t p)
{
    if (!p) return drawfx::Note(label, 0xdead);
    const auto *r = reinterpret_cast<const int32_t *>(uintptr_t(p));
    for (int i = 0; i < 4; ++i) drawfx::Note(label, uint32_t(r[i]));
}

// Handlers.
uint32_t IsFlagSet(Fake &f, const uint32_t *a) { return (f.value[0] & a[0]) != 0; }
uint32_t DrawImage(Fake &f, const uint32_t *a)
{
    NoteRect("source", a[1]);
    NoteRect("target", a[2]);
    return f.ret[39];
}
uint32_t DrawStretched(Fake &f, const uint32_t *a)
{
    NoteRect("stretch source", a[1]);
    NoteRect("stretch target", a[2]);
    return f.ret[40];
}
uint32_t DrawPrimitives(Fake &f, const uint32_t *a)
{
    const auto *p = reinterpret_cast<const float *>(uintptr_t(a[1]));
    for (int i = 0; i < 6; ++i) drawfx::Note("point", std::bit_cast<uint32_t>(p[i]));
    return f.ret[45];
}

int32_t Small(std::mt19937 &rng, int32_t lo, int32_t hi) { return lo + int32_t(rng() % uint32_t(hi - lo + 1)); }

// Builds the world for one case: the object's bytes, its embedded faces and the fakes' answers, all from `seed`.
void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 31);
    for (uint8_t &b : gBlock) b = uint8_t(rng());
    gWin.Reset<fx_slots::cIGZWin>(1);
    gBar.Reset<fx_slots::cIGZWinScrollbar2>(2);
    gTimer.Reset<kTimer>(3);
    for (int i = 0; i < 3; ++i) gImages[i].Reset<kImage>(10 + i);
    gDc.Reset<fx_slots::cIGZDrawContext>(13);
    gWm.Reset<fx_slots::cIGZWinMgr>(14);
    gOwner.Reset<fx_slots::cIGZWin>(15);
    gWin.Embed(gBlock);
    gBar.Embed(gBlock + 0xf8);
    gTimer.Embed(gBlock + 0x14c);
    drawfx::AddRange(gBlock, kBlock, kBlockTag);

    const auto coin = [&] { return rng() % 2 == 0; };
    // The cGZWin part.
    Set32(0x04, P(&gWm));
    gChildHead[0] = gChildHead[1] = P(gChildHead);
    gChildren[0] = P(gChildHead);
    gChildren[1] = 0;
    gChildren[2] = 1;
    Set32(0x54, P(gChildren));
    Set32(0x88, P(&gDc));
    Set32(0x94, coin() ? P(&gOwner) : 0);
    gMapHead[0] = gMapHead[1] = gMapHead[2] = P(gMapHead);
    gMapHead[3] = 0x0101;
    gListHead0[0] = gListHead0[1] = P(gListHead0);
    gListHead1[0] = gListHead1[1] = P(gListHead1);
    Set32(0x98, P(gMapHead));
    Set32(0x9c, 0);
    Set32(0xa0, P(gListHead0));
    Set32(0xa4, 0);
    Set32(0xa8, P(gListHead1));
    Set32(0xac, 0);
    const int32_t left = Small(rng, -20, 100), top = Small(rng, -20, 100);
    Set32(0xc8, uint32_t(left));
    Set32(0xcc, uint32_t(top));
    Set32(0xd0, uint32_t(left + Small(rng, -5, 260)));
    Set32(0xd4, uint32_t(top + Small(rng, -5, 260)));
    // The bar.
    const auto Word = [&](int32_t lo, int32_t hi) { return rng() % 10 == 0 ? rng() : uint32_t(Small(rng, lo, hi)); };
    Set32(0xfc, Word(-20, 300));  // value
    Set32(0x100, Word(-20, 100)); // min
    Set32(0x104, Word(-20, 400)); // max
    Set32(0x108, Word(-5, 120));  // page
    Set32(0x10c, Word(-5, 20));   // overlap
    Set32(0x110, Word(-5, 30));   // line step
    const bool images = coin();
    for (int i = 0; i < 3; ++i) Set32(0x114 + 4 * i, images && coin() ? P(&gImages[i]) : 0);
    for (uint32_t o = 0x120; o < 0x134; o += 4) Set32(o, uint32_t(Small(rng, -4, 40)));
    Set32(0x134, rng() % 3 ? 0 : uint32_t(Small(rng, 0, 60)));
    Set32(0x138, rng() % 3 ? 0 : uint32_t(Small(rng, 0, 60)));
    Set32(0x13c, uint32_t(Small(rng, -10, 200)));
    Set32(0x140, uint32_t(Small(rng, -10, 200)));
    Set32(0x144, uint32_t(Small(rng, -50, 50)));
    gBlock[0x148] = uint8_t(rng() % 4);
    gBlock[0x149] = uint8_t(rng() % 5 == 0 ? rng() : rng() % 2);
    gBlock[0x14a] = uint8_t(rng() % 7);
    gBlock[0x14b] = uint8_t(rng() % 7);

    // The fakes' answers.
    gWin.value[0] = rng();
    gWin.On(82, IsFlagSet);
    for (unsigned s : {3u, 78u, 83u, 105u, 109u, 113u, 148u, 149u, 166u, 168u}) gWin.Returns(s, rng());
    for (unsigned s = 4; s < 26; ++s) gBar.Returns(s, rng() % 4 == 0 ? rng() : rng() % 2);
    gBar.Returns(3, gBlock);
    gTimer.Returns(8, rng() % 2);
    for (unsigned s : {3u, 5u, 7u, 9u}) gTimer.Returns(s, rng());
    for (Fake &image : gImages) {
        image.Returns(9, rng() % 6 == 0 ? rng() : uint32_t(Small(rng, 0, 200)));
        image.Returns(10, rng() % 6 == 0 ? rng() : uint32_t(Small(rng, 0, 200)));
    }
    gDc.On(39, DrawImage);
    gDc.On(40, DrawStretched);
    gDc.On(45, DrawPrimitives);
    for (unsigned s : {29u, 30u, 31u}) gWm.Returns(s, rng() % 3 == 0 ? P(gBlock) : rng() % 2 ? 0 : rng());
}

// The repeat callback's address differs (the original's 0x95b10e, our RepeatTick): both log as one value.
void Normalise(drawfx::Log &log)
{
    for (drawfx::Call &c : log)
        if (c.iface == &kTimer && c.slot == 9 && !c.words.empty() &&
            (c.words[0] == gTickOrig ||
             c.words[0] == P(reinterpret_cast<const void *>(&cGZWinScrollbar2::RepeatTick))))
            c.words[0] = 0x71c4;
}

// Runs `cases` cases of fn(original, rng): fn calls the original (original true) or ours with arguments drawn from
// rng, which starts the same for both sides. `mask` keeps the result's meaningful bits (0xff for an al result, 0 for
// none).
template <class F> void Check(difftest::Context &ctx, const char *what, uint32_t cases, uint32_t mask, F &&fn)
{
    gTickOrig = P(ctx.image.At(0x95b10e));
    static uint8_t after[2][kBlock];
    for (uint32_t seed = 0; seed < cases; ++seed) {
        drawfx::Log logs[2];
        std::vector<std::string> errors;
        for (int side = 0; side < 2; ++side) {
            auto out = drawfx::Side([&] { Build(seed); },
                                    [&] {
                                        std::mt19937 rng(seed * 2654435761u + 7);
                                        const uint32_t result = fn(side == 0, rng);
                                        drawfx::Note("result", result & mask);
                                        for (Fake &image : gImages) drawfx::Note("image.refs", uint32_t(image.refs));
                                        drawfx::Note("owner.refs", uint32_t(gOwner.refs));
                                        std::memcpy(after[side], gBlock, kBlock);
                                    });
            Normalise(out.first);
            logs[side] = std::move(out.first);
            for (std::string &e : out.second) errors.push_back(std::move(e));
        }
        if (!drawfx::Same(ctx, logs[0], logs[1], what, seed, errors)) break;
        size_t i = 0;
        while (i < kBlock && after[0][i] == after[1][i]) ++i;
        if (i < kBlock) {
            ctx.Fail("%s seed %u: object byte +%#x differs: orig %02x ours %02x", what, seed, unsigned(i), after[0][i],
                     after[1][i]);
            break;
        }
    }
}

template <class... A> uint32_t Orig(difftest::Context &ctx, uint32_t rva, void *self, A... a)
{
    return ctx.Original<uint32_t(__thiscall *)(void *, A...)>(rva)(self, a...);
}

uint32_t Pick(std::mt19937 &rng, std::initializer_list<uint32_t> values)
{
    if (rng() % 6 == 0) return rng();
    return values.begin()[rng() % values.size()];
}
uint32_t Bool(std::mt19937 &rng) { return (rng() & 0xffffff00u) | (rng() % 3 == 0 ? 0 : rng() % 2 ? 1 : rng() & 0xff); }
// A point on or around the bar.
int32_t Coord(std::mt19937 &rng, uint32_t lo, uint32_t hi)
{
    const int32_t size = int32_t(Get32(hi) - Get32(lo));
    return Small(rng, -8, size > 0 ? size + 8 : 8);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// cIGZWin slots

DIFF_TEST(cGZWinScrollbar2_QueryInterface, 0x95ae54)
{
    Check(ctx, "QueryInterface", 300, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t riid = Pick(r, {cGZWinScrollbar2::kIid, 1, 0x22ba0121, 0xe98b2f57, 0x4b42da52});
        void *out = reinterpret_cast<void *>(uintptr_t(0x5555));
        const uint32_t res = o ? Orig(ctx, 0x95ae54, Obj(), riid, &out) : uint32_t(Obj()->QueryInterface(riid, &out));
        drawfx::Note("out", drawfx::Norm(P(out)));
        return res & 0xff;
    });
}

DIFF_TEST(cGZWinScrollbar2_DoMessage, 0x959b69)
{
    Check(ctx, "DoMessage", 1500, 0xff, [&](bool o, std::mt19937 &r) {
        uint32_t message[4] = {Pick(r, {cGZWinScrollbar2::kRepeatMessage, cGZWinScrollbar2::kRepeatMessage, 5, 7}), r(),
                               r(), r()};
        if (r() % 2) gBlock[0x14a] = gBlock[0x14b];
        return o ? Orig(ctx, 0x959b69, Obj(), message) : uint32_t(Obj()->DoMessage(message));
    });
}

DIFF_TEST(cGZWinScrollbar2_Activate, 0x95ad1d)
{
    Check(ctx, "Activate", 600, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x95ad1d, Obj()) : uint32_t(Obj()->Activate());
    });
}

DIFF_TEST(cGZWinScrollbar2_Teardown, 0x95b0c3)
{
    Check(ctx, "Teardown", 600, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x95b0c3, Obj()) : uint32_t(Obj()->Teardown());
    });
}

DIFF_TEST(cGZWinScrollbar2_SetArea, 0x95ae8d)
{
    Check(ctx, "SetArea", 1500, ~0u, [&](bool o, std::mt19937 &r) {
        int32_t a[4];
        for (int32_t &v : a) v = r() % 8 == 0 ? int32_t(r()) : Small(r, -100, 400);
        return o ? Orig(ctx, 0x95ae8d, Obj(), a[0], a[1], a[2], a[3]) : Obj()->SetArea(a[0], a[1], a[2], a[3]);
    });
}

DIFF_TEST(cGZWinScrollbar2_Draw, 0x959f6c)
{
    Check(ctx, "Draw", 6000, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x959f6c, Obj()) : uint32_t(Obj()->Draw());
    });
}

DIFF_TEST(cGZWinScrollbar2_OnMouseDown, 0x959c88)
{
    Check(ctx, "OnMouseDown", 3000, 0xff, [&](bool o, std::mt19937 &r) {
        const int32_t x = Coord(r, 0xc8, 0xd0), y = Coord(r, 0xcc, 0xd4);
        const uint32_t keys = r();
        return o ? Orig(ctx, 0x959c88, Obj(), x, y, keys) : uint32_t(Obj()->OnMouseDown(x, y, keys));
    });
}

DIFF_TEST(cGZWinScrollbar2_OnMouseUp, 0x959ee7)
{
    Check(ctx, "OnMouseUp", 1500, 0xff, [&](bool o, std::mt19937 &r) {
        const int32_t x = Coord(r, 0xc8, 0xd0), y = Coord(r, 0xcc, 0xd4);
        const uint32_t keys = r();
        return o ? Orig(ctx, 0x959ee7, Obj(), x, y, keys) : uint32_t(Obj()->OnMouseUp(x, y, keys));
    });
}

DIFF_TEST(cGZWinScrollbar2_OnMouseMove, 0x959de2)
{
    Check(ctx, "OnMouseMove", 3000, 0xff, [&](bool o, std::mt19937 &r) {
        const int32_t x = Coord(r, 0xc8, 0xd0), y = Coord(r, 0xcc, 0xd4);
        const uint32_t keys = r();
        if (r() % 2) gBlock[0x14b] = 5;
        return o ? Orig(ctx, 0x959de2, Obj(), x, y, keys) : uint32_t(Obj()->OnMouseMove(x, y, keys));
    });
}

DIFF_TEST(cGZWinScrollbar2_OnMouseExit, 0x959d96)
{
    Check(ctx, "OnMouseExit", 600, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t keys = r();
        return o ? Orig(ctx, 0x959d96, Obj(), keys) : uint32_t(Obj()->OnMouseExit(keys));
    });
}

// ---------------------------------------------------------------------------------------------------------
// Non-virtual members

DIFF_TEST(cGZWinScrollbar2_MeasureParts, 0x95980e)
{
    Check(ctx, "MeasureParts", 3000, 0, [&](bool o, std::mt19937 &) {
        if (o) return Orig(ctx, 0x95980e, Obj());
        Obj()->MeasureParts();
        return 0u;
    });
}

DIFF_TEST(cGZWinScrollbar2_PlaceThumb, 0x959a77)
{
    Check(ctx, "PlaceThumb", 4000, 0, [&](bool o, std::mt19937 &) {
        if (o) return Orig(ctx, 0x959a77, Obj());
        Obj()->PlaceThumb();
        return 0u;
    });
}

DIFF_TEST(cGZWinScrollbar2_HitTest, 0x95ad49)
{
    Check(ctx, "HitTest", 4000, ~0u, [&](bool o, std::mt19937 &r) {
        const int32_t x = Coord(r, 0xc8, 0xd0), y = Coord(r, 0xcc, 0xd4);
        return o ? Orig(ctx, 0x95ad49, Obj(), x, y) : uint32_t(Obj()->HitTest(x, y));
    });
}

DIFF_TEST(cGZWinScrollbar2_RepeatTick, 0x95b10e)
{
    Check(ctx, "RepeatTick", 200, 0, [&](bool o, std::mt19937 &) {
        if (o) ctx.Original<void(__cdecl *)(void *)>(0x95b10e)(Obj());
        else cGZWinScrollbar2::RepeatTick(Obj());
        return 0u;
    });
}

// ---------------------------------------------------------------------------------------------------------
// The cIGZWinScrollbar2 face

DIFF_TEST(cGZWinScrollbar2_SetOrientation, 0x95afb6)
{
    Check(ctx, "SetOrientation", 400, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t v = r() % 3 == 0 ? gBlock[0x149] : Pick(r, {0, 1, 2, 0x101});
        return o ? Orig(ctx, 0x95afb6, Face(), v) : uint32_t(Face()->SetOrientation(v));
    });
}

DIFF_TEST(cGZWinScrollbar2_GetOrientation, 0x95ace4)
{
    Check(ctx, "GetOrientation", 200, 0xff, [&](bool o, std::mt19937 &) {
        uint32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x95ace4, Face(), &out) : uint32_t(Face()->GetOrientation(&out));
        drawfx::Note("out", out);
        return res;
    });
}

DIFF_TEST(cGZWinScrollbar2_SetValue, 0x95b015)
{
    Check(ctx, "SetValue", 2000, 0xff, [&](bool o, std::mt19937 &r) {
        const int32_t v = r() % 4 == 0 ? int32_t(Get32(0xfc)) : r() % 8 == 0 ? int32_t(r()) : Small(r, -50, 450);
        const uint32_t notify = Bool(r);
        return o ? Orig(ctx, 0x95b015, Face(), v, notify) : uint32_t(Face()->SetValue(v, notify));
    });
}

DIFF_TEST(cGZWinScrollbar2_GetValue, 0x95ad01)
{
    Check(ctx, "GetValue", 200, 0xff, [&](bool o, std::mt19937 &) {
        int32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x95ad01, Face(), &out) : uint32_t(Face()->GetValue(&out));
        drawfx::Note("out", uint32_t(out));
        return res;
    });
}

DIFF_TEST(cGZWinScrollbar2_SetMin, 0x95af83)
{
    Check(ctx, "SetMin", 600, 0xff, [&](bool o, std::mt19937 &r) {
        const int32_t v = r() % 4 == 0 ? int32_t(Get32(0x100)) : Small(r, -50, 200);
        const uint32_t notify = Bool(r);
        return o ? Orig(ctx, 0x95af83, Face(), v, notify) : uint32_t(Face()->SetMin(v, notify));
    });
}

DIFF_TEST(cGZWinScrollbar2_SetMax, 0x95af50)
{
    Check(ctx, "SetMax", 600, 0xff, [&](bool o, std::mt19937 &r) {
        const int32_t v = r() % 4 == 0 ? int32_t(Get32(0x104)) : Small(r, -50, 400);
        const uint32_t notify = Bool(r);
        return o ? Orig(ctx, 0x95af50, Face(), v, notify) : uint32_t(Face()->SetMax(v, notify));
    });
}

DIFF_TEST(cGZWinScrollbar2_GetMax, 0x95ac9f)
{
    Check(ctx, "GetMax", 200, 0xff, [&](bool o, std::mt19937 &) {
        int32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x95ac9f, Face(), &out) : uint32_t(Face()->GetMax(&out));
        drawfx::Note("out", uint32_t(out));
        return res;
    });
}

DIFF_TEST(cGZWinScrollbar2_SetPageSize, 0x95b089)
{
    Check(ctx, "SetPageSize", 600, 0xff, [&](bool o, std::mt19937 &r) {
        const int32_t v = r() % 4 == 0 ? int32_t(Get32(0x108)) : Small(r, -20, 150);
        const uint32_t notify = Bool(r);
        return o ? Orig(ctx, 0x95b089, Face(), v, notify) : uint32_t(Face()->SetPageSize(v, notify));
    });
}

DIFF_TEST(cGZWinScrollbar2_SetPageOverlap, 0x95afdb)
{
    Check(ctx, "SetPageOverlap", 300, 0xff, [&](bool o, std::mt19937 &r) {
        const int32_t v = r() % 4 == 0 ? int32_t(r()) : Small(r, -20, 20);
        return o ? Orig(ctx, 0x95afdb, Face(), v) : uint32_t(Face()->SetPageOverlap(v));
    });
}

DIFF_TEST(cGZWinScrollbar2_SetLineStep, 0x95af3d)
{
    Check(ctx, "SetLineStep", 300, 0xff, [&](bool o, std::mt19937 &r) {
        const int32_t v = r() % 4 == 0 ? int32_t(r()) : Small(r, -20, 20);
        return o ? Orig(ctx, 0x95af3d, Face(), v) : uint32_t(Face()->SetLineStep(v));
    });
}

namespace {
void *PickImage(std::mt19937 &r, int own)
{
    const uint32_t k = r() % 4;
    return k == 0 ? nullptr : k == 1 ? reinterpret_cast<void *>(uintptr_t(Get32(0x114 + 4 * own))) : &gImages[r() % 3];
}
} // namespace

DIFF_TEST(cGZWinScrollbar2_SetArrowImage, 0x95aeef)
{
    Check(ctx, "SetArrowImage", 400, 0xff, [&](bool o, std::mt19937 &r) {
        void *image = PickImage(r, 0);
        return o ? Orig(ctx, 0x95aeef, Face(), image) : uint32_t(Face()->SetArrowImage(image));
    });
}

DIFF_TEST(cGZWinScrollbar2_SetTrackImage, 0x95af16)
{
    Check(ctx, "SetTrackImage", 400, 0xff, [&](bool o, std::mt19937 &r) {
        void *image = PickImage(r, 1);
        return o ? Orig(ctx, 0x95af16, Face(), image) : uint32_t(Face()->SetTrackImage(image));
    });
}

DIFF_TEST(cGZWinScrollbar2_SetThumbImage, 0x95afee)
{
    Check(ctx, "SetThumbImage", 400, 0xff, [&](bool o, std::mt19937 &r) {
        void *image = PickImage(r, 2);
        return o ? Orig(ctx, 0x95afee, Face(), image) : uint32_t(Face()->SetThumbImage(image));
    });
}

DIFF_TEST(cGZWinScrollbar2_GetMinSize, 0x95acad)
{
    Check(ctx, "GetMinSize", 600, 0xff, [&](bool o, std::mt19937 &) {
        int32_t out[2] = {0x5555, 0x5555};
        const uint32_t res = o ? Orig(ctx, 0x95acad, Face(), out) : uint32_t(Face()->GetMinSize(out));
        drawfx::Note("width", uint32_t(out[0]));
        drawfx::Note("height", uint32_t(out[1]));
        return res;
    });
}

// ---------------------------------------------------------------------------------------------------------
// Construction and destruction. The cGZWin constructor (original on both sides) allocates its child-list block and
// containers from the heap, so the words holding heap pointers differ between the sides: they are left out of the
// byte comparison (kHeapWords; everything else is compared).

namespace {

constexpr uint32_t kHeapWords[] = {0x54, 0x98, 0xa0, 0xa8};

// The game's operator new (0x824632) and sized delete (0x824624), redirected to ours for the scope of a test: the
// image's CRT heap is not initialised in the harness.
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
// The repeat timer's constructor and destructor (0x9ccf32 / 0x9ccfa9) register the timer in a global set under a
// global critical section, neither of which the harness initialises: stood in for, the constructor makes the timer
// the gTimer fake (and both are logged).
void *__fastcall TimerCtor(void *self, void *)
{
    drawfx::Note("timer constructed", 1);
    gTimer.Embed(self);
    return self;
}
void __fastcall TimerDtor(void *, void *) { drawfx::Note("timer destroyed", 1); }
struct GameHeap {
    shape::CodePatch news{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch deletes{0x824624, shape::CodeOf(&GameDelete)};
    shape::StandIn timerCtor{0x9ccf32, &cRZCallbackTimer::Construct, &TimerCtor};
    shape::StandIn timerDtor{0x9ccfa9, &cRZCallbackTimer::Destruct, &TimerDtor};
};

bool HeapWord(uint32_t offset)
{
    for (uint32_t w : kHeapWords)
        if (offset >= w && offset < w + 4) return true;
    return false;
}

// Side 0 runs the original constructor / destructor when ctor0 / dtor0, side 1 when ctor1 / dtor1 (else ours).
// Between them the images are set at random, so the destructor's releases run.
void Lifetime(difftest::Context &ctx, const char *what, bool ctor0, bool ctor1, bool dtor0, bool dtor1)
{
    GameHeap heap;
    static uint8_t afterCtor[2][kBlock];
    static uint8_t afterDtor[2][kBlock];
    for (uint32_t seed = 0; seed < 300; ++seed) {
        drawfx::Log logs[2];
        std::vector<std::string> errors;
        for (int side = 0; side < 2; ++side) {
            const bool origCtor = side == 0 ? ctor0 : ctor1;
            const bool origDtor = side == 0 ? dtor0 : dtor1;
            auto out = drawfx::Side(
                [&] { Build(seed); },
                [&] {
                    std::mt19937 rng(seed * 2654435761u + 7);
                    const uint32_t result = origCtor ? Orig(ctx, 0x9596cd, Obj()) : P(Obj()->Construct());
                    drawfx::Note("result", drawfx::Norm(result));
                    std::memcpy(afterCtor[side], gBlock, kBlock);
                    for (int i = 0; i < 3; ++i) Set32(0x114 + 4 * i, rng() % 2 ? P(&gImages[i]) : 0);
                    Set32(0xe8, Get32(0xe8) | (rng() % 2 ? 0x4000u : 0));
                    // A live window has its manager (+4; the cGZWin constructor leaves it null): the teardown's
                    // cGZWin SetFlag (0x932c88) reaches it through 0x931743 without a null check.
                    Set32(0x04, P(&gWm));
                    if (origDtor) Orig(ctx, 0x959799, Obj());
                    else Obj()->Destruct();
                    for (Fake &image : gImages) drawfx::Note("image.refs", uint32_t(image.refs));
                    std::memcpy(afterDtor[side], gBlock, kBlock);
                });
            logs[side] = std::move(out.first);
            for (std::string &e : out.second) errors.push_back(std::move(e));
        }
        if (!drawfx::Same(ctx, logs[0], logs[1], what, seed, errors)) break;
        bool failed = false;
        for (int pass = 0; pass < 2 && !failed; ++pass) {
            const uint8_t(*a)[kBlock] = pass == 0 ? afterCtor : afterDtor;
            for (uint32_t i = 0; i < kBlock; ++i)
                if (!HeapWord(i) && a[0][i] != a[1][i]) {
                    ctx.Fail("%s seed %u: after the %s, object byte +%#x differs: orig %02x ours %02x", what, seed,
                             pass == 0 ? "constructor" : "destructor", i, a[0][i], a[1][i]);
                    failed = true;
                    break;
                }
        }
        if (failed) break;
    }
}

} // namespace

DIFF_TEST(cGZWinScrollbar2_Construct, 0x9596cd) { Lifetime(ctx, "Construct", true, false, true, true); }
DIFF_TEST(cGZWinScrollbar2_Destruct, 0x959799) { Lifetime(ctx, "Destruct", false, false, true, false); }

// The deleting destructor on a heap object (flag 1 frees it with the game's operator delete; both CRTs share the
// process heap) and on the block (flag clear): the calls it makes and its result.
DIFF_TEST(cGZWinScrollbar2_DeletingDestructor, 0x9597ec)
{
    GameHeap heap;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        drawfx::Log logs[2];
        std::vector<std::string> errors;
        for (int side = 0; side < 2; ++side) {
            auto out = drawfx::Side(
                [&] { Build(seed); },
                [&] {
                    std::mt19937 rng(seed + 99);
                    const uint32_t flags = (rng() & ~1u) | (seed % 2);
                    cGZWinScrollbar2 *bar = Obj();
                    if (flags & 1) {
                        bar = static_cast<cGZWinScrollbar2 *>(::operator new(cGZWinScrollbar2::kSize));
                        std::memcpy(static_cast<void *>(bar), gBlock, cGZWinScrollbar2::kSize);
                        drawfx::AddRange(bar, cGZWinScrollbar2::kSize, kBlockTag);
                    }
                    bar->Construct();
                    const uint32_t result =
                        side == 0 ? Orig(ctx, 0x9597ec, bar, flags) : P(bar->DeletingDestructor(flags));
                    drawfx::Note("result is this", result == P(bar));
                });
            logs[side] = std::move(out.first);
            for (std::string &e : out.second) errors.push_back(std::move(e));
        }
        if (!drawfx::Same(ctx, logs[0], logs[1], "DeletingDestructor", seed, errors)) break;
    }
}
