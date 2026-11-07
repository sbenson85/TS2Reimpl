// cGZWinBtn (src/ui/cGZWinBtn.*) against the original, through drawfx recording fakes (tests/drawfx.h,
// docs/ui-testing.md). The button is a block of random bytes whose faces answer through embedded fakes
// (drawfx::Fake::Embed): the cIGZWinBtn face at +0 (53 slots), the cGZWin face at +4 (cIGZWin), the caption string
// at +0x64 and the tooltip string at +0x188 (cIGZString, answering Data / Strlen from the std::string behind them)
// and the flash timer at +0x1c4 (cRZCallbackTimer). So every call the button makes on itself, its window, its strings
// and its timer is logged and answered by a fake, like the calls on the font, the image, the draw contexts and the
// services it reaches (the UI defaults 0x10f51b0, the window manager 0x10f51ac, the tooltip window 0x10f5198 and the
// audio service 0x10f51b4, set in their cache cells). Each case builds the same world twice, runs the original on
// one and ours on the other, and compares the logs (calls, arguments, notes of the result and the fakes' reference
// counts) and the object's bytes afterwards.
#include "difftest.h"
#include "code_patch.h"
#include "drawfx.h"
#include "game/game.h"
#include "rz/cRZCallbackTimer.h"
#include "ui/cGZWinBtn.h"

#include <array>
#include <cstring>
#include <random>
#include <string>

namespace {

using drawfx::Fake;
using drawfx::Iface;
using drawfx::Ret;
using drawfx::Slot;

constexpr Slot kUnknown{0, Ret::Int, false, "", ""};

// cGZWinBtn's vtable at +0: cIGZWinBtn's 51 slots, then its own NotifyToggled / NotifyClicked.
constexpr std::array<Slot, 53> MakeBtnSlots()
{
    std::array<Slot, 53> s{};
    for (unsigned i = 0; i < 51; ++i) s[i] = fx_slots::kcIGZWinBtnSlots[i];
    s[51] = {0, Ret::Int, true, "NotifyToggled", ""};
    s[52] = {0, Ret::Int, true, "NotifyClicked", ""};
    return s;
}
constexpr std::array<Slot, 53> kBtnSlots = MakeBtnSlots();
constexpr Iface kBtn{"cGZWinBtn", 53, kBtnSlots.data()};

// cRZCallbackTimer (vtable 0xe61bb8): the slots the button calls.
constexpr Slot kTimerSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ii"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    kUnknown, kUnknown, {2, Ret::Int, true, "Start", "ii"}, kUnknown, {0, Ret::Int, true, "Stop", ""},
    {0, Ret::Int, true, "IsRunning", ""}, {2, Ret::Int, true, "SetCallback", "ii"},
};
constexpr Iface kTimer{"cRZCallbackTimer", 10, kTimerSlots};

// The UI defaults service (GetCachedServiceObjectB): font by id (5), image by id (7), default sound (29).
constexpr std::array<Slot, 30> MakeDefaultsSlots()
{
    std::array<Slot, 30> s{};
    for (Slot &x : s) x = kUnknown;
    s[0] = {2, Ret::Int, true, "QueryInterface", "ii"};
    s[1] = {0, Ret::Int, true, "AddRef", ""};
    s[2] = {0, Ret::Int, true, "Release", ""};
    s[5] = {1, Ret::Int, true, "GetFont", ""};
    s[7] = {1, Ret::Int, true, "GetImage", ""};
    s[29] = {1, Ret::Int, true, "GetDefaultSound", ""};
    return s;
}
constexpr std::array<Slot, 30> kDefaultsSlots = MakeDefaultsSlots();
constexpr Iface kDefaults{"UIDefaults", 30, kDefaultsSlots.data()};

// The audio service (0x9c5b7e): 13 creates a sound object, 12 plays it.
constexpr Slot kAudioSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ii"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown,
    {1, Ret::Int, true, "Play", ""}, {7, Ret::Int, true, "Create", ""},
};
constexpr Iface kAudio{"Audio", 14, kAudioSlots};

// An image (also the sound object): width 9, height 10, rect 12.
constexpr Slot kImageSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ii"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown,
    {0, Ret::Int, true, "GetWidth", ""}, {0, Ret::Int, true, "GetHeight", ""}, kUnknown,
    {0, Ret::Int, true, "GetRect", ""},
};
constexpr Iface kImage{"Image", 13, kImageSlots};

constexpr size_t kBlock = 0x200;
constexpr uint32_t kBlockTag = 0x7b000000;
alignas(16) uint8_t gBlock[kBlock];

Fake gBtn, gWin, gCaption, gTip, gTimer;               // embedded faces
Fake gFont, gImage, gDc, gTextDc, gSvc, gWm, gTips, gAudio, gSound, gStr;
int32_t gLimits[4];
int32_t gImageRect[4];
int32_t gFontRect[4];
char gStrText[8] = "Abc";
// Empty std::map / std::list heads for cGZWin's containers (Teardown clears them; nothing is freed).
uint32_t gMapHead[6];
uint32_t gListHead0[3];
uint32_t gListHead1[3];
// cGZWin's shared child list block (+0x54 of the face: list head, size, reference count), empty.
uint32_t gChildHead[3];
uint32_t gChildren[3];

cGZWinBtn *Btn() { return reinterpret_cast<cGZWinBtn *>(gBlock); }
cGZWinBtnWin *Face() { return reinterpret_cast<cGZWinBtnWin *>(gBlock + 4); }
using drawfx::P;
void Set32(uint32_t offset, uint32_t value) { std::memcpy(gBlock + offset, &value, 4); }
void Set16(uint32_t offset, uint16_t value) { std::memcpy(gBlock + offset, &value, 2); }

// A valid short (in-place) std::string at `offset` of the block: up to 15 characters.
void PutString(uint32_t offset, std::mt19937 &rng)
{
    const uint32_t length = rng() % 3 == 0 ? 0 : rng() % 16;
    for (uint32_t i = 0; i < 16; ++i) gBlock[offset + i] = i < length ? uint8_t('a' + rng() % 26) : 0;
    Set32(offset + 16, length);
    Set32(offset + 20, 15);
}

const std::string &StringOf(Fake &f) { return *reinterpret_cast<const std::string *>(static_cast<uint8_t *>(f.owner) + 4); }

