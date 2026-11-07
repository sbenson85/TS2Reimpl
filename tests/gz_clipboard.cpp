// Differential tests for the clipboard object (src/gz/cGZClipboardManagerClipboard.size.cpp, standalone shape):
// cGZClipboardManagerClipboard, its system clipboard viewer, the clipboard data constructor 0xc6d2a5 and the message
// server cache 0x9c5ab4 / 0x762db4.
//
// The original side builds its clipboard with the original constructor 0xc6d26f on the game allocator's memory, ours with
// ours; both run the same script through their own vtables, called through our cIGZClipboard (whose slots are the
// game's: GZClipboard_VTables). The viewer's window procedure is the one each side registers (RegisterClassA's
// procedure), called with the window the fake CreateWindowExA hands out.
//
// The world, built afresh for each side: a fake framework in 0x10c9378 whose GetSystemService hands out a fake message
// server (or nothing), the message server cell 0x10f5198 (empty, or the fake already), the system code page cell, fake
// clipboard data (format and object from the configuration), fake strings (the objects that answer cIGZString), and
// impfx fakes for every Win32 call: a system clipboard whose blocks are test-owned (a configuration the script changes the
// same way on both sides), blocks GlobalAlloc hands out per side, and a window whose user data the fakes keep.
//
// Text conversion: code page 1252's non-ASCII text goes through MultiByteToWideChar / WideCharToMultiByte, which the
// original calls through its imports and ours directly: the import fakes run the real conversion and take their call
// out of the log, so both sides log the same. Code pages 0 and 8 copy.
#include "game/vtable.h"
#include "gz/cGZClipboardManagerClipboard.h"
#include "ifaces/cIGZClipboardData.h"
#include "misc/ClassesRound4.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZString.h"

#include "difftest.h"
#include "drawfx_core.h"
#include "free_recorder.h"
#include "fx_slots.h"
#include "impfx.h"
#include "ts_services_fixture.h"

#undef GetObject
#undef SendMessage
#undef RegisterClass

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kCodePageCell = 0xff23a4;
constexpr uint32_t kClipTag = 0x7e000000;
constexpr uint32_t kViewerTag = 0x7d000000;
constexpr uint32_t kDataTag = 0x7c000000;
constexpr uint32_t kProcMarker = 0x7b000000;
constexpr uint32_t kRealData = 0x6000000;   // Ident of a real clipboard data: a hash of its format and object
constexpr uint32_t kRealString = 0x5000000; // Ident of a real string: a hash of its text
constexpr uint32_t kText = cGZClipboardManagerClipboard::kTextFormat;
constexpr uint32_t kDataIid = 0x7ec0006;
constexpr uint32_t kStringIid = 0x89b7dc8;

// Handles the fakes hand out: above the user address range, so normalisation leaves them alone.
constexpr uint32_t kWindow = 0xc8a00004;
constexpr uint32_t kNextViewer = 0xc8b00008;
constexpr uint32_t kOtherViewer = 0xc8c0000c;
constexpr uint32_t kInstance = 0xc9000000;
constexpr uint32_t kWideBlock = 0xc8000010;
constexpr uint32_t kAnsiBlock = 0xc8000020;
constexpr uint32_t kAllocBase = 0xc8100000;

using GameNew = void *(__cdecl *)(size_t);
using PlainCtor = void *(__thiscall *)(void *);
using OwnerCtor = void *(__thiscall *)(void *, void *);
using DataCtor = void *(__thiscall *)(void *, uint32_t, void *);
using ReadFn = bool(__thiscall *)(void *, cIGZString *);
using WriteFn = bool(__thiscall *)(void *, cIGZString *);
using DetachFn = void(__thiscall *)(void *);
using DeletingFn = void *(__thiscall *)(void *, uint32_t);
using Proc = intptr_t(__stdcall *)(void *, uint32_t, uintptr_t, intptr_t);

// ---- fakes ----
inline constexpr drawfx::Slot kFrameworkSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},
    {3, drawfx::Ret::Int, true, "GetSystemService", "uui"},
};
inline constexpr drawfx::Iface kFramework{"cIGZFrameWork", 6, kFrameworkSlots};

// The message is on the caller's stack: its words are noted by the handler, normalised.
inline constexpr drawfx::Slot kServerSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {1, drawfx::Ret::Int, true, "MessageSend", "W0"},
    {2, drawfx::Ret::Int, true, "MessagePost", "W0b"},
};
inline constexpr drawfx::Iface kServer{"cIGZMessageServer", 5, kServerSlots};

inline constexpr drawfx::Slot kDataSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, true, "GetFormat", ""},
    {2, drawfx::Ret::Int, true, "QueryData", "ui"},
    {0, drawfx::Ret::Int, true, "GetObject", ""},
};
inline constexpr drawfx::Iface kData{"cIGZClipboardData", 6, kDataSlots};

inline constexpr drawfx::Slot kUnknownSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
};
inline constexpr drawfx::Iface kUnknown{"cIGZUnknown", 3, kUnknownSlots};

constexpr drawfx::Slot Unsettled() { return {0, drawfx::Ret::Int, false, "", ""}; }
// A viewer's owner (cIGZClipboard): only SetText (slot 19) is reached.
inline constexpr drawfx::Slot kOwnerSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(),
    Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(),
    {2, drawfx::Ret::Int, true, "SetText", "Sb"},
};
inline constexpr drawfx::Iface kOwner{"cIGZClipboard", 20, kOwnerSlots};

