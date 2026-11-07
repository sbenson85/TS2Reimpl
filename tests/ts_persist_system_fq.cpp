// Differential test for the cTSPersistSystem function of lane FQ: the content-folder opener 0x1e3c70
// (cTSPersistSystem::OpenContentFolder, thiscall on the primary, ret 0x18).
//
// The world: one primary object block (0x300 bytes, rebuilt from the seed before each side) whose own vtable (slots 27
// AddSegment and 47) is a recording fake; real cRZString-keyed hash tables at +0xf4 (mNamedMap), +0x178 and +0x198 (built
// by the game's constructor 0x1d8a50, the last two filled from the seed through their try_emplace 0x1d7df0 with
// preloaded segments); the segment vector (+0xd4) over a fixed array with a seeded size and capacity; the COM director
// static (0x10c9390) whose framework object's GetClassObject hands out "created" fakes whose slot 3 is a segment fake
// (labelled seg0..seg5: 1 AddRef, 2 Release, 3 Init, 6 AutoOpen, 11 GetPath, 17 SetSegmentCache). The folder listing
// (0x1f58f0 constructor, 0x1f6dd0 first, 0x1f64f0 entry path, 0x1f65c0 kind, 0x1f61e0 next, 0x1f6090 destructor) is a
// recording stand-in that keeps its state in the listing's own bytes and lists seeded entries (folders and files,
// nested two levels); cRZFile's constructor, destructor and Open are stand-ins over the image's and ours (the
// constructor stores the image's cRZFile vtable, so the original's virtual Open reaches the stand-in). The original
// callees 0x1e6e80 (path key: the lowercased path), 0x1e7230 (set-up; also over our SetUpSegment), 0x1dab60 (key
// filters), 0x1e9b50 / 0x1e97b0 (group tables) and the segment vector's growth (0x1d57e0 and our
// ServiceRefVector::InsertGrown, verified elsewhere) are recording stand-ins; the game's operator new and sized delete
// go to ours. 0x1d84b0, 0x1d7df0, the string helpers, SplitInto, the out vector's growth (0x14aac0) and _stricmp run as
// they are. No file is touched.
// Compared: the result byte, the call log, the primary block outside the three tables' own words (heap pointers), the
// three tables walked in list order (key text and segment), the segment vector array and the out vector.
#include "difftest.h"
#include "app/AppDebugger.h"
#include "app_debugger_fakes.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSPersistSystem.h"
#include "ts/DirectoryListing.h"
#include "ts_persist_system_shape.h"

#include <algorithm>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <windows.h>

