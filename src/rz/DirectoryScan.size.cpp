// DirectoryScan (src/rz/DirectoryScan.h).
#include "rz/DirectoryScan.h"

#include "game/game.h"
#include "game/import.h"
#include "game/vtable.h"
#include "gz/GZCOM.h"
#include "rz/RZTextConvert.h"
#include "rz/WinApiTables.h"

#include <windows.h>

#include <cstring>
#include <cwchar>
#include <new>
#include <string>

namespace {

// A cRZString on the stack (the game's vtable), destroyed at the end of its scope.
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

bool EndsInSeparator(const std::string &text)
{
    return !text.empty() && (text.back() == '\\' || text.back() == '/');
}

// What the dot rules make of one find result: whether to keep it (Scan and ListMatching share the rule; `keepDot` /
// `keepDotDot` say what the first "." / ".." folder gets, later ones are kept like any folder).
struct DotRule {
    bool seenDot = false;
    bool seenDotDot = false;
    bool Keep(const std::string &name, bool keepDot, bool keepDotDot)
    {
        if (!seenDot && name == ts2_rva_ff2470) {
            seenDot = true;
            return keepDot;
        }
        if (!seenDotDot && name == ts2_rva_ff2474) {
            seenDotDot = true;
            return keepDotDot;
        }
        return true;
    }
};

} // namespace

namespace dscan {

Path::~Path()
{
    VirtualCall<void *(__thiscall *)(void *, uint32_t), kSlotDeletingDestructor>(&s, 0);
}

void Entry::FullPath(cRZString *out) const
{
    if (mScan == nullptr) {
        out->mString.erase(0);
    } else {
        out->mString = mScan->mDirectory.mString;
    }
    out->mString.append(mName.mString);
}

bool __cdecl FindNext(as2::FindNode *node)
{
    if (node == nullptr) return false;
    using FindNextW = BOOL(__stdcall *)(HANDLE, WIN32_FIND_DATAW *);
    using FindNextA = BOOL(__stdcall *)(HANDLE, WIN32_FIND_DATAA *);
    DWORD attributes;
    if (FindNextW wide = reinterpret_cast<FindNextW>(ts2_rva_10cb614.findNextFile)) {
        WIN32_FIND_DATAW data; // left unset until the call fills it
        if (!wide(node->mHandle, &data)) return false;
        const uint32_t length = static_cast<uint32_t>(std::wcslen(data.cFileName));
        ConvertBytesToString(data.cFileName, length, RZText::kUtf16, node->mName.AsInterface());
        attributes = data.dwFileAttributes;
    } else {
        WIN32_FIND_DATAA data;
        if (!reinterpret_cast<FindNextA>(ts2_rva_d9a238)(node->mHandle, &data)) return false;
        node->mName.mString.assign(data.cFileName, std::strlen(data.cFileName));
        as2::ConvertStringFrom(node->mName.AsInterface(), RZText::kSystemCodePage, node->mName.AsInterface());
        attributes = data.dwFileAttributes;
    }
    node->mIsDirectory = (attributes >> 4) & 1;
    return true;
}

} // namespace dscan

using dscan::Entry;
using dscan::Path;
using dscan::PathList;

DirectoryScan::DirectoryScan(const cRZString *directory, bool scanNow, bool keepDotDot, bool keepDot)
    : mCount(0), mKeepDotDot(keepDotDot), mKeepDot(keepDot), mDepth(0), mCursorIndex(-1), mCursor()
{
    mDirectory.Construct(*directory, 0, 0xffffffffu);
    mPattern.Construct(ts2_rva_ff2478);
    EndDirectory();
    if (scanNow) Scan();
}

DirectoryScan::~DirectoryScan()
{
    mPattern.Destruct();
    mDirectory.Destruct();
}

