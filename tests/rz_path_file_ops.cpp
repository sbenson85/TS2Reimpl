// Differential tests for PathFileTime (0x1c2068) and PathDelete (0x1c2e25), lane QK. The original reaches the CRT's stat in
// the image (0x83bab7) and a wide file function cell / an import slot, none of which run in the harness, so each is stood in
// on both sides alike: the image's stat by a wrapper over the host _stat64i32, the delete cells by fakes that log the text.
#include "difftest.h"
#include "code_patch.h"
#include "game/game.h"
#include "rz/PathFileOps.h"
#include "rz/RZTextConvert.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h"

#define NOMINMAX
#include <windows.h>

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<std::string> gLog;

int __cdecl StandStat(const char *path, struct _stat64i32 *out)
{
    gLog.push_back(std::string("stat ") + path);
    return _stat64i32(path, out);
}

bool __cdecl StandConvertString(const cIGZString &src, cIGZString &dst, int32_t)
{
    dst.FromChar(src.Data(), src.Strlen());
    return true;
}

BOOL WINAPI FakeDeleteW(LPCWSTR path)
{
    std::string text;
    for (; *path; ++path) text.push_back(static_cast<char>(*path));
    gLog.push_back("deleteW " + text);
    return 1;
}

BOOL WINAPI FakeDeleteA(LPCSTR path)
{
    gLog.push_back(std::string("deleteA ") + path);
    return path[0] == 'y';
}

void MakeFile(const std::wstring &name)
{
    void *h = difftest::SandboxCreateFile(name, 0x40000000, 2);
    difftest::SandboxCloseFile(h);
}

using TimeFn = uint32_t(__cdecl *)(const cIGZString *, int32_t);
using DeleteFn = uint32_t(__cdecl *)(const cIGZString *);

} // namespace

DIFF_TEST(PathFileOps_Time, 0x1c2068)
{
    auto orig = ctx.Original<TimeFn>(0x1c2068);
    shape::StandIn convert(0x1b025d, &RZText::ConvertStringToString, &StandConvertString);
    shape::CodePatch stat(0x83bab7, reinterpret_cast<const void *>(&StandStat));
    difftest::EmptySandbox();
    MakeFile(L"a.dat");
    {
        // Three different times, so that choosing the wrong one of the three shows.
        HANDLE h = CreateFileW(difftest::SandboxPath(L"a.dat").c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        auto at = [](uint64_t unixSeconds) {
            const uint64_t ticks = (unixSeconds + 11644473600ull) * 10000000ull;
            FILETIME t;
            t.dwLowDateTime = static_cast<DWORD>(ticks);
            t.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
            return t;
        };
        const FILETIME created = at(1500000000), accessed = at(1600000001), written = at(1700000002);
        if (h == INVALID_HANDLE_VALUE || !SetFileTime(h, &created, &accessed, &written)) ctx.Fail("could not set the sandbox file's times");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    SetCurrentDirectoryW(difftest::SandboxDir().c_str());  // short relative names stay in the string's inline buffer (no heap free)
    const std::string existing = "a.dat";
    const std::string missing = "none.dat";
    int nonzero = 0;
    for (const std::string &p : {existing, missing}) {
        for (int32_t which = -1; which <= 5; ++which) {
            alignas(8) unsigned char raw[sizeof(cRZString)];
            cRZString *s = reinterpret_cast<cRZString *>(raw);
            s->Construct(p.c_str());
            gLog.clear();
            const uint32_t a = orig(s->AsInterface(), which);
            const std::vector<std::string> la = gLog;
            gLog.clear();
            const uint32_t b = PathFileTime(s->AsInterface(), which);
            if (a != b) ctx.Fail("%s which %d: original %u, ours %u", p.c_str(), which, a, b);
            if (la.size() != 1 || la[0] != "stat " + p) ctx.Fail("%s which %d: original did not stat the converted text once", p.c_str(), which);
            if (a) ++nonzero;
            s->Destruct();
        }
    }
    if (nonzero < 3) ctx.Fail("setup reaches too little (%d nonzero times)", nonzero);
}

DIFF_TEST(PathFileOps_Delete, 0x1c2e25)
{
    auto orig = ctx.Original<DeleteFn>(0x1c2e25);
    void **wide = GameAddress<void *>(0x10cb5f0);
    void **iat = GameAddress<void *>(0xd9a1f0);
    void *savedWide = *wide, *savedIat = *iat;
    const char *names[] = {"yes.dat", "no.dat", "C:\\x\\y.dat", ""};
    int ok = 0;
    for (int variant = 0; variant < 2; ++variant) {
        *wide = variant ? reinterpret_cast<void *>(&FakeDeleteW) : nullptr;
        *iat = reinterpret_cast<void *>(&FakeDeleteA);
        for (const char *n : names) {
            alignas(8) unsigned char raw[sizeof(cRZString)];
            cRZString *s = reinterpret_cast<cRZString *>(raw);
            s->Construct(n);
            gLog.clear();
            const bool a = (orig(s->AsInterface()) & 0xff) != 0;
            const std::vector<std::string> la = gLog;
            gLog.clear();
            const bool b = PathDelete(s->AsInterface());
            if (a != b) ctx.Fail("variant %d '%s': original %d, ours %d", variant, n, a, b);
            if (la != gLog || la.size() != 1) ctx.Fail("variant %d '%s': calls differ", variant, n);
            if (a) ++ok;
            s->Destruct();
        }
    }
    *wide = savedWide;
    *iat = savedIat;
    if (ok < 3) ctx.Fail("setup reaches too little (%d successes)", ok);
}
