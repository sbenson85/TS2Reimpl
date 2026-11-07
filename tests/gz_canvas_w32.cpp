// Differential tests for nGZGraphic4::cCanvasW32 (the window service) as a whole class (src/gz/cCanvasW32.h, lane ON):
// the creator 0x155c70, every slot of its three faces, Init, Shutdown and the destructor.
//
// One world per case, built alike for each side from the case's seed:
// - The window is a 0xdc-byte block of the case's bytes (static; a heap block for the ops that free it) with the side's
//   three vtable words (the original's 0xd9cc04 / 0xd9ccbc / 0xd9ccf4 or ours); every slot is called through the block's
//   own vtables, so each original slot runs against ours. Its three strings are constructed in place with the case's
//   texts (short and long), its hook vector holds counted fakes.
// - Every Win32 import the class makes is an impfx fake answering from the case (rectangles, styles, return values; the
//   styles are kept like a window's), so both sides see the same system. The monitor stub 0x154ae0 is stood in the same
//   way on both sides (it is the game's multi-monitor shim, still game code).
// - The framework (the service cell 0x10c9378) is a fake that answers the window service 0x23ea70a1 with another fake;
//   events: our real cConnectionManager in the cell (0x10c9708), listener fakes connected from the window; the attribute
//   registry 0x156a70 runs for real over a map placed at its static (RegistryScope).
// Compared: the log (every call on every fake and import with its arguments, the notes), each slot's result, every byte
// of the window but its vtable words, the statics Show keeps (0x10c96dc..0x10c96f0), every fake's count, the connections,
// the allocations.
//
// The window procedure (lane OQ): the original 0x157800 against cCanvasW32::WindowProc on the same window, a seeded
// stream of messages per case (every case of its jump tables, messages it leaves to DefWindowProcA, a window whose user
// data is not ours), through the message hooks (fakes that handle a message or pass it on) and the listeners of every
// event it sends (they note the event's class, its words and what its slots answer, and may throw: the unwinding path).
// Its helpers are reached through it, the procedure's own statics are set per case (TrackMouseEvent looked up or not),
// the EnumDisplayMonitors stub 0x154950 is stood in on both sides (it calls the side's monitor callback), and the
// framework answers the window service with a fake or the window itself.
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "drawfx.h"
#include "impfx.h"

#include "game/vtable.h"
#include "gz/cCanvasEvents.h"
#include "gz/cCanvasW32.h"
#include "gz/cConnectionManager.h"
#include "gz/MultiMon.h"

#include <intrin.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace nGZGraphic4;
using nGZEvent::cConnectionManager;
using nGZEvent::cConnectionManagerAdapters;
using nGZEvent::cEventAttributeMap;

namespace {

constexpr uint32_t kVTableRva = 0xd9cc04, kFace8Rva = 0xd9ccbc, kFaceCRva = 0xd9ccf4;
constexpr uint32_t kEventManagerCell = 0x10c9708;
constexpr uint32_t kRegistryMap = 0x10c96fc, kRegistryMapGuard = 0x10c9704, kRegistryOnce = 0x10c970c,
                   kRegistryHolderGuard = 0x10c9714;
constexpr uint32_t kStatics = 0x10c96dc, kStaticsSize = 0x18; // 0x10c96dc flag, 0x10c96e0 rect, 0x10c96f0 offset
constexpr uint32_t kFrameworkCell = 0x10c9378, kCodePageCache = 0xff23a4;
constexpr size_t kSize = cCanvasW32::kSize;
constexpr uint32_t kWindowTag = 0x7e000000, kDataTag = 0x7c000000, kConnTag = 0x7b000000, kHookTag = 0x7a000000;
// The procedure: the original, the EnumDisplayMonitors stub it calls, TrackMouseLeave's static (cell, guard).
constexpr uint32_t kProcRva = 0x157800, kEnumMonitorsRva = 0x154950, kTrackCell = 0x10c96f4, kTrackGuard = 0x10c96f8;
constexpr uint32_t kProcWindow = 0x4a0000a0, kForeignWindow = 0x4a0000ee;
constexpr uint32_t kIconCallbackRva = 0x155b30, kCallbackTag = 0x79000000; // Init's icon callback: the side's, one tag

// The original's slots, as its vtables hold them (checked by cCanvasW32_Create).
constexpr uint32_t kPrimarySlots[45] = {
    0x1569e0, 0x146a20, 0x156d00, 0x155e50, 0x147ce0, 0x156df0, 0x155b50, 0x377e0,  0x155740, 0x155a10, 0x377e0, 0x147cf0,
    0x156210, 0x155110, 0x155380, 0x157220, 0x156f00, 0x156f60, 0x157040, 0x157190, 0x1555b0, 0x157070, 0x156ee0, 0x1561a0,
    0x157720, 0x157750, 0x1556c0, 0x149cf0,  0x155470, 0x156d70, 0x1561f0, 0x149d00, 0x1560f0, 0x156160, 0x156140, 0x156120,
    0x156190, 0x1570a0, 0x155460, 0x156110,  0x155e40, 0x1556b0, 0x154f10, 0x154f20, 0x154d40};

// ---- fakes ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRefSlot{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kReleaseSlot{0, drawfx::Ret::Int, true, "Release", ""};

// The framework: 0 QueryInterface, 5 GetSystemService, 9 AddToTick, 17 IsTickEnabled, 18 ToggleTick, 19 Quit.
inline constexpr drawfx::Slot kFrameworkSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U,
    {3, drawfx::Ret::Int, true, "fw.GetSystemService", "uui"}, U, U, U,
    {1, drawfx::Ret::Int, true, "fw.AddToTick", "i"}, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "fw.IsTickEnabled", ""},
    {1, drawfx::Ret::Int, true, "fw.ToggleTick", "b"},
    {1, drawfx::Ret::Int, true, "fw.Quit", "u"},
};
inline constexpr drawfx::Iface kFrameworkIface{"cIGZFrameWork", 20, kFrameworkSlots};

// The window service as GetSystemService gives it (another canvas): 16 SetTopmost.
inline constexpr drawfx::Slot kCanvasServiceSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "cs.SetTopmost", "b"},
};
inline constexpr drawfx::Iface kCanvasServiceIface{"cICanvasW32Behavior", 17, kCanvasServiceSlots};

// The framework's window service (0x23ea70a1): 6 takes the window.
inline constexpr drawfx::Slot kWindowServiceSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, {1, drawfx::Ret::Int, true, "ws.SetWindow", "i"}};
inline constexpr drawfx::Iface kWindowServiceIface{"windowService", 7, kWindowServiceSlots};

// A monitor: 4 writes its rectangle.
inline constexpr drawfx::Slot kMonitorSlots[] = {kQI, kAddRefSlot, kReleaseSlot, U, {1, drawfx::Ret::Int, true, "mon.GetRect", "i"}};
inline constexpr drawfx::Iface kMonitorIface{"cDisplayMonitor", 5, kMonitorSlots};

// A message hook: 3 sees a message first (hwnd, message, wParam, lParam, the handled flag it writes).
inline constexpr drawfx::Slot kHookSlots[] = {kQI, kAddRefSlot, kReleaseSlot,
                                              {5, drawfx::Ret::Int, true, "hook.OnMessage", "iuuui"}};
inline constexpr drawfx::Iface kHookIface{"cIWindowHook", 4, kHookSlots};

// The caller's string: 17 Copy takes the title.
inline constexpr drawfx::Slot kStringSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "str.Copy", "S"},
};
inline constexpr drawfx::Iface kStringIface{"cIGZString", 18, kStringSlots};

inline constexpr drawfx::Slot kListenerSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot,
    {4, drawfx::Ret::Int, true, "OnEvent", "uuii"},
    {3, drawfx::Ret::Int, true, "OnQuery", "uui"},
};
inline constexpr drawfx::Iface kListenerIface{"cIAbstractBehavior", 5, kListenerSlots};

enum : uint32_t {
    kFrameworkId = 0xe0100, kWindowServiceId, kMonitorId, kStringId, kCanvasServiceId, kHookId0 = 0xe0110, kListenerId0 = 0xe0200
};
constexpr unsigned kListeners = 3, kHookFakes = 4;
constexpr uint32_t kListenEvents[kListeners] = {0x1b637474, 0x1b637482, 0x11};
constexpr uint32_t kHeardAs[kListeners] = {0x61, 0x62, 0x63};
// The events the procedure sends or asks (each listened to by some cases' listeners, heard as 0x70 + i / 0x80 + i).
constexpr unsigned kProcEventCount = 6;
constexpr uint32_t kProcEvents[kProcEventCount] = {cCanvasW32::kCanvasEvent, cCanvasW32::kSizeMoveEvent, cCanvasW32::kMouseEvent,
                                                   cCanvasW32::kKeyEvent,    cCanvasW32::kCloseQuery,    cCanvasW32::kUserMessageEvent};

drawfx::Fake gFramework, gWindowService, gMonitor, gString, gCanvasService, gHook[kHookFakes], gListener[kListeners], gSource;
bool gWasLong[3]; // the window's strings were long before the run (a destroyed one keeps its stale pointer bytes)

struct Data {
    uint32_t words[8];   // event data
    uint32_t minmax[10]; // WM_GETMINMAXINFO's MINMAXINFO
    int32_t sizing[4];   // WM_SIZING's rectangle
} gData;

// ---- the blocks ----
alignas(16) uint8_t gWindowBlock[kSize];
uint8_t *gWindow = gWindowBlock;

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }
uint32_t Norm(const void *p) { return drawfx::Norm(Word(p)); }
template <class T> void Put(void *block, size_t offset, T value) { std::memcpy(static_cast<uint8_t *>(block) + offset, &value, sizeof(T)); }
template <class T> T Get(const void *block, size_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t *>(block) + offset, sizeof(T));
    return value;
}

// A slot called through the object's own vtable (the side's).
template <class R, class... A> R Call(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void *const *const *>(object))[slot])(object, args...);
}

// ---- a case ----
enum Op : uint32_t {
    kQueryInterface, kFaceRefs, kAddRef, kRelease, kOnQuery, kNotify, kAsk, kConnect, kDisconnect, kIsActive,
    kEventCount, kEventIds, kEventAttribute, kEmptySlots, kOnEvent, kShow, kTopmost, kTitle, kSizes, kPower, kPaint,
    kGetters, kFullscreen, kFlag19, kGetTitle, kHooks, kServiceSlots, kInit, kShutdown, kDestruct, kReleaseLast, kDelete,
    kProc, kOpCount
};
const char *const kOpNames[kOpCount] = {
    "QueryInterface", "face refs", "AddRef", "Release", "OnQuery", "NotifyListeners", "AskListeners", "Connect",
    "Disconnect", "IsActive", "GetEventCount", "GetEventIds", "GetEventAttribute", "Slot7/Slot10", "OnEvent", "Show",
    "SetTopmost", "SetTitle", "sizes", "power", "paint", "getters", "SetFullscreen", "SetFlag19", "GetTitle", "hooks",
    "service slots", "Init", "Shutdown", "destructor", "Release (last)", "delete", "procedure"};
