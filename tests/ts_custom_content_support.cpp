// Differential test for cTSCustomContentSupport (lane JB, src/ts/cTSCustomContentSupport.h): the class in the standalone shape.
//
// CustomContent_Run: a service built by the original constructor (0x38e0a0, game vtables 0xdbc854 / 0xdbc890 / 0xdbc8ac /
// 0xdbc8c0) or the original factory (0x3989c0) against one built by ours (the constructor hook's adapter, or Create), each
// driven through its own vtables by the same random sequence of operations, then destroyed by the last Release (the
// deleting destructor 0x38f970 behind the +4 thunk 0x38f95b, the destructor 0x38f420). Operations: Init (0x39cbb0),
// Shutdown (0x39fac0), DoMessage (0x398210), QueryInterface (0x39f030; thunks 0x39f061 / 0x39f069 / 0x39f071) / AddRef /
// Release on every face, the service accessors, SaveSnapshot (0x39a1d0), InstallFile (0x39eaa0), Reset (folded 0x1ae2f0),
// WrittenFilesSize (0x39cb10), GetCategoryName / GetTypeName (0x39bed0 / 0x39bf00, indexes around the table ends),
// FindCategory / FindType (0x39b0f0 / 0x39c470), LotObjectsAllowed (0x390be0), IsRegistered (0x39e6e0), Register
// (0x3975d0), Install (0x39e820), Commit (0x39cd10), SubmitWrittenFiles (0x39faf0), Cleanup (0x39fd30), ClosePackages
// (0x391040) with the state they read (records, package file names, +0x58, the state word and current object: the state
// code 0x39cb80), ResourceInUse (0x390d80), and every slot still game code through each side's vtable.
//
// The category and type name tables (static cRZStrings at 0x100a6e8 / 0x100a888, built by the game's initialiser, which
// the harness does not run) are built by the test from a pool of names per seed: duplicates after lower-casing, "Assets",
// "PET", long names. Every object the service reaches is a recording fake (tests/ts_services_fixture.h); stood in for on
// both sides: cRZFile::PathExists / LengthOfPath (0x1c1242 / 0x1c1b8b), CommonImplSlot4 (0x1d0c00), cRZFileName's
// constructor / destructor (0x1c7ef4 / 0x1c0eb2: the real one reaches GetCurrentDirectoryW), and on the image the
// message server getter 0x9c5a89, the directory maker 0x1c5031, the file deleter 0x1c2e25, the cRZFileName directory
// compare 0x1c8151 and the entries of the slots still game code (0x39eb80, 0x39f680, 0x39cf70, 0x39d400, 0x391c20,
// 0x3923f0, 0x39a2f0, 0x390400, 0x397730, 0x396070, 0x398240): each is a recorder logging `this` (the face), its
// arguments and returning a drawn value, so the original's calls through the game's vtable and ours through the forwarders must meet in the same order with
// the same arguments. Compared after every operation: the result (the low byte where the original returns a bool in al),
// the event log, every field (strings by content and capacity, vectors with their capacity, both maps in order; the
// never-written +0xc9..+0xcb too: blocks start as 0xc5) and every fake's count.
#include "rz/RZMessageServer.h"
#include "code_patch.h"
#include "difftest.h"
#include "rz/PathFileOps.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "app/AppInit.h"
#include "app/SpecialFolders.h"
#include "game/game.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocate
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSCustomContentSupport.h"

#include <cstring>
#include <new>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ccs_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{17, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSCustomContentSupport;

constexpr uint32_t kTag = 0x7d000000;      // the service under test
constexpr uint32_t kCategoryTag = 0x7c000000; // the static name tables
constexpr uint32_t kTypeTag = 0x7b000000;

enum : unsigned {
    kApp = 0, kService, kOptions, kRegistry, kPersist, kManager, kTracker, kImageService, kImages, kPicture, kSource,
    kResource, kKey, kEntry, kMessage, kLotSource, kLot, kBlocked, kObjects, kDefinition, kKindYes, kKindNo, kOut, kName,
};
// More fakes: the message server, the lot's objects, the files slot 13 hands out.
enum : unsigned {
    mServer = 0, mObject0, mObject5 = mObject0 + 5, mFile0, mFile2 = mFile0 + 2, mComFw, mInstaller, mTexts, mCommitter,
    mCurrent, mPackage, mOwner, kMore
};
Obj gMore[kMore];
Obj &M(unsigned i) { return gMore[i]; }

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); } // a bool in al with a busy upper byte

uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}

void LogText(const std::string &text)
{
    Ev(uint32_t(text.size()));
    for (char c : text) Ev(uint8_t(c));
}
void LogString(uint32_t at) { LogText(reinterpret_cast<const cRZString *>(uintptr_t(at))->mString); }

// ---- the world for a seed ----
struct World {
    bool framework;     // GetServiceObject answers
    bool app;           // the framework's slot 37 object
    bool appPaths;      // it answers the options iid
    bool registry;
    bool persist;
    bool tracker;
    bool imageService;
    bool images;
    bool picture;       // the source hands out a picture
    bool lot;
    bool installer;
    bool noBlocked;     // the app's slot 26 answers null
    bool cached[3];     // the registry / persist / tracker cells already filled
};
World gWorld;
// The persist system is there (the cell filled, or the framework hands it out).
bool PersistUsable() { return gWorld.persist && (gWorld.framework || gWorld.cached[1]); }

// The name pool for the static tables and the lookups.
const char *const kNames[] = {"Hair", "hair", "Skin", "A Long Category Name, Over Fifteen", "Assets", "assets", "Pet", "PET",
                              "Sim", "Part", "Objects", "Walls", "", "Neighborhood Deco", "x", "MakeUp", "makeup"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);
const char *const kPaths[] = {"C:\\Downloads\\a.package", "short", "C:\\Users\\Sim\\Documents\\EA Games\\Downloads\\long file name.package"};

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void *__cdecl StandServer() { return &M(mServer); }

