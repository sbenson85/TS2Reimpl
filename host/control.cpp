// Test control for scripted runs (roadmap milestone B). It is not a replacement and none of its
// code stands in for the game's: it drives the game through the game's own cheat-command system,
// the same way on reference runs (TS2HOST_NOHOOKS: no replacements) and on runs with replacements,
// so the two can be compared.
//
// tools/game.ps1 removes ts2control.txt (next to the executable) before it starts the host, and the host
// never truncates it: a slow start must not wipe a command a scenario has already written (own-start-menu-1,
// smoke-dcd7751b, first logged 8.7 s after launch, emptied at 13.9 s). tools/scenario.ps1 appends
// commands to it, one per line; a worker thread picks up new lines and posts each to the game
// window, whose procedure is subclassed, so every command runs on the game's main thread between
// messages, like input typed into the cheat console. Results go to the host log as "control: ...".
//
// Commands:
//   cheat <text>   run <text> through the cheat system's parser (e.g. "cheat boolProp ...")
//   probe <rva>[:off]... up to four hardware breakpoints on the main thread; each hit is logged with its
//                  registers (first 50 hits per address), and with :off the 11 dwords at
//                  *(ebp + off). Diagnostic only.
//   loadlot <id> [v0] post the enter-lot request the neighbourhood's lot dialog posts (value 0 can be
//                  overridden). The loadLot cheat posts a different request that doesn't enter lots here.
//   afterframes <n> <command> run <command> when n more framework ticks have run (counted at
//                  cCTGFramework::OnTick 0xc2d427, the game's frame tick), e.g. to save at a fixed
//                  frame after entering a lot, independent of how long the frames take.
//   draws          with TS2HOST_DRAWS (and TS2HOST_SEED) set: log the generator draw counts that
//                  changed since the last "draws" (host/control_seed.cpp; a diagnostic).
//   clocks         the first time: start counting the game's clock reads (QueryPerformanceCounter,
//                  GetTickCount, timeGetTime, ...) by caller; then log the counts that changed since the
//                  last "clocks" (host/control_clocks.cpp; a diagnostic, for reference runs).
//   atframe <n> <command> run <command> at framework tick n (counted from process start).
//   aftersimframes <n> <command> run <command> when n more simulator frames (ticks in which a primitive ran)
//                  have passed: unlike ticks, these line up between runs across a lot load, e.g.
//                  "atframe 4200 aftersimframes 300 click ..." counts from the enter-lot request.
//   click <x> <y>  post a left click at client (x, y) to the game window, as tools/click.ps1 does, but
//                  at frame boundaries: the move now, the button down 2 frames later and up 2 after
//                  that, so with "atframe" a click lands at the same tick on every run.
//   simtrace <n>   write the next n SimAntics primitives run (object, node, opcode, result) to
//                  simtrace.txt next to the executable (host/control_simtrace.cpp; a diagnostic).
//   mark <text>    log "control: mark <text>" with the frame counts: a checkpoint for until: steps,
//                  e.g. "atframe 4200 aftersimframes 600 mark lot-running".
//   frameclock     with TS2HOST_FRAMECLOCK set: log the frame clock's state (host/control_frameclock.cpp).
//   devlog <n> [label] with TS2HOST_DEVLOG set: write full call lists for this frame and the n-1 after it, under
//                  <label> (tools/devlog.py compare --align <label> lines runs up by it), e.g.
//                  "atframe 4200 aftersimframes 300 devlog 3 lot". "devlog mark <label>" only notes the frame.
//   wintree [label] append the window tree (class, id, area, visible, enabled, children; no pointers) to
//                  ts2wintree.txt next to the executable, under "wintree <frame> <label>" (host/control_wintree.cpp).
//   services [label] append every system service the framework holds (id, priority, tick/idle, class, each face's
//                  vtable as image RVA or "ours" offset, slots pointing into the image) to ts2services.txt next to the
//                  executable, under "services <frame> <label>", ending "N services: X ours, Y image, Z mixed (ours
//                  with image slots)", which is also logged (host/control_services.cpp; tools/services.py; milestone I),
//                  e.g. "atframe 900 services main-menu".
//   coverage <label> with TS2HOST_COVERAGE set: start a new first-call round labelled "<label> frame <n>" (every
//                  original function hit so far gets its breakpoint back; src/hook/coverage.h), e.g.
//                  "atframe 900 coverage menu-idle" so the next round shows what a steady menu frame runs.
//   post <id> [v0 [v1]] post a cRZMessage2Standard with that id and values (default 0) to the message
//                  server, as UI code does; e.g. "post 0xacfd5984" is what the options panel's save
//                  button posts (cTSWinProcOptionsPanel slot 25, 0x64a0da; values 0 and 0).
//   quit [value]   post the game's own quit request (id 0x4beb45bb, value 0 = 0x40000: no dialog, no save), as the
//                  options panel's Quit does once confirmed and as the launcher's shutdown request does. The game
//                  leaves its state and quits through its framework, so WinMain returns and the CRT's exit (and the
//                  atexit list) runs. Logs "control: quit: posting ..." when taken. Combine with afterframes etc.
#include "control.h"

