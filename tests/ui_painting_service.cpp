// Differential test for cTSUIPaintingService (lane KS, src/ui/cTSUIPaintingService.h): the class in the standalone shape.
//
// UIPainting_Run: a service built by the original constructor (0x6c0f3d, game vtables 0xdec7cc / 0xdec804 / 0xdec820 /
// 0xdec848) or the original creator (0x6c1e61) against one built by ours (our constructor, or Create), each driven through
// its own vtables by the same random sequence of operations, then destroyed by the last Release (the deleting destructor
// 0x6c1073 behind the +4 thunk 0x6c106b, the destructor 0x6c0ff9). Operations: Init (0x6c2219), Shutdown (0x6c2295),
// QueryInterface on every face (0x6c2240, thunks 0x6c227d / 0x6c2285 / 0x6c228d; the folded AddRef 0x1cc320 / Release
// 0x1cd7a0 and their thunks 0x1cc325 / 0x1cd7a5 / 0x1cc32d / 0x1cd7ad / 0x1cc335 / 0x1cd7b5 release what it hands out),
// GetLotColor (0x6c1ea4), ComposeLayers (0x6c1595), CreateTexture (0x6c1325), CreateThumbnailTexture (0x6c1092),
// CreateNumberTexture (0x6c1c3b), CreateTextTexture (0x6c1917) and DoMessage (0x6c1d9e), each with its arguments drawn
// around their edges.
//
// Every object the service reaches is a recording fake (tests/ts_services_fixture.h), each slot popping what the game's
// body pops (lane KV: cGZBuffer for the images, cGZFont, cGZDrawContext, cTSUIThumbnailService +8 for the UI object;
// a slot call nested in another's arguments is where a wrong count hides, as in-game lot loads showed for GetPixel /
// ConvertNativeValueToRGBA). Out pointers are written byte for byte as the bodies do. The texture names and texts are real
// cRZStrings (the game's vtable, so both sides run the image's string code), logged by content. Stood in for on both
// sides: the game's operator new / delete (ours), the overlay maker 0x743877, the overlay blend 0x741d03, the image scaler
// 0x7444d5 (it always answers an image: the original uses the scaled copy unchecked), the message-id walks 0x1c0458 /
// 0x1c081e (and ours). The cached getters (texture manager 0xae8750, dialog service 0x20e031, app 0x20ddfb, font service
// 0x9c5baa, language service 0x9c5a5d) answer their cells; the service fallback (0x10c9378) is null, so an empty cell
// answers null (the texture manager's cell is always set: 0xae8750 calls through its null result). Compared after every
// operation: the result (the low byte where the original answers a bool in al; nothing for GetLotColor, which leaves eax
// as is), the colours written, the event log, every field (the colour cache in list order, the id sets by content), every
// fake's count, and the strings' contents.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "stack_out.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/cRZString.h"
#include "ui/cTSUIPaintingService.h"

#include <bit>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace painting_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "d"}, {6, "d"}, {9, "-ddddddd"}, {10, "-ddddddd"}, {11, "-ddddddd"}, {12, "-ddddddd"}, {13, "-ddddddd"}, {19, "ddd"}, {22, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSUIPaintingService;

constexpr uint32_t kTag = 0x7d000000;  // the service
constexpr uint32_t kLotTag = 0x7b000000; // the lots
constexpr uint32_t kStrTag = 0x7a000000; // the strings

enum : unsigned {
    kFramework = 0, kTexMgr, kDialogs, kUI, kUI2, kApp, kLots, kLotMgr, kLot2, kLotImage, kCanvas, kScaled, kSrc0, kSrc1,
    kOverlay, kDrawer, kTexture, kHolder, kFontSvc, kFont, kLangSvc, kLang, kMsg, kStd,
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); } // a bool in al with a busy upper byte
uint32_t Mostly() { return 0x100u | uint32_t(Draw(5) != 0); }

// ---- strings ----

