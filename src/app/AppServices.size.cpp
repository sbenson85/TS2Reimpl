#include "app/AppServices.h"

#include "app/ExceptionReporter.h" // GetModuleDirectory
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZApp.h"
#include "gz/cIGZFrameWork.h"
#include "rz/DirectoryScan.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"

#include <cstring>
#include <list>
#include <new>
#include <string>

// ---- cGZFileSystem ----

cGZFileSystem::cGZFileSystem() : rz::cRZSystemService(kServiceId, kPriority)
{
    // A word write to +0x20 and a byte write to +0x22; +0x23 is left alone.
    mDataDirectorySet = 0;
    mPlugInDirectorySet = 0;
    mAppDirectorySet = 0;
    mAppDrive.Construct();
    mAppDirectory.Construct();
    mAppName.Construct();
    mAppExtension.Construct();
    mDataDirectory.Construct();
    mPlugInDirectory.Construct();
    mPluginDirectoryName.Construct(*GameAddress<const char *>(kPluginDirectoryNameGlobal));
    mCDResolved = 0;
    mCDAppDrive.Construct();
    mCDAppDirectory.Construct();
    mCDDataDirectory.Construct();
    mCDPlugInDirectory.Construct();
    mExpectedCDName.Construct();
    mExpectedCDAppPath.Construct();
    mUseCDIniFile = 0;
    mAllowRecursiveCDSearch = 0;
}

cGZFileSystem::~cGZFileSystem()
{
    mExpectedCDAppPath.Destruct();
    mExpectedCDName.Destruct();
    mCDPlugInDirectory.Destruct();
    mCDDataDirectory.Destruct();
    mCDAppDirectory.Destruct();
    mCDAppDrive.Destruct();
    mPluginDirectoryName.Destruct();
    mPlugInDirectory.Destruct();
    mDataDirectory.Destruct();
    mAppExtension.Destruct();
    mAppName.Destruct();
    mAppDirectory.Destruct();
    mAppDrive.Destruct();
}

bool cGZFileSystem::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != kInterfaceId) return rz::cRZSystemService::QueryInterface(riid, ppv);
    *ppv = static_cast<cIGZFileSystem *>(this); // no check on ppv
    AddRef();
    return true;
}

uint32_t cGZFileSystem::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZFileSystem::Release() { return rz::cRZSystemService::Release(); }

const char *cGZFileSystem::AppDirectory()
{
    if (!mAppDirectorySet) {
        ResolveApplicationPaths();
        mAppDirectorySet = 1;
    }
    return mAppDirectory.mString.c_str();
}

const char *cGZFileSystem::AppDrive()
{
    if (!mAppDirectorySet) {
        ResolveApplicationPaths();
        mAppDirectorySet = 1;
    }
    return mAppDrive.mString.c_str();
}

const char *cGZFileSystem::AppExtension()
{
    if (!mAppDirectorySet) {
        ResolveApplicationPaths();
        mAppDirectorySet = 1;
    }
    return mAppExtension.mString.c_str();
}

const char *cGZFileSystem::AppName()
{
    if (!mAppDirectorySet) {
        ResolveApplicationPaths();
        mAppDirectorySet = 1;
    }
    return mAppName.mString.c_str();
}

const char *cGZFileSystem::CDAppDirectory()
{
    if (!mCDResolved) CDAppDrive();
    return mCDAppDirectory.mString.c_str();
}

const char *cGZFileSystem::CDAppDrive()
{
    if (!mCDResolved) {
        if (!mAppDirectorySet) ResolveApplicationPaths(); // the flag is not set here
        if (!mDataDirectorySet) DataDirectory();
        if (!mPlugInDirectorySet) PlugInDirectory();
        FindCDDrive();
    }
    return mCDAppDrive.mString.c_str();
}

const char *cGZFileSystem::CDDataDirectory()
{
    if (!mCDResolved) CDAppDrive();
    return mCDDataDirectory.mString.c_str();
}

const char *cGZFileSystem::CDPlugInDirectory()
{
    if (!mCDResolved) CDAppDrive();
    return mCDPlugInDirectory.mString.c_str();
}

