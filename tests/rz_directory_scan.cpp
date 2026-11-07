// Differential tests for src/rz/DirectoryScan.*: the directory scan's members on a fake file system, and FindNext.
//
// FindFirstFileA / FindNextFileA / FindClose are the image's import slots pointed at a scripted file system (directories
// keyed by their path with every '/' read as '\\'; a pattern's mask is "*", "*.<ext>" or an exact name); both sides'
// searches get the same handles (numbered from the same start for each side) and every call is logged and compared.
// The scan objects live in filled memory and are compared through the class's own layout (the same as the original's):
// every field, the list's entries (owner as "self", kind, name), the cursor as a position in the list (only while the
// index isn't -1: 0x1c5047 reads the cursor only then, and Scan leaves a freed node there), and the caller lists as their
// strings. Each side's members are called at their own entry: the original's RVAs, ours through the hook adapters
// (__fastcall called as the original's __thiscall).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/DirectoryScan.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kBuild = 0x1c48da;
constexpr uint32_t kDestroy = 0x1c49eb;
constexpr uint32_t kScan = 0x1c5896;
constexpr uint32_t kSetDirectory = 0x1c4a45;
constexpr uint32_t kSetPattern = 0x1c63d4;
constexpr uint32_t kEndDirectory = 0x1c539a;
constexpr uint32_t kEntryAt = 0x1c5047;
constexpr uint32_t kHasEntry = 0x1c5000;
constexpr uint32_t kFullPath = 0x1c509f;
constexpr uint32_t kListMatching = 0x1c5db7;
constexpr uint32_t kListFiles = 0x1c5bbd;
constexpr uint32_t kListFolders = 0x1c587f;
constexpr uint32_t kListFilesBelow = 0x1c5bcb;
constexpr uint32_t kFindNext = 0x1c5678;
constexpr uint32_t kIatFindFirstFileA = 0xd9a1f8;
constexpr uint32_t kIatFindClose = 0xd9a1f4;
constexpr uint32_t kFindFirstFileWPointer = 0x10cb618;
constexpr uint32_t kGameNew = 0x824632;
constexpr uint32_t kGameSizedDelete = 0x824624;

// ---- the game's operator new and sized delete, ours ----

void *__cdecl GameNew(size_t n)
{
    return ::operator new(n);
}

void __cdecl GameDelete(void *p, size_t n)
{
    ::operator delete(p, n);
}

// Points slots of the image at fakes; restores them on destruction.
class Slots {
public:
    explicit Slots(difftest::Context &c) : ctx(c) {}
    ~Slots()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }
    void Set(uint32_t rva, void *value)
    {
        void **slot = ctx.image.At<void *>(rva);
        saved.emplace_back(rva, *slot);
        *slot = value;
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
};

// ---- the fake file system ----

struct FakeEntry {
    std::string name;
    DWORD attributes;
};

struct Search {
    std::vector<FakeEntry> found;
    size_t next = 0;
};

std::map<std::string, std::vector<FakeEntry>> gTree; // directory (ending in '\\') -> its entries
std::map<uintptr_t, Search> gSearches;
uintptr_t gNextHandle = 0;
std::vector<std::string> gLog;

std::string Key(std::string path)
{
    for (char &c : path)
        if (c == '/') c = '\\';
    return path;
}

bool Matches(const std::string &name, const std::string &mask)
{
    if (mask == "*") return true;
    if (mask.size() > 1 && mask[0] == '*') return name.size() >= mask.size() - 1 && name.compare(name.size() - (mask.size() - 1), std::string::npos, mask, 1) == 0;
    return name == mask;
}

void Fill(WIN32_FIND_DATAA *data, const FakeEntry &entry)
{
    std::memset(data, 0xa5, sizeof(*data));
    data->dwFileAttributes = entry.attributes;
    std::memset(data->cFileName, 0, sizeof(data->cFileName));
    std::memcpy(data->cFileName, entry.name.c_str(), entry.name.size());
}

