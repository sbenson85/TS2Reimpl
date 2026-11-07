#include "app/AppDebugger.h"
#include "rz/nGZLog.h"

#include "app/AppBase.h"
#include "app/AppInit.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/RZFileStreams.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZFile.h"

#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

// The thread-safe-statics helpers of our CRT, used on the game's guard variable (as in rz/cGZAllocatorService.cpp).
extern "C" __declspec(thread) int _Init_thread_epoch;

namespace appdebugger {

namespace {

// The cached allocator service of 0x1e7190, 0x152b80 and 0x197100: from GetAllocatorService on first use.
void *CachedAllocatorService()
{
    int *guard = &ts2_rva_10c95d8;
    void **service = &ts2_rva_10c95d4;
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            try {
                *service = GetAllocatorService();
            } catch (...) {
                _Init_thread_abort(guard);
                throw;
            }
            _Init_thread_footer(guard);
        }
    }
    return *service;
}

// A cRZString built the way the inlined constructors in 0xca9bf0 do it: the whole 16-byte buffer is zeroed
// (cRZString::Construct leaves the bytes after the terminator alone).
void ConstructZeroedString(cRZString *s)
{
    std::memset(static_cast<void *>(s), 0, sizeof(cRZString));
    s->Construct();
}

// A node of the std::list<std::string> the application's paths come in (0x20 bytes).
struct StringListNode {
    StringListNode *mNext;
    StringListNode *mPrev;
    std::string mText; // +0x08
};
static_assert(sizeof(StringListNode) == 0x20, "StringListNode layout");

// The std::string (MSVC 14.40, x86) as raw words: the buffer (inline up to 15 chars, else a pointer in its
// first word), the size and the capacity.
struct RawString {
    union {
        char mBuffer[16];
        char *mPointer;
    };
    uint32_t mSize;
    uint32_t mCapacity;
};
static_assert(sizeof(RawString) == 0x18, "RawString layout");

const char *CStr(const cRZString &s)
{
    return s.mString.c_str();
}

using Slot0 = uint32_t(__thiscall *)(void *);
using Slot1 = uint32_t(__thiscall *)(void *, uint32_t);

} // namespace

// ---- 0x1bf8fb ----

LogServiceHolder *LogServiceHolder::Acquire()
{
    mService = nullptr;
    if (void *framework = GetServiceObject()) {
        using QueryFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<QueryFn, 5>(framework, kServiceId, kInterfaceId, &mService);
    }
    return this;
}

void LogServiceHolder::Release()
{
    reinterpret_cast<AllocatorServiceHolder *>(this)->Release();
}

// ---- the stream bases and adapters ----

OStreamBase *OStreamBase::ConstructOStream()
{
    mRefCount = 0;
    mVTable = GameAddress(kOStreamVTable);
    mNativeOrder = 1;
    mField0c = 0;
    return this;
}

OStreamBase *OStreamBase::ConstructTextOStream()
{
    ConstructOStream();
    mVTable = &ts2_rva_da2b4c;
    return this;
}

cRZStdAdapter *cRZStdAdapter::Construct(int32_t target, const char *name)
{
    mBase.ConstructTextOStream();
    mBase.mVTable = GameAddress(kVTable);
    mTarget = target;
    mName.Construct(name);
    return this;
}

void __cdecl StoreCurrentThreadId(uint32_t *out)
{
    *out = GetCurrentThreadId();
}

// ---- cTSWinCheatConsole ----

cTSWinCheatConsole *cTSWinCheatConsole::Construct()
{
    // The constructor starts from interim vtables (+0 0xeb5b10, +4 0xdd902c, +8 0xda49bc) that are replaced
    // below before anything can see them.
    mStream.ConstructTextOStream();
    mSystem.Construct(kServiceId, 0);
    mStream.mVTable = GameAddress(kStreamVTable);
    mSystem.mVTable = GameAddress(kSystemVTable);
    mVTable = GameAddress(kVTable);
    mVTable4 = GameAddress(kVTable4);
    mVTable8 = GameAddress(kVTable8);
    mSystem.mBaseVTable = GameAddress(kUnknownVTable);
    appbase::ConstructIdSet(&mIds, nullptr);
    mField40 = 0;
    mField44 = 10;
    for (uint32_t &f : mFields48) f = 0;
    mByte58 = 1;
    for (uint32_t &f : mFields5c) f = 0;
    mWord80 = 0;
    mField84 = 100;
    ConstructZeroedString(&mStringA);
    mLock.Construct();
    for (uint32_t &f : mFieldsB4) f = 0;
    ConstructZeroedString(&mStringB);
    // The original now walks a vector at +0x70..+0x74 (calling slot 33 of each 0x20-byte element with 0,
    // then emptying it): both pointers were zeroed above, so the loop never runs.
    appbase::InsertId(&mIds, nullptr, kInitialId);
    StoreCurrentThreadId(&mOwnerThread);
    return this;
}

