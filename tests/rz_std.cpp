// Differential tests for cRZStd's slots and helpers (src/rz/cRZStd.cpp, lane LZ): the assertion (slot 1, 0x1bf9c4),
// Printf (slot 0, 0x1bfca9), Report (slot 2, 0x1bfb92), and LogLine (0x1c00a8), Output (0x1bff41), AskBox (0x1bfce6),
// AskWithFramework (0x1bfe0c), EnvironmentText (0x1b14b2), DebugText (0x1bfc9c).
//
// Every outside party is a logging fake: the framework (the game global 0x10c9378: its window face, services, debug
// level and StdOut / StdErr streams), the log service and its system-service face, the message server, the lock object
// at 0x10cb5cc, and the imports (environment, debugger output, module and path names, the message box, window checks).
// The debug level global (0x10cb5d4) is set per case and compared after. RZSTD_OUTPUT_FILE_PATH names a real file per
// side (the image's fopen / fwrite / fclose, _snprintf, _vsnprintf_l and atoi stand in with the CRT's); the files'
// contents are compared. Frees are compared as sorted block sizes (difftest::FreeRecorder); RZStd_Helpers also compares
// each call's allocated-minus-freed blocks and bytes (difftest::AllocationCounter, added by lane MH; the log is not counted).
//
// Not compared: above level 2 the original reads an unset stack byte for "report" (ORIGINAL-BUG); those cases always
// let the log service take the line, which clears it on both sides. Texts stay under the 0x3ff / 0x400 cuts (the
// original then writes unterminated text, or a length of -1).
#include "gzlog_support.h" // first: it defines NOMINMAX before windows.h
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/cRZStd.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#pragma warning(disable : 4996) // the CRT calls the fakes and stand-ins make, as the game makes them

namespace {

constexpr uint32_t kPrintf = 0x1bfca9, kAssert = 0x1bf9c4, kReport = 0x1bfb92;
constexpr uint32_t kLogLine = 0x1c00a8, kOutput = 0x1bff41, kAskBox = 0x1bfce6, kAskWithFramework = 0x1bfe0c;
constexpr uint32_t kEnvironmentText = 0x1b14b2, kDebugText = 0x1bfc9c;

constexpr uint32_t kFrameworkGlobal = 0x10c9378;
constexpr uint32_t kImageFopen = 0x83aa0f, kImageFwrite = 0x839fd2, kImageFclose = 0x838ff4;
constexpr uint32_t kImageSnprintf = 0x176ff0, kImageVsnprintfL = 0x14484b, kImageAtoi = 0x830972;

constexpr uint32_t kIatGetEnvironmentVariableA = 0xd9a164, kIatOutputDebugStringA = 0xd9a110;
constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c, kIatGetFullPathNameA = 0xd9a1ec, kIatGetCurrentProcessId = 0xd9a1bc;
constexpr uint32_t kIatMessageBoxA = 0xd9a568, kIatIsWindow = 0xd9a5c4, kIatGetWindowThreadProcessId = 0xd9a5c0;
constexpr uint32_t kIatGetCurrentThreadId = 0xd9a194;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[1400];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    difftest::NotCounted bookkeeping; // the log is the test's (AllocationCounter in RZStd_Helpers)
    if (gLog.capacity() < 4096) gLog.reserve(4096);
    gLog.push_back(buf);
}

std::string Joined()
{
    std::string s;
    for (const std::string &l : gLog) s += l + "; ";
    return s;
}

std::string Sizes(const difftest::FreeRecorder &r)
{
    size_t sizes[256];
    const size_t n = r.SortedSizes(sizes, 256);
    std::string s = r.Overflowed() ? "overflow " : "";
    for (size_t k = 0; k < n; ++k) s += std::to_string(sizes[k]) + " ";
    return s;
}

const char *Str(const char *s)
{
    return s ? s : "(null)";
}

// ---- The script --------------------------------------------------------------------------------------------------

