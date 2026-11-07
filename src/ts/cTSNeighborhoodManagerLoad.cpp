// cTSNeighborhoodManager's file-system members: slot 3 (Load, 0x4a4160) and its package probe (0x4a6000), finding the
// neighbourhoods at start-up; the tutorial step (AddTutorial, 0x4a27a0) and the file deletion Remove does (DeleteFiles,
// 0x4a2950), lane FU.
#include "rz/PathFileOps.h"
#include "ts/cTSNeighborhoodManager.h"

#include "app/AppDebugger.h"
#include "app/AppInit.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"
#include "rz/DirectoryScan.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZString.h"

#include <cstring>
#include <list>
#include <string.h>

// The directory scan object and the file name's name part (Load and DeleteFiles).
namespace hoodmgr {

// A directory scan on the caller's stack (our ::DirectoryScan, src/rz/DirectoryScan.h; lane LY switched it from the
// original callees): built on a directory, filled by a pattern, its entries each a kind (4 a directory, 8 a file) and a
// full path. Built and destroyed through the class's adapters, the addressable entry points tests stand in for.
struct FolderScan {
    alignas(::DirectoryScan) uint8_t bytes[sizeof(::DirectoryScan)];
    ::DirectoryScan *scan;

    explicit FolderScan(const cRZString *directory)
        : scan(DirectoryScanAdapters::Construct(bytes, nullptr, directory, false, false, false))
    {
    }
    FolderScan(const FolderScan &) = delete;
    FolderScan &operator=(const FolderScan &) = delete;
    ~FolderScan() { DirectoryScanAdapters::Destruct(scan, nullptr); }
    void Find(const char *pattern)
    {
        cRZString text;
        text.Construct(pattern);
        scan->SetPattern(&text, true);
        text.Destruct();
    }
    uint32_t Count() const { return scan->mCount; }
    dscan::Entry *Entry(uint32_t index) { return scan->EntryAt(index); }
    static uint8_t Flags(const dscan::Entry *entry) { return static_cast<uint8_t>(entry->mKind); }
    // Assigns the entry's full path to `out` (a built string).
    static void Path(const dscan::Entry *entry, cRZString *out) { entry->FullPath(out); }
};

constexpr uint8_t kScanDirectory = 4;
constexpr uint8_t kScanFile = 8;

// 0x1c8674 (45 bytes, inline here): the name part of a file name, split off the full path first when it is still empty.
inline cRZString *NamePart(cRZFileName &fileName)
{
    if (fileName.mName.mString.empty() && !fileName.mFull.mString.empty())
        cRZFileName::SplitInto(&fileName.mFull, nullptr, nullptr, &fileName.mName, nullptr);
    return &fileName.mName;
}

} // namespace hoodmgr

namespace {
// Calls slot `slot` of `object` (__thiscall) with the given dword arguments.
template <typename R = uint32_t, typename... A> R Call(const void *object, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return VirtualSlot<Fn>(object, slot)(object, args...);
}

void AddRefObject(void *object) { Call(object, 1); }
void ReleaseObject(void *object) { Call(object, 2); }

// Slots of the neighbourhood objects Load uses.
constexpr int kHoodSetName = 7;     // (const char *)
constexpr int kHoodName = 8;        // the name (cIGZString)
constexpr int kHoodSetState = 13;   // (2, 1) for a neighbourhood found in a directory
constexpr int kHoodPersist = 24;    // the persist resource face (slot 3 GetKey)
constexpr int kHoodLoaded = 25;     // called on every neighbourhood the persist system hands out
// Persist system slots.
constexpr int kPersistDatabase = 3;     // () the record database
constexpr int kPersistGetResource = 5;  // (key, type, out, 0, 0)
constexpr int kPersistOpenSegment = 22; // (0x3b2271bb, out, 2, directory, file name, 1, 1)
constexpr int kDatabaseKeys = 19;       // (out vector of keys, type): how many
constexpr int kDatabaseRecord = 34;     // (key, out record): whether found
constexpr int kRecordPath = 11;         // (cRZString *out): the file the record lives in
constexpr int kSegmentGroup = 4;        // () the group of an opened segment
constexpr int kStringIsEqual = 12;      // cIGZString::IsEqual(const cIGZString &, bool)
constexpr int kStringLength = 7;
constexpr int kStringData = 6;

constexpr uint32_t kHoodRecordType = 0xac8a7a2e;  // resource type of a neighbourhood's record
constexpr uint32_t kHoodInterface = 0xac8a7a29;   // what the persist system is asked for
constexpr uint32_t kSegmentInterface = 0x3b2271bb;
constexpr uint32_t kRootQuery = 0x4c4324c5;       // the common implementation's neighbourhood root
constexpr uint32_t kDirectoriesQuery = 0xce9b3252; // its list of extra neighbourhood directories
constexpr uint32_t kSeparator = 0xda52b4;          // "/"
constexpr uint32_t kPackageSuffix = 0xdcbc84;      // "_Neighborhood.package"

// The neighbourhood path helper (__cdecl (name, out path, 1, -1), an original callee): the path of the neighbourhood's
// directory, root included.
using PathFromName = uint8_t(__cdecl *)(void *, cRZString *, uint32_t, uint32_t);

using hoodmgr::FolderScan;

// A node of the game's std::list<std::string> (allocator-service storage).
struct StringListNode {
    StringListNode *mNext;
    StringListNode *mPrev;
    std::string mText; // +0x08
};
static_assert(sizeof(StringListNode) == 0x20, "StringListNode layout");

// The text before the first "/" of `rest` (erased from it), upper case (0x1ad17a: _strupr), built in `out`.
void FirstPartUpper(cRZString *rest, cRZString *out)
{
    reinterpret_cast<cRZIniStringHelpers *>(rest)->SplitOff(out, GameAddress<const char>(kSeparator));
    _strupr(out->mString.data());
}
} // namespace