// Handlers.
uint32_t StrData(Fake &f, const uint32_t *) { return P(StringOf(f).c_str()); }
uint32_t StrLength(Fake &f, const uint32_t *) { return uint32_t(StringOf(f).size()); }
uint32_t ArgData(Fake &, const uint32_t *) { return P(gStrText); }
uint32_t ArgLength(Fake &f, const uint32_t *) { return f.value[0]; }
uint32_t IsFlagSet(Fake &f, const uint32_t *a) { return (f.value[0] & a[0]) != 0; }
uint32_t ClientToScreen(Fake &f, const uint32_t *a)
{
    *reinterpret_cast<int32_t *>(uintptr_t(a[0])) += int32_t(f.value[1]);
    *reinterpret_cast<int32_t *>(uintptr_t(a[1])) += int32_t(f.value[2]);
    return 0;
}
uint32_t FromRGB(Fake &, const uint32_t *a) { return a[0] * 3 + 1; }
uint32_t ToRGB(Fake &, const uint32_t *a) { return a[0] ^ 0xff00ff; }
uint32_t SizeLimits(Fake &f, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gLimits, sizeof(gLimits));
    return f.value[0];
}
void NoteRect(const char *label, uint32_t p)
{
    if (!p) return drawfx::Note(label, 0xdead);
    const auto *r = reinterpret_cast<const int32_t *>(uintptr_t(p));
    for (int i = 0; i < 4; ++i) drawfx::Note(label, uint32_t(r[i]));
}
uint32_t FontLines(Fake &f, const uint32_t *a)
{
    *reinterpret_cast<uint8_t *>(uintptr_t(a[4])) = uint8_t(f.value[1]);
    return f.value[0];
}
uint32_t FontExtent(Fake &, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[2])), gFontRect, sizeof(gFontRect));
    return 1;
}
uint32_t FontDrawBox(Fake &f, const uint32_t *a)
{
    NoteRect("box", a[1]);
    return f.ret[53];
}
// cIGZWinBtn slot 12 (SetImage) called on itself: the rect it is given.
uint32_t SetImageArgs(Fake &f, const uint32_t *a)
{
    NoteRect("image rect", a[2]);
    return f.ret[12];
}
uint32_t DrawImage(Fake &f, const uint32_t *a)
{
    NoteRect("source", a[1]);
    NoteRect("target", a[2]);
    return f.ret[39];
}
uint32_t GetFont(Fake &f, const uint32_t *a)
{
    return ((a[0] ^ f.value[0]) * 0x9e3779b1u) >> 30 ? P(&gFont) : 0;
}
uint32_t GetImage(Fake &f, const uint32_t *) { return f.value[1] ? P(&gImage) : 0; }
uint32_t DefaultSound(Fake &f, const uint32_t *a) { return a[0] * 0x101 + f.value[2]; }
uint32_t CreateSound(Fake &f, const uint32_t *a)
{
    *reinterpret_cast<void **>(uintptr_t(a[2])) = f.value[1] ? &gSound : nullptr;
    return f.value[0] & 1;
}
uint32_t ImageRect(Fake &, const uint32_t *) { return P(gImageRect); }
// The tooltip window's DoMessage: notes the message and, for a show request, the request; answers the tooltip id.
uint32_t TooltipMessage(Fake &f, const uint32_t *a)
{
    const auto *m = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    drawfx::Note("message", m[0]);
    drawfx::Note("message.2", m[2]);
    drawfx::Note("message.3", m[3]);
    if (m[0] == 0x22c010d0) {
        auto *q = reinterpret_cast<uint32_t *>(uintptr_t(m[1]));
        for (int i = 0; i < 10; ++i) drawfx::Note("request", i == 2 ? drawfx::Norm(q[i]) : q[i]);
        q[10] = f.value[0];
    } else {
        drawfx::Note("message.1", m[1]);
    }
    return 1;
}

int32_t Small(std::mt19937 &rng, int32_t lo, int32_t hi) { return lo + int32_t(rng() % uint32_t(hi - lo + 1)); }

