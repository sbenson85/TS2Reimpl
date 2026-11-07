// nGZLog::cService's write path, output thread and destructor, with the helpers they use (lane LZ, F4b).
#include "rz/nGZLog.h"

#include "app/AppDebugger.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cIGZCriticalSection.h"
#include "ifaces/cIGZSystemService.h"
#include "ifaces/cIGZThreadSignal.h"
#include "rz/RZFileStreams.h"
#include "rz/StopwatchMore.h"
#include "rz/cRZStringSub.h"
#include "rz/cRZThreadRelease.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

namespace nGZLog {
namespace {

constexpr uint32_t kIatGetProcessTimes = 0xd9a42c;
using GetProcessTimesFn = BOOL(WINAPI *)(HANDLE, LPFILETIME, LPFILETIME, LPFILETIME, LPFILETIME);

// The record and output slots the write path and the output thread call (no method facts for these vtables).
constexpr int kSlotRelease = 2;
constexpr int kSlotAddRef = 1;
constexpr int kOutputSlotAccept = 4;       // (record): nonzero when the output takes it
constexpr int kOutputSlotOutput = 5;       // (record)
constexpr int kOutputSlotSynchronous = 15; // the "synch" byte: written by the write path itself
constexpr int kOutputSlotLastTime = 22;    // the time of the last record it was given
constexpr int kOutputSlotSetLastTime = 23; // (time)
constexpr int kServiceSlotEnabled = 27;    // ServiceLog (group, level)
constexpr int kServiceSlotFlush = 29;      // ServiceFlushOutputs
constexpr int kServiceSlotWrite = 28;      // ServiceWrite
constexpr uint32_t kTextBufferSize = 0x400;
constexpr int32_t kMaxTextSize = 100000;
constexpr uint32_t kLevelForTarget0 = 4;
constexpr uint32_t kLevelForOtherTargets = 2;
constexpr char kDateSeparator = '/';
constexpr char kClockSeparator = ':';
constexpr char kDot = '.';
constexpr uint32_t kWhole = 0xffffffffu;

using UnknownFn = uint32_t(__thiscall *)(void *);
using RecordFn = uint8_t(__thiscall *)(void *, void *);
using OutputFn = void(__thiscall *)(void *, void *);
using ByteFn = uint8_t(__thiscall *)(void *);
using WordFn = uint32_t(__thiscall *)(void *);
using SetWordFn = void(__thiscall *)(void *, uint32_t);
using EnabledFn = uint8_t(__thiscall *)(void *, const char *, int32_t);
using WriteFn = void(__thiscall *)(void *, const char *, uint32_t, const char *, int32_t, const char *, va_list);

cLogService *ServiceOf(void *sub)
{
    return reinterpret_cast<cLogService *>(static_cast<char *>(sub) - cLogService::kServiceSubobjectOffset);
}

void Release(void *object)
{
    VirtualCall<UnknownFn, kSlotRelease>(object);
}

ifaces::cIGZCriticalSection *Lock(cRZCriticalSection &lock)
{
    return As<ifaces::cIGZCriticalSection>(&lock);
}

// A cRZString built from a cIGZString (0x144f2a) and destroyed with the scope.
struct StringCopy : cRZString {
    explicit StringCopy(const cIGZString &text) { Construct(text); }
    ~StringCopy() { Destruct(); }
};

// Replaces every `from` in `text` with `to`.
void ReplaceAll(std::string &text, char from, char to)
{
    for (uint32_t i = 0; i < text.size(); ++i)
        if (text[i] == from) text[i] = to;
}

// The write path's output step for one output, inside the original's catch(...) (built /EHa, so it also takes
// structured exceptions; here __except, in a function with nothing to unwind).
void OfferRecord(void *output, cLogRecord *record, uint32_t time)
{
    __try {
        if (VirtualCall<RecordFn, kOutputSlotAccept>(output, record)) {
            const uint32_t last = VirtualCall<WordFn, kOutputSlotLastTime>(output);
            record->mSinceLast = last != 0 ? time - last : 0;
            VirtualCall<SetWordFn, kOutputSlotSetLastTime>(output, time);
            VirtualCall<OutputFn, kOutputSlotOutput>(output, record);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// The output thread's step for one output, also inside catch(...).
void DeliverRecord(void *output, void *record)
{
    __try {
        if (VirtualCall<RecordFn, kOutputSlotAccept>(output, record)) VirtualCall<OutputFn, kOutputSlotOutput>(output, record);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// The body of the write path once the nesting count allows it.
void WriteRecord(cLogService *service, void *sub, const char *file, uint32_t line, const char *group, int32_t level,
                 const char *format, va_list args, uint32_t threadId)
{
    const uint32_t number = ++service->mRecordNumber;
    char buffer[kTextBufferSize] = {};
    const char *text = args ? FormatText(buffer, kTextBufferSize, format, args) : format;
    service->mProcessInfo->Sample();
    const uint32_t cpuTime = service->mProcessInfo->CpuTime();
    const uint32_t frame = service->mFrame;
    const uint32_t time = service->mTimestamp->ElapsedMilliseconds();

    alignas(4) uint8_t storage[cLogRecord::kSize];
    cLogRecord *record = reinterpret_cast<cLogRecord *>(storage)->Construct();
    record->Set(static_cast<uint32_t>(level), reinterpret_cast<uintptr_t>(group), reinterpret_cast<uintptr_t>(text),
                threadId, number, time, cpuTime, 0, frame, reinterpret_cast<uintptr_t>(file), line);

    Lock(service->mLock90)->Lock();
    // The end is read again on every pass.
    for (RefHolder *it = service->mOutputs.data(); it != service->mOutputs.data() + service->mOutputs.size(); ++it)
        OfferRecord(it->mObject, record, time);
    Lock(service->mLock90)->Unlock();

    if (text != buffer && text != format) std::free(const_cast<char *>(text));
    if (threadId == service->mThreadId) VirtualCall<UnknownFn, kServiceSlotFlush>(sub);
    record->Destruct();
}

} // namespace

// ---- helpers ----------------------------------------------------------------------------------------------------

void cLogTimestamp::CopyFrom(const cLogTimestamp *other)
{
    mTime = other->mTime;
    mElapsedLow = other->mElapsedLow;
    mStopwatch.SetCount(static_cast<int32_t>(mElapsedLow));
}

uint32_t cLogTimestamp::ElapsedMilliseconds()
{
    uint64_t elapsed = 0;
    stopwatch_more::ElapsedAs64(&mStopwatch, nullptr, &elapsed);
    return static_cast<uint32_t>(elapsed);
}

void cLogProcessInfo::Sample()
{
    if (!mHasProcessTimes) return;
    FILETIME creation, exit, kernel, user;
    (*GameAddress<GetProcessTimesFn>(kIatGetProcessTimes))(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(mProcess)), &creation, &exit,
                                                         &kernel, &user);
    mKernelTimeLow = kernel.dwLowDateTime;
    mKernelTimeHigh = kernel.dwHighDateTime;
    mUserTimeLow = user.dwLowDateTime;
    mUserTimeHigh = user.dwHighDateTime;
}

uint32_t cLogProcessInfo::CpuTime() const
{
    const uint64_t kernel = static_cast<uint64_t>(mKernelTimeHigh) << 32 | mKernelTimeLow;
    const uint64_t user = static_cast<uint64_t>(mUserTimeHigh) << 32 | mUserTimeLow;
    return static_cast<uint32_t>((kernel + user) / static_cast<uint64_t>(mScaled));
}

void cLogRecord::Set(uint32_t level, uint32_t group, uint32_t message, uint32_t threadId, uint32_t number, uint32_t time,
                     uint32_t cpuTime, uint32_t field60, uint32_t frame, uint32_t file, uint32_t line)
{
    mFrame = frame;
    mTime = time;
    mCpuTime = cpuTime;
    mField60 = field60;
    mLevel = level;
    mNumber = number;
    mMessage = message;
    mGroup = group;
    mText2.mString.clear();
    mText.mString.clear();
    mFile = file;
    mLine = line;
    mThreadId = threadId;
}

char *__cdecl FormatText(char *buffer, uint32_t size, const char *format, va_list args)
{
    int32_t length = static_cast<int32_t>(size);
    int result = _vsnprintf(buffer, length, format, args);
    if (result >= length) {
        // Exactly `size` characters: printed again with room for the NUL.
        char *block = static_cast<char *>(std::malloc(length + 1));
        _vsnprintf(block, length + 1, format, args);
        return block;
    }
    char *text = buffer;
    char *block = nullptr;
    while (result < 0) {
        length *= 2;
        text = block;
        if (length >= kMaxTextSize) break; // ORIGINAL-BUG: returns the last, unterminated attempt
        std::free(block);
        block = static_cast<char *>(std::malloc(length));
        result = _vsnprintf(block, length, format, args);
        text = block;
    }
    return text;
}

void cLogService::ReturnRecord(void *record)
{
    Lock(mLockE0)->Lock();
    mRecords.push_back(record);
    Lock(mLockE0)->Unlock();
}

// ---- cIService slots 23 and 28 --------------------------------------------------------------------------------

void __fastcall ServiceTimestampedName(void *sub, void *, cIGZString *name)
{
    cLogService *service = ServiceOf(sub);
    alignas(8) uint8_t stampStorage[cLogTimestamp::kSize];
    cLogTimestamp *stamp = reinterpret_cast<cLogTimestamp *>(stampStorage)->Construct();
    stamp->CopyFrom(service->mTimestamp);
    LogString date;
    LogString clock;
    stamp->Update(static_cast<int32_t>(stamp->mElapsedLow));
    date.mString.append(stamp->mDate);
    stamp->Update(static_cast<int32_t>(stamp->mElapsedLow));
    clock.mString.append(stamp->mClock);
    ReplaceAll(date.mString, kDateSeparator, kDot);
    ReplaceAll(clock.mString, kClockSeparator, kDot);

    StringCopy stem(*name);
    LogString extension;
    const uint32_t dot = static_cast<uint32_t>(stem.mString.rfind(kDot, kWhole));
    if (dot != kWhole) {
        alignas(4) uint8_t pieceStorage[sizeof(cRZString)];
        cRZString *piece = reinterpret_cast<cRZString *>(pieceStorage);
        if (dot < stem.Strlen()) {
            // From the dot on, counted with the argument's own length.
            Sub(stem).Right(piece, name->Strlen() - dot);
            extension.mString = piece->mString;
            piece->Destruct();
        }
        if (dot != 0) {
            // ORIGINAL-BUG: the stem stops one character short of the dot.
            Sub(stem).Left(piece, dot - 1);
            stem.mString = piece->mString;
            piece->Destruct();
        }
    }
    stem.mString.append(" ", 1);
    stem.mString.append(date.mString);
    stem.mString.append(" ", 1);
    stem.mString.append(clock.mString);
    stem.mString.append(".", 1);
    stem.mString.append(extension.mString);
    *name = *stem.AsInterface();
    // The stopwatch's destructor does nothing.
}

void __fastcall ServiceWrite(void *sub, void *, const char *file, uint32_t line, const char *group, int32_t level, const char *format,
                             va_list args)
{
    cLogService *service = ServiceOf(sub);
    if (!As<ifaces::cIGZSystemService>(service)->IsServiceRunning()) return;
    if (service->mCollectGroups) {
        Lock(service->mLock28)->Lock();
        {
            LogString key(group);
            service->mNames.insert(key);
        }
        Lock(service->mLock28)->Unlock();
    }
    if (!VirtualCall<EnabledFn, kServiceSlotEnabled>(sub, group, level)) return;
    uint32_t threadId = 0;
    StoreCurrentThreadId(&threadId);
    if (threadId == service->mOutputThreadId) return;

    Lock(service->mLockCC)->Lock();
    uint32_t &depth = service->mNesting[threadId];
    Lock(service->mLockCC)->Unlock();
    if (depth++ < *GameAddress<uint32_t>(cLogService::kMaxNestingRva))
        WriteRecord(service, sub, file, line, group, level, format, args, threadId);
    --depth;
}

// ---- the output thread ------------------------------------------------------------------------------------------

void cLogService::OutputThread()
{
    StoreCurrentThreadId(&mOutputThreadId);
    bool more = false;
    while (mOutputRunning || more) {
        if (As<ifaces::cIGZThreadSignal>(&mSignal)->Wait(kWhole) != 1) continue;
        Lock(mLockA0)->Lock();
        // As many records as were queued when the pass started, each from the front.
        for (int32_t left = static_cast<int32_t>(mQueueCount); left > 0; --left) {
            void *record = mQueue[0];
            if (record) VirtualCall<UnknownFn, kSlotAddRef>(record);
            for (int32_t i = 1; i < static_cast<int32_t>(mQueueCount); ++i) mQueue[i - 1] = mQueue[i];
            mQueue.pop_back();
            --mQueueCount;
            Lock(mLock90)->Lock();
            for (RefHolder *it = mOutputs.data(); it != mOutputs.data() + mOutputs.size(); ++it)
                if (!VirtualCall<ByteFn, kOutputSlotSynchronous>(it->mObject)) DeliverRecord(it->mObject, record);
            Lock(mLock90)->Unlock();
            ReturnRecord(record);
            if (record) nGZLog::Release(record);
        }
        more = mQueueCount != 0;
        if (!more) As<ifaces::cIGZThreadSignal>(&mSignal)->Reset();
        Lock(mLockA0)->Unlock();
    }
}

uint32_t __stdcall cLogService::ThreadEntry(void *service)
{
    static_cast<cLogService *>(service)->OutputThread();
    return 0;
}

// ---- destruction ------------------------------------------------------------------------------------------------

// The members (the cleanup ones are in nGZLog.h) and the base go after the body, back to front.
cLogService::~cLogService()
{
    mFormatter.Assign(nullptr);
    for (void *record : mQueue) nGZLog::Release(record);
    for (void *record : mRecords) nGZLog::Release(record);
    if (mAliases) DeleteAliasMap(mAliases, nullptr, 1);
    ::operator delete(mProcessInfo, cLogProcessInfo::kSize);
    if (mTimestamp) ::operator delete(mTimestamp, cLogTimestamp::kSize); // the stopwatch's destructor does nothing
}

cLogService::OwnedThread::~OwnedThread() { appdebugger::DestroyThread(this, nullptr); }
cLogService::HeldObject::~HeldObject()
{
    if (mObject) nGZLog::Release(mObject);
}
cLogService::HeldWord::~HeldWord()
{
    if (mValue) nGZLog::Release(reinterpret_cast<void *>(static_cast<uintptr_t>(mValue)));
}

namespace thunks {
cLogService *__fastcall ServiceDeletingFromUnknown(void *self, void *, uint32_t flags)
{
    return cLogServiceAdapters::DeletingDestructor(reinterpret_cast<cLogService *>(static_cast<char *>(self) - 4), nullptr, flags);
}
} // namespace thunks

// ---- the std adapter --------------------------------------------------------------------------------------------

void cLogStdAdapter::Destruct()
{
    mName.Destruct();
    reinterpret_cast<cRZTextOStream *>(this)->Destruct();
}

cLogStdAdapter *cLogStdAdapter::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

uint8_t cLogStdAdapter::WriteText(const char *text)
{
    appdebugger::LogServiceHolder holder;
    holder.Acquire();
    uint8_t written = 0;
    if (holder.mService) {
        const uint32_t level = mTarget == 0 ? kLevelForTarget0 : kLevelForOtherTargets;
        VirtualCall<WriteFn, kServiceSlotWrite>(holder.mService, nullptr, 0u, mName.ToChar(), static_cast<int32_t>(level), text, nullptr);
        written = 1;
    }
    holder.Release();
    return written;
}

} // namespace nGZLog
