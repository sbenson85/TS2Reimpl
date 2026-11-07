// Differential test for src/app/ReportBlockers.* 0x1c4caf through the C runtime's _mkdir / _rmdir
// (the game never fills in the wide directory function pointers): real directories in the harness
// sandbox, one tree per side, compared after the call. The directory walk starts at the drive root,
// so FindFirstFileA is wrapped: sandbox paths go to the harness's real (sandboxed) function, the
// directories above it are reported as existing.
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "app/ReportBlockers.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCreateDirectories = 0x1c4caf;
constexpr uint32_t kCreateDirectoryWPointer = 0x10cb620, kRemoveDirectoryWPointer = 0x10cb624;

std::vector<std::string> gLog;

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + "|";
    return out;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) ctx.Fail("%s:\n  original: %.500s\n  ours:     %.500s", what, a.c_str(), b.c_str());
}

class Slots {
public:
    explicit Slots(difftest::Context &c) : ctx(c) {}
    ~Slots()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }
    void Set(uint32_t rva, void *value)
    {
        void **slot = ctx.image.At<void *>(rva);
        saved.emplace_back(rva, *slot);
        *slot = value;
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
};

struct Str {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    explicit Str(const char *text = "")
    {
        std::memset(raw, 0xcd, sizeof(raw));
        S().Construct(text);
    }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(raw); }
};

std::string ToUtf8(const std::wstring &wide)
{
    if (wide.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string AnsiOf(const std::wstring &wide)
{
    char ansi[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, ansi, sizeof(ansi), nullptr, nullptr);
    return ansi;
}

// The walk starts at the drive root: those directories (outside the sandbox) are reported as existing
// without asking the OS; the sandbox's own are looked up for real.
constexpr uint32_t kIatFindFirstFileA = 0xd9a1f8, kIatFindClose = 0xd9a1f4;
HANDLE const kPretendHandle = reinterpret_cast<HANDLE>(0x4242);

HANDLE WINAPI FindFirstA(const char *pattern, WIN32_FIND_DATAA *data)
{
    std::string prefix = AnsiOf(difftest::SandboxDir());
    gLog.push_back(std::string("find ") + pattern);
    if (_strnicmp(pattern, prefix.c_str(), prefix.size()) == 0)
        return reinterpret_cast<HANDLE(WINAPI *)(const char *, WIN32_FIND_DATAA *)>(difftest::SandboxFunction("FindFirstFileA"))(pattern, data);
    std::memset(data, 0, sizeof(*data));
    data->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    std::strcpy(data->cFileName, ".");
    return kPretendHandle;
}

BOOL WINAPI DummyWide(const wchar_t *, void *)
{
    gLog.push_back("DUMMY WIDE FUNCTION CALLED");
    return FALSE;
}

BOOL WINAPI FindCloseFake(HANDLE handle)
{
    if (handle == kPretendHandle) return TRUE;
    return reinterpret_cast<BOOL(WINAPI *)(HANDLE)>(difftest::SandboxFunction("FindClose"))(handle);
}

// Every file and directory below `root` as "relative path[\]" entries, sorted.
void ListTree(const std::wstring &root, const std::wstring &relative, std::vector<std::string> &out)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((root + relative + L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.push_back(ToUtf8(relative + name) + (directory ? "\\" : ""));
        if (directory) ListTree(root, relative + name + L"\\", out);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

std::string Tree(const std::wstring &root)
{
    std::vector<std::string> lines;
    ListTree(root, L"", lines);
    std::sort(lines.begin(), lines.end());
    std::string out;
    for (const std::string &l : lines) out += l + ";";
    return out;
}

} // namespace

DIFF_TEST(ReportBlockers_CreateDirectories_Crt, kCreateDirectories)
{
    Slots slots(ctx);
    slots.Set(kCreateDirectoryWPointer, nullptr);
    slots.Set(kRemoveDirectoryWPointer, nullptr);
    slots.Set(kIatFindFirstFileA, reinterpret_cast<void *>(&FindFirstA));
    slots.Set(kIatFindClose, reinterpret_cast<void *>(&FindCloseFake));
    auto original = ctx.Original<uint32_t(__cdecl *)(cRZString *)>(kCreateDirectories);
    struct Case {
        const char *path; // below the side's root
        bool trailing;    // append a backslash
    };
    // "blocker" is a file in the tree; "have\sub" exists already.
    const Case cases[] = {{"new", false},
                          {"new", true},
                          {"new\\deeper\\still", false},
                          {"new/deeper/still", true},
                          {"have\\sub", false},
                          {"have\\sub\\more\\", true},
                          {"have", false},
                          {"blocker", false},
                          {"blocker\\x", false},
                          {"p\\q\\bad:name\\z", false},
                          {"p\\bad:name", true},
                          {"bad:name\\x", false},
                          {"have\\sub\\r\\bad:name", false},
                          {"a\\b\\c\\d\\bad:name\\x", false},
                          {"p\\q\\blocker\\z", false},
                          {"p\\q\\r\\s\\t\\u", false},
                          {"caf\xc3\xa9\\ok", false},
                          {"a path that is long enough to need the heap\\and a second long component\\x", false},
                          {"", false}};
    // The wide path needs both function pointers: with only one set, the C runtime path runs (the
    // dummies must not be called).
    for (int config = 0; config < 3; ++config)
    for (const Case &c : cases) {
        *ctx.image.At<void *>(kCreateDirectoryWPointer) = config == 1 ? reinterpret_cast<void *>(&DummyWide) : nullptr;
        *ctx.image.At<void *>(kRemoveDirectoryWPointer) = config == 2 ? reinterpret_cast<void *>(&DummyWide) : nullptr;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            difftest::EmptySandbox();
            std::wstring root = difftest::SandboxPath(side == 0 ? L"orig\\" : L"ours\\");
            CreateDirectoryW(root.c_str(), nullptr);
            CreateDirectoryW((root + L"have").c_str(), nullptr);
            CreateDirectoryW((root + L"have\\sub").c_str(), nullptr);
            HANDLE f = CreateFileW((root + L"blocker").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            CloseHandle(f);
            std::string text = std::string(c.path).empty() ? "" : AnsiOf(root) + c.path + (c.trailing ? "\\" : "");
            gLog.clear();
            Str input(text.c_str());
            uint32_t r = side == 0 ? original(&input.S()) : appbase::CreateDirectories(&input.S());
            std::string log = Joined();
            const std::string mine = side == 0 ? "\\orig\\" : "\\ours\\";
            for (size_t at; (at = log.find(mine)) != std::string::npos;) log.replace(at, mine.size(), "\\SIDE\\");
            results[side] = "ret " + std::to_string(r & 0xff) + " arg-same " + std::to_string(input.S().mString == text) + " tree " + Tree(root) + " log " + log;
        }
        ExpectSame(ctx, c.path, results[0], results[1]);
    }
    difftest::EmptySandbox();
}