// A cRZString with the game's vtable (both sides run the image's string code on it).
struct Str {
    union {
        cRZString s;
    };
    Str()
    {
        ::new (&s.mString) std::string();
        s.mVTable = GameAddress<void>(cRZString::kVTable);
        s.mRefCount = 0;
    }
    ~Str() {}
    cIGZString *I() { return s.AsInterface(); }
};
Str gName, gText;

uint32_t TextHash(const char *t)
{
    uint32_t h = 0x811c9dc5;
    for (; *t; ++t) h = (h ^ uint8_t(*t)) * 0x01000193;
    return h;
}
void LogText(const char *t)
{
    Ev(0x7e57);
    Ev(t ? TextHash(t) : 0);
    Ev(t ? uint32_t(std::strlen(t)) : ~0u);
}

// ---- lots: objects with a virtual base (vbptr at +4; the key is the base at +4 + vbtable[2]) ----

const int32_t kLotVbTable[3] = {0, 0, 8};
struct Lot {
    void **vt;
    const int32_t *vbtab;
    uint32_t pad;
    uint32_t keyWords[2]; // the virtual base (+0xc), what the UI object's slot 41 gets
    const char *name;
};
const char *const kLotNames[] = {"Lot Alpha", "lot alpha", "Beta", "gamma lot", ""};
Lot gLots[4];
void *gLotVt[8];
uint32_t __fastcall LotName(Lot *self, void *)
{
    Ev(0x10700);
    Ev(svcfx::Norm(P(self)));
    return P(self->name);
}

// ---- image fakes ----

uint32_t PixelSeed;
// cIGZBuffer slot 21, GetPixel (x, y): the pixel's native value (cGZBuffer 0x8c5c20 / cGZDrawContextAsBuffer 0x8c1a90 pop
// two arguments, ret 8). Here a value from the position.
uint32_t ImagePixel(Obj *self, uint32_t *a)
{
    return (a[0] * 0x9e3779b1u) ^ (a[1] * 0x85ebca77u) ^ (self->id * 0xc2b2ae3du) ^ PixelSeed;
}
// cIGZBuffer slot 33, ConvertNativeValueToRGBA (value, &r, &g, &b, &a): one byte through each out pointer, a bool in al
// (cGZBuffer 0x8c68f0 pops five arguments, ret 0x14, and writes one byte each; lane KV). The bytes come from the value,
// alpha zero now and then. The fake writes them on both answers: the game's false answer leaves them unwritten, and
// GetLotColor reads them anyway (uninitialised stack the two sides cannot share).
uint32_t ImageRGBA(Obj *, uint32_t *a)
{
    const uint32_t h = a[0];
    *reinterpret_cast<uint8_t *>(uintptr_t(a[1])) = uint8_t(h);
    *reinterpret_cast<uint8_t *>(uintptr_t(a[2])) = uint8_t(h >> 8);
    *reinterpret_cast<uint8_t *>(uintptr_t(a[3])) = uint8_t(h >> 16);
    *reinterpret_cast<uint8_t *>(uintptr_t(a[4])) = (h >> 29) == 0 ? 0 : uint8_t(h >> 24) | 1;
    return Bool();
}
uint32_t ImageFormat(Obj *self, uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = self->value[7];
    out[1] = self->value[7] ^ 0x5500;
    difftest::FillOut(out, 8);
    return a[0];
}
void SetupImage(Obj &o)
{
    o.value[1] = Draw(4) == 0 ? 0 : Draw(4);      // width
    o.value[2] = Draw(4) == 0 ? 0 : Draw(4);      // height
    for (unsigned i = 3; i < 7; ++i) o.value[i] = gRng() % 0x200 - 0x100; // its rectangle
    o.value[7] = gRng();                           // its format
    o.Set(3, "dddd", Mostly());
    o.Set(6, "d", Mostly());
    o.Set(7, "d");
    o.Set(9, 0, o.value[1]);
    o.Set(10, 0, o.value[2]);
    o.Set(12, 0, &o.value[3]);
    o.Set(15, "p", 0, &ImageFormat);
    // The argument counts are the bodies' (cGZBuffer, lane KV): slot 18 Fill 0x8c5c00 ret 8, slot 30
    // ConvertRGBValueToNative 0x8c65f0 ret 0xc, slot 32 ConvertRGBAValueToNative 0x8c67f0 ret 0x10.
    o.Set(18, "d-");
    o.Set(21, "dd", 0, &ImagePixel);
    o.Set(30, "ddd", gRng());
    o.Set(32, "ddd-", gRng());
    o.Set(33, "dpppp", 0, &ImageRGBA);
}

