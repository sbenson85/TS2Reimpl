// Instance mode and the Steam stub in the host: see instance.h, src/platform/Instance.h and
// docs/contracts/steam.md.
#include "instance.h"

#include "hook/log.h"
#include "platform/Instance.h"
#include "platform/SteamStub.h"

#include <windows.h>

#include <share.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

namespace {

int gId = 0;
bool gSteamStub = false;
std::wstring gUserData, gAllUsers;
std::wstring gCommandLineW;
std::string gCommandLineA;

std::wstring IniString(const std::wstring &ini, const wchar_t *section, const wchar_t *key)
{
    wchar_t buffer[1024];
    GetPrivateProfileStringW(section, key, L"", buffer, 1024, ini.c_str());
    return buffer;
}

std::string Narrow(const std::wstring &s)
{
    std::string out(s.size() * 2 + 1, '\0');
    int n = WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, out.data(), static_cast<int>(out.size()), nullptr, nullptr);
    out.resize(n > 0 ? n - 1 : 0);
    return out;
}

void StubLog(const char *line) { Log("%s", line); }

// kernel32's own CreateMutexA: the host's import slot is redirected here, so it can't be called by name.
using CreateMutexAFn = HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
CreateMutexAFn RealCreateMutexA()
{
    static auto fn = reinterpret_cast<CreateMutexAFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateMutexA"));
    return fn;
}

HANDLE WINAPI InstanceCreateMutexA(LPSECURITY_ATTRIBUTES attributes, BOOL owner, LPCSTR name)
{
    std::string renamed;
    if (instance::MutexNameForInstance(name, gId, renamed)) {
        Log("instance %d: mutex \"%s\" is \"%s\"", gId, name, renamed.c_str());
        return RealCreateMutexA()(attributes, owner, renamed.c_str());
    }
    return RealCreateMutexA()(attributes, owner, name);
}

// TS2HOST_TRACEFILES=1: failed CreateFileA/W calls from the game and the host are logged with their path, access, share
// mode, disposition and error, and named kernel objects with their name (finding what two instances collide on).
bool gTraceFiles = false;
bool gTraceAllOpens = false; // TS2HOST_TRACEFILES=2: successful opens too

template <class Fn> Fn Kernel32(const char *name)
{
    return reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name));
}

HANDLE WINAPI TraceCreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE tmpl)
{
    static auto real = Kernel32<decltype(&CreateFileA)>("CreateFileA");
    HANDLE h = real(path, access, share, sa, disposition, flags, tmpl);
    if (h != INVALID_HANDLE_VALUE && path && (gTraceAllOpens || !strstr(path, "TS2Reimpl\\local") && !strstr(path, "TS2Reimpl/local"))) {
        DWORD error = GetLastError();
        Log("file: CreateFileA \"%s\" access 0x%lx share %lu disposition %lu (opened)", path, access, share, disposition);
        SetLastError(error);
    }
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        Log("file: CreateFileA \"%s\" access 0x%lx share %lu disposition %lu failed, error %lu", path ? path : "(null)", access, share,
            disposition, error);
        SetLastError(error);
    }
    return h;
}

HANDLE WINAPI TraceCreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE tmpl)
{
    static auto real = Kernel32<decltype(&CreateFileW)>("CreateFileW");
    HANDLE h = real(path, access, share, sa, disposition, flags, tmpl);
    if (h != INVALID_HANDLE_VALUE && path && (gTraceAllOpens || !wcsstr(path, L"TS2Reimpl\\local") && !wcsstr(path, L"TS2Reimpl/local"))) {
        DWORD error = GetLastError();
        Log("file: CreateFileW \"%ls\" access 0x%lx share %lu disposition %lu (opened)", path, access, share, disposition);
        SetLastError(error);
    }
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        Log("file: CreateFileW \"%ls\" access 0x%lx share %lu disposition %lu failed, error %lu", path ? path : L"(null)", access, share,
            disposition, error);
        SetLastError(error);
    }
    return h;
}