drawfx::Fake gFramework;
drawfx::Fake gServer;
constexpr unsigned kDatas = 4;
drawfx::Fake gData[kDatas];
constexpr unsigned kStrings = 2;
drawfx::Fake gStrings[kStrings];
drawfx::Fake gPlain; // an object that is not a string
drawfx::Fake gOwner;

// The texts: UTF-8 for strings, ANSI and UTF-16 for the system clipboard's blocks (each ends in a NUL inside its
// array; the copy runs to the NUL whatever the block's size). Indexes 2 and 4 are non-ASCII; 4 is dense (three UTF-8
// bytes per unit).
const char *const kUtf8Texts[] = {"hello", "", "caf\xc3\xa9 \xe4\xb8\xad", "a\r\nb\tc", "\xe4\xb8\xad\xe6\x96\x87\xc3\xa9"};
const char *const kAnsiTexts[] = {"plain", "", "ab\xe9\xff!", "x\r\ny", "\xc0\xe9\xf1\xfc"};
const char16_t kWideMixed[] = {0x63, 0x61, 0x66, 0xe9, 0x20, 0x4e2d, 0xd83d, 0xde00, 0};
const char16_t kWideDense[] = {0x4e2d, 0x6587, 0x5b57, 0x8a9e, 0};
const char16_t *const kWideTexts[] = {u"hi there", u"", kWideMixed, u"a\r\nb", kWideDense};
constexpr unsigned kTextCount = 5;

// ---- the configuration ----
struct Sys {
    uint32_t open;       // OpenClipboard's answer
    uint32_t wideAvail;  // IsClipboardFormatAvailable(CF_UNICODETEXT)
    uint32_t ansiAvail;  // and CF_TEXT
    uint32_t wideData;   // GetClipboardData's handle (0: none)
    uint32_t ansiData;
    uint32_t wideText;   // the blocks' texts
    uint32_t ansiText;
    uint32_t wideSize;   // GlobalSize's answers
    uint32_t ansiSize;
    uint32_t lockFails;  // GlobalLock of a read block answers null
    uint32_t setResult;  // SetClipboardData answers the handle
    uint32_t window;     // CreateWindowExA's answer
    uint32_t next;       // SetClipboardViewer's
};

struct World {
    uint32_t framework;
    uint32_t serviceAnswers;
    uint32_t serverCell;
    uint32_t codePage;
    uint32_t formats[kDatas];
    uint32_t objects[kDatas]; // 0..1 a string, 2 the plain object, 3 none
    uint32_t stringTexts[kStrings];
    Sys sys;
};
World gWorld;

// Per side.
Proc gProc;
uint32_t gUserData;
std::vector<std::vector<uint8_t>> gBlocks;
alignas(4) uint8_t gWideBlock[64];
alignas(4) uint8_t gAnsiBlock[64];

uint32_t PickText(std::mt19937 &rng, uint32_t) { return rng() % kTextCount; }

Sys MakeSys(std::mt19937 &rng, uint32_t codePage)
{
    Sys s{};
    s.open = rng() % 6 != 0;
    s.wideAvail = rng() % 2;
    s.ansiAvail = rng() % 3 != 0;
    s.wideData = rng() % 5 ? kWideBlock : 0;
    s.ansiData = rng() % 5 ? kAnsiBlock : 0;
    s.wideText = rng() % kTextCount;
    s.ansiText = PickText(rng, codePage);
    const uint32_t wideSizes[] = {0, 1, 2, 6, 40, 200};
    s.wideSize = wideSizes[rng() % 6];
    s.lockFails = rng() % 7 == 0;
    // A failed lock converts the block's size from the original's empty 16-byte buffer (ORIGINAL-BUG in ReadText).
    const uint32_t ansiSizes[] = {0, 1, 5, 16, 40, 200};
    s.ansiSize = ansiSizes[rng() % (s.lockFails ? 4 : 6)];
    s.setResult = rng() % 4 != 0;
    s.window = rng() % 8 ? kWindow : 0;
    s.next = rng() % 3 ? kNextViewer : 0;
    return s;
}

World MakeWorld(std::mt19937 &rng)
{
    World w{};
    w.framework = rng() % 8 != 0;
    w.serviceAnswers = rng() % 6 != 0;
    w.serverCell = rng() % 3 == 0;
    const uint32_t pages[] = {0, 8, 1252};
    w.codePage = pages[rng() % 3];
    const uint32_t formats[] = {kText, kText, 1, 2, 0, 0x55};
    for (unsigned i = 0; i < kDatas; ++i) {
        w.formats[i] = formats[rng() % 6];
        const uint32_t objects[] = {0, 1, 0, 1, 2, 3};
        w.objects[i] = objects[rng() % 6];
    }
    for (unsigned i = 0; i < kStrings; ++i) w.stringTexts[i] = PickText(rng, w.codePage);
    w.sys = MakeSys(rng, w.codePage);
    return w;
}

template <class T> T *At(uint32_t word) { return reinterpret_cast<T *>(uintptr_t(word)); }