HANDLE WINAPI FakeFindFirstA(const char *pattern, WIN32_FIND_DATAA *data)
{
    gLog.push_back(std::string("F ") + pattern);
    const std::string text = Key(pattern);
    const size_t cut = text.rfind('\\');
    const std::string dir = cut == std::string::npos ? std::string() : text.substr(0, cut + 1);
    const std::string mask = cut == std::string::npos ? text : text.substr(cut + 1);
    auto it = gTree.find(dir);
    if (it == gTree.end()) return INVALID_HANDLE_VALUE;
    Search search;
    for (const FakeEntry &entry : it->second)
        if (Matches(entry.name, mask)) search.found.push_back(entry);
    if (search.found.empty()) return INVALID_HANDLE_VALUE;
    Fill(data, search.found[0]);
    search.next = 1;
    const uintptr_t handle = gNextHandle++;
    gSearches[handle] = search;
    return reinterpret_cast<HANDLE>(handle);
}

BOOL WINAPI FakeFindNextA(HANDLE handle, WIN32_FIND_DATAA *data)
{
    char line[32];
    std::snprintf(line, sizeof(line), "N %x", static_cast<unsigned>(reinterpret_cast<uintptr_t>(handle)));
    gLog.push_back(line);
    auto it = gSearches.find(reinterpret_cast<uintptr_t>(handle));
    if (it == gSearches.end() || it->second.next >= it->second.found.size()) return FALSE;
    Fill(data, it->second.found[it->second.next++]);
    return TRUE;
}

BOOL WINAPI FakeFindClose(HANDLE handle)
{
    char line[32];
    std::snprintf(line, sizeof(line), "C %x", static_cast<unsigned>(reinterpret_cast<uintptr_t>(handle)));
    gLog.push_back(line);
    gSearches.erase(reinterpret_cast<uintptr_t>(handle));
    return TRUE;
}

// A random tree under `root`: files, folders (some without a listing of their own), "." / ".." now and then (folders,
// sometimes twice, once in a while as files).
void AddDirectory(std::mt19937 &rng, const std::string &path, int depth, std::vector<std::string> &dirs, std::vector<std::string> &names)
{
    static const char *const kFiles[] = {"a.txt", "b.dat", "c.TXT", "x", "long name of a file.txt", "caf\xe9.txt", "readme"};
    static const char *const kFolders[] = {"sub", "Docs", "deep folder", "\xe4rger", "n.txt"};
    std::vector<FakeEntry> entries;
    const int count = int(rng() % 9);
    for (int i = 0; i < count; ++i) {
        const uint32_t what = rng() % 10;
        if (what < 5) {
            const DWORD attributes = rng() % 4 == 0 ? (rng() & ~0x10u) : 0x20;
            entries.push_back({kFiles[rng() % std::size(kFiles)], attributes});
        } else if (what < 8) {
            const DWORD attributes = rng() % 4 == 0 ? (rng() | 0x10u) : 0x10;
            entries.push_back({kFolders[rng() % std::size(kFolders)], attributes});
        } else {
            entries.push_back({rng() % 2 ? "." : "..", rng() % 6 == 0 ? 0x20u : 0x10u});
        }
    }
    if (rng() % 2) entries.insert(entries.begin(), {".", 0x10});
    if (rng() % 2) entries.insert(entries.begin() + (entries.size() > 1 ? 1 : 0), {"..", 0x10});
    gTree[Key(path)] = entries;
    dirs.push_back(path);
    for (const FakeEntry &entry : entries) {
        names.push_back(entry.name);
        if ((entry.attributes & 0x10) == 0 || entry.name == "." || entry.name == "..") continue;
        if (depth < 3 && rng() % 4 != 0 && gTree.count(Key(path + entry.name + "\\")) == 0) {
            AddDirectory(rng, path + entry.name + "\\", depth + 1, dirs, names);
        }
    }
}

// ---- strings and the state of a side ----

struct Str {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    explicit Str(const std::string &text)
    {
        std::memset(raw, 0xcd, sizeof(raw));
        S().Construct(text.c_str());
        S().mString.assign(text); // embedded NULs too
    }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(raw); }
};