// Named kernel objects are machine-wide: two instances opening the same name share it. Each named create/open is logged
// with its result and last error (183: it already existed).
template <class R> R LogNamed(const char *api, const char *name, R result)
{
    DWORD error = GetLastError();
    if (name) Log("named: %s \"%s\" -> %p, error %lu", api, name, reinterpret_cast<void *>(result), error);
    SetLastError(error);
    return result;
}

std::string NarrowName(LPCWSTR name)
{
    return name ? Narrow(name) : std::string();
}

HANDLE WINAPI TraceCreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD protect, DWORD high, DWORD low, LPCSTR name)
{
    static auto real = Kernel32<decltype(&CreateFileMappingA)>("CreateFileMappingA");
    return LogNamed("CreateFileMappingA", name, real(file, sa, protect, high, low, name));
}

HANDLE WINAPI TraceCreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCSTR name)
{
    static auto real = Kernel32<decltype(&CreateEventA)>("CreateEventA");
    return LogNamed("CreateEventA", name, real(sa, manual, initial, name));
}

HANDLE WINAPI TraceCreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCWSTR name)
{
    static auto real = Kernel32<decltype(&CreateEventW)>("CreateEventW");
    HANDLE h = real(sa, manual, initial, name);
    std::string n = NarrowName(name);
    return LogNamed("CreateEventW", name ? n.c_str() : nullptr, h);
}

HANDLE WINAPI TraceOpenEventA(DWORD access, BOOL inherit, LPCSTR name)
{
    static auto real = Kernel32<decltype(&OpenEventA)>("OpenEventA");
    return LogNamed("OpenEventA", name, real(access, inherit, name));
}

HANDLE WINAPI TraceCreateNamedPipeW(LPCWSTR name, DWORD open, DWORD mode, DWORD max, DWORD out, DWORD in, DWORD timeout, LPSECURITY_ATTRIBUTES sa)
{
    static auto real = Kernel32<decltype(&CreateNamedPipeW)>("CreateNamedPipeW");
    HANDLE h = real(name, open, mode, max, out, in, timeout, sa);
    std::string n = NarrowName(name);
    return LogNamed("CreateNamedPipeW", name ? n.c_str() : nullptr, h);
}

std::string HandlePath(HANDLE file)
{
    wchar_t path[MAX_PATH] = L"?";
    GetFinalPathNameByHandleW(file, path, MAX_PATH, 0);
    return Narrow(path);
}

// Byte-range locks are per file, so two instances sharing a hardlinked file see each other's locks.
BOOL WINAPI TraceLockFile(HANDLE file, DWORD offsetLow, DWORD offsetHigh, DWORD lengthLow, DWORD lengthHigh)
{
    static auto real = Kernel32<decltype(&LockFile)>("LockFile");
    BOOL ok = real(file, offsetLow, offsetHigh, lengthLow, lengthHigh);
    DWORD error = GetLastError();
    Log("lock: LockFile \"%s\" at 0x%lx length 0x%lx -> %d, error %lu", HandlePath(file).c_str(), offsetLow, lengthLow, ok, ok ? 0 : error);
    SetLastError(error);
    return ok;
}

BOOL WINAPI TraceLockFileEx(HANDLE file, DWORD flags, DWORD reserved, DWORD lengthLow, DWORD lengthHigh, LPOVERLAPPED overlapped)
{
    static auto real = Kernel32<decltype(&LockFileEx)>("LockFileEx");
    BOOL ok = real(file, flags, reserved, lengthLow, lengthHigh, overlapped);
    DWORD error = GetLastError();
    Log("lock: LockFileEx \"%s\" flags %lu at 0x%lx length 0x%lx -> %d, error %lu", HandlePath(file).c_str(), flags,
        overlapped ? overlapped->Offset : 0, lengthLow, ok, ok ? 0 : error);
    SetLastError(error);
    return ok;
}

BOOL WINAPI TraceReadFile(HANDLE file, LPVOID buffer, DWORD bytes, LPDWORD read, LPOVERLAPPED overlapped)
{
    static auto real = Kernel32<decltype(&ReadFile)>("ReadFile");
    BOOL ok = real(file, buffer, bytes, read, overlapped);
    if (!ok) {
        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) Log("file: ReadFile \"%s\" 0x%lx bytes failed, error %lu", HandlePath(file).c_str(), bytes, error);
        SetLastError(error);
    }
    return ok;
}