uint32_t Hash(const void *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(p)[i]) * 16777619u;
    return h;
}

// ---- handlers ----
uint32_t OnGetSystemService(drawfx::Fake &, const uint32_t *args)
{
    if (!gWorld.serviceAnswers) return 0;
    ++gServer.refs;
    *At<void *>(args[2]) = &gServer;
    return 1;
}
void NoteMessage(const char *label, uint32_t at)
{
    const uint32_t *m = At<uint32_t>(at);
    drawfx::Note(label, {m[0], drawfx::Norm(m[1]), m[2], m[3]});
}
uint32_t OnSend(drawfx::Fake &, const uint32_t *args)
{
    NoteMessage("sent", args[0]);
    return 1;
}
uint32_t OnPost(drawfx::Fake &, const uint32_t *args)
{
    NoteMessage("posted", args[0]);
    return 1;
}
uint32_t OnFormat(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t OnObject(drawfx::Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t OnStringData(drawfx::Fake &self, const uint32_t *) { return drawfx::P(kUtf8Texts[self.value[0]]); }
uint32_t OnStringLength(drawfx::Fake &self, const uint32_t *) { return uint32_t(std::strlen(kUtf8Texts[self.value[0]])); }
uint32_t OnOwnerSetText(drawfx::Fake &, const uint32_t *) { return 1; }

uint32_t OnOpenClipboard(drawfx::Fake &, const uint32_t *) { return gWorld.sys.open; }
uint32_t OnAvailable(drawfx::Fake &, const uint32_t *args)
{
    if (args[0] == 13) return gWorld.sys.wideAvail;
    if (args[0] == 1) return gWorld.sys.ansiAvail;
    return 0;
}
uint32_t OnGetClipboardData(drawfx::Fake &, const uint32_t *args)
{
    if (args[0] == 13) return gWorld.sys.wideData;
    if (args[0] == 1) return gWorld.sys.ansiData;
    return 0;
}
std::vector<uint8_t> *Allocated(uint32_t handle)
{
    const uint32_t i = (handle - kAllocBase) / 0x10;
    return handle >= kAllocBase && i < gBlocks.size() ? &gBlocks[i] : nullptr;
}
uint32_t OnGlobalSize(drawfx::Fake &, const uint32_t *args)
{
    if (args[0] == kWideBlock) return gWorld.sys.wideSize;
    if (args[0] == kAnsiBlock) return gWorld.sys.ansiSize;
    if (std::vector<uint8_t> *b = Allocated(args[0])) return uint32_t(b->size());
    return 0;
}
uint32_t OnGlobalLock(drawfx::Fake &, const uint32_t *args)
{
    if (args[0] == kWideBlock || args[0] == kAnsiBlock) {
        if (gWorld.sys.lockFails) return 0;
        return drawfx::P(args[0] == kWideBlock ? gWideBlock : gAnsiBlock);
    }
    if (std::vector<uint8_t> *b = Allocated(args[0])) return drawfx::P(b->data());
    return 0;
}
uint32_t OnGlobalAlloc(drawfx::Fake &, const uint32_t *args)
{
    gBlocks.emplace_back(args[1], uint8_t(0xcd));
    return kAllocBase + uint32_t(gBlocks.size() - 1) * 0x10;
}
// The block's bytes as handed to the system.
uint32_t OnSetClipboardData(drawfx::Fake &, const uint32_t *args)
{
    if (std::vector<uint8_t> *b = Allocated(args[1])) {
        drawfx::Note("block", {uint32_t(b->size()), Hash(b->data(), b->size())});
        for (size_t i = 0; i < b->size() && i < 64; i += 4) {
            uint32_t w = 0;
            std::memcpy(&w, b->data() + i, b->size() - i < 4 ? b->size() - i : 4);
            drawfx::Note("block word", w);
        }
    }
    return gWorld.sys.setResult ? args[1] : 0;
}
uint32_t OnRegisterClass(drawfx::Fake &, const uint32_t *args)
{
    gProc = reinterpret_cast<Proc>(At<uint32_t>(args[0])[1]);
    drawfx::gLog.back().words[2] = kProcMarker; // [presence, style, procedure, ...]
    return 0xc0de;
}
uint32_t OnCreateWindow(drawfx::Fake &, const uint32_t *) { return gWorld.sys.window; }
uint32_t OnSetClipboardViewer(drawfx::Fake &, const uint32_t *) { return gWorld.sys.next; }
uint32_t OnSetWindowLong(drawfx::Fake &, const uint32_t *args)
{
    gUserData = args[2];
    if (args[2]) {
        drawfx::AddRange(At<void>(args[2]), sizeof(cGZClipboardViewer), kViewerTag);
        drawfx::gLog.back().words[2] = kViewerTag;
    }
    return 0;
}
uint32_t OnGetWindowLong(drawfx::Fake &, const uint32_t *args) { return args[0] == kWindow ? gUserData : 0; }
// The code page conversions: the real ones, unlogged (ours calls Windows directly, the original through its imports).
uint32_t OnMultiByteToWideChar(drawfx::Fake &, const uint32_t *args)
{
    drawfx::gLog.pop_back();
    return uint32_t(::MultiByteToWideChar(args[0], args[1], At<const char>(args[2]), int(args[3]), At<wchar_t>(args[4]),
                                          int(args[5])));
}
uint32_t OnWideCharToMultiByte(drawfx::Fake &, const uint32_t *args)
{
    drawfx::gLog.pop_back();
    return uint32_t(::WideCharToMultiByte(args[0], args[1], At<const wchar_t>(args[2]), int(args[3]), At<char>(args[4]),
                                          int(args[5]), At<const char>(args[6]), At<BOOL>(args[7])));
}
uint32_t OnDestroyWindow(drawfx::Fake &, const uint32_t *args)
{
    if (args[0] == kWindow) gUserData = 0; // the window is gone with its user data
    return 1;
}

void AddImports(impfx::Imports &imp)
{
    IMPFX_ADD(imp, OpenClipboard, "u").On(OnOpenClipboard);
    IMPFX_ADD(imp, CloseClipboard).Returns(1);
    IMPFX_ADD(imp, EmptyClipboard).Returns(1);
    IMPFX_ADD(imp, IsClipboardFormatAvailable, "u").On(OnAvailable);
    IMPFX_ADD(imp, GetClipboardData, "u").On(OnGetClipboardData);
    IMPFX_ADD(imp, SetClipboardData, "uu").On(OnSetClipboardData);
    IMPFX_ADD(imp, GlobalAlloc, "uu").On(OnGlobalAlloc);
    IMPFX_ADD(imp, GlobalSize, "u").On(OnGlobalSize);
    IMPFX_ADD(imp, GlobalLock, "u").On(OnGlobalLock);
    IMPFX_ADD(imp, GlobalUnlock, "u").Returns(0);
    IMPFX_ADD(imp, GlobalFree, "u").Returns(0);
    IMPFX_ADD(imp, ChangeClipboardChain, "uu").Returns(1);
    IMPFX_ADD(imp, SetClipboardViewer, "u").On(OnSetClipboardViewer);
    IMPFX_ADD(imp, DestroyWindow, "u").On(OnDestroyWindow);
    IMPFX_ADD(imp, GetVersion).Returns(0x0a280105);
    IMPFX_ADD(imp, GetModuleHandleA, "u").Returns(kInstance);
    IMPFX_ADD(imp, RegisterClassA, "W10").On(OnRegisterClass);
    IMPFX_ADD(imp, CreateWindowExA, "ussuuuuuuuuu").On(OnCreateWindow);
    IMPFX_ADD(imp, SetWindowLongA, "uuu").On(OnSetWindowLong);
    IMPFX_ADD(imp, GetWindowLongA, "uu").On(OnGetWindowLong);
    IMPFX_ADD(imp, SendMessageA, "uuuu").Returns(0);
    IMPFX_ADD(imp, DefWindowProcA, "uuuu").Returns(0x5a5a);
    IMPFX_ADD(imp, MultiByteToWideChar, "uuuuuu").On(OnMultiByteToWideChar);
    IMPFX_ADD(imp, WideCharToMultiByte, "uuuuuuuu").On(OnWideCharToMultiByte);
}

// The system clipboard's blocks, from the configuration.
void FillBlocks()
{
    std::memset(gWideBlock, 0x41, sizeof(gWideBlock));
    std::memset(gAnsiBlock, 0x42, sizeof(gAnsiBlock));
    const char16_t *wide = kWideTexts[gWorld.sys.wideText];
    size_t units = 0;
    while (wide[units]) ++units;
    std::memcpy(gWideBlock, wide, (units + 1) * 2);
    const char *ansi = kAnsiTexts[gWorld.sys.ansiText];
    std::memcpy(gAnsiBlock, ansi, std::strlen(ansi) + 1);
}

// The fakes and cells for one side, with the configuration as it is.
void BuildWorld(impfx::Imports &imp, drawfx::Cells &cells)
{
    imp.BeginSide();
    gProc = nullptr;
    gUserData = 0;
    gBlocks.clear();
    gBlocks.reserve(64);
    FillBlocks();
    gFramework.Reset<kFramework>(0x910);
    gFramework.On(5, OnGetSystemService);
    gServer.Reset<kServer>(0x920);
    gServer.On(3, OnSend);
    gServer.On(4, OnPost);
    for (unsigned i = 0; i < kStrings; ++i) {
        drawfx::Fake &s = gStrings[i];
        s.Reset<fx_slots::cIGZString>(0x960 + i);
        s.value[0] = gWorld.stringTexts[i];
        s.AddFace(kStringIid, s);
        s.On(5, OnStringData);
        s.On(6, OnStringData);
        s.On(7, OnStringLength);
    }
    gPlain.Reset<kUnknown>(0x970);
    for (unsigned i = 0; i < kDatas; ++i) {
        drawfx::Fake &d = gData[i];
        d.Reset<kData>(0x930 + i);
        d.value[0] = gWorld.formats[i];
        const uint32_t o = gWorld.objects[i];
        d.value[1] = o < kStrings ? drawfx::P(&gStrings[o]) : o == 2 ? drawfx::P(&gPlain) : 0;
        d.AddFace(kDataIid, d);
        d.On(3, OnFormat);
        d.On(5, OnObject);
    }
    gOwner.Reset<kOwner>(0x980);
    gOwner.On(19, OnOwnerSetText);
    cells.Set(kFrameworkCell, gWorld.framework ? &gFramework : nullptr);
    cells.Set(kMessageServerCell, gWorld.serverCell ? &gServer : nullptr);
    cells.Set(kCodePageCell, gWorld.codePage);
}

void NoteRefs()
{
    drawfx::Note("refs", {uint32_t(gFramework.refs), uint32_t(gServer.refs), uint32_t(gPlain.refs)});
    for (unsigned i = 0; i < kDatas; ++i) drawfx::Note("data refs", uint32_t(gData[i].refs));
    for (unsigned i = 0; i < kStrings; ++i) drawfx::Note("string refs", uint32_t(gStrings[i].refs));
}

// A game cRZString built and destroyed by hand.
struct Str {
    union {
        cRZString s;
    };
    explicit Str(const char *text) { s.Construct(text); }
    ~Str() { s.Destruct(); }
    Str(const Str &) = delete;
    Str &operator=(const Str &) = delete;
    cIGZString *Get() { return s.AsInterface(); }
};

uint32_t TextIdent(const cRZString *s) { return kRealString | (Hash(s->ToChar(), s->Strlen()) & 0xfffff); }

// What a pointer the clipboard hands back is: a fake's id, or a real clipboard data's format and object (the sides
// allocate differently).
uint32_t Ident(const void *p)
{
    if (!p) return 0;
    for (const drawfx::Fake *f : {&gData[0], &gData[1], &gData[2], &gData[3], &gStrings[0], &gStrings[1], &gPlain})
        if (p == f) return f->id;
    const auto *data = static_cast<const cGZClipboardManagerClipboardData *>(p);
    if (data->mVTable != GameAddress<void>(cGZClipboardManagerClipboardData::kVTable)) return 0xbadbad;
    uint32_t object = drawfx::P(data->mData);
    for (const drawfx::Fake *f : {&gStrings[0], &gStrings[1], &gPlain})
        if (data->mData == f) object = f->id;
    if (object == drawfx::P(data->mData) && data->mData) object = TextIdent(static_cast<const cRZString *>(data->mData));
    uint32_t h = 2166136261u;
    for (const uint32_t w : {data->mUnknown4, data->mUnknown8, object}) h = (h ^ w) * 16777619u;
    return kRealData | (h & 0xfffff);
}

void NoteViewer(const uint8_t *raw)
{
    auto word = [&](unsigned at) { return *reinterpret_cast<const uint32_t *>(raw + at); };
    drawfx::Note("viewer", {raw[0], word(4), word(8), drawfx::Norm(word(0xc)), word(0x10), word(0x14), word(0x18)});
}

// Every field of the clipboard (the flag bytes alone: their padding is never written; the map through its size and
// the slots; the lock not at all: its count returns to where it was).
void NoteClip(const char *label, cIGZClipboard *clip)
{
    const auto *raw = reinterpret_cast<const uint8_t *>(clip);
    auto word = [&](unsigned at) { return *reinterpret_cast<const uint32_t *>(raw + at); };
    drawfx::Note(label, {raw[4], raw[5], word(8), word(0xc), word(0x10), word(0x14), word(0x18) != 0, word(0x20)});
    if (word(0x18)) NoteViewer(At<const uint8_t>(word(0x18)));
}

void NoteContents(cIGZClipboard *clip)
{
    uint32_t formats[8];
    uint32_t count = 8;
    const uint32_t n = clip->GetFormats(formats, &count);
    drawfx::Note("contents", {n, count});
    for (uint32_t i = 0; i < count && i < 8; ++i) drawfx::Note("format", {formats[i], Ident(clip->GetData(formats[i]))});
}

void NoteString(const char *label, cRZString &s) { drawfx::Note(label, {s.Strlen(), TextIdent(&s)}); }

// A window message through the procedure the side registered.
void SendWindowMessage(std::mt19937 &rng)
{
    if (!gProc) {
        drawfx::Note("no procedure", 0);
        return;
    }
    const uint32_t messages[] = {0x308, 0x308, 0x30d, 0x30d, 0x10};
    const uint32_t message = messages[rng() % 5];
    const uint32_t windows[] = {kWindow, kWindow, kWindow, 0xc8d00010};
    const uint32_t window = windows[rng() % 4];
    const uint32_t wParams[] = {kNextViewer, kOtherViewer, 0, 7};
    const uint32_t wParam = wParams[rng() % 4];
    const uint32_t lParams[] = {kOtherViewer, 0, kNextViewer};
    const uint32_t lParam = lParams[rng() % 3];
    drawfx::Note("message", {message, window, wParam, lParam});
    drawfx::Note("answer", uint32_t(gProc(At<void>(window), message, wParam, intptr_t(lParam))));
}

const char *const kSetTexts[] = {"one", "", "caf\xc3\xa9", "zz\r\n"};

// One script on a clipboard.
void RunClipScript(cIGZClipboard *clip, std::mt19937 &rng, int steps)
{
    drawfx::Note("hold", clip->AddRef());
    const uint32_t ids[] = {0, 0, 1, 7, cGZClipboardManagerClipboard::kNoId};
    const uint32_t formats[] = {kText, 1, 2, 0, 0x55, 0xffffffff, 9};
    auto format = [&] { return formats[rng() % 7]; };
    for (int step = 0; step < steps; ++step) {
        switch (rng() % 23) {
        case 0:
        case 1: drawfx::Note("init", clip->Init() ? 1u : 0u); break;
        case 2: drawfx::Note("shutdown", clip->Shutdown() ? 1u : 0u); break;
        case 3: clip->SetId(ids[rng() % 5]); break;
        case 4: drawfx::Note("id", clip->GetId()); break;
        case 5: clip->SetNotify(rng() % 3 != 0); break;
        case 6: drawfx::Note("notify", clip->GetNotify() ? 1u : 0u); break;
        case 7: drawfx::Note("changes", clip->GetChangeCount()); break;
        case 8: drawfx::Note("has", clip->HasFormat(format()) ? 1u : 0u); break;
        case 9: {
            uint32_t out[6];
            for (uint32_t &w : out) w = 0xeeeeeeee;
            const uint32_t counts[] = {0, 1, 2, 6, 0x777};
            uint32_t count = counts[rng() % 5];
            const bool array = rng() % 3 != 0 && count <= 6;
            const uint32_t n = clip->GetFormats(array ? out : nullptr, &count);
            drawfx::Note("formats", {n, count});
            for (const uint32_t w : out) drawfx::Note("format word", w);
            break;
        }
        case 10: {
            const uint32_t iids[] = {1, kDataIid, kStringIid, 0x1234};
            void *out = At<void>(0xc7000000);
            const bool yes = clip->QueryData(format(), iids[rng() % 4], &out);
            drawfx::Note("query", {yes ? 1u : 0u, yes ? Ident(out) : drawfx::P(out)});
            if (yes) As<cIGZUnknown>(out)->Release();
            break;
        }
        case 11: drawfx::Note("get", Ident(clip->GetData(format()))); break;
        case 12:
        case 13: {
            const unsigned pick = rng() % (kDatas + 1);
            auto *data = pick < kDatas ? reinterpret_cast<ifaces::cIGZClipboardData *>(&gData[pick]) : nullptr;
            drawfx::Note("set", clip->SetData(data, rng() % 3 == 0) ? 1u : 0u);
            break;
        }
        case 14: clip->RemoveData(format()); break;
        case 15: drawfx::Note("slot 16", clip->Slot16(rng(), rng(), rng()) ? 1u : 0u); break;
        case 16: {
            Str out("old");
            drawfx::Note("get text", clip->GetText(out.Get()) ? 1u : 0u);
            NoteString("text", out.s);
            break;
        }
        case 17: drawfx::Note("slot 18", clip->Slot18(rng(), rng(), rng(), rng()) ? 1u : 0u); break;
        case 18: {
            Str text(kSetTexts[rng() % 4]);
            drawfx::Note("set text", clip->SetText(*text.Get(), rng() % 2 != 0) ? 1u : 0u);
            break;
        }
        case 19: gWorld.sys = MakeSys(rng, gWorld.codePage), FillBlocks(); break;
        case 20: {
            const uint32_t iids[] = {1, cIGZClipboard::kIid, kDataIid};
            void *out = At<void>(0xc7000000);
            const bool yes = clip->QueryInterface(iids[rng() % 3], &out);
            drawfx::Note("qi", {yes ? 1u : 0u, drawfx::Norm(drawfx::P(out))});
            if (yes) clip->Release();
            break;
        }
        default: SendWindowMessage(rng); break;
        }
        NoteClip("clip", clip);
    }
    NoteContents(clip);
}

void NoteFrees(difftest::FreeRecorder &rec)
{
    rec.Stop();
    size_t sizes[32];
    const size_t n = rec.SortedSizes(sizes, 32);
    drawfx::Note("frees", uint32_t(n));
    for (size_t i = 0; i < n && i < 32; ++i) drawfx::Note("freed", uint32_t(sizes[i]));
}

void *GameMemory(difftest::Context &ctx, size_t size) { return ctx.Original<GameNew>(0x824632)(size); }

cIGZClipboard *Build(difftest::Context &ctx, bool ours)
{
    cIGZClipboard *clip = ours ? static_cast<cIGZClipboard *>(new cGZClipboardManagerClipboard)
                               : static_cast<cIGZClipboard *>(
                                     ctx.Original<PlainCtor>(0xc6d26f)(GameMemory(ctx, cGZClipboardManagerClipboard::kSize)));
    drawfx::AddRange(clip, cGZClipboardManagerClipboard::kSize, kClipTag);
    return clip;
}

} // namespace

// The clipboard under a random script of every slot and the viewer's window messages, then its last Release. Reaches
// CreateViewer 0xc6dbc2 and DestroyViewer 0xc6e6d4 (Init, SetId, Shutdown), NotifyChanged 0xc6dc94 (the posted
// notices), NotifyState 0xc6e2e0 (the sent ones), the viewer's WindowProc 0xc6e73f (the window messages) and the
// destructor 0xc6d3c1 (the last Release).
DIFF_TEST(GZClipboard_Script, 0xc6e399)
{
    svcfx::AllocatorScope allocator;
    impfx::Imports imp(ctx);
    AddImports(imp);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        auto side = [&](bool ours) {
            drawfx::Cells cells;
            std::mt19937 rng(seed);
            gWorld = MakeWorld(rng);
            return drawfx::Side([&] { BuildWorld(imp, cells); }, [&] {
                cIGZClipboard *clip = Build(ctx, ours);
                NoteClip("built", clip);
                // Mostly clipboard 0 with notices, initialised early.
                if (rng() % 4) clip->SetId(0);
                if (rng() % 3) clip->SetNotify(true);
                if (rng() % 3) drawfx::Note("first init", clip->Init() ? 1u : 0u);
                RunClipScript(clip, rng, int(rng() % 30));
                if (rng() % 3 == 0) drawfx::Note("final shutdown", clip->Shutdown() ? 1u : 0u);
                difftest::FreeRecorder rec;
                drawfx::Note("last", clip->Release());
                NoteFrees(rec);
                NoteRefs();
                drawfx::Note("server cell", drawfx::Norm(*GameAddress<uint32_t>(kMessageServerCell)));
                drawfx::Note("user data", gUserData != 0);
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "clipboard", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

// The constructor (0xc6d26f against ours) and the deleting destructor (slot 20, then the destructor 0xc6d3c1) with and
// without data left in the map (the destructor does not release it).
DIFF_TEST(GZClipboard_Construct, 0xc6d26f)
{
    svcfx::AllocatorScope allocator;
    impfx::Imports imp(ctx);
    AddImports(imp);
    for (uint32_t seed = 0; seed < 40; ++seed) {
        auto side = [&](bool ours) {
            drawfx::Cells cells;
            std::mt19937 rng(seed);
            gWorld = MakeWorld(rng);
            return drawfx::Side([&] { BuildWorld(imp, cells); }, [&] {
                cIGZClipboard *clip = Build(ctx, ours);
                NoteClip("built", clip);
                const uint32_t datas = seed % 4;
                for (uint32_t i = 0; i < datas; ++i)
                    clip->SetData(reinterpret_cast<ifaces::cIGZClipboardData *>(&gData[i]), false);
                NoteClip("filled", clip);
                difftest::FreeRecorder rec;
                VirtualSlot<DeletingFn>(clip, 20)(clip, 1);
                NoteFrees(rec);
                NoteRefs();
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "construct", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

// The viewer alone (0xc6d2cf against ours) with a fake owner or none: reads and writes with every flag, window messages
// through the procedure each side registered (WindowProc 0xc6e73f), then Detach (0xc6d3f0).
DIFF_TEST(GZClipboard_Viewer, 0xc6dd9a)
{
    svcfx::AllocatorScope allocator;
    impfx::Imports imp(ctx);
    AddImports(imp);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        auto side = [&](bool ours) {
            drawfx::Cells cells;
            std::mt19937 rng(seed);
            gWorld = MakeWorld(rng);
            return drawfx::Side([&] { BuildWorld(imp, cells); }, [&] {
                void *owner = rng() % 4 ? &gOwner : nullptr;
                void *memory = ::operator new(sizeof(cGZClipboardViewer));
                std::memset(memory, 0xcd, sizeof(cGZClipboardViewer));
                drawfx::AddRange(memory, sizeof(cGZClipboardViewer), kViewerTag);
                if (ours)
                    new (memory) cGZClipboardViewer(static_cast<cGZClipboardManagerClipboard *>(owner));
                else
                    ctx.Original<OwnerCtor>(0xc6d2cf)(memory, owner);
                auto *raw = static_cast<uint8_t *>(memory);
                auto *viewer = static_cast<cGZClipboardViewer *>(memory);
                NoteViewer(raw);
                const int steps = int(rng() % 12);
                for (int step = 0; step < steps; ++step) {
                    switch (rng() % 6) {
                    case 0: {
                        Str out("old");
                        const bool read = ours ? viewer->ReadText(out.Get()) : ctx.Original<ReadFn>(0xc6dd9a)(memory, out.Get());
                        drawfx::Note("read", read ? 1u : 0u);
                        NoteString("read text", out.s);
                        break;
                    }
                    case 1: {
                        cIGZString *text = reinterpret_cast<cIGZString *>(&gStrings[rng() % kStrings]);
                        const bool written = ours ? viewer->WriteText(text) : ctx.Original<WriteFn>(0xc6e80f)(memory, text);
                        drawfx::Note("written", written ? 1u : 0u);
                        break;
                    }
                    case 2: {
                        // The flags: the enabled byte (its padding left alone) and the two counters.
                        const uint32_t flag = rng() % 3;
                        if (flag == 0) raw[0] = uint8_t(rng() % 2);
                        if (flag == 1) *reinterpret_cast<int32_t *>(raw + 4) = int32_t(rng() % 3) - (rng() % 4 == 0);
                        if (flag == 2) *reinterpret_cast<int32_t *>(raw + 8) = int32_t(rng() % 2);
                        break;
                    }
                    case 3: gWorld.sys = MakeSys(rng, gWorld.codePage), FillBlocks(); break;
                    default: SendWindowMessage(rng); break;
                    }
                    NoteViewer(raw);
                }
                if (ours)
                    viewer->Detach();
                else
                    ctx.Original<DetachFn>(0xc6d3f0)(memory);
                NoteViewer(raw);
                ::operator delete(memory);
                NoteRefs();
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "viewer", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

// The helpers: the clipboard data constructor (0xc6d2a5), the message server's acquisition (0x762db4) and its cache
// (0x9c5ab4) with and without a framework, an answering service and a cached value.
DIFF_TEST(GZClipboard_Helpers, 0x9c5ab4)
{
    impfx::Imports imp(ctx);
    using Acquire = ScopedRef *(__thiscall *)(ScopedRef *);
    using Get = void *(__cdecl *)();
    for (uint32_t mask = 0; mask < 16; ++mask) {
        auto side = [&](bool ours) {
            drawfx::Cells cells;
            std::mt19937 rng(mask);
            gWorld = MakeWorld(rng);
            gWorld.framework = mask & 1;
            gWorld.serviceAnswers = (mask >> 1) & 1;
            gWorld.serverCell = (mask >> 2) & 1;
            return drawfx::Side([&] { BuildWorld(imp, cells); }, [&] {
                if (mask & 8) {
                    ScopedRef ref{At<void>(0xc7000000)};
                    ScopedRef *r = ours ? cGZClipboardManagerClipboardAdapters::AcquireMessageServer(&ref, nullptr)
                                        : ctx.Original<Acquire>(0x762db4)(&ref);
                    drawfx::Note("acquire", {r == &ref ? 1u : 0u, drawfx::Norm(drawfx::P(ref.mObject))});
                } else {
                    void *g = ours ? static_cast<void *>(GetMessageServer()) : ctx.Original<Get>(0x9c5ab4)();
                    drawfx::Note("get", drawfx::Norm(drawfx::P(g)));
                    drawfx::Note("again", drawfx::Norm(drawfx::P(ours ? static_cast<void *>(GetMessageServer())
                                                                      : ctx.Original<Get>(0x9c5ab4)())));
                }
                drawfx::Note("cell", drawfx::Norm(*GameAddress<uint32_t>(kMessageServerCell)));
                NoteRefs();
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "server", mask, a.second.empty() ? b.second : a.second)) return;
    }
    for (uint32_t seed = 0; seed < 24; ++seed) {
        auto side = [&](bool ours) {
            drawfx::Cells cells;
            std::mt19937 rng(seed);
            gWorld = MakeWorld(rng);
            return drawfx::Side([&] { BuildWorld(imp, cells); }, [&] {
                alignas(4) uint8_t memory[sizeof(cGZClipboardManagerClipboardData) + 4];
                std::memset(memory, 0xcd, sizeof(memory));
                drawfx::AddRange(memory, sizeof(memory), kDataTag);
                const uint32_t formats[] = {kText, 0, 0xffffffff, 3};
                drawfx::Fake *objects[] = {&gStrings[0], &gPlain, nullptr};
                drawfx::Fake *object = objects[seed % 3];
                const uint32_t f = formats[(seed / 3) % 4];
                void *r = ours ? static_cast<void *>(cGZClipboardManagerClipboardAdapters::ConstructData(
                                     memory, nullptr, f, reinterpret_cast<cIGZUnknown *>(object)))
                               : ctx.Original<DataCtor>(0xc6d2a5)(memory, f, object);
                const auto *w = reinterpret_cast<const uint32_t *>(memory);
                drawfx::Note("data", {drawfx::Norm(drawfx::P(r)), w[0], w[1], w[2], drawfx::Norm(w[3]), w[4]});
                NoteRefs();
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "data", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

// The game's vtables hold the slots our declarations name (the scripts call the original objects through them).
DIFF_TEST(GZClipboard_VTables, 0xe9eda4)
{
    struct VSlot {
        uint32_t vtable, slot, rva;
    };
    static constexpr VSlot kSlots[] = {
        {0xe9eda4, 0, 0xc6dd35},  {0xe9eda4, 1, 0xc6d50d},  {0xe9eda4, 2, 0xc6e1a0},  {0xe9eda4, 3, 0xc6db80},
        {0xe9eda4, 4, 0xc6e689},  {0xe9eda4, 5, 0x145770},  {0xe9eda4, 6, 0xc6e634},  {0xe9eda4, 7, 0x24b3e0},
        {0xe9eda4, 8, 0xc6e31b},  {0xe9eda4, 9, 0x149d00},  {0xe9eda4, 10, 0xc6dc62}, {0xe9eda4, 11, 0xc6d6e6},
        {0xe9eda4, 12, 0xc6d832}, {0xe9eda4, 13, 0xc6d877}, {0xe9eda4, 14, 0xc6e399}, {0xe9eda4, 15, 0xc6d54e},
        {0xe9eda4, 16, 0xc6da06}, {0xe9eda4, 17, 0xc6d99c}, {0xe9eda4, 18, 0xc6e61c}, {0xe9eda4, 19, 0xc6e596},
        {0xe9eda4, 20, 0xc6d437}, {0xe9ed84, 3, 0x46390},   {0xe9ed84, 4, 0xc6daf4},  {0xe9ed84, 5, 0x145770},
    };
    for (const VSlot &v : kSlots) {
        ++ctx.cases;
        void *const *table = GameAddress<void *const>(v.vtable);
        if (table[v.slot] != GameAddress<void>(v.rva)) ctx.Fail("vtable 0x%x slot %u is not 0x%x", v.vtable, v.slot, v.rva);
    }
}