bool cTSNeighborhoodManager::HasPackage(void *hood)
{
    if (!hood) return false;
    cRZString path;
    path.Construct();
    OriginalFunction<PathFromName, 0xc0b314>()(Call<void *>(hood, kHoodName), &path, 1, 0xffffffffu);
    path.Append(*Call<const cIGZString *>(hood, kHoodName));
    path.mString.append(GameAddress<const char>(kPackageSuffix), 0x15);
    const bool exists = cRZFile::PathExists(*path.AsInterface());
    path.Destruct();
    return exists;
}

void cTSNeighborhoodManager::Load()
{
    cRZString root;
    root.Construct();
    appinit::CommonImplSlot4(kRootQuery, uint32_t(uintptr_t(&root)), 1, 0, 0xffffffffu);
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();

    // 1. The neighbourhood records the persist system knows. A record in a directory under the root whose first part names
    //    another neighbourhood is renamed and kept for step 2; one whose directory is its own is added when its package exists.
    ServiceBlockVector keys{};
    const uint32_t count = Call(Call<void *>(persist, kPersistDatabase), kDatabaseKeys, &keys, kHoodRecordType);
    std::list<void *> renamed; // counted
    for (uint32_t i = 0; i < count; ++i) {
        void *hood = nullptr;
        // ORIGINAL-BUG: the keys are indexed up to the count the database returned, only checked for not being empty.
        if (keys.mFirst != keys.mLast &&
            Call<uint8_t>(persist, kPersistGetResource, static_cast<uint8_t *>(keys.mFirst) + i * 16, kHoodInterface, &hood, 0u, 0u)) {
            Call(hood, kHoodLoaded);
            void *resource = Call<void *>(hood, kHoodPersist);
            void *database = Call<void *>(persist, kPersistDatabase);
            void *record = nullptr;
            cGZPersistResourceKey key;
            const auto *found = Call<const cGZPersistResourceKey *>(resource, 3, &key);
            if (Call<uint8_t>(database, kDatabaseRecord, found, &record)) {
                cRZString name;
                name.Construct(*Call<const cIGZString *>(hood, kHoodName));
                cRZString path;
                path.Construct();
                Call(record, kRecordPath, &path);
                if (path.Find(*root.AsInterface(), 0, false) != -1) {
                    cRZString rest;
                    rest.Construct(*path.Erase(0, root.Strlen())->AsInterface());
                    cRZString first;
                    FirstPartUpper(&rest, &first);
                    if (!Call<uint8_t>(&first, kStringIsEqual, &name, 1u)) {
                        if (hood) AddRefObject(hood);
                        renamed.push_back(hood);
                        Call(hood, kHoodSetName, first.ToChar());
                    } else if (HasPackage(hood)) {
                        Add(hood);
                    }
                    first.Destruct();
                    rest.Destruct();
                }
                path.Destruct();
                name.Destruct();
            }
            if (record) ReleaseObject(record);
        }
        if (hood) ReleaseObject(hood);
    }

    // 2. The renamed ones, when their package exists under the new name.
    for (void *hood : renamed) {
        if (hood) AddRefObject(hood);
        if (hood && HasPackage(hood)) Add(hood);
        if (hood) ReleaseObject(hood);
    }

    // The tutorial neighbourhood (0x4a27a0, lane FU).
    AddTutorial();

    // 3. Every package one directory down in each extra neighbourhood directory: opened as a segment, its neighbourhood record
    //    joins this's list (renamed after the directory it sits in, when that differs).
    appdebugger::ListHeader directories{nullptr, 0};
    auto *sentinel = static_cast<StringListNode *>(appdebugger::AllocateListNodes(1, 0));
    sentinel->mNext = sentinel;
    sentinel->mPrev = sentinel;
    directories.mHead = sentinel;
    appinit::CommonImplSlot5(kDirectoriesQuery, uint32_t(uintptr_t(&directories)), 1, 0, 0xffffffffu);
    for (auto *node = sentinel->mNext; node != sentinel; node = node->mNext) {
        cRZString directory;
        directory.Construct(node->mText.c_str());
        FolderScan top(&directory);
        top.Find("*.*");
        for (uint32_t i = 0, n = top.Count(); i < n; ++i) {
            dscan::Entry *sub = top.Entry(i);
            if (!(FolderScan::Flags(sub) & 4)) continue;
            cRZString subPath;
            subPath.Construct();
            FolderScan::Path(sub, &subPath);
            {
                FolderScan packages(&subPath);
                packages.Find("*.package");
                for (uint32_t j = 0, m = packages.Count(); j < m; ++j) {
                    dscan::Entry *entry = packages.Entry(j);
                    if (!(FolderScan::Flags(entry) & 8)) continue;
                    cRZString file;
                    file.Construct();
                    FolderScan::Path(entry, &file);
                    cRZFileName fileName;
                    fileName.Construct(&file);
                    void *segment = nullptr;
                    cRZString drivePath;
                    cRZString *drive = fileName.GetDrivePath(&drivePath);
                    using OpenSegment = uint32_t(__thiscall *)(void *, uint32_t, void **, uint32_t, cRZString *, cRZString *, uint32_t,
                        uint32_t);
                    cRZString *filePart = hoodmgr::NamePart(fileName);
                    VirtualCall<OpenSegment, kPersistOpenSegment>(persist, kSegmentInterface, &segment, 2, drive, filePart, 1, 1);
                    drivePath.Destruct();
                    void *hood = nullptr;
                    // The original asks the segment for its group without checking that it opened.
                    const cGZPersistResourceKey key = {kHoodRecordType, Call(segment, kSegmentGroup), 1, 0};
                    Call<uint8_t>(persist, kPersistGetResource, &key, kHoodInterface, &hood, 0u, 0u);
                    if (hood) {
                        Call(hood, kHoodLoaded);
                        Call(hood, kHoodSetState, 2u, 1u);
                        const auto *nameText = Call<const cIGZString *>(hood, kHoodName);
                        const uint32_t nameLength = Call(nameText, kStringLength);
                        const char *nameData = Call<const char *>(nameText, kStringData);
                        cRZString name;
                        name.Construct();
                        name.mString.assign(nameData, nameLength);
                        cRZString path;
                        path.Construct();
                        path.mString.assign(file.mString);
                        cRZFileName::NormaliseSeparators(&path, '/');
                        if (path.Find(*directory.AsInterface(), 0, false) != -1) {
                            cRZString *after = path.Erase(0, directory.Strlen());
                            const uint32_t restLength = Call(after, kStringLength);
                            const char *restData = Call<const char *>(after, kStringData);
                            cRZString rest;
                            rest.Construct();
                            rest.mString.assign(restData, restLength);
                            cRZString first;
                            FirstPartUpper(&rest, &first);
                            if (!Call<uint8_t>(&first, kStringIsEqual, &name, 1u)) Call(hood, kHoodSetName, first.ToChar());
                            first.Destruct();
                            rest.Destruct();
                        }
                        mHoods.PushBack(hood);
                        path.Destruct();
                        name.Destruct();
                    }
                    if (hood) ReleaseObject(hood);
                    if (segment) ReleaseObject(segment);
                    fileName.Destruct();
                    file.Destruct();
                }
            }
            subPath.Destruct();
        }
        directory.Destruct();
    }
    appdebugger::DestroyStringListNodes(&directories, directories.mHead);
    FreeThroughAllocatorService(directories.mHead, 1);

    for (void *hood : renamed)
        if (hood) ReleaseObject(hood);
    renamed.clear();
    keys.FreeBlock();
    root.Destruct();
}