uint32_t __cdecl StandEnsureDirectory(uint32_t path)
{
    Ev(0x1c5031);
    Ev(svcfx::Norm(path));
    LogString(path);
    return 1;
}
// StandCommonSlot4's arguments as logged (svcfx::DumpWords): the id, the out string, then the rest.
constexpr char kCommonSlot4Kinds[] = "dpd-d";
uint32_t __cdecl StandCommonSlot4(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    Ev(0x1d0c00);
    const uint32_t words[] = {a, b, c, d, e};
    svcfx::DumpWords(svcfx::gEv, words, sizeof(words), kCommonSlot4Kinds);
    if (a == 0xece41771) reinterpret_cast<cRZString *>(uintptr_t(b))->mString.assign(kPaths[Draw(3)]);
    return Draw(2);
}
bool __cdecl StandPathExists(const cIGZString &path)
{
    Ev(0x1c1242);
    LogString(P(&path));
    return Draw(3) != 0;
}
uint32_t __cdecl StandLengthOfPath(const cIGZString &path)
{
    Ev(0x1c1b8b);
    LogString(P(&path));
    return Draw(4) == 0 ? 0xffffffffu : 0x1000u + Draw(0x10000);
}

// ---- the slots still game code: recorders over the originals' entries ----

bool gTrackFiles; // slot 13's files vector: what was handed out is released by InstallFile

uint32_t OnSlot(uint32_t rva, void *self, const uint32_t *a)
{
    switch (rva) {
    case 0x391c20: { // slot 13 (file, files, out word): 0-3 counted files in an allocator-service block
        auto *files = reinterpret_cast<ServiceRefVector *>(uintptr_t(a[1]));
        const uint32_t n = Draw(4);
        if (n) {
            auto **block = static_cast<void **>(GZServiceAllocate((n + Draw(2)) * 4));
            for (uint32_t i = 0; i < n; ++i) {
                Obj &f = M(mFile0 + Draw(3));
                ++f.refs;
                block[i] = &f;
            }
            files->mFirst = block;
            files->mLast = block + n;
            files->mEnd = block + n;
        }
        *reinterpret_cast<uint32_t *>(uintptr_t(a[2])) = 0x13131313;
        return Bool();
    }
    case 0x396070: // slot 28 (image, path)
        LogString(a[1]);
        return Bool();
    default: (void)self; return gRng();
    }
}

// Record's arguments by the stood-in function, as logged (svcfx::detail::DumpKinds letters, one per argument).
constexpr const char *RecordKinds(uint32_t rva)
{
    switch (rva) {
    case 0x390400: return "d";
    case 0x391c20: return "ppp"; // the file, the files vector, the out word
    case 0x3923f0: return "ddddd";
    case 0x396070: return "dp";  // the image (data here), the path
    case 0x397730: return "dd";
    case 0x398240: return "dd";
    case 0x39cf70: return "dd";
    case 0x39eb80: return "ddd";
    case 0x39f680: return "d";
    default: return "";
    }
}
template <uint32_t Rva, unsigned... I> uint32_t __fastcall Record(void *self, void *, shape::StackWord<I>... w)
{
    const uint32_t words[] = {0u, w...};
    Ev(0xf0000000u | Rva);
    Ev(svcfx::Norm(P(self)));
    svcfx::detail::DumpKinds(svcfx::gEv, sizeof...(I), RecordKinds(Rva), [&](size_t i) { return words[i + 1]; }, [](size_t i) {
        char text[80];
        std::snprintf(text, sizeof(text), "Record %#x argument %u", Rva, unsigned(i));
        return std::string(text);
    });
    return OnSlot(Rva, self, words + 1);
}
template <uint32_t Rva, unsigned... I> const void *RecordAt(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<const void *>(&Record<Rva, I...>);
}
template <uint32_t Rva, unsigned N> const void *RecordFor() { return RecordAt<Rva>(std::make_integer_sequence<unsigned, N>()); }

struct GameSlot {
    unsigned slot;
    uint32_t rva;
    unsigned words;
};
// The cITSCustomContentSupport slots still game code (the forwarders).
constexpr GameSlot kGameSlots[] = {
    {4, 0x39eb80, 3}, {8, 0x39f680, 1}, {11, 0x39cf70, 2}, {12, 0x39d400, 0}, {13, 0x391c20, 3}, {14, 0x3923f0, 5},
    {16, 0x39a2f0, 0}, {22, 0x390400, 1}, {24, 0x397730, 2}, {28, 0x396070, 2}, {29, 0x398240, 2},
};

// ---- fake handlers ----

