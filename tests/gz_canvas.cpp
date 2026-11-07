// Differential tests for nGZGraphic4::cGZCanvas and the messages it sends (cGZCanvasMessage, cGZMouseMessage,
// cGZKeyMessage) as whole classes (src/gz/cGZCanvas.h, lane NQ): the creator 0x1495f0, every slot of the canvas's five
// faces, its destructor, and every slot of the three messages.
//
// One world per case, built alike for each side from the case's seed:
// - The canvas is a 0x30-byte block of the case's bytes (static; a heap block for the ops that free it) with the side's
//   five vtable words (the original's 0xd9c080 / 0xd9c0c4 / 0xd9c124 / 0xd9c144 / 0xd9c158 or ours); every slot is
//   called through the block's own vtables, so each original slot runs against ours.
// - Its window (+0x28) and message server (+0x2c) are fakes or null; the window answers its canvas interface
//   (0xbb6377fa) with another fake. Framework services (the message server, the window, cGraphicSystemDX) are answered
//   or not per case by a framework fake (a case may run without a framework).
// - The message pools (0xfeb660 / 0xfeb670 / 0xfeb680) are set per side to the same free list over static elements of
//   the case's bytes, so the original's allocator 0x149040 hands out the same elements on both sides; a third element
//   of each pool is a live message of the side's class for the message tests. The message server's Send reads each
//   message through its slots (type, code, position, ...) and may keep a reference.
// - Events: our real cConnectionManager in the cell (0x10c9500), listener fakes connected from the canvas; the
//   attribute registry 0x149ef0 runs for real over a map placed at its static (RegistryScope). SetWindowPos and
//   PostMessageA are impfx fakes.
// Compared: the log (every call on every fake with its arguments, the notes), each slot's result, every byte of the
// canvas but its vtable words, the pools and their elements (a message's vtable word compared as its class), the
// HWND cell 0x10c9570, every fake's count, the connections, the allocations.
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "impfx.h"

#include "game/vtable.h"
#include "gz/cConnectionManager.h"
#include "gz/cGZCanvas.h"

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

constexpr uint32_t kVTableRva = 0xd9c080, kFace8Rva = 0xd9c0c4, kFaceCRva = 0xd9c124, kFace10Rva = 0xd9c144,
                   kFace14Rva = 0xd9c158;
constexpr uint32_t kMessageVTableRva[3] = {0xd9c190, 0xd9c1c0, 0xd9c1fc};
constexpr uint32_t kEventManagerCell = 0x10c9500;
constexpr uint32_t kRegistryMap = 0x10c9574, kRegistryMapGuard = 0x10c957c, kRegistryOnce = 0x10c9580,
                   kRegistryHolderGuard = 0x10c9588;
constexpr uint32_t kHwndCell = 0x10c9570;
constexpr uint32_t kPoolRva[3] = {kCanvasMessagePool, kMouseMessagePool, kKeyMessagePool};
constexpr uint32_t kMessageSize[3] = {cGZCanvasMessage::kSize, cGZMouseMessage::kSize, cGZKeyMessage::kSize};
constexpr uint32_t kMessageType[3] = {cGZCanvasMessage::kType, cGZMouseMessage::kType, cGZKeyMessage::kType};
constexpr uint32_t kMessageIid[3] = {cIGZCanvasMessage::kIid, cIGZMouseMessage::kIid, cIGZKeyMessage::kIid};
constexpr size_t kSize = cGZCanvas::kSize;
constexpr unsigned kElements = 3, kElementBytes = 0x20;
constexpr uint32_t kCanvasTag = 0x7e000000, kElementTag = 0x7d000000, kDataTag = 0x7c000000, kConnTag = 0x7b000000,
                   kMarkTag = 0x7a000000;

// The original's slots, as its vtables hold them (checked by cGZCanvas_Create).
constexpr uint32_t kPrimarySlots[16] = {0x149de0, 0x148f80, 0x14a1a0, 0x149770, 0x147ce0, 0x148890, 0x1474c0, 0x377e0,
                                        0x147150, 0x1473e0, 0x377e0,  0x147cf0, 0x149d10, 0x1490b0, 0x149320, 0x148e20};
constexpr uint32_t kFace8Slots[23] = {0x149e37, 0x148f85, 0x14a1a5, 0x147a70, 0x148260, 0x149cd0, 0x14a280, 0x144910,
                                      0x144910, 0x149ce0, 0x149cc0, 0x14a210, 0x1496c0, 0x14a270, 0x140fe0, 0x14a240,
                                      0x140fe0, 0x140fe0, 0x148ba0, 0x147d00, 0x6c6b0,  0x149d80, 0x1496e0};
constexpr uint32_t kFaceCSlots[7] = {0x149e4f, 0x148f9d, 0x14a1bd, 0x1490a0, 0x149700, 0x149400, 0x148fc0};
constexpr uint32_t kFace10Slots[4] = {0x149e3f, 0x148f8d, 0x14a1ad, 0x147d00};
constexpr uint32_t kFace14Slots[13] = {0x149e47, 0x148f95, 0x14a1b5, 0x147a30, 0x1489e0, 0x147a40, 0x1480a0,
                                       0x1489f0, 0x149a90, 0x14a2c5, 0x148260, 0x148260, 0x147a40};
constexpr uint32_t kCanvasMessageSlots[11] = {0x149e60, 0x148fb0, 0x14a1d0, 0x149470, 0x46390, 0x14a260,
                                              0x148de0, 0x148e00, 0x149c60, 0x145770, 0x148ec0};
constexpr uint32_t kMouseMessageSlots[14] = {0x149ec0, 0x148fb0, 0x14a1f0, 0x149560, 0x46390,  0x14a260, 0x148de0,
                                             0x148e00, 0x149c90, 0x145770, 0x149d60, 0x148ba0, 0x147d00, 0x148f40};