// The registry is shared by every instance (and with the installed game).
template <class Fn> Fn Advapi32(const char *name)
{
    return reinterpret_cast<Fn>(GetProcAddress(LoadLibraryW(L"advapi32.dll"), name));
}

LSTATUS WINAPI TraceRegCreateKeyExA(HKEY key, LPCSTR sub, DWORD reserved, LPSTR cls, DWORD options, REGSAM sam, const LPSECURITY_ATTRIBUTES sa,
                                    PHKEY result, LPDWORD disposition)
{
    static auto real = Advapi32<decltype(&RegCreateKeyExA)>("RegCreateKeyExA");
    LSTATUS s = real(key, sub, reserved, cls, options, sam, sa, result, disposition);
    Log("reg: RegCreateKeyExA %p \"%s\" -> %ld, key %p", reinterpret_cast<void *>(key), sub ? sub : "", s, result ? reinterpret_cast<void *>(*result) : nullptr);
    return s;
}

LSTATUS WINAPI TraceRegOpenKeyExA(HKEY key, LPCSTR sub, DWORD options, REGSAM sam, PHKEY result)
{
    static auto real = Advapi32<decltype(&RegOpenKeyExA)>("RegOpenKeyExA");
    LSTATUS s = real(key, sub, options, sam, result);
    Log("reg: RegOpenKeyExA %p \"%s\" -> %ld, key %p", reinterpret_cast<void *>(key), sub ? sub : "", s, result ? reinterpret_cast<void *>(*result) : nullptr);
    return s;
}

LSTATUS WINAPI TraceRegSetValueExA(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE *data, DWORD size)
{
    static auto real = Advapi32<decltype(&RegSetValueExA)>("RegSetValueExA");
    LSTATUS s = real(key, name, reserved, type, data, size);
    Log("reg: RegSetValueExA %p \"%s\" type %lu size %lu%s%.*s -> %ld", reinterpret_cast<void *>(key), name ? name : "", type, size,
        type == REG_SZ ? " " : "", type == REG_SZ ? static_cast<int>(size) : 0, reinterpret_cast<const char *>(data), s);
    return s;
}

LSTATUS WINAPI TraceRegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    static auto real = Advapi32<decltype(&RegQueryValueExA)>("RegQueryValueExA");
    LSTATUS s = real(key, name, reserved, type, data, size);
    Log("reg: RegQueryValueExA %p \"%s\" -> %ld", reinterpret_cast<void *>(key), name ? name : "", s);
    return s;
}

void *TraceRegistryImport(const char *fn)
{
    if (!gTraceFiles) return nullptr;
    if (std::strcmp(fn, "RegCreateKeyExA") == 0) return reinterpret_cast<void *>(&TraceRegCreateKeyExA);
    if (std::strcmp(fn, "RegOpenKeyExA") == 0) return reinterpret_cast<void *>(&TraceRegOpenKeyExA);
    if (std::strcmp(fn, "RegSetValueExA") == 0) return reinterpret_cast<void *>(&TraceRegSetValueExA);
    if (std::strcmp(fn, "RegQueryValueExA") == 0) return reinterpret_cast<void *>(&TraceRegQueryValueExA);
    return nullptr;
}

// Directory listings (TS2HOST_TRACEFILES=2): each FindFirstFile pattern and its result, and each name found.
HANDLE WINAPI TraceFindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW data)
{
    static auto real = Kernel32<decltype(&FindFirstFileW)>("FindFirstFileW");
    HANDLE h = real(pattern, data);
    DWORD error = GetLastError();
    Log("find: FindFirstFileW \"%ls\" -> %p \"%ls\", error %lu", pattern, h, h != INVALID_HANDLE_VALUE ? data->cFileName : L"", error);
    SetLastError(error);
    return h;
}