// The framework's slot 5 GetSystemService(class, iid, out).
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    switch (a[0]) {
    case 0x0dc7057a: return gWorld.registry ? HandOut(a[2], F(kRegistry)) : 0;
    case 0x056b906e: return gWorld.persist ? HandOut(a[2], F(kPersist)) : 0;
    case 0xac6965bc: return gWorld.tracker ? HandOut(a[2], F(kTracker)) : 0;
    case 0x0bd09b0b: return gWorld.imageService ? HandOut(a[2], F(kImageService)) : 0;
    case 0x8af76dc8: return HandOut(a[2], M(mTexts));
    case 0x8bfc2f67: return HandOut(a[2], M(mCommitter));
    default: return 0;
    }
}
uint32_t QueryOptions(Obj *self, uint32_t *a)
{
    if (a[0] != 0xebabd35c || !gWorld.appPaths) return 0x200;
    return HandOut(a[1], *self);
}
// The source's slot 41 (width, height, iid, out): the picture handed out (or not).
uint32_t MakePicture(Obj *, uint32_t *a)
{
    if (gWorld.picture) HandOut(a[3], F(kPicture));
    return 0x4141;
}
// The key's slot 3 (buffer, entry out): the entry handed out, the buffer pointer returned.
uint32_t KeyOf(Obj *, uint32_t *a)
{
    auto *buffer = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) buffer[i] = 0x6b000000u + i;
    HandOut(a[1], F(kEntry));
    return a[0];
}
uint32_t KeyKnown(Obj *, uint32_t *a)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    return Bool();
}
uint32_t EntryPath(Obj *, uint32_t *a)
{
    reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString.assign(kPaths[Draw(3)]);
    return 0;
}
uint32_t RegistryHas(Obj *, uint32_t *a)
{
    LogString(a[0]);
    return Bool();
}
uint32_t LotSourceLot(Obj *, uint32_t *) { return gWorld.lot ? P(&F(kLot)) : 0; }
// The lot's slot 76 (set): some of the objects (and null) put in.
using ObjectSet = std::set<void *, std::less<void *>, GZServiceAllocator<void *>>;
uint32_t LotObjects(Obj *, uint32_t *a)
{
    auto *set = reinterpret_cast<ObjectSet *>(uintptr_t(a[0]));
    const uint32_t n = Draw(7);
    for (uint32_t i = 0; i < n; ++i) set->insert(Draw(8) == 0 ? nullptr : static_cast<void *>(&M(mObject0 + Draw(6))));
    return 0x7676;
}
uint32_t DefinitionOf(Obj *, uint32_t *) { return Draw(3) == 0 ? P(&F(kDefinition)) : 0; }
uint32_t IsBlocked(Obj *self, uint32_t *a) { return (self->value[0] >> (a[0] & 7) & 1) ? 0x10000u : 0u; }
// The COM framework's slot 3 GetClassObject(class, iid, out): the installer.
uint32_t GetClassObject(Obj *, uint32_t *a)
{
    if (a[0] != 0xad50ac22 || a[1] != 0xad50ac22 || !gWorld.installer) return 0x200;
    return HandOut(a[2], M(mInstaller));
}
uint32_t LogStringArg(Obj *, uint32_t *a)
{
    LogString(a[0]);
    return Bool();
}
uint32_t WriteText(Obj *, uint32_t *a)
{
    reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString.assign(kPaths[Draw(3)]);
    return 0;
}
// The installer's slot 7 (type name, path, key, name, extra): the texts and the key by content.
uint32_t InstallerAdd(Obj *, uint32_t *a)
{
    LogString(a[0]);
    LogString(a[1]);
    for (int i = 0; i < 4; ++i) Ev(reinterpret_cast<const uint32_t *>(uintptr_t(a[2]))[i]);
    LogString(a[3]);
    LogString(a[4]);
    return 0;
}
// A ServiceRefVector of counted strings, by content.
void LogStrings(uint32_t at)
{
    const auto *v = reinterpret_cast<const ServiceRefVector *>(uintptr_t(at));
    Ev(v->Size());
    Ev(uint32_t(v->mEnd - v->mFirst));
    for (void **p = v->mFirst; p != v->mLast; ++p) {
        const auto *s = static_cast<const cRZString *>(*p);
        Ev(s->mRefCount);
        LogText(s->mString);
    }
}
uint32_t CommitNames(Obj *, uint32_t *a)
{
    LogStrings(a[0]);
    return Bool();
}
uint32_t SubmitFiles(Obj *, uint32_t *a)
{
    LogStrings(a[1]);
    LogString(a[2]);
    return gRng();
}
// The resource manager's packages: slot 36 (index) a package or null; its slot 11 its file.
uint32_t PackageAt(Obj *, uint32_t *) { return Draw(4) ? P(&M(mPackage)) : 0; }
const char *const kFiles[] = {"C:\\Downloads\\a.package", "short", "C:\\Other\\b.package"};
uint32_t PackagePath(Obj *, uint32_t *a)
{
    reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString.assign(kFiles[Draw(3)]);
    return 0;
}
uint32_t __fastcall StandSameDirectory(void *, void *, void *, uint32_t flag)
{
    Ev(0x1c8151);
    Ev(flag);
    return Draw(3) ? 0x100u : 0u;
}
// cRZFileName's constructor and destructor (ours; the real one completes a relative path with GetCurrentDirectoryW): the
// path logged and kept whole, the other parts empty.
cRZFileName *__fastcall StandFileName(cRZFileName *self, void *, const cRZString *path)
{
    Ev(0x1c7ef4);
    LogText(path->mString);
    self->mDrive.Construct();
    self->mDir.Construct();
    self->mName.Construct();
    self->mExt.Construct();
    self->mFull.Construct(path->mString.c_str());
    return self;
}
void __fastcall StandFileNameDestruct(cRZFileName *self, void *)
{
    Ev(0x1c0eb2);
    self->mFull.Destruct();
    self->mExt.Destruct();
    self->mName.Destruct();
    self->mDir.Destruct();
    self->mDrive.Destruct();
}
uint32_t __cdecl StandRemoveFile(uint32_t path)
{
    Ev(0x1c2e25);
    LogString(path);
    return 1;
}

