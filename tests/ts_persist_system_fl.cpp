// Differential tests for the cTSPersistSystem function of lane FL: the segment set-up 0x1e7230
// (cTSPersistSystem::SetUpSegment, thiscall on the primary, ret 0x14).
//
// The world: one primary object block (0x300 bytes, rebuilt from the seed before each side) whose ITS face (+0xa8,
// slots 10, 11 and 12) is a recording fake, with a real mPathGroups hash table at +0x1e8 (built by the game's
// constructor 0x1d8a50 and filled from the seed through its try_emplace 0x1d7390); the cTSAppCommonImpl object (cache
// 0x10cc040: slot 6 has-paths, slot 5 fills a path list); the allocator service (cache 0x10c95d4) the path lists and
// the key vector come from; the segment and its faces as fakes of their own kinds: the segment (other: QueryInterface,
// Init, Open, IsOpen, Close, SetPath, GetResourceKeyList, OpenRecord), the group face 0xad095a01 (lock: 5 get, 6 set),
// the records (stream: 6 close, 8 read, 10 size), the key-status face 0x3b2271bb (svc: 5, 7, 12), the packed-file face
// 0x286d4a6a (fw: 49, 50), the directory-files face 0x856c8cf7 (log: 10, 14, 23, 28, 29). No file is touched:
// cRZFile::LengthOfPath is a recording stand-in on both sides (the image's 0x1c1b8b and ours), the folder listing
// (0x1f58f0 constructor, 0x1f6dd0 first, 0x1f64f0 entry path, 0x1f61e0 next, 0x1f6090 destructor) is a recording
// stand-in in the image (both sides call the image's), and cRZFile / cRZFileName run for real on absolute paths. The
// path-prefix test 0x1c2269, the table functions 0x1d4a70 / 0x1d5b50 / 0x1d7390, the file-name setters 0x1c8b12 /
// 0x1c8abb, the number parser 0x1b0e2c and the converter constructor 0x1f5f80 run as they are on both sides.
// Compared: the call log (allocations and frees of the allocator service included), the primary block outside the
// table's own words, and the table's entries in list order (text and group).
#include "difftest.h"
#include "app/AppDebugger.h"
#include "app_debugger_fakes.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSPersistSystem.h"
#include "ts/DirectoryListing.h"
#include "code_patch.h"
#include "ts_persist_system_shape.h"

#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <windows.h>

namespace {

using adt::Def;
using adt::gLog;

constexpr uint32_t kCommonCache = 0x10cc040;
constexpr uint32_t kSub = 0xa8;
constexpr uint32_t kTable = 0x1e8; // mPathGroups (0x20 bytes)

// Overwrites a function's first five bytes with a jump to a stand-in, for the scope of the test.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(void *at, void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    CodePatch &operator=(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

uint32_t gSeed = 0;
uint32_t gCalls = 0;
alignas(8) uint8_t gPrimary[0x300];
adt::AllocatorFake *gAlloc = nullptr;
uint32_t gFilterVTable = 0; // the image's cGZPersistResourceKeyFilterByType vtable (0xda5cf0)

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

uint32_t TextHash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return h;
}

std::string Hex(uint32_t v)
{
    return adt::Hex(v);
}