// Builds the world for one case: the object's bytes, its embedded faces and the fakes' answers, all from `seed`.
void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 13);
    for (uint8_t &b : gBlock) b = uint8_t(rng());
    gBtn.Reset<kBtn>(1);
    gWin.Reset<fx_slots::cIGZWin>(2);
    gCaption.Reset<fx_slots::cIGZString>(3);
    gTip.Reset<fx_slots::cIGZString>(4);
    gTimer.Reset<kTimer>(5);
    gFont.Reset<fx_slots::cIGZFont>(10);
    gImage.Reset<kImage>(11);
    gDc.Reset<fx_slots::cIGZDrawContext>(12);
    gTextDc.Reset<fx_slots::cIGZDrawContext>(13);
    gSvc.Reset<kDefaults>(20);
    gWm.Reset<fx_slots::cIGZWinMgr>(21);
    gTips.Reset<fx_slots::cIGZWin>(22);
    gAudio.Reset<kAudio>(23);
    gSound.Reset<kImage>(24);
    gStr.Reset<fx_slots::cIGZString>(30);
    gBtn.Embed(gBlock);
    gWin.Embed(gBlock + 4);
    gCaption.Embed(gBlock + 0x64);
    gTip.Embed(gBlock + 0x188);
    gTimer.Embed(gBlock + 0x1c4);
    drawfx::AddRange(gBlock, kBlock, kBlockTag);

    const auto coin = [&] { return rng() % 2 == 0; };
    // The cGZWin part.
    Set32(0x08, P(&gWm));
    for (uint32_t o = 0x28; o < 0x38; o += 4) Set32(o, uint32_t(Small(rng, -50, 400)));
    const int32_t left = Small(rng, -20, 100), top = Small(rng, -20, 100);
    Set32(0x38, uint32_t(left));
    Set32(0x3c, uint32_t(top));
    Set32(0x40, uint32_t(left + Small(rng, -5, 200)));
    Set32(0x44, uint32_t(top + Small(rng, -5, 100)));
    PutString(0x68, rng);
    Set32(0x88, P(&gTextDc));
    Set32(0x8c, P(&gDc));
    Set32(0x98, coin() ? P(&gSound) : 0);
    gMapHead[0] = gMapHead[1] = gMapHead[2] = P(gMapHead);
    gMapHead[3] = 0x0101;
    gListHead0[0] = gListHead0[1] = P(gListHead0);
    gListHead1[0] = gListHead1[1] = P(gListHead1);
    Set32(0x9c, P(gMapHead));
    Set32(0xa0, 0);
    Set32(0xa4, P(gListHead0));
    Set32(0xa8, 0);
    Set32(0xac, P(gListHead1));
    Set32(0xb0, 0);
    gChildHead[0] = gChildHead[1] = P(gChildHead);
    gChildren[0] = P(gChildHead);
    gChildren[1] = 0;
    gChildren[2] = 1;
    Set32(0x58, P(gChildren));
    // The button.
    static const uint32_t kStyleBits[] = {1, 2, 4, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000,
                                          0x4000, 0x1000000, 0x10000000};
    uint32_t style = 0;
    for (uint32_t bit : kStyleBits)
        if (coin()) style |= bit;
    if (rng() % 8 == 0) style = rng();
    Set32(0xfc, style);
    for (uint32_t o = 0x120; o < 0x128; o += 2) Set16(o, uint16_t(Small(rng, -40, 160)));
    Set32(0x128, rng() % 8 == 0 ? rng() : rng() % 4);
    for (uint32_t o = 0x12e; o < 0x132; ++o) gBlock[o] = uint8_t(rng() % 16);
    Set32(0x134, coin() ? P(&gFont) : 0);
    Set32(0x138, coin() ? 0 : rng());
    for (uint32_t o = 0x13c; o < 0x154; o += 4) Set32(o, coin() ? 0x8000000 : rng());
    for (uint32_t o = 0x154; o < 0x160; o += 4) Set32(o, coin() ? 0 : rng());
    Set32(0x160, rng() % 8 == 0 ? rng() : rng() % 6);
    Set32(0x164, rng() % 8 == 0 ? rng() : rng() % 6);
    const int32_t il = Small(rng, 0, 60), it = Small(rng, 0, 60);
    Set32(0x168, uint32_t(il));
    Set32(0x16c, uint32_t(it));
    Set32(0x170, uint32_t(il + Small(rng, -3, 120)));
    Set32(0x174, uint32_t(it + Small(rng, -3, 60)));
    for (uint32_t o = 0x178; o < 0x184; o += 4) Set32(o, uint32_t(Small(rng, -20, 200)));
    Set32(0x184, coin() ? P(&gImage) : 0);
    PutString(0x18c, rng);
    Set32(0x1ac, rng() % 4 == 0 ? rng() : (rng() & 0x10003));
    Set16(0x1b0, uint16_t(Small(rng, -100, 100)));
    Set16(0x1b2, uint16_t(Small(rng, -100, 100)));
    static const int32_t kCounts[] = {-2, -1, 0, 1, 2, 3};
    Set32(0x1c0, rng() % 8 == 0 ? rng() : uint32_t(kCounts[rng() % 6]));

    // The fakes' answers.
    gWin.Returns(49, uint32_t(Small(rng, -10, 300)));
    gWin.Returns(50, uint32_t(Small(rng, -10, 200)));
    gWin.Returns(134, rng() % 2);
    for (unsigned s : {3u, 62u, 64u, 83u, 105u, 141u, 148u, 149u, 166u, 168u}) gWin.Returns(s, rng());
    gWin.value[0] = rng();
    gWin.value[1] = uint32_t(Small(rng, -500, 500));
    gWin.value[2] = uint32_t(Small(rng, -500, 500));
    gWin.On(82, IsFlagSet);
    gWin.On(75, ClientToScreen);
    gWin.On(143, FromRGB);
    gWin.On(144, ToRGB);
    for (unsigned s = 3; s < 53; ++s) gBtn.Returns(s, rng() % 4 == 0 ? rng() : rng() % 2);
    gBtn.Returns(3, coin() ? gBlock + 4 : nullptr);
    gBtn.Returns(48, rng());
    gBtn.Returns(50, gBlock + 0x64);
    gBtn.value[0] = rng() % 2;
    for (int32_t &v : gLimits) v = coin() ? 0 : Small(rng, -5, 300);
    gBtn.On(34, SizeLimits);
    gBtn.On(12, SetImageArgs);
    gCaption.On(6, StrData);
    gCaption.On(7, StrLength);
    gTip.On(6, StrData);
    gTip.On(7, StrLength);
    gTimer.Returns(8, rng() % 2);
    gTimer.Returns(5, rng());
    gFont.Returns(12, rng());
    gFont.Returns(13, rng());
    gFont.Returns(35, uint32_t(Small(rng, 0, 30)));
    gFont.Returns(45, rng() % 4 == 0 ? rng() : uint32_t(Small(rng, 0, 400)));
    gFont.Returns(52, rng());
    gFont.Returns(53, rng());
    gFont.value[0] = rng() % 4;
    gFont.value[1] = rng();
    for (int32_t &v : gFontRect) v = Small(rng, -30, 300);
    gFont.On(43, FontLines);
    gFont.On(48, FontExtent);
    gFont.On(53, FontDrawBox);
    gDc.Returns(39, rng());
    gDc.On(39, DrawImage);
    gImage.Returns(9, uint32_t(Small(rng, 0, 400)));
    gImage.Returns(10, uint32_t(Small(rng, 0, 100)));
    for (int32_t &v : gImageRect) v = Small(rng, 0, 300);
    gImage.On(12, ImageRect);
    gSvc.value[0] = rng();
    gSvc.value[1] = rng() % 2;
    gSvc.value[2] = rng();
    gSvc.On(5, GetFont);
    gSvc.On(7, GetImage);
    gSvc.On(29, DefaultSound);
    gWm.Returns(29, rng() % 3 == 0 ? P(&gTips) : coin() ? P(gBlock + 4) : 0);
    gWm.Returns(12, rng());
    gTips.value[0] = rng();
    gTips.On(3, TooltipMessage);
    gAudio.value[0] = rng();
    gAudio.value[1] = rng() % 2;
    gAudio.On(13, CreateSound);
    gStr.value[0] = rng() % 4;
    gStr.On(6, ArgData);
    gStr.On(7, ArgLength);
}

class Services {
public:
    Services()
    {
        cells_.Set(0x10f51b0, &gSvc);
        cells_.Set(0x10f51ac, &gWm);
        cells_.Set(0x10f5198, &gTips);
        cells_.Set(0x10f51b4, &gAudio);
    }

private:
    drawfx::Cells cells_;
};

uint32_t gFlashOrig = 0;

