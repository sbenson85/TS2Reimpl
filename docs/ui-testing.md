# Testing UI and drawing code

Two halves (docs/archive/proposals/ui-testing.md is the proposal they implement): harness difftests with recording fakes (lane UA,
part 1) and in-game comparisons of reference and replacement runs (lane UB, part 2).

## Part 1. Harness: drawfx and winfx

How to difftest code that draws (a window's `Draw`, the GZWin draw helpers, text and Draw2D users) or sets window
properties (window procedures, the GZWin window helpers). The approach and its reasons: docs/archive/proposals/ui-testing.md
(sections 2-3; that page is the history, this one is what exists). Built by lane UA, 2026-10-19.

### What there is

| File | What |
|---|---|
| `tools/slotsig.py` | Per-slot signatures of an interface from every class implementing it: argument words (`ret N`), x87 (float) returns, agreement across the bodies; `--header` writes the fakes' slot tables |
| `tests/fx_slots.h` | Generated slot tables (`fx_slots::cIGZDrawContext`, ...): arity, return kind, settled, name and argument kinds per slot. Never edit; regenerate |
| `tests/drawfx_core.h` | `drawfx::Fake` (a recording fake of any table), the call log, argument normalisation, `Same` (names the first differing call), `Side`, `Note`, `Cells` |
| `tests/drawfx.h` | What draw tests include (core + tables) |
| `tests/winfx.h` | `winfx::Win` (a fake cIGZWin with state: id, area, flags, parent and children, faces), `winfx::World` (a random window tree from a seed), `winfx::Mgr` (a fake cIGZWinMgr) |
| `tests/drawfx_selftest.cpp` | Self-tests of the fakes (`-Filter drawfx_selftest`) |
| `tests/gz_win_helpers.cpp` | Worked examples: three draw helpers and four window helpers against the original |

Interfaces in `tests/fx_slots.h` (slot counts from the binary; names from db/methods/GZWinHelpers.jsonl):

| Interface | Slots | From | Named slots |
|---|---:|---|---|
| `cIGZDrawContext` | 49 | cGZDrawContext (tail calls into its engine at +0x18 resolved through cIGZDrawContextEngine) | 0-11 (PushTransform, ResetClip, IntersectClip, Translate, SetOrigin, PopTransform, PushState, PopState), 20 SetColorAt, 21 SetColor, 28 SetGradient, 36 FillRect, 45 DrawPrimitives |
| `nGZGraphic4::cIDraw2D` | 44 | cDraw2D | none yet |
| `cIGZFontRenderer` | 8 | cGZDrawFontRenderer, cGZBufferFontRenderer | none yet (lane IG's notes: 5 BeginDraw, 6 EndDraw, 7 DrawGlyphs) |
| `cIGZGraphicSystem` | 69 | nGZGraphic4::cGZGraphicSystem | none yet; 12, 34, 36 unsettled |
| `cIGZWin` | 173 | 86 window vtables (cGZWin and every control) | 0-3, 11, 15, 38, 39, 45, 49-55, 60-62, 64, 78, 79, 82-85, 101; 10, 21, 46 unsettled (they forward to the object at +4) |
| `cIGZWinMgr` | 48 | cGZWinMgrBase, cGZWinMgrW32 | none yet |

### Writing a draw test

The code under test gets the address of a `drawfx::Fake` where it expects the interface. Each side resets the fakes,
runs, and keeps its log; the logs are compared call by call:

```cpp
#include "drawfx.h"
drawfx::Fake gContext;   // static storage: a fake is ~2.5 KB
DIFF_TEST(GZWinHelpers_DrawFrame, 0x92ef70)
{
    const auto orig = ctx.Original<FrameFn>(0x92ef70);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        ... rect, argb, thickness from the seed ...
        auto a = drawfx::Side([] { gContext.Reset<fx_slots::cIGZDrawContext>(201); },
                              [&] { orig(&gContext, rect, argb, thickness); });
        auto b = drawfx::Side([] { gContext.Reset<fx_slots::cIGZDrawContext>(201); },
                              [&] { GZWinDrawFrame(&gContext, rect, argb, thickness); });
        if (!drawfx::Same(ctx, a.first, b.first, "DrawFrame", seed, a.second)) break;
    }
}
```

A failure names the call: `DrawFrame seed 17: call 3 of 7/7 differs, after #201 cIGZDrawContext.FillRect(rect{0, 0,
50, 2}): orig #201 cIGZDrawContext.FillRect(rect{0, 2, 2, 28}) | ours #201 cIGZDrawContext.FillRect(rect{48, 2, 50,
28})`.

- `Reset<Table>(id)`: every slot logs and returns 0; slots 1 / 2 count references (`refs`, from 1000); slot 0
  answers the faces added with `AddFace(iid, otherFake)`. The id is what the fake's address logs as.
- `Returns(slot, value)` / `ReturnsFloat(slot, f)` program an answer; `On(slot, handler)` runs
  `uint32_t handler(drawfx::Fake &self, const uint32_t *args)` after the call is logged (raw argument words; return the
  float's bits for an ST0 slot). `self.value[8]` and `self.owner` are free for the handler.
- `drawfx::Note("label", value)` adds a result, an out-parameter or a field of the object under test to the log, so one
  comparison covers it all.
- `drawfx::AddRange(p, size, tag)`: words pointing into a block both sides build at different addresses log as
  `tag | offset`. `drawfx::Cells` sets game globals (a graphic system cell) for a scope and restores them.

### Argument kinds

Each slot's arguments are normalised by kind before logging, so the same drawing logs the same from either side's
frames. One letter per argument, in the slot table:

| Kind | Logged |
|---|---|
| `i` (default) | the word; a fake's address becomes its id, a registered range `tag | offset`, a stack address `0x5ac00000` |
| `u` | the word as it is |
| `b` | the low byte only: bool and byte arguments, whose high bytes are a register's leftovers in the original |
| `f` | a float, bit for bit (printed as a float) |
| `r` / `R` | pointer to an int / float rect: null or not, then the 4 values |
| `p` / `P` | pointer to an int / float point (2 values) |
| `F<n>` / `W<n>` | pointer to n floats / n words |
| `s` / `w` | `const char *` / `const wchar_t *` text |
| `S` | a cIGZString: its text through slots 6 (Data) and 7 (Strlen); a fake logs its id |

Kinds come from the slot's method fact in db/methods: a param's `"fx"` key (`{"type": "i32*", "name": "rect", "fx":
"r"}`), else its type (`f32` f, `bool`/`u8`/`i8` b, `char*` s, `wchar*` w, `cIGZString*` S), else `i`. Use `b` only
where the implementations read the low byte (cGZWin::SetFlag 0x932c88 does: `cmp byte`), or a mutant that passes the
wrong high bytes survives. A stack pointer with kind `i` logs as the marker: its contents are not compared; give the
param a pointer kind when they matter (a rect, a point).

### Adding names, kinds or an interface

1. Establish the slot from the bodies (`tools/py -I tools/slotsig.py cIGZDrawContext --slots 30-40`; tail calls into
   another object: `--field 0x18=cIGZDrawContextEngine`) and the disassembly.
2. Record a method fact (iface, class, slot, name, ret, params with `fx` where needed) with
   `tools/ghidra.py annotate --lane X` (docs/lane/understand.md), then `export --lane X`.
3. Regenerate the tables (one command, every interface; add yours to the list):
   ```
   tools/py -I tools/slotsig.py --header tests/fx_slots.h cIGZDrawContext@0x18=cIGZDrawContextEngine nGZGraphic4::cIDraw2D cIGZFontRenderer cIGZGraphicSystem cIGZWin cIGZWinMgr
   ```
   It warns when a fact's parameter count disagrees with the bodies (the bodies win). `name@OFF=IFACE;OFF2=IFACE2`
   resolves tail calls through a field.
4. An interface no class implements in the image (or a test-only one): write a `drawfx::Slot[]` table in the test
   (tests/gz_win_helpers.cpp does for the 0x8810 face).

**Unsettled slots.** A slot the bodies can't settle (it only forwards to another object, or the implementations
disagree) is marked `false` in the table with its arity guessed as 0. Calling it adds an error to the side's result
(`Same` reports it) — and if the guess is wrong the stack is unbalanced, so expect a crash there. Settle it from the
caller's code for the test (`fake.Settle<10, 1>()`: slot 10 takes one word), or better with a method fact.

### winfx: window tests

```cpp
#include "winfx.h"
winfx::World gWorld;
auto a = drawfx::Side([&] { gWorld.Build(seed, 4); }, [&] { orig(&gWorld[0].f); });
... the same for ours ...
drawfx::Same(ctx, a.first, b.first, "HideQuiet", seed, a.second);   // and compare gWorld.State() taken in the run lambda
```

`winfx::Win` answers from its state: QueryInterface (faces), GetParent, GetChildCount, GetChildWindowFromID (direct),
GetChildWindowFromIDRecursive (children first, then itself, as cGZWin 0x931546), GetChildInterfaceRecursive (45),
GetWidth / GetHeight / GetLeft..GetBottom / GetArea, SetWidth / SetHeight / SetSize / SetArea, GetID / SetID,
IsFlagSet / SetFlag (low byte) / SetVisible (flag 1) / Hide. Every call is logged; the lookups a handler does inside
the fake are not (the real cGZWin's are virtual calls, so a log of the real thing would show them: the fake is a
model, not cGZWin). `World::Build(seed, n, idBase, idChoices)` makes n windows (fake ids 300+) with ids drawn from a
small set (lookups hit, miss and meet duplicates), random areas and flags, in a random tree under `[0]`;
`World::State()` is their state afterwards (ids, areas, flags, references, child order). `winfx::Mgr` is a plain
recording cIGZWinMgr; program what the code under test asks of it.

What winfx does not have yet: a caption (no caption slot is established on cIGZWin; cGZWinText keeps its text in a
std::string at +0x64 and its setter was not found), messages (DoMessage, slot 3, just logs: a window procedure test
builds its message object itself), the window manager's slots' meanings.

### Lessons from the proof functions (tests/gz_win_helpers.cpp)

- Draw order, coordinates, colour and the state push/pop are all in the log: every mutant of that kind failed at the
  call it changed (GZWinHelpers status notes list them).
- Byte arguments: GZWinHideQuiet (0x9351b9) passes the saved flag as the window pointer's high bytes with the flag in
  the low byte (it stores al into its own argument slot). SetFlag's `b` kind compares what the callee reads.
- The original reads the input rect between the draw calls; ours reads it once. The fakes never write to the caller's
  rect, so the two are the same for any real draw context (cGZDrawContext::FillRect 0x8bbbb0 only reads it); a fake
  that changed it would tell them apart.
- Out-parameters a fake fills (GetArea) end with `difftest::FillOut` (docs/lane/test.md).
- State-backed fakes make the window helpers' read-modify-restore sequences checkable (IsFlagSet, SetFlag, then
  SetFlag back): a mutant that restores the wrong value leaves a different flag word in `World::State()`.

## Part 2. In game: frame clock, device call log, window tree

These are the host-side tools from section 4 of docs/archive/proposals/ui-testing.md (the proposal stays as history). They
compare a reference run (`-Reference`, the original code under the host) with a replacement run at the same frames,
so UI, rendering and neighbourhood-view replacements get checks beyond "no crash". All of them are diagnostics: test
control like the rest of `host/`. They watch or set the game's clocks and never stand in for game code. They work the
same on reference and replacement runs.

| What | Switch (game.ps1 / scenario.ps1) | Environment | Writes (beside ts2host.log) | Read with |
|---|---|---|---|---|
| Frame clock (4.2) | `-FrameClock` | `TS2HOST_FRAMECLOCK=1` (or a frame time in µs, ≥ 1000); options in `TS2HOST_FRAMECLOCK_OPTS` | log lines `frameclock: ...`; `clk` lines in `ts2devlog.txt` | host log, `tools/devlog.py` |
| Device call log (4.1) | `-DeviceLog on` or `-DeviceLog 900,4000-4002` | `TS2HOST_DEVLOG` | `ts2devlog.txt` | `tools/devlog.py` |
| Window tree (4.4) | none: the control command `wintree [label]` | none | `ts2wintree.txt` (appended) | a plain diff |

`game.ps1 start` deletes the previous run's `ts2devlog.txt` and `ts2wintree.txt`. It also removes `TS2HOST_FRAMECLOCK`
and `TS2HOST_DEVLOG` when their switches are absent. `scenario.ps1` passes both switches through, records them in
`run.json` (`frameclock`, `devlog`), and copies the files beside each `snap:`. The `keep:NAME` step copies only the
small files (host log, device log, window tree, coverage, run.json) to `local/saves/<stamp>-NAME.*`, with no user-data
snapshot.

### Frame clock (TS2HOST_FRAMECLOCK)

The game's clocks tell time by the framework tick. Frame N therefore sees the same time on every run, so menu
animations, the neighbourhood's water and camera easing, and lot rendering repeat exactly. Seeds come from `-Seed`
and inputs from host clicks at fixed ticks, as before.

Version 2 (lane UC) also schedules the game's threads in lockstep with the tick, so the work done by frame N is the
same on every run too, not only the time it reads.

Code: `host/control_frameclock.cpp`; the arithmetic is in `host/shared/frameclock.h` (tests:
`tests/host_frameclock.cpp`), the scheduler in `host/shared/lockstep.h` (tests: `tests/host_lockstep.cpp`, real
threads with injected real-time jitter).

**Options.** `TS2HOST_FRAMECLOCK_OPTS` (comma- or space-separated; set it in the shell before `game.ps1` or
`scenario.ps1`, which pass the environment on) changes the defaults:

| Option | Default | Meaning |
|---|---|---|
| `still=<n>` | 1024 | reads per tick and thread that see the same time; then 1 µs per read (0: a step on every read, v1) |
| `stall=<ms>` | 0 (never) | a tick longer than this in real time lets time follow real time at 1/8 speed (v1: 500; not deterministic) |
| `threads=lockstep\|free` | lockstep | the game's threads run one at a time, in a fixed order (below); `free`: as Windows schedules them (v1) |
| `timers=virtual\|real` | virtual | window timers fire by virtual time, posted at the tick |
| `stuck=<ms>` | 10000 | lockstep ends when the thread holding the token makes no progress this long (0: never) |
| `trace=<a>-<b>` | off | ticks a to b: every clock read and scheduler call as a `ct`/`cs` line in `ts2devlog.txt` (diagnostics, below) |
| `tracelocks=1` | off | the trace includes `EnterCriticalSection` and `AcquireSRWLockExclusive` calls |
| `sound=off` | off with `threads=lockstep`, else on | no sound device: `DirectSoundCreate` answers `DSERR_NODRIVER` and the game plays silently. The sound library's mixer (0xe9e0d, on its timer thread) mixes as far as the device's play cursor has moved, a real-time input; with it the references' clock reads differed from tick 9 on (2026-10-19 12:06 runs). `sound=on` keeps the device |
| `heap=private` | `process` | an experiment, off by default: the image's `HeapAlloc`, `HeapFree`, `HeapReAlloc` and `HeapSize` (its only heap imports; its heap handle is `GetProcessHeap`'s) use a private heap (`host/shared/privateheap.h`). The process heap's low-fragmentation heap puts each block in a random free slot, so the same allocations get a different address order on every run, and the game orders some things by address: in the 2026-10-19 13:23 reference runs, with the clock and input equal through tick 4354, the neighbourhood (frame 1239 on) drew the same objects with the same draw count in a different order (frame 4000, call 588: two lots' transforms swapped), and the state calls between draws differed with it. The private heap is a fixed-size range reserved at start (`heapmb=<n>`, default 512 MB, committed as used; no LFH, no growth into wherever VirtualAlloc finds room), with a growable overflow heap for blocks of 508 KB and up and for anything once the fixed range is full (those addresses don't repeat). The `frameclock` control command logs its counts. Result (2026-10-19 14:18 runs): the menu loads twice as slowly (frame 900 after 2 minutes, not 1; the heap took 8.9 million allocations by then, with none from the overflow heap), the runs timed out before the neighbourhood loaded, and frame 1239 still differed, in the static buffers' contents as well as in order. So it stays off, and the neighbourhood is compared by the loose rule (below) |

`still=0,stall=500,threads=free,timers=real` is the first version.

**Virtual time.** Time is counted in 100 ns units. There is one global "now". Tick N starts one frame (1/30 s by
default) after tick N-1 started (tick 0 at 1 day), or at now if virtual sleeps in tick N-1 went past that. Each read on
a thread returns:

    max(that thread's previous read + step, now)

where step is 0 for the thread's first `still` reads in the tick and 1 µs after them. As a result:
- every read in a tick returns the tick's time until the thread has read `still` times, so one read more or less (a
  message, a poll) changes nothing;
- time never stands still for long: a loop waiting for the clock to move finishes;
- time never runs backwards on a thread. A thread that ran ahead (more than `still` + frame/1 µs reads in a tick, or a
  sleep that ended past the tick) continues from where it was, and the host logs "ahead" and "caught up" for the main
  thread.

**Lockstep** (`threads=lockstep`). The image's thread, wait and lock imports go to the scheduler (`CreateThread`,
`Suspend/ResumeThread`, `ExitThread`, `FreeLibraryAndExitThread`, `Sleep`, `SleepEx`, `WaitForSingleObject`, `WaitForMultipleObjects`,
`EnterCriticalSection`, `AcquireSRWLockExclusive`, `SleepConditionVariableSRW`, `WakeAllConditionVariable`,
`QueueUserAPC`, `SendMessageA`, `PeekMessageA`, `PulseEvent` and the six window functions below), and so do five of the
host's own imports, which our toolchain's calls go through (lane ND, `host/shared/ourcrt.h`):
- `EnterCriticalSection` (our cRZCriticalSection and GeneralAllocator call it directly) and `WaitForSingleObject`
  (cRZMutex);