constexpr uint32_t kKeyMessageSlots[14] = {0x149e90, 0x148fb0, 0x14a1f0, 0x1494e0, 0x46390,  0x14a260, 0x148de0,
                                           0x148e00, 0x149c70, 0x145770, 0x149cf0, 0x149d00, 0x148ba0, 0x148f00};

// ---- fakes ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRefSlot{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kReleaseSlot{0, drawfx::Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kServerSlots[] = {kQI, kAddRefSlot, kReleaseSlot, {1, drawfx::Ret::Int, true, "server.Send", "i"}};
inline constexpr drawfx::Iface kServerIface{"cIGZMessageServer2", 4, kServerSlots};

// The window's behaviour interface (0x5b637209): 8 Connect, 15 Show, 17 SetTitle, 29 GetClientRect, 34 / 35.
inline constexpr drawfx::Slot kWinSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U,
    {3, drawfx::Ret::Int, true, "win.Connect", "uiu"}, U, U, U, U, U, U,
    {2, drawfx::Ret::Int, true, "win.Show", "uu"}, U,
    {1, drawfx::Ret::Int, true, "win.SetTitle", "s"}, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "win.GetClientRect", "i"}, U, U, U, U,
    {0, drawfx::Ret::Int, true, "win.IsMaximized", ""},
    {0, drawfx::Ret::Int, true, "win.IsCaptionless", ""},
};
inline constexpr drawfx::Iface kWindowIface{"cIGZWin(0x5b637209)", 36, kWinSlots};

// Its canvas interface (0xbb6377fa): 40 the HWND, 41, 43.
inline constexpr drawfx::Slot kWindowCanvasSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "wc.GetHwnd", ""},
    {0, drawfx::Ret::Int, true, "wc.Slot41", ""}, U,
    {2, drawfx::Ret::Int, true, "wc.Slot43", "uu"},
};
inline constexpr drawfx::Iface kWindowCanvasIface{"cICanvasW32", 44, kWindowCanvasSlots};

inline constexpr drawfx::Slot kSystemSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, {3, drawfx::Ret::Int, true, "dx.Connect", "uiu"}};
inline constexpr drawfx::Iface kSystemIface{"cIGraphicSystemDX", 9, kSystemSlots};

inline constexpr drawfx::Slot kStringSlots[] = {kQI, kAddRefSlot, kReleaseSlot, U, U, {0, drawfx::Ret::Int, true, "str.ToChar", ""}};
inline constexpr drawfx::Iface kStringIface{"cIGZString", 6, kStringSlots};

// A window message for PostWindowMessage: 3 its type, 4 its word count, 108 its words.
constexpr drawfx::Slot kWindowMessageSlotsArray[109] = {
    kQI, kAddRefSlot, kReleaseSlot, {0, drawfx::Ret::Int, true, "wm.GetType", ""}, {0, drawfx::Ret::Int, true, "wm.Count", ""},
};
consteval auto MakeWindowMessageSlots()
{
    struct Table {
        drawfx::Slot s[109];
    } t{};
    for (unsigned i = 0; i < 109; ++i) t.s[i] = i < 5 ? kWindowMessageSlotsArray[i] : U;
    t.s[108] = {0, drawfx::Ret::Int, true, "wm.Words", ""};
    return t;
}
inline constexpr auto kWindowMessageTable = MakeWindowMessageSlots();
inline constexpr drawfx::Iface kWindowMessageIface{"cIGZMessage(0x8006)", 109, kWindowMessageTable.s};

// Another message for IsType / IsTypeBelow: 4 GetType.
inline constexpr drawfx::Slot kOtherSlots[] = {kQI, kAddRefSlot, kReleaseSlot, U, {0, drawfx::Ret::Int, true, "other.GetType", ""}};
inline constexpr drawfx::Iface kOtherIface{"cIGZMessage2", 5, kOtherSlots};

inline constexpr drawfx::Slot kListenerSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot,
    {4, drawfx::Ret::Int, true, "OnEvent", "uuii"},
    {3, drawfx::Ret::Int, true, "OnQuery", "uui"},
};
inline constexpr drawfx::Iface kListenerIface{"cIAbstractBehavior", 5, kListenerSlots};

enum : uint32_t {
    kServerId = 0xe0100, kServerOldId, kWindowId, kWindowOldId, kWindowCanvasId, kSystemId, kTitleId, kWindowMessageId,
    kOtherId, kListenerId0 = 0xe0200
};
constexpr unsigned kListeners = 3;
constexpr uint32_t kListenEvents[kListeners] = {0xca7ccbbc, 0x1b637482, 0x11};
constexpr uint32_t kHeardAs[kListeners] = {0x61, 0x62, 0x63};

drawfx::Fake gServer, gServerOld, gWindow, gWindowOld, gWindowCanvas, gSystem, gTitle, gWindowMessage, gOther,
    gListener[kListeners], gSource;

uint8_t gMarks[16]; // tokens (the pools' block lists)
struct Data {
    uint32_t message[8]; // event data
    uint32_t words[8];   // out-parameters, a window message's words
} gData;

// ---- the blocks ----
alignas(16) uint8_t gCanvasBlock[kSize];
uint8_t *gCanvas = gCanvasBlock;
alignas(16) uint8_t gElements[3][kElements][kElementBytes];

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
    kEventCount, kEventIds, kEventAttribute, kEmptySlots, kOnEvent, kCanvasSlots, kPost, kW32Slots, kMessagingSlots,
    kServiceSlots, kInit, kShutdown, kDestruct, kReleaseLast, kDelete, kCanvasMessage, kMouseMessage, kKeyMessage,
    kOpCount
};
const char *const kOpNames[kOpCount] = {
    "QueryInterface", "face refs", "AddRef", "Release", "OnQuery", "NotifyListeners", "AskListeners", "Connect",
    "Disconnect", "IsActive", "GetEventCount", "GetEventIds", "GetEventAttribute", "Slot7/Slot10", "OnEvent",
    "canvas slots", "PostWindowMessage", "W32 slots", "messaging slots", "service slots", "Init", "Shutdown",
    "destructor", "Release (last)", "delete", "canvas message", "mouse message", "key message"};
