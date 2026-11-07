// Differential test for cGZWinMgrW32 (lane KQ, src/gz/cGZWinMgrW32.h): the whole window manager in the standalone shape.
//
// WinMgr_Run: a manager built by the original constructor (0x970ea0, base 0x972aab; game vtables 0xe5b240 / 0xe5b36c /
// 0xe5b3a4, the targets' 0xe5b204 / 0xe5b218 / 0xe5b22c) or factory (0x970d39) against one built by ours (placement new /
// Create), each driven through its own vtables by the same random operations, then destroyed by the last Release
// (cRZSystemService's) or by the +8 face's deleting destructor (thunk 0x970f48, 0x970f50, the destructor 0x970f16 running
// Shutdown, the base's 0x972bcd). Operations: QueryInterface 0x973c78 through every face (thunks 0x9717b7 / 0x9717bf),
// Init 0x9714f4 (base 0x973692), Shutdown 0x971955 (base 0x973e55), OnTick 0x9737eb, every cIGZWinMgr slot: 0x971897,
// 0x973660, 0x973806, 0x971333, 0x9735e8, 0x973d5f, 0x973d39, 0x97386a, 0x97381b, 0x973b83, 0x9739e6, 0x9738b7 (with
// the bubble helper 0x9739af), 0x971302, 0x972cf6, 0x973cd4, 0x972d5f, 0x972ea1, 0x9737bf, 0x973670, 0x973d9f,
// 0x9713b7, 0x972cda, 0x973c9a, 0x973442, 0x97340a, 0x971314 (base 0x973561), 0x973645, 0x97361f, 0x971823, 0x9734cb,
// 0x97338d, 0x97358a, 0x972f48, 0x97377d, 0x973264, 0x9713be, 0x97163c, 0x971694, 0x9710b5, 0x971035, 0x9717c7,
// 0x971a55, 0x9719de, 0x973252, 0x97375b, 0x972edd, 0x972e47, 0x973788, 0x973684, 0x972e14, 0x973f77, 0x973d0a,
// 0x972cb6, 0x9716fc, and the folded accessors (slots 3, 9, 25, 29, 33, 37, 39, 44, 46, 47, 56 to 62); the message
// targets' DoMessage (0x97111c, 0x9711d1, 0x971239; the mouse flags 0x970f72 and event types 0x970fad through 0x971333).
//
// Every object reached is a recording fake (128 slots): the framework (cell 0x10c9378: GetSystemService, AddToTick,
// RemoveFromTick, OnIdle, its Win32 face), the graphics service (the canvas, the cursor), the settings ("uiScaleFactor"
// into the any), the canvas (rectangle, state, input: the variant logged by content), its input-source face and source,
// the cursor manager, the notifier (0x9c5ab4, a stand-in), the message server (0x9c5a89, a stand-in; the request is a
// real cRZMessage2Standard, 0x1c033d on RZMemory, and the server may answer the box), the message box, two hooks, the
// three canvas messages and eight windows (a parent forest; DoMessage, the mouse slot 96, flags, ids, the hit tests).
// Stand-ins: the root window's constructor 0x92fded, the cursor manager's 0x9d0158 (and ours, ConstructManager) and the
// message box's 0x952874 log their
// arguments and hand back the fakes. The cRZVariant constructor 0x9c7c75 / destructor 0x9c7d0c and the stopwatch's
// restart 0x1c95ec are the originals on both sides. The Win32 imports (ShowCursor 0xd9a4b0, ClientToScreen 0xd9a5e0,
// SetCursorPos 0xd9a4b4, Sleep 0xd9a144, PeekMessageA 0xd9a578, TranslateMessage 0xd9a580, DispatchMessageA 0xd9a57c,
// PostMessageA 0xd9a570) are logging fakes for the whole test: the real SetCursorPos / ShowCursor are never reached. The
// pump's PeekMessageA plays a script (nothing, other messages, results for other windows, then the window's result or
// WM_QUIT; WM_QUIT when the script runs out) so every loop ends. Re-entrancy: DispatchMessageA calls back into the manager
// (a key or mouse event, the mouse-over window, the focus, or a nested DoModal two deep, the interrupted window's result
// then arriving in the inner loop), a window's timer message may remove itself or another timer window, and a window
// torn down or removing a child may ask for a flush of the deferred destroys (refused inside a flush). Some Inits find a
// second framework in the cell (an Init after a Shutdown keeps the one it cached first). Each run
// starts by registering up to eight windows so the modal, focus and capture paths find registered windows. The clocks are scripted (QueryPerformanceCounter 0xd9a24c
// and our stopwatch's override). Compared after every operation: the results and out words, the vtable words, the service
// fields, every member (lists, the registry by content in order with its bucket count, the maps and sets, the timer
// position, the keyboard, the stopwatch, the option bytes, the targets' owners), the event log and every fake's count and
// values. Not compared: +0x1c4 (never written), the lock's words, +0x214 before an Init (the constructor leaves it).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "rz/RZMessageServer.h"
#include "code_patch.h"
#include "difftest.h"
#include "gzlog_support.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZClipboardManagerClipboard.h"
#include "gz/cGZWinMgrW32.h"
#include "gz/cRZCursor.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <utility>
#include <vector>

namespace winmgr_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::P;
using Mgr = cGZWinMgrW32;

constexpr uint32_t kTag = 0x7d000000; // the manager under test
constexpr unsigned kSlots = 128;

// ---- fakes with 128 slots (windows reach slot 104) ----

struct Fake;
using Custom = uint32_t (*)(Fake *self, uint32_t *args);
struct Fake {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t value[8];
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];
    char kinds[kSlots][8]; // each argument's kind: 'p' pointer, 'd' data, 0 undeclared (tests/norm_words.h)
    uint32_t line[kSlots]; // the Set's line (the undeclared-argument message names it)
    void Reset(uint32_t newId);
    void Set(unsigned slot, unsigned args, uint32_t result = 0, Custom fn = nullptr,
             std::source_location where = std::source_location::current());
    // Set with the arguments' kinds, one character each ('p', 'd', '-' undeclared): the count is the string's length.
    template <size_t N>
    void Set(unsigned slot, const char (&argKinds)[N], uint32_t result = 0, Custom fn = nullptr,
             std::source_location where = std::source_location::current())
    {
        static_assert(N - 1 <= 7, "seven arguments");
        Set(slot, unsigned(N - 1), result, fn, where);
        for (unsigned i = 0; i < N - 1; ++i) kinds[slot][i] = svcfx::Obj::Kind(argKinds[i]);
    }
};

enum : unsigned {
    kFramework,
    kFrameW32,
    kGraphics,
    kSettings,
    kCanvas,
    kInputFace,
    kInputSource,
    kCursorMgr,
    kNotifier,
    kServer,
    kBox,
    kHook0, kHooks = 2,
    kCanvasMsg = kHook0 + kHooks,
    kKeyMsg,
    kMouseMsg,
    kRoot, // the windows: the root, six others, a stray one never registered
    kWin0, kWins = 6,
    kStray = kWin0 + kWins,
    kFramework2, // a second framework in the cell for some Inits (Init caches the first it sees)
    kFakeCount
};
constexpr unsigned kWindowCount = kStray - kRoot + 1;
Fake gFakes[kFakeCount];
Fake &F(unsigned i) { return gFakes[i]; }

uint32_t Norm(uint32_t v, std::source_location where = std::source_location::current())
{
    for (unsigned i = 0; i < kFakeCount; ++i)
        if (v == P(&gFakes[i])) return 300 + i;
    return svcfx::Norm(v, where);
}
// A word the test fills with a fake (a window: PickWindow) or data (random words, PickWindowWord's 0x1230 tokens): the
// messages' words, the fakes' values, the modal results' and timer windows' keys. Tagged by identity with the test's
// fakes (their exact addresses, never a range): 1 and the fake's record, or 0 and the word as it is.
void UnionWord(Blob &b, uint32_t v)
{
    for (unsigned i = 0; i < kFakeCount; ++i)
        if (v == P(&gFakes[i])) return b.push_back(1), b.push_back(300 + i);
    for (unsigned i = 0; i < svcfx::kFakes; ++i)
        if (v == P(&svcfx::F(i))) return b.push_back(1), b.push_back(200 + i);
    b.push_back(0), b.push_back(v);
}
// A logged argument by its declared kind.
uint32_t Arg(const Fake *self, unsigned slot, unsigned i, uint32_t v)
{
    const char kind = self->kinds[slot][i];
    if (kind == 'd') return v;
    if (kind == 'p') return Norm(v);
    if (!v) return 0;
    char what[128];
    std::snprintf(what, sizeof(what), "fake #%u slot %u (Set at gz_win_mgr_w32.cpp:%u) argument %u", self->id, slot,
                  unsigned(self->line[slot]), i);
    return normwords::Undeclared(v, what, "in its Set: Set(slot, \"...\") with 'p' or 'd' per argument", svcfx::gRanges);
}