std::string Text(const cRZString &s)
{
    char line[48];
    std::snprintf(line, sizeof(line), "[vt %08x refs %x cap %u]", shape::P(s.mVTable) - shape::P(GameAddress<void>(cRZString::kVTable)),
                  s.mRefCount, static_cast<unsigned>(s.mString.capacity() > 15 ? 16 : s.mString.capacity()));
    return s.mString + line;
}

using EntryList = std::list<dscan::Entry>;

// Where `node` sits in the scan's list: "#k", "head", "null" or "other".
std::string Position(const DirectoryScan *scan, const void *value)
{
    if (value == nullptr) return "null";
    const EntryList &list = scan->mEntries;
    if (value == &*list.end()) return "head";
    int k = 0;
    for (const dscan::Entry &e : list) {
        if (&e == value) return "#" + std::to_string(k);
        ++k;
    }
    return "other";
}

std::vector<std::string> State(const DirectoryScan *scan)
{
    std::vector<std::string> s;
    char line[96];
    std::snprintf(line, sizeof(line), "size %u count %u flags %02x %02x pad %02x %02x depth %d index %d", static_cast<unsigned>(scan->mEntries.size()),
                  scan->mCount, scan->mKeepDotDot, scan->mKeepDot, scan->mPad4e[0], scan->mPad4e[1], scan->mDepth, scan->mCursorIndex);
    s.push_back(line);
    if (scan->mCursorIndex != -1) s.push_back("cursor " + Position(scan, &*scan->mCursor));
    s.push_back("dir " + Text(scan->mDirectory));
    s.push_back("pattern " + Text(scan->mPattern));
    for (const dscan::Entry &e : scan->mEntries) {
        std::snprintf(line, sizeof(line), "entry %s kind %x ", e.mScan == scan ? "self" : e.mScan == nullptr ? "null" : "other", e.mKind);
        s.push_back(line + Text(e.mName));
    }
    return s;
}

std::vector<std::string> Listed(const dscan::PathList &list)
{
    std::vector<std::string> s;
    for (const dscan::Path &p : list) s.push_back(Text(p.s));
    return s;
}

// ---- the members, a side at a time ----

using BuildFn = void *(__thiscall *)(void *, const cRZString *, uint32_t, uint32_t, uint32_t);
using VoidFn = void(__thiscall *)(void *);
using ScanFn = uint32_t(__thiscall *)(void *);
using SetFn = uint32_t(__thiscall *)(void *, const cRZString *, uint32_t);
using EntryAtFn = void *(__thiscall *)(void *, uint32_t);
using HasEntryFn = uint32_t(__thiscall *)(void *, const cRZString *);
using FullPathFn = void(__thiscall *)(void *, cRZString *);
using ListFn = uint32_t(__thiscall *)(void *, dscan::PathList *, uint32_t);
using ListFilesFn = uint32_t(__thiscall *)(void *, dscan::PathList *);
using BelowFn = uint32_t(__thiscall *)(void *, const cRZString *, dscan::PathList *, uint32_t);

template <class Fn> Fn Pick(difftest::Context &ctx, bool ours, uint32_t rva, const void *adapter)
{
    return ours ? reinterpret_cast<Fn>(const_cast<void *>(adapter)) : ctx.Original<Fn>(rva);
}

#define ADAPTER(name) reinterpret_cast<const void *>(&DirectoryScanAdapters::name)

enum Op : int { kOpScan, kOpSetDirectory, kOpSetPattern, kOpEnd, kOpEntryAt, kOpHasEntry, kOpFullPath, kOpListMatching, kOpListFiles, kOpListFolders, kOpBelow, kOpCount };

struct Call {
    int op;
    std::string text;
    uint32_t word;
    uint32_t flag;
};

