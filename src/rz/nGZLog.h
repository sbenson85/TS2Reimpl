#pragma once
#include "rz/RZAutoRef.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/cGZAllocatorService.h" // cRZSystemService
#include "rz/cRZCriticalSection.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "rz/cRZThread.h"
#include "rz/cRZThreadSignal.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The log service's object layouts and constructors (nGZLog, RVAs 1.18.0.187). The classes use their own
// names (cLog...); the original's are in each comment. Fields are named by offset where their role isn't known.
class cRZOFStream;

namespace nGZLog {

// A cRZString that can live in the standard containers the log classes use: the copy is the
// game's cRZString(const cRZString &, 0, npos) (0x141eb3), the destructor 0x141193.
struct LogString : cRZString {
    LogString() { Construct(); }
    LogString(const char *text) { Construct(text); }
    LogString(const LogString &other) { Construct(other, 0, 0xffffffffu); }
    LogString &operator=(const LogString &) = delete;
    ~LogString() { Destruct(); }
};
static_assert(sizeof(LogString) == 0x20, "LogString is a cRZString");

// 0x82fbf6 is library code (UCRT 10.0.22621's _time64; our code calls the CRT's _time64). Kept for its difftest, which
// pins the original's behaviour; nothing calls it. It was: (__cdecl) the game's C runtime _time64: seconds since 1970 from GetSystemTimeAsFileTime
// (through the game's import slot), stored to *out when it isn't null and returned. Times later than
// 0x793582aff seconds give -1.
int64_t __cdecl Time64(int64_t *out);

// 0x9cbcdb (__cdecl): the string hash of every log table: h = h * 33 + (signed char)c over the
// characters, starting at 0.
uint32_t __cdecl HashChars(const char *text);

// The hash of a log string key: HashChars over the key's ToChar() (slot 5 of its own vtable, as the
// original calls it).
struct LogStringHash {
    size_t operator()(const LogString &key) const;
};
// The key comparison: std::string's operator==, the library's 0x145ca4.
struct LogStringEq {
    bool operator()(const LogString &a, const LogString &b) const { return a.mString == b.mString; }
};

using NameSet = std::unordered_set<LogString, LogStringHash, LogStringEq>;             // 0x20 bytes, 32-byte elements
using AliasMap = std::unordered_map<LogString, LogString, LogStringHash, LogStringEq>; // 0x20 bytes, 64-byte elements
using LevelMap = std::unordered_map<LogString, uint32_t, LogStringHash, LogStringEq>;  // 0x20 bytes, 36-byte elements
static_assert(sizeof(NameSet) == 0x20 && sizeof(AliasMap) == 0x20 && sizeof(LevelMap) == 0x20, "hash containers are 0x20 bytes");

// An element of the service's lists of output back ends, filters and formatters: a counted
// reference. Copying calls AddRef (slot 1) on a non-null object, destroying calls Release (slot 2).
struct RefHolder {
    void *mObject;
    RefHolder(const RefHolder &other);
    ~RefHolder() noexcept;
    RefHolder &operator=(const RefHolder &other); // RZUnknownSlot::Assign (0x142080): AddRef the new, store, release the old
};
static_assert(sizeof(RefHolder) == 4, "RefHolder is one pointer");

// 0x1b9cad (__cdecl): *out = GetCurrentThreadId() (through the game's import table).
void __cdecl StoreCurrentThreadId(uint32_t *out);

// ---- Helper objects -------------------------------------------------------------------------

// The time stamp the service takes at construction (0x58 bytes, constructor 0xc9c291).
struct cLogTimestamp {
    static constexpr uint32_t kSize = 0x58;
    cLogTimestamp *Construct();

    // Lane LY. 0xc9d137 (ret 4): the texts for a record's time (milliseconds on the stopwatch's scale), unless they
    // already are for that time. The wall-clock time is mTime plus the whole seconds since mElapsedLow (signed);
    // mFlag2 picks UTC (_gmtime64) over local time. With mFlag0 mDate gets "%Y/%m/%d"; with mFlag1 mClock gets
    // "%H:%M:%S:" and the milliseconds (the signed remainder, through ThreeDigits) and mElapsed the time since
    // mElapsedLow (FormatElapsed).
    void Update(int32_t milliseconds);
    // 0xc9cee5 (ret 8; `this` not read): three decimal digits of `value` (unsigned, the lowest three) and a NUL.
    static void __stdcall ThreeDigits(char *out, uint32_t value); // __thiscall in the game; ecx unused
    // 0xc9d1d8 (ret 8; `this` only passed on): "HH:MM:SS:mmm" of a signed millisecond count (hours modulo 24,
    // C's truncating division), the milliseconds through ThreeDigits.
    static void __stdcall FormatElapsed(int32_t milliseconds, char *out);
    // Lane LZ. 0xc9e950 (ret 4): takes `other`'s wall-clock time and start reading, and sets the stopwatch's count
    // to that reading (sign-extended).
    void CopyFrom(const cLogTimestamp *other);
    // 0xc9d4ef: the stopwatch's elapsed count (its own units), low 32 bits.
    uint32_t ElapsedMilliseconds();

    uint8_t mFlag0;          // 0x00 set to 1: Update writes the date
    uint8_t mFlag1;          // 0x01 set to 1: Update writes the clock time and the elapsed time
    uint8_t mFlag2;          // 0x02 cleared: local time; set: UTC
    uint8_t mUnk03;          // 0x03 not written
    uint32_t mTime;          // 0x04 low 32 bits of _time64(), seconds
    uint32_t mElapsedLow;    // 0x08 the stopwatch's reading (low half) when the object was built
    uint32_t mElapsedHigh;   // 0x0c cleared; the time Update last formatted
    char mDate[16];          // 0x10 not written by the constructor
    char mClock[16];         // 0x20 not written by the constructor
    char mElapsed[16];       // 0x30 not written by the constructor
    cRZStopwatch mStopwatch; // 0x40 mode 4, started
};
static_assert(sizeof(cLogTimestamp) == cLogTimestamp::kSize, "cLogTimestamp size");
static_assert(offsetof(cLogTimestamp, mTime) == 4 && offsetof(cLogTimestamp, mStopwatch) == 0x40, "cLogTimestamp layout");

// The process-times probe (0x30 bytes, constructor 0xc9c221): the process handle, the processor
// count, and whether kernel32 exports GetProcessTimes.
struct cLogProcessInfo {
    static constexpr uint32_t kSize = 0x30;
    cLogProcessInfo *Construct();
    // Lane LZ. 0xc9cf24: with mHasProcessTimes, GetProcessTimes (the game's import slot) on mProcess; the kernel
    // and user times are kept, creation and exit dropped.
    void Sample();
    // 0xc9d2a2: (kernel + user) / mScaled, unsigned 64-bit, low 32 bits.
    uint32_t CpuTime() const;