uint32_t Dispatch(Fake *self, unsigned slot, uint32_t *args, unsigned count)
{
    if (svcfx::gTrace) fprintf(stderr, "  fake %u slot %u\n", self->id, slot);
    Ev(0x60);
    Ev(self->id);
    Ev(slot);
    Ev(count);
    for (unsigned i = 0; i < count; ++i) Ev(Arg(self, slot, i, args[i]));
    if (self->custom[slot]) return self->custom[slot](self, args);
    if (slot == 1) return uint32_t(++self->refs);
    if (slot == 2) return uint32_t(--self->refs);
    return self->ret[slot];
}

template <size_t> using Word = uint32_t;
template <unsigned S, class Seq> struct Gen;
template <unsigned S, size_t... I> struct Gen<S, std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(Fake *self, void *, Word<I>... a)
    {
        uint32_t args[] = {a..., 0u};
        return Dispatch(self, S, args, unsigned(sizeof...(I)));
    }
};
void *gTable[kSlots][6];
template <unsigned S> void FillSlot()
{
    gTable[S][0] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<0>>::Fn);
    gTable[S][1] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<1>>::Fn);
    gTable[S][2] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<2>>::Fn);
    gTable[S][3] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<3>>::Fn);
    gTable[S][4] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<4>>::Fn);
    gTable[S][5] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<5>>::Fn);
}
template <size_t... S> void FillAll(std::index_sequence<S...>) { (FillSlot<unsigned(S)>(), ...); }
bool gFilled = false;

void Fake::Reset(uint32_t newId)
{
    if (!gFilled) FillAll(std::make_index_sequence<kSlots>()), gFilled = true;
    vt = table;
    id = newId;
    refs = 1000;
    for (auto &v : value) v = 0;
    for (unsigned i = 0; i < kSlots; ++i) ret[i] = 0, custom[i] = nullptr, table[i] = nullptr;
    Set(1, 0);
    Set(2, 0);
}
void Fake::Set(unsigned slot, unsigned args, uint32_t result, Custom fn, std::source_location where)
{
    line[slot] = where.line();
    std::memset(kinds[slot], 0, sizeof(kinds[slot]));
    table[slot] = gTable[slot][args];
    ret[slot] = result;
    custom[slot] = fn;
}

template <class T> T *As(uintptr_t word) { return reinterpret_cast<T *>(word); }

// The messages and key events the test made (their four words), so a logged message is read by who made it: the test's
// (an id, then words that are windows or data: UnionWord) or the manager's (by id: ManagerKinds). Cleared per side.
std::vector<std::array<uint32_t, 4>> gMade;
void Made(uint32_t id, uint32_t a, uint32_t b, uint32_t c) { gMade.push_back({id, a, b, c}); }
// The manager's messages, notes and key events by id (cGZWinMgrW32.size.cpp's): the three words after the id, 'p' a
// window or an event, 'd' data.
const char *ManagerKinds(uint32_t id)
{
    switch (id) {
    case 0: case 5: case 6: case 7: return "ddd"; // a key or mouse event (DoMessage gets the event itself): data
    case 8: case 9: case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: case 0x15:
        return "ddd"; // a mouse event (the same): x, y, buttons and modifiers
    case 0x10: case 0x11: case 0x13: case 0x14: return "pdd"; // focus lost / gained, mouse enter / leave: a window
    case 0x12: return "ppd";                                 // capture changed: the old and the new capture
    case 0x1a: case 0x1b: return "pdp";                      // key, key unhandled: the target, the type, the event
    case 0x1c: return "ddd";                                 // timer: the stopwatch's time
    case 0x4348b111: return "ddd";                           // canvas changed: its kind
    case 0x62ba7b42: case 0x62ba7b43: return "dpd";          // the notes around a modal run: id, window, depth
    default: return nullptr;
    }
}
// Logs the four words a message or key event pointer points at (16 bytes): the id or type, then the three words as
// their maker declares them.
void LogWords(uint32_t pointer)
{
    const uint32_t *w = As<uint32_t>(pointer);
    Ev(w[0]);
    for (const auto &m : gMade)
        if (std::equal(m.begin(), m.end(), w)) {
            for (int i = 1; i < 4; ++i) UnionWord(svcfx::gEv, w[i]);
            return;
        }
    const char *kinds = ManagerKinds(w[0]);
    for (int i = 1; i < 4; ++i) {
        auto what = [&] {
            char text[96];
            std::snprintf(text, sizeof(text), "the manager's message %#x word %d", w[0], i);
            return std::string(text);
        };
        Ev(svcfx::Word(kinds ? kinds[i - 1] : '-', w[i], what, "in ManagerKinds (gz_win_mgr_w32.cpp)"));
    }
}
// A mouse event (type, x, y, buttons and modifiers): data.
void LogMouse(uint32_t pointer)
{
    const uint32_t *w = As<uint32_t>(pointer);
    for (int i = 0; i < 4; ++i) Ev(w[i]);
}
// A bool answer from the fake's bit pattern (value[1], stepped by value[2]), noise above the low byte.
uint32_t NextBit(Fake *self) { return (self->value[1] >> (self->value[2]++ & 31) & 1) | 0x3300u; }
uint32_t Noise(std::mt19937 &rng, bool bit) { return (bit ? 1u : 0u) | (rng() & 0x7f00u); }

// ---- the world ----

struct World {
    bool canvas, frameW32, settings, settingsWritesAnyway, notifier, server, serverAnswers, comCursor, comBox;
    uint32_t hwnd, scaleFactor;
};
World gWorld;

constexpr uint32_t kGraphicsId = 0xc416025c, kGraphicsIid = 0x73283c;
constexpr uint32_t kSettingsId = 0x0ac1887e, kSettingsIid = 0x4bb0d269;

// The framework's slot 5 (id, iid, out).
uint32_t GetSystemService(Fake *, uint32_t *a)
{
    unsigned which = kFakeCount;
    if (a[0] == kGraphicsId && a[1] == kGraphicsIid) which = kGraphics;
    if (a[0] == kSettingsId && a[1] == kSettingsIid && gWorld.settings) which = kSettings;
    if (which == kFakeCount) return 0x100;
    ++F(which).refs;
    *As<void *>(a[2]) = &F(which);
    return 0x101;
}
// The framework's QueryInterface: its Win32 face (0x23ea70a1).
uint32_t FrameworkQuery(Fake *, uint32_t *a)
{
    if (a[0] != 0x23ea70a1 || !gWorld.frameW32) return 0x200;
    ++F(kFrameW32).refs;
    *As<void *>(a[1]) = &F(kFrameW32);
    return 0x201;
}
// The settings' GetPropertyValue (key, any): the value word of the AnyUint32 (+4).
uint32_t SettingRead(Fake *, uint32_t *a)
{
    if (gWorld.settingsWritesAnyway || gWorld.scaleFactor != 0x5e5e5e5e) As<uint32_t>(a[1])[1] = gWorld.scaleFactor;
    return gWorld.scaleFactor != 0x5e5e5e5e ? 0x401u : 0x400u;
}
// The graphics service's canvas.
uint32_t GraphicsCanvas(Fake *, uint32_t *) { return gWorld.canvas ? P(&F(kCanvas)) : 0; }
// The canvas's QueryInterface: the input-source face (0x3ac7a210).
uint32_t CanvasQuery(Fake *, uint32_t *a)
{
    if (a[0] != 0x3ac7a210) return 0x500;
    ++F(kInputFace).refs;
    *As<void *>(a[1]) = &F(kInputFace);
    return 0x501;
}
// The canvas's rectangle (value[4..7]).
uint32_t CanvasRect(Fake *self, uint32_t *a)
{
    int32_t *rect = As<int32_t>(a[0]);
    for (int i = 0; i < 4; ++i) rect[i] = int32_t(self->value[4 + i]);
    return 0;
}
// The canvas's input (a cRZVariant): its tag, count and (an array) its words.
uint32_t CanvasInput(Fake *self, uint32_t *a)
{
    const auto *v = As<uint8_t>(a[0]);
    const uint16_t tag = *reinterpret_cast<const uint16_t *>(v + 0xe);
    const uint32_t count = *reinterpret_cast<const uint32_t *>(v + 0x10);
    Ev(0x6100), Ev(tag), Ev(count), Ev(*reinterpret_cast<const uint16_t *>(v + 0xc));
    if (tag == 0x8006 && count <= 8) {
        const uint32_t *words = *reinterpret_cast<const uint32_t *const *>(v + 4);
        for (uint32_t i = 0; i < count; ++i) Ev(words[i]); // the Win32 message, flags, param: data
    }
    return NextBit(self);
}