bool CanvasGone(uint32_t op) { return op == kReleaseLast || op == kDelete; }
bool Destroys(uint32_t op) { return op == kDestruct || op == kReleaseLast || op == kDelete; }
int MessageClass(uint32_t op) { return op == kCanvasMessage ? 0 : op == kMouseMessage ? 1 : op == kKeyMessage ? 2 : -1; }

struct Case {
    uint32_t op;
    uint8_t canvas[kSize];
    uint8_t elements[3][kElements][kElementBytes];
    uint32_t window, server;     // 0 null, 1 the fake, 2 the old fake
    bool hasServer, hasWindow, hasSystem, noFramework, keep;
    uint32_t connections, listenerAnswers;
    uint32_t live[3][4];         // the live message's count, type and two words
    Data data;
    uint32_t args[8];
};

struct Script {
    const Case *c = nullptr;
} gScript;

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

// ---- handlers ----
// The framework's GetSystemService: the message server, the window, cGraphicSystemDX, each as the case says.
uint32_t __fastcall ServiceQuery(void *, void *, uint32_t first, uint32_t second, void **out)
{
    drawfx::Note("service.Query", {first, second});
    const Case &c = *gScript.c;
    drawfx::Fake *f = nullptr;
    if (first == 0x4fa845b && second == 0x652294c7 && c.hasServer) f = &gServer;
    if (first == 0xfb6372ee && second == 0x5b637209 && c.hasWindow) f = &gWindow;
    if (first == 0x85522803 && second == 0xa55227bb && c.hasSystem) f = &gSystem;
    if (!f) return 0;
    ++f->refs;
    *out = f;
    return 1;
}

// The message server's Send: the message read through its slots; a reference kept when the case says.
uint32_t ServerSend(drawfx::Fake &, const uint32_t *a)
{
    void *const m = reinterpret_cast<void *>(uintptr_t(a[0]));
    const uint32_t type = Call<uint32_t>(m, 4);
    drawfx::Note("sent", {type, Get<uint32_t>(m, 4)});
    drawfx::Note("code", Call<uint32_t>(m, 9));
    if (type == cGZMouseMessage::kType) {
        cGZMousePosition p{};
        drawfx::Note("position", {Norm(Call<cGZMousePosition *>(m, 10, &p)), uint32_t(p.mX), uint32_t(p.mY)});
        drawfx::Note("wheel/keys", {Call<uint32_t>(m, 11), Call<uint32_t>(m, 12)});
    } else if (type == cGZKeyMessage::kType) {
        drawfx::Note("key", {Call<uint32_t>(m, 10), Call<uint32_t>(m, 11), Call<uint32_t>(m, 12)});
    }
    if (gScript.c->keep) drawfx::Note("kept", Call<uint32_t>(m, 1));
    return gScript.c->args[7] & 1;
}
uint32_t WindowRect(drawfx::Fake &, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 4; ++i) out[i] = gScript.c->args[i] % 2000;
    return a[0];
}
uint32_t SetPos(drawfx::Fake &, const uint32_t *) { return gScript.c->args[6] % 3; }
uint32_t Post(drawfx::Fake &, const uint32_t *) { return gScript.c->args[5] % 3; }
uint32_t ListenerOnQuery(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
// The canvas while the world connects it (an embedded fake answering the behaviour type with the block).
uint32_t SourceQuery(drawfx::Fake &self, const uint32_t *a)
{
    if (a[0] != nGZEvent::cIAbstractBehaviorType::kIid || !a[1]) return 0;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = self.owner;
    return 1;
}

// ---- statics: the registry map placed at its static, the pools, the HWND cell, the event manager cell ----
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
    Cells() : manager_(*GameAddress<uint32_t>(kEventManagerCell)), hwnd_(*GameAddress<uint32_t>(kHwndCell))
    {
        for (unsigned p = 0; p < 3; ++p) std::memcpy(pools_[p], GameAddress<void>(kPoolRva[p]), 16);
    }
    ~Cells()
    {
        *GameAddress<uint32_t>(kEventManagerCell) = manager_;
        *GameAddress<uint32_t>(kHwndCell) = hwnd_;
        for (unsigned p = 0; p < 3; ++p) std::memcpy(GameAddress<void>(kPoolRva[p]), pools_[p], 16);
    }
    Cells(const Cells &) = delete;
    Cells &operator=(const Cells &) = delete;

private:
    uint32_t manager_, hwnd_;
    uint8_t pools_[3][16];
};

struct StandIns {
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
};

// ---- seeding ----
uint32_t Pick(std::mt19937 &gen, unsigned oneIn) { return gen() % oneIn == 0; }

// The ops that use the window without a check: the +8 face's window slots (0x149ce0, 0x149cc0, 0x1496c0, 0x14a270,
// 0x14a240) and every HWND use (0x149700 / 0x149400 / 0x148fc0 dereference +0x28).
bool NeedsWindow(uint32_t op) { return op == kCanvasSlots || op == kPost || op == kW32Slots; }

