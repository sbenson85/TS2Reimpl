// Differential test for the log setup of AddApplicationServicesHere, 0xc29d5a (src/app/AppDebugger.*).
//
// Everything the function talks to is a fake with a recording vtable (tests/app_debugger_fakes.h): the
// framework (game global 0x10c9378), its command line, the log service, the cTSAppCommonImpl (cache
// 0x10cc040, for the cheat files), the allocator service (cache 0x10c95d4) and the cheat global's stream.
// The objects the function creates itself (the two std adapters, the caption back end, the cheat console)
// carry the game's vtables, so those vtables' called slots (QueryInterface, AddRef, Release and slot 3 of the
// console; AddRef and Release of the adapters) are patched in the mapped image to the same recording fakes.
// The original runs first, then ours, on the same scripted answers; the calls, the objects the function
// leaves behind and the cheat global's impl must agree.
#include "app/AppDebugger.h"
#include "rz/nGZLog.h"
#include "app_debugger_fakes.h"
#include "difftest.h"
#include "game/game.h"

#define NOMINMAX
#include <windows.h>

#include <climits>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace appdebugger;
using adt::kAdapter;
using adt::kCommandLine;
using adt::kCommonImpl;
using adt::kConsole;
using adt::kConsoleFace;
using adt::kConsoleService;
using adt::kFramework;
using adt::kLogService;
using adt::kStreamBase;

constexpr uint32_t kFrameworkGlobal = 0x10c9378, kCommonCache = 0x10cc040, kIatGetCurrentThreadId = 0xd9a194;
constexpr uint32_t kConsoleVTable = 0xeb5b44, kAdapterVTable = 0xeb47d8, kCaptionVTable = 0xeb4508;

std::string Utf8(const std::wstring &w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool WriteFileText(const std::wstring &path, const std::string &text)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(h);
    return ok;
}

uint32_t gRealThreadId = 0;
uint32_t WINAPI FakeGetCurrentThreadId()
{
    return gRealThreadId;
}

struct Node {
    Node *next;
    Node *prev;
    std::string text;
};

// ---- the script ---------------------------------------------------------------------------------------------------

struct Script {
    std::map<std::string, std::string> switches; // command line switches present, with their values
    int32_t levelResult = -1;                    // the log service's slot 4 answer
    bool faceAnswer = false;                     // the console answers QueryInterface 0xc51b11cb
    bool faceReturnsNull = false;                // ... returning true but a null object (the release is guarded)
    bool serviceAnswer = false;                  // ... and 0x287fb697
    std::vector<std::string> directories;        // the application's paths
    std::string optionText;                      // the log settings option
};
Script gScript;
adt::AllocatorFake *gAllocator = nullptr;
void *gCommandLine = nullptr, *gFace = nullptr, *gService = nullptr, *gLogService = nullptr;

void DefineFakes()
{
    using adt::Def;
    Def(kFramework, 3, 1, "p");
    Def(kFramework, 5, 3, "iio", [](void *, const uint32_t *args) {
        void *answer = nullptr;
        if (args[0] == 0x09b69c4e && args[1] == 0x09b69c46) answer = gLogService;
        *reinterpret_cast<void **>(args[2]) = answer;
        return static_cast<uint32_t>(answer != nullptr);
    });
    Def(kFramework, 21, 0, "", [](void *, const uint32_t *) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gCommandLine)); });
    Def(kFramework, 29, 1, "i");
    Def(kFramework, 35, 2, "ip");

    Def(kCommandLine, 8, 3, "SSi", [](void *, const uint32_t *args) {
        auto it = gScript.switches.find(reinterpret_cast<const cRZString *>(args[0])->mString);
        if (it == gScript.switches.end()) return 0u;
        reinterpret_cast<cRZString *>(args[1])->mString = it->second;
        return 1u;
    });
    Def(kCommandLine, 9, 1, "S", [](void *, const uint32_t *args) {
        return static_cast<uint32_t>(gScript.switches.count(reinterpret_cast<const cRZString *>(args[0])->mString) != 0);
    });

    Def(kLogService, 2, 0, "");
    Def(kLogService, 4, 1, "s", [](void *, const uint32_t *) { return static_cast<uint32_t>(gScript.levelResult); });
    Def(kLogService, 7, 3, "sis");
    Def(kLogService, 8, 1, "i");
    Def(kLogService, 11, 2, "sp");
    Def(kLogService, 15, 2, "si");
    Def(kLogService, 20, 3, "ssi");
    Def(kLogService, 21, 2, "sp");

    Def(kConsoleFace, 2, 0, "");
    Def(kConsoleService, 2, 0, "");

    Def(kConsole, 0, 2, "io", [](void *, const uint32_t *args) {
        void *answer = nullptr;
        bool result = false;
        if (args[0] == 0xc51b11cb) {
            result = gScript.faceAnswer;
            answer = gScript.faceReturnsNull ? nullptr : gFace;
        } else if (args[0] == 0x287fb697) {
            result = gScript.serviceAnswer;
            answer = gService;
        }
        *reinterpret_cast<void **>(args[1]) = answer;
        return static_cast<uint32_t>(result);
    });
    Def(kConsole, 1, 0, "");
    Def(kConsole, 2, 0, "");
    Def(kConsole, 3, 1, "i");
    Def(kAdapter, 1, 0, "");
    Def(kAdapter, 2, 0, "");

    // The cTSAppCommonImpl: slot 4 gives the option text, slot 5 the directories.
    Def(kCommonImpl, 4, 5, "iSiii", [](void *, const uint32_t *args) {
        if (args[0] == 0x4d4896ba) reinterpret_cast<cRZString *>(args[1])->mString = gScript.optionText;
        return 1u;
    });
    Def(kCommonImpl, 5, 5, "ioiii", [](void *, const uint32_t *args) {
        auto *header = reinterpret_cast<ListHeader *>(args[1]);
        auto *sentinel = static_cast<Node *>(header->mHead);
        for (const std::string &dir : gScript.directories) {
            auto *n = static_cast<Node *>(::operator new(sizeof(Node)));
            gAllocator->Track(n);
            new (&n->text) std::string(dir);
            n->next = sentinel;
            n->prev = sentinel->prev;
            sentinel->prev->next = n;
            sentinel->prev = n;
        }
        return 1u;
    });
    Def(kStreamBase, 12, 2, "si");
}