// The COM object's slot 3 (class id, iid, out): the cursor manager and the message box when the world says so.
uint32_t GetClassObject(svcfx::Obj *, uint32_t *a)
{
    unsigned which = kFakeCount;
    if (a[0] == 0x4352fef3 && a[1] == 0xa34c498a && gWorld.comCursor) which = kCursorMgr;
    if (a[0] == 0x593 && a[1] == 0x57c && gWorld.comBox) which = kBox;
    if (which == kFakeCount) return 0x8800;
    ++F(which).refs;
    *As<void *>(a[2]) = &F(which);
    return 0x7701;
}

// The notifier's slot 3 (note): its words.
uint32_t Notify(Fake *, uint32_t *a)
{
    LogWords(a[0]);
    return 0;
}
// The message server's slot 3 (message): its content; the box answered through the message's void2 (slot 15).
uint32_t ServerPost(Fake *, uint32_t *a)
{
    svcfx::LogMessage(a[0]);
    if (gWorld.serverAnswers) {
        void *message = As<void>(a[0]);
        auto get = reinterpret_cast<void *(__thiscall *)(void *)>((*static_cast<void ***>(message))[15]);
        if (void **out = static_cast<void **>(get(message))) {
            ++F(kBox).refs;
            *out = &F(kBox);
        }
    }
    return 0;
}

// Windows. value: [0] adopts parentless modal windows, [1] DoMessage / mouse bit pattern, [2] its step, [3] the parent,
// [4] the registry id, [5] flags, [6] / [7] the client offset.
uint32_t WinQuery(Fake *self, uint32_t *a)
{
    if (a[0] != 0x1234 && a[0] != 1) return 0x700;
    ++self->refs;
    *As<void *>(a[1]) = self;
    return 0x701;
}
void *gMgr = nullptr; // the manager under test, for the fakes that call back into it (as the game's window procedure does)

uint32_t WinDoMessage(Fake *self, uint32_t *a)
{
    LogWords(a[0]);
    // A timer window may leave the timer set from its own timer message (SendTimerMessages' position steps past it).
    if (gMgr && *As<uint32_t>(a[0]) == 0x1c && (self->value[1] >> 5 & 1)) {
        Ev(0x7120);
        reinterpret_cast<void(__thiscall *)(void *, uint32_t)>((*static_cast<void ***>(gMgr))[28])(gMgr, self->value[1] >> 6 & 1 ? P(self) : P(&F(kWin0)));
    }
    return NextBit(self);
}
uint32_t WinParent(Fake *self, uint32_t *) { return self->value[3]; }
bool InChain(Fake *start, Fake *target)
{
    for (Fake *w = start; w; w = As<Fake>(w->value[3]))
        if (w == target) return true;
    return false;
}
uint32_t WinAdopt(Fake *self, uint32_t *a)
{
    Fake *window = As<Fake>(a[0]);
    if (!(self->value[0] & 1) || !window || window->value[3] || InChain(self, window)) return 0x800;
    window->value[3] = P(self);
    return 0x801;
}
uint32_t WinScreenToClient(Fake *self, uint32_t *a)
{
    *As<int32_t>(a[0]) -= int32_t(self->value[6]);
    *As<int32_t>(a[1]) -= int32_t(self->value[7]);
    return 0;
}
uint32_t WinGetId(Fake *self, uint32_t *) { return self->value[4]; }
uint32_t WinSetId(Fake *self, uint32_t *a)
{
    self->value[4] = a[0];
    return 0;
}
uint32_t WinIsFlag(Fake *self, uint32_t *a) { return ((self->value[5] & a[0]) != 0 ? 1u : 0u) | 0x4400u; }
uint32_t WinSetFlag(Fake *self, uint32_t *a)
{
    const uint32_t old = self->value[5];
    self->value[5] = a[1] ? old | a[0] : old & ~a[0];
    return old;
}
uint32_t WinMouse(Fake *self, uint32_t *a)
{
    LogMouse(a[0]);
    return NextBit(self);
}
// A hook's slot 3 (0, message).
uint32_t HookCall(Fake *self, uint32_t *a)
{
    LogWords(a[1]);
    return NextBit(self);
}
// A canvas message's QueryInterface: itself for the iid in value[0] when value[1] says so.
uint32_t MsgQuery(Fake *self, uint32_t *a)
{
    if (a[0] != self->value[0] || !self->value[1]) return 0x900;
    ++self->refs;
    *As<void *>(a[1]) = self;
    return 0x901;
}
// A mouse message's point (slot 10: returned through the caller's buffer).
uint32_t MsgPoint(Fake *self, uint32_t *a)
{
    As<int32_t>(a[0])[0] = int32_t(self->value[4]);
    As<int32_t>(a[0])[1] = int32_t(self->value[5]);
    return a[0];
}

// ---- stand-ins ----

void *__fastcall RootCtor(void *self, void *, uint32_t manager, uint32_t keyboard)
{
    Ev(0x77000001), Ev(Norm(manager)), Ev(Norm(keyboard));
    ::operator delete(self);
    return &F(kRoot);
}
void *__fastcall CursorCtor(void *self, void *)
{
    Ev(0x77000002);
    ::operator delete(self);
    return &F(kCursorMgr);
}
void *__fastcall BoxCtor(void *self, void *)
{
    Ev(0x77000003);
    ::operator delete(self);
    return reinterpret_cast<uint8_t *>(&F(kBox)) - 0xfc;
}
void *__cdecl NotifierStandIn()
{
    Ev(0x77000004);
    return gWorld.notifier ? &F(kNotifier) : nullptr;
}
void *__cdecl ServerStandIn()
{
    Ev(0x77000005);
    return gWorld.server ? &F(kServer) : nullptr;
}
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- the Win32 fakes ----

struct Msg {
    void *hwnd;
    uint32_t message, wParam, lParam, time;
    int32_t x, y;
};
static_assert(sizeof(Msg) == 28);

std::vector<uint32_t> gScript; // PeekMessageA's steps: 0 nothing, 1 another message, 2 a result for another window,
size_t gScriptPos = 0;         // 3 the modal window's result, 4 WM_QUIT (also after the end)
std::vector<uint32_t> gModal; // the modal windows the pumps run for, innermost last (step 3 answers it)
unsigned gNest = 0;

