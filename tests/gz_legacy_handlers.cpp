// Difftests for the window classes' handlers of cGZLegacyControl and their helpers (lane MF, foundations F6 part 2):
// src/gz/cGZLegacyHandlers.size.cpp.
//
// The world per side (the same for both, built from the seed): the framework (the service object cell 0x10c9378) is a
// recording svcfx fake whose GetSystemService hands out the UI script service (or refuses) and the resource manager; the
// service's GetContext answers a context fake or null; the context's slot 3 (image) and the manager's slot 3 (the image
// resource, whose slot 3 gives the image) note the key's words and hand out the counted image (or nothing). The window is
// a winfx window (state-backed size and area; SetProperty logs the variant by content) with, mostly, the class's face
// (a drawfx fake of the face's slots: each call logs its arguments, floats bit for bit) and the image a drawfx fake whose
// slots 9 / 10 give its size, so one log (drawfx notes from the svcfx handlers) keeps the order of the calls on the
// window, the face, the image and the services. Compared: that log, the window's state, every fake's count.
//
// LegacyHandlers_Bitmap: ApplyBitmapProperties 0x864376, apply true and false, on generated lists over its attributes.
// LegacyHandlers_FindRect: FindRectAttribute 0x863ee9. LegacyHandlers_LoadImage: LoadImageResource 0x86dc44.
// LegacyHandlers_PairProperty: SetImagePairProperty 0x86df76.
//
// Not compared: the result of a handler (the original's is whatever its last call left in al; the table caller never reads
// it). An image text with no number at all ("{zz}"): the original passes the group uninitialised (0x8644bb..0x8644d5: the
// sscanf result is not checked and the local is never set before), so the pools only hold texts with at least the
// first number.
#include "code_patch.h"
#include "difftest.h"
#include "legacy_win_faces.h"
#include "shape.h"
#include "ts_services_fixture.h"
#include "winfx.h"

#include "game/game.h"
#include "gz/cGZLegacyControl.h"
#include "gz/cGZLegacyHandlers.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZUnknownAutoPtr.h"
#include "rz/cRZVariant.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace legacy_handlers_test {

using drawfx::Ret;
using svcfx::Blob;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;

constexpr uint32_t kServiceCell = 0x10c9378;
constexpr uint32_t kUIServiceId = 0x5a356e15;
constexpr uint32_t kUIServiceIid = 0xfa3562fa;
constexpr uint32_t kManagerId = 0x056b906e;
constexpr uint32_t kManagerIid = 0x656b8efc;
enum : unsigned { kFramework = 0, kService, kContext, kManager, kResource };

// ---- the faces' slot tables (the words each slot takes, from the callers' code) ---------------------------------------

using drawfx::Slot;
using namespace legacy_win_faces;

// The graphics system (cell 0x10f51a4): slot 34 turns three colour bytes into a native colour (the low bytes of its
// words: the original handlers pass dwords whose high bytes are stack leftovers, which the buffer's converter ignores).
constexpr uint32_t kGraphicsCell = 0x10f51a4;

// The UI defaults service (cell 0x10f51b0, GetCachedServiceObjectB): slot 5 gives the font for an id, slot 38 the id of a
// font name.
constexpr uint32_t kDefaultsCell = 0x10f51b0;

uint32_t ConvertRGB(drawfx::Fake &, const uint32_t *a)
{
    return (a[0] & 0xff) | (a[1] & 0xff) << 8 | (a[2] & 0xff) << 16 | 0x5a000000u;
}

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- values ------------------------------------------------------------------------------------------------------------

std::vector<cRZVariant *> gValues; // every value made for the seed (released after it)

cRZVariant *Keep(cRZVariant *v)
{
    v->AddRef();
    gValues.push_back(v);
    return v;
}
cRZVariant *TextValue(const char *text)
{
    LegacyText s;
    s.mText.FromChar(text);
    return Keep(new cRZVariant(*s.mText.AsInterface())); // an RZChar array: tag 0x800e
}
void ReleaseValues()
{
    for (cRZVariant *v : gValues) v->Release();
    gValues.clear();
}

const char *const kImageTexts[] = {"{1a,2b}", "{0,0}", "{ff}", "{abc,def}", "{FFFFFFFF,12345678}", "{12,zz}", "{3, 4}",
                                   "{7,8,9}", "{0x10,0x20}"};
const char *const kRectTexts[] = {"(1,2,3,4)", "(0,0,10,10)", "(1,2,3)", "x", "(5,6,7,8)", "( 1, 2, 3, 4)", "(-5,-6,70,80)",
                                  "", "(1,2,3,4,5)", "(1,2,3,"};
const uint32_t kNumbers[] = {0, 1, 2, 3, 0x7f, 0xfe, 0xff, 0x100, 0x101, 0x7fffffff, 0x80000000, 0xffffffff};
template <size_t N> const char *Pick(const char *const (&pool)[N]) { return pool[Draw(uint32_t(N))]; }

constexpr uint32_t kAlpha = 0xb00, kTransparentBack = 0xb01, kEdgeImage = 0xb02, kNotify = 0xf003, kImage = 0xf016,
                   kImageRect = 0xf018;
const uint32_t kBitmapIds[] = {kAlpha, kAlpha, kTransparentBack, kEdgeImage, kEdgeImage, kNotify, kImage, kImage, kImageRect,
                               kImageRect, 0x7, 0xb03};

// A value for attribute `id`: the right kind most of the time (a uint32 for the numbers, a text for the texts), else
// another kind, a sint32, or null.
cIGZUnknown *ValueFor(uint32_t id)
{
    const uint32_t pick = Draw(12);
    if (pick == 0) return nullptr;
    if (pick == 1) return Keep(new cRZVariant(int32_t(Draw(5)) - 2));
    if (pick == 2) return TextValue(id == kImageRect ? "5" : Pick(kImageTexts));
    if (pick == 3) return Keep(new cRZVariant(uint32_t(Draw(3))));
    if (id == kImage) return TextValue(Pick(kImageTexts));
    if (id == kImageRect) return TextValue(Pick(kRectTexts));
    return Keep(new cRZVariant(Draw(3) ? kNumbers[Draw(sizeof(kNumbers) / 4)] : uint32_t(gRng())));
}

// The generic runs' values: null, a sint32, a short text or a small uint32 (each wrong for most ids), else what `right`
// makes for the id.
uint32_t gNoShortTextId = 0; // an attribute whose short texts the run must not make (0: none)
std::vector<uint32_t> gNoShortExtra; // more of them
uint32_t gNonNullId = 0;     // an attribute whose value the original uses without a null check (0: none)
std::vector<uint32_t> gNonNullExtra; // more of them

cIGZUnknown *MixedValue(cIGZUnknown *(*right)(uint32_t), uint32_t id)
{
    const uint32_t pick = Draw(12);
    if (pick == 0)
        return id == gNonNullId || std::find(gNonNullExtra.begin(), gNonNullExtra.end(), id) != gNonNullExtra.end()
                   ? right(id)
                   : nullptr;
    if (pick == 1) return Keep(new cRZVariant(int32_t(Draw(5)) - 2));
    if (pick == 2 && id != gNoShortTextId &&
        std::find(gNoShortExtra.begin(), gNoShortExtra.end(), id) == gNoShortExtra.end())
        return TextValue("5");
    if (pick == 3) return Keep(new cRZVariant(uint32_t(Draw(3))));
    return right(id);
}

cGZScriptAttributes *MakeList(const uint32_t *ids, uint32_t idCount, uint32_t maxLength,
                              cIGZUnknown *(*right)(uint32_t) = nullptr)
{
    auto *list = new cGZScriptAttributes;
    const uint32_t n = Draw(maxLength + 1);
    for (uint32_t i = 0; i < n; ++i) {
        cGZScriptAttribute a;
        a.mId = ids[Draw(idCount)];
        a.mValue = right ? MixedValue(right, a.mId) : ValueFor(a.mId);
        list->PushBack(a);
    }
    return list;
}

// ---- the world ---------------------------------------------------------------------------------------------------------

winfx::World gWorld;
int gRunSide; // which side the handler run is on (0 the original, 1 ours)
drawfx::Fake gFace, gImage, gGraphics, gDefaults, gOption;

std::vector<cIGZVariant *> gProps; // what the window's SetProperty kept
bool gGiveService, gGiveContext, gContextImage, gManagerOk, gResourceImage, gContextTextOk, gManagerTextOk;
const char *gContextText;
cRZString *gTextResource; // the manager's string resource (counted)
std::vector<cRZString *> gKeptTexts; // texts a face slot holds on to (AddRef'd), checked at the end of a side

const uint8_t *Bytes(uint32_t p) { return reinterpret_cast<const uint8_t *>(uintptr_t(p)); }
template <class T> T At(const uint8_t *b, uint32_t off)
{
    T v;
    std::memcpy(&v, b + off, sizeof(T));
    return v;
}

// The kept variants (untyped bytes: the tag +0xe, the count +0x10, the payload at +4) by content.
uint32_t SetProperty(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Call &call = drawfx::gLog.back();
    call.words[1] = a[1] != 0;
    if (a[1]) {
        const uint8_t *v = Bytes(a[1]);
        const uint16_t tag = At<uint16_t>(v, 0xe);
        call.words.push_back(tag);
        call.words.push_back(At<uint16_t>(v, 0xc));
        if (!(tag & 0x8000)) { // not an array: the value words (+4, +8)
            call.words.push_back(At<uint32_t>(v, 4));
            call.words.push_back(At<uint32_t>(v, 8));
        }
        const uint32_t count = At<uint32_t>(v, 0x10);
        call.words.push_back(count);
        const uint8_t *data = Bytes(At<uint32_t>(v, 4));
        call.words.push_back(data != nullptr);
        for (uint32_t i = 0; data && i < count; ++i) call.words.push_back(data[i]);
        auto *value = reinterpret_cast<cIGZVariant *>(uintptr_t(a[1]));
        value->AddRef();
        gProps.push_back(value);
    }
    return 1;
}

uint32_t ImageWidth(drawfx::Fake &f, const uint32_t *) { return f.value[0]; }
uint32_t ImageHeight(drawfx::Fake &f, const uint32_t *) { return f.value[1]; }
uint32_t ImageRect(drawfx::Fake &f, const uint32_t *) { return uint32_t(reinterpret_cast<uintptr_t>(&f.value[2])); }

void LogKey(uint32_t key, const char *label)
{
    drawfx::Note(label, {At<uint32_t>(Bytes(key), 0), At<uint32_t>(Bytes(key), 4), At<uint32_t>(Bytes(key), 8),
                         At<uint32_t>(Bytes(key), 12)});
}

uint32_t GetSystemService(Obj *, uint32_t *a)
{
    drawfx::Note("service", {a[0], a[1]});
    void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if (a[0] == kUIServiceId && a[1] == kUIServiceIid && gGiveService) {
        ++F(kService).refs;
        *out = &F(kService);
        return 1;
    }
    if (a[0] == kManagerId && a[1] == kManagerIid) {
        ++F(kManager).refs;
        *out = &F(kManager);
        return 1;
    }
    return 0;
}
uint32_t ContextLoadImage(Obj *, uint32_t *a)
{
    LogKey(a[0], "context image");
    if (!gContextImage) return 0;
    ++gImage.refs;
    return svcfx::P(&gImage);
}
uint32_t ManagerGet(Obj *, uint32_t *a)
{
    LogKey(a[0], "manager key");
    drawfx::Note("manager get", {a[1], a[3], a[4]});
    if (!gManagerOk) return 0x7700; // false in al, noise above
    ++F(kResource).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kResource);
    return 0x7701;
}
uint32_t ResourceImage(Obj *, uint32_t *)
{
    drawfx::Note("resource image", 0);
    if (!gResourceImage) return 0;
    ++gImage.refs;
    return svcfx::P(&gImage);
}

