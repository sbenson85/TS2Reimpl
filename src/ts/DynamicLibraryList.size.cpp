// DynamicLibraryList (src/ts/DynamicLibraryList.h).
#include "ts/DynamicLibraryList.h"

#include "app/AppInit.h"
#include "app/IniFile.h"
#include "app/ReportFinish.h"
#include "game/game.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZString.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/cTSIniFile.h"

#include <new>

namespace {

// The option naming the directory the libraries are in.
constexpr uint32_t kOptionLibraryDirectory = 0x6bcfce93;
// "." (its first character is used) and the one-character path separator the DLLs lines put after a directory.
constexpr uint32_t kDotText = 0xd9f880;
constexpr uint32_t kSeparatorText = 0xda52b4;
// CommonImplSlot4's arguments for a string option.
constexpr uint32_t kOptionAsString = 1;
constexpr uint32_t kOptionNoDefault = 0;
constexpr uint32_t kOptionAllBits = 0xffffffff;

// A cRZString on the stack (the game's vtable), destroyed at the end of its scope.
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    explicit LocalString(const char *text) { s.Construct(text); }
    explicit LocalString(const cRZString &other) { s.Construct(other, 0, 0xffffffffu); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// The cRZString helper 0x1ad622 (ret 8): the text before the first `delimiter` into `head` and removed from `text`
// with the delimiter; without one, all of `text` moves to `head` (head cleared, then the two swapped).
bool SplitAt(std::string &text, char delimiter, std::string &head)
{
    const size_t at = text.find(delimiter);
    if (at == std::string::npos) {
        head.erase(0);
        text.swap(head);
        return false;
    }
    head.assign(text, 0, at);
    text.erase(0, at + 1);
    return true;
}

// cIGZCOM::AddLibrary of the root COM director.
uint8_t AddToDirector(const cRZString &path)
{
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    return com->AddLibrary(*path.AsInterface());
}

} // namespace

namespace dynlib {

void __cdecl AddConfiguration(const cRZString *key, const cRZString *value, void *list)
{
    Text name(*key);
    static_cast<DynamicLibraryList *>(list)->mConfigurations[name].s.mString = value->mString;
}

void __cdecl AddLibraryLine(const cRZString *key, const cRZString *value, void *list)
{
    DynamicLibraryList *const self = static_cast<DynamicLibraryList *>(list);
    uint8_t required;
    {
        LocalString word("required");
        required = key->AsInterface()->CompareTo(*word.s.AsInterface(), false) == 0;
    }
    LocalString configuration(*value);
    LocalString file;
    SplitAt(configuration.s.mString, ',', file.s.mString);
    const char separator = *GameAddress<const char>(kSeparatorText);
    const auto found = self->mConfigurations.find(configuration.s.mString);
    std::string prefix;
    if (found == self->mConfigurations.end()) {
        prefix.assign(GameAddress<const char>(kDotText), 1);
    } else {
        prefix = found->second.s.mString;
    }
    prefix.push_back(separator);
    file.s.mString = prefix + file.s.mString;
    LocalString full;
    LocalString directory;
    appinit::CommonImplSlot4(kOptionLibraryDirectory, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&directory.s)),
                             kOptionAsString, kOptionNoDefault, kOptionAllBits);
    inifile::CombinePathString(&full.s, &file.s, &directory.s);
    self->Add(&full.s, required);
}

} // namespace dynlib

using dynlib::Library;
using dynlib::Text;

void DynamicLibraryList::Add(const cRZString *path, uint8_t required)
{
    mLibraries.push_back(Library(*path, required));
}

bool DynamicLibraryList::ReadConfiguration(const cIGZString *path, const cRZString *configuration)
{
    alignas(cTSIniFile) unsigned char iniBytes[sizeof(cTSIniFile)];
    cTSIniFile *const ini = reinterpret_cast<cTSIniFile *>(iniBytes);
    ini->Construct(*path, configuration);
    bool found = false;
    {
        LocalString section("Configurations");
        if (ini->HasSectionPrefixed(*section.s.AsInterface())) {
            ini->SectionWalk(*section.s.AsInterface(), reinterpret_cast<void *>(&dynlib::AddConfiguration), this);
        }
        section.s.mString.assign("DLLs");
        if (ini->HasSectionPrefixed(*section.s.AsInterface())) {
            found = true;
            ini->SectionWalk(*section.s.AsInterface(), reinterpret_cast<void *>(&dynlib::AddLibraryLine), this);
        }
    }
    ini->Destruct();
    return found;
}

