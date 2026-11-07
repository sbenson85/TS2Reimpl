#pragma once
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZFrameworkHelpers.h" // not used here; files that include this header rely on it (RZUnknownLayout)
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// An application service the game creates at startup (RVAs 1.18.0.187; docs/startup-map.md 4.5). cTSGlobals, the other
// one, is in src/ts/cTSGlobals.h (standalone shape, lane KE).

// ---- cGZFileSystem (0x1cc bytes) ----
//
// The file-system service: remembers the application, data and plug-in directories and the CD locations. Service id
// 0x2073215d, priority 0x1e8480.
//
// Standalone shape (docs/standalone-shape.md; lane KU, from the hook shape of lanes before): RTTI bases cRZSystemService
// at +0 (cIGZSystemService +0, cRZUnknownMT +4) and cIGZFileSystem at +0x1c. Game vtables 0xea04e4 (15 slots: the
// service's 13, then ResolveApplicationPaths and CheckCDDrive), 0xea0524 (6; slot 3 the deleting destructor thunk
// 0xc75e8c into 0xc75e94) and 0xea0540 (23). The slot bodies live in three files (their status groups): this group's
// src/app/AppServices.size.cpp, AppServices2.size.cpp (ResolveApplicationPaths, FindCDDrive, the directory setters) and
// IniFile.cpp (DataDirectory, PlugInDirectory).
//
// Creation: the constructor 0xc75ccd is called only by cTSAppFW::AddApplicationServicesHere 0xc267fb (ours, which builds
// through the constructor adapter); the game's vtables are referenced only by that constructor and the destructor
// 0xc75d9b. With the constructor hook on, every file system carries our vtables.
//
// The three bytes at +0x20..+0x22 say whether the data, plug-in and application directory have been set; the getters
// call slot 13 of the primary vtable (ResolveApplicationPaths) when they are unset. +0x104 says whether the CD paths have
// been resolved.

// cIGZFileSystem (iid 0xe0203660; only cGZFileSystem implements it). Slot names from db/interface-slots.jsonl.
class cIGZFileSystem : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xe0203660;
    // Slots 3..12: the text of a cached path, looked up first when it has not been yet.
    virtual const char *AppDrive() = 0;          // slot 3, 0xc75ee5
    virtual const char *AppDirectory() = 0;      // slot 4, 0xc75ec3
    virtual const char *AppName() = 0;           // slot 5, 0xc75f29
    virtual const char *AppExtension() = 0;      // slot 6, 0xc75f07
    virtual const char *DataDirectory() = 0;     // slot 7, 0xc75ff9
    virtual const char *PlugInDirectory() = 0;   // slot 8, 0xc763f0
    virtual const char *CDAppDrive() = 0;        // slot 9, 0xc75f6c
    virtual const char *CDAppDirectory() = 0;    // slot 10, 0xc75f4b
    virtual const char *CDDataDirectory() = 0;   // slot 11, 0xc75fb7
    virtual const char *CDPlugInDirectory() = 0; // slot 12, 0xc75fd8
    virtual bool FindCDDrive() = 0;              // slot 13, 0xc76196 (al only)
    virtual void UseCDIniFile(uint8_t value) = 0;           // slot 14, 0xc766f5 (ret 4; the raw byte stored)
    virtual void AllowRecursiveCDSearch(uint8_t value) = 0; // slot 15, 0xc75eb6 (ret 4)
    // Slots 16, 17 and 22 (ret 4): the string's own slot 16 (operator=) on the argument, whose result is returned.
    virtual void *SetExpectedCDName(const cIGZString *text) = 0;    // slot 16, 0xc7669c
    virtual void *SetExpectedCDAppPath(const cIGZString *text) = 0; // slot 17, 0xc76691
    // Slots 18..20 (ret 4): the directory assigned, then marked set when it exists (al only).
    virtual bool SetAppDirectory(const cIGZString *path) = 0;     // slot 18, 0xc76611
    virtual bool SetDataDirectory(const cIGZString *path) = 0;    // slot 19, 0xc7664e
    virtual bool SetPlugInDirectory(const cIGZString *path) = 0;  // slot 20, 0xc766a7
    // Slot 21 (ret 4): dest's slot 3 (FromChar(text, size)) with the plug-in directory name; its eax is returned.
    virtual uint32_t GetPluginDirectoryName(cIGZString *dest) = 0; // slot 21, 0xc763cb
    virtual void *SetPluginDirectoryName(const cIGZString *text) = 0; // slot 22, 0xc766ea
};

class cGZFileSystem : public rz::cRZSystemService, public cIGZFileSystem {
public:
    static constexpr uint32_t kVTable = 0xea04e4; // the game's, for tests
    static constexpr uint32_t kUnknownVTable = 0xea0524;
    static constexpr uint32_t kFileSystemVTable = 0xea0540;
    static constexpr uint32_t kSize = 0x1cc;
    static constexpr uint32_t kServiceId = 0x2073215d;
    static constexpr int32_t kPriority = 0x1e8480;
    static constexpr uint32_t kInterfaceId = cIGZFileSystem::kIid;
    static constexpr uint32_t kPluginDirectoryNameGlobal = 0x1085c18; // a const char * the constructor copies
    static constexpr uint32_t kFace = 0x1c;

    cGZFileSystem();                    // 0xc75ccd
    ~cGZFileSystem() override;          // 0xc75d9b; deleting 0xc75e94 (+4 thunk 0xc75e8c)
    cGZFileSystem(const cGZFileSystem &) = delete;
    cGZFileSystem &operator=(const cGZFileSystem &) = delete;

