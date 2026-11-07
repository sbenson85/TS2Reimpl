#pragma once
// The game's in-process exception reporter: the object embedded in the application at +0x98 (size
// 0x1ac). It is a plain value object (no vtable of its own; the cRZString at +0 carries the string
// vtable). Its small setters live in src/app/AppBase.* (appbase::cExceptionReporter); this adds the
// layout, the constructor and destructor, and the helpers they use.
#include "app/AppBase.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"
#include "rz/cRZTextFile.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>

namespace appbase {

// 0x1c3a97 (__cdecl): the CRT's _splitpath with 255-character limits. Any output may be null. A
// drive is recognised when the second character is ':'; the extension starts at the last '.' after
// the last separator ('\' or '/'). Parts are cut at 255 characters (not 256) and terminated.
void __cdecl SplitPath(const char *path, char *drive, char *dir, char *name, char *ext);

// 0x1c1c8c (__cdecl): the drive and directory of the path in `path` (read through its ToChar slot)
// are handed to `out`'s FromChar(text, length) slot (slot 3). Returns whether that text is not
// empty (al only).
uint8_t __cdecl GetModuleDirectory(cIGZString *path, cIGZString *out);

// 0x1c3f3e (__thiscall): cRZTextFile's constructor without a path; returns this.
cRZTextFile *__fastcall ConstructTextFile(cRZTextFile *self, void *edx);

// 0x1c4347 (__thiscall, ret 8): writes the text through the file's Write slot (16), then the line
// end (CR LF as two bytes, or as two UTF-16 units when the file's character size is 2) in a second
// Write; stops after the first when that fails. Returns the last Write's result (al only).
uint8_t __fastcall WriteLine(cRZTextFile *file, void *edx, const char *text, uint32_t length);

// 0x1bd001 (__cdecl): removes the process's unhandled-exception filter (sets it to null) and
// terminates the process with exit code 0xffffffff.
void __cdecl RemoveFilterAndTerminate();

struct ExceptionReporterObject {
    static constexpr uint32_t kSize = 0x1ac;
    static constexpr uint32_t kInstanceGlobal = 0x10caff0; // the game's pointer to the live reporter

    // 0x1ba914 (__thiscall): constructs the reporter in raw memory and installs the unhandled-
    // exception filter (SetEnabled(1)). Returns this.
    ExceptionReporterObject *Construct();
    // 0x1baae3 (__thiscall): removes the filter (SetEnabled(0)), clears the instance global and
    // destroys the members. No deleting version: the object is embedded in the application.
    void Destruct();

    // 0x1bd42a (__thiscall, ret 4): the directory becomes a copy of the string in `directory`, with a
    // '\\' appended unless its last character is '\\' or '/'.
    // ORIGINAL-BUG: an empty directory makes the check read the byte before the text (for a small
    // string, the top byte of the vtable pointer; no crash, but the result depends on it).
    void SetDirectory(const cRZString *directory);
    // 0x1bac86 (__thiscall, ret 4): keeps a reference-counted listener for the broadcast: AddRef, AddRef again (the list node),
    // appended to the list, Release (one reference stays, held by the list).
    void AddListener(cIGZUnknown *listener);
    // 0x1bac12 (__thiscall, ret 0xc): "label: value" as a line of the report file. The first call
    // writes the "Extra Exception Data" header first. The third argument is unused.
    void AddExtraData(const char *label, const char *value, uint32_t unused);
    // 0x1bb00a (__thiscall): once the header was written, the closing line of that section.
    void WriteExtraDataEnd();
    // 0x1bd339 (__thiscall, ret 4): builds a message with the id, hands it to every listener
    // (0x1bd39f, errors in a listener are swallowed), then releases it.
    void Broadcast(uint32_t id);
    // 0x1bd39f (__thiscall, ret 8): listener->slot 3(message) inside an SEH __try that swallows any
    // exception. The result is the listener's, or unspecified after an exception.
    uint32_t NotifyListener(cIGZUnknown *listener, void *message);

    cRZString mDirectory;     // 0x00: drive and directory of the executable (the constructor fills it)
    cRZString mReportName;    // 0x20: "Exception Report" (set by SetNames)
    cRZString mDumpName;      // 0x40: "Minidump"
    cRZString mReportFile;    // 0x60: report name + ".txt"
    cRZString mDumpFile;      // 0x80: dump name + ".mdmp"
    cRZTextFile mLog;         // 0xa0: the report file
    uint8_t mLevels[6];       // 0x194: one byte per report level (SetLevel), all 1 after construction
    uint8_t mFlag19a;         // 0x19a: SetFlag19a
    uint8_t mEnabled;         // 0x19b: SetEnabled's state
    uint8_t mExtraStarted;    // 0x19c: the extra-data header was written; bytes +0x19d..+0x19f are never written
    std::list<void *> mListeners; // 0x1a0: cIGZUnknown pointers (the constructor is list<uint32_t>'s: same code)
    uint32_t mPreviousFilter; // 0x1a8
};

static_assert(sizeof(ExceptionReporterObject) == ExceptionReporterObject::kSize, "reporter size");
static_assert(offsetof(ExceptionReporterObject, mReportName) == 0x20, "reporter layout");
static_assert(offsetof(ExceptionReporterObject, mReportFile) == 0x60, "reporter layout");
static_assert(offsetof(ExceptionReporterObject, mDumpFile) == 0x80, "reporter layout");
static_assert(offsetof(ExceptionReporterObject, mLog) == 0xa0, "reporter layout");
static_assert(offsetof(ExceptionReporterObject, mLevels) == 0x194, "reporter layout");
static_assert(offsetof(ExceptionReporterObject, mEnabled) == 0x19b, "reporter layout");
static_assert(offsetof(ExceptionReporterObject, mListeners) == 0x1a0, "reporter layout");
static_assert(offsetof(ExceptionReporterObject, mPreviousFilter) == 0x1a8, "reporter layout");

} // namespace appbase