- `CreateThread`, only for our CRT's `_beginthreadex`: the threads our replacements start (cRZThread's Start slots: the
  callback thread, the speculative loader, the log's output thread, ...) are scheduled exactly as the image CRT's threads
  are through the image's import. The host knows them by the start routine our CRT hands `CreateThread` (its
  `thread_start`), which it finds at start-up with one probing `_beginthreadex` call that makes no thread. Every other
  call (the host's own threads: the control's, coverage, quiet mode) gets the real function;
- `ExitThread` and `FreeLibraryAndExitThread`: our CRT's `_endthreadex` (cRZThread's ExitWith, and a routine that
  returns), so our threads' ends are seen as the image's are.

The scheduler gives every thread it didn't start the real functions, so the host's own threads are not affected. Then:
- one game thread runs at a time (it holds the token). It gives the token up only where it would wait;
- the next to run is the first thread, in creation order (the main thread is 0), whose wait is satisfied. A waiting
  thread checks a wait that takes something (an auto-reset event, a mutex, a lock) itself when polled, so the right
  thread gets it;
- sleeps and wait time-outs end by virtual time: when nothing can run and the main thread is blocked, now moves to the
  earliest time-out. An 8.5 s wait costs no real time;
- at each tick, before the frame's work, the main thread yields: every worker that can run does, and workers' sleeps
  and time-outs that end before the next tick end in time order (a worker sleeping 7 ms runs about five times per
  frame, as in real time). Work a frame started is done before the next frame starts, whatever the machine;
