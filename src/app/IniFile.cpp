#include "app/IniFile.h"
#include "game/vtable.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <string.h>
#include <new>
#include <utility>

namespace inifile {

namespace {

// 0x14205a: assign through (data, size), and not at all to itself.
void AssignString(std::string &dst, const std::string &src)
{
    if (&dst != &src) dst.assign(src.c_str(), src.size());
}

bool IsSlash(char c)
{
    return c == '\\' || c == '/';
}

} // namespace

bool __cdecl IsAsciiLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool __cdecl IsCanonicalAbsolutePath(const char *text)
{
    bool absolute = false;
    if (std::isalpha(static_cast<int>(static_cast<signed char>(text[0]))) != 0 && text[1] == ':' && text[2] == '\\') absolute = true;
    else if (text[0] == '\\' && text[1] == '\\') absolute = true;
    if (!absolute) return false;
    if (std::strstr(text, "\\.") != nullptr) return false;
    return std::strstr(text, "/.") == nullptr;
}

bool __cdecl IsCanonicalAbsolutePathString(const cRZString *path)
{
    return IsCanonicalAbsolutePath(path->mString.c_str());
}

char *__cdecl CombinePath(char *dest, const char *path, int32_t maxlen, const char *base)
{
    if (!path) return nullptr;
    // storage[0] is the byte just before the buffer, which the original reads (always 0) when a
    // component walk backs up to the start.
    char storage[0x230];
    storage[0] = 0;
    char *buf = storage + 1;
    char *w = buf;

    if (*base != 0) {
        int32_t count = 0;
        char c = *base;
        char last = 0;
        for (;;) {
            *w = c;
            last = c;
            ++w;
            ++count;
            if (count >= 0x209) return nullptr;
            c = base[count];
            if (c == 0) break;
        }
        if (w > buf && !IsSlash(last)) *w++ = '\\';
    }

    int32_t length = static_cast<int32_t>(std::strlen(path)) + static_cast<int32_t>(w - buf);
    if (length >= 0x209) return nullptr;
    std::memcpy(w, path, std::strlen(path) + 1);

    bool trailingSlash = length > 0 && IsSlash(buf[length - 1]);

    // Collapse "." and ".." components in place.
    w = buf;
    const char *r = buf;
    for (;;) {
        char c = *r++;
        if (c != 0 && !IsSlash(c)) {
            *w++ = c;
            continue;
        }
        // A separator or the end: look at the component just written.
        bool dotdotAllowed;
        bool skipToWrite = false;
        if (w - buf >= 2) {
            if (w[-1] == '.' && IsSlash(w[-2])) {
                w -= 2;
                skipToWrite = true;
            }
            dotdotAllowed = true;
        } else {
            dotdotAllowed = false;
        }
        if (!skipToWrite && w - buf >= 3 && dotdotAllowed && w[-1] == '.' && w[-2] == '.' && IsSlash(w[-3])) {
            w -= 3;
            if (w[-1] == ':') return nullptr;
            while (w > buf) {
                --w;
                if (IsSlash(*w)) break;
            }
            if (w == buf) {
                --w;
                if (!IsSlash(*w)) return nullptr;
            }
        }
        if (c == 0) break;
        *w++ = c;
    }

    // Drop a trailing separator (keeping "X:\"), terminate.
    if (IsSlash(w[-1])) --w;
    if (w[-1] == ':') *w++ = '\\';
    *w = 0;

    int32_t resultLength = static_cast<int32_t>(std::strlen(buf));
    if (trailingSlash) {
        buf[resultLength] = '\\';
        ++resultLength;
        buf[resultLength] = 0;
    }
    ++resultLength;
    if (!dest) dest = static_cast<char *>(std::malloc(static_cast<size_t>(resultLength)));
    else if (resultLength > maxlen) return nullptr;
    std::memcpy(dest, buf, static_cast<size_t>(resultLength));
    return dest;
}

char *__cdecl MakeAbsolutePath(char *dest, const char *path, int32_t maxlen)
{
    char buf[0x20c];
    const char *s = path;
    char first = s[0];
    int32_t drive = 0;
    char letter = 0;
    bool resolved = false;

    if (first != 0 && s[1] != 0) {
        char second = s[1];
        if (IsSlash(first) && IsSlash(second)) {
            // A UNC prefix: the base is "\\".
            buf[0] = '\\';
            buf[1] = '\\';
            buf[2] = 0;
            s += 2;
            return CombinePath(dest, s, maxlen, buf);
        }
        if (second == ':' && IsAsciiLetter(first)) {
            letter = first >= 'a' ? static_cast<char>(first - 0x20) : first;
            s += 2;
            drive = letter - 0x40;
            resolved = true;
        }
    }
    if (!resolved) {
        drive = ::_getdrive();
        letter = drive != 0 ? static_cast<char>(drive + 0x40) : 'C';
    }

    if (IsSlash(*s)) {
        buf[0] = letter;
        ++s;
        buf[1] = ':';
        buf[2] = '\\';
        buf[3] = 0;
    } else {
        if (::_getdcwd(drive, buf, 0x209) == nullptr) return nullptr;
        size_t length = std::strlen(buf);
        if (!IsSlash(buf[length - 1])) {
            buf[length] = '\\';
            buf[length + 1] = 0;
        }
    }
    return CombinePath(dest, s, maxlen, buf);
}

bool __cdecl AbsolutePathString(cRZString *out, const cRZString *in)
{
    char buf[0x20c];
    if (MakeAbsolutePath(buf, in->mString.c_str(), 0x208) == nullptr) return false;
    out->mString.assign(buf, std::strlen(buf));
    return true;
}

bool __cdecl CombinePathString(cRZString *out, const cRZString *in, const cRZString *base)
{
    char buf[0x20c];
    const char *baseText = base ? base->mString.c_str() : "";
    if (CombinePath(buf, in->mString.c_str(), 0x208, baseText) == nullptr) return false;
    out->mString.assign(buf, std::strlen(buf));
    return true;
}

bool __cdecl ResolvePathString(cRZString *out, const cRZString *in, const cRZString *base)
{
    if (IsCanonicalAbsolutePathString(in)) {
        AssignString(out->mString, in->mString);
        return true;
    }
    if (base) return CombinePathString(out, in, base);
    return AbsolutePathString(out, in);
}

std::string *__cdecl ConcatMove(std::string *out, const std::string *left, std::string *right)
{
    right->insert(0, left->c_str(), left->size());
    new (out) std::string(std::move(*right));
    return out;
}

namespace {

// The temporary ini file on "<app directory><app name>.ini", read for one value and destroyed.
struct TempIni {
    alignas(cRZIniFile) unsigned char raw[0x40];
    cRZIniFile &Ini() { return *reinterpret_cast<cRZIniFile *>(raw); }
};

} // namespace

} // namespace inifile