struct Script {
    bool framework = true;
    bool windowFace = true;
    void *hwnd = nullptr;
    bool isWindow = true;
    bool sameThread = true;
    bool logService = true;
    bool sysFace = true;
    bool running = true;
    bool throwInWrite = false; // the log service's write throws
    bool messageServer = true;
    int32_t fwLevel = 2;
    bool stdOut = true, stdErr = true;
    bool streamTakes = false;
    bool throwInStream = false;
    std::map<std::string, std::string> env; // absent: GetEnvironmentVariableA returns 0
    bool moduleOk = true, fullOk = true;
    int boxAnswer = IDRETRY;
    int32_t assertAnswer = 0; // the fake slot 1 Printf calls
};
Script gS;
std::string gPath; // this side's RZSTD_OUTPUT_FILE_PATH (when the script has one)

struct Obj {
    void **vtable;
    int id;
};

// ---- Fakes: framework, its faces and services ------------------------------------------------------------------

Obj gWindowFace, gLogService, gSysFace, gServer, gStdOut, gStdErr;

uint32_t __fastcall FakeAddRef(Obj *self, void *)
{
    Log("addref %d", self->id);
    return 2;
}
uint32_t __fastcall FakeRelease(Obj *self, void *)
{
    Log("release %d", self->id);
    return 1;
}

bool __fastcall FwQuery(Obj *, void *, uint32_t riid, void **out)
{
    Log("fw.qi %08x", riid);
    if (riid == 0x23ea70a1 && gS.windowFace) {
        *out = &gWindowFace;
        return true;
    }
    return false;
}
bool __fastcall FwService(Obj *, void *, uint32_t id, uint32_t iid, void **out)
{
    Log("fw.service %08x %08x", id, iid);
    if (id == 0x09b69c4e && iid == 0x09b69c46 && gS.logService) {
        *out = &gLogService;
        return true;
    }
    if (id == 0x64063387 && iid == 0xc1085722 && gS.messageServer) {
        *out = &gServer;
        return true;
    }
    return false;
}
int32_t __fastcall FwDebugLevel(Obj *, void *)
{
    Log("fw.level");
    return gS.fwLevel;
}
void *__fastcall FwStdOut(Obj *, void *)
{
    Log("fw.stdout");
    return gS.stdOut ? &gStdOut : nullptr;
}
void *__fastcall FwStdErr(Obj *, void *)
{
    Log("fw.stderr");
    return gS.stdErr ? &gStdErr : nullptr;
}
void *gFwVTable[40];
Obj gFramework{gFwVTable, 1};

void *__fastcall WindowHandle(Obj *, void *)
{
    Log("window.hwnd");
    return gS.hwnd;
}
void *gWindowVTable[8];

bool __fastcall LogQuery(Obj *, void *, uint32_t riid, void **out)
{
    Log("log.qi %08x", riid);
    if (riid == 0x287fb697 && gS.sysFace) {
        *out = &gSysFace;
        return true;
    }
    return false;
}
void __fastcall LogWrite(Obj *, void *, const char *file, uint32_t line, const char *group, int32_t level, const char *format,
                         va_list args)
{
    char text[600];
    if (args)
        std::vsnprintf(text, sizeof(text), format, args);
    else
        std::snprintf(text, sizeof(text), "%s", Str(format));
    Log("log.write %s %u %s %d [%s] [%s]", Str(file), line, Str(group), level, Str(format), text);
    if (gS.throwInWrite) throw 3;
}
void *gLogVTable[30];

bool __fastcall SysRunning(Obj *, void *)
{
    Log("sys.running");
    return gS.running;
}
void *gSysVTable[8];

uint8_t __fastcall ServerSend(Obj *, void *, const uint32_t *message)
{
    Log("server.send %08x %u %u %u", message[0], message[1], message[2], message[3]);
    return 1;
}
void *gServerVTable[5];

uint8_t __fastcall StreamWrite(Obj *self, void *, const char *text)
{
    Log("stream%d.write [%s]", self->id, text);
    if (gS.throwInStream) throw 3;
    return gS.streamTakes ? 1 : 0;
}
void *gStreamVTable[16];

int32_t __fastcall LockSlot(void *self, void *)
{
    Log("lock %d", self == GameAddress<void>(cRZStd::kAskLockRva) ? 1 : 0);
    return 1;
}
int32_t __fastcall UnlockSlot(void *self, void *)
{
    Log("unlock %d", self == GameAddress<void>(cRZStd::kAskLockRva) ? 1 : 0);
    return 1;
}
void *gLockVTable[3];