// An out-parameter handed an object (counted) or left alone, the call answering a bool.
uint32_t HandOut(unsigned fake, uint32_t out, uint32_t yes)
{
    if (yes & 1) {
        ++F(fake).refs;
        *reinterpret_cast<void **>(uintptr_t(out)) = &F(fake);
    }
    return yes;
}

// The framework's GetClassObject (slot 3): drawer, blank image, texture, texture holder; refused now and then.
uint32_t ClassObject(Obj *, uint32_t *a)
{
    unsigned fake = kFramework;
    switch (a[0]) {
    case 0xae6320e: fake = kDrawer; break;
    case 0xc470d325: fake = kCanvas; break;
    case 0x1c4a276c: fake = kTexture; break;
    case 0x2a956dad: fake = kHolder; break;
    default: return 0x100;
    }
    return HandOut(fake, a[2], Draw(6) == 0 ? 0x100 : 0x101);
}

// The texture manager's slots taking the name (logged by content: the name may live on the heap).
uint32_t __fastcall HasTexture(Obj *self, void *, const char *name)
{
    Ev(0x2519);
    LogText(name);
    return self->value[0];
}
uint32_t __fastcall TextureHandle(Obj *, void *, const char *name)
{
    Ev(0x1719);
    LogText(name);
    return gRng();
}
uint32_t __fastcall RegisterTexture(Obj *, void *, void *holder, const char *name, uint32_t flag)
{
    Ev(0x2019);
    Ev(svcfx::Norm(P(holder)));
    LogText(name);
    Ev(flag);
    return gRng();
}
// The texture's SetPixel (x, y, colour[4], 0).
uint32_t SetPixel(Obj *, uint32_t *a)
{
    const auto *c = reinterpret_cast<const uint32_t *>(uintptr_t(a[2]));
    for (int i = 0; i < 4; ++i) Ev(c[i]);
    return gRng();
}
// The drawer's slot 39 (image, its rectangle, the target rectangle): both rectangles logged.
uint32_t DrawImage(Obj *, uint32_t *a)
{
    for (int r = 1; r <= 2; ++r) {
        const auto *rect = reinterpret_cast<const uint32_t *>(uintptr_t(a[r]));
        for (int i = 0; i < 4; ++i) Ev(rect[i]);
    }
    return gRng();
}

// The dialog service's slot 4: the UI object (two of them, so a call made on the wrong one shows), null now and then.
// The original checks only the first answer of an operation (CreateThumbnailTexture asks again and uses the later answers
// unchecked, as the game's dialog service never changes its answer in between): only the first may be null.
unsigned gDialogCalls;
uint32_t DialogUI(Obj *, uint32_t *)
{
    const uint32_t d = Draw(10);
    const bool first = gDialogCalls++ == 0;
    if (d == 0 && first) return 0;
    return P(&F(d < 5 ? kUI : kUI2));
}
uint32_t LoadLotImage(Obj *, uint32_t *a) { return HandOut(kLotImage, a[2], Mostly()); }
uint32_t ThumbnailImage(Obj *, uint32_t *a) { return HandOut(kLotImage, a[4], Mostly()); }
// The thumbnail lot's slot 10 (no arguments): its 16-byte key (lane KV: the UI object's slot 59 copies 16 bytes from it).
uint32_t gLotKey[4];
uint32_t LotKey(Obj *, uint32_t *) { return P(gLotKey); }
// cTSUIThumbnailService slot 59 (0x7264ef, ret 0x18): (key, 0x80, 0x80, iid, &image, 1); the key logged by content.
uint32_t LotThumbnail(Obj *, uint32_t *a)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    return HandOut(kLotImage, a[4], Mostly());
}
// UI slot 4 takes a bool the original sometimes passes with other bytes above it: the low byte only.
uint32_t __fastcall UIFlag(Obj *self, void *, uint32_t flag)
{
    Ev(0x40000 | self->id);
    Ev(flag & 0xff);
    return gRng();
}
// UI slot 29: (&done, source, 0x80, 0x80, iid, &image, 1, option).
uint32_t __fastcall UIRender(Obj *self, void *, uint8_t *done, void *source, uint32_t w, uint32_t h, uint32_t iid, void **out,
                             uint32_t one, uint32_t option)
{
    Ev(0x290000 | self->id);
    Ev(svcfx::Norm(P(done)));
    Ev(svcfx::Norm(P(source)));
    Ev(w);
    Ev(h);
    Ev(iid);
    Ev(svcfx::Norm(P(out)));
    Ev(one);
    Ev(option);
    *done = uint8_t(gRng());
    HandOut(kLotImage, P(out), Mostly());
    return gRng();
}

