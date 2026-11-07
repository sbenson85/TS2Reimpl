#pragma once
// Capture files and replay (docs/record-replay.md).
//
// A capture file holds one traced call (record/tracer.h): the function's RVA, the image base it ran
// at, entry registers, the bytes it read and wrote, and its boundary calls. Replay runs the call
// again in a fresh child process: the parent creates the child suspended, before its loader has
// built a heap, and places every recorded region at its recorded address (pattern-filled, then the
// recorded read values); the child maps the image at the same base, writes the recorded image
// bytes, sets the registers and stack, runs the function and compares its writes and return
// registers with the capture.
#include "tracer.h"

#include <cstdint>
#include <string>

namespace record {

// Fixed addresses used while capturing and replaying, chosen to be free in a fresh process.
constexpr uintptr_t kImageBase = 0xa0000000;   // the mapped image: the game base our code is linked against (src/game/game.h)
constexpr uintptr_t kStackBottom = 0x30000000; // the stack captured calls run on
constexpr uint32_t kStackSize = 0x100000;

struct CaptureFile {
    uint32_t rva = 0;
    uint32_t imageBase = 0, imageSize = 0;
    Capture capture;
};

bool WriteCapture(const std::wstring &path, const CaptureFile &file);
bool ReadCapture(const std::wstring &path, CaptureFile &file, std::string &error);

// Runs fn(arg) on the fixed capture stack (kStackBottom, committed, with the TEB's stack bounds
// switched for the call), so stack addresses in captures don't collide with a replay process's
// own stack.
void RunOnCaptureStack(void (*fn)(void *), void *arg);

enum class ReplayOutcome { Pass, Fail, Skipped };
struct ReplayResult {
    ReplayOutcome outcome = ReplayOutcome::Skipped;
    std::string detail;
};

// Parent side: replays one capture file in a child process (`exe` with `childArgs`, which must
// make the child call ReplayInChild on the same file) and returns its verdict.
ReplayResult ReplayInChildProcess(const std::wstring &exe, const std::wstring &childArgs, const std::wstring &capturePath);

// Child side: the image is mapped at the capture's base. Applies the image bytes, runs the
// original function (or `replacement`, our reimplementation of it) on the capture and prints one
// verdict line ("pass", "fail: ...", "skip: ..."); returns the process exit code (0 pass, 1 fail,
// 3 skip).
int ReplayInChild(uintptr_t imageBase, const std::wstring &capturePath, void *replacement = nullptr);

} // namespace record