bool WindowGone(uint32_t op) { return op == kReleaseLast || op == kDelete; }
bool Destroys(uint32_t op) { return op == kDestruct || op == kReleaseLast || op == kDelete; }

constexpr unsigned kTexts = 6;
const char *const kTextPool[kTexts] = {"", "Sims", "exactly 15 char", "sixteen chars ok", "a rather long window title for the heap",
                                       "x"};

struct Case {
    uint32_t op;
    uint8_t window[kSize];
    uint32_t titleText[3];         // the strings' texts (an index into the pool)
    uint32_t hookCount, hookSlack; // how many hooks the vector holds, free room after them
    uint32_t hookFirst;            // which fake the first hook is
    uint32_t hookStep;             // 1: each hook is the next fake; 0: all the same one (adjacent equal entries)
    uint32_t connections, listenerAnswers;
    uint32_t style, exStyle;       // the HWND's styles as the system has them
    uint32_t staticFlag;           // 0x10c96dc
    uint32_t staticRect[4];        // 0x10c96e0
    uint32_t staticOffset;         // 0x10c96f0
    uint32_t localeText;           // which text GetLocaleInfoA gives
    Data data;
    uint32_t args[12];
    // The procedure's stream (kProc): the messages, to the window (target 0) or a window that is not ours.
    static constexpr unsigned kMaxMessages = 8;
    uint32_t messages;
    uint32_t message[kMaxMessages], wParam[kMaxMessages], lParam[kMaxMessages], target[kMaxMessages];
    uint32_t procConnections; // bit i: a listener of kProcEvents[i]; bit 6 + i: a second one
    uint32_t serviceIsWindow; // GetSystemService gives the window itself (else the canvas service fake)
    uint32_t trackState;      // TrackMouseLeave's static: 0 built (TrackMouseEvent), 1 built (none), 2 not built
    uint32_t throwAt;         // the listener or SetTopmost call that throws (counted from 1; 0: none)
};

struct Script {
    const Case *c = nullptr;
    uint32_t rectCalls = 0, style = 0, exStyle = 0;
    uint32_t calls = 0;       // the procedure's fakes' answers, in call order
    uint32_t throwPoints = 0; // the calls that may throw so far
} gScript;

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

// ---- handlers ----
uint32_t Arg(unsigned i) { return gScript.c->args[i % 12]; }

// A rectangle from the case's words (always a positive size most of the time; the left and top may be negative).
void FillRect4(uint32_t *out, unsigned seed)
{
    const int32_t left = int32_t(Arg(seed) % 700) - 100, top = int32_t(Arg(seed + 1) % 500) - 50;
    out[0] = uint32_t(left);
    out[1] = uint32_t(top);
    out[2] = uint32_t(left + int32_t(Arg(seed + 2) % 1500));
    out[3] = uint32_t(top + int32_t(Arg(seed + 3) % 1100));
}

uint32_t GetWindowRectH(drawfx::Fake &, const uint32_t *a)
{
    FillRect4(reinterpret_cast<uint32_t *>(uintptr_t(a[1])), 2 * gScript.rectCalls++);
    return Arg(8) % 5 != 0;
}
uint32_t GetClientRectH(drawfx::Fake &, const uint32_t *a)
{
    FillRect4(reinterpret_cast<uint32_t *>(uintptr_t(a[1])), 5);
    return 1;
}
uint32_t ClientToScreenH(drawfx::Fake &, const uint32_t *a)
{
    int32_t *const p = reinterpret_cast<int32_t *>(uintptr_t(a[1]));
    p[0] += int32_t(Arg(9) % 300);
    p[1] += int32_t(Arg(10) % 200);
    return 1;
}
// The procedure's fakes draw their answers in call order (the same order on both sides when they agree).
uint32_t Draw()
{
    const uint32_t i = gScript.calls++;
    return ((Arg(i) ^ (i * 0x9e3779b9u)) * 0x85ebca6bu) >> 8;
}

bool IsProc(const Case &c) { return c.op == kProc; }

uint32_t GetWindowLongH(drawfx::Fake &, const uint32_t *a)
{
    if (int32_t(a[1]) == -0x10) return gScript.style;
    if (int32_t(a[1]) == -0x14) return gScript.exStyle;
    // The user data: the window (Init stores it) for the procedure's own window.
    if (int32_t(a[1]) == -0x15) return gScript.c && IsProc(*gScript.c) && a[0] != kForeignWindow ? Word(gWindow) : 0;
    return 0;
}
uint32_t SetWindowLongH(drawfx::Fake &, const uint32_t *a)
{
    uint32_t *slot = int32_t(a[1]) == -0x10 ? &gScript.style : int32_t(a[1]) == -0x14 ? &gScript.exStyle : nullptr;
    if (!slot) return 0;
    const uint32_t old = *slot;
    *slot = a[2];
    return old;
}
uint32_t AdjustRectH(drawfx::Fake &, const uint32_t *a)
{
    int32_t *const r = reinterpret_cast<int32_t *>(uintptr_t(a[0]));
    r[0] -= 4;
    r[1] -= 30;
    r[2] += 4;
    r[3] += 4;
    return 1;
}
uint32_t SpiH(drawfx::Fake &, const uint32_t *a)
{
    // The getters write a DWORD through the pointer; the setters take the value.
    if (a[0] == 0x72 || a[0] == 0x101a || a[0] == 0x5e || a[0] == 0x101c)
        *reinterpret_cast<uint32_t *>(uintptr_t(a[2])) = a[0] == 0x72 ? Arg(2) % 2 : Arg(a[0] & 7) % 1000;
    const bool ok = Arg(a[0] + 3) % 3 != 0;
    // The screen saver's timeout (0xe) and the wheel's lines (0x68) are written only by a call that works (the
    // procedure starts them at 0 / 1); the lines include WHEEL_PAGESCROLL (-1) and 0.
    if (ok && (a[0] == 0xe || a[0] == 0x68)) {
        const uint32_t pick = Draw();
        static const uint32_t lines[] = {3, 0, 0xffffffffu, 1, 7, 100000};
        *reinterpret_cast<uint32_t *>(uintptr_t(a[2])) =
            a[0] == 0x68 ? (pick % 3 ? lines[pick % 6] : pick >> 4) : (pick % 4 ? pick % 300 : uint32_t(-int32_t(pick % 50)));
    }
    return ok;
}
uint32_t LocaleH(drawfx::Fake &, const uint32_t *a)
{
    // Code pages: the string encoding (8), 0 (UTF-16 after the procedure's fix-up), single and double byte ones.
    static const char *const texts[] = {"1252", "0", "", "932", "abc", "-5", "8", "949"};
    std::strcpy(reinterpret_cast<char *>(uintptr_t(a[2])), texts[(gScript.c->localeText + gScript.calls++) % 8]);
    return 5;
}
struct VTables {
    void *primary, *face8, *faceC;
    void *proc; // the window procedure Init hands the window class (0x157800 / ours)
    void *icon; // the icon callback Init hands EnumResourceNamesA (0x155b30 / ours)
};
const VTables *gSide = nullptr; // the side being run
uint32_t RegisterClassH(drawfx::Fake &, const uint32_t *a)
{
    const auto *wc = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    // cbSize, style, the procedure (1: the side's own), extra bytes (2), instance, icon, cursor, background, menu, small
    // icon; the class name's text.
    const uint32_t proc = wc[2] == Word(gSide->proc) ? 1 : wc[2];
    drawfx::Note("wndclass", {wc[0], wc[1], proc, wc[3], wc[4], wc[5], wc[6], wc[7], wc[8], wc[9], wc[11]});
    drawfx::Note("class name", uint32_t(*reinterpret_cast<const char *>(uintptr_t(wc[10]))));
    return Arg(11) % 8 ? 0xc001 + Arg(11) % 7 : 0;
}
uint32_t Value(unsigned i) { return Arg(i) % 3 ? 0x4a000000u + Arg(i) % 0x40 * 4 : 0; }
uint32_t GetDCH(drawfx::Fake &, const uint32_t *) { return Value(3); }
uint32_t IsIconicH(drawfx::Fake &, const uint32_t *) { return Arg(4) % 2; }
uint32_t IsZoomedH(drawfx::Fake &, const uint32_t *) { return Arg(5) % 2; }
uint32_t GetAncestorH(drawfx::Fake &, const uint32_t *) { return 0x4a0000f0 + Arg(6) % 2 * 4; }
uint32_t GetActiveH(drawfx::Fake &, const uint32_t *) { return Arg(7) % 2 ? 0x4a0000f4u : 0x4a0000f8u; }
uint32_t CreateWindowH(drawfx::Fake &, const uint32_t *) { return Arg(8) % 6 ? 0x4a0000a0u + Arg(0) % 4 * 4 : 0; }
uint32_t Returns01(drawfx::Fake &, const uint32_t *) { return Arg(1) % 2; }
uint32_t SetPosH(drawfx::Fake &, const uint32_t *) { return Arg(0) % 3; }
uint32_t BrushH(drawfx::Fake &, const uint32_t *) { return Arg(2) % 4 ? 0x4a0000b0u : 0; }
uint32_t LayoutH(drawfx::Fake &, const uint32_t *) { return 0x04090000u | Arg(7) % 0x500; }

// ---- the events the window sends: their classes (the game's vtable, or ours for the first four) ----
struct EventClass {
    const char *label;
    uint32_t vtable; // the game's
    uint32_t size;
};
constexpr unsigned kEventClasses = 6, kOurEventClasses = 4;
const EventClass kEventClass[kEventClasses] = {
    {"canvas event", cCanvasEvent::kGameVTable, 8},     {"size/move event", cCanvasSizeMoveEvent::kGameVTable, 0x18},
    {"mouse event", cMouseEvent::kGameVTable, 0x20},    {"key event", cKeyEvent::kGameVTable, 0x14},
    {"scalar event", 0xd9cb2c, 8}, // cScalarEvent<unsigned long> (message 0x702; the game's class)
    {"null event", 0xd9ca78, 4},   // nGZEvent::cNullEvent (the close query; the game's class)
};

// Our classes' vtables (a constructed event's).
const void *OurEventVTable(unsigned k)
{
    static const void *vtables[kOurEventClasses] = {};
    if (!vtables[0]) {
        const cCanvasEvent canvas(0);
        const cCanvasSizeMoveEvent sizeMove(cCanvasRect{0, 0, 0, 0}, 0);
        const cMouseEvent mouse(0, 0, 0, 0, false, 0, 0.0f);
        const cKeyEvent key(0, 0, 0, 0);
        const void *const objects[kOurEventClasses] = {&canvas, &sizeMove, &mouse, &key};
        for (unsigned i = 0; i < kOurEventClasses; ++i) vtables[i] = Get<const void *>(objects[i], 0);
    }
    return vtables[k];
}