uint32_t ContextLoadText(Obj *, uint32_t *a)
{
    LogKey(a[0], "context text");
    reinterpret_cast<cIGZString *>(uintptr_t(a[1]))->FromChar(gContextText);
    return gContextTextOk ? 0x5a01 : 0x5a00;
}
uint32_t ManagerGetString(Obj *, uint32_t *a)
{
    LogKey(a[0], "manager text key");
    if (!gManagerTextOk) return 0x7700;
    gTextResource->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[2])) = gTextResource;
    return 0x7701;
}

// Rebuilds the services, the window with its face (when `withFace`) and the image for the seed.
template <const drawfx::Iface &Face = kBitmapFace>
void BuildWorld(uint32_t seed, bool withFace, uint32_t faceIid = cIGZWinBitmap::kIID)
{
    svcfx::ResetAll();
    drawfx::ResetAll();
    std::mt19937 rng(seed * 17 + 3);
    F(kFramework).Set(5, "ddp", 0, &GetSystemService);
    F(kService).Set(9, 0, 0u);
    gGiveService = rng() % 4 != 0;
    gGiveContext = rng() % 2 != 0;
    if (gGiveContext) F(kService).Set(9, 0, &F(kContext));
    F(kContext).Set(3, "p", 0, &ContextLoadImage);
    F(kManager).Set(3, "ppp--", 0, &ManagerGet);
    F(kResource).Set(3, 0, 0, &ResourceImage);
    gContextImage = rng() % 4 != 0;
    gManagerOk = rng() % 3 != 0;
    gResourceImage = rng() % 4 != 0;
    gProps.clear();
    F(kContext).Set(4, "pp", 0, &ContextLoadText);
    F(kManager).Set(5, "ppp--", 0, &ManagerGetString);
    gContextTextOk = rng() % 3 != 0;
    gManagerTextOk = rng() % 3 != 0;
    static const char *const kLocalized[] = {"localized", "", "Another caption"};
    gContextText = kLocalized[rng() % 3];
    gTextResource = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    gTextResource->Construct(kLocalized[rng() % 3]);
    gTextResource->AddRef();

    gImage.Reset<kImageInterface>(520);
    gImage.On(9, &ImageWidth);
    gImage.On(10, &ImageHeight);
    gImage.value[0] = rng() % 3 ? rng() % 300 : uint32_t(rng());
    gImage.value[1] = rng() % 3 ? rng() % 300 : uint32_t(rng());
    gImage.On(12, &ImageRect);
    for (int i = 2; i < 6; ++i) gImage.value[i] = rng() % 3 ? rng() % 100 : uint32_t(rng());

    gGraphics.Reset<kGraphicsFace>(540);
    gDefaults.Reset<kDefaultsFace>(560);
    gOption.Reset<kOptionItemFace>(430);
    gDefaults.Returns(5, 0x1000 + rng() % 16);
    gDefaults.Returns(38, 0x2000 + rng() % 16);
    gGraphics.On(34, &ConvertRGB);
    gWorld.Build(seed, 2);
    winfx::Win &w = gWorld[1];
    w.f.On(127, &SetProperty);
    gFace.Reset<Face>(410);
    if (withFace) w.f.AddFace(faceIid, gFace);
}

ifaces::cIGZWin *Window() { return reinterpret_cast<ifaces::cIGZWin *>(&gWorld[1].f); }

void ReleaseProps()
{
    for (cIGZVariant *v : gProps) v->Release();
    gProps.clear();
}

void (*gDumpExtra)(Blob &b) = nullptr; // set by a handler's run: what else to record before the kept variants go

// After a side: the windows' state, the fakes' counts, the kept variants' counts.
void EndSide(Blob &b)
{
    if (gDumpExtra) gDumpExtra(b);
    const std::vector<uint32_t> state = gWorld.State();
    b.insert(b.end(), state.begin(), state.end());
    b.push_back(uint32_t(gFace.refs));
    b.push_back(uint32_t(gImage.refs));
    b.push_back(uint32_t(gOption.refs));
    for (cIGZVariant *v : gProps) b.push_back(At<uint16_t>(reinterpret_cast<const uint8_t *>(v), 0xc));
    ReleaseProps();
    svcfx::CaptureFakes(b);
    for (cRZString *kept : gKeptTexts) { // the counts after the handler's own release: a leaked reference shows
        b.push_back(kept->mRefCount);
        kept->Release();
    }
    gKeptTexts.clear();
    b.push_back(gTextResource->mRefCount);
    b.push_back(uint32_t(gTextResource->mString.size()));
    gTextResource->Release();
    gTextResource = nullptr;
}

// ---- the generic handler run -------------------------------------------------------------------------------------------

// One handler's run: the window with the class's face (absent on some seeds), attribute lists over `ids` whose values are
// mixed kinds or what `right` makes, apply true and false; the original by RVA against ours on the same lists, with the
// window's calls, the face's, the window's state and the counts compared.
struct HandlerRun {
    const char *name;
    uint32_t rva;
    LegacyHandler ours;
    uint32_t faceIid;
    const uint32_t *ids;
    uint32_t idCount;
    uint32_t maxLength;
    cIGZUnknown *(*right)(uint32_t id);
    uint32_t seeds;
    uint32_t noShortTextId = 0; // an attribute that never gets the generic short text (its text is read without a check)
    uint32_t nonNullId = 0;     // an attribute that never gets a null value (the original does not check it)
    std::vector<uint32_t> noShortExtra = {}; // more attributes that never get the generic short text
    void (*setup)(uint32_t seed) = nullptr;  // run once the world is built (to put handlers on the face)
    void (*fixup)(cGZScriptAttributes *list) = nullptr; // makes the list one the original reads whole (see the run)
    std::vector<uint32_t> nonNullExtra = {}; // more attributes that never get a null value
};

template <const drawfx::Iface &Face> void RunHandler(difftest::Context &ctx, const HandlerRun &run)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    globals.Set(kServiceCell, &F(kFramework));
    globals.Set(kGraphicsCell, &gGraphics);
    globals.Set(kDefaultsCell, &gDefaults);
    using HandlerFn = bool(__cdecl *)(void *, void *, bool);
    const std::string label = run.name;
    gNoShortTextId = run.noShortTextId;
    gNoShortExtra = run.noShortExtra;
    gNonNullId = run.nonNullId;
    gNonNullExtra = run.nonNullExtra;
    for (uint32_t seed = 1; seed <= run.seeds; ++seed) {
        gRng.seed(seed);
        cGZScriptAttributes *list = MakeList(run.ids, run.idCount, run.maxLength, run.right);
        if (run.fixup) run.fixup(list);
        const bool apply = seed % 3 != 0;
        const bool withFace = seed % 7 != 0;
        Blob results[2];
        drawfx::Log logs[2];
        for (int side = 0; side < 2; ++side) {
            gRunSide = side;
            BuildWorld<Face>(seed, withFace, run.faceIid);
            if (run.setup) run.setup(seed);
            if (side)
                run.ours(reinterpret_cast<ifaces::cIGZWin *>(&gWorld[1].f), list, apply);
            else
                ctx.Original<HandlerFn>(run.rva)(&gWorld[1].f, list, apply);
            EndSide(results[side]);
            logs[side] = drawfx::gLog;
        }
        delete list;
        ReleaseValues();
        if (!svcfx::Same(ctx, results[0], results[1], run.name, seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], (label + " log").c_str(), seed)) break;
    }
}

// ---- GZWinFlatRect's face (slot 10: index and the three colour bytes; slot 12: the style bits) --------------------------

const uint32_t kStyleWords[] = {0xf006, 0xf007, 0x204, 0x205, 0x206, 0x207, 0x208, 0xf00b, 0xf00c, 0xf00e,
                                0xf008, 0xf009, 0xf00a, 0xf00d, 0x203,  0x209, 0};
const char *const kColorTexts[] = {"(1,2,3)", "(255,0,128)", "(300,400,500)", "(1,2)", "x", "(-1,-2,-3)", "(1,2,3,4)",
                                   "(0,0,0)", " (9, 8, 7)", ""};

cIGZUnknown *FlatRectValue(uint32_t id)
{
    if (id == 0xf005) {
        const uint32_t word = kStyleWords[Draw(sizeof(kStyleWords) / 4)];
        if (Draw(4) == 0) return Keep(new cRZVariant(int32_t(word))); // the right number in the wrong kind: skipped
        return Keep(new cRZVariant(word));
    }
    return TextValue(Pick(kColorTexts));
}

// ---- GZWinText's face --------------------------------------------------------------------------------------------------

const char *const kPairTexts[] = {"(1,2)", "(0,0)", "(-3,40)", "(1)", "x", "(1,2,3)", "( 5, 6)", "(7,8", "", "(100000,-100000)"};
const char *const kFontNames[] = {"Arial", "", "two words", "Tahoma Bold"};
const uint32_t kFontNumbers[] = {0, 1, 5, 0xf008, 0x7fffffff, 0xffffffff};
const uint32_t kAlignNumbers[] = {0, 1, 3, 4, 8, 0xf008, 0xf009, 0xffffffff};

cIGZUnknown *TextHandlerValue(uint32_t id)
{
    switch (id) {
    case 0xf000:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0xf001:
    case 0xf002: return TextValue(Pick(kColorTexts));
    case 0xf004:
    case 0x305: return TextValue(Pick(kPairTexts));
    case 0xf015: return Keep(new cRZVariant(kAlignNumbers[Draw(sizeof(kAlignNumbers) / 4)]));
    default: return Keep(new cRZVariant(Draw(3) ? Draw(3) : uint32_t(gRng())));
    }
}

// ---- GZWinSlider's and GZWinScrollbar's faces --------------------------------------------------------------------------

const char *const kRangeTexts[] = {"(0,100)", "(100,0)", "(5,5)", "(-5,5)", "(5,-5)", "(1)", "x", "(2147483647,-2147483648)",
                                   "( 1, 2)", "", "(3,2,1)"};
const uint32_t kDirections[] = {0xc02, 0xc03, 0xc03, 0, 1, 2, 0xffffffff};