int32_t __fastcall FakeAssertSlot(cRZStd *self, void *, const char *text)
{
    Log("slot1 %u [%s]", self->mSecond, text);
    return gS.assertAnswer;
}
void *gStdVTable[3];

void BuildVTables()
{
    gFwVTable[0] = reinterpret_cast<void *>(&FwQuery);
    gFwVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    gFwVTable[2] = reinterpret_cast<void *>(&FakeRelease);
    gFwVTable[5] = reinterpret_cast<void *>(&FwService);
    gFwVTable[30] = reinterpret_cast<void *>(&FwDebugLevel);
    gFwVTable[31] = reinterpret_cast<void *>(&FwStdOut);
    gFwVTable[32] = reinterpret_cast<void *>(&FwStdErr);
    gWindowVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    gWindowVTable[2] = reinterpret_cast<void *>(&FakeRelease);
    gWindowVTable[5] = reinterpret_cast<void *>(&WindowHandle);
    gLogVTable[0] = reinterpret_cast<void *>(&LogQuery);
    gLogVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    gLogVTable[2] = reinterpret_cast<void *>(&FakeRelease);
    gLogVTable[28] = reinterpret_cast<void *>(&LogWrite);
    gSysVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    gSysVTable[2] = reinterpret_cast<void *>(&FakeRelease);
    gSysVTable[6] = reinterpret_cast<void *>(&SysRunning);
    gServerVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    gServerVTable[2] = reinterpret_cast<void *>(&FakeRelease);
    gServerVTable[3] = reinterpret_cast<void *>(&ServerSend);
    gStreamVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    gStreamVTable[2] = reinterpret_cast<void *>(&FakeRelease);
    gStreamVTable[14] = reinterpret_cast<void *>(&StreamWrite);
    gLockVTable[1] = reinterpret_cast<void *>(&LockSlot);
    gLockVTable[2] = reinterpret_cast<void *>(&UnlockSlot);
    gStdVTable[1] = reinterpret_cast<void *>(&FakeAssertSlot);
    gWindowFace = {gWindowVTable, 10};
    gLogService = {gLogVTable, 11};
    gSysFace = {gSysVTable, 12};
    gServer = {gServerVTable, 13};
    gStdOut = {gStreamVTable, 31};
    gStdErr = {gStreamVTable, 32};
}

// ---- Fake imports ------------------------------------------------------------------------------------------------

DWORD WINAPI FakeGetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size)
{
    Log("env %s %u", name, size);
    auto it = gS.env.find(name);
    if (it == gS.env.end()) return 0;
    const std::string value = it->first == "RZSTD_OUTPUT_FILE_PATH" && it->second == "(per side)" ? gPath : it->second;
    if (value.size() + 1 > size) {
        // The buffer's contents are left undefined: fill it with digits, so code that read it anyway would see a
        // number rather than whatever was on its stack.
        if (size) {
            std::memset(buffer, '7', size - 1);
            buffer[size - 1] = 0;
        }
        return static_cast<DWORD>(value.size() + 1);
    }
    std::memcpy(buffer, value.c_str(), value.size() + 1);
    return static_cast<DWORD>(value.size());
}
void WINAPI FakeOutputDebugStringA(LPCSTR text)
{
    Log("ods [%s]", text);
}
DWORD WINAPI FakeGetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size)
{
    Log("module %p %u", static_cast<void *>(module), size);
    if (!gS.moduleOk) return 0;
    std::strcpy(buffer, "C:\\Games\\TS2\\TSBin\\Sims2EP9.exe");
    return static_cast<DWORD>(std::strlen(buffer));
}
DWORD WINAPI FakeGetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR *filePart)
{
    Log("fullpath %s %u", name, size);
    if (!gS.fullOk) return 0;
    std::strcpy(buffer, name);
    *filePart = std::strrchr(buffer, '\\') + 1;
    return static_cast<DWORD>(std::strlen(buffer));
}
DWORD WINAPI FakeGetCurrentProcessId()
{
    Log("pid");
    return 4321;
}
int WINAPI FakeMessageBoxA(HWND window, LPCSTR text, LPCSTR caption, UINT type)
{
    Log("box %p [%s] [%s] %x", static_cast<void *>(window), text, caption, type);
    return gS.boxAnswer;
}
BOOL WINAPI FakeIsWindow(HWND window)
{
    Log("iswindow %p", static_cast<void *>(window));
    return gS.isWindow;
}
DWORD WINAPI FakeGetWindowThreadProcessId(HWND window, LPDWORD process)
{
    Log("windowthread %p %p", static_cast<void *>(window), static_cast<void *>(process));
    return gS.sameThread ? GetCurrentThreadId() : GetCurrentThreadId() + 1;
}
DWORD WINAPI FakeGetCurrentThreadId()
{
    return GetCurrentThreadId();
}