namespace {

using adt::Def;
using adt::gLog;

constexpr uint32_t kDirectorStatic = 0x10c9390, kDirectorGuard = 0x10c93f4;
constexpr uint32_t kNamedMap = 0xf4, kMap178 = 0x178, kMap198 = 0x198;

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

// The code address of one of our member functions.
template <typename M> void *MemberCode(M m)
{
    static_assert(sizeof(m) == sizeof(void *), "plain member pointer");
    void *p;
    std::memcpy(&p, &m, sizeof(p));
    return p;
}

uint32_t gSeed = 0;
uint32_t gCalls = 0;
uint32_t gCreatedCount = 0;
alignas(8) uint8_t gPrimary[0x300];
void *gSegArray[8];
void *gFileVTable = nullptr;
void *gSegs[6];
void *gCreated[3];

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

std::string Lower(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// A byte answer with garbage above it (the callers test al only).
uint32_t Bit(uint32_t salt, uint32_t modulus = 2)
{
    return (Mix(gSeed, salt) & 0xffffff00u) | (Mix(gSeed, salt ^ 0x5a5a) % modulus != 0 ? 1u : 0u);
}

// ---- the listed entries ----

struct PoolEntry {
    const char *name;
    uint8_t kind; // 4 folder, 8 file
};
const PoolEntry kPool[] = {
    {"Gizmo.package", 8}, {"UPPER.PACKAGE", 8}, {"ui.string.files", 4}, {"Scene.sgfiles", 4}, {"CASFace", 4}, {"casface", 4},
    {"Objects", 4}, {"Sims", 4}, {"Thumbnails", 4}, {"Textures", 4}, {"LightRigs", 4}, {"Downloads", 4}, {"Neighborhoods", 4},
    {"readme.txt", 8}, {"Thing.Package", 8}, {"N001", 4}, {"A Folder Name Long Enough For The Heap", 4},
    {"Big Long Package Name Past Sixteen.package", 8}, {"noext", 8}, {"Weird.files", 8}, {"Sims.package", 4}, {"Textures.old", 4},
    {"Downloads", 4}, {"Thumbnails.SGFILES", 4},
};
constexpr uint32_t kPoolSize = sizeof(kPool) / sizeof(kPool[0]);

// The listing's state, kept in its own 0x3c bytes.
struct ListingState {
    uint32_t count;
    uint32_t at;
    uint32_t hash;
    std::string *path;
};
static_assert(sizeof(ListingState) <= 0x3c, "listing state fits the listing");

uint32_t Slashes(const std::string &s)
{
    uint32_t n = 0;
    for (char c : s) n += c == '/';
    return n;
}

void *__fastcall ListingCtor(void *self, void *, const cRZString *path, void *system)
{
    auto *s = static_cast<ListingState *>(self);
    s->path = new std::string(path->mString);
    s->hash = TextHash(path->mString);
    s->at = 0;
    s->count = Slashes(path->mString) >= 4 ? 0 : Mix(gSeed, s->hash) % 6;
    gLog.push_back("listing(S:" + adt::Describe(path) + ", " + (system == gPrimary ? "primary" : "?") + ") " + Hex(s->count));
    return self;
}
uint32_t __fastcall ListingFirst(void *self, void *)
{
    auto *s = static_cast<ListingState *>(self);
    s->at = 0;
    gLog.push_back("listing.first");
    return (Mix(gSeed, 901) & 0xffffff00u) | (s->count ? 1u : 0u);
}
const PoolEntry &EntryOf(const ListingState *s)
{
    return kPool[Mix(gSeed, s->hash * 7 + s->at) % kPoolSize];
}
void *__fastcall ListingEntry(void *self, void *, cRZString *out)
{
    auto *s = static_cast<ListingState *>(self);
    out->Construct((*s->path + "\\" + EntryOf(s).name).c_str());
    gLog.push_back(std::string("listing.entry ") + EntryOf(s).name);
    return out;
}
uint32_t __fastcall ListingKind(void *self, void *)
{
    auto *s = static_cast<ListingState *>(self);
    const uint32_t r = Mix(gSeed, s->hash * 11 + s->at);
    const uint8_t natural = EntryOf(s).kind;
    const uint32_t pick = r % 12;
    const uint32_t kind = pick == 0 ? (natural ^ 12u) : pick == 1 ? 0u : pick == 2 ? 12u : pick == 3 ? (natural | 0x40u) : natural;
    return (Mix(gSeed, r) & 0xffffff00u) | kind;
}
uint32_t __fastcall ListingNext(void *self, void *)
{
    auto *s = static_cast<ListingState *>(self);
    ++s->at;
    gLog.push_back("listing.next");
    return (Mix(gSeed, 920 + s->at) & 0xffffff00u) | (s->at < s->count ? 1u : 0u);
}
void __fastcall ListingDtor(void *self, void *)
{
    auto *s = static_cast<ListingState *>(self);
    gLog.push_back("listing.dtor " + *s->path);
    delete s->path;
    s->path = nullptr;
}

// ---- cRZFile (constructor, destructor, Open): image and ours ----

std::map<const void *, std::string> gFiles;

void *__fastcall FileConstruct(void *self, void *, const cIGZString *path)
{
    const std::string text = reinterpret_cast<const cRZString *>(path)->mString;
    std::memcpy(self, &gFileVTable, 4);
    gFiles[self] = text;
    gLog.push_back("file(" + text + ")");
    return self;
}
void __fastcall FileDestruct(void *self, void *)
{
    gLog.push_back("~file(" + gFiles[self] + ")");
    gFiles.erase(self);
}
uint32_t __fastcall FileOpen(void *self, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    ++gCalls;
    const uint32_t r = Bit(1500 + gCalls);
    gLog.push_back("file.Open(" + gFiles[self] + ", " + Hex(access) + ", " + Hex(creation) + ", " + Hex(sharing) + ") -> " + Hex(r & 0xff));
    return r;
}

// ---- other stand-ins (the same on both sides) ----

void *__cdecl GameNew(size_t n)
{
    return ::operator new(n);
}
void __cdecl GameDelete(void *p, size_t)
{
    ::operator delete(p);
}

void __fastcall KeyStand(void *self, void *, const cRZString *path, cRZString *key)
{
    gLog.push_back("key(" + std::string(self == gPrimary ? "primary" : "?") + ", S:" + adt::Describe(path) + ", S:" + adt::Describe(key) + ")");
    key->mString = Lower(path->mString);
}
void __fastcall SetUpStand(void *self, void *, const cRZString *path, const cRZString *name, uint32_t info, uint32_t type, void *segment)
{
    gLog.push_back("0x1e7230(" + std::string(self == gPrimary ? "primary" : "?") + ", S:" + adt::Describe(path) + ", S:" + adt::Describe(name) + ", " +
                   Hex(info) + ", " + Hex(type) + ", " + adt::Tag(segment) + ")");
}
void __cdecl FiltersStand(void *created)
{
    gLog.push_back("0x1dab60(" + adt::Tag(created) + ")");
}
uint32_t __fastcall GroupsStand(void *self, void *, void *segment)
{
    gLog.push_back("0x1e9b50(" + std::string(self == gPrimary ? "primary" : "?") + ", " + adt::Tag(segment) + ")");
    return Mix(gSeed, 700);
}
uint32_t __fastcall RangesStand(void *self, void *, void *segment)
{
    gLog.push_back("0x1e97b0(" + std::string(self == gPrimary ? "primary" : "?") + ", " + adt::Tag(segment) + ")");
    return Mix(gSeed, 701);
}
void **__fastcall InsertGrownStand(void *self, void *, void **where, void *const *value)
{
    gLog.push_back("grow(" + std::string(self == gPrimary + 0xd4 ? "segments" : "?") + ", " + std::to_string((Word(where) - Word(gSegArray)) / 4) + ", " +
                   adt::Tag(*value) + ")");
    return where;
}

// The segments' slot 6 (AutoOpen): the original pushes a whole dword of which it set only the low byte (0x1e43a8,
// 0x1e481a: `push dword ptr [ebp - 0x38]` after `mov byte ptr [ebp - 0x38], al`), so only that byte is logged.
uint32_t __fastcall SegmentAutoOpen(void *self, void *, uint32_t read, uint32_t write)
{
    gLog.push_back("other.6(" + adt::Tag(self) + ", " + Hex(read) + ", " + Hex(write & 0xff) + ")");
    return Mix(gSeed, 800);
}

// ---- the tables ----

struct PathNode {
    PathNode *next;
    PathNode *prev;
    cRZString key;
    void *segment;
};
static_assert(sizeof(PathNode) == 0x2c, "path segment node");

std::vector<std::string> DumpTable(const uint8_t *table)
{
    std::vector<std::string> out;
    PathNode *head = *reinterpret_cast<PathNode *const *>(table + 4);
    for (PathNode *n = head->next; n != head; n = n->next) out.push_back(n->key.mString + "=" + adt::Tag(n->segment));
    out.push_back("size " + Hex(*reinterpret_cast<const uint32_t *>(table + 8)));
    return out;
}

// The lists the function reads: std::list<cRZString> (the game's cRZString by value, refcount 0).
using StringList = std::list<cRZString>;
void Fill(StringList &list, const std::vector<std::string> &texts, const void *vtable)
{
    for (const std::string &t : texts) {
        list.emplace_back();
        list.back().mVTable = vtable;
        list.back().mString = t;
        list.back().mRefCount = 0;
    }
}

const char *const kRoots[] = {"C:\\Sims\\Content", "D:\\Other Place With A Long Name\\Stuff", "C:\\Sims\\Content", "E:\\x"};
const char *const kCategoryNames[] = {"downloads", "neighborhoods", "n001", "objects", "a folder name long enough for the heap", "Downloads", "sims"};
const char *const kWantedNames[] = {"gizmo.package", "GIZMO.PACKAGE", "ui.string.files", "thumbnails", "Textures", "Big Long Package Name Past Sixteen.package",
                                    "nothing", "scene.sgfiles", "thing.package"};

struct SideResult {
    uint32_t ret = 0;
    std::vector<std::string> log, named, map178, map198, segs, out;
    std::vector<uint8_t> primary;
};

} // namespace

PS_TEST(TSPersistSystemFQ_OpenContentFolder, 0x1e3c70)
{
    using F = uint32_t(__thiscall *)(void *, uint32_t, cRZString *, uint32_t, void *, void *, void *);
    auto orig = ctx.Original<F>(0x1e3c70);
    auto buildTable = ctx.Original<void(__thiscall *)(void *)>(0x1d8a50);
    auto emplace = ctx.Original<void *(__thiscall *)(void *, void *, const void *)>(0x1d7df0);
    gFileVTable = ctx.image.At<uint8_t>(cRZFile::kVTable);
    const void *stringVTable = ctx.image.At<uint8_t>(cRZString::kVTable);

    static void *comPtr = nullptr;
    // The primary (kConsole): 27 AddSegment, 47 a value for the new segment's slot 17.
    Def(adt::kConsole, 27, 1, "p", [](void *, const uint32_t *) { return Mix(gSeed, 27); });
    Def(adt::kConsole, 47, 0, "", [](void *, const uint32_t *) { return Mix(gSeed, 47); });
    // The COM director and the framework object (GetClassObject: class, iid, out).
    Def(adt::kAdapter, 12, 0, "", [](void *, const uint32_t *) { return Word(comPtr); });
    Def(adt::kLogService, 3, 3, "iio", [](void *, const uint32_t *args) {
        ++gCalls;
        const uint32_t r = Mix(gSeed, 30 + gCalls) % 6;
        if (r != 0) *reinterpret_cast<void **>(args[2]) = gCreated[gCreatedCount++ % 3]; // r == 1: handed out but reported as failing
        return (Mix(gSeed, 31 + gCalls) & 0xffffff00u) | (r >= 2 ? 1u : 0u);
    });
    // The created objects (kConsoleService): 2 Release, 3 the segment (created i -> seg 2 + i).
    Def(adt::kConsoleService, 2, 0, "");
    Def(adt::kConsoleService, 3, 0, "", [](void *self, const uint32_t *) {
        for (int i = 0; i < 3; ++i)
            if (gCreated[i] == self) return Word(gSegs[2 + i + (Mix(gSeed, 33) % 2 ? 1 : 0)]);
        return 0u;
    });
    // Segments (kOther): 1 AddRef, 2 Release, 3 Init, 6 AutoOpen (replaced below), 11 GetPath, 17 SetSegmentCache.
    Def(adt::kOther, 1, 0, "");
    Def(adt::kOther, 2, 0, "");
    Def(adt::kOther, 3, 0, "", [](void *, const uint32_t *) {
        ++gCalls;
        return Bit(300 + gCalls, 3);
    });
    Def(adt::kOther, 11, 1, "o", [](void *self, const uint32_t *args) {
        ++gCalls;
        static const char *const kPaths[] = {"C:\\Sims\\Content\\Downloads\\Data.package", "c:/sims/content/UI.STRING.FILES/", "D:\\A Path Long Enough\\Thing.Package",
                                             "E:/x/y"};
        reinterpret_cast<cRZString *>(args[0])->mString = kPaths[Mix(gSeed, 110 + gCalls) % 4] + adt::Tag(self);
        return Mix(gSeed, 11);
    });
    Def(adt::kOther, 17, 1, "i");

    adt::Fake com(adt::kLogService, "com");
    static const char *const kSegLabels[] = {"seg0", "seg1", "seg2", "seg3", "seg4", "seg5"};
    static const char *const kCreatedLabels[] = {"created0", "created1", "created2"};
    std::vector<std::unique_ptr<adt::Fake>> fakes;
    for (int i = 0; i < 6; ++i) {
        fakes.emplace_back(new adt::Fake(adt::kOther, kSegLabels[i]));
        fakes.back()->vtable[6] = reinterpret_cast<void *>(&SegmentAutoOpen);
        gSegs[i] = fakes.back()->get();
    }
    for (int i = 0; i < 3; ++i) {
        fakes.emplace_back(new adt::Fake(adt::kConsoleService, kCreatedLabels[i]));
        gCreated[i] = fakes.back()->get();
    }
    comPtr = com.get();
    auto primaryVTable = adt::BuildVTable(adt::kConsole);
    auto directorVTable = adt::BuildVTable(adt::kAdapter);
    adt::GlobalOverride<void *> director(ctx, kDirectorStatic, directorVTable.data());
    adt::GlobalOverride<int32_t> directorGuard(ctx, kDirectorGuard, INT32_MIN);
    adt::gFakes[gPrimary] = adt::kConsole;
    adt::gLabels[gPrimary] = "primary";
    adt::gFakes[ctx.image.At<uint8_t>(kDirectorStatic)] = adt::kAdapter;

    using CtorString = cRZFile *(cRZFile::*)(const cIGZString &);
    std::vector<std::unique_ptr<CodePatch>> patches;
    auto patch = [&](uint32_t rva, void *stand) { patches.emplace_back(new CodePatch(ctx.image.At<uint8_t>(rva), stand)); };
    auto patchOurs = [&](void *ours, void *stand) { patches.emplace_back(new CodePatch(ours, stand)); };
    patch(0x824632, reinterpret_cast<void *>(&GameNew));
    patch(0x824624, reinterpret_cast<void *>(&GameDelete));
    // The listing is ours now (DirectoryListing): stand in for the image's methods and ours.
    patch(0x1f58f0, reinterpret_cast<void *>(&ListingCtor));
    patchOurs(reinterpret_cast<void *>(&DirectoryListingAdapters::Construct), reinterpret_cast<void *>(&ListingCtor));
    patch(0x1f6dd0, reinterpret_cast<void *>(&ListingFirst));
    patchOurs(MemberCode(&DirectoryListing::First), reinterpret_cast<void *>(&ListingFirst));
    patch(0x1f64f0, reinterpret_cast<void *>(&ListingEntry));
    patchOurs(MemberCode(&DirectoryListing::EntryPath), reinterpret_cast<void *>(&ListingEntry));
    patch(0x1f65c0, reinterpret_cast<void *>(&ListingKind));
    patchOurs(MemberCode(&DirectoryListing::EntryKind), reinterpret_cast<void *>(&ListingKind));
    patch(0x1f61e0, reinterpret_cast<void *>(&ListingNext));
    patchOurs(MemberCode(&DirectoryListing::Next), reinterpret_cast<void *>(&ListingNext));
    patch(0x1f6090, reinterpret_cast<void *>(&ListingDtor));
    patchOurs(reinterpret_cast<void *>(&DirectoryListingAdapters::Destruct), reinterpret_cast<void *>(&ListingDtor));
    patch(0x1c0c07, reinterpret_cast<void *>(&FileConstruct));
    patchOurs(MemberCode(static_cast<CtorString>(&cRZFile::Construct)), reinterpret_cast<void *>(&FileConstruct));
    patch(0x1c0e6d, reinterpret_cast<void *>(&FileDestruct));
    patchOurs(MemberCode(&cRZFile::Destruct), reinterpret_cast<void *>(&FileDestruct));
    patch(0x1c285d, reinterpret_cast<void *>(&FileOpen));
    patchOurs(MemberCode(&cRZFile::Open), reinterpret_cast<void *>(&FileOpen));
    patch(0x1e6e80, reinterpret_cast<void *>(&KeyStand));
    patchOurs(shape::MemberCode(&cTSPersistSystem::MakePathKey), reinterpret_cast<void *>(&KeyStand));
    patch(0x1e7230, reinterpret_cast<void *>(&SetUpStand));
    patchOurs(shape::MemberCode(&cTSPersistSystem::SetUpSegment), reinterpret_cast<void *>(&SetUpStand));
    patch(0x1dab60, reinterpret_cast<void *>(&FiltersStand));
    patch(0x1e9b50, reinterpret_cast<void *>(&GroupsStand));
    patch(0x1e97b0, reinterpret_cast<void *>(&RangesStand));
    patchOurs(shape::MemberCode(&cTSPersistSystem::IndexSegmentGroups), reinterpret_cast<void *>(&GroupsStand));
    patchOurs(shape::MemberCode(&cTSPersistSystem::SegmentGroups), reinterpret_cast<void *>(&RangesStand));
    patch(0x1d57e0, reinterpret_cast<void *>(&InsertGrownStand));
    patchOurs(MemberCode(&ServiceRefVector::InsertGrown), reinterpret_cast<void *>(&InsertGrownStand));

    uint32_t created = 0, relisted = 0, skipped = 0, recursed = 0, excluded = 0, filtered = 0, types[4] = {}, grown = 0, outGrown = 0, packages = 0;
    for (uint32_t seed = 0; seed < 1000; ++seed) {
        std::mt19937 rng(seed * 37 + 5);
        const std::string root = kRoots[rng() % 4];
        const uint32_t mode = rng() % 3 ? rng() % 3 : rng();
        static const uint32_t kRecursive[] = {0, 1, 1, 0x101, 0x200};
        const uint32_t recursive = kRecursive[rng() % 5];
        const uint32_t segCount = rng() % 4, segCap = segCount + rng() % 3;
        const uint32_t outMode = rng() % 4; // 0 none, else a vector with capacity outMode - 1
        // Preloaded table entries: (table 178 or 198, key, segment).
        struct Preload {
            bool in178;
            std::string key;
            int seg;
        };
        std::vector<Preload> preloads;
        for (uint32_t n = rng() % 6; n > 0; --n) {
            // Mostly entries the top-level listing will show (ListingEntry's choice for this root), else any.
            const PoolEntry &entry = rng() % 4 ? kPool[Mix(seed, TextHash(root) * 7 + rng() % 5) % kPoolSize] : kPool[rng() % kPoolSize];
            const bool slash = (entry.kind == 4) != (rng() % 4 == 0); // folders of types 2 and 3 end in '/'
            std::string key = Lower(std::string("C:/Sims/Content/") + (rng() % 4 == 0 ? "downloads/" : "") + entry.name) + (slash ? "/" : "");
            preloads.push_back({rng() % 3 != 0, key, static_cast<int>(rng() % 2)});
        }
        StringList category, wanted;
        const bool useCategory = rng() % 3 != 0, useWanted = rng() % 4 == 0;
        {
            std::vector<std::string> names;
            for (uint32_t n = rng() % 4; n > 0; --n) names.push_back(kCategoryNames[rng() % 7]);
            Fill(category, names, stringVTable);
            names.clear();
            for (uint32_t n = rng() % 4; n > 0; --n) names.push_back(kWantedNames[rng() % 9]);
            Fill(wanted, names, stringVTable);
        }

        SideResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gCreatedCount = 0;
            adt::ResetSide();
            std::mt19937 fill(seed);
            for (auto &b : gPrimary) b = static_cast<uint8_t>(fill());
            auto put = [](uint32_t off, uint32_t v) { std::memcpy(gPrimary + off, &v, 4); };
            put(0, Word(primaryVTable.data()));
            for (auto &p : gSegArray) p = nullptr;
            for (uint32_t i = 0; i < segCount; ++i) gSegArray[i] = gSegs[i % 2];
            put(0xd4, Word(gSegArray));
            put(0xd8, Word(gSegArray + segCount));
            put(0xdc, Word(gSegArray + segCap));
            buildTable(gPrimary + kNamedMap);
            buildTable(gPrimary + kMap178);
            buildTable(gPrimary + kMap198);
            for (const Preload &p : preloads) {
                cRZString key;
                key.Construct(p.key.c_str());
                struct {
                    PathNode *node;
                    uint32_t inserted;
                } result{};
                emplace(gPrimary + (p.in178 ? kMap178 : kMap198), &result, &key);
                result.node->segment = gSegs[p.seg];
                key.Destruct();
            }
            std::vector<void *> out;
            if (outMode) out.reserve(outMode - 1);
            cRZString path;
            path.Construct(root.c_str());
            gLog.clear();
            ctx.Mark(side == 0 ? "original" : "ours");
            void *categoryArg = useCategory ? &category : nullptr;
            void *outArg = outMode ? &out : nullptr;
            void *wantedArg = useWanted ? &wanted : nullptr;
            const uint32_t r = side == 0 ? orig(gPrimary, mode, &path, recursive, categoryArg, outArg, wantedArg)
                                         : reinterpret_cast<cTSPersistSystem *>(gPrimary)->OpenContentFolder(mode, &path, recursive, categoryArg, outArg, wantedArg);
            path.Destruct();
            SideResult &res = sides[side];
            res.ret = r & 0xff;
            res.log = gLog;
            res.named = DumpTable(gPrimary + kNamedMap);
            res.map178 = DumpTable(gPrimary + kMap178);
            res.map198 = DumpTable(gPrimary + kMap198);
            for (void *p : gSegArray) res.segs.push_back(adt::Tag(p));
            for (void *p : out) res.out.push_back(adt::Tag(p));
            res.out.push_back("capacity " + std::to_string(out.capacity()));
            res.primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            for (uint32_t table : {kNamedMap, kMap178, kMap198}) std::memset(res.primary.data() + table, 0, 0x20); // compared by content
        }
        ++ctx.cases;
        bool anyCreated = false;
        for (const std::string &line : sides[0].log) {
            if (line.rfind("0x1e7230(", 0) == 0) {
                ++created;
                anyCreated = true;
                for (uint32_t t = 1; t <= 3; ++t)
                    if (line.find(", " + Hex(t) + ", <seg") != std::string::npos) ++types[t];
            }
            if (line.rfind("0x1e9b50(", 0) == 0) ++relisted;
            if (line.rfind("listing(S:vptr:C:/", 0) == 0 || line.rfind("listing(S:vptr:D:/", 0) == 0 || line.rfind("listing(S:vptr:E:/", 0) == 0) ++recursed;
            if (line.rfind("grow(", 0) == 0) ++grown;
            if (line.rfind("file.Open(", 0) == 0) ++packages;
        }
        if (anyCreated && outMode == 1 && sides[0].out.size() > 1) ++outGrown;
        if (useCategory && !category.empty() && recursive & 0xff) ++excluded;
        if (useWanted) ++filtered;
        const uint32_t keys = static_cast<uint32_t>(std::count_if(sides[0].log.begin(), sides[0].log.end(), [](const std::string &l) { return l.rfind("key(", 0) == 0; }));
        skipped += keys;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) ctx.Fail("seed %u (%s): calls differ: %s", seed, root.c_str(), adt::FirstDifference(sides[0].log, sides[1].log).c_str());
        if (sides[0].named != sides[1].named) ctx.Fail("seed %u: mNamedMap differs: %s", seed, adt::FirstDifference(sides[0].named, sides[1].named).c_str());
        if (sides[0].map178 != sides[1].map178) ctx.Fail("seed %u: mMap178 differs: %s", seed, adt::FirstDifference(sides[0].map178, sides[1].map178).c_str());
        if (sides[0].map198 != sides[1].map198) ctx.Fail("seed %u: mMap198 differs: %s", seed, adt::FirstDifference(sides[0].map198, sides[1].map198).c_str());
        if (sides[0].segs != sides[1].segs) ctx.Fail("seed %u: segment vector differs: %s", seed, adt::FirstDifference(sides[0].segs, sides[1].segs).c_str());
        if (sides[0].out != sides[1].out) ctx.Fail("seed %u: out vector differs: %s", seed, adt::FirstDifference(sides[0].out, sides[1].out).c_str());
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
    }
    if (created < 200 || relisted < 50 || recursed < 100 || types[1] < 30 || types[2] < 30 || types[3] < 30 || grown < 20 || outGrown < 10 || packages < 50)
        ctx.Fail("coverage: created %u (types %u/%u/%u), relisted %u, recursed %u, grown %u, out grown %u, package opens %u, keys %u", created, types[1], types[2],
                 types[3], relisted, recursed, grown, outGrown, packages, skipped);
    (void)excluded;
    (void)filtered;
    adt::gFakes.erase(gPrimary);
    adt::gLabels.erase(gPrimary);
    adt::gFakes.erase(ctx.image.At<uint8_t>(kDirectorStatic));
    for (int kind : {adt::kConsole, adt::kAdapter, adt::kLogService, adt::kConsoleService, adt::kOther})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}
