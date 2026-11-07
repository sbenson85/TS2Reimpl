// nGZLog::cGZLogFormatterText: the text formatter's slots and helpers, and the time stamp's texts (lane LY, F4).
// The objects, constructors and destructors are in nGZLog.size.cpp.
#include "rz/nGZLog.h"

#include "game/game.h"
#include "game/import.h"
#include "game/vtable.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

#include <windows.h>

namespace nGZLog {
namespace {

// The record's slots.
constexpr int kRecordSlotLevel = 3;
constexpr int kRecordSlotMessage = 4;
constexpr int kRecordSlotGroup = 5;
constexpr int kRecordSlotTime = 6;
constexpr int kRecordSlotField7 = 7;    // 'f'
constexpr int kRecordSlotDuration8 = 8; // 'c'
constexpr int kRecordSlotField9 = 9;    // 'C'
constexpr int kRecordSlotDuration10 = 10; // 'p'
constexpr int kRecordSlotField11 = 11;  // 'r'
constexpr int kRecordSlotFile = 12;
constexpr int kRecordSlotLine = 13;
using RecordWordFn = int32_t(__thiscall *)(void *);
using RecordTextFn = const char *(__thiscall *)(void *);

// The output stream's slots: a C string and a cIGZString.
constexpr int kStreamSlotText = 14;
constexpr int kStreamSlotString = 15;
using StreamTextFn = uint32_t(__thiscall *)(void *, const char *);
using StreamStringFn = uint32_t(__thiscall *)(void *, const void *);

// The memory report object's slot that prints into a string.
constexpr int kReportSlotPrint = 9;
constexpr uint32_t kReportArgument1 = 9;
constexpr uint32_t kReportArgument2 = 0x39;
constexpr uint32_t kReportArgument3 = 0;
using ReportPrintFn = void(__thiscall *)(void *, cRZString *, uint32_t, uint32_t, uint32_t);

constexpr uint32_t kIatGetModuleHandleA = 0xd9a114;
constexpr uint32_t kIatGetProcAddress = 0xd9a124;
using GetModuleHandleAFn = HMODULE(WINAPI *)(const char *);
using GetProcAddressFn = FARPROC(WINAPI *)(HMODULE, const char *);

constexpr int kFormatCount = 5;
constexpr int32_t kLevelAll = 7;
constexpr int32_t kLevelNone = 0;
// The levels Write lists, in its order.
constexpr int32_t kWrittenLevels[] = {6, 5, 4, 3, 2};

constexpr char kDecimal[] = "%d";
constexpr char kElapsedFormat[] = "%02d:%02d:%02d:";
constexpr char kDateFormat[] = "%Y/%m/%d";
constexpr char kClockFormat[] = "%H:%M:%S:";
constexpr size_t kTextSize = 16;
constexpr size_t kClockMillisecondsAt = 9;
constexpr int32_t kMillisecondsPerSecond = 1000;
constexpr int32_t kSecondsPerMinute = 60;
constexpr int32_t kMinutesPerHour = 60;
constexpr int32_t kHoursPerDay = 24;

constexpr char kFormatterWord[] = "logFormatter";
constexpr char kPresetWord[] = "logFormatterPreset";
constexpr char kSeparator[] = " ";
constexpr char kNewline[] = "\n";

int32_t RecordWord(void *record, int slot)
{
    switch (slot) {
    case kRecordSlotLevel: return VirtualCall<RecordWordFn, kRecordSlotLevel>(record);
    case kRecordSlotTime: return VirtualCall<RecordWordFn, kRecordSlotTime>(record);
    case kRecordSlotField7: return VirtualCall<RecordWordFn, kRecordSlotField7>(record);
    case kRecordSlotDuration8: return VirtualCall<RecordWordFn, kRecordSlotDuration8>(record);
    case kRecordSlotField9: return VirtualCall<RecordWordFn, kRecordSlotField9>(record);
    case kRecordSlotDuration10: return VirtualCall<RecordWordFn, kRecordSlotDuration10>(record);
    case kRecordSlotField11: return VirtualCall<RecordWordFn, kRecordSlotField11>(record);
    default: return VirtualCall<RecordWordFn, kRecordSlotLine>(record);
    }
}

const char *RecordText(void *record, int slot)
{
    switch (slot) {
    case kRecordSlotMessage: return VirtualCall<RecordTextFn, kRecordSlotMessage>(record);
    case kRecordSlotGroup: return VirtualCall<RecordTextFn, kRecordSlotGroup>(record);
    default: return VirtualCall<RecordTextFn, kRecordSlotFile>(record);
    }
}

const char *NoPreset() { return *GameAddress<const char *>(cLogService::kNoPresetPointerRva); }

uint32_t WriteText(void *stream, const char *text) { return VirtualCall<StreamTextFn, kStreamSlotText>(stream, text); }
uint32_t WriteString(void *stream, const cRZString &text) { return VirtualCall<StreamStringFn, kStreamSlotString>(stream, &text); }

} // namespace

// ---- the time stamp's texts ----

void __stdcall cLogTimestamp::ThreeDigits(char *out, uint32_t value)
{
    out[2] = static_cast<char>('0' + value % 10);
    out[3] = 0;
    out[0] = static_cast<char>('0' + value / 100 % 10);
    out[1] = static_cast<char>('0' + value / 10 % 10);
}

void __stdcall cLogTimestamp::FormatElapsed(int32_t milliseconds, char *out)
{
    const int32_t seconds = milliseconds / kMillisecondsPerSecond;
    const int32_t minutes = seconds / kSecondsPerMinute;
    const int32_t hours = minutes / kMinutesPerHour;
    std::sprintf(out, kElapsedFormat, hours % kHoursPerDay, minutes % kMinutesPerHour, seconds % kSecondsPerMinute);
    ThreeDigits(out + kClockMillisecondsAt, static_cast<uint32_t>(milliseconds % kMillisecondsPerSecond));
}

void cLogTimestamp::Update(int32_t milliseconds)
{
    if (static_cast<int32_t>(mElapsedHigh) == milliseconds) return;
    mElapsedHigh = static_cast<uint32_t>(milliseconds);
    // 32-bit arithmetic that wraps, as the original's; the seconds are sign-extended to 64 bits.
    const int32_t sinceStart = static_cast<int32_t>(static_cast<uint32_t>(milliseconds) - mElapsedLow);
    const int32_t remainder = sinceStart % kMillisecondsPerSecond;
    __time64_t when = static_cast<int32_t>(static_cast<uint32_t>(sinceStart / kMillisecondsPerSecond) + mTime);
    const tm *parts = mFlag2 ? _gmtime64(&when) : _localtime64(&when);
    if (mFlag0) std::strftime(mDate, kTextSize, kDateFormat, parts);
    if (mFlag1) {
        std::strftime(mClock, kTextSize, kClockFormat, parts);
        ThreeDigits(mClock + kClockMillisecondsAt, static_cast<uint32_t>(remainder));
        FormatElapsed(sinceStart, mElapsed);
    }
}

// ---- the formatter ----

int32_t __stdcall cLogFormatterText::LevelIndex(int32_t level)
{
    switch (level) {
    case 2: return 0;
    case 3: return 1;
    case 4: return 2;
    case 5: return 4;
    case 6: return 3;
    default: return -1;
    }
}

uint8_t __fastcall LookupAlias(AliasMap *self, void *, const char *name, cRZString *out)
{
    out->Erase(0, 0xffffffffu);
    const auto found = self->find(LogString(name));
    if (found == self->end()) return 0;
    out->Assign(*found->second.AsInterface());
    return 1;
}

void cLogFormatterText::SetFormat(int32_t level, const char *preset)
{
    LogString text;
    if (!mAliases || !LookupAlias(mAliases, nullptr, preset, &text)) return;
    const int32_t index = LevelIndex(level);
    if (index != -1) {
        mFormats[index].mString = text.mString;
        mPresets[index].mString.assign(preset);
    }
    if (level == kLevelAll) {
        for (int i = 0; i < kFormatCount; ++i) {
            mFormats[i].mString = text.mString;
            mPresets[i].mString.assign(preset);
        }
    } else if (level == kLevelNone) {
        for (int i = 0; i < kFormatCount; ++i) {
            mFormats[i].mString.assign("", 0);
            mPresets[i].mString.assign(NoPreset());
        }
    }
}

cRZString *cLogFormatterText::Format(void *record)
{
    Compose(record);
    return &mOutput;
}

void cLogFormatterText::Compose(void *record)
{
    mOutput.mString.clear();
    const int32_t index = LevelIndex(RecordWord(record, kRecordSlotLevel));
    if (index == -1 || mFormats[index].mString.empty()) return;
    const std::string &format = mFormats[index].mString;
    const uint32_t length = mFormats[index].Strlen();
    if (length == 0) return;
    for (uint32_t i = 0; i < length; ++i) {
        char c = format[i];
        if (c == '%') {
            if (i < length - 1) {
                ++i;
                Expand(record, format[i]);
            }
            continue;
        }
        if (c == '\\') {
            if (i >= length - 1) continue;
            ++i;
            c = format[i];
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
        }
        mOutput.mString.push_back(c);
    }
}

void cLogFormatterText::Expand(void *record, char code)
{
    std::string &out = mOutput.mString;
    char text[kTextSize];
    switch (code) {
    case 'N': {
        // The message with its line breaks as spaces.
        LogString message(RecordText(record, kRecordSlotMessage));
        for (uint32_t i = 0; i < message.Strlen(); ++i)
            if (message.mString[i] == '\n') message.mString[i] = ' ';
        out.append(message.mString);
        return;
    }
    case 'M': {
        // The message, less a final line break.
        const char *message = RecordText(record, kRecordSlotMessage);
        out.append(message);
        const int32_t last = static_cast<int32_t>(std::strlen(message)) - 1;
        if (last >= 0 && message[last] == '\n') out.resize(mOutput.Strlen() - 1, 0);
        return;
    }
    case '%':
        out.push_back('%');
        return;
    case 'G':
        out.append(RecordText(record, kRecordSlotGroup));
        return;
    case 'L':
        out.append(LevelName(RecordWord(record, kRecordSlotLevel)));
        return;
    case 'F':
        if (!RecordText(record, kRecordSlotFile)) return;
        out.append(RecordText(record, kRecordSlotFile));
        out.push_back('(');
        mScratch.Sprintf(kDecimal, RecordWord(record, kRecordSlotLine));
        out.append(mScratch.mString);
        out.push_back(')');
        return;
    case 'D':
        mTimestamp->Update(RecordWord(record, kRecordSlotTime));
        out.append(mTimestamp->mDate);
        return;
    case 't':
        mTimestamp->Update(RecordWord(record, kRecordSlotTime));
        out.append(mTimestamp->mClock);
        return;
    case 'e':
        mTimestamp->Update(RecordWord(record, kRecordSlotTime));
        out.append(mTimestamp->mElapsed);
        return;
    case 'c':
        cLogTimestamp::FormatElapsed(RecordWord(record, kRecordSlotDuration8), text);
        out.append(text, std::strlen(text));
        return;
    case 'p':
        cLogTimestamp::FormatElapsed(RecordWord(record, kRecordSlotDuration10), text);
        out.append(text, std::strlen(text));
        return;
    case 'C':
        mScratch.Sprintf(kDecimal, RecordWord(record, kRecordSlotField9));
        break;
    case 'f':
        mScratch.Sprintf(kDecimal, RecordWord(record, kRecordSlotField7));
        break;
    case 'r':
        mScratch.Sprintf(kDecimal, RecordWord(record, kRecordSlotField11));
        break;
    case 'R': {
        void *report = MemoryReport();
        if (!report) return;
        VirtualCall<ReportPrintFn, kReportSlotPrint>(report, &mScratch, kReportArgument1, kReportArgument2, kReportArgument3);
        break;
    }
    default:
        return;
    }
    out.append(mScratch.mString);
}

void cLogFormatterText::Write(void *stream)
{
    WriteText(stream, kFormatterWord);
    WriteText(stream, kSeparator);
    WriteString(stream, mName);
    WriteText(stream, kNewline);
    for (const int32_t level : kWrittenLevels) {
        WriteText(stream, kPresetWord);
        WriteText(stream, kSeparator);
        WriteString(stream, mName);
        WriteText(stream, kSeparator);
        WriteString(stream, mPresets[LevelIndex(level)]);
        WriteText(stream, kSeparator);
        WriteText(stream, LevelName(level));
        WriteText(stream, kNewline);
    }
}

cLogFormatterText *cLogFormatterText::Clone()
{
    void *memory = ::operator new(kSize);
    cLogFormatterText *clone = memory ? static_cast<cLogFormatterText *>(memory)->Construct() : nullptr;
    // ORIGINAL-BUG: a failed allocation is not checked; the calls below go through null.
    clone->SetAliasMap(mAliases);
    clone->SetTimestamp(mTimestamp);
    for (int i = 0; i < kFormatCount; ++i) {
        clone->mFormats[i].mString = mFormats[i].mString;
        clone->mPresets[i].mString = mPresets[i].mString;
    }
    return clone;
}

void *__cdecl MemoryReport()
{
    const HMODULE module = ImportFunction<GetModuleHandleAFn, kIatGetModuleHandleA>()(nullptr);
    const FARPROC report = ImportFunction<GetProcAddressFn, kIatGetProcAddress>()(module, "__export__GetMemoryReport");
    if (!report) return nullptr;
    return reinterpret_cast<void *(__cdecl *)()>(report)();
}

} // namespace nGZLog