// Slot 26's searches. The persist system's slot 48 (word, entries): counted entries (the key's entry among them, or not) in
// an allocator-service block.
uint32_t OwnerEntries(Obj *, uint32_t *a)
{
    auto *entries = reinterpret_cast<ServiceRefVector *>(uintptr_t(a[1]));
    const uint32_t n = Draw(5);
    if (n) {
        auto **block = static_cast<void **>(GZServiceAllocate((n + Draw(2)) * 4));
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t k = Draw(5);
            Obj &e = k == 4 ? F(kEntry) : M(mFile0 + (k % 3));
            ++e.refs;
            block[i] = &e;
        }
        entries->mFirst = block;
        entries->mLast = block + n;
        entries->mEnd = block + n;
    }
    return 0x4848;
}
// The app's object list (a std::list of pointers): objects whose slots 25, 188 and 295 are past the fakes' 80, with their
// own table.
struct BigObject {
    void **vt;
    uint32_t id;
};
constexpr unsigned kBigSlots = 300;
void *gBigTable[kBigSlots];
BigObject gBig[3];
uint32_t __fastcall BigSlot188(BigObject *self, void *)
{
    Ev(0xb1880000u | self->id);
    return Bool();
}
uint32_t __fastcall BigSlot25(BigObject *self, void *, uint32_t flag)
{
    Ev(0xb0250000u | self->id);
    Ev(flag);
    return Draw(3) == 0 ? 0x100u : 0u;
}
uint32_t __fastcall BigSlot295(BigObject *self, void *, void *resource)
{
    Ev(0xb2950000u | self->id);
    Ev(svcfx::Norm(P(resource)));
    return Bool();
}
struct ListNode {
    ListNode *next;
    ListNode *prev;
    void *object;
};
ListNode gListHead;
ListNode gListNodes[4];
ListNode *gList[2]; // {head, size}
void BuildList(std::mt19937 &rng)
{
    for (unsigned i = 0; i < kBigSlots; ++i) gBigTable[i] = nullptr;
    gBigTable[25] = reinterpret_cast<void *>(&BigSlot25);
    gBigTable[188] = reinterpret_cast<void *>(&BigSlot188);
    gBigTable[295] = reinterpret_cast<void *>(&BigSlot295);
    for (unsigned i = 0; i < 3; ++i) gBig[i] = {gBigTable, i};
    ListNode *last = &gListHead;
    const unsigned n = rng() % 5;
    for (unsigned i = 0; i < n; ++i) {
        ListNode &node = gListNodes[i];
        const uint32_t k = rng() % 4;
        node.object = k == 3 ? nullptr : &gBig[k];
        node.prev = last;
        last->next = &node;
        last = &node;
    }
    last->next = &gListHead;
    gListHead.prev = last;
    gList[0] = &gListHead;
}

// A name: slot 6 the characters, slot 7 the length (sometimes short of the whole text).
char gNameText[64];
uint32_t gNameLength;

// ---- building the fakes ----

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 3);
    svcfx::ResetAll();
    for (unsigned i = 0; i < kMore; ++i) gMore[i].Reset(300 + i);
    gWorld.framework = rng() % 8 != 0;
    gWorld.app = rng() % 5 != 0;
    gWorld.appPaths = rng() % 3 != 0;
    gWorld.registry = rng() % 5 != 0;
    gWorld.persist = rng() % 6 != 0;
    gWorld.tracker = rng() % 5 != 0;
    gWorld.imageService = rng() % 5 != 0;
    gWorld.images = rng() % 5 != 0;
    gWorld.picture = rng() % 4 != 0;
    gWorld.lot = rng() % 6 != 0;
    for (bool &c : gWorld.cached) c = rng() % 3 == 0;

    Obj &service = F(kService);
    service.Set(5, "ddp", 0, &GetSystemService);
    service.Set(37, 0, gWorld.app ? &F(kOptions) : nullptr);
    F(kOptions).Set(0, "dp", 0, &QueryOptions);
    svcfx::UseAsMessageServer(M(mServer));
    M(mServer).Set(5, "pd", 0x55);
    M(mServer).Set(6, "pd", 0x66);
    F(kRegistry).Set(16, "p", 0, &RegistryHas);
    F(kRegistry).Set(22, "p", 0, [](Obj *, uint32_t *) { return Bool(); });
    F(kPersist).Set(3, 0, &F(kManager));
    F(kManager).Set(34, "p", 0, &KeyKnown);
    F(kTracker).Set(4, "p", 0x44);
    F(kImageService).Set(4, 0, gWorld.images ? &F(kImages) : nullptr);
    F(kImages).Set(6, "d", 0, [](Obj *, uint32_t *) { return Bool(); });
    F(kSource).Set(41, "dddp", 0, &MakePicture);
    F(kResource).Set(3, 0, &F(kKey));
    F(kKey).Set(3, "pp", 0, &KeyOf);
    F(kEntry).Set(11, "p", 0, &EntryPath);
    F(kMessage).Set(4, 0, 0u);
    F(kLotSource).Set(1, 0, 0, &LotSourceLot);
    F(kLot).Set(76, "p", 0, &LotObjects);
    Obj &app = F(kApp);
    app.Set(26, 0, &F(kBlocked));
    app.Set(24, 0, &F(kObjects));
    F(kObjects).Set(55, "d", 0x5555);
    F(kObjects).Set(21, "p", 0, &DefinitionOf);
    F(kBlocked).Set(42, "d", 0, &IsBlocked);
    F(kBlocked).value[0] = rng() % 3 ? (1u << (rng() % 8)) : 0u;
    F(kDefinition).Set(28, 0, 0x2807u);
    F(kKindYes).Set(3, "d", 0x7f000000u);
    F(kKindNo).Set(3, "d", 0u);
    for (unsigned i = 0; i < 6; ++i) {
        const uint32_t r = rng() % 4;
        M(mObject0 + i).Set(4, 0, r == 0 ? nullptr : r == 1 ? &F(kKindYes) : &F(kKindNo));
        M(mObject0 + i).Set(28, 0, 0x2800u + i);
    }
    F(kOut).Set(16, "p", 0x1616);
    F(kName).Set(6, 0, gNameText);
    F(kName).Set(7, 0, 0u);

    // The installer (the director framework's class object), the text and commit services, the packages, the files.
    gWorld.installer = rng() % 5 != 0;
    svcfx::gFrameworkIndex = kService;
    service.Set(3, "ddp", 0, &GetClassObject);
    Obj &installer = M(mInstaller);
    for (unsigned s : {3u, 12u}) installer.Set(s, 1, 0, [](Obj *, uint32_t *) { return Bool(); });
    installer.Ptr(3, 1), installer.Data(12, 1);
    installer.Set(5, "p", 0, &LogStringArg);
    installer.Set(11, "p", 0, &LogStringArg);
    installer.Set(7, "ppppp", 0, &InstallerAdd);
    M(mTexts).Set(19, "p", 0, &WriteText);
    M(mCommitter).Set(11, "pdd", 0, &CommitNames);
    M(mCommitter).Set(9, "dppd", 0, &SubmitFiles);
    M(mCurrent).Set(24, 0, 1u + rng() % 2);
    F(kManager).Set(35, 0, uint32_t(rng() % 4));
    F(kManager).Set(36, "d", 0, &PackageAt);
    M(mPackage).Set(11, "p", 0, &PackagePath);
    M(mPackage).Set(9, 0, 0u);
    M(mPackage).Set(5, "dd", 0, [](Obj *, uint32_t *) { return Bool(); });
    F(kPersist).Set(48, "dp", 0, &OwnerEntries);
    M(mOwner).Set(76, 0, 0x7676u);
    BuildList(rng);
    F(kBlocked).Set(31, 0, gList); // the app's slot 26 object (the blocked list of slot 23)
    gWorld.noBlocked = rng() % 6 == 0;
    if (gWorld.noBlocked) F(kApp).Set(26, 0, 0u);
    for (unsigned i = 0; i < 3; ++i) {
        M(mFile0 + i).Set(11, "p", 0, &EntryPath);
        M(mFile0 + i).Set(6, 0, kFiles[i]);
        M(mFile0 + i).Set(7, 0, uint32_t(std::strlen(kFiles[i])) - (rng() % 4 == 0 ? 1 : 0));
    }

    *GameAddress<void *>(svcfx::kAppCell) = &F(kApp);
    *GameAddress<void *>(0x10c9378) = gWorld.framework ? &F(kService) : nullptr;
    *GameAddress<void *>(0x10faf80) = gWorld.cached[0] && gWorld.registry ? &F(kRegistry) : nullptr;
    *GameAddress<void *>(svcfx::kPersistCell) = gWorld.cached[1] && gWorld.persist ? &F(kPersist) : nullptr;
    *GameAddress<void *>(0x10faf7c) = gWorld.cached[2] && gWorld.tracker ? &F(kTracker) : nullptr;
}