namespace {
// ---- AddTutorial and DeleteFiles (lane FU) ----
void *PersistSystem() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }

// Neighbourhood slots.
constexpr int kHoodSetText = 3;     // (const char *)
constexpr int kHoodSetId = 11;
constexpr int kHoodKind = 18;       // 1 for a main neighbourhood
constexpr int kMainNeighborhood = 1;
// cIGZString slots.
constexpr int kStringText = 5;
// Persist system slots.
constexpr int kPersistGroupOfName = 11;  // (cIGZString *) the group of a file name
constexpr int kPersistReleaseFile = 25;  // (cIGZString *path, 1) releases the segments of a file
constexpr int kFrameworkCreate = 3;      // (class, interface, out)

constexpr uint32_t kHoodClass = 0xac8a7a2e;      // the neighbourhood class, also its record's resource type

// The neighbourhood path helpers (__cdecl (name text, out path, 1, -1), original callees): the four directories of a main
// neighbourhood.
using PathFromText = uint8_t(__cdecl *)(const char *, cRZString *, uint32_t, uint32_t);
// 0x1c628f (__cdecl (directory, 2, 1), original): walks the tree under the directory (0x1c3505 on every file) before the delete.
using PrepareTree = uint8_t(__cdecl *)(cRZString *, uint32_t, uint32_t);
// 0x1c5fa0 (__cdecl (directory, 1), original): deletes the directory, recursively.
using DeleteTree = uint8_t(__cdecl *)(cRZString *, uint32_t);
// 0x4a5570 (__cdecl (list name, neighbourhood name), original): drops the name from a "visited" list of the options.
using ForgetVisited = uint8_t(__cdecl *)(const char *, const char *);

