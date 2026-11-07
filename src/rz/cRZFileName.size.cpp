#include "rz/cRZFileName.h"

#include "app/ExceptionReporter.h"
#include "app/IniFile.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>

namespace {

bool IsSeparator(char c)
{
    return c == '\\' || c == '/';
}

// A cRZString on the stack that is built and destroyed like the game's (Construct / Destruct).
struct Temp {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    cRZString *S() { return reinterpret_cast<cRZString *>(raw); }
    ~Temp() { S()->Destruct(); }
};

} // namespace

// 0x1c39bc
void __cdecl cRZFileName::SplitInto(const cRZString *full, cRZString *drive, cRZString *dir, cRZString *name, cRZString *ext)
{
    char driveBuffer[4]; // the original keeps it in the first argument's stack slot
    char dirBuffer[256], nameBuffer[256], extBuffer[256];
    appbase::SplitPath(full->mString.c_str(), driveBuffer, dirBuffer, nameBuffer, extBuffer);
    if (drive) drive->mString.assign(driveBuffer, std::strlen(driveBuffer));
    if (dir) dir->mString.assign(dirBuffer, std::strlen(dirBuffer));
    if (name) name->mString.assign(nameBuffer, std::strlen(nameBuffer));
    if (ext) ext->mString.assign(extBuffer, std::strlen(extBuffer));
}

// 0x1c7ef4
cRZFileName *cRZFileName::Construct(const cRZString *path)
{
    mDrive.Construct();
    mDir.Construct();
    mName.Construct();
    mExt.Construct();
    mFull.Construct();
    Init(path);
    return this;
}

// 0x1c0eb2
void cRZFileName::Destruct()
{
    mFull.Destruct();
    mExt.Destruct();
    mName.Destruct();
    mDir.Destruct();
    mDrive.Destruct();
}

// 0x1c8bb8
bool cRZFileName::Init(const cRZString *path)
{
    inifile::ResolvePathString(&mFull, path, nullptr);
    SplitInto(&mFull, &mDrive, &mDir, &mName, &mExt);
    return true;
}

// 0x1c86fe
void __stdcall cRZFileName::NormaliseDrive(cRZString *drive)
{
    std::string &s = drive->mString;
    if (s.empty()) return;
    if (s.size() != 2) s.resize(2, 0);
    if (!std::isalpha(static_cast<int>(static_cast<signed char>(s[0])))) s[0] = 'C';
    s[1] = ':';
}

// 0x1c86a1
void __stdcall cRZFileName::NormaliseDirectory(cRZString *dir)
{
    std::string &s = dir->mString;
    if (s.empty()) return;
    if (!IsSeparator(s[0])) s.insert(0, "\\", 1);
    if (!IsSeparator(s[s.size() - 1])) s.append("\\", 1);
}

// 0x1c875f (inserts through std::string::insert(pos, count, char), the library's 0x1c8c11)
void __stdcall cRZFileName::NormaliseExtension(cRZString *ext)
{
    std::string &s = ext->mString;
    if (!s.empty() && s[0] != '.') s.insert(0, 1, '.');
}

// 0x1c87ba
cRZString *cRZFileName::GetDrive()
{
    NormaliseDrive(&mDrive);
    return &mDrive;
}

// 0x1c87ac
cRZString *cRZFileName::GetDirectory()
{
    NormaliseDirectory(&mDir);
    return &mDir;
}

// 0x1c8800
cRZString *cRZFileName::GetName()
{
    return &mName;
}

// 0x1c87ed
cRZString *cRZFileName::GetExtension()
{
    NormaliseExtension(&mExt);
    return &mExt;
}

// 0x1c898e
void cRZFileName::Assemble(uint32_t mask)
{
    std::string &full = mFull.mString;
    full.clear();
    if (mask & 1) full.append(GetDrive()->mString);
    if (mask & 2) full.append(GetDirectory()->mString);
    if (mask & 4) full.append(GetName()->mString);
    if (mask & 8) full.append(GetExtension()->mString);
}

// 0x1c882a
cRZString *cRZFileName::GetFullPath(cRZString *out)
{
    Assemble(0xf);
    out->Construct(mFull, 0, 0xFFFFFFFF);
    return out;
}

// 0x1c87c7
cRZString *cRZFileName::GetDrivePath(cRZString *out)
{
    Assemble(3);
    out->Construct(mFull, 0, 0xFFFFFFFF);
    return out;
}

// 0x1c8936
bool cRZFileName::IsAbsoluteFile()
{
    Temp full;
    GetFullPath(full.S());
    return GetName()->mString.size() != 0 && inifile::IsCanonicalAbsolutePathString(full.S());
}

// 0x1c88cd
bool cRZFileName::IsAbsoluteDirectory()
{
    Temp full;
    cRZString *dir = GetDirectory();
    cRZString *ext = GetExtension();
    GetFullPath(full.S());
    return dir->mString.size() != 0 && GetName()->mString.size() == 0 && ext->mString.size() == 0 &&
           inifile::IsCanonicalAbsolutePathString(full.S());
}

// 0x1c0f3b
void __cdecl cRZFileName::NormaliseSeparators(cRZString *path, char separator)
{
    std::string &s = path->mString;
    for (char &c : s) {
        if (IsSeparator(c) && c != separator) c = separator;
    }
    // The end test looks at the already replaced text, so a separator that isn't itself '\\' or '/'
    // is appended even after a replaced trailing one.
    if (s.empty() || !IsSeparator(s[s.size() - 1])) s.append(1, separator);
}
