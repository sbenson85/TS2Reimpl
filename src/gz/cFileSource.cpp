// nGZCommandParser::cFileSource (lane IM): the command parser's file source. src/gz/cFileSource.h has the layout.
#include "gz/cFileSource.h"

#include "app/IniFile.h"
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"

#include <string>

namespace nGZCommandParser {

namespace {

// A cRZString local, built and destroyed by the game's bodies (Construct / Destruct) as the original's are.
struct LocalString {
    union {
        cRZString value;
    };
    explicit LocalString(const char *text) { value.Construct(text); }
    LocalString() {}
    ~LocalString() { value.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// A cRZFileName local made empty (0x1c7f30) and destroyed (0x1c0eb2).
struct LocalFileName {
    union {
        cRZFileName value;
    };
    LocalFileName() { value.ConstructEmpty(); }
    ~LocalFileName() { value.Destruct(); }
    LocalFileName(const LocalFileName &) = delete;
    LocalFileName &operator=(const LocalFileName &) = delete;
};

// A cRZFile local on a path (0x1c0c8b), destroyed by 0x1c0e6d (which closes it if still open).
struct LocalFile {
    union {
        cRZFile value;
    };
    explicit LocalFile(const char *path) { value.Construct(path); }
    ~LocalFile() { value.Destruct(); }
    LocalFile(const LocalFile &) = delete;
    LocalFile &operator=(const LocalFile &) = delete;
};

} // namespace

// The compiler's destructor stores the class's vtables (then cRZRefCount's), as 0xaff0a0 does before its sized delete.
cFileSource::~cFileSource() = default;

bool cFileSource::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kInterfaceId) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cFileSource::AddRef()
{
    return rz::cRZRefCount::AddRef();
}

uint32_t cFileSource::Release()
{
    return rz::cRZRefCount::Release();
}

// The path with every '/' turned into '\\', then whether that is a canonical absolute path ("X:\..." or "\\...", no
// "\." in it).
bool cFileSource::AcceptsPath(const char *path)
{
    LocalString text(path);
    std::string &s = text.value.mString;
    for (size_t at; !s.empty() && (at = s.find('/')) != std::string::npos;) s[at] = '\\';
    return inifile::IsCanonicalAbsolutePathString(&text.value);
}

bool cFileSource::Exists(const char *path)
{
    LocalString text(path);
    return cRZFile::PathExists(*text.value.AsInterface());
}

// Opened for reading (access 1, open existing 2, share all 7). `out` becomes the length plus one bytes (new bytes zero,
// as std::vector::resize), the file is read over all but the last, and the read's result is returned.
bool cFileSource::ReadFile(const char *path, FileData *out)
{
    LocalFile file(path);
    if (!file.value.Open(1, 2, 7)) return false;
    out->resize(static_cast<uint32_t>(file.value.Length() + 1));
    const bool read = file.value.Read(out->data(), static_cast<uint32_t>(out->size()) - 1);
    file.value.Close();
    return read;
}

// The drive and directory of `relativeTo` (made absolute against the current directory when it is not), then `path`
// appended as it is.
void cFileSource::ResolvePath(const char *path, const char *relativeTo, cIGZString *out)
{
    LocalFileName name;
    {
        LocalString base(relativeTo);
        SetFileNamePath(&name.value, &base.value);
    }
    std::string joined;
    {
        LocalString drivePath;
        name.value.GetDrivePath(&drivePath.value);
        joined = std::move(drivePath.value.mString.append(path));
    }
    LocalString text(joined.c_str());
    out->FromChar(text.value.mString.c_str());
}

bool cFileSource::CanonicalPath(const char *path, cIGZString *out)
{
    out->FromChar(path);
    return true;
}

void SetFileNamePath(cRZFileName *name, const cRZString *path)
{
    inifile::ResolvePathString(&name->mFull, path, nullptr);
    SplitFileNamePath(name);
}

void SplitFileNamePath(cRZFileName *name)
{
    if (!name->mFull.mString.empty())
        cRZFileName::SplitInto(&name->mFull, &name->mDrive, &name->mDir, &name->mName, &name->mExt);
}

// ---- adapters ----

// The deleting destructor's two steps by hand (no name to call it by without dispatch).
cFileSource *__fastcall cFileSourceAdapters::DeletingDestructor(cFileSource *self, void *, uint32_t flags)
{
    self->cFileSource::~cFileSource();
    if (flags & 1) ::operator delete(self, sizeof(cFileSource));
    return self;
}
bool __fastcall cFileSourceAdapters::AcceptsPath(cFileSource *self, void *, const char *path)
{
    [[msvc::forceinline_calls]] return self->cFileSource::AcceptsPath(path);
}
bool __fastcall cFileSourceAdapters::Exists(cFileSource *self, void *, const char *path)
{
    [[msvc::forceinline_calls]] return self->cFileSource::Exists(path);
}
bool __fastcall cFileSourceAdapters::ReadFile(cFileSource *self, void *, const char *path, FileData *out)
{
    [[msvc::forceinline_calls]] return self->cFileSource::ReadFile(path, out);
}
void __fastcall cFileSourceAdapters::ResolvePath(cFileSource *self, void *, const char *path, const char *relativeTo, cIGZString *out)
{
    [[msvc::forceinline_calls]] self->cFileSource::ResolvePath(path, relativeTo, out);
}
bool __fastcall cFileSourceAdapters::CanonicalPath(cFileSource *self, void *, const char *path, cIGZString *out)
{
    [[msvc::forceinline_calls]] return self->cFileSource::CanonicalPath(path, out);
}
void __fastcall cFileSourceAdapters::SetFileNamePath(cRZFileName *name, void *, const cRZString *path)
{
    nGZCommandParser::SetFileNamePath(name, path);
}
void __fastcall cFileSourceAdapters::SplitFileNamePath(cRZFileName *name, void *)
{
    nGZCommandParser::SplitFileNamePath(name);
}

} // namespace nGZCommandParser