// ---- the allocator service helpers ----

void *__stdcall AllocateListNodes(uint32_t count, uint32_t /*unused*/)
{
    void *service = CachedAllocatorService();
    using AllocFn = void *(__thiscall *)(void *, uint32_t);
    return VirtualCall<AllocFn, 3>(service, count << 5);
}

void __cdecl FreeStringBuffer(void * /*unusedA*/, void *mem, uint32_t /*unusedC*/)
{
    void *service = CachedAllocatorService();
    using FreeFn = void(__thiscall *)(void *, void *);
    VirtualCall<FreeFn, 4>(service, mem);
}

void __cdecl DestroyStringListNodes(ListHeader * /*list*/, void *sentinel)
{
    auto *head = static_cast<StringListNode *>(sentinel);
    head->mPrev->mNext = nullptr;
    StringListNode *node = head->mNext;
    while (node) {
        auto *text = reinterpret_cast<RawString *>(&node->mText);
        StringListNode *next = node->mNext;
        if (text->mCapacity > 0xf) FreeStringBuffer(text, text->mPointer, text->mCapacity);
        text->mSize = 0;
        text->mCapacity = 0xf;
        text->mBuffer[0] = 0;
        FreeThroughAllocatorService(node, 1);
        node = next;
    }
}

// ---- the log-settings cheat files ----

void __fastcall SetCheatServiceA(LogCheatGlobal *self, void *, void *service)
{
    self->mImpl->mServiceA = service;
}

void __fastcall SetCheatServiceB(LogCheatGlobal *self, void *, void *service)
{
    self->mImpl->mServiceB = service;
}

void __fastcall SetCheatName(LogCheatGlobal *self, void *, const char *name)
{
    self->mImpl->mName.assign(name, std::strlen(name));
}

void __fastcall LoadCheatServices(LogCheatGlobal *self, void *)
{
    using QueryFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
    void *other = nullptr;
    if (void *framework = GetServiceObject()) VirtualCall<QueryFn, 5>(framework, 0x0b6b619a, 0xeb6b6192, &other);
    LogServiceHolder logService;
    logService.mService = nullptr;
    if (void *framework = GetServiceObject()) VirtualCall<QueryFn, 5>(framework, LogServiceHolder::kServiceId, LogServiceHolder::kInterfaceId, &logService.mService);
    SetCheatServiceA(self, nullptr, logService.mService);
    SetCheatServiceB(self, nullptr, other);
    logService.Release();
    reinterpret_cast<RZAutoRef *>(&other)->ReleaseObject();
}

void __cdecl PrepareCheatGlobal()
{
    auto *global = GameAddress<LogCheatGlobal>(LogCheatGlobal::kRva);
    cRZString option;
    option.Construct();
    appinit::CommonImplSlot4(0x4d4896ba, reinterpret_cast<uint32_t>(&option), 1, 0, 0xffffffffu);
    SetCheatName(global, nullptr, CStr(option));
    LoadCheatServices(global, nullptr);
    option.Destruct();
}

void __fastcall OpenCheatFile(LogCheatGlobal *self, void *, const char *name)
{
    // The object is a stream with a virtual base: its pointer at +8 leads to a table whose dword at +8 is
    // the distance (from the object + 8) to the base subobject, whose slot 12 is called.
    auto *object = static_cast<uint8_t *>(self->mObject);
    auto *table = *reinterpret_cast<uint8_t **>(object + 8);
    uint8_t *base = object + 8 + *reinterpret_cast<int32_t *>(table + 8);
    VirtualCall<uint32_t(__thiscall *)(void *, const char *, uint32_t), 12>(base, name, 0);
}