uint32_t Word(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// A byte result with garbage above it (the callers test al only).
uint32_t Bit(uint32_t salt, uint32_t modulus = 2)
{
    return (Mix(gSeed, salt) & 0xffffff00u) | (Mix(gSeed, salt ^ 0x5a5a) % modulus != 0 ? 1u : 0u);
}

// ---- the world ----

const char *const kPaths[] = {
    "C:\\Sims\\Downloads\\Gizmo.package",
    "C:\\Sims\\Downloads\\Objects\\Thing.bundle.package",
    "C:\\Sims\\TSData\\Res\\Text\\ui.string.package",
    "C:\\Sims\\TSData\\Res\\Text\\ui.string.files",
    "C:\\Sims\\TSData\\Res\\Objects\\SCObjects\\",
    "D:\\Neighborhoods\\N001\\Lots\\",
    "C:\\Sims\\Downloads\\A Folder Name Long Enough For The Heap\\Big.package",
    "C:\\Sims\\TSData\\Res\\Objects\\objects.package",
};
const char *const kNames[] = {"Gizmo", "", "Thing.Bundle", "N001_Neighborhood", "objects", "SCObjects"};
const char *const kPrefixes[] = {"C:\\Sims\\Downloads", "C:/Sims/TSData/Res/Objects", "C:\\Sims\\TSData", "D:\\Neighborhoods\\N001",
                                 "E:\\Nowhere", "C:\\Sims\\Download", "C:\\Sims\\TSData\\Res\\Text\\"};
const char *const kGroupNames[] = {"objects", "Gizmo", "Global", "N001", "SCObjects", "uitext", "PersonGlobals", "A Group Name Past Sixteen"};
const char *const kListed[] = {"C:\\Dir\\file.abc", "C:\\Dir\\file._ab", "C:\\Dir\\noext", "C:\\Dir\\file.abcd", "C:\\Dir\\file.abcde",
                               "C:\\Dir\\file.0x1234abcd", "C:\\Dir\\file.12345", "C:\\Dir\\file..x", "C:\\Dir\\file.a_b"};
const uint32_t kGroups[] = {0x6f000001, 0x6f000002, 0x7f123456};
const uint32_t kTypes[] = {1, 2, 3, 3, 0, 1};

// The current case's choices (set from the seed before each side).
struct Case {
    uint32_t pathIndex, nameIndex, info, type;
    uint32_t groupValue; // what the group face's slot 6 last stored
    uint32_t filterType; // the type of the last key list asked for
    uint32_t listIndex, listCount;
    void *converter;
} gCase;

// The table's node (0x2c bytes) as the game lays it out.
struct GroupNode {
    GroupNode *next;
    GroupNode *prev;
    cRZString key;
    uint32_t group;
};
static_assert(sizeof(GroupNode) == 0x2c, "path group node");

std::vector<std::string> DumpTable(const uint8_t *table)
{
    std::vector<std::string> out;
    GroupNode *head = *reinterpret_cast<GroupNode *const *>(table + 4);
    for (GroupNode *n = head->next; n != head; n = n->next) out.push_back(n->key.mString + "=" + Hex(n->group));
    out.push_back("size " + Hex(*reinterpret_cast<const uint32_t *>(table + 8)));
    return out;
}

// ---- stand-ins ----

// cRZFile::LengthOfPath (__cdecl, the path by reference): logged; 0, a length or -1 (not found) from the path's hash.
uint32_t __cdecl LengthStand(const cRZString *path)
{
    gLog.push_back("len(S:" + adt::Describe(path) + ")");
    static const uint32_t answers[] = {0, 0x400, 0xffffffffu};
    return answers[Mix(gSeed, TextHash(path->mString)) % 3];
}

// The folder listing (0x3c bytes on the caller's stack; thiscall).
void *__fastcall ListingCtor(void *self, void *, const cRZString *path, void *system)
{
    gLog.push_back("listing(S:" + adt::Describe(path) + ", " + (system == gPrimary ? "primary" : "?") + ")");
    gCase.listIndex = 0;
    gCase.listCount = Mix(gSeed, 70) % 5;
    return self;
}
uint32_t __fastcall ListingFirst(void *, void *)
{
    gLog.push_back("listing.first");
    gCase.listIndex = 0;
    return (Mix(gSeed, 71) & 0xffffff00u) | (gCase.listCount > 0 ? 1u : 0u);
}
void *__fastcall ListingEntry(void *, void *, cRZString *out)
{
    const char *text = kListed[Mix(gSeed, 80 + gCase.listIndex) % (sizeof(kListed) / sizeof(kListed[0]))];
    gLog.push_back(std::string("listing.entry ") + text);
    out->Construct(text);
    return out;
}
uint32_t __fastcall ListingNext(void *, void *)
{
    gLog.push_back("listing.next");
    ++gCase.listIndex;
    return (Mix(gSeed, 72 + gCase.listIndex) & 0xffffff00u) | (gCase.listIndex < gCase.listCount ? 1u : 0u);
}
void __fastcall ListingDtor(void *, void *)
{
    gLog.push_back("listing.dtor");
}

// The key vector's block: grown from the tracked heap (the allocator fake frees it at the end of the side).
void AppendKeys(ServiceBlockVector *vec, uint32_t n, uint32_t type)
{
    auto *first = static_cast<uint8_t *>(vec->mFirst);
    auto *last = static_cast<uint8_t *>(vec->mLast);
    auto *end = static_cast<uint8_t *>(vec->mEnd);
    const uint32_t used = static_cast<uint32_t>(last - first);
    if (static_cast<uint32_t>(end - last) < n * 16) {
        auto *block = static_cast<uint8_t *>(::operator new(used + n * 16 + 16));
        gAlloc->Track(block);
        if (used) std::memcpy(block, first, used);
        first = block;
        last = block + used;
        end = block + used + n * 16 + 16;
    }
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t key[4] = {Mix(gSeed, 90 + i) % 4 == 0 ? Mix(gSeed, 91 + i) : type, kGroups[Mix(gSeed, 92 + i) % 3] + Mix(gSeed, 93 + i) % 3,
                           Mix(gSeed, 94 + i), Mix(gSeed, 95 + i)};
        std::memcpy(last + i * 16, key, 16);
    }
    vec->mFirst = first;
    vec->mLast = last + n * 16;
    vec->mEnd = end;
}

