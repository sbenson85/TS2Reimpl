// cGZWinText (src/ui/cGZWinText.*) against the original, through drawfx recording fakes (tests/drawfx.h,
// docs/ui-testing.md). The label is a block of random bytes whose faces answer through embedded fakes
// (drawfx::Fake::Embed): the cGZWin face at +0 (cIGZWin), the cIGZWinText face at +0xf8 and the caption string at +0x60
// (cIGZString; its std::string at +0x64 is a valid short string, sometimes with line breaks). So every call the label
// makes on itself is logged and answered by a fake, like the calls on its font, draw context, parent window and the
// services it reaches (the UI defaults 0x10f51b0 and the colour service 0x10f51a4, set in their cache cells). Each case
// builds the same world twice, runs the original on one and ours on the other, and compares the logs (calls,
// arguments, notes of the result and the fakes' reference counts) and the object's bytes afterwards.
#include "difftest.h"
#include "code_patch.h"
#include "drawfx.h"
#include "game/game.h"
#include "ui/cGZWinText.h"

#include <array>
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

// cIGZWinText, with slot 7's flag argument compared by its low byte: SetColorRGB passes `cl` with the rest of ecx
// left from the call before (0x94a69d / 0x94a6a5).
constexpr std::array<Slot, 29> MakeTextSlots()
{
    std::array<Slot, 29> s{};
    for (unsigned i = 0; i < 29; ++i) s[i] = fx_slots::kcIGZWinTextSlots[i];
    s[7] = {s[7].words, s[7].ret, s[7].settled, s[7].name, "ub"};
    return s;
}
constexpr std::array<Slot, 29> kTextSlots = MakeTextSlots();
constexpr Iface kText{"cIGZWinText", 29, kTextSlots.data()};

// The UI defaults service (GetCachedServiceObjectB): font by id (5).
constexpr std::array<Slot, 30> MakeDefaultsSlots()
{
    std::array<Slot, 30> s{};
    for (Slot &x : s) x = kUnknown;
    s[0] = {2, Ret::Int, true, "QueryInterface", "ii"};
    s[1] = {0, Ret::Int, true, "AddRef", ""};
    s[2] = {0, Ret::Int, true, "Release", ""};
    s[5] = {1, Ret::Int, true, "GetFont", ""};
    return s;
}
constexpr std::array<Slot, 30> kDefaultsSlots = MakeDefaultsSlots();
constexpr Iface kDefaults{"UIDefaults", 30, kDefaultsSlots.data()};

// The colour service (0x9c5a05, cell 0x10f51a4): 36 splits a colour into its three channels.
constexpr std::array<Slot, 37> MakeColorSlots()
{
    std::array<Slot, 37> s{};
    for (Slot &x : s) x = kUnknown;
    s[36] = {4, Ret::Int, true, "SplitColor", ""};
    return s;
}
constexpr std::array<Slot, 37> kColorSlots = MakeColorSlots();
constexpr Iface kColors{"ColorService", 37, kColorSlots.data()};

constexpr size_t kBlock = 0x180;
constexpr uint32_t kBlockTag = 0x7b000000;
alignas(16) uint8_t gBlock[kBlock];
// The text context at +0x84, handed to the font's DrawBox (a cIGZBuffer pointer): only its address is passed on.
constexpr uint32_t kContextTag = 0x7c000000;
alignas(16) uint8_t gContext[0x40];

Fake gWin, gText, gCaption;   // embedded faces
Fake gFont, gDc, gParent, gWm, gSvc, gColors, gOwner, gOtherFont, gStr;
char gStrText[8] = "Ab\ncd";
int32_t gFontRect[4];
int32_t gArea[4];
uint32_t gMapHead[6];
uint32_t gListHead0[3];
uint32_t gListHead1[3];
uint32_t gChildHead[3];
uint32_t gChildren[3];

cGZWinText *Obj() { return reinterpret_cast<cGZWinText *>(gBlock); }
cGZWinTextFace *Face() { return reinterpret_cast<cGZWinTextFace *>(gBlock + 0xf8); }
void Set32(uint32_t offset, uint32_t value) { std::memcpy(gBlock + offset, &value, 4); }
uint32_t Get32(uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, gBlock + offset, 4);
    return v;
}