- `Sleep(0)` yields: workers that can run go first (at most 1000 yields per worker and tick ahead of a yielding main
  thread); while the main thread is blocked, each worker yield moves now on by 10 µs. A `Sleep(0)` of the main thread
  outside the tick's yield, when no worker can run, also moves now on by 10 µs (at most to the next worker time-out):
  the game stops its sound timer thread by spinning on `Sleep(0)` until the thread, asleep for up to 10 ms, notices
  (0xe9780), and without it the main thread spun for ever (the 11:44 runs, tick 9);
- APCs to a game thread are held until it waits alertably; a message one game thread sends to another's window is held
  until the receiver's next `PeekMessageA`. Both are where Windows would deliver them. The window functions that send
  to the window's thread and wait (`SetWindowTextA`, `SetWindowPos`, `ShowWindow`, `MoveWindow`, `SetWindowLongA`,
  `UpdateWindow`) are held the same way and run on the
  window's thread: otherwise the caller would wait outside the scheduler, holding the token, for a thread that is
  parked;
- new threads start parked; a thread that ends hands the token to a host thread (the driver), which picks the next, and
  leaves the scheduler (what runs on it while it ends, such as the C runtime's per-thread clean-up, gets the real
  functions). The C runtime's `_endthreadex` ends the image's threads with `FreeLibraryAndExitThread`; a token holder
  that ends some other way is found by the driver and logged (`frameclock: lockstep: thread N (...) ended without the
  scheduler seeing it; last ...`, `unseen=` in the `clk` line), and lockstep goes on;