    // cIGZUnknown (every face; the +4 and +0x1c ones through the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0xc765d7 (thunks 0xc76601, 0xc76609)
    uint32_t AddRef() override;                              // folded 0x1cc320 (cRZSystemService's)
    uint32_t Release() override;                             // folded 0x1cd7a0

    // The primary vtable's own slots
    // Slot 13 (0xc76294, AppServices2.size.cpp): the executable's path split into the application strings.
    virtual void ResolveApplicationPaths();
    // Slot 14 (0xc76702): whether the CD drive just assigned to mCDAppDrive is the game's; on success the CD
    // directories are set from it (Maxis.ini on the drive with mUseCDIniFile, else a search by volume name, path or
    // file with mAllowRecursiveCDSearch). al only.
    virtual bool CheckCDDrive();

    // cIGZFileSystem
    const char *AppDrive() override;
    const char *AppDirectory() override;
    const char *AppName() override;
    const char *AppExtension() override;
    const char *DataDirectory() override;   // IniFile.cpp
    const char *PlugInDirectory() override; // IniFile.cpp
    const char *CDAppDrive() override;
    const char *CDAppDirectory() override;
    const char *CDDataDirectory() override;
    const char *CDPlugInDirectory() override;
    bool FindCDDrive() override;            // AppServices2.size.cpp
    void UseCDIniFile(uint8_t value) override;
    void AllowRecursiveCDSearch(uint8_t value) override;
    void *SetExpectedCDName(const cIGZString *text) override;
    void *SetExpectedCDAppPath(const cIGZString *text) override;
    bool SetAppDirectory(const cIGZString *path) override;    // AppServices2.size.cpp
    bool SetDataDirectory(const cIGZString *path) override;   // AppServices2.size.cpp
    bool SetPlugInDirectory(const cIGZString *path) override; // AppServices2.size.cpp
    uint32_t GetPluginDirectoryName(cIGZString *dest) override;
    void *SetPluginDirectoryName(const cIGZString *text) override;

    // The strings are hook-shaped cRZString members (game vtable 0xd9b470), built and destroyed by their own
    // Construct / Destruct in the constructor and destructor (hence the unions).
    uint8_t mDataDirectorySet;    // 0x20
    uint8_t mPlugInDirectorySet;  // 0x21
    uint8_t mAppDirectorySet;     // 0x22
    uint8_t pad23;                // never written
    union { cRZString mAppDrive; };          // 0x24
    union { cRZString mAppDirectory; };      // 0x44
    union { cRZString mAppName; };           // 0x64
    union { cRZString mAppExtension; };      // 0x84
    union { cRZString mDataDirectory; };     // 0xa4
    union { cRZString mPlugInDirectory; };   // 0xc4
    union { cRZString mPluginDirectoryName; }; // 0xe4 starts as the text of the global
    uint8_t mCDResolved;          // 0x104
    uint8_t pad105[3];
    union { cRZString mCDAppDrive; };        // 0x108
    union { cRZString mCDAppDirectory; };    // 0x128
    union { cRZString mCDDataDirectory; };   // 0x148
    union { cRZString mCDPlugInDirectory; }; // 0x168
    union { cRZString mExpectedCDName; };    // 0x188 (the volume name CheckCDDrive looks for)
    union { cRZString mExpectedCDAppPath; }; // 0x1a8 (a file CheckCDDrive looks for, relative to the drive)
    uint8_t mUseCDIniFile;           // 0x1c8
    uint8_t mAllowRecursiveCDSearch; // 0x1c9
    uint8_t pad1ca[2];
};
static_assert(sizeof(cGZFileSystem) == cGZFileSystem::kSize, "cGZFileSystem layout");
static_assert(offsetof(cGZFileSystem, mDataDirectorySet) == 0x20, "cGZFileSystem layout");
static_assert(offsetof(cGZFileSystem, mAppDrive) == 0x24, "cGZFileSystem layout");
static_assert(offsetof(cGZFileSystem, mPluginDirectoryName) == 0xe4, "cGZFileSystem layout");
static_assert(offsetof(cGZFileSystem, mCDResolved) == 0x104, "cGZFileSystem layout");
static_assert(offsetof(cGZFileSystem, mCDAppDrive) == 0x108, "cGZFileSystem layout");
static_assert(offsetof(cGZFileSystem, mExpectedCDAppPath) == 0x1a8, "cGZFileSystem layout");
static_assert(offsetof(cGZFileSystem, mUseCDIniFile) == 0x1c8, "cGZFileSystem layout");

// The hooks' entry points (docs/standalone-shape.md, step 6): the constructor's, and the slots recorded as byte matches
// (one-statement adapters, so matchcheck ties them to the member our vtable holds).
struct cGZFileSystemAdapters {
    static cGZFileSystem *__fastcall Construct(void *memory, void *edx);                           // 0xc75ccd
    static void __fastcall UseCDIniFile(cIGZFileSystem *face, void *edx, uint8_t value);            // 0xc766f5
    static void __fastcall AllowRecursiveCDSearch(cIGZFileSystem *face, void *edx, uint8_t value);  // 0xc75eb6
    static void *__fastcall SetExpectedCDName(cIGZFileSystem *face, void *edx, const cIGZString *text);    // 0xc7669c
    static void *__fastcall SetExpectedCDAppPath(cIGZFileSystem *face, void *edx, const cIGZString *text); // 0xc76691
    static void *__fastcall SetPluginDirectoryName(cIGZFileSystem *face, void *edx, const cIGZString *text); // 0xc766ea
};