int __stdcall FakeShowCursor(int show)
{
    Ev(0x7101), Ev(uint32_t(show));
    return show ? 1 : -1;
}
int __stdcall FakeClientToScreen(void *hwnd, int32_t *point)
{
    Ev(0x7102), Ev(P(hwnd)), Ev(uint32_t(point[0])), Ev(uint32_t(point[1]));
    point[0] += 100, point[1] += 200;
    return 1;
}
int __stdcall FakeSetCursorPos(int x, int y)
{
    Ev(0x7103), Ev(uint32_t(x)), Ev(uint32_t(y));
    return 1;
}
void __stdcall FakeSleep(uint32_t ms) { Ev(0x7104), Ev(ms); }
int __stdcall FakePeekMessage(Msg *msg, void *hwnd, uint32_t lo, uint32_t hi, uint32_t remove)
{
    Ev(0x7105), Ev(P(hwnd)), Ev(lo), Ev(hi), Ev(remove);
    const size_t pos = gScriptPos++;
    const uint32_t step = pos < gScript.size() ? gScript[pos] : 4;
    if (step == 0) return 0;
    msg->hwnd = reinterpret_cast<void *>(uintptr_t(0x4440 + pos));
    msg->time = 77 + uint32_t(pos);
    msg->x = 5, msg->y = 6;
    switch (step) {
    case 1:
        msg->message = 0x100 + uint32_t(pos % 5) * 0x101; // 0x100, 0x201, 0x302, 0x403, 0x504
        msg->wParam = uint32_t(pos), msg->lParam = 9;
        break;
    case 2:
        msg->message = 0x700;
        msg->wParam = pos % 3 ? P(&F(kWin0 + pos % kWins)) : 0x1000 + uint32_t(pos);
        if (gModal.size() > 1 && pos % 2) msg->wParam = gModal[gModal.size() - 2]; // the interrupted modal window
        else if (!gModal.empty() && msg->wParam == gModal.back()) msg->wParam = P(&F(kStray));
        msg->lParam = 0x1000 + uint32_t(pos);
        break;
    case 3:
        msg->message = 0x700;
        msg->wParam = gModal.empty() ? 0 : gModal.back(), msg->lParam = 0x2000 + uint32_t(pos);
        break;
    default:
        msg->message = 0x12;
        msg->wParam = 7, msg->lParam = 8;
        break;
    }
    return 1;
}
int __stdcall FakeTranslateMessage(const Msg *msg)
{
    Ev(0x7106), Ev(P(msg->hwnd)), Ev(msg->message), Ev(msg->wParam), Ev(msg->lParam);
    return 1;
}
intptr_t __stdcall FakeDispatchMessage(const Msg *msg)
{
    Ev(0x7107), Ev(P(msg->hwnd)), Ev(msg->message), Ev(msg->wParam), Ev(msg->lParam), Ev(msg->time), Ev(uint32_t(msg->x)),
        Ev(uint32_t(msg->y));
    // The window procedure's input reaches the manager inside the pump (so with a modal window up): a key, a mouse event
    // (the modal branch of DispatchMouseEvent), the mouse-over and the focus, and (one level) a nested modal run whose
    // results can arrive for the interrupted window.
    if (!gMgr || gNest >= 3) return 0;
    ++gNest;
    const uint32_t w = msg->wParam;
    void *window = &F(kRoot + (w * 7 + 3) % kWindowCount);
    switch (msg->message) {
    case 0x100: {
        static const uint32_t kKeys[] = {0x10, 0x41, 0x0d, 0xff};
        const cIGZWinMgr::KeyEvent event{5 + w % 2, kKeys[w % 4], w, 0};
        Made(event.type, event.key, event.a, event.b);
        Ev(0x7110), Ev(reinterpret_cast<uint32_t(__thiscall *)(void *, const void *)>((*static_cast<void ***>(gMgr))[16])(gMgr, &event) & 0xff);
        break;
    }
    case 0x201: {
        static const uint32_t kTypes[] = {7, 8, 13, 10, 0x14, 0xd};
        cIGZWinMgr::MouseEvent event{kTypes[w % 6], int32_t(w * 37 % 900), int32_t(w * 53 % 700), uint16_t(w), uint16_t(w >> 1)};
        Ev(0x7111), Ev(reinterpret_cast<uint32_t(__thiscall *)(void *, void *)>((*static_cast<void ***>(gMgr))[15])(gMgr, &event) & 0xff);
        Ev(uint32_t(event.x)), Ev(uint32_t(event.y));
        break;
    }
    case 0x302:
        Ev(0x7112), Ev(reinterpret_cast<uint32_t(__thiscall *)(void *, void *)>((*static_cast<void ***>(gMgr))[24])(gMgr, window) & 0xff);
        break;
    case 0x403:
        if (gModal.size() < 2 && *reinterpret_cast<const uint32_t *>(static_cast<uint8_t *>(gMgr) + 0x68)) { // DoModal needs the framework (Init)
            gModal.push_back(P(window));
            Ev(0x7113), Ev(reinterpret_cast<uint32_t(__thiscall *)(void *, void *)>((*static_cast<void ***>(gMgr))[42])(gMgr, window));
            gModal.pop_back();
        }
        break;
    default:
        Ev(0x7114), Ev(Norm(reinterpret_cast<uint32_t(__thiscall *)(void *, void *)>((*static_cast<void ***>(gMgr))[38])(gMgr, window)));
        break;
    }
    --gNest;
    return 0;
}
int __stdcall FakePostMessage(void *hwnd, uint32_t message, uint32_t wParam, uint32_t lParam)
{
    Ev(0x7108), Ev(P(hwnd)), Ev(message), Ev(wParam), Ev(lParam);
    return 1;
}

void MakeScript(uint32_t c)
{
    std::mt19937 rng(c);
    gScript.clear();
    gScriptPos = 0;
    // Nothing, other messages (some calling back into the manager), results for other windows, now and then the innermost
    // window's result (ending a nested run).
    static const uint32_t kSteps[] = {0, 1, 1, 1, 2, 2, 3};
    const unsigned n = rng() % 16;
    for (unsigned i = 0; i < n; ++i) gScript.push_back(kSteps[rng() % 7]);
    gScript.push_back(rng() % 3 ? 3u : 4u);
}

// ---- building the fakes ----

uint32_t FloatBits(float f)
{
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return w;
}

// A window torn down (slot 5) or removing a child (slot 20) may ask the manager to flush its deferred destroys (slot
// 66): inside a flush the manager refuses (not reentered); elsewhere the queue is empty or the deferral still open.
uint32_t WinGoing(Fake *self, uint32_t *)
{
    if (gMgr && gNest < 3 && (self->value[1] >> 7 & 1)) {
        ++gNest;
        Ev(0x7130), Ev(reinterpret_cast<uint32_t(__thiscall *)(void *)>((*static_cast<void ***>(gMgr))[66])(gMgr) & 0xff);
        --gNest;
    }
    return 0;
}