// One call on one side; returns the result as text (al for the bools, a position for EntryAt) and what a caller's list
// or string received.
std::string Run(difftest::Context &ctx, bool ours, DirectoryScan *scan, const Call &c, std::vector<std::string> &listed)
{
    char line[64];
    uint32_t r = 0;
    switch (c.op) {
    case kOpScan:
        r = Pick<ScanFn>(ctx, ours, kScan, ADAPTER(Scan))(scan) & 0xff;
        break;
    case kOpSetDirectory: {
        Str dir(c.text);
        r = Pick<SetFn>(ctx, ours, kSetDirectory, ADAPTER(SetDirectory))(scan, &dir.S(), c.flag) & 0xff;
        break;
    }
    case kOpSetPattern: {
        Str pattern(c.text);
        Pick<SetFn>(ctx, ours, kSetPattern, ADAPTER(SetPattern))(scan, &pattern.S(), c.flag);
        break;
    }
    case kOpEnd:
        Pick<VoidFn>(ctx, ours, kEndDirectory, ADAPTER(EndDirectory))(scan);
        break;
    case kOpEntryAt: {
        void *entry = Pick<EntryAtFn>(ctx, ours, kEntryAt, ADAPTER(EntryAt))(scan, c.word);
        return "entry " + Position(scan, entry);
    }
    case kOpHasEntry: {
        Str name(c.text);
        r = Pick<HasEntryFn>(ctx, ours, kHasEntry, ADAPTER(HasEntry))(scan, &name.S()) & 0xff;
        break;
    }
    case kOpFullPath: {
        // An entry of the list (by position), or a made-up one with or without its scan.
        Str out(c.text);
        alignas(dscan::Entry) unsigned char raw[sizeof(dscan::Entry)];
        dscan::Entry *entry = nullptr;
        bool made = false;
        if (!scan->mEntries.empty() && (c.word & 1)) {
            auto it = scan->mEntries.begin();
            for (uint32_t k = (c.word >> 1) % scan->mEntries.size(); k > 0; --k) ++it;
            entry = &*it;
        } else {
            entry = ::new (raw) dscan::Entry((c.word & 2) ? scan : nullptr);
            entry->mName.mString = c.text.substr(0, c.text.size() / 2);
            made = true;
        }
        Pick<FullPathFn>(ctx, ours, kFullPath, ADAPTER(FullPath))(entry, &out.S());
        listed.push_back(Text(out.S()));
        if (made) entry->~Entry();
        break;
    }
    case kOpListMatching:
    case kOpListFiles:
    case kOpListFolders:
    case kOpBelow: {
        dscan::PathList list;
        if (c.flag & 0x100) {
            Str first("before");
            list.emplace_back(first.S());
        }
        if (c.op == kOpListMatching) r = Pick<ListFn>(ctx, ours, kListMatching, ADAPTER(ListMatching))(scan, &list, c.word);
        if (c.op == kOpListFiles) r = Pick<ListFilesFn>(ctx, ours, kListFiles, ADAPTER(ListFiles))(scan, &list);
        if (c.op == kOpListFolders) r = Pick<ListFn>(ctx, ours, kListFolders, ADAPTER(ListFolders))(scan, &list, c.word);
        if (c.op == kOpBelow) {
            Str dir(c.text);
            r = Pick<BelowFn>(ctx, ours, kListFilesBelow, ADAPTER(ListFilesBelow))(scan, &dir.S(), &list, c.flag);
        }
        r &= 0xff;
        listed = Listed(list);
        break;
    }
    default:
        std::abort();
    }
    std::snprintf(line, sizeof(line), "result %x", r);
    return line;
}