    uint32_t mField0;        // 0x00 cleared
    uint32_t mField4;        // 0x04 cleared
    uint8_t mHasProcessTimes; // 0x08 1 when LoadLibraryA("kernel32.dll") and GetProcAddress(.., "GetProcessTimes") both succeed
    uint8_t mPad09[3];
    uint32_t mField0c;       // 0x0c not written
    uint32_t mKernelTimeLow;       // 0x10 cleared
    uint32_t mKernelTimeHigh;       // 0x14 cleared
    uint32_t mUserTimeLow;       // 0x18 cleared
    uint32_t mUserTimeHigh;       // 0x1c cleared
    uint32_t mProcess;       // 0x20 GetCurrentProcess()
    uint32_t mProcessors;    // 0x24 dwNumberOfProcessors from GetSystemInfo
    int64_t mScaled;         // 0x28 processors (as a signed 32-bit) * 10000
};
static_assert(sizeof(cLogProcessInfo) == cLogProcessInfo::kSize, "cLogProcessInfo size");

// A log record (0x7c bytes, constructor 0xc9c1ed): two strings; cRZUnknown at +4. Vtables
// 0xeb458c (cIGZLogRecord, set first) then 0xeb473c / 0xeb4778.
struct cLogRecord {
    static constexpr uint32_t kSize = 0x7c;
    static constexpr uint32_t kBaseVTable = 0xeb458c;
    static constexpr uint32_t kVTable = 0xeb473c;
    static constexpr uint32_t kUnknownVTable = 0xeb4778;
    cLogRecord *Construct();
    void Destruct();                                  // 0xc9c7f1: vtables, both strings, then the cRZUnknown base
    cLogRecord *DeletingDestructor(uint32_t flags);   // 0xc9ca5a (ret 4): Destruct, then with flags & 1 a sized delete (0x7c)
    void SetLevel(uint32_t level, uint32_t arg);      // 0xc9e5e6 (ret 8): [+0x30] = 0, [+0x0c] = level, [+0x54] = arg
    // 0xc9e57b (ret 0x2c, lane LZ): fills a record for the write path (slot 28) and empties both strings.
    void Set(uint32_t level, uint32_t group, uint32_t message, uint32_t threadId, uint32_t number, uint32_t time,
             uint32_t cpuTime, uint32_t field60, uint32_t frame, uint32_t file, uint32_t line);

    // The getters of vtable 0xeb473c read these: slot 3 the level, 4 the message, 5 the group, 6 the time, 7 the
    // frame, 8 the CPU time, 9 +0x60, 10 the time since the output's last record, 11 the number, 12 the file, 13
    // the line. Pointers are kept as words (the record only hands them back).
    const void *mVTable;       // 0x00
    RZUnknownLayout mUnknown;  // 0x04 vtable (0xeb4778) and the reference count
    uint32_t mLevel;           // 0x0c not written by the constructor; SetLevel stores the log level here
    cRZString mText;           // 0x10
    uint32_t mMessage;         // 0x30 the message text (char *); SetLevel clears it
    cRZString mText2;          // 0x34
    uint32_t mGroup;           // 0x54 the group text (char *); SetLevel stores its second argument
    uint32_t mTime;            // 0x58 milliseconds on the service's time stamp
    uint32_t mCpuTime;         // 0x5c the process's CPU time (cLogProcessInfo::CpuTime)
    uint32_t mField60;         // 0x60 the write path stores 0
    uint32_t mSinceLast;       // 0x64 the write path: time since the output's last record (0 for its first)
    uint32_t mFrame;           // 0x68 the service's frame number
    uint32_t mNumber;          // 0x6c the record number
    uint32_t mFile;            // 0x70 the source file text (char *)
    uint32_t mLine;            // 0x74 the source line
    uint32_t mThreadId;        // 0x78 the writing thread (no getter)
};
static_assert(sizeof(cLogRecord) == cLogRecord::kSize, "cLogRecord size");
static_assert(offsetof(cLogRecord, mText) == 0x10 && offsetof(cLogRecord, mText2) == 0x34, "cLogRecord layout");
static_assert(offsetof(cLogRecord, mTime) == 0x58 && offsetof(cLogRecord, mSinceLast) == 0x64 && offsetof(cLogRecord, mThreadId) == 0x78, "cLogRecord layout");

// The text formatter (0x1bc bytes, constructor 0xc9c164). Vtables 0xeb45c8 (set first), 0xeb4794
// and 0xeb47bc (cRZUnknown at +4).
struct cLogFormatterText {
    static constexpr uint32_t kSize = 0x1bc;
    static constexpr uint32_t kBaseVTable = 0xeb45c8;
    static constexpr uint32_t kVTable = 0xeb4794;
    static constexpr uint32_t kUnknownVTable = 0xeb47bc;
    static constexpr int kSlotSetFormat = 3;  // (index, format name): 0xc9e426
    static constexpr int kSlotSetName = 6;    // 0xc9e41e
    cLogFormatterText *Construct();
    void Destruct();                                       // 0xc9c75e: vtables, the lock, the strings back to front, the base
    cLogFormatterText *DeletingDestructor(uint32_t flags); // 0xc9ca38 (ret 4): Destruct, then with flags & 1 a sized delete (0x1bc)
    // 0xc9e509 (ret 4): stores the alias table, then calls slot 3 with (0, the game's string global at 0x1085da0).
    void SetAliasMap(AliasMap *aliases);
    // 0xc9e822 (ret 4): stores the time stamp object.
    void SetTimestamp(cLogTimestamp *timestamp);

    // The slots of vtable 0xeb4794 and their helpers (lane LY). A level's format sits at LevelIndex(level).
    // Slot 3, 0xc9e426 (ret 8): looks the preset up in the alias table (none: nothing happens); found, the level's
    // format becomes its text and its preset name `preset`; level 7 sets all five, level 0 empties all five
    // formats and names them the game's "no preset" text (0x1085da0).
    void SetFormat(int32_t level, const char *preset);
    // Slot 4, 0xc9d121 (ret 4): Compose, then the output string.
    cRZString *Format(void *record);
    // 0xc9d711 (ret 4): empties the output and, when the record's level (its slot 3) has a non-empty format,
    // expands it into the output: "%x" through Expand ('%' last is dropped), "\\\\", "\\n", "\\t" as escapes and
    // "\\x" as x ('\\' last is dropped), every other character as is.
    void Compose(void *record);
    // 0xc9d807 (ret 8): appends the field `code` names (N M G L F D t e c p f r C R %) of the record; unknown codes
    // append nothing.
    void Expand(void *record, char code);
    // Slot 5, 0xc9eb68 (ret 4): "logFormatter <name>\n", then "logFormatterPreset <name> <preset> <level name>\n"
    // for the levels 6, 5, 4, 3, 2.
    void Write(void *stream);
    // Slot 8, 0xc9cfa1: a new formatter (no allocation check: ORIGINAL-BUG) with this one's alias table (SetAliasMap,
    // which resets its formats through slot 3), time stamp, formats and presets; the name is not copied.
    cLogFormatterText *Clone();
    // 0xc9d3bb (ret 4; `this` not read): the format index of a level: 2 -> 0, 3 -> 1, 4 -> 2, 5 -> 4, 6 -> 3, any
    // other -1.
    static int32_t __stdcall LevelIndex(int32_t level);
    // Slot 6, 0xc9e41e (ret 4): the name's text assigned (a tail jump to std::string::assign); returns that string.
    std::string *SetName(const char *name);