// Values for the slider's and scroll bar's attributes (ids: range / counts pairs, direction, sizes, image, rectangle,
// initvalue).
cIGZUnknown *RangeValue(uint32_t id)
{
    switch (id) {
    case 0xc00:
    case 0xd02: return TextValue(Pick(kRangeTexts));
    case 0xc01: return Keep(new cRZVariant(kDirections[Draw(sizeof(kDirections) / 4)]));
    case 0xf016: return TextValue(Pick(kImageTexts));
    case 0xf018: return TextValue(Pick(kRectTexts));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

// ---- GZWinOutline's face (slot 3 the style bits, 5 a colour, 13 the nodes) ----------------------------------------------

// Pair texts that always hold two numbers: the original reuses the previous pair for a text that does not (its locals
// stay), which the tests do not reach.
const char *const kNodeTexts[] = {"(1,2)", "(0,0)", "(-3,40)", "( 5, 6)", "(100000,-100000)", "(1,2,3)", "(9, -9)", "(-1,-2)"};
const uint32_t kOutlineStyles[] = {0xf009, 0xf00b, 0xf00c, 0xf00d, 0xf00a, 0xf006, 0xf00e, 0};

cIGZUnknown *OutlineValue(uint32_t id)
{
    switch (id) {
    case 0xf005:
        if (Draw(5) == 0) return Keep(new cRZVariant(int32_t(kOutlineStyles[Draw(sizeof(kOutlineStyles) / 4)])));
        return Keep(new cRZVariant(kOutlineStyles[Draw(sizeof(kOutlineStyles) / 4)]));
    case 0x1102: return TextValue(Pick(kNodeTexts));
    default: return TextValue(Pick(kColorTexts));
    }
}

// ---- GZWinScrollbar2's face ---------------------------------------------------------------------------------------------

const char *const kKeyTexts[] = {"{1,2,3}", "{856ddbac,1a,2b}", "{0,0,0}", "{1,2}", "x", "{1,2,3,4}",
                                 "{ffffffff,ffffffff,ffffffff}", "", "{ 5, 6, 7}", "{a,b,c"};

cIGZUnknown *Scrollbar2Value(uint32_t id)
{
    if (id >= 0x1707) return TextValue(Pick(kKeyTexts));
    if (id == 0x1700) return Keep(new cRZVariant(kDirections[Draw(sizeof(kDirections) / 4)]));
    return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
}

// ---- GZWinFolders' face (slot 4 adds an element's text, 9 clears them, 14 the font, 15 the image) -------------------------

// The image text is always "{group,instance}": the original reads it without a check (the numbers left over are stack).
const char *const kWholeImageTexts[] = {"{1a,2b}", "{0,0}", "{FFFFFFFF,12345678}", "{3, 4}", "{7,8,9}", "{abc,def}"};
const char *const kElementTexts[] = {"one", "", "two words", "x,y", "a b c d e f g h"};

cIGZUnknown *FoldersValue(uint32_t id)
{
    switch (id) {
    case 0xf000:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0xf016: return TextValue(Pick(kWholeImageTexts));
    default: return TextValue(Pick(kElementTexts)); // 0x502
    }
}

// ---- GZWinCombo's face -------------------------------------------------------------------------------------------------

cIGZUnknown *ComboValue(uint32_t id)
{
    switch (id) {
    case 0xf000:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0x800: return TextValue(Pick(kImageTexts));
    case 0x801: return TextValue(Pick(kRectTexts));
    case 0xf004: return TextValue(Pick(kPairTexts));
    case 0xf00f:
    case 0xf010:
    case 0xf011:
    case 0x408:
    case 0x40c:
    case 0x802: return TextValue(Pick(kColorTexts));
    case 0x502: return TextValue(Pick(kElementTexts));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

// ---- GZWinListBox's face -----------------------------------------------------------------------------------------------

const char *const kTwoColorTexts[] = {"(1,2,3)(4,5,6)", "(255,0,128)(1,2,3)", "(300,400,500)(1,2,3)", "(1,2,3)", "x",
                                      "(1,2,3)(4,5)", "(0,0,0)(0,0,0)", " (9, 8, 7)(1,2,3)", "", "(-1,-2,-3)(-4,-5,-6)"};

cIGZUnknown *ListBoxValue(uint32_t id)
{
    switch (id) {
    case 0xf000:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0xf00f:
    case 0xf010:
    case 0xf011:
    case 0x408: return TextValue(Pick(kColorTexts));
    case 0x40c: return TextValue(Pick(kTwoColorTexts));
    case 0x500:
    case 0xf004: return TextValue(Pick(kPairTexts));
    case 0x502: return TextValue(Pick(kElementTexts));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

// ---- GZWinLineInput's face ---------------------------------------------------------------------------------------------

// Gutter texts that read whole when they hold one or three commas (the original reuses stack values otherwise).
const char *const kGutterRectTexts[] = {"(1,2)", "(7,8", "( 5, 6)", "(-3,40)", "(1,2,3,4)", "(5,6,7,8", "(1,2,3)", "(1)", "x",
                                        "", "(1,2,3,4,5)", "(100000,-100000,7,8)"};
const uint32_t kLineStyles[] = {0xe01, 0xe02, 0xf00a, 0xf009, 0xf00d, 0, 0xe00};

cIGZUnknown *LineInputValue(uint32_t id)
{
    switch (id) {
    case 0xf000:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0xf00f:
    case 0xf010:
    case 0xf011:
    case 0xe03:
    case 0xe04:
    case 0xe05:
    case 0xe06:
    case 0xe07: return TextValue(Pick(kColorTexts));
    case 0xf005: return Keep(new cRZVariant(kLineStyles[Draw(sizeof(kLineStyles) / 4)]));
    case 0xf004: return TextValue(Pick(kGutterRectTexts));
    case 0xe0b: return TextValue(Pick(kPairTexts));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

// ---- GZWinSpinner's face -----------------------------------------------------------------------------------------------

const uint32_t kSideWords[] = {0, 0xf006, 0xf007, 0xf008, 1, 0xffffffff};

cIGZUnknown *SpinnerValue(uint32_t id)
{
    switch (id) {
    case 0xf000:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0x1000:
    case 0xf015: return Keep(new cRZVariant(kSideWords[Draw(sizeof(kSideWords) / 4)]));
    case 0xf00f:
    case 0xf010:
    case 0xf011:
    case 0x1006:
    case 0x1007:
    case 0x1008:
    case 0x1009:
    case 0x100a: return TextValue(Pick(kColorTexts));
    case 0xf016: return TextValue(Pick(kWholeImageTexts));
    case 0xf004:
    case 0x1004: return TextValue(Pick(kPairTexts));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

// ---- GZWinTextTicker's face --------------------------------------------------------------------------------------------

// SetMessage keeps a reference to the text so the end of the side can see whether the handler released its own.
uint32_t KeepMessage(drawfx::Fake &, const uint32_t *a)
{
    auto *text = reinterpret_cast<cRZString *>(uintptr_t(a[0]));
    text->AddRef();
    gKeptTexts.push_back(text);
    return 1;
}
void KeepTickerMessage(uint32_t) { gFace.On(3, &KeepMessage); }

// Colour texts that always hold three numbers (the original parses them unchecked).
const char *const kWholeColorTexts[] = {"(1,2,3)", "(255,0,128)", "(300,400,500)", "(-1,-2,-3)", "(0,0,0)", "(1,2,3,4)",
                                        "(9, 8, 7)"};

cIGZUnknown *TextTickerValue(uint32_t id)
{
    switch (id) {
    case 0xf000:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0xf001:
    case 0xf002: return TextValue(Pick(kWholeColorTexts));
    case 0x1202: return TextValue(Pick(kElementTexts));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

// ---- GZWinBtn's face ---------------------------------------------------------------------------------------------------

const uint32_t kButtonStyles[] = {0xa09, 0xa0a, 0xa0b, 0xa00, 0xa0c, 0xa0d, 0xa0e, 0xa0f, 0xa1b, 0xa01, 0, 7};
const uint32_t kButtonAligns[] = {0xf006, 0xf007, 0xf008, 0, 3};
// Gutter texts with one or three commas read whole; the others do nothing.
const char *const kButtonGutterTexts[] = {"(1,2)", "( 5, 6)", "(-3,40)", "(1,2,3,4)", "(5,6,7,8)", "(1,2,3)", "(1)", "x", "",
                                          "(1,2,3,4,5)", "(100000,-100000,7,8)"};

cIGZUnknown *ButtonValue(uint32_t id)
{
    switch (id) {
    case 0xf000:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0xf005: return Keep(new cRZVariant(kButtonStyles[Draw(sizeof(kButtonStyles) / 4)]));
    case 0xf015: return Keep(new cRZVariant(kButtonAligns[Draw(sizeof(kButtonAligns) / 4)]));
    case 0xf00f:
    case 0xf010:
    case 0xf011:
    case 0xf012:
    case 0xf013:
    case 0xf014: return TextValue(Pick(kColorTexts));
    case 0xf004: return TextValue(Pick(kButtonGutterTexts));
    case 0xf016:
    case 0xa12:
    case 0xa19:
    case 0xa1a: return TextValue(Pick(kWholeImageTexts));
    case 0xa10: return TextValue(Pick(kElementTexts));
    case 0xa11: return TextValue(Pick(kPairTexts));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

} // namespace legacy_handlers_test

using namespace legacy_handlers_test;

DIFF_TEST(LegacyHandlers_Bitmap, 0x864376)
{
    // Also reached: 0x863ee9, 0x86dc44, 0x86df76 (the handler's callees; direct tests below), 0x863fae, 0x86401e.
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    globals.Set(kServiceCell, &F(kFramework));
    using HandlerFn = bool(__cdecl *)(void *, void *, bool);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        gRng.seed(seed);
        cGZScriptAttributes *list = MakeList(kBitmapIds, sizeof(kBitmapIds) / 4, 10);
        const bool apply = seed % 3 != 0;
        const bool withFace = seed % 7 != 0;
        Blob results[2];
        drawfx::Log logs[2];
        for (int side = 0; side < 2; ++side) {
            BuildWorld(seed, withFace);
            if (side)
                ApplyBitmapProperties(Window(), list, apply);
            else
                ctx.Original<HandlerFn>(0x864376)(Window(), list, apply);
            EndSide(results[side]);
            logs[side] = drawfx::gLog;
        }
        delete list;
        ReleaseValues();
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyHandlers_Bitmap", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyHandlers_Bitmap log", seed)) break;
    }
}

DIFF_TEST(LegacyHandlers_FindRect, 0x863ee9)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    using RectFn = bool(__cdecl *)(const void *, uint32_t, int32_t *);
    const uint32_t ids[] = {kImageRect, kImageRect, 0x7};
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        gRng.seed(seed);
        cGZScriptAttributes *list = MakeList(ids, sizeof(ids) / 4, 4);
        const uint32_t id = ids[Draw(sizeof(ids) / 4)];
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            int32_t words[6] = {-0x11111112, -0x22222223, -0x33333334, -0x44444445, -0x55555556, -0x66666667};
            const bool r = side ? FindRectAttribute(list, id, &words[1]) : ctx.Original<RectFn>(0x863ee9)(list, id, &words[1]);
            Blob &b = results[side];
            b.push_back(r);
            for (int32_t w : words) b.push_back(uint32_t(w));
        }
        delete list;
        ReleaseValues();
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyHandlers_FindRect", seed)) break;
    }
}

DIFF_TEST(LegacyHandlers_LoadImage, 0x86dc44)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    globals.Set(kServiceCell, &F(kFramework));
    using LoadFn = void *(__cdecl *)(uint32_t, uint32_t);
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Blob results[2];
        drawfx::Log logs[2];
        for (int side = 0; side < 2; ++side) {
            BuildWorld(seed, false);
            gRng.seed(seed);
            const uint32_t group = Draw(2) ? Draw(4) : gRng();
            const uint32_t instance = Draw(2) ? Draw(4) : gRng();
            void *const image = side ? static_cast<void *>(LoadImageResource(group, instance))
                                     : ctx.Original<LoadFn>(0x86dc44)(group, instance);
            results[side].push_back(image == &gImage ? 1 : image ? 2 : 0);
            EndSide(results[side]);
            logs[side] = drawfx::gLog;
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyHandlers_LoadImage", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyHandlers_LoadImage log", seed)) break;
    }
}

DIFF_TEST(LegacyHandlers_PairProperty, 0x86df76)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    using PairFn = bool(__cdecl *)(void *, uint32_t, uint32_t, int32_t);
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        Blob results[2];
        drawfx::Log logs[2];
        for (int side = 0; side < 2; ++side) {
            BuildWorld(seed, false);
            gRng.seed(seed);
            const uint32_t group = Draw(2) ? Draw(4) : gRng();
            const uint32_t instance = Draw(2) ? Draw(4) : gRng();
            const int32_t index = Draw(3) ? int32_t(Draw(4)) : int32_t(gRng());
            const bool r = side ? SetImagePairProperty(Window(), group, instance, index)
                                : (ctx.Original<PairFn>(0x86df76)(Window(), group, instance, index) & 0xff) != 0;
            results[side].push_back(r);
            EndSide(results[side]);
            logs[side] = drawfx::gLog;
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyHandlers_PairProperty", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyHandlers_PairProperty log", seed)) break;
    }
}

DIFF_TEST(LegacyHandlers_FlatRect, 0x8674f6)
{
    // Also reached: 0x863e06 (FindColorAttribute), 0x86401e.
    const uint32_t ids[] = {0xf005, 0xf005, 0xf005, 0x200, 0x201, 0x202, 0x203, 0x200, 0x7, 0x204};
    RunHandler<kFlatRectFace>(ctx, {"LegacyHandlers_FlatRect", 0x8674f6, &ApplyFlatRectProperties, cIGZWinFlatRect::kIID, ids,
                                    sizeof(ids) / 4, 12, &FlatRectValue, 3000});
}

DIFF_TEST(LegacyHandlers_Text, 0x86b2be)
{
    // Also reached: 0x86df41 and 0x86df0c (the font properties; direct tests below), 0x863e87, 0x864077, 0x863e06.
    const uint32_t ids[] = {0xf003, 0x300, 0x301, 0xf000, 0xf000, 0xf001, 0xf002, 0xf004, 0x305, 0xf015, 0x304, 0x7};
    RunHandler<kTextFace>(ctx, {"LegacyHandlers_Text", 0x86b2be, &ApplyTextProperties, cIGZWinText::kIID, ids,
                                sizeof(ids) / 4, 12, &TextHandlerValue, 4000});
}

DIFF_TEST(LegacyHandlers_FontProperty, 0x86df41)
{
    // Also reached: 0x86df0c.
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    using NumberFn = bool(__cdecl *)(void *, uint32_t, int32_t);
    using NameFn = bool(__cdecl *)(void *, const void *, int32_t);
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        for (int kind = 0; kind < 2; ++kind) {
            Blob results[2];
            drawfx::Log logs[2];
            for (int side = 0; side < 2; ++side) {
                BuildWorld(seed, false);
                gRng.seed(seed);
                const uint32_t value = Draw(2) ? Draw(5) : gRng();
                const int32_t index = Draw(3) ? int32_t(Draw(4)) : int32_t(gRng());
                LegacyText text;
                text.mText.FromChar(Pick(kFontNames));
                bool r;
                if (kind == 0)
                    r = side ? SetUint32WindowProperty(Window(), value, index)
                             : (ctx.Original<NumberFn>(0x86df41)(Window(), value, index) & 0xff) != 0;
                else
                    r = side ? SetTextWindowProperty(Window(), &text.mText, index)
                             : (ctx.Original<NameFn>(0x86df0c)(Window(), &text.mText, index) & 0xff) != 0;
                results[side].push_back(r);
                EndSide(results[side]);
                logs[side] = drawfx::gLog;
            }
            if (!svcfx::Same(ctx, results[0], results[1], "LegacyHandlers_FontProperty", seed)) return;
            if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyHandlers_FontProperty log", seed)) return;
        }
    }
}