void SeedCase(std::mt19937 &gen, Case &c, uint32_t op)
{
    std::memset(&c, 0, sizeof(c));
    c.op = op;
    for (uint8_t &b : c.canvas) b = uint8_t(gen());
    for (auto &pool : c.elements)
        for (auto &e : pool)
            for (uint8_t &b : e) b = uint8_t(gen());
    // The count: 2-5 (1 for the last Release), the active bit either way.
    Put<uint32_t>(c.canvas, 4, (op == kReleaseLast ? 1 : 2 + gen() % 4) | (Pick(gen, 2) ? 0x80000000u : 0));
    c.canvas[0x1c] = uint8_t(gen() % 2);
    c.canvas[0x1d] = uint8_t(gen() % 2);
    c.window = NeedsWindow(op) ? 1 + gen() % 2 : gen() % 3;
    // OnEvent sends through the message server without a check (0x149770's server calls): always one there.
    c.server = op == kOnEvent ? 1 + gen() % 2 : gen() % 3;
    c.hasServer = !Pick(gen, 4);
    c.hasWindow = !Pick(gen, 4);
    c.hasSystem = !Pick(gen, 3);
    c.noFramework = (op == kInit) && Pick(gen, 6);
    c.keep = Pick(gen, 3);
    c.connections = gen() % 64;
    c.listenerAnswers = gen() % 8;
    for (unsigned p = 0; p < 3; ++p) {
        c.live[p][0] = gen() % 4;
        c.live[p][1] = Pick(gen, 3) ? gen() : kMessageType[p];
        c.live[p][2] = gen();
        c.live[p][3] = gen();
    }
    for (uint32_t &w : c.data.message) w = gen();
    for (uint32_t &w : c.data.words) w = gen();
    c.data.message[1] = gen() % 8;
    for (uint32_t &w : c.args) w = gen();
}

// ---- a side's world ----
cConnectionManager *gManager = nullptr;

struct VTables {
    void *primary, *face8, *faceC, *face10, *face14;
    void *message[3];
};

void ResetFakes(const Case &c)
{
    gServer.Reset<kServerIface>(kServerId);
    gServer.On(3, &ServerSend);
    gServerOld.Reset<kServerIface>(kServerOldId);
    gServerOld.On(3, &ServerSend);
    for (drawfx::Fake *w : {&gWindow, &gWindowOld}) {
        w->Reset<kWindowIface>(w == &gWindow ? kWindowId : kWindowOldId);
        w->AddFace(0xbb6377fa, gWindowCanvas);
        w->Returns(8, 0x55u);
        w->Returns(15, c.args[1]);
        w->Returns(17, c.args[2]);
        w->On(29, &WindowRect);
        w->Returns(34, c.args[3] % 2);
        w->Returns(35, c.args[4] % 2);
    }
    gWindowCanvas.Reset<kWindowCanvasIface>(kWindowCanvasId);
    gWindowCanvas.Returns(40, 0x4b000010u + c.args[0] % 4);
    gWindowCanvas.Returns(41, c.args[1]);
    gWindowCanvas.Returns(43, c.args[2] % 2);
    gSystem.Reset<kSystemIface>(kSystemId);
    gTitle.Reset<kStringIface>(kTitleId);
    gTitle.Returns(5, "the title");
    gWindowMessage.Reset<kWindowMessageIface>(kWindowMessageId);
    gWindowMessage.Returns(3, c.args[3] % 3 ? 0x8006u : c.args[4]);
    gWindowMessage.Returns(4, c.args[5] % 6);
    gWindowMessage.Returns(108, &gData.words[0]);
    gOther.Reset<kOtherIface>(kOtherId);
    gOther.Returns(4, c.args[6] % 3 ? c.live[0][1] + c.args[6] % 3 - 1 : c.args[6]);
    for (unsigned i = 0; i < kListeners; ++i) {
        gListener[i].Reset<kListenerIface>(kListenerId0 + i);
        gListener[i].AddFace(nGZEvent::cIAbstractBehaviorType::kIid, gListener[i]);
        gListener[i].On(4, &ListenerOnQuery);
        gListener[i].value[0] = (c.listenerAnswers >> i) & 1;
    }
    gSource.Reset<kListenerIface>(kListenerId0 + 9);
    gSource.On(0, &SourceQuery);
}

drawfx::Fake *Choice(uint32_t which, drawfx::Fake &fake, drawfx::Fake &old)
{
    return which == 1 ? &fake : which == 2 ? &old : nullptr;
}

// The pools: elements 0 and 1 free (in that order), element 2 a live message of the side's class.
void BuildPools(const Case &c, const VTables &vt)
{
    std::memcpy(gElements, c.elements, sizeof(gElements));
    for (unsigned p = 0; p < 3; ++p) {
        auto *const pool = GameAddress<uint32_t>(kPoolRva[p]);
        pool[0] = kMessageSize[p];
        pool[1] = kMessageSize[p] * 0x40;
        pool[2] = Word(&gMarks[p]);
        pool[3] = Word(gElements[p][0]);
        Put<uint32_t>(gElements[p][0], 0, Word(gElements[p][1]));
        Put<uint32_t>(gElements[p][1], 0, 0);
        uint8_t *const live = gElements[p][2];
        Put<void *>(live, 0, vt.message[p]);
        Put<uint32_t>(live, 4, c.live[p][0]);
        Put<uint32_t>(live, 8, c.live[p][1]);
        drawfx::AddRange(gElements[p], sizeof(gElements[p]), kElementTag | p << 8);
    }
}