uint32_t LotCount(Obj *self, uint32_t *) { return self->value[0]; }
uint32_t LotAt(Obj *, uint32_t *a) { return Draw(8) == 0 ? 0 : P(&gLots[a[1] % 4]); }
uint32_t LotById(Obj *, uint32_t *) { return Draw(3) == 0 ? 0 : P(&F(kLot2)); }

// The font: width of (text, length), drawing (image, 0, top, text, length, colour, 0x8000000, 0, 0: cGZFont slot 52
// 0x90c129 ends in ret 0x24, lane KV), scale out-parameters.
uint32_t __fastcall FontWidth(Obj *self, void *, const char *text, uint32_t length)
{
    Ev(0x4919);
    LogText(text);
    Ev(length);
    return self->value[0];
}
uint32_t __fastcall FontDraw(Obj *, void *, void *image, uint32_t x, uint32_t top, const char *text, uint32_t length,
                             uint32_t color, uint32_t flags, uint32_t arg7, uint32_t arg8)
{
    Ev(0x5219);
    Ev(svcfx::Norm(P(image)));
    Ev(x);
    Ev(top);
    LogText(text);
    Ev(length);
    Ev(color);
    Ev(flags);
    Ev(arg7);
    Ev(arg8);
    return gRng();
}
uint32_t FontScale(Obj *self, uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = self->value[1];
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = self->value[2];
    return gRng();
}

// The language's number text (value low, high, the cRZString to fill).
uint32_t __fastcall LanguageNumber(Obj *, void *, uint32_t low, uint32_t high, cRZString *out)
{
    Ev(0x1821);
    Ev(low);
    Ev(high);
    Ev(svcfx::Norm(P(out)));
    out->mString = std::to_string(low) + (Draw(2) ? "" : ",000,000,000,000,000");
    return gRng();
}

// The message's QueryInterface: the standard message (counted) for its id.
uint32_t MessageQuery(Obj *self, uint32_t *a)
{
    if (a[0] != 0x65297976 || !(self->value[0] & 1)) return 0x100;
    return HandOut(kStd, a[1], 0x101);
}

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
// cRZString::Sprintf (0x1ad6f7 and ours) on both sides: the image's formatting reaches CRT and allocator state the harness
// has not set up. The format and the text are logged.
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    Ev(0x1ad6f7);
    LogText(fmt);
    LogText(text);
    self->mString = text;
    return self;
}
void __cdecl StandMakeOverlay(uint32_t type, uint32_t iid, void **out, uint32_t group)
{
    Ev(0x743877);
    Ev(type);
    Ev(iid);
    Ev(svcfx::Norm(P(out)));
    Ev(group);
    if (Draw(3)) {
        ++F(kOverlay).refs;
        *out = &F(kOverlay);
    }
}
void __cdecl StandBlend(void *overlay, void *image)
{
    Ev(0x741d03);
    Ev(svcfx::Norm(P(overlay)));
    Ev(svcfx::Norm(P(image)));
}
void __cdecl StandScale(void *image, int32_t w, int32_t h, uint32_t iid, void **out, uint32_t flag)
{
    Ev(0x7444d5);
    Ev(svcfx::Norm(P(image)));
    Ev(uint32_t(w));
    Ev(uint32_t(h));
    Ev(iid);
    Ev(svcfx::Norm(P(out)));
    Ev(svcfx::Norm(P(*out)));
    Ev(flag);
    ++F(kScaled).refs;
    *out = &F(kScaled);
}
void __fastcall StandWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c0458);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c081e);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
}

