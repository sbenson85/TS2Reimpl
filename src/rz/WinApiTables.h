#pragma once
// Windows entry points the RZ framework looks up at start-up (lane HQ). Each table or loader is a static object whose
// constructor is an XC entry; while the image is hosted the objects and the globals they fill stay at the image's
// addresses, where game code calls through them. The lookups go through the image's import table (GetVersion,
// GetModuleHandleA, LoadLibraryA, GetProcAddress, FreeLibrary), the calls the originals make. Standalone, each becomes
// a plain static of the code that calls through it.
//
// "wide" below is GetVersion()'s top bit clear: an NT system (always, on anything the game runs on now).
#include "game/owned.h"

#include <cstddef>
#include <cstdint>

namespace rz {

using Proc = void *;

// 0x10cb5e0 (0x34 bytes; XC slot 102, entry 0x138396: `mov ecx, 0x10cb5e0; jmp 0x1c0d9b`): kernel32's wide-character
// file functions (cRZFile opens files through the first) and two ANSI ones looked up on every system.
struct WideFileFunctions {
    static constexpr uint32_t kRva = 0x10cb5e0;
    Proc createFile;          // +0x00 CreateFileW
    Proc copyFile;            // +0x04 CopyFileW
    Proc moveFileEx;          // +0x08 MoveFileExW
    Proc findFirstFile;       // +0x0c FindFirstFileW
    Proc deleteFile;          // +0x10 DeleteFileW
    Proc getFileAttributes;   // +0x14 GetFileAttributesW
    Proc setFileAttributes;   // +0x18 SetFileAttributesW
    Proc getLongPathNameA;    // +0x1c GetLongPathNameA (every system)
    Proc getLongPathName;     // +0x20 GetLongPathNameW
    Proc getTempPath;         // +0x24 GetTempPathW
    Proc getDiskFreeSpaceExA; // +0x28 GetDiskFreeSpaceExA (every system)
    Proc getDiskFreeSpaceEx;  // +0x2c GetDiskFreeSpaceExW
    bool wide;                // +0x30 (the three bytes after it are not written)

    // 0x1c0d9b (thiscall, returns this): kernel32 by GetModuleHandleA, the two ANSI entries, then GetVersion; wide: the
    // W entries in field order, else those fields zero.
    WideFileFunctions *Construct();
};
static_assert(offsetof(WideFileFunctions, getLongPathNameA) == 0x1c);
static_assert(offsetof(WideFileFunctions, getDiskFreeSpaceEx) == 0x2c);
static_assert(offsetof(WideFileFunctions, wide) == 0x30);

// 0x10cb614 (0x20 bytes; XC slot 103, entry 0x1383a0: `mov ecx, 0x10cb614; jmp 0x1c4830`): the wide-character
// directory functions (kernel32's and shell32's SHGetFolderPathW).
struct WideDirectoryFunctions {
    static constexpr uint32_t kRva = 0x10cb614;
    Proc moveFileEx;           // +0x00 MoveFileExW
    Proc findFirstFile;        // +0x04 FindFirstFileW
    Proc findNextFile;         // +0x08 FindNextFileW
    Proc createDirectory;      // +0x0c CreateDirectoryW
    Proc removeDirectory;      // +0x10 RemoveDirectoryW
    Proc getWindowsDirectory;  // +0x14 GetWindowsDirectoryW
    Proc shGetFolderPath;      // +0x18 SHGetFolderPathW (shell32)
    bool wide;                 // +0x1c

    // 0x1c4830 (thiscall, returns this): GetVersion first; wide: GetModuleHandleA of kernel32 and shell32, then the
    // entries in field order; else every field zero.
    WideDirectoryFunctions *Construct();
};
static_assert(offsetof(WideDirectoryFunctions, wide) == 0x1c);

// 0x10caea8 (an empty object; XC slot 93, entry 0x1382ac): loads kernel32 again for TryEnterCriticalSection, which
// cRZCriticalSection::TryEnter calls through the pointer at 0x10caea0 (null: not available). Skipped on Windows 95
// (GetVersion: top bit set, major <= 4, minor 0). The image's exit thunk 0xd90658 frees the module.
struct TryEnterCriticalSectionLoader {
    static constexpr uint32_t kRva = 0x10caea8;
    static constexpr uint32_t kModuleRva = 0x10caea4; // HMODULE, null unless loaded
    static constexpr uint32_t kProcRva = 0x10caea0;   // owned: ts2_rva_10caea0 (src/rz/cRZCriticalSection.h)
    TryEnterCriticalSectionLoader *Construct(); // 0x1af60a (thiscall, returns this)
    static void Unload();                       // the exit thunk: FreeLibrary(module) when loaded (the word is kept)
};

// 0x10caeb4 (an empty object; XC slot 94, entry 0x1382c2): shell32 for ShellExecuteA, called through the pointer at
// 0x10caeac. The image's exit thunk 0xd906b9 frees the module.
struct ShellExecuteLoader {
    static constexpr uint32_t kRva = 0x10caeb4;
    static constexpr uint32_t kModuleRva = 0x10caeb0;
    static constexpr uint32_t kProcRva = 0x10caeac;
    ShellExecuteLoader *Construct(); // 0x1afb32 (thiscall, returns this)
    static void Unload();
};

// The image's import table entries the lookups call through (tests bind them).
namespace winapi {
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114;
constexpr uint32_t kIatGetVersion = 0xd9a11c;
constexpr uint32_t kIatFreeLibrary = 0xd9a120;
constexpr uint32_t kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kIatLoadLibraryA = 0xd9a128;
} // namespace winapi

} // namespace rz

// The wide file function table (owned.h): the image's object in the hooked game (filled by its XC entry, or ours),
// standalone/owned_statics.cpp's otherwise (every supported Windows is NT: kernel32's entries directly).
TS2_OWNED_STATIC(rz::WideFileFunctions, 10cb5e0);

// The wide directory function table (same arrangement; kernel32's entries directly, the shell32 one left null).
TS2_OWNED_STATIC(rz::WideDirectoryFunctions, 10cb614);