void SetWindow(Fake &w, std::mt19937 &rng)
{
    w.Set(0, "dp", 0, &WinQuery);
    w.Set(3, "p", 0, &WinDoMessage);
    w.Set(4, 0), w.Set(5, 0, 0, &WinGoing);
    w.Set(11, 0, 0, &WinParent);
    w.Set(19, "p", 0, &WinAdopt);
    w.Set(20, "p", 0, &WinGoing);
    for (unsigned s : {22u, 26u, 27u}) w.Set(s, "p", Noise(rng, rng() % 3 != 0));
    w.Set(77, "dd", Noise(rng, rng() % 3 != 0));
    for (unsigned s : {86u, 88u}) w.Set(s, 0, Noise(rng, rng() % 3 != 0));
    w.Set(28, 0);
    w.Set(47, "dd", rng() % 3 ? P(&F(kRoot + rng() % kWindowCount)) : 0);
    w.Set(64, "--dd");
    w.Set(74, "pp", 0, &WinScreenToClient);
    w.Set(78, 0, 0x6000 + w.id);
    w.Set(80, 0, 0, &WinGetId);
    w.Set(81, "d", 0, &WinSetId);
    w.Set(82, "d", 0, &WinIsFlag);
    w.Set(83, "dd", 0, &WinSetFlag);
    w.Set(84, "d");
    w.Set(96, "p", 0, &WinMouse);
    w.Set(100, 0, rng() % 2 ? P(&F(kRoot + rng() % kWindowCount)) : 0);
    w.Set(102, 0), w.Set(104, 0);
    w.value[0] = rng() % 3 != 0;
    w.value[1] = rng();
    w.value[4] = rng() % 3 ? 0 : rng() % 50;
    w.value[5] = rng() % 4 ? 0x8000u | (rng() & 0x1000u) : rng() & 0x1000u;
    w.value[6] = rng() % 20, w.value[7] = rng() % 20;
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 17);
    svcfx::ResetAll();
    for (unsigned i = 0; i < kFakeCount; ++i) F(i).Reset(300 + i);
    gWorld.canvas = rng() % 8 != 0;
    gWorld.frameW32 = rng() % 4 != 0;
    gWorld.hwnd = rng() % 4 ? 0x5150 : 0;
    gWorld.settings = rng() % 4 != 0;
    static const uint32_t kFactors[] = {1, 2, 3, 0, 4, 0xffffffffu, 0x5e5e5e5e, 2, 3};
    gWorld.scaleFactor = kFactors[rng() % 9];
    gWorld.settingsWritesAnyway = rng() % 3 == 0;
    gWorld.notifier = rng() % 6 != 0;
    gWorld.server = rng() % 3 != 0;
    gWorld.serverAnswers = rng() % 2 == 0;
    gWorld.comCursor = rng() % 2 == 0;
    gWorld.comBox = rng() % 2 == 0;

    for (unsigned fw : {unsigned(kFramework), unsigned(kFramework2)}) {
        F(fw).Set(0, "dp", 0, &FrameworkQuery);
        F(fw).Set(5, "ddp", 0, &GetSystemService);
        F(fw).Set(9, "p", 0x901), F(fw).Set(10, "p", 0x902), F(fw).Set(16, 0);
    }
    F(kFrameW32).Set(5, 0, gWorld.hwnd);
    F(kGraphics).Set(14, 0, 0, &GraphicsCanvas);
    F(kGraphics).Set(30, "d", 0x3000 + rng() % 9);
    F(kSettings).Set(9, "pp", 0, &SettingRead);
    Fake &canvas = F(kCanvas);
    canvas.Set(0, "dp", 0, &CanvasQuery);
    canvas.Set(12, "p", 0, &CanvasRect);
    canvas.Set(20, 0, 0x2000 + rng() % 9);
    canvas.Set(21, "p", 0, &CanvasInput);
    canvas.value[1] = rng();
    static const int32_t kHeights[] = {300, 600, 767, 768, 769, 1080, 1440, 1536, 2160, 2304, 4000, 0};
    canvas.value[4] = rng() % 4 ? 0 : rng() % 30;
    canvas.value[5] = rng() % 4 ? 0 : rng() % 30;
    canvas.value[6] = canvas.value[4] + 640 + rng() % 3000;
    canvas.value[7] = canvas.value[5] + uint32_t(kHeights[rng() % 12]);
    F(kInputFace).Set(3, 0, P(&F(kInputSource)));
    F(kInputSource).Set(5, "pd"), F(kInputSource).Set(6, "pd");
    F(kCursorMgr).Set(3, 0), F(kCursorMgr).Set(4, 0);
    F(kNotifier).Set(3, "p", 0, &Notify);
    F(kServer).Set(3, "p", 0, &ServerPost);
    F(kBox).Set(5, "ddd", Noise(rng, rng() % 4 != 0));
    F(kBox).Set(3, 0, rng() % 5 ? P(&F(kRoot + rng() % kWindowCount)) : 0);
    for (unsigned i = 0; i < kHooks; ++i) {
        F(kHook0 + i).Set(3, "pp", 0, &HookCall);
        F(kHook0 + i).value[1] = rng() & rng();
    }
    static const uint32_t kMsgIids[3] = {0xda1062fa, 0x1a104742, 0x1a0c615f};
    for (unsigned i = 0; i < 3; ++i) {
        Fake &m = F(kCanvasMsg + i);
        m.Set(0, "dp", 0, &MsgQuery);
        m.value[0] = kMsgIids[i];
        m.value[1] = rng() % 6 != 0;
        m.Set(9, 0), m.Set(11, 0), m.Set(12, 0);
        if (i == 2) m.Set(10, "p", 0, &MsgPoint);
        else m.Set(10, 0);
    }
    for (unsigned i = 0; i < kWindowCount; ++i) SetWindow(F(kRoot + i), rng);
    // The parent forest: the root and the stray window have none; another window's parent is the root, an earlier
    // window, the stray window or none.
    for (unsigned i = 0; i < kWins; ++i) {
        const unsigned pick = rng() % (i + 4);
        Fake &w = F(kWin0 + i);
        w.value[3] = pick == 0 ? 0 : pick == 1 ? P(&F(kRoot)) : pick == 2 ? P(&F(kStray)) : P(&F(kWin0 + (pick - 3) % (i + 1)));
        if (w.value[3] == P(&w)) w.value[3] = 0;
    }

    svcfx::F(0).Set(3, "ddp", 0, reinterpret_cast<svcfx::Custom>(&GetClassObject));
    *GameAddress<void *>(0x10c9378) = &F(kFramework);
    // The UI scale's globals: the minimum height, the design height, the factor-2 and factor-3 heights.
    static const float kMin[] = {0.f, 480.f, 600.f, 1000.f};
    static const float kDesign[] = {768.f, 1024.f, 600.f, 2160.f, 1080.f};
    static const float kF2[] = {1536.f, 1440.f, 768.f};
    static const float kF3[] = {2160.f, 2304.f, 1080.f};
    *GameAddress<float>(0x105f808) = kMin[rng() % 4];
    *GameAddress<float>(0x10ecc88) = kDesign[rng() % 5];
    *GameAddress<float>(0x105f80c) = kF2[rng() % 3];
    *GameAddress<float>(0x105f810) = rng() % 4 ? kF3[rng() % 3] : *GameAddress<float>(0x10ecc88);
    gModal.clear();
    gMade.clear();
    gScript.clear();
    gScriptPos = 0;
}

void CaptureFakes(Blob &b)
{
    for (const Fake &o : gFakes) {
        b.push_back(uint32_t(o.refs));
        for (uint32_t v : o.value) UnionWord(b, v); // parents (windows) and data
    }
    svcfx::CaptureFakes(b);
    b.push_back(*GameAddress<uint32_t>(0x10ecc88));
}

// ---- driving ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
Mgr &View(void *object) { return *static_cast<Mgr *>(object); }
uint32_t Word32(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }
uint8_t Byte(const void *m, uint32_t off) { return static_cast<const uint8_t *>(m)[off]; }

bool gFactory;
bool gInitRan;

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) {
        void *face = ours ? static_cast<void *>(Mgr::Create()) : ctx.Original<void *(__cdecl *)()>(0x970d39)();
        return static_cast<uint8_t *>(face) - 4;
    }
    void *memory = ::operator new(Mgr::kSize);
    std::memset(memory, 0xc5, Mgr::kSize);
    if (ours) ::new (memory) Mgr;
    else ctx.Original<void *(__thiscall *)(void *)>(0x970ea0)(memory);
    return memory;
}

template <class C> void DumpPointers(Blob &b, const C &c)
{
    b.push_back(uint32_t(c.size()));
    for (const auto &e : c) b.push_back(Norm(uint32_t(uintptr_t(e))));
}

void Dump(Blob &b, void *object, bool ours)
{
    // The game's vtables of each face, the targets and the keyboard (kept here, not in src/: our code must not hold them).
    static const uint32_t kFaces[][2] = {{0, 0xe5b240},     {4, 0xe5b36c},     {8, 0xe5b3a4}, {0x1f4, 0xe5b204},
                                         {0x1fc, 0xe5b218}, {0x204, 0xe5b22c}, {0x6c, 0xe61c38}};
    for (const auto &f : kFaces) {
        const bool game = Word32(object, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 0xc; off <= 0x18; off += 4) b.push_back(Word32(object, off));
    b.push_back(Byte(object, 0x1c));
    const Mgr &m = View(object);
    b.push_back(uint32_t(m.mPosted.size()));
    for (const auto &p : m.mPosted) {
        b.push_back(Norm(P(p.target)));
        b.push_back(p.message.id); // posted by the test only
        UnionWord(b, p.message.a), UnionWord(b, p.message.b), UnionWord(b, p.message.c);
    }
    b.push_back(Byte(object, 0x30));
    for (uint32_t off = 0x34; off <= 0x44; off += 4) b.push_back(Norm(Word32(object, off)));
    b.push_back(uint32_t(m.mWindows.bucket_count()));
    DumpPointers(b, m.mWindows);
    b.push_back(Norm(Word32(object, 0x68)));
    for (uint32_t i = 0; i < cRZKeyboard::kKeyCount; i += 4) b.push_back(Word32(&m.mKeyboard.mDown[i], 0));
    b.push_back(uint32_t(m.mKeyboard.mDownOrder.size()));
    for (uint32_t key : m.mKeyboard.mDownOrder) b.push_back(key);
    b.push_back(Norm(Word32(object, 0x178)));
    for (uint32_t off = 0x17c; off <= 0x190; off += 4) b.push_back(Word32(object, off));
    b.push_back(Byte(object, 0x194));
    b.push_back(Word32(object, 0x198));
    DumpPointers(b, m.mDeferred);
    b.push_back(Word32(object, 0x1a4));
    b.push_back(uint32_t(m.mModalResults.size()));
    for (const auto &e : m.mModalResults) UnionWord(b, e.first), b.push_back(e.second);
    b.push_back(uint32_t(m.mHooks.size()));
    for (const WinRef &h : m.mHooks) b.push_back(Norm(P(h.Get())));
    b.push_back(uint32_t(m.mTimerWindows.size()));
    uint32_t next = 0;
    for (auto it = m.mTimerWindows.begin(); it != m.mTimerWindows.end() && it != m.mTimerNext; ++it) ++next;
    for (uint32_t w : m.mTimerWindows) UnionWord(b, w);
    b.push_back(next);
    b.push_back(Word32(object, 0x1c8));
    for (uint32_t off = 0x1d0; off < 0x1e0; off += 4) b.push_back(Word32(object, off));
    for (uint32_t off = 0x1e0; off <= 0x1e6; ++off) b.push_back(Byte(object, off));
    b.push_back(Word32(object, 0x1e8));
    b.push_back(uint32_t(m.mUnusedSet.size()));
    for (uint32_t off : {0x1f8u, 0x200u, 0x208u, 0x20cu, 0x210u}) b.push_back(Norm(Word32(object, off)));
    if (gInitRan) b.push_back(Word32(object, 0x214));
}

struct Op {
    unsigned kind;
    uint32_t a, c;
};

constexpr unsigned kKinds = 48;

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 7);
    std::vector<Op> ops;
    const unsigned n = 6 + rng() % 44;
    // Mostly Init first (some seeds run a few operations before it).
    if (rng() % 5) ops.push_back({0, rng(), rng()});
    // Most windows registered up front (the root registers itself in the game; here the test does it).
    for (unsigned i = rng() % 9; i > 0; --i) ops.push_back({13, rng(), rng()});
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % kKinds, rng(), rng()});
    return ops;
}