// A valid short (in-place) std::string at +0x64: up to 15 characters, some of them line breaks.
void PutCaption(std::mt19937 &rng)
{
    const uint32_t length = rng() % 4 == 0 ? 0 : rng() % 16;
    for (uint32_t i = 0; i < 16; ++i)
        gBlock[0x64 + i] = i < length ? uint8_t(rng() % 4 == 0 ? '\n' : 'a' + rng() % 26) : 0;
    Set32(0x74, length);
    Set32(0x78, 15);
}

const std::string &StringOf(Fake &f) { return *reinterpret_cast<const std::string *>(static_cast<uint8_t *>(f.owner) + 4); }

uint32_t StrData(Fake &f, const uint32_t *) { return P(StringOf(f).c_str()); }
uint32_t StrLength(Fake &f, const uint32_t *) { return uint32_t(StringOf(f).size()); }
uint32_t ArgData(Fake &, const uint32_t *) { return P(gStrText); }
uint32_t ArgLength(Fake &f, const uint32_t *) { return f.value[0]; }
uint32_t IsFlagSet(Fake &f, const uint32_t *a) { return (f.value[0] & a[0]) != 0 ? 1 + (f.value[1] & 0xfe) : 0; }
uint32_t FromRGB(Fake &, const uint32_t *a) { return a[0] * 3 + 1; }
uint32_t ToRGB(Fake &f, const uint32_t *a) { return f.value[2] ? a[0] : a[0] ^ 0xff00ff; }
uint32_t GetArea(Fake &, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gArea, sizeof(gArea));
    return 1;
}
void NoteRect(const char *label, uint32_t p)
{
    if (!p) return drawfx::Note(label, 0xdead);
    const auto *r = reinterpret_cast<const int32_t *>(uintptr_t(p));
    for (int i = 0; i < 4; ++i) drawfx::Note(label, uint32_t(r[i]));
}
uint32_t FontExtent(Fake &, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[2])), gFontRect, sizeof(gFontRect));
    return 1;
}
// Width of a run of text: its length and first character, so the runs measured are told apart.
uint32_t FontWidth(Fake &f, const uint32_t *a)
{
    const auto *text = reinterpret_cast<const uint8_t *>(uintptr_t(a[0]));
    return a[1] ? a[1] * (f.value[0] % 9 + 1) + text[0] % 7 : f.value[1];
}
uint32_t FontLines(Fake &f, const uint32_t *a) { return (a[2] % 5 + f.value[2]) % 4; }
uint32_t FontDrawBox(Fake &f, const uint32_t *a)
{
    NoteRect("box", a[1]);
    return f.ret[53];
}
uint32_t GetFont(Fake &f, const uint32_t *a)
{
    const uint32_t pick = ((a[0] ^ f.value[0]) * 0x9e3779b1u) >> 30;
    return pick == 0 ? 0 : pick == 1 ? P(&gOtherFont) : P(&gFont);
}
uint32_t SplitColor(Fake &, const uint32_t *a)
{
    for (int i = 0; i < 3; ++i) *reinterpret_cast<uint32_t *>(uintptr_t(a[1 + i])) = (a[0] >> (8 * (2 - i))) & 0xff;
    return 1;
}