// The class of an event by its vtable word (the game's or ours alike); -1: not an event of the window's.
int ClassOf(uint32_t vtable)
{
    for (unsigned k = 0; k < kEventClasses; ++k)
        if (vtable == Word(GameAddress<void>(kEventClass[k].vtable)) || (k < kOurEventClasses && vtable == Word(OurEventVTable(k))))
            return int(k);
    return -1;
}

// An event's words past its vtable; the mouse event's three bytes after its flag (+0x15) masked: no one writes them
// (the original builds it on its stack: 0x1566c0 `sete byte ptr [ebp - 0x18]`) and no one reads them (its copies take
// the byte alone: 0x1550d0, 0x155970).
void PushBody(std::vector<uint32_t> &words, const void *event, unsigned k)
{
    for (uint32_t at = 4; at < kEventClass[k].size; at += 4) {
        uint32_t w = Get<uint32_t>(event, at);
        if (k == 2 && at == 0x14) w &= 0xff;
        words.push_back(w);
    }
}

void NoteWords(const char *label, const std::vector<uint32_t> &words)
{
    drawfx::Call c;
    c.note = label;
    c.words = words;
    drawfx::gLog.push_back(std::move(c));
}

// A listener or service call that throws when the case says (the procedure unwinds through its frame).
void MaybeThrow()
{
    if (gScript.c->throwAt && ++gScript.throwPoints == gScript.c->throwAt) throw 42;
}

// The listener of an event: what the window sent (the event's class, its words) and, for the window's own event
// classes, what the event's slots answer (its type, sizes, a copy built in caller memory).
uint32_t ListenerOnEvent(drawfx::Fake &, const uint32_t *a)
{
    void *const data = reinterpret_cast<void *>(uintptr_t(a[2]));
    if (a[0] == cCanvasW32::kConnectionChangedEvent || !data) return 0;
    const int k = ClassOf(Get<uint32_t>(data, 0));
    if (k < 0) return 0;
    std::vector<uint32_t> words{a[0]};
    PushBody(words, data, unsigned(k));
    NoteWords(kEventClass[k].label, words);
    if (unsigned(k) < kOurEventClasses) {
        alignas(8) uint8_t copy[0x28];
        std::memset(copy, 0xcd, sizeof(copy));
        std::vector<uint32_t> slots{Call<uint32_t>(data, 3), Call<uint32_t>(data, 4), Call<uint32_t>(data, 5)};
        // The copy's result is not compared: the game's slot leaves the last word copied in eax (0x155900
        // `mov eax, dword ptr [ecx + 4]; mov dword ptr [edx + 4], eax; ret 4`), no caller reads it.
        Call<void *>(data, 6, static_cast<void *>(copy));
        slots.push_back(uint32_t(ClassOf(Get<uint32_t>(copy, 0))));
        PushBody(slots, copy, unsigned(k));
        slots.push_back(Get<uint32_t>(copy, kEventClass[k].size)); // nothing past it
        NoteWords("event slots", slots);
    }
    MaybeThrow();
    return 0;
}
uint32_t ListenerOnQuery(drawfx::Fake &self, const uint32_t *a)
{
    // The close query's event (the game's cNullEvent on both sides).
    void *const data = reinterpret_cast<void *>(uintptr_t(a[2]));
    if (data && a[0] >= 0x70 && a[0] < 0x90) {
        const int k = ClassOf(Get<uint32_t>(data, 0));
        drawfx::Note("asked", {a[0], uint32_t(k)});
    }
    return self.value[0];
}
// The window while the world connects it (an embedded fake answering the behaviour type with the block).
uint32_t SourceQuery(drawfx::Fake &self, const uint32_t *a)
{
    if (a[0] != nGZEvent::cIAbstractBehaviorType::kIid || !a[1]) return 0;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = self.owner;
    return 1;
}
uint32_t MonitorRect(drawfx::Fake &, const uint32_t *a)
{
    FillRect4(reinterpret_cast<uint32_t *>(uintptr_t(a[0])), 4);
    return a[0];
}
// The multi-monitor shim stood in for both sides.
uint32_t __stdcall MonitorFromWindowStandIn(void *hwnd, uint32_t flags)
{
    drawfx::Note("MonitorFromWindow", {Norm(hwnd), flags});
    if (gScript.c && IsProc(*gScript.c) && Draw() % 4 == 0) return 0; // no monitor (the paint then skips the others)
    return 0x4a0000c0u + Arg(0) % 4;
}

// ---- the procedure's fakes ----
// The EnumDisplayMonitors stub (0x154950, multimon.h's, game code) stood in on both sides: the side's callback gets
// up to three monitors (one may be the window's), each with its rectangle; what the callback's data holds is noted.
BOOL __stdcall EnumMonitorsStandIn(HDC dc, const RECT *clip, MONITORENUMPROC proc, LPARAM data)
{
    const auto *const pair = reinterpret_cast<const uint32_t *>(data);
    drawfx::Note("EnumDisplayMonitors", {Norm(dc), Word(clip), pair[0], pair[1]});
    const uint32_t count = Draw() % 4;
    for (uint32_t i = 0; i < count; ++i) {
        RECT rect = {LONG(i * 640), LONG(i % 2 * 20), LONG(i * 640 + 640), LONG(480 + i)};
        const HMONITOR monitor = reinterpret_cast<HMONITOR>(uintptr_t(0x4a0000c0u + (Draw() + i) % 4));
        drawfx::Note("monitor callback", uint32_t(proc(monitor, dc, &rect, data)));
    }
    return Draw() % 2;
}
// TrackMouseEvent as GetProcAddress gives it.
BOOL WINAPI TrackMouseEventStandIn(LPTRACKMOUSEEVENT request)
{
    drawfx::Note("TrackMouseEvent", {request->cbSize, request->dwFlags, Norm(request->hwndTrack), request->dwHoverTime});
    return Draw() % 2;
}
uint32_t GetProcAddressH(drawfx::Fake &, const uint32_t *a)
{
    const char *const name = reinterpret_cast<const char *>(uintptr_t(a[1]));
    if (a[1] < 0x10000 || std::strcmp(name, "TrackMouseEvent") != 0) {
        // The image's own CRT (its exception handling looks up the fiber-local storage functions when a listener
        // throws through the original): the real lookup, unlogged, as the harness gives it.
        drawfx::gLog.pop_back();
        return Word(reinterpret_cast<const void *>(::GetProcAddress(reinterpret_cast<HMODULE>(uintptr_t(a[0])), name)));
    }
    return Draw() % 4 ? Word(reinterpret_cast<const void *>(&TrackMouseEventStandIn)) : 0;
}
// The framework's services: the window service is the canvas service fake or the window itself (counted either way).
uint32_t FrameworkService(drawfx::Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if (gScript.c->serviceIsWindow) {
        Call<uint32_t>(gWindow, 1);
        *out = gWindow;
    } else {
        ++gCanvasService.refs;
        *out = &gCanvasService;
    }
    return 1;
}
// The tick: a bool in al (the word's other bytes set in some answers).
uint32_t TickEnabledH(drawfx::Fake &, const uint32_t *)
{
    const uint32_t pick = Draw();
    return pick % 3 ? 1 : pick % 2 ? 0x100 : 0;
}
uint32_t CanvasServiceTopmost(drawfx::Fake &, const uint32_t *)
{
    MaybeThrow();
    return Draw() % 2;
}
// A hook: handles the message (writes true, its result is the procedure's) or passes it on (writes false).
uint32_t HookOnMessage(drawfx::Fake &, const uint32_t *a)
{
    const uint32_t pick = Draw();
    *reinterpret_cast<uint8_t *>(uintptr_t(a[4])) = pick % 5 == 0;
    return 0x6000 + pick % 0x100;
}
uint32_t BeginPaintH(drawfx::Fake &, const uint32_t *a)
{
    // Every byte of the PAINTSTRUCT (hdc, fErase, rcPaint, fRestore, fIncUpdate, rgbReserved).
    auto *const paint = reinterpret_cast<PAINTSTRUCT *>(uintptr_t(a[1]));
    const uint32_t pick = Draw();
    const HDC dc = pick % 5 ? reinterpret_cast<HDC>(uintptr_t(0x4a000090u + pick % 3 * 4)) : nullptr;
    paint->hdc = dc;
    paint->fErase = pick >> 3 & 1;
    paint->rcPaint = {LONG(pick % 7), LONG(pick % 11), LONG(100 + pick % 13), LONG(80 + pick % 17)};
    paint->fRestore = pick >> 4 & 1;
    paint->fIncUpdate = pick >> 5 & 1;
    for (unsigned i = 0; i < sizeof(paint->rgbReserved); ++i) paint->rgbReserved[i] = uint8_t(pick >> (i % 24));
    return Word(dc);
}
uint32_t EndPaintH(drawfx::Fake &, const uint32_t *a)
{
    // The PAINTSTRUCT BeginPaint filled, as the procedure hands it back.
    const auto *const paint = reinterpret_cast<const PAINTSTRUCT *>(uintptr_t(a[1]));
    drawfx::Note("EndPaint's paint", {Word(paint->hdc), uint32_t(paint->fErase), uint32_t(paint->rcPaint.right),
                                      uint32_t(paint->fIncUpdate), paint->rgbReserved[31]});
    return 1;
}
uint32_t DefWindowProcH(drawfx::Fake &, const uint32_t *) { return Draw(); }
// A key's state: the sign bit of the SHORT for "held", other bits and the word's high half set in some answers.
uint32_t AsyncKeyH(drawfx::Fake &, const uint32_t *)
{
    const uint32_t pick = Draw();
    static const uint32_t answers[] = {0, 0x8000, 1, 0x8001, 0x12340000, 0xffff7fff, 0xffff8000};
    return answers[pick % 7];
}
uint32_t ScreenToClientH(drawfx::Fake &, const uint32_t *a)
{
    auto *const p = reinterpret_cast<int32_t *>(uintptr_t(a[1]));
    p[0] -= int32_t(Arg(9) % 300);
    p[1] -= int32_t(Arg(10) % 200);
    return 1;
}
uint32_t SetCaptureH(drawfx::Fake &, const uint32_t *) { return Draw() % 2 ? kProcWindow : 0; }
// Init's icon: the resource names, each handed to the side's callback (0x155b30 / ours) until it stops.
char gIconName[] = "SIMS2ICON";
uint32_t EnumResourceNamesH(drawfx::Fake &, const uint32_t *a)
{
    const auto proc = reinterpret_cast<ENUMRESNAMEPROCA>(uintptr_t(a[2]));
    const uint32_t names = Arg(6) % 3;
    for (uint32_t i = 0; i < names; ++i) {
        const LPSTR name = i == 0 && Arg(7) % 2 ? MAKEINTRESOURCEA(101) : gIconName;
        const BOOL more = proc(reinterpret_cast<HMODULE>(uintptr_t(a[0])), reinterpret_cast<LPCSTR>(uintptr_t(a[1])), name,
                               LONG_PTR(a[3]));
        drawfx::Note("icon callback", uint32_t(more));
        if (!more) return 0;
    }
    return 1;
}
uint32_t LoadIconH(drawfx::Fake &, const uint32_t *) { return Arg(5) % 4 ? 0x4a000070u + Arg(5) % 3 * 4 : 0; }
uint32_t FrameworkQuery(drawfx::Fake &self, const uint32_t *a)
{
    for (unsigned i = 0; i < self.faceCount; ++i)
        if (self.faces[i].iid == a[0]) {
            ++self.faces[i].fake->refs;
            *reinterpret_cast<void **>(uintptr_t(a[1])) = self.faces[i].fake;
            return 1;
        }
    return 0;
}

