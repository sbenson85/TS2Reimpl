// 0x1c2068 PathFileTime and 0x1c2e25 PathDelete (lane QK). See PathFileOps.h.
#include "rz/PathFileOps.h"

#include "app/AppServices2.h"
#include "game/game.h"
#include "game/import.h"
#include "gz/GZCOM.h"
#include "rz/RZTextChars.h"
#include "rz/cRZFile.h"
#include "rz/RZTextConvert.h"
#include "rz/WinApiTables.h"
#include "rz/cRZString.h"

#define NOMINMAX
#include <windows.h>

#include <sys/stat.h>

#include <ctime>
#include <string>

namespace {

using DeleteFileWFn = BOOL(WINAPI *)(LPCWSTR);
using DeleteFileAFn = BOOL(WINAPI *)(LPCSTR);
using GetTempPathWFn = DWORD(WINAPI *)(DWORD, LPWSTR);
using GetTempPathAFn = DWORD(WINAPI *)(DWORD, LPSTR);
using GetLongPathNameWFn = DWORD(WINAPI *)(LPCWSTR, LPWSTR, DWORD);
using GetLongPathNameAFn = DWORD(WINAPI *)(LPCSTR, LPSTR, DWORD);

// The path's text on the system code page, in a local cRZString (0x144f5b, RZText::ConvertStringToString).
struct AnsiText {
    cRZString text;
    explicit AnsiText(const cIGZString &path)
    {
        text.Construct();
        RZText::ConvertStringToString(path, *text.AsInterface(), RZText::kSystemCodePage);
    }
    ~AnsiText() { text.Destruct(); }
    AnsiText(const AnsiText &) = delete;
    AnsiText &operator=(const AnsiText &) = delete;
};

} // namespace

uint32_t __cdecl PathFileTime(const cIGZString *path, int32_t which)
{
    AnsiText ansi(*path);
    struct _stat64i32 info;
    if (_stat64i32(ansi.text.mString.c_str(), &info) != 0) return 0;
    switch (which) {
    case 1: return static_cast<uint32_t>(info.st_ctime);
    case 2: return static_cast<uint32_t>(info.st_mtime);
    case 3: return static_cast<uint32_t>(info.st_atime);
    default: return 0;
    }
}

bool __cdecl PathDelete(const cIGZString *path)
{
    if (auto deleteFile = reinterpret_cast<DeleteFileWFn>(ts2_rva_10cb5e0.deleteFile)) {
        alignas(std::u16string) unsigned char storage[sizeof(std::u16string)];
        std::u16string *wide = RZText::ToUtf16(reinterpret_cast<std::u16string *>(storage), *path);
        const BOOL done = deleteFile(reinterpret_cast<const wchar_t *>(wide->c_str()));
        wide->~basic_string();
        return done != 0;
    }
    AnsiText ansi(*path);
    return reinterpret_cast<DeleteFileAFn>(ts2_rva_d9a1f0)(ansi.text.mString.c_str()) != 0;
}

// ---- Temporary file names ---------------------------------------------------------------------------------------------------

bool __cdecl GetTempDirectory(cIGZString *out)
{
    auto tempPathW = reinterpret_cast<GetTempPathWFn>(ts2_rva_10cb5e0.getTempPath);
    auto longPathW = reinterpret_cast<GetLongPathNameWFn>(ts2_rva_10cb5e0.getLongPathName);
    if (tempPathW && longPathW) {
        wchar_t buffer[0x104];
        DWORD length = tempPathW(0x104, buffer);
        if (length == 0) return false;
        length = longPathW(buffer, buffer, 0x104);
        ConvertBytesToString(buffer, length, RZText::kUtf16, out);
        return true;
    }
    char buffer[0x104];
    DWORD length = reinterpret_cast<GetTempPathAFn>(ts2_rva_d9a21c)(0x104, buffer);
    if (length == 0) return false;
    if (auto longPathA = reinterpret_cast<GetLongPathNameAFn>(ts2_rva_10cb5e0.getLongPathNameA)) {
        length = longPathA(buffer, buffer, 0x104);
    }
    cRZString text;
    text.Construct();
    text.FromChar(buffer, length);
    as2::ConvertStringFrom(text.AsInterface(), RZText::kSystemCodePage, out);
    text.Destruct();
    return true;
}