int32_t Small(std::mt19937 &rng, int32_t lo, int32_t hi) { return lo + int32_t(rng() % uint32_t(hi - lo + 1)); }

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 29);
    for (uint8_t &b : gBlock) b = uint8_t(rng());
    gWin.Reset<fx_slots::cIGZWin>(1);
    gText.Reset<kText>(2);
    gCaption.Reset<fx_slots::cIGZString>(3);
    gFont.Reset<fx_slots::cIGZFont>(10);
    gOtherFont.Reset<fx_slots::cIGZFont>(11);
    gDc.Reset<fx_slots::cIGZDrawContext>(12);
    gParent.Reset<fx_slots::cIGZWin>(13);
    gWm.Reset<fx_slots::cIGZWinMgr>(14);
    gSvc.Reset<kDefaults>(20);
    gColors.Reset<kColors>(21);
    gOwner.Reset<fx_slots::cIGZWin>(22);
    gStr.Reset<fx_slots::cIGZString>(30);
    gWin.Embed(gBlock);
    gText.Embed(gBlock + 0xf8);
    gCaption.Embed(gBlock + 0x60);
    drawfx::AddRange(gBlock, kBlock, kBlockTag);

    const auto coin = [&] { return rng() % 2 == 0; };
    // The cGZWin part.
    Set32(0x04, P(&gWm));
    const int32_t left = Small(rng, -20, 100), top = Small(rng, -20, 100);
    Set32(0x34, uint32_t(left));
    Set32(0x38, uint32_t(top));
    Set32(0x3c, uint32_t(left + Small(rng, -5, 200)));
    Set32(0x40, uint32_t(top + Small(rng, -5, 100)));
    gChildHead[0] = gChildHead[1] = P(gChildHead);
    gChildren[0] = P(gChildHead);
    gChildren[1] = 0;
    gChildren[2] = 1;
    Set32(0x54, P(gChildren));
    Set32(0x58, coin() ? P(&gParent) : 0);
    PutCaption(rng);
    drawfx::AddRange(gContext, sizeof gContext, kContextTag);
    Set32(0x84, rng() % 4 ? P(gContext) : 0);
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
    // The label.
    Set32(0xfc, rng() % 3 ? P(&gFont) : 0);
    Set32(0x100, coin() ? 0 : rng());
    gBlock[0x104] = uint8_t(Small(rng, -4, 12));
    gBlock[0x105] = uint8_t(Small(rng, -4, 12));
    for (uint32_t o = 0x108; o < 0x118; o += 4) Set32(o, uint32_t(Small(rng, -50, 300)));
    uint32_t style = 0;
    for (uint32_t bit : {2u, 4u, 8u, 0x10000000u, 1u})
        if (coin()) style |= bit;
    if (rng() % 8 == 0) style = rng();
    Set32(0x118, style);
    Set32(0x124, rng() % 8 == 0 ? rng() : rng() % 9);
    for (uint32_t o = 0x128; o < 0x130; o += 4) Set32(o, coin() ? cGZWinText::kUnset : rng() & 0xffffff);

    // The fakes' answers.
    gWin.Returns(49, uint32_t(Small(rng, -10, 300)));
    gWin.Returns(50, uint32_t(Small(rng, -10, 200)));
    gWin.Returns(51, uint32_t(Small(rng, -100, 300)));
    gWin.Returns(52, uint32_t(Small(rng, -100, 300)));
    for (unsigned s : {62u, 83u, 105u, 120u, 142u, 148u, 149u}) gWin.Returns(s, rng());
    gWin.value[0] = rng();
    gWin.value[1] = rng();
    gWin.value[2] = rng() % 3 == 0;
    for (int32_t &v : gArea) v = Small(rng, -50, 300);
    gWin.On(55, GetArea);
    gWin.On(82, IsFlagSet);
    gWin.On(143, FromRGB);
    gWin.On(144, ToRGB);
    for (unsigned s = 4; s < 29; ++s) gText.Returns(s, rng() % 4 == 0 ? rng() : rng() % 2);
    gCaption.On(6, StrData);
    gCaption.On(7, StrLength);
    gParent.Returns(3, rng());
    for (Fake *f : {&gFont, &gOtherFont}) {
        f->Returns(12, rng() % 3 == 0 ? Get32(0x128) ^ 0xff00ff : rng());
        f->Returns(13, rng() % 3 == 0 ? Get32(0x12c) ^ 0xff00ff : rng());
        f->Returns(35, uint32_t(Small(rng, 0, 30)));
        f->Returns(53, rng());
        f->value[0] = rng();
        f->value[1] = rng() % 4 == 0 ? rng() : rng() % 400;
        f->value[2] = rng() % 4;
        f->On(44, FontLines);
        f->On(48, FontExtent);
        f->On(49, FontWidth);
        f->On(53, FontDrawBox);
    }
    for (int32_t &v : gFontRect) v = Small(rng, -30, 300);
    gSvc.value[0] = rng();
    gSvc.On(5, GetFont);
    gColors.On(36, SplitColor);
    gStr.value[0] = rng() % 6;
    gStr.On(6, ArgData);
    gStr.On(7, ArgLength);
}

