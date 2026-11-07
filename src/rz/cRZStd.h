#pragma once
#include "game/owned.h"
#include "rz/cRZCriticalSection.h"

#include <cstdarg>
#include <cstdint>

// cRZStd (RTTI name): the framework's assertion and report object. Code builds one on the stack (or as a static, at
// 0x10cb5bc) for a source file and line, then calls one of its three slots (vtable 0xda3e6c):
//   slot 0 (cdecl, this first): printf-style text, handed to slot 1;
//   slot 1 (thiscall): an assertion with that text: logged, reported and (per the flags) asked about in a message box;
//   slot 2 (cdecl, this first): a report line to the log service's group and level, else to the debugger output.
// Built by Construct (0x1bf920, src/rz/MiscHelpersA.cpp); the slots and their helpers are in src/rz/cRZStd.cpp
// (lane LZ, F4c).
//
// The flags (mFlags, set by Construct from the mode): bit 0 asks (the message box), bit 1 reports (to the output
// streams and the debugger), bit 2 picks the framework's StdOut stream (its slot 31) over its StdErr (slot 32), bit 3
// says the assertion prints mFirst as the file; bits 4-7 are the mode.
struct cRZStd {
    void *mVTable;
    uint32_t mFirst;  // +4: the source file's name (a char pointer stored as a word), as given to Construct
    uint32_t mSecond; // +8: the source line, as given to Construct
    uint32_t mFlags;  // +0xc: a byte (the upper bits are cleared), see Construct

    static constexpr uint32_t kVTable = 0xda3e6c;
    static constexpr uint32_t kDebugLevelRva = 0x10cb5d4; // the framework's debug level, read by every assertion
    static constexpr uint32_t kAskLockRva = 0x10cb5cc;    // the lock object held around the message box
    static constexpr uint32_t kAssertGroupRva = 0xda3f88; // "ASSERT", the assertions' log group
    static constexpr uint32_t kFlagAsk = 1;
    static constexpr uint32_t kFlagReport = 2;
    static constexpr uint32_t kFlagStdOut = 4;
    static constexpr uint32_t kFlagWithFile = 8;
    static constexpr int kSlotAssert = 1;

    // 0x1bf920 (__thiscall, ret 0xc): stores the vtable and the two arguments, then sets mFlags.
    // Let v = (old mFlags with bits 0 and 2 cleared, bits 1 and 3 set) and mode = the first argument.
    // mFlags = (v & 0x0f) | ((mode & 0x0f) << 4): the upper bits of the old value are dropped and
    // the mode nibble takes bits 4-7. A mode nibble of 8 stops there. Otherwise, when the global
    // byte at RVA 0xff246c is non-zero and the nibble is 4 or 5, bit 3 is cleared; then bit 2 is
    // set when the high nibble is 4, 5 or 7; then bit 0 is set when the value is below 0x10 or its
    // high nibble is 1, 2 or 6. Returns this.
    cRZStd *Construct(uint32_t mode, uint32_t first, uint32_t second);

    // Slot 0, 0x1bfca9 (__cdecl, this first): with a format, the text (1 KB, _vsnprintf_l) goes to slot 1 through the
    // vtable and its answer is returned; without one, 0.
    static int32_t __cdecl Printf(cRZStd *self, const char *format, ...);
    // Slot 1, 0x1bf9c4 (ret 4): the assertion. Returns 0 (go on), 1 (break: the box's Abort or any other answer) or 2
    // (ignore from now on: bits 0 and 1 cleared).
    int32_t Assert(const char *text);
    // Slot 2, 0x1bfb92 (__cdecl, this first): a report line in `group` at `level`; returns 0.
    static int32_t __cdecl Report(cRZStd *self, const char *group, int32_t level, const char *format, ...);

    uint32_t Mode() const { return (mFlags >> 4) & 0xf; }
    const char *File() const { return reinterpret_cast<const char *>(static_cast<uintptr_t>(mFirst)); }
};
static_assert(sizeof(cRZStd) == 0x10, "cRZStd layout");

// The word Construct stores (lane FA): the image's table in a hooked build; an image-free program's is ours
// (rz/cRZStdTable.cpp; standalone/owned_statics.cpp aliases it).
TS2_OWNED_STATIC(const OwnedVTable, da3e6c);
extern "C" const void *ts2_own_vt_da3e6c[3];
// The slots' cells (lane FA): the debug level every assertion reads (0 at load: .bss), the lock held around the message
// box (a cRZCriticalSection built by XC slot 9, 0x13836f, and destroyed by its atexit entry 0xd90741) and the
// assertions' log group, "ASSERT".
TS2_OWNED_STATIC(int32_t, 10cb5d4);
TS2_OWNED_STATIC(cRZCriticalSection, 10cb5cc);
TS2_OWNED_STATIC(const char, da3f88)[];

namespace rzstd {

// 0x1c00a8 (__cdecl): the text to the log service (its slot 28: file, line, group, level, format, args) when the
// service's enabled object (asked for as 0x287fb697) says so (its slot 6); a level of -1 is taken from the mode (0, 1,
// 2, 6: 2; 3: 3; 4: 6; 5, 7: 5; others stay -1). *pending is cleared when the service took it.
void __cdecl LogLine(const char *group, int32_t level, uint32_t mode, const char *format, va_list args, const char *file,
                     uint32_t line, uint8_t *pending);
// 0x1bff41 (__cdecl): "file (line): text" (or the text) to `stream`'s slot 14; when there is none or it declines, to
// the debugger (cRZTextODSStream) and, when RZSTD_OUTPUT_FILE_PATH names a file, appended to it after
// RZSTD_OUTPUT_PREFIX.
void __cdecl Output(void *stream, const char *text, const char *file, uint32_t line);
// 0x1bfce6 (__cdecl): the "Assert failed" message box (Abort / Retry / Ignore) owned by `window`. Returns 1 (Abort,
// or any other answer), 0 (Retry) or 2 (Ignore).
int32_t __cdecl AskBox(void *window, const char *text, const char *file, uint32_t line);
// 0x1bfe0c (__cdecl): AskBox under the lock at 0x10cb5cc, between the framework's assert-begin and assert-end
// messages (sent through its message server), owned by the main window when that belongs to this thread.
int32_t __cdecl AskWithFramework(void *framework, const char *text, const char *file, uint32_t line);
// 0x1b14b2 (__cdecl): GetEnvironmentVariableA through the game's import slot.
uint32_t __cdecl EnvironmentText(const char *name, char *buffer, uint32_t size);
// 0x1bfc9c (__cdecl): OutputDebugStringA(text); returns 1 (the slots' catch(...) handlers).
uint8_t __cdecl DebugText(const char *text);

} // namespace rzstd