// The flash callback's address differs (the original's 0x936a35, our FlashTick): both log as one value.
void Normalise(drawfx::Log &log)
{
    for (drawfx::Call &c : log)
        if (c.iface == &kTimer && c.slot == 9 && !c.words.empty() &&
            (c.words[0] == gFlashOrig || c.words[0] == P(reinterpret_cast<const void *>(&cGZWinBtn::FlashTick))))
            c.words[0] = 0xf1a5;
}

// Runs `cases` cases of fn(original, rng): fn calls the original (original true) or ours with arguments drawn from
// rng, which starts the same for both sides. `mask` keeps the result's meaningful bits (0xff for an al result, 0 for
// none).
template <class F> void Check(difftest::Context &ctx, const char *what, uint32_t cases, uint32_t mask, F &&fn)
{
    gFlashOrig = P(ctx.image.At(0x936a35));
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
                                        drawfx::Note("image.refs", uint32_t(gImage.refs));
                                        drawfx::Note("sound.refs", uint32_t(gSound.refs));
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

// Argument helpers.
uint32_t Pick(std::mt19937 &rng, std::initializer_list<uint32_t> values)
{
    if (rng() % 6 == 0) return rng();
    return values.begin()[rng() % values.size()];
}
uint32_t Byte(std::mt19937 &rng, uint32_t low) { return (rng() & 0xffffff00u) | (low & 0xff); }
uint32_t Bool(std::mt19937 &rng) { return Byte(rng, rng() % 3 == 0 ? 0 : rng() % 2 ? 1 : rng()); }
uint32_t Index(std::mt19937 &rng, uint32_t count) { return rng() % count; }

const std::initializer_list<uint32_t> kMasks = {1, 4, 0x20, 0x40, 0x100, 0x1000, 0x2000, 0x4000, 0x1000000, 0x800, 0x80};

} // namespace

// ---------------------------------------------------------------------------------------------------------
// cIGZWinBtn slots

DIFF_TEST(cGZWinBtn_QueryInterface, 0x93603e)
{
    Check(ctx, "QueryInterface", 400, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t riid = Pick(r, {0x8810, 1, 0x8811, 0x4132242b});
        void *out = nullptr;
        const uint32_t res = o ? Orig(ctx, 0x93603e, Btn(), riid, &out) : Btn()->QueryInterface(riid, &out);
        drawfx::Note("out", drawfx::Norm(P(out)));
        return res;
    });
}

DIFF_TEST(cGZWinBtn_IsStyleSet, 0x935b92)
{
    Check(ctx, "IsStyleSet", 300, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t mask = r() % 2 ? 1u << (r() % 32) : r();
        return o ? Orig(ctx, 0x935b92, Btn(), mask) : uint32_t(Btn()->IsStyleSet(mask));
    });
}

DIFF_TEST(cGZWinBtn_SetStyle, 0x9360a6)
{
    Check(ctx, "SetStyle", 2000, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t mask = Pick(r, kMasks), on = Bool(r);
        return o ? Orig(ctx, 0x9360a6, Btn(), mask, on) : uint32_t(Btn()->SetStyle(mask, on));
    });
}

DIFF_TEST(cGZWinBtn_SetStyleBits, 0x936f94)
{
    Check(ctx, "SetStyleBits", 2000, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t mask = Pick(r, kMasks), on = Bool(r);
        return o ? Orig(ctx, 0x936f94, Btn(), mask, on) : uint32_t(Btn()->SetStyleBits(mask, on));
    });
}

DIFF_TEST(cGZWinBtn_SetOn, 0x9366a7)
{
    Check(ctx, "SetOn", 300, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x9366a7, Btn()) : uint32_t(Btn()->SetOn());
    });
}

DIFF_TEST(cGZWinBtn_SetOff, 0x93669d)
{
    Check(ctx, "SetOff", 300, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x93669d, Btn()) : uint32_t(Btn()->SetOff());
    });
}

DIFF_TEST(cGZWinBtn_IsOn, 0x936036)
{
    Check(ctx, "IsOn", 200, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x936036, Btn()) : uint32_t(Btn()->IsOn());
    });
}

DIFF_TEST(cGZWinBtn_SetOnState, 0x93620f)
{
    Check(ctx, "SetOnState", 400, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t on = Bool(r);
        return o ? Orig(ctx, 0x93620f, Btn(), on) : uint32_t(Btn()->SetOnState(on));
    });
}

DIFF_TEST(cGZWinBtn_SetImage, 0x93648e)
{
    Check(ctx, "SetImage", 3000, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t type = Pick(r, {0, 1, 2, 3, 4, 5});
        void *image = r() % 3 ? static_cast<void *>(&gImage) : nullptr;
        int32_t rect[4];
        for (int32_t &v : rect) v = Small(r, -20, 300);
        const int32_t *pr = r() % 2 ? rect : nullptr;
        return o ? Orig(ctx, 0x93648e, Btn(), type, image, pr) : uint32_t(Btn()->SetImage(type, image, pr));
    });
}

DIFF_TEST(cGZWinBtn_SetType, 0x9360d0)
{
    Check(ctx, "SetType", 600, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t type = Pick(r, {0, 1, 2, 3, 4, 5});
        return o ? Orig(ctx, 0x9360d0, Btn(), type) : uint32_t(Btn()->SetType(type));
    });
}

DIFF_TEST(cGZWinBtn_GetImageWidth, 0x935cae)
{
    Check(ctx, "GetImageWidth", 200, ~0u, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x935cae, Btn()) : uint32_t(Btn()->GetImageWidth());
    });
}

DIFF_TEST(cGZWinBtn_GetImageHeight, 0x935ca1)
{
    Check(ctx, "GetImageHeight", 200, ~0u, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x935ca1, Btn()) : uint32_t(Btn()->GetImageHeight());
    });
}

DIFF_TEST(cGZWinBtn_GetImageRect, 0x935c8a)
{
    Check(ctx, "GetImageRect", 200, 0xff, [&](bool o, std::mt19937 &) {
        int32_t out[4] = {1, 2, 3, 4};
        const uint32_t res = o ? Orig(ctx, 0x935c8a, Btn(), out) : uint32_t(Btn()->GetImageRect(out));
        NoteRect("out", P(out));
        return res;
    });
}

DIFF_TEST(cGZWinBtn_SetImageResource, 0x9360ab)
{
    Check(ctx, "SetImageResource", 600, 0xff, [&](bool o, std::mt19937 &r) {
        void *image = r() % 3 ? static_cast<void *>(&gImage) : nullptr;
        return o ? Orig(ctx, 0x9360ab, Btn(), image) : uint32_t(Btn()->SetImageResource(image));
    });
}

