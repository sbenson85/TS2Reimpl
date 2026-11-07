// The speed-profile member of src/rz/WinApiTables.h (0x1c4830; lane HQ).
#include "rz/WinApiTables.h"

#include "game/game.h"
#include "game/import.h"

#include <windows.h>

namespace rz {

WideDirectoryFunctions *WideDirectoryFunctions::Construct()
{
    using GetModuleHandleAFn = HMODULE(WINAPI *)(const char *);
    using GetProcAddressFn = Proc(WINAPI *)(HMODULE, const char *);
    const bool nt = static_cast<int32_t>(ImportFunction<DWORD(WINAPI *)(), winapi::kIatGetVersion>()()) >= 0;
    if (nt) {
        auto moduleHandle = ImportFunction<GetModuleHandleAFn, winapi::kIatGetModuleHandleA>();
        HMODULE kernel = moduleHandle("kernel32.dll");
        HMODULE shell = moduleHandle("shell32.dll");
        auto lookup = ImportFunction<GetProcAddressFn, winapi::kIatGetProcAddress>();
        moveFileEx = lookup(kernel, "MoveFileExW");
        findFirstFile = lookup(kernel, "FindFirstFileW");
        findNextFile = lookup(kernel, "FindNextFileW");
        createDirectory = lookup(kernel, "CreateDirectoryW");
        removeDirectory = lookup(kernel, "RemoveDirectoryW");
        getWindowsDirectory = lookup(kernel, "GetWindowsDirectoryW");
        shGetFolderPath = lookup(shell, "SHGetFolderPathW");
    } else {
        moveFileEx = findFirstFile = findNextFile = createDirectory = removeDirectory = getWindowsDirectory = nullptr;
        shGetFolderPath = nullptr;
    }
    wide = nt;
    return this;
}

} // namespace rz