// The static name tables for a seed (the game's initialiser builds them; the harness doesn't run it).
void BuildNames(uint32_t seed)
{
    std::mt19937 rng(seed * 77 + 1);
    cRZString *categories = GameAddress<cRZString>(Sys::kCategoryNames);
    cRZString *types = GameAddress<cRZString>(Sys::kTypeNames);
    for (uint32_t i = 0; i < Sys::kCategoryCount; ++i) categories[i].mString.assign(kNames[rng() % kNameCount]);
    for (uint32_t i = 0; i < Sys::kTypeCount; ++i) types[i].mString.assign(kNames[rng() % kNameCount]);
}

// ---- driving a service ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
uint32_t CallWords(void *face, unsigned slot, unsigned words, const uint32_t *a)
{
    switch (words) {
    case 0: return Slot<uint32_t>(face, slot);
    case 1: return Slot<uint32_t>(face, slot, a[0]);
    case 2: return Slot<uint32_t>(face, slot, a[0], a[1]);
    case 3: return Slot<uint32_t>(face, slot, a[0], a[1], a[2]);
    case 4: return Slot<uint32_t>(face, slot, a[0], a[1], a[2], a[3]);
    default: return Slot<uint32_t>(face, slot, a[0], a[1], a[2], a[3], a[4]);
    }
}

// Built by the constructor (on 0xc5 memory) or the factory; the factory's result (the content face) is compared.
bool gFactory;
void *Build(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gFactory) {
        void *face = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0x3989c0)();
        void *object = static_cast<uint8_t *>(face) - 0x20;
        b.push_back(face ? 1 : 0);
        return object;
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(cTSCustomContentSupportAdapters::Construct(memory, nullptr))
                   : ctx.Original<void *(__thiscall *)(void *)>(0x38e0a0)(memory);
    b.push_back(r == memory);
    return memory;
}

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

void DumpString(Blob &b, const uint8_t *at)
{
    b.push_back(Word(at, 0) - P(GameAddress<void>(0)));
    const auto *s = reinterpret_cast<const std::string *>(at + 4);
    b.push_back(uint32_t(s->size()));
    b.push_back(uint32_t(s->capacity()));
    for (char c : *s) b.push_back(uint8_t(c));
    b.push_back(Word(at, 0x1c));
}
// A vector of 0x20-byte strings: count, capacity, each string.
void DumpStrings(Blob &b, const uint8_t *at)
{
    const uint32_t first = Word(at, 0), last = Word(at, 4), end = Word(at, 8);
    b.push_back(first != 0);
    b.push_back((last - first) / 0x20);
    b.push_back((end - first) / 0x20);
    for (uint32_t p = first; p != last; p += 0x20) DumpString(b, reinterpret_cast<const uint8_t *>(uintptr_t(p)));
}
void DumpMap(Blob &b, const uint8_t *at)
{
    const auto &map = *reinterpret_cast<const ContentNameMap *>(at);
    b.push_back(uint32_t(map.size()));
    for (const auto &entry : map) {
        DumpString(b, reinterpret_cast<const uint8_t *>(&entry.first));
        b.push_back(entry.second);
    }
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    static const uint32_t kFaces[4][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kMessageVTable}, {0x20, Sys::kContentVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    // +0x19..+0x1b (after the running flag) are never written: the 0xc5 fill, or heap leftovers from the factory.
    for (uint32_t off = 8; off < 0x1c; off += 4) b.push_back(off == 0x18 && gFactory ? m[0x18] : Word(m, off));
    DumpString(b, m + 0x24);
    b.push_back(svcfx::Norm(Word(m, 0x44)));
    b.push_back(svcfx::Norm(Word(m, 0x48)));
    DumpStrings(b, m + 0x4c);
    DumpString(b, m + 0x58);
    DumpStrings(b, m + 0x78);
    DumpString(b, m + 0x84);
    b.push_back(Word(m, 0xa4));
    b.push_back(svcfx::Norm(Word(m, 0xa8)));
    b.push_back(Word(m, 0xac) != 0);
    b.push_back((Word(m, 0xb0) - Word(m, 0xac)) / 0xac);
    b.push_back((Word(m, 0xb4) - Word(m, 0xac)) / 0xac);
    DumpMap(b, m + 0xb8);
    DumpMap(b, m + 0xc0);
    b.push_back(m[0xc8]);
    if (!gFactory) b.push_back(Word(m, 0xc8) >> 8); // never written: the 0xc5 fill
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 5);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 20;
    if (rng() % 4) ops.push_back({0, 0, 0});
    for (unsigned i = 0; i < n; ++i) ops.push_back({1 + unsigned(rng() % 18), uint32_t(rng()), uint32_t(rng())});
    return ops;
}

