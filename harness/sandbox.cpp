// File-system sandbox for original code: file imports are passed through only for paths inside a
// per-run temporary directory (difftest::SandboxDir) and for handles opened there. Anything else
// raises the import trap, exactly as an untrapped import would, so original code still can't touch
// the user's files. Our own code calls Windows directly; tests keep it inside the sandbox too.
#include "difftest.h"
#include "sandbox.h"

#include <windows.h>

#include <cwchar>
#include <mutex>
#include <set>
#include <string>

namespace {

std::wstring gDir; // with trailing backslash
std::mutex gLock;
std::set<HANDLE> gFiles, gFinds;

std::wstring Widen(const char *s)
{
    std::wstring w;
    if (!s) return w;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (n > 0) {
        w.resize(n);
        MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
        w.resize(n - 1);
    }
    return w;
}

bool Inside(const wchar_t *path)
{
    if (!path || gDir.empty()) return false;
    wchar_t full[4096];
    DWORD n = GetFullPathNameW(path, 4096, full, nullptr);
    if (n == 0 || n >= 4096) return false;
    // Device paths (\\?\, \\.\) and anything outside the directory are refused.
    if (std::wcsncmp(full, L"\\\\", 2) == 0) return false;
    return _wcsnicmp(full, gDir.c_str(), gDir.size()) == 0;
}
bool Inside(const char *path)
{
    return path && Inside(Widen(path).c_str());
}

bool KnownFile(HANDLE h)
{
    std::lock_guard<std::mutex> g(gLock);
    return gFiles.count(h) != 0;
}
HANDLE AddFile(HANDLE h)
{
    if (h != INVALID_HANDLE_VALUE) {
        std::lock_guard<std::mutex> g(gLock);
        gFiles.insert(h);
    }
    return h;
}
HANDLE AddFind(HANDLE h)
{
    if (h != INVALID_HANDLE_VALUE) {
        std::lock_guard<std::mutex> g(gLock);
        gFinds.insert(h);
    }
    return h;
}

[[noreturn]] void Refuse(const char *name)
{
    TrapImport(std::string("KERNEL32.dll!") + name + " (outside the file sandbox)");
}
#define REQUIRE(cond, name)                                                                            \
    if (!(cond)) Refuse(name)

// Handle-based functions.
BOOL WINAPI SbReadFile(HANDLE h, LPVOID b, DWORD n, LPDWORD r, LPOVERLAPPED o)
{
    REQUIRE(KnownFile(h) && !o, "ReadFile");
    return ReadFile(h, b, n, r, o);
}
BOOL WINAPI SbWriteFile(HANDLE h, LPCVOID b, DWORD n, LPDWORD w, LPOVERLAPPED o)
{
    REQUIRE(KnownFile(h) && !o, "WriteFile");
    return WriteFile(h, b, n, w, o);
}
DWORD WINAPI SbSetFilePointer(HANDLE h, LONG d, PLONG hi, DWORD m)
{
    REQUIRE(KnownFile(h), "SetFilePointer");
    return SetFilePointer(h, d, hi, m);
}
BOOL WINAPI SbSetFilePointerEx(HANDLE h, LARGE_INTEGER d, PLARGE_INTEGER p, DWORD m)
{
    REQUIRE(KnownFile(h), "SetFilePointerEx");
    return SetFilePointerEx(h, d, p, m);
}
DWORD WINAPI SbGetFileSize(HANDLE h, LPDWORD hi)
{
    REQUIRE(KnownFile(h), "GetFileSize");
    return GetFileSize(h, hi);
}
BOOL WINAPI SbGetFileSizeEx(HANDLE h, PLARGE_INTEGER s)
{
    REQUIRE(KnownFile(h), "GetFileSizeEx");
    return GetFileSizeEx(h, s);
}
BOOL WINAPI SbSetEndOfFile(HANDLE h)
{
    REQUIRE(KnownFile(h), "SetEndOfFile");
    return SetEndOfFile(h);
}
BOOL WINAPI SbFlushFileBuffers(HANDLE h)
{
    REQUIRE(KnownFile(h), "FlushFileBuffers");
    return FlushFileBuffers(h);
}
BOOL WINAPI SbGetFileInformationByHandle(HANDLE h, LPBY_HANDLE_FILE_INFORMATION i)
{
    REQUIRE(KnownFile(h), "GetFileInformationByHandle");
    return GetFileInformationByHandle(h, i);
}
BOOL WINAPI SbGetFileTime(HANDLE h, LPFILETIME c, LPFILETIME a, LPFILETIME w)
{
    REQUIRE(KnownFile(h), "GetFileTime");
    return GetFileTime(h, c, a, w);
}
BOOL WINAPI SbSetFileTime(HANDLE h, const FILETIME *c, const FILETIME *a, const FILETIME *w)
{
    REQUIRE(KnownFile(h), "SetFileTime");
    return SetFileTime(h, c, a, w);
}
BOOL WINAPI SbCloseHandle(HANDLE h)
{
    {
        std::lock_guard<std::mutex> g(gLock);
        if (!gFiles.erase(h)) Refuse("CloseHandle");
    }
    return CloseHandle(h);
}

// Path-based functions.
HANDLE WINAPI SbCreateFileW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t)
{
    REQUIRE(Inside(p) && !t, "CreateFileW");
    return AddFile(CreateFileW(p, a, s, sa, c, f, t));
}
HANDLE WINAPI SbCreateFileA(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t)
{
    REQUIRE(Inside(p) && !t, "CreateFileA");
    return AddFile(CreateFileA(p, a, s, sa, c, f, t));
}
DWORD WINAPI SbGetFileAttributesW(LPCWSTR p)
{
    REQUIRE(Inside(p), "GetFileAttributesW");
    return GetFileAttributesW(p);
}
DWORD WINAPI SbGetFileAttributesA(LPCSTR p)
{
    REQUIRE(Inside(p), "GetFileAttributesA");
    return GetFileAttributesA(p);
}
BOOL WINAPI SbGetFileAttributesExW(LPCWSTR p, GET_FILEEX_INFO_LEVELS l, LPVOID i)
{
    REQUIRE(Inside(p), "GetFileAttributesExW");
    return GetFileAttributesExW(p, l, i);
}
BOOL WINAPI SbSetFileAttributesA(LPCSTR p, DWORD a)
{
    REQUIRE(Inside(p), "SetFileAttributesA");
    return SetFileAttributesA(p, a);
}
BOOL WINAPI SbDeleteFileW(LPCWSTR p)
{
    REQUIRE(Inside(p), "DeleteFileW");
    return DeleteFileW(p);
}
BOOL WINAPI SbDeleteFileA(LPCSTR p)
{
    REQUIRE(Inside(p), "DeleteFileA");
    return DeleteFileA(p);
}
BOOL WINAPI SbCreateDirectoryW(LPCWSTR p, LPSECURITY_ATTRIBUTES sa)
{
    REQUIRE(Inside(p), "CreateDirectoryW");
    return CreateDirectoryW(p, sa);
}
BOOL WINAPI SbRemoveDirectoryW(LPCWSTR p)
{
    REQUIRE(Inside(p), "RemoveDirectoryW");
    return RemoveDirectoryW(p);
}
BOOL WINAPI SbRemoveDirectoryA(LPCSTR p)
{
    REQUIRE(Inside(p), "RemoveDirectoryA");
    return RemoveDirectoryA(p);
}
BOOL WINAPI SbMoveFileA(LPCSTR from, LPCSTR to)
{
    REQUIRE(Inside(from) && Inside(to), "MoveFileA");
    return MoveFileA(from, to);
}
BOOL WINAPI SbMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD f)
{
    REQUIRE(Inside(from) && (!to || Inside(to)) && !(f & MOVEFILE_DELAY_UNTIL_REBOOT), "MoveFileExW");
    return MoveFileExW(from, to, f);
}
BOOL WINAPI SbCopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists)
{
    REQUIRE(Inside(from) && Inside(to), "CopyFileA");
    return CopyFileA(from, to, failIfExists);
}
BOOL WINAPI SbCopyFileW(LPCWSTR from, LPCWSTR to, BOOL failIfExists)
{
    REQUIRE(Inside(from) && Inside(to), "CopyFileW");
    return CopyFileW(from, to, failIfExists);
}
HANDLE WINAPI SbFindFirstFileW(LPCWSTR p, LPWIN32_FIND_DATAW d)
{
    REQUIRE(Inside(p), "FindFirstFileW");
    return AddFind(FindFirstFileW(p, d));
}
HANDLE WINAPI SbFindFirstFileA(LPCSTR p, LPWIN32_FIND_DATAA d)
{
    REQUIRE(Inside(p), "FindFirstFileA");
    return AddFind(FindFirstFileA(p, d));
}
HANDLE WINAPI SbFindFirstFileExW(LPCWSTR p, FINDEX_INFO_LEVELS l, LPVOID d, FINDEX_SEARCH_OPS o, LPVOID f, DWORD fl)
{
    REQUIRE(Inside(p), "FindFirstFileExW");
    return AddFind(FindFirstFileExW(p, l, d, o, f, fl));
}
bool KnownFind(HANDLE h)
{
    std::lock_guard<std::mutex> g(gLock);
    return gFinds.count(h) != 0;
}
BOOL WINAPI SbFindNextFileA(HANDLE h, LPWIN32_FIND_DATAA d)
{
    REQUIRE(KnownFind(h), "FindNextFileA");
    return FindNextFileA(h, d);
}
BOOL WINAPI SbFindNextFileW(HANDLE h, LPWIN32_FIND_DATAW d)
{
    REQUIRE(KnownFind(h), "FindNextFileW");
    return FindNextFileW(h, d);
}
BOOL WINAPI SbFindClose(HANDLE h)
{
    {
        std::lock_guard<std::mutex> g(gLock);
        if (!gFinds.erase(h)) Refuse("FindClose");
    }
    return FindClose(h);
}

