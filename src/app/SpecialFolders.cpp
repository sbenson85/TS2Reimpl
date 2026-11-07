#include "app/SpecialFolders.h"

#include "app/AppServices2.h"
#include "app/ExceptionReporter.h"
#include "app/ReportBlockers.h"
#include "game/game.h"
#include "game/import.h"
#include "gz/GZCOM.h"

#include <windows.h>
#include <shlobj.h>

#include <cstring>
#include <cwchar>

namespace folders {

namespace {

// The image's optional shell entry points (wide; null when the module did not have them).
constexpr uint32_t kWindowsDirectoryWGlobal = 0x10cb628;
constexpr uint32_t kFolderPathWGlobal = 0x10cb62c;

// The ANSI imports the image falls back to.
constexpr uint32_t kIatGetWindowsDirectoryA = 0xd9a440;
constexpr uint32_t kIatSHGetSpecialFolderLocation = 0xd9a480; // through the image's thunk 0x859fa0
constexpr uint32_t kIatSHGetPathFromIDListA = 0xd9a47c;
constexpr uint32_t kIatSHGetMalloc = 0xd9a488;

using WindowsDirectoryWFn = UINT(WINAPI *)(wchar_t *, UINT);
using FolderPathWFn = HRESULT(WINAPI *)(HWND, int, HANDLE, DWORD, wchar_t *);

// The buffer size each call is given, in characters (MAX_PATH + 1).
constexpr UINT kBufferChars = 0x105;

// ConvertBytesToString's encodings: the ANSI code page and UTF-16.
constexpr int32_t kEncodingAnsi = -1;
constexpr int32_t kEncodingUtf16 = 0x10;

// The CSIDL pairs of the shell kinds 4..13, each with its fallback.
struct FolderIds {
    int first;
    int second;
};
constexpr FolderIds kPairs[] = {
    {CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY},
    {CSIDL_COMMON_DESKTOPDIRECTORY, CSIDL_DESKTOPDIRECTORY},
    {CSIDL_APPDATA, CSIDL_COMMON_APPDATA},
    {CSIDL_COMMON_APPDATA, CSIDL_APPDATA},
    {CSIDL_PERSONAL, CSIDL_COMMON_DOCUMENTS},
    {CSIDL_COMMON_DOCUMENTS, CSIDL_PERSONAL},
    {CSIDL_MYMUSIC, CSIDL_COMMON_MUSIC},
    {CSIDL_COMMON_MUSIC, CSIDL_MYMUSIC},
    {CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS},
    {CSIDL_COMMON_PROGRAMS, CSIDL_PROGRAMS},
};

constexpr char kRecycledPath[] = "C:\\recycled\\";
constexpr char kFontsFolder[] = "Fonts\\";

void StoreWide(const wchar_t *text, cRZString *out)
{
    ConvertBytesToString(text, static_cast<uint32_t>(std::wcslen(text)), kEncodingUtf16, out->AsInterface());
}

void StoreAnsi(const char *text, cRZString *out)
{
    ConvertBytesToString(text, static_cast<uint32_t>(std::strlen(text)), kEncodingAnsi, out->AsInterface());
}

// The Windows directory.
bool WindowsDirectory(cRZString *out)
{
    if (const auto wide = *GameAddress<WindowsDirectoryWFn>(kWindowsDirectoryWGlobal)) {
        wchar_t buffer[kBufferChars];
        if (wide(buffer, kBufferChars) >= kBufferChars) return false;
        StoreWide(buffer, out);
        return true;
    }
    char buffer[kBufferChars];
    if (ImportFunction<decltype(&GetWindowsDirectoryA), kIatGetWindowsDirectoryA>()(buffer, kBufferChars) >= kBufferChars)
        return false;
    StoreAnsi(buffer, out);
    return true;
}

// A shell folder through SHGetFolderPathW: `first`, else `second`. The buffer is not cleared between the two calls.
bool FolderPathWide(FolderPathWFn folderPath, int first, int second, cRZString *out)
{
    wchar_t buffer[kBufferChars];
    if ((folderPath(nullptr, first, nullptr, 0, buffer) == S_OK && buffer[0] != 0) ||
        (folderPath(nullptr, second, nullptr, 0, buffer) == S_OK && buffer[0] != 0)) {
        StoreWide(buffer, out);
        return true;
    }
    return false;
}

// A shell folder through its item id list (SHGetSpecialFolderLocation, SHGetPathFromIDListA): `first`, else `second`.
// The list is freed with the shell's allocator.
bool FolderPathAnsi(int first, int second, cRZString *out)
{
    const auto location = ImportFunction<decltype(&SHGetSpecialFolderLocation), kIatSHGetSpecialFolderLocation>();
    LPITEMIDLIST list = nullptr;
    if (location(nullptr, first, &list) != S_OK && location(nullptr, second, &list) != S_OK) return false;
    bool found = false;
    char buffer[kBufferChars];
    if (ImportFunction<decltype(&SHGetPathFromIDListA), kIatSHGetPathFromIDListA>()(list, buffer) && buffer[0] != 0) {
        StoreAnsi(buffer, out);
        found = true;
    }
    IMalloc *allocator = nullptr;
    if (ImportFunction<decltype(&SHGetMalloc), kIatSHGetMalloc>()(&allocator) == S_OK) {
        allocator->Free(list);
        allocator->Release();
    }
    return found;
}

// The shell kinds (1, 2 and 4..13).
bool ShellFolder(int first, int second, cRZString *out)
{
    if (const auto wide = *GameAddress<FolderPathWFn>(kFolderPathWGlobal)) return FolderPathWide(wide, first, second, out);
    if (first == CSIDL_BITBUCKET) {
        out->mString.assign(kRecycledPath, sizeof(kRecycledPath) - 1);
        return true;
    }
    if (first == CSIDL_FONTS) {
        if (!GetSpecialFolderPath(kWindows, out, 0)) return false;
        out->mString.append(kFontsFolder, sizeof(kFontsFolder) - 1);
        return true;
    }
    return FolderPathAnsi(first, second, out);
}

} // namespace

uint8_t __cdecl EnsureDirectory(cRZString *path)
{
    if (as2::DirectoryExists(path)) return 1;
    return static_cast<uint8_t>(appbase::CreateDirectories(path));
}

uint8_t __cdecl GetSpecialFolderPath(int32_t kind, cRZString *out, uint8_t ensure)
{
    bool found;
    switch (kind) {
    case kWindows:
        found = WindowsDirectory(out);
        break;
    case kRecycleBin:
        // With SHGetFolderPathW the fallback id is 0 (the desktop).
        found = ShellFolder(CSIDL_BITBUCKET, 0, out);
        break;
    case kFonts:
        found = ShellFolder(CSIDL_FONTS, 0, out);
        break;
    case kExecutable:
        GetModulePath(out->AsInterface());
        appbase::GetModuleDirectory(out->AsInterface(), out->AsInterface());
        found = true;
        break;
    case kDesktop:
    case kCommonDesktop:
    case kAppData:
    case kCommonAppData:
    case kDocuments:
    case kCommonDocuments:
    case kMusic:
    case kCommonMusic:
    case kPrograms:
    case kCommonPrograms:
        found = ShellFolder(kPairs[kind - kDesktop].first, kPairs[kind - kDesktop].second, out);
        break;
    default:
        return 0;
    }
    if (!found) return 0;

    std::string &text = out->mString;
    if (text.empty() || (text.back() != '\\' && text.back() != '/')) text.push_back('\\');
    if (ensure) return EnsureDirectory(out);
    return 1;
}

} // namespace folders