// ---- the world ----

constexpr uint32_t kTexMgrCell = 0x10f8f94, kDialogCell = 0x10cc2a0, kFontCell = 0x10f51b0, kLangCell = 0x10f51c8;

void BuildFakes(uint32_t seed, svcfx::Globals &globals)
{
    svcfx::ResetAll();
    gRng.seed(seed);
    PixelSeed = gRng();
    for (unsigned i : {kLotImage, kCanvas, kScaled, kSrc0, kSrc1}) SetupImage(F(i));
    F(kFramework).Set(3, "ddp", 0, &ClassObject);
    Obj &tm = F(kTexMgr);
    tm.value[0] = Bool();
    tm.table[25] = reinterpret_cast<void *>(&HasTexture);
    tm.table[17] = reinterpret_cast<void *>(&TextureHandle);
    tm.table[20] = reinterpret_cast<void *>(&RegisterTexture);
    F(kDialogs).Set(4, 0, 0, &DialogUI);
    for (unsigned u : {kUI, kUI2}) {
        Obj &ui = F(u);
        ui.Set(41, "pdp", 0, &LoadLotImage);
        ui.Set(10, "pdddpd-", 0, &ThumbnailImage);
        ui.Set(5, 0, gRng());
        ui.table[4] = reinterpret_cast<void *>(&UIFlag);
        ui.Set(59, "pdddpd", 0, &LotThumbnail);
        ui.table[29] = reinterpret_cast<void *>(&UIRender);
    }
    F(kApp).Set(16, 0, Draw(6) == 0 ? 0 : P(&F(kLots)));
    F(kApp).Set(5, 0, Draw(6) == 0 ? 0 : P(&F(kLotMgr)));
    F(kLots).value[0] = Draw(6);
    F(kLots).Set(40, "d", 0, &LotCount);
    F(kLots).Set(41, "dd", 0, &LotAt);
    F(kLotMgr).Set(6, "d", 0, &LotById);
    F(kLot2).Set(10, 0, 0, &LotKey);
    for (uint32_t &w : gLotKey) w = gRng();
    svcfx::AddRange(gLotKey, sizeof(gLotKey), kLotTag | 0x800);
    for (unsigned i : {kDrawer}) {
        for (unsigned s : {17u, 24u, 22u, 23u, 21u}) F(i).Set(s, 1);
        F(i).Ptr(17, 1), F(i).Data(21, 1), F(i).Data(22, 1), F(i).Data(24, 1);
        F(i).Set(39, "ppp", 0, &DrawImage);
    }
    F(kTexture).Set(6, "dddddd");
    F(kTexture).Set(20, "ddp-", 0, &SetPixel);
    F(kHolder).Set(9, "pd");
    F(kFontSvc).Set(5, "d", Draw(6) == 0 ? 0 : P(&F(kFont)));
    Obj &font = F(kFont);
    const uint32_t sizes[] = {0, 1, 0xff, 0x100, 0x101, 0x180, 0x200, 0x1fe, 0x1000};
    font.value[0] = Draw(3) ? sizes[Draw(9)] : gRng() % 0x400;
    font.value[1] = std::bit_cast<uint32_t>(float(gRng() % 1000) / 333.0f);
    font.value[2] = std::bit_cast<uint32_t>(float(gRng() % 1000) / 77.0f);
    font.table[49] = reinterpret_cast<void *>(&FontWidth);
    font.Set(37, 2, Draw(3) ? sizes[Draw(9)] : gRng() % 0x400);
    font.Set(29, "pp", 0, &FontScale);
    font.Set(30, "dd");
    font.table[52] = reinterpret_cast<void *>(&FontDraw);
    F(kLangSvc).Set(38, 1, Draw(5) == 0 ? 0 : P(&F(kLang)));
    F(kLang).table[18] = reinterpret_cast<void *>(&LanguageNumber);
    F(kMsg).Set(0, "dp", 0, &MessageQuery);
    F(kMsg).Set(4, 0);
    F(kStd).Set(8, 0);
    F(kStd).Set(13, 0);
    F(kStd).Set(9, 0);
    F(kStd).Set(14, 0);
    for (unsigned i = 0; i < 4; ++i) {
        gLots[i].vt = gLotVt;
        gLots[i].vbtab = kLotVbTable;
        gLots[i].pad = 0;
        gLots[i].keyWords[0] = gLots[i].keyWords[1] = 0;
        gLots[i].name = Draw(6) == 0 ? nullptr : kLotNames[Draw(5)];
    }
    gLotVt[7] = reinterpret_cast<void *>(&LotName);
    globals.Set(svcfx::kAppCell, Draw(8) == 0 ? nullptr : &F(kApp));
    globals.Set(kTexMgrCell, &F(kTexMgr));
    globals.Set(kDialogCell, Draw(8) == 0 ? nullptr : &F(kDialogs));
    globals.Set(kFontCell, Draw(8) == 0 ? nullptr : &F(kFontSvc));
    globals.Set(kLangCell, Draw(8) == 0 ? nullptr : &F(kLangSvc));
    svcfx::AddRange(gLots, sizeof(gLots), kLotTag);
    svcfx::AddRange(&gName, sizeof(gName), kStrTag);
    svcfx::AddRange(&gText, sizeof(gText), kStrTag | 0x100);
    gName.s.mString.clear();
    gText.s.mString.clear();
}