// ---- statics: the registry map placed at its static, the cells ----
char gNames[3][8];
const uint32_t kRegistryIds[] = {0x10, 0x1b637474, 0xa0000000};

class RegistryScope {
public:
    RegistryScope()
    {
        std::memcpy(saved_, GameAddress<void>(kRegistryMap), sizeof(saved_));
        savedMapGuard_ = *GameAddress<int32_t>(kRegistryMapGuard);
        savedOnce_ = *GameAddress<uint8_t>(kRegistryOnce);
        savedHolderGuard_ = *GameAddress<int32_t>(kRegistryHolderGuard);
        map_ = ::new (GameAddress<void>(kRegistryMap)) cEventAttributeMap;
        *GameAddress<int32_t>(kRegistryMapGuard) = INT_MIN;
        *GameAddress<uint8_t>(kRegistryOnce) = 1;
        *GameAddress<int32_t>(kRegistryHolderGuard) = INT_MIN;
        for (unsigned i = 0; i < 3; ++i) {
            std::snprintf(gNames[i], sizeof(gNames[i]), "c%u", i);
            (*map_)[kRegistryIds[i]] = {kRegistryIds[i], gNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
        }
    }
    ~RegistryScope()
    {
        std::destroy_at(map_);
        std::memcpy(GameAddress<void>(kRegistryMap), saved_, sizeof(saved_));
        *GameAddress<int32_t>(kRegistryMapGuard) = savedMapGuard_;
        *GameAddress<uint8_t>(kRegistryOnce) = savedOnce_;
        *GameAddress<int32_t>(kRegistryHolderGuard) = savedHolderGuard_;
    }
    RegistryScope(const RegistryScope &) = delete;
    RegistryScope &operator=(const RegistryScope &) = delete;

private:
    uint8_t saved_[sizeof(cEventAttributeMap)];
    int32_t savedMapGuard_;
    uint8_t savedOnce_;
    int32_t savedHolderGuard_;
    cEventAttributeMap *map_;
};

class Cells {
public:
    Cells()
        : manager_(*GameAddress<uint32_t>(kEventManagerCell)), framework_(*GameAddress<uint32_t>(kFrameworkCell)),
          codePage_(*GameAddress<uint32_t>(kCodePageCache))
    {
        std::memcpy(statics_, GameAddress<void>(kStatics), kStaticsSize);
        *GameAddress<uint32_t>(kCodePageCache) = 1252; // the cached ANSI code page (no GetACP call)
    }
    ~Cells()
    {
        *GameAddress<uint32_t>(kEventManagerCell) = manager_;
        *GameAddress<uint32_t>(kFrameworkCell) = framework_;
        *GameAddress<uint32_t>(kCodePageCache) = codePage_;
        std::memcpy(GameAddress<void>(kStatics), statics_, kStaticsSize);
    }
    Cells(const Cells &) = delete;
    Cells &operator=(const Cells &) = delete;

private:
    uint32_t manager_, framework_, codePage_;
    uint8_t statics_[kStaticsSize];
};

struct StandIns {
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    // Both stubs are ours since lane OS (src/gz/MultiMon.cpp): stood in over the image's and over ours.
    shape::StandIn monitor{0x154ae0, &nMultiMon::MonitorFromWindowStub, &MonitorFromWindowStandIn};
    shape::StandIn monitors{kEnumMonitorsRva, &nMultiMon::EnumDisplayMonitorsStub, &EnumMonitorsStandIn};
};

// TrackMouseLeave's static (the cell and its guard) and the thread's statics epoch (word 1 of the image's TLS block,
// -1 for the cases that start the static unbuilt), restored when the world goes.
class TrackStatic {
public:
    TrackStatic()
        : cell_(*GameAddress<uint32_t>(kTrackCell)), guard_(*GameAddress<int32_t>(kTrackGuard))
    {
        auto **array = reinterpret_cast<int **>(__readfsdword(0x2c));
        tls_ = array[*GameAddress<uint32_t>(0x10df814)];
        epoch_ = tls_[1];
    }
    ~TrackStatic()
    {
        *GameAddress<uint32_t>(kTrackCell) = cell_;
        *GameAddress<int32_t>(kTrackGuard) = guard_;
        tls_[1] = epoch_;
    }
    TrackStatic(const TrackStatic &) = delete;
    TrackStatic &operator=(const TrackStatic &) = delete;
    // 0: built, TrackMouseEvent found; 1: built, not found; 2: not built yet (the cell's word stale).
    void Set(uint32_t state)
    {
        tls_[1] = state == 2 ? -1 : epoch_;
        *GameAddress<int32_t>(kTrackGuard) = state == 2 ? 0 : INT_MIN;
        *GameAddress<uint32_t>(kTrackCell) =
            state == 0 ? Word(reinterpret_cast<const void *>(&TrackMouseEventStandIn)) : state == 2 ? 0x5eedc0deu : 0;
    }
    // Whether it is built (the guard's value is the CRT's epoch, not compared) and what the cell holds.
    static void Note()
    {
        const uint32_t cell = *GameAddress<uint32_t>(kTrackCell);
        drawfx::Note("track static", {*GameAddress<int32_t>(kTrackGuard) != 0,
                                      cell == Word(reinterpret_cast<const void *>(&TrackMouseEventStandIn)) ? 1u : cell});
    }

private:
    uint32_t cell_;
    int32_t guard_;
    int *tls_;
    int epoch_;
};

// ---- seeding ----
bool Pick(std::mt19937 &gen, unsigned oneIn) { return gen() % oneIn == 0; }

// The messages the procedure handles (every case of its jump tables and its compares) and some it leaves to
// DefWindowProcA (around and between them).
const uint32_t kHandledMessages[] = {
    WM_MOVE, WM_SIZE, WM_ACTIVATE, WM_PAINT, WM_CLOSE, WM_ERASEBKGND, WM_SETTINGCHANGE, WM_ACTIVATEAPP, WM_GETMINMAXINFO,
    WM_INPUTLANGCHANGE, WM_DISPLAYCHANGE, WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_DEADCHAR, WM_SYSKEYDOWN, WM_SYSKEYUP, WM_TIMER,
    WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK, WM_RBUTTONDOWN, WM_RBUTTONUP, WM_RBUTTONDBLCLK,
    WM_MBUTTONDOWN, WM_MBUTTONUP, WM_MBUTTONDBLCLK, WM_MOUSEWHEEL, WM_SIZING, WM_CAPTURECHANGED, WM_ENTERSIZEMOVE,
    WM_EXITSIZEMOVE, WM_MOUSELEAVE, 0x702};
const uint32_t kOtherMessages[] = {0,     1,     2,     4,     7,     8,     0x11,  0x20,  0x46,  0x7d,  0x7f,  0xff,
                                   0x106, 0x112, 0x114, 0x1ff, 0x20b, 0x20e, 0x213, 0x216, 0x230, 0x233, 0x2a2, 0x2a4,
                                   0x400, 0x701, 0x703};

// A message's wParam and lParam, as the system would send them (and some it would not).
void SeedMessage(std::mt19937 &gen, uint32_t message, uint32_t &w, uint32_t &l)
{
    w = gen();
    l = Pick(gen, 2) ? (gen() % 900) | (gen() % 700) << 16 : gen();
    switch (message) {
    case WM_SIZE: w = gen() % 6; break;
    case WM_ACTIVATE: w = gen() % 3 | (Pick(gen, 2) ? (gen() % 3) << 16 : 0); break;
    case WM_ERASEBKGND: w = 0x4a000090u; break;
    case WM_SETTINGCHANGE: {
        const uint32_t actions[] = {0x5d, 0x101b, 0x101d, 0x5e, 0x2f, 0x101a};
        w = Pick(gen, 6) ? gen() : actions[gen() % 6];
        break;
    }
    case WM_ACTIVATEAPP: w = Pick(gen, 3) ? gen() : gen() % 2; break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP: w = Pick(gen, 6) ? gen() : gen() % 0x100; break;
    case WM_CHAR: {
        // Text, lead and trail bytes of double-byte code pages, now and then a wide value (only its low byte is kept).
        const uint32_t pick = gen() % 4;
        w = pick == 0 ? 0x20 + gen() % 0x5f : pick == 1 ? 0x81 + gen() % 0x7c : pick == 2 ? gen() % 0x100 : gen() % 0x10000;
        break;
    }
    case WM_MOUSEWHEEL: {
        const int16_t deltas[] = {120, -120, 240, -360, 1, 0};
        const uint16_t delta = Pick(gen, 3) ? uint16_t(gen()) : uint16_t(deltas[gen() % 6]);
        w = uint32_t(delta) << 16 | (Pick(gen, 4) ? gen() % 0x10000 : gen() % 0x40);
        break;
    }
    case WM_SIZING: w = gen() % 10; break;
    default:
        if (message >= WM_MOUSEMOVE && message <= WM_MBUTTONDBLCLK) w = Pick(gen, 4) ? gen() : gen() % 0x40;
        break;
    }
}

void SeedProc(std::mt19937 &gen, Case &c)
{
    // The window as Init leaves it, mostly: active (its listeners hear), input on, a known code page.
    if (!Pick(gen, 5)) Put<uint32_t>(c.window, 4, Get<uint32_t>(c.window, 4) | 0x80000000u);
    c.window[0x19] = uint8_t(Pick(gen, 5));
    const uint32_t codePages[] = {8, 0, 0x10, 1252, 932, 949, 0xffffffffu};
    Put<uint32_t>(c.window, 0xb4, Pick(gen, 8) ? gen() : codePages[gen() % 7]);
    Put<uint32_t>(c.window, 0x40, Pick(gen, 4) ? gen() : gen() % 3);
    Put<uint32_t>(c.window, 0x14, Pick(gen, 4) ? gen() : gen() % 0x40);
    if (Pick(gen, 2)) c.titleText[gen() % 2] = Pick(gen, 2) ? 0 : 5; // OnChar's strings empty or one byte
    c.connections = 0;
    c.procConnections = gen() % 4096;
    c.serviceIsWindow = Pick(gen, 3);
    c.trackState = gen() % 3;
    c.throwAt = Pick(gen, 6) ? 1 + gen() % 6 : 0;
    c.messages = 1 + gen() % Case::kMaxMessages;
    for (unsigned i = 0; i < c.messages; ++i) {
        c.message[i] = Pick(gen, 6) ? (Pick(gen, 3) ? gen() : kOtherMessages[gen() % std::size(kOtherMessages)])
                                    : kHandledMessages[gen() % std::size(kHandledMessages)];
        c.target[i] = Pick(gen, 12);
        SeedMessage(gen, c.message[i], c.wParam[i], c.lParam[i]);
    }
    for (uint32_t &w : c.data.minmax) w = gen();
    for (int32_t &w : c.data.sizing) w = int32_t(gen() % 2000) - 300;
}

void SeedCase(std::mt19937 &gen, Case &c, uint32_t op)
{
    std::memset(&c, 0, sizeof(c));
    c.op = op;
    for (uint8_t &b : c.window) b = uint8_t(gen());
    // The count: 2-5 (1 for the last Release), the active bit either way.
    Put<uint32_t>(c.window, 4, (op == kReleaseLast ? 1 : 2 + gen() % 4) | (Pick(gen, 2) ? 0x80000000u : 0));
    // The flag bytes are real bools; the window's handle is there mostly.
    for (size_t i = 0x18; i <= 0x1e; ++i) c.window[i] = uint8_t(gen() % 2);
    if (op == kInit || op == kFullscreen) c.window[0x18] = uint8_t(gen() % 2 && gen() % 2);
    Put<uint32_t>(c.window, 0x10, gen() % 4 ? gen() % 16 : gen());
    Put<uint32_t>(c.window, 0x14, gen());
    Put<uint32_t>(c.window, 0x64, gen() % 3 ? 0 : gen() % 3);
    for (size_t i = 0x88; i < 0xa0; i += 4) Put<int32_t>(c.window, i, int32_t(gen() % 1200) - 100);
    Put<uint16_t>(c.window, 0xa0, Pick(gen, 4) ? 0 : uint16_t(gen() | 1));
    Put<uint32_t>(c.window, 0xa4, Pick(gen, 8) ? 0 : 0x4a0000e0u + gen() % 4 * 4);
    Put<uint32_t>(c.window, 0xa8, Pick(gen, 3) ? 0x4a0000b4u : 0);
    for (size_t i = 0xc4; i <= 0xc6; ++i) c.window[i] = uint8_t(gen() % 2);
    c.window[0xcc] = uint8_t(gen() % 2);
    for (unsigned i = 0; i < 3; ++i) c.titleText[i] = gen() % kTexts;
    c.hookCount = gen() % 4;
    c.hookSlack = gen() % 3;
    c.hookFirst = gen() % kHookFakes;
    c.hookStep = gen() % 3 ? 1 : 0;
    c.connections = gen() % 64;
    c.listenerAnswers = gen() % 8;
    c.style = gen();
    c.exStyle = gen();
    c.staticFlag = gen() % 2;
    for (uint32_t &w : c.staticRect) w = gen() % 900;
    c.staticOffset = Pick(gen, 2) ? 0 : gen() % 60 + 1;
    c.localeText = gen();
    for (uint32_t &w : c.data.words) w = gen();
    for (uint32_t &w : c.args) w = gen();
    if (op == kProc) SeedProc(gen, c);
}

// ---- a side's world ----
cConnectionManager *gManager = nullptr;
std::vector<void *> gHookStorage;

void ResetFakes(const Case &c)
{
    gFramework.Reset<kFrameworkIface>(kFrameworkId);
    gFramework.AddFace(0x23ea70a1, gWindowService);
    gFramework.On(0, &FrameworkQuery);
    gFramework.On(5, &FrameworkService);
    gFramework.On(17, &TickEnabledH);
    gWindowService.Reset<kWindowServiceIface>(kWindowServiceId);
    gMonitor.Reset<kMonitorIface>(kMonitorId);
    gMonitor.On(4, &MonitorRect);
    gString.Reset<kStringIface>(kStringId);
    gCanvasService.Reset<kCanvasServiceIface>(kCanvasServiceId);
    gCanvasService.On(16, &CanvasServiceTopmost);
    for (unsigned i = 0; i < kHookFakes; ++i) {
        gHook[i].Reset<kHookIface>(kHookId0 + i);
        gHook[i].On(3, &HookOnMessage);
    }
    for (unsigned i = 0; i < kListeners; ++i) {
        gListener[i].Reset<kListenerIface>(kListenerId0 + i);
        gListener[i].AddFace(nGZEvent::cIAbstractBehaviorType::kIid, gListener[i]);
        gListener[i].On(3, &ListenerOnEvent);
        gListener[i].On(4, &ListenerOnQuery);
        gListener[i].value[0] = (c.listenerAnswers >> i) & 1;
    }
    gSource.Reset<kListenerIface>(kListenerId0 + 9);
    gSource.On(0, &SourceQuery);
}

cRZString *String(uint8_t *window, size_t offset) { return reinterpret_cast<cRZString *>(window + offset); }

// The window's strings and hook vector, built for this side.
void BuildContents(const Case &c)
{
    const size_t offsets[3] = {0x20, 0x44, 0x68};
    for (unsigned i = 0; i < 3; ++i) {
        std::memset(gWindow + offsets[i], 0, sizeof(cRZString));
        cRZString *const s = String(gWindow, offsets[i]);
        s->Construct();
        s->mString.assign(kTextPool[c.titleText[i]]);
        gWasLong[i] = s->mString.size() > 15;
    }
    const unsigned capacity = c.hookCount + c.hookSlack;
    void **storage = nullptr;
    if (capacity) {
        storage = static_cast<void **>(::operator new(capacity * sizeof(void *)));
        for (unsigned i = 0; i < c.hookCount; ++i) gHook[(c.hookFirst + i * c.hookStep) % kHookFakes].refs = 0;
        for (unsigned i = 0; i < c.hookCount; ++i) {
            drawfx::Fake &hook = gHook[(c.hookFirst + i * c.hookStep) % kHookFakes];
            storage[i] = &hook;
            ++hook.refs;
        }
    }
    Put<void **>(gWindow, 0xd0, storage);
    Put<void **>(gWindow, 0xd4, storage ? storage + c.hookCount : nullptr);
    Put<void **>(gWindow, 0xd8, storage ? storage + capacity : nullptr);
}

void BuildSide(const Case &c, impfx::Imports &imp, const VTables &vt, TrackStatic &track)
{
    drawfx::ResetAll();
    imp.BeginSide();
    gScript = {&c, 0, c.style, c.exStyle};
    gSide = &vt;
    if (IsProc(c)) track.Set(c.trackState);
    std::memcpy(gWindow, c.window, kSize);
    gData = c.data;
    ResetFakes(c);
    drawfx::AddRange(gWindow, kSize, kWindowTag);
    drawfx::AddRange(&gData, sizeof(gData), kDataTag);
    drawfx::AddRange(vt.icon, 1, kCallbackTag);
    BuildContents(c);
    *GameAddress<uint8_t>(kStatics) = uint8_t(c.staticFlag);
    std::memcpy(GameAddress<void>(kStatics + 4), c.staticRect, 16);
    *GameAddress<uint32_t>(kStatics + 0x14) = c.staticOffset;
    *GameAddress<void *>(kFrameworkCell) = &gFramework;
    gManager = cConnectionManagerAdapters::Construct(::operator new(cConnectionManager::kSize), nullptr);
    drawfx::AddRange(gManager, cConnectionManager::kSize, kConnTag);
    *GameAddress<void *>(kEventManagerCell) = gManager;
    gSource.Embed(gWindow);
    for (unsigned i = 0; i < kListeners; ++i)
        if ((c.connections >> i) & 1)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(gWindow), kListenEvents[i], reinterpret_cast<cIGZUnknown *>(&gListener[i]),
                kHeardAs[i]);
    for (unsigned i = 0; i < kListeners; ++i)
        if ((c.connections >> (3 + i)) & 1)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(gWindow), kListenEvents[i],
                reinterpret_cast<cIGZUnknown *>(&gListener[(i + 1) % kListeners]), kHeardAs[(i + 1) % kListeners]);
    for (unsigned i = 0; i < kProcEventCount; ++i)
        for (unsigned second = 0; second < 2; ++second)
            if ((c.procConnections >> (i + 6 * second)) & 1)
                static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                    reinterpret_cast<cIGZUnknown *>(gWindow), kProcEvents[i],
                    reinterpret_cast<cIGZUnknown *>(&gListener[(i + second) % kListeners]), (second ? 0x80 : 0x70) + i);
    Put<void *>(gWindow, 0, vt.primary);
    Put<void *>(gWindow, 8, vt.face8);
    Put<void *>(gWindow, 0xc, vt.faceC);
    drawfx::gLog.clear();
}