DIFF_TEST(cGZWinBtn_GetImage, 0x935b97)
{
    Check(ctx, "GetImage", 200, 0xff, [&](bool o, std::mt19937 &) {
        void *out = nullptr;
        const uint32_t res = o ? Orig(ctx, 0x935b97, Btn(), &out) : uint32_t(Btn()->GetImage(&out));
        drawfx::Note("out", drawfx::Norm(P(out)));
        return res;
    });
}

DIFF_TEST(cGZWinBtn_SetFont, 0x936278)
{
    Check(ctx, "SetFont", 1000, 0xff, [&](bool o, std::mt19937 &r) {
        void *font = r() % 3 ? static_cast<void *>(&gFont) : nullptr;
        return o ? Orig(ctx, 0x936278, Btn(), font) : uint32_t(Btn()->SetFont(font));
    });
}

DIFF_TEST(cGZWinBtn_SetFontID, 0x93638e)
{
    Check(ctx, "SetFontID", 600, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t id = r() % 3 ? r() : 0;
        return o ? Orig(ctx, 0x93638e, Btn(), id) : uint32_t(Btn()->SetFontID(id));
    });
}

DIFF_TEST(cGZWinBtn_GetFontID, 0x935c11)
{
    Check(ctx, "GetFontID", 300, 0xff, [&](bool o, std::mt19937 &) {
        uint32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x935c11, Btn(), &out) : uint32_t(Btn()->GetFontID(&out));
        drawfx::Note("out", out);
        return res;
    });
}

DIFF_TEST(cGZWinBtn_SetColorRGB, 0x936354)
{
    Check(ctx, "SetColorRGB", 600, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t index = Index(r, 6);
        uint32_t rgb = r();
        if (r() % 3 == 0) rgb = (*reinterpret_cast<uint32_t *>(gBlock + 0x13c + 4 * index) - 1) / 3; // the same colour
        return o ? Orig(ctx, 0x936354, Btn(), index, rgb) : uint32_t(Btn()->SetColorRGB(index, rgb));
    });
}

DIFF_TEST(cGZWinBtn_SetColor, 0x93632c)
{
    Check(ctx, "SetColor", 600, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t index = Index(r, 6);
        uint32_t argb = r() % 2 ? r() : r() % 2 ? 0x8000000 : *reinterpret_cast<uint32_t *>(gBlock + 0x13c + 4 * index);
        return o ? Orig(ctx, 0x93632c, Btn(), index, argb) : uint32_t(Btn()->SetColor(index, int32_t(argb)));
    });
}

DIFF_TEST(cGZWinBtn_GetColorRGB, 0x935be3)
{
    Check(ctx, "GetColorRGB", 400, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t index = Index(r, 6);
        uint32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x935be3, Btn(), index, &out) : uint32_t(Btn()->GetColorRGB(index, &out));
        drawfx::Note("out", out);
        return res;
    });
}

DIFF_TEST(cGZWinBtn_GetColor, 0x935bc1)
{
    Check(ctx, "GetColor", 400, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t index = Index(r, 6);
        int32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x935bc1, Btn(), index, &out) : uint32_t(Btn()->GetColor(index, &out));
        drawfx::Note("out", uint32_t(out));
        return res;
    });
}

DIFF_TEST(cGZWinBtn_ResetColor, 0x93556d)
{
    Check(ctx, "ResetColor", 200, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t index = Index(r, 6);
        return o ? Orig(ctx, 0x93556d, Btn(), index) : uint32_t(Btn()->ResetColor(index));
    });
}

DIFF_TEST(cGZWinBtn_SetAlignment, 0x9361b1)
{
    Check(ctx, "SetAlignment", 800, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t a = Pick(r, {0, 1, 2, 3});
        return o ? Orig(ctx, 0x9361b1, Btn(), a) : uint32_t(Btn()->SetAlignment(a));
    });
}

DIFF_TEST(cGZWinBtn_GetAlignment, 0x935bb0)
{
    Check(ctx, "GetAlignment", 200, 0xff, [&](bool o, std::mt19937 &) {
        uint32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x935bb0, Btn(), &out) : uint32_t(Btn()->GetAlignment(&out));
        drawfx::Note("out", out);
        return res;
    });
}

DIFF_TEST(cGZWinBtn_SetMargins, 0x9363f7)
{
    Check(ctx, "SetMargins", 800, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t a = Byte(r, r() % 20), b = Byte(r, r() % 20), c = Byte(r, r() % 20), d = Byte(r, r() % 20);
        return o ? Orig(ctx, 0x9363f7, Btn(), a, b, c, d)
                 : uint32_t(Btn()->SetMargins(uint8_t(a), uint8_t(b), uint8_t(c), uint8_t(d)));
    });
}

DIFF_TEST(cGZWinBtn_SetMargins2, 0x9363df)
{
    Check(ctx, "SetMargins2", 300, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t h = r(), v = r();
        return o ? Orig(ctx, 0x9363df, Btn(), h, v) : Btn()->SetMargins2(h, v);
    });
}

DIFF_TEST(cGZWinBtn_GetMargins, 0x935c2a)
{
    Check(ctx, "GetMargins", 300, 0xff, [&](bool o, std::mt19937 &) {
        uint32_t out[4] = {0x55555555, 0x55555555, 0x55555555, 0x55555555};
        const uint32_t res = o ? Orig(ctx, 0x935c2a, Btn(), &out[0], &out[1], &out[2], &out[3])
                               : uint32_t(Btn()->GetMargins(&out[0], &out[1], &out[2], &out[3]));
        NoteRect("out", P(out));
        return res;
    });
}

DIFF_TEST(cGZWinBtn_GetMargins2, 0x935c63)
{
    Check(ctx, "GetMargins2", 300, 0xff, [&](bool o, std::mt19937 &) {
        uint32_t out[2] = {0x55555555, 0x55555555};
        const uint32_t res =
            o ? Orig(ctx, 0x935c63, Btn(), &out[0], &out[1]) : uint32_t(Btn()->GetMargins2(&out[0], &out[1]));
        drawfx::Note("out0", out[0]);
        drawfx::Note("out1", out[1]);
        return res;
    });
}