// Per-op random state the fakes answer from.
void Reroll()
{
    gDialogCalls = 0;
    F(kTexMgr).value[0] = Draw(3) == 0 ? Bool() : 0x100;
    F(kMsg).value[0] = Mostly();
    F(kMsg).Set(4, 0, Draw(3) == 0 ? 0xc5 : Draw(4) ? 0xc7 : 0xc6 + Draw(3));
    F(kStd).Set(8, 0, Mostly());
    F(kStd).Set(13, 0, Mostly());
    F(kStd).Set(9, 0, Draw(4) ? Sys::kDirtyKind : gRng());
    F(kStd).Set(14, 0, Draw(2) ? Draw(6) : gRng());
    F(kSrc0).Set(28, 0, Draw(6));
    F(kSrc0).Set(38, 0, Bool());
    F(kLots).value[0] = Draw(6);
    // Text sizes around 256 (where the font is scaled down), and 512 x 510 (the scaled height an exact odd number: the
    // original's ceil correction).
    F(kFont).value[0] = Draw(4) == 0 ? 0x200 : Draw(2) ? gRng() % 0x300 : 0x100 + Draw(3) - 1;
    F(kFont).Set(37, 2, Draw(4) == 0 ? 0x1fe : Draw(2) ? 0x100 + Draw(3) - 1 : gRng() % 0x300);
}

// ---- calls through a face's own vtable ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

bool gFactory;
void *Build(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gFactory) {
        void *face = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0x6c1e61)();
        b.push_back(face ? 1 : 0);
        auto *object = static_cast<uint8_t *>(face) - 0x1c;
        object[0x19] = object[0x1a] = object[0x1b] = 0xc5; // padding the constructor leaves as the heap had it
        return object;
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(::new (memory) Sys) : ctx.Original<void *(__thiscall *)(void *)>(0x6c0f3d)(memory);
    b.push_back(r == memory);
    return memory;
}

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