// ---- The image's CRT, stood in by ours ---------------------------------------------------------------------------

FILE *__cdecl StandFopen(const char *path, const char *mode)
{
    return std::fopen(path, mode);
}
size_t __cdecl StandFwrite(const void *data, size_t size, size_t count, FILE *file)
{
    return std::fwrite(data, size, count, file);
}
int __cdecl StandFclose(FILE *file)
{
    return std::fclose(file);
}
int __cdecl StandSnprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = _vsnprintf(buffer, size, format, args);
    va_end(args);
    return n;
}
int __cdecl StandVsnprintfL(char *buffer, size_t size, const char *format, _locale_t locale, va_list args)
{
    return _vsnprintf_l(buffer, size, format, locale, args);
}
int __cdecl StandAtoi(const char *text)
{
    return std::atoi(text);
}

// Everything a test needs in place, restored at the end.
class Scene {
public:
    explicit Scene(difftest::Context &c)
        : ctx(c), slots(c), fopenPatch(kImageFopen, shape::CodeOf(&StandFopen)), fwritePatch(kImageFwrite, shape::CodeOf(&StandFwrite)),
          fclosePatch(kImageFclose, shape::CodeOf(&StandFclose)), snprintfPatch(kImageSnprintf, shape::CodeOf(&StandSnprintf)),
          vsnprintfPatch(kImageVsnprintfL, shape::CodeOf(&StandVsnprintfL)), atoiPatch(kImageAtoi, shape::CodeOf(&StandAtoi))
    {
        BuildVTables();
        slots.Set(kIatGetEnvironmentVariableA, reinterpret_cast<void *>(&FakeGetEnvironmentVariableA));
        slots.Set(kIatOutputDebugStringA, reinterpret_cast<void *>(&FakeOutputDebugStringA));
        slots.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeGetModuleFileNameA));
        slots.Set(kIatGetFullPathNameA, reinterpret_cast<void *>(&FakeGetFullPathNameA));
        slots.Set(kIatGetCurrentProcessId, reinterpret_cast<void *>(&FakeGetCurrentProcessId));
        slots.Set(kIatMessageBoxA, reinterpret_cast<void *>(&FakeMessageBoxA));
        slots.Set(kIatIsWindow, reinterpret_cast<void *>(&FakeIsWindow));
        slots.Set(kIatGetWindowThreadProcessId, reinterpret_cast<void *>(&FakeGetWindowThreadProcessId));
        slots.Set(kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeGetCurrentThreadId));
        framework = c.image.At<uint32_t>(kFrameworkGlobal);
        lock = c.image.At<uint32_t>(cRZStd::kAskLockRva);
        level = c.image.At<int32_t>(cRZStd::kDebugLevelRva);
        savedFramework = *framework;
        savedLock = *lock;
        savedLevel = *level;
        *lock = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gLockVTable));
        char temp[MAX_PATH];
        GetTempPathA(MAX_PATH, temp);
        // Per process: difftest shards run in parallel, and fixed names let two RZStd tests overwrite each other's file.
        const std::string stem = std::string(temp) + "ts2_rzstd_" + std::to_string(GetCurrentProcessId());
        paths[0] = stem + "_a.txt";
        paths[1] = stem + "_b.txt";
    }
    ~Scene()
    {
        *framework = savedFramework;
        *lock = savedLock;
        *level = savedLevel;
        std::remove(paths[0].c_str());
        std::remove(paths[1].c_str());
    }
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;

    // Before a side runs: the script's framework, the level, an empty log and file.
    void Begin(int side, int32_t startLevel)
    {
        *framework = gS.framework ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFramework)) : 0;
        *level = startLevel;
        gPath = paths[side];
        std::remove(gPath.c_str());
        gLog.clear();
    }
    // After a side: what it did.
    std::string End(int side)
    {
        std::string out = Joined();
        out += " level=" + std::to_string(*level);
        std::ifstream in(paths[side], std::ios::binary);
        if (in) {
            std::stringstream text;
            text << in.rdbuf();
            out += " file=[" + text.str() + "]";
        } else {
            out += " nofile";
        }
        return out;
    }

    difftest::Context &ctx;