struct StreamFake {
    adt::Fake base{kStreamBase};
    alignas(8) uint8_t memory[0x30] = {};
    int32_t vbtable[4] = {0x11, 0x22, 0, 0x33};
    StreamFake()
    {
        vbtable[2] = static_cast<int32_t>(reinterpret_cast<uintptr_t>(base.get()) - (reinterpret_cast<uintptr_t>(memory) + 8));
        *reinterpret_cast<void **>(memory + 8) = vbtable;
    }
};

// What the function leaves behind in the objects it creates and registers (found by the vtable each one carries).
std::string DescribeObjects()
{
    std::string out;
    for (const void *p : adt::gSeen) {
        uint32_t vtable = *static_cast<const uint32_t *>(p);
        uint32_t base = static_cast<uint32_t>(GameBase());
        char buf[200];
        if (vtable == base + kAdapterVTable) {
            auto *a = static_cast<const cRZStdAdapter *>(p);
            std::snprintf(buf, sizeof(buf), "[adapter target=%d name=%s refs=%u] ", a->mTarget, a->mName.mString.c_str(), a->mBase.mRefCount);
        } else if (vtable == base + kCaptionVTable) {
            auto *c = static_cast<const nGZLog::cLogCaptionOutputBackEnd *>(p);
            std::snprintf(buf, sizeof(buf), "[caption text=%s flags=%u,%u interval=%d refs=%u] ", c->mBase.Name(), c->mBase.mEnabled, c->mBase.mFlag, c->mBase.mInterval, c->mBase.mRefCount);
        } else if (vtable == base + kConsoleVTable) {
            auto *c = static_cast<const cTSWinCheatConsole *>(p);
            std::snprintf(buf, sizeof(buf), "[console ids=%zu thread=%u id=%x] ", c->mIds.size(), c->mOwnerThread, c->mSystem.mServiceId);
        } else {
            continue;
        }
        out += buf;
    }
    return out;
}

std::string DescribeImpl(const LogCheatImpl *impl)
{
    char buf[120];
    std::snprintf(buf, sizeof(buf), "f0=%x a=%p b=%p cap=%zu size=%zu text=", impl->mField0, impl->mServiceA, impl->mServiceB, impl->mName.capacity(), impl->mName.size());
    return std::string(buf) + impl->mName.c_str();
}

LogCheatImpl *MakeImpl(uint32_t seed)
{
    auto *impl = new LogCheatImpl{};
    std::mt19937 rng(seed);
    impl->mField0 = rng();
    impl->mServiceA = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
    impl->mServiceB = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
    impl->mName = "old";
    return impl;
}

} // namespace