// Only the shell-link COM code (Ole32.dll) may be loaded by name: the original resolves .lnk
// shortcuts through it, in-process.
HMODULE WINAPI SbLoadLibraryA(LPCSTR name)
{
    REQUIRE(name && _stricmp(name, "Ole32.dll") == 0, "LoadLibraryA");
    return LoadLibraryA(name);
}
// Pure string functions.
int WINAPI SblstrcmpiA(LPCSTR a, LPCSTR b)
{
    return lstrcmpiA(a, b);
}
int WINAPI SblstrlenA(LPCSTR a)
{
    return lstrlenA(a);
}
LPSTR WINAPI SblstrcpynA(LPSTR d, LPCSTR s, int n)
{
    return lstrcpynA(d, s, n);
}

struct Entry {
    const char *name;
    void *fn;
};
#define E(f) {#f, reinterpret_cast<void *>(&Sb##f)}
const Entry kEntries[] = {
    E(ReadFile), E(WriteFile), E(SetFilePointer), E(SetFilePointerEx), E(GetFileSize), E(GetFileSizeEx),
    E(SetEndOfFile), E(FlushFileBuffers), E(GetFileInformationByHandle), E(GetFileTime), E(SetFileTime),
    E(CloseHandle), E(CreateFileW), E(CreateFileA), E(GetFileAttributesW), E(GetFileAttributesA),
    E(GetFileAttributesExW), E(SetFileAttributesA), E(DeleteFileW), E(DeleteFileA), E(CreateDirectoryW),
    E(RemoveDirectoryW), E(RemoveDirectoryA), E(MoveFileA), E(MoveFileExW), E(CopyFileA), E(CopyFileW), E(FindFirstFileW),
    E(FindFirstFileA), E(FindFirstFileExW), E(FindNextFileA), E(FindNextFileW), E(FindClose),
    E(LoadLibraryA), E(lstrcmpiA), E(lstrlenA), E(lstrcpynA),
};
#undef E