class Services {
public:
    Services()
    {
        cells_.Set(0x10f51b0, &gSvc);
        cells_.Set(0x10f51a4, &gColors);
    }

private:
    drawfx::Cells cells_;
};

template <class F> void Check(difftest::Context &ctx, const char *what, uint32_t cases, uint32_t mask, F &&fn)
{
    Services services;
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
                                        drawfx::Note("font.refs", uint32_t(gFont.refs));
                                        drawfx::Note("otherFont.refs", uint32_t(gOtherFont.refs));
                                        drawfx::Note("owner.refs", uint32_t(gOwner.refs));
                                        std::memcpy(after[side], gBlock, kBlock);
                                    });
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

} // namespace

// ---------------------------------------------------------------------------------------------------------
// cIGZWin slots

DIFF_TEST(cGZWinText_QueryInterface, 0x94a382)
{
    Check(ctx, "QueryInterface", 300, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t riid = Pick(r, {cGZWinText::kIid, 1, 0x212cdc1e});
        void *out = nullptr;
        const uint32_t res = o ? Orig(ctx, 0x94a382, Obj(), riid, &out) : uint32_t(Obj()->QueryInterface(riid, &out));
        drawfx::Note("out", drawfx::Norm(P(out)));
        return res;
    });
}

DIFF_TEST(cGZWinText_DoMessage, 0x949ffd)
{
    Check(ctx, "DoMessage", 1000, 0xff, [&](bool o, std::mt19937 &r) {
        uint32_t message[4] = {Pick(r, {5, 6, 7, 10, 14, 15, 6000}), r(), r(), r()};
        const uint32_t res = o ? Orig(ctx, 0x949ffd, Obj(), message) : uint32_t(Obj()->DoMessage(message));
        for (uint32_t w : message) drawfx::Note("message", w);
        return res;
    });
}

DIFF_TEST(cGZWinText_Activate, 0x94a315)
{
    Check(ctx, "Activate", 1000, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x94a315, Obj()) : uint32_t(Obj()->Activate());
    });
}

DIFF_TEST(cGZWinText_Teardown, 0x94a784)
{
    Check(ctx, "Teardown", 600, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x94a784, Obj()) : uint32_t(Obj()->Teardown());
    });
}

DIFF_TEST(cGZWinText_SetArea, 0x94a428)
{
    Check(ctx, "SetArea", 1500, 0, [&](bool o, std::mt19937 &r) {
        int32_t a[4];
        for (int32_t &v : a) v = r() % 8 == 0 ? int32_t(r()) : Small(r, -100, 400);
        // Sizes equal to the current one (slots 49 / 50), and inverted areas over an empty window: the size is
        // clamped at 0 before the comparison.
        switch (r() % 4) {
        case 0:
            a[2] = a[0] + int32_t(gWin.ret[49]);
            a[3] = a[1] + int32_t(gWin.ret[50]);
            break;
        case 1:
            gWin.Returns(49, 0u);
            gWin.Returns(50, 0u);
            a[2] = a[0] - Small(r, 0, 50);
            a[3] = a[1] - Small(r, 0, 50);
            break;
        case 2:
            gWin.Returns(50, 0u);
            a[2] = a[0] + int32_t(gWin.ret[49]);
            a[3] = a[1] - Small(r, 1, 50);
            break;
        default:
            break;
        }
        if (o) return Orig(ctx, 0x94a428, Obj(), a[0], a[1], a[2], a[3]);
        Obj()->SetArea(a[0], a[1], a[2], a[3]);
        return 0u;
    });
}

DIFF_TEST(cGZWinText_SetCaption, 0x94a4a0)
{
    Check(ctx, "SetCaption", 600, ~0u, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x94a4a0, Obj(), &gStr) : Obj()->SetCaption(&gStr);
    });
}

DIFF_TEST(cGZWinText_Draw, 0x94a0bb)
{
    Check(ctx, "Draw", 2000, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x94a0bb, Obj()) : uint32_t(Obj()->Draw());
    });
}

