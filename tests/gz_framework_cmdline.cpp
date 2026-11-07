// gz::cGZFramework's command-line pair (lane ER): 0x143660 SetCommandLine (cRZCmdLine::operator= into the framework's
// command line under its lock) and 0x1427a7 CommitCommandLine (__cdecl: the program-path step 0x1aeb57 with no text),
// against the original with real cRZCmdLine objects (built by our verified cRZCmdLine constructors). SetCommandLine runs
// on three routes: the original on an object from the original base constructor 0x141d76; the hook's adapter on such an
// object; our member on an object from our constructor. The lock's vtable is a recording copy on each object.
#include "difftest.h"
#include "game/game.h"
#include "gz/FrameworkShape.h"
#include "rz/cRZCmdLine.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

std::vector<std::string> gLog;

int32_t __fastcall LockRec(void *, void *)
{
    gLog.emplace_back("lock");
    return 1;
}
std::string Describe(const cRZCmdLine *cmd);
int32_t __fastcall UnlockRec(void *lock, void *)
{
    // The framework's command line (lock + 8) as the unlock sees it: the assignment happens under the lock.
    gLog.emplace_back("unlock " + Describe(reinterpret_cast<const cRZCmdLine *>(static_cast<char *>(lock) + 8)));
    return 0;
}

cRZCmdLine *NewCmd(const char *text)
{
    auto *cmd = static_cast<cRZCmdLine *>(::operator new(sizeof(cRZCmdLine)));
    std::memset(cmd, 0xCD, sizeof(cRZCmdLine));
    cmd->Construct(text);
    return cmd;
}

void FreeCmd(cRZCmdLine *cmd)
{
    cmd->Destruct();
    ::operator delete(cmd);
}

std::string Str(const std::string &s)
{
    return "[" + s + "]" + std::to_string(s.size());
}

std::string Describe(const cRZCmdLine *cmd)
{
    std::string out = "ref=" + std::to_string(cmd->mRefCount) + " n=" + std::to_string(cmd->mArgs.Count()) + " args:";
    for (const cRZString *a = cmd->mArgs.mFirst; a != cmd->mArgs.mLast; ++a) out += " " + Str(a->mString);
    out += " text=" + Str(cmd->mText.mString) + " empty=" + Str(cmd->mEmpty.mString);
    return out;
}

const char *kWords[] = {"-w", "/nosound", "Sims2EP9.exe", "C:\\Games\\Sims2EP9.exe", "-lang:en", "\"quoted arg\"", "a",
                        "longer_argument_to_force_heap_strings_in_every_element", "x.y.z", "TS2.exe"};

std::string RandomText(std::mt19937 &rng, int maxWords)
{
    std::string text;
    int n = static_cast<int>(rng() % (maxWords + 1));
    for (int i = 0; i < n; ++i) {
        if (i) text += ' ';
        text += kWords[rng() % (sizeof(kWords) / sizeof(kWords[0]))];
    }
    return text;
}

void EnsureClock(difftest::Context &ctx)
{
    static bool done = false;
    if (!done) ctx.Original<int(__cdecl *)()>(0x83a409)(); // the CRT's performance-counter cache (cRZRandom's seed)
    done = true;
}

struct Framework {
    void *memory;
    void *lockVt[8];
    cRZCmdLine &CmdLine() { return *reinterpret_cast<cRZCmdLine *>(static_cast<char *>(memory) + 0xfc); }
};

void Build(difftest::Context &ctx, Framework &f, bool ours, uint32_t fill)
{
    EnsureClock(ctx);
    f.memory = ::operator new(sizeof(gz::cGZFramework));
    std::memset(f.memory, static_cast<int>(fill), sizeof(gz::cGZFramework));
    if (ours) gz::cGZFrameworkAdapters::Construct(f.memory, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0x141d76)(f.memory);
    auto *lock = reinterpret_cast<cRZCriticalSection *>(static_cast<char *>(f.memory) + 0xf4);
    std::memcpy(f.lockVt, lock->mVTable, sizeof(void *) * 7);
    f.lockVt[1] = reinterpret_cast<void *>(&LockRec);
    f.lockVt[2] = reinterpret_cast<void *>(&UnlockRec);
    lock->mVTable = f.lockVt;
}