    const void *mVTable;        // 0x000
    RZUnknownLayout mUnknown;   // 0x004
    cRZString mName;            // 0x00c slot 6 sets it, slot 7 reads it
    cRZString mFormats[5];      // 0x02c the format texts
    cRZString mPresets[5];      // 0x0cc the presets they came from
    cRZString mOutput;          // 0x16c what Format composed
    cRZString mScratch;         // 0x18c numbers are printed here first
    cLogTimestamp *mTimestamp;  // 0x1ac
    cRZCriticalSection mLock;   // 0x1b0
    AliasMap *mAliases;         // 0x1b8
};

// 0xc9d452 (__thiscall on the alias table, ret 8): out is emptied (its slot 26, Erase(0, -1)); when the table has
// `name`, out is assigned the text (out's slot 16) and the result is true (al only).
uint8_t __fastcall LookupAlias(AliasMap *self, void *edx, const char *name, cRZString *out);

// 0xc6aa35 (ownerless, __cdecl): the executable's "__export__GetMemoryReport" export (GetModuleHandleA(null),
// GetProcAddress, through the game's import slots) called with no arguments (a tail jump); null without one.
void *__cdecl MemoryReport();
static_assert(sizeof(cLogFormatterText) == cLogFormatterText::kSize, "cLogFormatterText size");
static_assert(offsetof(cLogFormatterText, mFormats) == 0x2c && offsetof(cLogFormatterText, mPresets) == 0xcc, "cLogFormatterText layout");
static_assert(offsetof(cLogFormatterText, mTimestamp) == 0x1ac && offsetof(cLogFormatterText, mLock) == 0x1b0 && offsetof(cLogFormatterText, mAliases) == 0x1b8, "cLogFormatterText layout");

// The group level filter (0x54 bytes, constructor 0xc9c113). Vtables 0xeb45f0 (set first), 0xeb46f0
// and 0xeb4720 (cRZUnknown at +4).
struct cLogFilterGroupLevels {
    static constexpr uint32_t kSize = 0x54;
    static constexpr uint32_t kBaseVTable = 0xeb45f0;
    static constexpr uint32_t kVTable = 0xeb46f0;
    static constexpr uint32_t kUnknownVTable = 0xeb4720;
    static constexpr int kSlotSetName = 6;
    cLogFilterGroupLevels *Construct();
    void Destruct();                                           // 0xc9c724: vtables, the level table, the name, the base
    cLogFilterGroupLevels *DeletingDestructor(uint32_t flags); // 0xc9c9ef (ret 4): Destruct, then with flags & 1 a sized delete (0x54)

    // The slots of vtable 0xeb46f0 (lane LY). Levels compare signed; a smaller level is more important.
    static constexpr int kSlotGroupLevel = 10;
    // Slot 4, 0xc9dbfb (ret 4): 1 when the record's level (its slot 3) is at most the AnyGroup level and at most
    // the level of the record's group (its slot 5, asked through this object's slot 10); the record's level is
    // read again for the second test. Only al is defined.
    uint8_t Accept(void *record);
    // Slot 5, 0xc9e96e (ret 4): writes the filter as configuration text to `stream` (its slot 14 writes a C
    // string, slot 15 a cIGZString): "logFilter <name>", then a "logFilterLevel <name> <group> <level name>" line
    // for AnyGroup, NoGroup and every group in the level table (in its list order).
    void Write(void *stream);
    // Slot 8, 0xc9cf67: a new filter (operator new, constructor) with this one's two levels and level table; the
    // name is not copied. Null when the allocation fails.
    cLogFilterGroupLevels *Clone();
    // Slot 9, 0xc9cbf2 (ret 8): "AnyGroup" and "NoGroup" (the game's text pointers, compared case-insensitively)
    // set those levels; any other group's level goes into the table.
    void SetGroupLevel(const char *group, int32_t level);
    // Slot 10, 0xc9d322 (ret 4): the group's level from the table, else the NoGroup level.
    int32_t GroupLevel(const char *group);

    const void *mVTable;       // 0x00
    RZUnknownLayout mUnknown;  // 0x04
    cRZString mName;           // 0x0c
    int32_t mNoGroupLevel;     // 0x2c cleared
    int32_t mAnyGroupLevel;    // 0x30 set to 7
    LevelMap mLevels;          // 0x34 group -> level
};
static_assert(sizeof(cLogFilterGroupLevels) == cLogFilterGroupLevels::kSize, "cLogFilterGroupLevels size");
static_assert(offsetof(cLogFilterGroupLevels, mLevels) == 0x34, "cLogFilterGroupLevels layout");

// The output back end base, nGZLog::cOutputBackEnd (0x44 bytes, constructor 0xc9f372). Vtable 0xeb4b48, which the
// four back ends share up to their own slots 5 (output a record) and 21 (flush). Slots 7 / 9 set the formatter /
// filter, 8 / 6 read them, 12 / 13 the enabled byte, 15 the synch byte, 16 / 17 the flush interval, 22 / 23 the word
// at +0x40 (shared one-line bodies, not hooked). Members in src/rz/nGZLogOutput.cpp; the small slots (0, 2, 3, 4,
// 10, 11, 14, 18, 20) in src/rz/nGZLogBackEndSlots.size.cpp.
struct cLogOutputBackEnd {
    static constexpr uint32_t kSize = 0x44;
    static constexpr uint32_t kVTable = 0xeb4b48;
    cLogOutputBackEnd *Construct();
    // 0xc9f3a8: the destructor body: vtable reset, the text destroyed, then the formatter, the filter and the
    // stream released (slot 2) when present.
    void Destruct();
    // Slot 24, 0xc9f3e6 (ret 4): Destruct, then (flags & 1) a sized delete (0x44). Returns this.
    cLogOutputBackEnd *DeletingDestructor(uint32_t flags);
    // Slot 19, 0xc9f506 (ret 4): writes "logReporterProp <text> -formatter <name> -filter <name> -flush <interval>
    // -synch <true|false> -enabled <true|false>\n" to the stream (its slots 14, 15 and 8); a missing formatter or
    // filter writes an empty name.
    void WriteConfig(void *stream);

    static constexpr int kSlotAddRef = 1;
    static constexpr int kSlotDelete = 24;
    static constexpr int kFilterSlotAcceptFirst = 3;
    static constexpr int kFilterSlotAccept = 4;
    static constexpr int kGroupLevelsSlotGroupLevel = 10;
    static constexpr uint32_t kGroupLevelsIid = 0xaca92697; // the filter's group-levels face
    // Slot 0, 0xc9f4b9 (ret 8): matches the ids 1 and 0x49b84afb (this stored, AddRef through slot 1); only al.
    bool QueryInterface(uint32_t riid, void **out);
    // Slot 2, 0xc9f4dd: above 1 (signed) the count drops and is returned; otherwise the deleting destructor (slot 24,
    // flags 1) and 0.
    uint32_t Release();
    // Slots 3 and 4, 0xc9f45b / 0xc9f484 (ret 4): false when disabled or without a formatter; with a filter, its
    // answer (its slot 3 or 4) for the argument. Slot 4's argument is the record (the write path's accept test);
    // the filter's slot 3 is a body folded across classes (its role is not known).
    bool AcceptFirst(uint32_t arg);
    bool Accept(uint32_t record);
    // Slot 10, 0xc9f4f4 (ret 4): the name's text assigned (std::string::assign); returns that string.
    std::string *SetName(const char *name);
    // Slot 11, 0xc9f4ad: the name's text.
    const char *Name() const;
    // Slot 14, 0xc9f4fc (ret 4): the synch byte.
    void SetSynchronous(uint8_t value);
    // Slot 18, 0xc9f446: counts a record; true when the count before it is a multiple of the flush interval.
    bool FlushDue();
    // Slot 20, 0xc9f405 (ret 4): the filter's level for a group (its group-levels face, slot 10); 0 without a filter.
    uint32_t GroupLevel(uint32_t group);