void DeleteTree(const std::wstring &dir)
{
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &d);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = d.cFileName;
            if (n == L"." || n == L"..") continue;
            std::wstring p = dir + n;
            if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteTree(p + L"\\");
            } else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(h, &d));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

} // namespace

void *SandboxedImport(const std::string &dll, const std::string &fn)
{
    if (_stricmp(dll.c_str(), "KERNEL32.dll") != 0) return nullptr;
    for (const Entry &e : kEntries) {
        if (fn == e.name) return e.fn;
    }
    return nullptr;
}

bool InitSandbox()
{
    wchar_t temp[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp)) return false;
    std::wstring root = std::wstring(temp) + L"ts2difftest\\";
    CreateDirectoryW(root.c_str(), nullptr);
    gDir = root + std::to_wstring(GetCurrentProcessId()) + L"\\";
    DeleteTree(gDir);
    if (!CreateDirectoryW(gDir.c_str(), nullptr)) return false;
    wchar_t full[MAX_PATH];
    // Canonical form (long names), so prefix checks against GetFullPathName results work.
    if (GetLongPathNameW(gDir.c_str(), full, MAX_PATH)) gDir = full;
    return true;
}

void CleanupSandbox()
{
    if (!gDir.empty()) DeleteTree(gDir);
}

namespace difftest {

const std::wstring &SandboxDir()
{
    return gDir;
}

std::wstring SandboxPath(const std::wstring &name)
{
    return gDir + name;
}

void EmptySandbox()
{
    std::wstring dir = gDir;
    DeleteTree(dir);
    CreateDirectoryW(dir.c_str(), nullptr);
}

void *SandboxCreateFile(const std::wstring &name, uint32_t access, uint32_t creation, uint32_t sharing)
{
    return AddFile(CreateFileW((gDir + name).c_str(), access, sharing, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr));
}

void *SandboxFunction(const char *name)
{
    return SandboxedImport("KERNEL32.dll", name);
}

void SandboxCloseFile(void *handle)
{
    std::lock_guard<std::mutex> g(gLock);
    if (gFiles.erase(handle)) CloseHandle(handle);
}

} // namespace difftest