void BuildSide(const Case &c, d3dfx::ErrorLog &errors, impfx::Imports &imp, const VTables &vt)
{
    (void)errors;
    drawfx::ResetAll();
    imp.BeginSide();
    gScript = {&c};
    std::memcpy(gCanvas, c.canvas, kSize);
    gData = c.data;
    ResetFakes(c);
    Put<void *>(gCanvas, 0x28, Choice(c.window, gWindow, gWindowOld));
    Put<void *>(gCanvas, 0x2c, Choice(c.server, gServer, gServerOld));
    drawfx::AddRange(gCanvas, kSize, kCanvasTag);
    drawfx::AddRange(&gData, sizeof(gData), kDataTag);
    drawfx::AddRange(gMarks, sizeof(gMarks), kMarkTag);
    BuildPools(c, vt);
    *GameAddress<uint32_t>(kHwndCell) = 0x4c000001;
    gManager = cConnectionManagerAdapters::Construct(::operator new(cConnectionManager::kSize), nullptr);
    drawfx::AddRange(gManager, cConnectionManager::kSize, kConnTag);
    *GameAddress<void *>(kEventManagerCell) = gManager;
    gSource.Embed(gCanvas);
    for (unsigned i = 0; i < kListeners; ++i)
        if ((c.connections >> i) & 1)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(gCanvas), kListenEvents[i], reinterpret_cast<cIGZUnknown *>(&gListener[i]),
                kHeardAs[i]);
    for (unsigned i = 0; i < kListeners; ++i)
        if ((c.connections >> (3 + i)) & 1)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(gCanvas), kListenEvents[i],
                reinterpret_cast<cIGZUnknown *>(&gListener[(i + 1) % kListeners]), kHeardAs[(i + 1) % kListeners]);
    Put<void *>(gCanvas, 0, vt.primary);
    Put<void *>(gCanvas, 8, vt.face8);
    Put<void *>(gCanvas, 0xc, vt.faceC);
    Put<void *>(gCanvas, 0x10, vt.face10);
    Put<void *>(gCanvas, 0x14, vt.face14);
    drawfx::gLog.clear();
}

void DestroySide()
{
    cConnectionManagerAdapters::Destruct(gManager, nullptr);
    ::operator delete(gManager);
    gManager = nullptr;
}

void NoteRefs()
{
    const drawfx::Fake *all[] = {&gServer, &gServerOld, &gWindow, &gWindowOld, &gWindowCanvas, &gSystem, &gTitle,
                                 &gWindowMessage, &gOther, &gListener[0], &gListener[1], &gListener[2]};
    for (const drawfx::Fake *f : all) drawfx::Note("refs", {f->id, uint32_t(f->refs)});
}

void NoteConnections()
{
    auto *m = static_cast<nGZEvent::cIConnectionManager *>(gManager);
    drawfx::Note("connected", m->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(gCanvas)));
    for (unsigned i = 0; i < kListeners; ++i)
        drawfx::Note("from", {i, m->CountFrom(reinterpret_cast<cIGZUnknown *>(gCanvas), kListenEvents[i])});
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

const uint32_t kIids[] = {cIGZCanvas::kIid, cIGZSystemService::kIID, cIGZCanvasMessaging::kIid, cIGZCanvasW32::kIid,
                          1, 0x3b253dc7, 0xfb859324, 0xa3460deb, 0};
constexpr unsigned kIidCount = sizeof(kIids) / sizeof(kIids[0]);
bool IsFaceIid(uint32_t iid) { return iid == kIids[0] || iid == kIids[1] || iid == kIids[2] || iid == kIids[3]; }

const uint32_t kEventTypes[] = {cGZCanvas::kConnectionChangedEvent, cGZCanvas::kRenderSceneEvent, cGZCanvas::kCanvasEvent,
                                cGZCanvas::kMouseEvent, cGZCanvas::kKeyEvent, cGZCanvas::kDeviceStateEvent, 0};

// ---- one operation, through the side's vtables ----
void RunMessage(const Case &c, unsigned p)
{
    void *const m = gElements[p][2];
    const uint32_t *a = c.args;
    // QueryInterface: 1, cIGZMessage2, its own, another message's, anything.
    const uint32_t iids[] = {1, cIGZMessage2::kIid, kMessageIid[p], kMessageIid[(p + 1) % 3], a[1]};
    void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
    drawfx::Note("qi", {Call<bool>(m, 0, iids[a[0] % 5], &out), Norm(out)});
    Note("addref", Call<uint32_t>(m, 1));
    // Create: a fresh message of the class as the interface asked for (or freed back to the pool).
    out = nullptr;
    drawfx::Note("create", {Call<bool>(m, 3, iids[a[2] % 5], &out), Norm(out)});
    if (out && a[2] % 3 == 0) Note("created release", Call<uint32_t>(out, 2));
    Note("type", Call<uint32_t>(m, 4));
    Note("is", Call<bool>(m, 6, static_cast<void *>(&gOther)));
    Note("below", Call<bool>(m, 7, static_cast<void *>(&gOther)));
    if (a[3] % 2) Call<void>(m, 5, a[4]);
    Note("type2", Call<uint32_t>(m, 4));
    switch (p) {
    case 0:
        Note("code", Call<uint32_t>(m, 9));
        if (a[5] % 2) Call<void>(m, 8, a[6]);
        Note("code2", Call<uint32_t>(m, 9));
        break;
    case 1: {
        cGZMousePosition before{}, after{};
        drawfx::Note("position", {Norm(Call<cGZMousePosition *>(m, 10, &before)), uint32_t(before.mX), uint32_t(before.mY)});
        drawfx::Note("words", {Call<uint32_t>(m, 9), Call<uint32_t>(m, 11), Call<uint32_t>(m, 12)});
        const cGZMousePosition at{int32_t(a[4]), int32_t(a[5])};
        if (a[6] % 2) Call<void>(m, 8, a[1], &at, a[2], a[3]);
        Call<cGZMousePosition *>(m, 10, &after);
        drawfx::Note("set", {uint32_t(after.mX), uint32_t(after.mY), Call<uint32_t>(m, 9), Call<uint32_t>(m, 11), Call<uint32_t>(m, 12)});
        break;
    }
    case 2:
        drawfx::Note("words", {Call<uint32_t>(m, 9), Call<uint32_t>(m, 10), Call<uint32_t>(m, 11), Call<uint32_t>(m, 12)});
        if (a[6] % 2) Call<void>(m, 8, a[1], a[2], a[3], a[4]);
        drawfx::Note("set", {Call<uint32_t>(m, 9), Call<uint32_t>(m, 10), Call<uint32_t>(m, 11), Call<uint32_t>(m, 12)});
        break;
    }
    // The end: released (maybe to the last reference) or destroyed with a flag set.
    const unsigned destroySlot = p == 0 ? 10 : 13;
    switch (a[7] % 4) {
    case 0: Note("release", Call<uint32_t>(m, 2)); break;
    case 1:
        // Twice only while the message outlives the first (a count of 2 or more before the AddRef above).
        Note("release", Call<uint32_t>(m, 2));
        if (c.live[p][0] >= 2) Note("release", Call<uint32_t>(m, 2));
        break;
    case 2: Call<void *>(m, destroySlot, a[0] % 8); break;
    case 3: break;
    }
}