DIFF_TEST(cGZWinText_SetBackgroundColor, 0x94a4cc)
{
    Check(ctx, "SetBackgroundColor", 300, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t v = r();
        return o ? Orig(ctx, 0x94a4cc, Obj(), v) : Obj()->SetBackgroundColor(v);
    });
}

DIFF_TEST(cGZWinText_Layout, 0x94a7a9)
{
    Check(ctx, "Layout", 4000, 0, [&](bool o, std::mt19937 &) {
        if (o) return Orig(ctx, 0x94a7a9, Obj());
        Obj()->Layout();
        return 0u;
    });
}

DIFF_TEST(cGZWinText_Measure, 0x94a916)
{
    Check(ctx, "Measure", 6000, 0, [&](bool o, std::mt19937 &r) {
        int32_t out[4] = {0x5555, 0x5555, 0x5555, 0x5555};
        const uint32_t a = Bool(r), b = Bool(r);
        if (o) Orig(ctx, 0x94a916, Obj(), out, a, b);
        else Obj()->Measure(out, a, b);
        NoteRect("out", P(out));
        return 0u;
    });
}

// ---------------------------------------------------------------------------------------------------------
// The cIGZWinText face

DIFF_TEST(cGZWinText_SizeToText, 0x94a064)
{
    Check(ctx, "SizeToText", 2000, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t a = Bool(r), b = Bool(r);
        return o ? Orig(ctx, 0x94a064, Face(), a, b) : uint32_t(Face()->SizeToText(a, b));
    });
}

DIFF_TEST(cGZWinText_SetStyle, 0x94a6ed)
{
    Check(ctx, "SetStyle", 1000, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t mask = Pick(r, {2, 4, 8, 1, 0x10000000}), on = Bool(r);
        return o ? Orig(ctx, 0x94a6ed, Face(), mask, on) : uint32_t(Face()->SetStyle(mask, on));
    });
}

DIFF_TEST(cGZWinText_SetColorRGB, 0x94a632)
{
    Check(ctx, "SetColorRGB", 1500, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t index = Pick(r, {0, 1, 2});
        const uint32_t fontColor = r() % 2 && Get32(0xfc) ? P(&gFont) : 0;
        uint32_t rgb = r() & 0xffffff;
        if (fontColor && r() % 2) rgb = gFont.ret[12 + (index & 1)] ^ 0xff00ff; // the font's own colour
        return o ? Orig(ctx, 0x94a632, Face(), index, rgb) : uint32_t(Face()->SetColorRGB(index, rgb));
    });
}

DIFF_TEST(cGZWinText_SetColor, 0x94a609)
{
    Check(ctx, "SetColor", 300, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t index = Pick(r, {0, 1, 2}), color = r();
        return o ? Orig(ctx, 0x94a609, Face(), index, color) : Face()->SetColor(index, color);
    });
}

DIFF_TEST(cGZWinText_SetTextColor, 0x94a5fb)
{
    Check(ctx, "SetTextColor", 200, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t color = r();
        return o ? Orig(ctx, 0x94a5fb, Face(), color) : Face()->SetTextColor(color);
    });
}

DIFF_TEST(cGZWinText_GetColorRGB, 0x94a2d1)
{
    Check(ctx, "GetColorRGB", 400, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t index = Pick(r, {0, 1, 2});
        uint32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x94a2d1, Face(), index, &out) : uint32_t(Face()->GetColorRGB(index, &out));
        drawfx::Note("out", out);
        return res;
    });
}

DIFF_TEST(cGZWinText_GetColor, 0x94a292)
{
    Check(ctx, "GetColor", 400, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t index = Pick(r, {0, 1, 2});
        uint32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x94a292, Face(), index, &out) : uint32_t(Face()->GetColor(index, &out));
        drawfx::Note("out", out);
        return res;
    });
}

DIFF_TEST(cGZWinText_GetTextColor, 0x94a284)
{
    Check(ctx, "GetTextColor", 200, ~0u, [&](bool o, std::mt19937 &) {
        uint32_t out = 0x5555;
        return o ? Orig(ctx, 0x94a284, Face(), &out) : Face()->GetTextColor(&out);
    });
}