HANDLE WINAPI TraceFindFirstFileExW(LPCWSTR pattern, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags)
{
    static auto real = Kernel32<decltype(&FindFirstFileExW)>("FindFirstFileExW");
    HANDLE h = real(pattern, level, data, op, filter, flags);
    DWORD error = GetLastError();
    Log("find: FindFirstFileExW \"%ls\" level %d flags %lu -> %p \"%ls\", error %lu", pattern, level, flags, h,
        h != INVALID_HANDLE_VALUE ? static_cast<WIN32_FIND_DATAW *>(data)->cFileName : L"", error);
    SetLastError(error);
    return h;
}

BOOL WINAPI TraceFindNextFileW(HANDLE h, LPWIN32_FIND_DATAW data)
{
    static auto real = Kernel32<decltype(&FindNextFileW)>("FindNextFileW");
    BOOL ok = real(h, data);
    DWORD error = GetLastError();
    Log("find: next %p -> %d \"%ls\", error %lu", h, ok, ok ? data->cFileName : L"", ok ? 0 : error);
    SetLastError(error);
    return ok;
}

HANDLE WINAPI TraceFindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data)
{
    static auto real = Kernel32<decltype(&FindFirstFileA)>("FindFirstFileA");
    HANDLE h = real(pattern, data);
    DWORD error = GetLastError();
    Log("find: FindFirstFileA \"%s\" -> %p \"%s\", error %lu", pattern, h, h != INVALID_HANDLE_VALUE ? data->cFileName : "", error);
    SetLastError(error);
    return h;
}

BOOL WINAPI TraceFindNextFileA(HANDLE h, LPWIN32_FIND_DATAA data)
{
    static auto real = Kernel32<decltype(&FindNextFileA)>("FindNextFileA");
    BOOL ok = real(h, data);
    DWORD error = GetLastError();
    Log("find: nextA %p -> %d \"%s\", error %lu", h, ok, ok ? data->cFileName : "", ok ? 0 : error);
    SetLastError(error);
    return ok;
}

// The game's file layer does not reach the wide file functions through its imports: two static initialisers fetch them
// with GetProcAddress into tables that cRZFile, the directory scan and our replacements of them call through (lane FV):
//   0x1c0d9b, table 0x10cb5e0: CreateFileW, CopyFileW, MoveFileExW, FindFirstFileW, DeleteFileW, GetFileAttributesW,
//            SetFileAttributesW, GetLongPathNameA/W, GetTempPathW, GetDiskFreeSpaceExA/W (cRZFile: every package open)
//   0x1c4830, table 0x10cb614: MoveFileExW, FindFirstFileW (FindFirst 0x1c54dd), FindNextFileW (0x1c5678),
//            CreateDirectoryW, RemoveDirectoryW, GetWindowsDirectoryW, SHGetFolderPathW (the directory scan)
// So the traces above never saw a package open (only the CRT's _stat, access 0x80) or a folder listing. With
// TS2HOST_TRACEFILES set, the image's GetProcAddress hands out the traced versions below for those kernel32 names.
// Level 1: failed opens, every open that asks for write access, and the folder writes (delete, move, copy, mkdir,
// rmdir). Level 2: every open with its handle, the close of each handle opened here, and every listing entry.
SRWLOCK gOpenLock = SRWLOCK_INIT;
std::unordered_set<HANDLE> *gOpenHandles; // handles CreateFileW(proc) opened, at level 2 (never freed: process lifetime)

HANDLE WINAPI TraceProcCreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags,
                                   HANDLE tmpl)
{
    static auto real = Kernel32<decltype(&CreateFileW)>("CreateFileW");
    HANDLE h = real(path, access, share, sa, disposition, flags, tmpl);
    DWORD error = GetLastError();
    bool failed = h == INVALID_HANDLE_VALUE;
    if (failed || gTraceAllOpens || (access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA))) {
        Log("file: CreateFileW(proc) \"%ls\" access 0x%lx share %lu disposition %lu -> %p, error %lu%s", path ? path : L"(null)", access,
            share, disposition, h, failed ? error : 0, failed && error == ERROR_SHARING_VIOLATION ? " (sharing violation)" : "");
    }
    if (!failed && gTraceAllOpens) {
        AcquireSRWLockExclusive(&gOpenLock);
        if (!gOpenHandles) gOpenHandles = new std::unordered_set<HANDLE>;
        gOpenHandles->insert(h);
        ReleaseSRWLockExclusive(&gOpenLock);
    }
    SetLastError(error);
    return h;
}