static const uint32_t kIids[] = {1, Sys::kIid, 0x452294aa, 0x287fb697, 0xac59cfaf, 0x12345678};
static const uint32_t kMessageTypes[] = {0xc5, 0xc5, 0xc4, 0xc6, 0, 0x12345678};
static const uint32_t kIndexes[] = {0, 1, 2, 11, 12, 13, 14, 28, 29, 30, 31, 0x7fffffff, 0x80000000, 0xffffffff};

// The state-code helper (on the primary face): the original on the original's side, the hook's adapter on ours.
uint32_t(__fastcall *gStateCode)(void *self, void *edx);

// The records' block as a range (popped after the call): the installer (slot 5) gets their keys and names.
void AddRecordsRange(const uint8_t *m)
{
    const auto *records = reinterpret_cast<const ContentRecords *>(m + 0xac);
    svcfx::AddRange(records->data(), records->capacity() * sizeof(ContentRecord), 0x7a000000);
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const message = Face(object, 0x1c);
    void *const content = Face(object, 0x20);
    auto *const m = static_cast<uint8_t *>(object);
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: b.push_back(Slot<uint32_t>(primary, 8) & 0xff); break;  // Init
    case 1: b.push_back(Slot<uint32_t>(primary, 9) & 0xff); break;  // Shutdown
    case 2: { // DoMessage (the original doesn't check the message for null)
        F(kMessage).ret[4] = kMessageTypes[a % 6];
        b.push_back(Slot<uint32_t>(message, 3, static_cast<void *>(&F(kMessage))) & 0xff);
        break;
    }
    case 3: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, Face(object, 4), message, content};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, kIids[(a >> 4) % 6], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 4], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 4: { // the service accessors, OnTick / OnIdle
        b.push_back(Slot<uint32_t>(primary, 3));
        b.push_back(Slot<uint32_t>(primary, 5));
        b.push_back(Slot<uint32_t>(primary, 6) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 12));
        Slot<uint32_t>(primary, 7, a % 2);
        b.push_back(Slot<uint32_t>(primary, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        break;
    }
    case 5: { // SaveSnapshot (the path set or cleared first), then a source or null
        auto *path = reinterpret_cast<cRZString *>(m + 0x84);
        if (a % 3 == 0) path->mString.clear();
        else path->mString.assign(kPaths[(a >> 2) % 3]);
        Slot<uint32_t>(primary, 13, a % 7 == 0 ? nullptr : static_cast<void *>(&F(kSource)));
        break;
    }
    case 6: // InstallFile (slot 15 uses the persist system unchecked: 0x391082; slot 5 the text service: 0x39e92f)
        if (!PersistUsable() || !gWorld.framework) break;
        AddRecordsRange(m);
        b.push_back(Slot<uint32_t>(content, 3, static_cast<void *>(&F(kResource)), static_cast<void *>(&F(kLot))) & 0xff);
        svcfx::gRanges.pop_back();
        break;
    case 7: { // WrittenFilesSize (files and +0x58 set first)
        if (a % 2) reinterpret_cast<ContentServiceStrings *>(m + 0x4c)->push_back(ContentString(kPaths[(a >> 1) % 3]));
        auto *text = reinterpret_cast<cRZString *>(m + 0x58);
        if ((a >> 3) % 3 == 0) text->mString.clear();
        else text->mString.assign(kPaths[(a >> 5) % 3]);
        b.push_back(Slot<uint32_t>(content, 17));
        break;
    }
    case 8: { // the names: to a fake or to a real string
        static cRZString out;
        static bool built = false;
        if (!built) out.Construct(), built = true;
        const uint32_t index = kIndexes[a % 14];
        void *target = c % 2 ? static_cast<void *>(&F(kOut)) : static_cast<void *>(&out);
        Slot<uint32_t>(content, (a >> 4) % 2 ? 18 : 19, index, target);
        if (target == &out) LogText(out.mString);
        break;
    }
    case 9: { // FindCategory / FindType
        const char *name = kNames[a % kNameCount];
        std::memcpy(gNameText, name, std::strlen(name) + 1);
        if ((a >> 5) % 4 == 0 && gNameText[0]) gNameText[0] ^= 0x20; // another case
        gNameLength = uint32_t(std::strlen(gNameText));
        if ((a >> 7) % 5 == 0 && gNameLength) --gNameLength;       // the length short of the text
        F(kName).ret[7] = gNameLength;
        uint32_t found = 0xdddddddd;
        Slot<uint32_t>(content, (a >> 9) % 2 ? 20 : 21, static_cast<void *>(&F(kName)), &found);
        b.push_back(found);
        break;
    }
    case 10: // the blocked list is used unchecked (0x390ce4)
        if (gWorld.noBlocked) break;
        b.push_back(Slot<uint32_t>(content, 23, static_cast<void *>(&F(kLotSource))) & 0xff); break;
    case 11: // IsRegistered: with a resource, only while there is a persist system (used unchecked: 0x39e73e)
        if (a % 6 && !PersistUsable()) break;
        b.push_back(Slot<uint32_t>(content, 25, a % 6 ? static_cast<void *>(&F(kResource)) : nullptr) & 0xff); break;
    case 12: b.push_back(Slot<uint32_t>(content, 27, a % 6 ? static_cast<void *>(&F(kResource)) : nullptr) & 0xff); break;
    case 13: { // a slot still game code, or Reset
        const unsigned which = a % (sizeof(kGameSlots) / sizeof(kGameSlots[0]) + 1);
        if (which == sizeof(kGameSlots) / sizeof(kGameSlots[0])) {
            Slot<uint32_t>(content, 6); // void
            break;
        }
        // Small words (never inside a registered range). Slot 13 takes a files vector: InstallFile (6) gives it one.
        const GameSlot &g = kGameSlots[which];
        if (g.slot == 13) break;
        uint32_t args[5];
        for (uint32_t &w : args) w = gRng() & 0xffff;
        if (g.slot == 28) args[1] = P(Face(object, 0x84));
        b.push_back(CallWords(content, g.slot, g.words, args));
        break;
    }
    case 14: { // state: a record, a package file name, +0x58, the state word, the current object
        auto *records = reinterpret_cast<ContentRecords *>(m + 0xac);
        switch (a % 5) {
        case 0:
        case 1: {
            ContentRecord &r = records->emplace_back();
            r.mType = c % 30;
            for (uint32_t i = 0; i < 4; ++i) r.mKey[i] = c * (i + 3) + a;
            r.mName.s.mString.assign(kNames[(a >> 4) % kNameCount]);
            if ((a >> 9) % 4) r.mResource.Assign(static_cast<cIGZUnknown *>(static_cast<void *>(&F(kEntry))));
            break;
        }
        case 2:
            reinterpret_cast<ContentStrings *>(m + 0x78)->push_back(ContentString(kNames[(a >> 4) % kNameCount]));
            break;
        case 3: {
            auto *text = reinterpret_cast<cRZString *>(m + 0x58);
            if ((a >> 4) % 3 == 0) text->mString.clear();
            else text->mString.assign(kPaths[(a >> 6) % 3]);
            break;
        }
        default:
            *reinterpret_cast<uint32_t *>(m + 0xa4) = (a >> 4) % 3;
            *reinterpret_cast<void **>(m + 0xa8) = (a >> 6) % 2 ? static_cast<void *>(&M(mCurrent)) : nullptr;
            break;
        }
        b.push_back(gStateCode(object, nullptr)); // the original 0x39cb80, or the hook's adapter
        break;
    }
    case 15: { // slots 5 (Install), 7 (Commit), 9 (SubmitWrittenFiles), 10 (Cleanup), 15 (ClosePackages)
        AddRecordsRange(m);
        switch (a % 6) {
        case 0: // the text service is used unchecked (0x39e92f)
            if (gWorld.framework) b.push_back(Slot<uint32_t>(content, 5, static_cast<void *>(&F(kLot))) & 0xff);
            break;
        case 1: // the commit service is used unchecked (0x39cf1e)
            if (gWorld.framework) b.push_back(Slot<uint32_t>(content, 7) & 0xff);
            break;
        case 2: // the same, past the early return (0x39fcb3)
            if (gWorld.framework) b.push_back(Slot<uint32_t>(content, 9, c, a >> 8));
            break;
        case 3: Slot<uint32_t>(content, 10); break; // void
        default: { // a files vector over the allocator service: files or null
            if (!PersistUsable()) break;
            ServiceRefVector files{};
            const uint32_t n = (a >> 4) % 4;
            if (n) {
                auto **block = static_cast<void **>(GZServiceAllocate((n + (a >> 6) % 2) * 4));
                for (uint32_t i = 0; i < n; ++i) {
                    const uint32_t k = (c >> (2 * i)) % 4;
                    block[i] = k == 3 ? nullptr : static_cast<void *>(&M(mFile0 + k));
                    if (block[i]) ++M(mFile0 + k).refs;
                }
                files.mFirst = block;
                files.mLast = block + n;
                files.mEnd = block + n + (a >> 6) % 2;
            }
            Slot<uint32_t>(content, 15, static_cast<void *>(&files));
            b.push_back(files.Size());
            files.ReleaseAndFree();
            break;
        }
        }
        svcfx::gRanges.pop_back();
        break;
    }
    case 16: { // ResourceInUse: the persist system (IsRegistered's) and the kept one (+0x48) are used unchecked (0x39e73e, 0x390df2)
        void *owner = a % 5 ? static_cast<void *>(&M(mOwner)) : nullptr;
        void *resource = (a >> 3) % 5 ? static_cast<void *>(&F(kResource)) : nullptr;
        if (owner && resource && !(PersistUsable() && Word(m, 0x48))) break;
        uint8_t answers[2] = {0xcc, 0xcc};
        Slot<uint32_t>(content, 26, owner, resource, &answers[0], &answers[1]);
        b.push_back(answers[0]);
        b.push_back(answers[1]);
        break;
    }
    default: { // the written files as a game vector would have them, then the size
        auto *files = reinterpret_cast<ContentServiceStrings *>(m + 0x4c);
        const uint32_t n = a % 4;
        for (uint32_t i = 0; i < n; ++i) files->push_back(ContentString(kPaths[(c >> i) % 3]));
        b.push_back(Slot<uint32_t>(content, 17));
        break;
    }
    }
}

} // namespace ccs_test