    const void *mVTable;     // 0x00
    uint32_t mRefCount;      // 0x04 cleared
    RZUnknownSlot mStream;   // 0x08 the output stream (cleared)
    void *mFilter;           // 0x0c cleared
    void *mFormatter;        // 0x10 cleared
    cRZString mText;         // 0x14 the back end's name
    uint8_t mEnabled;        // 0x34 cleared
    uint8_t mFlag;           // 0x35 cleared ("synch")
    uint8_t mUnk36[2];       // 0x36 not written
    int32_t mInterval;       // 0x38 set to 1 (flush every N records)
    uint32_t mCounter;       // 0x3c cleared
    uint32_t mField40;       // 0x40 cleared
};
static_assert(sizeof(cLogOutputBackEnd) == cLogOutputBackEnd::kSize, "cLogOutputBackEnd size");
static_assert(offsetof(cLogOutputBackEnd, mText) == 0x14 && offsetof(cLogOutputBackEnd, mInterval) == 0x38, "cLogOutputBackEnd layout");

// nGZLog::cDefaultOutputBackEnd (0x44 bytes, vtable 0xeb4c08): writes each record's text to a stream.
struct cLogDefaultOutputBackEnd {
    static constexpr uint32_t kSize = 0x44;
    static constexpr uint32_t kVTable = 0xeb4c08;
    // 0xc9f5df (ret 8): the base, this vtable, the text set, the stream held (RZUnknownSlot::Assign).
    cLogDefaultOutputBackEnd *Construct(const char *text, void *stream);
    // 0xc9f65e: vtable reset, then the base's Destruct.
    void Destruct();
    // Slot 24, 0xc9f683 (ret 4): Destruct, then (flags & 1) a sized delete (0x44). Returns this.
    cLogDefaultOutputBackEnd *DeletingDestructor(uint32_t flags);
    // Slot 5, 0xc9f6c1 (ret 4): with a stream: the formatter's text for the record (its slot 4) to the stream (slot
    // 15); a failed write drops the stream and turns this back end off (own slot 12 with 0); otherwise, when own
    // slot 18 (TickEveryN) says so, the stream is flushed (slot 3).
    void Output(void *record);

    cLogOutputBackEnd mBase;   // 0x00
};
static_assert(sizeof(cLogDefaultOutputBackEnd) == cLogDefaultOutputBackEnd::kSize, "cLogDefaultOutputBackEnd size");

// nGZLog::cGZLogFileOutputBackEnd (0x68 bytes, vtable 0xeb4c70): the default back end on a log file (cRZOFStream).
struct cLogFileOutputBackEnd {
    static constexpr uint32_t kSize = 0x68;
    static constexpr uint32_t kVTable = 0xeb4c70;
    // 0xc9f60a (ret 0x10): unless deferOpen, opens `path` now (new cRZOFStream from a char string: text mode,
    // order 0, no auto flush, `append`); then the default back end's constructor with that stream, this vtable,
    // an empty mPath and mDeferOpen. Neither mPath nor mAppend is given the arguments: a deferred open (slot 5 or
    // 25) opens an empty path with whatever byte the allocation left at +0x64 (ORIGINAL-BUG).
    cLogFileOutputBackEnd *Construct(const char *text, const char *path, uint8_t append, uint8_t deferOpen);
    // 0xc9f669: mPath destroyed, then the default back end's Destruct.
    void Destruct();
    // Slot 24, 0xc9f6a2 (ret 4): Destruct, then (flags & 1) a sized delete (0x68). Returns this.
    cLogFileOutputBackEnd *DeletingDestructor(uint32_t flags);
    // Slot 5, 0xc9f716 (ret 4): with mDeferOpen and no stream yet, opens mPath (OpenStream) and holds it; then
    // the default back end's Output.
    void Output(void *record);
    // Slot 25, 0xc9f763: unless mAppend is set and a stream is open, drops the stream and opens mPath again.
    void Reopen();
    // new cRZOFStream on mPath (text mode, order 0, no auto flush, mAppend); null if the allocation fails.
    cRZOFStream *OpenStream();

    cLogDefaultOutputBackEnd mDefault; // 0x00
    cRZString mPath;                   // 0x44
    uint8_t mAppend;                   // 0x64 never written by the constructor
    uint8_t mDeferOpen;                // 0x65
    uint8_t mPad66[2];
};
static_assert(sizeof(cLogFileOutputBackEnd) == cLogFileOutputBackEnd::kSize, "cLogFileOutputBackEnd size");
static_assert(offsetof(cLogFileOutputBackEnd, mPath) == 0x44 && offsetof(cLogFileOutputBackEnd, mDeferOpen) == 0x65, "cLogFileOutputBackEnd layout");

// nGZLog::cW32CaptionOutputBackEnd (0x6c bytes, constructor 0xc9b998, ret 4): the base, a string at +0x44 and a
// lock at +0x64; then the text is set (std::string::assign). Vtable 0xeb4508. The caption is set from the log
// thread (Output) and shown on the main thread (Flush).
struct cLogCaptionOutputBackEnd {
    static constexpr uint32_t kSize = 0x6c;
    static constexpr uint32_t kVTable = 0xeb4508;
    cLogCaptionOutputBackEnd *Construct(const char *text);
    // 0xc9b9c7: vtable reset, the lock and the caption destroyed, then the base's Destruct.
    void Destruct();
    // Slot 24, 0xc9b9ef (ret 4): Destruct, then (flags & 1) a sized delete (0x6c). Returns this.
    cLogCaptionOutputBackEnd *DeletingDestructor(uint32_t flags);
    // Slot 5, 0xc9baa4 (ret 4): under the lock: the caption = the formatter's text for the record (its slot 4),
    // cut to 0x50 characters, line breaks made spaces.
    void Output(void *record);
    // Slot 21, 0xc9ba0e: under the lock, with a caption: the framework (game global 0x10c9378) is asked for the
    // window interface 0x23ea70a1 (its slot 0); a window whose handle (slot 5) is non-null and IsWindow gets the
    // caption (SetWindowTextA), which is then emptied. The interface is released.
    void Flush();

    cLogOutputBackEnd mBase;   // 0x00
    cRZString mCaption;        // 0x44
    cRZCriticalSection mLock;  // 0x64
};
static_assert(sizeof(cLogCaptionOutputBackEnd) == cLogCaptionOutputBackEnd::kSize, "cLogCaptionOutputBackEnd size");

// nGZLog::cRZStdAdapter (0x34 bytes, constructor 0xc9c2e2, ret 8): a cRZTextOStream whose first argument is
// kept at +0x10 and whose second becomes the string at +0x14. Vtable 0xeb47d8. The base constructors it runs
// (0x1b4fab, 0x1b4f60: the second is a 6-instruction cRZOStream constructor that library matching named
// SingleWaitBlock) are folded in: +4 cleared, +8 set to 1, +0xc cleared.
struct cLogStdAdapter {
    static constexpr uint32_t kSize = 0x34;
    static constexpr uint32_t kVTable = 0xeb47d8;
    static constexpr uint32_t kStreamBaseVTable = 0xda29bc; // set by 0x1b4f60, replaced at once
    static constexpr uint32_t kTextStreamVTable = 0xda2b4c; // set by 0x1b4fab, replaced at once
    cLogStdAdapter *Construct(uint32_t target, const char *name);
    // Lane LZ. 0xc9c829: the name destroyed, then cRZTextOStream::Destruct (0x1b5319).
    void Destruct();
    // Slot 21, 0xc9ca79 (ret 4): Destruct, then (flags & 1) a sized delete (0x34). Returns this.
    cLogStdAdapter *DeletingDestructor(uint32_t flags);
    // Slot 14, 0xc9e523 (ret 4): writes `text` to the log service: the service is asked for (0x1bf8fb) and, when
    // there is one, gets the record through its cIService slot 28 (no file or line, the name as the group, level
    // 4 for target 0 and 2 otherwise, `text` as the format with no arguments); 1, or 0 without a service. The
    // service reference is released (0x1be5d1) either way.
    uint8_t WriteText(const char *text);