DIFF_TEST(cGZWinBtn_SizeToFit, 0x93563a)
{
    Check(ctx, "SizeToFit", 400, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x93563a, Btn()) : uint32_t(Btn()->SizeToFit());
    });
}

DIFF_TEST(cGZWinBtn_GetSizeLimits, 0x935cbb)
{
    Check(ctx, "GetSizeLimits", 3000, 0xff, [&](bool o, std::mt19937 &) {
        int32_t out[4] = {0x5555, 0x5555, 0x5555, 0x5555};
        const uint32_t res = o ? Orig(ctx, 0x935cbb, Btn(), out) : uint32_t(Btn()->GetSizeLimits(out));
        NoteRect("out", P(out));
        return res;
    });
}

DIFF_TEST(cGZWinBtn_SetTooltip, 0x9365f2)
{
    Check(ctx, "SetTooltip", 400, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x9365f2, Btn(), &gStr) : uint32_t(Btn()->SetTooltip(&gStr));
    });
}

DIFF_TEST(cGZWinBtn_GetTooltip, 0x935f1c)
{
    Check(ctx, "GetTooltip", 200, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x935f1c, Btn(), &gStr) : uint32_t(Btn()->GetTooltip(&gStr));
    });
}

DIFF_TEST(cGZWinBtn_ClearTooltip, 0x935580)
{
    Check(ctx, "ClearTooltip", 300, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x935580, Btn()) : uint32_t(Btn()->ClearTooltip());
    });
}

DIFF_TEST(cGZWinBtn_SetTooltipFlags, 0x9365e3)
{
    Check(ctx, "SetTooltipFlags", 200, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t f = r();
        return o ? Orig(ctx, 0x9365e3, Btn(), f) : uint32_t(Btn()->SetTooltipFlags(f));
    });
}

DIFF_TEST(cGZWinBtn_GetTooltipFlags, 0x935f0b)
{
    Check(ctx, "GetTooltipFlags", 200, 0xff, [&](bool o, std::mt19937 &) {
        uint32_t out = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x935f0b, Btn(), &out) : uint32_t(Btn()->GetTooltipFlags(&out));
        drawfx::Note("out", out);
        return res;
    });
}

DIFF_TEST(cGZWinBtn_SetTooltipOffset, 0x9365c6)
{
    Check(ctx, "SetTooltipOffset", 300, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t x = r(), y = r();
        return o ? Orig(ctx, 0x9365c6, Btn(), x, y) : uint32_t(Btn()->SetTooltipOffset(int16_t(x), int16_t(y)));
    });
}

DIFF_TEST(cGZWinBtn_GetTooltipOffset, 0x935eec)
{
    Check(ctx, "GetTooltipOffset", 300, 0xff, [&](bool o, std::mt19937 &) {
        int32_t x = 0x5555, y = 0x5555;
        const uint32_t res = o ? Orig(ctx, 0x935eec, Btn(), &x, &y) : uint32_t(Btn()->GetTooltipOffset(&x, &y));
        drawfx::Note("x", uint32_t(x));
        drawfx::Note("y", uint32_t(y));
        return res;
    });
}

DIFF_TEST(cGZWinBtn_SetTooltipColor, 0x93659d)
{
    Check(ctx, "SetTooltipColor", 300, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t i = Index(r, 3), cr = r(), cg = r(), cb = r();
        return o ? Orig(ctx, 0x93659d, Btn(), i, cr, cg, cb)
                 : uint32_t(Btn()->SetTooltipColor(i, uint8_t(cr), uint8_t(cg), uint8_t(cb)));
    });
}

DIFF_TEST(cGZWinBtn_GetTooltipColor, 0x935eb5)
{
    Check(ctx, "GetTooltipColor", 300, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t i = Index(r, 3);
        uint8_t c[3] = {0x55, 0x55, 0x55};
        const uint32_t res = o ? Orig(ctx, 0x935eb5, Btn(), i, &c[0], &c[1], &c[2])
                               : uint32_t(Btn()->GetTooltipColor(i, &c[0], &c[1], &c[2]));
        drawfx::Note("rgb", uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2]);
        return res;
    });
}

DIFF_TEST(cGZWinBtn_Flash, 0x93567b)
{
    Check(ctx, "Flash", 600, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t count = Pick(r, {0, 1, 2, 0xffffffff}), interval = r();
        return o ? Orig(ctx, 0x93567b, Btn(), count, interval) : Btn()->Flash(int32_t(count), interval);
    });
}

DIFF_TEST(cGZWinBtn_SetSound, 0x93647a)
{
    Check(ctx, "SetSound", 200, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t i = Index(r, 3), s = r();
        return o ? Orig(ctx, 0x93647a, Btn(), i, s) : uint32_t(Btn()->SetSound(i, s));
    });
}

DIFF_TEST(cGZWinBtn_GetSound, 0x935ea7)
{
    Check(ctx, "GetSound", 200, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t i = Index(r, 3);
        return o ? Orig(ctx, 0x935ea7, Btn(), i) : Btn()->GetSound(i);
    });
}

DIFF_TEST(cGZWinBtn_SetID, 0x93644a)
{
    Check(ctx, "SetID", 200, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t id = r();
        return o ? Orig(ctx, 0x93644a, Btn(), id) : Btn()->SetID(id);
    });
}

DIFF_TEST(cGZWinBtn_GetID, 0x935c82)
{
    Check(ctx, "GetID", 200, ~0u, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x935c82, Btn()) : Btn()->GetID();
    });
}

DIFF_TEST(cGZWinBtn_SetCaption, 0x93613c)
{
    Check(ctx, "SetCaption", 800, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x93613c, Btn(), &gStr) : uint32_t(Btn()->SetCaption(&gStr));
    });
}

DIFF_TEST(cGZWinBtn_GetCaption, 0x935ba8)
{
    Check(ctx, "GetCaption", 100, ~0u, [&](bool o, std::mt19937 &) {
        return drawfx::Norm(o ? Orig(ctx, 0x935ba8, Btn()) : P(Btn()->GetCaption()));
    });
}

DIFF_TEST(cGZWinBtn_NotifyToggled, 0x93550a)
{
    Check(ctx, "NotifyToggled", 300, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x93550a, Btn()) : uint32_t(Btn()->NotifyToggled());
    });
}

DIFF_TEST(cGZWinBtn_NotifyClicked, 0x935541)
{
    Check(ctx, "NotifyClicked", 300, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x935541, Btn()) : uint32_t(Btn()->NotifyClicked());
    });
}