DIFF_TEST(cGZWinText_ResetColor, 0x949fb9)
{
    Check(ctx, "ResetColor", 400, 0xff, [&](bool o, std::mt19937 &r) {
        // The original reads the font without a null check: only called with one.
        Set32(0xfc, P(&gFont));
        const uint32_t index = Pick(r, {0, 1, 2});
        return o ? Orig(ctx, 0x949fb9, Face(), index) : Face()->ResetColor(index);
    });
}

DIFF_TEST(cGZWinText_ResetTextColor, 0x949ff5)
{
    Check(ctx, "ResetTextColor", 200, ~0u, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x949ff5, Face()) : Face()->ResetTextColor();
    });
}

DIFF_TEST(cGZWinText_SetFill, 0x94a492)
{
    Check(ctx, "SetFill", 200, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t on = r();
        return o ? Orig(ctx, 0x94a492, Face(), on) : Face()->SetFill(on);
    });
}

DIFF_TEST(cGZWinText_IsFilled, 0x94a37a)
{
    Check(ctx, "IsFilled", 200, ~0u, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x94a37a, Face()) : Face()->IsFilled();
    });
}

DIFF_TEST(cGZWinText_SetFont, 0x94a4e6)
{
    Check(ctx, "SetFont", 1500, 0xff, [&](bool o, std::mt19937 &r) {
        void *font = r() % 3 == 0 ? nullptr : r() % 2 ? static_cast<void *>(&gFont) : &gOtherFont;
        return o ? Orig(ctx, 0x94a4e6, Face(), font) : uint32_t(Face()->SetFont(font));
    });
}

DIFF_TEST(cGZWinText_SetFontID, 0x94a576)
{
    Check(ctx, "SetFontID", 600, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t id = r() % 3 ? r() : 0;
        return o ? Orig(ctx, 0x94a576, Face(), id) : uint32_t(Face()->SetFontID(id));
    });
}

DIFF_TEST(cGZWinText_GetFontID, 0x94a23c)
{
    Check(ctx, "GetFontID", 200, 0xff, [&](bool o, std::mt19937 &) {
        uint32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x94a23c, Face(), &out) : uint32_t(Face()->GetFontID(&out));
        drawfx::Note("out", out);
        return res;
    });
}

DIFF_TEST(cGZWinText_SetAlignment, 0x94a3bb)
{
    Check(ctx, "SetAlignment", 1500, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t a = r() % 4 == 0 ? Get32(0x124) : Pick(r, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
        return o ? Orig(ctx, 0x94a3bb, Face(), a) : uint32_t(Face()->SetAlignment(a));
    });
}

DIFF_TEST(cGZWinText_GetAlignment, 0x94a22e)
{
    Check(ctx, "GetAlignment", 200, 0xff, [&](bool o, std::mt19937 &) {
        uint32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x94a22e, Face(), &out) : uint32_t(Face()->GetAlignment(&out));
        drawfx::Note("out", out);
        return res;
    });
}

DIFF_TEST(cGZWinText_SetMargins, 0x94a5bd)
{
    Check(ctx, "SetMargins", 1000, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t x = r(), y = r();
        return o ? Orig(ctx, 0x94a5bd, Face(), x, y) : uint32_t(Face()->SetMargins(int8_t(x), int8_t(y)));
    });
}

DIFF_TEST(cGZWinText_GetMargins, 0x94a252)
{
    Check(ctx, "GetMargins", 200, 0xff, [&](bool o, std::mt19937 &) {
        int8_t x = 0x55, y = 0x55;
        const uint32_t res = o ? Orig(ctx, 0x94a252, Face(), &x, &y) : uint32_t(Face()->GetMargins(&x, &y));
        drawfx::Note("x", uint32_t(x));
        drawfx::Note("y", uint32_t(y));
        return res;
    });
}

DIFF_TEST(cGZWinText_SetExtra, 0x94a6da)
{
    Check(ctx, "SetExtra", 200, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t a = r(), b = r();
        return o ? Orig(ctx, 0x94a6da, Face(), a, b) : uint32_t(Face()->SetExtra(int8_t(a), int8_t(b)));
    });
}