    const void *mVTable;    // 0x00
    uint32_t mField4;       // 0x04 cleared
    uint8_t mField8;        // 0x08 set to 1
    uint8_t mPad9[3];
    uint32_t mFieldC;       // 0x0c cleared
    uint32_t mTarget;       // 0x10 the first argument
    cRZString mName;        // 0x14 built from the second
};
static_assert(sizeof(cLogStdAdapter) == cLogStdAdapter::kSize, "cLogStdAdapter size");

// 0xc9ce86 (__thiscall, ret 8): sets the alias for `key` to `value`, creating it first if there is
// none (a later call for the same key replaces the text). Does nothing for a null value. Called as
// (map, key, value). The library's std::string::assign(const char *) is what it calls (its match
// in db/library-matches.jsonl is also named append, which is wrong for this body).
void __fastcall AddAliasText(AliasMap *self, void *edx, const char *key, const char *value);

// 0xc9ca0e (__thiscall, ret 4): the alias table's deleting destructor: destroys the table (its
// entries, bucket vector and list head), then with flags & 1 a sized delete (0x20). Returns self.
AliasMap *__fastcall DeleteAliasMap(AliasMap *self, void *edx, uint32_t flags);

// QueryInterface of the helper objects and the service (slot 0 of their primary vtables; __thiscall,
// ret 8; written as __fastcall with a dummy edx like src/rz/QueryInterfaces.h). A null ppv fails the
// three helper objects (the service stores through it unchecked). Their own interface id stores this and
// AddRefs it (vtable slot 1); anything else goes to the cRZUnknown base at +4 (riid 1 only), except the
// service, which hands other ids to cRZSystemService::QueryInterface.
//   record 0xc9e06e: id 0xcca124a1; formatter 0xc9e035: id 0xcca124b6; filter 0xc9dff2: ids 0x2ca92615 and
//   0xaca92697; service 0xc9e0a7: id 0x09b69c46 stores the cIService subobject (this + 0x1c, null stays null).
bool __fastcall RecordQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);
bool __fastcall FormatterQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);
bool __fastcall FilterQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);
bool __fastcall ServiceQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// The same QueryInterface entered through a secondary vtable: each subtracts the subobject's offset from
// `self` and runs the one above (0xc9e09f / 0xc9e066 / 0xc9e02d from the base at +4, 0xc9e0d2 from the
// service's +4, 0xc9e0da from its cIService at +0x1c).
namespace thunks {
bool __fastcall RecordQueryInterfaceFromUnknown(void *self, void *edx, uint32_t riid, void **ppv);
bool __fastcall FormatterQueryInterfaceFromUnknown(void *self, void *edx, uint32_t riid, void **ppv);
bool __fastcall FilterQueryInterfaceFromUnknown(void *self, void *edx, uint32_t riid, void **ppv);
bool __fastcall ServiceQueryInterfaceFromUnknown(void *self, void *edx, uint32_t riid, void **ppv);
bool __fastcall ServiceQueryInterfaceFromService(void *self, void *edx, uint32_t riid, void **ppv);
} // namespace thunks

// The deleting destructors as seen from the cRZUnknown base at +4 (vtable slot 3 there): each subtracts
// 4 from `self` and runs the deleting destructor (0xc9c9df, 0xc9c9d7, 0xc9c9cf; ret 4).
namespace thunks {
cLogRecord *__fastcall RecordDeletingFromUnknown(void *self, void *edx, uint32_t flags);
cLogFormatterText *__fastcall FormatterDeletingFromUnknown(void *self, void *edx, uint32_t flags);
cLogFilterGroupLevels *__fastcall FilterDeletingFromUnknown(void *self, void *edx, uint32_t flags);
} // namespace thunks

// ---- The service -------------------------------------------------------------------------------

using NameCallback = void(__stdcall *)(void *arg, const char *name);

// nGZLog::cIService (iid 0x09b69c46): the log service's own interface, slots 3-29 of the game's vtable 0xeb4674. The
// bodies are the hooked free functions below (Service*), each taking this face; the slots' comments are there.
class cIService : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x09b69c46;

    virtual const char *LevelName(int32_t level) = 0;                                       // 3
    virtual int32_t ParseLevelName(const char *text) = 0;                                   // 4
    virtual void AddAlias(const char *key, const char *value) = 0;                          // 5
    virtual void AddFormatter(const char *name) = 0;                                        // 6
    virtual void SetFormatterFormat(const char *name, uint32_t index, const char *format) = 0; // 7
    virtual void SetLevelLimit(uint32_t level) = 0;                                         // 8: the level Log accepts up to
    virtual uint8_t SetFilterGroupLevel(const char *pattern, const char *group, int32_t level) = 0; // 9
    virtual void AddFilter(const char *name) = 0;                                           // 10
    virtual uint8_t AddOutput(const char *name, void *output) = 0;                          // 11
    virtual uint8_t QueryOutput(const char *name, void **ppv) = 0;                          // 12
    virtual bool RemoveOutput(const char *name) = 0;                                        // 13
    virtual uint32_t ClearOutputs() = 0;                                                    // 14
    virtual void OutputSlot12(const char *name, uint32_t arg) = 0;                          // 15
    virtual void SetOutputFilter(const char *name, const char *filter) = 0;                 // 16
    virtual void SetOutputFormatter(const char *name, const char *formatter) = 0;           // 17
    virtual void OutputSlot16(const char *name, uint32_t arg) = 0;                          // 18
    virtual void OutputSlot14(const char *name, uint32_t arg) = 0;                          // 19
    virtual uint8_t SetOutputGroupLevel(const char *pattern, const char *group, int32_t level) = 0; // 20
    virtual uint32_t AddStreamOutput(const char *name, void *stream) = 0;                   // 21
    virtual uint32_t AddFileOutput(const char *name, const char *path, uint8_t append, uint8_t deferOpen) = 0; // 22
    virtual void TimestampedName(cIGZString *name) = 0;                                     // 23
    virtual void WriteConfig(void *stream) = 0;                                             // 24
    virtual void EnumerateNames(void *arg, NameCallback callback) = 0;                      // 25
    virtual void SetFrame(uint32_t frame) = 0;                                              // 26: the frame records carry
    virtual uint8_t Log(uint32_t arg, int32_t level) = 0;                                   // 27
    virtual void Write(const char *file, uint32_t line, const char *group, int32_t level, const char *format,
                       va_list args) = 0;                                                   // 28
    virtual uint32_t FlushOutputs() = 0;                                                    // 29
};

// nGZLog::cService (0xfc bytes; id 0x09b69c4e, priority 0x89543f) in the standalone shape (docs/standalone-shape.md):
// the compiler emits the three vtables (the game's: 0xeb4620 cIGZSystemService, 0xeb4658 cRZUnknownMT at +4, 0xeb4674
// cIService at +0x1c) and the adjustor thunks. The constructor (0xc9c309) builds the record pool, the default formatter
// and filter, the alias table of format names, and starts the asynchronous output thread; it is hooked to
// cLogServiceAdapters::Construct and our caller (cTSAppFW::AddApplicationServicesHere) builds the class directly.
// Named cLogService here (see the file comment).
class cLogService : public rz::cRZSystemService, public cIService {
public:
    static constexpr uint32_t kSize = 0xfc;
    static constexpr uint32_t kServiceSubobjectOffset = 0x1c; // the cIService vtable pointer's offset
    static constexpr uint32_t kServiceId = 0x09b69c4e;
    static constexpr uint32_t kPriority = 0x89543f;
    static constexpr uint32_t kVTable = 0xeb4620;
    static constexpr uint32_t kUnknownVTable = 0xeb4658;
    static constexpr uint32_t kServiceVTable = 0xeb4674;
    static constexpr int kSlotAddRef = 1;
    static constexpr uint32_t kThreadProc = 0xc9e8ab; // the log thread's entry point (ThreadEntry, hooked)
    static constexpr uint32_t kThreadNameRva = 0xeb4b30; // "cGZLogAsynchOutput"
    static constexpr uint32_t kNoPresetPointerRva = 0x1085da0; // game global: pointer to the default preset name

