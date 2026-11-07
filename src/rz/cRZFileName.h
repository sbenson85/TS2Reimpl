#pragma once
#include "rz/cRZString.h"

#include <cstdint>

// A file path split into its parts: five cRZString members, no vtable of its own (the class has no RTTI;
// the name is inferred from its use: drive / directory / name / extension plus the assembled path).
// Built from a path by resolving it to an absolute one (inifile::ResolvePathString) and splitting that.
// The getters normalise their part in place (a drive becomes "X:", a directory gets a leading and
// trailing separator, an extension a leading dot) before handing it out.
//
//   +0x00 mDrive   "X:" or empty           +0x60 mExt    ".ext" or empty
//   +0x20 mDir     "\dir\sub\" or empty    +0x80 mFull   the whole path, rebuilt by Assemble()
//   +0x40 mName    file name without ext
class cRZFileName {
public:
    cRZFileName *Construct(const cRZString *path); // 0x1c7ef4 (5 string constructors, then Init)
    cRZFileName *ConstructEmpty();                 // 0x1c7f30 (the default constructor: 5 empty strings; cRZFileName.cpp)
    void Destruct();                               // 0x1c0eb2 (the five strings, last first)
    bool Init(const cRZString *path);              // 0x1c8bb8 (always true)

    cRZString *GetDrive();     // 0x1c87ba: normalises the drive (returns this)
    cRZString *GetDirectory(); // 0x1c87ac
    cRZString *GetName();      // 0x1c8800: plain accessor, shared with other classes (4 bytes, not hookable)
    cRZString *GetExtension(); // 0x1c87ed

    // 0x1c898e: mFull = the parts selected by `mask` concatenated (1 drive, 2 directory, 4 name, 8
    // extension; only the low byte of the stack argument is used), each part normalised on the way.
    void Assemble(uint32_t mask);
    // 0x1c882a: Assemble(0xf), then out is constructed as a copy of mFull. Returns out. out is raw memory.
    cRZString *GetFullPath(cRZString *out);
    // 0x1c87c7: the same with Assemble(3): drive and directory only.
    cRZString *GetDrivePath(cRZString *out);

    // 0x1c8936: the name is not empty and the assembled path is a canonical absolute one.
    bool IsAbsoluteFile();
    // 0x1c88cd: directory not empty, name and extension empty, assembled path canonical absolute.
    bool IsAbsoluteDirectory();

    // 0x1c86fe / 0x1c86a1 / 0x1c875f: the in-place normalisers behind the getters (__stdcall, one argument).
    static void __stdcall NormaliseDrive(cRZString *drive);
    static void __stdcall NormaliseDirectory(cRZString *dir);
    static void __stdcall NormaliseExtension(cRZString *ext);

    // 0x1c39bc: appbase::SplitPath (0x1c3a97, in app/ExceptionReporter) on full's text, assigning each non-null part string. __cdecl.
    static void __cdecl SplitInto(const cRZString *full, cRZString *drive, cRZString *dir, cRZString *name, cRZString *ext);

    // 0x1c0f3b: replaces every '\\' and '/' in the string with `separator`, then appends `separator`
    // unless the string now ends in '\\' or '/'. __cdecl, no meaningful return value.
    static void __cdecl NormaliseSeparators(cRZString *path, char separator);

    cRZString mDrive; // +0x00
    cRZString mDir;   // +0x20
    cRZString mName;  // +0x40
    cRZString mExt;   // +0x60
    cRZString mFull;  // +0x80
};

static_assert(sizeof(cRZFileName) == 0xa0, "cRZFileName layout: five cRZStrings");
static_assert(offsetof(cRZFileName, mFull) == 0x80, "cRZFileName layout: full path at 0x80");