// ---------------------------------------------------------------------------------------------------------
// Non-virtual members

DIFF_TEST(cGZWinBtn_HideTooltip, 0x936a6c)
{
    Check(ctx, "HideTooltip", 200, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x936a6c, Btn()) : uint32_t(Btn()->HideTooltip());
    });
}

DIFF_TEST(cGZWinBtn_ShowTooltip, 0x9370a2)
{
    Check(ctx, "ShowTooltip", 1500, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x9370a2, Btn()) : uint32_t(Btn()->ShowTooltip());
    });
}

DIFF_TEST(cGZWinBtn_Layout, 0x936aa8)
{
    Check(ctx, "Layout", 6000, 0xff, [&](bool o, std::mt19937 &r) {
        const int32_t w = Small(r, -10, 300), h = Small(r, -10, 200);
        return o ? Orig(ctx, 0x936aa8, Btn(), w, h) : uint32_t(Btn()->Layout(w, h));
    });
}

DIFF_TEST(cGZWinBtn_PlayEffect, 0x936f30)
{
    Check(ctx, "PlayEffect", 600, 0, [&](bool o, std::mt19937 &r) {
        const uint32_t i = Index(r, 3);
        if (o) return Orig(ctx, 0x936f30, Btn(), i);
        Btn()->PlayEffect(i);
        return 0u;
    });
}

DIFF_TEST(cGZWinBtn_UpdateTextExtent, 0x93723e)
{
    Check(ctx, "UpdateTextExtent", 3000, 0, [&](bool o, std::mt19937 &) {
        if (o) return Orig(ctx, 0x93723e, Btn());
        Btn()->UpdateTextExtent();
        return 0u;
    });
}

DIFF_TEST(cGZWinBtn_PlaceText, 0x9371b4)
{
    Check(ctx, "PlaceText", 1000, 0, [&](bool o, std::mt19937 &r) {
        int32_t rect[4];
        for (int32_t &v : rect) v = Small(r, -100, 400);
        if (o) return Orig(ctx, 0x9371b4, Btn(), rect);
        Btn()->PlaceText(rect);
        return 0u;
    });
}

DIFF_TEST(cGZWinBtn_DrawImage, 0x9366b1)
{
    // Only Draw calls it, when there is an image: the original's nine-slice path (0x92bd70) queries the image
    // without a null check.
    Check(ctx, "DrawImage", 1500, 0xff, [&](bool o, std::mt19937 &) {
        Set32(0x184, P(&gImage));
        return o ? Orig(ctx, 0x9366b1, Btn()) : uint32_t(Btn()->DrawImage());
    });
}

DIFF_TEST(cGZWinBtn_DrawBevel, 0x9367b9)
{
    Check(ctx, "DrawBevel", 1500, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x9367b9, Btn()) : uint32_t(Btn()->DrawBevel());
    });
}

DIFF_TEST(cGZWinBtn_DrawCaption, 0x936894)
{
    Check(ctx, "DrawCaption", 2000, 0, [&](bool o, std::mt19937 &) {
        if (o) return Orig(ctx, 0x936894, Btn());
        Btn()->DrawCaption();
        return 0u;
    });
}

DIFF_TEST(cGZWinBtn_FlashTick, 0x936a35)
{
    Check(ctx, "FlashTick", 200, 0, [&](bool o, std::mt19937 &) {
        if (o) ctx.Original<void(__cdecl *)(void *)>(0x936a35)(Btn());
        else cGZWinBtn::FlashTick(Btn());
        return 0u;
    });
}

// ---------------------------------------------------------------------------------------------------------
// The cGZWin face

DIFF_TEST(cGZWinBtn_DoMessage, 0x9355bd)
{
    Check(ctx, "DoMessage", 800, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t message[4] = {r() % 4 ? cGZWinBtn::kTimerMessage : r(), r(), r(), r()};
        return o ? Orig(ctx, 0x9355bd, Face(), message) : uint32_t(Face()->DoMessage(message));
    });
}

DIFF_TEST(cGZWinBtn_Activate, 0x935f35)
{
    Check(ctx, "Activate", 1000, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x935f35, Face()) : uint32_t(Face()->Activate());
    });
}

DIFF_TEST(cGZWinBtn_Teardown, 0x936640)
{
    Check(ctx, "Teardown", 600, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x936640, Face()) : uint32_t(Face()->Teardown());
    });
}

DIFF_TEST(cGZWinBtn_SetArea, 0x936060)
{
    Check(ctx, "SetArea", 1000, 0xff, [&](bool o, std::mt19937 &r) {
        int32_t a[4];
        for (int32_t &v : a) v = r() % 8 == 0 ? int32_t(r()) : Small(r, -100, 500);
        return o ? Orig(ctx, 0x936060, Face(), a[0], a[1], a[2], a[3]) : uint32_t(Face()->SetArea(a[0], a[1], a[2], a[3]));
    });
}

DIFF_TEST(cGZWinBtn_SetFlag, 0x93621d)
{
    Check(ctx, "SetFlag", 800, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t mask = Pick(r, {2, 0x800, 0x80000000}), on = Bool(r);
        return o ? Orig(ctx, 0x93621d, Face(), mask, on) : Face()->SetFlag(mask, on);
    });
}

DIFF_TEST(cGZWinBtn_SetFontStyle, 0x936452)
{
    Check(ctx, "SetFontStyle", 200, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t v = r();
        return o ? Orig(ctx, 0x936452, Face(), v) : Face()->SetFontStyle(v);
    });
}

DIFF_TEST(cGZWinBtn_Draw, 0x935b5f)
{
    Check(ctx, "Draw", 1500, 0xff, [&](bool o, std::mt19937 &) {
        return o ? Orig(ctx, 0x935b5f, Face()) : uint32_t(Face()->Draw());
    });
}

DIFF_TEST(cGZWinBtn_OnKeyDown, 0x93571b)
{
    Check(ctx, "OnKeyDown", 800, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t key = Pick(r, {0xd, 0x20, 0x21}), mods = r();
        return o ? Orig(ctx, 0x93571b, Face(), key, mods) : uint32_t(Face()->OnKeyDown(key, mods));
    });
}

