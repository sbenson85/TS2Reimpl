#pragma once
#include "app/AppServices.h" // cGZFileSystem
#include "game/owned.h"
#include "rz/Singleton_20ddfb.h" // ScopedRef
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <string>

// Second batch of the application services (group AppServices2): the testing-cheats flag of
// cTSGlobals and the string / path helpers behind cGZFileSystem's setters. RVAs are 1.18.0.187.

namespace as2 {

// ---- string and allocator helpers ----

// The layout of the library's std::string (24 bytes): the code below tidies strings it did not build
// itself.
struct RawString {
    union {
        char mBuf[16];
        char *mPtr;
    };
    uint32_t mSize;     // +0x10
    uint32_t mCapacity; // +0x14 (15 while the text lives in mBuf)
};
static_assert(sizeof(RawString) == 24, "std::string layout");

// 0x1bf84e (__stdcall, ret 8; a second copy of FreeThroughAllocatorService at 0x197100 with the same
// guard 0x10c95d8 and cached service 0x10c95d4): frees `mem` through slot 4 of the allocator
// service, fetching the service (GetAllocatorService) on first use. The second argument is unused.
// Unlike the 0x197100 copy it has no handler: if the fetch throws, the guard is not aborted.
void __stdcall FreeViaAllocatorService(void *mem, uint32_t unused);

// 0x1bf795 (__stdcall, ret 8; the allocation twin of FreeViaAllocatorService, same guard and cache):
// returns slot 3 of the allocator service called with `size`. The second argument (0 from the
// string code) is unused.
void *__stdcall AllocateViaAllocatorService(uint32_t size, uint32_t unused);

// 0x1be72c (__thiscall, ret 8): assign(text, length) of the strings whose storage comes from the allocator
// service (the layout of std::string, but the game's strings of this kind allocate with 0x1bf795 and
// free with 0x1bf84e). Starts from whatever is in the object: up to 15 characters are copied in place
// (size, capacity 15), more get a buffer of the grown capacity + 1 and the capacity is stored. Throws
// length_error above 0x7fffffff characters, before touching the object.
void __fastcall AssignServiceString(RawString *str, void *edx, const char *text, uint32_t length);

// 0x1bf753 (__thiscall, no arguments): the string tidy that frees a heap buffer (capacity > 15)
// through FreeViaAllocatorService(buffer, capacity + 1) and leaves the string empty with capacity 15.
void __fastcall TidyString(RawString *str, void *edx);

// 0x1cbace (__thiscall, no arguments): stores null in ref->mObject, then asks the service object
// (game global 0x10c9378) for one through its slot 5 with the identifiers 0x0ac1887e, 0x6bb22f38
// and the output pointer. Returns ref.
ScopedRef *__fastcall AcquireTestingService(ScopedRef *ref, void *edx);

// ---- cTSGlobals: the testing-cheats flag ----
//
// Both look the flag up in an object found through a function-local static (guards 0x10fdfdc and
// 0x10fdfe4 with the pointers at 0x10fdfd8 and 0x10fdfe0, both initialised from
// AcquireTestingService and released with ScopedRef::ReleaseObject). With the object, they use its
// slot 3 (returns a property-set object) and the name "testingCheatsEnabled".

// Slot 31 of the primary vtable (0xc2cba3, __thiscall, no arguments, `self` unused). Returns false
// (only al is defined) when there is no object or the property-set's slot 10 (name) says no;
// otherwise asks slot 3 of the object again for slot 9 (name, AnyBoolean out) and returns the
// boolean.
uint32_t __fastcall TestingCheatsEnabled(void *self, void *edx);

// Slot 56 (0xc2c9f6, ret 4): with the object, calls slot 3 of the property-set (name, AnyBoolean
// holding the low byte of `value`).
void __fastcall SetTestingCheatsEnabled(void *self, void *edx, uint32_t value);

// ---- file search and directory checks ----

// What a successful search returns (0x48 bytes, built by 0x1c54dd): the found name (UTF-8), whether
// it is a directory, a copy of the search pattern, and the search handle.
struct FindNode {
    cRZString mName;      // 0x00
    uint8_t mIsDirectory; // 0x20 (bit 4 of dwFileAttributes)
    uint8_t pad21[3];
    cRZString mPattern;   // 0x24
    void *mHandle;        // 0x44 from FindFirstFile
};
static_assert(sizeof(FindNode) == 0x48, "FindNode layout");
static_assert(offsetof(FindNode, mPattern) == 0x24, "FindNode layout");
static_assert(offsetof(FindNode, mHandle) == 0x44, "FindNode layout");

// 0x1b0356 (__cdecl): converts the text of `src` from `srcEncoding` (-2 default, -1 system code page)
// to the string encoding (UTF-8) and stores it in `dst` (slot 3, FromChar). The same encoding is a
// plain copy (nothing when src == dst). UTF-16 is refused. Returns whether everything converted;
// only al is defined.
bool __cdecl ConvertStringFrom(const cIGZString *src, int32_t srcEncoding, cIGZString *dst);

// 0x1c53c2 (__cdecl): the UTF-16 form of `src` (units counted as six per UTF-8 byte), built in
// the raw memory `out` (a std::u16string); returns out.
std::u16string *__cdecl StringToUtf16(void *out, const cIGZString *src);

// 0x1c54dd (__cdecl): FindFirstFile on the text of the pattern string (a cRZString: its std::string is read
// directly). Through the function pointer in game global 0x10cb618 as wide text when it is set (the
// game never sets it), else FindFirstFileA (import slot) on the text converted to the system code
// page. Returns null when nothing is found.
FindNode *__cdecl FindFirst(cRZString *pattern);

// 0x1c49cb (__thiscall): destroys the two strings of the node, the pattern first.
void __fastcall DestructFindNode(FindNode *node, void *edx);

// 0x1c54b2 (__cdecl): FindClose on the handle (unless it is -1), destroys the node and frees it
// (sized delete, 0x48). Null is ignored.
void __cdecl CloseFind(FindNode *node);

// 0x1c4f79 (__cdecl): true when FindFirst finds something (the node is closed again). Only al is defined.
bool __cdecl PatternMatches(cRZString *pattern);

// 0x1c4f94 (__cdecl): copies the path, makes sure it ends in a backslash or slash (appending a
// backslash), appends the text the game global 0xff2478 points to ("*") and returns PatternMatches
// of it: whether the directory exists and has entries. Only al is defined.
bool __cdecl DirectoryExists(cRZString *path);

// ---- cGZFileSystem: the application path lookup and the CD search ----

// 0x1b1454 (__cdecl): the path of the running executable (GetModulePath, 0x1b139f) copied to `buffer`
// with a terminator when it is shorter than `capacity`; returns its length, or 0 (buffer untouched)
// when it does not fit.
uint32_t __cdecl CopyModulePath(char *buffer, uint32_t capacity);

// The members of cGZFileSystem (src/app/AppServices.h) defined in AppServices2.size.cpp:
// - ResolveApplicationPaths, slot 13 of the primary vtable (0xc76294): splits the executable's path into drive,
//   directory, name and extension and stores them in the application strings. The drive string gets a backslash
//   appended unless it already ends in one or a slash; the directory string is drive + directory (without that added
//   backslash). Marks the application directory as set.
// - FindCDDrive, slot 13 of the cIGZFileSystem face (0xc76196): walks the logical drives (GetLogicalDriveStringsA,
//   GetDriveTypeA through import slots) and, for CD-ROM drives (and removable ones from "c" on), assigns the drive text to
//   the CD application drive and asks slot 14 of the primary vtable (CheckCDDrive) whether the game is there. Without a
//   match the CD paths are copies of the application ones. Marks the CD paths resolved. Returns whether a match was
//   found (al only).
// - SetAppDirectory 0xc76611 (string +0x44, flag +0x22), SetDataDirectory 0xc7664e (+0xa4, +0x20), SetPlugInDirectory
//   0xc766a7 (+0xc4, +0x21), cIGZFileSystem slots 18..20 (ret 4): assign the text of `path` (its slot 7 for the length,
//   slot 6 for the text) to the directory string, check it with DirectoryExists and, if it exists, mark the directory as
//   set. Return that result (only al is defined).
bool SetDirectory(cRZString &directory, uint8_t &flag, const cIGZString *path);

} // namespace as2

// Data the directory helpers read (owned.h). In the hooked game these are the image's: the two import address table slots
// (kernel32's FindFirstFileA and FindClose, the slots the original's calls read; tests point them at fakes) and the
// pointer to the wildcard text "*". Image-free, standalone/owned_statics.cpp defines them (the slots hold kernel32's own
// entries).
TS2_OWNED_STATIC(void *, d9a1f8);      // FindFirstFileA
TS2_OWNED_STATIC(void *, d9a1f4);      // FindClose
TS2_OWNED_STATIC(const char *, ff2478); // "*"