    static constexpr int kFrameworkSlotRegister = 9;    // the framework's slots Init and Shutdown call (the
    static constexpr int kFrameworkSlotUnregister = 10; // framework is the game global at 0x10c9378)
    static constexpr uint32_t kLoggingDisabledRva = 0x11074ba; // game byte global: nonzero turns off ServiceLog

    cLogService(); // 0xc9c309
    // 0xc9c843: the default formatter dropped; every queued record and every pooled one released (no null check);
    // the alias table, the process probe (no null check) and the time stamp deleted; then the members back to front
    // (the pool's buffer, two locks, the nesting map, the thread, the signal, two locks, the default filter, formatter
    // and +0x84 released when set, the three lists, the queue's buffer, the name table, the first lock) and the
    // cRZSystemService base. Deleting: 0xc9ca98 (+4 slot 3, thunk 0xc9c9e7).
    ~cLogService() override;
    cLogService(const cLogService &) = delete;
    cLogService &operator=(const cLogService &) = delete;

    // cIGZUnknown, every face (the +4 / +0x1c ones through the compiler's thunks: QueryInterface 0xc9e0d2 / 0xc9e0da)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0xc9e0a7 (ServiceQueryInterface)
    uint32_t AddRef() override;                              // shared 0x1cc320 (the service part's)
    uint32_t Release() override;                             // shared 0x1cd7a0 (the service part's)

    // cIGZSystemService
    // 0xc9db60 (slot 8): once only (mField20), registers this with the framework (its slot 9). Always true.
    bool Init() override;
    // 0xc9e82f (slot 9): StopOutputThread, unregisters from the framework (its slot 10) if registered, then
    // empties the three lists under the lock. Always true.
    bool Shutdown() override;
    // 0xc9dd3a (slot 10, ret 4): the cIService face's SetFrame (slot 26) with the argument, then its FlushOutputs
    // (slot 29). Always true.
    static constexpr int kServiceSlotTick = 26;
    static constexpr int kServiceSlotFlush = 29;
    bool OnTick(uint32_t arg) override;

    // cIService (the bodies at the RVAs of the free functions below)
    const char *LevelName(int32_t level) override;
    int32_t ParseLevelName(const char *text) override;
    void AddAlias(const char *key, const char *value) override;
    void AddFormatter(const char *name) override;
    void SetFormatterFormat(const char *name, uint32_t index, const char *format) override;
    void SetLevelLimit(uint32_t level) override; // shared_vf_14a260 (+0x24 of the service = +8 of the face)
    uint8_t SetFilterGroupLevel(const char *pattern, const char *group, int32_t level) override;
    void AddFilter(const char *name) override;
    uint8_t AddOutput(const char *name, void *output) override;
    uint8_t QueryOutput(const char *name, void **ppv) override;
    bool RemoveOutput(const char *name) override;
    uint32_t ClearOutputs() override;
    void OutputSlot12(const char *name, uint32_t arg) override;
    void SetOutputFilter(const char *name, const char *filter) override;
    void SetOutputFormatter(const char *name, const char *formatter) override;
    void OutputSlot16(const char *name, uint32_t arg) override;
    void OutputSlot14(const char *name, uint32_t arg) override;
    uint8_t SetOutputGroupLevel(const char *pattern, const char *group, int32_t level) override;
    uint32_t AddStreamOutput(const char *name, void *stream) override;
    uint32_t AddFileOutput(const char *name, const char *path, uint8_t append, uint8_t deferOpen) override;
    void TimestampedName(cIGZString *name) override;
    void WriteConfig(void *stream) override;
    void EnumerateNames(void *arg, NameCallback callback) override;
    void SetFrame(uint32_t frame) override; // shared_vf_470710 (+0xc0 of the service = +0xa4 of the face)
    uint8_t Log(uint32_t arg, int32_t level) override;
    void Write(const char *file, uint32_t line, const char *group, int32_t level, const char *format,
               va_list args) override;
    uint32_t FlushOutputs() override;

    void StartOutputThread();      // 0xc9e880: AddRef this, name the thread, start it on kThreadProc
    // 0xc9e8b9: clears the running flag, signals the thread signal (its slot 1), waits for the thread (its
    // slot 8 with -1), flushes the outputs (cIService slot 29) and releases this (slot 2, which is the
    // reference StartOutputThread took). Returns what the release returns.
    uint32_t StopOutputThread();

    // Lane LZ (src/rz/nGZLogLifetime.size.cpp).
    static constexpr uint32_t kMaxNestingRva = 0x1085da4; // game dword global: writes nested deeper are dropped
    // 0xc9e159: the output thread's loop. Records its thread id (+0x9c); while running, or while the last pass
    // left records queued, waits on the signal (its slot 3, -1) and, when that answers 1, under the queue lock
    // (+0xa0) takes as many records as were queued when it started, each from the front: held (AddRef), handed
    // to every output that is not synchronous (its slot 15 false) and accepts it (slot 4) for output (slot 5),
    // each output's calls inside catch(...), under the output lock (+0x90); then returned to the pool and
    // released. An empty queue resets the signal (its slot 6).
    void OutputThread();
    // 0xc9e8ab (ret 4): the thread procedure StartOutputThread passes: OutputThread on `service`, then 0.
    static uint32_t __stdcall ThreadEntry(void *service);
    // 0xc9d020 (ret 4): under the pool lock (+0xe0) the record goes back on the pool's list (push_back).
    void ReturnRecord(void *record);

    // The members that clean up after themselves: the hook-shaped types with the calls the original's constructor and
    // destructor make on them (the declaration order is the original's construction order, so the compiler's
    // destruction order is the original's too).
    struct OwnedLock : cRZCriticalSection {
        OwnedLock() { Construct(); }
        ~OwnedLock() { Destruct(); }
        OwnedLock(const OwnedLock &) = delete;
        OwnedLock &operator=(const OwnedLock &) = delete;
    };
    struct OwnedSignal : cRZThreadSignal { // an auto-reset event
        OwnedSignal() { Construct(1); }
        ~OwnedSignal() { Destruct(); }
        OwnedSignal(const OwnedSignal &) = delete;
        OwnedSignal &operator=(const OwnedSignal &) = delete;
    };
    struct OwnedThread : cRZThread {
        OwnedThread() { Construct(); }
        ~OwnedThread(); // appdebugger::DestroyThread
        OwnedThread(const OwnedThread &) = delete;
        OwnedThread &operator=(const OwnedThread &) = delete;
    };
    struct HeldObject : RZUnknownSlot { // released (slot 2) when set
        HeldObject() { mObject = nullptr; }
        ~HeldObject();
        HeldObject(const HeldObject &) = delete;
        HeldObject &operator=(const HeldObject &) = delete;
    };
    struct HeldWord { // an object kept as a dword, released (slot 2) when set
        uint32_t mValue = 0;
        HeldWord() = default;
        ~HeldWord();
        HeldWord(const HeldWord &) = delete;
        HeldWord &operator=(const HeldWord &) = delete;
        HeldWord &operator=(uint32_t value) { mValue = value; return *this; }
        operator uint32_t() const { return mValue; }
    };