Call RandomCall(std::mt19937 &rng, const std::vector<std::string> &dirs, const std::vector<std::string> &names, const DirectoryScan *scan)
{
    static const char *const kPatterns[] = {"*", "*", "*.txt", "", "a.txt", "*.TXT", "nothing"};
    static const char *const kOtherDirs[] = {"", "C:\\missing", "C:\\missing\\", "relative"};
    Call c;
    c.op = int(rng() % kOpCount);
    c.flag = (rng() & 0xfffffe00u) | (rng() % 2) | (rng() % 3 == 0 ? 0x100u : 0);
    c.word = rng();
    if (c.op == kOpSetDirectory || c.op == kOpBelow) {
        if (rng() % 5 == 0) {
            c.text = kOtherDirs[rng() % std::size(kOtherDirs)];
        } else {
            c.text = dirs[rng() % dirs.size()];
            if (rng() % 3 == 0) c.text.pop_back(); // without its separator
            if (rng() % 4 == 0) c.text.back() == '\\' ? (void)(c.text.back() = '/') : (void)0;
        }
    } else if (c.op == kOpSetPattern) {
        c.text = kPatterns[rng() % std::size(kPatterns)];
    } else if (c.op == kOpHasEntry || c.op == kOpFullPath) {
        c.text = rng() % 4 == 0 ? std::string("absent") : names.empty() ? std::string(".") : names[rng() % names.size()];
        if (c.op == kOpFullPath) c.text += "-out";
    } else if (c.op == kOpEntryAt) {
        const int32_t cursor = scan->mCursorIndex;
        switch (rng() % 5) {
        case 0: c.word = uint32_t(cursor + 1); break;
        case 1: c.word = uint32_t(cursor - 1); break;
        case 2: c.word = uint32_t(cursor); break;
        case 3: c.word = rng() % (scan->mCount + 2); break;
        default: c.word = rng() % 3 == 0 ? 0xffffffffu : rng() % 4; break;
        }
    } else if (c.op == kOpListMatching) {
        c.word = (rng() & 0xfffffff0u) * (rng() % 2) | (rng() % 16);
    } else if (c.op == kOpListFolders) {
        c.word = (rng() & 0xfffffffcu) * (rng() % 2) | (rng() % 4);
    }
    return c;
}

void CompareLines(difftest::Context &ctx, int i, size_t k, const char *what, const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    if (a == b) return;
    size_t n = 0;
    while (n < a.size() && n < b.size() && a[n] == b[n]) ++n;
    ctx.Fail("case %d call %zu %s line %zu (of %zu / %zu): \"%.80s\" / \"%.80s\"", i, k, what, n, a.size(), b.size(),
             n < a.size() ? a[n].c_str() : "(none)", n < b.size() ? b[n].c_str() : "(none)");
}

} // namespace