DIFF_TEST(LegacyHandlers_Slider, 0x86aa7f)
{
    // Also reached: 0x863f55 (FindSint32Attribute; direct test below), 0x863e87, 0x86401e, 0x863fae, 0x863ee9, 0x86dc44,
    // 0x87b35c, 0x86df76 (the image, as the bitmap's).
    const uint32_t ids[] = {0xc00, 0xc00, 0xc01, 0xf016, 0xf016, 0xf018, 0x40b, 0x40b, 0x7};
    RunHandler<kSliderFace>(ctx, {"LegacyHandlers_Slider", 0x86aa7f, &ApplySliderProperties, cIGZWinSlider::kIID, ids,
                                  sizeof(ids) / 4, 10, &RangeValue, 4000});
}

DIFF_TEST(LegacyHandlers_Scrollbar, 0x86a820)
{
    // Also reached: 0x863f55, 0x863e87, 0x86401e, 0x863fae, 0x863ee9, 0x86dc44, 0x87b35c, 0x86df76.
    const uint32_t ids[] = {0xc00, 0xc00, 0xc01, 0xd00, 0xd01, 0xd02, 0xf016, 0xf016, 0xf018, 0x40b, 0x40b, 0x7};
    RunHandler<kScrollbarFace>(ctx, {"LegacyHandlers_Scrollbar", 0x86a820, &ApplyScrollbarProperties,
                                     cIGZWinScrollbar::kIID, ids, sizeof(ids) / 4, 12, &RangeValue, 4000});
}

DIFF_TEST(LegacyHandlers_Outline, 0x86a2a5)
{
    // Also reached: 0x863e06 (FindColorAttribute), the face's slots only through the log. The node list is long enough
    // (up to 60 attributes, most of them nodes) to reach the cap of 32.
    const uint32_t ids[] = {0xf005, 0xf005, 0x1100, 0x1101, 0x7, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102,
                            0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102};
    RunHandler<kOutlineFace>(ctx, {"LegacyHandlers_Outline", 0x86a2a5, &ApplyOutlineProperties, cIGZWinOutline::kIID, ids,
                                   sizeof(ids) / 4, 60, &OutlineValue, 4000, 0x1102});
}

DIFF_TEST(LegacyHandlers_Scrollbar2, 0x86a4a8)
{
    // Also reached: 0x863f55, 0x86401e, 0x863fae, 0x86dbb5 (LoadImageByKey; direct test below), 0x87b35c.
    const uint32_t ids[] = {0x1700, 0x1701, 0x1702, 0x1703, 0x1704, 0x1705, 0x1706, 0x1707, 0x1707,
                            0x1708, 0x1708, 0x1709, 0x1709, 0x7};
    RunHandler<kScrollbar2Face>(ctx, {"LegacyHandlers_Scrollbar2", 0x86a4a8, &ApplyScrollbar2Properties,
                                      cIGZWinScrollbar2::kIID, ids, sizeof(ids) / 4, 14, &Scrollbar2Value, 4000});
}

DIFF_TEST(LegacyHandlers_Folders, 0x8676ea)
{
    // Also reached: 0x86df41, 0x86df0c, 0x86dc44, 0x86df76, 0x87b35c, 0x86401e, 0x863fae.
    const uint32_t ids[] = {0xf000, 0xf000, 0xf016, 0xf016, 0x502, 0x502, 0x502, 0x7};
    RunHandler<kFoldersFace>(ctx, {"LegacyHandlers_Folders", 0x8676ea, &ApplyFoldersProperties, cIGZWinFolders::kIID, ids,
                                   sizeof(ids) / 4, 8, &FoldersValue, 4000, 0xf016, 0x502});
}

DIFF_TEST(LegacyHandlers_LineInput, 0x8688a3)
{
    // Also reached: 0x86df41, 0x86df0c, 0x863e06, 0x863e87, 0x86401e, 0x863fae.
    const uint32_t ids[] = {0xf000, 0xf00f, 0xf010, 0xf011, 0xe03, 0xe04, 0xe05, 0xe06, 0xe07, 0xe08, 0xe00, 0xf005, 0xf004,
                            0xe09, 0x400, 0xe0a, 0xe0b, 0x7};
    RunHandler<kLineInputFace>(ctx, {"LegacyHandlers_LineInput", 0x8688a3, &ApplyLineInputProperties,
                                     cIGZWinLineInput::kIID, ids, sizeof(ids) / 4, 16, &LineInputValue, 5000, 0xf004});
}

DIFF_TEST(LegacyHandlers_ListBox, 0x868e93)
{
    // Also reached: 0x86df41, 0x86df0c, 0x863e06, 0x863e87, 0x864077, 0x86401e, 0x863fae.
    const uint32_t ids[] = {0x402, 0x401, 0xf000, 0xf00f, 0xf010, 0xf011, 0x408, 0xf019, 0x40c, 0xf00b, 0x500, 0xf004, 0x504,
                            0x502, 0x502, 0x503, 0x501, 0x505, 0x506, 0x304, 0x7};
    RunHandler<kListBoxFace>(ctx, {"LegacyHandlers_ListBox", 0x868e93, &ApplyListBoxProperties, cIGZWinListBox::kIID, ids,
                                   sizeof(ids) / 4, 24, &ListBoxValue, 5000, 0, 0x502});
}

DIFF_TEST(LegacyHandlers_Combo, 0x865043)
{
    // Also reached: 0x86df41, 0x86df0c, 0x86dc44, 0x86df76, 0x863ee9, 0x863e87, 0x863e06, 0x86401e, 0x863fae.
    const uint32_t ids[] = {0xf000, 0x800, 0x800, 0x801, 0x803, 0xf004, 0xf00f, 0xf010, 0xf011, 0x408, 0x40c, 0x802, 0x400,
                            0x502, 0x502, 0x501, 0x7};
    RunHandler<kComboFace>(ctx, {"LegacyHandlers_Combo", 0x865043, &ApplyComboProperties, cIGZWinCombo::kIID, ids,
                                 sizeof(ids) / 4, 14, &ComboValue, 5000, 0, 0x502});
}

DIFF_TEST(LegacyHandlers_Spinner, 0x86ac57)
{
    // Also reached: 0x86df41, 0x86df0c, 0x86dc44, 0x86df76, 0x87b35c, 0x863e06, 0x863e87, 0x863f55, 0x86401e, 0x863fae.
    const uint32_t ids[] = {0x1000, 0xf019, 0xf00d, 0x1001, 0x1002, 0x1003, 0xf000, 0xf00f, 0xf010, 0xf011, 0xf016, 0xf016,
                            0xf015, 0xf004, 0x1006, 0x1007, 0x1008, 0x1009, 0x100a, 0x1004, 0x1005, 0x40b, 0x40b, 0x7};
    RunHandler<kSpinnerFace>(ctx, {"LegacyHandlers_Spinner", 0x86ac57, &ApplySpinnerProperties, cIGZWinSpinner::kIID, ids,
                                   sizeof(ids) / 4, 14, &SpinnerValue, 6000, 0xf016});
}

DIFF_TEST(LegacyHandlers_TextTicker, 0x86be58)
{
    // Also reached: 0x86df41, 0x86df0c, 0x86401e, 0x863fae. The colours and the messages come in list order, so the
    // lists repeat each id (messages before and after the colours).
    const uint32_t ids[] = {0x1200, 0x1201, 0xf000, 0xf001, 0xf001, 0xf002, 0xf002, 0x1202, 0x1202, 0x1202, 0x1202, 0x7};
    RunHandler<kTextTickerFace>(ctx, {"LegacyHandlers_TextTicker", 0x86be58, &ApplyTextTickerProperties,
                                      cIGZWinTextTicker::kIID, ids, sizeof(ids) / 4, 14, &TextTickerValue, 6000, 0xf001, 0,
                                      {0xf002}, &KeepTickerMessage});
}

DIFF_TEST(LegacyHandlers_Button, 0x8645be)
{
    // Also reached: 0x86df41, 0x86df0c, 0x86dc44, 0x86df76, 0x86dceb, 0x86dbb5, 0x863fae.
    const uint32_t ids[] = {0xa00, 0xa01, 0xa02, 0xf00d, 0xa03, 0xa04, 0xa05, 0xa06, 0xa07, 0xa08, 0xf005, 0xf005, 0xf000,
                            0xf00f, 0xf010, 0xf011, 0xf012, 0xf013, 0xf014, 0xf004, 0xf016, 0xf015, 0xa10, 0xa12, 0xa11,
                            0xa18, 0xa19, 0xa1a, 0x7};
    RunHandler<kButtonFace>(ctx, {"LegacyHandlers_Button", 0x8645be, &ApplyButtonProperties, cIGZWinButton::kIID, ids,
                                  sizeof(ids) / 4, 16, &ButtonValue, 8000, 0xf016, 0, {0xa12, 0xa19, 0xa1a}});
}

// ---- GZWinTextEdit's face ----------------------------------------------------------------------------------------------

void GiveTextEditFlags(uint32_t seed) { gFace.Returns(86, uint32_t(seed * 2654435761u)); }

// Key texts the original parses unchecked: always both numbers.
const char *const kWholeKeyTexts[] = {"{1a,2b}", "{0,0}", "{abc,def}", "{FFFFFFFF,12345678}", "{3, 4}", "{7,8,9}"};