BOOL WINAPI TraceCloseHandle(HANDLE h)
{
    static auto real = Kernel32<decltype(&CloseHandle)>("CloseHandle");
    bool ours = false;
    AcquireSRWLockExclusive(&gOpenLock);
    if (gOpenHandles) ours = gOpenHandles->erase(h) != 0;
    ReleaseSRWLockExclusive(&gOpenLock);
    std::string path = ours ? HandlePath(h) : std::string();
    BOOL ok = real(h);
    if (ours) {
        DWORD error = GetLastError();
        Log("file: CloseHandle %p \"%s\" -> %d", h, path.c_str(), ok);
        SetLastError(error);
    }
    return ok;
}

template <class R> R LogPathCall(const char *api, LPCWSTR a, LPCWSTR b, R result)
{
    DWORD error = GetLastError();
    Log("file: %s(proc) \"%ls\"%s%ls%s -> %d, error %lu", api, a ? a : L"(null)", b ? " \"" : "", b ? b : L"", b ? "\"" : "",
        static_cast<int>(result), result ? 0 : error);
    SetLastError(error);
    return result;
}

BOOL WINAPI TraceProcDeleteFileW(LPCWSTR path)
{
    static auto real = Kernel32<decltype(&DeleteFileW)>("DeleteFileW");
    return LogPathCall("DeleteFileW", path, nullptr, real(path));
}

BOOL WINAPI TraceProcMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags)
{
    static auto real = Kernel32<decltype(&MoveFileExW)>("MoveFileExW");
    return LogPathCall("MoveFileExW", from, to, real(from, to, flags));
}

BOOL WINAPI TraceProcCopyFileW(LPCWSTR from, LPCWSTR to, BOOL failIfExists)
{
    static auto real = Kernel32<decltype(&CopyFileW)>("CopyFileW");
    return LogPathCall("CopyFileW", from, to, real(from, to, failIfExists));
}

BOOL WINAPI TraceProcCreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES sa)
{
    static auto real = Kernel32<decltype(&CreateDirectoryW)>("CreateDirectoryW");
    return LogPathCall("CreateDirectoryW", path, nullptr, real(path, sa));
}

BOOL WINAPI TraceProcRemoveDirectoryW(LPCWSTR path)
{
    static auto real = Kernel32<decltype(&RemoveDirectoryW)>("RemoveDirectoryW");
    return LogPathCall("RemoveDirectoryW", path, nullptr, real(path));
}

void *TraceProcAddress(const char *fn)
{
    if (std::strcmp(fn, "CreateFileW") == 0) return reinterpret_cast<void *>(&TraceProcCreateFileW);
    if (std::strcmp(fn, "DeleteFileW") == 0) return reinterpret_cast<void *>(&TraceProcDeleteFileW);
    if (std::strcmp(fn, "MoveFileExW") == 0) return reinterpret_cast<void *>(&TraceProcMoveFileExW);
    if (std::strcmp(fn, "CopyFileW") == 0) return reinterpret_cast<void *>(&TraceProcCopyFileW);
    if (std::strcmp(fn, "CreateDirectoryW") == 0) return reinterpret_cast<void *>(&TraceProcCreateDirectoryW);
    if (std::strcmp(fn, "RemoveDirectoryW") == 0) return reinterpret_cast<void *>(&TraceProcRemoveDirectoryW);
    if (gTraceAllOpens && std::strcmp(fn, "FindFirstFileW") == 0) return reinterpret_cast<void *>(&TraceFindFirstFileW);
    if (gTraceAllOpens && std::strcmp(fn, "FindNextFileW") == 0) return reinterpret_cast<void *>(&TraceFindNextFileW);
    return nullptr;
}