void __cdecl LoadCheatFile(const cRZString *path)
{
    if (cRZFile::PathExists(*path->AsInterface())) {
        PrepareCheatGlobal();
        OpenCheatFile(GameAddress<LogCheatGlobal>(LogCheatGlobal::kRva), nullptr, CStr(*path));
    }
}

void __cdecl LoadLogSettingsCheats()
{
    ListHeader list{nullptr, 0};
    auto *sentinel = static_cast<StringListNode *>(AllocateListNodes(1, 0));
    sentinel->mNext = sentinel;
    sentinel->mPrev = sentinel;
    list.mHead = sentinel;
    appinit::CommonImplSlot5(0xcc9f7591, reinterpret_cast<uint32_t>(&list), 1, 0, 0xffffffffu);

    // Each file name is tried in every directory in the list; the first directory that has it loads it
    // and ends that pass.
    struct Pass {
        const char *mFile;
        size_t mLength;
    };
    const Pass passes[2] = {{"LogSettings.cheat", 0x11}, {"UserLogSettings.cheat", 0x15}};
    for (const Pass &pass : passes) {
        for (auto *node = static_cast<StringListNode *>(list.mHead)->mNext; node != list.mHead; node = node->mNext) {
            cRZString path;
            path.Construct(node->mText.c_str());
            path.mString.append(pass.mFile, pass.mLength);
            bool found = cRZFile::PathExists(*path.AsInterface());
            if (found) LoadCheatFile(&path);
            path.Destruct();
            if (found) break;
        }
    }

    DestroyStringListNodes(&list, list.mHead);
    FreeThroughAllocatorService(list.mHead, 1);
}

// ---- 0xc29d5a ----