cIGZUnknown *TextEditValue(uint32_t id)
{
    switch (id) {
    case 0xf000:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0xf00f: case 0xf010: case 0xf011: case 0xf012: case 0xf013: case 0xf014:
    case 0x407: case 0x408: case 0x40c: return TextValue(Pick(kColorTexts));
    case 0xf004: case 0x40f: return TextValue(Pick(kPairTexts));
    case 0x414: case 0x412: return TextValue(Pick(kImageTexts));
    case 0x415: case 0x413: return TextValue(Pick(kRectTexts));
    case 0x41b: return TextValue(Pick(kFontNames));
    case 0x41c: return TextValue(Pick(kWholeKeyTexts));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

// The outline colour (0x40c) reads the outline flag (0xf019) without a check: a list with the colour gets a first 0xf019
// that is a number.
void TextEditFixup(cGZScriptAttributes *list)
{
    bool outline = false;
    for (uint32_t i = 0; i < list->Size(); ++i) outline |= list->mBegin[i].mId == 0x40c;
    if (!outline) return;
    cIGZUnknown *const number = Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    for (uint32_t i = 0; i < list->Size(); ++i) {
        if (list->mBegin[i].mId == 0xf019) {
            list->mBegin[i].mValue = number;
            return;
        }
    }
    cGZScriptAttribute attribute;
    attribute.mId = 0xf019;
    attribute.mValue = number;
    list->PushBack(attribute);
}

DIFF_TEST(LegacyHandlers_TextEdit, 0x86b5b0)
{
    // Also reached: 0x863aa8, 0x86df41, 0x86df0c, 0x86dc44, 0x86df76, 0x86dceb, 0x87b35c, 0x863ee9, 0x863e06, 0x863e87.
    const uint32_t ids[] = {0x400, 0x300, 0x401, 0x402, 0xf019, 0x301, 0x403, 0x404, 0x405, 0x406, 0x416, 0x417, 0x418,
                            0x419, 0x41a, 0xf000, 0xf00f, 0xf010, 0xf011, 0xf012, 0xf013, 0xf014, 0x407, 0x408, 0x40c,
                            0x41b, 0x41c, 0x409, 0x40a, 0xf004, 0x40d, 0x40e, 0x40f, 0x410, 0x411, 0x414, 0x415, 0x412,
                            0x413, 0x304, 0x7};
    RunHandler<kTextEditFace>(ctx, {"LegacyHandlers_TextEdit", 0x86b5b0, &ApplyTextEditProperties, cIGZWinTextEdit::kIID,
                                    ids, sizeof(ids) / 4, 24, &TextEditValue, 8000, 0, 0, {0x41c}, &GiveTextEditFlags,
                                    &TextEditFixup});
}

// ---- GZWinOptGrp's face ------------------------------------------------------------------------------------------------

bool gOptionFound;
uint32_t GetOption(drawfx::Fake &, const uint32_t *a)
{
    if (!gOptionFound) return 0x7700; // false in al, noise above
    ++gOption.refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &gOption;
    return 0x7701;
}

// The face answers with the window itself for slot 3 (the caption goes to it), the options answer with the other window.
void SetupOptGrp(uint32_t seed)
{
    gFace.Returns(3, &gWorld[1].f);
    gFace.On(27, &GetOption);
    gOptionFound = seed % 2 != 0; // (apply false is every third seed: both answers meet it)
    gOption.Returns(3, &gWorld[0].f);
    gWorld[0].f.On(127, &SetProperty);
}

const char *const kOptionNumbers[] = {"0", "1", "-3", "12", "7", "100"};
const char *const kOptionLabels[] = {"Yes", "", "two words", "a,b", "Label (x)", "{1,2}"};
const char *const kOptionImageTexts[] = {"{1a,2b}", "{0,0}", "{abc,def}", "{FFFFFFFF,12345678}", "{3, 4}", "{7,8,9}",
                                         "{5,x}", "{zz,6}", ""};
const char *const kOutlineColorTexts[] = {"(1,2,3)(4,5,6)(7,8,9)(10,11,12)", "(300,400,500)(1,2,3)(0,0,0)(255,255,255)",
                                          "(1,2,3)(4,5,6)(7,8,9)", "(1,2,3)", "x", "", "(1,2,3)(4,5,6)(7,8,9)(10,11)",
                                          "(-1,-2,-3)(4,5,6)(7,8,9)(10,11,12)(13,14,15)"};
const uint32_t kAutofillTypes[] = {0xf0e, 0xf0f, 0, 1, 0xf0e, 0xffffffff};

template <size_t N> const char *PickFrom(const char *const (&pool)[N]) { return pool[Draw(uint32_t(N))]; }

bool gFirstOption = true;
void OptGrpFixup(cGZScriptAttributes *)
{
    gFirstOption = true; // the list is made: the next one starts again
}

cIGZUnknown *OptGrpValue(uint32_t id)
{
    switch (id) {
    case 0xf03:
    case 0xf04:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0xf05: case 0xf06: case 0xf07: case 0xf08: case 0xf09: return TextValue(Pick(kColorTexts));
    case 0xf004: return TextValue(Pick(kPairTexts));
    case 0xf0a: return TextValue(Pick(kWholeKeyTexts));
    case 0xf0b: return TextValue(Pick(kImageTexts));
    case 0x40c: return TextValue(PickFrom(kOutlineColorTexts));
    case 0xf0c: return Keep(new cRZVariant(kAutofillTypes[Draw(sizeof(kAutofillTypes) / 4)]));
    case 0xf0d: { // "first:label:second:{group,instance}:(l,t,r,b)": every part there (the original parses the numbers unchecked)
        // The first option of a list always has a whole image text: the original's group and instance start as stack
        // leftovers, which a later option's failed parse would pass on (an option that parses keeps its own).
        const char *image = gFirstOption ? kOptionImageTexts[Draw(6)] : PickFrom(kOptionImageTexts);
        gFirstOption = false;
        const std::string text = std::string(PickFrom(kOptionNumbers)) + ":" + PickFrom(kOptionLabels) + ":" +
                                 PickFrom(kOptionNumbers) + ":" + image + ":" + Pick(kRectTexts);
        return TextValue(text.c_str());
    }
    case 0xf11:
    case 0xf12: {
        const std::string text = std::string(PickFrom(kOptionNumbers)) + ":" + Pick(kPairTexts);
        return TextValue(text.c_str());
    }
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

DIFF_TEST(LegacyHandlers_OptionGroup, 0x86942b)
{
    // Also reached: 0x86df41, 0x86df0c, 0x86dc44, 0x86df76, 0x86dceb, 0x87b35c, 0x863e06, 0x863e87, 0x86401e, 0x863fae,
    // 0x1ad555 (SplitOff).
    const uint32_t ids[] = {0xf00, 0xf01, 0xf02, 0xf019, 0xf004, 0xf03, 0xf04, 0xf05, 0xf06, 0xf07, 0xf08, 0xf09, 0xf0a,
                            0xf0b, 0x40c, 0xf0c, 0xf0c, 0xf10, 0xf0d, 0xf0d, 0xf0d, 0xf11, 0xf11, 0xf12, 0xf12, 0x501, 0x7};
    RunHandler<kOptGrpFace>(ctx, {"LegacyHandlers_OptionGroup", 0x86942b, &ApplyOptionGroupProperties, cIGZWinOptGrp::kIID,
                                  ids, sizeof(ids) / 4, 20, &OptGrpValue, 8000, 0, 0, {0xf0a, 0xf0d}, &SetupOptGrp, &OptGrpFixup,
                                  {0xf0d, 0xf11, 0xf12}});
}

// ---- SC3WinGen's face --------------------------------------------------------------------------------------------------

// The window procedure the handler makes: by content (it is ours or the game's by its vtable, whole and uncounted), then freed.
uint32_t SetWinProc(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Call &call = drawfx::gLog.back();
    auto *proc = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    call.words[0] = proc != nullptr;
    if (proc) {
        cGZDummyWinProc probe;
        const uint32_t own = gRunSide ? *reinterpret_cast<const uint32_t *>(&probe) : uint32_t(uintptr_t(GameAddress<void>(0xe49270)));
        call.words.push_back(proc[0] == own);
        call.words.push_back(proc[1]);
        ::operator delete(proc);
    }
    return 1;
}

void SetupGen(uint32_t)
{
    gFace.On(7, &SetWinProc);
}

const char *const kDividerTexts[] = {"(1,2)", "(4294967295,-5)", "(7)", "x", "", "(1,2,3)", "( 3 , 4 )", "(-1,5)", "(0,0)"};
const char *const kBlueBarTexts[] = {"(1,2,3,4,5)", "(1,2,3,4)", "(9,-1,-2,-3,-4)", "(4294967295,0,0,0,0)", "", "x",
                                     "(1,2,3,4,5,6)", "(0,0,0,0,0)"};

cIGZUnknown *GenValue(uint32_t id)
{
    switch (id) {
    case 0xf016: return TextValue(Pick(kImageTexts));
    case 0xf004: return TextValue(Pick(kPairTexts));
    case 0x10002: return TextValue(Pick(kDividerTexts));
    case 0x10003: return TextValue(Pick(kBlueBarTexts));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

DIFF_TEST(LegacyHandlers_Gen, 0x86d5ee)
{
    // Also reached: 0x86401e, 0x863fae, 0x863e87, 0x86dc44, 0x86df76, 0x87b35c.
    const uint32_t ids[] = {0x10004, 0x10005, 0x10006, 0x10007, 0x10008, 0x10009, 0x1000a, 0x1000b, 0x1000c, 0x1000d, 0xf00d,
                            0xf019,  0x1000e, 0x1000f, 0x10000, 0xf016,  0x10001, 0xf004,  0x10002, 0x10002, 0x10003, 0x10003,
                            0x7};
    RunHandler<kGenFace>(ctx, {"LegacyHandlers_Gen", 0x86d5ee, &ApplyGenProperties, cIGZWinGen::kIID, ids, sizeof(ids) / 4, 24,
                               &GenValue, 8000, 0, 0, {}, &SetupGen, nullptr});
}

// ---- GZWinFileBrowser's face -------------------------------------------------------------------------------------------

// The getters write the face's free words (set per seed) through their out pointers (the flag is one byte).
uint32_t *OutWord(const uint32_t *a, int index) { return reinterpret_cast<uint32_t *>(uintptr_t(a[index])); }
uint32_t BrowserGetCorners(drawfx::Fake &f, const uint32_t *a)
{
    *OutWord(a, 0) = f.value[0];
    *OutWord(a, 1) = f.value[1];
    return 0;
}
uint32_t BrowserGetBars(drawfx::Fake &f, const uint32_t *a)
{
    *OutWord(a, 0) = f.value[2];
    *OutWord(a, 1) = f.value[3];
    *OutWord(a, 2) = f.value[4];
    return 0;
}
uint32_t BrowserGetSelection(drawfx::Fake &f, const uint32_t *a)
{
    *reinterpret_cast<uint8_t *>(uintptr_t(a[0])) = uint8_t(f.value[5] & 1);
    *OutWord(a, 1) = f.value[6];
    return 0;
}
uint32_t BrowserGetCursor(drawfx::Fake &f, const uint32_t *a)
{
    *reinterpret_cast<uint8_t *>(uintptr_t(a[0])) = uint8_t(f.value[7] & 1);
    *OutWord(a, 1) = f.value[0];
    return 0;
}
uint32_t BrowserGetBorder(drawfx::Fake &f, const uint32_t *a)
{
    *OutWord(a, 0) = f.value[1];
    *OutWord(a, 1) = f.value[2];
    return 0;
}

// The window keeps the entry key list (a property of an untyped byte variant) so the real helper 0x87b143 finds what it
// stored before: GetProperty hands the stored variant out (AddRef'd), SetProperty records and stores a new one.
constexpr uint32_t kEntryKeyProperty = 0x2c64e5c4;
cIGZVariant *gKeyList; // the stored variant (not owned: gProps holds the reference)

uint32_t BrowserGetProperty(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Call &call = drawfx::gLog.back();
    call.words[1] = 1; // the out pointer is a stack address
    if (a[0] != kEntryKeyProperty || !gKeyList) return 0;
    gKeyList->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = gKeyList;
    return 1;
}
uint32_t BrowserSetProperty(drawfx::Fake &f, const uint32_t *a)
{
    SetProperty(f, a);
    if (a[0] == kEntryKeyProperty) gKeyList = reinterpret_cast<cIGZVariant *>(uintptr_t(a[1]));
    return 1;
}
// The list as the handler left it: the variant's count, tag and bytes (inserts go through SetValVoid, not SetProperty).
void DumpKeyList(Blob &b)
{
    if (!gKeyList) return;
    const uint8_t *v = reinterpret_cast<const uint8_t *>(gKeyList);
    b.push_back(0xc0000000u | At<uint16_t>(v, 0xc));
    b.push_back(At<uint16_t>(v, 0xe));
    const uint32_t count = At<uint32_t>(v, 0x10);
    b.push_back(count);
    const uint8_t *data = Bytes(At<uint32_t>(v, 4));
    for (uint32_t i = 0; data && i < count; ++i) b.push_back(data[i]);
    gKeyList = nullptr;
}

// Half of the lists get their entry attributes moved to the front: the key helper's index is the attribute's position, and
// the list only takes an index up to its length, so an entry past the first few positions is never added.
void FixupBrowser(cGZScriptAttributes *list)
{
    if (Draw(2) == 0) return;
    std::stable_partition(list->mBegin, list->mBegin + list->Size(),
                          [](const cGZScriptAttribute &a) { return a.mId == 0x1342; });
}

void SetupBrowser(uint32_t seed)
{
    gKeyList = nullptr;
    gDumpExtra = &DumpKeyList;
    gWorld[1].f.On(126, &BrowserGetProperty);
    gWorld[1].f.On(127, &BrowserSetProperty);
    std::mt19937 rng(seed * 977 + 1);
    for (uint32_t &word : gFace.value) word = rng() % 3 ? rng() % 8 : uint32_t(rng());
    gFace.On(36, &BrowserGetCorners);
    gFace.On(40, &BrowserGetBars);
    gFace.On(80, &BrowserGetSelection);
    gFace.On(84, &BrowserGetCursor);
    gFace.On(88, &BrowserGetBorder);
}

const char *const kBrowserTexts[] = {"Caption", "", "two words", "a:b", "{1,2}"};
const char *const kBrowserEntryTexts[] = {"label:3:{1a,2b}", "x:y:{3,4}", "name:-5:{abc,def}", "a::{1,2}", "no separators",
                                          "a:7", "", "l:1:{zz,6}", "a:1:{1,2}:more", "a:2:{FFFFFFFF,12345678}", "::", "b:9:{0,0}"};
const uint32_t kKindWords[] = {0x1302, 0x1303, 0x1304, 0x1305, 0, 1};
const uint32_t kCornerWords[] = {0x1308, 0x1309, 0x130a, 0x130b, 0x1307, 0x130c, 0};
const uint32_t kFilterWords[] = {0x130e, 0x130f, 0x1310, 0x1311, 0x130d, 0x1312, 0};
const uint32_t kBarWords[] = {0x1313, 0x1314, 0x1315, 0x1316, 0x1312, 0x1317, 0};
const uint32_t kModeWords[] = {0x1317, 0x1318, 0xf006, 0xf007, 0xf008, 0xf005, 0xf009, 0};

template <size_t N> uint32_t PickWord(const uint32_t (&pool)[N]) { return pool[Draw(uint32_t(N))]; }

bool IsBrowserColor(uint32_t id)
{
    return (id >= 0x131d && id <= 0x1322) || (id >= 0x1325 && id <= 0x132a) || (id >= 0x132d && id <= 0x1332) ||
           id == 0x1338 || id == 0x133a || id == 0x133b || id == 0x133c || id == 0x133f;
}

cIGZUnknown *BrowserValue(uint32_t id)
{
    if (IsBrowserColor(id)) return TextValue(Pick(kWholeColorTexts));
    switch (id) {
    case 0x1301: case 0x1306: case 0x1307: case 0x133d: return TextValue(Pick(kBrowserTexts));
    case 0x131c: case 0x1324: case 0x132c:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0x1305: return Keep(new cRZVariant(PickWord(kKindWords)));
    case 0x130c: case 0x130d: return Keep(new cRZVariant(PickWord(kCornerWords)));
    case 0x1312: return Keep(new cRZVariant(PickWord(kFilterWords)));
    case 0x1319: case 0x131a: case 0x131b: return Keep(new cRZVariant(PickWord(kBarWords)));
    case 0x1323: case 0x132b: case 0x1333: return Keep(new cRZVariant(PickWord(kModeWords)));
    case 0x1340: return TextValue(Pick(kImageTexts));
    case 0x1342: return TextValue(Pick(kBrowserEntryTexts));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

DIFF_TEST(LegacyHandlers_FileBrowser, 0x866410)
{
    // Also reached: 0x86401e, 0x863fae, 0x863e06, 0x86df41, 0x86df0c, 0x86dc44, 0x86df76, 0x87b35c, 0x9c5a05, 0x9c5baa.
    const uint32_t ids[] = {0x1301, 0x1305, 0x1306, 0x1307, 0x130c, 0x130d, 0x1312, 0x1319, 0x131a, 0x131b, 0x131c, 0x131d,
                            0x131e, 0x131f, 0x1320, 0x1321, 0x1322, 0x1323, 0x1324, 0x1325, 0x1326, 0x1327, 0x1328, 0x1329,
                            0x132a, 0x132b, 0x132c, 0x132d, 0x132e, 0x132f, 0x1330, 0x1331, 0x1332, 0x1333, 0x1334, 0x1335,
                            0x1336, 0x1337, 0x1338, 0x1339, 0x133a, 0x133b, 0x133c, 0x133d, 0x133e, 0x133f, 0x1340, 0x1341,
                            0x1342, 0x1342, 0x1342, 0xf019, 0x7};
    std::vector<uint32_t> colors;
    for (uint32_t id = 0x131d; id <= 0x133f; ++id) {
        if (IsBrowserColor(id)) colors.push_back(id);
    }
    RunHandler<kBrowserFace>(ctx, {"LegacyHandlers_FileBrowser", 0x866410, &ApplyFileBrowserProperties, cIGZWinFileBrowser::kIID,
                                   ids, sizeof(ids) / 4, 30, &BrowserValue, 8000, 0, 0x1342, colors, &SetupBrowser, &FixupBrowser});
}

DIFF_TEST(LegacyHandlers_FlagTable, 0x863aa8)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    using TableFn = uint32_t(__cdecl *)(const LegacyFlagTableEntry *, const void *, uint32_t);
    static const LegacyFlagTableEntry table[] = {{0x400, 1},    {0x300, 2},      {0x401, 4},      {0x402, 8},
                                                 {0xf019, 0x10}, {0x301, 0x20},  {0x403, 0x40},   {0x404, 0x80},
                                                 {0x300, 0x102}, {0x405, 0x100}, {0x406, 0x8000}, {0, 0}};
    const uint32_t ids[] = {0x400, 0x300, 0x401, 0x402, 0xf019, 0x301, 0x403, 0x404, 0x405, 0x406, 0x7};
    for (uint32_t seed = 1; seed <= 6000; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            gRng.seed(seed);
            cGZScriptAttributes *list = MakeList(ids, sizeof(ids) / 4, 14);
            const uint32_t flags = gRng();
            const uint32_t r = side ? ApplyFlagTable(table, list, flags)
                                    : ctx.Original<TableFn>(0x863aa8)(table, list, flags);
            results[side].push_back(r);
            delete list;
            ReleaseValues();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyHandlers_FlagTable", seed)) break;
    }
}

DIFF_TEST(LegacyHandlers_LoadImageKey, 0x86dbb5)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    globals.Set(kServiceCell, &F(kFramework));
    using LoadFn = void *(__cdecl *)(const cGZPersistResourceKey *);
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Blob results[2];
        drawfx::Log logs[2];
        for (int side = 0; side < 2; ++side) {
            BuildWorld(seed, false);
            gRng.seed(seed);
            cGZPersistResourceKey key;
            key.type = Draw(2) ? Draw(4) : gRng();
            key.group = Draw(2) ? Draw(4) : gRng();
            key.instance = Draw(2) ? Draw(4) : gRng();
            key.resource = Draw(3) ? 0 : gRng();
            void *const image = side ? static_cast<void *>(LoadImageByKey(&key))
                                     : ctx.Original<LoadFn>(0x86dbb5)(&key);
            results[side].push_back(image == &gImage ? 1 : image ? 2 : 0);
            EndSide(results[side]);
            logs[side] = drawfx::gLog;
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyHandlers_LoadImageKey", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyHandlers_LoadImageKey log", seed)) break;
    }
}

DIFF_TEST(LegacyHandlers_FindSint32, 0x863f55)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    using ReadFn = bool(__cdecl *)(const void *, uint32_t, int32_t *);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            gRng.seed(seed);
            const uint32_t ids[] = {0x40b, 0x40b, 0x7};
            cGZScriptAttributes *list = MakeList(ids, 3, 6);
            int32_t out = int32_t(0xdddddddd);
            const bool r = side ? FindSint32Attribute(list, 0x40b, &out)
                                : ctx.Original<ReadFn>(0x863f55)(list, 0x40b, &out);
            results[side].push_back(r);
            results[side].push_back(uint32_t(out));
            delete list;
            ReleaseValues();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyHandlers_FindSint32", seed)) break;
    }
}

// ---- GZWinGrid's face ---------------------------------------------------------------------------------------------------

std::vector<const uint8_t *> gHolders; // the counted holders the grid put on the window, in order
std::vector<bool> gHolderIsCell;       // whether each holds a cell matrix (else a column or row description matrix)

std::vector<const void *> gMatrices; // the matrices the grid made, in order (by their holders)

// The matrices as the grid left them, node by node (the real constructor and setters run on both sides now): the head's
// numbers, then each line and each entry in order with its keys, tree colours and nil flags, then the value.
uint32_t HashText(const char *text);

void DumpEntryValue(Blob &b, const uint8_t *node, bool cell)
{
    for (uint32_t off : {0x18u, 0x1cu, 0x20u}) b.push_back(At<uint32_t>(node, off)); // line data, or group, instance, flags
    if (!cell) return;
    const uint8_t *cellText = node + 0x24;
    const cRZString *text = reinterpret_cast<const cRZString *>(cellText);
    b.push_back(At<uint32_t>(cellText, 0)); // the string's vtable
    b.push_back(text->mRefCount); // its count
    b.push_back(HashText(text->ToChar()));
    b.push_back(text->Strlen());
    b.push_back(uint32_t(text->mString.capacity()));
    for (uint32_t off = 0x44; off <= 0x58; off += 4) b.push_back(At<uint32_t>(node, off));
}

uint32_t DumpTree(Blob &b, const uint8_t *node, bool nested, bool cell)
{
    if (node[0xd]) return 0;
    const uint32_t left = DumpTree(b, Bytes(At<uint32_t>(node, 0)), nested, cell);
    b.push_back(0xa0000000u | (node[0xc] << 8) | node[0xd]);
    b.push_back(At<uint32_t>(node, 0x10)); // the key
    b.push_back(At<uint32_t>(node, 0x14)); // its second copy
    uint32_t below = 0;
    if (nested) {
        const uint8_t *head = Bytes(At<uint32_t>(node, 0x18));
        b.push_back(At<uint32_t>(node, 0x1c)); // the entry map's size
        below = DumpTree(b, Bytes(At<uint32_t>(head, 4)), false, cell);
    } else {
        DumpEntryValue(b, node, cell);
    }
    return left + 1 + below + DumpTree(b, Bytes(At<uint32_t>(node, 8)), nested, cell);
}

void DumpMatrices(Blob &b)
{
    for (size_t i = 0; i < gHolders.size(); ++i) {
        const uint8_t *matrix = Bytes(At<uint32_t>(gHolders[i], 4));
        b.push_back(0xb0000000u | uint32_t(i) | (gHolderIsCell[i] ? 0x100u : 0u));
        for (uint32_t off = 4; off < 0x1c; off += 4) b.push_back(At<uint32_t>(matrix, off));
        b.push_back(matrix[0x1c]);
        const uint8_t *head = Bytes(At<uint32_t>(matrix, 0));
        b.push_back(head[0xc]);
        b.push_back(head[0xd]);
        b.push_back(DumpTree(b, Bytes(At<uint32_t>(head, 4)), true, gHolderIsCell[i]));
    }
    gHolders.clear();
    gHolderIsCell.clear();
}

uint32_t MatrixIndex(const void *matrix)
{
    for (size_t i = 0; i < gMatrices.size(); ++i) {
        if (gMatrices[i] == matrix) return uint32_t(i + 1);
    }
    return 0;
}

// A holder's vtable word as a code: the original's holder carries the game's table, ours its own class's (the holder
// classes are ours, rz/cRZUnknownAutoPtr.h, and the grid handler builds ours); each as the code of its property's kind, any
// other word as is. The words are the only bytes normalised.
uint32_t HolderTableCode(uint32_t property, uint32_t word)
{
    uint32_t gameRva;
    uint32_t oursWord;
    {
        cRZUnknownAutoPtrWinGridCellData cell;
        cRZUnknownAutoPtrWinGridColData column;
        cRZUnknownAutoPtrWinGridRowData row;
        const uint32_t *words[3] = {reinterpret_cast<const uint32_t *>(&cell), reinterpret_cast<const uint32_t *>(&column),
                                    reinterpret_cast<const uint32_t *>(&row)};
        const uint32_t rvas[3] = {0xe49b9c, 0xe49bac, 0xe49bbc};
        const unsigned kind = property == 0x287a7136 ? 0 : property == 0xc88ccb6d ? 1 : 2;
        gameRva = rvas[kind];
        oursWord = *words[kind];
        const uint32_t game = uint32_t(uintptr_t(GameAddress<void>(gameRva)));
        if (word == game || word == oursWord) return 0xd0000000u | kind;
    }
    return word;
}

// What the grid keeps on the window: a variant of a counted holder (its vtable, count, owning flag and matrix).
uint32_t GridSetProperty(drawfx::Fake &f, const uint32_t *a)
{
    if (a[0] != 0x287a7136 && a[0] != 0xc88ccb6d && a[0] != 0xc88ccb6e) return SetProperty(f, a); // the font's
    drawfx::Call &call = drawfx::gLog.back();
    call.words[1] = a[1] != 0;
    if (a[1]) {
        const uint8_t *v = Bytes(a[1]);
        call.words.push_back(At<uint16_t>(v, 0xe));
        const uint8_t *holder = Bytes(At<uint32_t>(v, 4));
        call.words.push_back(holder != nullptr);
        if (holder) {
            gHolders.push_back(holder);
            gHolderIsCell.push_back(a[0] == 0x287a7136);
            gMatrices.push_back(Bytes(At<uint32_t>(holder, 4)));
            call.words.push_back(HolderTableCode(a[0], At<uint32_t>(holder, 0)));
            call.words.push_back(At<uint32_t>(holder, 8));
            call.words.push_back(holder[0xc]);
            call.words.push_back(MatrixIndex(Bytes(At<uint32_t>(holder, 4))));
        }
        auto *value = reinterpret_cast<cIGZVariant *>(uintptr_t(a[1])); // kept, so the matrices can be dumped after the run
        value->AddRef();
        gProps.push_back(value);
    }
    return 1;
}

uint32_t HashText(const char *text)
{
    uint32_t h = 2166136261u;
    for (; *text; ++text) h = (h ^ uint8_t(*text)) * 16777619u;
    return h;
}

const uint32_t kCellFlagPool[] = {0x40000, 0xc0000, 0x2000, 0x10000, 0x90000, 0x41000, 0x103000, 0x11000, 0, 0x2000, 0x40001,
                                  0x1000, 0x80000, 0x100000};
const char *const kCellResources[] = {"00000001,00000002", "1a,2b", "0000ffff,0000eeee", "ffffffff,0", "5,6"};
uint32_t gGridSalt;  // varies the parser's answers from list to list
bool gGridForceText; // the list's last flag number has the 0x40000 bit: the handler does not scan the resource text
const char *const kCellLabels[] = {"label", "", "two words", "x"};

// The cell parser 0x863ba3 (the original, called by both sides): the outputs come from a hash of the text.
bool __cdecl StandParseCell(cRZString *text, uint32_t *column, uint32_t *row, uint32_t *flags, cRZString *resource,
                            uint32_t *fontId, uint32_t *w0, uint32_t *w1, uint32_t *w2, uint32_t *w3, uint32_t *w4)
{
    const uint32_t h = HashText(text->ToChar()) ^ gGridSalt;
    drawfx::Note("parse cell", {h});
    if (h % 6 == 0) return false;
    *column = (h >> 3) % 6;
    *row = (h >> 7) % 6;
    *flags = kCellFlagPool[(h >> 11) % (sizeof(kCellFlagPool) / 4)];
    // The original reads a cell's group and instance from locals it never sets when it skips or fails the scan (the last
    // flag number tests the 0x40000 bit): the run keeps those cases out, so every cell that needs them has them scanned.
    if (gGridForceText) {
        *flags |= 0x40000;
        resource->FromChar(kCellLabels[(h >> 15) % 4]);
    } else {
        resource->FromChar(kCellResources[(h >> 15) % (sizeof(kCellResources) / sizeof(kCellResources[0]))]);
    }
    *fontId = (h >> 19) % 3 ? 0x40 + (h >> 21) % 5 : 0;
    *w0 = h * 3;
    *w1 = h >> 5;
    *w2 = h >> 9;
    *w3 = h >> 13;
    *w4 = h ^ 0x5555;
    return true;
}

void SetupGrid(uint32_t)
{
    gMatrices.clear();
    gHolders.clear();
    gHolderIsCell.clear();
    gDumpExtra = &DumpMatrices;
    gFace.Returns(5, &gWorld[1].f);
    gWorld[1].f.On(121, &ConvertRGB);
    gWorld[1].f.On(127, &GridSetProperty);
}

const uint32_t kGridFlagValues[] = {0, 1, 2, 0x40000, 0x40001, 5, 0xffffffff, 0x80000000, 7, 0x40000};

void FixupGrid(cGZScriptAttributes *list)
{
    // The flag number the cells read is never set by the original when no flag attribute is found: such a list gets one.
    int32_t last = 0;
    bool any = false;
    for (uint32_t id = 0x160c; id <= 0x1622; ++id) any |= FindSint32Attribute(list, id, &last);
    if (!any) {
        cGZScriptAttribute a;
        a.mId = 0x161e;
        const uint32_t value = kGridFlagValues[Draw(sizeof(kGridFlagValues) / 4)];
        a.mValue = Keep(new cRZVariant(value));
        list->PushBack(a);
        last = int32_t(value);
    }
    gGridForceText = (last & 0x40000) != 0;
    gGridSalt = gRng();
}

const char *const kCellTexts[] = {"a", "bb", "ccc", "dddd", "e", "ff", "g1", "h22", "i333", "jj", "k", "ll"};
const char *const kLineTexts[] = {"1,3,5", "2,2,0", "x", "1,2", "-1,4,9", "7,3,2", "", "10,20,30", "0,0,0", "3,1,4"};

cIGZUnknown *GridValue(uint32_t id)
{
    switch (id) {
    case 0x1601: case 0x1604: case 0x1623: case 0x1624: return TextValue(Pick(kWholeColorTexts));
    case 0xf000:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(kFontNames[Draw(sizeof(kFontNames) / sizeof(kFontNames[0]))]);
    case 0xf004: return TextValue(Pick(kRectTexts));
    case 0x1625: return TextValue(Pick(kCellTexts));
    case 0x1626: case 0x1627: return TextValue(Pick(kLineTexts));
    case 0x1609: return Keep(new cRZVariant(uint32_t(Draw(5))));
    default:
        if (id >= 0x160c && id <= 0x1622) return Keep(new cRZVariant(kGridFlagValues[Draw(sizeof(kGridFlagValues) / 4)]));
        return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

DIFF_TEST(LegacyHandlers_Grid, 0x867920)
{
    // Also reached: 0x85d5f0, 0x940a6e, 0x85d468 (cell and font constructors, run for real on the original's side), the
    // attribute readers and the services as in the other handlers. The matrix helpers run for real on both sides (the original's, ours); the matrices are dumped node by node after each run.
    shape::StandIn parse(0x863ba3, &ParseGridCell, &StandParseCell); // ours since lane MM: LegacyHandlers_ParseGridCell
    const uint32_t ids[] = {0x1601, 0x1602, 0x1603, 0x1604, 0x1605, 0x1606, 0x1607, 0x1608, 0x1609, 0x160a, 0x160b, 0x160c,
                            0x160d, 0x160e, 0x160f, 0x160f, 0x1610, 0x1611, 0x1612, 0x1613, 0x1614, 0x1615, 0x1616, 0x1617,
                            0x1618, 0x1619, 0x161a, 0x161b, 0x161c, 0x161d, 0x161e, 0x161f, 0x1620, 0x1621, 0x1622, 0x1623,
                            0x1624, 0x1625, 0x1625, 0x1625, 0x1625, 0x1626, 0x1626, 0x1627, 0x1627, 0xf000, 0xf004, 0x7};
    RunHandler<kGridFace>(ctx, {"LegacyHandlers_Grid", 0x867920, &ApplyGridProperties, cIGZWinGrid::kIID, ids,
                                sizeof(ids) / 4, 40, &GridValue, 12000, 0, 0x1625, {0x1601, 0x1604, 0x1623, 0x1624},
                                &SetupGrid, &FixupGrid, {0x1626, 0x1627}});
}

// ---- GZWinTreeView's face ----------------------------------------------------------------------------------------------

uint32_t Mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x45d9f3b;
    x ^= x >> 16;
    x *= 0x45d9f3b;
    return x ^ (x >> 16);
}

// The numbers the face hands out (repeats, zero and values with the top bit set: the tables order them as signed).
const uint32_t kTreeNumbers[] = {1, 2, 3, 5, 0x80000001, 0xffffffff, 0x7fffffff, 2, 0, 0x80000000, 4, 1};

// The face's counters: value[0] images, value[1] the seed, value[2] nodes.
uint32_t TreeAddImage(drawfx::Fake &f, const uint32_t *)
{
    return kTreeNumbers[Mix(f.value[0]++ ^ f.value[1] * 31) % (sizeof(kTreeNumbers) / 4)];
}
uint32_t TreeAddNode(drawfx::Fake &f, const uint32_t *)
{
    return kTreeNumbers[Mix(f.value[2]++ * 7 + f.value[1]) % (sizeof(kTreeNumbers) / 4)];
}
uint32_t TreeGetFont(drawfx::Fake &, const uint32_t *a) { return a[0] * 3 + 0x11; }
uint32_t TreeFindFontId(drawfx::Fake &, const uint32_t *a)
{
    return HashText(reinterpret_cast<const cRZString *>(uintptr_t(a[0]))->ToChar());
}

// What the tree keeps on the window: the map behind each of the six table variants, dumped as the raw tree (the original's
// and ours must lay the nodes out alike): per node the colour byte, whether the parent link agrees, the key and the
// value; per name key the string's vtable, text and count. The pointers differ between the sides and are not compared.
enum class TableKind { NameNumber, NumberName, NameResource };

bool TableKindOf(uint32_t property, TableKind *kind)
{
    switch (property) {
    case 0x46c207a5: case 0x86c21da0: case 0xe6c21d83: *kind = TableKind::NameNumber; return true;
    case 0xc6c207c8: case 0x26c21e35: *kind = TableKind::NumberName; return true;
    case 0x66c20802: *kind = TableKind::NameResource; return true;
    }
    return false;
}

void DumpName(drawfx::Call &call, const uint8_t *key)
{
    const cRZString *s = reinterpret_cast<const cRZString *>(key);
    call.words.push_back(At<uint32_t>(key, 0) == uint32_t(uintptr_t(GameAddress<void>(cRZString::kVTable))));
    call.words.push_back(HashText(s->mString.c_str()));
    call.words.push_back(uint32_t(s->mString.size()));
    call.words.push_back(s->mRefCount);
}

uint32_t DumpNodes(drawfx::Call &call, const uint8_t *node, const uint8_t *parent, TableKind kind)
{
    if (At<uint8_t>(node, 0xd)) return 0;
    const uint32_t left = DumpNodes(call, Bytes(At<uint32_t>(node, 0)), node, kind);
    call.words.push_back(At<uint8_t>(node, 0xc));
    call.words.push_back(Bytes(At<uint32_t>(node, 4)) == parent);
    switch (kind) {
    case TableKind::NameNumber:
        DumpName(call, node + 0x10);
        call.words.push_back(At<uint32_t>(node, 0x30));
        break;
    case TableKind::NumberName:
        call.words.push_back(At<uint32_t>(node, 0x10));
        DumpName(call, node + 0x14);
        break;
    case TableKind::NameResource:
        DumpName(call, node + 0x10);
        call.words.push_back(At<uint32_t>(node, 0x30));
        call.words.push_back(At<uint32_t>(node, 0x34));
        break;
    }
    return left + 1 + DumpNodes(call, Bytes(At<uint32_t>(node, 8)), node, kind);
}

uint32_t TreeSetProperty(drawfx::Fake &f, const uint32_t *a)
{
    TableKind kind;
    if (!TableKindOf(a[0], &kind)) return SetProperty(f, a);
    drawfx::Call &call = drawfx::gLog.back();
    call.words[1] = a[1] != 0;
    if (a[1]) {
        const uint8_t *v = Bytes(a[1]);
        call.words.push_back(At<uint16_t>(v, 0xe));
        call.words.push_back(At<uint16_t>(v, 0xc));
        call.words.push_back(At<uint32_t>(v, 0x10));
        const uint8_t *map = Bytes(At<uint32_t>(v, 4));
        const uint8_t *head = Bytes(At<uint32_t>(map, 0));
        call.words.push_back(At<uint32_t>(map, 4));
        call.words.push_back(At<uint8_t>(head, 0xd));
        const uint32_t count = DumpNodes(call, Bytes(At<uint32_t>(head, 4)), head, kind);
        call.words.push_back(count);
        auto *value = reinterpret_cast<cIGZVariant *>(uintptr_t(a[1]));
        value->AddRef();
        gProps.push_back(value);
    }
    return 1;
}

void SetupTree(uint32_t seed)
{
    gFace.value[0] = 0;
    gFace.value[1] = seed;
    gFace.value[2] = 0;
    gFace.On(20, &TreeAddImage);
    gFace.On(24, &TreeAddNode);
    gDefaults.On(5, &TreeGetFont);
    gDefaults.On(38, &TreeFindFontId);
    gWorld[1].f.On(127, &TreeSetProperty);
}

const char *const kTreeNodeNames[] = {"n1", "n2", "n3", "n4", "n5", "n6", "dup", "dup", "Root", ""};
const char *const kTreeParents[] = {"Root", "Root", "n1", "n1", "n2", "n3", "dup", "zz", "n4", "Root", ""};
const char *const kTreeCaptions[] = {"Node", "", "two words", "x y z"};
const char *const kTreeImageNames[] = {"a", "b", "c", "dup", "", "img", "zz"};
const char *const kTreeWheres[] = {"first", "last", "sorted", "", "x", "last", "first"};
const char *const kTreeWhole[] = {"first", "last", "sorted"};
const char *const kTreeYesNo[] = {"yes", "no", "", "YES", "yes"};
const char *const kTreeSpecs[] = {"{1a,2b}", "{0,0}", "{ff}", "{zz}", "{12,34}", "{ffffffff,7}", "{1,2}"};
const char *const kTreeRects[] = {"(1,2,3,4)", "(0,0,0,0)", "(5,6,7)", "x", "(-1,-2,3,4)", "", "(9,9,9,9)", "(0,0,0,5)", "(0,0,5,0)",
                                    "(0,5,0,0)", "(5,0,0,0)"};

// "name:{group,instance}" with, mostly, ":(left,top,right,bottom)".
std::string TreeImageText()
{
    std::string text = std::string(Pick(kTreeImageNames)) + ":" + Pick(kTreeSpecs);
    if (Draw(4)) text += std::string(":") + Pick(kTreeRects);
    return text;
}

// A node's fifteen pieces; `whole` makes the position word one of the three the original understands.
std::string TreeNodeText(bool whole)
{
    std::string pieces[15];
    pieces[0] = Pick(kTreeNodeNames);
    pieces[1] = Pick(kTreeCaptions);
    pieces[2] = Pick(kTreeParents);
    pieces[3] = whole ? Pick(kTreeWhole) : Pick(kTreeWheres);
    for (int i = 4; i < 8; ++i) pieces[i] = Pick(kTreeImageNames);
    for (int i = 8; i < 12; ++i) pieces[i] = Pick(kColorTexts);
    pieces[12] = Pick(kFontNames);
    pieces[13] = Pick(kTreeYesNo);
    pieces[14] = Pick(kTreeYesNo);
    const uint32_t count = whole || Draw(3) ? 15 : 3 + Draw(12);
    std::string text;
    for (uint32_t i = 0; i < count; ++i) text += (i ? ":" : "") + pieces[i];
    return text;
}

cIGZUnknown *TreeValue(uint32_t id)
{
    switch (id) {
    case 0xf016: return TextValue(TreeImageText().c_str());
    case 0x1102: return TextValue(TreeNodeText(false).c_str());
    case 0x1407: return TextValue(Pick(kTreeCaptions));
    case 0x1408: case 0x1409: case 0x140a: case 0x140b: return TextValue(Pick(kTreeImageNames));
    case 0x140c: case 0x140d: case 0x140e: case 0x140f: case 0x133f: case 0x408: return TextValue(Pick(kWholeColorTexts));
    case 0x1410:
        if (Draw(2)) return Keep(new cRZVariant(kFontNumbers[Draw(sizeof(kFontNumbers) / 4)]));
        return TextValue(Pick(kFontNames));
    case 0x1411: case 0x1412: return Keep(new cRZVariant(uint32_t(Draw(3))));
    default: return Keep(new cRZVariant(kNumbers[Draw(sizeof(kNumbers) / 4)]));
    }
}

// The original reads the position of the first node from a stack slot nothing has set when its word is none of the three:
// the first node text of a list always gets a whole one.
void FixupTree(cGZScriptAttributes *list)
{
    for (uint32_t i = 0; i < list->Size(); ++i) {
        cGZScriptAttribute &a = list->mBegin[i];
        if (a.mId != 0x1102) continue;
        auto *item = reinterpret_cast<cIGZVariant *>(a.mValue);
        if (!item || item->GetType() != 0x800e) continue;
        a.mValue = TextValue(TreeNodeText(true).c_str());
        break;
    }
}

DIFF_TEST(LegacyHandlers_TreeView, 0x86c19e)
{
    // Also reached: the attribute readers 0x86401e, 0x863fae, 0x863e06, the window property setters 0x86df41, 0x86df0c,
    // the image loader 0x86dc44, the services 0x9c5a05, 0x9c5baa, and the standard-library code the maps and lists
    // instantiate (run for real on the original's side): 0x85a1eb, 0x85d423, 0x85d403, 0x85d581, 0x85d2c6, 0x85d280,
    // 0x85d25d, 0x85d2a3, 0x85cd2a, 0x85d011, 0x85d4e8, 0x86e35d, 0x86e2df, 0x70cb6a, 0x612f59.
    // Not compared: the tables' addresses (they differ; the raw trees are), and the result.
    // Left out of the pools: a null node or image text (the original calls GetType on it), and a first node whose position
    // word is none of first / last / sorted (the original keeps a stack slot's old content; the later nodes keep the
    // previous node's, which both sides do alike).
    const uint32_t ids[] = {0x1401, 0x1402, 0x1403, 0x1404, 0x1405, 0x1336, 0x1406, 0x133f, 0x408,  0x1407, 0x1408, 0x1409,
                            0x140a, 0x140b, 0x140c, 0x140d, 0x140e, 0x140f, 0x1410, 0x1411, 0x1412, 0xf016, 0xf016, 0xf016,
                            0xf016, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x1102, 0x7};
    RunHandler<kTreeFace>(ctx, {"LegacyHandlers_TreeView", 0x86c19e, &ApplyTreeViewProperties, cIGZWinTreeView::kIID, ids,
                                sizeof(ids) / 4, 40, &TreeValue, 6000, 0, 0xf016, {0x1102}, &SetupTree, &FixupTree, {0x1102}});
}

// ---- the grid's cell parser (ParseGridCell 0x863ba3) ---------------------------------------------------------------------

const char *const kCellNumbers[] = {"0", "3", "0x10", "12", "x", "", "-1", "7", "255", "0x7fffffff", "4294967295", " 9"};
const char *const kCellFlagTexts[] = {"40000", "1000", "41000", "0", "2000", "ffffffff", "0x1000", "zz", "40001", "11000",
                                      "0x40000", "", "1000 ", "100000", "41"};
const char *const kCellPieces[] = {"00000001", "1a", "ffffffff", "\"quoted, with comma\"", "label with space", "", "5",
                                   "  padded  ", "a\"b", "Arial", "two words", "main9", "7", "0x20", "99999999999", "-3"};

std::string CellText()
{
    std::string pieces[14];
    pieces[0] = Pick(kCellNumbers);
    pieces[1] = Pick(kCellNumbers);
    pieces[2] = Pick(kCellFlagTexts);
    for (int i = 3; i < 14; ++i) pieces[i] = Pick(kCellPieces);
    const uint32_t count = Draw(5) ? 4 + Draw(9) : Draw(4);
    std::string text = Draw(6) ? "" : "  ";
    for (uint32_t i = 0; i < count; ++i) text += (i ? (Draw(4) ? "," : " , ") : "") + pieces[i];
    if (Draw(8) == 0) text += ",";
    return text;
}

DIFF_TEST(LegacyHandlers_ParseGridCell, 0x863ba3)
{
    // Also reached: the text splitter 0x1b1557, the number parsers 0x1b10f3 and 0x1b0e2c, the string assignment 0x14205a,
    // cRZString's constructor 0x144f5b and Sprintf, the UI defaults service 0x9c5baa (its FindFontId is a recording fake
    // answering from the name's hash). Compared: the answer, every out the original writes (the others keep their
    // sentinel), the resource string (text, count, vtable) and the fake's log of font names asked for.
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    globals.Set(kDefaultsCell, &gDefaults);
    using ParseFn = bool(__cdecl *)(cRZString *, uint32_t *, uint32_t *, uint32_t *, cRZString *, uint32_t *, uint32_t *,
                                    uint32_t *, uint32_t *, uint32_t *, uint32_t *);
    for (uint32_t seed = 1; seed <= 8000; ++seed) {
        gRng.seed(seed);
        const std::string cell = CellText();
        const char *const old = Pick(kCellPieces);
        Blob results[2];
        drawfx::Log logs[2];
        for (int side = 0; side < 2; ++side) {
            drawfx::ResetAll();
            gDefaults.Reset<kDefaultsFace>(560);
            gDefaults.On(38, &TreeFindFontId);
            LegacyText text, resource;
            text.mText.FromChar(cell.c_str());
            resource.mText.FromChar(old);
            uint32_t outs[9];
            for (uint32_t i = 0; i < 9; ++i) outs[i] = 0xdead0000u + i;
            cRZString *const r = &resource.mText;
            const bool ok = side ? ParseGridCell(&text.mText, &outs[0], &outs[1], &outs[2], r, &outs[3], &outs[4], &outs[5],
                                                 &outs[6], &outs[7], &outs[8])
                                 : ctx.Original<ParseFn>(0x863ba3)(&text.mText, &outs[0], &outs[1], &outs[2], r, &outs[3],
                                                                   &outs[4], &outs[5], &outs[6], &outs[7], &outs[8]);
            Blob &b = results[side];
            b.push_back(ok);
            b.insert(b.end(), outs, outs + 9);
            b.push_back(HashText(resource.Chars()));
            b.push_back(uint32_t(resource.mText.mString.size()));
            b.push_back(resource.mText.mRefCount);
            b.push_back(At<uint32_t>(reinterpret_cast<const uint8_t *>(r), 0) ==
                        uint32_t(uintptr_t(GameAddress<void>(cRZString::kVTable))));
            b.push_back(HashText(text.Chars()));
            logs[side] = drawfx::gLog;
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyHandlers_ParseGridCell", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyHandlers_ParseGridCell log", seed)) break;
    }
}
