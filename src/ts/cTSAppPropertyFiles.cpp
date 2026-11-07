// The application's property files (lane MA, F4d). See cTSAppPropertyFiles.h.
#include "ts/cTSAppPropertyFiles.h"

#include "app/AppInit.h"        // appinit::CommonImplSlot4 / 5
#include "app/AppServices2.h"   // as2::DirectoryExists
#include "app/IniFile.h"        // inifile::IsCanonicalAbsolutePathString
#include "app/ReportBlockers.h" // appbase::ReadLineToString
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cGZPropertySetSerializer.h"
#include "gz/cIGZFrameWork.h"
#include "rz/PathAttributes.h"
#include "rz/RZFileStreams.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/cRZCmdLine.h"      // cIGZCmdLine
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"      // cRZIniStringHelpers::TrimBoth
#include "rz/cRZTextFile.h"
#include "rz/cTSIniFile.h"      // cRZStringTokenizer
#include "ts/cTSAppCommonImpl.h" // OptionStringList

#include <cstring>
#include <new>

namespace ts {

namespace {

constexpr char kSavedKeysFile[] = "savedProps.txt";
constexpr char kUserSavedKeysFile[] = "userSavedProps.txt";
constexpr char kGlobalPropsFile[] = "globalProps.xml";
constexpr char kUserPropsFile[] = "userProps.xml";
constexpr char kConfigPropsFile[] = "Config/globalProps.xml";
constexpr char kRevisionKey[] = "propertyRevision";
constexpr char kNoSaveSwitch[] = "propsNoSave";
constexpr char kNoLoadSwitch[] = "propsNoLoad";
constexpr char kCardConfigSwitch[] = "cardConfig";
constexpr char kPropertyListSwitch[] = "propertyList";
constexpr char kListDelimiter = ';';
constexpr char kCommentStart = '#';
constexpr uint32_t kReserveLine = 0x800;   // the line buffer's reserve
constexpr uint32_t kTextAccess = 1;        // cRZTextFile::Open: read
constexpr uint32_t kTextCreation = 2;      // open existing
constexpr uint32_t kTextSharing = 1;       // share read
constexpr uint32_t kAttributesAny = 1;     // PathAttributesMatch: no test
constexpr uint32_t kAttributesWritable = 2; // PathAttributesMatch: not read-only
constexpr int32_t kOptionNoMask = -1;

constexpr bool kTrimQuotes = true; // cIGZCmdLine::IsSwitchPresent(name, value, trimQuotes), slot 8

// cIGZCmdLine slot 9: IsSwitchPresent(name), through the command line's own vtable.
bool SwitchPresent(cIGZCmdLine *commandLine, const char *name)
{
    cRZString key;
    key.Construct(name);
    const bool found = commandLine->IsSwitchPresent(*key.AsInterface());
    key.Destruct();
    return found;
}

// A path: the directory's text, then the file name.
void AssignJoined(cRZString &out, const cRZString &directory, const char *name)
{
    out.mString = directory.mString + name;
}

// The first directory of a list option that exists (DirectoryExists), into `out`; `out` unchanged when none does.
template <class Use> void FirstExistingDirectory(uint32_t option, Use use)
{
    OptionStringList list;
    appinit::CommonImplSlot5(option, reinterpret_cast<uint32_t>(&list), 1, 0, static_cast<uint32_t>(kOptionNoMask));
    for (const OptionString &text : list) {
        cRZString directory;
        directory.Construct(text.c_str());
        const bool exists = as2::DirectoryExists(&directory);
        if (exists) use(directory);
        directory.Destruct();
        if (exists) break;
    }
}

} // namespace

bool KeyNameLess::operator()(const GZServiceString &a, const GZServiceString &b) const
{
    return _stricmp(a.c_str(), b.c_str()) < 0;
}

// ---- 0xcb0890 / 0xcb0ad0

cTSAppPropertyFiles *cTSAppPropertyFiles::Construct(bool enabled)
{
    mSave = enabled;
    mLoadUser = enabled;
    new (&mSavedKeys) KeyNameSet();
    mProperties = nullptr;
    mSavedKeysPath.Construct();
    mUserSavedKeysPath.Construct();
    mUserPropsPath.Construct();
    mGlobalPropsPath.Construct();
    mConfigPropsPath.Construct();
    mDataDirectory.Construct();
    mOverrideDirectory.Construct();
    cIGZPropertySetString *service = nullptr;
    if (auto *framework = static_cast<cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(kPropertyServiceClass, cGZPropertySetString::kIid, reinterpret_cast<void **>(&service));
    if (service != mProperties) {
        if (service) service->AddRef();
        cIGZPropertySetString *old = mProperties;
        mProperties = service;
        if (old) old->Release();
    }
    if (service) service->Release();
    return this;
}

void cTSAppPropertyFiles::Destruct()
{
    if (cIGZPropertySetString *service = mProperties) {
        mProperties = nullptr;
        service->Release();
    }
    mOverrideDirectory.Destruct();
    mDataDirectory.Destruct();
    mConfigPropsPath.Destruct();
    mGlobalPropsPath.Destruct();
    mUserPropsPath.Destruct();
    mUserSavedKeysPath.Destruct();
    mSavedKeysPath.Destruct();
    mSavedKeys.~KeyNameSet();
}

// ---- 0xcb0f40

void cTSAppPropertyFiles::SetupPaths()
{
    if (mOverrideDirectory.Strlen() == 0)
        FirstExistingDirectory(kDataDirectoriesOption, [this](const cRZString &directory) { mDataDirectory.mString = directory.mString; });
    else
        mDataDirectory.mString = mOverrideDirectory.mString;
    AssignJoined(mSavedKeysPath, mDataDirectory, kSavedKeysFile);
    AssignJoined(mUserSavedKeysPath, mDataDirectory, kUserSavedKeysFile);
    AssignJoined(mGlobalPropsPath, mDataDirectory, kGlobalPropsFile);

    cRZString user;
    user.Construct();
    if (mOverrideDirectory.Strlen() == 0)
        appinit::CommonImplSlot4(kUserDirectoryOption, reinterpret_cast<uint32_t>(&user), 1, 0, static_cast<uint32_t>(kOptionNoMask));
    else
        user.mString = mOverrideDirectory.mString;
    AssignJoined(mUserPropsPath, user, kUserPropsFile);

    if (mOverrideDirectory.Strlen() == 0)
        FirstExistingDirectory(kConfigDirectoriesOption, [this](const cRZString &directory) { AssignJoined(mConfigPropsPath, directory, kConfigPropsFile); });
    else
        AssignJoined(mConfigPropsPath, mOverrideDirectory, kConfigPropsFile);
    user.Destruct();
}

// ---- 0xcb18a0

void cTSAppPropertyFiles::LoadProperties()
{
    cIGZCmdLine *commandLine = static_cast<cIGZFrameWork *>(GetServiceObject())->CommandLine();
    cGZPropertySetString loaded;
    if (SwitchPresent(commandLine, kNoSaveSwitch) || SwitchPresent(commandLine, kCardConfigSwitch)) mSave = false;
    if (SwitchPresent(commandLine, kNoLoadSwitch) || SwitchPresent(commandLine, kCardConfigSwitch)) mLoadUser = false;

    cRZString list;
    list.Construct();
    cRZString name;
    name.Construct(kPropertyListSwitch);
    const bool haveList = commandLine->IsSwitchPresent(*name.AsInterface(), *list.AsInterface(), kTrimQuotes);
    name.Destruct();
    if (!haveList) {
        LoadXml(&loaded, &mGlobalPropsPath);
        if (mLoadUser) LoadUserProperties(&loaded, &mUserPropsPath);
    } else {
        LoadPropertyList(&loaded, &list);
    }
    CopyAll(&loaded, mProperties);
    list.Destruct();
}

// ---- 0xcb1780 / 0xcb1790

void cTSAppPropertyFiles::LoadGlobalInto(cIGZPropertySetString *target)
{
    LoadXml(target, &mGlobalPropsPath);
}

void cTSAppPropertyFiles::LoadConfigProperties()
{
    cGZPropertySetString loaded;
    LoadXml(&loaded, &mConfigPropsPath);
    {
        const GZServiceString key(kRevisionKey);
        loaded.RemoveProperty(&key);
    }
    CopyAll(&loaded, mProperties);
}

// ---- 0xcb1e60

bool cTSAppPropertyFiles::LoadUserProperties(cIGZPropertySetString *target, const cRZString *path)
{
    cGZPropertySetString loaded;
    if (!LoadXml(&loaded, path)) return false;
    gz::AnyUint32 wanted(0);
    bool haveRevision;
    {
        const GZServiceString key(kRevisionKey);
        haveRevision = target->GetPropertyValue(&key, &wanted);
    }
    bool taken = true;
    if (haveRevision) {
        gz::AnyUint32 found(0);
        const GZServiceString key(kRevisionKey);
        taken = loaded.GetPropertyValue(&key, &found) && found.mValue == wanted.mValue;
    }
    if (taken) {
        LoadSavedKeyNames(&mSavedKeys);
        CopyListed(&loaded, target, &mSavedKeys);
    }
    return taken;
}

// ---- 0xcb2020

void cTSAppPropertyFiles::LoadPropertyList(cIGZPropertySetString *target, const cRZString *list)
{
    cRZString file;
    file.Construct();
    cRZString rest;
    rest.Construct(*list, 0, 0xffffffff);
    auto *tokens = reinterpret_cast<cRZStringTokenizer *>(&rest);
    while (tokens->NextToken(kListDelimiter, &file)) {
        if (!inifile::IsCanonicalAbsolutePathString(&file)) file.mString = mDataDirectory.mString + file.mString;
        LoadXml(target, &file);
    }
    rest.Destruct();
    file.Destruct();
}

// ---- 0xcb2240

void cTSAppPropertyFiles::LoadSavedKeyNames(KeyNameSet *names)
{
    if (!names->empty()) return;
    KeyNameSet user;
    ReadKeyNames(mSavedKeysPath.ToChar(), names);
    ReadKeyNames(mUserSavedKeysPath.ToChar(), &user);
    names->insert(user.begin(), user.end());
}

// ---- 0xcb28b0

void cTSAppPropertyFiles::Save()
{
    if (!mSave) return;
    LoadSavedKeyNames(&mSavedKeys);
    if (mSavedKeys.empty()) return;
    cGZPropertySetString saved;
    CopyListed(mProperties, &saved, &mSavedKeys);
    auto *serializer = new cGZPropertySetSerializerXmlString();
    serializer->AddRef();
    const cIGZString &path = *mUserPropsPath.AsInterface();
    if (!cRZFile::PathExists(path) || PathAttributesMatch(&path, kAttributesWritable)) {
        alignas(cRZOFStream) unsigned char storage[sizeof(cRZOFStream)];
        auto *stream = reinterpret_cast<cRZOFStream *>(storage)->Construct(path, 0, 0, 0, 0);
        serializer->Write(stream, &saved, 0);
        stream->Destruct();
    }
    serializer->Release();
}

// ---- 0xcb0da0 / 0xcb0e50

void __stdcall cTSAppPropertyFiles::CopyAll(cIGZPropertySetString *from, cIGZPropertySetString *to)
{
    GZServiceString key;
    void *value = nullptr;
    for (bool more = from->GetFirst(&key, &value); more; more = from->GetNext(&key, &value)) to->SetProperty(&key, value);
}

void __stdcall cTSAppPropertyFiles::CopyListed(cIGZPropertySetString *from, cIGZPropertySetString *to, KeyNameSet *names)
{
    GZServiceString key;
    void *value = nullptr;
    for (bool more = from->GetFirst(&key, &value); more; more = from->GetNext(&key, &value))
        if (names->find(key) != names->end()) to->SetProperty(&key, value);
}

// ---- 0xcb1cf0

bool __stdcall cTSAppPropertyFiles::LoadXml(cIGZPropertySetString *target, const cRZString *path)
{
    const cIGZString &text = *path->AsInterface();
    if (!cRZFile::PathExists(text) || !PathAttributesMatch(&text, kAttributesAny)) return false;
    cGZPropertySetString loaded;
    auto *serializer = new cGZPropertySetSerializerXmlString();
    serializer->AddRef();
    alignas(cRZIFStream) unsigned char storage[sizeof(cRZIFStream)];
    auto *stream = reinterpret_cast<cRZIFStream *>(storage)->Construct(text, 0, 0);
    const bool read = serializer->Read(stream, &loaded);
    if (read) CopyAll(&loaded, target);
    stream->Destruct();
    serializer->Release();
    return read;
}

// ---- 0xcb2400

void __stdcall cTSAppPropertyFiles::ReadKeyNames(const char *path, KeyNameSet *names)
{
    names->clear();
    alignas(cRZTextFile) unsigned char storage[sizeof(cRZTextFile)];
    auto *file = reinterpret_cast<cRZTextFile *>(storage);
    file->Construct(path);
    if (file->mOpen || file->Open(kTextAccess, kTextCreation, kTextSharing)) {
        cRZString line;
        line.Construct();
        line.mString.reserve(kReserveLine);
        while (appbase::ReadLineToString(file, nullptr, line.AsInterface()) & 0xff) {
            reinterpret_cast<cRZIniStringHelpers *>(&line)->TrimBoth();
            try {
                if (!line.mString.empty() && line.mString[0] != kCommentStart) names->insert(GZServiceString(line.ToChar()));
            } catch (...) {
            }
        }
        line.Destruct();
    }
    file->Destruct();
}

} // namespace ts