private:
    gzlog_test::Slots slots;
    shape::CodePatch fopenPatch, fwritePatch, fclosePatch, snprintfPatch, vsnprintfPatch, atoiPatch;
    uint32_t *framework, *lock;
    int32_t *level;
    uint32_t savedFramework, savedLock;
    int32_t savedLevel;
    std::string paths[2];
};

// ---- Random scripts ----------------------------------------------------------------------------------------------

const char *const kFiles[] = {"C:\\src\\rz\\RZStd.cpp", "game.cpp", "", "a\\very\\long\\path\\name\\to\\some\\source\\file.cpp"};
const char *const kGroups[] = {"ASSERT", "AppDebugger", "Audio", ""};

std::string RandomText(std::mt19937 &rng)
{
    static const char kChars[] = "abcdefghij klmnop:%-.\\";
    std::string s;
    const size_t n = rng() % 4 == 0 ? 0 : rng() % 120;
    for (size_t i = 0; i < n; ++i) s += kChars[rng() % (sizeof(kChars) - 1)];
    // No '%' followed by a conversion the fakes would read: keep a lone '%' as "%%".
    std::string out;
    for (char c : s) out += c == '%' ? std::string("%%") : std::string(1, c);
    return out;
}

void RandomScript(std::mt19937 &rng)
{
    gS = Script();
    gS.framework = rng() % 3 != 0;
    gS.windowFace = rng() % 4 != 0;
    const uint32_t hwnds[] = {0, 0x1234, 0x20202};
    gS.hwnd = reinterpret_cast<void *>(static_cast<uintptr_t>(hwnds[rng() % 3]));
    gS.isWindow = rng() % 4 != 0;
    gS.sameThread = rng() % 4 != 0;
    gS.logService = rng() % 3 != 0;
    gS.sysFace = rng() % 4 != 0;
    gS.running = rng() % 3 != 0;
    gS.messageServer = rng() % 4 != 0;
    const int32_t levels[] = {0, 1, 2, 2, 3, 5, -1};
    gS.fwLevel = levels[rng() % 7];
    gS.stdOut = rng() % 4 != 0;
    gS.stdErr = rng() % 4 != 0;
    gS.streamTakes = rng() % 2 == 0;
    gS.moduleOk = rng() % 5 != 0;
    gS.fullOk = rng() % 5 != 0;
    const int answers[] = {IDABORT, IDRETRY, IDIGNORE, IDOK, IDCANCEL};
    gS.boxAnswer = answers[rng() % 5];
    gS.assertAnswer = static_cast<int32_t>(rng() % 3);
    const char *const envLevels[] = {nullptr, "0", "1", "2", "3", "-1", "", "12345678901234567", "7x", "0000000000000003", "000000000000003"};
    // The last two: 16 characters (one too many for the 16-byte buffer) and 15 (just fits).
    if (const char *v = envLevels[rng() % 11]) gS.env["GZFRAMEWORK_DEBUG_LEVEL"] = v;
    // A path one byte too long for the 512-byte buffer is refused.
    const int pathPick = static_cast<int>(rng() % 6);
    if (pathPick < 3) gS.env["RZSTD_OUTPUT_FILE_PATH"] = "(per side)";
    if (pathPick == 3) gS.env["RZSTD_OUTPUT_FILE_PATH"] = std::string(0x200, 'q');
    const int prefix = static_cast<int>(rng() % 4);
    if (prefix == 1) gS.env["RZSTD_OUTPUT_PREFIX"] = "";
    if (prefix == 2) gS.env["RZSTD_OUTPUT_PREFIX"] = "PFX: ";
    if (prefix == 3) gS.env["RZSTD_OUTPUT_PREFIX"] = std::string(200, 'p');
}

