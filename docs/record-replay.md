# Record and replay (design)

Verifies functions the difftest harness can't isolate (UI, rendering, audio, the sim tick, anything
whose inputs are a web of live game objects) by recording real calls in-game and replaying them
offline against the original and our replacement. Status (2026-10-11): capture and replay work in
the harness, boundary calls included. `difftest --trace n --capture-dir d` captures each test's
original calls on a fixed capture stack; `difftest --replay d` replays every capture in a fresh
child process and checks the original reproduces it; `--ours 1` runs the hook table's replacement
for the RVA instead. Over the suite: 4309 of 4361 captures reproduce exactly, 37 fail and 15 skip
(self-check faults); the failures are rdtsc reads, garbage eax from st0-returning functions, and a
few heap counters that end 2 higher in replay, an input not yet captured. Not built yet: capture in the
host, REPLAY_TEST and status rules.

`--ours` on the same captures (first run, 2026-10-11): 2827 pass, 609 fail, 925 skipped. What a
replacement needs to be compared fairly, in order of count:
- the return type (335 "eax only" failures are mostly void functions leaving a value in eax):
  take it from the hook's declared type or db/ and compare eax only for non-void returns;
- CRT internals as transparent boundaries (152 "made fewer boundary calls"): the original's
  statically linked CRT takes SRW locks for static locals and calls HeapAlloc/HeapFree through
  the image's imports, ours does neither the same way. Heap and lock imports should not have to
  match one for one, and pointers to blocks the call allocated should compare by allocation
  identity (n-th allocation of size s), not by address;
- boundaries that write into the original's own frame (415 skipped): map them to the
  replacement's frame through the boundary's pointer arguments, or accept them only when the
  boundary is also called by the replacement with an argument pointing at its own frame.

How boundaries work now (record/tracer.cpp, record/replay.cpp):
- The tracer keeps a model of every byte the call has seen (read or written, its own frame
  included) and, at each boundary entry, a copy of the call's frame. At the boundary's return it
  records the bytes the boundary changed among those (out-parameters in locals, refcounts in
  objects), the returned eax/ecx/edx, esp, and an x87 push (st0) if the stack grew by one.
