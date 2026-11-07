#pragma once
#include <cstdint>
#include <string>

// Starts the test-control worker (host/control.cpp): commands appended to <directory>ts2control.txt
// run on the game's main thread.
void StartControl(const std::wstring &directory);
// The message that hands the main thread a command (WM_APP + 0x3a7, posted to the game's window as each command line
// is read, so at a real-time moment; the frame clock leaves it out of its input digest).
constexpr uint32_t kControlMessage = 0x8000 + 0x3a7;

// The x87 control word probe (host/control_x87probe.cpp): with TS2HOST_X87PROBE set, "x87probe: " log lines with the
// control word of the game's threads at start-up, per tick on the main thread, per second on every thread and at the
// entry of a few hooked replacements. StartX87Probe after the hooks are in; X87ProbeOnFrame at each framework tick.
void StartX87Probe(uintptr_t imageBase, uint32_t imageSize);
void X87ProbeOnFrame(uint32_t frame);

// Seed control (host/control_seed.cpp): with TS2HOST_SEED set, clock seeds (-1) passed to the game's
// generators are replaced by a fixed sequence. Call after the hooks are installed.
void StartSeedControl();

// Logs the draw counts (TS2HOST_DRAWS, host/control_seed.cpp) that changed since the last call.
void LogDraws();

// Clock census (host/control_clocks.cpp): the first call starts counting the game's clock reads by
// caller; later calls log the counts that changed since the previous call.
void LogClocks();

// Framework ticks counted so far (host/control.cpp).
uint32_t ControlFrame();

// Frame clock (host/control_frameclock.cpp): with TS2HOST_FRAMECLOCK set, the game's clocks tell time by the framework
// tick. Start it before the image's entry point, after the hooks, traps and fault log; the frame counter reports each
// tick; "frameclock" (control command) logs its state.
void StartFrameClock(uintptr_t imageBase);
void FrameClockOnFrame(uint32_t frame);
void LogFrameClock();

// Device call log (host/control_devlog.cpp): with TS2HOST_DEVLOG set, the image's Direct3DCreate9 import is a wrapper
// that logs the device's calls per frame to <directory>ts2devlog.txt. ConfigureDeviceLog before mapping the image;
// DeviceLogImport is asked by the import resolver; DeviceLogCommand runs the "devlog" control command.
void ConfigureDeviceLog(const std::wstring &directory);
void *DeviceLogImport(const char *dll, const char *function);
void DeviceLogCommand(const char *arguments);
// A line of its own in ts2devlog.txt (the frame clock's per-tick "clk" lines); nothing when the log is off.
void DeviceLogNote(const char *line);

// Window tree (host/control_wintree.cpp, the "wintree" control command): appends the game's window tree under the
// focused window's root to <directory>ts2wintree.txt, headed "wintree <frame> <label>".
void WinTree(const std::wstring &directory, const char *label);

// System services (host/control_services.cpp, the "services" control command): appends every service the framework
// holds (id, priority, tick/idle, class, each face's vtable and its image slots) to <directory>ts2services.txt, headed
// "services <frame> <label> framework=<class> <vtable>", and logs the summary line.
void Services(const std::wstring &directory, const char *label);

// SimAntics primitive trace (host/control_simtrace.cpp): traces the next `count` primitives into
// <directory>simtrace.txt.
void SimTrace(const std::wstring &directory, uint32_t count);

// Simulator frames: framework ticks in which at least one primitive ran, counted once the counter is
// started (it installs the trace's call-site patches, without tracing). Lot loading runs its stages'
// primitives at ticks that shift by a frame or two between runs; counted in simulator frames they
// line up (host/control_simtrace.cpp, "aftersimframes" in host/control.cpp).
bool StartSimFrameCounter();
uint32_t SimFrames();