DIFF_TEST(AppDebugger_SetupLogging, 0xc29d5a)
{
    auto orig = ctx.Original<void(__cdecl *)()>(0xc29d5a);
    adt::InstallWideFunctions(ctx);
    DefineFakes();
    adt::AllocatorFake alloc(ctx);
    gAllocator = &alloc;
    gRealThreadId = GetCurrentThreadId();
    struct IatSwap {
        void **at;
        void *saved;
    } iat{ctx.image.At<void *>(kIatGetCurrentThreadId), nullptr};
    iat.saved = *iat.at;
    *iat.at = reinterpret_cast<void *>(&FakeGetCurrentThreadId);

    adt::Fake framework(kFramework), commandLine(kCommandLine), logService(kLogService), common(kCommonImpl), face(kConsoleFace), service(kConsoleService);
    StreamFake stream;
    gCommandLine = commandLine.get();
    gFace = face.get();
    gService = service.get();
    gLogService = logService.get();
    adt::PatchedVTable consoleVTable(ctx, kConsoleVTable, kConsole, {0, 1, 2, 3});
    adt::PatchedVTable adapterVTable(ctx, kAdapterVTable, kAdapter, {1, 2});
    adt::GlobalOverride<void *> frameworkGlobal(ctx, kFrameworkGlobal, framework.get());
    adt::GlobalOverride<void *> commonCache(ctx, kCommonCache, common.get());
    adt::GlobalOverride<void *> object(ctx, LogCheatGlobal::kRva, static_cast<void *>(stream.memory));
    adt::GlobalOverride<LogCheatImpl *> impl(ctx, LogCheatGlobal::kRva + 4);

    // Directories for the cheat files (sandbox), each maybe holding either file.
    std::wstring dirs[3];
    std::string dirText[3];
    for (int i = 0; i < 3; ++i) {
        dirs[i] = difftest::SandboxPath(L"setup_dir" + std::to_wstring(i));
        CreateDirectoryW(dirs[i].c_str(), nullptr);
        dirs[i] += L"\\";
        dirText[i] = Utf8(dirs[i]);
    }

    const std::vector<std::string> levelValues = {"", "0", "1", "2", "3", "-1", "abc", "2x", " 1", "+2", "0x1", "00002", "99999999999", "1.5"};
    const std::vector<std::string> levelNames = {"", "debug", "warning", "A fairly long level name that is not inline"};
    std::mt19937 rng(0xc29d5a);
    int calls[8] = {};
    for (int round = 0; round < 400; ++round) {
        gScript = Script();
        if (rng() % 3) gScript.switches["gzdebuglevel"] = levelValues[rng() % levelValues.size()];
        if (rng() % 2) gScript.switches["logServiceLevel"] = levelNames[rng() % levelNames.size()];
        if (rng() % 2) gScript.switches["loadLog"] = "";
        if (rng() % 2) gScript.switches["sessionLog"] = "";
        gScript.levelResult = rng() % 3 == 0 ? -1 : static_cast<int32_t>(rng() % 6);
        gScript.faceAnswer = rng() % 4 != 0;
        gScript.faceReturnsNull = rng() % 5 == 0;
        gScript.serviceAnswer = rng() % 4 != 0;
        gScript.optionText = rng() % 2 ? "option" : "an option text longer than the inline buffer holds";
        size_t count = rng() % 4;
        for (size_t i = 0; i < count; ++i) gScript.directories.push_back(dirText[rng() % 3]);
        for (int i = 0; i < 3; ++i) {
            for (const wchar_t *file : {L"LogSettings.cheat", L"UserLogSettings.cheat"}) {
                if (rng() % 3 == 0) WriteFileText(dirs[i] + file, "x");
                else DeleteFileW((dirs[i] + file).c_str());
            }
        }
        std::vector<std::string> logs[2];
        std::string state[2];
        for (int side = 0; side < 2; ++side) {
            LogCheatImpl *theImpl = MakeImpl(round);
            impl.Set(theImpl);
            adt::ResetSide();
            if (side == 0) orig();
            else SetupLogging();
            logs[side] = adt::gLog;
            state[side] = DescribeImpl(theImpl) + " | " + DescribeObjects();
            delete theImpl;
            alloc.Release();
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) ctx.Fail("round %d: calls differ: %s", round, adt::FirstDifference(logs[0], logs[1]).c_str());
        if (state[0] != state[1]) ctx.Fail("round %d: state differs:\n  original %s\n  ours     %s", round, state[0].c_str(), state[1].c_str());
        const std::string text = adt::Joined(logs[0]);
        calls[0] += text.find("fw.29(") != std::string::npos;
        calls[1] += text.find("log.8(") != std::string::npos;
        calls[2] += text.find("log.21(") != std::string::npos;
        calls[3] += text.find("fw.3(") != std::string::npos;
        calls[4] += text.find("stream.12(") != std::string::npos;
        calls[5] += text.find("AppSessionAllLog") != std::string::npos;
        calls[6] += text.find("AppLoadLog") != std::string::npos;
        calls[7] += text.find("face.2()") != std::string::npos;
    }
    for (int i = 0; i < 8; ++i)
        if (calls[i] < 20) ctx.Fail("path %d reached in only %d rounds", i, calls[i]);

    for (int i = 0; i < 3; ++i) {
        for (const wchar_t *file : {L"LogSettings.cheat", L"UserLogSettings.cheat"}) DeleteFileW((dirs[i] + file).c_str());
        RemoveDirectoryW(dirs[i].substr(0, dirs[i].size() - 1).c_str());
    }
    *iat.at = iat.saved;
    gAllocator = nullptr;
}
