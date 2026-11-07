#pragma once
// The shell's special folders and "make sure a directory exists" (foundations campaign F4, lane LY): two free __cdecl
// helpers of the file system, ownerless in the image (between DirectoryScan's members and the path helpers).
#include "rz/cRZString.h"

#include <cstdint>

namespace folders {

// The folder kinds of 0x1c50d6. The paired kinds name a user folder and its all-users counterpart; each asks the shell
// for its own CSIDL first and the other one's when that fails.
enum Kind : int32_t {
    kWindows = 0,         // the Windows directory
    kRecycleBin = 1,      // CSIDL_BITBUCKET; without SHGetFolderPathW, "C:\recycled\"
    kFonts = 2,           // CSIDL_FONTS; without SHGetFolderPathW, the Windows directory's "Fonts\"
    kExecutable = 3,      // the running executable's directory
    kDesktop = 4,         // CSIDL_DESKTOPDIRECTORY, else CSIDL_COMMON_DESKTOPDIRECTORY
    kCommonDesktop = 5,   // the reverse
    kAppData = 6,         // CSIDL_APPDATA, else CSIDL_COMMON_APPDATA
    kCommonAppData = 7,   // the reverse
    kDocuments = 8,       // CSIDL_PERSONAL, else CSIDL_COMMON_DOCUMENTS
    kCommonDocuments = 9, // the reverse
    kMusic = 10,          // CSIDL_MYMUSIC, else CSIDL_COMMON_MUSIC
    kCommonMusic = 11,    // the reverse
    kPrograms = 12,       // CSIDL_PROGRAMS, else CSIDL_COMMON_PROGRAMS
    kCommonPrograms = 13, // the reverse
};

// 0x1c5031 (__cdecl): true when the directory exists (DirectoryExists, 0x1c4f94), else CreateDirectories' (0x1c4caf)
// result. Only al is defined.
uint8_t __cdecl EnsureDirectory(cRZString *path);

// 0x1c50d6 (__cdecl): the special folder `kind` into `out`, ending in a separator ('\' appended unless the text ends in
// '\' or '/'); with `ensure`, the result is then EnsureDirectory's. False for an unknown kind or when the shell has no
// answer (out is then left as the failed calls left it). The shell is asked through two optional function pointers
// the image keeps (0x10cb628: GetWindowsDirectoryW, 0x10cb62c: SHGetFolderPathW, both wide; null when not found), else
// through the ANSI imports. Only al is defined.
uint8_t __cdecl GetSpecialFolderPath(int32_t kind, cRZString *out, uint8_t ensure);

} // namespace folders