DIFF_TEST(cGZWinText_GetExtra, 0x94a2fe)
{
    Check(ctx, "GetExtra", 200, 0xff, [&](bool o, std::mt19937 &) {
        int8_t a = 0x55, b = 0x55;
        const uint32_t res = o ? Orig(ctx, 0x94a2fe, Face(), &a, &b) : uint32_t(Face()->GetExtra(&a, &b));
        drawfx::Note("a", uint32_t(a));
        drawfx::Note("b", uint32_t(b));
        return res;
    });
}

DIFF_TEST(cGZWinText_SetFrame, 0x94a5db)
{
    Check(ctx, "SetFrame", 300, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t on = Bool(r), color = r();
        return o ? Orig(ctx, 0x94a5db, Face(), on, color) : Face()->SetFrame(on, color);
    });
}

DIFF_TEST(cGZWinText_GetFrame, 0x94a269)
{
    Check(ctx, "GetFrame", 300, ~0u, [&](bool o, std::mt19937 &) {
        uint8_t on = 0x55;
        uint32_t color = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x94a269, Face(), &on, &color) : P(Face()->GetFrame(&on, &color));
        drawfx::Note("on", on);
        drawfx::Note("color", color);
        return drawfx::Norm(res);
    });
}

// ---------------------------------------------------------------------------------------------------------
// Construction and destruction (the cGZWin constructor allocates: the words holding heap pointers are left out of
// the byte comparison, kHeapWords).

namespace {

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
struct GameHeap {
    shape::CodePatch news{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch deletes{0x824624, shape::CodeOf(&GameDelete)};
};

constexpr uint32_t kHeapWords[] = {0x54, 0x98, 0xa0, 0xa8};

bool HeapWord(uint32_t offset)
{
    for (uint32_t w : kHeapWords)
        if (offset >= w && offset < w + 4) return true;
    return false;
}

void Lifetime(difftest::Context &ctx, const char *what, bool ctor0, bool ctor1, bool dtor0, bool dtor1)
{
    Services services;
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
                    const uint32_t result = origCtor ? Orig(ctx, 0x949ed1, Obj()) : P(Obj()->Construct());
                    drawfx::Note("result", drawfx::Norm(result));
                    std::memcpy(afterCtor[side], gBlock, kBlock);
                    Set32(0xfc, rng() % 2 ? P(&gFont) : 0);
                    Set32(0xe8, Get32(0xe8) | (rng() % 2 ? 0x4000u : 0));
                    // A live window has its manager (+4; the cGZWin constructor leaves it null): the teardown's
                    // cGZWin SetFlag (0x932c88) reaches it through 0x931743 without a null check.
                    Set32(0x04, P(&gWm));
                    if (origDtor) Orig(ctx, 0x949f68, Obj());
                    else Obj()->Destruct();
                    drawfx::Note("font.refs", uint32_t(gFont.refs));
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

DIFF_TEST(cGZWinText_Construct, 0x949ed1) { Lifetime(ctx, "Construct", true, false, true, true); }
DIFF_TEST(cGZWinText_Destruct, 0x949f68) { Lifetime(ctx, "Destruct", false, false, true, false); }

DIFF_TEST(cGZWinText_DeletingDestructor, 0x949f97)
{
    Services services;
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
                    cGZWinText *text = Obj();
                    if (flags & 1) {
                        text = static_cast<cGZWinText *>(::operator new(cGZWinText::kSize));
                        std::memcpy(static_cast<void *>(text), gBlock, cGZWinText::kSize);
                        drawfx::AddRange(text, cGZWinText::kSize, kBlockTag);
                    }
                    text->Construct();
                    const uint32_t result =
                        side == 0 ? Orig(ctx, 0x949f97, text, flags) : P(text->DeletingDestructor(flags));
                    drawfx::Note("result is this", result == P(text));
                });
            logs[side] = std::move(out.first);
            for (std::string &e : out.second) errors.push_back(std::move(e));
        }
        if (!drawfx::Same(ctx, logs[0], logs[1], "DeletingDestructor", seed, errors)) break;
    }
}