uint32_t cGZFileSystem::GetPluginDirectoryName(cIGZString *dest)
{
    const std::string &name = mPluginDirectoryName.mString;
    // dest's slot 3, FromChar(text, size); the original tail-jumps, so its eax comes back.
    return VirtualCall<uint32_t(__thiscall *)(void *, const char *, uint32_t), 3>(dest, name.c_str(),
                                                                                    static_cast<uint32_t>(name.size()));
}

void cGZFileSystem::AllowRecursiveCDSearch(uint8_t value)
{
    mAllowRecursiveCDSearch = value;
}

void cGZFileSystem::UseCDIniFile(uint8_t value)
{
    mUseCDIniFile = value;
}

namespace {

// The string member's own slot 16 (operator=), through its vtable.
void *AssignThroughSlot16(cRZString &s, const void *text)
{
    return VirtualCall<void *(__thiscall *)(void *, const void *), 16>(&s, text);
}

} // namespace

void *cGZFileSystem::SetExpectedCDAppPath(const cIGZString *text)
{
    return AssignThroughSlot16(mExpectedCDAppPath, text);
}

void *cGZFileSystem::SetExpectedCDName(const cIGZString *text)
{
    return AssignThroughSlot16(mExpectedCDName, text);
}

void *cGZFileSystem::SetPluginDirectoryName(const cIGZString *text)
{
    return AssignThroughSlot16(mPluginDirectoryName, text);
}

// ---- CheckCDDrive (slot 14) ----

namespace {

// A cRZString on the stack, built and destroyed by the hook-shaped members.
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    explicit LocalString(const char *text) { s.Construct(text); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// A directory scan on the stack: our ::DirectoryScan (src/rz/DirectoryScan.h; lane LY switched it from the original
// callees), built and destroyed through its adapters, the addressable entry points tests stand in for.
struct LocalScan {
    alignas(::DirectoryScan) unsigned char raw[sizeof(::DirectoryScan)];
    ::DirectoryScan *scan;
    explicit LocalScan(const cRZString *directory)
        : scan(DirectoryScanAdapters::Construct(raw, nullptr, directory, false, false, false))
    {
    }
    ~LocalScan() { DirectoryScanAdapters::Destruct(scan, nullptr); }
    ::DirectoryScan *operator->() const { return scan; }
    LocalScan(const LocalScan &) = delete;
    LocalScan &operator=(const LocalScan &) = delete;
};

// 0x1c20d8 (cdecl): the volume name of the drive `root` into `out` (GetVolumeInformationA); al.
bool VolumeName(const cRZString *root, cRZString *out)
{
    return OriginalFunction<bool(__cdecl *)(const cRZString *, cRZString *), 0x1c20d8>()(root, out);
}

} // namespace

bool cGZFileSystem::CheckCDDrive()
{
    if (mCDAppDrive.mString.empty()) return false;
    if (mUseCDIniFile) {
        // "<drive>Maxis.ini": [<application name>] App= / Data= / PlugIn=, each appended to the drive.
        LocalString iniPath;
        iniPath.s.mString = mCDAppDrive.mString + "Maxis.ini";
        if (!cRZFile::PathExists(*iniPath.s.AsInterface())) return false;
        cIGZFrameWork *framework = reinterpret_cast<cIGZFrameWork *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
        LocalString section(framework->Application()->ModuleName());
        LocalString value;
        if (section.s.mString.empty()) return false;
        alignas(cRZIniFile) unsigned char iniRaw[0x40];
        cRZIniFile &ini = *reinterpret_cast<cRZIniFile *>(iniRaw);
        ini.Construct(*iniPath.s.AsInterface());
        struct Entry {
            const char *key;
            cRZString *target;
        };
        const Entry entries[] = {{"App", &mCDAppDirectory}, {"Data", &mCDDataDirectory}, {"PlugIn", &mCDPlugInDirectory}};
        for (const Entry &entry : entries) {
            bool found;
            {
                LocalString key(entry.key);
                found = ini.GetString(*section.s.AsInterface(), *key.s.AsInterface(), value.s.AsInterface());
            }
            if (found) reinterpret_cast<RZStringSlot *>(&entry.target->mString)->Assign(mCDAppDrive.mString + value.s.mString);
        }
        ini.Destruct();
        return true;
    }
    if (!mAllowRecursiveCDSearch) return false;
    LocalScan scan(&mCDAppDrive);
    if (mExpectedCDName.mString.empty() && mExpectedCDAppPath.mString.empty()) {
        // The executable's name searched for on the whole drive; its first match's folder is the CD application
        // directory, and the data and plug-in directories are the installed ones.
        LocalString pattern;
        pattern.s.mString = mAppName.mString + mAppExtension.mString;
        dscan::PathList found;
        scan->SetPattern(&pattern.s, false);
        if (!scan->ListFilesBelow(&mCDAppDrive, &found, true) || found.empty()) return false;
        appbase::GetModuleDirectory(found.front().s.AsInterface(), mCDAppDirectory.AsInterface());
        mCDDataDirectory.mString.assign(DataDirectory());
        mCDPlugInDirectory.mString.assign(PlugInDirectory());
        return true;
    }
    bool result = true;
    LocalString volume;
    if (!mExpectedCDName.mString.empty()) {
        if (!VolumeName(&mCDAppDrive, &volume.s)) return false;
        // The expected name's own slot 5 (ToChar), compared without case.
        const char *expected = VirtualCall<const char *(__thiscall *)(void *), 5>(&mExpectedCDName);
        result = _stricmp(volume.s.mString.c_str(), expected) == 0;
        if (!result) return false;
    }
    if (!mExpectedCDAppPath.mString.empty()) {
        LocalString path;
        path.s.mString = mCDAppDrive.mString + mExpectedCDAppPath.mString;
        if (!cRZFile::PathExists(*path.s.AsInterface())) return false;
        appbase::GetModuleDirectory(path.s.AsInterface(), mCDAppDirectory.AsInterface());
        reinterpret_cast<RZStringSlot *>(&mCDDataDirectory.mString)->Assign(mCDAppDirectory.mString);
        reinterpret_cast<RZStringSlot *>(&mCDPlugInDirectory.mString)->Assign(mCDAppDirectory.mString);
    }
    return result;
}

// ---- adapters ----

cGZFileSystem *__fastcall cGZFileSystemAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZFileSystem;
}

