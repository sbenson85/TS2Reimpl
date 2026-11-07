#include "app/ReportBlockers.h"

#include "app/AppServices2.h"
#include "app/ReportText.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/GZCOM.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/RZTextConvert.h"
#include "rz/WinApiTables.h"

#define NOMINMAX
#include <windows.h>

#include <direct.h>

#include <cstring>
#include <list>
#include <new>
#include <string>

namespace appbase {
namespace {

constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c, kIatVirtualQuery = 0xd9a1cc;
// The wide directory functions (CreateDirectoryW at +0xc, RemoveDirectoryW at +0x10 of ts2_rva_10cb614, an owned static:
// rz/WinApiTables.h) are looked up at run time.

constexpr uint32_t kReportExtension = 0xda31c4, kDumpExtension = 0xda31cc, kNameFormat = 0xda33fc;
constexpr int kSlotSprintf = 31;

template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}

} // namespace

// ---- 0x1bbe01 ----

uint32_t __cdecl FindModuleOfAddress(const void *address, cIGZString *name, uint32_t *section, uint32_t *offset)
{
    *section = 0;
    *offset = 0;
    MEMORY_BASIC_INFORMATION info; // 0x1c bytes, filled by the call
    if (!Import<SIZE_T(WINAPI *)(const void *, MEMORY_BASIC_INFORMATION *, SIZE_T)>(kIatVirtualQuery)(address, &info, 0x1c)) return 0;
    const uint8_t *base = static_cast<const uint8_t *>(info.AllocationBase);
    char path[0x200];
    std::memset(path, 0, sizeof(path));
    if (!base) return 0;
    if (!Import<DWORD(WINAPI *)(const void *, char *, DWORD)>(kIatGetModuleFileNameA)(base, path, 0x1ff)) return 0;
    path[0x1ff] = 0;
    {
        alignas(cRZString) char storage[sizeof(cRZString)];
        auto *text = reinterpret_cast<cRZString *>(storage);
        text->Construct(path);
        as2::ConvertStringFrom(text->AsInterface(), RZText::kSystemCodePage, name);
        text->Destruct();
    }
    // The PE headers: e_lfanew at +0x3c, then the file header (NumberOfSections at +6,
    // SizeOfOptionalHeader at +0x14) and the 0x28-byte section headers after the optional header.
    const uint8_t *nt = base + *reinterpret_cast<const uint32_t *>(base + 0x3c);
    uint32_t count = *reinterpret_cast<const uint16_t *>(nt + 6);
    const uint8_t *sections = nt + 0x18 + *reinterpret_cast<const uint16_t *>(nt + 0x14);
    uint32_t rva = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(base));
    for (uint32_t index = 0; index < count; ++index, sections += 0x28) {
        uint32_t virtualSize = *reinterpret_cast<const uint32_t *>(sections + 8);
        uint32_t virtualAddress = *reinterpret_cast<const uint32_t *>(sections + 0xc);
        uint32_t rawSize = *reinterpret_cast<const uint32_t *>(sections + 0x10);
        if (rva < virtualAddress) continue;
        uint32_t size = rawSize < virtualSize ? virtualSize : rawSize;
        if (rva <= virtualAddress + size) {
            *section = index + 1;
            *offset = rva - virtualAddress;
            return 1;
        }
    }
    return 0;
}

// ---- 0x1c4caf ----

uint32_t __cdecl CreateDirectories(cRZString *path)
{
    alignas(cRZString) char pathStorage[sizeof(cRZString)];
    alignas(cRZString) char curStorage[sizeof(cRZString)];
    auto *whole = reinterpret_cast<cRZString *>(pathStorage);
    auto *current = reinterpret_cast<cRZString *>(curStorage);
    std::list<cRZString> created;
    whole->Construct(*path, 0, std::string::npos);
    current->Construct();
    std::string &text = whole->mString;
    uint32_t length = static_cast<uint32_t>(text.size());
    uint32_t at = 0;
    uint32_t result = 0;
    if (length != 0) {
        if (text[length - 1] != '\\' && text[length - 1] != '/') {
            text.push_back('\\');
            length = static_cast<uint32_t>(text.size());
        }
        // "\\server\": the first three backslashes are copied without being created.
        if (length >= 3 && text[0] == '\\' && text[1] == '\\' && text[2] != '\\') {
            uint32_t slashes = 0;
            while (at < length && slashes < 3) {
                if (text[at] == '\\') ++slashes;
                current->mString.push_back(text[at]);
                ++at;
            }
        }
        result = 1;
        using CreateW = BOOL(WINAPI *)(const wchar_t *, void *);
        using RemoveW = BOOL(WINAPI *)(const wchar_t *);
        for (; at < length; ++at) {
            char c = text[at];
            current->mString.push_back(c);
            if (c != '\\' && c != '/') continue;
            if (as2::DirectoryExists(current)) continue;
            CreateW createW = reinterpret_cast<CreateW>(ts2_rva_10cb614.createDirectory);
            RemoveW removeW = reinterpret_cast<RemoveW>(ts2_rva_10cb614.removeDirectory);
            if (createW && removeW) {
                std::u16string wide;
                as2::StringToUtf16(&wide, current->AsInterface()); // raw storage in the original: same layout
                bool ok = createW(reinterpret_cast<const wchar_t *>(wide.c_str()), nullptr) != 0;
                wide.~basic_string();
                if (!ok) {
                    // ORIGINAL-BUG: the directories made so far are removed in the order they were
                    // made, parents first, so every removal but the last fails on a non-empty
                    // directory and the partial tree stays.
                    for (const cRZString &made : created) {
                        std::u16string name;
                        as2::StringToUtf16(&name, made.AsInterface());
                        removeW(reinterpret_cast<const wchar_t *>(name.c_str()));
                    }
                    result = 0;
                    break;
                }
                created.push_back(*current);
            } else {
                alignas(cRZString) char convertedStorage[sizeof(cRZString)];
                auto *converted = reinterpret_cast<cRZString *>(convertedStorage);
                converted->Construct();
                RZText::ConvertStringToString(*current->AsInterface(), *converted->AsInterface(), -1);
                int status = _mkdir(converted->mString.c_str());
                if (status != 0) {
                    // ORIGINAL-BUG: same removal order as above (and here the unconverted text).
                    for (const cRZString &made : created) _rmdir(made.mString.c_str());
                    converted->Destruct();
                    result = 0;
                    break;
                }
                created.push_back(*current);
                converted->Destruct();
            }
        }
    }
    current->Destruct();
    whole->Destruct();
    return result;
}