// The scan (built by 0x1c48da, with 0x1c539a, run by 0x1c5896 through FindFirst 0x1c54dd, FindNext 0x1c5678 and CloseFind
// 0x1c54b2; destroyed by 0x1c49eb) under random calls: Scan 0x1c5896, SetDirectory 0x1c4a45, SetPattern 0x1c63d4,
// EndDirectory 0x1c539a, EntryAt 0x1c5047, HasEntry 0x1c5000, Entry::FullPath 0x1c509f, ListMatching 0x1c5db7, ListFiles
// 0x1c5bbd, ListFolders 0x1c587f and ListFilesBelow 0x1c5bcb (recursive). Results, the object, the caller's lists and
// strings and the file-system calls are compared after every call; the destructor's frees (the list's head node) and
// strings at the end. The entry constructors 0x1c4962 (Scan, ListMatching) and 0x1c4939 (the copy, Scan) run
// inside them.
DIFF_TEST(DirectoryScan_Script, kScan)
{
    Slots slots(ctx);
    slots.Set(kIatFindFirstFileA, reinterpret_cast<void *>(&FakeFindFirstA));
    slots.Set(dscan::kIatFindNextFileA, reinterpret_cast<void *>(&FakeFindNextA));
    slots.Set(kIatFindClose, reinterpret_cast<void *>(&FakeFindClose));
    *ctx.image.At<void *>(kFindFirstFileWPointer) = nullptr;
    *ctx.image.At<void *>(dscan::kFindNextFileWPointer) = nullptr;
    shape::CodePatch gameNew{kGameNew, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{kGameSizedDelete, reinterpret_cast<const void *>(&GameDelete)};
    std::mt19937 rng(9301);
    static const char *const kRoots[] = {"C:\\T\\", "C:/T/", "D:\\Games\\The Sims\\"};
    for (int i = 0; i < 1500; ++i) {
        gTree.clear();
        std::vector<std::string> dirs, names;
        const std::string root = kRoots[rng() % std::size(kRoots)];
        AddDirectory(rng, root, 0, dirs, names);
        const uint32_t buildSeed = rng();
        const uint8_t fill = static_cast<uint8_t>(rng());
        const int calls = int(rng() % 30);
        const uint32_t scriptSeed = rng();

        alignas(8) unsigned char memory[2][DirectoryScan::kSize];
        DirectoryScan *scans[2];
        std::vector<std::string> logs[2];
        std::string builds[2];
        for (int side = 0; side < 2; ++side) {
            std::memset(memory[side], fill, sizeof(memory[side]));
            std::mt19937 setup(buildSeed);
            Str dir(rng() % 1 == 0 && setup() % 5 == 0 ? std::string("") : dirs[setup() % dirs.size()]);
            const uint32_t scanNow = (setup() & 0xffffff00u) | (setup() % 2);
            const uint32_t keepDotDot = (setup() & 0xffffff00u) | (setup() % 2);
            const uint32_t keepDot = (setup() & 0xffffff00u) | (setup() % 2);
            gLog.clear();
            gSearches.clear();
            gNextHandle = 0x500;
            ctx.Mark(side ? "scan ours: build" : "scan original: build");
            void *r = Pick<BuildFn>(ctx, side == 1, kBuild, ADAPTER(Construct))(memory[side], &dir.S(), scanNow, keepDotDot, keepDot);
            scans[side] = reinterpret_cast<DirectoryScan *>(memory[side]);
            builds[side] = r == memory[side] ? "this" : "other";
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (builds[0] != builds[1]) ctx.Fail("case %d: Build returns %s / %s", i, builds[0].c_str(), builds[1].c_str());
        CompareLines(ctx, i, 0, "build state", State(scans[0]), State(scans[1]));
        CompareLines(ctx, i, 0, "build calls", logs[0], logs[1]);
        uintptr_t handles[2] = {gNextHandle, gNextHandle};
        std::mt19937 script(scriptSeed);
        for (int k = 0; k < calls && ctx.failures.size() < 20; ++k) {
            const Call c = RandomCall(script, dirs, names, scans[1]);
            std::string results[2];
            std::vector<std::string> listed[2];
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                gSearches.clear();
                gNextHandle = 0x1000 + 0x100 * k;
                ctx.Mark(side ? "scan ours" : "scan original");
                results[side] = Run(ctx, side == 1, scans[side], c, listed[side]);
                logs[side] = gLog;
            }
            ++ctx.cases;
            if (results[0] != results[1]) ctx.Fail("case %d call %d op %d: %s / %s", i, k, c.op, results[0].c_str(), results[1].c_str());
            CompareLines(ctx, i, k, "state", State(scans[0]), State(scans[1]));
            CompareLines(ctx, i, k, "listed", listed[0], listed[1]);
            CompareLines(ctx, i, k, "calls", logs[0], logs[1]);
        }
        (void)handles;
        // The end: the head node freed, the strings emptied.
        bool freed[2];
        std::string after[2];
        for (int side = 0; side < 2; ++side) {
            const void *head = &*scans[side]->mEntries.end();
            difftest::FreeRecorder rec;
            Pick<VoidFn>(ctx, side == 1, kDestroy, ADAPTER(Destruct))(scans[side]);
            rec.Stop();
            freed[side] = rec.Freed(head);
            after[side] = Text(scans[side]->mDirectory) + "|" + Text(scans[side]->mPattern);
        }
        ++ctx.cases;
        if (freed[0] != freed[1] || after[0] != after[1]) {
            ctx.Fail("case %d end: head freed %d / %d, strings \"%s\" / \"%s\"", i, freed[0], freed[1], after[0].c_str(), after[1].c_str());
        }
        if (ctx.failures.size() >= 20) return;
    }
}

// FindNext 0x1c5678 on its own: no node; the ANSI import (names converted from the system code page, the directory bit
// from the attributes, the end of the search) and the game's optional FindNextFileW pointer (UTF-16 names).
namespace {

std::u16string gWideName;
DWORD gWideAttributes = 0;
BOOL gWideAnswer = TRUE;

BOOL WINAPI FakeFindNextW(HANDLE handle, WIN32_FIND_DATAW *data)
{
    char line[32];
    std::snprintf(line, sizeof(line), "W %x", static_cast<unsigned>(reinterpret_cast<uintptr_t>(handle)));
    gLog.push_back(line);
    if (!gWideAnswer) return FALSE;
    std::memset(data, 0xa5, sizeof(*data));
    data->dwFileAttributes = gWideAttributes;
    std::memset(data->cFileName, 0, sizeof(data->cFileName));
    std::memcpy(data->cFileName, gWideName.c_str(), gWideName.size() * 2);
    return TRUE;
}

} // namespace

DIFF_TEST(DirectoryScan_FindNext, kFindNext)
{
    Slots slots(ctx);
    slots.Set(dscan::kIatFindNextFileA, reinterpret_cast<void *>(&FakeFindNextA));
    using FindNextFn = uint32_t(__cdecl *)(as2::FindNode *);
    const char *const names[] = {"file.txt", "caf\xe9.txt", "", "a name that is longer than fifteen characters.dat", "\xe4\xf6\xfc", "."};
    const DWORD attributes[] = {0, 0x10, 0x20, 0x30, 0xffffffff, 0xffffffef};
    for (int wide = 0; wide < 2; ++wide) {
        *ctx.image.At<void *>(dscan::kFindNextFileWPointer) = wide ? reinterpret_cast<void *>(&FakeFindNextW) : nullptr;
        for (const char *name : names) {
            for (DWORD attr : attributes) {
                for (int answer = 0; answer < 2; ++answer) {
                    std::string got[2];
                    for (int side = 0; side < 2; ++side) {
                        gTree.clear();
                        gSearches.clear();
                        gLog.clear();
                        Search search;
                        search.found = {{"first", 0}, {name, attr}};
                        search.next = answer ? 1 : 2;
                        gSearches[0x77] = search;
                        gWideName = std::u16string(name, name + std::strlen(name));
                        gWideAttributes = attr;
                        gWideAnswer = answer;
                        alignas(as2::FindNode) unsigned char raw[sizeof(as2::FindNode)];
                        std::memset(raw, 0x5c, sizeof(raw));
                        auto *node = reinterpret_cast<as2::FindNode *>(raw);
                        node->mName.Construct("previous");
                        node->mPattern.Construct("C:\\T\\*");
                        node->mHandle = reinterpret_cast<HANDLE>(0x77);
                        const uint32_t r = (side ? reinterpret_cast<FindNextFn>(&dscan::FindNext) : ctx.Original<FindNextFn>(kFindNext))(node) & 0xff;
                        char line[48];
                        std::snprintf(line, sizeof(line), "r %x dir %02x pad %02x%02x%02x ", r, node->mIsDirectory, node->pad21[0], node->pad21[1], node->pad21[2]);
                        got[side] = line + Text(node->mName) + " " + Text(node->mPattern);
                        for (const std::string &l : gLog) got[side] += "|" + l;
                        node->mPattern.Destruct();
                        node->mName.Destruct();
                    }
                    ++ctx.cases;
                    if (got[0] != got[1]) ctx.Fail("wide %d name \"%s\" attr %lx answer %d:\n  %s\n  %s", wide, name, attr, answer, got[0].c_str(), got[1].c_str());
                }
            }
        }
    }
    *ctx.image.At<void *>(dscan::kFindNextFileWPointer) = nullptr;
    std::string got[2];
    for (int side = 0; side < 2; ++side) {
        got[side] = std::to_string((side ? reinterpret_cast<FindNextFn>(&dscan::FindNext) : ctx.Original<FindNextFn>(kFindNext))(nullptr) & 0xff);
    }
    ++ctx.cases;
    if (got[0] != got[1]) ctx.Fail("null node: %s / %s", got[0].c_str(), got[1].c_str());
}