void *PickWindow(uint32_t a, bool allowNull = true)
{
    if (allowNull && a % 9 == 0) return nullptr;
    return &F(kRoot + (a >> 4) % kWindowCount);
}
// A window word for the timer set and the modal results: a window, or a plain number.
uint32_t PickWindowWord(uint32_t a) { return a % 5 == 0 ? 0x1230 + a % 3 : P(PickWindow(a >> 3, false)); }

bool HasCanvas(void *o) { return Word32(o, 0x20c) != 0; }
bool HasMain(void *o) { return Word32(o, 0x44) != 0; }
bool Running(void *o) { return Byte(o, 0x30) != 0; }
// A registered capture is released (a mouse move through the canvas's scale) when a window goes.
bool CaptureSafe(void *o) { return HasCanvas(o) || Word32(o, 0x34) == 0; }

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const mgr = object;
    void *const service = Face(object, 4);
    const uint32_t a = op.a, c = op.c;
    switch (op.kind) {
    case 0: // Init (sometimes with the second framework in the cell: an Init after a Shutdown keeps the first)
        gInitRan = true;
        if (a % 3 == 0) *GameAddress<void *>(0x10c9378) = &F(kFramework2);
        b.push_back(Slot<uint32_t>(service, 8) & 0xff);
        *GameAddress<void *>(0x10c9378) = &F(kFramework);
        break;
    case 1: // Shutdown (rarely)
        if (a % 4) return;
        b.push_back(Slot<uint32_t>(service, 9) & 0xff);
        break;
    case 2: // OnTick
        if (!CaptureSafe(object)) return;
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        break;
    case 3: // SetMainWindow
        if (Running(object) && !HasCanvas(object)) return;
        b.push_back(Slot<uint32_t>(mgr, 4, PickWindow(a)) & 0xff);
        break;
    case 4: { // QueryMainWindow
        uint32_t out = 0x5e5e5e5e;
        b.push_back(Slot<uint32_t>(mgr, 5, a % 3 ? 0x1234u : 0x77u, &out) & 0xff);
        b.push_back(svcfx::NormOut(out)); // left as set, or the window
        if (out != 0x5e5e5e5e) Slot<uint32_t>(As<void>(out), 2);
        break;
    }
    case 5: // UpdateMainWindow, UpdateAllWindows
        if (a % 2 && HasMain(object)) Slot<void>(mgr, 6);
        else Slot<void>(mgr, 64);
        break;
    case 6: { // SendInputEvent / SendInputEvent2
        static const uint32_t kTypes[] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0x4348b111, 0, 0x16, 0xffffffff};
        b.push_back(Slot<uint32_t>(mgr, a % 2 ? 7u : 9u, kTypes[(a >> 1) % 15], c & 0x7f, c >> 7) & 0xff);
        break;
    }
    case 7: // SendMouseInput
        if (!HasCanvas(object)) return;
        b.push_back(Slot<uint32_t>(mgr, 8, 7 + a % 8, int32_t(c % 3000) - 100, int32_t((c >> 12) % 2000) - 50, a >> 8) & 0xff);
        break;
    case 8: { // SendWinMessage / SendWinMessageStruct
        void *w = PickWindow(a);
        if (a % 2) {
            Made(c % 0x30, c, P(PickWindow(c)), 3u);
            b.push_back(Slot<uint32_t>(mgr, 10, w, c % 0x30, c, P(PickWindow(c)), 3u) & 0xff);
        } else {
            const cIGZWinMgr::WinMessage message{c % 0x30, 1, P(PickWindow(c >> 4)), c};
            Made(message.id, message.a, message.b, message.c);
            b.push_back(Slot<uint32_t>(mgr, 11, w, &message) & 0xff);
        }
        break;
    }
    case 9: { // PostWinMessage / PostWinMessageStruct
        void *w = a % 4 == 0 ? nullptr : PickWindow(a);
        if (a % 2) {
            Made(c % 0x30, c, a, P(PickWindow(c)));
            Slot<void>(mgr, 12, w, c % 0x30, c, a, P(PickWindow(c)));
        } else {
            const cIGZWinMgr::WinMessage message{c % 0x30, 2, c, P(PickWindow(c >> 4))};
            Made(message.id, message.a, message.b, message.c);
            Slot<void>(mgr, 13, w, &message);
        }
        break;
    }
    case 10: // DispatchPostedMessages
        if (!CaptureSafe(object)) return;
        Slot<void>(mgr, 14);
        break;
    case 11: { // DispatchMouseEvent
        static const uint32_t kTypes[] = {7, 8, 10, 11, 13, 14, 0x14, 0xd, 5, 0, 0x15};
        cIGZWinMgr::MouseEvent event{kTypes[a % 11], int32_t(c % 900), int32_t((c >> 10) % 700), uint16_t(a >> 8),
                                     uint16_t(c >> 20)};
        b.push_back(Slot<uint32_t>(mgr, 15, &event) & 0xff);
        b.push_back(event.type), b.push_back(uint32_t(event.x)), b.push_back(uint32_t(event.y));
        break;
    }
    case 12: { // DispatchKeyEvent
        static const uint32_t kKeys[] = {0, 0xff, 0x10, 0x11, 0x12, 0x41, 0x0d};
        const cIGZWinMgr::KeyEvent event{a % 4 == 0 ? 7u : 5 + a % 2, a % 3 ? kKeys[(a >> 2) % 7] : c % 0x100, c, a};
        Made(event.type, event.key, event.a, event.b);
        b.push_back(Slot<uint32_t>(mgr, 16, &event) & 0xff);
        break;
    }
    case 13: // RegisterWindow
        b.push_back(Slot<uint32_t>(mgr, 18, PickWindow(a, false)) & 0xff);
        break;
    case 14: // UnregisterWindow
        b.push_back(Slot<uint32_t>(mgr, 19, PickWindow(a)) & 0xff);
        break;
    case 15: // OnWindowRemoved
        if (!CaptureSafe(object)) return;
        Slot<void>(mgr, 20, PickWindow(a, false));
        break;
    case 16: // DestroyWindow / DestroyWindowNow
        if (!CaptureSafe(object)) return;
        b.push_back(Slot<uint32_t>(mgr, a % 3 ? 21u : 65u, PickWindow(a, false)) & 0xff);
        break;
    case 17: // IsWindowRegistered, WindowAtPoint
        b.push_back(Slot<uint32_t>(mgr, 22, PickWindow(a)) & 0xff);
        b.push_back(Norm(Slot<uint32_t>(mgr, 23, int32_t(c % 500), int32_t(c >> 20))));
        break;
    case 18: // SetMouseOverWindow
        b.push_back(Slot<uint32_t>(mgr, 24, PickWindow(a)) & 0xff);
        break;
    case 19: // AddTimerWindow / RemoveTimerWindow
        Slot<void>(mgr, a % 3 ? 27u : 28u, PickWindowWord(c));
        break;
    case 20: // SendTimerMessages
        Slot<void>(mgr, 70);
        break;
    case 21: // SetCapture
        b.push_back(Slot<uint32_t>(mgr, 30, PickWindow(a)) & 0xff);
        break;
    case 22: // ReleaseCapture
        if (!HasCanvas(object)) return;
        b.push_back(Slot<uint32_t>(mgr, 31, a % 3 ? reinterpret_cast<void *>(uintptr_t(Word32(object, 0x34))) : PickWindow(a)) &
                    0xff);
        break;
    case 23: // ShowCursor
        b.push_back(Slot<uint32_t>(mgr, 32, a % 2 ? 0u : (a % 4 == 1 ? 1u : 0x101u)));
        break;
    case 24: { // GetCursorPos, GetCursorPosIn
        int32_t x = 0x5e5e5e5e, y = 0x5e5e5e5e;
        if (a % 2) Slot<void>(mgr, 34, &x, &y);
        else Slot<void>(mgr, 35, PickWindow(a, false), &x, &y);
        b.push_back(uint32_t(x)), b.push_back(uint32_t(y));
        break;
    }
    case 25: // SetCursorPos
        Slot<void>(mgr, 36, int32_t(c % 2000) - 10, int32_t((c >> 11) % 1500) - 10);
        break;
    case 26: // SetFocus
        b.push_back(Norm(Slot<uint32_t>(mgr, 38, PickWindow(a))));
        break;
    case 27: // AddMessageHook / RemoveMessageHook
        // A null hook is kept like any other, and CallMessageHooks would call it (both sides): never added.
        if (a % 3) Slot<void>(mgr, 40, &F(kHook0 + (a >> 3) % kHooks));
        else Slot<void>(mgr, 41, a % 7 == 0 ? nullptr : &F(kHook0 + (a >> 3) % kHooks));
        break;
    case 28: case 29: { // DoModal (the framework's OnIdle after the loop: not before an Init, on both sides)
        if (!Word32(object, 0x68)) return;
        void *w = PickWindow(a);
        gModal.assign(1, P(w));
        MakeScript(c);
        b.push_back(Slot<uint32_t>(mgr, 42, w));
        b.push_back(uint32_t(gScriptPos));
        break;
    }
    case 30: { // DoMessageBox (DoModal: as above)
        if (!Word32(object, 0x68)) return;
        gModal.assign(1, F(kBox).ret[3]);
        MakeScript(c);
        b.push_back(Slot<uint32_t>(mgr, 45, c % 7, a, c >> 3, a >> 2, 5u));
        b.push_back(uint32_t(gScriptPos));
        break;
    }
    case 31: { // GetUIScale, ScaleRectDown
        int32_t rect[4] = {int32_t(c % 50), int32_t((c >> 6) % 40), int32_t(640 + (c >> 12) % 3000), 0};
        static const int32_t kHeights[] = {300, 600, 767, 768, 769, 1080, 1440, 1536, 2160, 2304, 4000, 0, -5};
        rect[3] = rect[1] + kHeights[a % 13];
        if (a % 2) {
            b.push_back(FloatBits(Slot<float>(mgr, 48, rect)));
        } else {
            Slot<void>(mgr, 51, rect);
            for (int32_t v : rect) b.push_back(uint32_t(v));
        }
        break;
    }
    case 32: { // ScaleDown, ScaleUp, ScaleRectUp
        if (!HasCanvas(object)) return;
        int32_t p[4] = {int32_t(c % 3000) - 100, int32_t((c >> 12) % 2000) - 50, int32_t(a % 900), int32_t((a >> 10) % 700)};
        switch (a % 3) {
        case 0: Slot<void>(mgr, 49, &p[0], &p[1]); break;
        case 1: Slot<void>(mgr, 50, &p[0], &p[1]); break;
        default: Slot<void>(mgr, 52, p); break;
        }
        for (int32_t v : p) b.push_back(uint32_t(v));
        break;
    }
    case 33: { // SetDesignHeight, ApplyUIScale, ReloadUIScale
        if (!HasCanvas(object) || !HasMain(object)) return;
        static const float kHeights[] = {768.f, 1080.f, 2160.f, 600.f, 1536.f};
        switch (a % 3) {
        case 0: Slot<void>(mgr, 53, kHeights[c % 5]); break;
        case 1: Slot<void>(mgr, 54); break;
        default:
            if (c % 2) gWorld.scaleFactor = 1 + c % 3;
            Slot<void>(mgr, 55);
            break;
        }
        break;
    }
    case 34: // PostModalResult, SetModalResult
        if (a % 2) b.push_back(Slot<uint32_t>(mgr, 63, PickWindow(a), c) & 0xff);
        else Slot<void>(mgr, 72, PickWindowWord(a >> 1), c);
        break;
    case 35: // FlushDeferredDestroys, IsDestroyDeferred
        if (a % 2 && CaptureSafe(object)) b.push_back(Slot<uint32_t>(mgr, 66) & 0xff);
        else b.push_back(Slot<uint32_t>(mgr, 67, PickWindow(a, false)) & 0xff);
        break;
    case 36: // BeginDeferDestroy / EndDeferDestroy
        if (a % 2) b.push_back(Slot<uint32_t>(mgr, 68));
        else if (CaptureSafe(object)) b.push_back(Slot<uint32_t>(mgr, 69));
        break;
    case 37: { // CallMessageHooks
        const cIGZWinMgr::WinMessage message{c % 0x30, a, P(PickWindow(c)), 4};
        Made(message.id, message.a, message.b, message.c);
        b.push_back(Slot<uint32_t>(mgr, 71, &message) & 0xff);
        break;
    }
    case 38: { // RunModalLoop
        void *w = PickWindow(a);
        gModal.assign(1, P(w));
        MakeScript(c);
        b.push_back(Slot<uint32_t>(mgr, 73, w, a % 3 ? static_cast<void *>(&F(kFramework)) : nullptr, c, a >> 4));
        b.push_back(uint32_t(gScriptPos));
        break;
    }
    case 39: { // the folded accessors and the unused slots
        for (unsigned s : {3u, 25u, 29u, 33u, 37u, 39u, 26u}) b.push_back(Norm(Slot<uint32_t>(mgr, s))); // the objects
        for (unsigned s : {44u, 17u}) b.push_back(Slot<uint32_t>(mgr, s)); // the modal depth, the canvas state
        b.push_back(Slot<uint32_t>(mgr, 43) & 0xff); // IsModal: a bool (al)
        b.push_back(Slot<uint32_t>(mgr, 46, a, c, 1u) & 0xff);
        b.push_back(Slot<uint32_t>(mgr, 47, a, c, 2u) & 0xff);
        b.push_back(Slot<uint32_t>(mgr, 56) & 0xff), b.push_back(Slot<uint32_t>(mgr, 58) & 0xff),
            b.push_back(Slot<uint32_t>(mgr, 60) & 0xff);
        Slot<void>(mgr, 57, a), Slot<void>(mgr, 59, a), Slot<void>(mgr, 61, a), Slot<void>(mgr, 62);
        break;
    }
    case 40: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kAsk[] = {1, cIGZWinMgr::kIid, 0x287fb697, Mgr::kServiceId, 0x12345678};
        void *faces[] = {mgr, service, Face(object, 8)};
        uint32_t q = 0x5e5e5e5e;
        const uint32_t ok = Slot<uint32_t>(faces[a % 3], 0, kAsk[(a >> 4) % 5], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(q)); // left as set, or the face
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 3], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 3], 2));
        if (ok) b.push_back(Slot<uint32_t>(As<void>(q), 2));
        break;
    }
    case 41: // the cIGZSystemService accessors
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        break;
    case 42: { // the canvas target
        uint32_t kind = c % 10;
        if ((kind == 0 || kind == 4 || kind == 5 || kind == 7) && !(HasCanvas(object) && HasMain(object))) kind = 2 + c % 2;
        F(kCanvasMsg).ret[9] = kind;
        b.push_back(Slot<uint32_t>(Face(object, 0x1f4), 3, &F(kCanvasMsg)) & 0xff);
        break;
    }
    case 43: { // the key target
        Fake &m = F(kKeyMsg);
        m.ret[9] = a % 4 == 0 ? 9u : 5 + a % 2, m.ret[10] = a % 3 ? 0x10 + (a >> 2) % 3 : c % 0x100, m.ret[11] = c,
        m.ret[12] = a;
        b.push_back(Slot<uint32_t>(Face(object, 0x204), 3, &F(kKeyMsg)) & 0xff);
        break;
    }
    case 44: { // the mouse target
        if (!HasCanvas(object)) return;
        static const uint32_t kTypes[] = {7, 8, 10, 11, 13, 14, 0x14, 0xd, 5, 6, 0x15, 0};
        Fake &m = F(kMouseMsg);
        m.ret[9] = kTypes[a % 12], m.ret[11] = c, m.ret[12] = a;
        m.value[4] = c % 3000, m.value[5] = (c >> 12) % 2000;
        b.push_back(Slot<uint32_t>(Face(object, 0x1fc), 3, &F(kMouseMsg)) & 0xff);
        break;
    }
    case 45: { // the targets' QueryInterface / AddRef / Release
        static const uint32_t kOffsets[3] = {0x1f4, 0x1fc, 0x204};
        void *t = Face(object, kOffsets[a % 3]);
        uint32_t q = 0x5e5e5e5e;
        b.push_back(Slot<uint32_t>(t, 0, c, &q) & 0xff);
        b.push_back(q);
        b.push_back(Slot<uint32_t>(t, 1));
        b.push_back(Slot<uint32_t>(t, 2));
        break;
    }
    case 46: { // the world changes
        gWorld.scaleFactor = c % 5 == 0 ? 0x5e5e5e5e : c % 5;
        gWorld.frameW32 = (c >> 3) % 4 != 0;
        gWorld.notifier = (c >> 5) % 6 != 0;
        gWorld.server = (c >> 7) % 3 != 0;
        gWorld.serverAnswers = (c >> 9) % 2 == 0;
        Fake &w = F(kRoot + a % kWindowCount);
        w.value[1] = c;
        w.value[5] ^= (a >> 4) % 3 == 0 ? 0x8000u : 0;
        w.ret[88] ^= 1, w.ret[86] ^= (a >> 8) & 1, w.ret[77] ^= (a >> 9) & 1, w.ret[26] ^= (a >> 10) & 1;
        w.ret[47] = (a >> 11) % 3 ? P(&F(kRoot + (c >> 13) % kWindowCount)) : 0;
        F(kBox).ret[3] = (a >> 14) % 5 ? P(&F(kRoot + (a >> 17) % kWindowCount)) : 0;
        F(kBox).ret[5] ^= (a >> 20) & 1;
        break;
    }
    default: // more of the window operations
        RunOp(b, object, {8 + (op.kind - 47) * 5 + a % 5, a, c});
        break;
    }
}

} // namespace winmgr_test

