// nGZLog::cGZLogFilterGroupLevels: the group level filter's slots (lane LY, F4). The object, its constructor and
// destructors are in nGZLog.size.cpp.
#include "rz/nGZLog.h"

#include "game/game.h"
#include "game/vtable.h"

#include <cstring>
#include <new>

namespace nGZLog {
namespace {

// The game's pointers to the two reserved group names ("AnyGroup", "NoGroup").
constexpr uint32_t kAnyGroupPointerRva = 0x1085d9c;
constexpr uint32_t kNoGroupPointerRva = 0x1085d98;

// The configuration text's words.
constexpr char kFilterWord[] = "logFilter";
constexpr char kLevelWord[] = "logFilterLevel";
constexpr char kSeparator[] = " ";
constexpr char kNewline[] = "\n";

// The record's slots: its level and its group's name.
constexpr int kRecordSlotLevel = 3;
constexpr int kRecordSlotGroup = 5;
using RecordLevelFn = int32_t(__thiscall *)(void *);
using RecordGroupFn = const char *(__thiscall *)(void *);

// The output stream's slots: a C string and a cIGZString.
constexpr int kStreamSlotText = 14;
constexpr int kStreamSlotString = 15;
using StreamTextFn = uint32_t(__thiscall *)(void *, const char *);
using StreamStringFn = uint32_t(__thiscall *)(void *, const void *);

using GroupLevelFn = int32_t(__thiscall *)(void *, const char *);

const char *AnyGroup() { return *GameAddress<const char *>(kAnyGroupPointerRva); }
const char *NoGroup() { return *GameAddress<const char *>(kNoGroupPointerRva); }

uint32_t WriteText(void *stream, const char *text) { return VirtualCall<StreamTextFn, kStreamSlotText>(stream, text); }
uint32_t WriteString(void *stream, const cRZString &text) { return VirtualCall<StreamStringFn, kStreamSlotString>(stream, &text); }

} // namespace

uint8_t cLogFilterGroupLevels::Accept(void *record)
{
    if (VirtualCall<RecordLevelFn, kRecordSlotLevel>(record) > mAnyGroupLevel) return 0;
    const char *group = VirtualCall<RecordGroupFn, kRecordSlotGroup>(record);
    const int32_t groupLevel = VirtualCall<GroupLevelFn, kSlotGroupLevel>(this, group);
    if (VirtualCall<RecordLevelFn, kRecordSlotLevel>(record) > groupLevel) return 0;
    return 1;
}

void cLogFilterGroupLevels::Write(void *stream)
{
    WriteText(stream, kFilterWord);
    WriteText(stream, kSeparator);
    WriteString(stream, mName);
    WriteText(stream, kNewline);

    WriteText(stream, kLevelWord);
    WriteText(stream, kSeparator);
    WriteString(stream, mName);
    WriteText(stream, kSeparator);
    WriteText(stream, AnyGroup());
    WriteText(stream, kSeparator);
    WriteText(stream, LevelName(mAnyGroupLevel));
    WriteText(stream, kNewline);

    WriteText(stream, kLevelWord);
    WriteText(stream, kSeparator);
    WriteString(stream, mName);
    WriteText(stream, kSeparator);
    WriteText(stream, NoGroup());
    WriteText(stream, kSeparator);
    WriteText(stream, LevelName(mNoGroupLevel));
    WriteText(stream, kNewline);

    for (const auto &entry : mLevels) {
        WriteText(stream, kLevelWord);
        WriteText(stream, kSeparator);
        WriteString(stream, mName);
        WriteText(stream, kSeparator);
        WriteString(stream, entry.first);
        WriteText(stream, kSeparator);
        WriteText(stream, LevelName(static_cast<int32_t>(entry.second)));
        WriteText(stream, kNewline);
    }
}

cLogFilterGroupLevels *cLogFilterGroupLevels::Clone()
{
    void *memory = ::operator new(kSize);
    cLogFilterGroupLevels *clone = memory ? static_cast<cLogFilterGroupLevels *>(memory)->Construct() : nullptr;
    // ORIGINAL-BUG: a failed allocation is not checked here; the copy writes through null.
    clone->mAnyGroupLevel = mAnyGroupLevel;
    clone->mNoGroupLevel = mNoGroupLevel;
    // A copy moved in: the table's key type has no copy assignment (a fresh table never reuses nodes).
    clone->mLevels = LevelMap(mLevels);
    return clone;
}

void cLogFilterGroupLevels::SetGroupLevel(const char *group, int32_t level)
{
    if (_stricmp(group, AnyGroup()) == 0) {
        mAnyGroupLevel = level;
        return;
    }
    if (_stricmp(group, NoGroup()) == 0) {
        mNoGroupLevel = level;
        return;
    }
    mLevels[LogString(group)] = static_cast<uint32_t>(level);
}

int32_t cLogFilterGroupLevels::GroupLevel(const char *group)
{
    const auto found = mLevels.find(LogString(group));
    if (found == mLevels.end()) return mNoGroupLevel;
    return static_cast<int32_t>(found->second);
}

} // namespace nGZLog