// ---- cGZFileSystem::DataDirectory (cIGZFileSystem slot 7, 0xc75ff9) and PlugInDirectory (slot 8, 0xc763f0) ----

const char *cGZFileSystem::DataDirectory()
{
    if (!mDataDirectorySet) {
        cRZString appDirectory;
        appDirectory.Construct(AppDirectory());
        cRZString appName;
        appName.Construct(AppName());
        std::string joined;
        inifile::ConcatMove(&joined, &appDirectory.mString, &appName.mString);
        joined.append(".ini", 4);
        cRZString iniPath;
        iniPath.Construct();
        iniPath.mString = joined;

        inifile::TempIni temp;
        cRZIniFile &ini = temp.Ini();
        ini.Construct(*iniPath.AsInterface());
        cRZString key, section;
        key.Construct("Data");
        section.Construct("Directories");
        ini.GetString(*section.AsInterface(), *key.AsInterface(), mDataDirectory.AsInterface());
        key.Destruct();
        section.Destruct();
        if (mDataDirectory.mString.empty()) inifile::AssignString(mDataDirectory.mString, appDirectory.mString);
        else inifile::ResolvePathString(&mDataDirectory, &mDataDirectory, &appDirectory);
        mDataDirectorySet = 1;
        ini.Destruct();
        iniPath.Destruct();
        appDirectory.Destruct();
    }
    return mDataDirectory.mString.c_str();
}

const char *cGZFileSystem::PlugInDirectory()
{
    if (!mPlugInDirectorySet) {
        cRZString appDirectory;
        appDirectory.Construct(AppDirectory());
        cRZString appName;
        appName.Construct(AppName());
        cRZString appDirectoryAgain;
        appDirectoryAgain.Construct(AppDirectory());
        std::string joined(appDirectoryAgain.mString);
        joined.append(appName.mString);
        joined.append(".ini", 4);
        cRZString iniPath;
        iniPath.Construct();
        iniPath.mString = joined;
        appDirectoryAgain.Destruct();
        appName.Destruct();

        inifile::TempIni temp;
        cRZIniFile &ini = temp.Ini();
        ini.Construct(*iniPath.AsInterface());
        cRZString key, section;
        key.Construct("PlugIn");
        section.Construct("Directories");
        ini.GetString(*section.AsInterface(), *key.AsInterface(), mPlugInDirectory.AsInterface());
        key.Destruct();
        section.Destruct();
        if (mPlugInDirectory.mString.empty()) {
            inifile::AssignString(mPlugInDirectory.mString, appDirectory.mString);
            mPlugInDirectory.mString.append(mPluginDirectoryName.mString);
            mPlugInDirectory.mString.push_back('\\');
        } else {
            inifile::ResolvePathString(&mPlugInDirectory, &mPlugInDirectory, &appDirectory);
        }
        mPlugInDirectorySet = 1;
        ini.Destruct();
        iniPath.Destruct();
        appDirectory.Destruct();
    }
    return mPlugInDirectory.mString.c_str();
}