// The level an assertion runs at (as the original computes it), for the forced log-service rule.
int32_t EffectiveLevel(int32_t start)
{
    if (gS.framework) return gS.fwLevel;
    auto it = gS.env.find("GZFRAMEWORK_DEBUG_LEVEL");
    if (it == gS.env.end()) return start;
    const size_t n = it->second.size();
    if (n < 1 || n > 15) return start;
    const int v = std::atoi(it->second.c_str());
    return v == 2 ? start : v;
}

void ForceLogServiceTakes()
{
    gS.logService = true;
    gS.sysFace = true;
    gS.running = true;
}

void FillStd(cRZStd &s, std::mt19937 &rng, const char *file)
{
    s.mVTable = gStdVTable;
    s.mFirst = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file));
    s.mSecond = rng() % 5000;
    s.mFlags = rng() & 0xff;
}

} // namespace

// ---- Slot 1: the assertion ---------------------------------------------------------------------------------------

DIFF_TEST(RZStd_Assert, kAssert)
{
    Scene scene(ctx);
    auto original = ctx.Original<int32_t(__thiscall *)(cRZStd *, const char *)>(kAssert);
    std::mt19937 rng(kAssert);
    int asked = 0, shown = 0, thrown = 0, filed = 0;
    for (int i = 0; i < 4000; ++i) {
        RandomScript(rng);
        const int32_t start = static_cast<int32_t>(rng() % 5) - 1;
        const char *file = rng() % 4 == 0 ? nullptr : kFiles[rng() % 4];
        const std::string text = RandomText(rng);
        const bool nullText = rng() % 20 == 0;
        cRZStd a, b;
        FillStd(a, rng, file);
        b = a;
        const int32_t level = EffectiveLevel(start);
        if (level != 0 && level != 1 && level != 2) {
            // Above level 2 the original's "report" byte is unset (ORIGINAL-BUG): let the service take the line,
            // which clears it on both sides. Only when there is a framework for the holder to ask.
            if (!gS.framework) continue;
            ForceLogServiceTakes();
        }
        gS.throwInStream = rng() % 10 == 0;
        const char *argument = nullText ? nullptr : text.c_str();

        scene.Begin(0, start);
        difftest::FreeRecorder freesA;
        const int32_t ra = original(&a, argument);
        freesA.Stop();
        const std::string sa = scene.End(0);
        scene.Begin(1, start);
        difftest::FreeRecorder freesB;
        const int32_t rb = b.Assert(argument);
        freesB.Stop();
        const std::string sb = scene.End(1);
        ++ctx.cases;
        if (sa.find("box ") != std::string::npos) ++asked;
        if (sa.find(".write [") != std::string::npos || sa.find("ods [") != std::string::npos) ++shown;
        if (gS.throwInStream && sa.find("stream") != std::string::npos) ++thrown;
        if (sa.find("file=[") != std::string::npos) ++filed;
        if (ra != rb || a.mFlags != b.mFlags || sa != sb || Sizes(freesA) != Sizes(freesB))
            ctx.Fail("case %d (flags %02x, level %d): result %d / %d, flags %02x / %02x\n  %s\n  %s\n  frees %s / %s", i, a.mFlags,
                     level, ra, rb, a.mFlags, b.mFlags, sa.c_str(), sb.c_str(), Sizes(freesA).c_str(), Sizes(freesB).c_str());
    }
    if (asked < 200 || shown < 400 || thrown < 20 || filed < 100)
        ctx.Fail("coverage: %d asked, %d shown, %d thrown, %d to the file", asked, shown, thrown, filed);
}