bool DirectoryScan::Scan()
{
    LocalString path;
    path.s.mString = mDirectory.mString + mPattern.mString;
    Entry entry(nullptr);
    DotRule dots;
    mEntries.clear();
    mCursorIndex = -1;
    mCount = 0;
    as2::FindNode *node = as2::FindFirst(&path.s);
    if (node == nullptr) return false;
    entry.mScan = this;
    bool found = true;
    while (found) {
        entry.mName.mString = node->mName.mString;
        bool keep = true;
        if (node->mIsDirectory) {
            entry.mKind = dscan::kFolder;
            keep = dots.Keep(entry.mName.mString, mKeepDot, mKeepDotDot);
        } else {
            entry.mKind = dscan::kFile;
        }
        if (keep) {
            mEntries.push_back(entry);
            ++mCount;
        }
        found = dscan::FindNext(node);
    }
    as2::CloseFind(node);
    // A "*" search that didn't list a wanted "." / "..": put them first, ".." then "." in front of it.
    const char *wildcard = ts2_rva_ff2478;
    if (mKeepDotDot && !dots.seenDotDot && mPattern.mString == wildcard) {
        LocalString name(ts2_rva_ff2474);
        if (!HasEntry(&name.s)) {
            entry.mName.mString = name.s.mString;
            entry.mKind = dscan::kFolder;
            mEntries.push_front(entry);
            ++mCount;
        }
    }
    if (mKeepDot && !dots.seenDot && mPattern.mString == wildcard) {
        LocalString name(ts2_rva_ff2470);
        if (!HasEntry(&name.s)) {
            entry.mName.mString = name.s.mString;
            entry.mKind = dscan::kFolder;
            mEntries.push_front(entry);
            ++mCount;
        }
    }
    return true;
}

bool DirectoryScan::SetDirectory(const cRZString *directory, bool scanNow)
{
    mDirectory.mString = directory->mString;
    EndDirectory();
    if (scanNow) Scan();
    return true;
}

void DirectoryScan::SetPattern(const cRZString *pattern, bool scanNow)
{
    mPattern.mString = pattern->mString;
    if (mPattern.mString.empty()) mPattern.mString.assign(ts2_rva_ff2478);
    if (scanNow) Scan();
}

void DirectoryScan::EndDirectory()
{
    if (!EndsInSeparator(mDirectory.mString)) mDirectory.mString.push_back('\\');
}

Entry *DirectoryScan::EntryAt(uint32_t index)
{
    if (mCursorIndex != -1) {
        // One step from the last entry asked for. ORIGINAL-BUG: stepping past either end lands on the list's head
        // node, whose entry is unconstructed memory, and returns it.
        const uint32_t cursor = static_cast<uint32_t>(mCursorIndex);
        if (index == cursor + 1) {
            ++mCursor;
            mCursorIndex = static_cast<int32_t>(index);
            return &*mCursor;
        }
        if (index == cursor - 1) {
            --mCursor;
            mCursorIndex = static_cast<int32_t>(index);
            return &*mCursor;
        }
    }
    auto it = mEntries.begin();
    for (uint32_t i = 0; i < mCount; ++i, ++it) {
        if (i == index) {
            mCursorIndex = static_cast<int32_t>(index);
            mCursor = it;
            return &*it;
        }
    }
    return nullptr;
}

bool DirectoryScan::HasEntry(const cRZString *name) const
{
    for (const Entry &entry : mEntries) {
        if (entry.mName.mString == name->mString) return true;
    }
    return false;
}

bool DirectoryScan::ListMatching(PathList *out, uint32_t select)
{
    LocalString path;
    path.s.mString = mDirectory.mString + mPattern.mString;
    DotRule dots;
    as2::FindNode *node = as2::FindFirst(&path.s);
    if (node == nullptr) return false;
    bool found = true;
    while (found) {
        bool keep;
        if (!node->mIsDirectory) {
            keep = (select & dscan::kSelectFiles) != 0;
        } else if ((select & dscan::kSelectFolders) == 0) {
            keep = false;
        } else {
            keep = dots.Keep(node->mName.mString, (select & dscan::kSelectDot) != 0, (select & dscan::kSelectDotDot) != 0);
        }
        if (keep) out->emplace_back(node->mName);
        found = dscan::FindNext(node);
    }
    as2::CloseFind(node);
    const char *wildcard = ts2_rva_ff2478;
    if ((select & dscan::kSelectDotDot) != 0 && !dots.seenDotDot && mPattern.mString == wildcard) {
        LocalString name(ts2_rva_ff2474);
        out->emplace_front(name.s);
    }
    if ((select & dscan::kSelectDot) != 0 && !dots.seenDot && mPattern.mString == wildcard) {
        LocalString name(ts2_rva_ff2470);
        out->emplace_front(name.s);
    }
    return true;
}