void DynamicLibraryList::RequireVersion(const cRZString *path, const cRZString *version, uint8_t required)
{
    alignas(cRZFileName) unsigned char nameBytes[sizeof(cRZFileName)];
    cRZFileName *const fileName = reinterpret_cast<cRZFileName *>(nameBytes);
    fileName->Construct(path);
    {
        LocalString name(*fileName->GetName());
        reinterpret_cast<cRZStringHelpers *>(&name.s)->LowerCase();
        if (required) {
            mVersions[Text(name.s)].s.mString = version->mString;
        } else {
            const auto found = mVersions.find(name.s.mString);
            if (found != mVersions.end()) mVersions.erase(found);
        }
    }
    fileName->Destruct();
}

bool DynamicLibraryList::LoadAll()
{
    bool all = true;
    for (Library &library : mLibraries) {
        if (library.mLoaded) continue;
        if (!library.mRequired && !cRZFile::PathExists(*library.mPath.s.AsInterface())) continue;
        alignas(cRZFileName) unsigned char nameBytes[sizeof(cRZFileName)];
        cRZFileName *const fileName = reinterpret_cast<cRZFileName *>(nameBytes);
        fileName->Construct(&library.mPath.s);
        {
            LocalString name(*fileName->GetName());
            reinterpret_cast<cRZStringHelpers *>(&name.s)->LowerCase();
            const auto found = mVersions.find(name.s.mString);
            if (found == mVersions.end()) {
                library.mLoaded = AddToDirector(library.mPath.s);
            } else {
                alignas(appbase::cRZVersion) unsigned char versionBytes[sizeof(appbase::cRZVersion)];
                appbase::cRZVersion *const fileVersion = reinterpret_cast<appbase::cRZVersion *>(versionBytes);
                fileVersion->ConstructFromPath(&library.mPath.s);
                {
                    LocalString text;
                    fileVersion->GetFileVersion(&text.s);
                    if (text.s.IsEqualForward(*found->second.s.AsInterface(), true)) {
                        library.mLoaded = AddToDirector(library.mPath.s);
                    }
                }
                fileVersion->Destruct();
            }
        }
        if (library.mRequired && !library.mLoaded) all = false;
        fileName->Destruct();
    }
    return all;
}

void DynamicLibraryList::Report() const
{
    // The original builds a "Have (NOT) Loaded " + "Required/Optional DLL " line per library in a local string and
    // drops it (the log call is compiled out of this build): nothing to do.
}

// ---- adapters ----

DynamicLibraryList *__fastcall DynamicLibraryListAdapters::Construct(void *memory, void *)
{
    return ::new (memory) DynamicLibraryList();
}

uint8_t __fastcall DynamicLibraryListAdapters::Destruct(DynamicLibraryList *self, void *)
{
    self->~DynamicLibraryList();
    return 0;
}

void __fastcall DynamicLibraryListAdapters::Add(DynamicLibraryList *self, void *, const cRZString *path, uint32_t required)
{
    self->Add(path, static_cast<uint8_t>(required));
}

bool __fastcall DynamicLibraryListAdapters::ReadConfiguration(DynamicLibraryList *self, void *, const cIGZString *path,
                                                              const cRZString *configuration)
{
    return self->ReadConfiguration(path, configuration);
}

void __fastcall DynamicLibraryListAdapters::RequireVersion(DynamicLibraryList *self, void *, const cRZString *path,
                                                           const cRZString *version, uint32_t required)
{
    self->RequireVersion(path, version, static_cast<uint8_t>(required));
}

bool __fastcall DynamicLibraryListAdapters::LoadAll(DynamicLibraryList *self, void *)
{
    return self->LoadAll();
}

void __fastcall DynamicLibraryListAdapters::Report(const DynamicLibraryList *self, void *)
{
    self->Report();
}