// What the case's block still holds when the op did not free it: the strings and the hook vector.
void ReleaseContents(const Case &c)
{
    if (Destroys(c.op)) return;
    for (size_t offset : {size_t(0x20), size_t(0x44), size_t(0x68)}) String(gWindow, offset)->Destruct();
    if (void **const first = Get<void **>(gWindow, 0xd0)) ::operator delete(first);
}

void DestroySide()
{
    cConnectionManagerAdapters::Destruct(gManager, nullptr);
    ::operator delete(gManager);
    gManager = nullptr;
}

void NoteRefs()
{
    const drawfx::Fake *all[] = {&gFramework,   &gWindowService, &gMonitor,     &gString,      &gHook[0],
                                 &gHook[1],     &gHook[2],       &gHook[3],     &gListener[0], &gListener[1],
                                 &gListener[2], &gCanvasService};
    for (const drawfx::Fake *f : all) drawfx::Note("refs", {f->id, uint32_t(f->refs)});
}

void NoteConnections()
{
    auto *m = static_cast<nGZEvent::cIConnectionManager *>(gManager);
    drawfx::Note("connected", m->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(gWindow)));
    for (unsigned i = 0; i < kListeners; ++i)
        drawfx::Note("from", {i, m->CountFrom(reinterpret_cast<cIGZUnknown *>(gWindow), kListenEvents[i])});
}

void NoteIdRange(nGZEvent::cAttributeIDRange range)
{
    unsigned steps = 0;
    while (steps < 8 && !range.mCurrent->IsEqual(range.mEnd)) {
        drawfx::Note("id", range.mCurrent->Get());
        range.mCurrent->Next();
        ++steps;
    }
    nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
}