    uint8_t mField20 = 0;          // 0x20 registered with the framework
    uint8_t mPad21[3];
    uint32_t mField24 = 7;         // 0x24 the level limit (SetLevelLimit)
    OwnedLock mLock28;             // 0x28
    NameSet mNames;                // 0x30
    uint32_t mQueueCount = 0;      // 0x50
    std::vector<void *> mQueue;   // 0x54 reserved for 2 * mRecordCount entries, left empty
    std::vector<RefHolder> mOutputs;    // 0x60
    std::vector<RefHolder> mFilters;    // 0x6c
    std::vector<RefHolder> mFormatters; // 0x78
    HeldWord mField84;             // 0x84 cleared; an object the destructor releases (slot 2) when set
    HeldObject mFormatter;         // 0x88 the default formatter (cLogFormatterText)
    HeldObject mFilter;            // 0x8c the default filter (cLogFilterGroupLevels)
    OwnedLock mLock90;             // 0x90
    uint32_t mThreadId;            // 0x98 the constructing thread
    uint32_t mOutputThreadId;      // 0x9c cleared
    OwnedLock mLockA0;             // 0xa0
    OwnedSignal mSignal;           // 0xa8 auto-reset event
    OwnedThread mThread;           // 0xb0
    uint8_t mOutputRunning;        // 0xb8 set to 1 at the end of the constructor
    uint8_t mPadB9[3];
    int32_t mRecordCount = 100;    // 0xbc
    uint32_t mFrame = 0;           // 0xc0 (SetFrame)
    std::map<uint32_t, uint32_t> mNesting; // 0xc4
    OwnedLock mLockCC;             // 0xcc
    cLogTimestamp *mTimestamp;     // 0xd4
    cLogProcessInfo *mProcessInfo; // 0xd8
    uint32_t mRecordNumber;        // 0xdc cleared
    OwnedLock mLockE0;             // 0xe0
    std::vector<void *> mRecords;  // 0xe8 the record pool (mRecordCount cLogRecord, each AddRef'd once)
    uint8_t mCollectGroups = 1;    // 0xf4 group names given to the write path and the level setters are recorded
    uint8_t mPadF5[3];
    AliasMap *mAliases;            // 0xf8 format name -> format text
};
static_assert(sizeof(cLogService) == cLogService::kSize, "cLogService size");
static_assert(offsetof(cLogService, mServiceId) == 0xc && offsetof(cLogService, mField20) == 0x20, "cLogService layout: the service part at +0");
static_assert(offsetof(cLogService, mField24) == 0x24 && offsetof(cLogService, mField84) == 0x84 && offsetof(cLogService, mOutputRunning) == 0xb8, "cLogService layout");
static_assert(offsetof(cLogService, mFrame) == 0xc0 && offsetof(cLogService, mCollectGroups) == 0xf4, "cLogService layout");
static_assert(offsetof(cLogService, mNames) == 0x30 && offsetof(cLogService, mQueue) == 0x54 && offsetof(cLogService, mOutputs) == 0x60, "cLogService layout");
static_assert(offsetof(cLogService, mFormatter) == 0x88 && offsetof(cLogService, mLock90) == 0x90 && offsetof(cLogService, mSignal) == 0xa8, "cLogService layout");
static_assert(offsetof(cLogService, mThread) == 0xb0 && offsetof(cLogService, mNesting) == 0xc4 && offsetof(cLogService, mTimestamp) == 0xd4, "cLogService layout");
static_assert(offsetof(cLogService, mRecords) == 0xe8 && offsetof(cLogService, mAliases) == 0xf8, "cLogService layout");

// ---- cIService methods ---------------------------------------------------------------------------
// Slots of the cIService vtable (0xeb4674) at service + 0x1c: `sub` is that subobject, the service is
// sub - 0x1c. __thiscall; written as __fastcall with a dummy edx.

// Slot 5, 0xc9ceda (ret 8): AddAliasText on the service's alias table (key, value).
void __fastcall ServiceAddAlias(void *sub, void *edx, const char *key, const char *value);
// Slot 14, 0xc9e0e2: with the lock at +0x90 held (vtable slots 1 and 2 of the lock object), empties the
// list of output back ends (each released). Returns what the lock's slot 2 (unlock) returns.
uint32_t __fastcall ServiceClearOutputs(void *sub, void *edx);
// Slot 29, 0xc9d04d: with the same lock held, calls slot 21 of every output back end in order. Returns
// what the unlock returns.
uint32_t __fastcall ServiceFlushOutputs(void *sub, void *edx);

// The service's lookups by name (__thiscall on the service itself, ret 4): the first element of the list
// whose name matches case-insensitively (_stricmp), or null. The name comes from slot 11 of an output back
// end (its text) and slot 7 of a filter or formatter. The end of the list is read again on every pass.
void *__fastcall FindOutput(cLogService *service, void *edx, const char *name);     // 0xc9d4bc (outputs, +0x60)
void *__fastcall FindFilter(cLogService *service, void *edx, const char *name);     // 0xc9d2bc (filters, +0x6c)
void *__fastcall FindFormatter(cLogService *service, void *edx, const char *name);  // 0xc9d2ef (formatters, +0x78)

// 0xc9f33e (__thiscall, ret 8): vector<RefHolder>::erase: the elements after `where` are copy-assigned down
// (AddRef the new, release the old), the last one is destroyed (released) and the list shortens by one;
// *out receives `where`.
void __fastcall EraseRef(std::vector<RefHolder> *self, void *edx, RefHolder **out, RefHolder *where);

// Level names. 0xc9e8eb (__cdecl): the text for a log level: levels 0, 2, 3, 4, 5, 6 and 7 read game string
// globals (0x1085d94, 0x1085d6c, 0x1085d74, 0x1085d7c, 0x1085d84, 0x1085d8c, 0x1085d64); any other level
// (1 included) gives an empty string. 0xc9dc38 (__cdecl): the level a text names: -1 for null; otherwise
// the first of those texts (in the order 2, 3, 4, 5, 6, 0, 7) that the argument contains, case-insensitively,
// or -1. Slots 3 (0xc9e943, ret 4) and 4 (0xc9dd21, ret 4) of the cIService vtable forward to them.
const char *__cdecl LevelName(int32_t level);
int32_t __cdecl ParseLevelName(const char *text);
const char *__fastcall ServiceLevelName(void *sub, void *edx, int32_t level);
int32_t __fastcall ServiceParseLevelName(void *sub, void *edx, const char *text);

// 0xc9cb65 slot 6 / 0xc9caf5 slot 10 (ret 4): when no formatter (filter) of that name exists, makes one
// (new, constructor, slot 6 = its name; a formatter also gets the alias table and time stamp), holds a
// reference across adding it to the list (so the list's copy is the only reference left), and returns.
void __fastcall ServiceAddFormatter(void *sub, void *edx, const char *name);
void __fastcall ServiceAddFilter(void *sub, void *edx, const char *name);
// 0xc9db7e slot 27 (ret 8; arguments are the record's second value, then the level): builds a record on the stack (constructor, SetLevel(level, arg)) unless the level
// is above the service's limit (+0x24 of the service, signed) or logging is disabled (byte global); with the
// lock held offers it to each output back end (its slot 3) until one answers nonzero. 1 when one did.
uint8_t __fastcall ServiceLog(void *sub, void *edx, uint32_t arg, int32_t level);