using namespace ccs_test;

DIFF_TEST(CustomContent_Run, 0x38e0a0)
{
    // Also reached (see the file comment): 0x3989c0 (factory), 0x38f420 (destructor), 0x38f970 (deleting destructor),
    // 0x38f95b (its +4 thunk), 0x39f030 (QueryInterface) and its thunks 0x39f061 / 0x39f069 / 0x39f071, 0x39cbb0 (Init),
    // 0x39fac0 (Shutdown), 0x398210 (DoMessage), 0x39a1d0 (SaveSnapshot), 0x39eaa0, 0x39cb10, 0x39bed0, 0x39bf00, 0x39b0f0,
    // 0x39c470, 0x390be0, 0x39e6e0, 0x3975d0, 0x39e820, 0x39cd10, 0x39faf0, 0x39fd30, 0x391040, 0x39cb80, 0x390d80, the folded slot
    // 6 0x1ae2f0 and the AddRef / Release thunks 0x1cc335 / 0x1cd7b5.
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    for (uint32_t cell : {svcfx::kAppCell, 0x10c9378u, svcfx::kPersistCell, 0x10faf80u, 0x10faf7cu}) globals.SetWord(cell, 0);

    // The static name tables: built here, destroyed and restored at the end.
    constexpr size_t kCategoryBytes = Sys::kCategoryCount * 0x20, kTypeBytes = Sys::kTypeCount * 0x20;
    std::vector<uint8_t> savedCategories(kCategoryBytes), savedTypes(kTypeBytes);
    std::memcpy(savedCategories.data(), GameAddress<void>(Sys::kCategoryNames), kCategoryBytes);
    std::memcpy(savedTypes.data(), GameAddress<void>(Sys::kTypeNames), kTypeBytes);
    for (uint32_t i = 0; i < Sys::kCategoryCount; ++i) GameAddress<cRZString>(Sys::kCategoryNames)[i].Construct();
    for (uint32_t i = 0; i < Sys::kTypeCount; ++i) GameAddress<cRZString>(Sys::kTypeNames)[i].Construct();

    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::StandIn server(0x9c5a89, &GetCachedMessageServer, &StandServer);
    shape::StandIn ensure(0x1c5031, &folders::EnsureDirectory, &StandEnsureDirectory); // ours too (lane LY)
    shape::StandIn remove(0x1c2e25, &PathDelete, &StandRemoveFile);
    shape::CodePatch sameDirectory(0x1c8151, shape::CodeOf(&StandSameDirectory));
    shape::StandIn fileName(0x1c7ef4, &cRZFileName::Construct, &StandFileName);
    shape::StandIn fileNameDestruct(0x1c0eb2, &cRZFileName::Destruct, &StandFileNameDestruct);
    shape::StandIn common(0x1d0c00, &appinit::CommonImplSlot4, &StandCommonSlot4);
    // The slots still game code are where the game's content vtable has them (a slot-order mistake among the forwarders
    // would otherwise reach another recorder only in game).
    for (const GameSlot &g : kGameSlots) {
        ++ctx.cases;
        if (GameAddress<void *>(Sys::kContentVTable)[g.slot] != GameAddress<void>(g.rva))
            ctx.Fail("game content slot %u is not %x", g.slot, g.rva);
    }
    shape::StandIn exists(0x1c1242, &cRZFile::PathExists, &StandPathExists);
    shape::StandIn length(0x1c1b8b, &cRZFile::LengthOfPath, &StandLengthOfPath);
    shape::CodePatch r4(0x39eb80, RecordFor<0x39eb80, 3>());
    shape::CodePatch r8(0x39f680, RecordFor<0x39f680, 1>());
    shape::CodePatch r11(0x39cf70, RecordFor<0x39cf70, 2>());
    shape::CodePatch r12(0x39d400, RecordFor<0x39d400, 0>());
    shape::CodePatch r13(0x391c20, RecordFor<0x391c20, 3>());
    shape::CodePatch r14(0x3923f0, RecordFor<0x3923f0, 5>());
    shape::CodePatch r16(0x39a2f0, RecordFor<0x39a2f0, 0>());
    shape::CodePatch r22(0x390400, RecordFor<0x390400, 1>());
    shape::CodePatch r24(0x397730, RecordFor<0x397730, 2>());
    shape::CodePatch r28(0x396070, RecordFor<0x396070, 2>());
    shape::CodePatch r29(0x398240, RecordFor<0x398240, 2>());

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 4 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            BuildNames(seed);
            using StateFn = uint32_t(__fastcall *)(void *, void *);
            gStateCode = side == 1 ? reinterpret_cast<StateFn>(&cTSCustomContentSupportAdapters::StateCode)
                                   : ctx.Original<StateFn>(0x39cb80);
            svcfx::AddRange(GameAddress<void>(Sys::kCategoryNames), kCategoryBytes, kCategoryTag);
            svcfx::AddRange(GameAddress<void>(Sys::kTypeNames), kTypeBytes, kTypeTag);
            Blob first;
            void *object = Build(ctx, first, side == 1);
            svcfx::AddRange(object, Sys::kSize, kTag);
            Dump(first, object, side == 1);
            first.insert(first.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            results[side].push_back(std::move(first));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                for (const Obj &o : gMore) b.push_back(uint32_t(o.refs));
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "CustomContent_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                const size_t op = i ? i - 1 : 0;
                ctx.Fail("  at step %u of %u (kind %u)", unsigned(i), unsigned(ops.size() + 2),
                         i && op < ops.size() ? ops[op].kind : 99u);
                seed = 3001;
                break;
            }
        }
    }

    for (uint32_t i = 0; i < Sys::kCategoryCount; ++i) GameAddress<cRZString>(Sys::kCategoryNames)[i].Destruct();
    for (uint32_t i = 0; i < Sys::kTypeCount; ++i) GameAddress<cRZString>(Sys::kTypeNames)[i].Destruct();
    std::memcpy(GameAddress<void>(Sys::kCategoryNames), savedCategories.data(), kCategoryBytes);
    std::memcpy(GameAddress<void>(Sys::kTypeNames), savedTypes.data(), kTypeBytes);
}