namespace winmgr_test {
void RunSeeds(difftest::Context &ctx, const char *name, uint32_t seeds);
}

using namespace winmgr_test;

DIFF_TEST(WinMgr_Run, 0x970ea0) { RunSeeds(ctx, "WinMgr_Run", 6000); }

// The cdecl helpers on every flag bit and every event type the manager maps (and others): 0x970f72, 0x970fad.
DIFF_TEST(WinMgr_Maps, 0x970f72)
{
    auto flags = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x970f72);
    auto types = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x970fad);
    for (uint32_t v = 0; v < 0x200; ++v) {
        ++ctx.cases;
        if (flags(v) != Mgr::MapMouseFlags(v)) ctx.Fail("MapMouseFlags(%x): orig %x ours %x", v, flags(v), Mgr::MapMouseFlags(v));
        if (types(v) != Mgr::MapEventType(v)) ctx.Fail("MapEventType(%x): orig %x ours %x", v, types(v), Mgr::MapEventType(v));
    }
    for (uint32_t v : {0x4348b111u, 0x4348b110u, 0x4348b112u, 0xffffffffu, 0x80000005u}) {
        ++ctx.cases;
        if (types(v) != Mgr::MapEventType(v)) ctx.Fail("MapEventType(%x): orig %x ours %x", v, types(v), Mgr::MapEventType(v));
        if (flags(v) != Mgr::MapMouseFlags(v)) ctx.Fail("MapMouseFlags(%x): orig %x ours %x", v, flags(v), Mgr::MapMouseFlags(v));
    }
}