void Run(const Case &c)
{
    void *const self = gCanvas;
    void *const face8 = gCanvas + 8;
    void *const faceC = gCanvas + 0xc;
    void *const face10 = gCanvas + 0x10;
    void *const face14 = gCanvas + 0x14;
    const uint32_t *a = c.args;
    switch (c.op) {
    case kQueryInterface: {
        const uint32_t iid = kIids[a[0] % kIidCount] ? kIids[a[0] % kIidCount] : a[1];
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        // The four faces are stored without a null check (0x149de0's store): those always have an out.
        const bool ok = Call<bool>(self, 0, iid, a[2] % 5 || IsFaceIid(iid) ? &out : nullptr);
        drawfx::Note("ret", {ok, Norm(out)});
        break;
    }
    case kFaceRefs: {
        void *const faces[] = {face8, faceC, face10, face14};
        for (void *f : faces) {
            const uint32_t iid = kIids[a[0] % kIidCount] ? kIids[a[0] % kIidCount] : a[1];
            void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
            const bool ok = Call<bool>(f, 0, iid, &out);
            drawfx::Note("qi", {ok, Norm(out)});
            // A counted answer given back (0xfb859324's is not counted).
            if (ok && iid != 0xfb859324) Note("release", Call<uint32_t>(f, 2));
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
        const uint32_t type = kEventTypes[a[0] % 7] ? kEventTypes[a[0] % 7] : a[1];
        Call<void>(self, 3, type, a[2], static_cast<void *>(gData.message), a[3]);
        break;
    }
    case kCanvasSlots: {
        Note("3", Call<bool>(face8, 3, a[0], a[1], a[2]));
        Note("4", Call<bool>(face8, 4, a[3]));
        Note("5", Call<bool>(face8, 5));
        Note("7", Call<bool>(face8, 7));
        Note("8", Call<bool>(face8, 8));
        Note("9", Call<bool>(face8, 9));
        Note("10", Call<bool>(face8, 10));
        Note("11", Call<int32_t>(face8, 11, int32_t(a[0] % 900), int32_t(a[1] % 900), int32_t(a[2] % 2000), int32_t(a[3] % 2000)));
        cGZCanvasRect r{};
        Note("12", Norm(Call<cGZCanvasRect *>(face8, 12, &r)));
        drawfx::Note("rect", {uint32_t(r.mLeft), uint32_t(r.mTop), uint32_t(r.mRight), uint32_t(r.mBottom)});
        Note("13", Call<uint32_t>(face8, 13));
        Call<void>(face8, 14);
        Note("15", Call<uint32_t>(face8, 15, static_cast<void *>(&gTitle)));
        Call<void>(face8, 16);
        Call<void>(face8, 17);
        Note("18", Call<uint32_t>(face8, 18));
        Note("19", Call<uint32_t>(face8, 19));
        Note("20", Call<uint32_t>(face8, 20));
        Note("22", Norm(Call<void *>(face8, 22)));
        break;
    }
    case kPost: Note("21", Call<bool>(face8, 21, a[7] % 5 ? static_cast<void *>(&gWindowMessage) : nullptr)); break;
    case kW32Slots:
        Note("3", Norm(Call<void *>(faceC, 3)));
        Note("4", Norm(Call<void *>(faceC, 4)));
        Note("5", Call<uint16_t>(faceC, 5));
        Note("6", Call<bool>(faceC, 6, a[4], a[5]));
        break;
    case kMessagingSlots: Note("3", Norm(Call<void *>(face10, 3))); break;
    case kServiceSlots:
        Note("3", Call<uint32_t>(face14, 3));
        Note("5", Call<uint32_t>(face14, 5));
        Note("6", Call<bool>(face14, 6));
        if (a[0] % 2) Call<void>(face14, 4, a[1]);
        if (a[2] % 2) Call<void>(face14, 7, a[3] % 2 != 0);
        Note("3", Call<uint32_t>(face14, 3));
        Note("6", Call<bool>(face14, 6));
        Note("10", Call<bool>(face14, 10, a[4]));
        Note("11", Call<bool>(face14, 11, a[5]));
        Note("12", Call<uint32_t>(face14, 12));
        break;
    case kInit: Note("ret", Call<bool>(face14, 8)); break;
    case kShutdown: Note("ret", a[0] % 2 ? Call<bool>(face8, 6) : Call<bool>(face14, 9)); break;
    case kDestruct: Call<void *>(self, 15, 0u); break;
    case kReleaseLast: Note("ret", Call<uint32_t>(self, 2)); break;
    case kDelete: Call<void *>(self, 15, 1u); break;
    case kCanvasMessage: RunMessage(c, 0); break;
    case kMouseMessage: RunMessage(c, 1); break;
    case kKeyMessage: RunMessage(c, 2); break;
    }
}

struct Result {
    drawfx::Log log;
    std::vector<uint8_t> canvas, elements;
    uint32_t pools[3][4];
    uint32_t hwnd;
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

// The elements, a message's vtable word compared as its class.
std::vector<uint8_t> Elements(const VTables &vt)
{
    std::vector<uint8_t> bytes(reinterpret_cast<const uint8_t *>(gElements), reinterpret_cast<const uint8_t *>(gElements) + sizeof(gElements));
    for (unsigned p = 0; p < 3; ++p)
        for (unsigned e = 0; e < kElements; ++e)
            for (unsigned k = 0; k < 3; ++k)
                if (Get<void *>(gElements[p][e], 0) == vt.message[k]) Put<uint32_t>(bytes.data() + (p * kElements + e) * kElementBytes, 0, 0xee00u + k);
    return bytes;
}

template <class R> Result Side(const Case &c, d3dfx::ErrorLog &errors, impfx::Imports &imp, const VTables &vt, R run)
{
    gCanvas = CanvasGone(c.op) ? static_cast<uint8_t *>(::operator new(kSize)) : gCanvasBlock;
    BuildSide(c, errors, imp, vt);
    long long blocks = 0, bytes = 0;
    drawfx::gLog = drawfx::Log();
    drawfx::gLog.reserve(512);
    void *const framework = *GameAddress<void *>(d3dfx::ErrorLog::kServiceRva);
    if (c.noFramework) *GameAddress<void *>(d3dfx::ErrorLog::kServiceRva) = nullptr;
    {
        difftest::AllocationCounter counter;
        run();
        counter.Stop();
        blocks = counter.Blocks();
        bytes = counter.Bytes();
    }
    *GameAddress<void *>(d3dfx::ErrorLog::kServiceRva) = framework;
    NoteRefs();
    if (!Destroys(c.op)) NoteConnections();
    Result r;
    r.log = drawfx::gLog;
    if (!CanvasGone(c.op)) r.canvas = Without(gCanvas, kSize, {0, 8, 0xc, 0x10, 0x14});
    r.elements = Elements(vt);
    for (unsigned p = 0; p < 3; ++p) std::memcpy(r.pools[p], GameAddress<void>(kPoolRva[p]), 16);
    r.hwnd = *GameAddress<uint32_t>(kHwndCell);
    r.data = gData;
    r.blocks = blocks;
    r.bytes = bytes;
    DestroySide();
    gCanvas = gCanvasBlock;
    return r;
}

// Our vtable words (a constructed canvas's and messages').
VTables OurVTables()
{
    cGZCanvas *const canvas = new cGZCanvas;
    const auto *const b = reinterpret_cast<const uint8_t *>(static_cast<cGZCanvasBehavior *>(canvas));
    VTables vt{Get<void *>(b, 0), Get<void *>(b, 8), Get<void *>(b, 0xc), Get<void *>(b, 0x10), Get<void *>(b, 0x14), {}};
    delete canvas;
    cGZCanvasMessage m0;
    cGZMouseMessage m1;
    cGZKeyMessage m2;
    vt.message[0] = Get<void *>(&m0, 0);
    vt.message[1] = Get<void *>(&m1, 0);
    vt.message[2] = Get<void *>(&m2, 0);
    return vt;
}

VTables GameVTables()
{
    VTables vt{GameAddress<void>(kVTableRva), GameAddress<void>(kFace8Rva), GameAddress<void>(kFaceCRva),
               GameAddress<void>(kFace10Rva), GameAddress<void>(kFace14Rva), {}};
    for (unsigned k = 0; k < 3; ++k) vt.message[k] = GameAddress<void>(kMessageVTableRva[k]);
    return vt;
}

struct World {
    RegistryScope registry;
    shape::StandIn registryStand{0x149ef0, &cGZCanvasBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kRegistryMap>};
    Cells cells;
    StandIns stands;
    d3dfx::ErrorLog errors;
    impfx::Imports imp;
    explicit World(difftest::Context &ctx) : errors(ctx), imp(ctx)
    {
        d3dfx::detail::gServiceVTable[5] = reinterpret_cast<void *>(&ServiceQuery);
        IMPFX_ADD(imp, SetWindowPos, "iiuuuuu").On(&SetPos);
        IMPFX_ADD(imp, PostMessageA, "iuuu").On(&Post);
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
    World world(ctx);
    const VTables ours = OurVTables();
    const VTables game = GameVTables();
    std::mt19937 gen(seed);
    std::set<std::string> seen;
    for (uint32_t op : ops) {
        for (unsigned round = 0; round < rounds; ++round) {
            auto c = std::make_unique<Case>();
            SeedCase(gen, *c, op);
            const Result a = Side(*c, world.errors, world.imp, game, [&] { Run(*c); });
            const Result b = Side(*c, world.errors, world.imp, ours, [&] { Run(*c); });
            Seen(a, seen);
            const char *label = kOpNames[op];
            if (!drawfx::Same(ctx, a.log, b.log, label, round)) return;
            if (std::memcmp(&a.data, &b.data, sizeof(Data)) != 0) {
                ctx.Fail("%s round %u: the out-parameters differ", label, round);
                return;
            }
            if (a.canvas != b.canvas || a.elements != b.elements || std::memcmp(a.pools, b.pools, sizeof(a.pools)) != 0 ||
                a.hwnd != b.hwnd || a.blocks != b.blocks || a.bytes != b.bytes) {
                size_t at = 0;
                while (at < a.canvas.size() && at < b.canvas.size() && a.canvas[at] == b.canvas[at]) ++at;
                size_t eat = 0;
                while (eat < a.elements.size() && eat < b.elements.size() && a.elements[eat] == b.elements[eat]) ++eat;
                ctx.Fail("%s round %u: canvas %d (compared byte %zu) elements %d (byte %zu) pools %d hwnd %d blocks %lld/%lld "
                         "bytes %lld/%lld",
                         label, round, a.canvas == b.canvas, at, a.elements == b.elements, eat,
                         std::memcmp(a.pools, b.pools, sizeof(a.pools)) == 0, a.hwnd == b.hwnd, a.blocks, b.blocks, a.bytes,
                         b.bytes);
                return;
            }
        }
    }
    for (const char *need : needs)
        if (!seen.count(need)) ctx.Fail("the sweep never reached '%s' (a vacuous path)", need);
}

template <size_t N> bool SlotsAre(void *vtable, const uint32_t (&rvas)[N])
{
    for (size_t i = 0; i < N; ++i)
        if (static_cast<void *const *>(vtable)[i] != GameAddress<void>(rvas[i])) return false;
    return true;
}

} // namespace

// Construction (0x1495f0 with GameNew handing out a block of seeded bytes; ours constructed over a copy of the same
// bytes): every byte the creator writes, the original's vtables and their slots.
DIFF_TEST(cGZCanvas_Create, 0x1495f0)
{
    const VTables game = GameVTables();
    if (!SlotsAre(game.primary, kPrimarySlots) || !SlotsAre(game.face8, kFace8Slots) || !SlotsAre(game.faceC, kFaceCSlots) ||
        !SlotsAre(game.face10, kFace10Slots) || !SlotsAre(game.face14, kFace14Slots) ||
        !SlotsAre(game.message[0], kCanvasMessageSlots) || !SlotsAre(game.message[1], kMouseMessageSlots) ||
        !SlotsAre(game.message[2], kKeyMessageSlots))
        ctx.Fail("the original's vtables do not hold the slots this test names");
    std::mt19937 gen(0x1495f0);
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
        void *const made = ctx.Original<void *(__cdecl *)()>(0x1495f0)();
        if (made != seeded + 8) ctx.Fail("round %u: the creator's object is not the +8 face of the block it was given", round);
        ::new (ours) cGZCanvas;
        if (Without(seeded, kSize, {0, 8, 0xc, 0x10, 0x14}) != Without(ours, kSize, {0, 8, 0xc, 0x10, 0x14})) {
            ctx.Fail("round %u: the canvas differs", round);
            return;
        }
        if (Get<void *>(seeded, 0) != game.primary || Get<void *>(seeded, 8) != game.face8 || Get<void *>(seeded, 0xc) != game.faceC ||
            Get<void *>(seeded, 0x10) != game.face10 || Get<void *>(seeded, 0x14) != game.face14)
            ctx.Fail("round %u: the original's vtable words are not 0xd9c080 / 0xd9c0c4 / 0xd9c124 / 0xd9c144 / 0xd9c158", round);
    }
    // A failed allocation gives null.
    hand = nullptr;
    if (ctx.Original<void *(__cdecl *)()>(0x1495f0)() != nullptr) ctx.Fail("the creator did not give null without memory");
    // Ours: the +8 face of a fresh canvas.
    cIGZCanvas *const made = cGZCanvas::Create();
    auto *const canvas = static_cast<cGZCanvas *>(made);
    if (!made || canvas->mRefCount != 0 || canvas->mServiceId != cGZCanvas::kServiceId ||
        reinterpret_cast<uint8_t *>(made) != reinterpret_cast<uint8_t *>(static_cast<cGZCanvasBehavior *>(canvas)) + 8)
        ctx.Fail("Create: not a fresh canvas's +8 face");
    delete canvas;
}

// The behaviour slots (0-14) and the faces' QueryInterface / AddRef / Release.
DIFF_TEST(cGZCanvas_Behaviour, 0x149de0)
{
    Sweep(ctx, 0x149de0,
          {kQueryInterface, kFaceRefs, kAddRef, kRelease, kOnQuery, kNotify, kAsk, kConnect, kDisconnect, kIsActive,
           kEventCount, kEventIds, kEventAttribute, kEmptySlots},
          96, {"OnEvent", "OnQuery", "id", "attribute"});
}

// OnEvent: every event turned into its message (or nothing), sent through the message server. The mouse event's
// message is built by the mouse message's constructor 0x148da0 (called only from 0x149770).
DIFF_TEST(cGZCanvas_OnEvent, 0x149770)
{
    Sweep(ctx, 0x149770, {kOnEvent}, 600, {"server.Send", "code", "position", "key", "kept"});
}

// The +8, +0xc, +0x10 and +0x14 faces' slots.
DIFF_TEST(cGZCanvas_Slots, 0x14a210)
{
    Sweep(ctx, 0x14a210, {kCanvasSlots, kPost, kW32Slots, kMessagingSlots, kServiceSlots}, 160,
          {"win.IsMaximized", "win.IsCaptionless", "win.GetClientRect", "win.Show", "win.SetTitle", "str.ToChar",
           "wc.GetHwnd", "wc.Slot41", "wc.Slot43", "SetWindowPos", "PostMessageA", "wm.GetType", "wm.Count", "wm.Words"});
}

// Init, Shutdown, the destructors (the deleting destructor 0x148e20 ends in the template's base destructor 0x146660).
DIFF_TEST(cGZCanvas_Lifetime, 0x149a90)
{
    Sweep(ctx, 0x149a90, {kInit, kShutdown, kDestruct, kReleaseLast, kDelete}, 240,
          {"service.Query", "dx.Connect", "win.Connect", "Release", "AddRef"});
}

// The messages' slots (each class's live message in its pool).
DIFF_TEST(cGZCanvasMessage_Slots, 0x149470)
{
    Sweep(ctx, 0x149470, {kCanvasMessage}, 400, {"other.GetType"});
}
DIFF_TEST(cGZMouseMessage_Slots, 0x149560)
{
    Sweep(ctx, 0x149560, {kMouseMessage}, 400, {"other.GetType"});
}
DIFF_TEST(cGZKeyMessage_Slots, 0x1494e0)
{
    Sweep(ctx, 0x1494e0, {kKeyMessage}, 400, {"other.GetType"});
}