// Drops `name` from every "visited neighbourhoods" list: the plain one, NoCollege, NoDowntown, then NoNeighborhoodForEP1..31.
void ForgetEverywhere(const char *name)
{
    auto forget = OriginalFunction<ForgetVisited, 0x4a5570>();
    forget("visitedNeighborhoods", name);
    forget("visitedNeighborhoodsNoCollege", name);
    forget("visitedNeighborhoodsNoDowntown", name);
    for (uint32_t pack = 1; pack < 32; ++pack) {
        cRZString list;
        list.Construct();
        list.Sprintf("visitedNeighborhoodsNoNeighborhoodForEP%d", pack);
        forget(list.ToChar(), name);
        list.Destruct();
    }
}

// The packages in `directory` (made by the path helper from `text`): each one's segments released with its full path (byPath)
// or its file name; with `forget` also its "visited" entries. Then the directory's tree is deleted. The scan lives on until
// `scan` is destroyed by the caller (the original keeps the first three until the end).
void ReleasePackages(void *persist, hoodmgr::FolderScan &scan, cRZString *directory, bool byPath, bool forget)
{
    scan.Find("*.package");
    for (uint32_t i = 0; i < scan.Count(); ++i) {
        dscan::Entry *const entry = scan.Entry(i);
        if (!(hoodmgr::FolderScan::Flags(entry) & hoodmgr::kScanFile)) continue;
        cRZString path;
        path.Construct();
        hoodmgr::FolderScan::Path(entry, &path);
        if (byPath) {
            Call(persist, kPersistReleaseFile, &path, 1u);
        } else {
            cRZFileName fileName;
            fileName.Construct(&path);
            if (forget) {
                cRZString name;
                name.Construct(*hoodmgr::NamePart(fileName), 0, 0xffffffffu);
                Call(persist, kPersistReleaseFile, &name, 1u);
                ForgetEverywhere(name.ToChar());
                name.Destruct();
            } else {
                // (The original also copies the name part here and never uses the copy.)
                Call(persist, kPersistReleaseFile, hoodmgr::NamePart(fileName), 1u);
            }
            fileName.Destruct();
        }
        path.Destruct();
    }
    OriginalFunction<PrepareTree, 0x1c628f>()(directory, 2, 1);
    OriginalFunction<DeleteTree, 0x1c5fa0>()(directory, 1);
}