void winmgr_test::RunSeeds(difftest::Context &ctx, const char *name, uint32_t seeds)
{
    // Also reached (see the file comment): 0x970d39 (factory), 0x972aab, 0x970f16, 0x972bcd, 0x970f50, 0x970f48, 0x972c94,
    // 0x972c8c, 0x973c78, 0x9717b7, 0x9717bf, 0x9714f4, 0x973692, 0x971955, 0x973e55, 0x9737eb, 0x971897, 0x973660,
    // 0x973806, 0x971333, 0x9735e8, 0x973d5f, 0x973d39, 0x97386a, 0x97381b, 0x973b83, 0x9739e6, 0x9738b7, 0x9739af,
    // 0x971302, 0x972cf6, 0x973cd4, 0x972d5f, 0x972ea1, 0x9737bf, 0x973670, 0x973d9f, 0x9713b7, 0x972cda, 0x973c9a,
    // 0x973442, 0x97340a, 0x971314, 0x973561, 0x973645, 0x97361f, 0x971823, 0x9734cb, 0x97338d, 0x97358a, 0x972f48,
    // 0x97377d, 0x973264, 0x9713be, 0x97163c, 0x971694, 0x9710b5, 0x971035, 0x9717c7, 0x971a55, 0x9719de, 0x973252,
    // 0x97375b, 0x972edd, 0x972e47, 0x973788, 0x973684, 0x972e14, 0x973f77, 0x973d0a, 0x972cb6, 0x9716fc, 0x97111c,
    // 0x9711d1, 0x971239, 0x970f72, 0x970fad, 0x970e7a.
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    for (uint32_t rva : {0x105f808u, 0x10ecc88u, 0x105f80cu, 0x105f810u}) globals.SetWord(rva, *GameAddress<uint32_t>(rva));
    // The stopwatch's overhead and mode scales (mode 4: microseconds at a 1 MHz counter).
    globals.SetWord(cRZStopwatch::kOverheadRva, 0);
    globals.SetWord(cRZStopwatch::kOverheadRva + 4, 0);
    for (uint32_t i = 0; i < 5; ++i) globals.SetWord(cRZStopwatch::kScalesRva + 4 * i, FloatBits(i == 2 ? 1.0f : 0.001f));
    gzlog_test::Clocks clocks;
    for (uint64_t t = 1000; t < 1000 + 40 * 777; t += 777) clocks.counter.push_back(t);
    gzlog_test::ClockScope clockScope(ctx, clocks);
    gzlog_test::Slots iat(ctx);
    iat.Set(0xd9a4b0, reinterpret_cast<void *>(&FakeShowCursor));
    iat.Set(0xd9a5e0, reinterpret_cast<void *>(&FakeClientToScreen));
    iat.Set(0xd9a4b4, reinterpret_cast<void *>(&FakeSetCursorPos));
    iat.Set(0xd9a144, reinterpret_cast<void *>(&FakeSleep));
    iat.Set(0xd9a578, reinterpret_cast<void *>(&FakePeekMessage));
    iat.Set(0xd9a580, reinterpret_cast<void *>(&FakeTranslateMessage));
    iat.Set(0xd9a57c, reinterpret_cast<void *>(&FakeDispatchMessage));
    iat.Set(0xd9a570, reinterpret_cast<void *>(&FakePostMessage));
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch rootCtor(0x92fded, shape::CodeOf(&RootCtor));
    shape::StandIn cursorCtor(0x9d0158, &cRZCursorAdapters::ConstructManager, &CursorCtor);
    shape::CodePatch boxCtor(0x952874, shape::CodeOf(&BoxCtor));
    shape::StandIn notifier(0x9c5ab4, &GetMessageServer, &NotifierStandIn);
    shape::StandIn server(0x9c5a89, &GetCachedMessageServer, &ServerStandIn);
    svcfx::gFrameworkIndex = 0; // the COM object: svcfx fake 0
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;

    bool failed = false;
    for (uint32_t seed = 1; seed <= seeds && !failed; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        // Ours is built first (its constructor reaches no fake and no clock), so both sides know both objects' ranges: a
        // random word (a window's bits, a message word) that happens to fall inside either object then normalizes the
        // same way on both sides, where one range per side made it differ by address alone (a flaky seed now and then).
        void *objects[2] = {nullptr, Build(ctx, true)};
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            clocks.Rewind();
            gInitRan = false;
            if (side == 0) objects[0] = Build(ctx, false);
            void *object = objects[side];
            svcfx::AddRange(object, Mgr::kSize, kTag);
            svcfx::AddRange(objects[1 - side], Mgr::kSize, kTag);
            gMgr = object;
            gNest = 0;
            Slot<uint32_t>(Face(object, 4), 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    // The last release destroys it; every fifth seed the +8 face's deleting destructor (flags 1) instead.
                    gMgr = nullptr;
                    if (seed % 5) b.push_back(Slot<uint32_t>(Face(object, 4), 2));
                    else b.push_back(Norm(Slot<uint32_t>(Face(object, 8), 3, 1u)));
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                CaptureFakes(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], name, seed)) {
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                failed = true;
                break;
            }
        }
    }
    *GameAddress<void *>(0x10c9378) = nullptr;
}