#include "game/game.h"
#include "gz/FrameworkShape.h"
#include "hook/coverage.h"
#include "hook/log.h"

#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

// g_Framework, its GetSystemService slot, the cheat system's service and interface ids (the game's
// own getter passes these), the cheat system's slot that returns its parser, and the parser's
// ExecuteCommand(parser, text) (cdecl). RVAs are 1.18.0.187.
constexpr uint32_t kGlobalFramework = 0x10c9378;
constexpr int kFrameworkSlotGetSystemService = 5;
constexpr uint32_t kCheatSystemId = 0x2a4915ea, kCheatSystemIid = 0x4a490e00;
constexpr int kCheatSystemSlotParser = 3;
constexpr uint32_t kExecuteCommand = 0x24a1e0;

// The request the neighbourhood's lot dialog posts to enter a lot (cTSWinProcLotInfoDialog slot 3,
// 0x7b5b..): a cRZMessage2Standard (operator new 0x1c03b7, constructor 0x1c033d) with id 0x2bea2063,
// value 0 = 0x3000, value 1 = -1 and, through slot 30, a cRZString (operator new 0x824632,
// constructors 0x144f5b/0x14116d) holding "*<lot id>", posted to the message server (getter
// 0x9c5a89, slot 4 MessagePost(message, 0)). The dialog also sets slot 33 to an object it gets from
// the lot; the handler (0xc32733) treats that one as optional, and it is left unset here.
constexpr uint32_t kMessageNew = 0x1c03b7, kMessageConstruct = 0x1c033d, kMessageSize = 0x2c;
constexpr uint32_t kStringNew = 0x824632, kStringConstruct = 0x14116d, kStringSize = 0x20;
constexpr uint32_t kMessageServer = 0x9c5a89;
constexpr uint32_t kMsgLoadLot = 0x2bea2063;

// The game's quit request: a cRZMessage2Standard with id 0x4beb45bb and only value 0 set (slot 11), posted to the message
// server. The options panel's Quit button posts it (cTSWinProcOptionsPanel slot 25 0x649eef, control 0xa220; 0x40000
// once its "quit?" dialog was answered yes), and so does the launcher's shutdown request (cTSAppFW::PreTranslateMessage
// 0xc296b4, always 0x40000). cTSGameStateController::DoMessage 0xc31a9e takes it (0xc32d26): with value 0 present and the
// controller's +0x7c count above 0 it passes value 0 to 0xc3be33, otherwise 0x40001. There bit 0 asks the user
// (0xc2f1e8: the "quit?" / "save first?" dialogs) and bits 0x1800 save first; 0x40000 has neither, so the game leaves
// the current state without a dialog or a save and quits (in a lot: 0x6d03d837 is posted to leave it first).
constexpr uint32_t kMsgQuit = 0x4beb45bb, kQuitNoDialogNoSave = 0x40000;

constexpr UINT kMsgControl = kControlMessage;
static_assert(kControlMessage == WM_APP + 0x3a7);

std::wstring gPath, gDirectory;
std::mutex gLock;
std::vector<std::string> gCommands; // every command read so far; the message carries the index
HWND gWindow;
WNDPROC gPrevious;

template <typename Fn> Fn Slot(void *object, int slot)
{
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot]);
}