// ---- Slots 0 and 2: Printf and Report ----------------------------------------------------------------------------

DIFF_TEST(RZStd_PrintfReport, kPrintf)
{
    // Report is 0x1bfb92; both take (int, text) after formats that use them in that order or not at all.
    Scene scene(ctx);
    auto printf = ctx.Original<int32_t(__cdecl *)(cRZStd *, const char *, ...)>(kPrintf);
    auto report = ctx.Original<int32_t(__cdecl *)(cRZStd *, const char *, int32_t, const char *, ...)>(kReport);
    const char *const kFormats[] = {nullptr, "plain line", "n=%d s=%s", "%d%s!", "%%d literal %d [%s]", ""};
    std::mt19937 rng(kPrintf);
    int taken = 0, shown = 0, thrown = 0;
    for (int i = 0; i < 3000; ++i) {
        RandomScript(rng);
        gS.throwInStream = rng() % 10 == 0;
        gS.throwInWrite = rng() % 12 == 0;
        const char *file = rng() % 4 == 0 ? nullptr : kFiles[rng() % 4];
        const char *format = kFormats[rng() % 6];
        const int number = static_cast<int>(rng() % 100000) - 500;
        const std::string word = RandomText(rng);
        const char *group = kGroups[rng() % 4];
        const int32_t levels[] = {-1, 2, 3, 5, 6, 9};
        const int32_t level = levels[rng() % 6];
        const int32_t start = static_cast<int32_t>(rng() % 3);
        cRZStd a, b;
        FillStd(a, rng, file);
        b = a;
        const bool viaPrintf = i % 2 == 0;

        scene.Begin(0, start);
        difftest::FreeRecorder freesA;
        const int32_t ra = viaPrintf ? printf(&a, format, number, word.c_str()) : report(&a, group, level, format, number, word.c_str());
        freesA.Stop();
        const std::string sa = scene.End(0);
        scene.Begin(1, start);
        difftest::FreeRecorder freesB;
        const int32_t rb = viaPrintf ? cRZStd::Printf(&b, format, number, word.c_str())
                                     : cRZStd::Report(&b, group, level, format, number, word.c_str());
        freesB.Stop();
        const std::string sb = scene.End(1);
        ++ctx.cases;
        if (sa.find("log.write") != std::string::npos) ++taken;
        if (sa.find("ods [") != std::string::npos || sa.find(".write [") != std::string::npos) ++shown;
        if ((gS.throwInStream || gS.throwInWrite) && (sa.find("stream") != std::string::npos || sa.find("log.write") != std::string::npos)) ++thrown;
        if (ra != rb || std::memcmp(&a, &b, sizeof(a)) != 0 || sa != sb || Sizes(freesA) != Sizes(freesB))
            ctx.Fail("case %d (%s, format %s): result %d / %d\n  %s\n  %s\n  frees %s / %s", i, viaPrintf ? "Printf" : "Report",
                     Str(format), ra, rb, sa.c_str(), sb.c_str(), Sizes(freesA).c_str(), Sizes(freesB).c_str());
    }
    if (taken < 200 || shown < 300 || thrown < 50) ctx.Fail("coverage: %d taken, %d shown, %d thrown", taken, shown, thrown);
}

// ---- The helpers, called directly --------------------------------------------------------------------------------

namespace {

using LogLineFn = void(__cdecl *)(const char *, int32_t, uint32_t, const char *, va_list, const char *, uint32_t, uint8_t *);

void CallLogLine(LogLineFn fn, const char *group, int32_t level, uint32_t mode, const char *file, uint32_t line, uint8_t *pending,
                 const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fn(group, level, mode, format, args, file, line, pending);
    va_end(args);
}

} // namespace