// ---- 0x1bd49b ----

void __fastcall NameFiles(ExceptionReporterObject *self, void *)
{
    alignas(cRZString) char storage[sizeof(cRZString)];
    auto *directory = reinterpret_cast<cRZString *>(storage);
    directory->Construct(self->mDirectory, 0, std::string::npos);
    if (!as2::DirectoryExists(directory)) {
        CreateDirectories(directory);
        if (!as2::DirectoryExists(directory)) {
            alignas(cRZString) char moduleStorage[sizeof(cRZString)];
            auto *module = reinterpret_cast<cRZString *>(moduleStorage);
            module->Construct();
            GetModulePath(module->AsInterface());
            GetModuleDirectory(module->AsInterface(), directory->AsInterface());
            module->Destruct();
        }
    }
    cRZDate today;
    today.ConstructToday();
    cRZTime now;
    now.ConstructNow();
    uint32_t year = today.GetYear();
    uint32_t month = today.GetMonth();
    uint32_t day = today.GetDay();
    uint32_t hour = now.GetHour();
    uint32_t minute = now.GetMinute();
    uint32_t second = now.seconds % 60;
    using Sprintf = void(__cdecl *)(cRZString *, const char *, ...);
    const char *format = GameAddress<const char>(kNameFormat);
    const char *dir = directory->mString.c_str();
    VirtualSlot<Sprintf>(&self->mReportFile, kSlotSprintf)(&self->mReportFile, format, dir, self->mReportName.mString.c_str(), year, month,
                                                           day, hour, minute, second, GameAddress<const char>(kReportExtension));
    VirtualSlot<Sprintf>(&self->mDumpFile, kSlotSprintf)(&self->mDumpFile, format, dir, self->mDumpName.mString.c_str(), year, month, day, hour,
                                                         minute, second, GameAddress<const char>(kDumpExtension));
    directory->Destruct();
}

// ---- the text file's line reading ----

void __cdecl AssignText(const std::string *text, cRZString *dst)
{
    reinterpret_cast<RZStringSlot *>(&dst->mString)->Assign(*text);
}

void __cdecl AssignNarrowed(const std::u16string *wide, cRZString *dst)
{
    dst->mString.resize(0);
    int32_t count = static_cast<int32_t>(wide->size());
    for (int32_t i = 0; i < count; ++i) {
        char16_t unit = (*wide)[i];
        dst->mString.push_back(unit > 0x7f ? '?' : static_cast<char>(unit));
    }
}

namespace {

// cIGZFile / cIGZString slots the line reader calls.
constexpr int kSlotFileIsOpen = 4, kSlotFileOpen = 3, kSlotFilePosition = 6, kSlotFileLength = 7, kSlotFileSeekRelative = 11;
constexpr int kSlotFileSeekPosition = 12, kSlotFileRead = 14, kSlotStringFromChar = 3, kSlotStringResize = 18;

constexpr uint32_t kCarriageReturn = 0xd, kLineFeed = 0xa;
constexpr int32_t kChunk = 0x80; // bytes read at a time

} // namespace