void *CheatParser()
{
    void *framework = *GameAddress<void *>(kGlobalFramework);
    if (!framework) return nullptr;
    void *cheats = nullptr;
    auto get = Slot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, kFrameworkSlotGetSystemService);
    if (!get(framework, kCheatSystemId, kCheatSystemIid, &cheats) || !cheats) return nullptr;
    // Kept for the process's lifetime, as the game's own getter keeps it in a global.
    return Slot<void *(__thiscall *)(void *)>(cheats, kCheatSystemSlotParser)(cheats);
}

DWORD RunCheat(const char *text)
{
    __try {
        void *parser = CheatParser();
        if (!parser) return 1;
        GameAddress<void(__cdecl)(void *, const char *)>(kExecuteCommand)(parser, text);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

// ---- probes ----
uint32_t gProbe[4];
int32_t gProbeDeref[4]; // ebp offset of a pointer whose first 12 dwords are logged too (0: none)
LONG gProbeHits[4];

LONG CALLBACK ProbeHandler(EXCEPTION_POINTERS *info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = info->ContextRecord;
    uint32_t rva = c->Eip - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(0)));
    for (int i = 0; i < 4; ++i) {
        if (!gProbe[i] || gProbe[i] != rva) continue;
        if (InterlockedIncrement(&gProbeHits[i]) <= 50)
        {
            Log("control: probe 0x%x eax=%08lx ecx=%08lx edx=%08lx ebx=%08lx esi=%08lx edi=%08lx", rva, c->Eax, c->Ecx, c->Edx, c->Ebx, c->Esi, c->Edi);
            // Callers: the return address at [esp] (a probe at a function's first instruction), then
            // the ebp chain (frames whose code keeps ebp as a frame pointer), as image RVAs.
            uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(0)));
            char chain[256];
            int used = 0;
            __try {
                used += std::snprintf(chain + used, sizeof(chain) - used, "%x", *reinterpret_cast<uint32_t *>(c->Esp) - base);
                auto *frame = reinterpret_cast<uint32_t *>(c->Ebp);
                for (int k = 0; k < 12 && frame && used < 200; ++k) {
                    used += std::snprintf(chain + used, sizeof(chain) - used, " %x", frame[1] - base);
                    frame = reinterpret_cast<uint32_t *>(frame[0]);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            chain[sizeof(chain) - 1] = 0;
            Log("control:   callers %s", chain);
            if (gProbeDeref[i]) {
                const uint32_t *p = nullptr;
                __try {
                    p = *reinterpret_cast<const uint32_t *const *>(c->Ebp + gProbeDeref[i]);
                    Log("control:   [ebp%+d] %p: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x", gProbeDeref[i], static_cast<const void *>(p),
                        p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10]);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    Log("control:   [ebp%+d] unreadable", gProbeDeref[i]);
                }
            }
        }
        c->EFlags |= 0x10000; // resume flag: run the instruction without hitting the breakpoint again
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Runs on the main thread (from the window procedure), so it sets its own debug registers.
void SetProbes(const char *list)
{
    static bool installed = false;
    if (!installed) {
        AddVectoredExceptionHandler(1, &ProbeHandler);
        installed = true;
    }
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(GetCurrentThread(), &c);
    c.Dr7 = 0;
    char *next = const_cast<char *>(list);
    for (int i = 0; i < 4; ++i) {
        gProbe[i] = static_cast<uint32_t>(std::strtoul(next, &next, 0));
        gProbeDeref[i] = *next == ':' ? static_cast<int32_t>(std::strtol(next + 1, &next, 0)) : 0;
        gProbeHits[i] = 0;
        DWORD address = gProbe[i] ? static_cast<DWORD>(reinterpret_cast<uintptr_t>(GameAddress<void>(gProbe[i]))) : 0;
        (&c.Dr0)[i] = address;
        if (address) c.Dr7 |= 1u << (i * 2); // local enable, execute, length 1
        Log("control: probe %d at 0x%x", i, gProbe[i]);
    }
    SetThreadContext(GetCurrentThread(), &c);
}

// Posts a cRZMessage2Standard: id, value 0, value 1 (unless setValue1 is false: the message then carries only value 0,
// as the game's quit requests do) and, when given, the cRZString at slot 30.
DWORD PostGameMessage(uint32_t id, int32_t value0, int32_t value1, const char *text, bool setValue1 = true)
{
    __try {
        void *message = GameAddress<void *__cdecl(uint32_t)>(kMessageNew)(kMessageSize);
        message = GameAddress<void *__fastcall(void *)>(kMessageConstruct)(message);
        Slot<uint32_t(__thiscall *)(void *)>(message, 1)(message); // AddRef
        Slot<void(__thiscall *)(void *, uint32_t)>(message, 5)(message, id);
        Slot<void(__thiscall *)(void *, int32_t)>(message, 11)(message, value0);
        if (setValue1) Slot<void(__thiscall *)(void *, int32_t)>(message, 16)(message, value1);
        if (text) {
            void *string = GameAddress<void *__cdecl(uint32_t)>(kStringNew)(kStringSize);
            string = GameAddress<void *__fastcall(void *, void *, const char *)>(kStringConstruct)(string, nullptr, text);
            Slot<void(__thiscall *)(void *, void *)>(message, 30)(message, string);
        }
        void *server = GameAddress<void *__cdecl()>(kMessageServer)();
        Slot<bool(__thiscall *)(void *, void *, uint32_t)>(server, 4)(server, message, 0);
        Slot<uint32_t(__thiscall *)(void *)>(message, 2)(message); // Release
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

DWORD PostLoadLot(int32_t lot, int32_t value0)
{
    char spec[16];
    std::snprintf(spec, sizeof(spec), "*%d", lot);
    return PostGameMessage(kMsgLoadLot, value0, -1, spec);
}

// ---- frame counting ----
// A detour at cCTGFramework::OnTick's entry counts ticks and runs due commands, then continues into
// whatever runs there (our replacement, or the original on reference runs).
constexpr uint32_t kFrameTick = 0xc2d427, kFrameTickPrologue = 5; // mov eax, imm32
uint32_t gFrame;
struct Scheduled {
    uint32_t frame;
    std::string command;
    bool sim; // frame counts simulator frames (aftersimframes)
};
std::vector<Scheduled> gScheduled; // main thread only

void Run(const std::string &command);

void __cdecl OnFrame()
{
    ++gFrame;
    FrameClockOnFrame(gFrame); // TS2HOST_FRAMECLOCK: the new tick's time (host/control_frameclock.cpp)
    X87ProbeOnFrame(gFrame);   // TS2HOST_X87PROBE (host/control_x87probe.cpp)
    for (size_t i = 0; i < gScheduled.size();) {
        if (gScheduled[i].frame > (gScheduled[i].sim ? SimFrames() : gFrame)) {
            ++i;
            continue;
        }
        std::string command = gScheduled[i].command;
        gScheduled.erase(gScheduled.begin() + i);
        Log("control: frame %u (sim frame %u): %s", gFrame, SimFrames(), command.c_str());
        Run(command);
        i = 0;
    }
}

void InstallFrameCounter()
{
    // Our cCTGFramework (built by our constructor, hooks being in before the entry point) ticks through our vtable and
    // never passes 0xc2d427: it calls the observer. The detour below counts only when the game's own OnTick runs (no
    // hook at the site, e.g. reference runs); with the site hooked the adapter reaches our OnTick, so counting there too
    // would count twice.
    gz::cCTGFramework::sTickObserver = &OnFrame;
    auto *site = GameAddress<uint8_t>(kFrameTick);
    if (site[0] == 0xE9) return;
    auto *code = static_cast<uint8_t *>(VirtualAlloc(nullptr, 64, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    uint8_t *p = code;
    auto rel = [](uint8_t *at, const void *to) {
        int32_t r = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(at + 4));
        std::memcpy(at, &r, 4);
    };
    const uint8_t *target;
    if (site[0] == 0xE9) { // a hook: continue at its jump target
        int32_t r;
        std::memcpy(&r, site + 1, 4);
        target = site + 5 + r;
    } else { // the original: run its first instruction here, then jump back
        target = p;
        std::memcpy(p, site, kFrameTickPrologue);
        p += kFrameTickPrologue;
        *p++ = 0xE9;
        rel(p, site + kFrameTickPrologue);
        p += 4;
    }
    uint8_t *detour = p;
    *p++ = 0x51; // push ecx
    *p++ = 0xE8; // call OnFrame
    rel(p, reinterpret_cast<const void *>(&OnFrame));
    p += 4;
    *p++ = 0x59; // pop ecx
    *p++ = 0xE9; // jmp target
    rel(p, target);
    DWORD old;
    VirtualProtect(code, 64, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), code, 64);
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    site[0] = 0xE9;
    rel(site + 1, detour);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
}

void Run(const std::string &command)
{
    bool after = command.rfind("afterframes ", 0) == 0;
    if (after || command.rfind("atframe ", 0) == 0) {
        char *end = nullptr;
        uint32_t n = static_cast<uint32_t>(std::strtoul(command.c_str() + (after ? 12 : 8), &end, 0));
        while (*end == ' ') ++end;
        uint32_t frame = after ? gFrame + n : n;
        if (frame <= gFrame) Log("control: frame %u: \"%s\" asked for frame %u, already past", gFrame, end, frame);
        gScheduled.push_back({frame, end, false});
        Log("control: frame %u: \"%s\" scheduled for frame %u", gFrame, end, frame);
        return;
    }
    if (command.rfind("aftersimframes ", 0) == 0) {
        char *end = nullptr;
        uint32_t n = static_cast<uint32_t>(std::strtoul(command.c_str() + 15, &end, 0));
        while (*end == ' ') ++end;
        if (!StartSimFrameCounter()) Log("control: simulator frames can't be counted; \"%s\" not scheduled", end);
        else gScheduled.push_back({SimFrames() + n, end, true});
        Log("control: frame %u: \"%s\" scheduled for simulator frame %u", gFrame, end, SimFrames() + n);
        return;
    }
    if (command == "draws") {
        LogDraws();
        return;
    }
    if (command == "clocks") {
        LogClocks();
        return;
    }
    if (command == "frameclock") {
        LogFrameClock();
        return;
    }
    if (command == "devlog" || command.rfind("devlog ", 0) == 0) { // TS2HOST_DEVLOG (host/control_devlog.cpp)
        DeviceLogCommand(command.size() > 7 ? command.c_str() + 7 : "");
        return;
    }
    if (command == "wintree" || command.rfind("wintree ", 0) == 0) { // host/control_wintree.cpp
        WinTree(gDirectory, command.size() > 8 ? command.c_str() + 8 : "");
        return;
    }
    if (command == "services" || command.rfind("services ", 0) == 0) { // host/control_services.cpp
        Services(gDirectory, command.size() > 9 ? command.c_str() + 9 : "");
        return;
    }
    if (command.rfind("mark ", 0) == 0) { // a checkpoint for scenarios' until: steps
        Log("control: mark %s (frame %u, sim frame %u)", command.c_str() + 5, gFrame, SimFrames());
        return;
    }
    if (command.rfind("coverage ", 0) == 0) { // a new first-call round (TS2HOST_COVERAGE, src/hook/coverage.h)
        const std::string label = command.substr(9) + " frame " + std::to_string(gFrame);
        if (!CoverageRound(label.c_str())) Log("control: coverage is off (TS2HOST_COVERAGE not set)");
        return;
    }
    if (command.rfind("click ", 0) == 0 || command.rfind("mousedown ", 0) == 0 || command.rfind("mouseup ", 0) == 0) {
        const char *args = command.c_str() + command.find(' ') + 1;
        char *end = nullptr;
        long x = std::strtol(args, &end, 0), y = std::strtol(end, nullptr, 0);
        LPARAM at = MAKELPARAM(x, y);
        if (command[0] == 'c') {
            PostMessageW(gWindow, WM_MOUSEMOVE, 0, at);
            gScheduled.push_back({gFrame + 2, "mousedown " + std::string(args), false});
            gScheduled.push_back({gFrame + 4, "mouseup " + std::string(args), false});
        } else if (command[5] == 'd') {
            PostMessageW(gWindow, WM_LBUTTONDOWN, MK_LBUTTON, at);
        } else {
            PostMessageW(gWindow, WM_LBUTTONUP, 0, at);
        }
        return;
    }
    if (command.rfind("simtrace ", 0) == 0) {
        SimTrace(gDirectory, static_cast<uint32_t>(std::strtoul(command.c_str() + 9, nullptr, 0)));
        return;
    }
    if (command.rfind("probe ", 0) == 0) {
        SetProbes(command.c_str() + 6);
        return;
    }
    if (command.rfind("loadlot ", 0) == 0) {
        char *end = nullptr;
        int32_t lot = static_cast<int32_t>(std::strtol(command.c_str() + 8, &end, 0));
        int32_t value0 = *end ? static_cast<int32_t>(std::strtoul(end, nullptr, 0)) : 0x3000; // the dialog's
        Log("control: loadlot %d (value 0 = 0x%x)", lot, value0);
        if (DWORD result = PostLoadLot(lot, value0)) Log("control: loadlot raised exception 0x%08lx", result);
        else Log("control: loadlot posted");
        return;
    }
    if (command.rfind("post ", 0) == 0) {
        char *end = nullptr;
        uint32_t id = static_cast<uint32_t>(std::strtoul(command.c_str() + 5, &end, 0));
        int32_t value0 = static_cast<int32_t>(std::strtoul(end, &end, 0));
        int32_t value1 = static_cast<int32_t>(std::strtoul(end, &end, 0));
        Log("control: post 0x%08x (values 0x%x, 0x%x)", id, value0, value1);
        if (DWORD result = PostGameMessage(id, value0, value1, nullptr)) Log("control: post raised exception 0x%08lx", result);
        else Log("control: post done");
        return;
    }
    if (command == "quit" || command.rfind("quit ", 0) == 0) {
        // The game's own quit request (kMsgQuit above): the game then leaves its state, its framework posts WM_QUIT,
        // WinMain returns and the CRT's exit runs the atexit list. An optional value replaces 0x40000.
        uint32_t flags = command.size() > 5 ? static_cast<uint32_t>(std::strtoul(command.c_str() + 5, nullptr, 0)) : kQuitNoDialogNoSave;
        Log("control: quit: posting the game's quit request 0x%08x (value 0x%x) at frame %u", kMsgQuit, flags, gFrame);
        if (DWORD result = PostGameMessage(kMsgQuit, static_cast<int32_t>(flags), 0, nullptr, false)) Log("control: quit raised exception 0x%08lx", result);
        else Log("control: quit posted");
        return;
    }
    if (command.rfind("cheat ", 0) == 0) {
        const char *text = command.c_str() + 6;
        Log("control: cheat \"%s\"", text);
        DWORD result = RunCheat(text);
        if (result == 1) Log("control: no cheat system yet");
        else if (result) Log("control: cheat raised exception 0x%08lx", result);
        else Log("control: cheat done");
    } else {
        Log("control: unknown command \"%s\"", command.c_str());
    }
}

LRESULT CALLBACK ControlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == kMsgControl) {
        std::string command;
        {
            std::lock_guard<std::mutex> lock(gLock);
            if (wp < gCommands.size()) command = gCommands[wp];
        }
        Log("control: frame %u: command \"%s\"", gFrame, command.c_str());
        Run(command);
        return 0;
    }
    return CallWindowProcW(gPrevious, hwnd, msg, wp, lp);
}

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM param)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    *reinterpret_cast<HWND *>(param) = hwnd;
    return FALSE;
}