// A record's bytes: 'NREF' a group name (maybe cut short or with a NUL inside); 'GLOB' 0x40 bytes of header, then a
// name (after one more byte when the last byte is not 0).
void FillRecord(uint8_t *buffer, uint32_t size)
{
    ++gCalls;
    std::string text;
    if (gCase.filterType == 0x474c4f42) {
        text.assign(0x40, 'h');
        const bool lastSet = Mix(gSeed, 300 + gCalls) % 2;
        if (lastSet) text += '?';
        text += kGroupNames[Mix(gSeed, 310 + gCalls) % 8];
        text += '\0';
        while (text.size() < size) text += 'f';
        if (size) text[size - 1] = lastSet ? 'z' : '\0';
    } else {
        text = kGroupNames[Mix(gSeed, 320 + gCalls) % 8];
        if (Mix(gSeed, 330 + gCalls) % 5 == 0 && text.size() > 2) text[2] = '\0';
        while (text.size() < size) text += Mix(gSeed, 340 + gCalls) % 2 ? '\0' : 'q';
    }
    std::memcpy(buffer, text.data(), size);
}

void DefineFakes()
{
    // The ITS face: 10 GetGroupIdForPathChars, 11 GetGroupIdForPath, 12 GetGroupIdForNameChars.
    Def(adt::kConsoleFace, 10, 1, "s", [](void *, const uint32_t *args) { return kGroups[Mix(gSeed, TextHash(reinterpret_cast<const char *>(args[0]))) % 3]; });
    Def(adt::kConsoleFace, 11, 1, "S", [](void *, const uint32_t *) { return kGroups[Mix(gSeed, 11) % 3]; });
    Def(adt::kConsoleFace, 12, 1, "s", [](void *, const uint32_t *args) { return Mix(gSeed, TextHash(reinterpret_cast<const char *>(args[0]))) | 0x7f000000u; });
    // cTSAppCommonImpl: 6 has paths, 5 fills a list (entries from the path id and `which`, the second argument a list).
    Def(adt::kCommonImpl, 6, 1, "i", [](void *, const uint32_t *args) { return Bit(600 + args[0] % 97, 3); });
    Def(adt::kCommonImpl, 5, 5, "ioiii", [](void *, const uint32_t *args) {
        auto *list = reinterpret_cast<appdebugger::ListHeader *>(args[1]);
        struct Node {
            Node *next;
            Node *prev;
            std::string text;
        };
        auto *head = static_cast<Node *>(list->mHead);
        const uint32_t salt = args[0] % 1009 + args[3] * 7;
        const uint32_t n = Mix(gSeed, salt) % 3;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t pick = Mix(gSeed, salt * 5 + i) % 7;
            if (args[0] == 0xcc4368f3 && Mix(gSeed, salt * 3 + i) % 3 != 0) pick = 4; // content roots match rarely
            auto *node = static_cast<Node *>(appdebugger::AllocateListNodes(1, 0));
            new (&node->text) std::string(kPrefixes[pick]);
            node->next = head;
            node->prev = head->prev;
            head->prev->next = node;
            head->prev = node;
            ++list->mSize;
            gLog.push_back("  entry " + node->text);
        }
        return 1u;
    });
    // The segment (other); 0 and 22 are defined by the test.
    Def(adt::kOther, 2, 0, "");
    Def(adt::kOther, 3, 0, "", [](void *, const uint32_t *) { return Bit(3); });
    Def(adt::kOther, 5, 2, "ii", [](void *, const uint32_t *) { return Bit(5); });
    Def(adt::kOther, 7, 0, "", [](void *, const uint32_t *) { return Bit(7); });
    Def(adt::kOther, 9, 0, "", [](void *, const uint32_t *) { return Bit(9); });
    Def(adt::kOther, 12, 1, "S", [](void *, const uint32_t *) { return Bit(12); });
    Def(adt::kOther, 19, 2, "oo", [](void *, const uint32_t *args) {
        ++gCalls;
        const auto *filter = reinterpret_cast<const uint32_t *>(args[1]);
        gCase.filterType = filter[2];
        gLog.push_back("  filter type " + Hex(filter[2]) + " refs " + Hex(filter[1]) + (filter[0] == gFilterVTable ? " vt ok" : " vt other"));
        const uint32_t n = Mix(gSeed, 190 + gCalls) % 4;
        AppendKeys(reinterpret_cast<ServiceBlockVector *>(args[0]), n, filter[2]);
        const uint32_t r = Mix(gSeed, 195 + gCalls) % 6;
        return r == 0 ? 0u : r == 1 ? 0x100u : n;
    });
    // The group face (lock): 2 Release, 5 the group, 6 set it.
    Def(adt::kLock, 2, 0, "");
    Def(adt::kLock, 5, 0, "", [](void *, const uint32_t *) {
        const uint32_t r = Mix(gSeed, 500) % 10;
        return r < 6 ? gCase.groupValue : r < 9 ? 0xffffffffu : Mix(gSeed, 501);
    });
    Def(adt::kLock, 6, 1, "i", [](void *, const uint32_t *args) {
        gCase.groupValue = args[0];
        return Mix(gSeed, 6);
    });
    // Records (stream): 2 Release, 6 close, 8 read (buffer, size), 10 size.
    Def(adt::kStreamBase, 2, 0, "");
    Def(adt::kStreamBase, 6, 0, "");
    Def(adt::kStreamBase, 8, 2, "oi", [](void *, const uint32_t *args) {
        FillRecord(reinterpret_cast<uint8_t *>(args[0]), args[1]);
        return Bit(800 + gCalls, 4);
    });
    Def(adt::kStreamBase, 10, 0, "", [](void *, const uint32_t *) {
        ++gCalls;
        static const uint32_t globSizes[] = {0x10, 0x40, 0x41, 0x44, 0x50, 0x80};
        return gCase.filterType == 0x474c4f42 ? globSizes[Mix(gSeed, 1000 + gCalls) % 6] : Mix(gSeed, 1000 + gCalls) % 14;
    });
    // The key-status face (svc): 2, 5 group, 7 info, 12 (type, status).
    Def(adt::kConsoleService, 2, 0, "");
    Def(adt::kConsoleService, 5, 1, "i");
    Def(adt::kConsoleService, 7, 1, "i");
    Def(adt::kConsoleService, 12, 2, "ii");
    // The packed-file face (fw): 2, 49, 50.
    Def(adt::kFramework, 2, 0, "");
    Def(adt::kFramework, 49, 1, "i");
    Def(adt::kFramework, 50, 1, "i");
    // The directory-files face (log): 2, 10 group, 14 (type, extension), 23 converter, 28, 29.
    Def(adt::kLogService, 2, 0, "");
    Def(adt::kLogService, 10, 1, "i");
    Def(adt::kLogService, 14, 2, "iS");
    Def(adt::kLogService, 23, 1, "p", [](void *, const uint32_t *args) {
        gCase.converter = reinterpret_cast<void *>(args[0]);
        return 0u;
    });
    Def(adt::kLogService, 28, 1, "i");
    Def(adt::kLogService, 29, 1, "i");
}