// The image's GetProcAddress import (only the image's: the host's own stays the real one, which Kernel32<> uses).
FARPROC WINAPI TraceGetProcAddress(HMODULE module, LPCSTR name)
{
    static auto real = Kernel32<decltype(&GetProcAddress)>("GetProcAddress");
    FARPROC p = real(module, name);
    if (!p || reinterpret_cast<uintptr_t>(name) < 0x10000 || module != GetModuleHandleW(L"kernel32.dll")) return p;
    if (void *traced = TraceProcAddress(name)) {
        Log("trace: the image's GetProcAddress(kernel32, \"%s\") gets the traced version", name);
        return reinterpret_cast<FARPROC>(traced);
    }
    return p;
}

void *TraceImport(const char *fn)
{
    if (!gTraceFiles) return nullptr;
    if (gTraceAllOpens && std::strcmp(fn, "CloseHandle") == 0) return reinterpret_cast<void *>(&TraceCloseHandle);
    if (gTraceAllOpens && std::strcmp(fn, "FindFirstFileA") == 0) return reinterpret_cast<void *>(&TraceFindFirstFileA);
    if (gTraceAllOpens && std::strcmp(fn, "FindNextFileA") == 0) return reinterpret_cast<void *>(&TraceFindNextFileA);
    if (gTraceAllOpens && std::strcmp(fn, "FindFirstFileW") == 0) return reinterpret_cast<void *>(&TraceFindFirstFileW);
    if (gTraceAllOpens && std::strcmp(fn, "FindFirstFileExW") == 0) return reinterpret_cast<void *>(&TraceFindFirstFileExW);
    if (gTraceAllOpens && std::strcmp(fn, "FindNextFileW") == 0) return reinterpret_cast<void *>(&TraceFindNextFileW);
    if (std::strcmp(fn, "LockFile") == 0) return reinterpret_cast<void *>(&TraceLockFile);
    if (std::strcmp(fn, "LockFileEx") == 0) return reinterpret_cast<void *>(&TraceLockFileEx);
    if (std::strcmp(fn, "ReadFile") == 0) return reinterpret_cast<void *>(&TraceReadFile);
    if (std::strcmp(fn, "CreateFileMappingA") == 0) return reinterpret_cast<void *>(&TraceCreateFileMappingA);
    if (std::strcmp(fn, "CreateEventA") == 0) return reinterpret_cast<void *>(&TraceCreateEventA);
    if (std::strcmp(fn, "CreateEventW") == 0) return reinterpret_cast<void *>(&TraceCreateEventW);
    if (std::strcmp(fn, "OpenEventA") == 0) return reinterpret_cast<void *>(&TraceOpenEventA);
    if (std::strcmp(fn, "CreateNamedPipeW") == 0) return reinterpret_cast<void *>(&TraceCreateNamedPipeW);
    if (std::strcmp(fn, "CreateFileA") == 0) return reinterpret_cast<void *>(&TraceCreateFileA);
    if (std::strcmp(fn, "CreateFileW") == 0) return reinterpret_cast<void *>(&TraceCreateFileW);
    return nullptr;
}

LPWSTR WINAPI InstanceGetCommandLineW() { return gCommandLineW.data(); }
LPSTR WINAPI InstanceGetCommandLineA() { return gCommandLineA.data(); }

} // namespace