// More cIService slots (`sub` as above). Each looks an object up by name and, when found, holds a reference
// (AddRef, slot 1) across one call through its vtable and then releases it (slot 2).
//  0xc9e3ea slot 7  (ret 0xc): formatter `name`, its slot 3 (index, format name)
//  0xc9e5fb slot 15 (ret 8):   output `name`, its slot 12 (arg)
//  0xc9e669 slot 18 (ret 8):   output `name`, its slot 16 (arg)
//  0xc9e7f2 slot 19 (ret 8):   output `name`, its slot 14 (arg)
//  0xc9e62b slot 16 (ret 8):   output `name`, its slot 9 (the filter called `filter`, looked up after the AddRef)
//  0xc9e699 slot 17 (ret 8):   output `name`, its slot 7 (the formatter called `formatter`)
void __fastcall ServiceSetFormatterFormat(void *sub, void *edx, const char *name, uint32_t index, const char *format);
void __fastcall ServiceOutputSlot12(void *sub, void *edx, const char *name, uint32_t arg);
void __fastcall ServiceOutputSlot16(void *sub, void *edx, const char *name, uint32_t arg);
void __fastcall ServiceOutputSlot14(void *sub, void *edx, const char *name, uint32_t arg);
void __fastcall ServiceSetOutputFilter(void *sub, void *edx, const char *name, const char *filter);
void __fastcall ServiceSetOutputFormatter(void *sub, void *edx, const char *name, const char *formatter);
// 0xc9d374 slot 25 (ret 8): does nothing for a null callback; otherwise, with the lock at +0x28 held, calls
// callback(arg, name) (__stdcall) for every name in the service's name table, in the table's list order.
void __fastcall ServiceEnumerateNames(void *sub, void *edx, void *arg, NameCallback callback);
// 0xc9d3f5 slot 12 (ret 8): with the lock held, the output called `name` is asked for interface 0x49b84afb
// into *ppv (its slot 0); returns the byte it answered with (not normalised to 0/1), 0 when there is no such output. 0xc9e101 slot 13 (ret 4):
// with the lock held, removes the output called `name` from the list (EraseRef); true when there was one.
uint8_t __fastcall ServiceQueryOutput(void *sub, void *edx, const char *name, void **ppv);
bool __fastcall ServiceRemoveOutput(void *sub, void *edx, const char *name);
// 0xc9ce52 slot 21 (ret 8): a new default back end (cLogDefaultOutputBackEnd) named `name` on `stream`, handed to
// slot 11 (add an output) as (name, back end). 0xc9caba slot 22 (ret 0x10): the same with a log file back end
// (cLogFileOutputBackEnd: path, append, deferOpen). Both return what slot 11 returns (a failed allocation hands
// it null).
uint32_t __fastcall ServiceAddStreamOutput(void *sub, void *edx, const char *name, void *stream);
uint32_t __fastcall ServiceAddFileOutput(void *sub, void *edx, const char *name, const char *path, uint8_t append, uint8_t deferOpen);
// 0xc9cc74 slot 11 (ret 8): adds the output back end `output` as `name`; 0 (nothing done) when an output of that name
// (slot 11, _stricmp) is already listed. The filter is the output's own (slot 8), else the service's of that name,
// else a clone (slot 8) of the default filter (+0x8c) named `name` (slot 6) and listed; the formatter likewise
// (output slot 6, the default at +0x88). The output gets its text (slot 10), formatter (7), filter (9) and is
// enabled (12, 1), then is listed. All under the lock at +0x90; 1.
uint8_t __fastcall ServiceAddOutput(void *sub, void *edx, const char *name, void *output);
// 0xc9e2da slot 9 / 0xc9e6d7 slot 20 (ret 0xc): sets `group`'s level on every filter whose name (slot 7) matches
// `pattern` (RZWildcard::MatchCase, not case-sensitive), or on the filter of every output whose text (slot 11)
// matches. Filters are asked for interface 0xaca92697 first (their slot 0); the set is its slot 9. While the flag
// at +0xf4 is set, `group` is first recorded in the name table (lock +0x28) unless it is the "NoGroup" or
// "AnyGroup" text (game pointers 0x1085d98 / 0x1085d9c, _stricmp). 1 when anything matched (slot 20: any output,
// even one whose filter declines the interface).
uint8_t __fastcall ServiceSetFilterGroupLevel(void *sub, void *edx, const char *pattern, const char *group, int32_t level);
uint8_t __fastcall ServiceSetOutputGroupLevel(void *sub, void *edx, const char *pattern, const char *group, int32_t level);
// 0xc9d07a (ret 8, ecx unused): `out` = `text` in double quotes, line breaks and tabs written as \n and \t; returns
// `out`.
cRZString *__fastcall QuoteText(void *ecx, void *edx, const cRZString *text, cRZString *out);
// 0xc9ead7 (__thiscall on the alias table, ret 4): a "logPreset <name> <quoted text>" line on `stream` (slots 14
// and 15) for every alias, in the table's order.
void __fastcall WritePresets(AliasMap *aliases, void *edx, void *stream);
// 0xc9eded slot 24 (ret 4; a thunk to the body at 0xc9edf5): the configuration text on `stream`: the group names
// (lock +0x28) and presets, then every formatter's and filter's own text (their slot 5), then every output's
// (slot 19, lock +0x90), each section under its header line.
void __fastcall ServiceWriteConfig(void *sub, void *edx, void *stream);

// Lane LZ (src/rz/nGZLogLifetime.size.cpp).
// 0xc9d505 slot 23 (ret 4): turns the file name `name` into a time-stamped one (its slot 16 assigns the result):
// "<stem> <date> <clock>.<extension>", the date ("%Y/%m/%d") with '/' and the clock ("%H:%M:%S:mmm") with ':'
// written as '.', from a copy of the service's time stamp. The extension is the text from the last '.' on
// (the dot included, so the result has two); the stem loses the character before that dot.
void __fastcall ServiceTimestampedName(void *sub, void *edx, cIGZString *name);
// 0xc9dd56 slot 28 (ret 0x18): the write path. Nothing unless the service runs (primary slot 6); the group is
// recorded in the name table while +0xf4 is set; nothing unless slot 27 accepts (group, level), nor on the output
// thread itself. Per thread a nesting count (+0xc4, lock +0xcc) bounds re-entry by the game global
// kMaxNestingRva. Otherwise: the next record number, the text (`format` as is without `args`, else FormatText
// into a 1 KB buffer), the CPU and stamp times and the frame go into a record on the stack, which every output
// that accepts it (its slot 4) is given (slot 5) after its time since the last record (slots 22 / 23), each
// output inside catch(...), under the lock at +0x90. The writing (constructing) thread flushes (slot 29).
void __fastcall ServiceWrite(void *sub, void *edx, const char *file, uint32_t line, const char *group, int32_t level,
                             const char *format, va_list args);
// 0xc9d22a (__cdecl): `format` printed into `buffer` (_vsnprintf). A text of exactly `size` characters (no room
// for the NUL) is printed again into a new block (malloc) of size + 1; a -1 answer (too long) prints into blocks
// of twice the size (the previous one freed) until it fits or the size reaches 100000 (the last attempt's block
// is returned anyway). Returns the text's buffer.
char *__cdecl FormatText(char *buffer, uint32_t size, const char *format, va_list args);

namespace thunks {
// 0xc9c9e7 (ret 4): the service's deleting destructor seen from the cRZUnknown base at +4.
cLogService *__fastcall ServiceDeletingFromUnknown(void *self, void *edx, uint32_t flags);
} // namespace thunks

// The hooks' entry points for the class's members (docs/standalone-shape.md): `this` in ecx, the dummy edx.
struct cLogServiceAdapters {
    static cLogService *__fastcall Construct(void *memory, void *edx);                     // 0xc9c309
    static void __fastcall Destruct(cLogService *self, void *edx);                         // 0xc9c843
    static cLogService *__fastcall DeletingDestructor(cLogService *self, void *edx, uint32_t flags); // 0xc9ca98
    static bool __fastcall Init(cLogService *self, void *edx);                             // 0xc9db60
    static bool __fastcall Shutdown(cLogService *self, void *edx);                         // 0xc9e82f
    static bool __fastcall OnTick(cLogService *self, void *edx, uint32_t arg);             // 0xc9dd3a
};

} // namespace nGZLog
