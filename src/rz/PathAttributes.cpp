// 0x1c1931 PathAttributesMatch (lane MA). See PathAttributes.h.
#include "rz/PathAttributes.h"

#include "game/game.h"
#include "game/import.h"
#include "rz/RZTextConvert.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"

#define NOMINMAX
#include <windows.h>

#include <string>

namespace {

using GetFileAttributesWFn = DWORD(WINAPI *)(LPCWSTR);
using GetFileAttributesAFn = DWORD(WINAPI *)(LPCSTR);
constexpr uint32_t kGetFileAttributesW = 0x10cb5f4; // the game's table of wide file functions (cRZFile's)
constexpr uint32_t kIatGetFileAttributesA = 0xd9a204; // the import slot the original calls through
constexpr int32_t kShortcutBufferSize = 0x104;
constexpr uint32_t kShortcutBufferBytes = 0x108; // the original's local buffer

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

DWORD Attributes(const cIGZString &path)
{
    if (auto getAttributes = *GameAddress<GetFileAttributesWFn>(kGetFileAttributesW)) {
        alignas(std::u16string) unsigned char storage[sizeof(std::u16string)];
        std::u16string *wide = RZText::ToUtf16(reinterpret_cast<std::u16string *>(storage), path);
        const DWORD attributes = getAttributes(reinterpret_cast<const wchar_t *>(wide->c_str()));
        wide->~basic_string();
        return attributes;
    }
    AnsiText ansi(path);
    return ImportFunction<GetFileAttributesAFn, kIatGetFileAttributesA>()(ansi.text.mString.c_str());
}

} // namespace

bool __cdecl PathAttributesMatch(const cIGZString *path, uint32_t flags)
{
    using namespace pathattr;
    const DWORD attributes = Attributes(*path);
    if ((flags & kNotReadOnly) && (attributes & FILE_ATTRIBUTE_READONLY)) return false;
    if ((flags & kDirectory) && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    if ((flags & kShortcut) && !(flags & kSkipShortcut)) {
        char target[kShortcutBufferBytes];
        AnsiText ansi(*path);
        if (cRZFile::ResolveShortcutA(ansi.text.mString.c_str(), target, kShortcutBufferSize) == 0) return false;
    }
    if ((flags & kArchive) && !(attributes & FILE_ATTRIBUTE_ARCHIVE)) return false;
    if ((flags & kHidden) && !(attributes & FILE_ATTRIBUTE_HIDDEN)) return false;
    if ((flags & kSystem) && !(attributes & FILE_ATTRIBUTE_SYSTEM)) return false;
    if (flags & kReadOnlyAnswer) return (attributes & FILE_ATTRIBUTE_READONLY) != 0;
    return true;
}