// Deletes <drive and directory of fileName><its name part><extension> when it exists.
void DeleteBeside(cRZFileName &fileName, const char *extension)
{
    cRZString drive;
    fileName.GetDrivePath(&drive);
    cRZString file;
    file.Construct();
    const cRZString *name = hoodmgr::NamePart(fileName);
    file.mString = drive.mString;
    file.mString.append(name->mString);
    file.mString.append(extension);
    drive.Destruct();
    if (cRZFile::PathExists(*file.AsInterface())) PathDelete(file.AsInterface());
    file.Destruct();
}
} // namespace

void cTSNeighborhoodManager::AddTutorial()
{
    void *hood = nullptr;
    Call(GetDirectorFrameworkObject(), kFrameworkCreate, kHoodClass, kHoodInterface, &hood);
    // The original uses the hood unchecked.
    Call(hood, kHoodSetText, "Tutorial");
    Call(hood, kHoodSetName, "Tutorial");
    Call(hood, kHoodSetId, 0u);
    Call(hood, kHoodSetState, 1u, 1u);
    const void *nameText = Call<const void *>(hood, kHoodName);
    const uint32_t length = Call(nameText, kStringLength);
    const char *data = Call<const char *>(nameText, kStringData);
    cRZString groupName;
    groupName.Construct();
    groupName.mString.assign(data, length);
    groupName.mString.append("_Neighborhood", 13);
    const uint32_t group = Call(PersistSystem(), kPersistGroupOfName, &groupName);
    const cGZPersistResourceKey key = {kHoodClass, group, 1, 0};
    Call(Call<void *>(hood, kHoodPersist), 4, &key);
    Add(hood);
    groupName.Destruct();
    if (hood) ReleaseObject(hood);
}

void cTSNeighborhoodManager::DeleteFiles(void *hood)
{
    void *const persist = PersistSystem();
    const int32_t kind = Call<int32_t>(hood, kHoodKind);
    const char *text = Call<const char *>(Call<void *>(hood, kHoodName), kStringText);
    if (kind == kMainNeighborhood) {
        // The first three scans end together, last first, each before its directory string.
        cRZString first;
        first.Construct();
        OriginalFunction<PathFromText, 0xc0b29b>()(text, &first, 1, 0xffffffffu);
        {
            hoodmgr::FolderScan firstScan(&first);
            ReleasePackages(persist, firstScan, &first, true, false);
            cRZString second;
            second.Construct();
            OriginalFunction<PathFromText, 0xc0b1b9>()(text, &second, 1, 0xffffffffu);
            {
                hoodmgr::FolderScan secondScan(&second);
                ReleasePackages(persist, secondScan, &second, false, false);
                cRZString third;
                third.Construct();
                OriginalFunction<PathFromText, 0xc0b492>()(text, &third, 1, 0xffffffffu);
                {
                    hoodmgr::FolderScan thirdScan(&third);
                    ReleasePackages(persist, thirdScan, &third, false, false);
                }
                third.Destruct();
            }
            second.Destruct();
        }
        first.Destruct();
        cRZString fourth;
        fourth.Construct();
        OriginalFunction<PathFromText, 0xc0b362>()(text, &fourth, 1, 0xffffffffu);
        {
            hoodmgr::FolderScan fourthScan(&fourth);
            ReleasePackages(persist, fourthScan, &fourth, false, true);
        }
        fourth.Destruct();
        return;
    }

    // Any other neighbourhood: the file its record lives in.
    void *record = nullptr;
    void *resource = Call<void *>(hood, kHoodPersist);
    void *database = Call<void *>(persist, kPersistDatabase);
    cGZPersistResourceKey keyBuffer;
    Call<uint8_t>(database, kDatabaseRecord, Call<const cGZPersistResourceKey *>(resource, 3, &keyBuffer), &record);
    if (!record) return;
    cRZString path;
    path.Construct();
    Call(record, kRecordPath, &path);
    cRZFileName fileName;
    fileName.Construct(&path);
    cRZString name;
    name.Construct(*hoodmgr::NamePart(fileName), 0, 0xffffffffu);
    Call(persist, kPersistReleaseFile, &name, 1u);
    ForgetEverywhere(name.ToChar());
    DeleteBeside(fileName, ".png");
    DeleteBeside(fileName, ".reia");
    name.Destruct();
    fileName.Destruct();
    path.Destruct();
    ReleaseObject(record);
}