- Image code bytes that differ from the image as first mapped (a test's jump patch, such as
  tests/com_directors.cpp's atexit redirect) are recorded per capture: instruction fetches aren't
  reads.
- The replay child points every slot of the image's import table at an int3 stub (one per target):
  import targets are system code (HeapAlloc, the SRW locks) that the exception dispatcher also runs,
  so a debug-register breakpoint on them would fire inside the dispatcher. Other targets (vtable
  calls into our exe's fakes) get a DR0 breakpoint; hits whose return address isn't in the image
  (or, with `--ours`, our exe) run on, with RF. Each boundary hit must be the next one recorded
  (target, esp and return address; target and order only for a replacement) and is skipped by
  applying its recorded registers, esp and bytes. An import call the capture didn't make ends the
  replay as a divergence.

## Why the harness isn't enough

The harness maps the image and runs one function on inputs a test constructs. That works while a
test can build the inputs (leaf code, framework classes, containers). Higher up, a function reads a
graph of objects that only exists in a running game: a lot, a sim, a UI tree. Building fakes for
all of it is slower than writing the function, and fakes encode our assumptions. Real calls don't.

## Capture (in the host)

1. **Trampolines.** For a function under capture the host installs a recorder at its site instead
   of our replacement. The recorder calls the original through a trampoline (the original's first
   instructions relocated, then a jump back). This is a test tool calling original code, not a
   replacement doing so; replacements still never call original code.
2. **Tracing.** The recorder runs the original with the trap flag set; a vectored handler steps it
   one instruction at a time and decodes each instruction's memory operands (a decoder library,
   Zydis (MIT), linked into the host; hand-decoding x86 operands correctly, including string ops,
   x87 and SSE, isn't worth writing). Per call it records:
   - registers on entry (ecx, edx, esp, the stack arguments by the function's `ret N`), the FPU
     control word and MXCSR;
   - **reads**: for every byte read outside the call's own stack frame, the value the first time
     it's read (and again whenever it changed since, see below);
   - **writes**: the final value of every byte written outside the frame; the return registers.
   Callees inside the image are traced through, so the capture includes their reads and writes.
3. **Boundaries.** A call that leaves the image (an import, or a COM/vtable call into d3d9, dsound,
   steam_api...) is stepped over, not traced: the recorder logs the target, arguments and return
   registers. Memory the external code changed is noticed when image code next reads it (the value
   differs from the last one seen) and is attributed to the preceding boundary call, so replay can
   re-apply it there.
4. **Budget.** Tracing costs a few microseconds per instruction, so captures are sampled: the
   first N calls of a function in a scenario, then 1 in K, with a per-scenario byte cap. Scenarios
   are the existing ones (neighbourhood load, lot load, live-mode ticks).
5. **Storage.** Captures go to `local/captures/<rva>/<scenario>-<n>.bin`. They contain fragments of
   game state (and of the user's save), so they are never committed; verification results are
   recorded in `db/status` as usual, with the capture counts in the notes.

## Replay (in the harness)

1. The harness maps the image at the same base the host used (the host maps at a fixed base for
   capture runs), so pointers into the image need no translation.
2. For each capture: reserve the recorded heap and global pages at their recorded addresses (skip
   the capture, logged, if an address is taken), fill unrecorded bytes with a pattern, write the
   recorded read values, set up registers and the stack, and install the boundary log as the
   import/vtable behaviour: each boundary call returns its recorded registers and applies its
   recorded memory changes, in order; a call that doesn't match the log fails the replay.
3. **Self-check first:** run the *original* on the capture; it must reproduce the recorded writes,
   return registers and boundary calls exactly. A capture that doesn't (another thread changed
   memory mid-call, an untraced read) is discarded, so captures can't produce false passes.
4. Then run our replacement on a fresh copy and compare writes, returns and boundary calls with
   the original's. Mismatches are reported like a DIFF_TEST failure (first differing address).
5. Tests are declared like difftests: `REPLAY_TEST(name, rva)` iterates the local captures for
   `rva`; with none it reports "no captures", not a pass.

## Address space

Replay needs the recorded addresses free, and a plain replay process collides with them: its own
main-thread stack and process heap sit where the game's did (same loader, same layout), and many
captures read the caller's stack (out-parameters) and early heap blocks. So:
- **Capture:** the host runs the game's main thread on a stack it allocates at a fixed high address
  (with LARGEADDRESSAWARE), switching esp and the TEB stack bounds before the entry point, so stack
  addresses in captures never overlap a normal process's stack.
- **Replay:** the runner starts a child replay process suspended (only ntdll, its exe, PEB/TEB and
  the initial stack exist then), reserves every recorded region in it with VirtualAllocEx, then
  resumes it; the loader builds the child's heap around the reservations. The child maps the image
  at the capture base and replays. Regions that still collide (ntdll, the child's exe) are logged
  and the capture skipped.
- Thread state: fs-relative reads are recorded by TEB offset and set in the replay thread's TEB
  around the call (the SEH chain head too: replay catches faults with a vectored handler, so the
  chain can hold the captured value). Inside the tracer's handler, fs:[0] is the dispatcher's
  record; the traced code's head is the first record at or above its esp.
- Our own exe (the harness) maps at the same address in the child, so recorded bytes in its globals
  (test fakes) are written by the child itself, after its static initialisers.
- The child's initial thread stacks are placed before its loader runs and can collide with
  recorded heap addresses; a collision retries with a new child (up to 8 times).
- Writes below the esp after the return (argument slots popped by `ret n`) are dead and not
  recorded: the tracer's own handler frames overwrite them before the final values are read.

## Status rules

`verified` through replay needs: at least N self-checked captures (N per function, e.g. 20,
from more than one scenario where the function runs in more than one), all passing, plus a mutation
check as for difftests. The status notes name the scenarios and capture counts. Functions verified
by difftests stay as they are.

## Build order

1. Trampolines in the host, and a fixed-base mapping option.
2. The tracer and capture format, first on functions that already have difftests: replay verdicts
   must agree with the difftests, including on their mutants. This proves the machinery.
3. Boundary calls (imports first, then COM/vtable calls out of the image).
4. Replay runner in the harness; `REPLAY_TEST`; status rules in tools/status.py.
5. Capture scenarios and sampling, then use it on the first class the harness can't isolate.

## Decisions

- Zydis (MIT), the pinned 4.1.0 amalgamated release, fetched by CMake at configure time
  (approved 2026-10-10).
- Captures live in `local/` only, never committed (they hold save-derived data).