DWORD WINAPI ControlWorker(void *)
{
    while (!gWindow) {
        Sleep(200);
        HWND found = nullptr;
        EnumWindows(&FindGameWindow, reinterpret_cast<LPARAM>(&found));
        if (!found) continue;
        gPrevious = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(found, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ControlProc)));
        gWindow = found;
        Log("control: window %p subclassed", static_cast<void *>(found));
    }
    size_t offset = 0;
    std::string pending;
    for (;;) {
        Sleep(100);
        FILE *f = _wfsopen(gPath.c_str(), L"rb", _SH_DENYNO);
        if (!f) continue;
        std::fseek(f, static_cast<long>(offset), SEEK_SET);
        char buffer[4096];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
            offset += n;
            pending.append(buffer, n);
        }
        std::fclose(f);
        size_t end;
        while ((end = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, end);
            pending.erase(0, end + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;
            size_t index;
            {
                std::lock_guard<std::mutex> lock(gLock);
                index = gCommands.size();
                gCommands.push_back(line);
            }
            PostMessageW(gWindow, kMsgControl, index, 0);
        }
    }
}

} // namespace

uint32_t ControlFrame()
{
    return gFrame;
}

void StartControl(const std::wstring &directory)
{
    gDirectory = directory;
    gPath = directory + L"ts2control.txt";
    InstallFrameCounter();
    if (HANDLE t = CreateThread(nullptr, 0, ControlWorker, nullptr, 0, nullptr)) CloseHandle(t);
}