void __fastcall cGZFileSystemAdapters::UseCDIniFile(cIGZFileSystem *face, void *, uint8_t value)
{
    __assume(face != nullptr); // a slot's this is never null: no null test in the face-to-object cast
    [[msvc::forceinline_calls]] return static_cast<cGZFileSystem &>(*face).cGZFileSystem::UseCDIniFile(value);
}

void __fastcall cGZFileSystemAdapters::AllowRecursiveCDSearch(cIGZFileSystem *face, void *, uint8_t value)
{
    __assume(face != nullptr); // a slot's this is never null: no null test in the face-to-object cast
    [[msvc::forceinline_calls]] return static_cast<cGZFileSystem &>(*face).cGZFileSystem::AllowRecursiveCDSearch(value);
}

void *__fastcall cGZFileSystemAdapters::SetExpectedCDName(cIGZFileSystem *face, void *, const cIGZString *text)
{
    __assume(face != nullptr); // a slot's this is never null: no null test in the face-to-object cast
    [[msvc::forceinline_calls]] return static_cast<cGZFileSystem &>(*face).cGZFileSystem::SetExpectedCDName(text);
}

void *__fastcall cGZFileSystemAdapters::SetExpectedCDAppPath(cIGZFileSystem *face, void *, const cIGZString *text)
{
    __assume(face != nullptr); // a slot's this is never null: no null test in the face-to-object cast
    [[msvc::forceinline_calls]] return static_cast<cGZFileSystem &>(*face).cGZFileSystem::SetExpectedCDAppPath(text);
}

void *__fastcall cGZFileSystemAdapters::SetPluginDirectoryName(cIGZFileSystem *face, void *, const cIGZString *text)
{
    __assume(face != nullptr); // a slot's this is never null: no null test in the face-to-object cast
    [[msvc::forceinline_calls]] return static_cast<cGZFileSystem &>(*face).cGZFileSystem::SetPluginDirectoryName(text);
}