bool LoadInstanceSettings(const std::wstring &dir)
{
    const std::wstring ini = dir + L"ts2reimpl.ini";
    wchar_t traceLevel[8] = L"";
    gTraceFiles = GetEnvironmentVariableW(L"TS2HOST_TRACEFILES", traceLevel, 8) > 0;
    gTraceAllOpens = gTraceFiles && traceLevel[0] == L'2';
    if (gTraceFiles) Log("TS2HOST_TRACEFILES: failed file opens are logged");
    if (FILE *f = _wfsopen((dir + L"ts2deploy.txt").c_str(), L"r", _SH_DENYNO)) {
        char line[512] = "";
        if (std::fgets(line, sizeof(line), f)) {
            line[std::strcspn(line, "\r\n")] = 0;
            Log("deployed: %s", line);
        }
        std::fclose(f);
    }

    gSteamStub = GetPrivateProfileIntW(L"Steam", L"Stub", 0, ini.c_str()) != 0;
    if (gSteamStub) {
        std::wstring language = IniString(ini, L"Steam", L"Language");
        steamstub::SetLanguage(language.empty() ? "english" : Narrow(language));
        steamstub::SetLogger(&StubLog);
        Log("steam: stub (language \"%s\"%s), docs/contracts/steam.md", steamstub::Language().c_str(),
            steamstub::IsGameLanguage(steamstub::Language()) ? "" : ", not in the game's list: its id will be -1");
    }

    gId = static_cast<int>(GetPrivateProfileIntW(L"Instance", L"Id", 0, ini.c_str()));
    if (gId <= 0) {
        gId = 0;
        return true;
    }
    gUserData = IniString(ini, L"Instance", L"UserDataDir");
    gAllUsers = IniString(ini, L"Instance", L"AllUsersDataDir");
    if (!instance::IsUsableDataDir(gUserData) || (!gAllUsers.empty() && !instance::IsUsableDataDir(gAllUsers))) {
        Log("instance %d: UserDataDir \"%ls\" / AllUsersDataDir \"%ls\" must be absolute, end in a backslash and have no spaces; not starting "
            "(the game would use Documents)", gId, gUserData.c_str(), gAllUsers.c_str());
        return false;
    }
    CreateDirectoryW(gUserData.c_str(), nullptr);
    if (!gAllUsers.empty()) CreateDirectoryW(gAllUsers.c_str(), nullptr);
    gCommandLineW = instance::CommandLineForInstance(GetCommandLineW(), gUserData, gAllUsers);
    gCommandLineA = Narrow(gCommandLineW);
    Log("instance %d: user data %ls, all-users data %ls", gId, gUserData.c_str(), gAllUsers.empty() ? L"(game default)" : gAllUsers.c_str());
    Log("instance %d: game command line: %s", gId, gCommandLineA.c_str());
    return true;
}

void *InstanceImport(const char *dll, const char *fn)
{
    if (reinterpret_cast<uintptr_t>(fn) < 0x10000) return nullptr;
    if (gSteamStub && _stricmp(dll, "steam_api.dll") == 0) return steamstub::Resolve(fn);
    if (_stricmp(dll, "KERNEL32.dll") == 0) {
        if (gTraceFiles && std::strcmp(fn, "GetProcAddress") == 0) return reinterpret_cast<void *>(&TraceGetProcAddress);
        if (void *traced = TraceImport(fn)) return traced;
    }
    if (_stricmp(dll, "ADVAPI32.dll") == 0)
        if (void *traced = TraceRegistryImport(fn)) return traced;
    if (gId > 0 && _stricmp(dll, "KERNEL32.dll") == 0) {
        if (std::strcmp(fn, "CreateMutexA") == 0) return reinterpret_cast<void *>(&InstanceCreateMutexA);
        if (std::strcmp(fn, "GetCommandLineW") == 0) return reinterpret_cast<void *>(&InstanceGetCommandLineW);
        if (std::strcmp(fn, "GetCommandLineA") == 0) return reinterpret_cast<void *>(&InstanceGetCommandLineA);
    }
    return nullptr;
}

void PatchHostImportsForInstance()
{
    if (gId <= 0 && !gTraceFiles) return;
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
    const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int patched = 0;
    for (auto *d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name; ++d) {
        if (_stricmp(reinterpret_cast<const char *>(base + d->Name), "KERNEL32.dll") != 0 || !d->OriginalFirstThunk) continue;
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + d->OriginalFirstThunk);
        auto *slots = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            auto *byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            const char *name = reinterpret_cast<const char *>(byName->Name);
            void *to = gId > 0 && std::strcmp(name, "CreateMutexA") == 0 ? reinterpret_cast<void *>(&InstanceCreateMutexA) : TraceImport(name);
            if (!to) continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, sizeof(DWORD), PAGE_READWRITE, &old);
            slots->u1.Function = reinterpret_cast<DWORD>(to);
            VirtualProtect(&slots->u1.Function, sizeof(DWORD), old, &old);
            patched += to == reinterpret_cast<void *>(&InstanceCreateMutexA);
        }
    }
    if (gId <= 0) return;
    Log("instance %d: host CreateMutexA import %s", gId, patched ? "redirected" : "not found (our cRZMutex would use the shared name)");
}