void __cdecl SetupLogging()
{
    constexpr int kFrameworkSlotCommandLine = 21, kFrameworkSlotSetDebugLevel = 29, kFrameworkSlotAddService = 3, kFrameworkSlotAddStream = 35;
    // Command line slots: 8 reads a switch's value, 9 tests a switch.
    using SwitchValueFn = uint8_t(__thiscall *)(void *, const cIGZString *, cIGZString *, uint32_t);
    using SwitchFn = uint8_t(__thiscall *)(void *, const cIGZString *);
    // Log service slots (the arguments are named by the strings the setup passes).
    using LogFn20 = uint32_t(__thiscall *)(void *, const char *, const char *, uint32_t);
    using LogFn7 = uint32_t(__thiscall *)(void *, const char *, uint32_t, const char *);
    using LogFn11 = uint32_t(__thiscall *)(void *, const char *, void *);
    using LogFn15 = uint32_t(__thiscall *)(void *, const char *, uint32_t);
    using LogFn21 = uint32_t(__thiscall *)(void *, const char *, void *);
    using LogFn4 = int32_t(__thiscall *)(void *, const char *);
    using LogFn8 = uint32_t(__thiscall *)(void *, int32_t);
    using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, void **);

    void *framework = GetServiceObject();
    void *commandLine = VirtualCall<void *(__thiscall *)(void *), kFrameworkSlotCommandLine>(framework);

    // The debug level switch: "-gzdebuglevel:N" with N 0, 1 or 2 is passed on to the framework.
    cRZString levelText;
    levelText.Construct();
    {
        cRZString name;
        name.Construct("gzdebuglevel");
        uint8_t present = VirtualCall<SwitchValueFn, 8>(commandLine, name.AsInterface(), levelText.AsInterface(), 1);
        name.Destruct();
        if (present) {
            int32_t level = 2;
            if (std::sscanf(CStr(levelText), "%d", &level) == 1 && (level == 0 || level == 1 || level == 2))
                VirtualCall<Slot1, kFrameworkSlotSetDebugLevel>(framework, static_cast<uint32_t>(level));
        }
    }

    LogServiceHolder logService;
    logService.Acquire();
    void *log = logService.mService;

    // The two standard-handle adapters go to the framework (slot 35: index, stream), each AddRef'd first.
    auto *stdOut = static_cast<cRZStdAdapter *>(::operator new(cRZStdAdapter::kSize));
    stdOut->Construct(0, "StdOut");
    VirtualCall<Slot0, 1>(stdOut);
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, void *), kFrameworkSlotAddStream>(framework, 0, stdOut);
    auto *stdErr = static_cast<cRZStdAdapter *>(::operator new(cRZStdAdapter::kSize));
    stdErr->Construct(1, "StdErr");
    VirtualCall<Slot0, 1>(stdErr);
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, void *), kFrameworkSlotAddStream>(framework, 1, stdErr);

    // The AppDebugger group.
    VirtualCall<LogFn20, 20>(log, "AppDebugger", "NoGroup", 5);
    VirtualCall<LogFn7, 7>(log, "AppDebugger", 7, "OldTimestampOff");

    // The caption back end "AppTitleBar" (not AddRef'd here: the log service keeps it).
    auto *caption = static_cast<nGZLog::cLogCaptionOutputBackEnd *>(::operator new(nGZLog::cLogCaptionOutputBackEnd::kSize));
    caption->Construct("AppTitleBar");
    VirtualCall<LogFn11, 11>(log, "AppTitleBar", caption);
    VirtualCall<LogFn15, 15>(log, "AppTitleBar", 0);

    // The cheat console service.
    auto *console = static_cast<cTSWinCheatConsole *>(::operator new(cTSWinCheatConsole::kSize));
    console->Construct();
    VirtualCall<Slot0, 1>(console);
    VirtualCall<Slot1, 3>(console, 0);
    void *consoleFace = nullptr;
    if (VirtualCall<QueryFn, 0>(console, cTSWinCheatConsole::kQueryIdA, &consoleFace)) {
        VirtualCall<LogFn21, 21>(log, "AppConsole", consoleFace);
        VirtualCall<LogFn20, 20>(log, "AppConsole", "NoGroup", 4);
        VirtualCall<LogFn7, 7>(log, "AppConsole", 7, "OldTimestampOff");
        VirtualCall<LogFn7, 7>(log, "AppConsole", 2, "NoPreset");
        VirtualCall<LogFn7, 7>(log, "AppConsole", 3, "NoPreset");
    }
    void *consoleService = nullptr;
    if (VirtualCall<QueryFn, 0>(console, cTSWinCheatConsole::kQueryIdB, &consoleService))
        VirtualCall<Slot1, kFrameworkSlotAddService>(framework, reinterpret_cast<uint32_t>(consoleService));

    LoadLogSettingsCheats();

    // "-logServiceLevel:name" selects a level by name (-1: unknown, ignored).
    cRZString levelName;
    levelName.Construct();
    {
        cRZString name;
        name.Construct("logServiceLevel");
        uint8_t present = VirtualCall<SwitchValueFn, 8>(commandLine, name.AsInterface(), levelName.AsInterface(), 1);
        name.Destruct();
        if (present) {
            int32_t level = VirtualCall<LogFn4, 4>(log, CStr(levelName));
            if (level != -1) VirtualCall<LogFn8, 8>(log, level);
        }
    }
    {
        cRZString name;
        name.Construct("loadLog");
        uint8_t present = VirtualCall<SwitchFn, 9>(commandLine, name.AsInterface());
        name.Destruct();
        if (present) VirtualCall<LogFn15, 15>(log, "AppLoadLog", 1);
    }
    {
        cRZString name;
        name.Construct("sessionLog");
        uint8_t present = VirtualCall<SwitchFn, 9>(commandLine, name.AsInterface());
        name.Destruct();
        if (present) {
            VirtualCall<LogFn15, 15>(log, "AppSessionLoadLog", 1);
            VirtualCall<LogFn15, 15>(log, "AppSessionSaveLog", 1);
            VirtualCall<LogFn15, 15>(log, "AppSessionAllLog", 1);
        }
    }

    levelName.Destruct();
    if (consoleFace) VirtualCall<Slot0, 2>(consoleFace);
    VirtualCall<Slot0, 2>(console);
    VirtualCall<Slot0, 2>(stdErr);
    VirtualCall<Slot0, 2>(stdOut);
    logService.Release();
    levelText.Destruct();
    if (consoleService) VirtualCall<Slot0, 2>(consoleService);
}

} // namespace appdebugger