// The module path the program-path step reads (GetModuleFileNameW through the image's cached pointer, narrow fallback).
constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kModuleFileNameWResolved = 0x10caeb5, kModuleFileNameWPointer = 0x10caeb8;
std::wstring gWide;
DWORD WINAPI FakeWide(void *, wchar_t *buffer, DWORD size)
{
    gLog.emplace_back("GetModuleFileNameW");
    size_t n = gWide.size() < size - 1 ? gWide.size() : size - 1;
    std::wmemcpy(buffer, gWide.c_str(), n);
    buffer[n] = 0;
    return static_cast<DWORD>(n);
}
DWORD WINAPI FakeNarrow(void *, char *buffer, DWORD size)
{
    gLog.emplace_back("GetModuleFileNameA");
    if (size) buffer[0] = 0;
    return 0;
}

struct ModulePathFakes {
    difftest::Context &ctx;
    uint8_t savedResolved;
    void *savedCached, *savedNarrow;
    explicit ModulePathFakes(difftest::Context &c) : ctx(c)
    {
        savedResolved = *ctx.image.At<uint8_t>(kModuleFileNameWResolved);
        savedCached = *ctx.image.At<void *>(kModuleFileNameWPointer);
        savedNarrow = *ctx.image.At<void *>(kIatGetModuleFileNameA);
        *ctx.image.At<uint8_t>(kModuleFileNameWResolved) = 1;
        *ctx.image.At<void *>(kModuleFileNameWPointer) = reinterpret_cast<void *>(&FakeWide);
        *ctx.image.At<void *>(kIatGetModuleFileNameA) = reinterpret_cast<void *>(&FakeNarrow);
    }
    ~ModulePathFakes()
    {
        *ctx.image.At<uint8_t>(kModuleFileNameWResolved) = savedResolved;
        *ctx.image.At<void *>(kModuleFileNameWPointer) = savedCached;
        *ctx.image.At<void *>(kIatGetModuleFileNameA) = savedNarrow;
    }
};

void ExpectSame(difftest::Context &ctx, const char *what, int i, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) ctx.Fail("%s case %d:\n  original: %.300s\n  ours:     %.300s", what, i, a.c_str(), b.c_str());
}

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + ";";
    return out;
}

} // namespace

DIFF_TEST(GZFramework_SetCommandLine, 0x143660)
{
    std::mt19937 rng(0x143660);
    for (int i = 0; i < 600; ++i) {
        std::string srcText = RandomText(rng, i % 10);
        uint32_t srcRef = rng(), fill = rng() & 0xff;
        bool same = i % 23 == 0; // assign the framework's own command line to itself
        std::string results[3];
        for (int route = 0; route < 3; ++route) {
            Framework f;
            Build(ctx, f, route == 2, fill);
            cRZCmdLine *src = NewCmd(srcText.c_str());
            src->mRefCount = srcRef;
            const cRZCmdLine &from = same ? f.CmdLine() : *src;
            gLog.clear();
            if (route == 0) ctx.Original<void(__thiscall *)(void *, const cRZCmdLine *)>(0x143660)(f.memory, &from);
            else if (route == 1) gz::cGZFrameworkAdapters::SetCommandLine(static_cast<gz::cGZFramework *>(f.memory), nullptr, from);
            else static_cast<gz::cGZFramework *>(f.memory)->SetCommandLine(from);
            results[route] = Joined() + " fw: " + Describe(&f.CmdLine()) + " src: " + Describe(src);
            FreeCmd(src);
            // The framework's command line and containers are left to leak (the lock vtable is a stack copy).
        }
        ExpectSame(ctx, "SetCommandLine (adapter)", i, results[0], results[1]);
        ExpectSame(ctx, "SetCommandLine (ours)", i, results[0], results[2]);
    }
}

DIFF_TEST(GZFramework_CommitCommandLine, 0x1427a7)
{
    ModulePathFakes fakes(ctx);
    std::mt19937 rng(0x1427a7);
    const char *paths[] = {"C:\\Games\\Sims2EP9.exe", "D:\\x\\TS2.exe", "", "C:\\a.b\\Sims2EP9.EXE", "relative.exe"};
    for (int i = 0; i < 600; ++i) {
        std::string text = RandomText(rng, i % 8);
        std::string path = paths[rng() % (sizeof(paths) / sizeof(paths[0]))];
        gWide.assign(path.begin(), path.end());
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            cRZCmdLine *cmd = NewCmd(text.c_str());
            gLog.clear();
            auto *face = reinterpret_cast<cIGZCmdLine *>(cmd);
            if (side == 0) ctx.Original<void(__cdecl *)(cIGZCmdLine *)>(0x1427a7)(face);
            else gz::cGZFramework::CommitCommandLine(face);
            results[side] = Joined() + " " + Describe(cmd);
            FreeCmd(cmd);
        }
        ExpectSame(ctx, "CommitCommandLine", i, results[0], results[1]);
    }
}