DIFF_TEST(cGZWinBtn_OnKeyUp, 0x935797)
{
    Check(ctx, "OnKeyUp", 800, ~0u, [&](bool o, std::mt19937 &r) {
        const uint32_t key = Pick(r, {0xd, 0x20, 0x21}), mods = r();
        return o ? Orig(ctx, 0x935797, Face(), key, mods) : Face()->OnKeyUp(key, mods);
    });
}

namespace {
using MouseMember = bool (cGZWinBtnWin::*)(uint32_t, uint32_t, uint32_t);
void MouseTest(difftest::Context &ctx, const char *what, uint32_t rva, MouseMember ours)
{
    Check(ctx, what, 1500, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t x = uint32_t(Small(r, -50, 400)), y = uint32_t(Small(r, -50, 400)), keys = r();
        return o ? Orig(ctx, rva, Face(), x, y, keys) : uint32_t((Face()->*ours)(x, y, keys));
    });
}
} // namespace

DIFF_TEST(cGZWinBtn_OnMouseDown, 0x9357ef) { MouseTest(ctx, "OnMouseDown", 0x9357ef, &cGZWinBtnWin::OnMouseDown); }
DIFF_TEST(cGZWinBtn_OnMouseDownR, 0x935883) { MouseTest(ctx, "OnMouseDownR", 0x935883, &cGZWinBtnWin::OnMouseDownR); }
DIFF_TEST(cGZWinBtn_OnMouseUp, 0x9359f1) { MouseTest(ctx, "OnMouseUp", 0x9359f1, &cGZWinBtnWin::OnMouseUp); }
DIFF_TEST(cGZWinBtn_OnMouseUpR, 0x935a9b) { MouseTest(ctx, "OnMouseUpR", 0x935a9b, &cGZWinBtnWin::OnMouseUpR); }
DIFF_TEST(cGZWinBtn_OnMouseMove, 0x93596f) { MouseTest(ctx, "OnMouseMove", 0x93596f, &cGZWinBtnWin::OnMouseMove); }

DIFF_TEST(cGZWinBtn_OnFocusChange, 0x9356e2)
{
    Check(ctx, "OnFocusChange", 400, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t lost = r();
        void *gained = r() % 2 ? static_cast<void *>(Face()) : r() % 2 ? nullptr : &gTips;
        return o ? Orig(ctx, 0x9356e2, Face(), lost, gained) : uint32_t(Face()->OnFocusChange(lost, gained));
    });
}

DIFF_TEST(cGZWinBtn_OnMouseEnter, 0x935921)
{
    Check(ctx, "OnMouseEnter", 600, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t keys = r();
        return o ? Orig(ctx, 0x935921, Face(), keys) : uint32_t(Face()->OnMouseEnter(keys));
    });
}

DIFF_TEST(cGZWinBtn_OnMouseExit, 0x935950)
{
    Check(ctx, "OnMouseExit", 300, 0xff, [&](bool o, std::mt19937 &r) {
        const uint32_t keys = r();
        return o ? Orig(ctx, 0x935950, Face(), keys) : uint32_t(Face()->OnMouseExit(keys));
    });
}

// ---------------------------------------------------------------------------------------------------------
// Construction and destruction. The cGZWin constructor (original on both sides) allocates its child-list block,
// containers and strings from the heap, so the words holding heap pointers differ between the sides: they are left
// out of the byte comparison (kHeapWords; everything else is compared).

namespace {

constexpr uint32_t kHeapWords[] = {0x58, 0x9c, 0xa4, 0xac};

// The game's operator new (0x824632) and sized delete (0x824624), redirected to ours for the scope of a test: the
// image's CRT heap is not initialised in the harness.
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
// The flash timer's constructor and destructor (0x9ccf32 / 0x9ccfa9, called by both sides' button constructor and
// destructor) register the timer in a global set under a global critical section, neither of which the harness
// initialises: stood in for, the constructor makes the timer the gTimer fake (and both are logged).
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
// Between them the image, the font and flag 0x4000 of the window are set at random, so the destructor's Teardown and
// releases run.
void Lifetime(difftest::Context &ctx, const char *what, bool ctor0, bool ctor1, bool dtor0, bool dtor1)
{
    gFlashOrig = P(ctx.image.At(0x936a35));
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
                    const uint32_t result = origCtor ? Orig(ctx, 0x93536f, Btn()) : P(Btn()->Construct());
                    drawfx::Note("result", drawfx::Norm(result));
                    std::memcpy(afterCtor[side], gBlock, kBlock);
                    Set32(0x134, rng() % 2 ? P(&gFont) : 0);
                    Set32(0x184, rng() % 2 ? P(&gImage) : 0);
                    Set32(0xec, *reinterpret_cast<uint32_t *>(gBlock + 0xec) | (rng() % 2 ? 0x4000u : 0));
                    if (origDtor) Orig(ctx, 0x935495, Btn());
                    else Btn()->Destruct();
                    drawfx::Note("font.refs", uint32_t(gFont.refs));
                    drawfx::Note("image.refs", uint32_t(gImage.refs));
                    std::memcpy(afterDtor[side], gBlock, kBlock);
                });
            Normalise(out.first);
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

DIFF_TEST(cGZWinBtn_Construct, 0x93536f) { Lifetime(ctx, "Construct", true, false, true, true); }
DIFF_TEST(cGZWinBtn_Destruct, 0x935495) { Lifetime(ctx, "Destruct", false, false, true, false); }

// The deleting destructor on a heap object (flag 1 frees it with the game's operator delete; both CRTs share the
// process heap) and on the block (flag clear): the calls it makes and its result.
DIFF_TEST(cGZWinBtn_DeletingDestructor, 0x62a667)
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
                    cGZWinBtn *btn = Btn();
                    if (flags & 1) {
                        btn = static_cast<cGZWinBtn *>(::operator new(cGZWinBtn::kSize));
                        std::memcpy(static_cast<void *>(btn), gBlock, cGZWinBtn::kSize);
                        drawfx::AddRange(btn, cGZWinBtn::kSize, kBlockTag);
                    }
                    btn->Construct();
                    const uint32_t result = side == 0 ? Orig(ctx, 0x62a667, btn, flags) : P(btn->DeletingDestructor(flags));
                    drawfx::Note("result is this", result == P(btn));
                });
            logs[side] = std::move(out.first);
            for (std::string &e : out.second) errors.push_back(std::move(e));
        }
        if (!drawfx::Same(ctx, logs[0], logs[1], "DeletingDestructor", seed, errors)) break;
    }
}