- a critical section held by a thread outside the game is waited for in real time (it lets go by itself);
- `PulseEvent` (cRZThreadSignal's Signal and Broadcast) releases the game threads waiting on the event at that moment: the
  first in creation order for an auto-reset event, all for a manual-reset one. A parked thread is not in a real wait, so
  the real pulse alone would be lost and the waiter would only see its time-out.

A watchdog (a host thread of its own: nothing the scheduler or its driver blocks in can stop it) checks once a second.
If the token holder makes no progress for `stuck` ms (no hand-over, under 1% of a processor) while other threads wait,
lockstep ends for the rest of the run: `frameclock: lockstep: free from here on: thread N (...) made no progress ...`.
That is the fallback for a wait the scheduler doesn't see (below); the run is not deterministic after it. The kind in
brackets is the thread's last wait (`start`: it has not waited since it started). If no tick comes for 2 x `stuck`
(a busy or spinning holder, which the first rule can't see), it logs `frameclock: lockstep: no tick for N s (tick T)`
and the report as `lockstep: no tick: ...` lines, and at 6 x `stuck` lockstep ends (`free from here on: no tick for N
s`). After either end come `frameclock: lockstep: stuck: ...` lines, captured before anything moved on:
- `token thread N, H hand-overs, T ticks, tick K, now V, cursor C, in the frame` (or `at the tick's yield`): who holds
  the token (`driver`, `nobody`) and the virtual time;
- the census (below), with each thread's last intercepted call and its caller (`last WaitForSingleObject from image
  0x1b9df1`);
- `thread N stack: eip <where>; returns <where>, ...`: each live scheduled thread's instruction pointer and the return
  addresses found on its stack, innermost first (`host/shared/threadstack.h`: a scan for values just after a call, so an
  old value now and then; the first few are reliable). The stuck thread's tells what it waits in;
  `not captured: SuspendThread failed (5)` means the thread is ending or has ended;
- `driver stack: eip ...`: the driver (it picks the next thread when nobody holds the token);
- `outside thread id N (start <where>): eip ...`: every other thread of the process (Direct3D's, the sound driver's,
  the host's) with its start address and stack.

Addresses in the image are `image 0xRVA`; others `module.dll+0xoffset`.

**Diagnostics.** Beyond the report:
- threads are named by their start routine; the C runtime's `thread_start` and cRZThread's entry are looked through to
  the work (`start image 0x83a760 > image 0x1b9d82 runnable vtable image 0x...`: the vtable names the class;
  `> image 0x1b9d56 function image 0x...`: the function that thread runs). Our cRZThread's threads the same way:
  `start ts2host.exe+0x... (our CRT) > ts2host.exe+0x... (cRZThread::RunStartable) runnable vtable image 0x...` (or
  `(cRZThread::RunRoutine) function image 0x...`); the start-up line `frameclock: our CRT: thread start ...` names the
  routine;
- a thread outside the lockstep that reads a game clock is logged once: `frameclock: thread id N (start ...), outside
  the lockstep, reads the clock: <c> from <where> at frame F`;
- the `clk` line's informational part ends with `pulses=`, `unseen=`, `msgs=N:hash`, `in=N:hash`, `ctl=N` and
  `by=0:3,1:2,x:4`: `msgs` the window messages the game's threads took (message ids; for mouse messages also where),
  `in` the game's reads of the real input state (`GetCursorPos`, `GetAsyncKeyState`, `GetKeyState`,
  `GetForegroundWindow`, `GetActiveWindow`, read through), both from outside the game; `ctl` the control's own command
  messages (one per command line, posted when the control reads the line, so at a real-time tick: in the reference
  runs of 2026-10-19 the scenario's 16 lines arrived at tick 64 in one run and 66 in the other, with no effect on the
  clock); and `by` the tick's clock reads by thread index (`x`: outside the lockstep). `devlog.py compare` names the first tick where `msgs` or `in` differ (`input: first difference
  at tick N`): a difference there is input, not scheduling;
- `trace=<a>-<b>` writes, for ticks a to b, `ct <tick> <thread> <clock> <caller> <time in us>[ < <caller's caller> ...]`
  for each clock read (clock `q` QueryPerformanceCounter, `t` GetTickCount, `m` timeGetTime, `f`
  GetSystemTimeAsFileTime, `l` GetLocalTime, `r` rdtsc, `s`/`S` the stopwatch, `c` our CRT's clock(); the host's own
  `QueryPerformanceCounter` and `GetSystemTimeAsFileTime` imports log `q` and `f` as the image's do; up to four more image return addresses,
  found by scanning the stack from just above the hook's return address, since the image's frames have no frame
  pointers; a stale slot can add one now and then)
  and `cs <tick> <thread> <import>(<arguments>) <caller> -> <result>` for each scheduler call
  from the image except the lock calls (`tracelocks=1` adds them), at most 200 reads and 200 calls per thread and
  tick. Threads are `t<index>` or `x<id>` (outside); a thread argument (QueueUserAPC, Suspend/ResumeThread, the thread
  CreateThread made) is `thread <index>`, or `tid <id>` outside the lockstep. Needs the device
  log on. `tools/devlog.py trace A B` names each tick's first difference.

**Virtual timers** (`timers=virtual`). `SetTimer` with a window of the main thread and no callback (the canvas's 2 s
timer) is kept by the host: at each tick a due timer posts one `WM_TIMER`. Other timers are Windows's.

**Clock lines.** With the device log on, each tick writes `clk <tick> t=<tick start, µs> main=<reads>:<hash>
other=<reads>:<hash> seq=<schedule hash> hand=<hand-overs> adv=<time advances> threads= apcs= calls= timers= |
polls= idle= outside=` to `ts2devlog.txt`. The part before `|` is deterministic under lockstep; `devlog.py compare`
names the first tick where it differs: the first frame whose time, clock reads or thread schedule went another way.

**What is covered**, on reference and replacement runs alike:
- **Imports.** The image's `QueryPerformanceCounter`, `GetTickCount`, `timeGetTime`, `GetSystemTimeAsFileTime` and
  `GetLocalTime`, patched in its import address table. The wall clock reads 2026-01-01 00:00 UTC at tick 0, and local
  time is that same UTC time. `QueryPerformanceFrequency` is untouched; counter values are given in its unit.
- **The time-stamp counter.** The image's 20 `rdtsc` instructions become `ud2`, and a vectored handler answers them
  with a 3 GHz virtual counter. That covers:
  - the stopwatch's ReadTsc 0x1c9244;
  - the start-up CPU speed probe 0x1cb3b0;
  - OpenSSL's entropy timer 0x116f44;
  - small "one of four by the counter's low bits" functions such as 0x5b7990.
- **cRZStopwatch** (ours). It reads the counters through its own imports, so its two override hooks are set.
- **Our C runtime's `clock()` and `_time64`** (lane ND; the speculative loader's request times and per-tick `Process`,
  the skin catalog's and CAS's seeds, GZCOM's GUID times, nGZLog, the ini loader). They read the host's own
  `QueryPerformanceCounter` and `GetSystemTimeAsFileTime` imports, which the host points at the virtual clock on every
  thread (`_time64` then reads exactly what the image's `_time64` reads). `clock()` subtracts a start count our CRT read
  in real time when the host started, so its reads (known by their call site in `clock()`) get our start count plus the
  virtual counter's distance from the image CRT's start count (0x10dfcd0, read by the image's `clock()` 0x83a46e): our
  `clock()` returns what the image's would at the same virtual time, -1 included (`tests/host_ourcrt.cpp` compares the
  two). The start count is found at start-up by calling `clock()` with the import answering probe values (a binary
  search, about 45 calls). Other reads through the host's `QueryPerformanceCounter` import get the virtual counter.
  Trace letter `c`.

**Stalls** (`stall=<ms>`, off by default). In v1 a tick that lasted over 500 ms in real time (start-up, a load inside
one tick) let virtual time follow real time at 1/8 speed, so time-outs still ended; that was the first cause of the two
reference runs' differences. Under lockstep, time-outs end by virtual time, so stalls are not needed. With the option
on, the host logs such ticks (`frameclock: frame N: the previous tick lasted over ...`).

**Still not deterministic:**
- message times (`GetMessageTime`), `GetProcessTimes`;
- other DLLs' threads and clock reads (the sound system's, Direct3D's, Steam's): their signals to game threads are seen
  when the scheduler next polls, which depends on real time. A game thread waiting on them (the sound timer on the
  sound driver) runs at varying ticks;
- window messages from outside the game: input, focus, and quiet mode's asynchronous window moves
  (`src/hook/quiet.cpp`);
- our toolchain's waits other than `EnterCriticalSection` and `WaitForSingleObject` (none of our replacements make one
  today: they wait through the image's import slots), and threads our code would start without `_beginthreadex`
  (`_beginthread`, whose suspended start and `ResumeThread` the host doesn't take, or a raw `CreateThread`): a
  replacement that starts a game thread must go through `_beginthreadex` (as cRZThread does) or the image's
  `CreateThread` slot;
- anything after lockstep ended (the host log says so).

The `frameclock` control command logs the clock's state and one line per scheduled thread:

    control: frameclock: frame N, F us per frame, tick start +T us, main thread ahead/on time, S stalled ticks, R rdtsc
             reads answered, V virtual timers, lockstep on|ended (free)|off
    control: frameclock: thread I (id D, start 0xADDR): running|waiting|dead <wait kind>, R runs, W waits, T time-outs

The tick count at which the main menu appears can differ from a run without the frame clock. Check the first run's
screenshot or marks before trusting frame-anchored clicks.

### Device call log (TS2HOST_DEVLOG)

The image's `Direct3DCreate9` import resolves to a wrapper (`host/control_devlog.cpp`). It loads the real one from
System32 and wraps the `IDirect3D9` and the device it creates. Every call is logged and forwarded unchanged. This
covers all 119 device methods and all `IDirect3D9` methods (`host/shared/d3dlog_device.cpp`).

Resources are not wrapped. The game gets the real textures and buffers. Their `Lock`/`Unlock` slots (vertex and
index buffers, textures, cube textures, surfaces) are patched once per class, so their contents are hashed at
`Unlock`, before the real `Unlock` (`host/shared/d3dlog_tracker.cpp`).

Calls are grouped per framework tick (the host's frame counter) by the recorder (`host/shared/devlog.h`):
- **stream** records are ordered: state, draws, dynamic-buffer uploads, `Present`, and other main-thread calls;
- **async** records are a multiset: resource creation, static uploads, and any call from another thread (written as
  `w <call>`).

`TS2HOST_DEVLOG=on` (or `1`) writes a digest line per frame. `TS2HOST_DEVLOG=900,4000-4002` also writes the full call
lists of those frames. The control commands are:
- `devlog <n> [label]`: full lists from the current frame for n frames, under `label`, e.g.
  `atframe 4200 aftersimframes 300 devlog 3 lot`;
- `devlog mark <label>`: notes the frame only.

### File format (ts2devlog.txt, version 2)

    devlog 2 full=<frame list|none> frameclock=<TS2HOST_FRAMECLOCK value|off>
    frame <n> stream <count> <hash16> async <count> <sum16> <seq16> draws <d> presents <p>
    s <n> <i> <call text>        (full frames only: stream call i of frame n)
    a <n> <i> <call text>        (full frames only: async call i, in the order made)
    full <label> <first frame> <count>
    mark <frame> <label>
    clk <tick> <fields> | <fields>   (the frame clock's, above)

- `hash16` digests the stream calls in order.
- `sum16` is the sum of the async calls' hashes, so it does not depend on their order.
- `seq16` digests the async calls in order.
- Spaces in labels become `_`.
- Only frames with calls appear. A frame can be written in parts: frame 0 three times, because the game creates its
  Direct3D object three times and each device's release ends the frame. `devlog.py` merges the parts.
- The file is flushed per frame and readable while the game runs.

Version 2 changes the buffer records so they say what is drawn, not where it lies. In the two v1 reference runs most
frames differed only in positions: the dynamic rings' upload offsets and their DISCARD/NOOVERWRITE flags, and the
static pools' base vertices, start indices and upload chains (the hood's buffers had the same positions in both runs
but different `V:` names, and the menu's ring offsets drifted). Now:
- every buffer's bytes are shadowed (dynamic ones always, static ones up to 96 MB in all);
- `Upload <buffer> <bytes> <flags> #<hash>`: no offset, no DISCARD/NOOVERWRITE, no `from` name;
- `SetStreamSource <stream> <buffer> * <stride>`: the offset is `*`;
- `DrawPrimitive <type> <primitives> s0=<buffer>/<stride> #<vertices drawn> ...`;
- `DrawIndexedPrimitive <type> <primitives> n=<vertices used> s0=<buffer>/<stride> #<vertices> ... ib=<buffer>
  #<indices>`: the vertices are those the indices reach (base vertex + smallest index on), and the indices are hashed
  relative to the smallest, so the same mesh at another pool position, or drawn with absolute indices and base vertex
  0, gives the same text.

A buffer over the shadow budget is logged as in version 1: positions in its draws (`DrawPrimitive <type> @<start>
...`, `DrawIndexedPrimitive <type> <base> <min> <vertices> <start> <primitives> ...`), `+<offset>/<stride>` in its
streams, and offsets in its uploads.

Call text is the method name and its arguments: numbers, flags in hex, floats `%.9g`, rectangles `[l,t,r,b]`, and
present parameters without the window handle. A result that isn't `D3D_OK` is appended as ` =0x...`. Pointers never
appear: resources are named by what they hold, so names match across runs.

| Resource | Name | Changes when |
|---|---|---|
| Texture, cube texture | `T:`, `C:` + 12 hex digits of its content hash (descriptor at creation, then each upload) | each upload: `Upload <new> from <old> ...` |
| Static vertex/index buffer | `V:`, `I:` + descriptor (format 2; over the shadow budget: content hash, as textures) | never; draws carry the hashes of what they draw |
| Dynamic vertex/index buffer | `Vd<n>:` / `Id<n>:` (descriptor, numbered in creation order) | never; draws carry `#<hash>` of the vertex and index range they draw (a shadow copy of each upload) |
| Vertex/pixel shader, declaration | `v:`, `p:`, `d:` + hash of the function or elements | never |
| Render target, depth surface, offscreen plain surface | `S<n>:`, `Z<n>:`, `O:` + descriptor; back buffers `BB<n>` | uploads to `O:` surfaces |
| Texture level surface | `<texture>/<w>x<h>` | the texture's uploads |
| State block, query | `b<type>.<n>`, `q<type>.<n>` | never |
| Created out of sight (e.g. by D3DX) | `T?:` etc. + descriptor | never |

Textures are hashed row by row over the rectangle locked, without row padding, for the formats whose layout is known:
the 8/16/24/32-bit formats, DXT1-5 by 4×4 blocks, and float formats. Other formats are hashed over whole pitches.
Read-only locks are not uploads.

**Not tracked:**
- volume textures' contents;
- `Get*` results (only the calls are logged);
- the contents behind `UpdateSurface` / `UpdateTexture` / `StretchRect` (logged as calls with the names involved);
- objects created behind the wrapper whose pointer is reused, which can keep a stale name until they are uploaded to.

Hashing costs time on write-combined memory, so expect slower lot loads with the log on.

### tools/devlog.py

    tools/py -I tools/devlog.py summary LOG
    tools/py -I tools/devlog.py show LOG FRAME [--kind s|a]
    tools/py -I tools/devlog.py compare A B [--ref2 C] [--align LABEL|auto] [--frames 900,4000-4002] [--shift N]
                                            [--max 20] [--context 2] [--loose-from auto|none|N]

`compare` takes a reference run A, a run under test B, and optionally a second reference run C. Frames are matched by
number, or with `--align LABEL` relative to the first frame of the `full LABEL` request (or a `mark`) in each file.
`--align auto` aligns piecewise: each label both files have starts a stretch matched relative to it, until the next
(frames before the first match by number), so the menu, hood and lot frames each line up on their own anchor (the
lot's is `aftersimframes`, the simulator frame). It prints the offsets it used (`aligned (B): from A 900: +0, ...`).
Each frame is compared in two parts:
- **Stream:** the digests are compared. Where both files hold the frame in full, the first differing call is printed
  with A's and B's text, plus `--context` calls before it.
- **Async:** compared as a set, and the calls only one side made are printed. Order counts only with `--ref2`, in frames
  where A and C made the async calls in the same order.

With `--ref2`, a part where the two references already differ is reference noise. It is counted, not reported: the
trace suite's "no worse than reference-to-reference" rule. The last line is a summary:

    devlog compare: N frames compared, D differ (S stream, A async)[; R reference-noise parts][; ...]

Three more rules:
- **The loose rule (default from the neighbourhood).** From the frame `--loose-from` names, frames compare by their
  draw count, present count and async call count only, not by content or order. The default, `auto`, is the first
  frame from 1000 on with at least 1000 async calls: the neighbourhood's resource load (frame 1239 in the 2026-10-19
  runs). The menu (frame 900 included) and the click into the neighbourhood stay exact. `--loose-from none` compares
  every frame exactly, and a number starts the rule there. The first line says where it starts (`loose from frame N`);
  the summary counts the frames that are equal only by it. Why: from the neighbourhood on, two reference runs with the
  same clock, schedule and input still draw the same objects in another order, with other vertex and index contents in
  the static uploads (see "What the frame clock guarantees").
- **Shifted frames.** A differing frame whose content B has within 2 frames is marked `B's frame m has it: shifted
  +k` (an animation phase or a load that came a frame later). With `--shift N` such frames, within N, count as
  shifted, not as differences. Use it only to read past a known shift; under lockstep there should be none.
- **Clock lines.** When both files have `clk` lines, the comparison prints `clock: T ticks agree` or the first tick
  whose deterministic part differs, with both lines. That tick is where the runs' time or thread schedule parted:
  start there when frames differ. It doesn't change the exit status.

Exit status is 1 when any frame differs, 2 for a missing `--align` label, and 0 otherwise. Tests:
`tools/tests/test_devlog.py`.

### Window tree (the `wintree` control command)

`wintree [label]` appends the game's window tree to `ts2wintree.txt`, starting with a header line
`wintree <frame> <label> windows=<n>`. It writes one line per window, depth-first in child order, two spaces per
level, with no pointers:

    cGZWinGen id=a1b2c3d4 area=10,-20,310,180 vis dis children=2

The fields are:
- the class, from the RTTI name of its vtable;
- the window id;
- the area: left, top, right, bottom;
- visible (`vis`/`hid`) and enabled (`ena`/`dis`), from flags 1 and 2;
- the child count;
- `text="..."`, the window's caption when it has one (cGZWin's string at +0x60; escaped, at most 80 bytes). Text
  fields keep their typed text elsewhere; CAS's first name shows in the editor's label 0x2cb7b3d6.

Windows whose cIGZWin face isn't their primary vtable (cGZWinBtn and its subclasses: cIGZWinBtn first, cIGZWin at +4)
are listed since lane JH (2026-10-20); dumps from before lack every button and captions, so they don't diff against
newer ones. A child that isn't a window shows as `? (not a window)`. A slot that faults is caught and shows as `(fault)`. A child
list shorter than the count shows as `(listed n)`.

Code: `host/control_wintree.cpp`, with the walk in `host/shared/wintree.cpp`, tested on fake windows by
`tests/host_wintree.cpp`. Compare two dumps with a plain diff (`git diff --no-index A B`).

**Where it starts.** The window manager is the object cached at 0x10f51ac by its getter 0x9c5bd6, which TS2-Extender
calls `RZ::WM`. The command reads that cache without calling the getter, and checks that the object is a
`cGZWinMgrW32` (vtable 0xe5b240). The manager's slot 37 (0x1694c0, `+0x38`) gives the focused window. The tree dumped
is the one above it: GetParent is climbed to the top.

If nothing has focus, the command logs `no focused window` and writes nothing. No slot for the manager's own top
window is established yet.

**Slots used** (cGZWin's vtable 0xe52f54):

| Slot | Method | Address | Notes |
|---:|---|---|---|
| 11 | GetParent | 0x1dff70 | Returns +0x58 (lane UA, tentative). |
| 15 | GetChildCount | 0x9312f7 | Lane UA, tentative. |
| 37 | EnumChildren | 0x930cff | Takes the query, a callback and data. With query 0x22ba0121 (all children) it calls the cdecl `callback(parent, id, child, data)` in list order. |
| 55 | GetArea | 0x931197 | Copies left, top, right, bottom from +0xc8. |
| 78 | GetID | 0x149cf0 | Returns +0x10 (lane UA, tentative). |
| 82 | IsFlagSet | 0x931733 | (+0xe8 & mask) != 0. |

Each object must pass a check before the dump calls a slot on it:
- its vtable lies in the image;
- its RTTI class hierarchy includes `cIGZWin`;
- the vtable seen is the primary one.

**Not dumped:**
- caption text: no text slot is established;
- windows outside the focused window's tree, if the manager keeps more than one top window.

### Coordinator: the in-game comparison

The scenario `tools/scenarios/ui-compare.txt` uses the inputs of `coverage-phases`. At three points it takes 3 frames
of full device calls, a window tree and the frame clock's state:
- main menu, frame 900 (`menu`);
- neighbourhood, frame 4000 (`hood`);
- lot, 300 simulator frames after `loadlot 6` (`lot`).

It ends with `keep:ui`. Three runs, one after the other (or on instances):

    scenario.ps1 @ui-compare -ResetUserData -Quiet -Seed 1 -FrameClock -DeviceLog on -Reference     # reference A
    scenario.ps1 @ui-compare -ResetUserData -Quiet -Seed 1 -FrameClock -DeviceLog on -Reference     # reference C
    scenario.ps1 @ui-compare -ResetUserData -Quiet -Seed 1 -FrameClock -DeviceLog on                # replacement B

Then compare, from the `local/saves/<stamp>-ui.*` files:

    tools/py -I tools/devlog.py summary A.ts2devlog.txt
    tools/py -I tools/devlog.py compare A.ts2devlog.txt C.ts2devlog.txt --align auto     # reference noise
    tools/py -I tools/devlog.py compare A.ts2devlog.txt B.ts2devlog.txt --ref2 C.ts2devlog.txt --align auto
    git diff --no-index A.ts2wintree.txt B.ts2wintree.txt

Expected:
- **Host logs.** `frameclock: on, 33333 us per frame, still 1024, stall 0 ms, threads lockstep (28 import slots and 5
  of the host's imports), timers virtual (5 clock imports, 20 of 20 rdtsc sites, the stopwatch overrides, our CRT's
  clock() and _time64: 2 of 2 host imports)`, `frameclock: our CRT: thread start ts2host.exe+0x... (its _beginthreadex
  threads are scheduled), clock() start count found` and `devlog: Direct3DCreate9 wrapped; logging to ts2devlog.txt`.
  No `lockstep: free from here on` line, no `NOT FOUND` / `NOT PATCHED` / `doesn't import` frameclock line, and no
  `outside the lockstep, reads the clock` line whose start is `ts2host.exe+...` (our threads are scheduled; a line for
  another module's thread is the known noise of other DLLs).
- **The three `frameclock` lines.** Each shows "on time" (not "ahead") and `lockstep on`, followed by the thread list.
- **Device log.** `devlog 2` header, digest lines for every frame, `clk` lines for every tick, and full lists for
  900-902, 4000-4002 and the three lot frames.
- **Window tree.** Three blocks in `ts2wintree.txt` (menu, hood, lot) with a few dozen to a few hundred windows each,
  or a `wintree: no focused window` log line. That line would mean the start point needs another slot: tell the lane
  that owns cGZWinMgr.
- **Reference A against reference C.** Exit status 0 is the usual result: the menu exact, the neighbourhood and lot by
  the loose rule. What can still differ is listed under "What the frame clock guarantees". `clock: first difference
  at tick T` says where the runs' schedules parted; frames from T on can differ. If A and C differ before the
  neighbourhood (frame 900), look for the tick-700 split; run the pair again rather than compare B against it.
- **Replacement B.** It should be no worse than reference to reference. A difference prints the first differing call
  with both texts; `show` lists the whole frame.

If the main menu isn't up by frame 900 under the frame clock, check with `shot:` steps or the marks, then move the
frames in a copy of the scenario.

### What the frame clock guarantees

From the reference pairs of 2026-10-19 (frame clock with lockstep, sound off, the stage 4 to 7 builds of lane UE):

**Guaranteed** (seen in every pair, and held by tests):
- the clocks the game reads (the five clock imports, rdtsc, the stopwatch) tell virtual time by the tick;
- the game's threads run one at a time in a fixed order, and their sleeps, waits and timers end by virtual time;
- the same input: `msgs` and `in` agreed in every tick of every pair (the control's own command messages, `ctl`,
  arrive at a real-time tick by design and are left out);
- no sound device (sound=off), so the mixer's real-time play cursor can't steer the game.

**Usually, not always:**
- the menu (frames 0 to the click, frame 900 included) is identical in 2 of 3 pairs. In the third, at tick 700 the main
  thread read the clock 16 times in one run and 15 in the other, with nothing else running; from there one texture
  upload came a frame later, and the texture names chained from it differed through frame 900. A second such split
  was seen at tick 964 (3 vs 4 reads), without effect on frames. The reads come from the stopwatch
  (cRZStopwatch::ReadPerformanceCounter), and the cause was not found (trace with callers: `trace=`). One run in five
  also differed in content (not counts) in six menu frames (4, 6, 72, 84, 246, 292), with the same clock and input.
- the neighbourhood and the lot have the same draw, present and async counts, frame by frame (the loose rule), except
  around a schedule split (the 13:23 pair: lot loading at tick 4355, 117 vs 106 main-thread reads, three frames).

**Not guaranteed** (known to differ between two references):
- from the neighbourhood's first frame (1239): the order of draws (the same objects, e.g. two lots' world transforms
  swapped at frame 4000) and the contents of static vertex and index uploads. The likely cause is heap addresses:
  Windows's low-fragmentation heap places blocks at random, and the game orders things by address. A deterministic
  private heap (`heap=private`) was tried: it made loading twice as slow and frame 1239 still differed, so it stays
  off. Not covered at all: Direct3D's, the driver's and Steam's own threads, and uninitialised bytes in buffers.

So a replacement run B is compared against a reference A with `--ref2 C`: exact in the menu, the loose rule from the
neighbourhood on, and every reference-to-reference difference counted as noise.