void Dump(Blob &b, void *object, bool ours, const shape::OurTables<4> &tables)
{
    const auto *m = static_cast<const uint8_t *>(object);
    static const uint32_t kFaces[4][2] = {
        {0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kPaintingVTable}, {0x20, Sys::kMessageVTable}};
    for (unsigned f = 0; f < 4; ++f) {
        const uint32_t w = Word(m, kFaces[f][0]);
        b.push_back(ours ? w == P(tables.face[f]) : w == P(GameAddress<void>(kFaces[f][1])));
    }
    for (uint32_t off = 8; off < 0x1c; off += 4) b.push_back(Word(m, off));
    const auto &colors = *reinterpret_cast<const std::unordered_map<uint32_t, Sys::Color> *>(m + 0x24);
    b.push_back(uint32_t(colors.size()));
    b.push_back(uint32_t(colors.bucket_count()));
    for (const auto &e : colors) {
        b.push_back(e.first);
        b.push_back(std::bit_cast<uint32_t>(e.second.r));
        b.push_back(std::bit_cast<uint32_t>(e.second.g));
        b.push_back(std::bit_cast<uint32_t>(e.second.b));
    }
    b.push_back(svcfx::Norm(Word(m, 0x44)));
    for (uint32_t off : {0x48u, 0x50u}) {
        const auto &ids = *reinterpret_cast<const std::set<uint32_t> *>(m + off);
        b.push_back(uint32_t(ids.size()));
        for (uint32_t id : ids) b.push_back(id);
    }
}

constexpr uint32_t kFaceOffsets[4] = {0, 4, 0x1c, 0x20};
const uint32_t kIids[] = {cITSUIPaintingService::kIID, 1, 0x287fb697, 0xa52294b4, 0x65297976, 0x6834ba25, 0, 0xffffffff};

enum : unsigned {
    opInit, opShutdown, opQuery, opColor, opCompose, opTexture, opThumbnail, opNumber, opText, opMessage, opCount
};

void RunOp(Blob &b, void *object, unsigned kind)
{
    Reroll();
    uint8_t *paint = Face(object, 0x1c);
    switch (kind) {
    case opInit: b.push_back(Slot<uint32_t>(object, 8) & 0xff); break;
    case opShutdown: b.push_back(Slot<uint32_t>(object, 9) & 0xff); break;
    case opQuery: {
        const uint32_t face = kFaceOffsets[Draw(4)];
        const uint32_t iid = kIids[Draw(8)];
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const bool nullOut = Draw(5) == 0;
        const uint32_t r = Slot<uint32_t>(Face(object, face), 0, iid, nullOut ? nullptr : &out) & 0xff;
        b.push_back(r);
        b.push_back(nullOut ? 0 : svcfx::NormOut(P(out), 0x5a5a5a5a));
        if (r && !nullOut) { // through the face handed out: AddRef, then two Releases (the thunks of every face)
            b.push_back(Slot<uint32_t>(out, 1));
            b.push_back(Slot<uint32_t>(out, 2));
            b.push_back(Slot<uint32_t>(out, 2));
        }
        break;
    }
    case opColor: {
        float rgb[4] = {-1.0f, -2.0f, -3.0f, -4.0f};
        const int32_t kinds[] = {1, 2, 0, 3, -1};
        const uint32_t which = Draw(3);
        const char *names[] = {"Lot Alpha", "LOT ALPHA", "Beta", "Delta", ""};
        const char *name = Draw(8) == 0 ? nullptr : names[Draw(5)];
        Slot<void>(paint, 3, kinds[Draw(5)], name, which == 0 ? nullptr : rgb);
        for (float v : rgb) b.push_back(std::bit_cast<uint32_t>(v));
        break;
    }
    case opCompose: {
        cTSUIPaintLayers layers;
        const unsigned n = Draw(4);
        for (unsigned i = 0; i < n; ++i) {
            cTSUIPaintLayer l;
            const uint32_t pick = Draw(5);
            l.mImage = pick == 0 ? nullptr : reinterpret_cast<cIGZUnknown *>(&F(pick & 1 ? kSrc0 : kSrc1));
            l.mOverlay = Draw(2) != 0;
            const float scales[] = {0.0f, 0.5f, 0.7f, 0.25f, 0.00390625f, 0.3f, 0.0117187f, 1.9921875f};
            l.mScale = scales[Draw(8)];
            l.mVAlign = int32_t(Draw(5));
            l.mHAlign = int32_t(Draw(5) + 3);
            layers.push_back(l);
        }
        gName.s.mString = Draw(2) ? "LayerTex" : "a-much-longer-layer-texture-name";
        using Fn = uint32_t(__thiscall *)(void *, cTSUIPaintLayers, cIGZString *, uint32_t);
        auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void ***>(paint))[4]);
        b.push_back(fn(paint, layers, gName.I(), Bool()));
        break;
    }
    case opTexture: {
        const uint32_t pick = Draw(4);
        void *image = pick == 0 ? nullptr : &F(pick == 1 ? kLotImage : pick == 2 ? kCanvas : kSrc0);
        gName.s.mString = Draw(2) ? "Tex" : "a-much-longer-texture-name-than-sso";
        b.push_back(Slot<uint32_t>(paint, 5, image, gName.I(), Bool()));
        break;
    }
    case opThumbnail: {
        void *source = Draw(6) == 0 ? nullptr : &F(kSrc0);
        b.push_back(Slot<uint32_t>(paint, 6, gName.I(), source, Bool(), gRng()));
        break;
    }
    case opNumber: {
        const int32_t values[] = {0, 1, -1, 7, -42, 1000000, INT32_MIN, INT32_MAX};
        b.push_back(Slot<uint32_t>(paint, 7, Draw(2) ? values[Draw(8)] : int32_t(gRng()), gName.I()));
        break;
    }
    case opText: {
        const char *texts[] = {"", "Hi", "Headline text", "a headline longer than the small string buffer"};
        gText.s.mString = texts[Draw(4)];
        b.push_back(Slot<uint32_t>(paint, 8, gText.I(), gName.I()));
        break;
    }
    case opMessage: {
        void *message = Draw(8) == 0 ? nullptr : &F(kMsg);
        b.push_back(Slot<uint32_t>(Face(object, 0x20), 3, message) & 0xff);
        break;
    }
    }
    LogText(gName.s.mString.c_str());
    LogText(gText.s.mString.c_str());
}

} // namespace painting_test