DIFF_TEST(RZStd_Helpers, kLogLine)
{
    // LogLine 0x1c00a8, Output 0x1bff41, AskBox 0x1bfce6, AskWithFramework 0x1bfe0c, EnvironmentText 0x1b14b2,
    // DebugText 0x1bfc9c.
    Scene scene(ctx);
    auto logLine = ctx.Original<LogLineFn>(kLogLine);
    auto output = ctx.Original<void(__cdecl *)(void *, const char *, const char *, uint32_t)>(kOutput);
    auto askBox = ctx.Original<int32_t(__cdecl *)(void *, const char *, const char *, uint32_t)>(kAskBox);
    auto askWith = ctx.Original<int32_t(__cdecl *)(void *, const char *, const char *, uint32_t)>(kAskWithFramework);
    auto environment = ctx.Original<uint32_t(__cdecl *)(const char *, char *, uint32_t)>(kEnvironmentText);
    auto debugText = ctx.Original<uint8_t(__cdecl *)(const char *)>(kDebugText);
    static const char *const kNames[] = {"GZFRAMEWORK_DEBUG_LEVEL", "RZSTD_OUTPUT_PREFIX", "NOT_SET"};
    std::mt19937 rng(kLogLine);
    int taken = 0, asked = 0;
    for (int i = 0; i < 6000; ++i) {
        RandomScript(rng);
        const int which = i % 6;
        const char *file = rng() % 3 == 0 ? nullptr : kFiles[rng() % 4];
        const std::string text = RandomText(rng);
        const uint32_t line = rng() % 100000;
        const int32_t start = static_cast<int32_t>(rng() % 3);
        const char *group = kGroups[rng() % 4];
        const int32_t level = rng() % 2 ? -1 : static_cast<int32_t>(rng() % 8);
        const uint32_t mode = rng() % 10;
        const int number = static_cast<int>(rng() % 1000);
        const uint8_t pendingStart = static_cast<uint8_t>(1 + rng() % 200);
        const int streamPick = static_cast<int>(rng() % 3);
        void *stream = streamPick == 0 ? nullptr : streamPick == 1 ? static_cast<void *>(&gStdOut) : static_cast<void *>(&gStdErr);
        const char *name = kNames[rng() % 3];
        const uint32_t size = rng() % 3 == 0 ? rng() % 4 : 0x80;
        int64_t r[2] = {};
        std::string s[2];
        for (int side = 0; side < 2; ++side) {
            scene.Begin(side, start);
            difftest::AllocationCounter net;
            difftest::FreeRecorder frees;
            switch (which) {
            case 0: {
                uint8_t pending = pendingStart;
                CallLogLine(side == 0 ? logLine : &rzstd::LogLine, group, level, mode, file, line, &pending, "n=%d s=%s", number,
                            text.c_str());
                r[side] = pending;
                break;
            }
            case 1:
                if (side == 0)
                    output(stream, text.c_str(), file, line);
                else
                    rzstd::Output(stream, text.c_str(), file, line);
                break;
            case 2:
                r[side] = side == 0 ? askBox(gS.hwnd, text.c_str(), file, line) : rzstd::AskBox(gS.hwnd, text.c_str(), file, line);
                break;
            case 3:
                r[side] = side == 0 ? askWith(&gFramework, text.c_str(), file, line)
                                    : rzstd::AskWithFramework(&gFramework, text.c_str(), file, line);
                break;
            case 4: {
                char buffer[0x80];
                std::memset(buffer, 0x5c, sizeof(buffer));
                r[side] = side == 0 ? environment(name, buffer, size) : rzstd::EnvironmentText(name, buffer, size);
                Log("buffer %s", std::string(buffer, sizeof(buffer)).c_str());
                break;
            }
            default:
                r[side] = side == 0 ? debugText(text.c_str()) : rzstd::DebugText(text.c_str());
                break;
            }
            frees.Stop();
            net.Stop();
            s[side] = scene.End(side) + "frees " + Sizes(frees) + "net " + std::to_string(net.Blocks()) + " blocks " +
                      std::to_string(net.Bytes()) + " bytes";
        }
        ++ctx.cases;
        if (which == 0 && r[0] == 0) ++taken;
        if (s[0].find("box ") != std::string::npos) ++asked;
        if (r[0] != r[1] || s[0] != s[1])
            ctx.Fail("case %d (helper %d): %lld / %lld\n  %s\n  %s", i, which, r[0], r[1], s[0].c_str(), s[1].c_str());
    }
    if (taken < 100 || asked < 1000) ctx.Fail("coverage: %d taken, %d asked", taken, asked);
}