bool DirectoryScan::ListFiles(PathList *out)
{
    return ListMatching(out, dscan::kSelectFiles);
}

bool DirectoryScan::ListFolders(PathList *out, uint32_t dots)
{
    return ListMatching(out, (dots & (dscan::kSelectDot | dscan::kSelectDotDot)) | dscan::kSelectFolders);
}

bool DirectoryScan::ListFilesBelow(const cRZString *directory, PathList *out, bool includeTop)
{
    PathList files;
    PathList folders;
    LocalString below;
    LocalString pattern;
    ++mDepth;
    SetDirectory(directory, true);
    if (includeTop || mDepth > 1) {
        ListFiles(&files);
        for (const Path &file : files) {
            LocalString full;
            full.s.mString = directory->mString;
            if (!EndsInSeparator(full.s.mString)) full.s.mString.push_back('\\');
            full.s.mString.append(file.s.mString);
            out->emplace_back(full.s);
        }
    }
    // The folders below, whatever the pattern.
    pattern.s.mString = mPattern.mString;
    mPattern.mString.assign(ts2_rva_ff2478);
    ListFolders(&folders, 0);
    mPattern.mString = pattern.s.mString;
    for (const Path &folder : folders) {
        below.s.mString = directory->mString + folder.s.mString;
        below.s.mString.push_back('\\');
        ListFilesBelow(&below.s, out, includeTop);
    }
    --mDepth;
    return true;
}

// ---- adapters ----

DirectoryScan *__fastcall DirectoryScanAdapters::Construct(void *memory, void *, const cRZString *directory, bool scanNow,
                                                           bool keepDotDot, bool keepDot)
{
    return ::new (memory) DirectoryScan(directory, scanNow, keepDotDot, keepDot);
}

void __fastcall DirectoryScanAdapters::Destruct(DirectoryScan *self, void *)
{
    self->~DirectoryScan();
}

bool __fastcall DirectoryScanAdapters::Scan(DirectoryScan *self, void *)
{
    return self->Scan();
}

bool __fastcall DirectoryScanAdapters::SetDirectory(DirectoryScan *self, void *, const cRZString *directory, bool scanNow)
{
    return self->SetDirectory(directory, scanNow);
}

void __fastcall DirectoryScanAdapters::SetPattern(DirectoryScan *self, void *, const cRZString *pattern, bool scanNow)
{
    self->SetPattern(pattern, scanNow);
}

void __fastcall DirectoryScanAdapters::EndDirectory(DirectoryScan *self, void *)
{
    self->EndDirectory();
}

Entry *__fastcall DirectoryScanAdapters::EntryAt(DirectoryScan *self, void *, uint32_t index)
{
    return self->EntryAt(index);
}

bool __fastcall DirectoryScanAdapters::HasEntry(DirectoryScan *self, void *, const cRZString *name)
{
    return self->HasEntry(name);
}

void __fastcall DirectoryScanAdapters::FullPath(const Entry *self, void *, cRZString *out)
{
    self->FullPath(out);
}

bool __fastcall DirectoryScanAdapters::ListMatching(DirectoryScan *self, void *, PathList *out, uint32_t select)
{
    return self->ListMatching(out, select);
}

bool __fastcall DirectoryScanAdapters::ListFiles(DirectoryScan *self, void *, PathList *out)
{
    return self->ListFiles(out);
}

bool __fastcall DirectoryScanAdapters::ListFolders(DirectoryScan *self, void *, PathList *out, uint32_t dots)
{
    return self->ListFolders(out, dots);
}

bool __fastcall DirectoryScanAdapters::ListFilesBelow(DirectoryScan *self, void *, const cRZString *directory, PathList *out,
                                                      bool includeTop)
{
    return self->ListFilesBelow(directory, out, includeTop);
}