void Note(const char *label, uint32_t value) { drawfx::Note(label, value); }

const uint32_t kIids[] = {cCanvasW32::kIidCanvas, cCanvasW32::kIidBehavior, 1, cCanvasW32::kIidBehavior2, cIGZSystemService::kIID,
                          cIGZMessageTarget::kIid, cCanvasW32::kIidBehaviorType, 0xfb6372ee, 0};
constexpr unsigned kIidCount = sizeof(kIids) / sizeof(kIids[0]);

const uint32_t kEventTypes[] = {cCanvasW32::kConnectionChangedEvent, cCanvasW32::kCanvasEvent, 0x1b637482, 0};

// The style the flags make (Show's own table), to make the window "already like that" in some cases.
uint32_t StyleFor(uint32_t f)
{
    uint32_t style = 0x80000;
    if (f & 2) style = 0xa0000;
    if (f & 4) style |= 0x10000;
    if (f & 1) style |= 0x40000;
    if (f & 8) style |= 0xc00000;
    return style;
}

// ---- one operation, through the side's vtables ----
void Run(const Case &c)
{
    void *const self = gWindow;
    void *const face8 = gWindow + 8;
    void *const faceC = gWindow + 0xc;
    const uint32_t *a = c.args;
    switch (c.op) {
    case kQueryInterface: {
        const uint32_t iid = kIids[a[0] % kIidCount] ? kIids[a[0] % kIidCount] : a[1];
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        const bool ok = Call<bool>(self, 0, iid, a[2] % 5 ? &out : nullptr);
        drawfx::Note("ret", {ok, Norm(out)});
        break;
    }
    case kFaceRefs: {
        void *const faces[] = {face8, faceC};
        for (void *f : faces) {
            const uint32_t iid = kIids[a[0] % kIidCount] ? kIids[a[0] % kIidCount] : a[1];
            void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
            const bool ok = Call<bool>(f, 0, iid, &out);
            drawfx::Note("qi", {ok, Norm(out)});
            if (ok && iid != cCanvasW32::kIidBehaviorType) Note("release", Call<uint32_t>(f, 2));
            Note("addref", Call<uint32_t>(f, 1));
            Note("release", Call<uint32_t>(f, 2));
            ++a;
        }
        break;
    }
    case kAddRef: Note("ret", Call<uint32_t>(self, 1)); break;
    case kRelease: Note("ret", Call<uint32_t>(self, 2)); break;
    case kOnQuery: Note("ret", Call<bool>(self, 4, kListenEvents[a[0] % 3], a[1], static_cast<void *>(gData.words))); break;
    case kNotify: Call<void>(self, 5, kListenEvents[a[0] % 3], a[1], static_cast<void *>(gData.words)); break;
    case kAsk: Note("ret", Call<bool>(self, 6, kListenEvents[a[0] % 3], a[1], static_cast<void *>(gData.words))); break;
    case kConnect: Call<void>(self, 8, kListenEvents[a[0] % 3], static_cast<void *>(&gListener[a[1] % 3]), a[2] % 4); break;
    case kDisconnect: Call<void>(self, 9, kListenEvents[a[0] % 3], static_cast<void *>(&gListener[a[0] % 3]), kHeardAs[a[0] % 3]); break;
    case kIsActive: Note("ret", Call<uint32_t>(self, 11)); break;
    case kEventCount: Note("ret", Call<uint32_t>(self, 12)); break;
    case kEventIds: {
        nGZEvent::cAttributeIDRange range;
        Call<void *>(self, 13, &range);
        NoteIdRange(range);
        break;
    }
    case kEventAttribute: {
        nGZEvent::cEventAttribute r;
        const uint32_t ids[] = {kRegistryIds[0], kRegistryIds[1], kRegistryIds[2], a[1]};
        Call<void *>(self, 14, &r, ids[a[0] % 4]);
        drawfx::Note("attribute", {r.mId, Word(r.mName), r.mWord08, r.mWord0C, r.mWord10, r.mWord14});
        break;
    }
    case kEmptySlots:
        Call<void>(self, 7, a[0], a[1], a[2]);
        Call<void>(self, 10, a[0], a[1], a[2]);
        break;
    case kOnEvent: {
        const uint32_t type = kEventTypes[a[0] % 4] ? kEventTypes[a[0] % 4] : a[1];
        Call<void>(self, 3, type, a[2], static_cast<void *>(gData.words), a[3]);
        break;
    }
    case kShow: {
        // Several shows in a row: the flags (or -1 for the stored ones), restore or not; the system's styles set to what
        // the flags make in some of them, so the window is "already like that".
        for (unsigned round = 0; round < 3; ++round) {
            const uint32_t flags = a[round] % 4 == 0 ? 0xffffffffu : a[round + 3] % 16;
            const uint32_t effective = flags == 0xffffffffu ? Get<uint32_t>(gWindow, 0x10) : flags;
            if (a[round + 6] % 3 == 0) {
                gScript.style = (gScript.style & ~0x80cf0000u) | StyleFor(effective);
                gScript.exStyle = (gScript.exStyle & ~0x300u) | (effective & 1 ? 0x300u : 0);
            }
            Call<void>(self, 15, int32_t(flags), bool(a[round + 9] % 2));
            Note("style", Get<uint32_t>(gWindow, 0x10));
            Note("system style", gScript.style);
            Note("system ex style", gScript.exStyle);
        }
        break;
    }
    case kTopmost:
        Note("ret", uint32_t(Call<int32_t>(self, 16, bool(a[0] % 2))));
        Note("ret", uint32_t(Call<int32_t>(self, 16, bool(a[1] % 2))));
        break;
    case kTitle: {
        const char *const text = kTextPool[a[0] % kTexts];
        Call<void>(self, 17, text);
        break;
    }
    case kSizes:
        Call<void>(self, 18, int32_t(a[0] % 1000), int32_t(a[1] % 800));
        Call<void>(self, 19, int32_t(a[2] % 1100), int32_t(a[3] % 900));
        Call<void>(self, 20, static_cast<void *>(&gMonitor));
        break;
    case kPower:
        Note("power", Call<uint32_t>(self, 21, a[0] % 4 == 0 ? a[1] : a[1] % 16));
        Note("active", Norm(Call<void *>(self, 22, bool(a[2] % 2))));
        Note("power", Call<uint32_t>(self, 21, a[3] % 16));
        Note("active", Norm(Call<void *>(self, 22, bool(a[4] % 2))));
        break;
    case kPaint:
        for (unsigned i = 0; i < 6; ++i) {
            switch (a[i] % 5) {
            case 0: Call<void>(self, 23); break;
            case 1: Call<void>(self, 24); break;
            case 2: Call<void>(self, 25); break;
            case 3: Call<void>(self, 26); break;
            case 4: Note("unlocked", Call<bool>(self, 36)); break;
            }
        }
        break;
    case kGetters: {
        Note("27", Call<uint32_t>(self, 27));
        cCanvasRect r{};
        Note("29", Norm(Call<cCanvasRect *>(self, 29, &r)));
        drawfx::Note("rect", {uint32_t(r.mLeft), uint32_t(r.mTop), uint32_t(r.mRight), uint32_t(r.mBottom)});
        cCanvasSize s{};
        Note("30", Norm(Call<cCanvasSize *>(self, 30, &s)));
        drawfx::Note("size", {uint32_t(s.mWidth), uint32_t(s.mHeight)});
        Note("31", Call<uint32_t>(self, 31));
        Note("32", Call<bool>(self, 32));
        Note("33", Call<bool>(self, 33));
        Note("34", Call<bool>(self, 34));
        Note("35", Call<bool>(self, 35));
        Note("36", Call<bool>(self, 36));
        Note("39", Call<bool>(self, 39));
        Note("40", Norm(Call<void *>(self, 40)));
        Note("41", Call<uint16_t>(self, 41));
        Note("42", Norm(Call<void *>(self, 42)));
        break;
    }
    case kFullscreen:
        Call<void>(self, 37, bool(a[0] % 2));
        Call<void>(self, 37, bool(a[1] % 2));
        Call<void>(self, 37, bool(a[2] % 2));
        break;
    case kFlag19:
        Call<void>(self, 38, bool(a[0] % 2));
        Call<void>(self, 38, bool(a[1] % 2));
        break;
    case kGetTitle: Call<void>(self, 28, static_cast<void *>(&gString)); break;
    case kHooks:
        for (unsigned i = 0; i < 5; ++i) {
            // A hook held already, one that is not, or none.
            drawfx::Fake *const hook = a[i] % 7 == 0 ? nullptr : &gHook[a[i + 5] % kHookFakes];
            Note("hook", Call<bool>(self, 43, static_cast<void *>(hook), bool(a[i + 1] % 2)));
        }
        break;
    case kServiceSlots:
        Note("3", Call<uint32_t>(face8, 3));
        Note("5", Call<int32_t>(face8, 5));
        Note("6", Call<bool>(face8, 6));
        if (a[0] % 2) Call<void>(face8, 4, a[1]);
        if (a[2] % 2) Call<void>(face8, 7, a[3] % 2 != 0);
        Note("3", Call<uint32_t>(face8, 3));
        Note("6", Call<bool>(face8, 6));
        Note("10", Call<bool>(face8, 10, a[4]));
        Note("11", Call<bool>(face8, 11, a[5]));
        Note("12", Call<int32_t>(face8, 12));
        Note("target", Call<bool>(faceC, 3, static_cast<void *>(gData.words)));
        break;
    case kInit: Note("ret", Call<bool>(face8, 8)); break;
    case kShutdown: Note("ret", Call<bool>(face8, 9)); break;
    case kDestruct: Call<void *>(self, 44, 0u); break;
    case kReleaseLast: Note("ret", Call<uint32_t>(self, 2)); break;
    case kDelete: Call<void *>(self, 44, 1u); break;
    case kProc: {
        // The side's procedure (the original 0x157800 or ours), each message in turn; one a listener or the service
        // throws out of is noted and the stream goes on.
        using Proc = int32_t(__stdcall *)(void *, uint32_t, uint32_t, int32_t);
        const Proc proc = reinterpret_cast<Proc>(gSide->proc);
        for (unsigned i = 0; i < c.messages; ++i) {
            uint32_t l = c.lParam[i];
            if (c.message[i] == WM_GETMINMAXINFO) l = Word(gData.minmax);
            if (c.message[i] == WM_SIZING) l = Word(gData.sizing);
            void *const hwnd = reinterpret_cast<void *>(uintptr_t(c.target[i] ? kForeignWindow : kProcWindow));
            try {
                Note("result", uint32_t(proc(hwnd, c.message[i], c.wParam[i], int32_t(l))));
            } catch (int thrown) {
                Note("threw", uint32_t(thrown));
            }
        }
        TrackStatic::Note();
        break;
    }
    }
}

