// The size-profile members of src/rz/WinApiTables.h (0x1af60a, 0x1afb32, 0x1c0d9b; lane HQ).
#include "rz/WinApiTables.h"

#include "game/game.h"
#include "game/import.h"
#include "rz/cRZCriticalSection.h"

#include <windows.h>

namespace rz {

namespace {
using GetModuleHandleAFn = HMODULE(WINAPI *)(const char *);
using GetVersionFn = DWORD(WINAPI *)();
using FreeLibraryFn = BOOL(WINAPI *)(HMODULE);
using GetProcAddressFn = Proc(WINAPI *)(HMODULE, const char *);
using LoadLibraryAFn = HMODULE(WINAPI *)(const char *);

Proc Lookup(HMODULE module, const char *name)
{
    return ImportFunction<GetProcAddressFn, winapi::kIatGetProcAddress>()(module, name);
}
} // namespace

WideFileFunctions *WideFileFunctions::Construct()
{
    HMODULE kernel = ImportFunction<GetModuleHandleAFn, winapi::kIatGetModuleHandleA>()("kernel32.dll");
    getLongPathNameA = Lookup(kernel, "GetLongPathNameA");
    getDiskFreeSpaceExA = Lookup(kernel, "GetDiskFreeSpaceExA");
    const bool nt = static_cast<int32_t>(ImportFunction<GetVersionFn, winapi::kIatGetVersion>()()) >= 0;
    if (nt) {
        createFile = Lookup(kernel, "CreateFileW");
        copyFile = Lookup(kernel, "CopyFileW");
        moveFileEx = Lookup(kernel, "MoveFileExW");
        findFirstFile = Lookup(kernel, "FindFirstFileW");
        deleteFile = Lookup(kernel, "DeleteFileW");
        getFileAttributes = Lookup(kernel, "GetFileAttributesW");
        setFileAttributes = Lookup(kernel, "SetFileAttributesW");
        getLongPathName = Lookup(kernel, "GetLongPathNameW");
        getTempPath = Lookup(kernel, "GetTempPathW");
        getDiskFreeSpaceEx = Lookup(kernel, "GetDiskFreeSpaceExW");
    } else {
        createFile = copyFile = moveFileEx = findFirstFile = deleteFile = nullptr;
        getFileAttributes = setFileAttributes = getLongPathName = getTempPath = getDiskFreeSpaceEx = nullptr;
    }
    wide = nt;
    return this;
}

TryEnterCriticalSectionLoader *TryEnterCriticalSectionLoader::Construct()
{
    const DWORD version = ImportFunction<GetVersionFn, winapi::kIatGetVersion>()();
    const bool windows95 = version >= 0x80000000u && static_cast<uint8_t>(version) <= 4 && static_cast<uint8_t>(version >> 8) == 0;
    if (!windows95) {
        HMODULE module = ImportFunction<LoadLibraryAFn, winapi::kIatLoadLibraryA>()("Kernel32.dll");
        *GameAddress<HMODULE>(kModuleRva) = module;
        if (module) ts2_rva_10caea0 = reinterpret_cast<cRZCriticalSection::TryEnterFn>(Lookup(module, "TryEnterCriticalSection"));
    }
    return this;
}

void TryEnterCriticalSectionLoader::Unload()
{
    if (HMODULE module = *GameAddress<HMODULE>(kModuleRva)) ImportFunction<FreeLibraryFn, winapi::kIatFreeLibrary>()(module);
}

ShellExecuteLoader *ShellExecuteLoader::Construct()
{
    HMODULE module = ImportFunction<LoadLibraryAFn, winapi::kIatLoadLibraryA>()("shell32.dll");
    *GameAddress<HMODULE>(kModuleRva) = module;
    if (module) *GameAddress<Proc>(kProcRva) = Lookup(module, "ShellExecuteA");
    return this;
}

void ShellExecuteLoader::Unload()
{
    if (HMODULE module = *GameAddress<HMODULE>(kModuleRva)) ImportFunction<FreeLibraryFn, winapi::kIatFreeLibrary>()(module);
}

} // namespace rz