void __cdecl TrimToByteLength(cRZString *text, uint32_t maxBytes)
{
    std::string &bytes = text->mString;
    const uint32_t size = static_cast<uint32_t>(bytes.size());
    uint32_t kept = 0;
    if (size != 0) {
        do {
            const uint32_t step = RZText::CharLength(bytes.data() + kept, size - kept, RZText::kDefault);
            if (step == 0 || kept + step > maxBytes) break;
            kept += step;
        } while (kept < size);
        if (kept < size) bytes.erase(kept);
    }
}

namespace {

// The loop both name makers share. `directoryText` yields the directory's text (called once for each name tried; the first
// maker reads it through its string's ToChar slot, the second from its local string), `prefix` is the cut-down copy of the
// caller's text, `suffix` the text after the time.
template <typename DirectoryText>
bool TryTempNames(DirectoryText directoryText, cIGZString *name, cRZString &prefix, cRZString &suffix, int32_t seconds)
{
    cRZString path;
    path.Construct();
    bool made = false;
    for (int attempt = 0; attempt < 10000; ++attempt) {
        cRZString unused; // the original builds a second, never-used string for each name
        unused.Construct();
        path.Sprintf("%s%s%d%s", directoryText(), prefix.mString.c_str(), seconds, suffix.mString.c_str());
        alignas(cRZFile) unsigned char storage[sizeof(cRZFile)];
        cRZFile *file = reinterpret_cast<cRZFile *>(storage);
        file->Construct(*path.AsInterface());
        const bool opened = file->Open(3, 0, 0);
        if (opened) {
            file->Close();
            name->FromChar(path.mString.c_str(), static_cast<uint32_t>(path.mString.size()));
            file->Destruct();
            unused.Destruct();
            made = true;
            break;
        }
        --seconds;
        file->Destruct();
        unused.Destruct();
    }
    path.Destruct();
    return made;
}

// The prefix and suffix texts of a name: the caller's text cut to 244 bytes ("RZ_" when nothing is left), the suffix text or ".tmp".
void BuildNameParts(const cIGZString *name, const cIGZString *suffixText, cRZString &prefix, cRZString &suffix)
{
    prefix.Construct(*name);
    suffix.Construct();
    if (suffixText) {
        suffix.Assign(*suffixText);
    } else {
        suffix.FromChar(".tmp", 4);
    }
    TrimToByteLength(&prefix, 0xf4);
    if (prefix.mString.empty()) prefix.FromChar("RZ_", 3);
}

} // namespace

bool __cdecl MakeTempFileIn(const cIGZString *directory, cIGZString *name, const cIGZString *suffixText)
{
    const int32_t seconds = static_cast<int32_t>(_time64(nullptr));
    cRZString prefix, suffix;
    BuildNameParts(name, suffixText, prefix, suffix);
    const bool made = TryTempNames([directory] { return directory->ToChar(); }, name, prefix, suffix, seconds);
    suffix.Destruct();
    prefix.Destruct();
    return made;
}

bool __cdecl MakeTempFile(cIGZString *name, const cIGZString *suffixText)
{
    const int32_t seconds = static_cast<int32_t>(_time64(nullptr));
    cRZString directory;
    directory.Construct();
    GetTempDirectory(directory.AsInterface());
    cRZString prefix, suffix;
    BuildNameParts(name, suffixText, prefix, suffix);
    const bool made = TryTempNames([&directory] { return directory.mString.c_str(); }, name, prefix, suffix, seconds);
    suffix.Destruct();
    prefix.Destruct();
    directory.Destruct();
    return made;
}