struct Result {
    drawfx::Log log;
    std::vector<uint8_t> window;
    uint8_t statics[kStaticsSize];
    Data data;
    long long blocks, bytes;
};

std::vector<uint8_t> Without(const uint8_t *block, size_t size, std::initializer_list<size_t> skip)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < size; i += 4) {
        bool skipped = false;
        for (size_t s : skip) skipped |= s == i;
        if (!skipped) bytes.insert(bytes.end(), block + i, block + i + 4);
    }
    return bytes;
}

// The window's pointers into the heap (a long string's text, the hook vector) differ between the sides: the bytes
// compared leave them out (the texts and the hooks are compared on their own).
std::vector<uint8_t> WindowBytes(const uint8_t *window)
{
    uint8_t copy[kSize];
    std::memcpy(copy, window, kSize);
    for (size_t offset : {size_t(0x20), size_t(0x44), size_t(0x68)})
        if (Get<uint32_t>(copy, offset + 0x18) > 15 || gWasLong[(offset - 0x20) / 0x24]) Put<uint32_t>(copy, offset + 4, 0);
    for (size_t at : {size_t(0xd0), size_t(0xd4), size_t(0xd8)}) Put<uint32_t>(copy, at, 0);
    return Without(copy, kSize, {0, 8, 0xc});
}

template <class R> Result Side(const Case &c, impfx::Imports &imp, const VTables &vt, TrackStatic &track, R run)
{
    gWindow = WindowGone(c.op) ? static_cast<uint8_t *>(::operator new(kSize)) : gWindowBlock;
    BuildSide(c, imp, vt, track);
    long long blocks = 0, bytes = 0;
    drawfx::gLog = drawfx::Log();
    drawfx::gLog.reserve(512);
    {
        difftest::AllocationCounter counter;
        run();
        counter.Stop();
        blocks = counter.Blocks();
        bytes = counter.Bytes();
    }
    NoteRefs();
    if (!Destroys(c.op)) NoteConnections();
    Result r;
    r.log = drawfx::gLog;
    if (!WindowGone(c.op)) {
        // The strings (their text and size) as text, the rest as bytes with the strings' and the vector's heap words out.
        r.window = WindowBytes(gWindow);
        if (!Destroys(c.op)) {
            for (size_t offset : {size_t(0x20), size_t(0x44), size_t(0x68)}) {
                const std::string &text = String(gWindow, offset)->mString;
                r.window.insert(r.window.end(), text.begin(), text.end());
                r.window.push_back(0);
            }
            const uint32_t count = uint32_t((Get<uint32_t>(gWindow, 0xd4) - Get<uint32_t>(gWindow, 0xd0)) / 4);
            r.window.insert(r.window.end(), reinterpret_cast<const uint8_t *>(&count), reinterpret_cast<const uint8_t *>(&count) + 4);
            const void *const *items = Get<const void *const *>(gWindow, 0xd0);
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t item = Norm(items[i]);
                r.window.insert(r.window.end(), reinterpret_cast<const uint8_t *>(&item), reinterpret_cast<const uint8_t *>(&item) + 4);
            }
        }
    }
    std::memcpy(r.statics, GameAddress<void>(kStatics), kStaticsSize);
    r.data = gData;
    r.blocks = blocks;
    r.bytes = bytes;
    ReleaseContents(c);
    DestroySide();
    gWindow = gWindowBlock;
    return r;
}

// Our vtable words (a constructed window's).
VTables OurVTables()
{
    cCanvasW32 *const window = new cCanvasW32;
    const auto *const b = reinterpret_cast<const uint8_t *>(static_cast<cICanvasW32Behavior *>(window));
    const VTables vt{Get<void *>(b, 0), Get<void *>(b, 8), Get<void *>(b, 0xc),
                     reinterpret_cast<void *>(&cCanvasW32::WindowProc), reinterpret_cast<void *>(&cCanvasW32::LoadClassIcon)};
    delete window;
    return vt;
}

VTables GameVTables()
{
    return {GameAddress<void>(kVTableRva), GameAddress<void>(kFace8Rva), GameAddress<void>(kFaceCRva), GameAddress<void>(kProcRva),
            GameAddress<void>(kIconCallbackRva)};
}

// The imports: those every op may reach, then Init's and Shutdown's or the procedure's (one Imports fakes at most 48).
struct World {
    RegistryScope registry;
    shape::StandIn registryStand{0x156a70, &cCanvasW32Traits::RegistryFor, &pdfx::StandRegistry<kRegistryMap>};
    Cells cells;
    StandIns stands;
    TrackStatic track;
    impfx::Imports imp;
    World(difftest::Context &ctx, bool proc) : imp(ctx)
    {
        IMPFX_ADD(imp, GetWindowRect).On(&GetWindowRectH);
        IMPFX_ADD(imp, GetWindowLongA).On(&GetWindowLongH);
        IMPFX_ADD(imp, SetWindowLongA, "iiu").On(&SetWindowLongH);
        IMPFX_ADD(imp, ShowWindow, "iu").On(&Returns01);
        IMPFX_ADD(imp, SetWindowPos, "iiuuuuu").On(&SetPosH);
        IMPFX_ADD(imp, GetDesktopWindow).Returns(0x4a0000d8);
        IMPFX_ADD(imp, IsIconic).On(&IsIconicH);
        IMPFX_ADD(imp, IsZoomed).On(&IsZoomedH);
        IMPFX_ADD(imp, GetAncestor, "iu").On(&GetAncestorH);
        IMPFX_ADD(imp, ValidateRect, "ir").On(&Returns01);
        IMPFX_ADD(imp, InvalidateRect, "iru").On(&Returns01);
        IMPFX_ADD(imp, UpdateWindow).On(&Returns01);
        IMPFX_ADD(imp, SetWindowTextA, "is").On(&Returns01);
        IMPFX_ADD(imp, GetClientRect).On(&GetClientRectH);
        IMPFX_ADD(imp, ClientToScreen).On(&ClientToScreenH);
        IMPFX_ADD(imp, GetActiveWindow).On(&GetActiveH);
        IMPFX_ADD(imp, MoveWindow, "iuuuuu");
        IMPFX_ADD(imp, SetActiveWindow).Returns(0x4a0000f4);
        IMPFX_ADD(imp, FillRect, "iri").On(&Returns01);
        IMPFX_ADD(imp, GetDC).On(&GetDCH);
        IMPFX_ADD(imp, ReleaseDC).On(&Returns01);
        IMPFX_ADD(imp, SetThreadExecutionState, "u").Returns(0x80000001);
        IMPFX_ADD(imp, AdjustWindowRectEx, "ruuu").On(&AdjustRectH);
        IMPFX_ADD(imp, SystemParametersInfoA, "uuiu").On(&SpiH);
        IMPFX_ADD(imp, GetLocaleInfoA, "uuiu").On(&LocaleH);
        IMPFX_ADD(imp, GetModuleHandleA, "s").Returns(0x400000);
        if (proc) {
            IMPFX_ADD(imp, GetProcAddress, "is").On(&GetProcAddressH);
            IMPFX_ADD(imp, Sleep, "u");
            IMPFX_ADD(imp, ScreenToClient).On(&ScreenToClientH);
            IMPFX_ADD(imp, DefWindowProcA, "iuuu").On(&DefWindowProcH);
            IMPFX_ADD(imp, GetAsyncKeyState, "u").On(&AsyncKeyH);
            IMPFX_ADD(imp, SetCapture).On(&SetCaptureH);
            IMPFX_ADD(imp, ReleaseCapture).Returns(1);
            IMPFX_ADD(imp, BeginPaint).On(&BeginPaintH);
            IMPFX_ADD(imp, EndPaint).On(&EndPaintH);
            // Left to the harness (real, unlogged): SetLastError (the image's CRT keeps the thread's last error while
            // a listener's exception unwinds the original), MultiByteToWideChar / WideCharToMultiByte (the original's
            // text conversion goes through its imports, ours does not).
            return;
        }
        IMPFX_ADD(imp, SetLastError, "u");
        IMPFX_ADD(imp, GetKeyboardLayout).On(&LayoutH);
        IMPFX_ADD(imp, CreateSolidBrush, "u").On(&BrushH);
        IMPFX_ADD(imp, EnumResourceNamesA, "iuii").On(&EnumResourceNamesH);
        IMPFX_ADD(imp, LoadIconA, "uu").On(&LoadIconH);
        IMPFX_ADD(imp, RegisterClassExA).On(&RegisterClassH);
        IMPFX_ADD(imp, CreateWindowExA, "uuuuuuuuiiii").On(&CreateWindowH);
        IMPFX_ADD(imp, SetTimer, "iuui").Returns(0x2a);
        IMPFX_ADD(imp, KillTimer, "iu").Returns(1);
        IMPFX_ADD(imp, DestroyWindow).Returns(1);
        IMPFX_ADD(imp, UnregisterClassA, "ui").Returns(1);
        IMPFX_ADD(imp, DeleteObject).Returns(1);
    }
};

void Seen(const Result &r, std::set<std::string> &seen)
{
    for (const drawfx::Call &call : r.log) {
        if (!call.iface) seen.insert(call.note);
        else if (call.slot < call.iface->count) seen.insert(call.iface->slots[call.slot].name);
    }
}

void Sweep(difftest::Context &ctx, uint32_t seed, std::initializer_list<uint32_t> ops, unsigned rounds,
           std::initializer_list<const char *> needs = {})
{
    bool proc = false;
    for (uint32_t op : ops) proc = proc || op == kProc;
    World world(ctx, proc);
    const VTables ours = OurVTables();
    const VTables game = GameVTables();
    // One unmeasured case per side first (its own generator, so the cases are unchanged): a first-use allocation (8 bytes
    // on our side, a lazily built static) otherwise lands in round 0 when this test runs alone, and only then.
    {
        std::mt19937 warm(seed);
        auto c = std::make_unique<Case>();
        SeedCase(warm, *c, *ops.begin());
        Side(*c, world.imp, game, world.track, [&] { Run(*c); });
        Side(*c, world.imp, ours, world.track, [&] { Run(*c); });
    }
    std::mt19937 gen(seed);
    std::set<std::string> seen;
    for (uint32_t op : ops) {
        for (unsigned round = 0; round < rounds; ++round) {
            auto c = std::make_unique<Case>();
            SeedCase(gen, *c, op);
            const Result a = Side(*c, world.imp, game, world.track, [&] { Run(*c); });
            const Result b = Side(*c, world.imp, ours, world.track, [&] { Run(*c); });
            Seen(a, seen);
            const char *label = kOpNames[op];
            if (!drawfx::Same(ctx, a.log, b.log, label, round)) return;
            if (std::memcmp(&a.data, &b.data, sizeof(Data)) != 0) {
                ctx.Fail("%s round %u: the out-parameters differ", label, round);
                return;
            }
            if (a.window != b.window || std::memcmp(a.statics, b.statics, kStaticsSize) != 0 || a.blocks != b.blocks ||
                a.bytes != b.bytes) {
                size_t at = 0;
                while (at < a.window.size() && at < b.window.size() && a.window[at] == b.window[at]) ++at;
                ctx.Fail("%s round %u: window %d (compared byte %zu of %zu/%zu) statics %d blocks %lld/%lld bytes %lld/%lld", label,
                         round, a.window == b.window, at, a.window.size(), b.window.size(),
                         std::memcmp(a.statics, b.statics, kStaticsSize) == 0, a.blocks, b.blocks, a.bytes, b.bytes);
                return;
            }
        }
    }
    for (const char *need : needs)
        if (!seen.count(need)) {
            std::string all;
            for (const std::string &s : seen) all += s + ", ";
            ctx.Fail("the sweep never reached '%s' (a vacuous path); seen: %s", need, all.c_str());
        }
}