using namespace painting_test;

DIFF_TEST(UIPainting_Run, 0x6c1e61)
{
    // Also reached: every RVA in the file comment.
    svcfx::AllocatorScope allocator; // the layers' vector (the allocator service's memory)
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0); // the service fallback of the cached getters: none
    // The director's framework is fake kFramework whatever an earlier test left in the shared index (lane KV: in the full
    // suite a ts_* test left it on another fake, whose null slot 3 the first CreateInstance called).
    struct FrameworkIndex {
        unsigned saved = svcfx::gFrameworkIndex;
        FrameworkIndex() { svcfx::gFrameworkIndex = kFramework; }
        ~FrameworkIndex() { svcfx::gFrameworkIndex = saved; }
    } frameworkIndex;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::StandIn sprintfStand(0x1ad6f7, &cRZString::Sprintf, &StandSprintf);
    shape::CodePatch overlay(0x743877, shape::CodeOf(&StandMakeOverlay));
    shape::CodePatch blend(0x741d03, shape::CodeOf(&StandBlend));
    shape::CodePatch scale(0x7444d5, shape::CodeOf(&StandScale));
    shape::StandIn walk5{0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5};
    shape::StandIn walk6{0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6};

    const shape::OurTables<4> &tables = shape::Ours<Sys>(kFaceOffsets);
    svcfx::gEv.clear();

    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        std::mt19937 opRng(seed * 7919);
        std::vector<unsigned> ops(1 + opRng() % 12);
        for (unsigned &op : ops) op = opRng() % opCount;
        gFactory = seed % 3 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed, globals);
            Blob first;
            void *object = Build(ctx, first, side == 1);
            svcfx::AddRange(object, Sys::kSize, kTag);
            Dump(first, object, side == 1, tables);
            first.insert(first.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            results[side].push_back(std::move(first));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1, tables);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "UIPainting_Run", seed)) {
                const size_t op = i ? i - 1 : 0;
                ctx.Fail("  at step %u of %u (op %u)", unsigned(i), unsigned(ops.size() + 2),
                         i && op < ops.size() ? ops[op] : 99u);
                seed = 100000;
                break;
            }
        }
    }
}