uint32_t __fastcall ReadLineToString(cRZTextFile *file, void *, cIGZString *out)
{
    uint8_t result = 0;
    uint8_t wasOpen = VirtualCall<uint8_t(__thiscall *)(void *), kSlotFileIsOpen>(file);
    VirtualCall<void(__thiscall *)(void *, uint32_t), kSlotStringResize>(out, 0);
    if (!wasOpen && !(VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), kSlotFileOpen>(file, 1, 2, 1) & 0xff)) return result;

    int32_t position = VirtualCall<int32_t(__thiscall *)(void *), kSlotFilePosition>(file);
    int32_t length = VirtualCall<int32_t(__thiscall *)(void *), kSlotFileLength>(file);
    if (length <= position) return result;

    int32_t remaining = length - position;
    int32_t charSize = file->mCharSize;
    int32_t step = (charSize == 2 ? 1 : 0) + 1;
    bool foundTerminator = false;
    // The original's buffer holds 0x80 bytes and the wide path reads up to twice what it filled; the
    // bytes past the 0x80 are whatever the stack holds there, zero here.
    alignas(4) uint8_t buffer[0x100] = {};

    if (charSize == 2) file->mWide.resize(0);
    else file->mLine.resize(0);
    VirtualSlot<void(__thiscall *)(void *, uint32_t)>(&file->mText, kSlotStringResize)(&file->mText, 0);

    if (remaining != 0) {
        for (;;) {
            int32_t chunk = remaining > kChunk ? kChunk : remaining;
            if (!(VirtualCall<uint8_t(__thiscall *)(void *, void *, int32_t), kSlotFileRead>(file, buffer, chunk) & 0xff)) break;
            remaining -= chunk;
            int32_t index = 0;
            if (chunk > 0) {
                for (;;) {
                    uint32_t unit = charSize == 2 ? reinterpret_cast<const uint16_t *>(buffer)[index >> 1] : buffer[index];
                    if (unit == kCarriageReturn || unit == kLineFeed) {
                        foundTerminator = true;
                        VirtualCall<int32_t(__thiscall *)(void *, int32_t), kSlotFileSeekPosition>(file, position + index + step);
                        uint32_t next = 0; // the original leaves the other bytes of this local unset (only step are read and used)
                        if (VirtualCall<uint8_t(__thiscall *)(void *, void *, int32_t), kSlotFileRead>(file, &next, step) & 0xff) {
                            uint32_t following = charSize == 2 ? (next & 0xffff) : (next & 0xff);
                            bool terminator = following == kCarriageReturn || following == kLineFeed;
                            if (!terminator || following == unit) {
                                // Not a CR LF / LF CR pair: step back over the character just read.
                                VirtualCall<int32_t(__thiscall *)(void *, int32_t), kSlotFileSeekRelative>(file, (charSize != 2 ? 1 : 0) - 2);
                            }
                        }
                        break;
                    }
                    index += step;
                    if (index >= chunk) break;
                }
            }
            // ORIGINAL-BUG: the wide buffer takes `index` UTF-16 units, but index counts bytes (see the header).
            if (file->mCharSize == 2) file->mWide.append(reinterpret_cast<const char16_t *>(buffer), static_cast<size_t>(index));
            else file->mLine.append(reinterpret_cast<const char *>(buffer), static_cast<size_t>(index));
            result = 1;
            if (foundTerminator) break;
            position = VirtualCall<int32_t(__thiscall *)(void *), kSlotFilePosition>(file);
            if (remaining == 0) break;
        }
    }
    if (file->mCharSize == 2) AssignNarrowed(&file->mWide, &file->mText);
    else AssignText(&file->mLine, &file->mText);
    VirtualCall<void(__thiscall *)(void *, const char *, uint32_t), kSlotStringFromChar>(out, file->mText.mString.c_str(),
                                                                                              static_cast<uint32_t>(file->mText.mString.size()));
    return result;
}

uint32_t __fastcall ReadLineToBuffer(cRZTextFile *file, void *, char *buffer, uint32_t *length)
{
    alignas(cRZString) char storage[sizeof(cRZString)];
    auto *line = reinterpret_cast<cRZString *>(storage);
    line->Construct();
    uint8_t ok = ReadLineToString(file, nullptr, line->AsInterface()) & 0xff;
    if (ok) {
        uint32_t count = *length;
        if (line->mString.size() < count) count = static_cast<uint32_t>(line->mString.size());
        *length = count;
        std::memcpy(buffer, line->mString.c_str(), count);
    }
    line->Destruct();
    return ok ? 1 : 0;
}

uint32_t __cdecl ReadFileLine(const char *path, int32_t lineIndex, cRZString *out)
{
    out->mString.resize(0);
    int32_t wanted = lineIndex >= 0 ? lineIndex : 0;
    alignas(cRZTextFile) char storage[sizeof(cRZTextFile)];
    auto *file = reinterpret_cast<cRZTextFile *>(storage);
    file->Construct(path);
    uint32_t ok = 0;
    if (file->mOpen || file->Open(1, 2, 1)) {
        char buffer[0x800];
        int32_t read = -1;
        uint32_t size = 0;
        do {
            size = 0x800;
            if (!ReadLineToBuffer(file, nullptr, buffer, &size)) break;
            ++read;
        } while (read < wanted);
        if (read == wanted) {
            out->mString.assign(buffer, size);
            ok = 1;
        }
        file->Close();
    }
    file->Destruct();
    return ok;
}

} // namespace appbase