template <size_t N> bool SlotsAre(void *vtable, const uint32_t (&rvas)[N])
{
    for (size_t i = 0; i < N; ++i)
        if (static_cast<void *const *>(vtable)[i] != GameAddress<void>(rvas[i])) return false;
    return true;
}

} // namespace

// Construction (0x155c70 with GameNew handing out a block of seeded bytes; ours constructed over a copy of the same
// bytes): every byte the creator writes, the original's vtables and their slots.
DIFF_TEST(cCanvasW32_Create, 0x155c70)
{
    const VTables game = GameVTables();
    if (!SlotsAre(game.primary, kPrimarySlots)) ctx.Fail("the original's vtable does not hold the slots this test names");
    std::mt19937 gen(0x155c70);
    static uint8_t seeded[kSize], ours[kSize];
    static uint8_t *hand = nullptr;
    struct New {
        static void *__cdecl Give(size_t size) { return size == kSize ? hand : ::operator new(size); }
    };
    shape::CodePatch give{0x824632, reinterpret_cast<const void *>(&New::Give)};
    for (unsigned round = 0; round < 64; ++round) {
        for (uint8_t &x : seeded) x = uint8_t(gen());
        std::memcpy(ours, seeded, kSize);
        hand = seeded;
        ++ctx.cases;
        void *const made = ctx.Original<void *(__cdecl *)()>(0x155c70)();
        if (made != seeded) ctx.Fail("round %u: the creator's object is not the block it was given", round);
        ::new (ours) cCanvasW32;
        if (Without(seeded, kSize, {0, 8, 0xc}) != Without(ours, kSize, {0, 8, 0xc})) {
            size_t at = 0;
            const auto x = Without(seeded, kSize, {0, 8, 0xc}), y = Without(ours, kSize, {0, 8, 0xc});
            while (at < x.size() && x[at] == y[at]) ++at;
            ctx.Fail("round %u: the window differs (compared byte %zu)", round, at);
            return;
        }
        if (Get<void *>(seeded, 0) != game.primary || Get<void *>(seeded, 8) != game.face8 || Get<void *>(seeded, 0xc) != game.faceC)
            ctx.Fail("round %u: the original's vtable words are not 0xd9cc04 / 0xd9ccbc / 0xd9ccf4", round);
    }
    // A failed allocation gives null.
    hand = nullptr;
    if (ctx.Original<void *(__cdecl *)()>(0x155c70)() != nullptr) ctx.Fail("the creator did not give null without memory");
    // Ours: a fresh window.
    cICanvasW32Behavior *const made = cCanvasW32::Create();
    auto *const window = static_cast<cCanvasW32 *>(made);
    if (!made || window->mRefCount != 0 || window->mServiceId != cCanvasW32::kServiceId) ctx.Fail("Create: not a fresh window");
    delete window;
}

// The behaviour slots (0-14) and the faces' QueryInterface / AddRef / Release: QueryInterface 0x1569e0 (thunks 0x156a58, 0x156a60), Release
// 0x156d00 (thunks 0x156d56, 0x156d5e), OnEvent 0x155e50, NotifyListeners 0x156df0, AskListeners 0x155b50, Connect 0x155740, Disconnect
// 0x155a10, GetEventCount 0x156210, GetEventIds 0x155110, GetEventAttribute 0x155380 (and the shared slots 0x146a20, 0x147ce0, 0x147cf0).
DIFF_TEST(cCanvasW32_Behaviour, 0x1569e0)
{
    Sweep(ctx, 0x1569e0,
          {kQueryInterface, kFaceRefs, kAddRef, kRelease, kOnQuery, kNotify, kAsk, kConnect, kDisconnect, kIsActive,
           kEventCount, kEventIds, kEventAttribute, kEmptySlots, kOnEvent},
          96, {"OnEvent", "OnQuery", "id", "attribute"});
}

// Show (0x157220): the styles, the rectangles, the statics and the event.
DIFF_TEST(cCanvasW32_Show, 0x157220)
{
    Sweep(ctx, 0x157220, {kShow}, 1500,
          {"SetWindowLongA", "GetWindowRect", "ShowWindow", "SetWindowPos", "canvas event"});
}

// The size, title, power and paint slots, the getters, fullscreen and the hooks: SetSize 0x157040, SetTopmost 0x156f00, SetTitle 0x156f60,
// SetMinSize 0x157190, PlaceOnMonitor 0x1555b0, SetPowerFlags 0x157070, SetActive 0x156ee0, LockPaint 0x1561a0, UnlockPaint 0x157720,
// Repaint 0x157750, FillBackground 0x1556c0, GetTitle 0x155470, GetClientRect 0x156d70, GetMinSize 0x1561f0, IsActiveWindow 0x1560f0,
// IsMinimized 0x156160, IsMaximized 0x156140, IsCaptionless 0x156120, IsPaintUnlocked 0x156190, SetFullscreen 0x1570a0, SetFlag19 0x155460,
// GetWindowClass 0x1556b0, GetMonitor 0x154f10, SetMessageHook 0x154f20, the service face 0x155e20, 0x157200, 0x155e30, 0x156180, 0x157210,
// 0x1569b0, the window's GetHwnd 0x155e40, and the helpers 0x155ab0 and 0x157780 (SetFlag19 and the tick reach it).
DIFF_TEST(cCanvasW32_Slots, 0x157040)
{
    Sweep(ctx, 0x157040,
          {kTopmost, kTitle, kSizes, kPower, kPaint, kGetters, kFullscreen, kFlag19, kGetTitle, kHooks, kServiceSlots}, 400,
          {"SetWindowTextA", "MoveWindow", "AdjustWindowRectEx", "mon.GetRect", "SetThreadExecutionState", "SetActiveWindow",
           "ValidateRect", "InvalidateRect", "UpdateWindow", "FillRect", "GetClientRect", "ClientToScreen", "IsIconic",
           "IsZoomed", "GetAncestor", "GetActiveWindow", "MonitorFromWindow", "fw.ToggleTick", "str.Copy"});
}

// Init 0x155e90, Shutdown 0x1574f0 and the deleting destructor 0x154d40 (destructor, last Release, delete); Init's
// icon callback 0x155b30 (EnumResourceNamesA hands it the names).
DIFF_TEST(cCanvasW32_Lifetime, 0x155e90)
{
    Sweep(ctx, 0x155e90, {kInit, kShutdown, kDestruct, kReleaseLast, kDelete}, 600,
          {"RegisterClassExA", "CreateWindowExA", "SetTimer", "KillTimer", "DestroyWindow", "UnregisterClassA", "DeleteObject",
           "fw.AddToTick", "ws.SetWindow", "SystemParametersInfoA", "GetLocaleInfoA", "canvas event", "LoadIconA",
           "icon callback"});
}

// The window procedure 0x157800 and the helpers it reaches: OnChar 0x156260, ExitSizeMove 0x1563f0, the key handlers
// 0x1564d0 / 0x156550, the button-down handlers 0x1565d0 / 0x1566c0 / 0x1568c0, TrackMouseLeave 0x1567b0,
// KeyModifiers 0x155860, the monitor callback 0x155710; the events it builds (cCanvasEvent, cCanvasSizeMoveEvent,
// cMouseEvent, cKeyEvent) through the listeners.
DIFF_TEST(cCanvasW32_WindowProc, 0x157800)
{
    Sweep(ctx, 0x157800, {kProc}, 6000,
          {"result", "threw", "hook.OnMessage", "DefWindowProcA", "canvas event", "size/move event", "mouse event", "key event",
           "scalar event", "event slots", "OnQuery", "asked", "fw.Quit", "fw.IsTickEnabled", "fw.GetSystemService", "cs.SetTopmost",
           "Sleep", "BeginPaint", "EndPaint's paint", "EnumDisplayMonitors", "monitor callback", "FillRect", "TrackMouseEvent",
           "GetProcAddress", "GetModuleHandleA", "SetCapture", "ReleaseCapture", "ScreenToClient", "GetAsyncKeyState",
           "AdjustWindowRectEx", "GetWindowRect", "GetLocaleInfoA", "ShowWindow", "SystemParametersInfoA", "IsIconic",
           "IsZoomed"});
}

// KeyModifiers 0x155860 alone (cdecl, the IME server's PostCharacter calls it too): any key flags, bit 30 set or clear
// (the procedure only passes lParam >> 16, so this is where the 0x40000000 test is reached), against the same
// GetAsyncKeyState answers.
DIFF_TEST(cCanvasW32_KeyModifiers, 0x155860)
{
    impfx::Imports imp(ctx);
    IMPFX_ADD(imp, GetAsyncKeyState, "u").On(&AsyncKeyH);
    std::mt19937 gen(0x155860);
    bool sawBit30 = false;
    for (unsigned round = 0; round < 2000; ++round) {
        auto c = std::make_unique<Case>();
        for (uint32_t &arg : c->args) arg = gen();
        const uint32_t flags = round % 2 ? gen() | 0x40000000u : gen() & ~0x40000000u;
        uint32_t result[2];
        drawfx::Log logs[2];
        for (int side = 0; side < 2; ++side) {
            gScript = {c.get(), 0, 0, 0};
            drawfx::gLog = drawfx::Log();
            result[side] = side == 0 ? ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x155860)(flags)
                                     : cCanvasW32::KeyModifiers(flags);
            logs[side] = drawfx::gLog;
        }
        gScript = {};
        ++ctx.cases;
        sawBit30 = sawBit30 || (result[0] & 0x40000000u) != 0;
        if (!drawfx::Same(ctx, logs[0], logs[1], "KeyModifiers", round)) return;
        if (result[0] != result[1]) {
            ctx.Fail("round %u: flags 0x%08x give 0x%x (original) / 0x%x (ours)", round, flags, result[0], result[1]);
            return;
        }
    }
    if (!sawBit30) ctx.Fail("the original never kept the flags' 0x40000000 bit (a vacuous sweep)");
}