void *gFaces[5]; // group, key status, packed, directory, record

} // namespace

PS_TEST(TSPersistSystemFL_SetUpSegment, 0x1e7230)
{
    using F = void(__thiscall *)(void *, cRZString *, cRZString *, uint32_t, uint32_t, void *);
    auto orig = ctx.Original<F>(0x1e7230);
    auto buildTable = ctx.Original<void(__thiscall *)(void *)>(0x1d8a50);
    auto emplace = ctx.Original<void(__thiscall *)(void *, void *, const void *)>(0x1d7390);

    adt::AllocatorFake alloc(ctx);
    gAlloc = &alloc;
    gFilterVTable = Word(ctx.image.At<uint8_t>(0xda5cf0));
    DefineFakes();
    // QueryInterface: each face from the iid, sometimes handed out but reported as failing, sometimes absent.
    Def(adt::kOther, 0, 2, "io", [](void *, const uint32_t *args) {
        static const uint32_t iids[] = {0xad095a01, 0x3b2271bb, 0x286d4a6a, 0x856c8cf7};
        uint32_t which = 0;
        while (which < 4 && iids[which] != args[0]) ++which;
        const uint32_t r = Mix(gSeed, 40 + which) % 5;
        void *answer = which < 4 && r != 0 ? gFaces[which] : nullptr;
        *reinterpret_cast<void **>(args[1]) = answer;
        return (Mix(gSeed, 45 + which) & 0xffffff00u) | (answer && r != 1 ? 1u : 0u);
    });
    // OpenRecord (key, out, mode): the key's words, then the record (or none, or one handed out on failure).
    Def(adt::kOther, 22, 3, "ooi", [](void *, const uint32_t *args) {
        ++gCalls;
        const auto *key = reinterpret_cast<const uint32_t *>(args[0]);
        gLog.push_back("  key " + Hex(key[0]) + " " + Hex(key[1]) + " " + Hex(key[2]) + " " + Hex(key[3]) + " mode " + Hex(args[2]));
        const uint32_t r = Mix(gSeed, 220 + gCalls) % 6;
        *reinterpret_cast<void **>(args[1]) = r == 0 ? nullptr : gFaces[4];
        return (Mix(gSeed, 230 + gCalls) & 0xffffff00u) | (r >= 2 ? 1u : 0u);
    });

    adt::Fake segment(adt::kOther, "seg"), group(adt::kLock, "grp"), keyStatus(adt::kConsoleService, "ks"), directory(adt::kLogService, "dir"),
        record(adt::kStreamBase, "rec");
    static auto packedVTable = adt::BuildVTable<64>(adt::kFramework);
    static adt::FakeObject packed{packedVTable.data(), adt::kFramework, {0, 0}};
    adt::gFakes[&packed] = adt::kFramework;
    adt::gLabels[&packed] = "packed";
    gFaces[0] = group.get();
    gFaces[1] = keyStatus.get();
    gFaces[2] = &packed;
    gFaces[3] = directory.get();
    gFaces[4] = record.get();
    auto itsVTable = adt::BuildVTable(adt::kConsoleFace);
    adt::Fake commonImpl(adt::kCommonImpl);
    adt::GlobalOverride<void *> commonCache(ctx, kCommonCache, commonImpl.get());
    CodePatch lengthImage(ctx.image.At<uint8_t>(0x1c1b8b), reinterpret_cast<void *>(&LengthStand));
    CodePatch lengthOurs(reinterpret_cast<void *>(&cRZFile::LengthOfPath), reinterpret_cast<void *>(&LengthStand));
    // The listing is ours now (DirectoryListing): stand in for the image's methods and ours.
    shape::StandIn listingCtor(0x1f58f0, &DirectoryListingAdapters::Construct, &ListingCtor);
    shape::StandIn listingFirst(0x1f6dd0, &DirectoryListing::First, &ListingFirst);
    shape::StandIn listingEntry(0x1f64f0, &DirectoryListing::EntryPath, &ListingEntry);
    shape::StandIn listingNext(0x1f61e0, &DirectoryListing::Next, &ListingNext);
    shape::StandIn listingDtor(0x1f6090, &DirectoryListingAdapters::Destruct, &ListingDtor);
    adt::gFakes[gPrimary + kSub] = adt::kConsoleFace;

    uint32_t belowRoot = 0, unknown = 0, scanned = 0, named = 0, globals = 0, strings = 0, templates = 0, listed = 0, bundle = 0;
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        std::mt19937 rng(seed * 31 + 7);
        Case start{};
        start.pathIndex = rng() % (sizeof(kPaths) / sizeof(kPaths[0]));
        start.nameIndex = rng() % (sizeof(kNames) / sizeof(kNames[0]));
        start.info = rng() % 3 ? rng() % 4 : rng();
        start.type = kTypes[rng() % 6];
        start.groupValue = 0x5eed0000 + seed;
        std::vector<std::pair<std::string, uint32_t>> entries;
        for (uint32_t n = rng() % 5; n > 0; --n) entries.push_back({kGroupNames[rng() % 8], rng()});

        struct Side {
            std::vector<std::string> log, table;
            std::vector<uint8_t> primary;
        } sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gCase = start;
            adt::ResetSide();
            std::mt19937 fill(seed);
            for (auto &b : gPrimary) b = static_cast<uint8_t>(fill());
            const uint32_t vt = Word(itsVTable.data());
            std::memcpy(gPrimary + kSub, &vt, 4);
            buildTable(gPrimary + kTable);
            for (const auto &e : entries) {
                cRZString key;
                key.Construct(e.first.c_str());
                struct {
                    GroupNode *node;
                    uint32_t inserted;
                } result{};
                emplace(gPrimary + kTable, &result, &key);
                result.node->group = e.second;
                key.Destruct();
            }
            const std::vector<std::string> before = DumpTable(gPrimary + kTable);
            gLog.clear();
            cRZString path, name;
            path.Construct(kPaths[start.pathIndex]);
            name.Construct(kNames[start.nameIndex]);
            ctx.Mark(side == 0 ? "original" : "ours");
            if (side == 0)
                orig(gPrimary, &path, &name, start.info, start.type, segment.get());
            else
                reinterpret_cast<cTSPersistSystem *>(gPrimary)->SetUpSegment(&path, &name, start.info, start.type, segment.get());
            path.Destruct();
            name.Destruct();
            sides[side].log = gLog;
            sides[side].table = DumpTable(gPrimary + kTable);
            if (side == 0 && sides[side].table != before) ++named;
            sides[side].primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            std::memset(sides[side].primary.data() + kTable, 0, 0x20); // the table's own words (heap pointers) are compared by content
            if (gCase.converter) ::operator delete(gCase.converter);
            alloc.Release();
        }
        ++ctx.cases;
        for (const std::string &line : sides[0].log) {
            if (line == "lock.6(0x0)") ++belowRoot;
            if (line == "lock.6(0xffffffff)") ++unknown;
            if (line.rfind("  filter type 0x4e524546", 0) == 0) ++scanned;
            if (line.rfind("face.12(", 0) == 0 && line.find("\"Global\"") != std::string::npos) ++globals;
            if (line == "svc.12(0x53545223, 0x2)") ++strings;
            if (line.rfind("log.23(", 0) == 0) ++templates;
            if (line.rfind("log.14(", 0) == 0 && start.type != 3) ++listed;
            if (line.rfind("len(", 0) == 0 && start.pathIndex == 1) ++bundle;
        }
        if (sides[0].log != sides[1].log)
            ctx.Fail("seed %u (%s|%s): calls differ: %s", seed, kPaths[start.pathIndex], kNames[start.nameIndex], adt::FirstDifference(sides[0].log, sides[1].log).c_str());
        if (sides[0].table != sides[1].table) ctx.Fail("seed %u: path groups differ: %s", seed, adt::FirstDifference(sides[0].table, sides[1].table).c_str());
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
    }
    if (belowRoot < 40 || unknown < 100 || scanned < 100 || named < 60 || globals < 20 || strings < 20 || templates < 30 || listed < 60 || bundle < 20)
        ctx.Fail("coverage: below root %u, unknown group %u, scanned %u, groups named %u, Global %u, string statuses %u, templates %u, listed "
                 "extensions %u, bundle paths %u",
                 belowRoot, unknown, scanned, named, globals, strings, templates, listed, bundle);
    gAlloc = nullptr;
    adt::gFakes.erase(gPrimary + kSub);
    adt::gFakes.erase(&packed);
    adt::gLabels.erase(&packed);
    for (int kind : {adt::kConsoleFace, adt::kCommonImpl, adt::kOther, adt::kLock, adt::kStreamBase, adt::kConsoleService, adt::kFramework, adt::kLogService})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}
